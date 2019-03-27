/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Shteryana Sotirova Shopova
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Begemot: bsnmp/lib/snmp.c,v 1.40 2005/10/04 14:32:42 brandt_h Exp $
 *
 * SNMP
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#elif defined(HAVE_INTTYPES_H)
#include <inttypes.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>

#include "asn1.h"
#include "snmp.h"
#include "snmppriv.h"

static void snmp_error_func(const char *, ...);
static void snmp_printf_func(const char *, ...);

void (*snmp_error)(const char *, ...) = snmp_error_func;
void (*snmp_printf)(const char *, ...) = snmp_printf_func;

/*
 * Get the next variable binding from the list.
 * ASN errors on the sequence or the OID are always fatal.
 */
static enum asn_err
get_var_binding(struct asn_buf *b, struct snmp_value *binding)
{
	u_char type;
	asn_len_t len, trailer;
	enum asn_err err;

	if (asn_get_sequence(b, &len) != ASN_ERR_OK) {
		snmp_error("cannot parse varbind header");
		return (ASN_ERR_FAILED);
	}

	/* temporary truncate the length so that the parser does not
	 * eat up bytes behind the sequence in the case the encoding is
	 * wrong of inner elements. */
	trailer = b->asn_len - len;
	b->asn_len = len;

	if (asn_get_objid(b, &binding->var) != ASN_ERR_OK) {
		snmp_error("cannot parse binding objid");
		return (ASN_ERR_FAILED);
	}
	if (asn_get_header(b, &type, &len) != ASN_ERR_OK) {
		snmp_error("cannot parse binding value header");
		return (ASN_ERR_FAILED);
	}

	switch (type) {

	  case ASN_TYPE_NULL:
		binding->syntax = SNMP_SYNTAX_NULL;
		err = asn_get_null_raw(b, len);
		break;

	  case ASN_TYPE_INTEGER:
		binding->syntax = SNMP_SYNTAX_INTEGER;
		err = asn_get_integer_raw(b, len, &binding->v.integer);
		break;

	  case ASN_TYPE_OCTETSTRING:
		binding->syntax = SNMP_SYNTAX_OCTETSTRING;
		binding->v.octetstring.octets = malloc(len);
		if (binding->v.octetstring.octets == NULL) {
			snmp_error("%s", strerror(errno));
			return (ASN_ERR_FAILED);
		}
		binding->v.octetstring.len = len;
		err = asn_get_octetstring_raw(b, len,
		    binding->v.octetstring.octets,
		    &binding->v.octetstring.len);
		if (ASN_ERR_STOPPED(err)) {
			free(binding->v.octetstring.octets);
			binding->v.octetstring.octets = NULL;
		}
		break;

	  case ASN_TYPE_OBJID:
		binding->syntax = SNMP_SYNTAX_OID;
		err = asn_get_objid_raw(b, len, &binding->v.oid);
		break;

	  case ASN_CLASS_APPLICATION|ASN_APP_IPADDRESS:
		binding->syntax = SNMP_SYNTAX_IPADDRESS;
		err = asn_get_ipaddress_raw(b, len, binding->v.ipaddress);
		break;

	  case ASN_CLASS_APPLICATION|ASN_APP_TIMETICKS:
		binding->syntax = SNMP_SYNTAX_TIMETICKS;
		err = asn_get_uint32_raw(b, len, &binding->v.uint32);
		break;

	  case ASN_CLASS_APPLICATION|ASN_APP_COUNTER:
		binding->syntax = SNMP_SYNTAX_COUNTER;
		err = asn_get_uint32_raw(b, len, &binding->v.uint32);
		break;

	  case ASN_CLASS_APPLICATION|ASN_APP_GAUGE:
		binding->syntax = SNMP_SYNTAX_GAUGE;
		err = asn_get_uint32_raw(b, len, &binding->v.uint32);
		break;

	  case ASN_CLASS_APPLICATION|ASN_APP_COUNTER64:
		binding->syntax = SNMP_SYNTAX_COUNTER64;
		err = asn_get_counter64_raw(b, len, &binding->v.counter64);
		break;

	  case ASN_CLASS_CONTEXT | ASN_EXCEPT_NOSUCHOBJECT:
		binding->syntax = SNMP_SYNTAX_NOSUCHOBJECT;
		err = asn_get_null_raw(b, len);
		break;

	  case ASN_CLASS_CONTEXT | ASN_EXCEPT_NOSUCHINSTANCE:
		binding->syntax = SNMP_SYNTAX_NOSUCHINSTANCE;
		err = asn_get_null_raw(b, len);
		break;

	  case ASN_CLASS_CONTEXT | ASN_EXCEPT_ENDOFMIBVIEW:
		binding->syntax = SNMP_SYNTAX_ENDOFMIBVIEW;
		err = asn_get_null_raw(b, len);
		break;

	  default:
		if ((err = asn_skip(b, len)) == ASN_ERR_OK)
			err = ASN_ERR_TAG;
		snmp_error("bad binding value type 0x%x", type);
		break;
	}

	if (ASN_ERR_STOPPED(err)) {
		snmp_error("cannot parse binding value");
		return (err);
	}

	if (b->asn_len != 0)
		snmp_error("ignoring junk at end of binding");

	b->asn_len = trailer;

	return (err);
}

/*
 * Parse the different PDUs contents. Any ASN error in the outer components
 * are fatal. Only errors in variable values may be tolerated. If all
 * components can be parsed it returns either ASN_ERR_OK or the first
 * error that was found.
 */
enum asn_err
snmp_parse_pdus_hdr(struct asn_buf *b, struct snmp_pdu *pdu, asn_len_t *lenp)
{
	if (pdu->type == SNMP_PDU_TRAP) {
		if (asn_get_objid(b, &pdu->enterprise) != ASN_ERR_OK) {
			snmp_error("cannot parse trap enterprise");
			return (ASN_ERR_FAILED);
		}
		if (asn_get_ipaddress(b, pdu->agent_addr) != ASN_ERR_OK) {
			snmp_error("cannot parse trap agent address");
			return (ASN_ERR_FAILED);
		}
		if (asn_get_integer(b, &pdu->generic_trap) != ASN_ERR_OK) {
			snmp_error("cannot parse 'generic-trap'");
			return (ASN_ERR_FAILED);
		}
		if (asn_get_integer(b, &pdu->specific_trap) != ASN_ERR_OK) {
			snmp_error("cannot parse 'specific-trap'");
			return (ASN_ERR_FAILED);
		}
		if (asn_get_timeticks(b, &pdu->time_stamp) != ASN_ERR_OK) {
			snmp_error("cannot parse trap 'time-stamp'");
			return (ASN_ERR_FAILED);
		}
	} else {
		if (asn_get_integer(b, &pdu->request_id) != ASN_ERR_OK) {
			snmp_error("cannot parse 'request-id'");
			return (ASN_ERR_FAILED);
		}
		if (asn_get_integer(b, &pdu->error_status) != ASN_ERR_OK) {
			snmp_error("cannot parse 'error_status'");
			return (ASN_ERR_FAILED);
		}
		if (asn_get_integer(b, &pdu->error_index) != ASN_ERR_OK) {
			snmp_error("cannot parse 'error_index'");
			return (ASN_ERR_FAILED);
		}
	}

	if (asn_get_sequence(b, lenp) != ASN_ERR_OK) {
		snmp_error("cannot get varlist header");
		return (ASN_ERR_FAILED);
	}

	return (ASN_ERR_OK);
}

static enum asn_err
parse_pdus(struct asn_buf *b, struct snmp_pdu *pdu, int32_t *ip)
{
	asn_len_t len, trailer;
	struct snmp_value *v;
	enum asn_err err, err1;

	err = snmp_parse_pdus_hdr(b, pdu, &len);
	if (ASN_ERR_STOPPED(err))
		return (err);

	trailer = b->asn_len - len;

	v = pdu->bindings;
	err = ASN_ERR_OK;
	while (b->asn_len != 0) {
		if (pdu->nbindings == SNMP_MAX_BINDINGS) {
			snmp_error("too many bindings (> %u) in PDU",
			    SNMP_MAX_BINDINGS);
			return (ASN_ERR_FAILED);
		}
		err1 = get_var_binding(b, v);
		if (ASN_ERR_STOPPED(err1))
			return (ASN_ERR_FAILED);
		if (err1 != ASN_ERR_OK && err == ASN_ERR_OK) {
			err = err1;
			*ip = pdu->nbindings + 1;
		}
		pdu->nbindings++;
		v++;
	}

	b->asn_len = trailer;

	return (err);
}


static enum asn_err
parse_secparams(struct asn_buf *b, struct snmp_pdu *pdu)
{
	asn_len_t octs_len;
	u_char buf[256]; /* XXX: calc max possible size here */
	struct asn_buf tb;

	memset(buf, 0, 256);
	tb.asn_ptr = buf;
	tb.asn_len = 256;
	u_int len = 256;

	if (asn_get_octetstring(b, buf, &len) != ASN_ERR_OK) {
		snmp_error("cannot parse usm header");
		return (ASN_ERR_FAILED);
	}
	tb.asn_len = len;

	if (asn_get_sequence(&tb, &octs_len) != ASN_ERR_OK) {
		snmp_error("cannot decode usm header");
		return (ASN_ERR_FAILED);
	}

	octs_len = SNMP_ENGINE_ID_SIZ;
	if (asn_get_octetstring(&tb, (u_char *)&pdu->engine.engine_id,
	    &octs_len) != ASN_ERR_OK) {
		snmp_error("cannot decode msg engine id");
		return (ASN_ERR_FAILED);
	}
	pdu->engine.engine_len = octs_len;

	if (asn_get_integer(&tb, &pdu->engine.engine_boots) != ASN_ERR_OK) {
		snmp_error("cannot decode msg engine boots");
		return (ASN_ERR_FAILED);
	}

	if (asn_get_integer(&tb, &pdu->engine.engine_time) != ASN_ERR_OK) {
		snmp_error("cannot decode msg engine time");
		return (ASN_ERR_FAILED);
	}

	octs_len = SNMP_ADM_STR32_SIZ - 1;
	if (asn_get_octetstring(&tb, (u_char *)&pdu->user.sec_name, &octs_len)
	    != ASN_ERR_OK) {
		snmp_error("cannot decode msg user name");
		return (ASN_ERR_FAILED);
	}
	pdu->user.sec_name[octs_len] = '\0';

	octs_len = sizeof(pdu->msg_digest);
	if (asn_get_octetstring(&tb, (u_char *)&pdu->msg_digest, &octs_len) !=
	    ASN_ERR_OK || ((pdu->flags & SNMP_MSG_AUTH_FLAG) != 0 &&
	    octs_len != sizeof(pdu->msg_digest))) {
		snmp_error("cannot decode msg authentication param");
		return (ASN_ERR_FAILED);
	}

	octs_len = sizeof(pdu->msg_salt);
	if (asn_get_octetstring(&tb, (u_char *)&pdu->msg_salt, &octs_len) !=
	    ASN_ERR_OK ||((pdu->flags & SNMP_MSG_PRIV_FLAG) != 0 &&
	    octs_len != sizeof(pdu->msg_salt))) {
		snmp_error("cannot decode msg authentication param");
		return (ASN_ERR_FAILED);
	}

	if ((pdu->flags & SNMP_MSG_AUTH_FLAG) != 0) {
		pdu->digest_ptr = b->asn_ptr - SNMP_USM_AUTH_SIZE;
		pdu->digest_ptr -= octs_len + ASN_MAXLENLEN;
	}

	return (ASN_ERR_OK);
}

static enum snmp_code
pdu_encode_secparams(struct asn_buf *b, struct snmp_pdu *pdu)
{
	u_char buf[256], *sptr;
	struct asn_buf tb;
	size_t auth_off, moved = 0;

	auth_off = 0;
	memset(buf, 0, 256);
	tb.asn_ptr = buf;
	tb.asn_len = 256;

	if (asn_put_temp_header(&tb, (ASN_TYPE_SEQUENCE|ASN_TYPE_CONSTRUCTED),
	    &sptr) != ASN_ERR_OK)
		return (SNMP_CODE_FAILED);

	if (asn_put_octetstring(&tb, (u_char *)pdu->engine.engine_id,
	    pdu->engine.engine_len) != ASN_ERR_OK)
		return (SNMP_CODE_FAILED);

	if (asn_put_integer(&tb, pdu->engine.engine_boots) != ASN_ERR_OK)
		return (SNMP_CODE_FAILED);

	if (asn_put_integer(&tb, pdu->engine.engine_time) != ASN_ERR_OK)
		return (SNMP_CODE_FAILED);

	if (asn_put_octetstring(&tb, (u_char *)pdu->user.sec_name,
	    strlen(pdu->user.sec_name)) != ASN_ERR_OK)
		return (SNMP_CODE_FAILED);

	if ((pdu->flags & SNMP_MSG_AUTH_FLAG) != 0) {
		auth_off = sizeof(buf) - tb.asn_len + ASN_MAXLENLEN;
		if (asn_put_octetstring(&tb, (u_char *)pdu->msg_digest,
		    sizeof(pdu->msg_digest)) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);
	} else {
		if (asn_put_octetstring(&tb, (u_char *)pdu->msg_digest, 0)
		    != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);
	}

	if ((pdu->flags & SNMP_MSG_PRIV_FLAG) != 0) {
		if (asn_put_octetstring(&tb, (u_char *)pdu->msg_salt,
		    sizeof(pdu->msg_salt)) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);
	} else {
		if (asn_put_octetstring(&tb, (u_char *)pdu->msg_salt, 0)
		    != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);
	}

	if (asn_commit_header(&tb, sptr, &moved) != ASN_ERR_OK)
		return (SNMP_CODE_FAILED);

	if ((pdu->flags & SNMP_MSG_AUTH_FLAG) != 0)
		pdu->digest_ptr = b->asn_ptr + auth_off - moved;

	if (asn_put_octetstring(b, buf, sizeof(buf) - tb.asn_len) != ASN_ERR_OK)
		return (SNMP_CODE_FAILED);
	pdu->digest_ptr += ASN_MAXLENLEN;

	if ((pdu->flags & SNMP_MSG_PRIV_FLAG) != 0 && asn_put_temp_header(b,
	    ASN_TYPE_OCTETSTRING, &pdu->encrypted_ptr) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);

	return (SNMP_CODE_OK);
}

/*
 * Decode the PDU except for the variable bindings itself.
 * If decoding fails because of a bad binding, but the rest can be
 * decoded, ip points to the index of the failed variable (errors
 * OORANGE, BADLEN or BADVERS).
 */
enum snmp_code
snmp_pdu_decode(struct asn_buf *b, struct snmp_pdu *pdu, int32_t *ip)
{
	enum snmp_code code;

	if ((code = snmp_pdu_decode_header(b, pdu)) != SNMP_CODE_OK)
		return (code);

	if (pdu->version == SNMP_V3) {
		if (pdu->security_model != SNMP_SECMODEL_USM)
			return (SNMP_CODE_FAILED);
		if ((code = snmp_pdu_decode_secmode(b, pdu)) != SNMP_CODE_OK)
			return (code);
	}

	code = snmp_pdu_decode_scoped(b, pdu, ip);

	switch (code) {
	  case SNMP_CODE_FAILED:
		snmp_pdu_free(pdu);
		break;

	  case SNMP_CODE_BADENC:
		if (pdu->version == SNMP_Verr)
			return (SNMP_CODE_BADVERS);

	  default:
		break;
	}

	return (code);
}

enum snmp_code
snmp_pdu_decode_header(struct asn_buf *b, struct snmp_pdu *pdu)
{
	int32_t version;
	u_int octs_len;
	asn_len_t len;

	pdu->outer_ptr = b->asn_ptr;
	pdu->outer_len = b->asn_len;

	if (asn_get_sequence(b, &len) != ASN_ERR_OK) {
		snmp_error("cannot decode pdu header");
		return (SNMP_CODE_FAILED);
	}
	if (b->asn_len < len) {
		snmp_error("outer sequence value too short");
		return (SNMP_CODE_FAILED);
	}
	if (b->asn_len != len) {
		snmp_error("ignoring trailing junk in message");
		b->asn_len = len;
	}

	if (asn_get_integer(b, &version) != ASN_ERR_OK) {
		snmp_error("cannot decode version");
		return (SNMP_CODE_FAILED);
	}

	if (version == 0)
		pdu->version = SNMP_V1;
	else if (version == 1)
		pdu->version = SNMP_V2c;
	else if (version == 3)
		pdu->version = SNMP_V3;
	else {
		pdu->version = SNMP_Verr;
		snmp_error("unsupported SNMP version");
		return (SNMP_CODE_BADENC);
	}

	if (pdu->version == SNMP_V3) {
		if (asn_get_sequence(b, &len) != ASN_ERR_OK) {
			snmp_error("cannot decode pdu global data header");
			return (SNMP_CODE_FAILED);
		}

		if (asn_get_integer(b, &pdu->identifier) != ASN_ERR_OK) {
			snmp_error("cannot decode msg indetifier");
			return (SNMP_CODE_FAILED);
		}

		if (asn_get_integer(b, &pdu->engine.max_msg_size)
		    != ASN_ERR_OK) {
			snmp_error("cannot decode msg size");
			return (SNMP_CODE_FAILED);
		}

		octs_len = 1;
		if (asn_get_octetstring(b, (u_char *)&pdu->flags,
		    &octs_len) != ASN_ERR_OK) {
			snmp_error("cannot decode msg flags");
			return (SNMP_CODE_FAILED);
		}

		if (asn_get_integer(b, &pdu->security_model) != ASN_ERR_OK) {
			snmp_error("cannot decode msg size");
			return (SNMP_CODE_FAILED);
		}

		if (pdu->security_model != SNMP_SECMODEL_USM)
			return (SNMP_CODE_FAILED);

		if (parse_secparams(b, pdu) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);
	} else {
		octs_len = SNMP_COMMUNITY_MAXLEN;
		if (asn_get_octetstring(b, (u_char *)pdu->community,
		    &octs_len) != ASN_ERR_OK) {
			snmp_error("cannot decode community");
			return (SNMP_CODE_FAILED);
		}
		pdu->community[octs_len] = '\0';
	}

	return (SNMP_CODE_OK);
}

enum snmp_code
snmp_pdu_decode_scoped(struct asn_buf *b, struct snmp_pdu *pdu, int32_t *ip)
{
	u_char type;
	asn_len_t len, trailer;
	enum asn_err err;

	if (pdu->version == SNMP_V3) {
		if (asn_get_sequence(b, &len) != ASN_ERR_OK) {
			snmp_error("cannot decode scoped pdu header");
			return (SNMP_CODE_FAILED);
		}

		len = SNMP_ENGINE_ID_SIZ;
		if (asn_get_octetstring(b, (u_char *)&pdu->context_engine,
		    &len) != ASN_ERR_OK) {
			snmp_error("cannot decode msg context engine");
			return (SNMP_CODE_FAILED);
		}
		pdu->context_engine_len = len;

		len = SNMP_CONTEXT_NAME_SIZ;
		if (asn_get_octetstring(b, (u_char *)&pdu->context_name,
		    &len) != ASN_ERR_OK) {
			snmp_error("cannot decode msg context name");
			return (SNMP_CODE_FAILED);
		}
		pdu->context_name[len] = '\0';
	}

	if (asn_get_header(b, &type, &len) != ASN_ERR_OK) {
		snmp_error("cannot get pdu header");
		return (SNMP_CODE_FAILED);
	}
	if ((type & ~ASN_TYPE_MASK) !=
	    (ASN_TYPE_CONSTRUCTED | ASN_CLASS_CONTEXT)) {
		snmp_error("bad pdu header tag");
		return (SNMP_CODE_FAILED);
	}
	pdu->type = type & ASN_TYPE_MASK;

	switch (pdu->type) {

	  case SNMP_PDU_GET:
	  case SNMP_PDU_GETNEXT:
	  case SNMP_PDU_RESPONSE:
	  case SNMP_PDU_SET:
		break;

	  case SNMP_PDU_TRAP:
		if (pdu->version != SNMP_V1) {
			snmp_error("bad pdu type %u", pdu->type);
			return (SNMP_CODE_FAILED);
		}
		break;

	  case SNMP_PDU_GETBULK:
	  case SNMP_PDU_INFORM:
	  case SNMP_PDU_TRAP2:
	  case SNMP_PDU_REPORT:
		if (pdu->version == SNMP_V1) {
			snmp_error("bad pdu type %u", pdu->type);
			return (SNMP_CODE_FAILED);
		}
		break;

	  default:
		snmp_error("bad pdu type %u", pdu->type);
		return (SNMP_CODE_FAILED);
	}

	trailer = b->asn_len - len;
	b->asn_len = len;

	err = parse_pdus(b, pdu, ip);
	if (ASN_ERR_STOPPED(err))
		return (SNMP_CODE_FAILED);

	if (b->asn_len != 0)
		snmp_error("ignoring trailing junk after pdu");

	b->asn_len = trailer;

	return (SNMP_CODE_OK);
}

enum snmp_code
snmp_pdu_decode_secmode(struct asn_buf *b, struct snmp_pdu *pdu)
{
	u_char type;
	enum snmp_code code;
	uint8_t	digest[SNMP_USM_AUTH_SIZE];

	if (pdu->user.auth_proto != SNMP_AUTH_NOAUTH &&
	    (pdu->flags & SNMP_MSG_AUTH_FLAG) == 0)
		return (SNMP_CODE_BADSECLEVEL);

	if ((code = snmp_pdu_calc_digest(pdu, digest)) !=
	    SNMP_CODE_OK)
		return (SNMP_CODE_FAILED);

	if (pdu->user.auth_proto != SNMP_AUTH_NOAUTH &&
	    memcmp(digest, pdu->msg_digest, sizeof(pdu->msg_digest)) != 0)
		return (SNMP_CODE_BADDIGEST);

	if (pdu->user.priv_proto != SNMP_PRIV_NOPRIV && (asn_get_header(b, &type,
	    &pdu->scoped_len) != ASN_ERR_OK || type != ASN_TYPE_OCTETSTRING)) {
		snmp_error("cannot decode encrypted pdu");
		return (SNMP_CODE_FAILED);
	}
	pdu->scoped_ptr = b->asn_ptr;

	if (pdu->user.priv_proto != SNMP_PRIV_NOPRIV &&
	    (pdu->flags & SNMP_MSG_PRIV_FLAG) == 0)
		return (SNMP_CODE_BADSECLEVEL);

	if ((code = snmp_pdu_decrypt(pdu)) != SNMP_CODE_OK)
		return (SNMP_CODE_FAILED);

	return (code);
}

/*
 * Check whether what we have is the complete PDU by snooping at the
 * enclosing structure header. This returns:
 *   -1		if there are ASN.1 errors
 *    0		if we need more data
 *  > 0		the length of this PDU
 */
int
snmp_pdu_snoop(const struct asn_buf *b0)
{
	u_int length;
	asn_len_t len;
	struct asn_buf b = *b0;

	/* <0x10|0x20> <len> <data...> */

	if (b.asn_len == 0)
		return (0);
	if (b.asn_cptr[0] != (ASN_TYPE_SEQUENCE | ASN_TYPE_CONSTRUCTED)) {
		asn_error(&b, "bad sequence type %u", b.asn_cptr[0]);
		return (-1);
	}
	b.asn_len--;
	b.asn_cptr++;

	if (b.asn_len == 0)
		return (0);

	if (*b.asn_cptr & 0x80) {
		/* long length */
		length = *b.asn_cptr++ & 0x7f;
		b.asn_len--;
		if (length == 0) {
			asn_error(&b, "indefinite length not supported");
			return (-1);
		}
		if (length > ASN_MAXLENLEN) {
			asn_error(&b, "long length too long (%u)", length);
			return (-1);
		}
		if (length > b.asn_len)
			return (0);
		len = 0;
		while (length--) {
			len = (len << 8) | *b.asn_cptr++;
			b.asn_len--;
		}
	} else {
		len = *b.asn_cptr++;
		b.asn_len--;
	}

	if (len > b.asn_len)
		return (0);

	return (len + b.asn_cptr - b0->asn_cptr);
}

/*
 * Encode the SNMP PDU without the variable bindings field.
 * We do this the rather uneffective way by
 * moving things around and assuming that the length field will never
 * use more than 2 bytes.
 * We need a number of pointers to apply the fixes afterwards.
 */
enum snmp_code
snmp_pdu_encode_header(struct asn_buf *b, struct snmp_pdu *pdu)
{
	enum asn_err err;
	u_char *v3_hdr_ptr;

	if (asn_put_temp_header(b, (ASN_TYPE_SEQUENCE|ASN_TYPE_CONSTRUCTED),
	    &pdu->outer_ptr) != ASN_ERR_OK)
		return (SNMP_CODE_FAILED);

	if (pdu->version == SNMP_V1)
		err = asn_put_integer(b, 0);
	else if (pdu->version == SNMP_V2c)
		err = asn_put_integer(b, 1);
	else if (pdu->version == SNMP_V3)
		err = asn_put_integer(b, 3);
	else
		return (SNMP_CODE_BADVERS);
	if (err != ASN_ERR_OK)
		return (SNMP_CODE_FAILED);

	if (pdu->version == SNMP_V3) {
		if (asn_put_temp_header(b, (ASN_TYPE_SEQUENCE |
		    ASN_TYPE_CONSTRUCTED), &v3_hdr_ptr) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);

		if (asn_put_integer(b, pdu->identifier) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);

		if (asn_put_integer(b, pdu->engine.max_msg_size) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);

		if (pdu->type != SNMP_PDU_RESPONSE &&
		    pdu->type != SNMP_PDU_TRAP &&
		    pdu->type != SNMP_PDU_TRAP2 &&
		    pdu->type != SNMP_PDU_REPORT)
			pdu->flags |= SNMP_MSG_REPORT_FLAG;

		if (asn_put_octetstring(b, (u_char *)&pdu->flags, 1)
		    != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);

		if (asn_put_integer(b, pdu->security_model) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);

		if (asn_commit_header(b, v3_hdr_ptr, NULL) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);

		if (pdu->security_model != SNMP_SECMODEL_USM)
			return (SNMP_CODE_FAILED);

		if (pdu_encode_secparams(b, pdu) != SNMP_CODE_OK)
			return (SNMP_CODE_FAILED);

		/*  View-based Access Conntrol information */
		if (asn_put_temp_header(b, (ASN_TYPE_SEQUENCE |
		    ASN_TYPE_CONSTRUCTED), &pdu->scoped_ptr) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);

		if (asn_put_octetstring(b, (u_char *)pdu->context_engine,
		    pdu->context_engine_len) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);

		if (asn_put_octetstring(b, (u_char *)pdu->context_name,
		    strlen(pdu->context_name)) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);
	} else {
		if (asn_put_octetstring(b, (u_char *)pdu->community,
		    strlen(pdu->community)) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);
	}

	if (asn_put_temp_header(b, (ASN_TYPE_CONSTRUCTED | ASN_CLASS_CONTEXT |
	    pdu->type), &pdu->pdu_ptr) != ASN_ERR_OK)
		return (SNMP_CODE_FAILED);

	if (pdu->type == SNMP_PDU_TRAP) {
		if (pdu->version != SNMP_V1 ||
		    asn_put_objid(b, &pdu->enterprise) != ASN_ERR_OK ||
		    asn_put_ipaddress(b, pdu->agent_addr) != ASN_ERR_OK ||
		    asn_put_integer(b, pdu->generic_trap) != ASN_ERR_OK ||
		    asn_put_integer(b, pdu->specific_trap) != ASN_ERR_OK ||
		    asn_put_timeticks(b, pdu->time_stamp) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);
	} else {
		if (pdu->version == SNMP_V1 && (pdu->type == SNMP_PDU_GETBULK ||
		    pdu->type == SNMP_PDU_INFORM ||
		    pdu->type == SNMP_PDU_TRAP2 ||
		    pdu->type == SNMP_PDU_REPORT))
			return (SNMP_CODE_FAILED);

		if (asn_put_integer(b, pdu->request_id) != ASN_ERR_OK ||
		    asn_put_integer(b, pdu->error_status) != ASN_ERR_OK ||
		    asn_put_integer(b, pdu->error_index) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);
	}

	if (asn_put_temp_header(b, (ASN_TYPE_SEQUENCE|ASN_TYPE_CONSTRUCTED),
	    &pdu->vars_ptr) != ASN_ERR_OK)
		return (SNMP_CODE_FAILED);

	return (SNMP_CODE_OK);
}

static enum asn_err
snmp_pdu_fix_padd(struct asn_buf *b, struct snmp_pdu *pdu)
{
	asn_len_t padlen;

	if (pdu->user.priv_proto == SNMP_PRIV_DES && pdu->scoped_len % 8 != 0) {
		padlen = 8 - (pdu->scoped_len % 8);
		if (asn_pad(b, padlen) != ASN_ERR_OK)
			return (ASN_ERR_FAILED);
		pdu->scoped_len += padlen;
	}

	return (ASN_ERR_OK);
}

enum snmp_code
snmp_fix_encoding(struct asn_buf *b, struct snmp_pdu *pdu)
{
	size_t moved = 0;
	enum snmp_code code;

	if (asn_commit_header(b, pdu->vars_ptr, NULL) != ASN_ERR_OK ||
	    asn_commit_header(b, pdu->pdu_ptr, NULL) != ASN_ERR_OK)
		return (SNMP_CODE_FAILED);

	if (pdu->version == SNMP_V3) {
		if (asn_commit_header(b, pdu->scoped_ptr, NULL) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);

		pdu->scoped_len = b->asn_ptr - pdu->scoped_ptr;
		if (snmp_pdu_fix_padd(b, pdu) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);

		if (pdu->security_model != SNMP_SECMODEL_USM)
			return (SNMP_CODE_FAILED);

		if (snmp_pdu_encrypt(pdu) != SNMP_CODE_OK)
			return (SNMP_CODE_FAILED);

		if (pdu->user.priv_proto != SNMP_PRIV_NOPRIV &&
		    asn_commit_header(b, pdu->encrypted_ptr, NULL) != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);
	}

	if (asn_commit_header(b, pdu->outer_ptr, &moved) != ASN_ERR_OK)
		return (SNMP_CODE_FAILED);

	pdu->outer_len = b->asn_ptr - pdu->outer_ptr;
	pdu->digest_ptr -= moved;

	if (pdu->version == SNMP_V3) {
		if ((code = snmp_pdu_calc_digest(pdu, pdu->msg_digest)) !=
		    SNMP_CODE_OK)
			return (SNMP_CODE_FAILED);

		if ((pdu->flags & SNMP_MSG_AUTH_FLAG) != 0)
			memcpy(pdu->digest_ptr, pdu->msg_digest,
			    sizeof(pdu->msg_digest));
	}

	return (SNMP_CODE_OK);
}

/*
 * Encode a binding. Caller must ensure, that the syntax is ok for that version.
 * Be sure not to cobber b, when something fails.
 */
enum asn_err
snmp_binding_encode(struct asn_buf *b, const struct snmp_value *binding)
{
	u_char *ptr;
	enum asn_err err;
	struct asn_buf save = *b;

	if ((err = asn_put_temp_header(b, (ASN_TYPE_SEQUENCE |
	    ASN_TYPE_CONSTRUCTED), &ptr)) != ASN_ERR_OK) {
		*b = save;
		return (err);
	}

	if ((err = asn_put_objid(b, &binding->var)) != ASN_ERR_OK) {
		*b = save;
		return (err);
	}

	switch (binding->syntax) {

	  case SNMP_SYNTAX_NULL:
		err = asn_put_null(b);
		break;

	  case SNMP_SYNTAX_INTEGER:
		err = asn_put_integer(b, binding->v.integer);
		break;

	  case SNMP_SYNTAX_OCTETSTRING:
		err = asn_put_octetstring(b, binding->v.octetstring.octets,
		    binding->v.octetstring.len);
		break;

	  case SNMP_SYNTAX_OID:
		err = asn_put_objid(b, &binding->v.oid);
		break;

	  case SNMP_SYNTAX_IPADDRESS:
		err = asn_put_ipaddress(b, binding->v.ipaddress);
		break;

	  case SNMP_SYNTAX_TIMETICKS:
		err = asn_put_uint32(b, ASN_APP_TIMETICKS, binding->v.uint32);
		break;

	  case SNMP_SYNTAX_COUNTER:
		err = asn_put_uint32(b, ASN_APP_COUNTER, binding->v.uint32);
		break;

	  case SNMP_SYNTAX_GAUGE:
		err = asn_put_uint32(b, ASN_APP_GAUGE, binding->v.uint32);
		break;

	  case SNMP_SYNTAX_COUNTER64:
		err = asn_put_counter64(b, binding->v.counter64);
		break;

	  case SNMP_SYNTAX_NOSUCHOBJECT:
		err = asn_put_exception(b, ASN_EXCEPT_NOSUCHOBJECT);
		break;

	  case SNMP_SYNTAX_NOSUCHINSTANCE:
		err = asn_put_exception(b, ASN_EXCEPT_NOSUCHINSTANCE);
		break;

	  case SNMP_SYNTAX_ENDOFMIBVIEW:
		err = asn_put_exception(b, ASN_EXCEPT_ENDOFMIBVIEW);
		break;
	}

	if (err != ASN_ERR_OK) {
		*b = save;
		return (err);
	}

	err = asn_commit_header(b, ptr, NULL);
	if (err != ASN_ERR_OK) {
		*b = save;
		return (err);
	}

	return (ASN_ERR_OK);
}

/*
 * Encode an PDU.
 */
enum snmp_code
snmp_pdu_encode(struct snmp_pdu *pdu, struct asn_buf *resp_b)
{
	u_int idx;
	enum snmp_code err;

	if ((err = snmp_pdu_encode_header(resp_b, pdu)) != SNMP_CODE_OK)
		return (err);
	for (idx = 0; idx < pdu->nbindings; idx++)
		if (snmp_binding_encode(resp_b, &pdu->bindings[idx])
		    != ASN_ERR_OK)
			return (SNMP_CODE_FAILED);

	return (snmp_fix_encoding(resp_b, pdu));
}

static void
dump_binding(const struct snmp_value *b)
{
	u_int i;
	char buf[ASN_OIDSTRLEN];

	snmp_printf("%s=", asn_oid2str_r(&b->var, buf));
	switch (b->syntax) {

	  case SNMP_SYNTAX_NULL:
		snmp_printf("NULL");
		break;

	  case SNMP_SYNTAX_INTEGER:
		snmp_printf("INTEGER %d", b->v.integer);
		break;

	  case SNMP_SYNTAX_OCTETSTRING:
		snmp_printf("OCTET STRING %lu:", b->v.octetstring.len);
		for (i = 0; i < b->v.octetstring.len; i++)
			snmp_printf(" %02x", b->v.octetstring.octets[i]);
		break;

	  case SNMP_SYNTAX_OID:
		snmp_printf("OID %s", asn_oid2str_r(&b->v.oid, buf));
		break;

	  case SNMP_SYNTAX_IPADDRESS:
		snmp_printf("IPADDRESS %u.%u.%u.%u", b->v.ipaddress[0],
		    b->v.ipaddress[1], b->v.ipaddress[2], b->v.ipaddress[3]);
		break;

	  case SNMP_SYNTAX_COUNTER:
		snmp_printf("COUNTER %u", b->v.uint32);
		break;

	  case SNMP_SYNTAX_GAUGE:
		snmp_printf("GAUGE %u", b->v.uint32);
		break;

	  case SNMP_SYNTAX_TIMETICKS:
		snmp_printf("TIMETICKS %u", b->v.uint32);
		break;

	  case SNMP_SYNTAX_COUNTER64:
		snmp_printf("COUNTER64 %lld", b->v.counter64);
		break;

	  case SNMP_SYNTAX_NOSUCHOBJECT:
		snmp_printf("NoSuchObject");
		break;

	  case SNMP_SYNTAX_NOSUCHINSTANCE:
		snmp_printf("NoSuchInstance");
		break;

	  case SNMP_SYNTAX_ENDOFMIBVIEW:
		snmp_printf("EndOfMibView");
		break;

	  default:
		snmp_printf("UNKNOWN SYNTAX %u", b->syntax);
		break;
	}
}

static __inline void
dump_bindings(const struct snmp_pdu *pdu)
{
	u_int i;

	for (i = 0; i < pdu->nbindings; i++) {
		snmp_printf(" [%u]: ", i);
		dump_binding(&pdu->bindings[i]);
		snmp_printf("\n");
	}
}

static __inline void
dump_notrap(const struct snmp_pdu *pdu)
{
	snmp_printf(" request_id=%d", pdu->request_id);
	snmp_printf(" error_status=%d", pdu->error_status);
	snmp_printf(" error_index=%d\n", pdu->error_index);
	dump_bindings(pdu);
}

void
snmp_pdu_dump(const struct snmp_pdu *pdu)
{
	char buf[ASN_OIDSTRLEN];
	const char *vers;
	static const char *types[] = {
		[SNMP_PDU_GET] =	"GET",
		[SNMP_PDU_GETNEXT] =	"GETNEXT",
		[SNMP_PDU_RESPONSE] =	"RESPONSE",
		[SNMP_PDU_SET] =	"SET",
		[SNMP_PDU_TRAP] =	"TRAPv1",
		[SNMP_PDU_GETBULK] =	"GETBULK",
		[SNMP_PDU_INFORM] =	"INFORM",
		[SNMP_PDU_TRAP2] =	"TRAPv2",
		[SNMP_PDU_REPORT] =	"REPORT",
	};

	if (pdu->version == SNMP_V1)
		vers = "SNMPv1";
	else if (pdu->version == SNMP_V2c)
		vers = "SNMPv2c";
	else if (pdu->version == SNMP_V3)
		vers = "SNMPv3";
	else
		vers = "v?";

	switch (pdu->type) {
	  case SNMP_PDU_TRAP:
		snmp_printf("%s %s '%s'", types[pdu->type], vers, pdu->community);
		snmp_printf(" enterprise=%s", asn_oid2str_r(&pdu->enterprise, buf));
		snmp_printf(" agent_addr=%u.%u.%u.%u", pdu->agent_addr[0],
		    pdu->agent_addr[1], pdu->agent_addr[2], pdu->agent_addr[3]);
		snmp_printf(" generic_trap=%d", pdu->generic_trap);
		snmp_printf(" specific_trap=%d", pdu->specific_trap);
		snmp_printf(" time-stamp=%u\n", pdu->time_stamp);
		dump_bindings(pdu);
		break;

	  case SNMP_PDU_GET:
	  case SNMP_PDU_GETNEXT:
	  case SNMP_PDU_RESPONSE:
	  case SNMP_PDU_SET:
	  case SNMP_PDU_GETBULK:
	  case SNMP_PDU_INFORM:
	  case SNMP_PDU_TRAP2:
	  case SNMP_PDU_REPORT:
		snmp_printf("%s %s '%s'", types[pdu->type], vers, pdu->community);
		dump_notrap(pdu);
		break;

	  default:
		snmp_printf("bad pdu type %u\n", pdu->type);
		break;
	}
}

void
snmp_value_free(struct snmp_value *value)
{

	if (value->syntax == SNMP_SYNTAX_OCTETSTRING) {
		free(value->v.octetstring.octets);
		value->v.octetstring.octets = NULL;
	}
	value->syntax = SNMP_SYNTAX_NULL;
}

int
snmp_value_copy(struct snmp_value *to, const struct snmp_value *from)
{
	to->var = from->var;
	to->syntax = from->syntax;

	if (from->syntax == SNMP_SYNTAX_OCTETSTRING) {
		if ((to->v.octetstring.len = from->v.octetstring.len) == 0)
			to->v.octetstring.octets = NULL;
		else {
			to->v.octetstring.octets = malloc(to->v.octetstring.len);
			if (to->v.octetstring.octets == NULL)
				return (-1);
			(void)memcpy(to->v.octetstring.octets,
			    from->v.octetstring.octets, to->v.octetstring.len);
		}
	} else
		to->v = from->v;
	return (0);
}

void
snmp_pdu_init_secparams(struct snmp_pdu *pdu)
{
	int32_t rval;

	if (pdu->user.auth_proto != SNMP_AUTH_NOAUTH)
		pdu->flags |= SNMP_MSG_AUTH_FLAG;

	switch (pdu->user.priv_proto) {
	case SNMP_PRIV_DES:
		memcpy(pdu->msg_salt, &pdu->engine.engine_boots,
		    sizeof(pdu->engine.engine_boots));
		rval = random();
		memcpy(pdu->msg_salt + sizeof(pdu->engine.engine_boots), &rval,
		    sizeof(int32_t));
		pdu->flags |= SNMP_MSG_PRIV_FLAG;
		break;
	case SNMP_PRIV_AES:
		rval = random();
		memcpy(pdu->msg_salt, &rval, sizeof(int32_t));
		rval = random();
		memcpy(pdu->msg_salt + sizeof(int32_t), &rval, sizeof(int32_t));
		pdu->flags |= SNMP_MSG_PRIV_FLAG;
		break;
	default:
		break;
	}
}

void
snmp_pdu_free(struct snmp_pdu *pdu)
{
	u_int i;

	for (i = 0; i < pdu->nbindings; i++)
		snmp_value_free(&pdu->bindings[i]);
	pdu->nbindings = 0;
}

/*
 * Parse an ASCII SNMP value into the binary form
 */
int
snmp_value_parse(const char *str, enum snmp_syntax syntax, union snmp_values *v)
{
	char *end;

	switch (syntax) {

	  case SNMP_SYNTAX_NULL:
	  case SNMP_SYNTAX_NOSUCHOBJECT:
	  case SNMP_SYNTAX_NOSUCHINSTANCE:
	  case SNMP_SYNTAX_ENDOFMIBVIEW:
		if (*str != '\0')
			return (-1);
		return (0);

	  case SNMP_SYNTAX_INTEGER:
		v->integer = strtoll(str, &end, 0);
		if (*end != '\0')
			return (-1);
		return (0);

	  case SNMP_SYNTAX_OCTETSTRING:
	    {
		u_long len;	/* actual length of string */
		u_long alloc;	/* allocate length of string */
		u_char *octs;	/* actual octets */
		u_long oct;	/* actual octet */
		u_char *nocts;	/* to avoid memory leak */
		u_char c;	/* actual character */

# define STUFFC(C)							\
		if (alloc == len) {					\
			alloc += 100;					\
			if ((nocts = realloc(octs, alloc)) == NULL) {	\
				free(octs);				\
				return (-1);				\
			}						\
			octs = nocts;					\
		}							\
		octs[len++] = (C);

		len = alloc = 0;
		octs = NULL;

		if (*str == '"') {
			str++;
			while((c = *str++) != '\0') {
				if (c == '"') {
					if (*str != '\0') {
						free(octs);
						return (-1);
					}
					break;
				}
				if (c == '\\') {
					switch (c = *str++) {

					  case '\\':
						break;
					  case 'a':
						c = '\a';
						break;
					  case 'b':
						c = '\b';
						break;
					  case 'f':
						c = '\f';
						break;
					  case 'n':
						c = '\n';
						break;
					  case 'r':
						c = '\r';
						break;
					  case 't':
						c = '\t';
						break;
					  case 'v':
						c = '\v';
						break;
					  case 'x':
						c = 0;
						if (!isxdigit(*str))
							break;
						if (isdigit(*str))
							c = *str++ - '0';
						else if (isupper(*str))
							c = *str++ - 'A' + 10;
						else
							c = *str++ - 'a' + 10;
						if (!isxdigit(*str))
							break;
						if (isdigit(*str))
							c += *str++ - '0';
						else if (isupper(*str))
							c += *str++ - 'A' + 10;
						else
							c += *str++ - 'a' + 10;
						break;
					  case '0': case '1': case '2':
					  case '3': case '4': case '5':
					  case '6': case '7':
						c = *str++ - '0';
						if (*str < '0' || *str > '7')
							break;
						c = *str++ - '0';
						if (*str < '0' || *str > '7')
							break;
						c = *str++ - '0';
						break;
					  default:
						break;
					}
				}
				STUFFC(c);
			}
		} else {
			while (*str != '\0') {
				oct = strtoul(str, &end, 16);
				str = end;
				if (oct > 0xff) {
					free(octs);
					return (-1);
				}
				STUFFC(oct);
				if (*str == ':')
					str++;
				else if(*str != '\0') {
					free(octs);
					return (-1);
				}
			}
		}
		v->octetstring.octets = octs;
		v->octetstring.len = len;
		return (0);
# undef STUFFC
	    }

	  case SNMP_SYNTAX_OID:
	    {
		u_long subid;

		v->oid.len = 0;

		for (;;) {
			if (v->oid.len == ASN_MAXOIDLEN)
				return (-1);
			subid = strtoul(str, &end, 10);
			str = end;
			if (subid > ASN_MAXID)
				return (-1);
			v->oid.subs[v->oid.len++] = (asn_subid_t)subid;
			if (*str == '\0')
				break;
			if (*str != '.')
				return (-1);
			str++;
		}
		return (0);
	    }

	  case SNMP_SYNTAX_IPADDRESS:
	    {
		struct hostent *he;

		if (inet_pton(AF_INET, str, &v->ipaddress) == 1)
			return (0);
		if ((he = gethostbyname2(str, AF_INET)) == NULL)
			return (-1);
		if (he->h_addrtype != AF_INET)
			return (-1);

		memcpy(v->ipaddress, he->h_addr, sizeof(v->ipaddress));

		return (0);
	    }

	  case SNMP_SYNTAX_COUNTER:
	  case SNMP_SYNTAX_GAUGE:
	  case SNMP_SYNTAX_TIMETICKS:
	    {
		uint64_t sub;

		sub = strtoull(str, &end, 0);
		if (*end != '\0' || sub > 0xffffffff)
			return (-1);
		v->uint32 = (uint32_t)sub;
		return (0);
	    }

	  case SNMP_SYNTAX_COUNTER64:
		v->counter64 = strtoull(str, &end, 0);
		if (*end != '\0')
			return (-1);
		return (0);
	}
	abort();
}

static void
snmp_error_func(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "SNMP: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

static void
snmp_printf_func(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}
