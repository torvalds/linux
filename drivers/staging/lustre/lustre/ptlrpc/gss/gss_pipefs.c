/*
 * Modifications for Lustre
 *
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Copyright (c) 2012, Intel Corporation.
 *
 * Author: Eric Mei <ericm@clusterfs.com>
 */

/*
 * linux/net/sunrpc/auth_gss.c
 *
 * RPCSEC_GSS client authentication.
 *
 *  Copyright (c) 2000 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Dug Song       <dugsong@monkey.org>
 *  Andy Adamson   <andros@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#define DEBUG_SUBSYSTEM S_SEC
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/crypto.h>
#include <asm/atomic.h>
struct rpc_clnt; /* for rpc_pipefs */
#include <linux/sunrpc/rpc_pipe_fs.h>

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

static struct ptlrpc_sec_policy gss_policy_pipefs;
static struct ptlrpc_ctx_ops gss_pipefs_ctxops;

static int gss_cli_ctx_refresh_pf(struct ptlrpc_cli_ctx *ctx);

static int gss_sec_pipe_upcall_init(struct gss_sec *gsec)
{
	return 0;
}

static void gss_sec_pipe_upcall_fini(struct gss_sec *gsec)
{
}

/****************************************
 * internel context helpers	     *
 ****************************************/

static
struct ptlrpc_cli_ctx *ctx_create_pf(struct ptlrpc_sec *sec,
				     struct vfs_cred *vcred)
{
	struct gss_cli_ctx *gctx;
	int		 rc;

	OBD_ALLOC_PTR(gctx);
	if (gctx == NULL)
		return NULL;

	rc = gss_cli_ctx_init_common(sec, &gctx->gc_base,
				     &gss_pipefs_ctxops, vcred);
	if (rc) {
		OBD_FREE_PTR(gctx);
		return NULL;
	}

	return &gctx->gc_base;
}

static
void ctx_destroy_pf(struct ptlrpc_sec *sec, struct ptlrpc_cli_ctx *ctx)
{
	struct gss_cli_ctx *gctx = ctx2gctx(ctx);

	if (gss_cli_ctx_fini_common(sec, ctx))
		return;

	OBD_FREE_PTR(gctx);

	atomic_dec(&sec->ps_nctx);
	sptlrpc_sec_put(sec);
}

static
void ctx_enhash_pf(struct ptlrpc_cli_ctx *ctx, struct hlist_head *hash)
{
	set_bit(PTLRPC_CTX_CACHED_BIT, &ctx->cc_flags);
	atomic_inc(&ctx->cc_refcount);
	hlist_add_head(&ctx->cc_cache, hash);
}

/*
 * caller must hold spinlock
 */
static
void ctx_unhash_pf(struct ptlrpc_cli_ctx *ctx, struct hlist_head *freelist)
{
	LASSERT(spin_is_locked(&ctx->cc_sec->ps_lock));
	LASSERT(atomic_read(&ctx->cc_refcount) > 0);
	LASSERT(test_bit(PTLRPC_CTX_CACHED_BIT, &ctx->cc_flags));
	LASSERT(!hlist_unhashed(&ctx->cc_cache));

	clear_bit(PTLRPC_CTX_CACHED_BIT, &ctx->cc_flags);

	if (atomic_dec_and_test(&ctx->cc_refcount)) {
		__hlist_del(&ctx->cc_cache);
		hlist_add_head(&ctx->cc_cache, freelist);
	} else {
		hlist_del_init(&ctx->cc_cache);
	}
}

/*
 * return 1 if the context is dead.
 */
static
int ctx_check_death_pf(struct ptlrpc_cli_ctx *ctx,
		       struct hlist_head *freelist)
{
	if (cli_ctx_check_death(ctx)) {
		if (freelist)
			ctx_unhash_pf(ctx, freelist);
		return 1;
	}

	return 0;
}

static inline
int ctx_check_death_locked_pf(struct ptlrpc_cli_ctx *ctx,
			      struct hlist_head *freelist)
{
	LASSERT(ctx->cc_sec);
	LASSERT(atomic_read(&ctx->cc_refcount) > 0);
	LASSERT(test_bit(PTLRPC_CTX_CACHED_BIT, &ctx->cc_flags));

	return ctx_check_death_pf(ctx, freelist);
}

static inline
int ctx_match_pf(struct ptlrpc_cli_ctx *ctx, struct vfs_cred *vcred)
{
	/* a little bit optimization for null policy */
	if (!ctx->cc_ops->match)
		return 1;

	return ctx->cc_ops->match(ctx, vcred);
}

static
void ctx_list_destroy_pf(struct hlist_head *head)
{
	struct ptlrpc_cli_ctx *ctx;

	while (!hlist_empty(head)) {
		ctx = hlist_entry(head->first, struct ptlrpc_cli_ctx,
				      cc_cache);

		LASSERT(atomic_read(&ctx->cc_refcount) == 0);
		LASSERT(test_bit(PTLRPC_CTX_CACHED_BIT,
				     &ctx->cc_flags) == 0);

		hlist_del_init(&ctx->cc_cache);
		ctx_destroy_pf(ctx->cc_sec, ctx);
	}
}

/****************************************
 * context apis			 *
 ****************************************/

static
int gss_cli_ctx_validate_pf(struct ptlrpc_cli_ctx *ctx)
{
	if (ctx_check_death_pf(ctx, NULL))
		return 1;
	if (cli_ctx_is_ready(ctx))
		return 0;
	return 1;
}

static
void gss_cli_ctx_die_pf(struct ptlrpc_cli_ctx *ctx, int grace)
{
	LASSERT(ctx->cc_sec);
	LASSERT(atomic_read(&ctx->cc_refcount) > 0);

	cli_ctx_expire(ctx);

	spin_lock(&ctx->cc_sec->ps_lock);

	if (test_and_clear_bit(PTLRPC_CTX_CACHED_BIT, &ctx->cc_flags)) {
		LASSERT(!hlist_unhashed(&ctx->cc_cache));
		LASSERT(atomic_read(&ctx->cc_refcount) > 1);

		hlist_del_init(&ctx->cc_cache);
		if (atomic_dec_and_test(&ctx->cc_refcount))
			LBUG();
	}

	spin_unlock(&ctx->cc_sec->ps_lock);
}

/****************************************
 * reverse context installation	 *
 ****************************************/

static inline
unsigned int ctx_hash_index(int hashsize, __u64 key)
{
	return (unsigned int) (key & ((__u64) hashsize - 1));
}

static
void gss_sec_ctx_replace_pf(struct gss_sec *gsec,
			    struct ptlrpc_cli_ctx *new)
{
	struct gss_sec_pipefs *gsec_pf;
	struct ptlrpc_cli_ctx *ctx;
	struct hlist_node     *next;
	HLIST_HEAD(freelist);
	unsigned int hash;
	ENTRY;

	gsec_pf = container_of(gsec, struct gss_sec_pipefs, gsp_base);

	hash = ctx_hash_index(gsec_pf->gsp_chash_size,
			      (__u64) new->cc_vcred.vc_uid);
	LASSERT(hash < gsec_pf->gsp_chash_size);

	spin_lock(&gsec->gs_base.ps_lock);

	hlist_for_each_entry_safe(ctx, next,
				      &gsec_pf->gsp_chash[hash], cc_cache) {
		if (!ctx_match_pf(ctx, &new->cc_vcred))
			continue;

		cli_ctx_expire(ctx);
		ctx_unhash_pf(ctx, &freelist);
		break;
	}

	ctx_enhash_pf(new, &gsec_pf->gsp_chash[hash]);

	spin_unlock(&gsec->gs_base.ps_lock);

	ctx_list_destroy_pf(&freelist);
	EXIT;
}

static
int gss_install_rvs_cli_ctx_pf(struct gss_sec *gsec,
			       struct ptlrpc_svc_ctx *svc_ctx)
{
	struct vfs_cred	  vcred;
	struct ptlrpc_cli_ctx   *cli_ctx;
	int		      rc;
	ENTRY;

	vcred.vc_uid = 0;
	vcred.vc_gid = 0;

	cli_ctx = ctx_create_pf(&gsec->gs_base, &vcred);
	if (!cli_ctx)
		RETURN(-ENOMEM);

	rc = gss_copy_rvc_cli_ctx(cli_ctx, svc_ctx);
	if (rc) {
		ctx_destroy_pf(cli_ctx->cc_sec, cli_ctx);
		RETURN(rc);
	}

	gss_sec_ctx_replace_pf(gsec, cli_ctx);
	RETURN(0);
}

static
void gss_ctx_cache_gc_pf(struct gss_sec_pipefs *gsec_pf,
			 struct hlist_head *freelist)
{
	struct ptlrpc_sec       *sec;
	struct ptlrpc_cli_ctx   *ctx;
	struct hlist_node       *next;
	int i;
	ENTRY;

	sec = &gsec_pf->gsp_base.gs_base;

	CDEBUG(D_SEC, "do gc on sec %s@%p\n", sec->ps_policy->sp_name, sec);

	for (i = 0; i < gsec_pf->gsp_chash_size; i++) {
		hlist_for_each_entry_safe(ctx, next,
					      &gsec_pf->gsp_chash[i], cc_cache)
			ctx_check_death_locked_pf(ctx, freelist);
	}

	sec->ps_gc_next = cfs_time_current_sec() + sec->ps_gc_interval;
	EXIT;
}

static
struct ptlrpc_sec* gss_sec_create_pf(struct obd_import *imp,
				     struct ptlrpc_svc_ctx *ctx,
				     struct sptlrpc_flavor *sf)
{
	struct gss_sec_pipefs   *gsec_pf;
	int		      alloc_size, hash_size, i;
	ENTRY;

#define GSS_SEC_PIPEFS_CTX_HASH_SIZE    (32)

	if (ctx ||
	    sf->sf_flags & (PTLRPC_SEC_FL_ROOTONLY | PTLRPC_SEC_FL_REVERSE))
		hash_size = 1;
	else
		hash_size = GSS_SEC_PIPEFS_CTX_HASH_SIZE;

	alloc_size = sizeof(*gsec_pf) +
		     sizeof(struct hlist_head) * hash_size;

	OBD_ALLOC(gsec_pf, alloc_size);
	if (!gsec_pf)
		RETURN(NULL);

	gsec_pf->gsp_chash_size = hash_size;
	for (i = 0; i < hash_size; i++)
		INIT_HLIST_HEAD(&gsec_pf->gsp_chash[i]);

	if (gss_sec_create_common(&gsec_pf->gsp_base, &gss_policy_pipefs,
				  imp, ctx, sf))
		goto err_free;

	if (ctx == NULL) {
		if (gss_sec_pipe_upcall_init(&gsec_pf->gsp_base))
			goto err_destroy;
	} else {
		if (gss_install_rvs_cli_ctx_pf(&gsec_pf->gsp_base, ctx))
			goto err_destroy;
	}

	RETURN(&gsec_pf->gsp_base.gs_base);

err_destroy:
	gss_sec_destroy_common(&gsec_pf->gsp_base);
err_free:
	OBD_FREE(gsec_pf, alloc_size);
	RETURN(NULL);
}

static
void gss_sec_destroy_pf(struct ptlrpc_sec *sec)
{
	struct gss_sec_pipefs   *gsec_pf;
	struct gss_sec	  *gsec;

	CWARN("destroy %s@%p\n", sec->ps_policy->sp_name, sec);

	gsec = container_of(sec, struct gss_sec, gs_base);
	gsec_pf = container_of(gsec, struct gss_sec_pipefs, gsp_base);

	LASSERT(gsec_pf->gsp_chash);
	LASSERT(gsec_pf->gsp_chash_size);

	gss_sec_pipe_upcall_fini(gsec);

	gss_sec_destroy_common(gsec);

	OBD_FREE(gsec, sizeof(*gsec_pf) +
		       sizeof(struct hlist_head) * gsec_pf->gsp_chash_size);
}

static
struct ptlrpc_cli_ctx * gss_sec_lookup_ctx_pf(struct ptlrpc_sec *sec,
					      struct vfs_cred *vcred,
					      int create, int remove_dead)
{
	struct gss_sec	 *gsec;
	struct gss_sec_pipefs  *gsec_pf;
	struct ptlrpc_cli_ctx  *ctx = NULL, *new = NULL;
	struct hlist_head       *hash_head;
	struct hlist_node       *next;
	HLIST_HEAD(freelist);
	unsigned int	    hash, gc = 0, found = 0;
	ENTRY;

	might_sleep();

	gsec = container_of(sec, struct gss_sec, gs_base);
	gsec_pf = container_of(gsec, struct gss_sec_pipefs, gsp_base);

	hash = ctx_hash_index(gsec_pf->gsp_chash_size,
			      (__u64) vcred->vc_uid);
	hash_head = &gsec_pf->gsp_chash[hash];
	LASSERT(hash < gsec_pf->gsp_chash_size);

retry:
	spin_lock(&sec->ps_lock);

	/* gc_next == 0 means never do gc */
	if (remove_dead && sec->ps_gc_next &&
	    cfs_time_after(cfs_time_current_sec(), sec->ps_gc_next)) {
		gss_ctx_cache_gc_pf(gsec_pf, &freelist);
		gc = 1;
	}

	hlist_for_each_entry_safe(ctx, next, hash_head, cc_cache) {
		if (gc == 0 &&
		    ctx_check_death_locked_pf(ctx,
					      remove_dead ? &freelist : NULL))
			continue;

		if (ctx_match_pf(ctx, vcred)) {
			found = 1;
			break;
		}
	}

	if (found) {
		if (new && new != ctx) {
			/* lost the race, just free it */
			hlist_add_head(&new->cc_cache, &freelist);
			new = NULL;
		}

		/* hot node, move to head */
		if (hash_head->first != &ctx->cc_cache) {
			__hlist_del(&ctx->cc_cache);
			hlist_add_head(&ctx->cc_cache, hash_head);
		}
	} else {
		/* don't allocate for reverse sec */
		if (sec_is_reverse(sec)) {
			spin_unlock(&sec->ps_lock);
			RETURN(NULL);
		}

		if (new) {
			ctx_enhash_pf(new, hash_head);
			ctx = new;
		} else if (create) {
			spin_unlock(&sec->ps_lock);
			new = ctx_create_pf(sec, vcred);
			if (new) {
				clear_bit(PTLRPC_CTX_NEW_BIT, &new->cc_flags);
				goto retry;
			}
		} else {
			ctx = NULL;
		}
	}

	/* hold a ref */
	if (ctx)
		atomic_inc(&ctx->cc_refcount);

	spin_unlock(&sec->ps_lock);

	/* the allocator of the context must give the first push to refresh */
	if (new) {
		LASSERT(new == ctx);
		gss_cli_ctx_refresh_pf(new);
	}

	ctx_list_destroy_pf(&freelist);
	RETURN(ctx);
}

static
void gss_sec_release_ctx_pf(struct ptlrpc_sec *sec,
			    struct ptlrpc_cli_ctx *ctx,
			    int sync)
{
	LASSERT(test_bit(PTLRPC_CTX_CACHED_BIT, &ctx->cc_flags) == 0);
	LASSERT(hlist_unhashed(&ctx->cc_cache));

	/* if required async, we must clear the UPTODATE bit to prevent extra
	 * rpcs during destroy procedure. */
	if (!sync)
		clear_bit(PTLRPC_CTX_UPTODATE_BIT, &ctx->cc_flags);

	/* destroy this context */
	ctx_destroy_pf(sec, ctx);
}

/*
 * @uid: which user. "-1" means flush all.
 * @grace: mark context DEAD, allow graceful destroy like notify
 *	 server side, etc.
 * @force: also flush busy entries.
 *
 * return the number of busy context encountered.
 *
 * In any cases, never touch "eternal" contexts.
 */
static
int gss_sec_flush_ctx_cache_pf(struct ptlrpc_sec *sec,
			       uid_t uid,
			       int grace, int force)
{
	struct gss_sec	  *gsec;
	struct gss_sec_pipefs   *gsec_pf;
	struct ptlrpc_cli_ctx   *ctx;
	struct hlist_node       *next;
	HLIST_HEAD(freelist);
	int i, busy = 0;
	ENTRY;

	might_sleep_if(grace);

	gsec = container_of(sec, struct gss_sec, gs_base);
	gsec_pf = container_of(gsec, struct gss_sec_pipefs, gsp_base);

	spin_lock(&sec->ps_lock);
	for (i = 0; i < gsec_pf->gsp_chash_size; i++) {
		hlist_for_each_entry_safe(ctx, next,
					      &gsec_pf->gsp_chash[i],
					      cc_cache) {
			LASSERT(atomic_read(&ctx->cc_refcount) > 0);

			if (uid != -1 && uid != ctx->cc_vcred.vc_uid)
				continue;

			if (atomic_read(&ctx->cc_refcount) > 1) {
				busy++;
				if (!force)
					continue;

				CWARN("flush busy(%d) ctx %p(%u->%s) by force, "
				      "grace %d\n",
				      atomic_read(&ctx->cc_refcount),
				      ctx, ctx->cc_vcred.vc_uid,
				      sec2target_str(ctx->cc_sec), grace);
			}
			ctx_unhash_pf(ctx, &freelist);

			set_bit(PTLRPC_CTX_DEAD_BIT, &ctx->cc_flags);
			if (!grace)
				clear_bit(PTLRPC_CTX_UPTODATE_BIT,
					  &ctx->cc_flags);
		}
	}
	spin_unlock(&sec->ps_lock);

	ctx_list_destroy_pf(&freelist);
	RETURN(busy);
}

/****************************************
 * service apis			 *
 ****************************************/

static
int gss_svc_accept_pf(struct ptlrpc_request *req)
{
	return gss_svc_accept(&gss_policy_pipefs, req);
}

static
int gss_svc_install_rctx_pf(struct obd_import *imp,
			    struct ptlrpc_svc_ctx *ctx)
{
	struct ptlrpc_sec *sec;
	int		rc;

	sec = sptlrpc_import_sec_ref(imp);
	LASSERT(sec);
	rc = gss_install_rvs_cli_ctx_pf(sec2gsec(sec), ctx);

	sptlrpc_sec_put(sec);
	return rc;
}

/****************************************
 * rpc_pipefs definitions	       *
 ****************************************/

#define LUSTRE_PIPE_ROOT	"/lustre"
#define LUSTRE_PIPE_KRB5	LUSTRE_PIPE_ROOT"/krb5"

struct gss_upcall_msg_data {
	__u32			   gum_seq;
	__u32			   gum_uid;
	__u32			   gum_gid;
	__u32			   gum_svc;	/* MDS/OSS... */
	__u64			   gum_nid;	/* peer NID */
	__u8			    gum_obd[64];    /* client obd name */
};

struct gss_upcall_msg {
	struct rpc_pipe_msg	     gum_base;
	atomic_t		    gum_refcount;
	struct list_head		      gum_list;
	__u32			   gum_mechidx;
	struct gss_sec		 *gum_gsec;
	struct gss_cli_ctx	     *gum_gctx;
	struct gss_upcall_msg_data      gum_data;
};

static atomic_t upcall_seq = ATOMIC_INIT(0);

static inline
__u32 upcall_get_sequence(void)
{
	return (__u32) atomic_inc_return(&upcall_seq);
}

enum mech_idx_t {
	MECH_KRB5   = 0,
	MECH_MAX
};

static inline
__u32 mech_name2idx(const char *name)
{
	LASSERT(!strcmp(name, "krb5"));
	return MECH_KRB5;
}

/* pipefs dentries for each mechanisms */
static struct dentry *de_pipes[MECH_MAX] = { NULL, };
/* all upcall messgaes linked here */
static struct list_head upcall_lists[MECH_MAX];
/* and protected by this */
static spinlock_t upcall_locks[MECH_MAX];

static inline
void upcall_list_lock(int idx)
{
	spin_lock(&upcall_locks[idx]);
}

static inline
void upcall_list_unlock(int idx)
{
	spin_unlock(&upcall_locks[idx]);
}

static
void upcall_msg_enlist(struct gss_upcall_msg *msg)
{
	__u32 idx = msg->gum_mechidx;

	upcall_list_lock(idx);
	list_add(&msg->gum_list, &upcall_lists[idx]);
	upcall_list_unlock(idx);
}

static
void upcall_msg_delist(struct gss_upcall_msg *msg)
{
	__u32 idx = msg->gum_mechidx;

	upcall_list_lock(idx);
	list_del_init(&msg->gum_list);
	upcall_list_unlock(idx);
}

/****************************************
 * rpc_pipefs upcall helpers	    *
 ****************************************/

static
void gss_release_msg(struct gss_upcall_msg *gmsg)
{
	ENTRY;
	LASSERT(atomic_read(&gmsg->gum_refcount) > 0);

	if (!atomic_dec_and_test(&gmsg->gum_refcount)) {
		EXIT;
		return;
	}

	if (gmsg->gum_gctx) {
		sptlrpc_cli_ctx_wakeup(&gmsg->gum_gctx->gc_base);
		sptlrpc_cli_ctx_put(&gmsg->gum_gctx->gc_base, 1);
		gmsg->gum_gctx = NULL;
	}

	LASSERT(list_empty(&gmsg->gum_list));
	LASSERT(list_empty(&gmsg->gum_base.list));
	OBD_FREE_PTR(gmsg);
	EXIT;
}

static
void gss_unhash_msg_nolock(struct gss_upcall_msg *gmsg)
{
	__u32 idx = gmsg->gum_mechidx;

	LASSERT(idx < MECH_MAX);
	LASSERT(spin_is_locked(&upcall_locks[idx]));

	if (list_empty(&gmsg->gum_list))
		return;

	list_del_init(&gmsg->gum_list);
	LASSERT(atomic_read(&gmsg->gum_refcount) > 1);
	atomic_dec(&gmsg->gum_refcount);
}

static
void gss_unhash_msg(struct gss_upcall_msg *gmsg)
{
	__u32 idx = gmsg->gum_mechidx;

	LASSERT(idx < MECH_MAX);
	upcall_list_lock(idx);
	gss_unhash_msg_nolock(gmsg);
	upcall_list_unlock(idx);
}

static
void gss_msg_fail_ctx(struct gss_upcall_msg *gmsg)
{
	if (gmsg->gum_gctx) {
		struct ptlrpc_cli_ctx *ctx = &gmsg->gum_gctx->gc_base;

		LASSERT(atomic_read(&ctx->cc_refcount) > 0);
		sptlrpc_cli_ctx_expire(ctx);
		set_bit(PTLRPC_CTX_ERROR_BIT, &ctx->cc_flags);
	}
}

static
struct gss_upcall_msg * gss_find_upcall(__u32 mechidx, __u32 seq)
{
	struct gss_upcall_msg *gmsg;

	upcall_list_lock(mechidx);
	list_for_each_entry(gmsg, &upcall_lists[mechidx], gum_list) {
		if (gmsg->gum_data.gum_seq != seq)
			continue;

		LASSERT(atomic_read(&gmsg->gum_refcount) > 0);
		LASSERT(gmsg->gum_mechidx == mechidx);

		atomic_inc(&gmsg->gum_refcount);
		upcall_list_unlock(mechidx);
		return gmsg;
	}
	upcall_list_unlock(mechidx);
	return NULL;
}

static
int simple_get_bytes(char **buf, __u32 *buflen, void *res, __u32 reslen)
{
	if (*buflen < reslen) {
		CERROR("buflen %u < %u\n", *buflen, reslen);
		return -EINVAL;
	}

	memcpy(res, *buf, reslen);
	*buf += reslen;
	*buflen -= reslen;
	return 0;
}

/****************************************
 * rpc_pipefs apis		      *
 ****************************************/

static
ssize_t gss_pipe_upcall(struct file *filp, struct rpc_pipe_msg *msg,
			char *dst, size_t buflen)
{
	char *data = (char *)msg->data + msg->copied;
	ssize_t mlen = msg->len;
	ssize_t left;
	ENTRY;

	if (mlen > buflen)
		mlen = buflen;
	left = copy_to_user(dst, data, mlen);
	if (left < 0) {
		msg->errno = left;
		RETURN(left);
	}
	mlen -= left;
	msg->copied += mlen;
	msg->errno = 0;
	RETURN(mlen);
}

static
ssize_t gss_pipe_downcall(struct file *filp, const char *src, size_t mlen)
{
	struct rpc_inode	*rpci = RPC_I(filp->f_dentry->d_inode);
	struct gss_upcall_msg   *gss_msg;
	struct ptlrpc_cli_ctx   *ctx;
	struct gss_cli_ctx      *gctx = NULL;
	char		    *buf, *data;
	int		      datalen;
	int		      timeout, rc;
	__u32		    mechidx, seq, gss_err;
	ENTRY;

	mechidx = (__u32) (long) rpci->private;
	LASSERT(mechidx < MECH_MAX);

	OBD_ALLOC(buf, mlen);
	if (!buf)
		RETURN(-ENOMEM);

	if (copy_from_user(buf, src, mlen)) {
		CERROR("failed copy user space data\n");
		GOTO(out_free, rc = -EFAULT);
	}
	data = buf;
	datalen = mlen;

	/* data passed down format:
	 *  - seq
	 *  - timeout
	 *  - gc_win / error
	 *  - wire_ctx (rawobj)
	 *  - mech_ctx (rawobj)
	 */
	if (simple_get_bytes(&data, &datalen, &seq, sizeof(seq))) {
		CERROR("fail to get seq\n");
		GOTO(out_free, rc = -EFAULT);
	}

	gss_msg = gss_find_upcall(mechidx, seq);
	if (!gss_msg) {
		CERROR("upcall %u has aborted earlier\n", seq);
		GOTO(out_free, rc = -EINVAL);
	}

	gss_unhash_msg(gss_msg);
	gctx = gss_msg->gum_gctx;
	LASSERT(gctx);
	LASSERT(atomic_read(&gctx->gc_base.cc_refcount) > 0);

	/* timeout is not in use for now */
	if (simple_get_bytes(&data, &datalen, &timeout, sizeof(timeout)))
		GOTO(out_msg, rc = -EFAULT);

	/* lgssd signal an error by gc_win == 0 */
	if (simple_get_bytes(&data, &datalen, &gctx->gc_win,
			     sizeof(gctx->gc_win)))
		GOTO(out_msg, rc = -EFAULT);

	if (gctx->gc_win == 0) {
		/* followed by:
		 * - rpc error
		 * - gss error
		 */
		if (simple_get_bytes(&data, &datalen, &rc, sizeof(rc)))
			GOTO(out_msg, rc = -EFAULT);
		if (simple_get_bytes(&data, &datalen, &gss_err,sizeof(gss_err)))
			GOTO(out_msg, rc = -EFAULT);

		if (rc == 0 && gss_err == GSS_S_COMPLETE) {
			CWARN("both rpc & gss error code not set\n");
			rc = -EPERM;
		}
	} else {
		rawobj_t tmpobj;

		/* handle */
		if (rawobj_extract_local(&tmpobj, (__u32 **) &data, &datalen))
			GOTO(out_msg, rc = -EFAULT);
		if (rawobj_dup(&gctx->gc_handle, &tmpobj))
			GOTO(out_msg, rc = -ENOMEM);

		/* mechctx */
		if (rawobj_extract_local(&tmpobj, (__u32 **) &data, &datalen))
			GOTO(out_msg, rc = -EFAULT);
		gss_err = lgss_import_sec_context(&tmpobj,
						  gss_msg->gum_gsec->gs_mech,
						  &gctx->gc_mechctx);
		rc = 0;
	}

	if (likely(rc == 0 && gss_err == GSS_S_COMPLETE)) {
		gss_cli_ctx_uptodate(gctx);
	} else {
		ctx = &gctx->gc_base;
		sptlrpc_cli_ctx_expire(ctx);
		if (rc != -ERESTART || gss_err != GSS_S_COMPLETE)
			set_bit(PTLRPC_CTX_ERROR_BIT, &ctx->cc_flags);

		CERROR("refresh ctx %p(uid %d) failed: %d/0x%08x: %s\n",
		       ctx, ctx->cc_vcred.vc_uid, rc, gss_err,
		       test_bit(PTLRPC_CTX_ERROR_BIT, &ctx->cc_flags) ?
		       "fatal error" : "non-fatal");
	}

	rc = mlen;

out_msg:
	gss_release_msg(gss_msg);

out_free:
	OBD_FREE(buf, mlen);
	/* FIXME
	 * hack pipefs: always return asked length unless all following
	 * downcalls might be messed up. */
	rc = mlen;
	RETURN(rc);
}

static
void gss_pipe_destroy_msg(struct rpc_pipe_msg *msg)
{
	struct gss_upcall_msg	  *gmsg;
	struct gss_upcall_msg_data     *gumd;
	static cfs_time_t	       ratelimit = 0;
	ENTRY;

	LASSERT(list_empty(&msg->list));

	/* normally errno is >= 0 */
	if (msg->errno >= 0) {
		EXIT;
		return;
	}

	gmsg = container_of(msg, struct gss_upcall_msg, gum_base);
	gumd = &gmsg->gum_data;
	LASSERT(atomic_read(&gmsg->gum_refcount) > 0);

	CERROR("failed msg %p (seq %u, uid %u, svc %u, nid "LPX64", obd %.*s): "
	       "errno %d\n", msg, gumd->gum_seq, gumd->gum_uid, gumd->gum_svc,
	       gumd->gum_nid, (int) sizeof(gumd->gum_obd),
	       gumd->gum_obd, msg->errno);

	atomic_inc(&gmsg->gum_refcount);
	gss_unhash_msg(gmsg);
	if (msg->errno == -ETIMEDOUT || msg->errno == -EPIPE) {
		cfs_time_t now = cfs_time_current_sec();

		if (cfs_time_after(now, ratelimit)) {
			CWARN("upcall timed out, is lgssd running?\n");
			ratelimit = now + 15;
		}
	}
	gss_msg_fail_ctx(gmsg);
	gss_release_msg(gmsg);
	EXIT;
}

static
void gss_pipe_release(struct inode *inode)
{
	struct rpc_inode *rpci = RPC_I(inode);
	__u32	     idx;
	ENTRY;

	idx = (__u32) (long) rpci->private;
	LASSERT(idx < MECH_MAX);

	upcall_list_lock(idx);
	while (!list_empty(&upcall_lists[idx])) {
		struct gss_upcall_msg      *gmsg;
		struct gss_upcall_msg_data *gumd;

		gmsg = list_entry(upcall_lists[idx].next,
				      struct gss_upcall_msg, gum_list);
		gumd = &gmsg->gum_data;
		LASSERT(list_empty(&gmsg->gum_base.list));

		CERROR("failing remaining msg %p:seq %u, uid %u, svc %u, "
		       "nid "LPX64", obd %.*s\n", gmsg,
		       gumd->gum_seq, gumd->gum_uid, gumd->gum_svc,
		       gumd->gum_nid, (int) sizeof(gumd->gum_obd),
		       gumd->gum_obd);

		gmsg->gum_base.errno = -EPIPE;
		atomic_inc(&gmsg->gum_refcount);
		gss_unhash_msg_nolock(gmsg);

		gss_msg_fail_ctx(gmsg);

		upcall_list_unlock(idx);
		gss_release_msg(gmsg);
		upcall_list_lock(idx);
	}
	upcall_list_unlock(idx);
	EXIT;
}

static struct rpc_pipe_ops gss_upcall_ops = {
	.upcall	 = gss_pipe_upcall,
	.downcall       = gss_pipe_downcall,
	.destroy_msg    = gss_pipe_destroy_msg,
	.release_pipe   = gss_pipe_release,
};

/****************************************
 * upcall helper functions	      *
 ****************************************/

static
int gss_ctx_refresh_pf(struct ptlrpc_cli_ctx *ctx)
{
	struct obd_import	  *imp;
	struct gss_sec	     *gsec;
	struct gss_upcall_msg      *gmsg;
	int			 rc = 0;
	ENTRY;

	might_sleep();

	LASSERT(ctx->cc_sec);
	LASSERT(ctx->cc_sec->ps_import);
	LASSERT(ctx->cc_sec->ps_import->imp_obd);

	imp = ctx->cc_sec->ps_import;
	if (!imp->imp_connection) {
		CERROR("import has no connection set\n");
		RETURN(-EINVAL);
	}

	gsec = container_of(ctx->cc_sec, struct gss_sec, gs_base);

	OBD_ALLOC_PTR(gmsg);
	if (!gmsg)
		RETURN(-ENOMEM);

	/* initialize pipefs base msg */
	INIT_LIST_HEAD(&gmsg->gum_base.list);
	gmsg->gum_base.data = &gmsg->gum_data;
	gmsg->gum_base.len = sizeof(gmsg->gum_data);
	gmsg->gum_base.copied = 0;
	gmsg->gum_base.errno = 0;

	/* init upcall msg */
	atomic_set(&gmsg->gum_refcount, 1);
	gmsg->gum_mechidx = mech_name2idx(gsec->gs_mech->gm_name);
	gmsg->gum_gsec = gsec;
	gmsg->gum_gctx = container_of(sptlrpc_cli_ctx_get(ctx),
				      struct gss_cli_ctx, gc_base);
	gmsg->gum_data.gum_seq = upcall_get_sequence();
	gmsg->gum_data.gum_uid = ctx->cc_vcred.vc_uid;
	gmsg->gum_data.gum_gid = 0; /* not used for now */
	gmsg->gum_data.gum_svc = import_to_gss_svc(imp);
	gmsg->gum_data.gum_nid = imp->imp_connection->c_peer.nid;
	strncpy(gmsg->gum_data.gum_obd, imp->imp_obd->obd_name,
		sizeof(gmsg->gum_data.gum_obd));

	/* This only could happen when sysadmin set it dead/expired
	 * using lctl by force. */
	if (ctx->cc_flags & PTLRPC_CTX_STATUS_MASK) {
		CWARN("ctx %p(%u->%s) was set flags %lx unexpectedly\n",
		      ctx, ctx->cc_vcred.vc_uid, sec2target_str(ctx->cc_sec),
		      ctx->cc_flags);

		LASSERT(!(ctx->cc_flags & PTLRPC_CTX_UPTODATE));
		ctx->cc_flags |= PTLRPC_CTX_DEAD | PTLRPC_CTX_ERROR;

		rc = -EIO;
		goto err_free;
	}

	upcall_msg_enlist(gmsg);

	rc = rpc_queue_upcall(de_pipes[gmsg->gum_mechidx]->d_inode,
			      &gmsg->gum_base);
	if (rc) {
		CERROR("rpc_queue_upcall failed: %d\n", rc);

		upcall_msg_delist(gmsg);
		goto err_free;
	}

	RETURN(0);
err_free:
	OBD_FREE_PTR(gmsg);
	RETURN(rc);
}

static
int gss_cli_ctx_refresh_pf(struct ptlrpc_cli_ctx *ctx)
{
	/* if we are refreshing for root, also update the reverse
	 * handle index, do not confuse reverse contexts. */
	if (ctx->cc_vcred.vc_uid == 0) {
		struct gss_sec *gsec;

		gsec = container_of(ctx->cc_sec, struct gss_sec, gs_base);
		gsec->gs_rvs_hdl = gss_get_next_ctx_index();
	}

	return gss_ctx_refresh_pf(ctx);
}

/****************************************
 * lustre gss pipefs policy	     *
 ****************************************/

static struct ptlrpc_ctx_ops gss_pipefs_ctxops = {
	.match		  = gss_cli_ctx_match,
	.refresh		= gss_cli_ctx_refresh_pf,
	.validate	       = gss_cli_ctx_validate_pf,
	.die		    = gss_cli_ctx_die_pf,
	.sign		   = gss_cli_ctx_sign,
	.verify		 = gss_cli_ctx_verify,
	.seal		   = gss_cli_ctx_seal,
	.unseal		 = gss_cli_ctx_unseal,
	.wrap_bulk	      = gss_cli_ctx_wrap_bulk,
	.unwrap_bulk	    = gss_cli_ctx_unwrap_bulk,
};

static struct ptlrpc_sec_cops gss_sec_pipefs_cops = {
	.create_sec	     = gss_sec_create_pf,
	.destroy_sec	    = gss_sec_destroy_pf,
	.kill_sec	       = gss_sec_kill,
	.lookup_ctx	     = gss_sec_lookup_ctx_pf,
	.release_ctx	    = gss_sec_release_ctx_pf,
	.flush_ctx_cache	= gss_sec_flush_ctx_cache_pf,
	.install_rctx	   = gss_sec_install_rctx,
	.alloc_reqbuf	   = gss_alloc_reqbuf,
	.free_reqbuf	    = gss_free_reqbuf,
	.alloc_repbuf	   = gss_alloc_repbuf,
	.free_repbuf	    = gss_free_repbuf,
	.enlarge_reqbuf	 = gss_enlarge_reqbuf,
};

static struct ptlrpc_sec_sops gss_sec_pipefs_sops = {
	.accept		 = gss_svc_accept_pf,
	.invalidate_ctx	 = gss_svc_invalidate_ctx,
	.alloc_rs	       = gss_svc_alloc_rs,
	.authorize	      = gss_svc_authorize,
	.free_rs		= gss_svc_free_rs,
	.free_ctx	       = gss_svc_free_ctx,
	.unwrap_bulk	    = gss_svc_unwrap_bulk,
	.wrap_bulk	      = gss_svc_wrap_bulk,
	.install_rctx	   = gss_svc_install_rctx_pf,
};

static struct ptlrpc_sec_policy gss_policy_pipefs = {
	.sp_owner	       = THIS_MODULE,
	.sp_name		= "gss.pipefs",
	.sp_policy	      = SPTLRPC_POLICY_GSS_PIPEFS,
	.sp_cops		= &gss_sec_pipefs_cops,
	.sp_sops		= &gss_sec_pipefs_sops,
};

static
int __init gss_init_pipefs_upcall(void)
{
	struct dentry   *de;

	/* pipe dir */
	de = rpc_mkdir(LUSTRE_PIPE_ROOT, NULL);
	if (IS_ERR(de) && PTR_ERR(de) != -EEXIST) {
		CERROR("Failed to create gss pipe dir: %ld\n", PTR_ERR(de));
		return PTR_ERR(de);
	}

	/* FIXME hack pipefs: dput will sometimes cause oops during module
	 * unload and lgssd close the pipe fds. */

	/* krb5 mechanism */
	de = rpc_mkpipe(LUSTRE_PIPE_KRB5, (void *) MECH_KRB5, &gss_upcall_ops,
			RPC_PIPE_WAIT_FOR_OPEN);
	if (!de || IS_ERR(de)) {
		CERROR("failed to make rpc_pipe %s: %ld\n",
		       LUSTRE_PIPE_KRB5, PTR_ERR(de));
		rpc_rmdir(LUSTRE_PIPE_ROOT);
		return PTR_ERR(de);
	}

	de_pipes[MECH_KRB5] = de;
	INIT_LIST_HEAD(&upcall_lists[MECH_KRB5]);
	spin_lock_init(&upcall_locks[MECH_KRB5]);

	return 0;
}

static
void __exit gss_exit_pipefs_upcall(void)
{
	__u32   i;

	for (i = 0; i < MECH_MAX; i++) {
		LASSERT(list_empty(&upcall_lists[i]));

		/* dput pipe dentry here might cause lgssd oops. */
		de_pipes[i] = NULL;
	}

	rpc_unlink(LUSTRE_PIPE_KRB5);
	rpc_rmdir(LUSTRE_PIPE_ROOT);
}

int __init gss_init_pipefs(void)
{
	int rc;

	rc = gss_init_pipefs_upcall();
	if (rc)
		return rc;

	rc = sptlrpc_register_policy(&gss_policy_pipefs);
	if (rc) {
		gss_exit_pipefs_upcall();
		return rc;
	}

	return 0;
}

void __exit gss_exit_pipefs(void)
{
	gss_exit_pipefs_upcall();
	sptlrpc_unregister_policy(&gss_policy_pipefs);
}
