/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
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
 * $Begemot: bsnmp/snmpd/export.c,v 1.8 2006/02/14 09:04:20 brandt_h Exp $
 *
 * Support functions for modules.
 */
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <stdarg.h>

#include "snmpmod.h"
#include "snmpd.h"
#include "tree.h"

/*
 * Support functions
 */

/*
 * This is user for SET of string variables. If 'req' is not -1 then
 * the arguments is checked to be of that length. The old value is saved
 * in scratch->ptr1 and the new value is allocated and copied.
 * If there is an old values it must have been allocated by malloc.
 */
int
string_save(struct snmp_value *value, struct snmp_context *ctx,
    ssize_t req_size, u_char **valp)
{
	if (req_size != -1 && value->v.octetstring.len != (u_long)req_size)
		return (SNMP_ERR_BADVALUE);

	ctx->scratch->ptr1 = *valp;

	if ((*valp = malloc(value->v.octetstring.len + 1)) == NULL) {
		*valp = ctx->scratch->ptr1;
		return (SNMP_ERR_RES_UNAVAIL);
	}

	memcpy(*valp, value->v.octetstring.octets, value->v.octetstring.len);
	(*valp)[value->v.octetstring.len] = '\0';

	return (0);
}

/*
 * Commit a string. This is easy - free the old value.
 */
void
string_commit(struct snmp_context *ctx)
{
	free(ctx->scratch->ptr1);
}

/*
 * Rollback a string - free new value and copy back old one.
 */
void
string_rollback(struct snmp_context *ctx, u_char **valp)
{
	free(*valp);
	*valp = ctx->scratch->ptr1;
}

/*
 * ROLLBACK or COMMIT fails because instance has disappeared. Free string.
 */
void
string_free(struct snmp_context *ctx)
{
	free(ctx->scratch->ptr1);
}

/*
 * Get a string value for a response packet
 */
int
string_get(struct snmp_value *value, const u_char *ptr, ssize_t len)
{
	if (ptr == NULL) {
		value->v.octetstring.len = 0;
		value->v.octetstring.octets = NULL;
		return (SNMP_ERR_NOERROR);
	}
	if (len == -1)
		len = strlen(ptr);
	if ((value->v.octetstring.octets = malloc((size_t)len)) == NULL) {
		value->v.octetstring.len = 0;
		return (SNMP_ERR_RES_UNAVAIL);
	}
	value->v.octetstring.len = (u_long)len;
	memcpy(value->v.octetstring.octets, ptr, (size_t)len);
	return (SNMP_ERR_NOERROR);
}

/*
 * Get a string value for a response packet but cut it if it is too long.
 */
int
string_get_max(struct snmp_value *value, const u_char *ptr, ssize_t len,
    size_t maxlen)
{

	if (ptr == NULL) {
		value->v.octetstring.len = 0;
		value->v.octetstring.octets = NULL;
		return (SNMP_ERR_NOERROR);
	}
	if (len == -1)
		len = strlen(ptr);
	if ((size_t)len > maxlen)
		len = maxlen;
	if ((value->v.octetstring.octets = malloc((size_t)len)) == NULL) {
		value->v.octetstring.len = 0;
		return (SNMP_ERR_RES_UNAVAIL);
	}
	value->v.octetstring.len = (u_long)len;
	memcpy(value->v.octetstring.octets, ptr, (size_t)len);
	return (SNMP_ERR_NOERROR);
}

/*
 * Support for IPADDRESS
 *
 * Save the old IP address in scratch->int1 and set the new one.
 */
int
ip_save(struct snmp_value *value, struct snmp_context *ctx, u_char *valp)
{
	ctx->scratch->int1 = (valp[0] << 24) | (valp[1] << 16) | (valp[2] << 8)
	    | valp[3];

	valp[0] = value->v.ipaddress[0];
	valp[1] = value->v.ipaddress[1];
	valp[2] = value->v.ipaddress[2];
	valp[3] = value->v.ipaddress[3];

	return (0);
}

/*
 * Rollback the address by copying back the old one
 */
void
ip_rollback(struct snmp_context *ctx, u_char *valp)
{
	valp[0] = ctx->scratch->int1 >> 24;
	valp[1] = ctx->scratch->int1 >> 16;
	valp[2] = ctx->scratch->int1 >> 8;
	valp[3] = ctx->scratch->int1;
}

/*
 * Nothing to do for commit
 */
void
ip_commit(struct snmp_context *ctx __unused)
{
}

/*
 * Retrieve an IP address
 */
int
ip_get(struct snmp_value *value, u_char *valp)
{
	value->v.ipaddress[0] = valp[0];
	value->v.ipaddress[1] = valp[1];
	value->v.ipaddress[2] = valp[2];
	value->v.ipaddress[3] = valp[3];

	return (SNMP_ERR_NOERROR);
}

/*
 * Object ID support
 *
 * Save the old value in a fresh allocated oid pointed to by scratch->ptr1.
 */
int
oid_save(struct snmp_value *value, struct snmp_context *ctx,
    struct asn_oid *oid)
{
	if ((ctx->scratch->ptr1 = malloc(sizeof(struct asn_oid))) == NULL)
		return (SNMP_ERR_RES_UNAVAIL);
	*(struct asn_oid *)ctx->scratch->ptr1 = *oid;
	*oid = value->v.oid;

	return (0);
}

void
oid_rollback(struct snmp_context *ctx, struct asn_oid *oid)
{
	*oid = *(struct asn_oid *)ctx->scratch->ptr1;
	free(ctx->scratch->ptr1);
}

void
oid_commit(struct snmp_context *ctx)
{
	free(ctx->scratch->ptr1);
}

int
oid_get(struct snmp_value *value, const struct asn_oid *oid)
{
	value->v.oid = *oid;
	return (SNMP_ERR_NOERROR);
}

/*
 * Decode an index
 */
int
index_decode(const struct asn_oid *oid, u_int sub, u_int code, ...)
{
	va_list ap;
	u_int index_count;
	void *octs[10];
	u_int nocts;
	u_int idx;

	va_start(ap, code);
	index_count = SNMP_INDEX_COUNT(code);
	nocts = 0;

	for (idx = 0; idx < index_count; idx++) {
		switch (SNMP_INDEX(code, idx)) {

		  case SNMP_SYNTAX_NULL:
			break;

		  case SNMP_SYNTAX_INTEGER:
			if (sub == oid->len)
				goto err;
			*va_arg(ap, int32_t *) = oid->subs[sub++];
			break;

		  case SNMP_SYNTAX_COUNTER64:
			if (sub == oid->len)
				goto err;
			*va_arg(ap, u_int64_t *) = oid->subs[sub++];
			break;

		  case SNMP_SYNTAX_OCTETSTRING:
		    {
			u_char **cval;
			size_t *sval;
			u_int i;

			/* only variable size supported */
			if (sub == oid->len)
				goto err;
			cval = va_arg(ap, u_char **);
			sval = va_arg(ap, size_t *);
			*sval = oid->subs[sub++];
			if (sub + *sval > oid->len)
				goto err;
			if ((*cval = malloc(*sval)) == NULL) {
				syslog(LOG_ERR, "%s: %m", __func__);
				goto err;
			}
			octs[nocts++] = *cval;
			for (i = 0; i < *sval; i++) {
				if (oid->subs[sub] > 0xff)
					goto err;
				(*cval)[i] = oid->subs[sub++];
			}
			break;
		    }

		  case SNMP_SYNTAX_OID:
		    {
			struct asn_oid *aval;
			u_int i;

			if (sub == oid->len)
				goto err;
			aval = va_arg(ap, struct asn_oid *);
			aval->len = oid->subs[sub++];
			if (aval->len > ASN_MAXOIDLEN)
				goto err;
			for (i = 0; i < aval->len; i++)
				aval->subs[i] = oid->subs[sub++];
			break;
		    }

		  case SNMP_SYNTAX_IPADDRESS:
		    {
			u_int8_t *pval;
			u_int i;

			if (sub + 4 > oid->len)
				goto err;
			pval = va_arg(ap, u_int8_t *);
			for (i = 0; i < 4; i++) {
				if (oid->subs[sub] > 0xff)
					goto err;
				pval[i] = oid->subs[sub++];
			}
			break;
		    }

		  case SNMP_SYNTAX_COUNTER:
		  case SNMP_SYNTAX_GAUGE:
		  case SNMP_SYNTAX_TIMETICKS:
			if (sub == oid->len)
				goto err;
			if (oid->subs[sub] > 0xffffffff)
				goto err;
			*va_arg(ap, u_int32_t *) = oid->subs[sub++];
			break;
		}
	}

	va_end(ap);
	return (0);

  err:
	va_end(ap);
	while(nocts > 0)
		free(octs[--nocts]);
	return (-1);
}

/*
 * Compare the index part of an OID and an index.
 */
int
index_compare_off(const struct asn_oid *oid, u_int sub,
    const struct asn_oid *idx, u_int off)
{
	u_int i;

	for (i = off; i < idx->len && i < oid->len - sub; i++) {
		if (oid->subs[sub + i] < idx->subs[i])
			return (-1);
		if (oid->subs[sub + i] > idx->subs[i])
			return (+1);
	}
	if (oid->len - sub < idx->len)
		return (-1);
	if (oid->len - sub > idx->len)
		return (+1);

	return (0);
}

int
index_compare(const struct asn_oid *oid, u_int sub, const struct asn_oid *idx)
{
	return (index_compare_off(oid, sub, idx, 0));
}

/*
 * Append an index to an oid
 */
void
index_append_off(struct asn_oid *var, u_int sub, const struct asn_oid *idx,
    u_int off)
{
	u_int i;

	var->len = sub + idx->len;
	for (i = off; i < idx->len; i++)
		var->subs[sub + i] = idx->subs[i];
}
void
index_append(struct asn_oid *var, u_int sub, const struct asn_oid *idx)
{
	index_append_off(var, sub, idx, 0);
}

