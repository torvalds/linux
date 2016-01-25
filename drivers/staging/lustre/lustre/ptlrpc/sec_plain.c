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
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/ptlrpc/sec_plain.c
 *
 * Author: Eric Mei <ericm@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_SEC

#include "../include/obd_support.h"
#include "../include/obd_cksum.h"
#include "../include/obd_class.h"
#include "../include/lustre_net.h"
#include "../include/lustre_sec.h"
#include "ptlrpc_internal.h"

struct plain_sec {
	struct ptlrpc_sec       pls_base;
	rwlock_t	    pls_lock;
	struct ptlrpc_cli_ctx  *pls_ctx;
};

static inline struct plain_sec *sec2plsec(struct ptlrpc_sec *sec)
{
	return container_of(sec, struct plain_sec, pls_base);
}

static struct ptlrpc_sec_policy plain_policy;
static struct ptlrpc_ctx_ops    plain_ctx_ops;
static struct ptlrpc_svc_ctx    plain_svc_ctx;

static unsigned int plain_at_offset;

/*
 * for simplicity, plain policy rpc use fixed layout.
 */
#define PLAIN_PACK_SEGMENTS	     (4)

#define PLAIN_PACK_HDR_OFF	      (0)
#define PLAIN_PACK_MSG_OFF	      (1)
#define PLAIN_PACK_USER_OFF	     (2)
#define PLAIN_PACK_BULK_OFF	     (3)

#define PLAIN_FL_USER		   (0x01)
#define PLAIN_FL_BULK		   (0x02)

struct plain_header {
	__u8	    ph_ver;	    /* 0 */
	__u8	    ph_flags;
	__u8	    ph_sp;	     /* source */
	__u8	    ph_bulk_hash_alg;  /* complete flavor desc */
	__u8	    ph_pad[4];
};

struct plain_bulk_token {
	__u8	    pbt_hash[8];
};

#define PLAIN_BSD_SIZE \
	(sizeof(struct ptlrpc_bulk_sec_desc) + sizeof(struct plain_bulk_token))

/****************************************
 * bulk checksum helpers		*
 ****************************************/

static int plain_unpack_bsd(struct lustre_msg *msg, int swabbed)
{
	struct ptlrpc_bulk_sec_desc *bsd;

	if (bulk_sec_desc_unpack(msg, PLAIN_PACK_BULK_OFF, swabbed))
		return -EPROTO;

	bsd = lustre_msg_buf(msg, PLAIN_PACK_BULK_OFF, PLAIN_BSD_SIZE);
	if (bsd == NULL) {
		CERROR("bulk sec desc has short size %d\n",
		       lustre_msg_buflen(msg, PLAIN_PACK_BULK_OFF));
		return -EPROTO;
	}

	if (bsd->bsd_svc != SPTLRPC_BULK_SVC_NULL &&
	    bsd->bsd_svc != SPTLRPC_BULK_SVC_INTG) {
		CERROR("invalid bulk svc %u\n", bsd->bsd_svc);
		return -EPROTO;
	}

	return 0;
}

static int plain_generate_bulk_csum(struct ptlrpc_bulk_desc *desc,
				    __u8 hash_alg,
				    struct plain_bulk_token *token)
{
	if (hash_alg == BULK_HASH_ALG_NULL)
		return 0;

	memset(token->pbt_hash, 0, sizeof(token->pbt_hash));
	return sptlrpc_get_bulk_checksum(desc, hash_alg, token->pbt_hash,
					 sizeof(token->pbt_hash));
}

static int plain_verify_bulk_csum(struct ptlrpc_bulk_desc *desc,
				  __u8 hash_alg,
				  struct plain_bulk_token *tokenr)
{
	struct plain_bulk_token tokenv;
	int rc;

	if (hash_alg == BULK_HASH_ALG_NULL)
		return 0;

	memset(&tokenv.pbt_hash, 0, sizeof(tokenv.pbt_hash));
	rc = sptlrpc_get_bulk_checksum(desc, hash_alg, tokenv.pbt_hash,
				       sizeof(tokenv.pbt_hash));
	if (rc)
		return rc;

	if (memcmp(tokenr->pbt_hash, tokenv.pbt_hash, sizeof(tokenr->pbt_hash)))
		return -EACCES;
	return 0;
}

static void corrupt_bulk_data(struct ptlrpc_bulk_desc *desc)
{
	char *ptr;
	unsigned int off, i;

	for (i = 0; i < desc->bd_iov_count; i++) {
		if (desc->bd_iov[i].kiov_len == 0)
			continue;

		ptr = kmap(desc->bd_iov[i].kiov_page);
		off = desc->bd_iov[i].kiov_offset & ~CFS_PAGE_MASK;
		ptr[off] ^= 0x1;
		kunmap(desc->bd_iov[i].kiov_page);
		return;
	}
}

/****************************************
 * cli_ctx apis			 *
 ****************************************/

static
int plain_ctx_refresh(struct ptlrpc_cli_ctx *ctx)
{
	/* should never reach here */
	LBUG();
	return 0;
}

static
int plain_ctx_validate(struct ptlrpc_cli_ctx *ctx)
{
	return 0;
}

static
int plain_ctx_sign(struct ptlrpc_cli_ctx *ctx, struct ptlrpc_request *req)
{
	struct lustre_msg *msg = req->rq_reqbuf;
	struct plain_header *phdr;

	msg->lm_secflvr = req->rq_flvr.sf_rpc;

	phdr = lustre_msg_buf(msg, PLAIN_PACK_HDR_OFF, 0);
	phdr->ph_ver = 0;
	phdr->ph_flags = 0;
	phdr->ph_sp = ctx->cc_sec->ps_part;
	phdr->ph_bulk_hash_alg = req->rq_flvr.u_bulk.hash.hash_alg;

	if (req->rq_pack_udesc)
		phdr->ph_flags |= PLAIN_FL_USER;
	if (req->rq_pack_bulk)
		phdr->ph_flags |= PLAIN_FL_BULK;

	req->rq_reqdata_len = lustre_msg_size_v2(msg->lm_bufcount,
						 msg->lm_buflens);
	return 0;
}

static
int plain_ctx_verify(struct ptlrpc_cli_ctx *ctx, struct ptlrpc_request *req)
{
	struct lustre_msg *msg = req->rq_repdata;
	struct plain_header *phdr;
	__u32 cksum;
	int swabbed;

	if (msg->lm_bufcount != PLAIN_PACK_SEGMENTS) {
		CERROR("unexpected reply buf count %u\n", msg->lm_bufcount);
		return -EPROTO;
	}

	swabbed = ptlrpc_rep_need_swab(req);

	phdr = lustre_msg_buf(msg, PLAIN_PACK_HDR_OFF, sizeof(*phdr));
	if (phdr == NULL) {
		CERROR("missing plain header\n");
		return -EPROTO;
	}

	if (phdr->ph_ver != 0) {
		CERROR("Invalid header version\n");
		return -EPROTO;
	}

	/* expect no user desc in reply */
	if (phdr->ph_flags & PLAIN_FL_USER) {
		CERROR("Unexpected udesc flag in reply\n");
		return -EPROTO;
	}

	if (phdr->ph_bulk_hash_alg != req->rq_flvr.u_bulk.hash.hash_alg) {
		CERROR("reply bulk flavor %u != %u\n", phdr->ph_bulk_hash_alg,
		       req->rq_flvr.u_bulk.hash.hash_alg);
		return -EPROTO;
	}

	if (unlikely(req->rq_early)) {
		unsigned int hsize = 4;

		cfs_crypto_hash_digest(CFS_HASH_ALG_CRC32,
				lustre_msg_buf(msg, PLAIN_PACK_MSG_OFF, 0),
				lustre_msg_buflen(msg, PLAIN_PACK_MSG_OFF),
				NULL, 0, (unsigned char *)&cksum, &hsize);
		if (cksum != msg->lm_cksum) {
			CDEBUG(D_SEC,
			       "early reply checksum mismatch: %08x != %08x\n",
			       cpu_to_le32(cksum), msg->lm_cksum);
			return -EINVAL;
		}
	} else {
		/* whether we sent with bulk or not, we expect the same
		 * in reply, except for early reply */
		if (!req->rq_early &&
		    !equi(req->rq_pack_bulk == 1,
			  phdr->ph_flags & PLAIN_FL_BULK)) {
			CERROR("%s bulk checksum in reply\n",
			       req->rq_pack_bulk ? "Missing" : "Unexpected");
			return -EPROTO;
		}

		if (phdr->ph_flags & PLAIN_FL_BULK) {
			if (plain_unpack_bsd(msg, swabbed))
				return -EPROTO;
		}
	}

	req->rq_repmsg = lustre_msg_buf(msg, PLAIN_PACK_MSG_OFF, 0);
	req->rq_replen = lustre_msg_buflen(msg, PLAIN_PACK_MSG_OFF);
	return 0;
}

static
int plain_cli_wrap_bulk(struct ptlrpc_cli_ctx *ctx,
			struct ptlrpc_request *req,
			struct ptlrpc_bulk_desc *desc)
{
	struct ptlrpc_bulk_sec_desc *bsd;
	struct plain_bulk_token *token;
	int rc;

	LASSERT(req->rq_pack_bulk);
	LASSERT(req->rq_reqbuf->lm_bufcount == PLAIN_PACK_SEGMENTS);

	bsd = lustre_msg_buf(req->rq_reqbuf, PLAIN_PACK_BULK_OFF, 0);
	token = (struct plain_bulk_token *) bsd->bsd_data;

	bsd->bsd_version = 0;
	bsd->bsd_flags = 0;
	bsd->bsd_type = SPTLRPC_BULK_DEFAULT;
	bsd->bsd_svc = SPTLRPC_FLVR_BULK_SVC(req->rq_flvr.sf_rpc);

	if (bsd->bsd_svc == SPTLRPC_BULK_SVC_NULL)
		return 0;

	if (req->rq_bulk_read)
		return 0;

	rc = plain_generate_bulk_csum(desc, req->rq_flvr.u_bulk.hash.hash_alg,
				      token);
	if (rc) {
		CERROR("bulk write: failed to compute checksum: %d\n", rc);
	} else {
		/*
		 * for sending we only compute the wrong checksum instead
		 * of corrupting the data so it is still correct on a redo
		 */
		if (OBD_FAIL_CHECK(OBD_FAIL_OSC_CHECKSUM_SEND) &&
		    req->rq_flvr.u_bulk.hash.hash_alg != BULK_HASH_ALG_NULL)
			token->pbt_hash[0] ^= 0x1;
	}

	return rc;
}

static
int plain_cli_unwrap_bulk(struct ptlrpc_cli_ctx *ctx,
			  struct ptlrpc_request *req,
			  struct ptlrpc_bulk_desc *desc)
{
	struct ptlrpc_bulk_sec_desc *bsdv;
	struct plain_bulk_token *tokenv;
	int rc;
	int i, nob;

	LASSERT(req->rq_pack_bulk);
	LASSERT(req->rq_reqbuf->lm_bufcount == PLAIN_PACK_SEGMENTS);
	LASSERT(req->rq_repdata->lm_bufcount == PLAIN_PACK_SEGMENTS);

	bsdv = lustre_msg_buf(req->rq_repdata, PLAIN_PACK_BULK_OFF, 0);
	tokenv = (struct plain_bulk_token *) bsdv->bsd_data;

	if (req->rq_bulk_write) {
		if (bsdv->bsd_flags & BSD_FL_ERR)
			return -EIO;
		return 0;
	}

	/* fix the actual data size */
	for (i = 0, nob = 0; i < desc->bd_iov_count; i++) {
		if (desc->bd_iov[i].kiov_len + nob > desc->bd_nob_transferred) {
			desc->bd_iov[i].kiov_len =
				desc->bd_nob_transferred - nob;
		}
		nob += desc->bd_iov[i].kiov_len;
	}

	rc = plain_verify_bulk_csum(desc, req->rq_flvr.u_bulk.hash.hash_alg,
				    tokenv);
	if (rc)
		CERROR("bulk read: client verify failed: %d\n", rc);

	return rc;
}

/****************************************
 * sec apis			     *
 ****************************************/

static
struct ptlrpc_cli_ctx *plain_sec_install_ctx(struct plain_sec *plsec)
{
	struct ptlrpc_cli_ctx *ctx, *ctx_new;

	ctx_new = kzalloc(sizeof(*ctx_new), GFP_NOFS);

	write_lock(&plsec->pls_lock);

	ctx = plsec->pls_ctx;
	if (ctx) {
		atomic_inc(&ctx->cc_refcount);

		kfree(ctx_new);
	} else if (ctx_new) {
		ctx = ctx_new;

		atomic_set(&ctx->cc_refcount, 1); /* for cache */
		ctx->cc_sec = &plsec->pls_base;
		ctx->cc_ops = &plain_ctx_ops;
		ctx->cc_expire = 0;
		ctx->cc_flags = PTLRPC_CTX_CACHED | PTLRPC_CTX_UPTODATE;
		ctx->cc_vcred.vc_uid = 0;
		spin_lock_init(&ctx->cc_lock);
		INIT_LIST_HEAD(&ctx->cc_req_list);
		INIT_LIST_HEAD(&ctx->cc_gc_chain);

		plsec->pls_ctx = ctx;
		atomic_inc(&plsec->pls_base.ps_nctx);
		atomic_inc(&plsec->pls_base.ps_refcount);

		atomic_inc(&ctx->cc_refcount); /* for caller */
	}

	write_unlock(&plsec->pls_lock);

	return ctx;
}

static
void plain_destroy_sec(struct ptlrpc_sec *sec)
{
	struct plain_sec *plsec = sec2plsec(sec);

	LASSERT(sec->ps_policy == &plain_policy);
	LASSERT(sec->ps_import);
	LASSERT(atomic_read(&sec->ps_refcount) == 0);
	LASSERT(atomic_read(&sec->ps_nctx) == 0);
	LASSERT(plsec->pls_ctx == NULL);

	class_import_put(sec->ps_import);

	kfree(plsec);
}

static
void plain_kill_sec(struct ptlrpc_sec *sec)
{
	sec->ps_dying = 1;
}

static
struct ptlrpc_sec *plain_create_sec(struct obd_import *imp,
				    struct ptlrpc_svc_ctx *svc_ctx,
				    struct sptlrpc_flavor *sf)
{
	struct plain_sec *plsec;
	struct ptlrpc_sec *sec;
	struct ptlrpc_cli_ctx *ctx;

	LASSERT(SPTLRPC_FLVR_POLICY(sf->sf_rpc) == SPTLRPC_POLICY_PLAIN);

	plsec = kzalloc(sizeof(*plsec), GFP_NOFS);
	if (!plsec)
		return NULL;

	/*
	 * initialize plain_sec
	 */
	rwlock_init(&plsec->pls_lock);
	plsec->pls_ctx = NULL;

	sec = &plsec->pls_base;
	sec->ps_policy = &plain_policy;
	atomic_set(&sec->ps_refcount, 0);
	atomic_set(&sec->ps_nctx, 0);
	sec->ps_id = sptlrpc_get_next_secid();
	sec->ps_import = class_import_get(imp);
	sec->ps_flvr = *sf;
	spin_lock_init(&sec->ps_lock);
	INIT_LIST_HEAD(&sec->ps_gc_list);
	sec->ps_gc_interval = 0;
	sec->ps_gc_next = 0;

	/* install ctx immediately if this is a reverse sec */
	if (svc_ctx) {
		ctx = plain_sec_install_ctx(plsec);
		if (ctx == NULL) {
			plain_destroy_sec(sec);
			return NULL;
		}
		sptlrpc_cli_ctx_put(ctx, 1);
	}

	return sec;
}

static
struct ptlrpc_cli_ctx *plain_lookup_ctx(struct ptlrpc_sec *sec,
					struct vfs_cred *vcred,
					int create, int remove_dead)
{
	struct plain_sec *plsec = sec2plsec(sec);
	struct ptlrpc_cli_ctx *ctx;

	read_lock(&plsec->pls_lock);
	ctx = plsec->pls_ctx;
	if (ctx)
		atomic_inc(&ctx->cc_refcount);
	read_unlock(&plsec->pls_lock);

	if (unlikely(ctx == NULL))
		ctx = plain_sec_install_ctx(plsec);

	return ctx;
}

static
void plain_release_ctx(struct ptlrpc_sec *sec,
		       struct ptlrpc_cli_ctx *ctx, int sync)
{
	LASSERT(atomic_read(&sec->ps_refcount) > 0);
	LASSERT(atomic_read(&sec->ps_nctx) > 0);
	LASSERT(atomic_read(&ctx->cc_refcount) == 0);
	LASSERT(ctx->cc_sec == sec);

	kfree(ctx);

	atomic_dec(&sec->ps_nctx);
	sptlrpc_sec_put(sec);
}

static
int plain_flush_ctx_cache(struct ptlrpc_sec *sec,
			  uid_t uid, int grace, int force)
{
	struct plain_sec *plsec = sec2plsec(sec);
	struct ptlrpc_cli_ctx *ctx;

	/* do nothing unless caller want to flush for 'all' */
	if (uid != -1)
		return 0;

	write_lock(&plsec->pls_lock);
	ctx = plsec->pls_ctx;
	plsec->pls_ctx = NULL;
	write_unlock(&plsec->pls_lock);

	if (ctx)
		sptlrpc_cli_ctx_put(ctx, 1);
	return 0;
}

static
int plain_alloc_reqbuf(struct ptlrpc_sec *sec,
		       struct ptlrpc_request *req,
		       int msgsize)
{
	__u32 buflens[PLAIN_PACK_SEGMENTS] = { 0, };
	int alloc_len;

	buflens[PLAIN_PACK_HDR_OFF] = sizeof(struct plain_header);
	buflens[PLAIN_PACK_MSG_OFF] = msgsize;

	if (req->rq_pack_udesc)
		buflens[PLAIN_PACK_USER_OFF] = sptlrpc_current_user_desc_size();

	if (req->rq_pack_bulk) {
		LASSERT(req->rq_bulk_read || req->rq_bulk_write);
		buflens[PLAIN_PACK_BULK_OFF] = PLAIN_BSD_SIZE;
	}

	alloc_len = lustre_msg_size_v2(PLAIN_PACK_SEGMENTS, buflens);

	if (!req->rq_reqbuf) {
		LASSERT(!req->rq_pool);

		alloc_len = size_roundup_power2(alloc_len);
		req->rq_reqbuf = libcfs_kvzalloc(alloc_len, GFP_NOFS);
		if (!req->rq_reqbuf)
			return -ENOMEM;

		req->rq_reqbuf_len = alloc_len;
	} else {
		LASSERT(req->rq_pool);
		LASSERT(req->rq_reqbuf_len >= alloc_len);
		memset(req->rq_reqbuf, 0, alloc_len);
	}

	lustre_init_msg_v2(req->rq_reqbuf, PLAIN_PACK_SEGMENTS, buflens, NULL);
	req->rq_reqmsg = lustre_msg_buf(req->rq_reqbuf, PLAIN_PACK_MSG_OFF, 0);

	if (req->rq_pack_udesc)
		sptlrpc_pack_user_desc(req->rq_reqbuf, PLAIN_PACK_USER_OFF);

	return 0;
}

static
void plain_free_reqbuf(struct ptlrpc_sec *sec,
		       struct ptlrpc_request *req)
{
	if (!req->rq_pool) {
		kvfree(req->rq_reqbuf);
		req->rq_reqbuf = NULL;
		req->rq_reqbuf_len = 0;
	}
}

static
int plain_alloc_repbuf(struct ptlrpc_sec *sec,
		       struct ptlrpc_request *req,
		       int msgsize)
{
	__u32 buflens[PLAIN_PACK_SEGMENTS] = { 0, };
	int alloc_len;

	buflens[PLAIN_PACK_HDR_OFF] = sizeof(struct plain_header);
	buflens[PLAIN_PACK_MSG_OFF] = msgsize;

	if (req->rq_pack_bulk) {
		LASSERT(req->rq_bulk_read || req->rq_bulk_write);
		buflens[PLAIN_PACK_BULK_OFF] = PLAIN_BSD_SIZE;
	}

	alloc_len = lustre_msg_size_v2(PLAIN_PACK_SEGMENTS, buflens);

	/* add space for early reply */
	alloc_len += plain_at_offset;

	alloc_len = size_roundup_power2(alloc_len);

	req->rq_repbuf = libcfs_kvzalloc(alloc_len, GFP_NOFS);
	if (!req->rq_repbuf)
		return -ENOMEM;

	req->rq_repbuf_len = alloc_len;
	return 0;
}

static
void plain_free_repbuf(struct ptlrpc_sec *sec,
		       struct ptlrpc_request *req)
{
	kvfree(req->rq_repbuf);
	req->rq_repbuf = NULL;
	req->rq_repbuf_len = 0;
}

static
int plain_enlarge_reqbuf(struct ptlrpc_sec *sec,
			 struct ptlrpc_request *req,
			 int segment, int newsize)
{
	struct lustre_msg *newbuf;
	int oldsize;
	int newmsg_size, newbuf_size;

	LASSERT(req->rq_reqbuf);
	LASSERT(req->rq_reqbuf_len >= req->rq_reqlen);
	LASSERT(lustre_msg_buf(req->rq_reqbuf, PLAIN_PACK_MSG_OFF, 0) ==
		req->rq_reqmsg);

	/* compute new embedded msg size.  */
	oldsize = req->rq_reqmsg->lm_buflens[segment];
	req->rq_reqmsg->lm_buflens[segment] = newsize;
	newmsg_size = lustre_msg_size_v2(req->rq_reqmsg->lm_bufcount,
					 req->rq_reqmsg->lm_buflens);
	req->rq_reqmsg->lm_buflens[segment] = oldsize;

	/* compute new wrapper msg size.  */
	oldsize = req->rq_reqbuf->lm_buflens[PLAIN_PACK_MSG_OFF];
	req->rq_reqbuf->lm_buflens[PLAIN_PACK_MSG_OFF] = newmsg_size;
	newbuf_size = lustre_msg_size_v2(req->rq_reqbuf->lm_bufcount,
					 req->rq_reqbuf->lm_buflens);
	req->rq_reqbuf->lm_buflens[PLAIN_PACK_MSG_OFF] = oldsize;

	/* request from pool should always have enough buffer */
	LASSERT(!req->rq_pool || req->rq_reqbuf_len >= newbuf_size);

	if (req->rq_reqbuf_len < newbuf_size) {
		newbuf_size = size_roundup_power2(newbuf_size);

		newbuf = libcfs_kvzalloc(newbuf_size, GFP_NOFS);
		if (newbuf == NULL)
			return -ENOMEM;

		/* Must lock this, so that otherwise unprotected change of
		 * rq_reqmsg is not racing with parallel processing of
		 * imp_replay_list traversing threads. See LU-3333
		 * This is a bandaid at best, we really need to deal with this
		 * in request enlarging code before unpacking that's already
		 * there */
		if (req->rq_import)
			spin_lock(&req->rq_import->imp_lock);

		memcpy(newbuf, req->rq_reqbuf, req->rq_reqbuf_len);

		kvfree(req->rq_reqbuf);
		req->rq_reqbuf = newbuf;
		req->rq_reqbuf_len = newbuf_size;
		req->rq_reqmsg = lustre_msg_buf(req->rq_reqbuf,
						PLAIN_PACK_MSG_OFF, 0);

		if (req->rq_import)
			spin_unlock(&req->rq_import->imp_lock);
	}

	_sptlrpc_enlarge_msg_inplace(req->rq_reqbuf, PLAIN_PACK_MSG_OFF,
				     newmsg_size);
	_sptlrpc_enlarge_msg_inplace(req->rq_reqmsg, segment, newsize);

	req->rq_reqlen = newmsg_size;
	return 0;
}

/****************************************
 * service apis			 *
 ****************************************/

static struct ptlrpc_svc_ctx plain_svc_ctx = {
	.sc_refcount    = ATOMIC_INIT(1),
	.sc_policy      = &plain_policy,
};

static
int plain_accept(struct ptlrpc_request *req)
{
	struct lustre_msg *msg = req->rq_reqbuf;
	struct plain_header *phdr;
	int swabbed;

	LASSERT(SPTLRPC_FLVR_POLICY(req->rq_flvr.sf_rpc) ==
		SPTLRPC_POLICY_PLAIN);

	if (SPTLRPC_FLVR_BASE(req->rq_flvr.sf_rpc) !=
	    SPTLRPC_FLVR_BASE(SPTLRPC_FLVR_PLAIN) ||
	    SPTLRPC_FLVR_BULK_TYPE(req->rq_flvr.sf_rpc) !=
	    SPTLRPC_FLVR_BULK_TYPE(SPTLRPC_FLVR_PLAIN)) {
		CERROR("Invalid rpc flavor %x\n", req->rq_flvr.sf_rpc);
		return SECSVC_DROP;
	}

	if (msg->lm_bufcount < PLAIN_PACK_SEGMENTS) {
		CERROR("unexpected request buf count %u\n", msg->lm_bufcount);
		return SECSVC_DROP;
	}

	swabbed = ptlrpc_req_need_swab(req);

	phdr = lustre_msg_buf(msg, PLAIN_PACK_HDR_OFF, sizeof(*phdr));
	if (phdr == NULL) {
		CERROR("missing plain header\n");
		return -EPROTO;
	}

	if (phdr->ph_ver != 0) {
		CERROR("Invalid header version\n");
		return -EPROTO;
	}

	if (phdr->ph_bulk_hash_alg >= BULK_HASH_ALG_MAX) {
		CERROR("invalid hash algorithm: %u\n", phdr->ph_bulk_hash_alg);
		return -EPROTO;
	}

	req->rq_sp_from = phdr->ph_sp;
	req->rq_flvr.u_bulk.hash.hash_alg = phdr->ph_bulk_hash_alg;

	if (phdr->ph_flags & PLAIN_FL_USER) {
		if (sptlrpc_unpack_user_desc(msg, PLAIN_PACK_USER_OFF,
					     swabbed)) {
			CERROR("Mal-formed user descriptor\n");
			return SECSVC_DROP;
		}

		req->rq_pack_udesc = 1;
		req->rq_user_desc = lustre_msg_buf(msg, PLAIN_PACK_USER_OFF, 0);
	}

	if (phdr->ph_flags & PLAIN_FL_BULK) {
		if (plain_unpack_bsd(msg, swabbed))
			return SECSVC_DROP;

		req->rq_pack_bulk = 1;
	}

	req->rq_reqmsg = lustre_msg_buf(msg, PLAIN_PACK_MSG_OFF, 0);
	req->rq_reqlen = msg->lm_buflens[PLAIN_PACK_MSG_OFF];

	req->rq_svc_ctx = &plain_svc_ctx;
	atomic_inc(&req->rq_svc_ctx->sc_refcount);

	return SECSVC_OK;
}

static
int plain_alloc_rs(struct ptlrpc_request *req, int msgsize)
{
	struct ptlrpc_reply_state *rs;
	__u32 buflens[PLAIN_PACK_SEGMENTS] = { 0, };
	int rs_size = sizeof(*rs);

	LASSERT(msgsize % 8 == 0);

	buflens[PLAIN_PACK_HDR_OFF] = sizeof(struct plain_header);
	buflens[PLAIN_PACK_MSG_OFF] = msgsize;

	if (req->rq_pack_bulk && (req->rq_bulk_read || req->rq_bulk_write))
		buflens[PLAIN_PACK_BULK_OFF] = PLAIN_BSD_SIZE;

	rs_size += lustre_msg_size_v2(PLAIN_PACK_SEGMENTS, buflens);

	rs = req->rq_reply_state;

	if (rs) {
		/* pre-allocated */
		LASSERT(rs->rs_size >= rs_size);
	} else {
		rs = libcfs_kvzalloc(rs_size, GFP_NOFS);
		if (rs == NULL)
			return -ENOMEM;

		rs->rs_size = rs_size;
	}

	rs->rs_svc_ctx = req->rq_svc_ctx;
	atomic_inc(&req->rq_svc_ctx->sc_refcount);
	rs->rs_repbuf = (struct lustre_msg *) (rs + 1);
	rs->rs_repbuf_len = rs_size - sizeof(*rs);

	lustre_init_msg_v2(rs->rs_repbuf, PLAIN_PACK_SEGMENTS, buflens, NULL);
	rs->rs_msg = lustre_msg_buf_v2(rs->rs_repbuf, PLAIN_PACK_MSG_OFF, 0);

	req->rq_reply_state = rs;
	return 0;
}

static
void plain_free_rs(struct ptlrpc_reply_state *rs)
{
	LASSERT(atomic_read(&rs->rs_svc_ctx->sc_refcount) > 1);
	atomic_dec(&rs->rs_svc_ctx->sc_refcount);

	if (!rs->rs_prealloc)
		kvfree(rs);
}

static
int plain_authorize(struct ptlrpc_request *req)
{
	struct ptlrpc_reply_state *rs = req->rq_reply_state;
	struct lustre_msg_v2 *msg = rs->rs_repbuf;
	struct plain_header *phdr;
	int len;

	LASSERT(rs);
	LASSERT(msg);

	if (req->rq_replen != msg->lm_buflens[PLAIN_PACK_MSG_OFF])
		len = lustre_shrink_msg(msg, PLAIN_PACK_MSG_OFF,
					req->rq_replen, 1);
	else
		len = lustre_msg_size_v2(msg->lm_bufcount, msg->lm_buflens);

	msg->lm_secflvr = req->rq_flvr.sf_rpc;

	phdr = lustre_msg_buf(msg, PLAIN_PACK_HDR_OFF, 0);
	phdr->ph_ver = 0;
	phdr->ph_flags = 0;
	phdr->ph_bulk_hash_alg = req->rq_flvr.u_bulk.hash.hash_alg;

	if (req->rq_pack_bulk)
		phdr->ph_flags |= PLAIN_FL_BULK;

	rs->rs_repdata_len = len;

	if (likely(req->rq_packed_final)) {
		if (lustre_msghdr_get_flags(req->rq_reqmsg) & MSGHDR_AT_SUPPORT)
			req->rq_reply_off = plain_at_offset;
		else
			req->rq_reply_off = 0;
	} else {
		unsigned int hsize = 4;

		cfs_crypto_hash_digest(CFS_HASH_ALG_CRC32,
			lustre_msg_buf(msg, PLAIN_PACK_MSG_OFF, 0),
			lustre_msg_buflen(msg, PLAIN_PACK_MSG_OFF),
			NULL, 0, (unsigned char *)&msg->lm_cksum, &hsize);
		req->rq_reply_off = 0;
	}

	return 0;
}

static
int plain_svc_unwrap_bulk(struct ptlrpc_request *req,
			  struct ptlrpc_bulk_desc *desc)
{
	struct ptlrpc_reply_state *rs = req->rq_reply_state;
	struct ptlrpc_bulk_sec_desc *bsdr, *bsdv;
	struct plain_bulk_token *tokenr;
	int rc;

	LASSERT(req->rq_bulk_write);
	LASSERT(req->rq_pack_bulk);

	bsdr = lustre_msg_buf(req->rq_reqbuf, PLAIN_PACK_BULK_OFF, 0);
	tokenr = (struct plain_bulk_token *) bsdr->bsd_data;
	bsdv = lustre_msg_buf(rs->rs_repbuf, PLAIN_PACK_BULK_OFF, 0);

	bsdv->bsd_version = 0;
	bsdv->bsd_type = SPTLRPC_BULK_DEFAULT;
	bsdv->bsd_svc = bsdr->bsd_svc;
	bsdv->bsd_flags = 0;

	if (bsdr->bsd_svc == SPTLRPC_BULK_SVC_NULL)
		return 0;

	rc = plain_verify_bulk_csum(desc, req->rq_flvr.u_bulk.hash.hash_alg,
				    tokenr);
	if (rc) {
		bsdv->bsd_flags |= BSD_FL_ERR;
		CERROR("bulk write: server verify failed: %d\n", rc);
	}

	return rc;
}

static
int plain_svc_wrap_bulk(struct ptlrpc_request *req,
			struct ptlrpc_bulk_desc *desc)
{
	struct ptlrpc_reply_state *rs = req->rq_reply_state;
	struct ptlrpc_bulk_sec_desc *bsdr, *bsdv;
	struct plain_bulk_token *tokenv;
	int rc;

	LASSERT(req->rq_bulk_read);
	LASSERT(req->rq_pack_bulk);

	bsdr = lustre_msg_buf(req->rq_reqbuf, PLAIN_PACK_BULK_OFF, 0);
	bsdv = lustre_msg_buf(rs->rs_repbuf, PLAIN_PACK_BULK_OFF, 0);
	tokenv = (struct plain_bulk_token *) bsdv->bsd_data;

	bsdv->bsd_version = 0;
	bsdv->bsd_type = SPTLRPC_BULK_DEFAULT;
	bsdv->bsd_svc = bsdr->bsd_svc;
	bsdv->bsd_flags = 0;

	if (bsdr->bsd_svc == SPTLRPC_BULK_SVC_NULL)
		return 0;

	rc = plain_generate_bulk_csum(desc, req->rq_flvr.u_bulk.hash.hash_alg,
				      tokenv);
	if (rc) {
		CERROR("bulk read: server failed to compute checksum: %d\n",
		       rc);
	} else {
		if (OBD_FAIL_CHECK(OBD_FAIL_OSC_CHECKSUM_RECEIVE))
			corrupt_bulk_data(desc);
	}

	return rc;
}

static struct ptlrpc_ctx_ops plain_ctx_ops = {
	.refresh		= plain_ctx_refresh,
	.validate	       = plain_ctx_validate,
	.sign		   = plain_ctx_sign,
	.verify		 = plain_ctx_verify,
	.wrap_bulk	      = plain_cli_wrap_bulk,
	.unwrap_bulk	    = plain_cli_unwrap_bulk,
};

static struct ptlrpc_sec_cops plain_sec_cops = {
	.create_sec	     = plain_create_sec,
	.destroy_sec	    = plain_destroy_sec,
	.kill_sec	       = plain_kill_sec,
	.lookup_ctx	     = plain_lookup_ctx,
	.release_ctx	    = plain_release_ctx,
	.flush_ctx_cache	= plain_flush_ctx_cache,
	.alloc_reqbuf	   = plain_alloc_reqbuf,
	.free_reqbuf	    = plain_free_reqbuf,
	.alloc_repbuf	   = plain_alloc_repbuf,
	.free_repbuf	    = plain_free_repbuf,
	.enlarge_reqbuf	 = plain_enlarge_reqbuf,
};

static struct ptlrpc_sec_sops plain_sec_sops = {
	.accept		 = plain_accept,
	.alloc_rs	       = plain_alloc_rs,
	.authorize	      = plain_authorize,
	.free_rs		= plain_free_rs,
	.unwrap_bulk	    = plain_svc_unwrap_bulk,
	.wrap_bulk	      = plain_svc_wrap_bulk,
};

static struct ptlrpc_sec_policy plain_policy = {
	.sp_owner	       = THIS_MODULE,
	.sp_name		= "plain",
	.sp_policy	      = SPTLRPC_POLICY_PLAIN,
	.sp_cops		= &plain_sec_cops,
	.sp_sops		= &plain_sec_sops,
};

int sptlrpc_plain_init(void)
{
	__u32 buflens[PLAIN_PACK_SEGMENTS] = { 0, };
	int rc;

	buflens[PLAIN_PACK_MSG_OFF] = lustre_msg_early_size();
	plain_at_offset = lustre_msg_size_v2(PLAIN_PACK_SEGMENTS, buflens);

	rc = sptlrpc_register_policy(&plain_policy);
	if (rc)
		CERROR("failed to register: %d\n", rc);

	return rc;
}

void sptlrpc_plain_fini(void)
{
	int rc;

	rc = sptlrpc_unregister_policy(&plain_policy);
	if (rc)
		CERROR("cannot unregister: %d\n", rc);
}
