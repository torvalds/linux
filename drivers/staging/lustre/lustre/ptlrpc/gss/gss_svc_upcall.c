/*
 * Modifications for Lustre
 *
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 *
 * Author: Eric Mei <ericm@clusterfs.com>
 */

/*
 * Neil Brown <neilb@cse.unsw.edu.au>
 * J. Bruce Fields <bfields@umich.edu>
 * Andy Adamson <andros@umich.edu>
 * Dug Song <dugsong@monkey.org>
 *
 * RPCSEC_GSS server authentication.
 * This implements RPCSEC_GSS as defined in rfc2203 (rpcsec_gss) and rfc2078
 * (gssapi)
 *
 * The RPCSEC_GSS involves three stages:
 *  1/ context creation
 *  2/ data exchange
 *  3/ context destruction
 *
 * Context creation is handled largely by upcalls to user-space.
 *  In particular, GSS_Accept_sec_context is handled by an upcall
 * Data exchange is handled entirely within the kernel
 *  In particular, GSS_GetMIC, GSS_VerifyMIC, GSS_Seal, GSS_Unseal are in-kernel.
 * Context destruction is handled in-kernel
 *  GSS_Delete_sec_context is in-kernel
 *
 * Context creation is initiated by a RPCSEC_GSS_INIT request arriving.
 * The context handle and gss_token are used as a key into the rpcsec_init cache.
 * The content of this cache includes some of the outputs of GSS_Accept_sec_context,
 * being major_status, minor_status, context_handle, reply_token.
 * These are sent back to the client.
 * Sequence window management is handled by the kernel.  The window size if currently
 * a compile time constant.
 *
 * When user-space is happy that a context is established, it places an entry
 * in the rpcsec_context cache. The key for this cache is the context_handle.
 * The content includes:
 *   uid/gidlist - for determining access rights
 *   mechanism type
 *   mechanism specific information, such as a key
 *
 */

#define DEBUG_SUBSYSTEM S_SEC
#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/hash.h>
#include <linux/mutex.h>
#include <linux/sunrpc/cache.h>

#include <obd.h>
#include <obd_class.h>
#include <obd_support.h>
#include <lustre/lustre_idl.h>
#include <lustre_net.h>
#include <lustre_import.h>
#include <lustre_sec.h>

#include "gss_err.h"
#include "gss_internal.h"
#include "gss_api.h"

#define GSS_SVC_UPCALL_TIMEOUT  (20)

static spinlock_t __ctx_index_lock;
static __u64 __ctx_index;

__u64 gss_get_next_ctx_index(void)
{
	__u64 idx;

	spin_lock(&__ctx_index_lock);
	idx = __ctx_index++;
	spin_unlock(&__ctx_index_lock);

	return idx;
}

static inline unsigned long hash_mem(char *buf, int length, int bits)
{
	unsigned long hash = 0;
	unsigned long l = 0;
	int len = 0;
	unsigned char c;

	do {
		if (len == length) {
			c = (char) len;
			len = -1;
		} else
			c = *buf++;

		l = (l << 8) | c;
		len++;

		if ((len & (BITS_PER_LONG/8-1)) == 0)
			hash = cfs_hash_long(hash^l, BITS_PER_LONG);
	} while (len);

	return hash >> (BITS_PER_LONG - bits);
}

/****************************************
 * rsi cache			    *
 ****************************************/

#define RSI_HASHBITS    (6)
#define RSI_HASHMAX     (1 << RSI_HASHBITS)
#define RSI_HASHMASK    (RSI_HASHMAX - 1)

struct rsi {
	struct cache_head       h;
	__u32		   lustre_svc;
	__u64		   nid;
	wait_queue_head_t	     waitq;
	rawobj_t		in_handle, in_token;
	rawobj_t		out_handle, out_token;
	int		     major_status, minor_status;
};

static struct cache_head *rsi_table[RSI_HASHMAX];
static struct cache_detail rsi_cache;
static struct rsi *rsi_update(struct rsi *new, struct rsi *old);
static struct rsi *rsi_lookup(struct rsi *item);

static inline int rsi_hash(struct rsi *item)
{
	return hash_mem((char *)item->in_handle.data, item->in_handle.len,
			RSI_HASHBITS) ^
	       hash_mem((char *)item->in_token.data, item->in_token.len,
			RSI_HASHBITS);
}

static inline int __rsi_match(struct rsi *item, struct rsi *tmp)
{
	return (rawobj_equal(&item->in_handle, &tmp->in_handle) &&
		rawobj_equal(&item->in_token, &tmp->in_token));
}

static void rsi_free(struct rsi *rsi)
{
	rawobj_free(&rsi->in_handle);
	rawobj_free(&rsi->in_token);
	rawobj_free(&rsi->out_handle);
	rawobj_free(&rsi->out_token);
}

static void rsi_request(struct cache_detail *cd,
			struct cache_head *h,
			char **bpp, int *blen)
{
	struct rsi *rsi = container_of(h, struct rsi, h);
	__u64 index = 0;

	/* if in_handle is null, provide kernel suggestion */
	if (rsi->in_handle.len == 0)
		index = gss_get_next_ctx_index();

	qword_addhex(bpp, blen, (char *) &rsi->lustre_svc,
		     sizeof(rsi->lustre_svc));
	qword_addhex(bpp, blen, (char *) &rsi->nid, sizeof(rsi->nid));
	qword_addhex(bpp, blen, (char *) &index, sizeof(index));
	qword_addhex(bpp, blen, rsi->in_handle.data, rsi->in_handle.len);
	qword_addhex(bpp, blen, rsi->in_token.data, rsi->in_token.len);
	(*bpp)[-1] = '\n';
}

static int rsi_upcall(struct cache_detail *cd, struct cache_head *h)
{
	return sunrpc_cache_pipe_upcall(cd, h, rsi_request);
}

static inline void __rsi_init(struct rsi *new, struct rsi *item)
{
	new->out_handle = RAWOBJ_EMPTY;
	new->out_token = RAWOBJ_EMPTY;

	new->in_handle = item->in_handle;
	item->in_handle = RAWOBJ_EMPTY;
	new->in_token = item->in_token;
	item->in_token = RAWOBJ_EMPTY;

	new->lustre_svc = item->lustre_svc;
	new->nid = item->nid;
	init_waitqueue_head(&new->waitq);
}

static inline void __rsi_update(struct rsi *new, struct rsi *item)
{
	LASSERT(new->out_handle.len == 0);
	LASSERT(new->out_token.len == 0);

	new->out_handle = item->out_handle;
	item->out_handle = RAWOBJ_EMPTY;
	new->out_token = item->out_token;
	item->out_token = RAWOBJ_EMPTY;

	new->major_status = item->major_status;
	new->minor_status = item->minor_status;
}

static void rsi_put(struct kref *ref)
{
	struct rsi *rsi = container_of(ref, struct rsi, h.ref);

	LASSERT(rsi->h.next == NULL);
	rsi_free(rsi);
	OBD_FREE_PTR(rsi);
}

static int rsi_match(struct cache_head *a, struct cache_head *b)
{
	struct rsi *item = container_of(a, struct rsi, h);
	struct rsi *tmp = container_of(b, struct rsi, h);

	return __rsi_match(item, tmp);
}

static void rsi_init(struct cache_head *cnew, struct cache_head *citem)
{
	struct rsi *new = container_of(cnew, struct rsi, h);
	struct rsi *item = container_of(citem, struct rsi, h);

	__rsi_init(new, item);
}

static void update_rsi(struct cache_head *cnew, struct cache_head *citem)
{
	struct rsi *new = container_of(cnew, struct rsi, h);
	struct rsi *item = container_of(citem, struct rsi, h);

	__rsi_update(new, item);
}

static struct cache_head *rsi_alloc(void)
{
	struct rsi *rsi;

	OBD_ALLOC_PTR(rsi);
	if (rsi)
		return &rsi->h;
	else
		return NULL;
}

static int rsi_parse(struct cache_detail *cd, char *mesg, int mlen)
{
	char	   *buf = mesg;
	char	   *ep;
	int	     len;
	struct rsi      rsii, *rsip = NULL;
	time_t	  expiry;
	int	     status = -EINVAL;
	ENTRY;


	memset(&rsii, 0, sizeof(rsii));

	/* handle */
	len = qword_get(&mesg, buf, mlen);
	if (len < 0)
		goto out;
	if (rawobj_alloc(&rsii.in_handle, buf, len)) {
		status = -ENOMEM;
		goto out;
	}

	/* token */
	len = qword_get(&mesg, buf, mlen);
	if (len < 0)
		goto out;
	if (rawobj_alloc(&rsii.in_token, buf, len)) {
		status = -ENOMEM;
		goto out;
	}

	rsip = rsi_lookup(&rsii);
	if (!rsip)
		goto out;

	rsii.h.flags = 0;
	/* expiry */
	expiry = get_expiry(&mesg);
	if (expiry == 0)
		goto out;

	len = qword_get(&mesg, buf, mlen);
	if (len <= 0)
		goto out;

	/* major */
	rsii.major_status = simple_strtol(buf, &ep, 10);
	if (*ep)
		goto out;

	/* minor */
	len = qword_get(&mesg, buf, mlen);
	if (len <= 0)
		goto out;
	rsii.minor_status = simple_strtol(buf, &ep, 10);
	if (*ep)
		goto out;

	/* out_handle */
	len = qword_get(&mesg, buf, mlen);
	if (len < 0)
		goto out;
	if (rawobj_alloc(&rsii.out_handle, buf, len)) {
		status = -ENOMEM;
		goto out;
	}

	/* out_token */
	len = qword_get(&mesg, buf, mlen);
	if (len < 0)
		goto out;
	if (rawobj_alloc(&rsii.out_token, buf, len)) {
		status = -ENOMEM;
		goto out;
	}

	rsii.h.expiry_time = expiry;
	rsip = rsi_update(&rsii, rsip);
	status = 0;
out:
	rsi_free(&rsii);
	if (rsip) {
		wake_up_all(&rsip->waitq);
		cache_put(&rsip->h, &rsi_cache);
	} else {
		status = -ENOMEM;
	}

	if (status)
		CERROR("rsi parse error %d\n", status);
	RETURN(status);
}

static struct cache_detail rsi_cache = {
	.hash_size      = RSI_HASHMAX,
	.hash_table     = rsi_table,
	.name	   = "auth.sptlrpc.init",
	.cache_put      = rsi_put,
	.cache_upcall   = rsi_upcall,
	.cache_parse    = rsi_parse,
	.match	  = rsi_match,
	.init	   = rsi_init,
	.update	 = update_rsi,
	.alloc	  = rsi_alloc,
};

static struct rsi *rsi_lookup(struct rsi *item)
{
	struct cache_head *ch;
	int hash = rsi_hash(item);

	ch = sunrpc_cache_lookup(&rsi_cache, &item->h, hash);
	if (ch)
		return container_of(ch, struct rsi, h);
	else
		return NULL;
}

static struct rsi *rsi_update(struct rsi *new, struct rsi *old)
{
	struct cache_head *ch;
	int hash = rsi_hash(new);

	ch = sunrpc_cache_update(&rsi_cache, &new->h, &old->h, hash);
	if (ch)
		return container_of(ch, struct rsi, h);
	else
		return NULL;
}

/****************************************
 * rsc cache			    *
 ****************************************/

#define RSC_HASHBITS    (10)
#define RSC_HASHMAX     (1 << RSC_HASHBITS)
#define RSC_HASHMASK    (RSC_HASHMAX - 1)

struct rsc {
	struct cache_head       h;
	struct obd_device      *target;
	rawobj_t		handle;
	struct gss_svc_ctx      ctx;
};

static struct cache_head *rsc_table[RSC_HASHMAX];
static struct cache_detail rsc_cache;
static struct rsc *rsc_update(struct rsc *new, struct rsc *old);
static struct rsc *rsc_lookup(struct rsc *item);

static void rsc_free(struct rsc *rsci)
{
	rawobj_free(&rsci->handle);
	rawobj_free(&rsci->ctx.gsc_rvs_hdl);
	lgss_delete_sec_context(&rsci->ctx.gsc_mechctx);
}

static inline int rsc_hash(struct rsc *rsci)
{
	return hash_mem((char *)rsci->handle.data,
			rsci->handle.len, RSC_HASHBITS);
}

static inline int __rsc_match(struct rsc *new, struct rsc *tmp)
{
	return rawobj_equal(&new->handle, &tmp->handle);
}

static inline void __rsc_init(struct rsc *new, struct rsc *tmp)
{
	new->handle = tmp->handle;
	tmp->handle = RAWOBJ_EMPTY;

	new->target = NULL;
	memset(&new->ctx, 0, sizeof(new->ctx));
	new->ctx.gsc_rvs_hdl = RAWOBJ_EMPTY;
}

static inline void __rsc_update(struct rsc *new, struct rsc *tmp)
{
	new->ctx = tmp->ctx;
	tmp->ctx.gsc_rvs_hdl = RAWOBJ_EMPTY;
	tmp->ctx.gsc_mechctx = NULL;

	memset(&new->ctx.gsc_seqdata, 0, sizeof(new->ctx.gsc_seqdata));
	spin_lock_init(&new->ctx.gsc_seqdata.ssd_lock);
}

static void rsc_put(struct kref *ref)
{
	struct rsc *rsci = container_of(ref, struct rsc, h.ref);

	LASSERT(rsci->h.next == NULL);
	rsc_free(rsci);
	OBD_FREE_PTR(rsci);
}

static int rsc_match(struct cache_head *a, struct cache_head *b)
{
	struct rsc *new = container_of(a, struct rsc, h);
	struct rsc *tmp = container_of(b, struct rsc, h);

	return __rsc_match(new, tmp);
}

static void rsc_init(struct cache_head *cnew, struct cache_head *ctmp)
{
	struct rsc *new = container_of(cnew, struct rsc, h);
	struct rsc *tmp = container_of(ctmp, struct rsc, h);

	__rsc_init(new, tmp);
}

static void update_rsc(struct cache_head *cnew, struct cache_head *ctmp)
{
	struct rsc *new = container_of(cnew, struct rsc, h);
	struct rsc *tmp = container_of(ctmp, struct rsc, h);

	__rsc_update(new, tmp);
}

static struct cache_head * rsc_alloc(void)
{
	struct rsc *rsc;

	OBD_ALLOC_PTR(rsc);
	if (rsc)
		return &rsc->h;
	else
		return NULL;
}

static int rsc_parse(struct cache_detail *cd, char *mesg, int mlen)
{
	char		*buf = mesg;
	int		  len, rv, tmp_int;
	struct rsc	   rsci, *rscp = NULL;
	time_t	       expiry;
	int		  status = -EINVAL;
	struct gss_api_mech *gm = NULL;

	memset(&rsci, 0, sizeof(rsci));

	/* context handle */
	len = qword_get(&mesg, buf, mlen);
	if (len < 0) goto out;
	status = -ENOMEM;
	if (rawobj_alloc(&rsci.handle, buf, len))
		goto out;

	rsci.h.flags = 0;
	/* expiry */
	expiry = get_expiry(&mesg);
	status = -EINVAL;
	if (expiry == 0)
		goto out;

	/* remote flag */
	rv = get_int(&mesg, &tmp_int);
	if (rv) {
		CERROR("fail to get remote flag\n");
		goto out;
	}
	rsci.ctx.gsc_remote = (tmp_int != 0);

	/* root user flag */
	rv = get_int(&mesg, &tmp_int);
	if (rv) {
		CERROR("fail to get oss user flag\n");
		goto out;
	}
	rsci.ctx.gsc_usr_root = (tmp_int != 0);

	/* mds user flag */
	rv = get_int(&mesg, &tmp_int);
	if (rv) {
		CERROR("fail to get mds user flag\n");
		goto out;
	}
	rsci.ctx.gsc_usr_mds = (tmp_int != 0);

	/* oss user flag */
	rv = get_int(&mesg, &tmp_int);
	if (rv) {
		CERROR("fail to get oss user flag\n");
		goto out;
	}
	rsci.ctx.gsc_usr_oss = (tmp_int != 0);

	/* mapped uid */
	rv = get_int(&mesg, (int *) &rsci.ctx.gsc_mapped_uid);
	if (rv) {
		CERROR("fail to get mapped uid\n");
		goto out;
	}

	rscp = rsc_lookup(&rsci);
	if (!rscp)
		goto out;

	/* uid, or NEGATIVE */
	rv = get_int(&mesg, (int *) &rsci.ctx.gsc_uid);
	if (rv == -EINVAL)
		goto out;
	if (rv == -ENOENT) {
		CERROR("NOENT? set rsc entry negative\n");
		set_bit(CACHE_NEGATIVE, &rsci.h.flags);
	} else {
		rawobj_t tmp_buf;
		unsigned long ctx_expiry;

		/* gid */
		if (get_int(&mesg, (int *) &rsci.ctx.gsc_gid))
			goto out;

		/* mech name */
		len = qword_get(&mesg, buf, mlen);
		if (len < 0)
			goto out;
		gm = lgss_name_to_mech(buf);
		status = -EOPNOTSUPP;
		if (!gm)
			goto out;

		status = -EINVAL;
		/* mech-specific data: */
		len = qword_get(&mesg, buf, mlen);
		if (len < 0)
			goto out;

		tmp_buf.len = len;
		tmp_buf.data = (unsigned char *)buf;
		if (lgss_import_sec_context(&tmp_buf, gm,
					    &rsci.ctx.gsc_mechctx))
			goto out;

		/* currently the expiry time passed down from user-space
		 * is invalid, here we retrive it from mech. */
		if (lgss_inquire_context(rsci.ctx.gsc_mechctx, &ctx_expiry)) {
			CERROR("unable to get expire time, drop it\n");
			goto out;
		}
		expiry = (time_t) ctx_expiry;
	}

	rsci.h.expiry_time = expiry;
	rscp = rsc_update(&rsci, rscp);
	status = 0;
out:
	if (gm)
		lgss_mech_put(gm);
	rsc_free(&rsci);
	if (rscp)
		cache_put(&rscp->h, &rsc_cache);
	else
		status = -ENOMEM;

	if (status)
		CERROR("parse rsc error %d\n", status);
	return status;
}

static struct cache_detail rsc_cache = {
	.hash_size      = RSC_HASHMAX,
	.hash_table     = rsc_table,
	.name	   = "auth.sptlrpc.context",
	.cache_put      = rsc_put,
	.cache_parse    = rsc_parse,
	.match	  = rsc_match,
	.init	   = rsc_init,
	.update	 = update_rsc,
	.alloc	  = rsc_alloc,
};

static struct rsc *rsc_lookup(struct rsc *item)
{
	struct cache_head *ch;
	int		hash = rsc_hash(item);

	ch = sunrpc_cache_lookup(&rsc_cache, &item->h, hash);
	if (ch)
		return container_of(ch, struct rsc, h);
	else
		return NULL;
}

static struct rsc *rsc_update(struct rsc *new, struct rsc *old)
{
	struct cache_head *ch;
	int		hash = rsc_hash(new);

	ch = sunrpc_cache_update(&rsc_cache, &new->h, &old->h, hash);
	if (ch)
		return container_of(ch, struct rsc, h);
	else
		return NULL;
}

#define COMPAT_RSC_PUT(item, cd)	cache_put((item), (cd))

/****************************************
 * rsc cache flush		      *
 ****************************************/

typedef int rsc_entry_match(struct rsc *rscp, long data);

static void rsc_flush(rsc_entry_match *match, long data)
{
	struct cache_head **ch;
	struct rsc *rscp;
	int n;
	ENTRY;

	write_lock(&rsc_cache.hash_lock);
	for (n = 0; n < RSC_HASHMAX; n++) {
		for (ch = &rsc_cache.hash_table[n]; *ch;) {
			rscp = container_of(*ch, struct rsc, h);

			if (!match(rscp, data)) {
				ch = &((*ch)->next);
				continue;
			}

			/* it seems simply set NEGATIVE doesn't work */
			*ch = (*ch)->next;
			rscp->h.next = NULL;
			cache_get(&rscp->h);
			set_bit(CACHE_NEGATIVE, &rscp->h.flags);
			COMPAT_RSC_PUT(&rscp->h, &rsc_cache);
			rsc_cache.entries--;
		}
	}
	write_unlock(&rsc_cache.hash_lock);
	EXIT;
}

static int match_uid(struct rsc *rscp, long uid)
{
	if ((int) uid == -1)
		return 1;
	return ((int) rscp->ctx.gsc_uid == (int) uid);
}

static int match_target(struct rsc *rscp, long target)
{
	return (rscp->target == (struct obd_device *) target);
}

static inline void rsc_flush_uid(int uid)
{
	if (uid == -1)
		CWARN("flush all gss contexts...\n");

	rsc_flush(match_uid, (long) uid);
}

static inline void rsc_flush_target(struct obd_device *target)
{
	rsc_flush(match_target, (long) target);
}

void gss_secsvc_flush(struct obd_device *target)
{
	rsc_flush_target(target);
}
EXPORT_SYMBOL(gss_secsvc_flush);

static struct rsc *gss_svc_searchbyctx(rawobj_t *handle)
{
	struct rsc  rsci;
	struct rsc *found;

	memset(&rsci, 0, sizeof(rsci));
	if (rawobj_dup(&rsci.handle, handle))
		return NULL;

	found = rsc_lookup(&rsci);
	rsc_free(&rsci);
	if (!found)
		return NULL;
	if (cache_check(&rsc_cache, &found->h, NULL))
		return NULL;
	return found;
}

int gss_svc_upcall_install_rvs_ctx(struct obd_import *imp,
				   struct gss_sec *gsec,
				   struct gss_cli_ctx *gctx)
{
	struct rsc      rsci, *rscp = NULL;
	unsigned long   ctx_expiry;
	__u32	   major;
	int	     rc;
	ENTRY;

	memset(&rsci, 0, sizeof(rsci));

	if (rawobj_alloc(&rsci.handle, (char *) &gsec->gs_rvs_hdl,
			 sizeof(gsec->gs_rvs_hdl)))
		GOTO(out, rc = -ENOMEM);

	rscp = rsc_lookup(&rsci);
	if (rscp == NULL)
		GOTO(out, rc = -ENOMEM);

	major = lgss_copy_reverse_context(gctx->gc_mechctx,
					  &rsci.ctx.gsc_mechctx);
	if (major != GSS_S_COMPLETE)
		GOTO(out, rc = -ENOMEM);

	if (lgss_inquire_context(rsci.ctx.gsc_mechctx, &ctx_expiry)) {
		CERROR("unable to get expire time, drop it\n");
		GOTO(out, rc = -EINVAL);
	}
	rsci.h.expiry_time = (time_t) ctx_expiry;

	if (strcmp(imp->imp_obd->obd_type->typ_name, LUSTRE_MDC_NAME) == 0)
		rsci.ctx.gsc_usr_mds = 1;
	else if (strcmp(imp->imp_obd->obd_type->typ_name, LUSTRE_OSC_NAME) == 0)
		rsci.ctx.gsc_usr_oss = 1;
	else
		rsci.ctx.gsc_usr_root = 1;

	rscp = rsc_update(&rsci, rscp);
	if (rscp == NULL)
		GOTO(out, rc = -ENOMEM);

	rscp->target = imp->imp_obd;
	rawobj_dup(&gctx->gc_svc_handle, &rscp->handle);

	CWARN("create reverse svc ctx %p to %s: idx "LPX64"\n",
	      &rscp->ctx, obd2cli_tgt(imp->imp_obd), gsec->gs_rvs_hdl);
	rc = 0;
out:
	if (rscp)
		cache_put(&rscp->h, &rsc_cache);
	rsc_free(&rsci);

	if (rc)
		CERROR("create reverse svc ctx: idx "LPX64", rc %d\n",
		       gsec->gs_rvs_hdl, rc);
	RETURN(rc);
}

int gss_svc_upcall_expire_rvs_ctx(rawobj_t *handle)
{
	const cfs_time_t	expire = 20;
	struct rsc	     *rscp;

	rscp = gss_svc_searchbyctx(handle);
	if (rscp) {
		CDEBUG(D_SEC, "reverse svcctx %p (rsc %p) expire soon\n",
		       &rscp->ctx, rscp);

		rscp->h.expiry_time = cfs_time_current_sec() + expire;
		COMPAT_RSC_PUT(&rscp->h, &rsc_cache);
	}
	return 0;
}

int gss_svc_upcall_dup_handle(rawobj_t *handle, struct gss_svc_ctx *ctx)
{
	struct rsc *rscp = container_of(ctx, struct rsc, ctx);

	return rawobj_dup(handle, &rscp->handle);
}

int gss_svc_upcall_update_sequence(rawobj_t *handle, __u32 seq)
{
	struct rsc	     *rscp;

	rscp = gss_svc_searchbyctx(handle);
	if (rscp) {
		CDEBUG(D_SEC, "reverse svcctx %p (rsc %p) update seq to %u\n",
		       &rscp->ctx, rscp, seq + 1);

		rscp->ctx.gsc_rvs_seq = seq + 1;
		COMPAT_RSC_PUT(&rscp->h, &rsc_cache);
	}
	return 0;
}

static struct cache_deferred_req* cache_upcall_defer(struct cache_req *req)
{
	return NULL;
}
static struct cache_req cache_upcall_chandle = { cache_upcall_defer };

int gss_svc_upcall_handle_init(struct ptlrpc_request *req,
			       struct gss_svc_reqctx *grctx,
			       struct gss_wire_ctx *gw,
			       struct obd_device *target,
			       __u32 lustre_svc,
			       rawobj_t *rvs_hdl,
			       rawobj_t *in_token)
{
	struct ptlrpc_reply_state *rs;
	struct rsc		*rsci = NULL;
	struct rsi		*rsip = NULL, rsikey;
	wait_queue_t	     wait;
	int			replen = sizeof(struct ptlrpc_body);
	struct gss_rep_header     *rephdr;
	int			first_check = 1;
	int			rc = SECSVC_DROP;
	ENTRY;

	memset(&rsikey, 0, sizeof(rsikey));
	rsikey.lustre_svc = lustre_svc;
	rsikey.nid = (__u64) req->rq_peer.nid;

	/* duplicate context handle. for INIT it always 0 */
	if (rawobj_dup(&rsikey.in_handle, &gw->gw_handle)) {
		CERROR("fail to dup context handle\n");
		GOTO(out, rc);
	}

	if (rawobj_dup(&rsikey.in_token, in_token)) {
		CERROR("can't duplicate token\n");
		rawobj_free(&rsikey.in_handle);
		GOTO(out, rc);
	}

	rsip = rsi_lookup(&rsikey);
	rsi_free(&rsikey);
	if (!rsip) {
		CERROR("error in rsi_lookup.\n");

		if (!gss_pack_err_notify(req, GSS_S_FAILURE, 0))
			rc = SECSVC_COMPLETE;

		GOTO(out, rc);
	}

	cache_get(&rsip->h); /* take an extra ref */
	init_waitqueue_head(&rsip->waitq);
	init_waitqueue_entry_current(&wait);
	add_wait_queue(&rsip->waitq, &wait);

cache_check:
	/* Note each time cache_check() will drop a reference if return
	 * non-zero. We hold an extra reference on initial rsip, but must
	 * take care of following calls. */
	rc = cache_check(&rsi_cache, &rsip->h, &cache_upcall_chandle);
	switch (rc) {
	case -EAGAIN: {
		int valid;

		if (first_check) {
			first_check = 0;

			read_lock(&rsi_cache.hash_lock);
			valid = test_bit(CACHE_VALID, &rsip->h.flags);
			if (valid == 0)
				set_current_state(TASK_INTERRUPTIBLE);
			read_unlock(&rsi_cache.hash_lock);

			if (valid == 0)
				schedule_timeout(GSS_SVC_UPCALL_TIMEOUT *
						     HZ);

			cache_get(&rsip->h);
			goto cache_check;
		}
		CWARN("waited %ds timeout, drop\n", GSS_SVC_UPCALL_TIMEOUT);
		break;
	}
	case -ENOENT:
		CWARN("cache_check return ENOENT, drop\n");
		break;
	case 0:
		/* if not the first check, we have to release the extra
		 * reference we just added on it. */
		if (!first_check)
			cache_put(&rsip->h, &rsi_cache);
		CDEBUG(D_SEC, "cache_check is good\n");
		break;
	}

	remove_wait_queue(&rsip->waitq, &wait);
	cache_put(&rsip->h, &rsi_cache);

	if (rc)
		GOTO(out, rc = SECSVC_DROP);

	rc = SECSVC_DROP;
	rsci = gss_svc_searchbyctx(&rsip->out_handle);
	if (!rsci) {
		CERROR("authentication failed\n");

		if (!gss_pack_err_notify(req, GSS_S_FAILURE, 0))
			rc = SECSVC_COMPLETE;

		GOTO(out, rc);
	} else {
		cache_get(&rsci->h);
		grctx->src_ctx = &rsci->ctx;
	}

	if (rawobj_dup(&rsci->ctx.gsc_rvs_hdl, rvs_hdl)) {
		CERROR("failed duplicate reverse handle\n");
		GOTO(out, rc);
	}

	rsci->target = target;

	CDEBUG(D_SEC, "server create rsc %p(%u->%s)\n",
	       rsci, rsci->ctx.gsc_uid, libcfs_nid2str(req->rq_peer.nid));

	if (rsip->out_handle.len > PTLRPC_GSS_MAX_HANDLE_SIZE) {
		CERROR("handle size %u too large\n", rsip->out_handle.len);
		GOTO(out, rc = SECSVC_DROP);
	}

	grctx->src_init = 1;
	grctx->src_reserve_len = cfs_size_round4(rsip->out_token.len);

	rc = lustre_pack_reply_v2(req, 1, &replen, NULL, 0);
	if (rc) {
		CERROR("failed to pack reply: %d\n", rc);
		GOTO(out, rc = SECSVC_DROP);
	}

	rs = req->rq_reply_state;
	LASSERT(rs->rs_repbuf->lm_bufcount == 3);
	LASSERT(rs->rs_repbuf->lm_buflens[0] >=
		sizeof(*rephdr) + rsip->out_handle.len);
	LASSERT(rs->rs_repbuf->lm_buflens[2] >= rsip->out_token.len);

	rephdr = lustre_msg_buf(rs->rs_repbuf, 0, 0);
	rephdr->gh_version = PTLRPC_GSS_VERSION;
	rephdr->gh_flags = 0;
	rephdr->gh_proc = PTLRPC_GSS_PROC_ERR;
	rephdr->gh_major = rsip->major_status;
	rephdr->gh_minor = rsip->minor_status;
	rephdr->gh_seqwin = GSS_SEQ_WIN;
	rephdr->gh_handle.len = rsip->out_handle.len;
	memcpy(rephdr->gh_handle.data, rsip->out_handle.data,
	       rsip->out_handle.len);

	memcpy(lustre_msg_buf(rs->rs_repbuf, 2, 0), rsip->out_token.data,
	       rsip->out_token.len);

	rs->rs_repdata_len = lustre_shrink_msg(rs->rs_repbuf, 2,
					       rsip->out_token.len, 0);

	rc = SECSVC_OK;

out:
	/* it looks like here we should put rsip also, but this mess up
	 * with NFS cache mgmt code... FIXME */
#if 0
	if (rsip)
		rsi_put(&rsip->h, &rsi_cache);
#endif

	if (rsci) {
		/* if anything went wrong, we don't keep the context too */
		if (rc != SECSVC_OK)
			set_bit(CACHE_NEGATIVE, &rsci->h.flags);
		else
			CDEBUG(D_SEC, "create rsc with idx "LPX64"\n",
			       gss_handle_to_u64(&rsci->handle));

		COMPAT_RSC_PUT(&rsci->h, &rsc_cache);
	}
	RETURN(rc);
}

struct gss_svc_ctx *gss_svc_upcall_get_ctx(struct ptlrpc_request *req,
					   struct gss_wire_ctx *gw)
{
	struct rsc *rsc;

	rsc = gss_svc_searchbyctx(&gw->gw_handle);
	if (!rsc) {
		CWARN("Invalid gss ctx idx "LPX64" from %s\n",
		      gss_handle_to_u64(&gw->gw_handle),
		      libcfs_nid2str(req->rq_peer.nid));
		return NULL;
	}

	return &rsc->ctx;
}

void gss_svc_upcall_put_ctx(struct gss_svc_ctx *ctx)
{
	struct rsc *rsc = container_of(ctx, struct rsc, ctx);

	COMPAT_RSC_PUT(&rsc->h, &rsc_cache);
}

void gss_svc_upcall_destroy_ctx(struct gss_svc_ctx *ctx)
{
	struct rsc *rsc = container_of(ctx, struct rsc, ctx);

	/* can't be found */
	set_bit(CACHE_NEGATIVE, &rsc->h.flags);
	/* to be removed at next scan */
	rsc->h.expiry_time = 1;
}

int __init gss_init_svc_upcall(void)
{
	int     i;

	spin_lock_init(&__ctx_index_lock);
	/*
	 * this helps reducing context index confliction. after server reboot,
	 * conflicting request from clients might be filtered out by initial
	 * sequence number checking, thus no chance to sent error notification
	 * back to clients.
	 */
	cfs_get_random_bytes(&__ctx_index, sizeof(__ctx_index));


	cache_register(&rsi_cache);
	cache_register(&rsc_cache);

	/* FIXME this looks stupid. we intend to give lsvcgssd a chance to open
	 * the init upcall channel, otherwise there's big chance that the first
	 * upcall issued before the channel be opened thus nfsv4 cache code will
	 * drop the request direclty, thus lead to unnecessary recovery time.
	 * here we wait at miximum 1.5 seconds. */
	for (i = 0; i < 6; i++) {
		if (atomic_read(&rsi_cache.readers) > 0)
			break;
		set_current_state(TASK_UNINTERRUPTIBLE);
		LASSERT(HZ >= 4);
		schedule_timeout(HZ / 4);
	}

	if (atomic_read(&rsi_cache.readers) == 0)
		CWARN("Init channel is not opened by lsvcgssd, following "
		      "request might be dropped until lsvcgssd is active\n");

	return 0;
}

void __exit gss_exit_svc_upcall(void)
{
	cache_purge(&rsi_cache);
	cache_unregister(&rsi_cache);

	cache_purge(&rsc_cache);
	cache_unregister(&rsc_cache);
}
