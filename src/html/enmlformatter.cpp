/*********************************************************************************
NixNote - An open-source client for the Evernote service.
Copyright (C) 2013 Randy Baumgarte

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
***********************************************************************************/


#include <QFileIconProvider>
#include <QWebPage>
#include <QWebFrame>
#include <QIcon>
#include <QMessageBox>
#include <tidy.h>
#include <tidybuffio.h>
#include <iostream>

#include "enmlformatter.h"
#include "src/utilities/encrypt.h"
#include "src/logger/qslog.h"

#define ENML_MODULE_LOGPREFIX "enml-cleanup: "

using namespace std;

/* Constructor. */
EnmlFormatter::EnmlFormatter(
        QString html,
        bool guiAvailable,
        QHash<QString, QPair<QString, QString> > passwordSafe,
        QString cryptoJarPath
) : QObject(nullptr) {
    this->guiAvailable = guiAvailable;
    this->passwordSafe = passwordSafe;
    this->cryptoJarPath = cryptoJarPath;

    setContent(html);

    // initial state without error
    formattingError = false;

    coreattrs.append("style");
    coreattrs.append("title");

    i18n.append("lang");
    i18n.append("xml:lang");
    i18n.append("dir");

    focus.append("accesskey");
    focus.append("tabindex");

    attrs.append(coreattrs);
    attrs.append(i18n);

    textAlign.append("align");

    cellHalign.append("align");
    cellHalign.append("char");
    cellHalign.append("charoff");

    cellValign.append("valign");

    a.append("charset");
    a.append("type");
    a.append("name");
    a.append("href");
    a.append("hreflang");
    a.append("rel");
    a.append("rev");
    a.append("shape");
    a.append("coords");
    a.append("target");
    // nixnote internal
    a.append("hash");
    a.append("en-tag");
    a.append("lid");

    area.append("shape");
    area.append("coords");
    area.append("href");
    area.append("nohref");
    area.append("alt");
    area.append("target");

    bdo.append("lang");
    bdo.append("xml:lang");
    bdo.append("dir");

    blockQuote.append("cite");

    br.append("clear");

    caption.append("align");

    col.append("span");
    col.append("width");

    colGroup.append("span");
    colGroup.append("width");

    del.append("cite");
    del.append("datetime");

    dl.append("compact");

    font.append("size");
    font.append("color");
    font.append("face");

    hr.append("align");
    hr.append("noshade");
    hr.append("size");
    hr.append("width");

    input.append("checked");
    input.append("type");

    img.append("src");
    img.append("alt");
    img.append("name");
    img.append("longdesc");
    img.append("height");
    img.append("width");
    img.append("usemap");
    img.append("ismap");
    img.append("align");
    img.append("border");
    img.append("hspace");
    img.append("vspace");
    // nixnote internal
    img.append("hash");
    img.append("lid");
    img.append("en-tag");
    img.append("type");

    ins.append("cite");
    ins.append("datetime");

    li.append("type");
    li.append("value");

    map.append("title");
    map.append("name");

    object.append("type");

    ol.append("type");
    ol.append("compact");
    ol.append("start");

    pre.append("width");
    pre.append("xml:space");

    table.append("summary");
    table.append("width");
    table.append("border");
    table.append("cellspacing");
    table.append("cellpadding");
    table.append("align");
    table.append("bgcolor");

    td.append("abbr");
    td.append("rowspan");
    td.append("colspan");
    td.append("nowrap");
    td.append("bgcolor");
    td.append("width");
    td.append("height");

    th.append("abbr");
    th.append("rowspan");
    th.append("colspan");
    th.append("nowrap");
    th.append("bgcolor");
    th.append("width");
    th.append("height");

    tr_.append("bgcolor");

    ul.append("type");
    ul.append("compact");
}

/**
 * Return the formatted content as unicode string
 */
QString EnmlFormatter::getContent() const {
    return QString::fromUtf8(content);
}

/**
 * Return the formatted content as byte array (in utf8).
 */
QByteArray EnmlFormatter::getContentBytes() const {
    return content;
}


/**
 * Tidy current content.
 * Will set formattingError to true on error.
 * Else content is input and also output.
 *
 * Adapted from example at http://www.html-tidy.org/developer/
 */
void EnmlFormatter::tidyHtml(HtmlCleanupMode mode) {
    QLOG_DEBUG_FILE("fmt-pre-tidy.html", getContent());

    TidyBuffer output = {nullptr};
    TidyBuffer errbuf = {nullptr};
    int rc = -1;

    // Initialize "document"
    TidyDoc tdoc = tidyCreate();

    // Convert to XHTML
    Bool ok = tidyOptSetBool(tdoc, TidyXhtmlOut, yes);
    if (ok) {
        // Treat input as XML: no
        rc = tidyOptSetBool(tdoc, TidyXmlTags, no);
    }

    if (mode == HtmlCleanupMode::Simplify) {
        if (ok) {
            // Make bare HTML: remove Microsoft cruft
            rc = tidyOptSetBool(tdoc, TidyMakeBare, yes);
        }
        if (ok) {
            // Discard proprietary attributes ## warn may discard images
            rc = tidyOptSetBool(tdoc, TidyDropPropAttrs, yes);
        }
        if (ok) {
            // Clean up HTML exported from Google Docs
            rc = tidyOptSetBool(tdoc, TidyGDocClean, yes);
        }
        if (ok) {
            // Replace presentational clutter by style rules
            rc = tidyOptSetBool(tdoc, TidyMakeClean, yes);
        }
        if (ok) {
            // Ensure tags and attributes match output HTML version
            rc = tidyOptSetBool(tdoc, TidyStrictTagsAttr, yes);
        }
    }


    if (ok) {
        rc = tidySetCharEncoding(tdoc, "utf8");
    }
    if (ok) {
        // Capture diagnostics
        rc = tidySetErrorBuffer(tdoc, &errbuf);
    }
    if (rc >= 0) {
        // Parse the input
        rc = tidyParseString(tdoc, content.constData());
    }
    if (rc >= 0) {
        // Tidy it up!
        rc = tidyCleanAndRepair(tdoc);
    }
    if (rc >= 0) {
        // Kvetch
        rc = tidyRunDiagnostics(tdoc);
    }
    // if error, force output.
    if (rc > 1) {
        rc = (tidyOptSetBool(tdoc, TidyForceOutput, yes) ? rc : -1);
    }

    if (rc >= 0) {
        // pretty print
        rc = tidySaveBuffer(tdoc, &output);
    }

    // delete content in both cases
    content.clear();

    if (rc >= 0) {
        if (rc > 0) {
            QString tidyErrors((char *) errbuf.bp);
            tidyErrors.replace("\n", "; ");
            QLOG_INFO() << ENML_MODULE_LOGPREFIX "tidy DONE: diagnostics: " << tidyErrors;
        }
        // results
        content.append((char *) output.bp);

        //content = content.replace("</body>", "<br/>tidy ok</body>");
    } else {
        formattingError = true;
        QLOG_ERROR() << ENML_MODULE_LOGPREFIX "tidy FAILED: severe error occurred, code=" << rc;
    }
    QLOG_DEBUG_FILE("fmt-post-tidy.html", getContent());

    tidyBufFree(&output);
    tidyBufFree(&errbuf);
    tidyRelease(tdoc);
}

/**
 * Remove html header.
 * Called before tidy.
 */
void EnmlFormatter::removeHtmlHeader() {
    // remove all before body
    qint32 index = content.indexOf("<body");
    content.remove(0, index);
    // remove all after body
    index = content.indexOf("</body");
    content.truncate(index);
    content.append("</body>");
}


/**
 * Take the WebKit HTML and transform it into ENML
 * */
void EnmlFormatter::rebuildNoteEnml() {
    QLOG_INFO() << ENML_MODULE_LOGPREFIX "===== rebuilding note ENML";
    QLOG_DEBUG_FILE("fmt-html-input.html", getContent());

    // list of referenced LIDs
    resources.clear();


    tidyHtml(HtmlCleanupMode::Tidy);
    if (isFormattingError()) {
        QLOG_ERROR() << ENML_MODULE_LOGPREFIX "got no output from tidy - cleanup failed";
        return;
    }
    removeHtmlHeader();

    content.prepend(DEFAULT_HTML_HEAD);
    content.prepend(DEFAULT_HTML_TYPE);

    // Tidy puts this in place, but we don't really need it.
    content.replace("<form>", "");
    content.replace("</form>", "");

    content = fixEncryptionTags(content);

    QLOG_DEBUG_FILE("fmt-pre-dt-check.html", getContent());
    if (guiAvailable) {
        QWebPage page;
        QEventLoop loop;
        page.mainFrame()->setContent(getContentBytes());
        QObject::connect(&page, SIGNAL(loadFinished(bool)), &loop, SLOT(quit()));
        loop.exit();

        QWebElement elementRoot = page.mainFrame()->documentElement();
        QStringList tags = findAllTags(elementRoot);

        for (int i = 0; i < tags.size(); i++) {
            QString tag = tags[i];
            QLOG_DEBUG() << ENML_MODULE_LOGPREFIX "== processing tag " << tag;

            QWebElementCollection anchors = page.mainFrame()->findAllElements(tag);
                foreach(QWebElement
                            element, anchors) {
                    QString tagname = element.tagName().toLower();

                    if (!checkEndFixElement(element)) {
                        // was logged already
                        element.removeFromDocument();
                        continue;
                    }

                    if (tagname == "input") {
                        fixInputNode(element);
                    } else if (tagname == "a") {
                        fixANode(element);
                    } else if (tagname == "object") {
                        fixObjectNode(element); // TODO
                    } else if (tagname == "img") {
                        fixImgNode(element);
                    }
                }
        }
        QString outerXml = elementRoot.toOuterXml();
        setContent(outerXml);
    }
    QLOG_DEBUG_FILE("fmt-post-dt-check.html", getContent());

    // TEMP hack - rerun tidy - to fix XML after manual fixup
    tidyHtml(HtmlCleanupMode::Tidy);


    /// TEMPORARY POST TIDY HACK - this is how it shouldn't be done
    /// TEMPORARY POST TIDY HACK
    content.replace(HTML_COMMENT_START, "");
    content.replace(HTML_COMMENT_END, "");

    /// TEMPORARY POST TIDY HACK
    /// TEMPORARY POST TIDY HACK


    // Add EN xml header
    {
        // because tidy will add one
        removeHtmlHeader();

        QByteArray b;
        b.clear();
        b.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        b.append("<!DOCTYPE en-note SYSTEM 'http://xml.evernote.com/pub/enml2.dtd'>");
        b.append(content);
        content.clear();

        content = b.replace("<body", "<en-note");
        content = b.replace("</body>", "</en-note>");
    }


    QLOG_DEBUG_FILE("fmt-enml-final.xml", getContent());
    QLOG_INFO() << ENML_MODULE_LOGPREFIX "===== finished rebuilding note ENML";
}


QStringList EnmlFormatter::findAllTags(QWebElement &element) {
    QStringList tags;
    QString body = element.toOuterXml();
    body = body.replace("</", "<").replace("/>", " ").replace(">", " ");
    int i = body.indexOf("<body") + 1;
    // Look through and get a list of all possible tags
    i = body.indexOf("<", i);

    while (i != -1) {
        int eot = body.indexOf(" ", i);
        QString tag = body.mid(i + 1, eot - i - 1);
        if (!tags.contains(tag) && tag != "body") {
            tags.append(tag);
        }
        i = body.indexOf("<", eot);
        if (tag == "body") {
            i = -1;
        }
    }
    QLOG_DEBUG() << ENML_MODULE_LOGPREFIX "find all tags: " << tags;
    return tags;
}


void EnmlFormatter::fixInputNode(QWebElement &e) {
    QString type = e.attribute("type", "");
    if (type != "checkbox") {
        QLOG_WARN() << ENML_MODULE_LOGPREFIX "fixed unknown <input> node by removing it";
        e.removeFromDocument();
        return;
    }

    bool checked = false;
    if (e.hasAttribute("checked")) {
        checked = true;
    }

    removeInvalidAttributes(e);
    // those 2 are additionaly needed (as they pass the basic checks)
    e.removeAttribute("style");
    e.removeAttribute("type");

    if (checked) {
        e.setAttribute("checked", "true");
    }

    // quite a hack
    QRegularExpression reInput("<input([^>]*)>");
    QString markup = e.toOuterXml();
    markup = markup.replace(reInput, HTML_COMMENT_START "<en-todo\\1/>" HTML_COMMENT_END);
    e.setOuterXml(markup);
}


void EnmlFormatter::fixObjectNode(QWebElement &e) {
    QString type = e.attribute("type", "");
    if (type == "application/pdf") {
        qint32 lid = e.attribute("lid", "0").toInt();
        removeInvalidAttributes(e);

        // e.removeAttribute("width");
        // e.removeAttribute("height");
        // e.removeAttribute("lid");
        // e.removeAttribute("border");

        if (lid > 0) {
            resources.append(lid);
            const QString xml = e.toOuterXml()
                    // temp hack for tidy call
                    .replace("<object", HTML_COMMENT_START "<en-media")
                    .replace("</object>", "</en-media>" HTML_COMMENT_END);
            QLOG_DEBUG() << ENML_MODULE_LOGPREFIX "fixed object node holding pdf to " << xml;
            e.setOuterXml(xml);
        }
    } else {
        QLOG_WARN() << ENML_MODULE_LOGPREFIX "fixed unknown <object> node by removing it";
        e.removeFromDocument();
    }
}


void EnmlFormatter::fixImgNode(QWebElement &e) {
    QString enType = e.attribute("en-tag", "").toLower();

    // Check if we have an en-crypt tag.  Change it from an img to en-crypt
    if (enType == "en-crypt") {
        QString encrypted = e.attribute("alt");
        QString cipher = e.attribute("cipher", "RC2");
        QString hint = e.attribute("hint", "");
        QString length = e.attribute("length", "64");

        e.removeAttribute("onmouseover");
        e.removeAttribute("name");
        e.removeAttribute("alt");
        e.removeAttribute("en-tag");
        e.removeAttribute("contenteditable");
        e.removeAttribute("style");
        removeInvalidAttributes(e);

        e.setInnerXml(encrypted);
        const QString xml = HTML_COMMENT_START "en-crypt cipher=\"" + cipher + "\" length=\"" +
                            length + "\" hint=\"" + hint
                            + "\">" + encrypted + "</en-crypt" HTML_COMMENT_END;
        e.setOuterXml(xml);
        QLOG_DEBUG() << ENML_MODULE_LOGPREFIX "processing tag 'img', type=en-crypt' - fixed img node to " << xml;
    } else if (enType == "temporary") { ;
        // Temporary image.  If so, remove it
        e.removeFromDocument();
        QLOG_DEBUG() << ENML_MODULE_LOGPREFIX "processing tag 'img', type=temporary' - fixed temporary img node by deleting it";
    } else {
        // If we've gotten this far, we have an en-media tag

        e.removeAttribute("en-tag");
        int lid = e.attribute("lid").toInt();
        QString type = e.attribute("type");
        QString hash = e.attribute("hash");
        QLOG_DEBUG() << "Processing tag 'img', type=" << type << ", hash=" << hash;
        if ((lid <= 0) || (hash.isEmpty())) {
            QLOG_WARN() << ENML_MODULE_LOGPREFIX "deleting invalid 'img' tag";
            e.removeFromDocument();
            return;
        }

        resources.append(lid);
        removeInvalidAttributes(e);
        // temp hack for tidy call
        const QString xml = e.toOuterXml().replace("<img", HTML_COMMENT_START "<en-media").append("</en-media>" HTML_COMMENT_END);
        e.setOuterXml(xml);
        QLOG_DEBUG() << ENML_MODULE_LOGPREFIX "fixed img node to: " << xml;
    }
}


void EnmlFormatter::fixANode(QWebElement e) {
    QString enTag = e.attribute("en-tag", "").toLower();
    QString lid = e.attribute("lid");
    removeInvalidAttributes(e);
    if (enTag == "en-media") {
        resources.append(lid.toInt());
        e.removeAttribute("style");
        e.removeAttribute("href");
        e.removeAttribute("title");

        e.removeAllChildren();
        QString xml = e.toOuterXml();
        xml.replace("<a", HTML_COMMENT_START "<en-media");
        xml.replace("</a>", "</en-media>" HTML_COMMENT_END);
        QLOG_DEBUG() << ENML_MODULE_LOGPREFIX "fixed link node to " << xml;
        e.setOuterXml(xml);
    }

    QString latex = e.attribute("href", "");
    if (latex.toLower().startsWith("latex:///")) {
        //removeInvalidAttributes(e);
        QString formula = e.attribute("title");
        const QString attr = QString("http://latex.codecogs.com/gif.latex?%1").arg(formula);
        QLOG_DEBUG() << ENML_MODULE_LOGPREFIX "fixed latex attr to " << attr;
        e.setAttribute("href", attr);
    }

    //removeInvalidAttributes(e);
    //checkAttributes(e, attrs + focus + a);
}

// https://dev.evernote.com/doc/articles/enml.php#prohibited
// A number of attributes are also disallowed from the supported XHTML elements:
//   id
//   class
//   on*
//   accesskey
//   data
//   dynsrc
//   tabindex
//
bool EnmlFormatter::isAttributeValid(QString attribute) {
    bool isInvalid =
            attribute.startsWith("on")
            || (attribute == "id")
            || (attribute == "class")
            || (attribute == "accesskey")
            || (attribute == "data")
            || (attribute == "dynsrc")
            || (attribute == "tabindex")
            // These are things that are NixNote specific
            || (attribute == "en-tag")
            || (attribute == "src")
            || (attribute == "en-new")
            || (attribute == "guid")
            || (attribute == "lid")
            || (attribute == "cipher")
            || (attribute == "hint");
    return !isInvalid;
}


bool EnmlFormatter::checkEndFixElement(QWebElement e) {
    QString element = e.tagName().toLower();
    //QLOG_DEBUG() << "Checking tag " << element;

    // this removes all generally prohibited attributes
    bool needSpecialCare = (element == "a") || (element == "object")|| (element == "img");
    if (!needSpecialCare) {
        // leave out for attribute which have internal attributes like "lid"
        removeInvalidAttributes(e);
    }

    if (element == "a") {
        checkAttributes(e, attrs + focus + a);
    } else if (element == "abbr") {
        checkAttributes(e, attrs);
    } else if (element == "acronym") {
        checkAttributes(e, attrs);
    } else if (element == "address") {
        checkAttributes(e, attrs);
    } else if (element == "area") {
        checkAttributes(e, attrs + focus + area);
    } else if (element == "b") {
        checkAttributes(e, attrs);
    } else if (element == "bdo") {
        checkAttributes(e, coreattrs + bdo);
    } else if (element == "big") {
        checkAttributes(e, attrs);
    } else if (element == "blockquote") {
        checkAttributes(e, attrs + blockQuote);
    } else if (element == "br") {
        checkAttributes(e, coreattrs + br);
    } else if (element == "caption") {
        checkAttributes(e, attrs + caption);
    } else if (element == "center") {
        checkAttributes(e, attrs);
    } else if (element == "cite") {
        checkAttributes(e, attrs);
    } else if (element == "code") {
        checkAttributes(e, attrs);
    } else if (element == "col") {
        checkAttributes(e, attrs + cellHalign + cellValign + col);
    } else if (element == "colgroup") {
        checkAttributes(e, attrs + cellHalign + cellValign + colGroup);
    } else if (element == "dd") {
        checkAttributes(e, attrs);
    } else if (element == "del") {
        checkAttributes(e, attrs + del);
    } else if (element == "dfn") {
        checkAttributes(e, attrs);
    } else if (element == "div") {
        checkAttributes(e, attrs + textAlign);
    } else if (element == "dl") {
        checkAttributes(e, attrs + dl);
    } else if (element == "dt") {
        checkAttributes(e, attrs);
    } else if (element == "em") {
        checkAttributes(e, attrs);
    } else if ((element == "en-media") || (element == "en-crypt") || (element == "en-todo") || (element == "en-note")) {
    } else if (element == "font") {
        checkAttributes(e, coreattrs + i18n + font);
    } else if ((element == "h1") || (element == "h2") || (element == "h3") || (element == "h4") || (element == "h5") ||
               (element == "h6")) {
        checkAttributes(e, attrs + textAlign);
    } else if (element == "hr") {
        checkAttributes(e, attrs + hr);
    } else if (element == "i") {
        checkAttributes(e, attrs);
    } else if (element == "input") {
        checkAttributes(e, attrs + input);
    } else if (element == "img") {
        checkAttributes(e, attrs + img);
    } else if (element == "ins") {
        checkAttributes(e, attrs + ins);
    } else if (element == "kbd") {
        checkAttributes(e, attrs);
    } else if (element == "li") {
        checkAttributes(e, attrs + li);
    } else if (element == "map") {
        checkAttributes(e, i18n + map);
    } else if (element == "object") {
        checkAttributes(e, attrs + object);
    } else if (element == "ol") {
        checkAttributes(e, attrs + ol);
    } else if (element == "p") {
        checkAttributes(e, attrs + textAlign);
    } else if (element == "pre") {
        checkAttributes(e, attrs + pre);
    } else if (element == "q") {
        checkAttributes(e, attrs + q);
    } else if (element == "s") {
        checkAttributes(e, attrs);
    } else if (element == "samp") {
        checkAttributes(e, attrs);
    } else if (element == "small") {
        checkAttributes(e, attrs);
    } else if (element == "span") {
        checkAttributes(e, attrs);
    } else if (element == "strike") {
        checkAttributes(e, attrs);
    } else if (element == "strong") {
        checkAttributes(e, attrs);
    } else if (element == "sub") {
        checkAttributes(e, attrs);
    } else if (element == "sup") {
        checkAttributes(e, attrs);
    } else if (element == "table") {
        checkAttributes(e, attrs + table);
    } else if (element == "tbody") {
        checkAttributes(e, attrs + cellHalign + cellValign);
    } else if (element == "td") {
        checkAttributes(e, attrs + cellValign + cellHalign + td);
    } else if (element == "tfoot") {
        checkAttributes(e, attrs + cellHalign + cellValign);
    } else if (element == "th") {
        checkAttributes(e, attrs + cellHalign + cellValign + th);
    } else if (element == "thread") {
        checkAttributes(e, attrs + cellHalign + cellValign);
    } else if (element == "title") {

    } else if (element == "tr") {
        checkAttributes(e, attrs + cellHalign + cellValign + tr_);
    } else if (element == "tt") {
        checkAttributes(e, attrs);
    } else if (element == "u") {
        checkAttributes(e, attrs);
    } else if (element == "ul") {
        checkAttributes(e, attrs + ul);
    } else if (element == "var") {
        checkAttributes(e, attrs);
    } else if (element == "xmp") {

    } else {
        QLOG_WARN() << ENML_MODULE_LOGPREFIX << element << " is invalid .. will be removed";
        return false;
    }
    // possibly fixed, but now is valid
    return true;
}


void EnmlFormatter::removeInvalidAttributes(QWebElement &node) {
    // Remove any invalid attributes
    QStringList attributes = node.attributeNames();
    for (int i = 0; i < attributes.size(); i++) {
        QString a = attributes[i];
        if (!isAttributeValid(a)) {
            QLOG_WARN() << ENML_MODULE_LOGPREFIX "removeInvalidAttributes - tag " << node.tagName().toLower() << " removing  invalid attribute " << a;
            node.removeAttribute(a);
        }
    }
}

QByteArray EnmlFormatter::fixEncryptionTags(QByteArray newContent) {
    int endPos, startPos, endData, slotStart, slotEnd;
    QByteArray eTag = "<table class=\"en-crypt-temp\"";
    for (int i = newContent.indexOf(eTag); i != -1; i = newContent.indexOf(eTag, i + 1)) {
        slotStart = newContent.indexOf("slot", i + 1) + 6;
        slotEnd = newContent.indexOf("\"", slotStart + 1);
        QString slot = newContent.mid(slotStart, slotEnd - slotStart);
        slot = slot.replace("\"", "");
        startPos = newContent.indexOf("<td>", i + 1) + 4;
        endData = newContent.indexOf("</td>", startPos);
        QString text = newContent.mid(startPos, endData - startPos);
        endPos = newContent.indexOf("</table>", i + 1) + 8;

        // Encrypt the text
        QPair<QString, QString> pair = passwordSafe.value(slot);
        QString password = pair.first;
        QString hint = pair.second;
        EnCrypt crypt(cryptoJarPath);
        QString encrypted;
        crypt.encrypt(encrypted, text, password);

        // replace the table with an en-crypt tag.
        QByteArray start = newContent.mid(0, i - 1);
        QByteArray end = newContent.mid(endPos);
        newContent.clear();
        newContent.append(start);
        newContent.append(QByteArray("<en-crypt cipher=\"RC2\" length=\"64\" hint=\""));
        newContent.append(hint.toLocal8Bit());
        newContent.append(QByteArray("\">"));
        newContent.append(encrypted.toLocal8Bit());
        newContent.append(QByteArray("</en-crypt>"));
        newContent.append(end);
        QLOG_DEBUG() << ENML_MODULE_LOGPREFIX "rewritten 'en-crypt' tag";
    }
    return newContent;
}


/**
 * Remove any invalid unicode characters to allow it to sync properly.
 * @return
 */
void EnmlFormatter::removeInvalidUnicode() {
    QString c(content);
    // 1b is ascii 27 = escape character
    c = c.remove(QChar(0x1b), Qt::CaseInsensitive);
    content = c.toUtf8();
}


/**
 * Look through all attributes of the node.  If it isn't in the list of
 * valid attributes, we remove it.
 */
void EnmlFormatter::checkAttributes(QWebElement &element, QStringList valid) {
    QStringList attrs = element.attributeNames();
    for (int i = 0; i < attrs.size(); i++) {
        if (!valid.contains(attrs[i])) {
            QLOG_WARN() << ENML_MODULE_LOGPREFIX "checkAttributes - tag " << element.tagName().toLower() << " removing invalid attribute " << attrs[i];
            element.removeAttribute(attrs[i]);
        }
    }
}

bool EnmlFormatter::isFormattingError() const {
    return formattingError;
}


/**
 * Set content from unicode string.
 * @param contentStr unicode string.
 */
void EnmlFormatter::setContent(QString &contentStr) {
    this->content.clear();
    this->content.append(contentStr.toUtf8());
}