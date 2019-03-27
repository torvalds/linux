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

#define	SNMPTREE_TYPES
#include "vacm_tree.h"
#include "vacm_oid.h"

static struct lmodule *vacm_module;
/* For the registration. */
static const struct asn_oid oid_vacm = OIDX_snmpVacmMIB;

static uint reg_vacm;

static int32_t vacm_lock;

/*
 * Internal datastructures and forward declarations.
 */
static void		vacm_append_userindex(struct asn_oid *,
    uint, const struct vacm_user *);
static int		vacm_user_index_decode(const struct asn_oid *,
    uint, int32_t *, char *);
static struct vacm_user *vacm_get_user(const struct asn_oid *,
    uint);
static struct vacm_user *vacm_get_next_user(const struct asn_oid *,
    uint);
static void		vacm_append_access_rule_index(struct asn_oid *,
    uint, const struct vacm_access *);
static int		vacm_access_rule_index_decode(const struct asn_oid *,
    uint, char *, char *, int32_t *, int32_t *);
static struct vacm_access *	vacm_get_access_rule(const struct asn_oid *,
    uint);
static struct vacm_access *	vacm_get_next_access_rule(const struct asn_oid *,
    uint);
static int		vacm_view_index_decode(const struct asn_oid *, uint,
    char *, struct asn_oid *);
static void		vacm_append_viewindex(struct asn_oid *, uint,
    const struct vacm_view *);
static struct vacm_view	*vacm_get_view(const struct asn_oid *, uint);
static struct vacm_view	*vacm_get_next_view(const struct asn_oid *, uint);
static struct vacm_view *vacm_get_view_by_name(u_char *, u_int);
static struct vacm_context	*vacm_get_context(const struct asn_oid *, uint);
static struct vacm_context	*vacm_get_next_context(const struct asn_oid *,
    uint);
static void			vacm_append_ctxindex(struct asn_oid *, uint,
    const struct vacm_context *);

int
op_vacm_context(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	char cname[SNMP_ADM_STR32_SIZ];
	size_t cnamelen;
	struct vacm_context *vacm_ctx;

	if (val->var.subs[sub - 1] != LEAF_vacmContextName)
		abort();

	switch (op) {
	case SNMP_OP_GET:
		if ((vacm_ctx = vacm_get_context(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		if ((vacm_ctx = vacm_get_next_context(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		vacm_append_ctxindex(&val->var, sub, vacm_ctx);
		break;

	case SNMP_OP_SET:
		if ((vacm_ctx = vacm_get_context(&val->var, sub)) != NULL)
			return (SNMP_ERR_WRONG_VALUE);
		if (community != COMM_INITIALIZE)
			return (SNMP_ERR_NOT_WRITEABLE);
		if (val->var.subs[sub] >= SNMP_ADM_STR32_SIZ)
			return (SNMP_ERR_WRONG_VALUE);
		if (index_decode(&val->var, sub, iidx, &cname, &cnamelen))
			return (SNMP_ERR_GENERR);
		cname[cnamelen] = '\0';
		if ((vacm_ctx = vacm_add_context(cname, reg_vacm)) == NULL)
			return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		/* FALLTHROUGH*/
	case SNMP_OP_ROLLBACK:
		return (SNMP_ERR_NOERROR);
	default:
		abort();
	}

	return (string_get(val, vacm_ctx->ctxname, -1));
}

int
op_vacm_security_to_group(struct snmp_context *ctx, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	int32_t smodel;
	char uname[SNMP_ADM_STR32_SIZ];
	struct vacm_user *user;

	switch (op) {
	case SNMP_OP_GET:
		if ((user = vacm_get_user(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		if ((user = vacm_get_next_user(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		vacm_append_userindex(&val->var, sub, user);
		break;

	case SNMP_OP_SET:
		if ((user = vacm_get_user(&val->var, sub)) == NULL &&
		    val->var.subs[sub - 1] != LEAF_vacmSecurityToGroupStatus)
			return (SNMP_ERR_NOSUCHNAME);

		if (user != NULL) {
			if (community != COMM_INITIALIZE &&
			    user->type == StorageType_readOnly)
				return (SNMP_ERR_NOT_WRITEABLE);
			if (user->status == RowStatus_active &&
			    val->v.integer != RowStatus_destroy)
				return (SNMP_ERR_INCONS_VALUE);
		}

		switch (val->var.subs[sub - 1]) {
		case LEAF_vacmGroupName:
			ctx->scratch->ptr1 = user->group->groupname;
			ctx->scratch->int1 = strlen(user->group->groupname);
			return (vacm_user_set_group(user,
			    val->v.octetstring.octets,val->v.octetstring.len));

		case LEAF_vacmSecurityToGroupStorageType:
			return (SNMP_ERR_INCONS_VALUE);

		case LEAF_vacmSecurityToGroupStatus:
			if (user == NULL) {
				if (val->v.integer != RowStatus_createAndGo ||
				    vacm_user_index_decode(&val->var, sub,
				    &smodel, uname) < 0)
					return (SNMP_ERR_INCONS_VALUE);
				user = vacm_new_user(smodel, uname);
				if (user == NULL)
					return (SNMP_ERR_GENERR);
				user->status = RowStatus_destroy;
				if (community != COMM_INITIALIZE)
					user->type = StorageType_volatile;
				else
					user->type = StorageType_readOnly;
			} else if (val->v.integer != RowStatus_active &&
			    val->v.integer != RowStatus_destroy)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->int1 = user->status;
			user->status = val->v.integer;
			break;
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		if (val->var.subs[sub - 1] != LEAF_vacmSecurityToGroupStatus)
			return (SNMP_ERR_NOERROR);
		if ((user = vacm_get_user(&val->var, sub)) == NULL)
			return (SNMP_ERR_GENERR);
		switch (val->v.integer) {
		case  RowStatus_destroy:
			return (vacm_delete_user(user));

		case RowStatus_createAndGo:
			user->status = RowStatus_active;
			break;

		default:
			break;
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		if ((user = vacm_get_user(&val->var, sub)) == NULL)
			return (SNMP_ERR_GENERR);
		switch (val->var.subs[sub - 1]) {
		case LEAF_vacmGroupName:
			return (vacm_user_set_group(user, ctx->scratch->ptr1,
			    ctx->scratch->int1));

		case LEAF_vacmSecurityToGroupStatus:
			if (ctx->scratch->int1 == RowStatus_destroy)
				return (vacm_delete_user(user));
			user->status = ctx->scratch->int1;
			break;

		default:
			break;
		}
		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_vacmGroupName:
		return (string_get(val, user->group->groupname, -1));
	case LEAF_vacmSecurityToGroupStorageType:
		val->v.integer = user->type;
		break;
	case LEAF_vacmSecurityToGroupStatus:
		val->v.integer = user->status;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_vacm_access(struct snmp_context *ctx, struct snmp_value *val, uint32_t sub,
    uint32_t iidx __unused, enum snmp_op op)
{
	int32_t smodel, slevel;
	char gname[SNMP_ADM_STR32_SIZ], cprefix[SNMP_ADM_STR32_SIZ];
	struct vacm_access *acl;

	switch (op) {
	case SNMP_OP_GET:
		if ((acl = vacm_get_access_rule(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		if ((acl = vacm_get_next_access_rule(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		vacm_append_access_rule_index(&val->var, sub, acl);
		break;

	case SNMP_OP_SET:
		if ((acl = vacm_get_access_rule(&val->var, sub)) == NULL &&
		    val->var.subs[sub - 1] != LEAF_vacmAccessStatus)
				return (SNMP_ERR_NOSUCHNAME);
		if (acl != NULL && community != COMM_INITIALIZE &&
		    acl->type == StorageType_readOnly)
			return (SNMP_ERR_NOT_WRITEABLE);

		switch (val->var.subs[sub - 1]) {
		case LEAF_vacmAccessContextMatch:
			ctx->scratch->int1 = acl->ctx_match;
			if (val->v.integer == vacmAccessContextMatch_exact)
				acl->ctx_match = 1;
			else if (val->v.integer == vacmAccessContextMatch_prefix)
				acl->ctx_match = 0;
			else
				return (SNMP_ERR_WRONG_VALUE);
			break;

		case LEAF_vacmAccessReadViewName:
			ctx->scratch->ptr1 = acl->read_view;
			acl->read_view = vacm_get_view_by_name(val->v.octetstring.octets, val->v.octetstring.len);
			if (acl->read_view == NULL) {
				acl->read_view = ctx->scratch->ptr1;
				return (SNMP_ERR_INCONS_VALUE);
			}
			return (SNMP_ERR_NOERROR);

		case LEAF_vacmAccessWriteViewName:
			ctx->scratch->ptr1 = acl->write_view;
			if ((acl->write_view =
			    vacm_get_view_by_name(val->v.octetstring.octets,
			    val->v.octetstring.len)) == NULL) {
				acl->write_view = ctx->scratch->ptr1;
				return (SNMP_ERR_INCONS_VALUE);
			}
			break;

		case LEAF_vacmAccessNotifyViewName:
			ctx->scratch->ptr1 = acl->notify_view;
			if ((acl->notify_view =
			    vacm_get_view_by_name(val->v.octetstring.octets,
			    val->v.octetstring.len)) == NULL) {
				acl->notify_view = ctx->scratch->ptr1;
				return (SNMP_ERR_INCONS_VALUE);
			}
			break;

		case LEAF_vacmAccessStorageType:
			return (SNMP_ERR_INCONS_VALUE);

		case LEAF_vacmAccessStatus:
			if (acl == NULL) {
				if (val->v.integer != RowStatus_createAndGo ||
				    vacm_access_rule_index_decode(&val->var,
				    sub, gname, cprefix, &smodel, &slevel) < 0)
					return (SNMP_ERR_INCONS_VALUE);
				if ((acl = vacm_new_access_rule(gname, cprefix,
				    smodel, slevel)) == NULL)
					return (SNMP_ERR_GENERR);
				acl->status = RowStatus_destroy;
				if (community != COMM_INITIALIZE)
					acl->type = StorageType_volatile;
				else
					acl->type = StorageType_readOnly;
			} else if (val->v.integer != RowStatus_active &&
			    val->v.integer != RowStatus_destroy)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->int1 = acl->status;
			acl->status = val->v.integer;
			break;
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		if (val->var.subs[sub - 1] != LEAF_vacmAccessStatus)
			return (SNMP_ERR_NOERROR);
		if ((acl = vacm_get_access_rule(&val->var, sub)) == NULL)
			return (SNMP_ERR_GENERR);
		if (val->v.integer == RowStatus_destroy)
			return (vacm_delete_access_rule(acl));
		else
			acl->status = RowStatus_active;
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		if ((acl = vacm_get_access_rule(&val->var, sub)) == NULL)
			return (SNMP_ERR_GENERR);
		switch (val->var.subs[sub - 1]) {
		case LEAF_vacmAccessContextMatch:
			acl->ctx_match = ctx->scratch->int1;
			break;
		case LEAF_vacmAccessReadViewName:
			acl->read_view = ctx->scratch->ptr1;
			break;
		case LEAF_vacmAccessWriteViewName:
			acl->write_view = ctx->scratch->ptr1;
			break;
		case LEAF_vacmAccessNotifyViewName:
			acl->notify_view = ctx->scratch->ptr1;
			break;
		case LEAF_vacmAccessStatus:
			if (ctx->scratch->int1 == RowStatus_destroy)
				return (vacm_delete_access_rule(acl));
		default:
			break;
		}
		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_vacmAccessContextMatch:
		return (string_get(val, acl->ctx_prefix, -1));
	case LEAF_vacmAccessReadViewName:
		if (acl->read_view != NULL)
			return (string_get(val, acl->read_view->viewname, -1));
		else
			return (string_get(val, NULL, 0));
	case LEAF_vacmAccessWriteViewName:
		if (acl->write_view != NULL)
			return (string_get(val, acl->write_view->viewname, -1));
		else
			return (string_get(val, NULL, 0));
	case LEAF_vacmAccessNotifyViewName:
		if (acl->notify_view != NULL)
			return (string_get(val, acl->notify_view->viewname, -1));
		else
			return (string_get(val, NULL, 0));
	case LEAF_vacmAccessStorageType:
		val->v.integer = acl->type;
		break;
	case LEAF_vacmAccessStatus:
		val->v.integer = acl->status;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

int
op_vacm_view_lock(struct snmp_context *ctx __unused, struct snmp_value *val,
    uint32_t sub, uint32_t iidx __unused, enum snmp_op op)
{
	if (val->var.subs[sub - 1] != LEAF_vacmViewSpinLock)
		return (SNMP_ERR_NOSUCHNAME);

	switch (op) {
	case SNMP_OP_GET:
		if (++vacm_lock == INT32_MAX)
			vacm_lock = 0;
		val->v.integer = vacm_lock;
		break;

	case SNMP_OP_GETNEXT:
		abort();

	case SNMP_OP_SET:
		if (val->v.integer != vacm_lock)
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
op_vacm_view(struct snmp_context *ctx, struct snmp_value *val, uint32_t sub,
    uint32_t iidx __unused, enum snmp_op op)
{
	char vname[SNMP_ADM_STR32_SIZ];
	struct asn_oid oid;
	struct vacm_view *view;

	switch (op) {
	case SNMP_OP_GET:
		if ((view = vacm_get_view(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_GETNEXT:
		if ((view = vacm_get_next_view(&val->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		vacm_append_viewindex(&val->var, sub, view);
		break;

	case SNMP_OP_SET:
		if ((view = vacm_get_view(&val->var, sub)) == NULL &&
		    val->var.subs[sub - 1] != LEAF_vacmViewTreeFamilyStatus)
				return (SNMP_ERR_NOSUCHNAME);

		if (view != NULL) {
			if (community != COMM_INITIALIZE &&
			    view->type == StorageType_readOnly)
				return (SNMP_ERR_NOT_WRITEABLE);
			if (view->status == RowStatus_active &&
			    val->v.integer != RowStatus_destroy)
				return (SNMP_ERR_INCONS_VALUE);
		}

		switch (val->var.subs[sub - 1]) {
		case LEAF_vacmViewTreeFamilyMask:
			if (val->v.octetstring.len > sizeof(view->mask))
			ctx->scratch->ptr1 = malloc(sizeof(view->mask));
			if (ctx->scratch->ptr1 == NULL)
				return (SNMP_ERR_GENERR);
			memset(ctx->scratch->ptr1, 0, sizeof(view->mask));
			memcpy(ctx->scratch->ptr1, view->mask,
			    sizeof(view->mask));
			memset(view->mask, 0, sizeof(view->mask));
			memcpy(view->mask, val->v.octetstring.octets,
			    val->v.octetstring.len);
			break;

		case LEAF_vacmViewTreeFamilyType:
			ctx->scratch->int1 = view->exclude;
			if (val->v.integer == vacmViewTreeFamilyType_included)
				view->exclude = 0;
			else if (val->v.integer == vacmViewTreeFamilyType_excluded)
				view->exclude = 1;
			else
				return (SNMP_ERR_WRONG_VALUE);
			break;

		case LEAF_vacmViewTreeFamilyStorageType:
			return (SNMP_ERR_INCONS_VALUE);

		case LEAF_vacmViewTreeFamilyStatus:
			if (view == NULL) {
				if (val->v.integer != RowStatus_createAndGo ||
				    vacm_view_index_decode(&val->var, sub, vname,
				    &oid) < 0)
					return (SNMP_ERR_INCONS_VALUE);
				if ((view = vacm_new_view(vname, &oid)) == NULL)
					return (SNMP_ERR_GENERR);
				view->status = RowStatus_destroy;
				if (community != COMM_INITIALIZE)
					view->type = StorageType_volatile;
				else
					view->type = StorageType_readOnly;
			} else if (val->v.integer != RowStatus_active &&
			    val->v.integer != RowStatus_destroy)
				return (SNMP_ERR_INCONS_VALUE);
			ctx->scratch->int1 = view->status;
			view->status = val->v.integer;
			break;
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_COMMIT:
		switch (val->var.subs[sub - 1]) {
		case LEAF_vacmViewTreeFamilyMask:
			free(ctx->scratch->ptr1);
			break;
		case LEAF_vacmViewTreeFamilyStatus:
			if ((view = vacm_get_view(&val->var, sub)) == NULL)
				return (SNMP_ERR_GENERR);
			switch (val->v.integer) {
			case  RowStatus_destroy:
				return (vacm_delete_view(view));

			case RowStatus_createAndGo:
				view->status = RowStatus_active;
				break;

			default:
				/* NOTREACHED*/
				return (SNMP_ERR_GENERR);
			}
		default:
			break;
		}
		return (SNMP_ERR_NOERROR);

	case SNMP_OP_ROLLBACK:
		if ((view = vacm_get_view(&val->var, sub)) == NULL)
			return (SNMP_ERR_GENERR);
		switch (val->var.subs[sub - 1]) {
		case LEAF_vacmViewTreeFamilyMask:
			memcpy(view->mask, ctx->scratch->ptr1,
			    sizeof(view->mask));
			free(ctx->scratch->ptr1);
			break;
		case LEAF_vacmViewTreeFamilyType:
			view->exclude = ctx->scratch->int1;
			break;
		case LEAF_vacmViewTreeFamilyStatus:
			if (ctx->scratch->int1 == RowStatus_destroy)
				return (vacm_delete_view(view));
			break;
		default:
			break;
		}
		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}

	switch (val->var.subs[sub - 1]) {
	case LEAF_vacmViewTreeFamilyMask:
		return (string_get(val, view->mask, sizeof(view->mask)));
	case LEAF_vacmViewTreeFamilyType:
		if (view->exclude)
			val->v.integer = vacmViewTreeFamilyType_excluded;
		else
			val->v.integer = vacmViewTreeFamilyType_included;
		break;
	case LEAF_vacmViewTreeFamilyStorageType:
		val->v.integer = view->type;
		break;
	case LEAF_vacmViewTreeFamilyStatus:
		val->v.integer = view->status;
		break;
	default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}

static void
vacm_append_userindex(struct asn_oid *oid, uint sub,
    const struct vacm_user *user)
{
	uint32_t i;

	oid->len = sub + strlen(user->secname) + 2;
	oid->subs[sub++] = user->sec_model;
	oid->subs[sub] = strlen(user->secname);
	for (i = 1; i <= strlen(user->secname); i++)
		oid->subs[sub + i] = user->secname[i - 1];
}

static int
vacm_user_index_decode(const struct asn_oid *oid, uint sub,
    int32_t *smodel, char *uname)
{
	uint32_t i;

	*smodel = oid->subs[sub++];

	if (oid->subs[sub] >= SNMP_ADM_STR32_SIZ)
		return (-1);

	for (i = 0; i < oid->subs[sub]; i++)
		uname[i] = oid->subs[sub + i + 1];
	uname[i] = '\0';

	return (0);
}

static struct vacm_user *
vacm_get_user(const struct asn_oid *oid, uint sub)
{
	int32_t smodel;
	char uname[SNMP_ADM_STR32_SIZ];
	struct vacm_user *user;

	if (vacm_user_index_decode(oid, sub, &smodel, uname) < 0)
		return (NULL);

	for (user = vacm_first_user(); user != NULL; user = vacm_next_user(user))
		if (strcmp(uname, user->secname) == 0 &&
		    user->sec_model == smodel)
			return (user);

	return (NULL);
}

static struct vacm_user *
vacm_get_next_user(const struct asn_oid *oid, uint sub)
{
	int32_t smodel;
	char uname[SNMP_ADM_STR32_SIZ];
	struct vacm_user *user;

	if (oid->len - sub == 0)
		return (vacm_first_user());

	if (vacm_user_index_decode(oid, sub, &smodel, uname) < 0)
		return (NULL);

	for (user = vacm_first_user(); user != NULL; user = vacm_next_user(user))
		if (strcmp(uname, user->secname) == 0 &&
		    user->sec_model == smodel)
			return (vacm_next_user(user));

	return (NULL);
}

static void
vacm_append_access_rule_index(struct asn_oid *oid, uint sub,
    const struct vacm_access *acl)
{
	uint32_t i;

	oid->len = sub + strlen(acl->group->groupname) +
	    strlen(acl->ctx_prefix) + 4;

	oid->subs[sub] = strlen(acl->group->groupname);
	for (i = 1; i <= strlen(acl->group->groupname); i++)
		oid->subs[sub + i] = acl->group->groupname[i - 1];
	sub += strlen(acl->group->groupname) + 1;

	oid->subs[sub] = strlen(acl->ctx_prefix);
	for (i = 1; i <= strlen(acl->ctx_prefix); i++)
		oid->subs[sub + i] = acl->ctx_prefix[i - 1];
	sub += strlen(acl->ctx_prefix) + 1;
	oid->subs[sub++] = acl->sec_model;
	oid->subs[sub] = acl->sec_level;
}

static int
vacm_access_rule_index_decode(const struct asn_oid *oid, uint sub, char *gname,
    char *cprefix, int32_t *smodel, int32_t *slevel)
{
	uint32_t i;

	if (oid->subs[sub] >= SNMP_ADM_STR32_SIZ)
		return (-1);

	for (i = 0; i < oid->subs[sub]; i++)
		gname[i] = oid->subs[sub + i + 1];
	gname[i] = '\0';
	sub += strlen(gname) + 1;

	if (oid->subs[sub] >= SNMP_ADM_STR32_SIZ)
		return (-1);

	for (i = 0; i < oid->subs[sub]; i++)
		cprefix[i] = oid->subs[sub + i + 1];
	cprefix[i] = '\0';
	sub += strlen(cprefix) + 1;

	*smodel = oid->subs[sub++];
	*slevel = oid->subs[sub];

	return (0);
}

struct vacm_access *
vacm_get_access_rule(const struct asn_oid *oid, uint sub)
{
	int32_t smodel, slevel;
	char gname[SNMP_ADM_STR32_SIZ], prefix[SNMP_ADM_STR32_SIZ];
	struct vacm_access *acl;

	if (vacm_access_rule_index_decode(oid, sub, gname, prefix, &smodel,
	    &slevel) < 0)
		return (NULL);

	for (acl = vacm_first_access_rule(); acl != NULL;
	    acl = vacm_next_access_rule(acl))
		if (strcmp(gname, acl->group->groupname) == 0 &&
		    strcmp(prefix, acl->ctx_prefix) == 0 &&
		    smodel == acl->sec_model && slevel == acl->sec_level)
			return (acl);

	return (NULL);
}

struct vacm_access *
vacm_get_next_access_rule(const struct asn_oid *oid __unused, uint sub __unused)
{
	int32_t smodel, slevel;
	char gname[SNMP_ADM_STR32_SIZ], prefix[SNMP_ADM_STR32_SIZ];
	struct vacm_access *acl;

	if (oid->len - sub == 0)
		return (vacm_first_access_rule());

	if (vacm_access_rule_index_decode(oid, sub, gname, prefix, &smodel,
	    &slevel) < 0)
		return (NULL);

	for (acl = vacm_first_access_rule(); acl != NULL;
	    acl = vacm_next_access_rule(acl))
		if (strcmp(gname, acl->group->groupname) == 0 &&
		    strcmp(prefix, acl->ctx_prefix) == 0 &&
		    smodel == acl->sec_model && slevel == acl->sec_model)
			return (vacm_next_access_rule(acl));

	return (NULL);
}

static int
vacm_view_index_decode(const struct asn_oid *oid, uint sub, char *vname,
   struct asn_oid *view_oid)
{
	uint32_t i;
	int viod_off;

	if (oid->subs[sub] >= SNMP_ADM_STR32_SIZ)
		return (-1);

	for (i = 0; i < oid->subs[sub]; i++)
		vname[i] = oid->subs[sub + i + 1];
	vname[i] = '\0';

	viod_off = sub + oid->subs[sub] + 1;
	if ((view_oid->len = oid->subs[viod_off]) > ASN_MAXOIDLEN)
		return (-1);

	memcpy(&view_oid->subs[0], &oid->subs[viod_off + 1],
	    view_oid->len * sizeof(view_oid->subs[0]));

	return (0);
}

static void
vacm_append_viewindex(struct asn_oid *oid, uint sub, const struct vacm_view *view)
{
	uint32_t i;

	oid->len = sub + strlen(view->viewname) + 1;
	oid->subs[sub] = strlen(view->viewname);
	for (i = 1; i <= strlen(view->viewname); i++)
		oid->subs[sub + i] = view->viewname[i - 1];

	sub += strlen(view->viewname) + 1;
	oid->subs[sub] = view->subtree.len;
	oid->len++;
	asn_append_oid(oid, &view->subtree);
}

struct vacm_view *
vacm_get_view(const struct asn_oid *oid, uint sub)
{
	char vname[SNMP_ADM_STR32_SIZ];
	struct asn_oid subtree;
	struct vacm_view *view;

	if (vacm_view_index_decode(oid, sub, vname, &subtree) < 0)
		return (NULL);

	for (view = vacm_first_view(); view != NULL; view = vacm_next_view(view))
		if (strcmp(vname, view->viewname) == 0 &&
		    asn_compare_oid(&subtree, &view->subtree)== 0)
			return (view);

	return (NULL);
}

struct vacm_view *
vacm_get_next_view(const struct asn_oid *oid, uint sub)
{
	char vname[SNMP_ADM_STR32_SIZ];
	struct asn_oid subtree;
	struct vacm_view *view;

	if (oid->len - sub == 0)
		return (vacm_first_view());

	if (vacm_view_index_decode(oid, sub, vname, &subtree) < 0)
		return (NULL);

	for (view = vacm_first_view(); view != NULL; view = vacm_next_view(view))
		if (strcmp(vname, view->viewname) == 0 &&
		    asn_compare_oid(&subtree, &view->subtree)== 0)
			return (vacm_next_view(view));

	return (NULL);
}

static struct vacm_view *
vacm_get_view_by_name(u_char *octets, u_int len)
{
	struct vacm_view *view;

	for (view = vacm_first_view(); view != NULL; view = vacm_next_view(view))
		if (strlen(view->viewname) == len &&
		    memcmp(octets, view->viewname, len) == 0)
			return (view);

	return (NULL);
}

static struct vacm_context *
vacm_get_context(const struct asn_oid *oid, uint sub)
{
	char cname[SNMP_ADM_STR32_SIZ];
	size_t cnamelen;
	u_int index_count;
	struct vacm_context *vacm_ctx;

	if (oid->subs[sub] >= SNMP_ADM_STR32_SIZ)
		return (NULL);

	index_count = 0;
	index_count = SNMP_INDEX(index_count, 1);
	if (index_decode(oid, sub, index_count, &cname, &cnamelen))
		return (NULL);

	for (vacm_ctx = vacm_first_context(); vacm_ctx != NULL;
	    vacm_ctx = vacm_next_context(vacm_ctx))
		if (strcmp(cname, vacm_ctx->ctxname) == 0)
			return (vacm_ctx);

	return (NULL);
}

static struct vacm_context *
vacm_get_next_context(const struct asn_oid *oid, uint sub)
{
	char cname[SNMP_ADM_STR32_SIZ];
	size_t cnamelen;
	u_int index_count;
	struct vacm_context *vacm_ctx;

	if (oid->len - sub == 0)
		return (vacm_first_context());

	if (oid->subs[sub] >= SNMP_ADM_STR32_SIZ)
		return (NULL);

	index_count = 0;
	index_count = SNMP_INDEX(index_count, 1);
	if (index_decode(oid, sub, index_count, &cname, &cnamelen))
		return (NULL);

	for (vacm_ctx = vacm_first_context(); vacm_ctx != NULL;
	    vacm_ctx = vacm_next_context(vacm_ctx))
		if (strcmp(cname, vacm_ctx->ctxname) == 0)
			return (vacm_next_context(vacm_ctx));

	return (NULL);
}

static void
vacm_append_ctxindex(struct asn_oid *oid, uint sub,
    const struct vacm_context *ctx)
{
	uint32_t i;

	oid->len = sub + strlen(ctx->ctxname) + 1;
	oid->subs[sub] = strlen(ctx->ctxname);
	for (i = 1; i <= strlen(ctx->ctxname); i++)
		oid->subs[sub + i] = ctx->ctxname[i - 1];
}

/*
 * VACM snmp module initialization hook.
 * Returns 0 on success, < 0 on error.
 */
static int
vacm_init(struct lmodule *mod, int argc __unused, char *argv[] __unused)
{
	vacm_module = mod;
	vacm_lock = random();
	vacm_groups_init();

	/* XXX: TODO - initialize structures */
	return (0);
}

/*
 * VACM snmp module finalization hook.
 */
static int
vacm_fini(void)
{
	/* XXX: TODO - cleanup */
	vacm_flush_contexts(reg_vacm);
	or_unregister(reg_vacm);

	return (0);
}

/*
 * VACM snmp module start operation.
 */
static void
vacm_start(void)
{
	static char dflt_ctx[] = "";

	reg_vacm = or_register(&oid_vacm,
	    "The MIB module for managing SNMP View-based Access Control Model.",
	    vacm_module);

	(void)vacm_add_context(dflt_ctx, reg_vacm);
}

static void
vacm_dump(void)
{
	struct vacm_context *vacmctx;
	struct vacm_user *vuser;
	struct vacm_access *vacl;
	struct vacm_view *view;
	static char oidbuf[ASN_OIDSTRLEN];

	syslog(LOG_ERR, "\n");
	syslog(LOG_ERR, "Context list:");
	for (vacmctx = vacm_first_context(); vacmctx != NULL;
	    vacmctx = vacm_next_context(vacmctx))
		syslog(LOG_ERR, "Context \"%s\", module id %d",
		    vacmctx->ctxname, vacmctx->regid);

	syslog(LOG_ERR, "VACM users:");
	for (vuser = vacm_first_user(); vuser != NULL;
	    vuser = vacm_next_user(vuser))
		syslog(LOG_ERR, "Uname %s, Group %s, model %d", vuser->secname,
		    vuser->group!= NULL?vuser->group->groupname:"Unknown",
		    vuser->sec_model);

	syslog(LOG_ERR, "VACM Access rules:");
	for (vacl = vacm_first_access_rule(); vacl != NULL;
	    vacl = vacm_next_access_rule(vacl))
		syslog(LOG_ERR, "Group %s, CtxPrefix %s, Model %d, Level %d, "
		    "RV %s, WR %s, NV %s", vacl->group!=NULL?
		    vacl->group->groupname:"Unknown", vacl->ctx_prefix,
		    vacl->sec_model, vacl->sec_level, vacl->read_view!=NULL?
		    vacl->read_view->viewname:"None", vacl->write_view!=NULL?
		    vacl->write_view->viewname:"None", vacl->notify_view!=NULL?
		    vacl->notify_view->viewname:"None");

	syslog(LOG_ERR, "VACM Views:");
	for (view = vacm_first_view(); view != NULL; view = vacm_next_view(view))
		syslog(LOG_ERR, "View %s, Tree %s - %s", view->viewname,
		    asn_oid2str_r(&view->subtree, oidbuf), view->exclude?
		    "excluded":"included");
}

static const char vacm_comment[] = \
"This module implements SNMP View-based Access Control Model defined in RFC 3415.";

extern const struct snmp_module config;
const struct snmp_module config = {
	.comment =	vacm_comment,
	.init =		vacm_init,
	.fini =		vacm_fini,
	.start =	vacm_start,
	.tree =		vacm_ctree,
	.dump =		vacm_dump,
	.tree_size =	vacm_CTREE_SIZE,
};
