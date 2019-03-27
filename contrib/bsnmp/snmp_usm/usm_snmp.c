/*-
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Shteryana Sotirova Shopova under
 * sponsorship from the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#include <sys/queue.h>
#include <sys/types.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include "asn1.h"
#include "snmp.h"
#include "snmpmod.h"

#define	SNMPTREE_TYPES
#include "usm_tree.h"
#include "usm_oid.h"

static struct lmodule *usm_module;
/* For the registration. */
static const struct asn_oid oid_usm = OIDX_snmpUsmMIB;

static const struct asn_oid oid_usmNoAuthProtocol = OIDX_usmNoAuthProtocol;
static const struct asn_oid oid_usmHMACMD5AuthProtocol =		\
    OIDX_usmHMACMD5AuthProtocol;
static const struct asn_oid oid_usmHMACSHAAuthProtocol =		\
    OIDX_usmHMACSHAAuthProtocol;

static const struct asn_oid oid_usmNoPrivProtocol = OIDX_usmNoPrivProtocol;
static const struct asn_oid oid_usmDESPrivProtocol = OIDX_usmDESPrivProtocol;
static const struct asn_oid oid_usmAesCfb128Protocol = OIDX_usmAesCfb128Protocol;

static const struct asn_oid oid_usmUserSecurityName = OIDX_usmUserSecurityName;

/* The registration. */
static uint reg_usm;

static int32_t usm_lock;

static struct usm_user *	usm_get_user(const struct asn_oid *, uint);
static struct usm_user *	usm_get_next_user(const struct asn_oid *, uint);
static void	usm_append_userindex(struct asn_oid *, uint,
    const struct usm_user *);
static int	usm_user_index_decode(const struct asn_oid *, uint, uint8_t *,
    uint32_t *, char *);

int
op_usm_stats(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub __unused, uint32_t iidx __unused, enum snmp_op op)
{
	struct snmpd_usmstat *usmstats;

	if (op == SNMP_OP_SET)
		return (SNMP_ERR_NOT_WRITEABLE);

	if ((usmstats = bsnmpd_get_usm_stats()) == NULL)
		return (SNMP_ERR_GENERR);

	if (op == SNMP_OP_GET) {
		switch (val->var.subs[sub - 1]) {
		case LEAF_usmStatsUnsupportedSecLevels:
			val->v.uint32 = usmstats->unsupported_seclevels;
			break;
		case LEAF_usmStatsNotInTimeWindows:
			val->v.uint32 = usmstats->not_in_time_windows;
			break;
		case LEAF_usmStatsUnknownUserNames:
			val->v.uint32 = usmstats->unknown_users;
			break;
		case LEAF_usmStatsUnknownEngineIDs:
			val->v.uint32 = usmstats->unknown_engine_ids;
			break;
		case LEAF_usmStatsWrongDigests:
			val->v.uint32 = usmstats->wrong_digests;
			break;
		case LEAF_usmStatsDecryptionErrors:
			val->v.uint32 = usmstats->decrypt_errors;
			break;
		default:
			return (SNMP_ERR_NOSUCHNAME);
		}
		return (SNMP_ERR_NOERROR);
	}
	abort();
}

int
op_usm_lock(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	if (val->var.subs[sub - 1] != LEAF_usmUserSpinLock)
		return (SNMP_ERR_NOSUCHNAME);

	switch (op) {
	case SNMP_OP_GET:
		if (++usm_lock == INT32_MAX)
			usm_lock = 0;
		val->v.integer = usm_lock;
		break;
	case SNMP_OP_GETNEXT:
		abort();
	case SNMP_OP_SET:
		if (val->v.integer != usm_lock)
			return (SNMP_ERR_INCONS_VALUE);
		break;
	case SNMP_OP_ROLLBACK:
		/* FALLTHROUGH */
	case SNMP_OP_COMMIT:
		break;
	}

	return (SNMP_ERR_NOERROR);
}

int
op_usm_users(struct snmp_context *ctx, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	uint32_t elen;
	struct usm_user *uuser, *clone;
	char uname[SNMP_ADM_STR32_SIZ];
	uint8_t eid[SNMP_ENGINE_ID_SIZ];

	switch (op) {
	case SNMP_OP_GET:
		if ((uuser = usm_get_user(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		if ((uuser = usm_get_next_user(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		usm_append_userindex(&val->var, sub, uuser);
		break;

	case SNMP_OP_SET:
		if ((uuser = usm_get_user(&val->var, sub)) == NULL &&
		    val->var.subs[sub - 1] != LEAF_usmUserStatus &&
		    val->var.subs[sub - 1] != LEAF_usmUserCloneFrom)
			return (SNMP_ERR_NOSUCHNAME);

		/*
		 * XXX (ngie): need to investigate the MIB to determine how
		 * this is possible given some of the transitions below.
		 */
		if (community != COMM_INITIALIZE &&
		    uuser != NULL && uuser->type == StorageType_readOnly)
			return (SNMP_ERR_NOT_WRITEABLE);

		switch (val->var.subs[sub - 1]) {
		case LEAF_usmUserSecurityName:
			return (SNMP_ERR_NOT_WRITEABLE);

		case LEAF_usmUserCloneFrom:
			if (uuser != NULL || usm_user_index_decode(&val->var,
			    sub, eid, &elen, uname) < 0 ||
			    !(asn_is_suboid(&oid_usmUserSecurityName, &val->v.oid)))
				return (SNMP_ERR_WRONG_VALUE);
			if ((clone = usm_get_user(&val->v.oid, sub)) == NULL)
				return (SNMP_ERR_INCONS_VALUE);
			if ((uuser = usm_new_user(eid, elen, uname)) == NULL)
				return (SNMP_ERR_GENERR);
			uuser->status = RowStatus_notReady;
			if (community != COMM_INITIALIZE)
				uuser->type = StorageType_volatile;
			else
				uuser->type = StorageType_readOnly;

			uuser->suser.auth_proto = clone->suser.auth_proto;
			uuser->suser.priv_proto = clone->suser.priv_proto;
			memcpy(uuser->suser.auth_key, clone->suser.auth_key,
			    sizeof(uuser->suser.auth_key));
			memcpy(uuser->suser.priv_key, clone->suser.priv_key,
			    sizeof(uuser->suser.priv_key));
			ctx->scratch->int1 = RowStatus_createAndWait;
			break;

		case LEAF_usmUserAuthProtocol:
			ctx->scratch->int1 = uuser->suser.auth_proto;
			if (asn_compare_oid(&oid_usmNoAuthProtocol,
			    &val->v.oid) == 0)
				uuser->suser.auth_proto = SNMP_AUTH_NOAUTH;
			else if (asn_compare_oid(&oid_usmHMACMD5AuthProtocol,
			    &val->v.oid) == 0)
				uuser->suser.auth_proto = SNMP_AUTH_HMAC_MD5;
			else if (asn_compare_oid(&oid_usmHMACSHAAuthProtocol,
			    &val->v.oid) == 0)
				uuser->suser.auth_proto = SNMP_AUTH_HMAC_SHA;
			else
				return (SNMP_ERR_WRONG_VALUE);
			break;

		case LEAF_usmUserAuthKeyChange:
		case LEAF_usmUserOwnAuthKeyChange:
			if (val->var.subs[sub - 1] ==
			    LEAF_usmUserOwnAuthKeyChange &&
			    (usm_user == NULL || strcmp(uuser->suser.sec_name,
			    usm_user->suser.sec_name) != 0))
				return (SNMP_ERR_NO_ACCESS);
			if (val->v.octetstring.len > SNMP_AUTH_KEY_SIZ)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->ptr1 = malloc(SNMP_AUTH_KEY_SIZ);
			if (ctx->scratch->ptr1 == NULL)
				return (SNMP_ERR_GENERR);
			memcpy(ctx->scratch->ptr1, uuser->suser.auth_key,
			    SNMP_AUTH_KEY_SIZ);
			memcpy(uuser->suser.auth_key, val->v.octetstring.octets,
			    val->v.octetstring.len);
			break;

		case LEAF_usmUserPrivProtocol:
			ctx->scratch->int1 = uuser->suser.priv_proto;
			if (asn_compare_oid(&oid_usmNoPrivProtocol,
			    &val->v.oid) == 0)
				uuser->suser.priv_proto = SNMP_PRIV_NOPRIV;
			else if (asn_compare_oid(&oid_usmDESPrivProtocol,
			    &val->v.oid) == 0)
				uuser->suser.priv_proto = SNMP_PRIV_DES;
			else if (asn_compare_oid(&oid_usmAesCfb128Protocol,
			    &val->v.oid) == 0)
				uuser->suser.priv_proto = SNMP_PRIV_AES;
			else
				return (SNMP_ERR_WRONG_VALUE);
			break;

		case LEAF_usmUserPrivKeyChange:
		case LEAF_usmUserOwnPrivKeyChange:
			if (val->var.subs[sub - 1] ==
			    LEAF_usmUserOwnPrivKeyChange &&
			    (usm_user == NULL || strcmp(uuser->suser.sec_name,
			    usm_user->suser.sec_name) != 0))
				return (SNMP_ERR_NO_ACCESS);
			if (val->v.octetstring.len > SNMP_PRIV_KEY_SIZ)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->ptr1 = malloc(SNMP_PRIV_KEY_SIZ);
			if (ctx->scratch->ptr1 == NULL)
				return (SNMP_ERR_GENERR);
			memcpy(ctx->scratch->ptr1, uuser->suser.priv_key,
			    sizeof(uuser->suser.priv_key));
			memcpy(uuser->suser.priv_key, val->v.octetstring.octets,
			    val->v.octetstring.len);
			break;

		case LEAF_usmUserPublic:
			if (val->v.octetstring.len > SNMP_ADM_STR32_SIZ)
				return (SNMP_ERR_INCONS_VALUE);
			if (uuser->user_public_len > 0) {
				ctx->scratch->ptr2 =
				    malloc(uuser->user_public_len);
				if (ctx->scratch->ptr2 == NULL)
					return (SNMP_ERR_GENERR);
				memcpy(ctx->scratch->ptr2, uuser->user_public,
			 	   uuser->user_public_len);
				ctx->scratch->int2 = uuser->user_public_len;
			}
			if (val->v.octetstring.len > 0) {
				memcpy(uuser->user_public,
				    val->v.octetstring.octets,
				    val->v.octetstring.len);
				uuser->user_public_len = val->v.octetstring.len;
			} else {
				memset(uuser->user_public, 0,
				    sizeof(uuser->user_public));
				uuser->user_public_len = 0;
			}
			break;

		case LEAF_usmUserStorageType:
			return (SNMP_ERR_INCONS_VALUE);

		case LEAF_usmUserStatus:
			if (uuser == NULL) {
				if (val->v.integer != RowStatus_createAndWait ||
				    usm_user_index_decode(&val->var, sub, eid,
				    &elen, uname) < 0)
					return (SNMP_ERR_INCONS_VALUE);
				uuser = usm_new_user(eid, elen, uname);
				if (uuser == NULL)
					return (SNMP_ERR_GENERR);
				uuser->status = RowStatus_notReady;
				if (community != COMM_INITIALIZE)
					uuser->type = StorageType_volatile;
				else
					uuser->type = StorageType_readOnly;
			} else if (val->v.integer != RowStatus_active &&
			    val->v.integer != RowStatus_destroy)
				return (SNMP_ERR_INCONS_VALUE);

			uuser->status = val->v.integer;
			break;
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		switch (val->var.subs[sub - 1]) {
		case LEAF_usmUserAuthKeyChange:
		case LEAF_usmUserOwnAuthKeyChange:
		case LEAF_usmUserPrivKeyChange:
		case LEAF_usmUserOwnPrivKeyChange:
			free(ctx->scratch->ptr1);
			break;
		case LEAF_usmUserPublic:
			if (ctx->scratch->ptr2 != NULL)
				free(ctx->scratch->ptr2);
			break;
		case LEAF_usmUserStatus:
			if (val->v.integer != RowStatus_destroy)
				break;
			if ((uuser = usm_get_user(&val->var, sub)) == NULL)
				return (SNMP_ERR_GENERR);
			usm_delete_user(uuser);
			break;
		default:
			break;
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		if ((uuser = usm_get_user(&val->var, sub)) == NULL)
			return (SNMP_ERR_GENERR);
		switch (val->var.subs[sub - 1]) {
		case LEAF_usmUserAuthProtocol:
			uuser->suser.auth_proto = ctx->scratch->int1;
			break;
		case LEAF_usmUserAuthKeyChange:
		case LEAF_usmUserOwnAuthKeyChange:
			memcpy(uuser->suser.auth_key, ctx->scratch->ptr1,
			    sizeof(uuser->suser.auth_key));
			free(ctx->scratch->ptr1);
			break;
		case LEAF_usmUserPrivProtocol:
			uuser->suser.priv_proto = ctx->scratch->int1;
			break;
		case LEAF_usmUserPrivKeyChange:
		case LEAF_usmUserOwnPrivKeyChange:
			memcpy(uuser->suser.priv_key, ctx->scratch->ptr1,
			    sizeof(uuser->suser.priv_key));
			free(ctx->scratch->ptr1);
			break;
		case LEAF_usmUserPublic:
			if (ctx->scratch->ptr2 != NULL) {
				memcpy(uuser->user_public, ctx->scratch->ptr2,
			 	   ctx->scratch->int2);
				uuser->user_public_len = ctx->scratch->int2;
				free(ctx->scratch->ptr2);
			} else {
				memset(uuser->user_public, 0,
				    sizeof(uuser->user_public));
				uuser->user_public_len = 0;
			}
			break;
		case LEAF_usmUserCloneFrom:
		case LEAF_usmUserStatus:
			if (ctx->scratch->int1 == RowStatus_createAndWait)
				usm_delete_user(uuser);
			break;
		default:
			break;
		}
		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_usmUserSecurityName:
		return (string_get(val, uuser->suser.sec_name, -1));
	case LEAF_usmUserCloneFrom:
		memcpy(&val->v.oid, &oid_zeroDotZero, sizeof(oid_zeroDotZero));
		break;
	case LEAF_usmUserAuthProtocol:
		switch (uuser->suser.auth_proto) {
		case SNMP_AUTH_HMAC_MD5:
			memcpy(&val->v.oid, &oid_usmHMACMD5AuthProtocol,
			    sizeof(oid_usmHMACMD5AuthProtocol));
			break;
		case SNMP_AUTH_HMAC_SHA:
			memcpy(&val->v.oid, &oid_usmHMACSHAAuthProtocol,
			    sizeof(oid_usmHMACSHAAuthProtocol));
			break;
		default:
			memcpy(&val->v.oid, &oid_usmNoAuthProtocol,
			    sizeof(oid_usmNoAuthProtocol));
			break;
		}
		break;
	case LEAF_usmUserAuthKeyChange:
	case LEAF_usmUserOwnAuthKeyChange:
		return (string_get(val, (char *)uuser->suser.auth_key, 0));
	case LEAF_usmUserPrivProtocol:
		switch (uuser->suser.priv_proto) {
		case SNMP_PRIV_DES:
			memcpy(&val->v.oid, &oid_usmDESPrivProtocol,
			    sizeof(oid_usmDESPrivProtocol));
			break;
		case SNMP_PRIV_AES:
			memcpy(&val->v.oid, &oid_usmAesCfb128Protocol,
			    sizeof(oid_usmAesCfb128Protocol));
			break;
		default:
			memcpy(&val->v.oid, &oid_usmNoPrivProtocol,
			    sizeof(oid_usmNoPrivProtocol));
			break;
		}
		break;
	case LEAF_usmUserPrivKeyChange:
	case LEAF_usmUserOwnPrivKeyChange:
		return (string_get(val, (char *)uuser->suser.priv_key, 0));
	case LEAF_usmUserPublic:
		return (string_get(val, uuser->user_public,
		    uuser->user_public_len));
	case LEAF_usmUserStorageType:
		val->v.integer = uuser->type;
		break;
	case LEAF_usmUserStatus:
		val->v.integer = uuser->status;
		break;
	}

	return (SNMP_ERR_NOERROR);
}

static int
usm_user_index_decode(const struct asn_oid *oid, uint sub, uint8_t *engine,
    uint32_t *elen, char *uname)
{
	uint32_t i, nlen;
	int uname_off;

	if (oid->subs[sub] > SNMP_ENGINE_ID_SIZ)
		return (-1);

	for (i = 0; i < oid->subs[sub]; i++)
		engine[i] = oid->subs[sub + i + 1];
	*elen = i;

	uname_off = sub + oid->subs[sub] + 1;
	if ((nlen = oid->subs[uname_off]) >= SNMP_ADM_STR32_SIZ)
		return (-1);

	for (i = 0; i < nlen; i++)
		uname[i] = oid->subs[uname_off + i + 1];
	uname[nlen] = '\0';

	return (0);
}

static void
usm_append_userindex(struct asn_oid *oid, uint sub,
    const struct usm_user *uuser)
{
	uint32_t i;

	oid->len = sub + uuser->user_engine_len + strlen(uuser->suser.sec_name);
	oid->len += 2;
	oid->subs[sub] = uuser->user_engine_len;
	for (i = 1; i < uuser->user_engine_len + 1; i++)
		oid->subs[sub + i] = uuser->user_engine_id[i - 1];

	sub += uuser->user_engine_len + 1;
	oid->subs[sub] = strlen(uuser->suser.sec_name);
	for (i = 1; i <= oid->subs[sub]; i++)
		oid->subs[sub + i] = uuser->suser.sec_name[i - 1];
}

static struct usm_user *
usm_get_user(const struct asn_oid *oid, uint sub)
{
	uint32_t enginelen;
	char username[SNMP_ADM_STR32_SIZ];
	uint8_t engineid[SNMP_ENGINE_ID_SIZ];

	if (usm_user_index_decode(oid, sub, engineid, &enginelen, username) < 0)
		return (NULL);

	return (usm_find_user(engineid, enginelen, username));
}

static struct usm_user *
usm_get_next_user(const struct asn_oid *oid, uint sub)
{
	uint32_t enginelen;
	char username[SNMP_ADM_STR32_SIZ];
	uint8_t engineid[SNMP_ENGINE_ID_SIZ];
	struct usm_user *uuser;

	if (oid->len - sub == 0)
		return (usm_first_user());

	if (usm_user_index_decode(oid, sub, engineid, &enginelen, username) < 0)
		return (NULL);

	if ((uuser = usm_find_user(engineid, enginelen, username)) != NULL)
		return (usm_next_user(uuser));

	return (NULL);
}

/*
 * USM snmp module initialization hook.
 * Returns 0 on success, < 0 on error.
 */
static int
usm_init(struct lmodule * mod, int argc __unused, char *argv[] __unused)
{
	usm_module = mod;
	usm_lock = random();
	bsnmpd_reset_usm_stats();
	return (0);
}

/*
 * USM snmp module finalization hook.
 */
static int
usm_fini(void)
{
	usm_flush_users();
	or_unregister(reg_usm);

	return (0);
}

/*
 * USM snmp module start operation.
 */
static void
usm_start(void)
{
	reg_usm = or_register(&oid_usm,
	    "The MIB module for managing SNMP User-Based Security Model.",
	    usm_module);
}

static void
usm_dump(void)
{
	struct usm_user *uuser;
	struct snmpd_usmstat *usmstats;
	const char *const authstr[] = {
		"noauth",
		"md5",
		"sha",
		NULL
	};
	const char *const privstr[] = {
		"nopriv",
		"des",
		"aes",
		NULL
	};

	if ((usmstats = bsnmpd_get_usm_stats()) != NULL) {
		syslog(LOG_ERR, "UnsupportedSecLevels\t\t%u",
		    usmstats->unsupported_seclevels);
		syslog(LOG_ERR, "NotInTimeWindows\t\t%u",
		    usmstats->not_in_time_windows);
		syslog(LOG_ERR, "UnknownUserNames\t\t%u",
		    usmstats->unknown_users);
		syslog(LOG_ERR, "UnknownEngineIDs\t\t%u",
		    usmstats->unknown_engine_ids);
		syslog(LOG_ERR, "WrongDigests\t\t%u",
		    usmstats->wrong_digests);
		syslog(LOG_ERR, "DecryptionErrors\t\t%u",
		    usmstats->decrypt_errors);
	}

	syslog(LOG_ERR, "USM users");
	for (uuser = usm_first_user(); uuser != NULL;
	    (uuser = usm_next_user(uuser)))
		syslog(LOG_ERR, "user %s\t\t%s, %s", uuser->suser.sec_name,
		    authstr[uuser->suser.auth_proto],
		    privstr[uuser->suser.priv_proto]);
}

static const char usm_comment[] = \
"This module implements SNMP User-based Security Model defined in RFC 3414.";

extern const struct snmp_module config;
const struct snmp_module config = {
	.comment =	usm_comment,
	.init =		usm_init,
	.fini =		usm_fini,
	.start =	usm_start,
	.tree =		usm_ctree,
	.dump =		usm_dump,
	.tree_size =	usm_CTREE_SIZE,
};
