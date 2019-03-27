/**
 * str2wire.c - read txt presentation of RRs
 *
 * (c) NLnet Labs, 2005-2006
 *
 * See the file LICENSE for the license
 */

/**
 * \file
 *
 * Parses text to wireformat.
 */
#include "config.h"
#include "sldns/str2wire.h"
#include "sldns/wire2str.h"
#include "sldns/sbuffer.h"
#include "sldns/parse.h"
#include "sldns/parseutil.h"
#include <ctype.h>
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

/** return an error */
#define RET_ERR(e, off) ((int)((e)|((off)<<LDNS_WIREPARSE_SHIFT)))
/** Move parse error but keep its ID */
#define RET_ERR_SHIFT(e, move) RET_ERR(LDNS_WIREPARSE_ERROR(e), LDNS_WIREPARSE_OFFSET(e)+(move));
#define LDNS_IP6ADDRLEN      (128/8)

/*
 * No special care is taken, all dots are translated into
 * label separators.
 * @param rel: true if the domain is not absolute (not terminated in .).
 * 	The output is then still terminated with a '0' rootlabel.
 */
static int sldns_str2wire_dname_buf_rel(const char* str, uint8_t* buf,
	size_t* olen, int* rel)
{
	size_t len;

	const char *s;
	uint8_t *q, *pq, label_len;

	if(rel) *rel = 0;
	len = strlen((char*)str);
	/* octet representation can make strings a lot longer than actual length */
	if (len > LDNS_MAX_DOMAINLEN * 4) {
		return RET_ERR(LDNS_WIREPARSE_ERR_DOMAINNAME_OVERFLOW, 0);
	}
	if (0 == len) {
		return RET_ERR(LDNS_WIREPARSE_ERR_DOMAINNAME_UNDERFLOW, 0);
	}

	/* root label */
	if (1 == len && *str == '.') {
		if(*olen < 1)
			return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL, 0);
		buf[0] = 0;
		*olen = 1;
		return LDNS_WIREPARSE_ERR_OK;
	}

	/* get on with the rest */

	/* s is on the current character in the string
         * pq points to where the labellength is going to go
         * label_len keeps track of the current label's length
	 * q builds the dname inside the buf array
	 */
	len = 0;
	if(*olen < 1)
		return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL, 0);
	q = buf+1;
	pq = buf;
	label_len = 0;
	for (s = str; *s; s++, q++) {
		if (q >= buf + *olen)
			return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL, q-buf);
		if (q > buf + LDNS_MAX_DOMAINLEN)
			return RET_ERR(LDNS_WIREPARSE_ERR_DOMAINNAME_OVERFLOW, q-buf);
		switch (*s) {
		case '.':
			if (label_len > LDNS_MAX_LABELLEN) {
				return RET_ERR(LDNS_WIREPARSE_ERR_LABEL_OVERFLOW, q-buf);
			}
			if (label_len == 0) {
				return RET_ERR(LDNS_WIREPARSE_ERR_EMPTY_LABEL, q-buf);
			}
			len += label_len + 1;
			*q = 0;
			*pq = label_len;
			label_len = 0;
			pq = q;
			break;
		case '\\':
			/* octet value or literal char */
			s += 1;
			if (!sldns_parse_escape(q, &s)) {
				*q = 0;
				return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_BAD_ESCAPE, q-buf);
			}
			s -= 1;
			label_len++;
			break;
		default:
			*q = (uint8_t)*s;
			label_len++;
		}
	}

	/* add root label if last char was not '.' */
	if(label_len != 0) {
		if(rel) *rel = 1;
		if (q >= buf + *olen)
			return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL, q-buf);
		if (q > buf + LDNS_MAX_DOMAINLEN) {
			return RET_ERR(LDNS_WIREPARSE_ERR_DOMAINNAME_OVERFLOW, q-buf);
		}
                if (label_len > LDNS_MAX_LABELLEN) {
                        return RET_ERR(LDNS_WIREPARSE_ERR_LABEL_OVERFLOW, q-buf);
                }
                if (label_len == 0) { /* label_len 0 but not . at end? */
                        return RET_ERR(LDNS_WIREPARSE_ERR_EMPTY_LABEL, q-buf);
                }
		len += label_len + 1;
		*pq = label_len;
		*q = 0;
	}
	len++;
	*olen = len;

	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_dname_buf(const char* str, uint8_t* buf, size_t* len)
{
	return sldns_str2wire_dname_buf_rel(str, buf, len, NULL);
}

int sldns_str2wire_dname_buf_origin(const char* str, uint8_t* buf, size_t* len,
	uint8_t* origin, size_t origin_len)
{
	size_t dlen = *len;
	int rel = 0;
	int s = sldns_str2wire_dname_buf_rel(str, buf, &dlen, &rel);
	if(s) return s;

	if(rel && origin && dlen > 0) {
		if(dlen + origin_len - 1 > LDNS_MAX_DOMAINLEN)
			return RET_ERR(LDNS_WIREPARSE_ERR_DOMAINNAME_OVERFLOW,
				LDNS_MAX_DOMAINLEN);
		if(dlen + origin_len - 1 > *len)
			return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL,
				*len);
		memmove(buf+dlen-1, origin, origin_len);
		*len = dlen + origin_len - 1;
	} else
		*len = dlen;
	return LDNS_WIREPARSE_ERR_OK;
}

uint8_t* sldns_str2wire_dname(const char* str, size_t* len)
{
	uint8_t dname[LDNS_MAX_DOMAINLEN+1];
	*len = sizeof(dname);
	if(sldns_str2wire_dname_buf(str, dname, len) == 0) {
		uint8_t* r = (uint8_t*)malloc(*len);
		if(r) return memcpy(r, dname, *len);
	}
	*len = 0;
	return NULL;
}

/** read owner name */
static int
rrinternal_get_owner(sldns_buffer* strbuf, uint8_t* rr, size_t* len,
	size_t* dname_len, uint8_t* origin, size_t origin_len, uint8_t* prev,
	size_t prev_len, char* token, size_t token_len)
{
	/* split the rr in its parts -1 signals trouble */
	if(sldns_bget_token(strbuf, token, "\t\n ", token_len) == -1) {
		return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX,
			sldns_buffer_position(strbuf));
	}

	if(strcmp(token, "@") == 0) {
		uint8_t* tocopy;
		if (origin) {
			*dname_len = origin_len;
			tocopy = origin;
		} else if (prev) {
			*dname_len = prev_len;
			tocopy = prev;
		} else {
			/* default to root */
			*dname_len = 1;
			tocopy = (uint8_t*)"\0";
		}
		if(*len < *dname_len)
			return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL,
				sldns_buffer_position(strbuf));
		memmove(rr, tocopy, *dname_len);
	} else if(*token == '\0') {
		/* no ownername was given, try prev, if that fails
		 * origin, else default to root */
		uint8_t* tocopy;
		if(prev) {
			*dname_len = prev_len;
			tocopy = prev;
		} else if(origin) {
			*dname_len = origin_len;
			tocopy = origin;
		} else {
			*dname_len = 1;
			tocopy = (uint8_t*)"\0";
		}
		if(*len < *dname_len)
			return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL,
				sldns_buffer_position(strbuf));
		memmove(rr, tocopy, *dname_len);
	} else {
		size_t dlen = *len;
		int s = sldns_str2wire_dname_buf_origin(token, rr, &dlen,
			origin, origin_len);
		if(s) return RET_ERR_SHIFT(s,
			sldns_buffer_position(strbuf)-strlen(token));
		*dname_len = dlen;
	}
	return LDNS_WIREPARSE_ERR_OK;
}

/** read ttl */
static int
rrinternal_get_ttl(sldns_buffer* strbuf, char* token, size_t token_len,
	int* not_there, uint32_t* ttl, uint32_t default_ttl)
{
	const char* endptr;
	if(sldns_bget_token(strbuf, token, "\t\n ", token_len) == -1) {
		return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_TTL,
			sldns_buffer_position(strbuf));
	}
	*ttl = (uint32_t) sldns_str2period(token, &endptr);

	if (strlen(token) > 0 && !isdigit((unsigned char)token[0])) {
		*not_there = 1;
		/* ah, it's not there or something */
		if (default_ttl == 0) {
			*ttl = LDNS_DEFAULT_TTL;
		} else {
			*ttl = default_ttl;
		}
	}
	return LDNS_WIREPARSE_ERR_OK;
}

/** read class */
static int
rrinternal_get_class(sldns_buffer* strbuf, char* token, size_t token_len,
	int* not_there, uint16_t* cl)
{
	/* if 'not_there' then we got token from previous parse routine */
	if(!*not_there) {
		/* parse new token for class */
		if(sldns_bget_token(strbuf, token, "\t\n ", token_len) == -1) {
			return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_CLASS,
				sldns_buffer_position(strbuf));
		}
	} else *not_there = 0;
	*cl = sldns_get_rr_class_by_name(token);
	/* class can be left out too, assume IN, current token must be type */
	if(*cl == 0 && strcmp(token, "CLASS0") != 0) {
		*not_there = 1;
		*cl = LDNS_RR_CLASS_IN;
	}
	return LDNS_WIREPARSE_ERR_OK;
}

/** read type */
static int
rrinternal_get_type(sldns_buffer* strbuf, char* token, size_t token_len,
	int* not_there, uint16_t* tp)
{
	/* if 'not_there' then we got token from previous parse routine */
	if(!*not_there) {
		/* parse new token for type */
		if(sldns_bget_token(strbuf, token, "\t\n ", token_len) == -1) {
			return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_TYPE,
				sldns_buffer_position(strbuf));
		}
	}
	*tp = sldns_get_rr_type_by_name(token);
	if(*tp == 0 && strcmp(token, "TYPE0") != 0) {
		return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_TYPE,
			sldns_buffer_position(strbuf));
	}
	return LDNS_WIREPARSE_ERR_OK;
}

/** put type, class, ttl into rr buffer */
static int
rrinternal_write_typeclassttl(sldns_buffer* strbuf, uint8_t* rr, size_t len,
	size_t dname_len, uint16_t tp, uint16_t cl, uint32_t ttl, int question)
{
	if(question) {
		/* question is : name, type, class */
		if(dname_len + 4 > len)
			return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL,
				sldns_buffer_position(strbuf));
		sldns_write_uint16(rr+dname_len, tp);
		sldns_write_uint16(rr+dname_len+2, cl);
		return LDNS_WIREPARSE_ERR_OK;
	}

	/* type(2), class(2), ttl(4), rdatalen(2 (later)) = 10 */
	if(dname_len + 10 > len)
		return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL,
			sldns_buffer_position(strbuf));
	sldns_write_uint16(rr+dname_len, tp);
	sldns_write_uint16(rr+dname_len+2, cl);
	sldns_write_uint32(rr+dname_len+4, ttl);
	sldns_write_uint16(rr+dname_len+8, 0); /* rdatalen placeholder */
	return LDNS_WIREPARSE_ERR_OK;
}

/** find delimiters for type */
static const char*
rrinternal_get_delims(sldns_rdf_type rdftype, size_t r_cnt, size_t r_max)
{
	switch(rdftype) {
	case LDNS_RDF_TYPE_B64        :
	case LDNS_RDF_TYPE_HEX        : /* These rdf types may con- */
	case LDNS_RDF_TYPE_LOC        : /* tain whitespace, only if */
	case LDNS_RDF_TYPE_WKS        : /* it is the last rd field. */
	case LDNS_RDF_TYPE_IPSECKEY   :
	case LDNS_RDF_TYPE_NSEC       :	if (r_cnt == r_max - 1) {
						return "\n";
					}
					break;
	default                       :	break;
	}
	return "\n\t "; 
}

/* Syntactic sugar for sldns_rr_new_frm_str_internal */
static int
sldns_rdf_type_maybe_quoted(sldns_rdf_type rdf_type)
{
	return  rdf_type == LDNS_RDF_TYPE_STR ||
		rdf_type == LDNS_RDF_TYPE_LONG_STR;
}

/** see if rdata is quoted */
static int
rrinternal_get_quoted(sldns_buffer* strbuf, const char** delimiters,
	sldns_rdf_type rdftype)
{
	if(sldns_rdf_type_maybe_quoted(rdftype) &&
		sldns_buffer_remaining(strbuf) > 0) {

		/* skip spaces */
		while(sldns_buffer_remaining(strbuf) > 0 &&
			*(sldns_buffer_current(strbuf)) == ' ') {
			sldns_buffer_skip(strbuf, 1);
		}

		if(sldns_buffer_remaining(strbuf) > 0 &&
			*(sldns_buffer_current(strbuf)) == '\"') {
			*delimiters = "\"\0";
			sldns_buffer_skip(strbuf, 1);
			return 1;
		}
	}
	return 0;
}

/** spool hex data into rdata */
static int
rrinternal_spool_hex(char* token, uint8_t* rr, size_t rr_len,
	size_t rr_cur_len, size_t* cur_hex_data_size, size_t hex_data_size)
{
	char* p = token;
	while(*p) {
		if(isspace((unsigned char)*p)) {
			p++;
			continue;
		}
		if(!isxdigit((unsigned char)*p))
			return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_RDATA,
				p-token);
		if(*cur_hex_data_size >= hex_data_size)
			return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_RDATA,
				p-token);
		/* extra robust check */
		if(rr_cur_len+(*cur_hex_data_size)/2 >= rr_len)
			return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL,
				p-token);
		/* see if 16s or 1s */
		if( ((*cur_hex_data_size)&1) == 0) {
			rr[rr_cur_len+(*cur_hex_data_size)/2] =
				(uint8_t)sldns_hexdigit_to_int(*p)*16;
		} else {
			rr[rr_cur_len+(*cur_hex_data_size)/2] +=
				(uint8_t)sldns_hexdigit_to_int(*p);
		}
		p++;
		(*cur_hex_data_size)++;
	}
	return LDNS_WIREPARSE_ERR_OK;
}

/** read unknown rr type format */
static int
rrinternal_parse_unknown(sldns_buffer* strbuf, char* token, size_t token_len,
        uint8_t* rr, size_t* rr_len, size_t* rr_cur_len, size_t pre_data_pos)
{
	const char* delim = "\n\t ";
	size_t hex_data_size, cur_hex_data_size;
	/* go back to before \#
	 * and skip it while setting delimiters better
	 */
	sldns_buffer_set_position(strbuf, pre_data_pos);
	if(sldns_bget_token(strbuf, token, delim, token_len) == -1)
		return LDNS_WIREPARSE_ERR_GENERAL; /* should not fail */
	/* read rdata octet length */
	if(sldns_bget_token(strbuf, token, delim, token_len) == -1) {
		/* something goes very wrong here */
		return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_RDATA,
			sldns_buffer_position(strbuf));
	}
	hex_data_size = (size_t)atoi(token);
	if(hex_data_size > LDNS_MAX_RDFLEN || 
		*rr_cur_len + hex_data_size > *rr_len) {
		return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL,
			sldns_buffer_position(strbuf));
	}
	/* copy hex chars into hex str (2 chars per byte) */
	hex_data_size *= 2;
	cur_hex_data_size = 0;
	while(cur_hex_data_size < hex_data_size) {
		int status;
		ssize_t c = sldns_bget_token(strbuf, token, delim, token_len);
		if((status = rrinternal_spool_hex(token, rr, *rr_len,
			*rr_cur_len, &cur_hex_data_size, hex_data_size)) != 0)
			return RET_ERR_SHIFT(status,
				sldns_buffer_position(strbuf)-strlen(token));
		if(c == -1) {
			if(cur_hex_data_size != hex_data_size)
				return RET_ERR(
					LDNS_WIREPARSE_ERR_SYNTAX_RDATA,
					sldns_buffer_position(strbuf));
			break;
		}
	}
	*rr_cur_len += hex_data_size/2;
	return LDNS_WIREPARSE_ERR_OK;
}

/** parse normal RR rdata element */
static int
rrinternal_parse_rdf(sldns_buffer* strbuf, char* token, size_t token_len,
	uint8_t* rr, size_t rr_len, size_t* rr_cur_len, sldns_rdf_type rdftype,
	uint16_t rr_type, size_t r_cnt, size_t r_max, size_t dname_len,
	uint8_t* origin, size_t origin_len)
{
	size_t len;
	int status;

	switch(rdftype) {
	case LDNS_RDF_TYPE_DNAME:
		/* check if the origin should be used or concatenated */
		if(strcmp(token, "@") == 0) {
			uint8_t* tocopy;
			size_t copylen;
			if(origin) {
				copylen = origin_len;
				tocopy = origin;
			} else if(rr_type == LDNS_RR_TYPE_SOA) {
				copylen = dname_len;
				tocopy = rr; /* copy rr owner name */
			} else {
				copylen = 1;
				tocopy = (uint8_t*)"\0";
			}
			if((*rr_cur_len) + copylen > rr_len)
				return RET_ERR(
					LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL,
					sldns_buffer_position(strbuf));
			memmove(rr+*rr_cur_len, tocopy, copylen);
			(*rr_cur_len) += copylen;
		} else {
			size_t dlen = rr_len - (*rr_cur_len);
			int s = sldns_str2wire_dname_buf_origin(token,
				rr+*rr_cur_len, &dlen, origin, origin_len);
			if(s) return RET_ERR_SHIFT(s,
				sldns_buffer_position(strbuf)-strlen(token));
			(*rr_cur_len) += dlen;
		}
		return LDNS_WIREPARSE_ERR_OK;

	case LDNS_RDF_TYPE_HEX:
	case LDNS_RDF_TYPE_B64:
		/* When this is the last rdata field, then the
		 * rest should be read in (cause then these
		 * rdf types may contain spaces). */
		if(r_cnt == r_max - 1) {
			size_t tlen = strlen(token);
			(void)sldns_bget_token(strbuf, token+tlen, "\n",
				token_len - tlen);
		}
		break;
	default:
		break;
	}

	len = rr_len - (*rr_cur_len);
	if((status=sldns_str2wire_rdf_buf(token, rr+(*rr_cur_len), &len,
		rdftype)) != 0)
		return RET_ERR_SHIFT(status,
			sldns_buffer_position(strbuf)-strlen(token));
	*rr_cur_len += len;
	return LDNS_WIREPARSE_ERR_OK;
}

/**
 * Parse one rdf token.  Takes care of quotes and parenthesis.
 */
static int
sldns_parse_rdf_token(sldns_buffer* strbuf, char* token, size_t token_len,
	int* quoted, int* parens, size_t* pre_data_pos,
	const char* delimiters, sldns_rdf_type rdftype, size_t* token_strlen)
{
	size_t slen;

	/* skip spaces */
	while(sldns_buffer_remaining(strbuf) > 0 && !*quoted &&
		*(sldns_buffer_current(strbuf)) == ' ') {
		sldns_buffer_skip(strbuf, 1);
	}

	*pre_data_pos = sldns_buffer_position(strbuf);
	if(sldns_bget_token_par(strbuf, token, (*quoted)?"\"":delimiters,
		token_len, parens, (*quoted)?NULL:" \t") == -1) {
		return 0;
	}
	slen = strlen(token);
	/* check if not quoted yet, and we have encountered quotes */
	if(!*quoted && sldns_rdf_type_maybe_quoted(rdftype) &&
		slen >= 2 &&
		(token[0] == '"' || token[0] == '\'') && 
		(token[slen-1] == '"' || token[slen-1] == '\'')) {
		/* move token two smaller (quotes) with endnull */
		memmove(token, token+1, slen-2);
		token[slen-2] = 0;
		slen -= 2;
		*quoted = 1;
	} else if(!*quoted && sldns_rdf_type_maybe_quoted(rdftype) &&
		slen >= 2 &&
		(token[0] == '"' || token[0] == '\'')) {
		/* got the start quote (remove it) but read remainder
		 * of quoted string as well into remainder of token */
		memmove(token, token+1, slen-1);
		token[slen-1] = 0;
		slen -= 1;
		*quoted = 1;
		/* rewind buffer over skipped whitespace */
		while(sldns_buffer_position(strbuf) > 0 &&
			(sldns_buffer_current(strbuf)[-1] == ' ' ||
			sldns_buffer_current(strbuf)[-1] == '\t')) {
			sldns_buffer_skip(strbuf, -1);
		}
		if(sldns_bget_token_par(strbuf, token+slen,
			"\"", token_len-slen,
			parens, NULL) == -1) {
			return 0;
		}
		slen = strlen(token);
	}
	*token_strlen = slen;
	return 1;
}

/** Add space and one more rdf token onto the existing token string. */
static int
sldns_affix_token(sldns_buffer* strbuf, char* token, size_t* token_len,
	int* quoted, int* parens, size_t* pre_data_pos,
	const char* delimiters, sldns_rdf_type rdftype, size_t* token_strlen)
{
	size_t addlen = *token_len - *token_strlen;
	size_t addstrlen = 0;

	/* add space */
	if(addlen < 1) return 0;
	token[*token_strlen] = ' ';
	token[++(*token_strlen)] = 0;

	/* read another token */
	addlen = *token_len - *token_strlen;
	if(!sldns_parse_rdf_token(strbuf, token+*token_strlen, addlen, quoted,
		parens, pre_data_pos, delimiters, rdftype, &addstrlen))
		return 0;
	(*token_strlen) += addstrlen;
	return 1;
}

/** parse rdata from string into rr buffer(-remainder after dname). */
static int
rrinternal_parse_rdata(sldns_buffer* strbuf, char* token, size_t token_len,
	uint8_t* rr, size_t* rr_len, size_t dname_len, uint16_t rr_type,
	uint8_t* origin, size_t origin_len)
{
	const sldns_rr_descriptor *desc = sldns_rr_descript((uint16_t)rr_type);
	size_t r_cnt, r_min, r_max;
	size_t rr_cur_len = dname_len + 10, pre_data_pos, token_strlen;
	int was_unknown_rr_format = 0, parens = 0, status, quoted;
	const char* delimiters;
	sldns_rdf_type rdftype;
	/* a desc is always returned */
	if(!desc) return LDNS_WIREPARSE_ERR_GENERAL;
	r_max = sldns_rr_descriptor_maximum(desc);
	r_min = sldns_rr_descriptor_minimum(desc);
	/* robust check */
	if(rr_cur_len > *rr_len)
		return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL,
			sldns_buffer_position(strbuf));

	/* because number of fields can be variable, we can't rely on
	 * _maximum() only */
	for(r_cnt=0; r_cnt < r_max; r_cnt++) {
		rdftype = sldns_rr_descriptor_field_type(desc, r_cnt);
		delimiters = rrinternal_get_delims(rdftype, r_cnt, r_max);
		quoted = rrinternal_get_quoted(strbuf, &delimiters, rdftype);

		if(!sldns_parse_rdf_token(strbuf, token, token_len, &quoted,
			&parens, &pre_data_pos, delimiters, rdftype,
			&token_strlen))
			break;

		/* rfc3597 specifies that any type can be represented
		 * with \# method, which can contain spaces...
		 * it does specify size though... */

		/* unknown RR data */
		if(token_strlen>=2 && strncmp(token, "\\#", 2) == 0 &&
			!quoted && (token_strlen == 2 || token[2]==' ')) {
			was_unknown_rr_format = 1;
			if((status=rrinternal_parse_unknown(strbuf, token,
				token_len, rr, rr_len, &rr_cur_len, 
				pre_data_pos)) != 0)
				return status;
		} else if(token_strlen > 0 || quoted) {
			if(rdftype == LDNS_RDF_TYPE_HIP) {
				/* affix the HIT and PK fields, with a space */
				if(!sldns_affix_token(strbuf, token,
					&token_len, &quoted, &parens,
					&pre_data_pos, delimiters,
					rdftype, &token_strlen))
					break;
				if(!sldns_affix_token(strbuf, token,
					&token_len, &quoted, &parens,
					&pre_data_pos, delimiters,
					rdftype, &token_strlen))
					break;
			} else if(rdftype == LDNS_RDF_TYPE_INT16_DATA &&
				strcmp(token, "0")!=0) {
				/* affix len and b64 fields */
				if(!sldns_affix_token(strbuf, token,
					&token_len, &quoted, &parens,
					&pre_data_pos, delimiters,
					rdftype, &token_strlen))
					break;
			}

			/* normal RR */
			if((status=rrinternal_parse_rdf(strbuf, token,
				token_len, rr, *rr_len, &rr_cur_len, rdftype,
				rr_type, r_cnt, r_max, dname_len, origin,
				origin_len)) != 0) {
				return status;
			}
		}
	}
	if(!was_unknown_rr_format && r_cnt+1 < r_min) {
		return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_MISSING_VALUE,
			sldns_buffer_position(strbuf));
	}
	while(parens != 0) {
		/* read remainder, must be "" */
		if(sldns_bget_token_par(strbuf, token, "\n", token_len,
			&parens, " \t") == -1) {
			if(parens != 0)
				return RET_ERR(LDNS_WIREPARSE_ERR_PARENTHESIS,
					sldns_buffer_position(strbuf));
			break;
		}
		if(strcmp(token, "") != 0)
			return RET_ERR(LDNS_WIREPARSE_ERR_PARENTHESIS,
				sldns_buffer_position(strbuf));
	}
	/* write rdata length */
	sldns_write_uint16(rr+dname_len+8, (uint16_t)(rr_cur_len-dname_len-10));
	*rr_len = rr_cur_len;
	return LDNS_WIREPARSE_ERR_OK;
}

/*
 * trailing spaces are allowed
 * leading spaces are not allowed
 * allow ttl to be optional
 * class is optional too
 * if ttl is missing, and default_ttl is 0, use DEF_TTL
 * allow ttl to be written as 1d3h
 * So the RR should look like. e.g.
 * miek.nl. 3600 IN MX 10 elektron.atoom.net
 * or
 * miek.nl. 1h IN MX 10 elektron.atoom.net
 * or
 * miek.nl. IN MX 10 elektron.atoom.net
 */
static int
sldns_str2wire_rr_buf_internal(const char* str, uint8_t* rr, size_t* len,
	size_t* dname_len, uint32_t default_ttl, uint8_t* origin,
	size_t origin_len, uint8_t* prev, size_t prev_len, int question)
{
	int status;
	int not_there = 0;
	char token[LDNS_MAX_RDFLEN+1];
	uint32_t ttl = 0;
	uint16_t tp = 0, cl = 0;
	size_t ddlen = 0;

	/* string in buffer */
	sldns_buffer strbuf;
	sldns_buffer_init_frm_data(&strbuf, (uint8_t*)str, strlen(str));
	if(!dname_len) dname_len = &ddlen;

	/* parse the owner */
	if((status=rrinternal_get_owner(&strbuf, rr, len, dname_len, origin,
		origin_len, prev, prev_len, token, sizeof(token))) != 0)
		return status;

	/* parse the [ttl] [class] <type> */
	if((status=rrinternal_get_ttl(&strbuf, token, sizeof(token),
		&not_there, &ttl, default_ttl)) != 0)
		return status;
	if((status=rrinternal_get_class(&strbuf, token, sizeof(token),
		&not_there, &cl)) != 0)
		return status;
	if((status=rrinternal_get_type(&strbuf, token, sizeof(token),
		&not_there, &tp)) != 0)
		return status;
	/* put ttl, class, type into the rr result */
	if((status=rrinternal_write_typeclassttl(&strbuf, rr, *len, *dname_len, tp, cl,
		ttl, question)) != 0)
		return status;
	/* for a question-RR we are done, no rdata */
	if(question) {
		*len = *dname_len + 4;
		return LDNS_WIREPARSE_ERR_OK;
	}

	/* rdata */
	if((status=rrinternal_parse_rdata(&strbuf, token, sizeof(token),
		rr, len, *dname_len, tp, origin, origin_len)) != 0)
		return status;

	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_rr_buf(const char* str, uint8_t* rr, size_t* len,
	size_t* dname_len, uint32_t default_ttl, uint8_t* origin,
	size_t origin_len, uint8_t* prev, size_t prev_len)
{
	return sldns_str2wire_rr_buf_internal(str, rr, len, dname_len,
		default_ttl, origin, origin_len, prev, prev_len, 0);
}

int sldns_str2wire_rr_question_buf(const char* str, uint8_t* rr, size_t* len,
	size_t* dname_len, uint8_t* origin, size_t origin_len, uint8_t* prev,
	size_t prev_len)
{
	return sldns_str2wire_rr_buf_internal(str, rr, len, dname_len,
		0, origin, origin_len, prev, prev_len, 1);
}

uint16_t sldns_wirerr_get_type(uint8_t* rr, size_t len, size_t dname_len)
{
	if(len < dname_len+2)
		return 0;
	return sldns_read_uint16(rr+dname_len);
}

uint16_t sldns_wirerr_get_class(uint8_t* rr, size_t len, size_t dname_len)
{
	if(len < dname_len+4)
		return 0;
	return sldns_read_uint16(rr+dname_len+2);
}

uint32_t sldns_wirerr_get_ttl(uint8_t* rr, size_t len, size_t dname_len)
{
	if(len < dname_len+8)
		return 0;
	return sldns_read_uint32(rr+dname_len+4);
}

uint16_t sldns_wirerr_get_rdatalen(uint8_t* rr, size_t len, size_t dname_len)
{
	if(len < dname_len+10)
		return 0;
	return sldns_read_uint16(rr+dname_len+8);
}

uint8_t* sldns_wirerr_get_rdata(uint8_t* rr, size_t len, size_t dname_len)
{
	if(len < dname_len+10)
		return NULL;
	return rr+dname_len+10;
}

uint8_t* sldns_wirerr_get_rdatawl(uint8_t* rr, size_t len, size_t dname_len)
{
	if(len < dname_len+10)
		return NULL;
	return rr+dname_len+8;
}

const char* sldns_get_errorstr_parse(int e)
{
	sldns_lookup_table *lt;
	lt = sldns_lookup_by_id(sldns_wireparse_errors, LDNS_WIREPARSE_ERROR(e));
	return lt?lt->name:"unknown error";
}

/* Strip whitespace from the start and the end of <line>.  */
char *
sldns_strip_ws(char *line)
{
        char *s = line, *e;

        for (s = line; *s && isspace((unsigned char)*s); s++)
                ;
        for (e = strchr(s, 0); e > s+2 && isspace((unsigned char)e[-1]) && e[-2] != '\\'; e--)
                ;
        *e = 0;
        return s;
}

int sldns_fp2wire_rr_buf(FILE* in, uint8_t* rr, size_t* len, size_t* dname_len,
	struct sldns_file_parse_state* parse_state)
{
	char line[LDNS_RR_BUF_SIZE+1];
	ssize_t size;

	/* read an entire line in from the file */
	if((size = sldns_fget_token_l(in, line, LDNS_PARSE_SKIP_SPACE,
		LDNS_RR_BUF_SIZE, parse_state?&parse_state->lineno:NULL))
		== -1) {
		/* if last line was empty, we are now at feof, which is not
		 * always a parse error (happens when for instance last line
		 * was a comment)
		 */
		return LDNS_WIREPARSE_ERR_SYNTAX;
	}

	/* we can have the situation, where we've read ok, but still got
	 * no bytes to play with, in this case size is 0 */
	if(size == 0) {
		if(*len > 0)
			rr[0] = 0;
		*len = 0;
		*dname_len = 0;
		return LDNS_WIREPARSE_ERR_OK;
	}

	if(strncmp(line, "$ORIGIN", 7) == 0 && isspace((unsigned char)line[7])) {
		int s;
		strlcpy((char*)rr, line, *len);
		*len = 0;
		*dname_len = 0;
		if(!parse_state) return LDNS_WIREPARSE_ERR_OK;
		parse_state->origin_len = sizeof(parse_state->origin);
		s = sldns_str2wire_dname_buf(sldns_strip_ws(line+8),
			parse_state->origin, &parse_state->origin_len);
		if(s) parse_state->origin_len = 0;
		return s;
	} else if(strncmp(line, "$TTL", 4) == 0 && isspace((unsigned char)line[4])) {
		const char* end = NULL;
		strlcpy((char*)rr, line, *len);
		*len = 0;
		*dname_len = 0;
		if(!parse_state) return LDNS_WIREPARSE_ERR_OK;
		parse_state->default_ttl = sldns_str2period(
			sldns_strip_ws(line+5), &end);
	} else if (strncmp(line, "$INCLUDE", 8) == 0) {
		strlcpy((char*)rr, line, *len);
		*len = 0;
		*dname_len = 0;
		return LDNS_WIREPARSE_ERR_INCLUDE;
	} else if (strncmp(line, "$", 1) == 0) {
		strlcpy((char*)rr, line, *len);
		*len = 0;
		*dname_len = 0;
		return LDNS_WIREPARSE_ERR_INCLUDE;
	} else {
		int r = sldns_str2wire_rr_buf(line, rr, len, dname_len,
			parse_state?parse_state->default_ttl:0,
			(parse_state&&parse_state->origin_len)?
				parse_state->origin:NULL,
			parse_state?parse_state->origin_len:0,
			(parse_state&&parse_state->prev_rr_len)?
				parse_state->prev_rr:NULL,
			parse_state?parse_state->prev_rr_len:0);
		if(r == LDNS_WIREPARSE_ERR_OK && (*dname_len) != 0 &&
			parse_state &&
			(*dname_len) <= sizeof(parse_state->prev_rr)) {
			memmove(parse_state->prev_rr, rr, *dname_len);
			parse_state->prev_rr_len = (*dname_len);
		}
		return r;
	}
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_rdf_buf(const char* str, uint8_t* rd, size_t* len,
	sldns_rdf_type rdftype)
{
	switch (rdftype) {
	case LDNS_RDF_TYPE_DNAME:
		return sldns_str2wire_dname_buf(str, rd, len);
	case LDNS_RDF_TYPE_INT8:
		return sldns_str2wire_int8_buf(str, rd, len);
	case LDNS_RDF_TYPE_INT16:
		return sldns_str2wire_int16_buf(str, rd, len);
	case LDNS_RDF_TYPE_INT32:
		return sldns_str2wire_int32_buf(str, rd, len);
	case LDNS_RDF_TYPE_A:
		return sldns_str2wire_a_buf(str, rd, len);
	case LDNS_RDF_TYPE_AAAA:
		return sldns_str2wire_aaaa_buf(str, rd, len);
	case LDNS_RDF_TYPE_STR:
		return sldns_str2wire_str_buf(str, rd, len);
	case LDNS_RDF_TYPE_APL:
		return sldns_str2wire_apl_buf(str, rd, len);
	case LDNS_RDF_TYPE_B64:
		return sldns_str2wire_b64_buf(str, rd, len);
	case LDNS_RDF_TYPE_B32_EXT:
		return sldns_str2wire_b32_ext_buf(str, rd, len);
	case LDNS_RDF_TYPE_HEX:
		return sldns_str2wire_hex_buf(str, rd, len);
	case LDNS_RDF_TYPE_NSEC:
		return sldns_str2wire_nsec_buf(str, rd, len);
	case LDNS_RDF_TYPE_TYPE:
		return sldns_str2wire_type_buf(str, rd, len);
	case LDNS_RDF_TYPE_CLASS:
		return sldns_str2wire_class_buf(str, rd, len);
	case LDNS_RDF_TYPE_CERT_ALG:
		return sldns_str2wire_cert_alg_buf(str, rd, len);
	case LDNS_RDF_TYPE_ALG:
		return sldns_str2wire_alg_buf(str, rd, len);
	case LDNS_RDF_TYPE_TIME:
		return sldns_str2wire_time_buf(str, rd, len);
	case LDNS_RDF_TYPE_PERIOD:
		return sldns_str2wire_period_buf(str, rd, len);
	case LDNS_RDF_TYPE_TSIGTIME:
		return sldns_str2wire_tsigtime_buf(str, rd, len);
	case LDNS_RDF_TYPE_LOC:
		return sldns_str2wire_loc_buf(str, rd, len);
	case LDNS_RDF_TYPE_WKS:
		return sldns_str2wire_wks_buf(str, rd, len);
	case LDNS_RDF_TYPE_NSAP:
		return sldns_str2wire_nsap_buf(str, rd, len);
	case LDNS_RDF_TYPE_ATMA:
		return sldns_str2wire_atma_buf(str, rd, len);
	case LDNS_RDF_TYPE_IPSECKEY:
		return sldns_str2wire_ipseckey_buf(str, rd, len);
	case LDNS_RDF_TYPE_NSEC3_SALT:
		return sldns_str2wire_nsec3_salt_buf(str, rd, len);
	case LDNS_RDF_TYPE_NSEC3_NEXT_OWNER:
		return sldns_str2wire_b32_ext_buf(str, rd, len);
	case LDNS_RDF_TYPE_ILNP64:
		return sldns_str2wire_ilnp64_buf(str, rd, len);
	case LDNS_RDF_TYPE_EUI48:
		return sldns_str2wire_eui48_buf(str, rd, len);
	case LDNS_RDF_TYPE_EUI64:
		return sldns_str2wire_eui64_buf(str, rd, len);
	case LDNS_RDF_TYPE_TAG:
		return sldns_str2wire_tag_buf(str, rd, len);
	case LDNS_RDF_TYPE_LONG_STR:
		return sldns_str2wire_long_str_buf(str, rd, len);
	case LDNS_RDF_TYPE_TSIGERROR:
		return sldns_str2wire_tsigerror_buf(str, rd, len);
	case LDNS_RDF_TYPE_HIP:
		return sldns_str2wire_hip_buf(str, rd, len);
	case LDNS_RDF_TYPE_INT16_DATA:
		return sldns_str2wire_int16_data_buf(str, rd, len);
	case LDNS_RDF_TYPE_UNKNOWN:
	case LDNS_RDF_TYPE_SERVICE:
		return LDNS_WIREPARSE_ERR_NOT_IMPL;
	case LDNS_RDF_TYPE_NONE:
	default:
		break;
	}
	return LDNS_WIREPARSE_ERR_GENERAL;
}

int sldns_str2wire_int8_buf(const char* str, uint8_t* rd, size_t* len)
{
	char* end;
	uint8_t r = (uint8_t)strtol((char*)str, &end, 10);
	if(*end != 0)
		return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_INT, end-(char*)str);
	if(*len < 1)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	rd[0] = r;
	*len = 1;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_int16_buf(const char* str, uint8_t* rd, size_t* len)
{
	char* end;
	uint16_t r = (uint16_t)strtol((char*)str, &end, 10);
	if(*end != 0)
		return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_INT, end-(char*)str);
	if(*len < 2)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	sldns_write_uint16(rd, r);
	*len = 2;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_int32_buf(const char* str, uint8_t* rd, size_t* len)
{
	char* end;
	uint32_t r;
	errno = 0; /* must set to zero before call,
			note race condition on errno */
	if(*str == '-')
		r = (uint32_t)strtol((char*)str, &end, 10);
	else	r = (uint32_t)strtoul((char*)str, &end, 10);
	if(*end != 0)
		return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_INT, end-(char*)str);
	if(errno == ERANGE)
		return LDNS_WIREPARSE_ERR_SYNTAX_INTEGER_OVERFLOW;
	if(*len < 4)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	sldns_write_uint32(rd, r);
	*len = 4;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_a_buf(const char* str, uint8_t* rd, size_t* len)
{
	struct in_addr address;
	if(inet_pton(AF_INET, (char*)str, &address) != 1)
		return LDNS_WIREPARSE_ERR_SYNTAX_IP4;
	if(*len < sizeof(address))
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	memmove(rd, &address, sizeof(address));
	*len = sizeof(address);
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_aaaa_buf(const char* str, uint8_t* rd, size_t* len)
{
#ifdef AF_INET6
	uint8_t address[LDNS_IP6ADDRLEN + 1];
	if(inet_pton(AF_INET6, (char*)str, address) != 1)
		return LDNS_WIREPARSE_ERR_SYNTAX_IP6;
	if(*len < LDNS_IP6ADDRLEN)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	memmove(rd, address, LDNS_IP6ADDRLEN);
	*len = LDNS_IP6ADDRLEN;
	return LDNS_WIREPARSE_ERR_OK;
#else
	return LDNS_WIREPARSE_ERR_NOT_IMPL;
#endif
}

int sldns_str2wire_str_buf(const char* str, uint8_t* rd, size_t* len)
{
	uint8_t ch = 0;
	size_t sl = 0;
	const char* s = str;
	/* skip length byte */
	if(*len < 1)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;

	/* read characters */
	while(sldns_parse_char(&ch, &s)) {
		if(sl >= 255)
			return RET_ERR(LDNS_WIREPARSE_ERR_INVALID_STR, s-str);
		if(*len < sl+1)
			return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL,
				s-str);
		rd[++sl] = ch;
	}
	if(!s)
		return LDNS_WIREPARSE_ERR_SYNTAX_BAD_ESCAPE;
	rd[0] = (uint8_t)sl;
	*len = sl+1;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_apl_buf(const char* str, uint8_t* rd, size_t* len)
{
	const char *my_str = str;

	char my_ip_str[64];
	size_t ip_str_len;

	uint16_t family;
	int negation;
	size_t adflength = 0;
	uint8_t data[16+4];
	uint8_t prefix;
	size_t i;

	if(*my_str == '\0') {
		/* empty APL element, no data, no string */
		*len = 0;
		return LDNS_WIREPARSE_ERR_OK;
	}

	/* [!]afi:address/prefix */
	if (strlen(my_str) < 2
			|| strchr(my_str, ':') == NULL
			|| strchr(my_str, '/') == NULL
			|| strchr(my_str, ':') > strchr(my_str, '/')) {
		return LDNS_WIREPARSE_ERR_INVALID_STR;
	}

	if (my_str[0] == '!') {
		negation = 1;
		my_str += 1;
	} else {
		negation = 0;
	}

	family = (uint16_t) atoi(my_str);

	my_str = strchr(my_str, ':') + 1;

	/* need ip addr and only ip addr for inet_pton */
	ip_str_len = (size_t) (strchr(my_str, '/') - my_str);
	if(ip_str_len+1 > sizeof(my_ip_str))
		return LDNS_WIREPARSE_ERR_INVALID_STR;
	(void)strlcpy(my_ip_str, my_str, sizeof(my_ip_str));
	my_ip_str[ip_str_len] = 0;

	if (family == 1) {
		/* ipv4 */
		if(inet_pton(AF_INET, my_ip_str, data+4) == 0)
			return LDNS_WIREPARSE_ERR_INVALID_STR;
		for (i = 0; i < 4; i++) {
			if (data[i+4] != 0) {
				adflength = i + 1;
			}
		}
	} else if (family == 2) {
		/* ipv6 */
		if (inet_pton(AF_INET6, my_ip_str, data+4) == 0)
			return LDNS_WIREPARSE_ERR_INVALID_STR;
		for (i = 0; i < 16; i++) {
			if (data[i+4] != 0) {
				adflength = i + 1;
			}
		}
	} else {
		/* unknown family */
		return LDNS_WIREPARSE_ERR_INVALID_STR;
	}

	my_str = strchr(my_str, '/') + 1;
	prefix = (uint8_t) atoi(my_str);

	sldns_write_uint16(data, family);
	data[2] = prefix;
	data[3] = (uint8_t)adflength;
	if (negation) {
		/* set bit 1 of byte 3 */
		data[3] = data[3] | 0x80;
	}

	if(*len < 4+adflength)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	memmove(rd, data, 4+adflength);
	*len = 4+adflength;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_b64_buf(const char* str, uint8_t* rd, size_t* len)
{
	size_t sz = sldns_b64_pton_calculate_size(strlen(str));
	int n;
	if(strcmp(str, "0") == 0) {
		*len = 0;
		return LDNS_WIREPARSE_ERR_OK;
	}
	if(*len < sz)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	n = sldns_b64_pton(str, rd, *len);
	if(n < 0)
		return LDNS_WIREPARSE_ERR_SYNTAX_B64;
	*len = (size_t)n;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_b32_ext_buf(const char* str, uint8_t* rd, size_t* len)
{
	size_t slen = strlen(str);
	size_t sz = sldns_b32_pton_calculate_size(slen);
	int n;
	if(*len < 1+sz)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	rd[0] = (uint8_t)sz;
	n = sldns_b32_pton_extended_hex(str, slen, rd+1, *len-1);
	if(n < 0)
		return LDNS_WIREPARSE_ERR_SYNTAX_B32_EXT;
	*len = (size_t)n+1;
	return LDNS_WIREPARSE_ERR_OK;
}

/** see if the string ends, or ends in whitespace */
static int
sldns_is_last_of_string(const char* str)
{
	if(*str == 0) return 1;
	while(isspace((unsigned char)*str))
		str++;
	if(*str == 0) return 1;
	return 0;
}

int sldns_str2wire_hex_buf(const char* str, uint8_t* rd, size_t* len)
{
	const char* s = str;
	size_t dlen = 0; /* number of hexdigits parsed */
	while(*s) {
		if(isspace((unsigned char)*s)) {
			s++;
			continue;
		}
		if(dlen == 0 && *s == '0' && sldns_is_last_of_string(s+1)) {
			*len = 0;
			return LDNS_WIREPARSE_ERR_OK;
		}
		if(!isxdigit((unsigned char)*s))
			return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_HEX, s-str);
		if(*len < dlen/2 + 1)
			return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL,
				s-str);
		if((dlen&1)==0)
			rd[dlen/2] = (uint8_t)sldns_hexdigit_to_int(*s++) * 16;
		else	rd[dlen/2] += (uint8_t)sldns_hexdigit_to_int(*s++);
		dlen++;
	}
	if((dlen&1)!=0)
		return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_HEX, s-str);
	*len = dlen/2;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_nsec_buf(const char* str, uint8_t* rd, size_t* len)
{
	const char *delim = "\n\t ";
	char token[64]; /* for a type name */
	size_t type_count = 0;
	int block;
	size_t used = 0;
	uint16_t maxtype = 0;
	uint8_t typebits[8192]; /* 65536 bits */
	uint8_t window_in_use[256];

	/* string in buffer */
	sldns_buffer strbuf;
	sldns_buffer_init_frm_data(&strbuf, (uint8_t*)str, strlen(str));

	/* parse the types */
	memset(typebits, 0, sizeof(typebits));
	memset(window_in_use, 0, sizeof(window_in_use));
	while(sldns_buffer_remaining(&strbuf) > 0 &&
		sldns_bget_token(&strbuf, token, delim, sizeof(token)) != -1) {
		uint16_t t = sldns_get_rr_type_by_name(token);
		if(token[0] == 0)
			continue;
		if(t == 0 && strcmp(token, "TYPE0") != 0)
			return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_TYPE,
				sldns_buffer_position(&strbuf));
		typebits[t/8] |= (0x80>>(t%8));
		window_in_use[t/256] = 1;
		type_count++;
		if(t > maxtype) maxtype = t;
	}

	/* empty NSEC bitmap */
	if(type_count == 0) {
		*len = 0;
		return LDNS_WIREPARSE_ERR_OK;
	}

	/* encode windows {u8 windowblock, u8 bitmaplength, 0-32u8 bitmap},
	 * block is 0-255 upper octet of types, length if 0-32. */
	for(block = 0; block <= (int)maxtype/256; block++) {
		int i, blocklen = 0;
		if(!window_in_use[block])
			continue;
		for(i=0; i<32; i++) {
			if(typebits[block*32+i] != 0)
				blocklen = i+1;
		}
		if(blocklen == 0)
			continue; /* empty window should have been !in_use */
		if(used+blocklen+2 > *len)
			return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
		rd[used+0] = (uint8_t)block;
		rd[used+1] = (uint8_t)blocklen;
		for(i=0; i<blocklen; i++) {
			rd[used+2+i] = typebits[block*32+i];
		}
		used += blocklen+2;
	}
	*len = used;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_type_buf(const char* str, uint8_t* rd, size_t* len)
{
	uint16_t t = sldns_get_rr_type_by_name(str);
	if(t == 0 && strcmp(str, "TYPE0") != 0)
		return LDNS_WIREPARSE_ERR_SYNTAX_TYPE;
	if(*len < 2)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	sldns_write_uint16(rd, t);
	*len = 2;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_class_buf(const char* str, uint8_t* rd, size_t* len)
{
	uint16_t c = sldns_get_rr_class_by_name(str);
	if(c == 0 && strcmp(str, "CLASS0") != 0)
		return LDNS_WIREPARSE_ERR_SYNTAX_CLASS;
	if(*len < 2)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	sldns_write_uint16(rd, c);
	*len = 2;
	return LDNS_WIREPARSE_ERR_OK;
}

/* An certificate alg field can either be specified as a 8 bits number
 * or by its symbolic name. Handle both */
int sldns_str2wire_cert_alg_buf(const char* str, uint8_t* rd, size_t* len)
{
	sldns_lookup_table *lt = sldns_lookup_by_name(sldns_cert_algorithms,
		str);
	if(*len < 2)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	if(lt) {
		sldns_write_uint16(rd, (uint16_t)lt->id);
	} else {
		int s = sldns_str2wire_int16_buf(str, rd, len);
		if(s) return s;
		if(sldns_read_uint16(rd) == 0)
			return LDNS_WIREPARSE_ERR_CERT_BAD_ALGORITHM;
	}
	*len = 2;
	return LDNS_WIREPARSE_ERR_OK;
}

/* An alg field can either be specified as a 8 bits number
 * or by its symbolic name. Handle both */
int sldns_str2wire_alg_buf(const char* str, uint8_t* rd, size_t* len)
{
	sldns_lookup_table *lt = sldns_lookup_by_name(sldns_algorithms, str);
	if(*len < 1)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	if(lt) {
		rd[0] = (uint8_t)lt->id;
		*len = 1;
	} else {
		/* try as-is (a number) */
		return sldns_str2wire_int8_buf(str, rd, len);
	}
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_tsigerror_buf(const char* str, uint8_t* rd, size_t* len)
{
	sldns_lookup_table *lt = sldns_lookup_by_name(sldns_tsig_errors, str);
	if(*len < 2)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	if(lt) {
		sldns_write_uint16(rd, (uint16_t)lt->id);
		*len = 2;
	} else {
		/* try as-is (a number) */
		return sldns_str2wire_int16_buf(str, rd, len);
	}
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_time_buf(const char* str, uint8_t* rd, size_t* len)
{
	/* convert a time YYYYDDMMHHMMSS to wireformat */
	struct tm tm;
	if(*len < 4)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;

	/* Try to scan the time... */
	memset(&tm, 0, sizeof(tm));
	if (strlen(str) == 14 && sscanf(str, "%4d%2d%2d%2d%2d%2d",
		&tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour,
		&tm.tm_min, &tm.tm_sec) == 6) {
	   	tm.tm_year -= 1900;
	   	tm.tm_mon--;
	   	/* Check values */
		if (tm.tm_year < 70)
			return LDNS_WIREPARSE_ERR_SYNTAX_TIME;
		if (tm.tm_mon < 0 || tm.tm_mon > 11)
			return LDNS_WIREPARSE_ERR_SYNTAX_TIME;
		if (tm.tm_mday < 1 || tm.tm_mday > 31)
			return LDNS_WIREPARSE_ERR_SYNTAX_TIME;
		if (tm.tm_hour < 0 || tm.tm_hour > 23)
			return LDNS_WIREPARSE_ERR_SYNTAX_TIME;
		if (tm.tm_min < 0 || tm.tm_min > 59)
			return LDNS_WIREPARSE_ERR_SYNTAX_TIME;
		if (tm.tm_sec < 0 || tm.tm_sec > 59)
			return LDNS_WIREPARSE_ERR_SYNTAX_TIME;

		sldns_write_uint32(rd, (uint32_t)sldns_mktime_from_utc(&tm));
	} else {
		/* handle it as 32 bits timestamp */
		char *end;
		uint32_t l = (uint32_t)strtol((char*)str, &end, 10);
		if(*end != 0)
			return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_TIME,
				end-(char*)str);
		sldns_write_uint32(rd, l);
	}
	*len = 4;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_tsigtime_buf(const char* str, uint8_t* rd, size_t* len)
{
	char* end;
	uint64_t t = (uint64_t)strtol((char*)str, &end, 10);
	uint16_t high;
	uint32_t low;
	if(*end != 0)
		return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_TIME, end-str);
	if(*len < 6)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	high = (uint16_t)(t>>32);
	low = (uint32_t)(t);
	sldns_write_uint16(rd, high);
	sldns_write_uint32(rd+2, low);
	*len = 6;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_period_buf(const char* str, uint8_t* rd, size_t* len)
{
	const char* end;
	uint32_t p = sldns_str2period(str, &end);
	if(*end != 0)
		return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_PERIOD, end-str);
	if(*len < 4)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	sldns_write_uint32(rd, p);
	*len = 4;
	return LDNS_WIREPARSE_ERR_OK;
}

/** read "<digits>[.<digits>][mM]" into mantissa exponent format for LOC type */
static int
loc_parse_cm(char* my_str, char** endstr, uint8_t* m, uint8_t* e)
{
	uint32_t meters = 0, cm = 0, val;
	while (isblank((unsigned char)*my_str)) {
		my_str++;
	}
	meters = (uint32_t)strtol(my_str, &my_str, 10);
	if (*my_str == '.') {
		my_str++;
		cm = (uint32_t)strtol(my_str, &my_str, 10);
	}
	if (meters >= 1) {
		*e = 2;
		val = meters;
	} else	{
		*e = 0;
		val = cm;
	}
	while(val >= 10) {
		(*e)++;
		val /= 10;
	}
	*m = (uint8_t)val;

	if (*e > 9)
		return 0;
	if (*my_str == 'm' || *my_str == 'M') {
		my_str++;
	}
	*endstr = my_str;
	return 1;
}

int sldns_str2wire_loc_buf(const char* str, uint8_t* rd, size_t* len)
{
	uint32_t latitude = 0;
	uint32_t longitude = 0;
	uint32_t altitude = 0;

	uint32_t equator = (uint32_t)1<<31; /* 2**31 */

	/* only support version 0 */
	uint32_t h = 0;
	uint32_t m = 0;
	uint8_t size_b = 1, size_e = 2;
	uint8_t horiz_pre_b = 1, horiz_pre_e = 6;
	uint8_t vert_pre_b = 1, vert_pre_e = 3;

	double s = 0.0;
	int northerness;
	int easterness;

	char *my_str = (char *) str;

	if (isdigit((unsigned char) *my_str)) {
		h = (uint32_t) strtol(my_str, &my_str, 10);
	} else {
		return LDNS_WIREPARSE_ERR_INVALID_STR;
	}

	while (isblank((unsigned char) *my_str)) {
		my_str++;
	}

	if (isdigit((unsigned char) *my_str)) {
		m = (uint32_t) strtol(my_str, &my_str, 10);
	} else if (*my_str == 'N' || *my_str == 'S') {
		goto north;
	} else {
		return LDNS_WIREPARSE_ERR_INVALID_STR;
	}

	while (isblank((unsigned char) *my_str)) {
		my_str++;
	}

	if (isdigit((unsigned char) *my_str)) {
		s = strtod(my_str, &my_str);
	}

	/* skip blanks before northerness */
	while (isblank((unsigned char) *my_str)) {
		my_str++;
	}

north:
	if (*my_str == 'N') {
		northerness = 1;
	} else if (*my_str == 'S') {
		northerness = 0;
	} else {
		return LDNS_WIREPARSE_ERR_INVALID_STR;
	}

	my_str++;

	/* store number */
	s = 1000.0 * s;
	/* add a little to make floor in conversion a round */
	s += 0.0005;
	latitude = (uint32_t) s;
	latitude += 1000 * 60 * m;
	latitude += 1000 * 60 * 60 * h;
	if (northerness) {
		latitude = equator + latitude;
	} else {
		latitude = equator - latitude;
	}
	while (isblank((unsigned char)*my_str)) {
		my_str++;
	}

	if (isdigit((unsigned char) *my_str)) {
		h = (uint32_t) strtol(my_str, &my_str, 10);
	} else {
		return LDNS_WIREPARSE_ERR_INVALID_STR;
	}

	while (isblank((unsigned char) *my_str)) {
		my_str++;
	}

	if (isdigit((unsigned char) *my_str)) {
		m = (uint32_t) strtol(my_str, &my_str, 10);
	} else if (*my_str == 'E' || *my_str == 'W') {
		goto east;
	} else {
		return LDNS_WIREPARSE_ERR_INVALID_STR;
	}

	while (isblank((unsigned char)*my_str)) {
		my_str++;
	}

	if (isdigit((unsigned char) *my_str)) {
		s = strtod(my_str, &my_str);
	}

	/* skip blanks before easterness */
	while (isblank((unsigned char)*my_str)) {
		my_str++;
	}

east:
	if (*my_str == 'E') {
		easterness = 1;
	} else if (*my_str == 'W') {
		easterness = 0;
	} else {
		return LDNS_WIREPARSE_ERR_INVALID_STR;
	}

	my_str++;

	/* store number */
	s *= 1000.0;
	/* add a little to make floor in conversion a round */
	s += 0.0005;
	longitude = (uint32_t) s;
	longitude += 1000 * 60 * m;
	longitude += 1000 * 60 * 60 * h;

	if (easterness) {
		longitude += equator;
	} else {
		longitude = equator - longitude;
	}

	altitude = (uint32_t)(strtod(my_str, &my_str)*100.0 +
		10000000.0 + 0.5);
	if (*my_str == 'm' || *my_str == 'M') {
		my_str++;
	}

	if (strlen(my_str) > 0) {
		if(!loc_parse_cm(my_str, &my_str, &size_b, &size_e))
			return LDNS_WIREPARSE_ERR_INVALID_STR;
	}

	if (strlen(my_str) > 0) {
		if(!loc_parse_cm(my_str, &my_str, &horiz_pre_b, &horiz_pre_e))
			return LDNS_WIREPARSE_ERR_INVALID_STR;
	}

	if (strlen(my_str) > 0) {
		if(!loc_parse_cm(my_str, &my_str, &vert_pre_b, &vert_pre_e))
			return LDNS_WIREPARSE_ERR_INVALID_STR;
	}

	if(*len < 16)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	rd[0] = 0;
	rd[1] = ((size_b << 4) & 0xf0) | (size_e & 0x0f);
	rd[2] = ((horiz_pre_b << 4) & 0xf0) | (horiz_pre_e & 0x0f);
	rd[3] = ((vert_pre_b << 4) & 0xf0) | (vert_pre_e & 0x0f);
	sldns_write_uint32(rd + 4, latitude);
	sldns_write_uint32(rd + 8, longitude);
	sldns_write_uint32(rd + 12, altitude);
	*len = 16;
	return LDNS_WIREPARSE_ERR_OK;
}

static void
ldns_tolower_str(char* s)
{
	if(s) {
		while(*s) {
			*s = (char)tolower((unsigned char)*s);
			s++;
		}
	}
}

int sldns_str2wire_wks_buf(const char* str, uint8_t* rd, size_t* len)
{
	int rd_len = 1;
	int have_proto = 0;
	char token[50], proto_str[50];
	sldns_buffer strbuf;
	sldns_buffer_init_frm_data(&strbuf, (uint8_t*)str, strlen(str));
	proto_str[0]=0;

	/* check we have one byte for proto */
	if(*len < 1)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;

	while(sldns_bget_token(&strbuf, token, "\t\n ", sizeof(token)) > 0) {
		ldns_tolower_str(token);
		if(!have_proto) {
			struct protoent *p = getprotobyname(token);
			have_proto = 1;
			if(p) rd[0] = (uint8_t)p->p_proto;
			else if(strcasecmp(token, "tcp")==0) rd[0]=6;
			else if(strcasecmp(token, "udp")==0) rd[0]=17;
			else rd[0] = (uint8_t)atoi(token);
			(void)strlcpy(proto_str, token, sizeof(proto_str));
		} else {
			int serv_port;
			struct servent *serv = getservbyname(token, proto_str);
			if(serv) serv_port=(int)ntohs((uint16_t)serv->s_port);
			else if(strcasecmp(token, "domain")==0) serv_port=53;
			else {
				serv_port = atoi(token);
				if(serv_port == 0 && strcmp(token, "0") != 0) {
#ifdef HAVE_ENDSERVENT
					endservent();
#endif
#ifdef HAVE_ENDPROTOENT
					endprotoent();
#endif
					return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX,
						sldns_buffer_position(&strbuf));
				}
				if(serv_port < 0 || serv_port > 65535) {
#ifdef HAVE_ENDSERVENT
					endservent();
#endif
#ifdef HAVE_ENDPROTOENT
					endprotoent();
#endif
					return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX,
						sldns_buffer_position(&strbuf));
				}
			}
			if(rd_len < 1+serv_port/8+1) {
				/* bitmap is larger, init new bytes at 0 */
				if(*len < 1+(size_t)serv_port/8+1) {
#ifdef HAVE_ENDSERVENT
					endservent();
#endif
#ifdef HAVE_ENDPROTOENT
					endprotoent();
#endif
					return RET_ERR(
					LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL,
					sldns_buffer_position(&strbuf));
				}
				memset(rd+rd_len, 0, 1+(size_t)serv_port/8+1-rd_len);
				rd_len = 1+serv_port/8+1;
			}
			rd[1+ serv_port/8] |= (1 << (7 - serv_port % 8));
		}
	}
	*len = (size_t)rd_len;

#ifdef HAVE_ENDSERVENT
	endservent();
#endif
#ifdef HAVE_ENDPROTOENT
	endprotoent();
#endif
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_nsap_buf(const char* str, uint8_t* rd, size_t* len)
{
	const char* s = str;
	size_t slen;
	size_t dlen = 0; /* number of hexdigits parsed */

	/* just a hex string with optional dots? */
	if (s[0] != '0' || s[1] != 'x')
		return LDNS_WIREPARSE_ERR_INVALID_STR;
	s += 2;
	slen = strlen(s);
	if(slen > LDNS_MAX_RDFLEN*2)
		return LDNS_WIREPARSE_ERR_LABEL_OVERFLOW;
	while(*s) {
		if(isspace((unsigned char)*s) || *s == '.') {
			s++;
			continue;
		}
		if(!isxdigit((unsigned char)*s))
			return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_HEX, s-str);
		if(*len < dlen/2 + 1)
			return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL,
				s-str);
		if((dlen&1)==0)
			rd[dlen/2] = (uint8_t)sldns_hexdigit_to_int(*s++) * 16;
		else	rd[dlen/2] += sldns_hexdigit_to_int(*s++);
		dlen++;
	}
	if((dlen&1)!=0)
		return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_HEX, s-str);
	*len = dlen/2;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_atma_buf(const char* str, uint8_t* rd, size_t* len)
{
	const char* s = str;
	size_t slen = strlen(str);
	size_t dlen = 0; /* number of hexdigits parsed */

	/* just a hex string with optional dots? */
	/* notimpl e.164 format */
	if(slen > LDNS_MAX_RDFLEN*2)
		return LDNS_WIREPARSE_ERR_LABEL_OVERFLOW;
	while(*s) {
		if(isspace((unsigned char)*s) || *s == '.') {
			s++;
			continue;
		}
		if(!isxdigit((unsigned char)*s))
			return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_HEX, s-str);
		if(*len < dlen/2 + 1)
			return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL,
				s-str);
		if((dlen&1)==0)
			rd[dlen/2] = (uint8_t)sldns_hexdigit_to_int(*s++) * 16;
		else	rd[dlen/2] += sldns_hexdigit_to_int(*s++);
		dlen++;
	}
	if((dlen&1)!=0)
		return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_HEX, s-str);
	*len = dlen/2;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_ipseckey_buf(const char* str, uint8_t* rd, size_t* len)
{
	size_t gwlen = 0, keylen = 0;
	int s;
	uint8_t gwtype;
	char token[512];
	sldns_buffer strbuf;
	sldns_buffer_init_frm_data(&strbuf, (uint8_t*)str, strlen(str));

	if(*len < 3)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	/* precedence */
	if(sldns_bget_token(&strbuf, token, "\t\n ", sizeof(token)) <= 0)
		return RET_ERR(LDNS_WIREPARSE_ERR_INVALID_STR,
			sldns_buffer_position(&strbuf));
	rd[0] = (uint8_t)atoi(token);
	/* gateway_type */
	if(sldns_bget_token(&strbuf, token, "\t\n ", sizeof(token)) <= 0)
		return RET_ERR(LDNS_WIREPARSE_ERR_INVALID_STR,
			sldns_buffer_position(&strbuf));
	rd[1] = (uint8_t)atoi(token);
	gwtype = rd[1];
	/* algorithm */
	if(sldns_bget_token(&strbuf, token, "\t\n ", sizeof(token)) <= 0)
		return RET_ERR(LDNS_WIREPARSE_ERR_INVALID_STR,
			sldns_buffer_position(&strbuf));
	rd[2] = (uint8_t)atoi(token);

	/* gateway */
	if(sldns_bget_token(&strbuf, token, "\t\n ", sizeof(token)) <= 0)
		return RET_ERR(LDNS_WIREPARSE_ERR_INVALID_STR,
			sldns_buffer_position(&strbuf));
	if(gwtype == 0) {
		/* NOGATEWAY */
		if(strcmp(token, ".") != 0)
			return RET_ERR(LDNS_WIREPARSE_ERR_INVALID_STR,
				sldns_buffer_position(&strbuf));
		gwlen = 0;
	} else if(gwtype == 1) {
		/* IP4 */
		gwlen = *len - 3;
		s = sldns_str2wire_a_buf(token, rd+3, &gwlen);
		if(s) return RET_ERR_SHIFT(s, sldns_buffer_position(&strbuf));
	} else if(gwtype == 2) {
		/* IP6 */
		gwlen = *len - 3;
		s = sldns_str2wire_aaaa_buf(token, rd+3, &gwlen);
		if(s) return RET_ERR_SHIFT(s, sldns_buffer_position(&strbuf));
	} else if(gwtype == 3) {
		/* DNAME */
		gwlen = *len - 3;
		s = sldns_str2wire_dname_buf(token, rd+3, &gwlen);
		if(s) return RET_ERR_SHIFT(s, sldns_buffer_position(&strbuf));
	} else {
		/* unknown gateway type */
		return RET_ERR(LDNS_WIREPARSE_ERR_INVALID_STR,
			sldns_buffer_position(&strbuf));
	}
	/* double check for size */
	if(*len < 3 + gwlen)
		return RET_ERR(LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL,
			sldns_buffer_position(&strbuf));

	/* publickey in remainder of strbuf */
	keylen = *len - 3 - gwlen;
	s = sldns_str2wire_b64_buf((const char*)sldns_buffer_current(&strbuf),
		rd+3+gwlen, &keylen);
	if(s) return RET_ERR_SHIFT(s, sldns_buffer_position(&strbuf));

	*len = 3 + gwlen + keylen;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_nsec3_salt_buf(const char* str, uint8_t* rd, size_t* len)
{
	int i, salt_length_str = (int)strlen(str);
	if (salt_length_str == 1 && str[0] == '-') {
		salt_length_str = 0;
	} else if (salt_length_str % 2 != 0) {
		return LDNS_WIREPARSE_ERR_SYNTAX_HEX;
	}
	if (salt_length_str > 512)
		return LDNS_WIREPARSE_ERR_SYNTAX_HEX;
	if(*len < 1+(size_t)salt_length_str / 2)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	rd[0] = (uint8_t) (salt_length_str / 2);
	for (i = 0; i < salt_length_str; i += 2) {
		if (isxdigit((unsigned char)str[i]) &&
			isxdigit((unsigned char)str[i+1])) {
			rd[1+i/2] = (uint8_t)(sldns_hexdigit_to_int(str[i])*16
				+ sldns_hexdigit_to_int(str[i+1]));
		} else {
			return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_HEX, i);
		}
	}
	*len = 1 + (size_t)rd[0];
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_ilnp64_buf(const char* str, uint8_t* rd, size_t* len)
{
	unsigned int a, b, c, d;
	uint16_t shorts[4];
	int l;
	if(*len < sizeof(shorts))
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;

	if (sscanf(str, "%4x:%4x:%4x:%4x%n", &a, &b, &c, &d, &l) != 4 ||
			l != (int)strlen(str) || /* more data to read */
			strpbrk(str, "+-")       /* signed hexes */
			)
		return LDNS_WIREPARSE_ERR_SYNTAX_ILNP64;
	shorts[0] = htons(a);
	shorts[1] = htons(b);
	shorts[2] = htons(c);
	shorts[3] = htons(d);
	memmove(rd, &shorts, sizeof(shorts));
	*len = sizeof(shorts);
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_eui48_buf(const char* str, uint8_t* rd, size_t* len)
{
	unsigned int a, b, c, d, e, f;
	int l;

	if(*len < 6)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	if (sscanf(str, "%2x-%2x-%2x-%2x-%2x-%2x%n",
			&a, &b, &c, &d, &e, &f, &l) != 6 ||
			l != (int)strlen(str))
		return LDNS_WIREPARSE_ERR_SYNTAX_EUI48;
	rd[0] = a;
	rd[1] = b;
	rd[2] = c;
	rd[3] = d;
	rd[4] = e;
	rd[5] = f;
	*len = 6;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_eui64_buf(const char* str, uint8_t* rd, size_t* len)
{
	unsigned int a, b, c, d, e, f, g, h;
	int l;

	if(*len < 8)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	if (sscanf(str, "%2x-%2x-%2x-%2x-%2x-%2x-%2x-%2x%n",
			&a, &b, &c, &d, &e, &f, &g, &h, &l) != 8 ||
			l != (int)strlen(str))
		return LDNS_WIREPARSE_ERR_SYNTAX_EUI64;
	rd[0] = a;
	rd[1] = b;
	rd[2] = c;
	rd[3] = d;
	rd[4] = e;
	rd[5] = f;
	rd[6] = g;
	rd[7] = h;
	*len = 8;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_tag_buf(const char* str, uint8_t* rd, size_t* len)
{
	size_t slen = strlen(str);
	const char* ptr;

	if (slen > 255)
		return LDNS_WIREPARSE_ERR_SYNTAX_TAG;
	if(*len < slen+1)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	for (ptr = str; *ptr; ptr++) {
		if(!isalnum((unsigned char)*ptr))
			return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_TAG, ptr-str);
	}
	rd[0] = (uint8_t)slen;
	memmove(rd+1, str, slen);
	*len = slen+1;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_long_str_buf(const char* str, uint8_t* rd, size_t* len)
{
	uint8_t ch = 0;
	const char* pstr = str;
	size_t length = 0;

	/* Fill data with parsed bytes */
	while (sldns_parse_char(&ch, &pstr)) {
		if(*len < length+1)
			return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
		rd[length++] = ch;
	}
	if(!pstr)
		return LDNS_WIREPARSE_ERR_SYNTAX_BAD_ESCAPE;
	*len = length;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_hip_buf(const char* str, uint8_t* rd, size_t* len)
{
	char* s, *end;
	int e;
	size_t hitlen, pklen = 0;
	/* presentation format:
	 * 	pk-algo HIThex pubkeybase64
	 * wireformat:
	 * 	hitlen[1byte] pkalgo[1byte] pubkeylen[2byte] [hit] [pubkey] */
	if(*len < 4)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;

	/* read PK algorithm */
	rd[1] = (uint8_t)strtol((char*)str, &s, 10);
	if(*s != ' ')
		return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_INT, s-(char*)str);
	s++;
	while(*s == ' ')
		s++;

	/* read HIT hex tag */
	/* zero terminate the tag (replace later) */
	end = strchr(s, ' ');
	if(!end) return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX, s-(char*)str);
	*end = 0;
	hitlen = *len - 4;
	if((e = sldns_str2wire_hex_buf(s, rd+4, &hitlen)) != 0) {
		*end = ' ';
		return RET_ERR_SHIFT(e, s-(char*)str);
	}
	if(hitlen > 255) {
		*end = ' ';
		return RET_ERR(LDNS_WIREPARSE_ERR_LABEL_OVERFLOW, s-(char*)str+255*2);
	}
	rd[0] = (uint8_t)hitlen;
	*end = ' ';
	s = end+1;

	/* read pubkey base64 sequence */
	pklen = *len - 4 - hitlen;
	if((e = sldns_str2wire_b64_buf(s, rd+4+hitlen, &pklen)) != 0)
		return RET_ERR_SHIFT(e, s-(char*)str);
	if(pklen > 65535)
		return RET_ERR(LDNS_WIREPARSE_ERR_LABEL_OVERFLOW, s-(char*)str+65535);
	sldns_write_uint16(rd+2, (uint16_t)pklen);

	*len = 4 + hitlen + pklen;
	return LDNS_WIREPARSE_ERR_OK;
}

int sldns_str2wire_int16_data_buf(const char* str, uint8_t* rd, size_t* len)
{
	char* s;
	int n;
	n = strtol(str, &s, 10);
	if(*len < ((size_t)n)+2)
		return LDNS_WIREPARSE_ERR_BUFFER_TOO_SMALL;
	if(n > 65535)
		return LDNS_WIREPARSE_ERR_LABEL_OVERFLOW;

	if(n == 0) {
		sldns_write_uint16(rd, 0);
		*len = 2;
		return LDNS_WIREPARSE_ERR_OK;
	}
	if(*s != ' ')
		return RET_ERR(LDNS_WIREPARSE_ERR_SYNTAX_INT, s-(char*)str);
	s++;
	while(*s == ' ')
		s++;

	n = sldns_b64_pton(s, rd+2, (*len)-2);
	if(n < 0)
		return LDNS_WIREPARSE_ERR_SYNTAX_B64;
	sldns_write_uint16(rd, (uint16_t)n);
	*len = ((size_t)n)+2;
	return LDNS_WIREPARSE_ERR_OK;
}
