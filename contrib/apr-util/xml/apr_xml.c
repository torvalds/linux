/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apr.h"
#include "apr_strings.h"

#define APR_WANT_STDIO          /* for sprintf() */
#define APR_WANT_STRFUNC
#include "apr_want.h"

#include "apr_xml.h"

#include "apu_config.h"

#if defined(HAVE_XMLPARSE_XMLPARSE_H)
#include <xmlparse/xmlparse.h>
#elif defined(HAVE_XMLTOK_XMLPARSE_H)
#include <xmltok/xmlparse.h>
#elif defined(HAVE_XML_XMLPARSE_H)
#include <xml/xmlparse.h>
#else
#include <expat.h>
#endif

#define DEBUG_CR "\r\n"

static const char APR_KW_xmlns[] = { 0x78, 0x6D, 0x6C, 0x6E, 0x73, '\0' };
static const char APR_KW_xmlns_lang[] = { 0x78, 0x6D, 0x6C, 0x3A, 0x6C, 0x61, 0x6E, 0x67, '\0' };
static const char APR_KW_DAV[] = { 0x44, 0x41, 0x56, 0x3A, '\0' };

/* errors related to namespace processing */
#define APR_XML_NS_ERROR_UNKNOWN_PREFIX (-1000)
#define APR_XML_NS_ERROR_INVALID_DECL (-1001)

/* test for a namespace prefix that begins with [Xx][Mm][Ll] */
#define APR_XML_NS_IS_RESERVED(name) \
	( (name[0] == 0x58 || name[0] == 0x78) && \
	  (name[1] == 0x4D || name[1] == 0x6D) && \
	  (name[2] == 0x4C || name[2] == 0x6C) )


/* the real (internal) definition of the parser context */
struct apr_xml_parser {
    apr_xml_doc *doc;		/* the doc we're parsing */
    apr_pool_t *p;		/* the pool we allocate from */
    apr_xml_elem *cur_elem;	/* current element */

    int error;			/* an error has occurred */
#define APR_XML_ERROR_EXPAT             1
#define APR_XML_ERROR_PARSE_DONE        2
/* also: public APR_XML_NS_ERROR_* values (if any) */

    XML_Parser xp;              /* the actual (Expat) XML parser */
    enum XML_Error xp_err;      /* stored Expat error code */
};

/* struct for scoping namespace declarations */
typedef struct apr_xml_ns_scope {
    const char *prefix;		/* prefix used for this ns */
    int ns;			/* index into namespace table */
    int emptyURI;		/* the namespace URI is the empty string */
    struct apr_xml_ns_scope *next;	/* next scoped namespace */
} apr_xml_ns_scope;


/* return namespace table index for a given prefix */
static int find_prefix(apr_xml_parser *parser, const char *prefix)
{
    apr_xml_elem *elem = parser->cur_elem;

    /*
    ** Walk up the tree, looking for a namespace scope that defines this
    ** prefix.
    */
    for (; elem; elem = elem->parent) {
	apr_xml_ns_scope *ns_scope = elem->ns_scope;

	for (ns_scope = elem->ns_scope; ns_scope; ns_scope = ns_scope->next) {
	    if (strcmp(prefix, ns_scope->prefix) == 0) {
		if (ns_scope->emptyURI) {
		    /*
		    ** It is possible to set the default namespace to an
		    ** empty URI string; this resets the default namespace
		    ** to mean "no namespace." We just found the prefix
		    ** refers to an empty URI, so return "no namespace."
		    */
		    return APR_XML_NS_NONE;
		}

		return ns_scope->ns;
	    }
	}
    }

    /*
     * If the prefix is empty (""), this means that a prefix was not
     * specified in the element/attribute. The search that was performed
     * just above did not locate a default namespace URI (which is stored
     * into ns_scope with an empty prefix). This means the element/attribute
     * has "no namespace". We have a reserved value for this.
     */
    if (*prefix == '\0') {
	return APR_XML_NS_NONE;
    }

    /* not found */
    return APR_XML_NS_ERROR_UNKNOWN_PREFIX;
}

static void start_handler(void *userdata, const char *name, const char **attrs)
{
    apr_xml_parser *parser = userdata;
    apr_xml_elem *elem;
    apr_xml_attr *attr;
    apr_xml_attr *prev;
    char *colon;
    const char *quoted;
    char *elem_name;

    /* punt once we find an error */
    if (parser->error)
	return;

    elem = apr_pcalloc(parser->p, sizeof(*elem));

    /* prep the element */
    elem->name = elem_name = apr_pstrdup(parser->p, name);

    /* fill in the attributes (note: ends up in reverse order) */
    while (*attrs) {
	attr = apr_palloc(parser->p, sizeof(*attr));
	attr->name = apr_pstrdup(parser->p, *attrs++);
	attr->value = apr_pstrdup(parser->p, *attrs++);
	attr->next = elem->attr;
	elem->attr = attr;
    }

    /* hook the element into the tree */
    if (parser->cur_elem == NULL) {
	/* no current element; this also becomes the root */
	parser->cur_elem = parser->doc->root = elem;
    }
    else {
	/* this element appeared within the current elem */
	elem->parent = parser->cur_elem;

	/* set up the child/sibling links */
	if (elem->parent->last_child == NULL) {
	    /* no first child either */
	    elem->parent->first_child = elem->parent->last_child = elem;
	}
	else {
	    /* hook onto the end of the parent's children */
	    elem->parent->last_child->next = elem;
	    elem->parent->last_child = elem;
	}

	/* this element is now the current element */
	parser->cur_elem = elem;
    }

    /* scan the attributes for namespace declarations */
    for (prev = NULL, attr = elem->attr;
	 attr;
	 attr = attr->next) {
	if (strncmp(attr->name, APR_KW_xmlns, 5) == 0) {
	    const char *prefix = &attr->name[5];
	    apr_xml_ns_scope *ns_scope;

	    /* test for xmlns:foo= form and xmlns= form */
	    if (*prefix == 0x3A) {
                /* a namespace prefix declaration must have a
                   non-empty value. */
                if (attr->value[0] == '\0') {
                    parser->error = APR_XML_NS_ERROR_INVALID_DECL;
                    return;
                }
		++prefix;
            }
	    else if (*prefix != '\0') {
		/* advance "prev" since "attr" is still present */
		prev = attr;
		continue;
	    }

	    /* quote the URI before we ever start working with it */
	    quoted = apr_xml_quote_string(parser->p, attr->value, 1);

	    /* build and insert the new scope */
	    ns_scope = apr_pcalloc(parser->p, sizeof(*ns_scope));
	    ns_scope->prefix = prefix;
	    ns_scope->ns = apr_xml_insert_uri(parser->doc->namespaces, quoted);
	    ns_scope->emptyURI = *quoted == '\0';
	    ns_scope->next = elem->ns_scope;
	    elem->ns_scope = ns_scope;

	    /* remove this attribute from the element */
	    if (prev == NULL)
		elem->attr = attr->next;
	    else
		prev->next = attr->next;

	    /* Note: prev will not be advanced since we just removed "attr" */
	}
	else if (strcmp(attr->name, APR_KW_xmlns_lang) == 0) {
	    /* save away the language (in quoted form) */
	    elem->lang = apr_xml_quote_string(parser->p, attr->value, 1);

	    /* remove this attribute from the element */
	    if (prev == NULL)
		elem->attr = attr->next;
	    else
		prev->next = attr->next;

	    /* Note: prev will not be advanced since we just removed "attr" */
	}
	else {
	    /* advance "prev" since "attr" is still present */
	    prev = attr;
	}
    }

    /*
    ** If an xml:lang attribute didn't exist (lang==NULL), then copy the
    ** language from the parent element (if present).
    **
    ** NOTE: elem_size() *depends* upon this pointer equality.
    */
    if (elem->lang == NULL && elem->parent != NULL)
	elem->lang = elem->parent->lang;

    /* adjust the element's namespace */
    colon = strchr(elem_name, 0x3A);
    if (colon == NULL) {
	/*
	 * The element is using the default namespace, which will always
	 * be found. Either it will be "no namespace", or a default
	 * namespace URI has been specified at some point.
	 */
	elem->ns = find_prefix(parser, "");
    }
    else if (APR_XML_NS_IS_RESERVED(elem->name)) {
	elem->ns = APR_XML_NS_NONE;
    }
    else {
	*colon = '\0';
	elem->ns = find_prefix(parser, elem->name);
	elem->name = colon + 1;

	if (APR_XML_NS_IS_ERROR(elem->ns)) {
	    parser->error = elem->ns;
	    return;
	}
    }

    /* adjust all remaining attributes' namespaces */
    for (attr = elem->attr; attr; attr = attr->next) {
        /*
         * apr_xml_attr defines this as "const" but we dup'd it, so we
         * know that we can change it. a bit hacky, but the existing
         * structure def is best.
         */
        char *attr_name = (char *)attr->name;

	colon = strchr(attr_name, 0x3A);
	if (colon == NULL) {
	    /*
	     * Attributes do NOT use the default namespace. Therefore,
	     * we place them into the "no namespace" category.
	     */
	    attr->ns = APR_XML_NS_NONE;
	}
	else if (APR_XML_NS_IS_RESERVED(attr->name)) {
	    attr->ns = APR_XML_NS_NONE;
	}
	else {
	    *colon = '\0';
	    attr->ns = find_prefix(parser, attr->name);
	    attr->name = colon + 1;

	    if (APR_XML_NS_IS_ERROR(attr->ns)) {
		parser->error = attr->ns;
		return;
	    }
	}
    }
}

static void end_handler(void *userdata, const char *name)
{
    apr_xml_parser *parser = userdata;

    /* punt once we find an error */
    if (parser->error)
	return;

    /* pop up one level */
    parser->cur_elem = parser->cur_elem->parent;
}

static void cdata_handler(void *userdata, const char *data, int len)
{
    apr_xml_parser *parser = userdata;
    apr_xml_elem *elem;
    apr_text_header *hdr;
    const char *s;

    /* punt once we find an error */
    if (parser->error)
	return;

    elem = parser->cur_elem;
    s = apr_pstrndup(parser->p, data, len);

    if (elem->last_child == NULL) {
	/* no children yet. this cdata follows the start tag */
	hdr = &elem->first_cdata;
    }
    else {
	/* child elements exist. this cdata follows the last child. */
	hdr = &elem->last_child->following_cdata;
    }

    apr_text_append(parser->p, hdr, s);
}

static apr_status_t cleanup_parser(void *ctx)
{
    apr_xml_parser *parser = ctx;

    XML_ParserFree(parser->xp);
    parser->xp = NULL;

    return APR_SUCCESS;
}

#if XML_MAJOR_VERSION > 1
/* Stop the parser if an entity declaration is hit. */
static void entity_declaration(void *userData, const XML_Char *entityName,
                               int is_parameter_entity, const XML_Char *value,
                               int value_length, const XML_Char *base,
                               const XML_Char *systemId, const XML_Char *publicId,
                               const XML_Char *notationName)
{
    apr_xml_parser *parser = userData;

    XML_StopParser(parser->xp, XML_FALSE);
}
#else
/* A noop default_handler. */
static void default_handler(void *userData, const XML_Char *s, int len)
{
}
#endif

APU_DECLARE(apr_xml_parser *) apr_xml_parser_create(apr_pool_t *pool)
{
    apr_xml_parser *parser = apr_pcalloc(pool, sizeof(*parser));

    parser->p = pool;
    parser->doc = apr_pcalloc(pool, sizeof(*parser->doc));

    parser->doc->namespaces = apr_array_make(pool, 5, sizeof(const char *));

    /* ### is there a way to avoid hard-coding this? */
    apr_xml_insert_uri(parser->doc->namespaces, APR_KW_DAV);

    parser->xp = XML_ParserCreate(NULL);
    if (parser->xp == NULL) {
        (*apr_pool_abort_get(pool))(APR_ENOMEM);
        return NULL;
    }

    apr_pool_cleanup_register(pool, parser, cleanup_parser,
                              apr_pool_cleanup_null);

    XML_SetUserData(parser->xp, parser);
    XML_SetElementHandler(parser->xp, start_handler, end_handler);
    XML_SetCharacterDataHandler(parser->xp, cdata_handler);

    /* Prevent the "billion laughs" attack against expat by disabling
     * internal entity expansion.  With 2.x, forcibly stop the parser
     * if an entity is declared - this is safer and a more obvious
     * failure mode.  With older versions, installing a noop
     * DefaultHandler means that internal entities will be expanded as
     * the empty string, which is also sufficient to prevent the
     * attack. */
#if XML_MAJOR_VERSION > 1
    XML_SetEntityDeclHandler(parser->xp, entity_declaration);
#else
    XML_SetDefaultHandler(parser->xp, default_handler);
#endif

    return parser;
}

static apr_status_t do_parse(apr_xml_parser *parser,
                             const char *data, apr_size_t len,
                             int is_final)
{
    if (parser->xp == NULL) {
        parser->error = APR_XML_ERROR_PARSE_DONE;
    }
    else {
        int rv = XML_Parse(parser->xp, data, (int)len, is_final);

        if (rv == 0) {
            parser->error = APR_XML_ERROR_EXPAT;
            parser->xp_err = XML_GetErrorCode(parser->xp);
        }
    }

    /* ### better error code? */
    return parser->error ? APR_EGENERAL : APR_SUCCESS;
}

APU_DECLARE(apr_status_t) apr_xml_parser_feed(apr_xml_parser *parser,
                                              const char *data,
                                              apr_size_t len)
{
    return do_parse(parser, data, len, 0 /* is_final */);
}

APU_DECLARE(apr_status_t) apr_xml_parser_done(apr_xml_parser *parser,
                                              apr_xml_doc **pdoc)
{
    char end;
    apr_status_t status = do_parse(parser, &end, 0, 1 /* is_final */);

    /* get rid of the parser */
    (void) apr_pool_cleanup_run(parser->p, parser, cleanup_parser);

    if (status)
        return status;

    if (pdoc != NULL)
        *pdoc = parser->doc;
    return APR_SUCCESS;
}

APU_DECLARE(char *) apr_xml_parser_geterror(apr_xml_parser *parser,
                                            char *errbuf,
                                            apr_size_t errbufsize)
{
    int error = parser->error;
    const char *msg;

    /* clear our record of an error */
    parser->error = 0;

    switch (error) {
    case 0:
        msg = "No error.";
        break;

    case APR_XML_NS_ERROR_UNKNOWN_PREFIX:
        msg = "An undefined namespace prefix was used.";
        break;

    case APR_XML_NS_ERROR_INVALID_DECL:
        msg = "A namespace prefix was defined with an empty URI.";
        break;

    case APR_XML_ERROR_EXPAT:
        (void) apr_snprintf(errbuf, errbufsize,
                            "XML parser error code: %s (%d)",
                            XML_ErrorString(parser->xp_err), parser->xp_err);
        return errbuf;

    case APR_XML_ERROR_PARSE_DONE:
        msg = "The parser is not active.";
        break;

    default:
        msg = "There was an unknown error within the XML body.";
        break;
    }

    (void) apr_cpystrn(errbuf, msg, errbufsize);
    return errbuf;
}

APU_DECLARE(apr_status_t) apr_xml_parse_file(apr_pool_t *p,
                                             apr_xml_parser **parser,
                                             apr_xml_doc **ppdoc,
                                             apr_file_t *xmlfd,
                                             apr_size_t buffer_length)
{
    apr_status_t rv;
    char *buffer;
    apr_size_t length;

    *parser = apr_xml_parser_create(p);
    if (*parser == NULL) {
        /* FIXME: returning an error code would be nice,
         * but we dont get one ;( */
        return APR_EGENERAL;
    }
    buffer = apr_palloc(p, buffer_length);
    length = buffer_length;

    rv = apr_file_read(xmlfd, buffer, &length);

    while (rv == APR_SUCCESS) {
        rv = apr_xml_parser_feed(*parser, buffer, length);
        if (rv != APR_SUCCESS) {
            return rv;
        }

        length = buffer_length;
        rv = apr_file_read(xmlfd, buffer, &length);
    }
    if (rv != APR_EOF) {
        return rv;
    }
    rv = apr_xml_parser_done(*parser, ppdoc);
    *parser = NULL;
    return rv;
}

APU_DECLARE(void) apr_text_append(apr_pool_t * p, apr_text_header *hdr,
                                  const char *text)
{
    apr_text *t = apr_palloc(p, sizeof(*t));

    t->text = text;
    t->next = NULL;

    if (hdr->first == NULL) {
	/* no text elements yet */
	hdr->first = hdr->last = t;
    }
    else {
	/* append to the last text element */
	hdr->last->next = t;
	hdr->last = t;
    }
}


/* ---------------------------------------------------------------
**
** XML UTILITY FUNCTIONS
*/

/*
** apr_xml_quote_string: quote an XML string
**
** Replace '<', '>', and '&' with '&lt;', '&gt;', and '&amp;'.
** If quotes is true, then replace '"' with '&quot;'.
**
** quotes is typically set to true for XML strings that will occur within
** double quotes -- attribute values.
*/
APU_DECLARE(const char *) apr_xml_quote_string(apr_pool_t *p, const char *s,
                                               int quotes)
{
    const char *scan;
    apr_size_t len = 0;
    apr_size_t extra = 0;
    char *qstr;
    char *qscan;
    char c;

    for (scan = s; (c = *scan) != '\0'; ++scan, ++len) {
	if (c == '<' || c == '>')
	    extra += 3;		/* &lt; or &gt; */
	else if (c == '&')
	    extra += 4;		/* &amp; */
	else if (quotes && c == '"')
	    extra += 5;		/* &quot; */
    }

    /* nothing to do? */
    if (extra == 0)
	return s;

    qstr = apr_palloc(p, len + extra + 1);
    for (scan = s, qscan = qstr; (c = *scan) != '\0'; ++scan) {
	if (c == '<') {
	    *qscan++ = '&';
	    *qscan++ = 'l';
	    *qscan++ = 't';
	    *qscan++ = ';';
	}
	else if (c == '>') {
	    *qscan++ = '&';
	    *qscan++ = 'g';
	    *qscan++ = 't';
	    *qscan++ = ';';
	}
	else if (c == '&') {
	    *qscan++ = '&';
	    *qscan++ = 'a';
	    *qscan++ = 'm';
	    *qscan++ = 'p';
	    *qscan++ = ';';
	}
	else if (quotes && c == '"') {
	    *qscan++ = '&';
	    *qscan++ = 'q';
	    *qscan++ = 'u';
	    *qscan++ = 'o';
	    *qscan++ = 't';
	    *qscan++ = ';';
	}
	else {
	    *qscan++ = c;
	}
    }

    *qscan = '\0';
    return qstr;
}

/* how many characters for the given integer? */
#define APR_XML_NS_LEN(ns) ((ns) < 10 ? 1 : (ns) < 100 ? 2 : (ns) < 1000 ? 3 : \
                            (ns) < 10000 ? 4 : (ns) < 100000 ? 5 : \
                            (ns) < 1000000 ? 6 : (ns) < 10000000 ? 7 : \
                            (ns) < 100000000 ? 8 : (ns) < 1000000000 ? 9 : 10)

static apr_size_t text_size(const apr_text *t)
{
    apr_size_t size = 0;

    for (; t; t = t->next)
	size += strlen(t->text);
    return size;
}

static apr_size_t elem_size(const apr_xml_elem *elem, int style,
                            apr_array_header_t *namespaces, int *ns_map)
{
    apr_size_t size;

    if (style == APR_XML_X2T_FULL || style == APR_XML_X2T_FULL_NS_LANG) {
	const apr_xml_attr *attr;

	size = 0;

	if (style == APR_XML_X2T_FULL_NS_LANG) {
	    int i;

	    /*
	    ** The outer element will contain xmlns:ns%d="%s" attributes
	    ** and an xml:lang attribute, if applicable.
	    */

	    for (i = namespaces->nelts; i--;) {
		/* compute size of: ' xmlns:ns%d="%s"' */
		size += (9 + APR_XML_NS_LEN(i) + 2 +
			 strlen(APR_XML_GET_URI_ITEM(namespaces, i)) + 1);
	    }

	    if (elem->lang != NULL) {
		/* compute size of: ' xml:lang="%s"' */
		size += 11 + strlen(elem->lang) + 1;
	    }
	}

	if (elem->ns == APR_XML_NS_NONE) {
	    /* compute size of: <%s> */
	    size += 1 + strlen(elem->name) + 1;
	}
	else {
	    int ns = ns_map ? ns_map[elem->ns] : elem->ns;

	    /* compute size of: <ns%d:%s> */
	    size += 3 + APR_XML_NS_LEN(ns) + 1 + strlen(elem->name) + 1;
	}

	if (APR_XML_ELEM_IS_EMPTY(elem)) {
	    /* insert a closing "/" */
	    size += 1;
	}
	else {
	    /*
	     * two of above plus "/":
	     *     <ns%d:%s> ... </ns%d:%s>
	     * OR  <%s> ... </%s>
	     */
	    size = 2 * size + 1;
	}

	for (attr = elem->attr; attr; attr = attr->next) {
	    if (attr->ns == APR_XML_NS_NONE) {
		/* compute size of: ' %s="%s"' */
		size += 1 + strlen(attr->name) + 2 + strlen(attr->value) + 1;
	    }
	    else {
		/* compute size of: ' ns%d:%s="%s"' */
                int ns = ns_map ? ns_map[attr->ns] : attr->ns;
                size += 3 + APR_XML_NS_LEN(ns) + 1 + strlen(attr->name) + 2 + strlen(attr->value) + 1;
	    }
	}

	/*
	** If the element has an xml:lang value that is *different* from
	** its parent, then add the thing in: ' xml:lang="%s"'.
	**
	** NOTE: we take advantage of the pointer equality established by
	** the parsing for "inheriting" the xml:lang values from parents.
	*/
	if (elem->lang != NULL &&
	    (elem->parent == NULL || elem->lang != elem->parent->lang)) {
	    size += 11 + strlen(elem->lang) + 1;
	}
    }
    else if (style == APR_XML_X2T_LANG_INNER) {
	/*
	 * This style prepends the xml:lang value plus a null terminator.
	 * If a lang value is not present, then we insert a null term.
	 */
	size = elem->lang ? strlen(elem->lang) + 1 : 1;
    }
    else
	size = 0;

    size += text_size(elem->first_cdata.first);

    for (elem = elem->first_child; elem; elem = elem->next) {
	/* the size of the child element plus the CDATA that follows it */
	size += (elem_size(elem, APR_XML_X2T_FULL, NULL, ns_map) +
		 text_size(elem->following_cdata.first));
    }

    return size;
}

static char *write_text(char *s, const apr_text *t)
{
    for (; t; t = t->next) {
	apr_size_t len = strlen(t->text);
	memcpy(s, t->text, len);
	s += len;
    }
    return s;
}

static char *write_elem(char *s, const apr_xml_elem *elem, int style,
			apr_array_header_t *namespaces, int *ns_map)
{
    const apr_xml_elem *child;
    apr_size_t len;
    int ns;

    if (style == APR_XML_X2T_FULL || style == APR_XML_X2T_FULL_NS_LANG) {
	int empty = APR_XML_ELEM_IS_EMPTY(elem);
	const apr_xml_attr *attr;

	if (elem->ns == APR_XML_NS_NONE) {
	    len = sprintf(s, "<%s", elem->name);
	}
	else {
	    ns = ns_map ? ns_map[elem->ns] : elem->ns;
	    len = sprintf(s, "<ns%d:%s", ns, elem->name);
	}
	s += len;

	for (attr = elem->attr; attr; attr = attr->next) {
	    if (attr->ns == APR_XML_NS_NONE)
		len = sprintf(s, " %s=\"%s\"", attr->name, attr->value);
            else {
                ns = ns_map ? ns_map[attr->ns] : attr->ns;
                len = sprintf(s, " ns%d:%s=\"%s\"", ns, attr->name, attr->value);
            }
	    s += len;
	}

	/* add the xml:lang value if necessary */
	if (elem->lang != NULL &&
	    (style == APR_XML_X2T_FULL_NS_LANG ||
	     elem->parent == NULL ||
	     elem->lang != elem->parent->lang)) {
	    len = sprintf(s, " xml:lang=\"%s\"", elem->lang);
	    s += len;
	}

	/* add namespace definitions, if required */
	if (style == APR_XML_X2T_FULL_NS_LANG) {
	    int i;

	    for (i = namespaces->nelts; i--;) {
		len = sprintf(s, " xmlns:ns%d=\"%s\"", i,
			      APR_XML_GET_URI_ITEM(namespaces, i));
		s += len;
	    }
	}

	/* no more to do. close it up and go. */
	if (empty) {
	    *s++ = '/';
	    *s++ = '>';
	    return s;
	}

	/* just close it */
	*s++ = '>';
    }
    else if (style == APR_XML_X2T_LANG_INNER) {
	/* prepend the xml:lang value */
	if (elem->lang != NULL) {
	    len = strlen(elem->lang);
	    memcpy(s, elem->lang, len);
	    s += len;
	}
	*s++ = '\0';
    }

    s = write_text(s, elem->first_cdata.first);

    for (child = elem->first_child; child; child = child->next) {
	s = write_elem(s, child, APR_XML_X2T_FULL, NULL, ns_map);
	s = write_text(s, child->following_cdata.first);
    }

    if (style == APR_XML_X2T_FULL || style == APR_XML_X2T_FULL_NS_LANG) {
	if (elem->ns == APR_XML_NS_NONE) {
	    len = sprintf(s, "</%s>", elem->name);
	}
	else {
	    ns = ns_map ? ns_map[elem->ns] : elem->ns;
	    len = sprintf(s, "</ns%d:%s>", ns, elem->name);
	}
	s += len;
    }

    return s;
}

APU_DECLARE(void) apr_xml_quote_elem(apr_pool_t *p, apr_xml_elem *elem)
{
    apr_text *scan_txt;
    apr_xml_attr *scan_attr;
    apr_xml_elem *scan_elem;

    /* convert the element's text */
    for (scan_txt = elem->first_cdata.first;
	 scan_txt != NULL;
	 scan_txt = scan_txt->next) {
	scan_txt->text = apr_xml_quote_string(p, scan_txt->text, 0);
    }
    for (scan_txt = elem->following_cdata.first;
	 scan_txt != NULL;
	 scan_txt = scan_txt->next) {
	scan_txt->text = apr_xml_quote_string(p, scan_txt->text, 0);
    }

    /* convert the attribute values */
    for (scan_attr = elem->attr;
	 scan_attr != NULL;
	 scan_attr = scan_attr->next) {
	scan_attr->value = apr_xml_quote_string(p, scan_attr->value, 1);
    }

    /* convert the child elements */
    for (scan_elem = elem->first_child;
	 scan_elem != NULL;
	 scan_elem = scan_elem->next) {
	apr_xml_quote_elem(p, scan_elem);
    }
}

/* convert an element to a text string */
APU_DECLARE(void) apr_xml_to_text(apr_pool_t * p, const apr_xml_elem *elem,
                                  int style, apr_array_header_t *namespaces,
                                  int *ns_map, const char **pbuf,
                                  apr_size_t *psize)
{
    /* get the exact size, plus a null terminator */
    apr_size_t size = elem_size(elem, style, namespaces, ns_map) + 1;
    char *s = apr_palloc(p, size);

    (void) write_elem(s, elem, style, namespaces, ns_map);
    s[size - 1] = '\0';

    *pbuf = s;
    if (psize)
	*psize = size;
}

APU_DECLARE(const char *) apr_xml_empty_elem(apr_pool_t * p,
                                             const apr_xml_elem *elem)
{
    if (elem->ns == APR_XML_NS_NONE) {
	/*
	 * The prefix (xml...) is already within the prop name, or
	 * the element simply has no prefix.
	 */
	return apr_psprintf(p, "<%s/>" DEBUG_CR, elem->name);
    }

    return apr_psprintf(p, "<ns%d:%s/>" DEBUG_CR, elem->ns, elem->name);
}

/* return the URI's (existing) index, or insert it and return a new index */
APU_DECLARE(int) apr_xml_insert_uri(apr_array_header_t *uri_array,
                                    const char *uri)
{
    int i;
    const char **pelt;

    /* never insert an empty URI; this index is always APR_XML_NS_NONE */
    if (*uri == '\0')
        return APR_XML_NS_NONE;  

    for (i = uri_array->nelts; i--;) {
	if (strcmp(uri, APR_XML_GET_URI_ITEM(uri_array, i)) == 0)
	    return i;
    }

    pelt = apr_array_push(uri_array);
    *pelt = uri;		/* assume uri is const or in a pool */
    return uri_array->nelts - 1;
}

/* convert the element to EBCDIC */
#if APR_CHARSET_EBCDIC
static apr_status_t apr_xml_parser_convert_elem(apr_xml_elem *e,
                                                apr_xlate_t *convset)
{
    apr_xml_attr *a;
    apr_xml_elem *ec;
    apr_text *t;
    apr_size_t inbytes_left, outbytes_left;
    apr_status_t status;

    inbytes_left = outbytes_left = strlen(e->name);
    status = apr_xlate_conv_buffer(convset, e->name,  &inbytes_left, (char *) e->name, &outbytes_left);
    if (status) {
        return status;
    }

    for (t = e->first_cdata.first; t != NULL; t = t->next) {
        inbytes_left = outbytes_left = strlen(t->text);
        status = apr_xlate_conv_buffer(convset, t->text, &inbytes_left, (char *) t->text, &outbytes_left);
        if (status) {
            return status;
        }
    }

    for (t = e->following_cdata.first;  t != NULL; t = t->next) {
        inbytes_left = outbytes_left = strlen(t->text);
        status = apr_xlate_conv_buffer(convset, t->text, &inbytes_left, (char *) t->text, &outbytes_left);
        if (status) {
            return status;
        }
    }

    for (a = e->attr; a != NULL; a = a->next) {
        inbytes_left = outbytes_left = strlen(a->name);
        status = apr_xlate_conv_buffer(convset, a->name, &inbytes_left, (char *) a->name, &outbytes_left);
        if (status) {
            return status;
        }
        inbytes_left = outbytes_left = strlen(a->value);
        status = apr_xlate_conv_buffer(convset, a->value, &inbytes_left, (char *) a->value, &outbytes_left);
        if (status) {
            return status;
        }
    }

    for (ec = e->first_child; ec != NULL; ec = ec->next) {
        status = apr_xml_parser_convert_elem(ec, convset);
        if (status) {
            return status;
        }
    }
    return APR_SUCCESS;
}

/* convert the whole document to EBCDIC */
APU_DECLARE(apr_status_t) apr_xml_parser_convert_doc(apr_pool_t *pool,
                                                     apr_xml_doc *pdoc,
                                                     apr_xlate_t *convset)
{
    apr_status_t status;
    /* Don't convert the namespaces: they are constant! */
    if (pdoc->namespaces != NULL) {
        int i;
        apr_array_header_t *namespaces;
        namespaces = apr_array_make(pool, pdoc->namespaces->nelts, sizeof(const char *));
        if (namespaces == NULL)
            return APR_ENOMEM;
        for (i = 0; i < pdoc->namespaces->nelts; i++) {
            apr_size_t inbytes_left, outbytes_left;
            char *ptr = (char *) APR_XML_GET_URI_ITEM(pdoc->namespaces, i);
            ptr = apr_pstrdup(pool, ptr);
            if ( ptr == NULL)
                return APR_ENOMEM;
            inbytes_left = outbytes_left = strlen(ptr);
            status = apr_xlate_conv_buffer(convset, ptr, &inbytes_left, ptr, &outbytes_left);
            if (status) {
                return status;
            }
            apr_xml_insert_uri(namespaces, ptr);
        }
        pdoc->namespaces = namespaces;
    }
    return apr_xml_parser_convert_elem(pdoc->root, convset);
}
#endif
