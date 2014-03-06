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
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/ptlrpc/gss/gss_keyring.c
 *
 * Author: Eric Mei <ericm@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_SEC
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/crypto.h>
#include <linux/key.h>
#include <linux/keyctl.h>
#include <linux/key-type.h>
#include <linux/mutex.h>
#include <asm/atomic.h>

#include <obd.h>
#include <obd_class.h>
#include <obd_support.h>
#include <lustre/lustre_idl.h>
#include <lustre_sec.h>
#include <lustre_net.h>
#include <lustre_import.h>

#include "gss_err.h"
#include "gss_internal.h"
#include "gss_api.h"

static struct ptlrpc_sec_policy gss_policy_keyring;
static struct ptlrpc_ctx_ops gss_keyring_ctxops;
static struct key_type gss_key_type;

static int sec_install_rctx_kr(struct ptlrpc_sec *sec,
			       struct ptlrpc_svc_ctx *svc_ctx);

/*
 * the timeout is only for the case that upcall child process die abnormally.
 * in any other cases it should finally update kernel key.
 *
 * FIXME we'd better to incorporate the client & server side upcall timeouts
 * into the framework of Adaptive Timeouts, but we need to figure out how to
 * make sure that kernel knows the upcall processes is in-progress or died
 * unexpectedly.
 */
#define KEYRING_UPCALL_TIMEOUT  (obd_timeout + obd_timeout)

/****************************************
 * internal helpers		     *
 ****************************************/

#define DUMP_PROCESS_KEYRINGS(tsk)					\
{									\
	CWARN("DUMP PK: %s[%u,%u/%u](<-%s[%u,%u/%u]): "			\
	      "a %d, t %d, p %d, s %d, u %d, us %d, df %d\n",		\
	      tsk->comm, tsk->pid, tsk->uid, tsk->fsuid,		\
	      tsk->parent->comm, tsk->parent->pid,			\
	      tsk->parent->uid, tsk->parent->fsuid,			\
	      tsk->request_key_auth ?					\
	      tsk->request_key_auth->serial : 0,			\
	      key_cred(tsk)->thread_keyring ?				\
	      key_cred(tsk)->thread_keyring->serial : 0,		\
	      key_tgcred(tsk)->process_keyring ?			\
	      key_tgcred(tsk)->process_keyring->serial : 0,		\
	      key_tgcred(tsk)->session_keyring ?			\
	      key_tgcred(tsk)->session_keyring->serial : 0,		\
	      key_cred(tsk)->user->uid_keyring ?			\
	      key_cred(tsk)->user->uid_keyring->serial : 0,		\
	      key_cred(tsk)->user->session_keyring ?			\
	      key_cred(tsk)->user->session_keyring->serial : 0,		\
	      key_cred(tsk)->jit_keyring				\
	     );								\
}

#define DUMP_KEY(key)						   \
{								       \
	CWARN("DUMP KEY: %p(%d) ref %d u%u/g%u desc %s\n",	      \
	      key, key->serial, atomic_read(&key->usage),	       \
	      key->uid, key->gid,				       \
	      key->description ? key->description : "n/a"	       \
	     );							 \
}

#define key_cred(tsk)   ((tsk)->cred)
#define key_tgcred(tsk) ((tsk)->cred->tgcred)

static inline void keyring_upcall_lock(struct gss_sec_keyring *gsec_kr)
{
#ifdef HAVE_KEYRING_UPCALL_SERIALIZED
	mutex_lock(&gsec_kr->gsk_uc_lock);
#endif
}

static inline void keyring_upcall_unlock(struct gss_sec_keyring *gsec_kr)
{
#ifdef HAVE_KEYRING_UPCALL_SERIALIZED
	mutex_unlock(&gsec_kr->gsk_uc_lock);
#endif
}

static inline void key_revoke_locked(struct key *key)
{
	set_bit(KEY_FLAG_REVOKED, &key->flags);
}

static void ctx_upcall_timeout_kr(unsigned long data)
{
	struct ptlrpc_cli_ctx *ctx = (struct ptlrpc_cli_ctx *) data;
	struct key	    *key = ctx2gctx_keyring(ctx)->gck_key;

	CWARN("ctx %p, key %p\n", ctx, key);

	LASSERT(key);

	cli_ctx_expire(ctx);
	key_revoke_locked(key);
}

static
void ctx_start_timer_kr(struct ptlrpc_cli_ctx *ctx, long timeout)
{
	struct gss_cli_ctx_keyring *gctx_kr = ctx2gctx_keyring(ctx);
	struct timer_list	  *timer = gctx_kr->gck_timer;

	LASSERT(timer);

	CDEBUG(D_SEC, "ctx %p: start timer %lds\n", ctx, timeout);
	timeout = timeout * HZ + cfs_time_current();

	init_timer(timer);
	timer->expires = timeout;
	timer->data = (unsigned long) ctx;
	timer->function = ctx_upcall_timeout_kr;

	add_timer(timer);
}

/*
 * caller should make sure no race with other threads
 */
static
void ctx_clear_timer_kr(struct ptlrpc_cli_ctx *ctx)
{
	struct gss_cli_ctx_keyring *gctx_kr = ctx2gctx_keyring(ctx);
	struct timer_list	  *timer = gctx_kr->gck_timer;

	if (timer == NULL)
		return;

	CDEBUG(D_SEC, "ctx %p, key %p\n", ctx, gctx_kr->gck_key);

	gctx_kr->gck_timer = NULL;

	del_singleshot_timer_sync(timer);

	OBD_FREE_PTR(timer);
}

static
struct ptlrpc_cli_ctx *ctx_create_kr(struct ptlrpc_sec *sec,
				     struct vfs_cred *vcred)
{
	struct ptlrpc_cli_ctx      *ctx;
	struct gss_cli_ctx_keyring *gctx_kr;

	OBD_ALLOC_PTR(gctx_kr);
	if (gctx_kr == NULL)
		return NULL;

	OBD_ALLOC_PTR(gctx_kr->gck_timer);
	if (gctx_kr->gck_timer == NULL) {
		OBD_FREE_PTR(gctx_kr);
		return NULL;
	}
	init_timer(gctx_kr->gck_timer);

	ctx = &gctx_kr->gck_base.gc_base;

	if (gss_cli_ctx_init_common(sec, ctx, &gss_keyring_ctxops, vcred)) {
		OBD_FREE_PTR(gctx_kr->gck_timer);
		OBD_FREE_PTR(gctx_kr);
		return NULL;
	}

	ctx->cc_expire = cfs_time_current_sec() + KEYRING_UPCALL_TIMEOUT;
	clear_bit(PTLRPC_CTX_NEW_BIT, &ctx->cc_flags);
	atomic_inc(&ctx->cc_refcount); /* for the caller */

	return ctx;
}

static void ctx_destroy_kr(struct ptlrpc_cli_ctx *ctx)
{
	struct ptlrpc_sec	  *sec = ctx->cc_sec;
	struct gss_cli_ctx_keyring *gctx_kr = ctx2gctx_keyring(ctx);

	CDEBUG(D_SEC, "destroying ctx %p\n", ctx);

	/* at this time the association with key has been broken. */
	LASSERT(sec);
	LASSERT(atomic_read(&sec->ps_refcount) > 0);
	LASSERT(atomic_read(&sec->ps_nctx) > 0);
	LASSERT(test_bit(PTLRPC_CTX_CACHED_BIT, &ctx->cc_flags) == 0);
	LASSERT(gctx_kr->gck_key == NULL);

	ctx_clear_timer_kr(ctx);
	LASSERT(gctx_kr->gck_timer == NULL);

	if (gss_cli_ctx_fini_common(sec, ctx))
		return;

	OBD_FREE_PTR(gctx_kr);

	atomic_dec(&sec->ps_nctx);
	sptlrpc_sec_put(sec);
}

static void ctx_release_kr(struct ptlrpc_cli_ctx *ctx, int sync)
{
	if (sync) {
		ctx_destroy_kr(ctx);
	} else {
		atomic_inc(&ctx->cc_refcount);
		sptlrpc_gc_add_ctx(ctx);
	}
}

static void ctx_put_kr(struct ptlrpc_cli_ctx *ctx, int sync)
{
	LASSERT(atomic_read(&ctx->cc_refcount) > 0);

	if (atomic_dec_and_test(&ctx->cc_refcount))
		ctx_release_kr(ctx, sync);
}

/*
 * key <-> ctx association and rules:
 * - ctx might not bind with any key
 * - key/ctx binding is protected by key semaphore (if the key present)
 * - key and ctx each take a reference of the other
 * - ctx enlist/unlist is protected by ctx spinlock
 * - never enlist a ctx after it's been unlisted
 * - whoever do enlist should also do bind, lock key before enlist:
 *   - lock key -> lock ctx -> enlist -> unlock ctx -> bind -> unlock key
 * - whoever do unlist should also do unbind:
 *   - lock key -> lock ctx -> unlist -> unlock ctx -> unbind -> unlock key
 *   - lock ctx -> unlist -> unlock ctx -> lock key -> unbind -> unlock key
 */

static inline void spin_lock_if(spinlock_t *lock, int condition)
{
	if (condition)
		spin_lock(lock);
}

static inline void spin_unlock_if(spinlock_t *lock, int condition)
{
	if (condition)
		spin_unlock(lock);
}

static void ctx_enlist_kr(struct ptlrpc_cli_ctx *ctx, int is_root, int locked)
{
	struct ptlrpc_sec      *sec = ctx->cc_sec;
	struct gss_sec_keyring *gsec_kr = sec2gsec_keyring(sec);

	LASSERT(!test_bit(PTLRPC_CTX_CACHED_BIT, &ctx->cc_flags));
	LASSERT(atomic_read(&ctx->cc_refcount) > 0);

	spin_lock_if(&sec->ps_lock, !locked);

	atomic_inc(&ctx->cc_refcount);
	set_bit(PTLRPC_CTX_CACHED_BIT, &ctx->cc_flags);
	hlist_add_head(&ctx->cc_cache, &gsec_kr->gsk_clist);
	if (is_root)
		gsec_kr->gsk_root_ctx = ctx;

	spin_unlock_if(&sec->ps_lock, !locked);
}

/*
 * Note after this get called, caller should not access ctx again because
 * it might have been freed, unless caller hold at least one refcount of
 * the ctx.
 *
 * return non-zero if we indeed unlist this ctx.
 */
static int ctx_unlist_kr(struct ptlrpc_cli_ctx *ctx, int locked)
{
	struct ptlrpc_sec       *sec = ctx->cc_sec;
	struct gss_sec_keyring  *gsec_kr = sec2gsec_keyring(sec);

	/* if hashed bit has gone, leave the job to somebody who is doing it */
	if (test_and_clear_bit(PTLRPC_CTX_CACHED_BIT, &ctx->cc_flags) == 0)
		return 0;

	/* drop ref inside spin lock to prevent race with other operations */
	spin_lock_if(&sec->ps_lock, !locked);

	if (gsec_kr->gsk_root_ctx == ctx)
		gsec_kr->gsk_root_ctx = NULL;
	hlist_del_init(&ctx->cc_cache);
	atomic_dec(&ctx->cc_refcount);

	spin_unlock_if(&sec->ps_lock, !locked);

	return 1;
}

/*
 * bind a key with a ctx together.
 * caller must hold write lock of the key, as well as ref on key & ctx.
 */
static void bind_key_ctx(struct key *key, struct ptlrpc_cli_ctx *ctx)
{
	LASSERT(atomic_read(&ctx->cc_refcount) > 0);
	LASSERT(atomic_read(&key->usage) > 0);
	LASSERT(ctx2gctx_keyring(ctx)->gck_key == NULL);
	LASSERT(key->payload.data == NULL);

	/* at this time context may or may not in list. */
	key_get(key);
	atomic_inc(&ctx->cc_refcount);
	ctx2gctx_keyring(ctx)->gck_key = key;
	key->payload.data = ctx;
}

/*
 * unbind a key and a ctx.
 * caller must hold write lock, as well as a ref of the key.
 */
static void unbind_key_ctx(struct key *key, struct ptlrpc_cli_ctx *ctx)
{
	LASSERT(key->payload.data == ctx);
	LASSERT(test_bit(PTLRPC_CTX_CACHED_BIT, &ctx->cc_flags) == 0);

	/* must revoke the key, or others may treat it as newly created */
	key_revoke_locked(key);

	key->payload.data = NULL;
	ctx2gctx_keyring(ctx)->gck_key = NULL;

	/* once ctx get split from key, the timer is meaningless */
	ctx_clear_timer_kr(ctx);

	ctx_put_kr(ctx, 1);
	key_put(key);
}

/*
 * given a ctx, unbind with its coupled key, if any.
 * unbind could only be called once, so we don't worry the key be released
 * by someone else.
 */
static void unbind_ctx_kr(struct ptlrpc_cli_ctx *ctx)
{
	struct key      *key = ctx2gctx_keyring(ctx)->gck_key;

	if (key) {
		LASSERT(key->payload.data == ctx);

		key_get(key);
		down_write(&key->sem);
		unbind_key_ctx(key, ctx);
		up_write(&key->sem);
		key_put(key);
	}
}

/*
 * given a key, unbind with its coupled ctx, if any.
 * caller must hold write lock, as well as a ref of the key.
 */
static void unbind_key_locked(struct key *key)
{
	struct ptlrpc_cli_ctx   *ctx = key->payload.data;

	if (ctx)
		unbind_key_ctx(key, ctx);
}

/*
 * unlist a ctx, and unbind from coupled key
 */
static void kill_ctx_kr(struct ptlrpc_cli_ctx *ctx)
{
	if (ctx_unlist_kr(ctx, 0))
		unbind_ctx_kr(ctx);
}

/*
 * given a key, unlist and unbind with the coupled ctx (if any).
 * caller must hold write lock, as well as a ref of the key.
 */
static void kill_key_locked(struct key *key)
{
	struct ptlrpc_cli_ctx *ctx = key->payload.data;

	if (ctx && ctx_unlist_kr(ctx, 0))
		unbind_key_locked(key);
}

/*
 * caller should hold one ref on contexts in freelist.
 */
static void dispose_ctx_list_kr(struct hlist_head *freelist)
{
	struct hlist_node      *next;
	struct ptlrpc_cli_ctx  *ctx;
	struct gss_cli_ctx     *gctx;

	hlist_for_each_entry_safe(ctx, next, freelist, cc_cache) {
		hlist_del_init(&ctx->cc_cache);

		/* reverse ctx: update current seq to buddy svcctx if exist.
		 * ideally this should be done at gss_cli_ctx_finalize(), but
		 * the ctx destroy could be delayed by:
		 *  1) ctx still has reference;
		 *  2) ctx destroy is asynchronous;
		 * and reverse import call inval_all_ctx() require this be done
		 *_immediately_ otherwise newly created reverse ctx might copy
		 * the very old sequence number from svcctx. */
		gctx = ctx2gctx(ctx);
		if (!rawobj_empty(&gctx->gc_svc_handle) &&
		    sec_is_reverse(gctx->gc_base.cc_sec)) {
			gss_svc_upcall_update_sequence(&gctx->gc_svc_handle,
					(__u32) atomic_read(&gctx->gc_seq));
		}

		/* we need to wakeup waiting reqs here. the context might
		 * be forced released before upcall finished, then the
		 * late-arrived downcall can't find the ctx even. */
		sptlrpc_cli_ctx_wakeup(ctx);

		unbind_ctx_kr(ctx);
		ctx_put_kr(ctx, 0);
	}
}

/*
 * lookup a root context directly in a sec, return root ctx with a
 * reference taken or NULL.
 */
static
struct ptlrpc_cli_ctx * sec_lookup_root_ctx_kr(struct ptlrpc_sec *sec)
{
	struct gss_sec_keyring  *gsec_kr = sec2gsec_keyring(sec);
	struct ptlrpc_cli_ctx   *ctx = NULL;

	spin_lock(&sec->ps_lock);

	ctx = gsec_kr->gsk_root_ctx;

	if (ctx == NULL && unlikely(sec_is_reverse(sec))) {
		struct ptlrpc_cli_ctx  *tmp;

		/* reverse ctx, search root ctx in list, choose the one
		 * with shortest expire time, which is most possibly have
		 * an established peer ctx at client side. */
		hlist_for_each_entry(tmp, &gsec_kr->gsk_clist, cc_cache) {
			if (ctx == NULL || ctx->cc_expire == 0 ||
			    ctx->cc_expire > tmp->cc_expire) {
				ctx = tmp;
				/* promote to be root_ctx */
				gsec_kr->gsk_root_ctx = ctx;
			}
		}
	}

	if (ctx) {
		LASSERT(atomic_read(&ctx->cc_refcount) > 0);
		LASSERT(!hlist_empty(&gsec_kr->gsk_clist));
		atomic_inc(&ctx->cc_refcount);
	}

	spin_unlock(&sec->ps_lock);

	return ctx;
}

#define RVS_CTX_EXPIRE_NICE    (10)

static
void rvs_sec_install_root_ctx_kr(struct ptlrpc_sec *sec,
				 struct ptlrpc_cli_ctx *new_ctx,
				 struct key *key)
{
	struct gss_sec_keyring *gsec_kr = sec2gsec_keyring(sec);
	struct ptlrpc_cli_ctx  *ctx;
	cfs_time_t	      now;

	LASSERT(sec_is_reverse(sec));

	spin_lock(&sec->ps_lock);

	now = cfs_time_current_sec();

	/* set all existing ctxs short expiry */
	hlist_for_each_entry(ctx, &gsec_kr->gsk_clist, cc_cache) {
		if (ctx->cc_expire > now + RVS_CTX_EXPIRE_NICE) {
			ctx->cc_early_expire = 1;
			ctx->cc_expire = now + RVS_CTX_EXPIRE_NICE;
		}
	}

	/* if there's root_ctx there, instead obsolete the current
	 * immediately, we leave it continue operating for a little while.
	 * hopefully when the first backward rpc with newest ctx send out,
	 * the client side already have the peer ctx well established. */
	ctx_enlist_kr(new_ctx, gsec_kr->gsk_root_ctx ? 0 : 1, 1);

	if (key)
		bind_key_ctx(key, new_ctx);

	spin_unlock(&sec->ps_lock);
}

static void construct_key_desc(void *buf, int bufsize,
			       struct ptlrpc_sec *sec, uid_t uid)
{
	snprintf(buf, bufsize, "%d@%x", uid, sec->ps_id);
	((char *)buf)[bufsize - 1] = '\0';
}

/****************************************
 * sec apis			     *
 ****************************************/

static
struct ptlrpc_sec * gss_sec_create_kr(struct obd_import *imp,
				      struct ptlrpc_svc_ctx *svcctx,
				      struct sptlrpc_flavor *sf)
{
	struct gss_sec_keyring  *gsec_kr;

	OBD_ALLOC(gsec_kr, sizeof(*gsec_kr));
	if (gsec_kr == NULL)
		return NULL;

	INIT_HLIST_HEAD(&gsec_kr->gsk_clist);
	gsec_kr->gsk_root_ctx = NULL;
	mutex_init(&gsec_kr->gsk_root_uc_lock);
#ifdef HAVE_KEYRING_UPCALL_SERIALIZED
	mutex_init(&gsec_kr->gsk_uc_lock);
#endif

	if (gss_sec_create_common(&gsec_kr->gsk_base, &gss_policy_keyring,
				  imp, svcctx, sf))
		goto err_free;

	if (svcctx != NULL &&
	    sec_install_rctx_kr(&gsec_kr->gsk_base.gs_base, svcctx)) {
		gss_sec_destroy_common(&gsec_kr->gsk_base);
		goto err_free;
	}

	return &gsec_kr->gsk_base.gs_base;

err_free:
	OBD_FREE(gsec_kr, sizeof(*gsec_kr));
	return NULL;
}

static
void gss_sec_destroy_kr(struct ptlrpc_sec *sec)
{
	struct gss_sec	  *gsec = sec2gsec(sec);
	struct gss_sec_keyring  *gsec_kr = sec2gsec_keyring(sec);

	CDEBUG(D_SEC, "destroy %s@%p\n", sec->ps_policy->sp_name, sec);

	LASSERT(hlist_empty(&gsec_kr->gsk_clist));
	LASSERT(gsec_kr->gsk_root_ctx == NULL);

	gss_sec_destroy_common(gsec);

	OBD_FREE(gsec_kr, sizeof(*gsec_kr));
}

static inline int user_is_root(struct ptlrpc_sec *sec, struct vfs_cred *vcred)
{
	/* except the ROOTONLY flag, treat it as root user only if real uid
	 * is 0, euid/fsuid being 0 are handled as setuid scenarios */
	if (sec_is_rootonly(sec) || (vcred->vc_uid == 0))
		return 1;
	else
		return 0;
}

/*
 * unlink request key from it's ring, which is linked during request_key().
 * sadly, we have to 'guess' which keyring it's linked to.
 *
 * FIXME this code is fragile, depend on how request_key_link() is implemented.
 */
static void request_key_unlink(struct key *key)
{
	struct task_struct *tsk = current;
	struct key *ring;

	switch (key_cred(tsk)->jit_keyring) {
	case KEY_REQKEY_DEFL_DEFAULT:
	case KEY_REQKEY_DEFL_THREAD_KEYRING:
		ring = key_get(key_cred(tsk)->thread_keyring);
		if (ring)
			break;
	case KEY_REQKEY_DEFL_PROCESS_KEYRING:
		ring = key_get(key_tgcred(tsk)->process_keyring);
		if (ring)
			break;
	case KEY_REQKEY_DEFL_SESSION_KEYRING:
		rcu_read_lock();
		ring = key_get(rcu_dereference(key_tgcred(tsk)
					       ->session_keyring));
		rcu_read_unlock();
		if (ring)
			break;
	case KEY_REQKEY_DEFL_USER_SESSION_KEYRING:
		ring = key_get(key_cred(tsk)->user->session_keyring);
		break;
	case KEY_REQKEY_DEFL_USER_KEYRING:
		ring = key_get(key_cred(tsk)->user->uid_keyring);
		break;
	case KEY_REQKEY_DEFL_GROUP_KEYRING:
	default:
		LBUG();
	}

	LASSERT(ring);
	key_unlink(ring, key);
	key_put(ring);
}

static
struct ptlrpc_cli_ctx * gss_sec_lookup_ctx_kr(struct ptlrpc_sec *sec,
					      struct vfs_cred *vcred,
					      int create, int remove_dead)
{
	struct obd_import       *imp = sec->ps_import;
	struct gss_sec_keyring  *gsec_kr = sec2gsec_keyring(sec);
	struct ptlrpc_cli_ctx   *ctx = NULL;
	unsigned int	     is_root = 0, create_new = 0;
	struct key	      *key;
	char		     desc[24];
	char		    *coinfo;
	int		      coinfo_size;
	char		    *co_flags = "";

	LASSERT(imp != NULL);

	is_root = user_is_root(sec, vcred);

	/* a little bit optimization for root context */
	if (is_root) {
		ctx = sec_lookup_root_ctx_kr(sec);
		/*
		 * Only lookup directly for REVERSE sec, which should
		 * always succeed.
		 */
		if (ctx || sec_is_reverse(sec))
			return ctx;
	}

	LASSERT(create != 0);

	/* for root context, obtain lock and check again, this time hold
	 * the root upcall lock, make sure nobody else populated new root
	 * context after last check. */
	if (is_root) {
		mutex_lock(&gsec_kr->gsk_root_uc_lock);

		ctx = sec_lookup_root_ctx_kr(sec);
		if (ctx)
			goto out;

		/* update reverse handle for root user */
		sec2gsec(sec)->gs_rvs_hdl = gss_get_next_ctx_index();

		switch (sec->ps_part) {
		case LUSTRE_SP_MDT:
			co_flags = "m";
			break;
		case LUSTRE_SP_OST:
			co_flags = "o";
			break;
		case LUSTRE_SP_MGC:
			co_flags = "rmo";
			break;
		case LUSTRE_SP_CLI:
			co_flags = "r";
			break;
		case LUSTRE_SP_MGS:
		default:
			LBUG();
		}
	}

	/* in case of setuid, key will be constructed as owner of fsuid/fsgid,
	 * but we do authentication based on real uid/gid. the key permission
	 * bits will be exactly as POS_ALL, so only processes who subscribed
	 * this key could have the access, although the quota might be counted
	 * on others (fsuid/fsgid).
	 *
	 * keyring will use fsuid/fsgid as upcall parameters, so we have to
	 * encode real uid/gid into callout info.
	 */

	construct_key_desc(desc, sizeof(desc), sec, vcred->vc_uid);

	/* callout info format:
	 * secid:mech:uid:gid:flags:svc_type:peer_nid:target_uuid
	 */
	coinfo_size = sizeof(struct obd_uuid) + MAX_OBD_NAME + 64;
	OBD_ALLOC(coinfo, coinfo_size);
	if (coinfo == NULL)
		goto out;

	snprintf(coinfo, coinfo_size, "%d:%s:%u:%u:%s:%d:"LPX64":%s",
		 sec->ps_id, sec2gsec(sec)->gs_mech->gm_name,
		 vcred->vc_uid, vcred->vc_gid,
		 co_flags, import_to_gss_svc(imp),
		 imp->imp_connection->c_peer.nid, imp->imp_obd->obd_name);

	CDEBUG(D_SEC, "requesting key for %s\n", desc);

	keyring_upcall_lock(gsec_kr);
	key = request_key(&gss_key_type, desc, coinfo);
	keyring_upcall_unlock(gsec_kr);

	OBD_FREE(coinfo, coinfo_size);

	if (IS_ERR(key)) {
		CERROR("failed request key: %ld\n", PTR_ERR(key));
		goto out;
	}
	CDEBUG(D_SEC, "obtained key %08x for %s\n", key->serial, desc);

	/* once payload.data was pointed to a ctx, it never changes until
	 * we de-associate them; but parallel request_key() may return
	 * a key with payload.data == NULL at the same time. so we still
	 * need wirtelock of key->sem to serialize them. */
	down_write(&key->sem);

	if (likely(key->payload.data != NULL)) {
		ctx = key->payload.data;

		LASSERT(atomic_read(&ctx->cc_refcount) >= 1);
		LASSERT(ctx2gctx_keyring(ctx)->gck_key == key);
		LASSERT(atomic_read(&key->usage) >= 2);

		/* simply take a ref and return. it's upper layer's
		 * responsibility to detect & replace dead ctx. */
		atomic_inc(&ctx->cc_refcount);
	} else {
		/* pre initialization with a cli_ctx. this can't be done in
		 * key_instantiate() because we'v no enough information
		 * there. */
		ctx = ctx_create_kr(sec, vcred);
		if (ctx != NULL) {
			ctx_enlist_kr(ctx, is_root, 0);
			bind_key_ctx(key, ctx);

			ctx_start_timer_kr(ctx, KEYRING_UPCALL_TIMEOUT);

			CDEBUG(D_SEC, "installed key %p <-> ctx %p (sec %p)\n",
			       key, ctx, sec);
		} else {
			/* we'd prefer to call key_revoke(), but we more like
			 * to revoke it within this key->sem locked period. */
			key_revoke_locked(key);
		}

		create_new = 1;
	}

	up_write(&key->sem);

	if (is_root && create_new)
		request_key_unlink(key);

	key_put(key);
out:
	if (is_root)
		mutex_unlock(&gsec_kr->gsk_root_uc_lock);
	return ctx;
}

static
void gss_sec_release_ctx_kr(struct ptlrpc_sec *sec,
			    struct ptlrpc_cli_ctx *ctx,
			    int sync)
{
	LASSERT(atomic_read(&sec->ps_refcount) > 0);
	LASSERT(atomic_read(&ctx->cc_refcount) == 0);
	ctx_release_kr(ctx, sync);
}

/*
 * flush context of normal user, we must resort to keyring itself to find out
 * contexts which belong to me.
 *
 * Note here we suppose only to flush _my_ context, the "uid" will
 * be ignored in the search.
 */
static
void flush_user_ctx_cache_kr(struct ptlrpc_sec *sec,
			     uid_t uid,
			     int grace, int force)
{
	struct key	      *key;
	char		     desc[24];

	/* nothing to do for reverse or rootonly sec */
	if (sec_is_reverse(sec) || sec_is_rootonly(sec))
		return;

	construct_key_desc(desc, sizeof(desc), sec, uid);

	/* there should be only one valid key, but we put it in the
	 * loop in case of any weird cases */
	for (;;) {
		key = request_key(&gss_key_type, desc, NULL);
		if (IS_ERR(key)) {
			CDEBUG(D_SEC, "No more key found for current user\n");
			break;
		}

		down_write(&key->sem);

		kill_key_locked(key);

		/* kill_key_locked() should usually revoke the key, but we
		 * revoke it again to make sure, e.g. some case the key may
		 * not well coupled with a context. */
		key_revoke_locked(key);

		up_write(&key->sem);

		key_put(key);
	}
}

/*
 * flush context of root or all, we iterate through the list.
 */
static
void flush_spec_ctx_cache_kr(struct ptlrpc_sec *sec,
			     uid_t uid,
			     int grace, int force)
{
	struct gss_sec_keyring *gsec_kr;
	struct hlist_head	freelist = HLIST_HEAD_INIT;
	struct hlist_node      *next;
	struct ptlrpc_cli_ctx  *ctx;

	gsec_kr = sec2gsec_keyring(sec);

	spin_lock(&sec->ps_lock);
	hlist_for_each_entry_safe(ctx, next,
				      &gsec_kr->gsk_clist, cc_cache) {
		LASSERT(atomic_read(&ctx->cc_refcount) > 0);

		if (uid != -1 && uid != ctx->cc_vcred.vc_uid)
			continue;

		/* at this moment there's at least 2 base reference:
		 * key association and in-list. */
		if (atomic_read(&ctx->cc_refcount) > 2) {
			if (!force)
				continue;
			CWARN("flush busy ctx %p(%u->%s, extra ref %d)\n",
			      ctx, ctx->cc_vcred.vc_uid,
			      sec2target_str(ctx->cc_sec),
			      atomic_read(&ctx->cc_refcount) - 2);
		}

		set_bit(PTLRPC_CTX_DEAD_BIT, &ctx->cc_flags);
		if (!grace)
			clear_bit(PTLRPC_CTX_UPTODATE_BIT, &ctx->cc_flags);

		atomic_inc(&ctx->cc_refcount);

		if (ctx_unlist_kr(ctx, 1)) {
			hlist_add_head(&ctx->cc_cache, &freelist);
		} else {
			LASSERT(atomic_read(&ctx->cc_refcount) >= 2);
			atomic_dec(&ctx->cc_refcount);
		}
	}
	spin_unlock(&sec->ps_lock);

	dispose_ctx_list_kr(&freelist);
}

static
int gss_sec_flush_ctx_cache_kr(struct ptlrpc_sec *sec,
			       uid_t uid, int grace, int force)
{
	CDEBUG(D_SEC, "sec %p(%d, nctx %d), uid %d, grace %d, force %d\n",
	       sec, atomic_read(&sec->ps_refcount),
	       atomic_read(&sec->ps_nctx),
	       uid, grace, force);

	if (uid != -1 && uid != 0)
		flush_user_ctx_cache_kr(sec, uid, grace, force);
	else
		flush_spec_ctx_cache_kr(sec, uid, grace, force);

	return 0;
}

static
void gss_sec_gc_ctx_kr(struct ptlrpc_sec *sec)
{
	struct gss_sec_keyring *gsec_kr = sec2gsec_keyring(sec);
	struct hlist_head	freelist = HLIST_HEAD_INIT;
	struct hlist_node      *next;
	struct ptlrpc_cli_ctx  *ctx;

	CWARN("running gc\n");

	spin_lock(&sec->ps_lock);
	hlist_for_each_entry_safe(ctx, next,
				      &gsec_kr->gsk_clist, cc_cache) {
		LASSERT(atomic_read(&ctx->cc_refcount) > 0);

		atomic_inc(&ctx->cc_refcount);

		if (cli_ctx_check_death(ctx) && ctx_unlist_kr(ctx, 1)) {
			hlist_add_head(&ctx->cc_cache, &freelist);
			CWARN("unhashed ctx %p\n", ctx);
		} else {
			LASSERT(atomic_read(&ctx->cc_refcount) >= 2);
			atomic_dec(&ctx->cc_refcount);
		}
	}
	spin_unlock(&sec->ps_lock);

	dispose_ctx_list_kr(&freelist);
}

static
int gss_sec_display_kr(struct ptlrpc_sec *sec, struct seq_file *seq)
{
	struct gss_sec_keyring *gsec_kr = sec2gsec_keyring(sec);
	struct hlist_node      *next;
	struct ptlrpc_cli_ctx  *ctx;
	struct gss_cli_ctx     *gctx;
	time_t		  now = cfs_time_current_sec();

	spin_lock(&sec->ps_lock);
	hlist_for_each_entry_safe(ctx, next,
				  &gsec_kr->gsk_clist, cc_cache) {
		struct key	     *key;
		char		    flags_str[40];
		char		    mech[40];

		gctx = ctx2gctx(ctx);
		key = ctx2gctx_keyring(ctx)->gck_key;

		gss_cli_ctx_flags2str(ctx->cc_flags,
				      flags_str, sizeof(flags_str));

		if (gctx->gc_mechctx)
			lgss_display(gctx->gc_mechctx, mech, sizeof(mech));
		else
			snprintf(mech, sizeof(mech), "N/A");
		mech[sizeof(mech) - 1] = '\0';

		seq_printf(seq, "%p: uid %u, ref %d, expire %ld(%+ld), fl %s, "
			   "seq %d, win %u, key %08x(ref %d), "
			   "hdl "LPX64":"LPX64", mech: %s\n",
			   ctx, ctx->cc_vcred.vc_uid,
			   atomic_read(&ctx->cc_refcount),
			   ctx->cc_expire,
			   ctx->cc_expire ?  ctx->cc_expire - now : 0,
			   flags_str,
			   atomic_read(&gctx->gc_seq),
			   gctx->gc_win,
			   key ? key->serial : 0,
			   key ? atomic_read(&key->usage) : 0,
			   gss_handle_to_u64(&gctx->gc_handle),
			   gss_handle_to_u64(&gctx->gc_svc_handle),
			   mech);
	}
	spin_unlock(&sec->ps_lock);

	return 0;
}

/****************************************
 * cli_ctx apis			 *
 ****************************************/

static
int gss_cli_ctx_refresh_kr(struct ptlrpc_cli_ctx *ctx)
{
	/* upcall is already on the way */
	return 0;
}

static
int gss_cli_ctx_validate_kr(struct ptlrpc_cli_ctx *ctx)
{
	LASSERT(atomic_read(&ctx->cc_refcount) > 0);
	LASSERT(ctx->cc_sec);

	if (cli_ctx_check_death(ctx)) {
		kill_ctx_kr(ctx);
		return 1;
	}

	if (cli_ctx_is_ready(ctx))
		return 0;
	return 1;
}

static
void gss_cli_ctx_die_kr(struct ptlrpc_cli_ctx *ctx, int grace)
{
	LASSERT(atomic_read(&ctx->cc_refcount) > 0);
	LASSERT(ctx->cc_sec);

	cli_ctx_expire(ctx);
	kill_ctx_kr(ctx);
}

/****************************************
 * (reverse) service		    *
 ****************************************/

/*
 * reverse context could have nothing to do with keyrings. here we still keep
 * the version which bind to a key, for future reference.
 */
#define HAVE_REVERSE_CTX_NOKEY


static
int sec_install_rctx_kr(struct ptlrpc_sec *sec,
			struct ptlrpc_svc_ctx *svc_ctx)
{
	struct ptlrpc_cli_ctx   *cli_ctx;
	struct vfs_cred	  vcred = { 0, 0 };
	int		      rc;

	LASSERT(sec);
	LASSERT(svc_ctx);

	cli_ctx = ctx_create_kr(sec, &vcred);
	if (cli_ctx == NULL)
		return -ENOMEM;

	rc = gss_copy_rvc_cli_ctx(cli_ctx, svc_ctx);
	if (rc) {
		CERROR("failed copy reverse cli ctx: %d\n", rc);

		ctx_put_kr(cli_ctx, 1);
		return rc;
	}

	rvs_sec_install_root_ctx_kr(sec, cli_ctx, NULL);

	ctx_put_kr(cli_ctx, 1);

	return 0;
}


/****************************************
 * service apis			 *
 ****************************************/

static
int gss_svc_accept_kr(struct ptlrpc_request *req)
{
	return gss_svc_accept(&gss_policy_keyring, req);
}

static
int gss_svc_install_rctx_kr(struct obd_import *imp,
			    struct ptlrpc_svc_ctx *svc_ctx)
{
	struct ptlrpc_sec *sec;
	int		rc;

	sec = sptlrpc_import_sec_ref(imp);
	LASSERT(sec);

	rc = sec_install_rctx_kr(sec, svc_ctx);
	sptlrpc_sec_put(sec);

	return rc;
}

/****************************************
 * key apis			     *
 ****************************************/

static
int gss_kt_instantiate(struct key *key, const void *data, size_t datalen)
{
	int	     rc;

	if (data != NULL || datalen != 0) {
		CERROR("invalid: data %p, len %lu\n", data, (long)datalen);
		return -EINVAL;
	}

	if (key->payload.data != 0) {
		CERROR("key already have payload\n");
		return -EINVAL;
	}

	/* link the key to session keyring, so following context negotiation
	 * rpc fired from user space could find this key. This will be unlinked
	 * automatically when upcall processes die.
	 *
	 * we can't do this through keyctl from userspace, because the upcall
	 * might be neither possessor nor owner of the key (setuid).
	 *
	 * the session keyring is created upon upcall, and don't change all
	 * the way until upcall finished, so rcu lock is not needed here.
	 */
	LASSERT(key_tgcred(current)->session_keyring);

	lockdep_off();
	rc = key_link(key_tgcred(current)->session_keyring, key);
	lockdep_on();
	if (unlikely(rc)) {
		CERROR("failed to link key %08x to keyring %08x: %d\n",
		       key->serial,
		       key_tgcred(current)->session_keyring->serial, rc);
		return rc;
	}

	CDEBUG(D_SEC, "key %p instantiated, ctx %p\n", key, key->payload.data);
	return 0;
}

/*
 * called with key semaphore write locked. it means we can operate
 * on the context without fear of loosing refcount.
 */
static
int gss_kt_update(struct key *key, const void *data, size_t datalen)
{
	struct ptlrpc_cli_ctx   *ctx = key->payload.data;
	struct gss_cli_ctx      *gctx;
	rawobj_t		 tmpobj = RAWOBJ_EMPTY;
	__u32		    datalen32 = (__u32) datalen;
	int		      rc;

	if (data == NULL || datalen == 0) {
		CWARN("invalid: data %p, len %lu\n", data, (long)datalen);
		return -EINVAL;
	}

	/* if upcall finished negotiation too fast (mostly likely because
	 * of local error happened) and call kt_update(), the ctx
	 * might be still NULL. but the key will finally be associate
	 * with a context, or be revoked. if key status is fine, return
	 * -EAGAIN to allow userspace sleep a while and call again. */
	if (ctx == NULL) {
		CDEBUG(D_SEC, "update too soon: key %p(%x) flags %lx\n",
		      key, key->serial, key->flags);

		rc = key_validate(key);
		if (rc == 0)
			return -EAGAIN;
		else
			return rc;
	}

	LASSERT(atomic_read(&ctx->cc_refcount) > 0);
	LASSERT(ctx->cc_sec);

	ctx_clear_timer_kr(ctx);

	/* don't proceed if already refreshed */
	if (cli_ctx_is_refreshed(ctx)) {
		CWARN("ctx already done refresh\n");
		return 0;
	}

	sptlrpc_cli_ctx_get(ctx);
	gctx = ctx2gctx(ctx);

	rc = buffer_extract_bytes(&data, &datalen32, &gctx->gc_win,
				  sizeof(gctx->gc_win));
	if (rc) {
		CERROR("failed extract seq_win\n");
		goto out;
	}

	if (gctx->gc_win == 0) {
		__u32   nego_rpc_err, nego_gss_err;

		rc = buffer_extract_bytes(&data, &datalen32, &nego_rpc_err,
					  sizeof(nego_rpc_err));
		if (rc) {
			CERROR("failed to extrace rpc rc\n");
			goto out;
		}

		rc = buffer_extract_bytes(&data, &datalen32, &nego_gss_err,
					  sizeof(nego_gss_err));
		if (rc) {
			CERROR("failed to extrace gss rc\n");
			goto out;
		}

		CERROR("negotiation: rpc err %d, gss err %x\n",
		       nego_rpc_err, nego_gss_err);

		rc = nego_rpc_err ? nego_rpc_err : -EACCES;
	} else {
		rc = rawobj_extract_local_alloc(&gctx->gc_handle,
						(__u32 **) &data, &datalen32);
		if (rc) {
			CERROR("failed extract handle\n");
			goto out;
		}

		rc = rawobj_extract_local(&tmpobj, (__u32 **) &data,&datalen32);
		if (rc) {
			CERROR("failed extract mech\n");
			goto out;
		}

		rc = lgss_import_sec_context(&tmpobj,
					     sec2gsec(ctx->cc_sec)->gs_mech,
					     &gctx->gc_mechctx);
		if (rc != GSS_S_COMPLETE)
			CERROR("failed import context\n");
		else
			rc = 0;
	}
out:
	/* we don't care what current status of this ctx, even someone else
	 * is operating on the ctx at the same time. we just add up our own
	 * opinions here. */
	if (rc == 0) {
		gss_cli_ctx_uptodate(gctx);
	} else {
		/* this will also revoke the key. has to be done before
		 * wakeup waiters otherwise they can find the stale key */
		kill_key_locked(key);

		cli_ctx_expire(ctx);

		if (rc != -ERESTART)
			set_bit(PTLRPC_CTX_ERROR_BIT, &ctx->cc_flags);
	}

	/* let user space think it's a success */
	sptlrpc_cli_ctx_put(ctx, 1);
	return 0;
}

static
int gss_kt_match(const struct key *key, const void *desc)
{
	return (strcmp(key->description, (const char *) desc) == 0);
}

static
void gss_kt_destroy(struct key *key)
{
	LASSERT(key->payload.data == NULL);
	CDEBUG(D_SEC, "destroy key %p\n", key);
}

static
void gss_kt_describe(const struct key *key, struct seq_file *s)
{
	if (key->description == NULL)
		seq_puts(s, "[null]");
	else
		seq_puts(s, key->description);
}

static struct key_type gss_key_type =
{
	.name	   = "lgssc",
	.def_datalen    = 0,
	.instantiate    = gss_kt_instantiate,
	.update	 = gss_kt_update,
	.match	  = gss_kt_match,
	.destroy	= gss_kt_destroy,
	.describe       = gss_kt_describe,
};

/****************************************
 * lustre gss keyring policy	    *
 ****************************************/

static struct ptlrpc_ctx_ops gss_keyring_ctxops = {
	.match		  = gss_cli_ctx_match,
	.refresh		= gss_cli_ctx_refresh_kr,
	.validate	       = gss_cli_ctx_validate_kr,
	.die		    = gss_cli_ctx_die_kr,
	.sign		   = gss_cli_ctx_sign,
	.verify		 = gss_cli_ctx_verify,
	.seal		   = gss_cli_ctx_seal,
	.unseal		 = gss_cli_ctx_unseal,
	.wrap_bulk	      = gss_cli_ctx_wrap_bulk,
	.unwrap_bulk	    = gss_cli_ctx_unwrap_bulk,
};

static struct ptlrpc_sec_cops gss_sec_keyring_cops = {
	.create_sec	     = gss_sec_create_kr,
	.destroy_sec	    = gss_sec_destroy_kr,
	.kill_sec	       = gss_sec_kill,
	.lookup_ctx	     = gss_sec_lookup_ctx_kr,
	.release_ctx	    = gss_sec_release_ctx_kr,
	.flush_ctx_cache	= gss_sec_flush_ctx_cache_kr,
	.gc_ctx		 = gss_sec_gc_ctx_kr,
	.install_rctx	   = gss_sec_install_rctx,
	.alloc_reqbuf	   = gss_alloc_reqbuf,
	.free_reqbuf	    = gss_free_reqbuf,
	.alloc_repbuf	   = gss_alloc_repbuf,
	.free_repbuf	    = gss_free_repbuf,
	.enlarge_reqbuf	 = gss_enlarge_reqbuf,
	.display		= gss_sec_display_kr,
};

static struct ptlrpc_sec_sops gss_sec_keyring_sops = {
	.accept		 = gss_svc_accept_kr,
	.invalidate_ctx	 = gss_svc_invalidate_ctx,
	.alloc_rs	       = gss_svc_alloc_rs,
	.authorize	      = gss_svc_authorize,
	.free_rs		= gss_svc_free_rs,
	.free_ctx	       = gss_svc_free_ctx,
	.prep_bulk	      = gss_svc_prep_bulk,
	.unwrap_bulk	    = gss_svc_unwrap_bulk,
	.wrap_bulk	      = gss_svc_wrap_bulk,
	.install_rctx	   = gss_svc_install_rctx_kr,
};

static struct ptlrpc_sec_policy gss_policy_keyring = {
	.sp_owner	       = THIS_MODULE,
	.sp_name		= "gss.keyring",
	.sp_policy	      = SPTLRPC_POLICY_GSS,
	.sp_cops		= &gss_sec_keyring_cops,
	.sp_sops		= &gss_sec_keyring_sops,
};


int __init gss_init_keyring(void)
{
	int rc;

	rc = register_key_type(&gss_key_type);
	if (rc) {
		CERROR("failed to register keyring type: %d\n", rc);
		return rc;
	}

	rc = sptlrpc_register_policy(&gss_policy_keyring);
	if (rc) {
		unregister_key_type(&gss_key_type);
		return rc;
	}

	return 0;
}

void __exit gss_exit_keyring(void)
{
	unregister_key_type(&gss_key_type);
	sptlrpc_unregister_policy(&gss_policy_keyring);
}
