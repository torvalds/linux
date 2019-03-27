/*-
 * Copyright (c) 2010,2018 The FreeBSD Foundation
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

#define SNMPTREE_TYPES
#include "target_tree.h"
#include "target_oid.h"

static struct lmodule *target_module;
/* For the registration. */
static const struct asn_oid oid_target = OIDX_snmpTargetMIB;
static const struct asn_oid oid_notification = OIDX_snmpNotificationMIB;

static uint reg_target;
static uint reg_notification;

static int32_t target_lock;

static const struct asn_oid oid_udp_domain = OIDX_snmpUDPDomain;

/*
 * Internal datastructures and forward declarations.
 */
static void		target_append_index(struct asn_oid *, uint,
    const char *);
static int		target_decode_index(const struct asn_oid *, uint,
    char *);
static struct target_address *target_get_address(const struct asn_oid *,
    uint);
static struct target_address *target_get_next_address(const struct asn_oid *,
    uint);
static struct target_param *target_get_param(const struct asn_oid *,
    uint);
static struct target_param *target_get_next_param(const struct asn_oid *,
    uint);
static struct target_notify *target_get_notify(const struct asn_oid *,
    uint);
static struct target_notify *target_get_next_notify(const struct asn_oid *,
    uint);

int
op_snmp_target(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	struct snmpd_target_stats *ctx_stats;

	if (val->var.subs[sub - 1] == LEAF_snmpTargetSpinLock) {
		switch (op) {
		case SNMP_OP_GET:
			if (++target_lock == INT32_MAX)
				target_lock = 0;
			val->v.integer = target_lock;
			break;
		case SNMP_OP_GETNEXT:
			abort();
		case SNMP_OP_SET:
			if (val->v.integer != target_lock)
				return (SNMP_ERR_INCONS_VALUE);
			break;
		case SNMP_OP_ROLLBACK:
			/* FALLTHROUGH */
		case SNMP_OP_COMMIT:
			break;
		}
		return (SNMP_ERR_NOERROR);
	} else if (op == SNMP_OP_SET)
		return (SNMP_ERR_NOT_WRITEABLE);

	if ((ctx_stats = bsnmpd_get_target_stats()) == NULL)
		return (SNMP_ERR_GENERR);

	if (op == SNMP_OP_GET) {
		switch (val->var.subs[sub - 1]) {
		case LEAF_snmpUnavailableContexts:
			val->v.uint32 = ctx_stats->unavail_contexts;
			break;
		case LEAF_snmpUnknownContexts:
			val->v.uint32 = ctx_stats->unknown_contexts;
			break;
		default:
			return (SNMP_ERR_NOSUCHNAME);
		}
		return (SNMP_ERR_NOERROR);
	}
	abort();
}

int
op_snmp_target_addrs(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	char aname[SNMP_ADM_STR32_SIZ];
	struct target_address *addrs;

	switch (op) {
	case SNMP_OP_GET:
		if ((addrs = target_get_address(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		if ((addrs = target_get_next_address(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		target_append_index(&val->var, sub, addrs->name);
		break;

	case SNMP_OP_SET:
		if ((addrs = target_get_address(&val->var, sub)) == NULL &&
		    (val->var.subs[sub - 1] != LEAF_snmpTargetAddrRowStatus ||
		    val->v.integer != RowStatus_createAndWait))
			return (SNMP_ERR_NOSUCHNAME);

		if (addrs != NULL) {
			if (community != COMM_INITIALIZE &&
			    addrs->type == StorageType_readOnly)
				return (SNMP_ERR_NOT_WRITEABLE);
			if (addrs->status == RowStatus_active &&
			    val->v.integer != RowStatus_destroy)
				return (SNMP_ERR_INCONS_VALUE);
		}

		switch (val->var.subs[sub - 1]) {
		case LEAF_snmpTargetAddrTDomain:
			return (SNMP_ERR_INCONS_VALUE);
		case LEAF_snmpTargetAddrTAddress:
			if (val->v.octetstring.len != SNMP_UDP_ADDR_SIZ)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->ptr1 = malloc(SNMP_UDP_ADDR_SIZ);
			if (ctx->scratch->ptr1 == NULL)
				return (SNMP_ERR_GENERR);
			memcpy(ctx->scratch->ptr1, addrs->address,
			    SNMP_UDP_ADDR_SIZ);
			memcpy(addrs->address, val->v.octetstring.octets,
			    SNMP_UDP_ADDR_SIZ);
			break;

		case LEAF_snmpTargetAddrTagList:
			if (val->v.octetstring.len >= SNMP_TAG_SIZ)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->int1 = strlen(addrs->taglist) + 1;
			ctx->scratch->ptr1 = malloc(ctx->scratch->int1);
			if (ctx->scratch->ptr1 == NULL)
				return (SNMP_ERR_GENERR);
			strlcpy(ctx->scratch->ptr1, addrs->taglist,
			    ctx->scratch->int1);
			memcpy(addrs->taglist, val->v.octetstring.octets,
			    val->v.octetstring.len);
			addrs->taglist[val->v.octetstring.len] = '\0';
			break;

		case LEAF_snmpTargetAddrParams:
			if (val->v.octetstring.len >= SNMP_ADM_STR32_SIZ)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->int1 = strlen(addrs->paramname) + 1;
			ctx->scratch->ptr1 = malloc(ctx->scratch->int1);
			if (ctx->scratch->ptr1 == NULL)
				return (SNMP_ERR_GENERR);
			strlcpy(ctx->scratch->ptr1, addrs->paramname,
			    ctx->scratch->int1);
			memcpy(addrs->paramname, val->v.octetstring.octets,
			    val->v.octetstring.len);
			addrs->paramname[val->v.octetstring.len] = '\0';
			break;

		case LEAF_snmpTargetAddrRetryCount:
			ctx->scratch->int1 = addrs->retry;
			addrs->retry = val->v.integer;
			break;

		case LEAF_snmpTargetAddrTimeout:
			ctx->scratch->int1 = addrs->timeout;
			addrs->timeout = val->v.integer / 10;
			break;

		case LEAF_snmpTargetAddrStorageType:
			return (SNMP_ERR_INCONS_VALUE);

		case LEAF_snmpTargetAddrRowStatus:
			if (addrs != NULL) {
				if (val->v.integer != RowStatus_active &&
				    val->v.integer != RowStatus_destroy)
					return (SNMP_ERR_INCONS_VALUE);
				if (val->v.integer == RowStatus_active &&
				    (addrs->address[0] == 0 ||
				    strlen(addrs->taglist) == 0 ||
				    strlen(addrs->paramname) == 0))
					return (SNMP_ERR_INCONS_VALUE);
				ctx->scratch->int1 = addrs->status;
				addrs->status = val->v.integer;
				return (SNMP_ERR_NOERROR);
			}
			if (val->v.integer != RowStatus_createAndWait ||
			    target_decode_index(&val->var, sub, aname) < 0)
				return (SNMP_ERR_INCONS_VALUE);
			if ((addrs = target_new_address(aname)) == NULL)
				return (SNMP_ERR_GENERR);
			addrs->status = RowStatus_destroy;
			if (community != COMM_INITIALIZE)
				addrs->type = StorageType_volatile;
			else
				addrs->type = StorageType_readOnly;
			break;
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		switch (val->var.subs[sub - 1]) {
		case LEAF_snmpTargetAddrTAddress:
		case LEAF_snmpTargetAddrTagList:
		case LEAF_snmpTargetAddrParams:
			free(ctx->scratch->ptr1);
			break;
		case LEAF_snmpTargetAddrRowStatus:
			if ((addrs = target_get_address(&val->var, sub)) == NULL)
				return (SNMP_ERR_GENERR);
			if (val->v.integer == RowStatus_destroy)
				return (target_delete_address(addrs));
			else if (val->v.integer == RowStatus_active)
				return (target_activate_address(addrs));
			break;
		default:
			break;
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		if ((addrs = target_get_address(&val->var, sub)) == NULL)
			return (SNMP_ERR_GENERR);

		switch (val->var.subs[sub - 1]) {
		case LEAF_snmpTargetAddrTAddress:
			memcpy(addrs->address, ctx->scratch->ptr1,
			    SNMP_UDP_ADDR_SIZ);
			free(ctx->scratch->ptr1);
			break;

		case LEAF_snmpTargetAddrTagList:
			strlcpy(addrs->taglist, ctx->scratch->ptr1,
			    ctx->scratch->int1);
			free(ctx->scratch->ptr1);
			break;

		case LEAF_snmpTargetAddrParams:
			strlcpy(addrs->paramname, ctx->scratch->ptr1,
			    ctx->scratch->int1);
			free(ctx->scratch->ptr1);
			break;

		case LEAF_snmpTargetAddrRetryCount:
			addrs->retry = ctx->scratch->int1;
			break;

		case LEAF_snmpTargetAddrTimeout:
			addrs->timeout = ctx->scratch->int1;
			break;

		case LEAF_snmpTargetAddrRowStatus:
			if (ctx->scratch->int1 == RowStatus_destroy)
				return (target_delete_address(addrs));
			break;
		default:
			break;
		}
		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_snmpTargetAddrTDomain:
		return (oid_get(val, &oid_udp_domain));
	case LEAF_snmpTargetAddrTAddress:
		return (string_get(val, addrs->address, SNMP_UDP_ADDR_SIZ));
	case LEAF_snmpTargetAddrTimeout:
		val->v.integer = addrs->timeout;
		break;
	case LEAF_snmpTargetAddrRetryCount:
		val->v.integer = addrs->retry;
		break;
	case LEAF_snmpTargetAddrTagList:
		return (string_get(val, addrs->taglist, -1));
	case LEAF_snmpTargetAddrParams:
		return (string_get(val, addrs->paramname, -1));
	case LEAF_snmpTargetAddrStorageType:
		val->v.integer = addrs->type;
		break;
	case LEAF_snmpTargetAddrRowStatus:
		val->v.integer = addrs->status;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_snmp_target_params(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	char pname[SNMP_ADM_STR32_SIZ];
	struct target_param *param;

	switch (op) {
	case SNMP_OP_GET:
		if ((param = target_get_param(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		if ((param = target_get_next_param(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		target_append_index(&val->var, sub, param->name);
		break;

	case SNMP_OP_SET:
		if ((param = target_get_param(&val->var, sub)) == NULL &&
		    (val->var.subs[sub - 1] != LEAF_snmpTargetParamsRowStatus ||
		    val->v.integer != RowStatus_createAndWait))
			return (SNMP_ERR_NOSUCHNAME);

		if (param != NULL) {
			if (community != COMM_INITIALIZE &&
			    param->type == StorageType_readOnly)
				return (SNMP_ERR_NOT_WRITEABLE);
			if (param->status == RowStatus_active &&
			    val->v.integer != RowStatus_destroy)
				return (SNMP_ERR_INCONS_VALUE);
		}

		switch (val->var.subs[sub - 1]) {
		case LEAF_snmpTargetParamsMPModel:
			if (val->v.integer != SNMP_MPM_SNMP_V1 &&
			    val->v.integer != SNMP_MPM_SNMP_V2c &&
			    val->v.integer != SNMP_MPM_SNMP_V3)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->int1 = param->mpmodel;
			param->mpmodel = val->v.integer;
			break;

		case LEAF_snmpTargetParamsSecurityModel:
			if (val->v.integer != SNMP_SECMODEL_SNMPv1 &&
			    val->v.integer != SNMP_SECMODEL_SNMPv2c &&
			    val->v.integer != SNMP_SECMODEL_USM)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->int1 = param->sec_model;
			param->sec_model = val->v.integer;
			break;

		case LEAF_snmpTargetParamsSecurityName:
			if (val->v.octetstring.len >= SNMP_ADM_STR32_SIZ)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->int1 = strlen(param->secname) + 1;
			ctx->scratch->ptr1 = malloc(ctx->scratch->int1);
			if (ctx->scratch->ptr1 == NULL)
				return (SNMP_ERR_GENERR);
			strlcpy(ctx->scratch->ptr1, param->secname,
			    ctx->scratch->int1);
			memcpy(param->secname, val->v.octetstring.octets,
			    val->v.octetstring.len);
			param->secname[val->v.octetstring.len] = '\0';
			break;

		case LEAF_snmpTargetParamsSecurityLevel:
			if (val->v.integer != SNMP_noAuthNoPriv &&
			    val->v.integer != SNMP_authNoPriv &&
			    val->v.integer != SNMP_authPriv)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->int1 = param->sec_level;
			param->sec_level = val->v.integer;
			break;

		case LEAF_snmpTargetParamsStorageType:
			return (SNMP_ERR_INCONS_VALUE);

		case LEAF_snmpTargetParamsRowStatus:
			if (param != NULL) {
				if (val->v.integer != RowStatus_active &&
				    val->v.integer != RowStatus_destroy)
					return (SNMP_ERR_INCONS_VALUE);
				if (val->v.integer == RowStatus_active &&
				    (param->sec_model == 0 ||
				    param->sec_level == 0 ||
				    strlen(param->secname) == 0))
					return (SNMP_ERR_INCONS_VALUE);
				ctx->scratch->int1 = param->status;
				param->status = val->v.integer;
				return (SNMP_ERR_NOERROR);
			}
			if (val->v.integer != RowStatus_createAndWait ||
			    target_decode_index(&val->var, sub, pname) < 0)
				return (SNMP_ERR_INCONS_VALUE);
			if ((param = target_new_param(pname)) == NULL)
				return (SNMP_ERR_GENERR);
			param->status = RowStatus_destroy;
			if (community != COMM_INITIALIZE)
				param->type = StorageType_volatile;
			else
				param->type = StorageType_readOnly;
			break;
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		switch (val->var.subs[sub - 1]) {
		case LEAF_snmpTargetParamsSecurityName:
			free(ctx->scratch->ptr1);
			break;
		case LEAF_snmpTargetParamsRowStatus:
			if ((param = target_get_param(&val->var, sub)) == NULL)
				return (SNMP_ERR_GENERR);
			if (val->v.integer == RowStatus_destroy)
				return (target_delete_param(param));
			break;
		default:
			break;
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		if ((param = target_get_param(&val->var, sub)) == NULL &&
		    (val->var.subs[sub - 1] != LEAF_snmpTargetParamsRowStatus ||
		    val->v.integer != RowStatus_createAndWait))
			return (SNMP_ERR_GENERR);
		switch (val->var.subs[sub - 1]) {
		case LEAF_snmpTargetParamsMPModel:
			param->mpmodel = ctx->scratch->int1;
			break;
		case LEAF_snmpTargetParamsSecurityModel:
			param->sec_model = ctx->scratch->int1;
			break;
		case LEAF_snmpTargetParamsSecurityName:
			strlcpy(param->secname, ctx->scratch->ptr1,
			    sizeof(param->secname));
			free(ctx->scratch->ptr1);
			break;
		case LEAF_snmpTargetParamsSecurityLevel:
			param->sec_level = ctx->scratch->int1;
			break;
		case LEAF_snmpTargetParamsRowStatus:
			if (ctx->scratch->int1 == RowStatus_destroy)
				return (target_delete_param(param));
			break;
		default:
			break;
		}

		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_snmpTargetParamsMPModel:
		val->v.integer = param->mpmodel;
		break;
	case LEAF_snmpTargetParamsSecurityModel:
		val->v.integer = param->sec_model;
		break;
	case LEAF_snmpTargetParamsSecurityName:
		return (string_get(val, param->secname, -1));
	case LEAF_snmpTargetParamsSecurityLevel:
		val->v.integer = param->sec_level;
		break;
	case LEAF_snmpTargetParamsStorageType:
		val->v.integer = param->type;
		break;
	case LEAF_snmpTargetParamsRowStatus:
		val->v.integer = param->status;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_snmp_notify(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	char nname[SNMP_ADM_STR32_SIZ];
	struct target_notify *notify;

	switch (op) {
	case SNMP_OP_GET:
		if ((notify = target_get_notify(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		if ((notify = target_get_next_notify(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		target_append_index(&val->var, sub, notify->name);
		break;

	case SNMP_OP_SET:
		if ((notify = target_get_notify(&val->var, sub)) == NULL &&
		    (val->var.subs[sub - 1] != LEAF_snmpNotifyRowStatus ||
		    val->v.integer != RowStatus_createAndGo))
			return (SNMP_ERR_NOSUCHNAME);

		if (notify != NULL) {
			if (community != COMM_INITIALIZE &&
			    notify->type == StorageType_readOnly)
				return (SNMP_ERR_NOT_WRITEABLE);
		}

		switch (val->var.subs[sub - 1]) {
		case LEAF_snmpNotifyTag:
			if (val->v.octetstring.len >= SNMP_TAG_SIZ)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->int1 = strlen(notify->taglist) + 1;
			ctx->scratch->ptr1 = malloc(ctx->scratch->int1);
			if (ctx->scratch->ptr1 == NULL)
				return (SNMP_ERR_GENERR);
			strlcpy(ctx->scratch->ptr1, notify->taglist,
			    ctx->scratch->int1);
			memcpy(notify->taglist, val->v.octetstring.octets,
			    val->v.octetstring.len);
			notify->taglist[val->v.octetstring.len] = '\0';
			break;

		case LEAF_snmpNotifyType:
			/* FALLTHROUGH */
		case LEAF_snmpNotifyStorageType:
			return (SNMP_ERR_INCONS_VALUE);
		case LEAF_snmpNotifyRowStatus:
			if (notify != NULL) {
				if (val->v.integer != RowStatus_active &&
				    val->v.integer != RowStatus_destroy)
					return (SNMP_ERR_INCONS_VALUE);
				ctx->scratch->int1 = notify->status;
				notify->status = val->v.integer;
				return (SNMP_ERR_NOERROR);
			}
			if (val->v.integer != RowStatus_createAndGo ||
			    target_decode_index(&val->var, sub, nname) < 0)
				return (SNMP_ERR_INCONS_VALUE);
			if ((notify = target_new_notify(nname)) == NULL)
				return (SNMP_ERR_GENERR);
			notify->status = RowStatus_destroy;
			if (community != COMM_INITIALIZE)
				notify->type = StorageType_volatile;
			else
				notify->type = StorageType_readOnly;
			break;
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		switch (val->var.subs[sub - 1]) {
		case LEAF_snmpNotifyTag:
			free(ctx->scratch->ptr1);
			break;
		case LEAF_snmpNotifyRowStatus:
			notify = target_get_notify(&val->var, sub);
			if (notify == NULL)
				return (SNMP_ERR_GENERR);
			if (val->v.integer == RowStatus_destroy)
				return (target_delete_notify(notify));
			else
				notify->status = RowStatus_active;
			break;
		default:
			break;
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		if ((notify = target_get_notify(&val->var, sub)) == NULL)
			return (SNMP_ERR_GENERR);

		switch (val->var.subs[sub - 1]) {
		case LEAF_snmpNotifyTag:
			strlcpy(notify->taglist, ctx->scratch->ptr1,
			    ctx->scratch->int1);
			free(ctx->scratch->ptr1);
			break;
		case LEAF_snmpNotifyRowStatus:
			if (ctx->scratch->int1 == RowStatus_destroy)
				return (target_delete_notify(notify));
			break;
		default:
			break;
		}
		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}


	switch (val->var.subs[sub - 1]) {
	case LEAF_snmpNotifyTag:
		return (string_get(val, notify->taglist, -1));
	case LEAF_snmpNotifyType:
		val->v.integer = snmpNotifyType_trap;
		break;
	case LEAF_snmpNotifyStorageType:
		val->v.integer = notify->type;
		break;
	case LEAF_snmpNotifyRowStatus:
		val->v.integer = notify->status;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

static void
target_append_index(struct asn_oid *oid, uint sub, const char *name)
{
	uint32_t i;

	oid->len = sub + strlen(name);
	for (i = 0; i < strlen(name); i++)
		oid->subs[sub + i] = name[i];
}

static int
target_decode_index(const struct asn_oid *oid, uint sub, char *name)
{
	uint32_t i;

	if (oid->len - sub != oid->subs[sub] + 1 || oid->subs[sub] >=
	    SNMP_ADM_STR32_SIZ)
		return (-1);

	for (i = 0; i < oid->subs[sub]; i++)
		name[i] = oid->subs[sub + i + 1];
	name[i] = '\0';

	return (0);
}

static struct target_address *
target_get_address(const struct asn_oid *oid, uint sub)
{
	char aname[SNMP_ADM_STR32_SIZ];
	struct target_address *addrs;

	if (target_decode_index(oid, sub, aname) < 0)
		return (NULL);

	for (addrs = target_first_address(); addrs != NULL;
	    addrs = target_next_address(addrs))
		if (strcmp(aname, addrs->name) == 0)
			return (addrs);

	return (NULL);
}

static struct target_address *
target_get_next_address(const struct asn_oid * oid, uint sub)
{
	char aname[SNMP_ADM_STR32_SIZ];
	struct target_address *addrs;

	if (oid->len - sub == 0)
		return (target_first_address());

	if (target_decode_index(oid, sub, aname) < 0)
		return (NULL);

	for (addrs = target_first_address(); addrs != NULL;
	    addrs = target_next_address(addrs))
		if (strcmp(aname, addrs->name) == 0)
			return (target_next_address(addrs));

	return (NULL);
}

static struct target_param *
target_get_param(const struct asn_oid *oid, uint sub)
{
	char pname[SNMP_ADM_STR32_SIZ];
	struct target_param *param;

	if (target_decode_index(oid, sub, pname) < 0)
		return (NULL);

	for (param = target_first_param(); param != NULL;
	    param = target_next_param(param))
		if (strcmp(pname, param->name) == 0)
			return (param);

	return (NULL);
}

static struct target_param *
target_get_next_param(const struct asn_oid *oid, uint sub)
{
	char pname[SNMP_ADM_STR32_SIZ];
	struct target_param *param;

	if (oid->len - sub == 0)
		return (target_first_param());

	if (target_decode_index(oid, sub, pname) < 0)
		return (NULL);

	for (param = target_first_param(); param != NULL;
	    param = target_next_param(param))
		if (strcmp(pname, param->name) == 0)
			return (target_next_param(param));

	return (NULL);
}

static struct target_notify *
target_get_notify(const struct asn_oid *oid, uint sub)
{
	char nname[SNMP_ADM_STR32_SIZ];
	struct target_notify *notify;

	if (target_decode_index(oid, sub, nname) < 0)
		return (NULL);

	for (notify = target_first_notify(); notify != NULL;
	    notify = target_next_notify(notify))
		if (strcmp(nname, notify->name) == 0)
			return (notify);

	return (NULL);
}

static struct target_notify *
target_get_next_notify(const struct asn_oid *oid, uint sub)
{
	char nname[SNMP_ADM_STR32_SIZ];
	struct target_notify *notify;

	if (oid->len - sub == 0)
		return (target_first_notify());

	if (target_decode_index(oid, sub, nname) < 0)
		return (NULL);

	for (notify = target_first_notify(); notify != NULL;
	    notify = target_next_notify(notify))
		if (strcmp(nname, notify->name) == 0)
			return (target_next_notify(notify));

	return (NULL);
}

static int
target_init(struct lmodule *mod, int argc __unused, char *argv[] __unused)
{
	target_module = mod;
	target_lock = random();

	return (0);
}


static int
target_fini(void)
{
	target_flush_all();
	or_unregister(reg_target);
	or_unregister(reg_notification);

	return (0);
}

static void
target_start(void)
{
	reg_target = or_register(&oid_target,
	    "The MIB module for managing SNMP Management Targets.",
	    target_module);
	reg_notification = or_register(&oid_notification,
	    "The MIB module for configuring generation of SNMP notifications.",
	    target_module);
}

static void
target_dump(void)
{
	/* XXX: dump the module stats & list of mgmt targets */
}

static const char target_comment[] = \
"This module implements SNMP Management Target MIB Module defined in RFC 3413.";

extern const struct snmp_module config;
const struct snmp_module config = {
	.comment =	target_comment,
	.init =		target_init,
	.fini =		target_fini,
	.start =	target_start,
	.tree =		target_ctree,
	.dump =		target_dump,
	.tree_size =	target_CTREE_SIZE,
};
