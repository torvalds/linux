/*
 * UPnP XML helper routines
 * Copyright (c) 2000-2003 Intel Corporation
 * Copyright (c) 2006-2007 Sony Corporation
 * Copyright (c) 2008-2009 Atheros Communications
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * See wps_upnp.c for more details on licensing and code history.
 */

#include "includes.h"

#include "common.h"
#include "base64.h"
#include "http.h"
#include "upnp_xml.h"


/*
 * XML parsing and formatting
 *
 * XML is a markup language based on unicode; usually (and in our case,
 * always!) based on utf-8. utf-8 uses a variable number of bytes per
 * character. utf-8 has the advantage that all non-ASCII unicode characters are
 * represented by sequences of non-ascii (high bit set) bytes, whereas ASCII
 * characters are single ascii bytes, thus we can use typical text processing.
 *
 * (One other interesting thing about utf-8 is that it is possible to look at
 * any random byte and determine if it is the first byte of a character as
 * versus a continuation byte).
 *
 * The base syntax of XML uses a few ASCII punctionation characters; any
 * characters that would appear in the payload data are rewritten using
 * sequences, e.g., &amp; for ampersand(&) and &lt for left angle bracket (<).
 * Five such escapes total (more can be defined but that does not apply to our
 * case). Thus we can safely parse for angle brackets etc.
 *
 * XML describes tree structures of tagged data, with each element beginning
 * with an opening tag <label> and ending with a closing tag </label> with
 * matching label. (There is also a self-closing tag <label/> which is supposed
 * to be equivalent to <label></label>, i.e., no payload, but we are unlikely
 * to see it for our purpose).
 *
 * Actually the opening tags are a little more complicated because they can
 * contain "attributes" after the label (delimited by ascii space or tab chars)
 * of the form attribute_label="value" or attribute_label='value'; as it turns
 * out we do not have to read any of these attributes, just ignore them.
 *
 * Labels are any sequence of chars other than space, tab, right angle bracket
 * (and ?), but may have an inner structure of <namespace><colon><plain_label>.
 * As it turns out, we can ignore the namespaces, in fact we can ignore the
 * entire tree hierarchy, because the plain labels we are looking for will be
 * unique (not in general, but for this application). We do however have to be
 * careful to skip over the namespaces.
 *
 * In generating XML we have to be more careful, but that is easy because
 * everything we do is pretty canned. The only real care to take is to escape
 * any special chars in our payload.
 */

/**
 * xml_next_tag - Advance to next tag
 * @in: Input
 * @out: OUT: start of tag just after '<'
 * @out_tagname: OUT: start of name of tag, skipping namespace
 * @end: OUT: one after tag
 * Returns: 0 on success, 1 on failure
 *
 * A tag has form:
 *     <left angle bracket><...><right angle bracket>
 * Within the angle brackets, there is an optional leading forward slash (which
 * makes the tag an ending tag), then an optional leading label (followed by
 * colon) and then the tag name itself.
 *
 * Note that angle brackets present in the original data must have been encoded
 * as &lt; and &gt; so they will not trouble us.
 */
static int xml_next_tag(const char *in, const char **out,
			const char **out_tagname, const char **end)
{
	while (*in && *in != '<')
		in++;
	if (*in != '<')
		return 1;
	*out = ++in;
	if (*in == '/')
		in++;
	*out_tagname = in; /* maybe */
	while (isalnum(*in) || *in == '-')
		in++;
	if (*in == ':')
		*out_tagname = ++in;
	while (*in && *in != '>')
		in++;
	if (*in != '>')
		return 1;
	*end = ++in;
	return 0;
}


/* xml_data_encode -- format data for xml file, escaping special characters.
 *
 * Note that we assume we are using utf8 both as input and as output!
 * In utf8, characters may be classed as follows:
 *     0xxxxxxx(2) -- 1 byte ascii char
 *     11xxxxxx(2) -- 1st byte of multi-byte char w/ unicode value >= 0x80
 *         110xxxxx(2) -- 1st byte of 2 byte sequence (5 payload bits here)
 *         1110xxxx(2) -- 1st byte of 3 byte sequence (4 payload bits here)
 *         11110xxx(2) -- 1st byte of 4 byte sequence (3 payload bits here)
 *      10xxxxxx(2) -- extension byte (6 payload bits per byte)
 *      Some values implied by the above are however illegal because they
 *      do not represent unicode chars or are not the shortest encoding.
 * Actually, we can almost entirely ignore the above and just do
 * text processing same as for ascii text.
 *
 * XML is written with arbitrary unicode characters, except that five
 * characters have special meaning and so must be escaped where they
 * appear in payload data... which we do here.
 */
void xml_data_encode(struct wpabuf *buf, const char *data, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		u8 c = ((u8 *) data)[i];
		if (c == '<') {
			wpabuf_put_str(buf, "&lt;");
			continue;
		}
		if (c == '>') {
			wpabuf_put_str(buf, "&gt;");
			continue;
		}
		if (c == '&') {
			wpabuf_put_str(buf, "&amp;");
			continue;
		}
		if (c == '\'') {
			wpabuf_put_str(buf, "&apos;");
			continue;
		}
		if (c == '"') {
			wpabuf_put_str(buf, "&quot;");
			continue;
		}
		/*
		 * We could try to represent control characters using the
		 * sequence: &#x; where x is replaced by a hex numeral, but not
		 * clear why we would do this.
		 */
		wpabuf_put_u8(buf, c);
	}
}


/* xml_add_tagged_data -- format tagged data as a new xml line.
 *
 * tag must not have any special chars.
 * data may have special chars, which are escaped.
 */
void xml_add_tagged_data(struct wpabuf *buf, const char *tag, const char *data)
{
	wpabuf_printf(buf, "<%s>", tag);
	xml_data_encode(buf, data, os_strlen(data));
	wpabuf_printf(buf, "</%s>\n", tag);
}


/* A POST body looks something like (per upnp spec):
 * <?xml version="1.0"?>
 * <s:Envelope
 *     xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"
 *     s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
 *   <s:Body>
 *     <u:actionName xmlns:u="urn:schemas-upnp-org:service:serviceType:v">
 *       <argumentName>in arg value</argumentName>
 *       other in args and their values go here, if any
 *     </u:actionName>
 *   </s:Body>
 * </s:Envelope>
 *
 * where :
 *      s: might be some other namespace name followed by colon
 *      u: might be some other namespace name followed by colon
 *      actionName will be replaced according to action requested
 *      schema following actionName will be WFA scheme instead
 *      argumentName will be actual argument name
 *      (in arg value) will be actual argument value
 */
char * xml_get_first_item(const char *doc, const char *item)
{
	const char *match = item;
	int match_len = os_strlen(item);
	const char *tag, *tagname, *end;
	char *value;

	/*
	 * This is crude: ignore any possible tag name conflicts and go right
	 * to the first tag of this name. This should be ok for the limited
	 * domain of UPnP messages.
	 */
	for (;;) {
		if (xml_next_tag(doc, &tag, &tagname, &end))
			return NULL;
		doc = end;
		if (!os_strncasecmp(tagname, match, match_len) &&
		    *tag != '/' &&
		    (tagname[match_len] == '>' ||
		     !isgraph(tagname[match_len]))) {
			break;
		}
	}
	end = doc;
	while (*end && *end != '<')
		end++;
	value = os_zalloc(1 + (end - doc));
	if (value == NULL)
		return NULL;
	os_memcpy(value, doc, end - doc);
	return value;
}


struct wpabuf * xml_get_base64_item(const char *data, const char *name,
				    enum http_reply_code *ret)
{
	char *msg;
	struct wpabuf *buf;
	unsigned char *decoded;
	size_t len;

	msg = xml_get_first_item(data, name);
	if (msg == NULL) {
		*ret = UPNP_ARG_VALUE_INVALID;
		return NULL;
	}

	decoded = base64_decode((unsigned char *) msg, os_strlen(msg), &len);
	os_free(msg);
	if (decoded == NULL) {
		*ret = UPNP_OUT_OF_MEMORY;
		return NULL;
	}

	buf = wpabuf_alloc_ext_data(decoded, len);
	if (buf == NULL) {
		os_free(decoded);
		*ret = UPNP_OUT_OF_MEMORY;
		return NULL;
	}
	return buf;
}
