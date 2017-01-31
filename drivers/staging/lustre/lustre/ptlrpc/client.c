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
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

/** Implementation of client-side PortalRPC interfaces */

#define DEBUG_SUBSYSTEM S_RPC

#include "../include/obd_support.h"
#include "../include/obd_class.h"
#include "../include/lustre_lib.h"
#include "../include/lustre_ha.h"
#include "../include/lustre_import.h"
#include "../include/lustre_req_layout.h"

#include "ptlrpc_internal.h"

const struct ptlrpc_bulk_frag_ops ptlrpc_bulk_kiov_pin_ops = {
	.add_kiov_frag	= ptlrpc_prep_bulk_page_pin,
	.release_frags	= ptlrpc_release_bulk_page_pin,
};
EXPORT_SYMBOL(ptlrpc_bulk_kiov_pin_ops);

const struct ptlrpc_bulk_frag_ops ptlrpc_bulk_kiov_nopin_ops = {
	.add_kiov_frag	= ptlrpc_prep_bulk_page_nopin,
	.release_frags	= NULL,
};
EXPORT_SYMBOL(ptlrpc_bulk_kiov_nopin_ops);

static int ptlrpc_send_new_req(struct ptlrpc_request *req);
static int ptlrpcd_check_work(struct ptlrpc_request *req);
static int ptlrpc_unregister_reply(struct ptlrpc_request *request, int async);

/**
 * Initialize passed in client structure \a cl.
 */
void ptlrpc_init_client(int req_portal, int rep_portal, char *name,
			struct ptlrpc_client *cl)
{
	cl->cli_request_portal = req_portal;
	cl->cli_reply_portal = rep_portal;
	cl->cli_name = name;
}
EXPORT_SYMBOL(ptlrpc_init_client);

/**
 * Return PortalRPC connection for remote uud \a uuid
 */
struct ptlrpc_connection *ptlrpc_uuid_to_connection(struct obd_uuid *uuid)
{
	struct ptlrpc_connection *c;
	lnet_nid_t self;
	lnet_process_id_t peer;
	int err;

	/*
	 * ptlrpc_uuid_to_peer() initializes its 2nd parameter
	 * before accessing its values.
	 * coverity[uninit_use_in_call]
	 */
	err = ptlrpc_uuid_to_peer(uuid, &peer, &self);
	if (err != 0) {
		CNETERR("cannot find peer %s!\n", uuid->uuid);
		return NULL;
	}

	c = ptlrpc_connection_get(peer, self, uuid);
	if (c) {
		memcpy(c->c_remote_uuid.uuid,
		       uuid->uuid, sizeof(c->c_remote_uuid.uuid));
	}

	CDEBUG(D_INFO, "%s -> %p\n", uuid->uuid, c);

	return c;
}

/**
 * Allocate and initialize new bulk descriptor on the sender.
 * Returns pointer to the descriptor or NULL on error.
 */
struct ptlrpc_bulk_desc *ptlrpc_new_bulk(unsigned int nfrags,
					 unsigned int max_brw,
					 enum ptlrpc_bulk_op_type type,
					 unsigned int portal,
					 const struct ptlrpc_bulk_frag_ops *ops)
{
	struct ptlrpc_bulk_desc *desc;
	int i;

	/* ensure that only one of KIOV or IOVEC is set but not both */
	LASSERT((ptlrpc_is_bulk_desc_kiov(type) && ops->add_kiov_frag) ||
		(ptlrpc_is_bulk_desc_kvec(type) && ops->add_iov_frag));

	desc = kzalloc(sizeof(*desc), GFP_NOFS);
	if (!desc)
		return NULL;

	if (type & PTLRPC_BULK_BUF_KIOV) {
		GET_KIOV(desc) = kcalloc(nfrags, sizeof(*GET_KIOV(desc)),
					 GFP_NOFS);
		if (!GET_KIOV(desc))
			goto free_desc;
	} else {
		GET_KVEC(desc) = kcalloc(nfrags, sizeof(*GET_KVEC(desc)),
					 GFP_NOFS);
		if (!GET_KVEC(desc))
			goto free_desc;
	}

	spin_lock_init(&desc->bd_lock);
	init_waitqueue_head(&desc->bd_waitq);
	desc->bd_max_iov = nfrags;
	desc->bd_iov_count = 0;
	desc->bd_portal = portal;
	desc->bd_type = type;
	desc->bd_md_count = 0;
	desc->bd_frag_ops = (struct ptlrpc_bulk_frag_ops *)ops;
	LASSERT(max_brw > 0);
	desc->bd_md_max_brw = min(max_brw, PTLRPC_BULK_OPS_COUNT);
	/*
	 * PTLRPC_BULK_OPS_COUNT is the compile-time transfer limit for this
	 * node. Negotiated ocd_brw_size will always be <= this number.
	 */
	for (i = 0; i < PTLRPC_BULK_OPS_COUNT; i++)
		LNetInvalidateHandle(&desc->bd_mds[i]);

	return desc;
free_desc:
	kfree(desc);
	return NULL;
}

/**
 * Prepare bulk descriptor for specified outgoing request \a req that
 * can fit \a nfrags * pages. \a type is bulk type. \a portal is where
 * the bulk to be sent. Used on client-side.
 * Returns pointer to newly allocated initialized bulk descriptor or NULL on
 * error.
 */
struct ptlrpc_bulk_desc *ptlrpc_prep_bulk_imp(struct ptlrpc_request *req,
					      unsigned int nfrags,
					      unsigned int max_brw,
					      unsigned int type,
					      unsigned int portal,
					      const struct ptlrpc_bulk_frag_ops *ops)
{
	struct obd_import *imp = req->rq_import;
	struct ptlrpc_bulk_desc *desc;

	LASSERT(ptlrpc_is_bulk_op_passive(type));

	desc = ptlrpc_new_bulk(nfrags, max_brw, type, portal, ops);
	if (!desc)
		return NULL;

	desc->bd_import_generation = req->rq_import_generation;
	desc->bd_import = class_import_get(imp);
	desc->bd_req = req;

	desc->bd_cbid.cbid_fn = client_bulk_callback;
	desc->bd_cbid.cbid_arg = desc;

	/* This makes req own desc, and free it when she frees herself */
	req->rq_bulk = desc;

	return desc;
}
EXPORT_SYMBOL(ptlrpc_prep_bulk_imp);

void __ptlrpc_prep_bulk_page(struct ptlrpc_bulk_desc *desc,
			     struct page *page, int pageoffset, int len, int pin)
{
	struct bio_vec *kiov;

	LASSERT(desc->bd_iov_count < desc->bd_max_iov);
	LASSERT(page);
	LASSERT(pageoffset >= 0);
	LASSERT(len > 0);
	LASSERT(pageoffset + len <= PAGE_SIZE);
	LASSERT(ptlrpc_is_bulk_desc_kiov(desc->bd_type));

	kiov = &BD_GET_KIOV(desc, desc->bd_iov_count);

	desc->bd_nob += len;

	if (pin)
		get_page(page);

	kiov->bv_page = page;
	kiov->bv_offset = pageoffset;
	kiov->bv_len = len;

	desc->bd_iov_count++;
}
EXPORT_SYMBOL(__ptlrpc_prep_bulk_page);

int ptlrpc_prep_bulk_frag(struct ptlrpc_bulk_desc *desc,
			  void *frag, int len)
{
	struct kvec *iovec;

	LASSERT(desc->bd_iov_count < desc->bd_max_iov);
	LASSERT(frag);
	LASSERT(len > 0);
	LASSERT(ptlrpc_is_bulk_desc_kvec(desc->bd_type));

	iovec = &BD_GET_KVEC(desc, desc->bd_iov_count);

	desc->bd_nob += len;

	iovec->iov_base = frag;
	iovec->iov_len = len;

	desc->bd_iov_count++;

	return desc->bd_nob;
}
EXPORT_SYMBOL(ptlrpc_prep_bulk_frag);

void ptlrpc_free_bulk(struct ptlrpc_bulk_desc *desc)
{
	LASSERT(desc->bd_iov_count != LI_POISON); /* not freed already */
	LASSERT(desc->bd_md_count == 0);	 /* network hands off */
	LASSERT((desc->bd_export != NULL) ^ (desc->bd_import != NULL));
	LASSERT(desc->bd_frag_ops);

	if (ptlrpc_is_bulk_desc_kiov(desc->bd_type))
		sptlrpc_enc_pool_put_pages(desc);

	if (desc->bd_export)
		class_export_put(desc->bd_export);
	else
		class_import_put(desc->bd_import);

	if (desc->bd_frag_ops->release_frags)
		desc->bd_frag_ops->release_frags(desc);

	if (ptlrpc_is_bulk_desc_kiov(desc->bd_type))
		kfree(GET_KIOV(desc));
	else
		kfree(GET_KVEC(desc));

	kfree(desc);
}
EXPORT_SYMBOL(ptlrpc_free_bulk);

/**
 * Set server timelimit for this req, i.e. how long are we willing to wait
 * for reply before timing out this request.
 */
void ptlrpc_at_set_req_timeout(struct ptlrpc_request *req)
{
	__u32 serv_est;
	int idx;
	struct imp_at *at;

	LASSERT(req->rq_import);

	if (AT_OFF) {
		/*
		 * non-AT settings
		 *
		 * \a imp_server_timeout means this is reverse import and
		 * we send (currently only) ASTs to the client and cannot afford
		 * to wait too long for the reply, otherwise the other client
		 * (because of which we are sending this request) would
		 * timeout waiting for us
		 */
		req->rq_timeout = req->rq_import->imp_server_timeout ?
				  obd_timeout / 2 : obd_timeout;
	} else {
		at = &req->rq_import->imp_at;
		idx = import_at_get_index(req->rq_import,
					  req->rq_request_portal);
		serv_est = at_get(&at->iat_service_estimate[idx]);
		req->rq_timeout = at_est2timeout(serv_est);
	}
	/*
	 * We could get even fancier here, using history to predict increased
	 * loading...
	 */

	/*
	 * Let the server know what this RPC timeout is by putting it in the
	 * reqmsg
	 */
	lustre_msg_set_timeout(req->rq_reqmsg, req->rq_timeout);
}
EXPORT_SYMBOL(ptlrpc_at_set_req_timeout);

/* Adjust max service estimate based on server value */
static void ptlrpc_at_adj_service(struct ptlrpc_request *req,
				  unsigned int serv_est)
{
	int idx;
	unsigned int oldse;
	struct imp_at *at;

	LASSERT(req->rq_import);
	at = &req->rq_import->imp_at;

	idx = import_at_get_index(req->rq_import, req->rq_request_portal);
	/*
	 * max service estimates are tracked on the server side,
	 * so just keep minimal history here
	 */
	oldse = at_measured(&at->iat_service_estimate[idx], serv_est);
	if (oldse != 0)
		CDEBUG(D_ADAPTTO, "The RPC service estimate for %s ptl %d has changed from %d to %d\n",
		       req->rq_import->imp_obd->obd_name, req->rq_request_portal,
		       oldse, at_get(&at->iat_service_estimate[idx]));
}

/* Expected network latency per remote node (secs) */
int ptlrpc_at_get_net_latency(struct ptlrpc_request *req)
{
	return AT_OFF ? 0 : at_get(&req->rq_import->imp_at.iat_net_latency);
}

/* Adjust expected network latency */
void ptlrpc_at_adj_net_latency(struct ptlrpc_request *req,
			       unsigned int service_time)
{
	unsigned int nl, oldnl;
	struct imp_at *at;
	time64_t now = ktime_get_real_seconds();

	LASSERT(req->rq_import);

	if (service_time > now - req->rq_sent + 3) {
		/*
		 * bz16408, however, this can also happen if early reply
		 * is lost and client RPC is expired and resent, early reply
		 * or reply of original RPC can still be fit in reply buffer
		 * of resent RPC, now client is measuring time from the
		 * resent time, but server sent back service time of original
		 * RPC.
		 */
		CDEBUG((lustre_msg_get_flags(req->rq_reqmsg) & MSG_RESENT) ?
		       D_ADAPTTO : D_WARNING,
		       "Reported service time %u > total measured time "
		       CFS_DURATION_T"\n", service_time,
		       (long)(now - req->rq_sent));
		return;
	}

	/* Network latency is total time less server processing time */
	nl = max_t(int, now - req->rq_sent -
			service_time, 0) + 1; /* st rounding */
	at = &req->rq_import->imp_at;

	oldnl = at_measured(&at->iat_net_latency, nl);
	if (oldnl != 0)
		CDEBUG(D_ADAPTTO, "The network latency for %s (nid %s) has changed from %d to %d\n",
		       req->rq_import->imp_obd->obd_name,
		       obd_uuid2str(
			       &req->rq_import->imp_connection->c_remote_uuid),
		       oldnl, at_get(&at->iat_net_latency));
}

static int unpack_reply(struct ptlrpc_request *req)
{
	int rc;

	if (SPTLRPC_FLVR_POLICY(req->rq_flvr.sf_rpc) != SPTLRPC_POLICY_NULL) {
		rc = ptlrpc_unpack_rep_msg(req, req->rq_replen);
		if (rc) {
			DEBUG_REQ(D_ERROR, req, "unpack_rep failed: %d", rc);
			return -EPROTO;
		}
	}

	rc = lustre_unpack_rep_ptlrpc_body(req, MSG_PTLRPC_BODY_OFF);
	if (rc) {
		DEBUG_REQ(D_ERROR, req, "unpack ptlrpc body failed: %d", rc);
		return -EPROTO;
	}
	return 0;
}

/**
 * Handle an early reply message, called with the rq_lock held.
 * If anything goes wrong just ignore it - same as if it never happened
 */
static int ptlrpc_at_recv_early_reply(struct ptlrpc_request *req)
	__must_hold(&req->rq_lock)
{
	struct ptlrpc_request *early_req;
	time64_t olddl;
	int rc;

	req->rq_early = 0;
	spin_unlock(&req->rq_lock);

	rc = sptlrpc_cli_unwrap_early_reply(req, &early_req);
	if (rc) {
		spin_lock(&req->rq_lock);
		return rc;
	}

	rc = unpack_reply(early_req);
	if (rc) {
		sptlrpc_cli_finish_early_reply(early_req);
		spin_lock(&req->rq_lock);
		return rc;
	}

	/*
	 * Use new timeout value just to adjust the local value for this
	 * request, don't include it into at_history. It is unclear yet why
	 * service time increased and should it be counted or skipped, e.g.
	 * that can be recovery case or some error or server, the real reply
	 * will add all new data if it is worth to add.
	 */
	req->rq_timeout = lustre_msg_get_timeout(early_req->rq_repmsg);
	lustre_msg_set_timeout(req->rq_reqmsg, req->rq_timeout);

	/* Network latency can be adjusted, it is pure network delays */
	ptlrpc_at_adj_net_latency(req,
				  lustre_msg_get_service_time(early_req->rq_repmsg));

	sptlrpc_cli_finish_early_reply(early_req);

	spin_lock(&req->rq_lock);
	olddl = req->rq_deadline;
	/*
	 * server assumes it now has rq_timeout from when the request
	 * arrived, so the client should give it at least that long.
	 * since we don't know the arrival time we'll use the original
	 * sent time
	 */
	req->rq_deadline = req->rq_sent + req->rq_timeout +
			   ptlrpc_at_get_net_latency(req);

	DEBUG_REQ(D_ADAPTTO, req,
		  "Early reply #%d, new deadline in %lds (%lds)",
		  req->rq_early_count,
		  (long)(req->rq_deadline - ktime_get_real_seconds()),
		  (long)(req->rq_deadline - olddl));

	return rc;
}

static struct kmem_cache *request_cache;

int ptlrpc_request_cache_init(void)
{
	request_cache = kmem_cache_create("ptlrpc_cache",
					  sizeof(struct ptlrpc_request),
					  0, SLAB_HWCACHE_ALIGN, NULL);
	return !request_cache ? -ENOMEM : 0;
}

void ptlrpc_request_cache_fini(void)
{
	kmem_cache_destroy(request_cache);
}

struct ptlrpc_request *ptlrpc_request_cache_alloc(gfp_t flags)
{
	struct ptlrpc_request *req;

	req = kmem_cache_zalloc(request_cache, flags);
	return req;
}

void ptlrpc_request_cache_free(struct ptlrpc_request *req)
{
	kmem_cache_free(request_cache, req);
}

/**
 * Wind down request pool \a pool.
 * Frees all requests from the pool too
 */
void ptlrpc_free_rq_pool(struct ptlrpc_request_pool *pool)
{
	struct list_head *l, *tmp;
	struct ptlrpc_request *req;

	spin_lock(&pool->prp_lock);
	list_for_each_safe(l, tmp, &pool->prp_req_list) {
		req = list_entry(l, struct ptlrpc_request, rq_list);
		list_del(&req->rq_list);
		LASSERT(req->rq_reqbuf);
		LASSERT(req->rq_reqbuf_len == pool->prp_rq_size);
		kvfree(req->rq_reqbuf);
		ptlrpc_request_cache_free(req);
	}
	spin_unlock(&pool->prp_lock);
	kfree(pool);
}
EXPORT_SYMBOL(ptlrpc_free_rq_pool);

/**
 * Allocates, initializes and adds \a num_rq requests to the pool \a pool
 */
int ptlrpc_add_rqs_to_pool(struct ptlrpc_request_pool *pool, int num_rq)
{
	int i;
	int size = 1;

	while (size < pool->prp_rq_size)
		size <<= 1;

	LASSERTF(list_empty(&pool->prp_req_list) ||
		 size == pool->prp_rq_size,
		 "Trying to change pool size with nonempty pool from %d to %d bytes\n",
		 pool->prp_rq_size, size);

	spin_lock(&pool->prp_lock);
	pool->prp_rq_size = size;
	for (i = 0; i < num_rq; i++) {
		struct ptlrpc_request *req;
		struct lustre_msg *msg;

		spin_unlock(&pool->prp_lock);
		req = ptlrpc_request_cache_alloc(GFP_NOFS);
		if (!req)
			return i;
		msg = libcfs_kvzalloc(size, GFP_NOFS);
		if (!msg) {
			ptlrpc_request_cache_free(req);
			return i;
		}
		req->rq_reqbuf = msg;
		req->rq_reqbuf_len = size;
		req->rq_pool = pool;
		spin_lock(&pool->prp_lock);
		list_add_tail(&req->rq_list, &pool->prp_req_list);
	}
	spin_unlock(&pool->prp_lock);
	return num_rq;
}
EXPORT_SYMBOL(ptlrpc_add_rqs_to_pool);

/**
 * Create and initialize new request pool with given attributes:
 * \a num_rq - initial number of requests to create for the pool
 * \a msgsize - maximum message size possible for requests in thid pool
 * \a populate_pool - function to be called when more requests need to be added
 *		    to the pool
 * Returns pointer to newly created pool or NULL on error.
 */
struct ptlrpc_request_pool *
ptlrpc_init_rq_pool(int num_rq, int msgsize,
		    int (*populate_pool)(struct ptlrpc_request_pool *, int))
{
	struct ptlrpc_request_pool *pool;

	pool = kzalloc(sizeof(struct ptlrpc_request_pool), GFP_NOFS);
	if (!pool)
		return NULL;

	/*
	 * Request next power of two for the allocation, because internally
	 * kernel would do exactly this
	 */

	spin_lock_init(&pool->prp_lock);
	INIT_LIST_HEAD(&pool->prp_req_list);
	pool->prp_rq_size = msgsize + SPTLRPC_MAX_PAYLOAD;
	pool->prp_populate = populate_pool;

	populate_pool(pool, num_rq);

	return pool;
}
EXPORT_SYMBOL(ptlrpc_init_rq_pool);

/**
 * Fetches one request from pool \a pool
 */
static struct ptlrpc_request *
ptlrpc_prep_req_from_pool(struct ptlrpc_request_pool *pool)
{
	struct ptlrpc_request *request;
	struct lustre_msg *reqbuf;

	if (!pool)
		return NULL;

	spin_lock(&pool->prp_lock);

	/*
	 * See if we have anything in a pool, and bail out if nothing,
	 * in writeout path, where this matters, this is safe to do, because
	 * nothing is lost in this case, and when some in-flight requests
	 * complete, this code will be called again.
	 */
	if (unlikely(list_empty(&pool->prp_req_list))) {
		spin_unlock(&pool->prp_lock);
		return NULL;
	}

	request = list_entry(pool->prp_req_list.next, struct ptlrpc_request,
			     rq_list);
	list_del_init(&request->rq_list);
	spin_unlock(&pool->prp_lock);

	LASSERT(request->rq_reqbuf);
	LASSERT(request->rq_pool);

	reqbuf = request->rq_reqbuf;
	memset(request, 0, sizeof(*request));
	request->rq_reqbuf = reqbuf;
	request->rq_reqbuf_len = pool->prp_rq_size;
	request->rq_pool = pool;

	return request;
}

/**
 * Returns freed \a request to pool.
 */
static void __ptlrpc_free_req_to_pool(struct ptlrpc_request *request)
{
	struct ptlrpc_request_pool *pool = request->rq_pool;

	spin_lock(&pool->prp_lock);
	LASSERT(list_empty(&request->rq_list));
	LASSERT(!request->rq_receiving_reply);
	list_add_tail(&request->rq_list, &pool->prp_req_list);
	spin_unlock(&pool->prp_lock);
}

void ptlrpc_add_unreplied(struct ptlrpc_request *req)
{
	struct obd_import	*imp = req->rq_import;
	struct list_head	*tmp;
	struct ptlrpc_request	*iter;

	assert_spin_locked(&imp->imp_lock);
	LASSERT(list_empty(&req->rq_unreplied_list));

	/* unreplied list is sorted by xid in ascending order */
	list_for_each_prev(tmp, &imp->imp_unreplied_list) {
		iter = list_entry(tmp, struct ptlrpc_request,
				  rq_unreplied_list);

		LASSERT(req->rq_xid != iter->rq_xid);
		if (req->rq_xid < iter->rq_xid)
			continue;
		list_add(&req->rq_unreplied_list, &iter->rq_unreplied_list);
		return;
	}
	list_add(&req->rq_unreplied_list, &imp->imp_unreplied_list);
}

void ptlrpc_assign_next_xid_nolock(struct ptlrpc_request *req)
{
	req->rq_xid = ptlrpc_next_xid();
	ptlrpc_add_unreplied(req);
}

static inline void ptlrpc_assign_next_xid(struct ptlrpc_request *req)
{
	spin_lock(&req->rq_import->imp_lock);
	ptlrpc_assign_next_xid_nolock(req);
	spin_unlock(&req->rq_import->imp_lock);
}

int ptlrpc_request_bufs_pack(struct ptlrpc_request *request,
			     __u32 version, int opcode, char **bufs,
			     struct ptlrpc_cli_ctx *ctx)
{
	int count;
	struct obd_import *imp;
	__u32 *lengths;
	int rc;

	count = req_capsule_filled_sizes(&request->rq_pill, RCL_CLIENT);
	imp = request->rq_import;
	lengths = request->rq_pill.rc_area[RCL_CLIENT];

	if (unlikely(ctx)) {
		request->rq_cli_ctx = sptlrpc_cli_ctx_get(ctx);
	} else {
		rc = sptlrpc_req_get_ctx(request);
		if (rc)
			goto out_free;
	}
	sptlrpc_req_set_flavor(request, opcode);

	rc = lustre_pack_request(request, imp->imp_msg_magic, count,
				 lengths, bufs);
	if (rc)
		goto out_ctx;

	lustre_msg_add_version(request->rq_reqmsg, version);
	request->rq_send_state = LUSTRE_IMP_FULL;
	request->rq_type = PTL_RPC_MSG_REQUEST;

	request->rq_req_cbid.cbid_fn = request_out_callback;
	request->rq_req_cbid.cbid_arg = request;

	request->rq_reply_cbid.cbid_fn = reply_in_callback;
	request->rq_reply_cbid.cbid_arg = request;

	request->rq_reply_deadline = 0;
	request->rq_bulk_deadline = 0;
	request->rq_req_deadline = 0;
	request->rq_phase = RQ_PHASE_NEW;
	request->rq_next_phase = RQ_PHASE_UNDEFINED;

	request->rq_request_portal = imp->imp_client->cli_request_portal;
	request->rq_reply_portal = imp->imp_client->cli_reply_portal;

	ptlrpc_at_set_req_timeout(request);

	lustre_msg_set_opc(request->rq_reqmsg, opcode);
	ptlrpc_assign_next_xid(request);

	/* Let's setup deadline for req/reply/bulk unlink for opcode. */
	if (cfs_fail_val == opcode) {
		time_t *fail_t = NULL, *fail2_t = NULL;

		if (CFS_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_BULK_UNLINK)) {
			fail_t = &request->rq_bulk_deadline;
		} else if (CFS_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_REPL_UNLINK)) {
			fail_t = &request->rq_reply_deadline;
		} else if (CFS_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_REQ_UNLINK)) {
			fail_t = &request->rq_req_deadline;
		} else if (CFS_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_BOTH_UNLINK)) {
			fail_t = &request->rq_reply_deadline;
			fail2_t = &request->rq_bulk_deadline;
		}

		if (fail_t) {
			*fail_t = ktime_get_real_seconds() + LONG_UNLINK;

			if (fail2_t)
				*fail2_t = ktime_get_real_seconds() +
						 LONG_UNLINK;

			/* The RPC is infected, let the test change the
			 * fail_loc
			 */
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(cfs_time_seconds(2));
			set_current_state(TASK_RUNNING);
		}
	}

	return 0;

out_ctx:
	LASSERT(!request->rq_pool);
	sptlrpc_cli_ctx_put(request->rq_cli_ctx, 1);
out_free:
	class_import_put(imp);
	return rc;
}
EXPORT_SYMBOL(ptlrpc_request_bufs_pack);

/**
 * Pack request buffers for network transfer, performing necessary encryption
 * steps if necessary.
 */
int ptlrpc_request_pack(struct ptlrpc_request *request,
			__u32 version, int opcode)
{
	int rc;

	rc = ptlrpc_request_bufs_pack(request, version, opcode, NULL, NULL);
	if (rc)
		return rc;

	/*
	 * For some old 1.8 clients (< 1.8.7), they will LASSERT the size of
	 * ptlrpc_body sent from server equal to local ptlrpc_body size, so we
	 * have to send old ptlrpc_body to keep interoperability with these
	 * clients.
	 *
	 * Only three kinds of server->client RPCs so far:
	 *  - LDLM_BL_CALLBACK
	 *  - LDLM_CP_CALLBACK
	 *  - LDLM_GL_CALLBACK
	 *
	 * XXX This should be removed whenever we drop the interoperability with
	 *     the these old clients.
	 */
	if (opcode == LDLM_BL_CALLBACK || opcode == LDLM_CP_CALLBACK ||
	    opcode == LDLM_GL_CALLBACK)
		req_capsule_shrink(&request->rq_pill, &RMF_PTLRPC_BODY,
				   sizeof(struct ptlrpc_body_v2), RCL_CLIENT);

	return rc;
}
EXPORT_SYMBOL(ptlrpc_request_pack);

/**
 * Helper function to allocate new request on import \a imp
 * and possibly using existing request from pool \a pool if provided.
 * Returns allocated request structure with import field filled or
 * NULL on error.
 */
static inline
struct ptlrpc_request *__ptlrpc_request_alloc(struct obd_import *imp,
					      struct ptlrpc_request_pool *pool)
{
	struct ptlrpc_request *request;

	request = ptlrpc_request_cache_alloc(GFP_NOFS);

	if (!request && pool)
		request = ptlrpc_prep_req_from_pool(pool);

	if (request) {
		ptlrpc_cli_req_init(request);

		LASSERTF((unsigned long)imp > 0x1000, "%p", imp);
		LASSERT(imp != LP_POISON);
		LASSERTF((unsigned long)imp->imp_client > 0x1000, "%p\n",
			 imp->imp_client);
		LASSERT(imp->imp_client != LP_POISON);

		request->rq_import = class_import_get(imp);
	} else {
		CERROR("request allocation out of memory\n");
	}

	return request;
}

/**
 * Helper function for creating a request.
 * Calls __ptlrpc_request_alloc to allocate new request structure and inits
 * buffer structures according to capsule template \a format.
 * Returns allocated request structure pointer or NULL on error.
 */
static struct ptlrpc_request *
ptlrpc_request_alloc_internal(struct obd_import *imp,
			      struct ptlrpc_request_pool *pool,
			      const struct req_format *format)
{
	struct ptlrpc_request *request;

	request = __ptlrpc_request_alloc(imp, pool);
	if (!request)
		return NULL;

	req_capsule_init(&request->rq_pill, request, RCL_CLIENT);
	req_capsule_set(&request->rq_pill, format);
	return request;
}

/**
 * Allocate new request structure for import \a imp and initialize its
 * buffer structure according to capsule template \a format.
 */
struct ptlrpc_request *ptlrpc_request_alloc(struct obd_import *imp,
					    const struct req_format *format)
{
	return ptlrpc_request_alloc_internal(imp, NULL, format);
}
EXPORT_SYMBOL(ptlrpc_request_alloc);

/**
 * Allocate new request structure for import \a imp from pool \a pool and
 * initialize its buffer structure according to capsule template \a format.
 */
struct ptlrpc_request *ptlrpc_request_alloc_pool(struct obd_import *imp,
						 struct ptlrpc_request_pool *pool,
						 const struct req_format *format)
{
	return ptlrpc_request_alloc_internal(imp, pool, format);
}
EXPORT_SYMBOL(ptlrpc_request_alloc_pool);

/**
 * For requests not from pool, free memory of the request structure.
 * For requests obtained from a pool earlier, return request back to pool.
 */
void ptlrpc_request_free(struct ptlrpc_request *request)
{
	if (request->rq_pool)
		__ptlrpc_free_req_to_pool(request);
	else
		ptlrpc_request_cache_free(request);
}
EXPORT_SYMBOL(ptlrpc_request_free);

/**
 * Allocate new request for operation \a opcode and immediately pack it for
 * network transfer.
 * Only used for simple requests like OBD_PING where the only important
 * part of the request is operation itself.
 * Returns allocated request or NULL on error.
 */
struct ptlrpc_request *ptlrpc_request_alloc_pack(struct obd_import *imp,
						 const struct req_format *format,
						 __u32 version, int opcode)
{
	struct ptlrpc_request *req = ptlrpc_request_alloc(imp, format);
	int rc;

	if (req) {
		rc = ptlrpc_request_pack(req, version, opcode);
		if (rc) {
			ptlrpc_request_free(req);
			req = NULL;
		}
	}
	return req;
}
EXPORT_SYMBOL(ptlrpc_request_alloc_pack);

/**
 * Allocate and initialize new request set structure on the current CPT.
 * Returns a pointer to the newly allocated set structure or NULL on error.
 */
struct ptlrpc_request_set *ptlrpc_prep_set(void)
{
	struct ptlrpc_request_set *set;
	int cpt;

	cpt = cfs_cpt_current(cfs_cpt_table, 0);
	set = kzalloc_node(sizeof(*set), GFP_NOFS,
			   cfs_cpt_spread_node(cfs_cpt_table, cpt));
	if (!set)
		return NULL;
	atomic_set(&set->set_refcount, 1);
	INIT_LIST_HEAD(&set->set_requests);
	init_waitqueue_head(&set->set_waitq);
	atomic_set(&set->set_new_count, 0);
	atomic_set(&set->set_remaining, 0);
	spin_lock_init(&set->set_new_req_lock);
	INIT_LIST_HEAD(&set->set_new_requests);
	INIT_LIST_HEAD(&set->set_cblist);
	set->set_max_inflight = UINT_MAX;
	set->set_producer = NULL;
	set->set_producer_arg = NULL;
	set->set_rc = 0;

	return set;
}
EXPORT_SYMBOL(ptlrpc_prep_set);

/**
 * Allocate and initialize new request set structure with flow control
 * extension. This extension allows to control the number of requests in-flight
 * for the whole set. A callback function to generate requests must be provided
 * and the request set will keep the number of requests sent over the wire to
 * @max_inflight.
 * Returns a pointer to the newly allocated set structure or NULL on error.
 */
struct ptlrpc_request_set *ptlrpc_prep_fcset(int max, set_producer_func func,
					     void *arg)

{
	struct ptlrpc_request_set *set;

	set = ptlrpc_prep_set();
	if (!set)
		return NULL;

	set->set_max_inflight = max;
	set->set_producer = func;
	set->set_producer_arg = arg;

	return set;
}

/**
 * Wind down and free request set structure previously allocated with
 * ptlrpc_prep_set.
 * Ensures that all requests on the set have completed and removes
 * all requests from the request list in a set.
 * If any unsent request happen to be on the list, pretends that they got
 * an error in flight and calls their completion handler.
 */
void ptlrpc_set_destroy(struct ptlrpc_request_set *set)
{
	struct list_head *tmp;
	struct list_head *next;
	int expected_phase;
	int n = 0;

	/* Requests on the set should either all be completed, or all be new */
	expected_phase = (atomic_read(&set->set_remaining) == 0) ?
			 RQ_PHASE_COMPLETE : RQ_PHASE_NEW;
	list_for_each(tmp, &set->set_requests) {
		struct ptlrpc_request *req =
			list_entry(tmp, struct ptlrpc_request, rq_set_chain);

		LASSERT(req->rq_phase == expected_phase);
		n++;
	}

	LASSERTF(atomic_read(&set->set_remaining) == 0 ||
		 atomic_read(&set->set_remaining) == n, "%d / %d\n",
		 atomic_read(&set->set_remaining), n);

	list_for_each_safe(tmp, next, &set->set_requests) {
		struct ptlrpc_request *req =
			list_entry(tmp, struct ptlrpc_request, rq_set_chain);
		list_del_init(&req->rq_set_chain);

		LASSERT(req->rq_phase == expected_phase);

		if (req->rq_phase == RQ_PHASE_NEW) {
			ptlrpc_req_interpret(NULL, req, -EBADR);
			atomic_dec(&set->set_remaining);
		}

		spin_lock(&req->rq_lock);
		req->rq_set = NULL;
		req->rq_invalid_rqset = 0;
		spin_unlock(&req->rq_lock);

		ptlrpc_req_finished(req);
	}

	LASSERT(atomic_read(&set->set_remaining) == 0);

	ptlrpc_reqset_put(set);
}
EXPORT_SYMBOL(ptlrpc_set_destroy);

/**
 * Add a new request to the general purpose request set.
 * Assumes request reference from the caller.
 */
void ptlrpc_set_add_req(struct ptlrpc_request_set *set,
			struct ptlrpc_request *req)
{
	LASSERT(list_empty(&req->rq_set_chain));

	/* The set takes over the caller's request reference */
	list_add_tail(&req->rq_set_chain, &set->set_requests);
	req->rq_set = set;
	atomic_inc(&set->set_remaining);
	req->rq_queued_time = cfs_time_current();

	if (req->rq_reqmsg)
		lustre_msg_set_jobid(req->rq_reqmsg, NULL);

	if (set->set_producer)
		/*
		 * If the request set has a producer callback, the RPC must be
		 * sent straight away
		 */
		ptlrpc_send_new_req(req);
}
EXPORT_SYMBOL(ptlrpc_set_add_req);

/**
 * Add a request to a request with dedicated server thread
 * and wake the thread to make any necessary processing.
 * Currently only used for ptlrpcd.
 */
void ptlrpc_set_add_new_req(struct ptlrpcd_ctl *pc,
			    struct ptlrpc_request *req)
{
	struct ptlrpc_request_set *set = pc->pc_set;
	int count, i;

	LASSERT(!req->rq_set);
	LASSERT(test_bit(LIOD_STOP, &pc->pc_flags) == 0);

	spin_lock(&set->set_new_req_lock);
	/* The set takes over the caller's request reference.  */
	req->rq_set = set;
	req->rq_queued_time = cfs_time_current();
	list_add_tail(&req->rq_set_chain, &set->set_new_requests);
	count = atomic_inc_return(&set->set_new_count);
	spin_unlock(&set->set_new_req_lock);

	/* Only need to call wakeup once for the first entry. */
	if (count == 1) {
		wake_up(&set->set_waitq);

		/*
		 * XXX: It maybe unnecessary to wakeup all the partners. But to
		 *      guarantee the async RPC can be processed ASAP, we have
		 *      no other better choice. It maybe fixed in future.
		 */
		for (i = 0; i < pc->pc_npartners; i++)
			wake_up(&pc->pc_partners[i]->pc_set->set_waitq);
	}
}

/**
 * Based on the current state of the import, determine if the request
 * can be sent, is an error, or should be delayed.
 *
 * Returns true if this request should be delayed. If false, and
 * *status is set, then the request can not be sent and *status is the
 * error code.  If false and status is 0, then request can be sent.
 *
 * The imp->imp_lock must be held.
 */
static int ptlrpc_import_delay_req(struct obd_import *imp,
				   struct ptlrpc_request *req, int *status)
{
	int delay = 0;

	*status = 0;

	if (req->rq_ctx_init || req->rq_ctx_fini) {
		/* always allow ctx init/fini rpc go through */
	} else if (imp->imp_state == LUSTRE_IMP_NEW) {
		DEBUG_REQ(D_ERROR, req, "Uninitialized import.");
		*status = -EIO;
	} else if (imp->imp_state == LUSTRE_IMP_CLOSED) {
		/* pings may safely race with umount */
		DEBUG_REQ(lustre_msg_get_opc(req->rq_reqmsg) == OBD_PING ?
			  D_HA : D_ERROR, req, "IMP_CLOSED ");
		*status = -EIO;
	} else if (ptlrpc_send_limit_expired(req)) {
		/* probably doesn't need to be a D_ERROR after initial testing */
		DEBUG_REQ(D_HA, req, "send limit expired ");
		*status = -ETIMEDOUT;
	} else if (req->rq_send_state == LUSTRE_IMP_CONNECTING &&
		   imp->imp_state == LUSTRE_IMP_CONNECTING) {
		/* allow CONNECT even if import is invalid */
		if (atomic_read(&imp->imp_inval_count) != 0) {
			DEBUG_REQ(D_ERROR, req, "invalidate in flight");
			*status = -EIO;
		}
	} else if (imp->imp_invalid || imp->imp_obd->obd_no_recov) {
		if (!imp->imp_deactive)
			DEBUG_REQ(D_NET, req, "IMP_INVALID");
		*status = -ESHUTDOWN; /* bz 12940 */
	} else if (req->rq_import_generation != imp->imp_generation) {
		DEBUG_REQ(D_ERROR, req, "req wrong generation:");
		*status = -EIO;
	} else if (req->rq_send_state != imp->imp_state) {
		/* invalidate in progress - any requests should be drop */
		if (atomic_read(&imp->imp_inval_count) != 0) {
			DEBUG_REQ(D_ERROR, req, "invalidate in flight");
			*status = -EIO;
		} else if (imp->imp_dlm_fake || req->rq_no_delay) {
			*status = -EWOULDBLOCK;
		} else if (req->rq_allow_replay &&
			  (imp->imp_state == LUSTRE_IMP_REPLAY ||
			   imp->imp_state == LUSTRE_IMP_REPLAY_LOCKS ||
			   imp->imp_state == LUSTRE_IMP_REPLAY_WAIT ||
			   imp->imp_state == LUSTRE_IMP_RECOVER)) {
			DEBUG_REQ(D_HA, req, "allow during recovery.\n");
		} else {
			delay = 1;
		}
	}

	return delay;
}

/**
 * Decide if the error message should be printed to the console or not.
 * Makes its decision based on request type, status, and failure frequency.
 *
 * \param[in] req  request that failed and may need a console message
 *
 * \retval false if no message should be printed
 * \retval true  if console message should be printed
 */
static bool ptlrpc_console_allow(struct ptlrpc_request *req)
{
	__u32 opc;

	LASSERT(req->rq_reqmsg);
	opc = lustre_msg_get_opc(req->rq_reqmsg);

	/* Suppress particular reconnect errors which are to be expected. */
	if (opc == OST_CONNECT || opc == MDS_CONNECT || opc == MGS_CONNECT) {
		int err;

		/* Suppress timed out reconnect requests */
		if (lustre_handle_is_used(&req->rq_import->imp_remote_handle) ||
		    req->rq_timedout)
			return false;

		/*
		 * Suppress most unavailable/again reconnect requests, but
		 * print occasionally so it is clear client is trying to
		 * connect to a server where no target is running.
		 */
		err = lustre_msg_get_status(req->rq_repmsg);
		if ((err == -ENODEV || err == -EAGAIN) &&
		    req->rq_import->imp_conn_cnt % 30 != 20)
			return false;
	}

	return true;
}

/**
 * Check request processing status.
 * Returns the status.
 */
static int ptlrpc_check_status(struct ptlrpc_request *req)
{
	int err;

	err = lustre_msg_get_status(req->rq_repmsg);
	if (lustre_msg_get_type(req->rq_repmsg) == PTL_RPC_MSG_ERR) {
		struct obd_import *imp = req->rq_import;
		lnet_nid_t nid = imp->imp_connection->c_peer.nid;
		__u32 opc = lustre_msg_get_opc(req->rq_reqmsg);

		/* -EAGAIN is normal when using POSIX flocks */
		if (ptlrpc_console_allow(req) &&
		    !(opc == LDLM_ENQUEUE && err == -EAGAIN))
			LCONSOLE_ERROR_MSG(0x011, "%s: operation %s to node %s failed: rc = %d\n",
					   imp->imp_obd->obd_name,
					   ll_opcode2str(opc),
					   libcfs_nid2str(nid), err);
		return err < 0 ? err : -EINVAL;
	}

	if (err < 0)
		DEBUG_REQ(D_INFO, req, "status is %d", err);
	else if (err > 0)
		/* XXX: translate this error from net to host */
		DEBUG_REQ(D_INFO, req, "status is %d", err);

	return err;
}

/**
 * save pre-versions of objects into request for replay.
 * Versions are obtained from server reply.
 * used for VBR.
 */
static void ptlrpc_save_versions(struct ptlrpc_request *req)
{
	struct lustre_msg *repmsg = req->rq_repmsg;
	struct lustre_msg *reqmsg = req->rq_reqmsg;
	__u64 *versions = lustre_msg_get_versions(repmsg);

	if (lustre_msg_get_flags(req->rq_reqmsg) & MSG_REPLAY)
		return;

	LASSERT(versions);
	lustre_msg_set_versions(reqmsg, versions);
	CDEBUG(D_INFO, "Client save versions [%#llx/%#llx]\n",
	       versions[0], versions[1]);
}

__u64 ptlrpc_known_replied_xid(struct obd_import *imp)
{
	struct ptlrpc_request *req;

	assert_spin_locked(&imp->imp_lock);
	if (list_empty(&imp->imp_unreplied_list))
		return 0;

	req = list_entry(imp->imp_unreplied_list.next, struct ptlrpc_request,
			 rq_unreplied_list);
	LASSERTF(req->rq_xid >= 1, "XID:%llu\n", req->rq_xid);

	if (imp->imp_known_replied_xid < req->rq_xid - 1)
		imp->imp_known_replied_xid = req->rq_xid - 1;

	return req->rq_xid - 1;
}

/**
 * Callback function called when client receives RPC reply for \a req.
 * Returns 0 on success or error code.
 * The return value would be assigned to req->rq_status by the caller
 * as request processing status.
 * This function also decides if the request needs to be saved for later replay.
 */
static int after_reply(struct ptlrpc_request *req)
{
	struct obd_import *imp = req->rq_import;
	struct obd_device *obd = req->rq_import->imp_obd;
	int rc;
	struct timespec64 work_start;
	long timediff;
	u64 committed;

	LASSERT(obd);
	/* repbuf must be unlinked */
	LASSERT(!req->rq_receiving_reply && req->rq_reply_unlinked);

	if (req->rq_reply_truncated) {
		if (ptlrpc_no_resend(req)) {
			DEBUG_REQ(D_ERROR, req, "reply buffer overflow, expected: %d, actual size: %d",
				  req->rq_nob_received, req->rq_repbuf_len);
			return -EOVERFLOW;
		}

		sptlrpc_cli_free_repbuf(req);
		/*
		 * Pass the required reply buffer size (include space for early
		 * reply).  NB: no need to round up because alloc_repbuf will
		 * round it up
		 */
		req->rq_replen       = req->rq_nob_received;
		req->rq_nob_received = 0;
		spin_lock(&req->rq_lock);
		req->rq_resend       = 1;
		spin_unlock(&req->rq_lock);
		return 0;
	}

	ktime_get_real_ts64(&work_start);
	timediff = (work_start.tv_sec - req->rq_sent_tv.tv_sec) * USEC_PER_SEC +
		   (work_start.tv_nsec - req->rq_sent_tv.tv_nsec) /
								 NSEC_PER_USEC;
	/*
	 * NB Until this point, the whole of the incoming message,
	 * including buflens, status etc is in the sender's byte order.
	 */
	rc = sptlrpc_cli_unwrap_reply(req);
	if (rc) {
		DEBUG_REQ(D_ERROR, req, "unwrap reply failed (%d):", rc);
		return rc;
	}

	/* Security layer unwrap might ask resend this request. */
	if (req->rq_resend)
		return 0;

	rc = unpack_reply(req);
	if (rc)
		return rc;

	/* retry indefinitely on EINPROGRESS */
	if (lustre_msg_get_status(req->rq_repmsg) == -EINPROGRESS &&
	    ptlrpc_no_resend(req) == 0 && !req->rq_no_retry_einprogress) {
		time64_t now = ktime_get_real_seconds();

		DEBUG_REQ(D_RPCTRACE, req, "Resending request on EINPROGRESS");
		spin_lock(&req->rq_lock);
		req->rq_resend = 1;
		spin_unlock(&req->rq_lock);
		req->rq_nr_resend++;

		/* Readjust the timeout for current conditions */
		ptlrpc_at_set_req_timeout(req);
		/*
		 * delay resend to give a chance to the server to get ready.
		 * The delay is increased by 1s on every resend and is capped to
		 * the current request timeout (i.e. obd_timeout if AT is off,
		 * or AT service time x 125% + 5s, see at_est2timeout)
		 */
		if (req->rq_nr_resend > req->rq_timeout)
			req->rq_sent = now + req->rq_timeout;
		else
			req->rq_sent = now + req->rq_nr_resend;

		/* Resend for EINPROGRESS will use a new XID */
		spin_lock(&imp->imp_lock);
		list_del_init(&req->rq_unreplied_list);
		spin_unlock(&imp->imp_lock);

		return 0;
	}

	if (obd->obd_svc_stats) {
		lprocfs_counter_add(obd->obd_svc_stats, PTLRPC_REQWAIT_CNTR,
				    timediff);
		ptlrpc_lprocfs_rpc_sent(req, timediff);
	}

	if (lustre_msg_get_type(req->rq_repmsg) != PTL_RPC_MSG_REPLY &&
	    lustre_msg_get_type(req->rq_repmsg) != PTL_RPC_MSG_ERR) {
		DEBUG_REQ(D_ERROR, req, "invalid packet received (type=%u)",
			  lustre_msg_get_type(req->rq_repmsg));
		return -EPROTO;
	}

	if (lustre_msg_get_opc(req->rq_reqmsg) != OBD_PING)
		CFS_FAIL_TIMEOUT(OBD_FAIL_PTLRPC_PAUSE_REP, cfs_fail_val);
	ptlrpc_at_adj_service(req, lustre_msg_get_timeout(req->rq_repmsg));
	ptlrpc_at_adj_net_latency(req,
				  lustre_msg_get_service_time(req->rq_repmsg));

	rc = ptlrpc_check_status(req);
	imp->imp_connect_error = rc;

	if (rc) {
		/*
		 * Either we've been evicted, or the server has failed for
		 * some reason. Try to reconnect, and if that fails, punt to
		 * the upcall.
		 */
		if (ptlrpc_recoverable_error(rc)) {
			if (req->rq_send_state != LUSTRE_IMP_FULL ||
			    imp->imp_obd->obd_no_recov || imp->imp_dlm_fake) {
				return rc;
			}
			ptlrpc_request_handle_notconn(req);
			return rc;
		}
	} else {
		/*
		 * Let's look if server sent slv. Do it only for RPC with
		 * rc == 0.
		 */
		ldlm_cli_update_pool(req);
	}

	/* Store transno in reqmsg for replay. */
	if (!(lustre_msg_get_flags(req->rq_reqmsg) & MSG_REPLAY)) {
		req->rq_transno = lustre_msg_get_transno(req->rq_repmsg);
		lustre_msg_set_transno(req->rq_reqmsg, req->rq_transno);
	}

	if (imp->imp_replayable) {
		spin_lock(&imp->imp_lock);
		/*
		 * No point in adding already-committed requests to the replay
		 * list, we will just remove them immediately. b=9829
		 */
		if (req->rq_transno != 0 &&
		    (req->rq_transno >
		     lustre_msg_get_last_committed(req->rq_repmsg) ||
		     req->rq_replay)) {
			/* version recovery */
			ptlrpc_save_versions(req);
			ptlrpc_retain_replayable_request(req, imp);
		} else if (req->rq_commit_cb &&
			   list_empty(&req->rq_replay_list)) {
			/*
			 * NB: don't call rq_commit_cb if it's already on
			 * rq_replay_list, ptlrpc_free_committed() will call
			 * it later, see LU-3618 for details
			 */
			spin_unlock(&imp->imp_lock);
			req->rq_commit_cb(req);
			spin_lock(&imp->imp_lock);
		}

		/* Replay-enabled imports return commit-status information. */
		committed = lustre_msg_get_last_committed(req->rq_repmsg);
		if (likely(committed > imp->imp_peer_committed_transno))
			imp->imp_peer_committed_transno = committed;

		ptlrpc_free_committed(imp);

		if (!list_empty(&imp->imp_replay_list)) {
			struct ptlrpc_request *last;

			last = list_entry(imp->imp_replay_list.prev,
					  struct ptlrpc_request,
					  rq_replay_list);
			/*
			 * Requests with rq_replay stay on the list even if no
			 * commit is expected.
			 */
			if (last->rq_transno > imp->imp_peer_committed_transno)
				ptlrpc_pinger_commit_expected(imp);
		}

		spin_unlock(&imp->imp_lock);
	}

	return rc;
}

/**
 * Helper function to send request \a req over the network for the first time
 * Also adjusts request phase.
 * Returns 0 on success or error code.
 */
static int ptlrpc_send_new_req(struct ptlrpc_request *req)
{
	struct obd_import *imp = req->rq_import;
	u64 min_xid = 0;
	int rc;

	LASSERT(req->rq_phase == RQ_PHASE_NEW);

	/* do not try to go further if there is not enough memory in enc_pool */
	if (req->rq_sent && req->rq_bulk)
		if (req->rq_bulk->bd_iov_count > get_free_pages_in_pool() &&
		    pool_is_at_full_capacity())
			return -ENOMEM;

	if (req->rq_sent && (req->rq_sent > ktime_get_real_seconds()) &&
	    (!req->rq_generation_set ||
	     req->rq_import_generation == imp->imp_generation))
		return 0;

	ptlrpc_rqphase_move(req, RQ_PHASE_RPC);

	spin_lock(&imp->imp_lock);

	LASSERT(req->rq_xid);
	LASSERT(!list_empty(&req->rq_unreplied_list));

	if (!req->rq_generation_set)
		req->rq_import_generation = imp->imp_generation;

	if (ptlrpc_import_delay_req(imp, req, &rc)) {
		spin_lock(&req->rq_lock);
		req->rq_waiting = 1;
		spin_unlock(&req->rq_lock);

		DEBUG_REQ(D_HA, req, "req from PID %d waiting for recovery: (%s != %s)",
			  lustre_msg_get_status(req->rq_reqmsg),
			  ptlrpc_import_state_name(req->rq_send_state),
			  ptlrpc_import_state_name(imp->imp_state));
		LASSERT(list_empty(&req->rq_list));
		list_add_tail(&req->rq_list, &imp->imp_delayed_list);
		atomic_inc(&req->rq_import->imp_inflight);
		spin_unlock(&imp->imp_lock);
		return 0;
	}

	if (rc != 0) {
		spin_unlock(&imp->imp_lock);
		req->rq_status = rc;
		ptlrpc_rqphase_move(req, RQ_PHASE_INTERPRET);
		return rc;
	}

	LASSERT(list_empty(&req->rq_list));
	list_add_tail(&req->rq_list, &imp->imp_sending_list);
	atomic_inc(&req->rq_import->imp_inflight);

	/* find the known replied XID from the unreplied list, CONNECT
	 * and DISCONNECT requests are skipped to make the sanity check
	 * on server side happy. see process_req_last_xid().
	 *
	 * For CONNECT: Because replay requests have lower XID, it'll
	 * break the sanity check if CONNECT bump the exp_last_xid on
	 * server.
	 *
	 * For DISCONNECT: Since client will abort inflight RPC before
	 * sending DISCONNECT, DISCONNECT may carry an XID which higher
	 * than the inflight RPC.
	 */
	if (!ptlrpc_req_is_connect(req) && !ptlrpc_req_is_disconnect(req))
		min_xid = ptlrpc_known_replied_xid(imp);
	spin_unlock(&imp->imp_lock);

	lustre_msg_set_last_xid(req->rq_reqmsg, min_xid);

	lustre_msg_set_status(req->rq_reqmsg, current_pid());

	rc = sptlrpc_req_refresh_ctx(req, -1);
	if (rc) {
		if (req->rq_err) {
			req->rq_status = rc;
			return 1;
		}
		spin_lock(&req->rq_lock);
		req->rq_wait_ctx = 1;
		spin_unlock(&req->rq_lock);
		return 0;
	}

	CDEBUG(D_RPCTRACE, "Sending RPC pname:cluuid:pid:xid:nid:opc %s:%s:%d:%llu:%s:%d\n",
	       current_comm(),
	       imp->imp_obd->obd_uuid.uuid,
	       lustre_msg_get_status(req->rq_reqmsg), req->rq_xid,
	       libcfs_nid2str(imp->imp_connection->c_peer.nid),
	       lustre_msg_get_opc(req->rq_reqmsg));

	rc = ptl_send_rpc(req, 0);
	if (rc == -ENOMEM) {
		spin_lock(&imp->imp_lock);
		if (!list_empty(&req->rq_list)) {
			list_del_init(&req->rq_list);
			atomic_dec(&req->rq_import->imp_inflight);
		}
		spin_unlock(&imp->imp_lock);
		ptlrpc_rqphase_move(req, RQ_PHASE_NEW);
		return rc;
	}
	if (rc) {
		DEBUG_REQ(D_HA, req, "send failed (%d); expect timeout", rc);
		spin_lock(&req->rq_lock);
		req->rq_net_err = 1;
		spin_unlock(&req->rq_lock);
		return rc;
	}
	return 0;
}

static inline int ptlrpc_set_producer(struct ptlrpc_request_set *set)
{
	int remaining, rc;

	LASSERT(set->set_producer);

	remaining = atomic_read(&set->set_remaining);

	/*
	 * populate the ->set_requests list with requests until we
	 * reach the maximum number of RPCs in flight for this set
	 */
	while (atomic_read(&set->set_remaining) < set->set_max_inflight) {
		rc = set->set_producer(set, set->set_producer_arg);
		if (rc == -ENOENT) {
			/* no more RPC to produce */
			set->set_producer     = NULL;
			set->set_producer_arg = NULL;
			return 0;
		}
	}

	return (atomic_read(&set->set_remaining) - remaining);
}

/**
 * this sends any unsent RPCs in \a set and returns 1 if all are sent
 * and no more replies are expected.
 * (it is possible to get less replies than requests sent e.g. due to timed out
 * requests or requests that we had trouble to send out)
 *
 * NOTE: This function contains a potential schedule point (cond_resched()).
 */
int ptlrpc_check_set(const struct lu_env *env, struct ptlrpc_request_set *set)
{
	struct list_head *tmp, *next;
	struct list_head comp_reqs;
	int force_timer_recalc = 0;

	if (atomic_read(&set->set_remaining) == 0)
		return 1;

	INIT_LIST_HEAD(&comp_reqs);
	list_for_each_safe(tmp, next, &set->set_requests) {
		struct ptlrpc_request *req =
			list_entry(tmp, struct ptlrpc_request, rq_set_chain);
		struct obd_import *imp = req->rq_import;
		int unregistered = 0;
		int rc = 0;

		/*
		 * This schedule point is mainly for the ptlrpcd caller of this
		 * function.  Most ptlrpc sets are not long-lived and unbounded
		 * in length, but at the least the set used by the ptlrpcd is.
		 * Since the processing time is unbounded, we need to insert an
		 * explicit schedule point to make the thread well-behaved.
		 */
		cond_resched();

		if (req->rq_phase == RQ_PHASE_NEW &&
		    ptlrpc_send_new_req(req)) {
			force_timer_recalc = 1;
		}

		/* delayed send - skip */
		if (req->rq_phase == RQ_PHASE_NEW && req->rq_sent)
			continue;

		/* delayed resend - skip */
		if (req->rq_phase == RQ_PHASE_RPC && req->rq_resend &&
		    req->rq_sent > ktime_get_real_seconds())
			continue;

		if (!(req->rq_phase == RQ_PHASE_RPC ||
		      req->rq_phase == RQ_PHASE_BULK ||
		      req->rq_phase == RQ_PHASE_INTERPRET ||
		      req->rq_phase == RQ_PHASE_UNREG_RPC ||
		      req->rq_phase == RQ_PHASE_UNREG_BULK ||
		      req->rq_phase == RQ_PHASE_COMPLETE)) {
			DEBUG_REQ(D_ERROR, req, "bad phase %x", req->rq_phase);
			LBUG();
		}

		if (req->rq_phase == RQ_PHASE_UNREG_RPC ||
		    req->rq_phase == RQ_PHASE_UNREG_BULK) {
			LASSERT(req->rq_next_phase != req->rq_phase);
			LASSERT(req->rq_next_phase != RQ_PHASE_UNDEFINED);

			if (req->rq_req_deadline &&
			    !OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_REQ_UNLINK))
				req->rq_req_deadline = 0;
			if (req->rq_reply_deadline &&
			    !OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_REPL_UNLINK))
				req->rq_reply_deadline = 0;
			if (req->rq_bulk_deadline &&
			    !OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_BULK_UNLINK))
				req->rq_bulk_deadline = 0;

			/*
			 * Skip processing until reply is unlinked. We
			 * can't return to pool before that and we can't
			 * call interpret before that. We need to make
			 * sure that all rdma transfers finished and will
			 * not corrupt any data.
			 */
			if (req->rq_phase == RQ_PHASE_UNREG_RPC &&
			    ptlrpc_client_recv_or_unlink(req))
				continue;
			if (req->rq_phase == RQ_PHASE_UNREG_BULK &&
			    ptlrpc_client_bulk_active(req))
				continue;

			/*
			 * Turn fail_loc off to prevent it from looping
			 * forever.
			 */
			if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_REPL_UNLINK)) {
				OBD_FAIL_CHECK_ORSET(OBD_FAIL_PTLRPC_LONG_REPL_UNLINK,
						     OBD_FAIL_ONCE);
			}
			if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_BULK_UNLINK)) {
				OBD_FAIL_CHECK_ORSET(OBD_FAIL_PTLRPC_LONG_BULK_UNLINK,
						     OBD_FAIL_ONCE);
			}

			/* Move to next phase if reply was successfully
			 * unlinked.
			 */
			ptlrpc_rqphase_move(req, req->rq_next_phase);
		}

		if (req->rq_phase == RQ_PHASE_COMPLETE) {
			list_move_tail(&req->rq_set_chain, &comp_reqs);
			continue;
		}

		if (req->rq_phase == RQ_PHASE_INTERPRET)
			goto interpret;

		/* Note that this also will start async reply unlink. */
		if (req->rq_net_err && !req->rq_timedout) {
			ptlrpc_expire_one_request(req, 1);

			/* Check if we still need to wait for unlink. */
			if (ptlrpc_client_recv_or_unlink(req) ||
			    ptlrpc_client_bulk_active(req))
				continue;
			/* If there is no need to resend, fail it now. */
			if (req->rq_no_resend) {
				if (req->rq_status == 0)
					req->rq_status = -EIO;
				ptlrpc_rqphase_move(req, RQ_PHASE_INTERPRET);
				goto interpret;
			} else {
				continue;
			}
		}

		if (req->rq_err) {
			spin_lock(&req->rq_lock);
			req->rq_replied = 0;
			spin_unlock(&req->rq_lock);
			if (req->rq_status == 0)
				req->rq_status = -EIO;
			ptlrpc_rqphase_move(req, RQ_PHASE_INTERPRET);
			goto interpret;
		}

		/*
		 * ptlrpc_set_wait->l_wait_event sets lwi_allow_intr
		 * so it sets rq_intr regardless of individual rpc
		 * timeouts. The synchronous IO waiting path sets
		 * rq_intr irrespective of whether ptlrpcd
		 * has seen a timeout.  Our policy is to only interpret
		 * interrupted rpcs after they have timed out, so we
		 * need to enforce that here.
		 */

		if (req->rq_intr && (req->rq_timedout || req->rq_waiting ||
				     req->rq_wait_ctx)) {
			req->rq_status = -EINTR;
			ptlrpc_rqphase_move(req, RQ_PHASE_INTERPRET);
			goto interpret;
		}

		if (req->rq_phase == RQ_PHASE_RPC) {
			if (req->rq_timedout || req->rq_resend ||
			    req->rq_waiting || req->rq_wait_ctx) {
				int status;

				if (!ptlrpc_unregister_reply(req, 1)) {
					ptlrpc_unregister_bulk(req, 1);
					continue;
				}

				spin_lock(&imp->imp_lock);
				if (ptlrpc_import_delay_req(imp, req,
							    &status)) {
					/*
					 * put on delay list - only if we wait
					 * recovery finished - before send
					 */
					list_del_init(&req->rq_list);
					list_add_tail(&req->rq_list,
						      &imp->imp_delayed_list);
					spin_unlock(&imp->imp_lock);
					continue;
				}

				if (status != 0) {
					req->rq_status = status;
					ptlrpc_rqphase_move(req,
							    RQ_PHASE_INTERPRET);
					spin_unlock(&imp->imp_lock);
					goto interpret;
				}
				if (ptlrpc_no_resend(req) &&
				    !req->rq_wait_ctx) {
					req->rq_status = -ENOTCONN;
					ptlrpc_rqphase_move(req,
							    RQ_PHASE_INTERPRET);
					spin_unlock(&imp->imp_lock);
					goto interpret;
				}

				list_del_init(&req->rq_list);
				list_add_tail(&req->rq_list,
					      &imp->imp_sending_list);

				spin_unlock(&imp->imp_lock);

				spin_lock(&req->rq_lock);
				req->rq_waiting = 0;
				spin_unlock(&req->rq_lock);

				if (req->rq_timedout || req->rq_resend) {
					/* This is re-sending anyway, let's mark req as resend. */
					spin_lock(&req->rq_lock);
					req->rq_resend = 1;
					spin_unlock(&req->rq_lock);
					if (req->rq_bulk &&
					    !ptlrpc_unregister_bulk(req, 1))
						continue;
				}
				/*
				 * rq_wait_ctx is only touched by ptlrpcd,
				 * so no lock is needed here.
				 */
				status = sptlrpc_req_refresh_ctx(req, -1);
				if (status) {
					if (req->rq_err) {
						req->rq_status = status;
						spin_lock(&req->rq_lock);
						req->rq_wait_ctx = 0;
						spin_unlock(&req->rq_lock);
						force_timer_recalc = 1;
					} else {
						spin_lock(&req->rq_lock);
						req->rq_wait_ctx = 1;
						spin_unlock(&req->rq_lock);
					}

					continue;
				} else {
					spin_lock(&req->rq_lock);
					req->rq_wait_ctx = 0;
					spin_unlock(&req->rq_lock);
				}

				rc = ptl_send_rpc(req, 0);
				if (rc == -ENOMEM) {
					spin_lock(&imp->imp_lock);
					if (!list_empty(&req->rq_list))
						list_del_init(&req->rq_list);
					spin_unlock(&imp->imp_lock);
					ptlrpc_rqphase_move(req, RQ_PHASE_NEW);
					continue;
				}
				if (rc) {
					DEBUG_REQ(D_HA, req,
						  "send failed: rc = %d", rc);
					force_timer_recalc = 1;
					spin_lock(&req->rq_lock);
					req->rq_net_err = 1;
					spin_unlock(&req->rq_lock);
					continue;
				}
				/* need to reset the timeout */
				force_timer_recalc = 1;
			}

			spin_lock(&req->rq_lock);

			if (ptlrpc_client_early(req)) {
				ptlrpc_at_recv_early_reply(req);
				spin_unlock(&req->rq_lock);
				continue;
			}

			/* Still waiting for a reply? */
			if (ptlrpc_client_recv(req)) {
				spin_unlock(&req->rq_lock);
				continue;
			}

			/* Did we actually receive a reply? */
			if (!ptlrpc_client_replied(req)) {
				spin_unlock(&req->rq_lock);
				continue;
			}

			spin_unlock(&req->rq_lock);

			/*
			 * unlink from net because we are going to
			 * swab in-place of reply buffer
			 */
			unregistered = ptlrpc_unregister_reply(req, 1);
			if (!unregistered)
				continue;

			req->rq_status = after_reply(req);
			if (req->rq_resend)
				continue;

			/*
			 * If there is no bulk associated with this request,
			 * then we're done and should let the interpreter
			 * process the reply. Similarly if the RPC returned
			 * an error, and therefore the bulk will never arrive.
			 */
			if (!req->rq_bulk || req->rq_status < 0) {
				ptlrpc_rqphase_move(req, RQ_PHASE_INTERPRET);
				goto interpret;
			}

			ptlrpc_rqphase_move(req, RQ_PHASE_BULK);
		}

		LASSERT(req->rq_phase == RQ_PHASE_BULK);
		if (ptlrpc_client_bulk_active(req))
			continue;

		if (req->rq_bulk->bd_failure) {
			/*
			 * The RPC reply arrived OK, but the bulk screwed
			 * up!  Dead weird since the server told us the RPC
			 * was good after getting the REPLY for her GET or
			 * the ACK for her PUT.
			 */
			DEBUG_REQ(D_ERROR, req, "bulk transfer failed");
			req->rq_status = -EIO;
		}

		ptlrpc_rqphase_move(req, RQ_PHASE_INTERPRET);

interpret:
		LASSERT(req->rq_phase == RQ_PHASE_INTERPRET);

		/*
		 * This moves to "unregistering" phase we need to wait for
		 * reply unlink.
		 */
		if (!unregistered && !ptlrpc_unregister_reply(req, 1)) {
			/* start async bulk unlink too */
			ptlrpc_unregister_bulk(req, 1);
			continue;
		}

		if (!ptlrpc_unregister_bulk(req, 1))
			continue;

		/* When calling interpret receive should already be finished. */
		LASSERT(!req->rq_receiving_reply);

		ptlrpc_req_interpret(env, req, req->rq_status);

		if (ptlrpcd_check_work(req)) {
			atomic_dec(&set->set_remaining);
			continue;
		}
		ptlrpc_rqphase_move(req, RQ_PHASE_COMPLETE);

		CDEBUG(req->rq_reqmsg ? D_RPCTRACE : 0,
		       "Completed RPC pname:cluuid:pid:xid:nid:opc %s:%s:%d:%llu:%s:%d\n",
		       current_comm(), imp->imp_obd->obd_uuid.uuid,
		       lustre_msg_get_status(req->rq_reqmsg), req->rq_xid,
		       libcfs_nid2str(imp->imp_connection->c_peer.nid),
		       lustre_msg_get_opc(req->rq_reqmsg));

		spin_lock(&imp->imp_lock);
		/*
		 * Request already may be not on sending or delaying list. This
		 * may happen in the case of marking it erroneous for the case
		 * ptlrpc_import_delay_req(req, status) find it impossible to
		 * allow sending this rpc and returns *status != 0.
		 */
		if (!list_empty(&req->rq_list)) {
			list_del_init(&req->rq_list);
			atomic_dec(&imp->imp_inflight);
		}
		list_del_init(&req->rq_unreplied_list);
		spin_unlock(&imp->imp_lock);

		atomic_dec(&set->set_remaining);
		wake_up_all(&imp->imp_recovery_waitq);

		if (set->set_producer) {
			/* produce a new request if possible */
			if (ptlrpc_set_producer(set) > 0)
				force_timer_recalc = 1;

			/*
			 * free the request that has just been completed
			 * in order not to pollute set->set_requests
			 */
			list_del_init(&req->rq_set_chain);
			spin_lock(&req->rq_lock);
			req->rq_set = NULL;
			req->rq_invalid_rqset = 0;
			spin_unlock(&req->rq_lock);

			/* record rq_status to compute the final status later */
			if (req->rq_status != 0)
				set->set_rc = req->rq_status;
			ptlrpc_req_finished(req);
		} else {
			list_move_tail(&req->rq_set_chain, &comp_reqs);
		}
	}

	/*
	 * move completed request at the head of list so it's easier for
	 * caller to find them
	 */
	list_splice(&comp_reqs, &set->set_requests);

	/* If we hit an error, we want to recover promptly. */
	return atomic_read(&set->set_remaining) == 0 || force_timer_recalc;
}
EXPORT_SYMBOL(ptlrpc_check_set);

/**
 * Time out request \a req. is \a async_unlink is set, that means do not wait
 * until LNet actually confirms network buffer unlinking.
 * Return 1 if we should give up further retrying attempts or 0 otherwise.
 */
int ptlrpc_expire_one_request(struct ptlrpc_request *req, int async_unlink)
{
	struct obd_import *imp = req->rq_import;
	int rc = 0;

	spin_lock(&req->rq_lock);
	req->rq_timedout = 1;
	spin_unlock(&req->rq_lock);

	DEBUG_REQ(D_WARNING, req, "Request sent has %s: [sent %lld/real %lld]",
		  req->rq_net_err ? "failed due to network error" :
		     ((req->rq_real_sent == 0 ||
		       req->rq_real_sent < req->rq_sent ||
		       req->rq_real_sent >= req->rq_deadline) ?
		      "timed out for sent delay" : "timed out for slow reply"),
		  (s64)req->rq_sent, (s64)req->rq_real_sent);

	if (imp && obd_debug_peer_on_timeout)
		LNetDebugPeer(imp->imp_connection->c_peer);

	ptlrpc_unregister_reply(req, async_unlink);
	ptlrpc_unregister_bulk(req, async_unlink);

	if (obd_dump_on_timeout)
		libcfs_debug_dumplog();

	if (!imp) {
		DEBUG_REQ(D_HA, req, "NULL import: already cleaned up?");
		return 1;
	}

	atomic_inc(&imp->imp_timeouts);

	/* The DLM server doesn't want recovery run on its imports. */
	if (imp->imp_dlm_fake)
		return 1;

	/*
	 * If this request is for recovery or other primordial tasks,
	 * then error it out here.
	 */
	if (req->rq_ctx_init || req->rq_ctx_fini ||
	    req->rq_send_state != LUSTRE_IMP_FULL ||
	    imp->imp_obd->obd_no_recov) {
		DEBUG_REQ(D_RPCTRACE, req, "err -110, sent_state=%s (now=%s)",
			  ptlrpc_import_state_name(req->rq_send_state),
			  ptlrpc_import_state_name(imp->imp_state));
		spin_lock(&req->rq_lock);
		req->rq_status = -ETIMEDOUT;
		req->rq_err = 1;
		spin_unlock(&req->rq_lock);
		return 1;
	}

	/*
	 * if a request can't be resent we can't wait for an answer after
	 * the timeout
	 */
	if (ptlrpc_no_resend(req)) {
		DEBUG_REQ(D_RPCTRACE, req, "TIMEOUT-NORESEND:");
		rc = 1;
	}

	ptlrpc_fail_import(imp, lustre_msg_get_conn_cnt(req->rq_reqmsg));

	return rc;
}

/**
 * Time out all uncompleted requests in request set pointed by \a data
 * Callback used when waiting on sets with l_wait_event.
 * Always returns 1.
 */
int ptlrpc_expired_set(void *data)
{
	struct ptlrpc_request_set *set = data;
	struct list_head *tmp;
	time64_t now = ktime_get_real_seconds();

	/* A timeout expired. See which reqs it applies to...  */
	list_for_each(tmp, &set->set_requests) {
		struct ptlrpc_request *req =
			list_entry(tmp, struct ptlrpc_request, rq_set_chain);

		/* don't expire request waiting for context */
		if (req->rq_wait_ctx)
			continue;

		/* Request in-flight? */
		if (!((req->rq_phase == RQ_PHASE_RPC &&
		       !req->rq_waiting && !req->rq_resend) ||
		      (req->rq_phase == RQ_PHASE_BULK)))
			continue;

		if (req->rq_timedout ||     /* already dealt with */
		    req->rq_deadline > now) /* not expired */
			continue;

		/*
		 * Deal with this guy. Do it asynchronously to not block
		 * ptlrpcd thread.
		 */
		ptlrpc_expire_one_request(req, 1);
	}

	/*
	 * When waiting for a whole set, we always break out of the
	 * sleep so we can recalculate the timeout, or enable interrupts
	 * if everyone's timed out.
	 */
	return 1;
}

/**
 * Sets rq_intr flag in \a req under spinlock.
 */
void ptlrpc_mark_interrupted(struct ptlrpc_request *req)
{
	spin_lock(&req->rq_lock);
	req->rq_intr = 1;
	spin_unlock(&req->rq_lock);
}
EXPORT_SYMBOL(ptlrpc_mark_interrupted);

/**
 * Interrupts (sets interrupted flag) all uncompleted requests in
 * a set \a data. Callback for l_wait_event for interruptible waits.
 */
static void ptlrpc_interrupted_set(void *data)
{
	struct ptlrpc_request_set *set = data;
	struct list_head *tmp;

	CDEBUG(D_RPCTRACE, "INTERRUPTED SET %p\n", set);

	list_for_each(tmp, &set->set_requests) {
		struct ptlrpc_request *req =
			list_entry(tmp, struct ptlrpc_request, rq_set_chain);

		if (req->rq_phase != RQ_PHASE_RPC &&
		    req->rq_phase != RQ_PHASE_UNREG_RPC)
			continue;

		ptlrpc_mark_interrupted(req);
	}
}

/**
 * Get the smallest timeout in the set; this does NOT set a timeout.
 */
int ptlrpc_set_next_timeout(struct ptlrpc_request_set *set)
{
	struct list_head *tmp;
	time64_t now = ktime_get_real_seconds();
	int timeout = 0;
	struct ptlrpc_request *req;
	time64_t deadline;

	list_for_each(tmp, &set->set_requests) {
		req = list_entry(tmp, struct ptlrpc_request, rq_set_chain);

		/* Request in-flight? */
		if (!(((req->rq_phase == RQ_PHASE_RPC) && !req->rq_waiting) ||
		      (req->rq_phase == RQ_PHASE_BULK) ||
		      (req->rq_phase == RQ_PHASE_NEW)))
			continue;

		/* Already timed out. */
		if (req->rq_timedout)
			continue;

		/* Waiting for ctx. */
		if (req->rq_wait_ctx)
			continue;

		if (req->rq_phase == RQ_PHASE_NEW)
			deadline = req->rq_sent;
		else if (req->rq_phase == RQ_PHASE_RPC && req->rq_resend)
			deadline = req->rq_sent;
		else
			deadline = req->rq_sent + req->rq_timeout;

		if (deadline <= now)    /* actually expired already */
			timeout = 1;    /* ASAP */
		else if (timeout == 0 || timeout > deadline - now)
			timeout = deadline - now;
	}
	return timeout;
}

/**
 * Send all unset request from the set and then wait until all
 * requests in the set complete (either get a reply, timeout, get an
 * error or otherwise be interrupted).
 * Returns 0 on success or error code otherwise.
 */
int ptlrpc_set_wait(struct ptlrpc_request_set *set)
{
	struct list_head *tmp;
	struct ptlrpc_request *req;
	struct l_wait_info lwi;
	int rc, timeout;

	if (set->set_producer)
		(void)ptlrpc_set_producer(set);
	else
		list_for_each(tmp, &set->set_requests) {
			req = list_entry(tmp, struct ptlrpc_request,
					 rq_set_chain);
			if (req->rq_phase == RQ_PHASE_NEW)
				(void)ptlrpc_send_new_req(req);
		}

	if (list_empty(&set->set_requests))
		return 0;

	do {
		timeout = ptlrpc_set_next_timeout(set);

		/*
		 * wait until all complete, interrupted, or an in-flight
		 * req times out
		 */
		CDEBUG(D_RPCTRACE, "set %p going to sleep for %d seconds\n",
		       set, timeout);

		if (timeout == 0 && !signal_pending(current))
			/*
			 * No requests are in-flight (ether timed out
			 * or delayed), so we can allow interrupts.
			 * We still want to block for a limited time,
			 * so we allow interrupts during the timeout.
			 */
			lwi = LWI_TIMEOUT_INTR_ALL(cfs_time_seconds(1),
						   ptlrpc_expired_set,
						   ptlrpc_interrupted_set, set);
		else
			/*
			 * At least one request is in flight, so no
			 * interrupts are allowed. Wait until all
			 * complete, or an in-flight req times out.
			 */
			lwi = LWI_TIMEOUT(cfs_time_seconds(timeout ? timeout : 1),
					  ptlrpc_expired_set, set);

		rc = l_wait_event(set->set_waitq, ptlrpc_check_set(NULL, set), &lwi);

		/*
		 * LU-769 - if we ignored the signal because it was already
		 * pending when we started, we need to handle it now or we risk
		 * it being ignored forever
		 */
		if (rc == -ETIMEDOUT && !lwi.lwi_allow_intr &&
		    signal_pending(current)) {
			sigset_t blocked_sigs =
					   cfs_block_sigsinv(LUSTRE_FATAL_SIGS);

			/*
			 * In fact we only interrupt for the "fatal" signals
			 * like SIGINT or SIGKILL. We still ignore less
			 * important signals since ptlrpc set is not easily
			 * reentrant from userspace again
			 */
			if (signal_pending(current))
				ptlrpc_interrupted_set(set);
			cfs_restore_sigs(blocked_sigs);
		}

		LASSERT(rc == 0 || rc == -EINTR || rc == -ETIMEDOUT);

		/*
		 * -EINTR => all requests have been flagged rq_intr so next
		 * check completes.
		 * -ETIMEDOUT => someone timed out.  When all reqs have
		 * timed out, signals are enabled allowing completion with
		 * EINTR.
		 * I don't really care if we go once more round the loop in
		 * the error cases -eeb.
		 */
		if (rc == 0 && atomic_read(&set->set_remaining) == 0) {
			list_for_each(tmp, &set->set_requests) {
				req = list_entry(tmp, struct ptlrpc_request,
						 rq_set_chain);
				spin_lock(&req->rq_lock);
				req->rq_invalid_rqset = 1;
				spin_unlock(&req->rq_lock);
			}
		}
	} while (rc != 0 || atomic_read(&set->set_remaining) != 0);

	LASSERT(atomic_read(&set->set_remaining) == 0);

	rc = set->set_rc; /* rq_status of already freed requests if any */
	list_for_each(tmp, &set->set_requests) {
		req = list_entry(tmp, struct ptlrpc_request, rq_set_chain);

		LASSERT(req->rq_phase == RQ_PHASE_COMPLETE);
		if (req->rq_status != 0)
			rc = req->rq_status;
	}

	if (set->set_interpret) {
		int (*interpreter)(struct ptlrpc_request_set *set, void *, int) =
			set->set_interpret;
		rc = interpreter(set, set->set_arg, rc);
	} else {
		struct ptlrpc_set_cbdata *cbdata, *n;
		int err;

		list_for_each_entry_safe(cbdata, n,
					 &set->set_cblist, psc_item) {
			list_del_init(&cbdata->psc_item);
			err = cbdata->psc_interpret(set, cbdata->psc_data, rc);
			if (err && !rc)
				rc = err;
			kfree(cbdata);
		}
	}

	return rc;
}
EXPORT_SYMBOL(ptlrpc_set_wait);

/**
 * Helper function for request freeing.
 * Called when request count reached zero and request needs to be freed.
 * Removes request from all sorts of sending/replay lists it might be on,
 * frees network buffers if any are present.
 * If \a locked is set, that means caller is already holding import imp_lock
 * and so we no longer need to reobtain it (for certain lists manipulations)
 */
static void __ptlrpc_free_req(struct ptlrpc_request *request, int locked)
{
	if (!request)
		return;
	LASSERT(!request->rq_srv_req);
	LASSERT(!request->rq_export);
	LASSERTF(!request->rq_receiving_reply, "req %p\n", request);
	LASSERTF(list_empty(&request->rq_list), "req %p\n", request);
	LASSERTF(list_empty(&request->rq_set_chain), "req %p\n", request);
	LASSERTF(!request->rq_replay, "req %p\n", request);

	req_capsule_fini(&request->rq_pill);

	/*
	 * We must take it off the imp_replay_list first.  Otherwise, we'll set
	 * request->rq_reqmsg to NULL while osc_close is dereferencing it.
	 */
	if (request->rq_import) {
		if (!locked)
			spin_lock(&request->rq_import->imp_lock);
		list_del_init(&request->rq_replay_list);
		list_del_init(&request->rq_unreplied_list);
		if (!locked)
			spin_unlock(&request->rq_import->imp_lock);
	}
	LASSERTF(list_empty(&request->rq_replay_list), "req %p\n", request);

	if (atomic_read(&request->rq_refcount) != 0) {
		DEBUG_REQ(D_ERROR, request,
			  "freeing request with nonzero refcount");
		LBUG();
	}

	if (request->rq_repbuf)
		sptlrpc_cli_free_repbuf(request);

	if (request->rq_import) {
		class_import_put(request->rq_import);
		request->rq_import = NULL;
	}
	if (request->rq_bulk)
		ptlrpc_free_bulk(request->rq_bulk);

	if (request->rq_reqbuf || request->rq_clrbuf)
		sptlrpc_cli_free_reqbuf(request);

	if (request->rq_cli_ctx)
		sptlrpc_req_put_ctx(request, !locked);

	if (request->rq_pool)
		__ptlrpc_free_req_to_pool(request);
	else
		ptlrpc_request_cache_free(request);
}

/**
 * Helper function
 * Drops one reference count for request \a request.
 * \a locked set indicates that caller holds import imp_lock.
 * Frees the request when reference count reaches zero.
 */
static int __ptlrpc_req_finished(struct ptlrpc_request *request, int locked)
{
	if (!request)
		return 1;

	if (request == LP_POISON ||
	    request->rq_reqmsg == LP_POISON) {
		CERROR("dereferencing freed request (bug 575)\n");
		LBUG();
		return 1;
	}

	DEBUG_REQ(D_INFO, request, "refcount now %u",
		  atomic_read(&request->rq_refcount) - 1);

	if (atomic_dec_and_test(&request->rq_refcount)) {
		__ptlrpc_free_req(request, locked);
		return 1;
	}

	return 0;
}

/**
 * Drops one reference count for a request.
 */
void ptlrpc_req_finished(struct ptlrpc_request *request)
{
	__ptlrpc_req_finished(request, 0);
}
EXPORT_SYMBOL(ptlrpc_req_finished);

/**
 * Returns xid of a \a request
 */
__u64 ptlrpc_req_xid(struct ptlrpc_request *request)
{
	return request->rq_xid;
}
EXPORT_SYMBOL(ptlrpc_req_xid);

/**
 * Disengage the client's reply buffer from the network
 * NB does _NOT_ unregister any client-side bulk.
 * IDEMPOTENT, but _not_ safe against concurrent callers.
 * The request owner (i.e. the thread doing the I/O) must call...
 * Returns 0 on success or 1 if unregistering cannot be made.
 */
static int ptlrpc_unregister_reply(struct ptlrpc_request *request, int async)
{
	int rc;
	wait_queue_head_t *wq;
	struct l_wait_info lwi;

	/* Might sleep. */
	LASSERT(!in_interrupt());

	/* Let's setup deadline for reply unlink. */
	if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_REPL_UNLINK) &&
	    async && request->rq_reply_deadline == 0 && cfs_fail_val == 0)
		request->rq_reply_deadline =
			ktime_get_real_seconds() + LONG_UNLINK;

	/* Nothing left to do. */
	if (!ptlrpc_client_recv_or_unlink(request))
		return 1;

	LNetMDUnlink(request->rq_reply_md_h);

	/* Let's check it once again. */
	if (!ptlrpc_client_recv_or_unlink(request))
		return 1;

	/* Move to "Unregistering" phase as reply was not unlinked yet. */
	ptlrpc_rqphase_move(request, RQ_PHASE_UNREG_RPC);

	/* Do not wait for unlink to finish. */
	if (async)
		return 0;

	/*
	 * We have to l_wait_event() whatever the result, to give liblustre
	 * a chance to run reply_in_callback(), and to make sure we've
	 * unlinked before returning a req to the pool.
	 */
	if (request->rq_set)
		wq = &request->rq_set->set_waitq;
	else
		wq = &request->rq_reply_waitq;

	for (;;) {
		/*
		 * Network access will complete in finite time but the HUGE
		 * timeout lets us CWARN for visibility of sluggish NALs
		 */
		lwi = LWI_TIMEOUT_INTERVAL(cfs_time_seconds(LONG_UNLINK),
					   cfs_time_seconds(1), NULL, NULL);
		rc = l_wait_event(*wq, !ptlrpc_client_recv_or_unlink(request),
				  &lwi);
		if (rc == 0) {
			ptlrpc_rqphase_move(request, request->rq_next_phase);
			return 1;
		}

		LASSERT(rc == -ETIMEDOUT);
		DEBUG_REQ(D_WARNING, request,
			  "Unexpectedly long timeout receiving_reply=%d req_ulinked=%d reply_unlinked=%d",
			  request->rq_receiving_reply,
			  request->rq_req_unlinked,
			  request->rq_reply_unlinked);
	}
	return 0;
}

static void ptlrpc_free_request(struct ptlrpc_request *req)
{
	spin_lock(&req->rq_lock);
	req->rq_replay = 0;
	spin_unlock(&req->rq_lock);

	if (req->rq_commit_cb)
		req->rq_commit_cb(req);
	list_del_init(&req->rq_replay_list);

	__ptlrpc_req_finished(req, 1);
}

/**
 * the request is committed and dropped from the replay list of its import
 */
void ptlrpc_request_committed(struct ptlrpc_request *req, int force)
{
	struct obd_import	*imp = req->rq_import;

	spin_lock(&imp->imp_lock);
	if (list_empty(&req->rq_replay_list)) {
		spin_unlock(&imp->imp_lock);
		return;
	}

	if (force || req->rq_transno <= imp->imp_peer_committed_transno)
		ptlrpc_free_request(req);

	spin_unlock(&imp->imp_lock);
}
EXPORT_SYMBOL(ptlrpc_request_committed);

/**
 * Iterates through replay_list on import and prunes
 * all requests have transno smaller than last_committed for the
 * import and don't have rq_replay set.
 * Since requests are sorted in transno order, stops when meeting first
 * transno bigger than last_committed.
 * caller must hold imp->imp_lock
 */
void ptlrpc_free_committed(struct obd_import *imp)
{
	struct ptlrpc_request *req, *saved;
	struct ptlrpc_request *last_req = NULL; /* temporary fire escape */
	bool skip_committed_list = true;

	assert_spin_locked(&imp->imp_lock);

	if (imp->imp_peer_committed_transno == imp->imp_last_transno_checked &&
	    imp->imp_generation == imp->imp_last_generation_checked) {
		CDEBUG(D_INFO, "%s: skip recheck: last_committed %llu\n",
		       imp->imp_obd->obd_name, imp->imp_peer_committed_transno);
		return;
	}
	CDEBUG(D_RPCTRACE, "%s: committing for last_committed %llu gen %d\n",
	       imp->imp_obd->obd_name, imp->imp_peer_committed_transno,
	       imp->imp_generation);

	if (imp->imp_generation != imp->imp_last_generation_checked ||
	    !imp->imp_last_transno_checked)
		skip_committed_list = false;

	imp->imp_last_transno_checked = imp->imp_peer_committed_transno;
	imp->imp_last_generation_checked = imp->imp_generation;

	list_for_each_entry_safe(req, saved, &imp->imp_replay_list,
				 rq_replay_list) {
		/* XXX ok to remove when 1357 resolved - rread 05/29/03  */
		LASSERT(req != last_req);
		last_req = req;

		if (req->rq_transno == 0) {
			DEBUG_REQ(D_EMERG, req, "zero transno during replay");
			LBUG();
		}
		if (req->rq_import_generation < imp->imp_generation) {
			DEBUG_REQ(D_RPCTRACE, req, "free request with old gen");
			goto free_req;
		}

		/* not yet committed */
		if (req->rq_transno > imp->imp_peer_committed_transno) {
			DEBUG_REQ(D_RPCTRACE, req, "stopping search");
			break;
		}

		if (req->rq_replay) {
			DEBUG_REQ(D_RPCTRACE, req, "keeping (FL_REPLAY)");
			list_move_tail(&req->rq_replay_list,
				       &imp->imp_committed_list);
			continue;
		}

		DEBUG_REQ(D_INFO, req, "commit (last_committed %llu)",
			  imp->imp_peer_committed_transno);
free_req:
		ptlrpc_free_request(req);
	}
	if (skip_committed_list)
		return;

	list_for_each_entry_safe(req, saved, &imp->imp_committed_list,
				 rq_replay_list) {
		LASSERT(req->rq_transno != 0);
		if (req->rq_import_generation < imp->imp_generation) {
			DEBUG_REQ(D_RPCTRACE, req, "free stale open request");
			ptlrpc_free_request(req);
		} else if (!req->rq_replay) {
			DEBUG_REQ(D_RPCTRACE, req, "free closed open request");
			ptlrpc_free_request(req);
		}
	}
}

/**
 * Schedule previously sent request for resend.
 * For bulk requests we assign new xid (to avoid problems with
 * lost replies and therefore several transfers landing into same buffer
 * from different sending attempts).
 */
void ptlrpc_resend_req(struct ptlrpc_request *req)
{
	DEBUG_REQ(D_HA, req, "going to resend");
	spin_lock(&req->rq_lock);

	/*
	 * Request got reply but linked to the import list still.
	 * Let ptlrpc_check_set() to process it.
	 */
	if (ptlrpc_client_replied(req)) {
		spin_unlock(&req->rq_lock);
		DEBUG_REQ(D_HA, req, "it has reply, so skip it");
		return;
	}

	lustre_msg_set_handle(req->rq_reqmsg, &(struct lustre_handle){ 0 });
	req->rq_status = -EAGAIN;

	req->rq_resend = 1;
	req->rq_net_err = 0;
	req->rq_timedout = 0;
	ptlrpc_client_wake_req(req);
	spin_unlock(&req->rq_lock);
}

/**
 * Grab additional reference on a request \a req
 */
struct ptlrpc_request *ptlrpc_request_addref(struct ptlrpc_request *req)
{
	atomic_inc(&req->rq_refcount);
	return req;
}
EXPORT_SYMBOL(ptlrpc_request_addref);

/**
 * Add a request to import replay_list.
 * Must be called under imp_lock
 */
void ptlrpc_retain_replayable_request(struct ptlrpc_request *req,
				      struct obd_import *imp)
{
	struct list_head *tmp;

	assert_spin_locked(&imp->imp_lock);

	if (req->rq_transno == 0) {
		DEBUG_REQ(D_EMERG, req, "saving request with zero transno");
		LBUG();
	}

	/*
	 * clear this for new requests that were resent as well
	 * as resent replayed requests.
	 */
	lustre_msg_clear_flags(req->rq_reqmsg, MSG_RESENT);

	/* don't re-add requests that have been replayed */
	if (!list_empty(&req->rq_replay_list))
		return;

	lustre_msg_add_flags(req->rq_reqmsg, MSG_REPLAY);

	spin_lock(&req->rq_lock);
	req->rq_resend = 0;
	spin_unlock(&req->rq_lock);

	LASSERT(imp->imp_replayable);
	/* Balanced in ptlrpc_free_committed, usually. */
	ptlrpc_request_addref(req);
	list_for_each_prev(tmp, &imp->imp_replay_list) {
		struct ptlrpc_request *iter =
			list_entry(tmp, struct ptlrpc_request, rq_replay_list);

		/*
		 * We may have duplicate transnos if we create and then
		 * open a file, or for closes retained if to match creating
		 * opens, so use req->rq_xid as a secondary key.
		 * (See bugs 684, 685, and 428.)
		 * XXX no longer needed, but all opens need transnos!
		 */
		if (iter->rq_transno > req->rq_transno)
			continue;

		if (iter->rq_transno == req->rq_transno) {
			LASSERT(iter->rq_xid != req->rq_xid);
			if (iter->rq_xid > req->rq_xid)
				continue;
		}

		list_add(&req->rq_replay_list, &iter->rq_replay_list);
		return;
	}

	list_add(&req->rq_replay_list, &imp->imp_replay_list);
}

/**
 * Send request and wait until it completes.
 * Returns request processing status.
 */
int ptlrpc_queue_wait(struct ptlrpc_request *req)
{
	struct ptlrpc_request_set *set;
	int rc;

	LASSERT(!req->rq_set);
	LASSERT(!req->rq_receiving_reply);

	set = ptlrpc_prep_set();
	if (!set) {
		CERROR("cannot allocate ptlrpc set: rc = %d\n", -ENOMEM);
		return -ENOMEM;
	}

	/* for distributed debugging */
	lustre_msg_set_status(req->rq_reqmsg, current_pid());

	/* add a ref for the set (see comment in ptlrpc_set_add_req) */
	ptlrpc_request_addref(req);
	ptlrpc_set_add_req(set, req);
	rc = ptlrpc_set_wait(set);
	ptlrpc_set_destroy(set);

	return rc;
}
EXPORT_SYMBOL(ptlrpc_queue_wait);

/**
 * Callback used for replayed requests reply processing.
 * In case of successful reply calls registered request replay callback.
 * In case of error restart replay process.
 */
static int ptlrpc_replay_interpret(const struct lu_env *env,
				   struct ptlrpc_request *req,
				   void *data, int rc)
{
	struct ptlrpc_replay_async_args *aa = data;
	struct obd_import *imp = req->rq_import;

	atomic_dec(&imp->imp_replay_inflight);

	/*
	 * Note: if it is bulk replay (MDS-MDS replay), then even if
	 * server got the request, but bulk transfer timeout, let's
	 * replay the bulk req again
	 */
	if (!ptlrpc_client_replied(req) ||
	    (req->rq_bulk &&
	     lustre_msg_get_status(req->rq_repmsg) == -ETIMEDOUT)) {
		DEBUG_REQ(D_ERROR, req, "request replay timed out.\n");
		rc = -ETIMEDOUT;
		goto out;
	}

	if (lustre_msg_get_type(req->rq_repmsg) == PTL_RPC_MSG_ERR &&
	    (lustre_msg_get_status(req->rq_repmsg) == -ENOTCONN ||
	     lustre_msg_get_status(req->rq_repmsg) == -ENODEV)) {
		rc = lustre_msg_get_status(req->rq_repmsg);
		goto out;
	}

	/** VBR: check version failure */
	if (lustre_msg_get_status(req->rq_repmsg) == -EOVERFLOW) {
		/** replay was failed due to version mismatch */
		DEBUG_REQ(D_WARNING, req, "Version mismatch during replay\n");
		spin_lock(&imp->imp_lock);
		imp->imp_vbr_failed = 1;
		imp->imp_no_lock_replay = 1;
		spin_unlock(&imp->imp_lock);
		lustre_msg_set_status(req->rq_repmsg, aa->praa_old_status);
	} else {
		/** The transno had better not change over replay. */
		LASSERTF(lustre_msg_get_transno(req->rq_reqmsg) ==
			 lustre_msg_get_transno(req->rq_repmsg) ||
			 lustre_msg_get_transno(req->rq_repmsg) == 0,
			 "%#llx/%#llx\n",
			 lustre_msg_get_transno(req->rq_reqmsg),
			 lustre_msg_get_transno(req->rq_repmsg));
	}

	spin_lock(&imp->imp_lock);
	/** if replays by version then gap occur on server, no trust to locks */
	if (lustre_msg_get_flags(req->rq_repmsg) & MSG_VERSION_REPLAY)
		imp->imp_no_lock_replay = 1;
	imp->imp_last_replay_transno = lustre_msg_get_transno(req->rq_reqmsg);
	spin_unlock(&imp->imp_lock);
	LASSERT(imp->imp_last_replay_transno);

	/* transaction number shouldn't be bigger than the latest replayed */
	if (req->rq_transno > lustre_msg_get_transno(req->rq_reqmsg)) {
		DEBUG_REQ(D_ERROR, req,
			  "Reported transno %llu is bigger than the replayed one: %llu",
			  req->rq_transno,
			  lustre_msg_get_transno(req->rq_reqmsg));
		rc = -EINVAL;
		goto out;
	}

	DEBUG_REQ(D_HA, req, "got rep");

	/* let the callback do fixups, possibly including in the request */
	if (req->rq_replay_cb)
		req->rq_replay_cb(req);

	if (ptlrpc_client_replied(req) &&
	    lustre_msg_get_status(req->rq_repmsg) != aa->praa_old_status) {
		DEBUG_REQ(D_ERROR, req, "status %d, old was %d",
			  lustre_msg_get_status(req->rq_repmsg),
			  aa->praa_old_status);
	} else {
		/* Put it back for re-replay. */
		lustre_msg_set_status(req->rq_repmsg, aa->praa_old_status);
	}

	/*
	 * Errors while replay can set transno to 0, but
	 * imp_last_replay_transno shouldn't be set to 0 anyway
	 */
	if (req->rq_transno == 0)
		CERROR("Transno is 0 during replay!\n");

	/* continue with recovery */
	rc = ptlrpc_import_recovery_state_machine(imp);
 out:
	req->rq_send_state = aa->praa_old_state;

	if (rc != 0)
		/* this replay failed, so restart recovery */
		ptlrpc_connect_import(imp);

	return rc;
}

/**
 * Prepares and queues request for replay.
 * Adds it to ptlrpcd queue for actual sending.
 * Returns 0 on success.
 */
int ptlrpc_replay_req(struct ptlrpc_request *req)
{
	struct ptlrpc_replay_async_args *aa;

	LASSERT(req->rq_import->imp_state == LUSTRE_IMP_REPLAY);

	LASSERT(sizeof(*aa) <= sizeof(req->rq_async_args));
	aa = ptlrpc_req_async_args(req);
	memset(aa, 0, sizeof(*aa));

	/* Prepare request to be resent with ptlrpcd */
	aa->praa_old_state = req->rq_send_state;
	req->rq_send_state = LUSTRE_IMP_REPLAY;
	req->rq_phase = RQ_PHASE_NEW;
	req->rq_next_phase = RQ_PHASE_UNDEFINED;
	if (req->rq_repmsg)
		aa->praa_old_status = lustre_msg_get_status(req->rq_repmsg);
	req->rq_status = 0;
	req->rq_interpret_reply = ptlrpc_replay_interpret;
	/* Readjust the timeout for current conditions */
	ptlrpc_at_set_req_timeout(req);

	/*
	 * Tell server the net_latency, so the server can calculate how long
	 * it should wait for next replay
	 */
	lustre_msg_set_service_time(req->rq_reqmsg,
				    ptlrpc_at_get_net_latency(req));
	DEBUG_REQ(D_HA, req, "REPLAY");

	atomic_inc(&req->rq_import->imp_replay_inflight);
	ptlrpc_request_addref(req); /* ptlrpcd needs a ref */

	ptlrpcd_add_req(req);
	return 0;
}

/**
 * Aborts all in-flight request on import \a imp sending and delayed lists
 */
void ptlrpc_abort_inflight(struct obd_import *imp)
{
	struct list_head *tmp, *n;

	/*
	 * Make sure that no new requests get processed for this import.
	 * ptlrpc_{queue,set}_wait must (and does) hold imp_lock while testing
	 * this flag and then putting requests on sending_list or delayed_list.
	 */
	spin_lock(&imp->imp_lock);

	/*
	 * XXX locking?  Maybe we should remove each request with the list
	 * locked?  Also, how do we know if the requests on the list are
	 * being freed at this time?
	 */
	list_for_each_safe(tmp, n, &imp->imp_sending_list) {
		struct ptlrpc_request *req =
			list_entry(tmp, struct ptlrpc_request, rq_list);

		DEBUG_REQ(D_RPCTRACE, req, "inflight");

		spin_lock(&req->rq_lock);
		if (req->rq_import_generation < imp->imp_generation) {
			req->rq_err = 1;
			req->rq_status = -EIO;
			ptlrpc_client_wake_req(req);
		}
		spin_unlock(&req->rq_lock);
	}

	list_for_each_safe(tmp, n, &imp->imp_delayed_list) {
		struct ptlrpc_request *req =
			list_entry(tmp, struct ptlrpc_request, rq_list);

		DEBUG_REQ(D_RPCTRACE, req, "aborting waiting req");

		spin_lock(&req->rq_lock);
		if (req->rq_import_generation < imp->imp_generation) {
			req->rq_err = 1;
			req->rq_status = -EIO;
			ptlrpc_client_wake_req(req);
		}
		spin_unlock(&req->rq_lock);
	}

	/*
	 * Last chance to free reqs left on the replay list, but we
	 * will still leak reqs that haven't committed.
	 */
	if (imp->imp_replayable)
		ptlrpc_free_committed(imp);

	spin_unlock(&imp->imp_lock);
}

/**
 * Abort all uncompleted requests in request set \a set
 */
void ptlrpc_abort_set(struct ptlrpc_request_set *set)
{
	struct list_head *tmp, *pos;

	list_for_each_safe(pos, tmp, &set->set_requests) {
		struct ptlrpc_request *req =
			list_entry(pos, struct ptlrpc_request, rq_set_chain);

		spin_lock(&req->rq_lock);
		if (req->rq_phase != RQ_PHASE_RPC) {
			spin_unlock(&req->rq_lock);
			continue;
		}

		req->rq_err = 1;
		req->rq_status = -EINTR;
		ptlrpc_client_wake_req(req);
		spin_unlock(&req->rq_lock);
	}
}

static __u64 ptlrpc_last_xid;
static spinlock_t ptlrpc_last_xid_lock;

/**
 * Initialize the XID for the node.  This is common among all requests on
 * this node, and only requires the property that it is monotonically
 * increasing.  It does not need to be sequential.  Since this is also used
 * as the RDMA match bits, it is important that a single client NOT have
 * the same match bits for two different in-flight requests, hence we do
 * NOT want to have an XID per target or similar.
 *
 * To avoid an unlikely collision between match bits after a client reboot
 * (which would deliver old data into the wrong RDMA buffer) initialize
 * the XID based on the current time, assuming a maximum RPC rate of 1M RPC/s.
 * If the time is clearly incorrect, we instead use a 62-bit random number.
 * In the worst case the random number will overflow 1M RPCs per second in
 * 9133 years, or permutations thereof.
 */
#define YEAR_2004 (1ULL << 30)
void ptlrpc_init_xid(void)
{
	time64_t now = ktime_get_real_seconds();

	spin_lock_init(&ptlrpc_last_xid_lock);
	if (now < YEAR_2004) {
		cfs_get_random_bytes(&ptlrpc_last_xid, sizeof(ptlrpc_last_xid));
		ptlrpc_last_xid >>= 2;
		ptlrpc_last_xid |= (1ULL << 61);
	} else {
		ptlrpc_last_xid = (__u64)now << 20;
	}

	/* Always need to be aligned to a power-of-two for multi-bulk BRW */
	CLASSERT(((PTLRPC_BULK_OPS_COUNT - 1) & PTLRPC_BULK_OPS_COUNT) == 0);
	ptlrpc_last_xid &= PTLRPC_BULK_OPS_MASK;
}

/**
 * Increase xid and returns resulting new value to the caller.
 *
 * Multi-bulk BRW RPCs consume multiple XIDs for each bulk transfer, starting
 * at the returned xid, up to xid + PTLRPC_BULK_OPS_COUNT - 1. The BRW RPC
 * itself uses the last bulk xid needed, so the server can determine the
 * the number of bulk transfers from the RPC XID and a bitmask.  The starting
 * xid must align to a power-of-two value.
 *
 * This is assumed to be true due to the initial ptlrpc_last_xid
 * value also being initialized to a power-of-two value. LU-1431
 */
__u64 ptlrpc_next_xid(void)
{
	__u64 next;

	spin_lock(&ptlrpc_last_xid_lock);
	next = ptlrpc_last_xid + PTLRPC_BULK_OPS_COUNT;
	ptlrpc_last_xid = next;
	spin_unlock(&ptlrpc_last_xid_lock);

	return next;
}

/**
 * If request has a new allocated XID (new request or EINPROGRESS resend),
 * use this XID as matchbits of bulk, otherwise allocate a new matchbits for
 * request to ensure previous bulk fails and avoid problems with lost replies
 * and therefore several transfers landing into the same buffer from different
 * sending attempts.
 */
void ptlrpc_set_bulk_mbits(struct ptlrpc_request *req)
{
	struct ptlrpc_bulk_desc *bd = req->rq_bulk;

	LASSERT(bd);

	if (!req->rq_resend) {
		/* this request has a new xid, just use it as bulk matchbits */
		req->rq_mbits = req->rq_xid;

	} else { /* needs to generate a new matchbits for resend */
		u64 old_mbits = req->rq_mbits;

		if ((bd->bd_import->imp_connect_data.ocd_connect_flags &
		     OBD_CONNECT_BULK_MBITS)) {
			req->rq_mbits = ptlrpc_next_xid();
		} else {
			/* old version transfers rq_xid to peer as matchbits */
			req->rq_mbits = ptlrpc_next_xid();
			req->rq_xid = req->rq_mbits;
		}

		CDEBUG(D_HA, "resend bulk old x%llu new x%llu\n",
		       old_mbits, req->rq_mbits);
	}

	/*
	 * For multi-bulk RPCs, rq_mbits is the last mbits needed for bulks so
	 * that server can infer the number of bulks that were prepared,
	 * see LU-1431
	 */
	req->rq_mbits += ((bd->bd_iov_count + LNET_MAX_IOV - 1) /
			  LNET_MAX_IOV) - 1;
}

/**
 * Get a glimpse at what next xid value might have been.
 * Returns possible next xid.
 */
__u64 ptlrpc_sample_next_xid(void)
{
#if BITS_PER_LONG == 32
	/* need to avoid possible word tearing on 32-bit systems */
	__u64 next;

	spin_lock(&ptlrpc_last_xid_lock);
	next = ptlrpc_last_xid + PTLRPC_BULK_OPS_COUNT;
	spin_unlock(&ptlrpc_last_xid_lock);

	return next;
#else
	/* No need to lock, since returned value is racy anyways */
	return ptlrpc_last_xid + PTLRPC_BULK_OPS_COUNT;
#endif
}
EXPORT_SYMBOL(ptlrpc_sample_next_xid);

/**
 * Functions for operating ptlrpc workers.
 *
 * A ptlrpc work is a function which will be running inside ptlrpc context.
 * The callback shouldn't sleep otherwise it will block that ptlrpcd thread.
 *
 * 1. after a work is created, it can be used many times, that is:
 *	 handler = ptlrpcd_alloc_work();
 *	 ptlrpcd_queue_work();
 *
 *    queue it again when necessary:
 *	 ptlrpcd_queue_work();
 *	 ptlrpcd_destroy_work();
 * 2. ptlrpcd_queue_work() can be called by multiple processes meanwhile, but
 *    it will only be queued once in any time. Also as its name implies, it may
 *    have delay before it really runs by ptlrpcd thread.
 */
struct ptlrpc_work_async_args {
	int (*cb)(const struct lu_env *, void *);
	void *cbdata;
};

static void ptlrpcd_add_work_req(struct ptlrpc_request *req)
{
	/* re-initialize the req */
	req->rq_timeout		= obd_timeout;
	req->rq_sent		= ktime_get_real_seconds();
	req->rq_deadline	= req->rq_sent + req->rq_timeout;
	req->rq_phase		= RQ_PHASE_INTERPRET;
	req->rq_next_phase	= RQ_PHASE_COMPLETE;
	req->rq_xid		= ptlrpc_next_xid();
	req->rq_import_generation = req->rq_import->imp_generation;

	ptlrpcd_add_req(req);
}

static int work_interpreter(const struct lu_env *env,
			    struct ptlrpc_request *req, void *data, int rc)
{
	struct ptlrpc_work_async_args *arg = data;

	LASSERT(ptlrpcd_check_work(req));

	rc = arg->cb(env, arg->cbdata);

	list_del_init(&req->rq_set_chain);
	req->rq_set = NULL;

	if (atomic_dec_return(&req->rq_refcount) > 1) {
		atomic_set(&req->rq_refcount, 2);
		ptlrpcd_add_work_req(req);
	}
	return rc;
}

static int worker_format;

static int ptlrpcd_check_work(struct ptlrpc_request *req)
{
	return req->rq_pill.rc_fmt == (void *)&worker_format;
}

/**
 * Create a work for ptlrpc.
 */
void *ptlrpcd_alloc_work(struct obd_import *imp,
			 int (*cb)(const struct lu_env *, void *), void *cbdata)
{
	struct ptlrpc_request	 *req = NULL;
	struct ptlrpc_work_async_args *args;

	might_sleep();

	if (!cb)
		return ERR_PTR(-EINVAL);

	/* copy some code from deprecated fakereq. */
	req = ptlrpc_request_cache_alloc(GFP_NOFS);
	if (!req) {
		CERROR("ptlrpc: run out of memory!\n");
		return ERR_PTR(-ENOMEM);
	}

	ptlrpc_cli_req_init(req);

	req->rq_send_state = LUSTRE_IMP_FULL;
	req->rq_type = PTL_RPC_MSG_REQUEST;
	req->rq_import = class_import_get(imp);
	req->rq_interpret_reply = work_interpreter;
	/* don't want reply */
	req->rq_no_delay = 1;
	req->rq_no_resend = 1;
	req->rq_pill.rc_fmt = (void *)&worker_format;

	CLASSERT(sizeof(*args) <= sizeof(req->rq_async_args));
	args = ptlrpc_req_async_args(req);
	args->cb = cb;
	args->cbdata = cbdata;

	return req;
}
EXPORT_SYMBOL(ptlrpcd_alloc_work);

void ptlrpcd_destroy_work(void *handler)
{
	struct ptlrpc_request *req = handler;

	if (req)
		ptlrpc_req_finished(req);
}
EXPORT_SYMBOL(ptlrpcd_destroy_work);

int ptlrpcd_queue_work(void *handler)
{
	struct ptlrpc_request *req = handler;

	/*
	 * Check if the req is already being queued.
	 *
	 * Here comes a trick: it lacks a way of checking if a req is being
	 * processed reliably in ptlrpc. Here I have to use refcount of req
	 * for this purpose. This is okay because the caller should use this
	 * req as opaque data. - Jinshan
	 */
	LASSERT(atomic_read(&req->rq_refcount) > 0);
	if (atomic_inc_return(&req->rq_refcount) == 2)
		ptlrpcd_add_work_req(req);
	return 0;
}
EXPORT_SYMBOL(ptlrpcd_queue_work);
