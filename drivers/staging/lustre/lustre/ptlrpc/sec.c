/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/ptlrpc/sec.c
 *
 * Author: Eric Mei <ericm@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_SEC

#include "../../include/linux/libcfs/libcfs.h"
#include <linux/crypto.h>
#include <linux/key.h>

#include "../include/obd.h"
#include "../include/obd_class.h"
#include "../include/obd_support.h"
#include "../include/lustre_net.h"
#include "../include/lustre_import.h"
#include "../include/lustre_dlm.h"
#include "../include/lustre_sec.h"

#include "ptlrpc_internal.h"

/***********************************************
 * policy registers			    *
 ***********************************************/

static rwlock_t policy_lock;
static struct ptlrpc_sec_policy *policies[SPTLRPC_POLICY_MAX] = {
	NULL,
};

int sptlrpc_register_policy(struct ptlrpc_sec_policy *policy)
{
	__u16 number = policy->sp_policy;

	LASSERT(policy->sp_name);
	LASSERT(policy->sp_cops);
	LASSERT(policy->sp_sops);

	if (number >= SPTLRPC_POLICY_MAX)
		return -EINVAL;

	write_lock(&policy_lock);
	if (unlikely(policies[number])) {
		write_unlock(&policy_lock);
		return -EALREADY;
	}
	policies[number] = policy;
	write_unlock(&policy_lock);

	CDEBUG(D_SEC, "%s: registered\n", policy->sp_name);
	return 0;
}
EXPORT_SYMBOL(sptlrpc_register_policy);

int sptlrpc_unregister_policy(struct ptlrpc_sec_policy *policy)
{
	__u16 number = policy->sp_policy;

	LASSERT(number < SPTLRPC_POLICY_MAX);

	write_lock(&policy_lock);
	if (unlikely(policies[number] == NULL)) {
		write_unlock(&policy_lock);
		CERROR("%s: already unregistered\n", policy->sp_name);
		return -EINVAL;
	}

	LASSERT(policies[number] == policy);
	policies[number] = NULL;
	write_unlock(&policy_lock);

	CDEBUG(D_SEC, "%s: unregistered\n", policy->sp_name);
	return 0;
}
EXPORT_SYMBOL(sptlrpc_unregister_policy);

static
struct ptlrpc_sec_policy *sptlrpc_wireflavor2policy(__u32 flavor)
{
	static DEFINE_MUTEX(load_mutex);
	static atomic_t loaded = ATOMIC_INIT(0);
	struct ptlrpc_sec_policy *policy;
	__u16 number = SPTLRPC_FLVR_POLICY(flavor);
	__u16 flag = 0;

	if (number >= SPTLRPC_POLICY_MAX)
		return NULL;

	while (1) {
		read_lock(&policy_lock);
		policy = policies[number];
		if (policy && !try_module_get(policy->sp_owner))
			policy = NULL;
		if (policy == NULL)
			flag = atomic_read(&loaded);
		read_unlock(&policy_lock);

		if (policy != NULL || flag != 0 ||
		    number != SPTLRPC_POLICY_GSS)
			break;

		/* try to load gss module, once */
		mutex_lock(&load_mutex);
		if (atomic_read(&loaded) == 0) {
			if (request_module("ptlrpc_gss") == 0)
				CDEBUG(D_SEC,
				       "module ptlrpc_gss loaded on demand\n");
			else
				CERROR("Unable to load module ptlrpc_gss\n");

			atomic_set(&loaded, 1);
		}
		mutex_unlock(&load_mutex);
	}

	return policy;
}

__u32 sptlrpc_name2flavor_base(const char *name)
{
	if (!strcmp(name, "null"))
		return SPTLRPC_FLVR_NULL;
	if (!strcmp(name, "plain"))
		return SPTLRPC_FLVR_PLAIN;
	if (!strcmp(name, "krb5n"))
		return SPTLRPC_FLVR_KRB5N;
	if (!strcmp(name, "krb5a"))
		return SPTLRPC_FLVR_KRB5A;
	if (!strcmp(name, "krb5i"))
		return SPTLRPC_FLVR_KRB5I;
	if (!strcmp(name, "krb5p"))
		return SPTLRPC_FLVR_KRB5P;

	return SPTLRPC_FLVR_INVALID;
}
EXPORT_SYMBOL(sptlrpc_name2flavor_base);

const char *sptlrpc_flavor2name_base(__u32 flvr)
{
	__u32   base = SPTLRPC_FLVR_BASE(flvr);

	if (base == SPTLRPC_FLVR_BASE(SPTLRPC_FLVR_NULL))
		return "null";
	else if (base == SPTLRPC_FLVR_BASE(SPTLRPC_FLVR_PLAIN))
		return "plain";
	else if (base == SPTLRPC_FLVR_BASE(SPTLRPC_FLVR_KRB5N))
		return "krb5n";
	else if (base == SPTLRPC_FLVR_BASE(SPTLRPC_FLVR_KRB5A))
		return "krb5a";
	else if (base == SPTLRPC_FLVR_BASE(SPTLRPC_FLVR_KRB5I))
		return "krb5i";
	else if (base == SPTLRPC_FLVR_BASE(SPTLRPC_FLVR_KRB5P))
		return "krb5p";

	CERROR("invalid wire flavor 0x%x\n", flvr);
	return "invalid";
}
EXPORT_SYMBOL(sptlrpc_flavor2name_base);

char *sptlrpc_flavor2name_bulk(struct sptlrpc_flavor *sf,
			       char *buf, int bufsize)
{
	if (SPTLRPC_FLVR_POLICY(sf->sf_rpc) == SPTLRPC_POLICY_PLAIN)
		snprintf(buf, bufsize, "hash:%s",
			 sptlrpc_get_hash_name(sf->u_bulk.hash.hash_alg));
	else
		snprintf(buf, bufsize, "%s",
			 sptlrpc_flavor2name_base(sf->sf_rpc));

	buf[bufsize - 1] = '\0';
	return buf;
}
EXPORT_SYMBOL(sptlrpc_flavor2name_bulk);

char *sptlrpc_flavor2name(struct sptlrpc_flavor *sf, char *buf, int bufsize)
{
	strlcpy(buf, sptlrpc_flavor2name_base(sf->sf_rpc), bufsize);

	/*
	 * currently we don't support customized bulk specification for
	 * flavors other than plain
	 */
	if (SPTLRPC_FLVR_POLICY(sf->sf_rpc) == SPTLRPC_POLICY_PLAIN) {
		char bspec[16];

		bspec[0] = '-';
		sptlrpc_flavor2name_bulk(sf, &bspec[1], sizeof(bspec) - 1);
		strlcat(buf, bspec, bufsize);
	}

	return buf;
}
EXPORT_SYMBOL(sptlrpc_flavor2name);

char *sptlrpc_secflags2str(__u32 flags, char *buf, int bufsize)
{
	buf[0] = '\0';

	if (flags & PTLRPC_SEC_FL_REVERSE)
		strlcat(buf, "reverse,", bufsize);
	if (flags & PTLRPC_SEC_FL_ROOTONLY)
		strlcat(buf, "rootonly,", bufsize);
	if (flags & PTLRPC_SEC_FL_UDESC)
		strlcat(buf, "udesc,", bufsize);
	if (flags & PTLRPC_SEC_FL_BULK)
		strlcat(buf, "bulk,", bufsize);
	if (buf[0] == '\0')
		strlcat(buf, "-,", bufsize);

	return buf;
}
EXPORT_SYMBOL(sptlrpc_secflags2str);

/**************************************************
 * client context APIs			    *
 **************************************************/

static
struct ptlrpc_cli_ctx *get_my_ctx(struct ptlrpc_sec *sec)
{
	struct vfs_cred vcred;
	int create = 1, remove_dead = 1;

	LASSERT(sec);
	LASSERT(sec->ps_policy->sp_cops->lookup_ctx);

	if (sec->ps_flvr.sf_flags & (PTLRPC_SEC_FL_REVERSE |
				     PTLRPC_SEC_FL_ROOTONLY)) {
		vcred.vc_uid = 0;
		vcred.vc_gid = 0;
		if (sec->ps_flvr.sf_flags & PTLRPC_SEC_FL_REVERSE) {
			create = 0;
			remove_dead = 0;
		}
	} else {
		vcred.vc_uid = from_kuid(&init_user_ns, current_uid());
		vcred.vc_gid = from_kgid(&init_user_ns, current_gid());
	}

	return sec->ps_policy->sp_cops->lookup_ctx(sec, &vcred,
						   create, remove_dead);
}

struct ptlrpc_cli_ctx *sptlrpc_cli_ctx_get(struct ptlrpc_cli_ctx *ctx)
{
	atomic_inc(&ctx->cc_refcount);
	return ctx;
}
EXPORT_SYMBOL(sptlrpc_cli_ctx_get);

void sptlrpc_cli_ctx_put(struct ptlrpc_cli_ctx *ctx, int sync)
{
	struct ptlrpc_sec *sec = ctx->cc_sec;

	LASSERT(sec);
	LASSERT_ATOMIC_POS(&ctx->cc_refcount);

	if (!atomic_dec_and_test(&ctx->cc_refcount))
		return;

	sec->ps_policy->sp_cops->release_ctx(sec, ctx, sync);
}
EXPORT_SYMBOL(sptlrpc_cli_ctx_put);

/**
 * Expire the client context immediately.
 *
 * \pre Caller must hold at least 1 reference on the \a ctx.
 */
void sptlrpc_cli_ctx_expire(struct ptlrpc_cli_ctx *ctx)
{
	LASSERT(ctx->cc_ops->force_die);
	ctx->cc_ops->force_die(ctx, 0);
}
EXPORT_SYMBOL(sptlrpc_cli_ctx_expire);

/**
 * To wake up the threads who are waiting for this client context. Called
 * after some status change happened on \a ctx.
 */
void sptlrpc_cli_ctx_wakeup(struct ptlrpc_cli_ctx *ctx)
{
	struct ptlrpc_request *req, *next;

	spin_lock(&ctx->cc_lock);
	list_for_each_entry_safe(req, next, &ctx->cc_req_list,
				     rq_ctx_chain) {
		list_del_init(&req->rq_ctx_chain);
		ptlrpc_client_wake_req(req);
	}
	spin_unlock(&ctx->cc_lock);
}
EXPORT_SYMBOL(sptlrpc_cli_ctx_wakeup);

int sptlrpc_cli_ctx_display(struct ptlrpc_cli_ctx *ctx, char *buf, int bufsize)
{
	LASSERT(ctx->cc_ops);

	if (ctx->cc_ops->display == NULL)
		return 0;

	return ctx->cc_ops->display(ctx, buf, bufsize);
}

static int import_sec_check_expire(struct obd_import *imp)
{
	int adapt = 0;

	spin_lock(&imp->imp_lock);
	if (imp->imp_sec_expire &&
	    imp->imp_sec_expire < get_seconds()) {
		adapt = 1;
		imp->imp_sec_expire = 0;
	}
	spin_unlock(&imp->imp_lock);

	if (!adapt)
		return 0;

	CDEBUG(D_SEC, "found delayed sec adapt expired, do it now\n");
	return sptlrpc_import_sec_adapt(imp, NULL, NULL);
}

static int import_sec_validate_get(struct obd_import *imp,
				   struct ptlrpc_sec **sec)
{
	int rc;

	if (unlikely(imp->imp_sec_expire)) {
		rc = import_sec_check_expire(imp);
		if (rc)
			return rc;
	}

	*sec = sptlrpc_import_sec_ref(imp);
	if (*sec == NULL) {
		CERROR("import %p (%s) with no sec\n",
		       imp, ptlrpc_import_state_name(imp->imp_state));
		return -EACCES;
	}

	if (unlikely((*sec)->ps_dying)) {
		CERROR("attempt to use dying sec %p\n", sec);
		sptlrpc_sec_put(*sec);
		return -EACCES;
	}

	return 0;
}

/**
 * Given a \a req, find or allocate a appropriate context for it.
 * \pre req->rq_cli_ctx == NULL.
 *
 * \retval 0 succeed, and req->rq_cli_ctx is set.
 * \retval -ev error number, and req->rq_cli_ctx == NULL.
 */
int sptlrpc_req_get_ctx(struct ptlrpc_request *req)
{
	struct obd_import *imp = req->rq_import;
	struct ptlrpc_sec *sec;
	int		rc;

	LASSERT(!req->rq_cli_ctx);
	LASSERT(imp);

	rc = import_sec_validate_get(imp, &sec);
	if (rc)
		return rc;

	req->rq_cli_ctx = get_my_ctx(sec);

	sptlrpc_sec_put(sec);

	if (!req->rq_cli_ctx) {
		CERROR("req %p: fail to get context\n", req);
		return -ENOMEM;
	}

	return 0;
}

/**
 * Drop the context for \a req.
 * \pre req->rq_cli_ctx != NULL.
 * \post req->rq_cli_ctx == NULL.
 *
 * If \a sync == 0, this function should return quickly without sleep;
 * otherwise it might trigger and wait for the whole process of sending
 * an context-destroying rpc to server.
 */
void sptlrpc_req_put_ctx(struct ptlrpc_request *req, int sync)
{
	LASSERT(req);
	LASSERT(req->rq_cli_ctx);

	/* request might be asked to release earlier while still
	 * in the context waiting list.
	 */
	if (!list_empty(&req->rq_ctx_chain)) {
		spin_lock(&req->rq_cli_ctx->cc_lock);
		list_del_init(&req->rq_ctx_chain);
		spin_unlock(&req->rq_cli_ctx->cc_lock);
	}

	sptlrpc_cli_ctx_put(req->rq_cli_ctx, sync);
	req->rq_cli_ctx = NULL;
}

static
int sptlrpc_req_ctx_switch(struct ptlrpc_request *req,
			   struct ptlrpc_cli_ctx *oldctx,
			   struct ptlrpc_cli_ctx *newctx)
{
	struct sptlrpc_flavor old_flvr;
	char *reqmsg = NULL; /* to workaround old gcc */
	int reqmsg_size;
	int rc = 0;

	LASSERT(req->rq_reqmsg);
	LASSERT(req->rq_reqlen);
	LASSERT(req->rq_replen);

	CDEBUG(D_SEC, "req %p: switch ctx %p(%u->%s) -> %p(%u->%s), switch sec %p(%s) -> %p(%s)\n",
	       req,
	       oldctx, oldctx->cc_vcred.vc_uid, sec2target_str(oldctx->cc_sec),
	       newctx, newctx->cc_vcred.vc_uid, sec2target_str(newctx->cc_sec),
	       oldctx->cc_sec, oldctx->cc_sec->ps_policy->sp_name,
	       newctx->cc_sec, newctx->cc_sec->ps_policy->sp_name);

	/* save flavor */
	old_flvr = req->rq_flvr;

	/* save request message */
	reqmsg_size = req->rq_reqlen;
	if (reqmsg_size != 0) {
		reqmsg = libcfs_kvzalloc(reqmsg_size, GFP_NOFS);
		if (reqmsg == NULL)
			return -ENOMEM;
		memcpy(reqmsg, req->rq_reqmsg, reqmsg_size);
	}

	/* release old req/rep buf */
	req->rq_cli_ctx = oldctx;
	sptlrpc_cli_free_reqbuf(req);
	sptlrpc_cli_free_repbuf(req);
	req->rq_cli_ctx = newctx;

	/* recalculate the flavor */
	sptlrpc_req_set_flavor(req, 0);

	/* alloc new request buffer
	 * we don't need to alloc reply buffer here, leave it to the
	 * rest procedure of ptlrpc */
	if (reqmsg_size != 0) {
		rc = sptlrpc_cli_alloc_reqbuf(req, reqmsg_size);
		if (!rc) {
			LASSERT(req->rq_reqmsg);
			memcpy(req->rq_reqmsg, reqmsg, reqmsg_size);
		} else {
			CWARN("failed to alloc reqbuf: %d\n", rc);
			req->rq_flvr = old_flvr;
		}

		kvfree(reqmsg);
	}
	return rc;
}

/**
 * If current context of \a req is dead somehow, e.g. we just switched flavor
 * thus marked original contexts dead, we'll find a new context for it. if
 * no switch is needed, \a req will end up with the same context.
 *
 * \note a request must have a context, to keep other parts of code happy.
 * In any case of failure during the switching, we must restore the old one.
 */
int sptlrpc_req_replace_dead_ctx(struct ptlrpc_request *req)
{
	struct ptlrpc_cli_ctx *oldctx = req->rq_cli_ctx;
	struct ptlrpc_cli_ctx *newctx;
	int rc;

	LASSERT(oldctx);

	sptlrpc_cli_ctx_get(oldctx);
	sptlrpc_req_put_ctx(req, 0);

	rc = sptlrpc_req_get_ctx(req);
	if (unlikely(rc)) {
		LASSERT(!req->rq_cli_ctx);

		/* restore old ctx */
		req->rq_cli_ctx = oldctx;
		return rc;
	}

	newctx = req->rq_cli_ctx;
	LASSERT(newctx);

	if (unlikely(newctx == oldctx &&
		     test_bit(PTLRPC_CTX_DEAD_BIT, &oldctx->cc_flags))) {
		/*
		 * still get the old dead ctx, usually means system too busy
		 */
		CDEBUG(D_SEC,
		       "ctx (%p, fl %lx) doesn't switch, relax a little bit\n",
		       newctx, newctx->cc_flags);

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	} else {
		/*
		 * it's possible newctx == oldctx if we're switching
		 * subflavor with the same sec.
		 */
		rc = sptlrpc_req_ctx_switch(req, oldctx, newctx);
		if (rc) {
			/* restore old ctx */
			sptlrpc_req_put_ctx(req, 0);
			req->rq_cli_ctx = oldctx;
			return rc;
		}

		LASSERT(req->rq_cli_ctx == newctx);
	}

	sptlrpc_cli_ctx_put(oldctx, 1);
	return 0;
}
EXPORT_SYMBOL(sptlrpc_req_replace_dead_ctx);

static
int ctx_check_refresh(struct ptlrpc_cli_ctx *ctx)
{
	if (cli_ctx_is_refreshed(ctx))
		return 1;
	return 0;
}

static
int ctx_refresh_timeout(void *data)
{
	struct ptlrpc_request *req = data;
	int rc;

	/* conn_cnt is needed in expire_one_request */
	lustre_msg_set_conn_cnt(req->rq_reqmsg, req->rq_import->imp_conn_cnt);

	rc = ptlrpc_expire_one_request(req, 1);
	/* if we started recovery, we should mark this ctx dead; otherwise
	 * in case of lgssd died nobody would retire this ctx, following
	 * connecting will still find the same ctx thus cause deadlock.
	 * there's an assumption that expire time of the request should be
	 * later than the context refresh expire time.
	 */
	if (rc == 0)
		req->rq_cli_ctx->cc_ops->force_die(req->rq_cli_ctx, 0);
	return rc;
}

static
void ctx_refresh_interrupt(void *data)
{
	struct ptlrpc_request *req = data;

	spin_lock(&req->rq_lock);
	req->rq_intr = 1;
	spin_unlock(&req->rq_lock);
}

static
void req_off_ctx_list(struct ptlrpc_request *req, struct ptlrpc_cli_ctx *ctx)
{
	spin_lock(&ctx->cc_lock);
	if (!list_empty(&req->rq_ctx_chain))
		list_del_init(&req->rq_ctx_chain);
	spin_unlock(&ctx->cc_lock);
}

/**
 * To refresh the context of \req, if it's not up-to-date.
 * \param timeout
 * - < 0: don't wait
 * - = 0: wait until success or fatal error occur
 * - > 0: timeout value (in seconds)
 *
 * The status of the context could be subject to be changed by other threads
 * at any time. We allow this race, but once we return with 0, the caller will
 * suppose it's uptodated and keep using it until the owning rpc is done.
 *
 * \retval 0 only if the context is uptodated.
 * \retval -ev error number.
 */
int sptlrpc_req_refresh_ctx(struct ptlrpc_request *req, long timeout)
{
	struct ptlrpc_cli_ctx *ctx = req->rq_cli_ctx;
	struct ptlrpc_sec *sec;
	struct l_wait_info lwi;
	int rc;

	LASSERT(ctx);

	if (req->rq_ctx_init || req->rq_ctx_fini)
		return 0;

	/*
	 * during the process a request's context might change type even
	 * (e.g. from gss ctx to null ctx), so each loop we need to re-check
	 * everything
	 */
again:
	rc = import_sec_validate_get(req->rq_import, &sec);
	if (rc)
		return rc;

	if (sec->ps_flvr.sf_rpc != req->rq_flvr.sf_rpc) {
		CDEBUG(D_SEC, "req %p: flavor has changed %x -> %x\n",
		      req, req->rq_flvr.sf_rpc, sec->ps_flvr.sf_rpc);
		req_off_ctx_list(req, ctx);
		sptlrpc_req_replace_dead_ctx(req);
		ctx = req->rq_cli_ctx;
	}
	sptlrpc_sec_put(sec);

	if (cli_ctx_is_eternal(ctx))
		return 0;

	if (unlikely(test_bit(PTLRPC_CTX_NEW_BIT, &ctx->cc_flags))) {
		LASSERT(ctx->cc_ops->refresh);
		ctx->cc_ops->refresh(ctx);
	}
	LASSERT(test_bit(PTLRPC_CTX_NEW_BIT, &ctx->cc_flags) == 0);

	LASSERT(ctx->cc_ops->validate);
	if (ctx->cc_ops->validate(ctx) == 0) {
		req_off_ctx_list(req, ctx);
		return 0;
	}

	if (unlikely(test_bit(PTLRPC_CTX_ERROR_BIT, &ctx->cc_flags))) {
		spin_lock(&req->rq_lock);
		req->rq_err = 1;
		spin_unlock(&req->rq_lock);
		req_off_ctx_list(req, ctx);
		return -EPERM;
	}

	/*
	 * There's a subtle issue for resending RPCs, suppose following
	 * situation:
	 *  1. the request was sent to server.
	 *  2. recovery was kicked start, after finished the request was
	 *     marked as resent.
	 *  3. resend the request.
	 *  4. old reply from server received, we accept and verify the reply.
	 *     this has to be success, otherwise the error will be aware
	 *     by application.
	 *  5. new reply from server received, dropped by LNet.
	 *
	 * Note the xid of old & new request is the same. We can't simply
	 * change xid for the resent request because the server replies on
	 * it for reply reconstruction.
	 *
	 * Commonly the original context should be uptodate because we
	 * have a expiry nice time; server will keep its context because
	 * we at least hold a ref of old context which prevent context
	 * destroying RPC being sent. So server still can accept the request
	 * and finish the RPC. But if that's not the case:
	 *  1. If server side context has been trimmed, a NO_CONTEXT will
	 *     be returned, gss_cli_ctx_verify/unseal will switch to new
	 *     context by force.
	 *  2. Current context never be refreshed, then we are fine: we
	 *     never really send request with old context before.
	 */
	if (test_bit(PTLRPC_CTX_UPTODATE_BIT, &ctx->cc_flags) &&
	    unlikely(req->rq_reqmsg) &&
	    lustre_msg_get_flags(req->rq_reqmsg) & MSG_RESENT) {
		req_off_ctx_list(req, ctx);
		return 0;
	}

	if (unlikely(test_bit(PTLRPC_CTX_DEAD_BIT, &ctx->cc_flags))) {
		req_off_ctx_list(req, ctx);
		/*
		 * don't switch ctx if import was deactivated
		 */
		if (req->rq_import->imp_deactive) {
			spin_lock(&req->rq_lock);
			req->rq_err = 1;
			spin_unlock(&req->rq_lock);
			return -EINTR;
		}

		rc = sptlrpc_req_replace_dead_ctx(req);
		if (rc) {
			LASSERT(ctx == req->rq_cli_ctx);
			CERROR("req %p: failed to replace dead ctx %p: %d\n",
			       req, ctx, rc);
			spin_lock(&req->rq_lock);
			req->rq_err = 1;
			spin_unlock(&req->rq_lock);
			return rc;
		}

		ctx = req->rq_cli_ctx;
		goto again;
	}

	/*
	 * Now we're sure this context is during upcall, add myself into
	 * waiting list
	 */
	spin_lock(&ctx->cc_lock);
	if (list_empty(&req->rq_ctx_chain))
		list_add(&req->rq_ctx_chain, &ctx->cc_req_list);
	spin_unlock(&ctx->cc_lock);

	if (timeout < 0)
		return -EWOULDBLOCK;

	/* Clear any flags that may be present from previous sends */
	LASSERT(req->rq_receiving_reply == 0);
	spin_lock(&req->rq_lock);
	req->rq_err = 0;
	req->rq_timedout = 0;
	req->rq_resend = 0;
	req->rq_restart = 0;
	spin_unlock(&req->rq_lock);

	lwi = LWI_TIMEOUT_INTR(timeout * HZ, ctx_refresh_timeout,
			       ctx_refresh_interrupt, req);
	rc = l_wait_event(req->rq_reply_waitq, ctx_check_refresh(ctx), &lwi);

	/*
	 * following cases could lead us here:
	 * - successfully refreshed;
	 * - interrupted;
	 * - timedout, and we don't want recover from the failure;
	 * - timedout, and waked up upon recovery finished;
	 * - someone else mark this ctx dead by force;
	 * - someone invalidate the req and call ptlrpc_client_wake_req(),
	 *   e.g. ptlrpc_abort_inflight();
	 */
	if (!cli_ctx_is_refreshed(ctx)) {
		/* timed out or interrupted */
		req_off_ctx_list(req, ctx);

		LASSERT(rc != 0);
		return rc;
	}

	goto again;
}

/**
 * Initialize flavor settings for \a req, according to \a opcode.
 *
 * \note this could be called in two situations:
 * - new request from ptlrpc_pre_req(), with proper @opcode
 * - old request which changed ctx in the middle, with @opcode == 0
 */
void sptlrpc_req_set_flavor(struct ptlrpc_request *req, int opcode)
{
	struct ptlrpc_sec *sec;

	LASSERT(req->rq_import);
	LASSERT(req->rq_cli_ctx);
	LASSERT(req->rq_cli_ctx->cc_sec);
	LASSERT(req->rq_bulk_read == 0 || req->rq_bulk_write == 0);

	/* special security flags according to opcode */
	switch (opcode) {
	case OST_READ:
	case MDS_READPAGE:
	case MGS_CONFIG_READ:
	case OBD_IDX_READ:
		req->rq_bulk_read = 1;
		break;
	case OST_WRITE:
	case MDS_WRITEPAGE:
		req->rq_bulk_write = 1;
		break;
	case SEC_CTX_INIT:
		req->rq_ctx_init = 1;
		break;
	case SEC_CTX_FINI:
		req->rq_ctx_fini = 1;
		break;
	case 0:
		/* init/fini rpc won't be resend, so can't be here */
		LASSERT(req->rq_ctx_init == 0);
		LASSERT(req->rq_ctx_fini == 0);

		/* cleanup flags, which should be recalculated */
		req->rq_pack_udesc = 0;
		req->rq_pack_bulk = 0;
		break;
	}

	sec = req->rq_cli_ctx->cc_sec;

	spin_lock(&sec->ps_lock);
	req->rq_flvr = sec->ps_flvr;
	spin_unlock(&sec->ps_lock);

	/* force SVC_NULL for context initiation rpc, SVC_INTG for context
	 * destruction rpc */
	if (unlikely(req->rq_ctx_init))
		flvr_set_svc(&req->rq_flvr.sf_rpc, SPTLRPC_SVC_NULL);
	else if (unlikely(req->rq_ctx_fini))
		flvr_set_svc(&req->rq_flvr.sf_rpc, SPTLRPC_SVC_INTG);

	/* user descriptor flag, null security can't do it anyway */
	if ((sec->ps_flvr.sf_flags & PTLRPC_SEC_FL_UDESC) &&
	    (req->rq_flvr.sf_rpc != SPTLRPC_FLVR_NULL))
		req->rq_pack_udesc = 1;

	/* bulk security flag */
	if ((req->rq_bulk_read || req->rq_bulk_write) &&
	    sptlrpc_flavor_has_bulk(&req->rq_flvr))
		req->rq_pack_bulk = 1;
}

void sptlrpc_request_out_callback(struct ptlrpc_request *req)
{
	if (SPTLRPC_FLVR_SVC(req->rq_flvr.sf_rpc) != SPTLRPC_SVC_PRIV)
		return;

	LASSERT(req->rq_clrbuf);
	if (req->rq_pool || !req->rq_reqbuf)
		return;

	kfree(req->rq_reqbuf);
	req->rq_reqbuf = NULL;
	req->rq_reqbuf_len = 0;
}

/**
 * Given an import \a imp, check whether current user has a valid context
 * or not. We may create a new context and try to refresh it, and try
 * repeatedly try in case of non-fatal errors. Return 0 means success.
 */
int sptlrpc_import_check_ctx(struct obd_import *imp)
{
	struct ptlrpc_sec *sec;
	struct ptlrpc_cli_ctx *ctx;
	struct ptlrpc_request *req = NULL;
	int rc;

	might_sleep();

	sec = sptlrpc_import_sec_ref(imp);
	ctx = get_my_ctx(sec);
	sptlrpc_sec_put(sec);

	if (!ctx)
		return -ENOMEM;

	if (cli_ctx_is_eternal(ctx) ||
	    ctx->cc_ops->validate(ctx) == 0) {
		sptlrpc_cli_ctx_put(ctx, 1);
		return 0;
	}

	if (cli_ctx_is_error(ctx)) {
		sptlrpc_cli_ctx_put(ctx, 1);
		return -EACCES;
	}

	req = ptlrpc_request_cache_alloc(GFP_NOFS);
	if (!req)
		return -ENOMEM;

	spin_lock_init(&req->rq_lock);
	atomic_set(&req->rq_refcount, 10000);
	INIT_LIST_HEAD(&req->rq_ctx_chain);
	init_waitqueue_head(&req->rq_reply_waitq);
	init_waitqueue_head(&req->rq_set_waitq);
	req->rq_import = imp;
	req->rq_flvr = sec->ps_flvr;
	req->rq_cli_ctx = ctx;

	rc = sptlrpc_req_refresh_ctx(req, 0);
	LASSERT(list_empty(&req->rq_ctx_chain));
	sptlrpc_cli_ctx_put(req->rq_cli_ctx, 1);
	ptlrpc_request_cache_free(req);

	return rc;
}

/**
 * Used by ptlrpc client, to perform the pre-defined security transformation
 * upon the request message of \a req. After this function called,
 * req->rq_reqmsg is still accessible as clear text.
 */
int sptlrpc_cli_wrap_request(struct ptlrpc_request *req)
{
	struct ptlrpc_cli_ctx *ctx = req->rq_cli_ctx;
	int rc = 0;

	LASSERT(ctx);
	LASSERT(ctx->cc_sec);
	LASSERT(req->rq_reqbuf || req->rq_clrbuf);

	/* we wrap bulk request here because now we can be sure
	 * the context is uptodate.
	 */
	if (req->rq_bulk) {
		rc = sptlrpc_cli_wrap_bulk(req, req->rq_bulk);
		if (rc)
			return rc;
	}

	switch (SPTLRPC_FLVR_SVC(req->rq_flvr.sf_rpc)) {
	case SPTLRPC_SVC_NULL:
	case SPTLRPC_SVC_AUTH:
	case SPTLRPC_SVC_INTG:
		LASSERT(ctx->cc_ops->sign);
		rc = ctx->cc_ops->sign(ctx, req);
		break;
	case SPTLRPC_SVC_PRIV:
		LASSERT(ctx->cc_ops->seal);
		rc = ctx->cc_ops->seal(ctx, req);
		break;
	default:
		LBUG();
	}

	if (rc == 0) {
		LASSERT(req->rq_reqdata_len);
		LASSERT(req->rq_reqdata_len % 8 == 0);
		LASSERT(req->rq_reqdata_len <= req->rq_reqbuf_len);
	}

	return rc;
}

static int do_cli_unwrap_reply(struct ptlrpc_request *req)
{
	struct ptlrpc_cli_ctx *ctx = req->rq_cli_ctx;
	int rc;

	LASSERT(ctx);
	LASSERT(ctx->cc_sec);
	LASSERT(req->rq_repbuf);
	LASSERT(req->rq_repdata);
	LASSERT(req->rq_repmsg == NULL);

	req->rq_rep_swab_mask = 0;

	rc = __lustre_unpack_msg(req->rq_repdata, req->rq_repdata_len);
	switch (rc) {
	case 1:
		lustre_set_rep_swabbed(req, MSG_PTLRPC_HEADER_OFF);
	case 0:
		break;
	default:
		CERROR("failed unpack reply: x%llu\n", req->rq_xid);
		return -EPROTO;
	}

	if (req->rq_repdata_len < sizeof(struct lustre_msg)) {
		CERROR("replied data length %d too small\n",
		       req->rq_repdata_len);
		return -EPROTO;
	}

	if (SPTLRPC_FLVR_POLICY(req->rq_repdata->lm_secflvr) !=
	    SPTLRPC_FLVR_POLICY(req->rq_flvr.sf_rpc)) {
		CERROR("reply policy %u doesn't match request policy %u\n",
		       SPTLRPC_FLVR_POLICY(req->rq_repdata->lm_secflvr),
		       SPTLRPC_FLVR_POLICY(req->rq_flvr.sf_rpc));
		return -EPROTO;
	}

	switch (SPTLRPC_FLVR_SVC(req->rq_flvr.sf_rpc)) {
	case SPTLRPC_SVC_NULL:
	case SPTLRPC_SVC_AUTH:
	case SPTLRPC_SVC_INTG:
		LASSERT(ctx->cc_ops->verify);
		rc = ctx->cc_ops->verify(ctx, req);
		break;
	case SPTLRPC_SVC_PRIV:
		LASSERT(ctx->cc_ops->unseal);
		rc = ctx->cc_ops->unseal(ctx, req);
		break;
	default:
		LBUG();
	}
	LASSERT(rc || req->rq_repmsg || req->rq_resend);

	if (SPTLRPC_FLVR_POLICY(req->rq_flvr.sf_rpc) != SPTLRPC_POLICY_NULL &&
	    !req->rq_ctx_init)
		req->rq_rep_swab_mask = 0;
	return rc;
}

/**
 * Used by ptlrpc client, to perform security transformation upon the reply
 * message of \a req. After return successfully, req->rq_repmsg points to
 * the reply message in clear text.
 *
 * \pre the reply buffer should have been un-posted from LNet, so nothing is
 * going to change.
 */
int sptlrpc_cli_unwrap_reply(struct ptlrpc_request *req)
{
	LASSERT(req->rq_repbuf);
	LASSERT(req->rq_repdata == NULL);
	LASSERT(req->rq_repmsg == NULL);
	LASSERT(req->rq_reply_off + req->rq_nob_received <= req->rq_repbuf_len);

	if (req->rq_reply_off == 0 &&
	    (lustre_msghdr_get_flags(req->rq_reqmsg) & MSGHDR_AT_SUPPORT)) {
		CERROR("real reply with offset 0\n");
		return -EPROTO;
	}

	if (req->rq_reply_off % 8 != 0) {
		CERROR("reply at odd offset %u\n", req->rq_reply_off);
		return -EPROTO;
	}

	req->rq_repdata = (struct lustre_msg *)
				(req->rq_repbuf + req->rq_reply_off);
	req->rq_repdata_len = req->rq_nob_received;

	return do_cli_unwrap_reply(req);
}

/**
 * Used by ptlrpc client, to perform security transformation upon the early
 * reply message of \a req. We expect the rq_reply_off is 0, and
 * rq_nob_received is the early reply size.
 *
 * Because the receive buffer might be still posted, the reply data might be
 * changed at any time, no matter we're holding rq_lock or not. For this reason
 * we allocate a separate ptlrpc_request and reply buffer for early reply
 * processing.
 *
 * \retval 0 success, \a req_ret is filled with a duplicated ptlrpc_request.
 * Later the caller must call sptlrpc_cli_finish_early_reply() on the returned
 * \a *req_ret to release it.
 * \retval -ev error number, and \a req_ret will not be set.
 */
int sptlrpc_cli_unwrap_early_reply(struct ptlrpc_request *req,
				   struct ptlrpc_request **req_ret)
{
	struct ptlrpc_request *early_req;
	char *early_buf;
	int early_bufsz, early_size;
	int rc;

	early_req = ptlrpc_request_cache_alloc(GFP_NOFS);
	if (early_req == NULL)
		return -ENOMEM;

	early_size = req->rq_nob_received;
	early_bufsz = size_roundup_power2(early_size);
	early_buf = libcfs_kvzalloc(early_bufsz, GFP_NOFS);
	if (early_buf == NULL) {
		rc = -ENOMEM;
		goto err_req;
	}

	/* sanity checkings and copy data out, do it inside spinlock */
	spin_lock(&req->rq_lock);

	if (req->rq_replied) {
		spin_unlock(&req->rq_lock);
		rc = -EALREADY;
		goto err_buf;
	}

	LASSERT(req->rq_repbuf);
	LASSERT(req->rq_repdata == NULL);
	LASSERT(req->rq_repmsg == NULL);

	if (req->rq_reply_off != 0) {
		CERROR("early reply with offset %u\n", req->rq_reply_off);
		spin_unlock(&req->rq_lock);
		rc = -EPROTO;
		goto err_buf;
	}

	if (req->rq_nob_received != early_size) {
		/* even another early arrived the size should be the same */
		CERROR("data size has changed from %u to %u\n",
		       early_size, req->rq_nob_received);
		spin_unlock(&req->rq_lock);
		rc = -EINVAL;
		goto err_buf;
	}

	if (req->rq_nob_received < sizeof(struct lustre_msg)) {
		CERROR("early reply length %d too small\n",
		       req->rq_nob_received);
		spin_unlock(&req->rq_lock);
		rc = -EALREADY;
		goto err_buf;
	}

	memcpy(early_buf, req->rq_repbuf, early_size);
	spin_unlock(&req->rq_lock);

	spin_lock_init(&early_req->rq_lock);
	early_req->rq_cli_ctx = sptlrpc_cli_ctx_get(req->rq_cli_ctx);
	early_req->rq_flvr = req->rq_flvr;
	early_req->rq_repbuf = early_buf;
	early_req->rq_repbuf_len = early_bufsz;
	early_req->rq_repdata = (struct lustre_msg *) early_buf;
	early_req->rq_repdata_len = early_size;
	early_req->rq_early = 1;
	early_req->rq_reqmsg = req->rq_reqmsg;

	rc = do_cli_unwrap_reply(early_req);
	if (rc) {
		DEBUG_REQ(D_ADAPTTO, early_req,
			  "error %d unwrap early reply", rc);
		goto err_ctx;
	}

	LASSERT(early_req->rq_repmsg);
	*req_ret = early_req;
	return 0;

err_ctx:
	sptlrpc_cli_ctx_put(early_req->rq_cli_ctx, 1);
err_buf:
	kvfree(early_buf);
err_req:
	ptlrpc_request_cache_free(early_req);
	return rc;
}

/**
 * Used by ptlrpc client, to release a processed early reply \a early_req.
 *
 * \pre \a early_req was obtained from calling sptlrpc_cli_unwrap_early_reply().
 */
void sptlrpc_cli_finish_early_reply(struct ptlrpc_request *early_req)
{
	LASSERT(early_req->rq_repbuf);
	LASSERT(early_req->rq_repdata);
	LASSERT(early_req->rq_repmsg);

	sptlrpc_cli_ctx_put(early_req->rq_cli_ctx, 1);
	kvfree(early_req->rq_repbuf);
	ptlrpc_request_cache_free(early_req);
}

/**************************************************
 * sec ID					 *
 **************************************************/

/*
 * "fixed" sec (e.g. null) use sec_id < 0
 */
static atomic_t sptlrpc_sec_id = ATOMIC_INIT(1);

int sptlrpc_get_next_secid(void)
{
	return atomic_inc_return(&sptlrpc_sec_id);
}
EXPORT_SYMBOL(sptlrpc_get_next_secid);

/**************************************************
 * client side high-level security APIs	   *
 **************************************************/

static int sec_cop_flush_ctx_cache(struct ptlrpc_sec *sec, uid_t uid,
				   int grace, int force)
{
	struct ptlrpc_sec_policy *policy = sec->ps_policy;

	LASSERT(policy->sp_cops);
	LASSERT(policy->sp_cops->flush_ctx_cache);

	return policy->sp_cops->flush_ctx_cache(sec, uid, grace, force);
}

static void sec_cop_destroy_sec(struct ptlrpc_sec *sec)
{
	struct ptlrpc_sec_policy *policy = sec->ps_policy;

	LASSERT_ATOMIC_ZERO(&sec->ps_refcount);
	LASSERT_ATOMIC_ZERO(&sec->ps_nctx);
	LASSERT(policy->sp_cops->destroy_sec);

	CDEBUG(D_SEC, "%s@%p: being destroyed\n", sec->ps_policy->sp_name, sec);

	policy->sp_cops->destroy_sec(sec);
	sptlrpc_policy_put(policy);
}

void sptlrpc_sec_destroy(struct ptlrpc_sec *sec)
{
	sec_cop_destroy_sec(sec);
}
EXPORT_SYMBOL(sptlrpc_sec_destroy);

static void sptlrpc_sec_kill(struct ptlrpc_sec *sec)
{
	LASSERT_ATOMIC_POS(&sec->ps_refcount);

	if (sec->ps_policy->sp_cops->kill_sec) {
		sec->ps_policy->sp_cops->kill_sec(sec);

		sec_cop_flush_ctx_cache(sec, -1, 1, 1);
	}
}

struct ptlrpc_sec *sptlrpc_sec_get(struct ptlrpc_sec *sec)
{
	if (sec)
		atomic_inc(&sec->ps_refcount);

	return sec;
}
EXPORT_SYMBOL(sptlrpc_sec_get);

void sptlrpc_sec_put(struct ptlrpc_sec *sec)
{
	if (sec) {
		LASSERT_ATOMIC_POS(&sec->ps_refcount);

		if (atomic_dec_and_test(&sec->ps_refcount)) {
			sptlrpc_gc_del_sec(sec);
			sec_cop_destroy_sec(sec);
		}
	}
}
EXPORT_SYMBOL(sptlrpc_sec_put);

/*
 * policy module is responsible for taking reference of import
 */
static
struct ptlrpc_sec *sptlrpc_sec_create(struct obd_import *imp,
				      struct ptlrpc_svc_ctx *svc_ctx,
				      struct sptlrpc_flavor *sf,
				      enum lustre_sec_part sp)
{
	struct ptlrpc_sec_policy *policy;
	struct ptlrpc_sec *sec;
	char str[32];

	if (svc_ctx) {
		LASSERT(imp->imp_dlm_fake == 1);

		CDEBUG(D_SEC, "%s %s: reverse sec using flavor %s\n",
		       imp->imp_obd->obd_type->typ_name,
		       imp->imp_obd->obd_name,
		       sptlrpc_flavor2name(sf, str, sizeof(str)));

		policy = sptlrpc_policy_get(svc_ctx->sc_policy);
		sf->sf_flags |= PTLRPC_SEC_FL_REVERSE | PTLRPC_SEC_FL_ROOTONLY;
	} else {
		LASSERT(imp->imp_dlm_fake == 0);

		CDEBUG(D_SEC, "%s %s: select security flavor %s\n",
		       imp->imp_obd->obd_type->typ_name,
		       imp->imp_obd->obd_name,
		       sptlrpc_flavor2name(sf, str, sizeof(str)));

		policy = sptlrpc_wireflavor2policy(sf->sf_rpc);
		if (!policy) {
			CERROR("invalid flavor 0x%x\n", sf->sf_rpc);
			return NULL;
		}
	}

	sec = policy->sp_cops->create_sec(imp, svc_ctx, sf);
	if (sec) {
		atomic_inc(&sec->ps_refcount);

		sec->ps_part = sp;

		if (sec->ps_gc_interval && policy->sp_cops->gc_ctx)
			sptlrpc_gc_add_sec(sec);
	} else {
		sptlrpc_policy_put(policy);
	}

	return sec;
}

struct ptlrpc_sec *sptlrpc_import_sec_ref(struct obd_import *imp)
{
	struct ptlrpc_sec *sec;

	spin_lock(&imp->imp_lock);
	sec = sptlrpc_sec_get(imp->imp_sec);
	spin_unlock(&imp->imp_lock);

	return sec;
}
EXPORT_SYMBOL(sptlrpc_import_sec_ref);

static void sptlrpc_import_sec_install(struct obd_import *imp,
				       struct ptlrpc_sec *sec)
{
	struct ptlrpc_sec *old_sec;

	LASSERT_ATOMIC_POS(&sec->ps_refcount);

	spin_lock(&imp->imp_lock);
	old_sec = imp->imp_sec;
	imp->imp_sec = sec;
	spin_unlock(&imp->imp_lock);

	if (old_sec) {
		sptlrpc_sec_kill(old_sec);

		/* balance the ref taken by this import */
		sptlrpc_sec_put(old_sec);
	}
}

static inline
int flavor_equal(struct sptlrpc_flavor *sf1, struct sptlrpc_flavor *sf2)
{
	return (memcmp(sf1, sf2, sizeof(*sf1)) == 0);
}

static inline
void flavor_copy(struct sptlrpc_flavor *dst, struct sptlrpc_flavor *src)
{
	*dst = *src;
}

static void sptlrpc_import_sec_adapt_inplace(struct obd_import *imp,
					     struct ptlrpc_sec *sec,
					     struct sptlrpc_flavor *sf)
{
	char str1[32], str2[32];

	if (sec->ps_flvr.sf_flags != sf->sf_flags)
		CDEBUG(D_SEC, "changing sec flags: %s -> %s\n",
		       sptlrpc_secflags2str(sec->ps_flvr.sf_flags,
					    str1, sizeof(str1)),
		       sptlrpc_secflags2str(sf->sf_flags,
					    str2, sizeof(str2)));

	spin_lock(&sec->ps_lock);
	flavor_copy(&sec->ps_flvr, sf);
	spin_unlock(&sec->ps_lock);
}

/**
 * To get an appropriate ptlrpc_sec for the \a imp, according to the current
 * configuration. Upon called, imp->imp_sec may or may not be NULL.
 *
 *  - regular import: \a svc_ctx should be NULL and \a flvr is ignored;
 *  - reverse import: \a svc_ctx and \a flvr are obtained from incoming request.
 */
int sptlrpc_import_sec_adapt(struct obd_import *imp,
			     struct ptlrpc_svc_ctx *svc_ctx,
			     struct sptlrpc_flavor *flvr)
{
	struct ptlrpc_connection *conn;
	struct sptlrpc_flavor sf;
	struct ptlrpc_sec *sec, *newsec;
	enum lustre_sec_part sp;
	char str[24];
	int rc = 0;

	might_sleep();

	if (imp == NULL)
		return 0;

	conn = imp->imp_connection;

	if (svc_ctx == NULL) {
		struct client_obd *cliobd = &imp->imp_obd->u.cli;
		/*
		 * normal import, determine flavor from rule set, except
		 * for mgc the flavor is predetermined.
		 */
		if (cliobd->cl_sp_me == LUSTRE_SP_MGC)
			sf = cliobd->cl_flvr_mgc;
		else
			sptlrpc_conf_choose_flavor(cliobd->cl_sp_me,
						   cliobd->cl_sp_to,
						   &cliobd->cl_target_uuid,
						   conn->c_self, &sf);

		sp = imp->imp_obd->u.cli.cl_sp_me;
	} else {
		/* reverse import, determine flavor from incoming request */
		sf = *flvr;

		if (sf.sf_rpc != SPTLRPC_FLVR_NULL)
			sf.sf_flags = PTLRPC_SEC_FL_REVERSE |
				      PTLRPC_SEC_FL_ROOTONLY;

		sp = sptlrpc_target_sec_part(imp->imp_obd);
	}

	sec = sptlrpc_import_sec_ref(imp);
	if (sec) {
		char str2[24];

		if (flavor_equal(&sf, &sec->ps_flvr))
			goto out;

		CDEBUG(D_SEC, "import %s->%s: changing flavor %s -> %s\n",
		       imp->imp_obd->obd_name,
		       obd_uuid2str(&conn->c_remote_uuid),
		       sptlrpc_flavor2name(&sec->ps_flvr, str, sizeof(str)),
		       sptlrpc_flavor2name(&sf, str2, sizeof(str2)));

		if (SPTLRPC_FLVR_POLICY(sf.sf_rpc) ==
		    SPTLRPC_FLVR_POLICY(sec->ps_flvr.sf_rpc) &&
		    SPTLRPC_FLVR_MECH(sf.sf_rpc) ==
		    SPTLRPC_FLVR_MECH(sec->ps_flvr.sf_rpc)) {
			sptlrpc_import_sec_adapt_inplace(imp, sec, &sf);
			goto out;
		}
	} else if (SPTLRPC_FLVR_BASE(sf.sf_rpc) !=
		   SPTLRPC_FLVR_BASE(SPTLRPC_FLVR_NULL)) {
		CDEBUG(D_SEC, "import %s->%s netid %x: select flavor %s\n",
		       imp->imp_obd->obd_name,
		       obd_uuid2str(&conn->c_remote_uuid),
		       LNET_NIDNET(conn->c_self),
		       sptlrpc_flavor2name(&sf, str, sizeof(str)));
	}

	mutex_lock(&imp->imp_sec_mutex);

	newsec = sptlrpc_sec_create(imp, svc_ctx, &sf, sp);
	if (newsec) {
		sptlrpc_import_sec_install(imp, newsec);
	} else {
		CERROR("import %s->%s: failed to create new sec\n",
		       imp->imp_obd->obd_name,
		       obd_uuid2str(&conn->c_remote_uuid));
		rc = -EPERM;
	}

	mutex_unlock(&imp->imp_sec_mutex);
out:
	sptlrpc_sec_put(sec);
	return rc;
}

void sptlrpc_import_sec_put(struct obd_import *imp)
{
	if (imp->imp_sec) {
		sptlrpc_sec_kill(imp->imp_sec);

		sptlrpc_sec_put(imp->imp_sec);
		imp->imp_sec = NULL;
	}
}

static void import_flush_ctx_common(struct obd_import *imp,
				    uid_t uid, int grace, int force)
{
	struct ptlrpc_sec *sec;

	if (imp == NULL)
		return;

	sec = sptlrpc_import_sec_ref(imp);
	if (sec == NULL)
		return;

	sec_cop_flush_ctx_cache(sec, uid, grace, force);
	sptlrpc_sec_put(sec);
}

void sptlrpc_import_flush_root_ctx(struct obd_import *imp)
{
	/* it's important to use grace mode, see explain in
	 * sptlrpc_req_refresh_ctx() */
	import_flush_ctx_common(imp, 0, 1, 1);
}

void sptlrpc_import_flush_my_ctx(struct obd_import *imp)
{
	import_flush_ctx_common(imp, from_kuid(&init_user_ns, current_uid()),
				1, 1);
}
EXPORT_SYMBOL(sptlrpc_import_flush_my_ctx);

void sptlrpc_import_flush_all_ctx(struct obd_import *imp)
{
	import_flush_ctx_common(imp, -1, 1, 1);
}
EXPORT_SYMBOL(sptlrpc_import_flush_all_ctx);

/**
 * Used by ptlrpc client to allocate request buffer of \a req. Upon return
 * successfully, req->rq_reqmsg points to a buffer with size \a msgsize.
 */
int sptlrpc_cli_alloc_reqbuf(struct ptlrpc_request *req, int msgsize)
{
	struct ptlrpc_cli_ctx *ctx = req->rq_cli_ctx;
	struct ptlrpc_sec_policy *policy;
	int rc;

	LASSERT(ctx);
	LASSERT(ctx->cc_sec);
	LASSERT(ctx->cc_sec->ps_policy);
	LASSERT(req->rq_reqmsg == NULL);
	LASSERT_ATOMIC_POS(&ctx->cc_refcount);

	policy = ctx->cc_sec->ps_policy;
	rc = policy->sp_cops->alloc_reqbuf(ctx->cc_sec, req, msgsize);
	if (!rc) {
		LASSERT(req->rq_reqmsg);
		LASSERT(req->rq_reqbuf || req->rq_clrbuf);

		/* zeroing preallocated buffer */
		if (req->rq_pool)
			memset(req->rq_reqmsg, 0, msgsize);
	}

	return rc;
}

/**
 * Used by ptlrpc client to free request buffer of \a req. After this
 * req->rq_reqmsg is set to NULL and should not be accessed anymore.
 */
void sptlrpc_cli_free_reqbuf(struct ptlrpc_request *req)
{
	struct ptlrpc_cli_ctx *ctx = req->rq_cli_ctx;
	struct ptlrpc_sec_policy *policy;

	LASSERT(ctx);
	LASSERT(ctx->cc_sec);
	LASSERT(ctx->cc_sec->ps_policy);
	LASSERT_ATOMIC_POS(&ctx->cc_refcount);

	if (req->rq_reqbuf == NULL && req->rq_clrbuf == NULL)
		return;

	policy = ctx->cc_sec->ps_policy;
	policy->sp_cops->free_reqbuf(ctx->cc_sec, req);
	req->rq_reqmsg = NULL;
}

/*
 * NOTE caller must guarantee the buffer size is enough for the enlargement
 */
void _sptlrpc_enlarge_msg_inplace(struct lustre_msg *msg,
				  int segment, int newsize)
{
	void *src, *dst;
	int oldsize, oldmsg_size, movesize;

	LASSERT(segment < msg->lm_bufcount);
	LASSERT(msg->lm_buflens[segment] <= newsize);

	if (msg->lm_buflens[segment] == newsize)
		return;

	/* nothing to do if we are enlarging the last segment */
	if (segment == msg->lm_bufcount - 1) {
		msg->lm_buflens[segment] = newsize;
		return;
	}

	oldsize = msg->lm_buflens[segment];

	src = lustre_msg_buf(msg, segment + 1, 0);
	msg->lm_buflens[segment] = newsize;
	dst = lustre_msg_buf(msg, segment + 1, 0);
	msg->lm_buflens[segment] = oldsize;

	/* move from segment + 1 to end segment */
	LASSERT(msg->lm_magic == LUSTRE_MSG_MAGIC_V2);
	oldmsg_size = lustre_msg_size_v2(msg->lm_bufcount, msg->lm_buflens);
	movesize = oldmsg_size - ((unsigned long) src - (unsigned long) msg);
	LASSERT(movesize >= 0);

	if (movesize)
		memmove(dst, src, movesize);

	/* note we don't clear the ares where old data live, not secret */

	/* finally set new segment size */
	msg->lm_buflens[segment] = newsize;
}
EXPORT_SYMBOL(_sptlrpc_enlarge_msg_inplace);

/**
 * Used by ptlrpc client to enlarge the \a segment of request message pointed
 * by req->rq_reqmsg to size \a newsize, all previously filled-in data will be
 * preserved after the enlargement. this must be called after original request
 * buffer being allocated.
 *
 * \note after this be called, rq_reqmsg and rq_reqlen might have been changed,
 * so caller should refresh its local pointers if needed.
 */
int sptlrpc_cli_enlarge_reqbuf(struct ptlrpc_request *req,
			       int segment, int newsize)
{
	struct ptlrpc_cli_ctx *ctx = req->rq_cli_ctx;
	struct ptlrpc_sec_cops *cops;
	struct lustre_msg *msg = req->rq_reqmsg;

	LASSERT(ctx);
	LASSERT(msg);
	LASSERT(msg->lm_bufcount > segment);
	LASSERT(msg->lm_buflens[segment] <= newsize);

	if (msg->lm_buflens[segment] == newsize)
		return 0;

	cops = ctx->cc_sec->ps_policy->sp_cops;
	LASSERT(cops->enlarge_reqbuf);
	return cops->enlarge_reqbuf(ctx->cc_sec, req, segment, newsize);
}
EXPORT_SYMBOL(sptlrpc_cli_enlarge_reqbuf);

/**
 * Used by ptlrpc client to allocate reply buffer of \a req.
 *
 * \note After this, req->rq_repmsg is still not accessible.
 */
int sptlrpc_cli_alloc_repbuf(struct ptlrpc_request *req, int msgsize)
{
	struct ptlrpc_cli_ctx *ctx = req->rq_cli_ctx;
	struct ptlrpc_sec_policy *policy;

	LASSERT(ctx);
	LASSERT(ctx->cc_sec);
	LASSERT(ctx->cc_sec->ps_policy);

	if (req->rq_repbuf)
		return 0;

	policy = ctx->cc_sec->ps_policy;
	return policy->sp_cops->alloc_repbuf(ctx->cc_sec, req, msgsize);
}

/**
 * Used by ptlrpc client to free reply buffer of \a req. After this
 * req->rq_repmsg is set to NULL and should not be accessed anymore.
 */
void sptlrpc_cli_free_repbuf(struct ptlrpc_request *req)
{
	struct ptlrpc_cli_ctx *ctx = req->rq_cli_ctx;
	struct ptlrpc_sec_policy *policy;

	LASSERT(ctx);
	LASSERT(ctx->cc_sec);
	LASSERT(ctx->cc_sec->ps_policy);
	LASSERT_ATOMIC_POS(&ctx->cc_refcount);

	if (req->rq_repbuf == NULL)
		return;
	LASSERT(req->rq_repbuf_len);

	policy = ctx->cc_sec->ps_policy;
	policy->sp_cops->free_repbuf(ctx->cc_sec, req);
	req->rq_repmsg = NULL;
}

int sptlrpc_cli_install_rvs_ctx(struct obd_import *imp,
				struct ptlrpc_cli_ctx *ctx)
{
	struct ptlrpc_sec_policy *policy = ctx->cc_sec->ps_policy;

	if (!policy->sp_cops->install_rctx)
		return 0;
	return policy->sp_cops->install_rctx(imp, ctx->cc_sec, ctx);
}

int sptlrpc_svc_install_rvs_ctx(struct obd_import *imp,
				struct ptlrpc_svc_ctx *ctx)
{
	struct ptlrpc_sec_policy *policy = ctx->sc_policy;

	if (!policy->sp_sops->install_rctx)
		return 0;
	return policy->sp_sops->install_rctx(imp, ctx);
}

/****************************************
 * server side security		 *
 ****************************************/

static int flavor_allowed(struct sptlrpc_flavor *exp,
			  struct ptlrpc_request *req)
{
	struct sptlrpc_flavor *flvr = &req->rq_flvr;

	if (exp->sf_rpc == SPTLRPC_FLVR_ANY || exp->sf_rpc == flvr->sf_rpc)
		return 1;

	if ((req->rq_ctx_init || req->rq_ctx_fini) &&
	    SPTLRPC_FLVR_POLICY(exp->sf_rpc) ==
	    SPTLRPC_FLVR_POLICY(flvr->sf_rpc) &&
	    SPTLRPC_FLVR_MECH(exp->sf_rpc) == SPTLRPC_FLVR_MECH(flvr->sf_rpc))
		return 1;

	return 0;
}

#define EXP_FLVR_UPDATE_EXPIRE      (OBD_TIMEOUT_DEFAULT + 10)

/**
 * Given an export \a exp, check whether the flavor of incoming \a req
 * is allowed by the export \a exp. Main logic is about taking care of
 * changing configurations. Return 0 means success.
 */
int sptlrpc_target_export_check(struct obd_export *exp,
				struct ptlrpc_request *req)
{
	struct sptlrpc_flavor flavor;

	if (exp == NULL)
		return 0;

	/* client side export has no imp_reverse, skip
	 * FIXME maybe we should check flavor this as well??? */
	if (exp->exp_imp_reverse == NULL)
		return 0;

	/* don't care about ctx fini rpc */
	if (req->rq_ctx_fini)
		return 0;

	spin_lock(&exp->exp_lock);

	/* if flavor just changed (exp->exp_flvr_changed != 0), we wait for
	 * the first req with the new flavor, then treat it as current flavor,
	 * adapt reverse sec according to it.
	 * note the first rpc with new flavor might not be with root ctx, in
	 * which case delay the sec_adapt by leaving exp_flvr_adapt == 1. */
	if (unlikely(exp->exp_flvr_changed) &&
	    flavor_allowed(&exp->exp_flvr_old[1], req)) {
		/* make the new flavor as "current", and old ones as
		 * about-to-expire */
		CDEBUG(D_SEC, "exp %p: just changed: %x->%x\n", exp,
		       exp->exp_flvr.sf_rpc, exp->exp_flvr_old[1].sf_rpc);
		flavor = exp->exp_flvr_old[1];
		exp->exp_flvr_old[1] = exp->exp_flvr_old[0];
		exp->exp_flvr_expire[1] = exp->exp_flvr_expire[0];
		exp->exp_flvr_old[0] = exp->exp_flvr;
		exp->exp_flvr_expire[0] = get_seconds() +
					  EXP_FLVR_UPDATE_EXPIRE;
		exp->exp_flvr = flavor;

		/* flavor change finished */
		exp->exp_flvr_changed = 0;
		LASSERT(exp->exp_flvr_adapt == 1);

		/* if it's gss, we only interested in root ctx init */
		if (req->rq_auth_gss &&
		    !(req->rq_ctx_init &&
		      (req->rq_auth_usr_root || req->rq_auth_usr_mdt ||
		       req->rq_auth_usr_ost))) {
			spin_unlock(&exp->exp_lock);
			CDEBUG(D_SEC, "is good but not root(%d:%d:%d:%d:%d)\n",
			       req->rq_auth_gss, req->rq_ctx_init,
			       req->rq_auth_usr_root, req->rq_auth_usr_mdt,
			       req->rq_auth_usr_ost);
			return 0;
		}

		exp->exp_flvr_adapt = 0;
		spin_unlock(&exp->exp_lock);

		return sptlrpc_import_sec_adapt(exp->exp_imp_reverse,
						req->rq_svc_ctx, &flavor);
	}

	/* if it equals to the current flavor, we accept it, but need to
	 * dealing with reverse sec/ctx */
	if (likely(flavor_allowed(&exp->exp_flvr, req))) {
		/* most cases should return here, we only interested in
		 * gss root ctx init */
		if (!req->rq_auth_gss || !req->rq_ctx_init ||
		    (!req->rq_auth_usr_root && !req->rq_auth_usr_mdt &&
		     !req->rq_auth_usr_ost)) {
			spin_unlock(&exp->exp_lock);
			return 0;
		}

		/* if flavor just changed, we should not proceed, just leave
		 * it and current flavor will be discovered and replaced
		 * shortly, and let _this_ rpc pass through */
		if (exp->exp_flvr_changed) {
			LASSERT(exp->exp_flvr_adapt);
			spin_unlock(&exp->exp_lock);
			return 0;
		}

		if (exp->exp_flvr_adapt) {
			exp->exp_flvr_adapt = 0;
			CDEBUG(D_SEC, "exp %p (%x|%x|%x): do delayed adapt\n",
			       exp, exp->exp_flvr.sf_rpc,
			       exp->exp_flvr_old[0].sf_rpc,
			       exp->exp_flvr_old[1].sf_rpc);
			flavor = exp->exp_flvr;
			spin_unlock(&exp->exp_lock);

			return sptlrpc_import_sec_adapt(exp->exp_imp_reverse,
							req->rq_svc_ctx,
							&flavor);
		} else {
			CDEBUG(D_SEC, "exp %p (%x|%x|%x): is current flavor, install rvs ctx\n",
			       exp, exp->exp_flvr.sf_rpc,
			       exp->exp_flvr_old[0].sf_rpc,
			       exp->exp_flvr_old[1].sf_rpc);
			spin_unlock(&exp->exp_lock);

			return sptlrpc_svc_install_rvs_ctx(exp->exp_imp_reverse,
							   req->rq_svc_ctx);
		}
	}

	if (exp->exp_flvr_expire[0]) {
		if (exp->exp_flvr_expire[0] >= get_seconds()) {
			if (flavor_allowed(&exp->exp_flvr_old[0], req)) {
				CDEBUG(D_SEC, "exp %p (%x|%x|%x): match the middle one (" CFS_DURATION_T ")\n", exp,
				       exp->exp_flvr.sf_rpc,
				       exp->exp_flvr_old[0].sf_rpc,
				       exp->exp_flvr_old[1].sf_rpc,
				       exp->exp_flvr_expire[0] -
				       get_seconds());
				spin_unlock(&exp->exp_lock);
				return 0;
			}
		} else {
			CDEBUG(D_SEC, "mark middle expired\n");
			exp->exp_flvr_expire[0] = 0;
		}
		CDEBUG(D_SEC, "exp %p (%x|%x|%x): %x not match middle\n", exp,
		       exp->exp_flvr.sf_rpc,
		       exp->exp_flvr_old[0].sf_rpc, exp->exp_flvr_old[1].sf_rpc,
		       req->rq_flvr.sf_rpc);
	}

	/* now it doesn't match the current flavor, the only chance we can
	 * accept it is match the old flavors which is not expired. */
	if (exp->exp_flvr_changed == 0 && exp->exp_flvr_expire[1]) {
		if (exp->exp_flvr_expire[1] >= get_seconds()) {
			if (flavor_allowed(&exp->exp_flvr_old[1], req)) {
				CDEBUG(D_SEC, "exp %p (%x|%x|%x): match the oldest one (" CFS_DURATION_T ")\n",
				       exp,
				       exp->exp_flvr.sf_rpc,
				       exp->exp_flvr_old[0].sf_rpc,
				       exp->exp_flvr_old[1].sf_rpc,
				       exp->exp_flvr_expire[1] -
				       get_seconds());
				spin_unlock(&exp->exp_lock);
				return 0;
			}
		} else {
			CDEBUG(D_SEC, "mark oldest expired\n");
			exp->exp_flvr_expire[1] = 0;
		}
		CDEBUG(D_SEC, "exp %p (%x|%x|%x): %x not match found\n",
		       exp, exp->exp_flvr.sf_rpc,
		       exp->exp_flvr_old[0].sf_rpc, exp->exp_flvr_old[1].sf_rpc,
		       req->rq_flvr.sf_rpc);
	} else {
		CDEBUG(D_SEC, "exp %p (%x|%x|%x): skip the last one\n",
		       exp, exp->exp_flvr.sf_rpc, exp->exp_flvr_old[0].sf_rpc,
		       exp->exp_flvr_old[1].sf_rpc);
	}

	spin_unlock(&exp->exp_lock);

	CWARN("exp %p(%s): req %p (%u|%u|%u|%u|%u|%u) with unauthorized flavor %x, expect %x|%x(%+ld)|%x(%+ld)\n",
	      exp, exp->exp_obd->obd_name,
	      req, req->rq_auth_gss, req->rq_ctx_init, req->rq_ctx_fini,
	      req->rq_auth_usr_root, req->rq_auth_usr_mdt, req->rq_auth_usr_ost,
	      req->rq_flvr.sf_rpc,
	      exp->exp_flvr.sf_rpc,
	      exp->exp_flvr_old[0].sf_rpc,
	      exp->exp_flvr_expire[0] ?
	      (unsigned long) (exp->exp_flvr_expire[0] -
			       get_seconds()) : 0,
	      exp->exp_flvr_old[1].sf_rpc,
	      exp->exp_flvr_expire[1] ?
	      (unsigned long) (exp->exp_flvr_expire[1] -
			       get_seconds()) : 0);
	return -EACCES;
}
EXPORT_SYMBOL(sptlrpc_target_export_check);

void sptlrpc_target_update_exp_flavor(struct obd_device *obd,
				      struct sptlrpc_rule_set *rset)
{
	struct obd_export *exp;
	struct sptlrpc_flavor new_flvr;

	LASSERT(obd);

	spin_lock(&obd->obd_dev_lock);

	list_for_each_entry(exp, &obd->obd_exports, exp_obd_chain) {
		if (exp->exp_connection == NULL)
			continue;

		/* note if this export had just been updated flavor
		 * (exp_flvr_changed == 1), this will override the
		 * previous one. */
		spin_lock(&exp->exp_lock);
		sptlrpc_target_choose_flavor(rset, exp->exp_sp_peer,
					     exp->exp_connection->c_peer.nid,
					     &new_flvr);
		if (exp->exp_flvr_changed ||
		    !flavor_equal(&new_flvr, &exp->exp_flvr)) {
			exp->exp_flvr_old[1] = new_flvr;
			exp->exp_flvr_expire[1] = 0;
			exp->exp_flvr_changed = 1;
			exp->exp_flvr_adapt = 1;

			CDEBUG(D_SEC, "exp %p (%s): updated flavor %x->%x\n",
			       exp, sptlrpc_part2name(exp->exp_sp_peer),
			       exp->exp_flvr.sf_rpc,
			       exp->exp_flvr_old[1].sf_rpc);
		}
		spin_unlock(&exp->exp_lock);
	}

	spin_unlock(&obd->obd_dev_lock);
}
EXPORT_SYMBOL(sptlrpc_target_update_exp_flavor);

static int sptlrpc_svc_check_from(struct ptlrpc_request *req, int svc_rc)
{
	/* peer's claim is unreliable unless gss is being used */
	if (!req->rq_auth_gss || svc_rc == SECSVC_DROP)
		return svc_rc;

	switch (req->rq_sp_from) {
	case LUSTRE_SP_CLI:
		if (req->rq_auth_usr_mdt || req->rq_auth_usr_ost) {
			DEBUG_REQ(D_ERROR, req, "faked source CLI");
			svc_rc = SECSVC_DROP;
		}
		break;
	case LUSTRE_SP_MDT:
		if (!req->rq_auth_usr_mdt) {
			DEBUG_REQ(D_ERROR, req, "faked source MDT");
			svc_rc = SECSVC_DROP;
		}
		break;
	case LUSTRE_SP_OST:
		if (!req->rq_auth_usr_ost) {
			DEBUG_REQ(D_ERROR, req, "faked source OST");
			svc_rc = SECSVC_DROP;
		}
		break;
	case LUSTRE_SP_MGS:
	case LUSTRE_SP_MGC:
		if (!req->rq_auth_usr_root && !req->rq_auth_usr_mdt &&
		    !req->rq_auth_usr_ost) {
			DEBUG_REQ(D_ERROR, req, "faked source MGC/MGS");
			svc_rc = SECSVC_DROP;
		}
		break;
	case LUSTRE_SP_ANY:
	default:
		DEBUG_REQ(D_ERROR, req, "invalid source %u", req->rq_sp_from);
		svc_rc = SECSVC_DROP;
	}

	return svc_rc;
}

/**
 * Used by ptlrpc server, to perform transformation upon request message of
 * incoming \a req. This must be the first thing to do with a incoming
 * request in ptlrpc layer.
 *
 * \retval SECSVC_OK success, and req->rq_reqmsg point to request message in
 * clear text, size is req->rq_reqlen; also req->rq_svc_ctx is set.
 * \retval SECSVC_COMPLETE success, the request has been fully processed, and
 * reply message has been prepared.
 * \retval SECSVC_DROP failed, this request should be dropped.
 */
int sptlrpc_svc_unwrap_request(struct ptlrpc_request *req)
{
	struct ptlrpc_sec_policy *policy;
	struct lustre_msg *msg = req->rq_reqbuf;
	int rc;

	LASSERT(msg);
	LASSERT(req->rq_reqmsg == NULL);
	LASSERT(req->rq_repmsg == NULL);
	LASSERT(req->rq_svc_ctx == NULL);

	req->rq_req_swab_mask = 0;

	rc = __lustre_unpack_msg(msg, req->rq_reqdata_len);
	switch (rc) {
	case 1:
		lustre_set_req_swabbed(req, MSG_PTLRPC_HEADER_OFF);
	case 0:
		break;
	default:
		CERROR("error unpacking request from %s x%llu\n",
		       libcfs_id2str(req->rq_peer), req->rq_xid);
		return SECSVC_DROP;
	}

	req->rq_flvr.sf_rpc = WIRE_FLVR(msg->lm_secflvr);
	req->rq_sp_from = LUSTRE_SP_ANY;
	req->rq_auth_uid = -1;
	req->rq_auth_mapped_uid = -1;

	policy = sptlrpc_wireflavor2policy(req->rq_flvr.sf_rpc);
	if (!policy) {
		CERROR("unsupported rpc flavor %x\n", req->rq_flvr.sf_rpc);
		return SECSVC_DROP;
	}

	LASSERT(policy->sp_sops->accept);
	rc = policy->sp_sops->accept(req);
	sptlrpc_policy_put(policy);
	LASSERT(req->rq_reqmsg || rc != SECSVC_OK);
	LASSERT(req->rq_svc_ctx || rc == SECSVC_DROP);

	/*
	 * if it's not null flavor (which means embedded packing msg),
	 * reset the swab mask for the coming inner msg unpacking.
	 */
	if (SPTLRPC_FLVR_POLICY(req->rq_flvr.sf_rpc) != SPTLRPC_POLICY_NULL)
		req->rq_req_swab_mask = 0;

	/* sanity check for the request source */
	rc = sptlrpc_svc_check_from(req, rc);
	return rc;
}

/**
 * Used by ptlrpc server, to allocate reply buffer for \a req. If succeed,
 * req->rq_reply_state is set, and req->rq_reply_state->rs_msg point to
 * a buffer of \a msglen size.
 */
int sptlrpc_svc_alloc_rs(struct ptlrpc_request *req, int msglen)
{
	struct ptlrpc_sec_policy *policy;
	struct ptlrpc_reply_state *rs;
	int rc;

	LASSERT(req->rq_svc_ctx);
	LASSERT(req->rq_svc_ctx->sc_policy);

	policy = req->rq_svc_ctx->sc_policy;
	LASSERT(policy->sp_sops->alloc_rs);

	rc = policy->sp_sops->alloc_rs(req, msglen);
	if (unlikely(rc == -ENOMEM)) {
		struct ptlrpc_service_part *svcpt = req->rq_rqbd->rqbd_svcpt;
		if (svcpt->scp_service->srv_max_reply_size <
		   msglen + sizeof(struct ptlrpc_reply_state)) {
			/* Just return failure if the size is too big */
			CERROR("size of message is too big (%zd), %d allowed",
				msglen + sizeof(struct ptlrpc_reply_state),
				svcpt->scp_service->srv_max_reply_size);
			return -ENOMEM;
		}

		/* failed alloc, try emergency pool */
		rs = lustre_get_emerg_rs(svcpt);
		if (rs == NULL)
			return -ENOMEM;

		req->rq_reply_state = rs;
		rc = policy->sp_sops->alloc_rs(req, msglen);
		if (rc) {
			lustre_put_emerg_rs(rs);
			req->rq_reply_state = NULL;
		}
	}

	LASSERT(rc != 0 ||
		(req->rq_reply_state && req->rq_reply_state->rs_msg));

	return rc;
}

/**
 * Used by ptlrpc server, to perform transformation upon reply message.
 *
 * \post req->rq_reply_off is set to appropriate server-controlled reply offset.
 * \post req->rq_repmsg and req->rq_reply_state->rs_msg becomes inaccessible.
 */
int sptlrpc_svc_wrap_reply(struct ptlrpc_request *req)
{
	struct ptlrpc_sec_policy *policy;
	int rc;

	LASSERT(req->rq_svc_ctx);
	LASSERT(req->rq_svc_ctx->sc_policy);

	policy = req->rq_svc_ctx->sc_policy;
	LASSERT(policy->sp_sops->authorize);

	rc = policy->sp_sops->authorize(req);
	LASSERT(rc || req->rq_reply_state->rs_repdata_len);

	return rc;
}

/**
 * Used by ptlrpc server, to free reply_state.
 */
void sptlrpc_svc_free_rs(struct ptlrpc_reply_state *rs)
{
	struct ptlrpc_sec_policy *policy;
	unsigned int prealloc;

	LASSERT(rs->rs_svc_ctx);
	LASSERT(rs->rs_svc_ctx->sc_policy);

	policy = rs->rs_svc_ctx->sc_policy;
	LASSERT(policy->sp_sops->free_rs);

	prealloc = rs->rs_prealloc;
	policy->sp_sops->free_rs(rs);

	if (prealloc)
		lustre_put_emerg_rs(rs);
}

void sptlrpc_svc_ctx_addref(struct ptlrpc_request *req)
{
	struct ptlrpc_svc_ctx *ctx = req->rq_svc_ctx;

	if (ctx != NULL)
		atomic_inc(&ctx->sc_refcount);
}

void sptlrpc_svc_ctx_decref(struct ptlrpc_request *req)
{
	struct ptlrpc_svc_ctx *ctx = req->rq_svc_ctx;

	if (ctx == NULL)
		return;

	LASSERT_ATOMIC_POS(&ctx->sc_refcount);
	if (atomic_dec_and_test(&ctx->sc_refcount)) {
		if (ctx->sc_policy->sp_sops->free_ctx)
			ctx->sc_policy->sp_sops->free_ctx(ctx);
	}
	req->rq_svc_ctx = NULL;
}

void sptlrpc_svc_ctx_invalidate(struct ptlrpc_request *req)
{
	struct ptlrpc_svc_ctx *ctx = req->rq_svc_ctx;

	if (ctx == NULL)
		return;

	LASSERT_ATOMIC_POS(&ctx->sc_refcount);
	if (ctx->sc_policy->sp_sops->invalidate_ctx)
		ctx->sc_policy->sp_sops->invalidate_ctx(ctx);
}
EXPORT_SYMBOL(sptlrpc_svc_ctx_invalidate);

/****************************************
 * bulk security			*
 ****************************************/

/**
 * Perform transformation upon bulk data pointed by \a desc. This is called
 * before transforming the request message.
 */
int sptlrpc_cli_wrap_bulk(struct ptlrpc_request *req,
			  struct ptlrpc_bulk_desc *desc)
{
	struct ptlrpc_cli_ctx *ctx;

	LASSERT(req->rq_bulk_read || req->rq_bulk_write);

	if (!req->rq_pack_bulk)
		return 0;

	ctx = req->rq_cli_ctx;
	if (ctx->cc_ops->wrap_bulk)
		return ctx->cc_ops->wrap_bulk(ctx, req, desc);
	return 0;
}
EXPORT_SYMBOL(sptlrpc_cli_wrap_bulk);

/**
 * This is called after unwrap the reply message.
 * return nob of actual plain text size received, or error code.
 */
int sptlrpc_cli_unwrap_bulk_read(struct ptlrpc_request *req,
				 struct ptlrpc_bulk_desc *desc,
				 int nob)
{
	struct ptlrpc_cli_ctx *ctx;
	int rc;

	LASSERT(req->rq_bulk_read && !req->rq_bulk_write);

	if (!req->rq_pack_bulk)
		return desc->bd_nob_transferred;

	ctx = req->rq_cli_ctx;
	if (ctx->cc_ops->unwrap_bulk) {
		rc = ctx->cc_ops->unwrap_bulk(ctx, req, desc);
		if (rc < 0)
			return rc;
	}
	return desc->bd_nob_transferred;
}
EXPORT_SYMBOL(sptlrpc_cli_unwrap_bulk_read);

/**
 * This is called after unwrap the reply message.
 * return 0 for success or error code.
 */
int sptlrpc_cli_unwrap_bulk_write(struct ptlrpc_request *req,
				  struct ptlrpc_bulk_desc *desc)
{
	struct ptlrpc_cli_ctx *ctx;
	int rc;

	LASSERT(!req->rq_bulk_read && req->rq_bulk_write);

	if (!req->rq_pack_bulk)
		return 0;

	ctx = req->rq_cli_ctx;
	if (ctx->cc_ops->unwrap_bulk) {
		rc = ctx->cc_ops->unwrap_bulk(ctx, req, desc);
		if (rc < 0)
			return rc;
	}

	/*
	 * if everything is going right, nob should equals to nob_transferred.
	 * in case of privacy mode, nob_transferred needs to be adjusted.
	 */
	if (desc->bd_nob != desc->bd_nob_transferred) {
		CERROR("nob %d doesn't match transferred nob %d",
		       desc->bd_nob, desc->bd_nob_transferred);
		return -EPROTO;
	}

	return 0;
}
EXPORT_SYMBOL(sptlrpc_cli_unwrap_bulk_write);


/****************************************
 * user descriptor helpers	      *
 ****************************************/

int sptlrpc_current_user_desc_size(void)
{
	int ngroups;

	ngroups = current_ngroups;

	if (ngroups > LUSTRE_MAX_GROUPS)
		ngroups = LUSTRE_MAX_GROUPS;
	return sptlrpc_user_desc_size(ngroups);
}
EXPORT_SYMBOL(sptlrpc_current_user_desc_size);

int sptlrpc_pack_user_desc(struct lustre_msg *msg, int offset)
{
	struct ptlrpc_user_desc *pud;

	pud = lustre_msg_buf(msg, offset, 0);

	pud->pud_uid = from_kuid(&init_user_ns, current_uid());
	pud->pud_gid = from_kgid(&init_user_ns, current_gid());
	pud->pud_fsuid = from_kuid(&init_user_ns, current_fsuid());
	pud->pud_fsgid = from_kgid(&init_user_ns, current_fsgid());
	pud->pud_cap = cfs_curproc_cap_pack();
	pud->pud_ngroups = (msg->lm_buflens[offset] - sizeof(*pud)) / 4;

	task_lock(current);
	if (pud->pud_ngroups > current_ngroups)
		pud->pud_ngroups = current_ngroups;
	memcpy(pud->pud_groups, current_cred()->group_info->blocks[0],
	       pud->pud_ngroups * sizeof(__u32));
	task_unlock(current);

	return 0;
}
EXPORT_SYMBOL(sptlrpc_pack_user_desc);

int sptlrpc_unpack_user_desc(struct lustre_msg *msg, int offset, int swabbed)
{
	struct ptlrpc_user_desc *pud;
	int i;

	pud = lustre_msg_buf(msg, offset, sizeof(*pud));
	if (!pud)
		return -EINVAL;

	if (swabbed) {
		__swab32s(&pud->pud_uid);
		__swab32s(&pud->pud_gid);
		__swab32s(&pud->pud_fsuid);
		__swab32s(&pud->pud_fsgid);
		__swab32s(&pud->pud_cap);
		__swab32s(&pud->pud_ngroups);
	}

	if (pud->pud_ngroups > LUSTRE_MAX_GROUPS) {
		CERROR("%u groups is too large\n", pud->pud_ngroups);
		return -EINVAL;
	}

	if (sizeof(*pud) + pud->pud_ngroups * sizeof(__u32) >
	    msg->lm_buflens[offset]) {
		CERROR("%u groups are claimed but bufsize only %u\n",
		       pud->pud_ngroups, msg->lm_buflens[offset]);
		return -EINVAL;
	}

	if (swabbed) {
		for (i = 0; i < pud->pud_ngroups; i++)
			__swab32s(&pud->pud_groups[i]);
	}

	return 0;
}
EXPORT_SYMBOL(sptlrpc_unpack_user_desc);

/****************************************
 * misc helpers			 *
 ****************************************/

const char *sec2target_str(struct ptlrpc_sec *sec)
{
	if (!sec || !sec->ps_import || !sec->ps_import->imp_obd)
		return "*";
	if (sec_is_reverse(sec))
		return "c";
	return obd_uuid2str(&sec->ps_import->imp_obd->u.cli.cl_target_uuid);
}
EXPORT_SYMBOL(sec2target_str);

/*
 * return true if the bulk data is protected
 */
int sptlrpc_flavor_has_bulk(struct sptlrpc_flavor *flvr)
{
	switch (SPTLRPC_FLVR_BULK_SVC(flvr->sf_rpc)) {
	case SPTLRPC_BULK_SVC_INTG:
	case SPTLRPC_BULK_SVC_PRIV:
		return 1;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(sptlrpc_flavor_has_bulk);

/****************************************
 * crypto API helper/alloc blkciper     *
 ****************************************/

/****************************************
 * initialize/finalize		  *
 ****************************************/

int sptlrpc_init(void)
{
	int rc;

	rwlock_init(&policy_lock);

	rc = sptlrpc_gc_init();
	if (rc)
		goto out;

	rc = sptlrpc_conf_init();
	if (rc)
		goto out_gc;

	rc = sptlrpc_enc_pool_init();
	if (rc)
		goto out_conf;

	rc = sptlrpc_null_init();
	if (rc)
		goto out_pool;

	rc = sptlrpc_plain_init();
	if (rc)
		goto out_null;

	rc = sptlrpc_lproc_init();
	if (rc)
		goto out_plain;

	return 0;

out_plain:
	sptlrpc_plain_fini();
out_null:
	sptlrpc_null_fini();
out_pool:
	sptlrpc_enc_pool_fini();
out_conf:
	sptlrpc_conf_fini();
out_gc:
	sptlrpc_gc_fini();
out:
	return rc;
}

void sptlrpc_fini(void)
{
	sptlrpc_lproc_fini();
	sptlrpc_plain_fini();
	sptlrpc_null_fini();
	sptlrpc_enc_pool_fini();
	sptlrpc_conf_fini();
	sptlrpc_gc_fini();
}
