/*
 * Copyright(c) 2015 - 2017 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/dmapool.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/uio.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mmu_context.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/string.h>

#include "hfi.h"
#include "sdma.h"
#include "mmu_rb.h"
#include "user_sdma.h"
#include "verbs.h"  /* for the headers */
#include "common.h" /* for struct hfi1_tid_info */
#include "trace.h"

static uint hfi1_sdma_comp_ring_size = 128;
module_param_named(sdma_comp_size, hfi1_sdma_comp_ring_size, uint, S_IRUGO);
MODULE_PARM_DESC(sdma_comp_size, "Size of User SDMA completion ring. Default: 128");

static unsigned initial_pkt_count = 8;

static int user_sdma_send_pkts(struct user_sdma_request *req,
			       unsigned maxpkts);
static void user_sdma_txreq_cb(struct sdma_txreq *txreq, int status);
static inline void pq_update(struct hfi1_user_sdma_pkt_q *pq);
static void user_sdma_free_request(struct user_sdma_request *req, bool unpin);
static int pin_vector_pages(struct user_sdma_request *req,
			    struct user_sdma_iovec *iovec);
static void unpin_vector_pages(struct mm_struct *mm, struct page **pages,
			       unsigned start, unsigned npages);
static int check_header_template(struct user_sdma_request *req,
				 struct hfi1_pkt_header *hdr, u32 lrhlen,
				 u32 datalen);
static int set_txreq_header(struct user_sdma_request *req,
			    struct user_sdma_txreq *tx, u32 datalen);
static int set_txreq_header_ahg(struct user_sdma_request *req,
				struct user_sdma_txreq *tx, u32 len);
static inline void set_comp_state(struct hfi1_user_sdma_pkt_q *pq,
				  struct hfi1_user_sdma_comp_q *cq,
				  u16 idx, enum hfi1_sdma_comp_state state,
				  int ret);
static inline u32 set_pkt_bth_psn(__be32 bthpsn, u8 expct, u32 frags);
static inline u32 get_lrh_len(struct hfi1_pkt_header, u32 len);

static int defer_packet_queue(
	struct sdma_engine *sde,
	struct iowait *wait,
	struct sdma_txreq *txreq,
	uint seq,
	bool pkts_sent);
static void activate_packet_queue(struct iowait *wait, int reason);
static bool sdma_rb_filter(struct mmu_rb_node *node, unsigned long addr,
			   unsigned long len);
static int sdma_rb_insert(void *arg, struct mmu_rb_node *mnode);
static int sdma_rb_evict(void *arg, struct mmu_rb_node *mnode,
			 void *arg2, bool *stop);
static void sdma_rb_remove(void *arg, struct mmu_rb_node *mnode);
static int sdma_rb_invalidate(void *arg, struct mmu_rb_node *mnode);

static struct mmu_rb_ops sdma_rb_ops = {
	.filter = sdma_rb_filter,
	.insert = sdma_rb_insert,
	.evict = sdma_rb_evict,
	.remove = sdma_rb_remove,
	.invalidate = sdma_rb_invalidate
};

static int defer_packet_queue(
	struct sdma_engine *sde,
	struct iowait *wait,
	struct sdma_txreq *txreq,
	uint seq,
	bool pkts_sent)
{
	struct hfi1_user_sdma_pkt_q *pq =
		container_of(wait, struct hfi1_user_sdma_pkt_q, busy);
	struct hfi1_ibdev *dev = &pq->dd->verbs_dev;
	struct user_sdma_txreq *tx =
		container_of(txreq, struct user_sdma_txreq, txreq);

	if (sdma_progress(sde, seq, txreq)) {
		if (tx->busycount++ < MAX_DEFER_RETRY_COUNT)
			goto eagain;
	}
	/*
	 * We are assuming that if the list is enqueued somewhere, it
	 * is to the dmawait list since that is the only place where
	 * it is supposed to be enqueued.
	 */
	xchg(&pq->state, SDMA_PKT_Q_DEFERRED);
	write_seqlock(&dev->iowait_lock);
	if (list_empty(&pq->busy.list))
		iowait_queue(pkts_sent, &pq->busy, &sde->dmawait);
	write_sequnlock(&dev->iowait_lock);
	return -EBUSY;
eagain:
	return -EAGAIN;
}

static void activate_packet_queue(struct iowait *wait, int reason)
{
	struct hfi1_user_sdma_pkt_q *pq =
		container_of(wait, struct hfi1_user_sdma_pkt_q, busy);
	xchg(&pq->state, SDMA_PKT_Q_ACTIVE);
	wake_up(&wait->wait_dma);
};

int hfi1_user_sdma_alloc_queues(struct hfi1_ctxtdata *uctxt,
				struct hfi1_filedata *fd)
{
	int ret = -ENOMEM;
	char buf[64];
	struct hfi1_devdata *dd;
	struct hfi1_user_sdma_comp_q *cq;
	struct hfi1_user_sdma_pkt_q *pq;

	if (!uctxt || !fd)
		return -EBADF;

	if (!hfi1_sdma_comp_ring_size)
		return -EINVAL;

	dd = uctxt->dd;

	pq = kzalloc(sizeof(*pq), GFP_KERNEL);
	if (!pq)
		return -ENOMEM;

	pq->dd = dd;
	pq->ctxt = uctxt->ctxt;
	pq->subctxt = fd->subctxt;
	pq->n_max_reqs = hfi1_sdma_comp_ring_size;
	pq->state = SDMA_PKT_Q_INACTIVE;
	atomic_set(&pq->n_reqs, 0);
	init_waitqueue_head(&pq->wait);
	atomic_set(&pq->n_locked, 0);
	pq->mm = fd->mm;

	iowait_init(&pq->busy, 0, NULL, defer_packet_queue,
		    activate_packet_queue, NULL);
	pq->reqidx = 0;

	pq->reqs = kcalloc(hfi1_sdma_comp_ring_size,
			   sizeof(*pq->reqs),
			   GFP_KERNEL);
	if (!pq->reqs)
		goto pq_reqs_nomem;

	pq->req_in_use = kcalloc(BITS_TO_LONGS(hfi1_sdma_comp_ring_size),
				 sizeof(*pq->req_in_use),
				 GFP_KERNEL);
	if (!pq->req_in_use)
		goto pq_reqs_no_in_use;

	snprintf(buf, 64, "txreq-kmem-cache-%u-%u-%u", dd->unit, uctxt->ctxt,
		 fd->subctxt);
	pq->txreq_cache = kmem_cache_create(buf,
					    sizeof(struct user_sdma_txreq),
					    L1_CACHE_BYTES,
					    SLAB_HWCACHE_ALIGN,
					    NULL);
	if (!pq->txreq_cache) {
		dd_dev_err(dd, "[%u] Failed to allocate TxReq cache\n",
			   uctxt->ctxt);
		goto pq_txreq_nomem;
	}

	cq = kzalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq)
		goto cq_nomem;

	cq->comps = vmalloc_user(PAGE_ALIGN(sizeof(*cq->comps)
				 * hfi1_sdma_comp_ring_size));
	if (!cq->comps)
		goto cq_comps_nomem;

	cq->nentries = hfi1_sdma_comp_ring_size;

	ret = hfi1_mmu_rb_register(pq, pq->mm, &sdma_rb_ops, dd->pport->hfi1_wq,
				   &pq->handler);
	if (ret) {
		dd_dev_err(dd, "Failed to register with MMU %d", ret);
		goto pq_mmu_fail;
	}

	fd->pq = pq;
	fd->cq = cq;

	return 0;

pq_mmu_fail:
	vfree(cq->comps);
cq_comps_nomem:
	kfree(cq);
cq_nomem:
	kmem_cache_destroy(pq->txreq_cache);
pq_txreq_nomem:
	kfree(pq->req_in_use);
pq_reqs_no_in_use:
	kfree(pq->reqs);
pq_reqs_nomem:
	kfree(pq);

	return ret;
}

int hfi1_user_sdma_free_queues(struct hfi1_filedata *fd,
			       struct hfi1_ctxtdata *uctxt)
{
	struct hfi1_user_sdma_pkt_q *pq;

	trace_hfi1_sdma_user_free_queues(uctxt->dd, uctxt->ctxt, fd->subctxt);

	pq = fd->pq;
	if (pq) {
		if (pq->handler)
			hfi1_mmu_rb_unregister(pq->handler);
		iowait_sdma_drain(&pq->busy);
		/* Wait until all requests have been freed. */
		wait_event_interruptible(
			pq->wait,
			(ACCESS_ONCE(pq->state) == SDMA_PKT_Q_INACTIVE));
		kfree(pq->reqs);
		kfree(pq->req_in_use);
		kmem_cache_destroy(pq->txreq_cache);
		kfree(pq);
		fd->pq = NULL;
	}
	if (fd->cq) {
		vfree(fd->cq->comps);
		kfree(fd->cq);
		fd->cq = NULL;
	}
	return 0;
}

static u8 dlid_to_selector(u16 dlid)
{
	static u8 mapping[256];
	static int initialized;
	static u8 next;
	int hash;

	if (!initialized) {
		memset(mapping, 0xFF, 256);
		initialized = 1;
	}

	hash = ((dlid >> 8) ^ dlid) & 0xFF;
	if (mapping[hash] == 0xFF) {
		mapping[hash] = next;
		next = (next + 1) & 0x7F;
	}

	return mapping[hash];
}

int hfi1_user_sdma_process_request(struct hfi1_filedata *fd,
				   struct iovec *iovec, unsigned long dim,
				   unsigned long *count)
{
	int ret = 0, i;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_user_sdma_pkt_q *pq = fd->pq;
	struct hfi1_user_sdma_comp_q *cq = fd->cq;
	struct hfi1_devdata *dd = pq->dd;
	unsigned long idx = 0;
	u8 pcount = initial_pkt_count;
	struct sdma_req_info info;
	struct user_sdma_request *req;
	u8 opcode, sc, vl;
	u16 pkey;
	u32 slid;
	int req_queued = 0;
	u16 dlid;
	u32 selector;

	if (iovec[idx].iov_len < sizeof(info) + sizeof(req->hdr)) {
		hfi1_cdbg(
		   SDMA,
		   "[%u:%u:%u] First vector not big enough for header %lu/%lu",
		   dd->unit, uctxt->ctxt, fd->subctxt,
		   iovec[idx].iov_len, sizeof(info) + sizeof(req->hdr));
		return -EINVAL;
	}
	ret = copy_from_user(&info, iovec[idx].iov_base, sizeof(info));
	if (ret) {
		hfi1_cdbg(SDMA, "[%u:%u:%u] Failed to copy info QW (%d)",
			  dd->unit, uctxt->ctxt, fd->subctxt, ret);
		return -EFAULT;
	}

	trace_hfi1_sdma_user_reqinfo(dd, uctxt->ctxt, fd->subctxt,
				     (u16 *)&info);
	if (info.comp_idx >= hfi1_sdma_comp_ring_size) {
		hfi1_cdbg(SDMA,
			  "[%u:%u:%u:%u] Invalid comp index",
			  dd->unit, uctxt->ctxt, fd->subctxt, info.comp_idx);
		return -EINVAL;
	}

	/*
	 * Sanity check the header io vector count.  Need at least 1 vector
	 * (header) and cannot be larger than the actual io vector count.
	 */
	if (req_iovcnt(info.ctrl) < 1 || req_iovcnt(info.ctrl) > dim) {
		hfi1_cdbg(SDMA,
			  "[%u:%u:%u:%u] Invalid iov count %d, dim %ld",
			  dd->unit, uctxt->ctxt, fd->subctxt, info.comp_idx,
			  req_iovcnt(info.ctrl), dim);
		return -EINVAL;
	}

	if (!info.fragsize) {
		hfi1_cdbg(SDMA,
			  "[%u:%u:%u:%u] Request does not specify fragsize",
			  dd->unit, uctxt->ctxt, fd->subctxt, info.comp_idx);
		return -EINVAL;
	}

	/* Try to claim the request. */
	if (test_and_set_bit(info.comp_idx, pq->req_in_use)) {
		hfi1_cdbg(SDMA, "[%u:%u:%u] Entry %u is in use",
			  dd->unit, uctxt->ctxt, fd->subctxt,
			  info.comp_idx);
		return -EBADSLT;
	}
	/*
	 * All safety checks have been done and this request has been claimed.
	 */
	trace_hfi1_sdma_user_process_request(dd, uctxt->ctxt, fd->subctxt,
					     info.comp_idx);
	req = pq->reqs + info.comp_idx;
	req->data_iovs = req_iovcnt(info.ctrl) - 1; /* subtract header vector */
	req->data_len  = 0;
	req->pq = pq;
	req->cq = cq;
	req->status = -1;
	req->ahg_idx = -1;
	req->iov_idx = 0;
	req->sent = 0;
	req->seqnum = 0;
	req->seqcomp = 0;
	req->seqsubmitted = 0;
	req->tids = NULL;
	req->done = 0;
	req->has_error = 0;
	INIT_LIST_HEAD(&req->txps);

	memcpy(&req->info, &info, sizeof(info));

	if (req_opcode(info.ctrl) == EXPECTED) {
		/* expected must have a TID info and at least one data vector */
		if (req->data_iovs < 2) {
			SDMA_DBG(req,
				 "Not enough vectors for expected request");
			ret = -EINVAL;
			goto free_req;
		}
		req->data_iovs--;
	}

	if (!info.npkts || req->data_iovs > MAX_VECTORS_PER_REQ) {
		SDMA_DBG(req, "Too many vectors (%u/%u)", req->data_iovs,
			 MAX_VECTORS_PER_REQ);
		ret = -EINVAL;
		goto free_req;
	}
	/* Copy the header from the user buffer */
	ret = copy_from_user(&req->hdr, iovec[idx].iov_base + sizeof(info),
			     sizeof(req->hdr));
	if (ret) {
		SDMA_DBG(req, "Failed to copy header template (%d)", ret);
		ret = -EFAULT;
		goto free_req;
	}

	/* If Static rate control is not enabled, sanitize the header. */
	if (!HFI1_CAP_IS_USET(STATIC_RATE_CTRL))
		req->hdr.pbc[2] = 0;

	/* Validate the opcode. Do not trust packets from user space blindly. */
	opcode = (be32_to_cpu(req->hdr.bth[0]) >> 24) & 0xff;
	if ((opcode & USER_OPCODE_CHECK_MASK) !=
	     USER_OPCODE_CHECK_VAL) {
		SDMA_DBG(req, "Invalid opcode (%d)", opcode);
		ret = -EINVAL;
		goto free_req;
	}
	/*
	 * Validate the vl. Do not trust packets from user space blindly.
	 * VL comes from PBC, SC comes from LRH, and the VL needs to
	 * match the SC look up.
	 */
	vl = (le16_to_cpu(req->hdr.pbc[0]) >> 12) & 0xF;
	sc = (((be16_to_cpu(req->hdr.lrh[0]) >> 12) & 0xF) |
	      (((le16_to_cpu(req->hdr.pbc[1]) >> 14) & 0x1) << 4));
	if (vl >= dd->pport->vls_operational ||
	    vl != sc_to_vlt(dd, sc)) {
		SDMA_DBG(req, "Invalid SC(%u)/VL(%u)", sc, vl);
		ret = -EINVAL;
		goto free_req;
	}

	/* Checking P_KEY for requests from user-space */
	pkey = (u16)be32_to_cpu(req->hdr.bth[0]);
	slid = be16_to_cpu(req->hdr.lrh[3]);
	if (egress_pkey_check(dd->pport, slid, pkey, sc, PKEY_CHECK_INVALID)) {
		ret = -EINVAL;
		goto free_req;
	}

	/*
	 * Also should check the BTH.lnh. If it says the next header is GRH then
	 * the RXE parsing will be off and will land in the middle of the KDETH
	 * or miss it entirely.
	 */
	if ((be16_to_cpu(req->hdr.lrh[0]) & 0x3) == HFI1_LRH_GRH) {
		SDMA_DBG(req, "User tried to pass in a GRH");
		ret = -EINVAL;
		goto free_req;
	}

	req->koffset = le32_to_cpu(req->hdr.kdeth.swdata[6]);
	/*
	 * Calculate the initial TID offset based on the values of
	 * KDETH.OFFSET and KDETH.OM that are passed in.
	 */
	req->tidoffset = KDETH_GET(req->hdr.kdeth.ver_tid_offset, OFFSET) *
		(KDETH_GET(req->hdr.kdeth.ver_tid_offset, OM) ?
		 KDETH_OM_LARGE : KDETH_OM_SMALL);
	trace_hfi1_sdma_user_initial_tidoffset(dd, uctxt->ctxt, fd->subctxt,
					       info.comp_idx, req->tidoffset);
	idx++;

	/* Save all the IO vector structures */
	for (i = 0; i < req->data_iovs; i++) {
		req->iovs[i].offset = 0;
		INIT_LIST_HEAD(&req->iovs[i].list);
		memcpy(&req->iovs[i].iov,
		       iovec + idx++,
		       sizeof(req->iovs[i].iov));
		ret = pin_vector_pages(req, &req->iovs[i]);
		if (ret) {
			req->data_iovs = i;
			req->status = ret;
			goto free_req;
		}
		req->data_len += req->iovs[i].iov.iov_len;
	}
	trace_hfi1_sdma_user_data_length(dd, uctxt->ctxt, fd->subctxt,
					 info.comp_idx, req->data_len);
	if (pcount > req->info.npkts)
		pcount = req->info.npkts;
	/*
	 * Copy any TID info
	 * User space will provide the TID info only when the
	 * request type is EXPECTED. This is true even if there is
	 * only one packet in the request and the header is already
	 * setup. The reason for the singular TID case is that the
	 * driver needs to perform safety checks.
	 */
	if (req_opcode(req->info.ctrl) == EXPECTED) {
		u16 ntids = iovec[idx].iov_len / sizeof(*req->tids);
		u32 *tmp;

		if (!ntids || ntids > MAX_TID_PAIR_ENTRIES) {
			ret = -EINVAL;
			goto free_req;
		}

		/*
		 * We have to copy all of the tids because they may vary
		 * in size and, therefore, the TID count might not be
		 * equal to the pkt count. However, there is no way to
		 * tell at this point.
		 */
		tmp = memdup_user(iovec[idx].iov_base,
				  ntids * sizeof(*req->tids));
		if (IS_ERR(tmp)) {
			ret = PTR_ERR(tmp);
			SDMA_DBG(req, "Failed to copy %d TIDs (%d)",
				 ntids, ret);
			goto free_req;
		}
		req->tids = tmp;
		req->n_tids = ntids;
		req->tididx = 0;
		idx++;
	}

	dlid = be16_to_cpu(req->hdr.lrh[1]);
	selector = dlid_to_selector(dlid);
	selector += uctxt->ctxt + fd->subctxt;
	req->sde = sdma_select_user_engine(dd, selector, vl);

	if (!req->sde || !sdma_running(req->sde)) {
		ret = -ECOMM;
		goto free_req;
	}

	/* We don't need an AHG entry if the request contains only one packet */
	if (req->info.npkts > 1 && HFI1_CAP_IS_USET(SDMA_AHG))
		req->ahg_idx = sdma_ahg_alloc(req->sde);

	set_comp_state(pq, cq, info.comp_idx, QUEUED, 0);
	atomic_inc(&pq->n_reqs);
	req_queued = 1;
	/* Send the first N packets in the request to buy us some time */
	ret = user_sdma_send_pkts(req, pcount);
	if (unlikely(ret < 0 && ret != -EBUSY)) {
		req->status = ret;
		goto free_req;
	}

	/*
	 * It is possible that the SDMA engine would have processed all the
	 * submitted packets by the time we get here. Therefore, only set
	 * packet queue state to ACTIVE if there are still uncompleted
	 * requests.
	 */
	if (atomic_read(&pq->n_reqs))
		xchg(&pq->state, SDMA_PKT_Q_ACTIVE);

	/*
	 * This is a somewhat blocking send implementation.
	 * The driver will block the caller until all packets of the
	 * request have been submitted to the SDMA engine. However, it
	 * will not wait for send completions.
	 */
	while (req->seqsubmitted != req->info.npkts) {
		ret = user_sdma_send_pkts(req, pcount);
		if (ret < 0) {
			if (ret != -EBUSY) {
				req->status = ret;
				WRITE_ONCE(req->has_error, 1);
				if (ACCESS_ONCE(req->seqcomp) ==
				    req->seqsubmitted - 1)
					goto free_req;
				return ret;
			}
			wait_event_interruptible_timeout(
				pq->busy.wait_dma,
				(pq->state == SDMA_PKT_Q_ACTIVE),
				msecs_to_jiffies(
					SDMA_IOWAIT_TIMEOUT));
		}
	}
	*count += idx;
	return 0;
free_req:
	user_sdma_free_request(req, true);
	if (req_queued)
		pq_update(pq);
	set_comp_state(pq, cq, info.comp_idx, ERROR, req->status);
	return ret;
}

static inline u32 compute_data_length(struct user_sdma_request *req,
				      struct user_sdma_txreq *tx)
{
	/*
	 * Determine the proper size of the packet data.
	 * The size of the data of the first packet is in the header
	 * template. However, it includes the header and ICRC, which need
	 * to be subtracted.
	 * The minimum representable packet data length in a header is 4 bytes,
	 * therefore, when the data length request is less than 4 bytes, there's
	 * only one packet, and the packet data length is equal to that of the
	 * request data length.
	 * The size of the remaining packets is the minimum of the frag
	 * size (MTU) or remaining data in the request.
	 */
	u32 len;

	if (!req->seqnum) {
		if (req->data_len < sizeof(u32))
			len = req->data_len;
		else
			len = ((be16_to_cpu(req->hdr.lrh[2]) << 2) -
			       (sizeof(tx->hdr) - 4));
	} else if (req_opcode(req->info.ctrl) == EXPECTED) {
		u32 tidlen = EXP_TID_GET(req->tids[req->tididx], LEN) *
			PAGE_SIZE;
		/*
		 * Get the data length based on the remaining space in the
		 * TID pair.
		 */
		len = min(tidlen - req->tidoffset, (u32)req->info.fragsize);
		/* If we've filled up the TID pair, move to the next one. */
		if (unlikely(!len) && ++req->tididx < req->n_tids &&
		    req->tids[req->tididx]) {
			tidlen = EXP_TID_GET(req->tids[req->tididx],
					     LEN) * PAGE_SIZE;
			req->tidoffset = 0;
			len = min_t(u32, tidlen, req->info.fragsize);
		}
		/*
		 * Since the TID pairs map entire pages, make sure that we
		 * are not going to try to send more data that we have
		 * remaining.
		 */
		len = min(len, req->data_len - req->sent);
	} else {
		len = min(req->data_len - req->sent, (u32)req->info.fragsize);
	}
	trace_hfi1_sdma_user_compute_length(req->pq->dd,
					    req->pq->ctxt,
					    req->pq->subctxt,
					    req->info.comp_idx,
					    len);
	return len;
}

static inline u32 pad_len(u32 len)
{
	if (len & (sizeof(u32) - 1))
		len += sizeof(u32) - (len & (sizeof(u32) - 1));
	return len;
}

static inline u32 get_lrh_len(struct hfi1_pkt_header hdr, u32 len)
{
	/* (Size of complete header - size of PBC) + 4B ICRC + data length */
	return ((sizeof(hdr) - sizeof(hdr.pbc)) + 4 + len);
}

static int user_sdma_txadd_ahg(struct user_sdma_request *req,
			       struct user_sdma_txreq *tx,
			       u32 datalen)
{
	int ret;
	u16 pbclen = le16_to_cpu(req->hdr.pbc[0]);
	u32 lrhlen = get_lrh_len(req->hdr, pad_len(datalen));
	struct hfi1_user_sdma_pkt_q *pq = req->pq;

	/*
	 * Copy the request header into the tx header
	 * because the HW needs a cacheline-aligned
	 * address.
	 * This copy can be optimized out if the hdr
	 * member of user_sdma_request were also
	 * cacheline aligned.
	 */
	memcpy(&tx->hdr, &req->hdr, sizeof(tx->hdr));
	if (PBC2LRH(pbclen) != lrhlen) {
		pbclen = (pbclen & 0xf000) | LRH2PBC(lrhlen);
		tx->hdr.pbc[0] = cpu_to_le16(pbclen);
	}
	ret = check_header_template(req, &tx->hdr, lrhlen, datalen);
	if (ret)
		return ret;
	ret = sdma_txinit_ahg(&tx->txreq, SDMA_TXREQ_F_AHG_COPY,
			      sizeof(tx->hdr) + datalen, req->ahg_idx,
			      0, NULL, 0, user_sdma_txreq_cb);
	if (ret)
		return ret;
	ret = sdma_txadd_kvaddr(pq->dd, &tx->txreq, &tx->hdr, sizeof(tx->hdr));
	if (ret)
		sdma_txclean(pq->dd, &tx->txreq);
	return ret;
}

static int user_sdma_txadd(struct user_sdma_request *req,
			   struct user_sdma_txreq *tx,
			   struct user_sdma_iovec *iovec, u32 datalen,
			   u32 *queued_ptr, u32 *data_sent_ptr,
			   u64 *iov_offset_ptr)
{
	int ret;
	unsigned int pageidx, len;
	unsigned long base, offset;
	u64 iov_offset = *iov_offset_ptr;
	u32 queued = *queued_ptr, data_sent = *data_sent_ptr;
	struct hfi1_user_sdma_pkt_q *pq = req->pq;

	base = (unsigned long)iovec->iov.iov_base;
	offset = offset_in_page(base + iovec->offset + iov_offset);
	pageidx = (((iovec->offset + iov_offset + base) - (base & PAGE_MASK)) >>
		   PAGE_SHIFT);
	len = offset + req->info.fragsize > PAGE_SIZE ?
		PAGE_SIZE - offset : req->info.fragsize;
	len = min((datalen - queued), len);
	ret = sdma_txadd_page(pq->dd, &tx->txreq, iovec->pages[pageidx],
			      offset, len);
	if (ret) {
		SDMA_DBG(req, "SDMA txreq add page failed %d\n", ret);
		return ret;
	}
	iov_offset += len;
	queued += len;
	data_sent += len;
	if (unlikely(queued < datalen && pageidx == iovec->npages &&
		     req->iov_idx < req->data_iovs - 1)) {
		iovec->offset += iov_offset;
		iovec = &req->iovs[++req->iov_idx];
		iov_offset = 0;
	}

	*queued_ptr = queued;
	*data_sent_ptr = data_sent;
	*iov_offset_ptr = iov_offset;
	return ret;
}

static int user_sdma_send_pkts(struct user_sdma_request *req, unsigned maxpkts)
{
	int ret = 0, count;
	unsigned npkts = 0;
	struct user_sdma_txreq *tx = NULL;
	struct hfi1_user_sdma_pkt_q *pq = NULL;
	struct user_sdma_iovec *iovec = NULL;

	if (!req->pq)
		return -EINVAL;

	pq = req->pq;

	/* If tx completion has reported an error, we are done. */
	if (READ_ONCE(req->has_error))
		return -EFAULT;

	/*
	 * Check if we might have sent the entire request already
	 */
	if (unlikely(req->seqnum == req->info.npkts)) {
		if (!list_empty(&req->txps))
			goto dosend;
		return ret;
	}

	if (!maxpkts || maxpkts > req->info.npkts - req->seqnum)
		maxpkts = req->info.npkts - req->seqnum;

	while (npkts < maxpkts) {
		u32 datalen = 0, queued = 0, data_sent = 0;
		u64 iov_offset = 0;

		/*
		 * Check whether any of the completions have come back
		 * with errors. If so, we are not going to process any
		 * more packets from this request.
		 */
		if (READ_ONCE(req->has_error))
			return -EFAULT;

		tx = kmem_cache_alloc(pq->txreq_cache, GFP_KERNEL);
		if (!tx)
			return -ENOMEM;

		tx->flags = 0;
		tx->req = req;
		tx->busycount = 0;
		INIT_LIST_HEAD(&tx->list);

		/*
		 * For the last packet set the ACK request
		 * and disable header suppression.
		 */
		if (req->seqnum == req->info.npkts - 1)
			tx->flags |= (TXREQ_FLAGS_REQ_ACK |
				      TXREQ_FLAGS_REQ_DISABLE_SH);

		/*
		 * Calculate the payload size - this is min of the fragment
		 * (MTU) size or the remaining bytes in the request but only
		 * if we have payload data.
		 */
		if (req->data_len) {
			iovec = &req->iovs[req->iov_idx];
			if (ACCESS_ONCE(iovec->offset) == iovec->iov.iov_len) {
				if (++req->iov_idx == req->data_iovs) {
					ret = -EFAULT;
					goto free_txreq;
				}
				iovec = &req->iovs[req->iov_idx];
				WARN_ON(iovec->offset);
			}

			datalen = compute_data_length(req, tx);

			/*
			 * Disable header suppression for the payload <= 8DWS.
			 * If there is an uncorrectable error in the receive
			 * data FIFO when the received payload size is less than
			 * or equal to 8DWS then the RxDmaDataFifoRdUncErr is
			 * not reported.There is set RHF.EccErr if the header
			 * is not suppressed.
			 */
			if (!datalen) {
				SDMA_DBG(req,
					 "Request has data but pkt len is 0");
				ret = -EFAULT;
				goto free_tx;
			} else if (datalen <= 32) {
				tx->flags |= TXREQ_FLAGS_REQ_DISABLE_SH;
			}
		}

		if (req->ahg_idx >= 0) {
			if (!req->seqnum) {
				ret = user_sdma_txadd_ahg(req, tx, datalen);
				if (ret)
					goto free_tx;
			} else {
				int changes;

				changes = set_txreq_header_ahg(req, tx,
							       datalen);
				if (changes < 0)
					goto free_tx;
			}
		} else {
			ret = sdma_txinit(&tx->txreq, 0, sizeof(req->hdr) +
					  datalen, user_sdma_txreq_cb);
			if (ret)
				goto free_tx;
			/*
			 * Modify the header for this packet. This only needs
			 * to be done if we are not going to use AHG. Otherwise,
			 * the HW will do it based on the changes we gave it
			 * during sdma_txinit_ahg().
			 */
			ret = set_txreq_header(req, tx, datalen);
			if (ret)
				goto free_txreq;
		}

		/*
		 * If the request contains any data vectors, add up to
		 * fragsize bytes to the descriptor.
		 */
		while (queued < datalen &&
		       (req->sent + data_sent) < req->data_len) {
			ret = user_sdma_txadd(req, tx, iovec, datalen,
					      &queued, &data_sent, &iov_offset);
			if (ret)
				goto free_txreq;
		}
		/*
		 * The txreq was submitted successfully so we can update
		 * the counters.
		 */
		req->koffset += datalen;
		if (req_opcode(req->info.ctrl) == EXPECTED)
			req->tidoffset += datalen;
		req->sent += data_sent;
		if (req->data_len)
			iovec->offset += iov_offset;
		list_add_tail(&tx->txreq.list, &req->txps);
		/*
		 * It is important to increment this here as it is used to
		 * generate the BTH.PSN and, therefore, can't be bulk-updated
		 * outside of the loop.
		 */
		tx->seqnum = req->seqnum++;
		npkts++;
	}
dosend:
	ret = sdma_send_txlist(req->sde, &pq->busy, &req->txps, &count);
	req->seqsubmitted += count;
	if (req->seqsubmitted == req->info.npkts) {
		WRITE_ONCE(req->done, 1);
		/*
		 * The txreq has already been submitted to the HW queue
		 * so we can free the AHG entry now. Corruption will not
		 * happen due to the sequential manner in which
		 * descriptors are processed.
		 */
		if (req->ahg_idx >= 0)
			sdma_ahg_free(req->sde, req->ahg_idx);
	}
	return ret;

free_txreq:
	sdma_txclean(pq->dd, &tx->txreq);
free_tx:
	kmem_cache_free(pq->txreq_cache, tx);
	return ret;
}

static u32 sdma_cache_evict(struct hfi1_user_sdma_pkt_q *pq, u32 npages)
{
	struct evict_data evict_data;

	evict_data.cleared = 0;
	evict_data.target = npages;
	hfi1_mmu_rb_evict(pq->handler, &evict_data);
	return evict_data.cleared;
}

static int pin_sdma_pages(struct user_sdma_request *req,
			  struct user_sdma_iovec *iovec,
			  struct sdma_mmu_node *node,
			  int npages)
{
	int pinned, cleared;
	struct page **pages;
	struct hfi1_user_sdma_pkt_q *pq = req->pq;

	pages = kcalloc(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;
	memcpy(pages, node->pages, node->npages * sizeof(*pages));

	npages -= node->npages;
retry:
	if (!hfi1_can_pin_pages(pq->dd, pq->mm,
				atomic_read(&pq->n_locked), npages)) {
		cleared = sdma_cache_evict(pq, npages);
		if (cleared >= npages)
			goto retry;
	}
	pinned = hfi1_acquire_user_pages(pq->mm,
					 ((unsigned long)iovec->iov.iov_base +
					 (node->npages * PAGE_SIZE)), npages, 0,
					 pages + node->npages);
	if (pinned < 0) {
		kfree(pages);
		return pinned;
	}
	if (pinned != npages) {
		unpin_vector_pages(pq->mm, pages, node->npages, pinned);
		return -EFAULT;
	}
	kfree(node->pages);
	node->rb.len = iovec->iov.iov_len;
	node->pages = pages;
	atomic_add(pinned, &pq->n_locked);
	return pinned;
}

static void unpin_sdma_pages(struct sdma_mmu_node *node)
{
	if (node->npages) {
		unpin_vector_pages(node->pq->mm, node->pages, 0, node->npages);
		atomic_sub(node->npages, &node->pq->n_locked);
	}
}

static int pin_vector_pages(struct user_sdma_request *req,
			    struct user_sdma_iovec *iovec)
{
	int ret = 0, pinned, npages;
	struct hfi1_user_sdma_pkt_q *pq = req->pq;
	struct sdma_mmu_node *node = NULL;
	struct mmu_rb_node *rb_node;
	struct iovec *iov;
	bool extracted;

	extracted =
		hfi1_mmu_rb_remove_unless_exact(pq->handler,
						(unsigned long)
						iovec->iov.iov_base,
						iovec->iov.iov_len, &rb_node);
	if (rb_node) {
		node = container_of(rb_node, struct sdma_mmu_node, rb);
		if (!extracted) {
			atomic_inc(&node->refcount);
			iovec->pages = node->pages;
			iovec->npages = node->npages;
			iovec->node = node;
			return 0;
		}
	}

	if (!node) {
		node = kzalloc(sizeof(*node), GFP_KERNEL);
		if (!node)
			return -ENOMEM;

		node->rb.addr = (unsigned long)iovec->iov.iov_base;
		node->pq = pq;
		atomic_set(&node->refcount, 0);
	}

	iov = &iovec->iov;
	npages = num_user_pages((unsigned long)iov->iov_base, iov->iov_len);
	if (node->npages < npages) {
		pinned = pin_sdma_pages(req, iovec, node, npages);
		if (pinned < 0) {
			ret = pinned;
			goto bail;
		}
		node->npages += pinned;
		npages = node->npages;
	}
	iovec->pages = node->pages;
	iovec->npages = npages;
	iovec->node = node;

	ret = hfi1_mmu_rb_insert(req->pq->handler, &node->rb);
	if (ret) {
		iovec->node = NULL;
		goto bail;
	}
	return 0;
bail:
	unpin_sdma_pages(node);
	kfree(node);
	return ret;
}

static void unpin_vector_pages(struct mm_struct *mm, struct page **pages,
			       unsigned start, unsigned npages)
{
	hfi1_release_user_pages(mm, pages + start, npages, false);
	kfree(pages);
}

static int check_header_template(struct user_sdma_request *req,
				 struct hfi1_pkt_header *hdr, u32 lrhlen,
				 u32 datalen)
{
	/*
	 * Perform safety checks for any type of packet:
	 *    - transfer size is multiple of 64bytes
	 *    - packet length is multiple of 4 bytes
	 *    - packet length is not larger than MTU size
	 *
	 * These checks are only done for the first packet of the
	 * transfer since the header is "given" to us by user space.
	 * For the remainder of the packets we compute the values.
	 */
	if (req->info.fragsize % PIO_BLOCK_SIZE || lrhlen & 0x3 ||
	    lrhlen > get_lrh_len(*hdr, req->info.fragsize))
		return -EINVAL;

	if (req_opcode(req->info.ctrl) == EXPECTED) {
		/*
		 * The header is checked only on the first packet. Furthermore,
		 * we ensure that at least one TID entry is copied when the
		 * request is submitted. Therefore, we don't have to verify that
		 * tididx points to something sane.
		 */
		u32 tidval = req->tids[req->tididx],
			tidlen = EXP_TID_GET(tidval, LEN) * PAGE_SIZE,
			tididx = EXP_TID_GET(tidval, IDX),
			tidctrl = EXP_TID_GET(tidval, CTRL),
			tidoff;
		__le32 kval = hdr->kdeth.ver_tid_offset;

		tidoff = KDETH_GET(kval, OFFSET) *
			  (KDETH_GET(req->hdr.kdeth.ver_tid_offset, OM) ?
			   KDETH_OM_LARGE : KDETH_OM_SMALL);
		/*
		 * Expected receive packets have the following
		 * additional checks:
		 *     - offset is not larger than the TID size
		 *     - TIDCtrl values match between header and TID array
		 *     - TID indexes match between header and TID array
		 */
		if ((tidoff + datalen > tidlen) ||
		    KDETH_GET(kval, TIDCTRL) != tidctrl ||
		    KDETH_GET(kval, TID) != tididx)
			return -EINVAL;
	}
	return 0;
}

/*
 * Correctly set the BTH.PSN field based on type of
 * transfer - eager packets can just increment the PSN but
 * expected packets encode generation and sequence in the
 * BTH.PSN field so just incrementing will result in errors.
 */
static inline u32 set_pkt_bth_psn(__be32 bthpsn, u8 expct, u32 frags)
{
	u32 val = be32_to_cpu(bthpsn),
		mask = (HFI1_CAP_IS_KSET(EXTENDED_PSN) ? 0x7fffffffull :
			0xffffffull),
		psn = val & mask;
	if (expct)
		psn = (psn & ~BTH_SEQ_MASK) | ((psn + frags) & BTH_SEQ_MASK);
	else
		psn = psn + frags;
	return psn & mask;
}

static int set_txreq_header(struct user_sdma_request *req,
			    struct user_sdma_txreq *tx, u32 datalen)
{
	struct hfi1_user_sdma_pkt_q *pq = req->pq;
	struct hfi1_pkt_header *hdr = &tx->hdr;
	u8 omfactor; /* KDETH.OM */
	u16 pbclen;
	int ret;
	u32 tidval = 0, lrhlen = get_lrh_len(*hdr, pad_len(datalen));

	/* Copy the header template to the request before modification */
	memcpy(hdr, &req->hdr, sizeof(*hdr));

	/*
	 * Check if the PBC and LRH length are mismatched. If so
	 * adjust both in the header.
	 */
	pbclen = le16_to_cpu(hdr->pbc[0]);
	if (PBC2LRH(pbclen) != lrhlen) {
		pbclen = (pbclen & 0xf000) | LRH2PBC(lrhlen);
		hdr->pbc[0] = cpu_to_le16(pbclen);
		hdr->lrh[2] = cpu_to_be16(lrhlen >> 2);
		/*
		 * Third packet
		 * This is the first packet in the sequence that has
		 * a "static" size that can be used for the rest of
		 * the packets (besides the last one).
		 */
		if (unlikely(req->seqnum == 2)) {
			/*
			 * From this point on the lengths in both the
			 * PBC and LRH are the same until the last
			 * packet.
			 * Adjust the template so we don't have to update
			 * every packet
			 */
			req->hdr.pbc[0] = hdr->pbc[0];
			req->hdr.lrh[2] = hdr->lrh[2];
		}
	}
	/*
	 * We only have to modify the header if this is not the
	 * first packet in the request. Otherwise, we use the
	 * header given to us.
	 */
	if (unlikely(!req->seqnum)) {
		ret = check_header_template(req, hdr, lrhlen, datalen);
		if (ret)
			return ret;
		goto done;
	}

	hdr->bth[2] = cpu_to_be32(
		set_pkt_bth_psn(hdr->bth[2],
				(req_opcode(req->info.ctrl) == EXPECTED),
				req->seqnum));

	/* Set ACK request on last packet */
	if (unlikely(tx->flags & TXREQ_FLAGS_REQ_ACK))
		hdr->bth[2] |= cpu_to_be32(1UL << 31);

	/* Set the new offset */
	hdr->kdeth.swdata[6] = cpu_to_le32(req->koffset);
	/* Expected packets have to fill in the new TID information */
	if (req_opcode(req->info.ctrl) == EXPECTED) {
		tidval = req->tids[req->tididx];
		/*
		 * If the offset puts us at the end of the current TID,
		 * advance everything.
		 */
		if ((req->tidoffset) == (EXP_TID_GET(tidval, LEN) *
					 PAGE_SIZE)) {
			req->tidoffset = 0;
			/*
			 * Since we don't copy all the TIDs, all at once,
			 * we have to check again.
			 */
			if (++req->tididx > req->n_tids - 1 ||
			    !req->tids[req->tididx]) {
				return -EINVAL;
			}
			tidval = req->tids[req->tididx];
		}
		omfactor = EXP_TID_GET(tidval, LEN) * PAGE_SIZE >=
			KDETH_OM_MAX_SIZE ? KDETH_OM_LARGE_SHIFT :
			KDETH_OM_SMALL_SHIFT;
		/* Set KDETH.TIDCtrl based on value for this TID. */
		KDETH_SET(hdr->kdeth.ver_tid_offset, TIDCTRL,
			  EXP_TID_GET(tidval, CTRL));
		/* Set KDETH.TID based on value for this TID */
		KDETH_SET(hdr->kdeth.ver_tid_offset, TID,
			  EXP_TID_GET(tidval, IDX));
		/* Clear KDETH.SH when DISABLE_SH flag is set */
		if (unlikely(tx->flags & TXREQ_FLAGS_REQ_DISABLE_SH))
			KDETH_SET(hdr->kdeth.ver_tid_offset, SH, 0);
		/*
		 * Set the KDETH.OFFSET and KDETH.OM based on size of
		 * transfer.
		 */
		trace_hfi1_sdma_user_tid_info(
			pq->dd, pq->ctxt, pq->subctxt, req->info.comp_idx,
			req->tidoffset, req->tidoffset >> omfactor,
			omfactor != KDETH_OM_SMALL_SHIFT);
		KDETH_SET(hdr->kdeth.ver_tid_offset, OFFSET,
			  req->tidoffset >> omfactor);
		KDETH_SET(hdr->kdeth.ver_tid_offset, OM,
			  omfactor != KDETH_OM_SMALL_SHIFT);
	}
done:
	trace_hfi1_sdma_user_header(pq->dd, pq->ctxt, pq->subctxt,
				    req->info.comp_idx, hdr, tidval);
	return sdma_txadd_kvaddr(pq->dd, &tx->txreq, hdr, sizeof(*hdr));
}

static int set_txreq_header_ahg(struct user_sdma_request *req,
				struct user_sdma_txreq *tx, u32 datalen)
{
	u32 ahg[AHG_KDETH_ARRAY_SIZE];
	int idx = 0;
	u8 omfactor; /* KDETH.OM */
	struct hfi1_user_sdma_pkt_q *pq = req->pq;
	struct hfi1_pkt_header *hdr = &req->hdr;
	u16 pbclen = le16_to_cpu(hdr->pbc[0]);
	u32 val32, tidval = 0, lrhlen = get_lrh_len(*hdr, pad_len(datalen));
	size_t array_size = ARRAY_SIZE(ahg);

	if (PBC2LRH(pbclen) != lrhlen) {
		/* PBC.PbcLengthDWs */
		idx = ahg_header_set(ahg, idx, array_size, 0, 0, 12,
				     (__force u16)cpu_to_le16(LRH2PBC(lrhlen)));
		if (idx < 0)
			return idx;
		/* LRH.PktLen (we need the full 16 bits due to byte swap) */
		idx = ahg_header_set(ahg, idx, array_size, 3, 0, 16,
				     (__force u16)cpu_to_be16(lrhlen >> 2));
		if (idx < 0)
			return idx;
	}

	/*
	 * Do the common updates
	 */
	/* BTH.PSN and BTH.A */
	val32 = (be32_to_cpu(hdr->bth[2]) + req->seqnum) &
		(HFI1_CAP_IS_KSET(EXTENDED_PSN) ? 0x7fffffff : 0xffffff);
	if (unlikely(tx->flags & TXREQ_FLAGS_REQ_ACK))
		val32 |= 1UL << 31;
	idx = ahg_header_set(ahg, idx, array_size, 6, 0, 16,
			     (__force u16)cpu_to_be16(val32 >> 16));
	if (idx < 0)
		return idx;
	idx = ahg_header_set(ahg, idx, array_size, 6, 16, 16,
			     (__force u16)cpu_to_be16(val32 & 0xffff));
	if (idx < 0)
		return idx;
	/* KDETH.Offset */
	idx = ahg_header_set(ahg, idx, array_size, 15, 0, 16,
			     (__force u16)cpu_to_le16(req->koffset & 0xffff));
	if (idx < 0)
		return idx;
	idx = ahg_header_set(ahg, idx, array_size, 15, 16, 16,
			     (__force u16)cpu_to_le16(req->koffset >> 16));
	if (idx < 0)
		return idx;
	if (req_opcode(req->info.ctrl) == EXPECTED) {
		__le16 val;

		tidval = req->tids[req->tididx];

		/*
		 * If the offset puts us at the end of the current TID,
		 * advance everything.
		 */
		if ((req->tidoffset) == (EXP_TID_GET(tidval, LEN) *
					 PAGE_SIZE)) {
			req->tidoffset = 0;
			/*
			 * Since we don't copy all the TIDs, all at once,
			 * we have to check again.
			 */
			if (++req->tididx > req->n_tids - 1 ||
			    !req->tids[req->tididx])
				return -EINVAL;
			tidval = req->tids[req->tididx];
		}
		omfactor = ((EXP_TID_GET(tidval, LEN) *
				  PAGE_SIZE) >=
				 KDETH_OM_MAX_SIZE) ? KDETH_OM_LARGE_SHIFT :
				 KDETH_OM_SMALL_SHIFT;
		/* KDETH.OM and KDETH.OFFSET (TID) */
		idx = ahg_header_set(
				ahg, idx, array_size, 7, 0, 16,
				((!!(omfactor - KDETH_OM_SMALL_SHIFT)) << 15 |
				((req->tidoffset >> omfactor)
				& 0x7fff)));
		if (idx < 0)
			return idx;
		/* KDETH.TIDCtrl, KDETH.TID, KDETH.Intr, KDETH.SH */
		val = cpu_to_le16(((EXP_TID_GET(tidval, CTRL) & 0x3) << 10) |
				   (EXP_TID_GET(tidval, IDX) & 0x3ff));

		if (unlikely(tx->flags & TXREQ_FLAGS_REQ_DISABLE_SH)) {
			val |= cpu_to_le16((KDETH_GET(hdr->kdeth.ver_tid_offset,
						      INTR) <<
					    AHG_KDETH_INTR_SHIFT));
		} else {
			val |= KDETH_GET(hdr->kdeth.ver_tid_offset, SH) ?
			       cpu_to_le16(0x1 << AHG_KDETH_SH_SHIFT) :
			       cpu_to_le16((KDETH_GET(hdr->kdeth.ver_tid_offset,
						      INTR) <<
					     AHG_KDETH_INTR_SHIFT));
		}

		idx = ahg_header_set(ahg, idx, array_size,
				     7, 16, 14, (__force u16)val);
		if (idx < 0)
			return idx;
	}

	trace_hfi1_sdma_user_header_ahg(pq->dd, pq->ctxt, pq->subctxt,
					req->info.comp_idx, req->sde->this_idx,
					req->ahg_idx, ahg, idx, tidval);
	sdma_txinit_ahg(&tx->txreq,
			SDMA_TXREQ_F_USE_AHG,
			datalen, req->ahg_idx, idx,
			ahg, sizeof(req->hdr),
			user_sdma_txreq_cb);

	return idx;
}

/*
 * SDMA tx request completion callback. Called when the SDMA progress
 * state machine gets notification that the SDMA descriptors for this
 * tx request have been processed by the DMA engine. Called in
 * interrupt context.
 */
static void user_sdma_txreq_cb(struct sdma_txreq *txreq, int status)
{
	struct user_sdma_txreq *tx =
		container_of(txreq, struct user_sdma_txreq, txreq);
	struct user_sdma_request *req;
	struct hfi1_user_sdma_pkt_q *pq;
	struct hfi1_user_sdma_comp_q *cq;
	u16 idx;

	if (!tx->req)
		return;

	req = tx->req;
	pq = req->pq;
	cq = req->cq;

	if (status != SDMA_TXREQ_S_OK) {
		SDMA_DBG(req, "SDMA completion with error %d",
			 status);
		WRITE_ONCE(req->has_error, 1);
	}

	req->seqcomp = tx->seqnum;
	kmem_cache_free(pq->txreq_cache, tx);
	tx = NULL;

	idx = req->info.comp_idx;
	if (req->status == -1 && status == SDMA_TXREQ_S_OK) {
		if (req->seqcomp == req->info.npkts - 1) {
			req->status = 0;
			user_sdma_free_request(req, false);
			pq_update(pq);
			set_comp_state(pq, cq, idx, COMPLETE, 0);
		}
	} else {
		if (status != SDMA_TXREQ_S_OK)
			req->status = status;
		if (req->seqcomp == (ACCESS_ONCE(req->seqsubmitted) - 1) &&
		    (READ_ONCE(req->done) ||
		     READ_ONCE(req->has_error))) {
			user_sdma_free_request(req, false);
			pq_update(pq);
			set_comp_state(pq, cq, idx, ERROR, req->status);
		}
	}
}

static inline void pq_update(struct hfi1_user_sdma_pkt_q *pq)
{
	if (atomic_dec_and_test(&pq->n_reqs)) {
		xchg(&pq->state, SDMA_PKT_Q_INACTIVE);
		wake_up(&pq->wait);
	}
}

static void user_sdma_free_request(struct user_sdma_request *req, bool unpin)
{
	int i;

	if (!list_empty(&req->txps)) {
		struct sdma_txreq *t, *p;

		list_for_each_entry_safe(t, p, &req->txps, list) {
			struct user_sdma_txreq *tx =
				container_of(t, struct user_sdma_txreq, txreq);
			list_del_init(&t->list);
			sdma_txclean(req->pq->dd, t);
			kmem_cache_free(req->pq->txreq_cache, tx);
		}
	}

	for (i = 0; i < req->data_iovs; i++) {
		struct sdma_mmu_node *node = req->iovs[i].node;

		if (!node)
			continue;

		if (unpin)
			hfi1_mmu_rb_remove(req->pq->handler,
					   &node->rb);
		else
			atomic_dec(&node->refcount);
	}

	kfree(req->tids);
	clear_bit(req->info.comp_idx, req->pq->req_in_use);
}

static inline void set_comp_state(struct hfi1_user_sdma_pkt_q *pq,
				  struct hfi1_user_sdma_comp_q *cq,
				  u16 idx, enum hfi1_sdma_comp_state state,
				  int ret)
{
	if (state == ERROR)
		cq->comps[idx].errcode = -ret;
	smp_wmb(); /* make sure errcode is visible first */
	cq->comps[idx].status = state;
	trace_hfi1_sdma_user_completion(pq->dd, pq->ctxt, pq->subctxt,
					idx, state, ret);
}

static bool sdma_rb_filter(struct mmu_rb_node *node, unsigned long addr,
			   unsigned long len)
{
	return (bool)(node->addr == addr);
}

static int sdma_rb_insert(void *arg, struct mmu_rb_node *mnode)
{
	struct sdma_mmu_node *node =
		container_of(mnode, struct sdma_mmu_node, rb);

	atomic_inc(&node->refcount);
	return 0;
}

/*
 * Return 1 to remove the node from the rb tree and call the remove op.
 *
 * Called with the rb tree lock held.
 */
static int sdma_rb_evict(void *arg, struct mmu_rb_node *mnode,
			 void *evict_arg, bool *stop)
{
	struct sdma_mmu_node *node =
		container_of(mnode, struct sdma_mmu_node, rb);
	struct evict_data *evict_data = evict_arg;

	/* is this node still being used? */
	if (atomic_read(&node->refcount))
		return 0; /* keep this node */

	/* this node will be evicted, add its pages to our count */
	evict_data->cleared += node->npages;

	/* have enough pages been cleared? */
	if (evict_data->cleared >= evict_data->target)
		*stop = true;

	return 1; /* remove this node */
}

static void sdma_rb_remove(void *arg, struct mmu_rb_node *mnode)
{
	struct sdma_mmu_node *node =
		container_of(mnode, struct sdma_mmu_node, rb);

	unpin_sdma_pages(node);
	kfree(node);
}

static int sdma_rb_invalidate(void *arg, struct mmu_rb_node *mnode)
{
	struct sdma_mmu_node *node =
		container_of(mnode, struct sdma_mmu_node, rb);

	if (!atomic_read(&node->refcount))
		return 1;
	return 0;
}
