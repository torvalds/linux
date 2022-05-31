// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause

/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/* Copyright (c) 2008-2019, IBM Corporation */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/net.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>

#include <rdma/iw_cm.h>
#include <rdma/ib_verbs.h>

#include "siw.h"
#include "siw_verbs.h"
#include "siw_mem.h"

/*
 * siw_rx_umem()
 *
 * Receive data of @len into target referenced by @dest_addr.
 *
 * @srx:	Receive Context
 * @umem:	siw representation of target memory
 * @dest_addr:	user virtual address
 * @len:	number of bytes to place
 */
static int siw_rx_umem(struct siw_rx_stream *srx, struct siw_umem *umem,
		       u64 dest_addr, int len)
{
	int copied = 0;

	while (len) {
		struct page *p;
		int pg_off, bytes, rv;
		void *dest;

		p = siw_get_upage(umem, dest_addr);
		if (unlikely(!p)) {
			pr_warn("siw: %s: [QP %u]: bogus addr: %pK, %pK\n",
				__func__, qp_id(rx_qp(srx)),
				(void *)(uintptr_t)dest_addr,
				(void *)(uintptr_t)umem->fp_addr);
			/* siw internal error */
			srx->skb_copied += copied;
			srx->skb_new -= copied;

			return -EFAULT;
		}
		pg_off = dest_addr & ~PAGE_MASK;
		bytes = min(len, (int)PAGE_SIZE - pg_off);

		siw_dbg_qp(rx_qp(srx), "page %pK, bytes=%u\n", p, bytes);

		dest = kmap_atomic(p);
		rv = skb_copy_bits(srx->skb, srx->skb_offset, dest + pg_off,
				   bytes);

		if (unlikely(rv)) {
			kunmap_atomic(dest);
			srx->skb_copied += copied;
			srx->skb_new -= copied;

			pr_warn("siw: [QP %u]: %s, len %d, page %p, rv %d\n",
				qp_id(rx_qp(srx)), __func__, len, p, rv);

			return -EFAULT;
		}
		if (srx->mpa_crc_hd) {
			if (rdma_is_kernel_res(&rx_qp(srx)->base_qp.res)) {
				crypto_shash_update(srx->mpa_crc_hd,
					(u8 *)(dest + pg_off), bytes);
				kunmap_atomic(dest);
			} else {
				kunmap_atomic(dest);
				/*
				 * Do CRC on original, not target buffer.
				 * Some user land applications may
				 * concurrently write the target buffer,
				 * which would yield a broken CRC.
				 * Walking the skb twice is very ineffcient.
				 * Folding the CRC into skb_copy_bits()
				 * would be much better, but is currently
				 * not supported.
				 */
				siw_crc_skb(srx, bytes);
			}
		} else {
			kunmap_atomic(dest);
		}
		srx->skb_offset += bytes;
		copied += bytes;
		len -= bytes;
		dest_addr += bytes;
		pg_off = 0;
	}
	srx->skb_copied += copied;
	srx->skb_new -= copied;

	return copied;
}

static int siw_rx_kva(struct siw_rx_stream *srx, void *kva, int len)
{
	int rv;

	siw_dbg_qp(rx_qp(srx), "kva: 0x%pK, len: %u\n", kva, len);

	rv = skb_copy_bits(srx->skb, srx->skb_offset, kva, len);
	if (unlikely(rv)) {
		pr_warn("siw: [QP %u]: %s, len %d, kva 0x%pK, rv %d\n",
			qp_id(rx_qp(srx)), __func__, len, kva, rv);

		return rv;
	}
	if (srx->mpa_crc_hd)
		crypto_shash_update(srx->mpa_crc_hd, (u8 *)kva, len);

	srx->skb_offset += len;
	srx->skb_copied += len;
	srx->skb_new -= len;

	return len;
}

static int siw_rx_pbl(struct siw_rx_stream *srx, int *pbl_idx,
		      struct siw_mem *mem, u64 addr, int len)
{
	struct siw_pbl *pbl = mem->pbl;
	u64 offset = addr - mem->va;
	int copied = 0;

	while (len) {
		int bytes;
		dma_addr_t buf_addr =
			siw_pbl_get_buffer(pbl, offset, &bytes, pbl_idx);
		if (!buf_addr)
			break;

		bytes = min(bytes, len);
		if (siw_rx_kva(srx, (void *)(uintptr_t)buf_addr, bytes) ==
		    bytes) {
			copied += bytes;
			offset += bytes;
			len -= bytes;
		} else {
			break;
		}
	}
	return copied;
}

/*
 * siw_rresp_check_ntoh()
 *
 * Check incoming RRESP fragment header against expected
 * header values and update expected values for potential next
 * fragment.
 *
 * NOTE: This function must be called only if a RRESP DDP segment
 *       starts but not for fragmented consecutive pieces of an
 *       already started DDP segment.
 */
static int siw_rresp_check_ntoh(struct siw_rx_stream *srx,
				struct siw_rx_fpdu *frx)
{
	struct iwarp_rdma_rresp *rresp = &srx->hdr.rresp;
	struct siw_wqe *wqe = &frx->wqe_active;
	enum ddp_ecode ecode;

	u32 sink_stag = be32_to_cpu(rresp->sink_stag);
	u64 sink_to = be64_to_cpu(rresp->sink_to);

	if (frx->first_ddp_seg) {
		srx->ddp_stag = wqe->sqe.sge[0].lkey;
		srx->ddp_to = wqe->sqe.sge[0].laddr;
		frx->pbl_idx = 0;
	}
	/* Below checks extend beyond the semantics of DDP, and
	 * into RDMAP:
	 * We check if the read response matches exactly the
	 * read request which was send to the remote peer to
	 * trigger this read response. RFC5040/5041 do not
	 * always have a proper error code for the detected
	 * error cases. We choose 'base or bounds error' for
	 * cases where the inbound STag is valid, but offset
	 * or length do not match our response receive state.
	 */
	if (unlikely(srx->ddp_stag != sink_stag)) {
		pr_warn("siw: [QP %u]: rresp stag: %08x != %08x\n",
			qp_id(rx_qp(srx)), sink_stag, srx->ddp_stag);
		ecode = DDP_ECODE_T_INVALID_STAG;
		goto error;
	}
	if (unlikely(srx->ddp_to != sink_to)) {
		pr_warn("siw: [QP %u]: rresp off: %016llx != %016llx\n",
			qp_id(rx_qp(srx)), (unsigned long long)sink_to,
			(unsigned long long)srx->ddp_to);
		ecode = DDP_ECODE_T_BASE_BOUNDS;
		goto error;
	}
	if (unlikely(!frx->more_ddp_segs &&
		     (wqe->processed + srx->fpdu_part_rem != wqe->bytes))) {
		pr_warn("siw: [QP %u]: rresp len: %d != %d\n",
			qp_id(rx_qp(srx)),
			wqe->processed + srx->fpdu_part_rem, wqe->bytes);
		ecode = DDP_ECODE_T_BASE_BOUNDS;
		goto error;
	}
	return 0;
error:
	siw_init_terminate(rx_qp(srx), TERM_ERROR_LAYER_DDP,
			   DDP_ETYPE_TAGGED_BUF, ecode, 0);
	return -EINVAL;
}

/*
 * siw_write_check_ntoh()
 *
 * Check incoming WRITE fragment header against expected
 * header values and update expected values for potential next
 * fragment
 *
 * NOTE: This function must be called only if a WRITE DDP segment
 *       starts but not for fragmented consecutive pieces of an
 *       already started DDP segment.
 */
static int siw_write_check_ntoh(struct siw_rx_stream *srx,
				struct siw_rx_fpdu *frx)
{
	struct iwarp_rdma_write *write = &srx->hdr.rwrite;
	enum ddp_ecode ecode;

	u32 sink_stag = be32_to_cpu(write->sink_stag);
	u64 sink_to = be64_to_cpu(write->sink_to);

	if (frx->first_ddp_seg) {
		srx->ddp_stag = sink_stag;
		srx->ddp_to = sink_to;
		frx->pbl_idx = 0;
	} else {
		if (unlikely(srx->ddp_stag != sink_stag)) {
			pr_warn("siw: [QP %u]: write stag: %08x != %08x\n",
				qp_id(rx_qp(srx)), sink_stag,
				srx->ddp_stag);
			ecode = DDP_ECODE_T_INVALID_STAG;
			goto error;
		}
		if (unlikely(srx->ddp_to != sink_to)) {
			pr_warn("siw: [QP %u]: write off: %016llx != %016llx\n",
				qp_id(rx_qp(srx)),
				(unsigned long long)sink_to,
				(unsigned long long)srx->ddp_to);
			ecode = DDP_ECODE_T_BASE_BOUNDS;
			goto error;
		}
	}
	return 0;
error:
	siw_init_terminate(rx_qp(srx), TERM_ERROR_LAYER_DDP,
			   DDP_ETYPE_TAGGED_BUF, ecode, 0);
	return -EINVAL;
}

/*
 * siw_send_check_ntoh()
 *
 * Check incoming SEND fragment header against expected
 * header values and update expected MSN if no next
 * fragment expected
 *
 * NOTE: This function must be called only if a SEND DDP segment
 *       starts but not for fragmented consecutive pieces of an
 *       already started DDP segment.
 */
static int siw_send_check_ntoh(struct siw_rx_stream *srx,
			       struct siw_rx_fpdu *frx)
{
	struct iwarp_send_inv *send = &srx->hdr.send_inv;
	struct siw_wqe *wqe = &frx->wqe_active;
	enum ddp_ecode ecode;

	u32 ddp_msn = be32_to_cpu(send->ddp_msn);
	u32 ddp_mo = be32_to_cpu(send->ddp_mo);
	u32 ddp_qn = be32_to_cpu(send->ddp_qn);

	if (unlikely(ddp_qn != RDMAP_UNTAGGED_QN_SEND)) {
		pr_warn("siw: [QP %u]: invalid ddp qn %d for send\n",
			qp_id(rx_qp(srx)), ddp_qn);
		ecode = DDP_ECODE_UT_INVALID_QN;
		goto error;
	}
	if (unlikely(ddp_msn != srx->ddp_msn[RDMAP_UNTAGGED_QN_SEND])) {
		pr_warn("siw: [QP %u]: send msn: %u != %u\n",
			qp_id(rx_qp(srx)), ddp_msn,
			srx->ddp_msn[RDMAP_UNTAGGED_QN_SEND]);
		ecode = DDP_ECODE_UT_INVALID_MSN_RANGE;
		goto error;
	}
	if (unlikely(ddp_mo != wqe->processed)) {
		pr_warn("siw: [QP %u], send mo: %u != %u\n",
			qp_id(rx_qp(srx)), ddp_mo, wqe->processed);
		ecode = DDP_ECODE_UT_INVALID_MO;
		goto error;
	}
	if (frx->first_ddp_seg) {
		/* initialize user memory write position */
		frx->sge_idx = 0;
		frx->sge_off = 0;
		frx->pbl_idx = 0;

		/* only valid for SEND_INV and SEND_SE_INV operations */
		srx->inval_stag = be32_to_cpu(send->inval_stag);
	}
	if (unlikely(wqe->bytes < wqe->processed + srx->fpdu_part_rem)) {
		siw_dbg_qp(rx_qp(srx), "receive space short: %d - %d < %d\n",
			   wqe->bytes, wqe->processed, srx->fpdu_part_rem);
		wqe->wc_status = SIW_WC_LOC_LEN_ERR;
		ecode = DDP_ECODE_UT_INVALID_MSN_NOBUF;
		goto error;
	}
	return 0;
error:
	siw_init_terminate(rx_qp(srx), TERM_ERROR_LAYER_DDP,
			   DDP_ETYPE_UNTAGGED_BUF, ecode, 0);
	return -EINVAL;
}

static struct siw_wqe *siw_rqe_get(struct siw_qp *qp)
{
	struct siw_rqe *rqe;
	struct siw_srq *srq;
	struct siw_wqe *wqe = NULL;
	bool srq_event = false;
	unsigned long flags;

	srq = qp->srq;
	if (srq) {
		spin_lock_irqsave(&srq->lock, flags);
		if (unlikely(!srq->num_rqe))
			goto out;

		rqe = &srq->recvq[srq->rq_get % srq->num_rqe];
	} else {
		if (unlikely(!qp->recvq))
			goto out;

		rqe = &qp->recvq[qp->rq_get % qp->attrs.rq_size];
	}
	if (likely(rqe->flags == SIW_WQE_VALID)) {
		int num_sge = rqe->num_sge;

		if (likely(num_sge <= SIW_MAX_SGE)) {
			int i = 0;

			wqe = rx_wqe(&qp->rx_untagged);
			rx_type(wqe) = SIW_OP_RECEIVE;
			wqe->wr_status = SIW_WR_INPROGRESS;
			wqe->bytes = 0;
			wqe->processed = 0;

			wqe->rqe.id = rqe->id;
			wqe->rqe.num_sge = num_sge;

			while (i < num_sge) {
				wqe->rqe.sge[i].laddr = rqe->sge[i].laddr;
				wqe->rqe.sge[i].lkey = rqe->sge[i].lkey;
				wqe->rqe.sge[i].length = rqe->sge[i].length;
				wqe->bytes += wqe->rqe.sge[i].length;
				wqe->mem[i] = NULL;
				i++;
			}
			/* can be re-used by appl */
			smp_store_mb(rqe->flags, 0);
		} else {
			siw_dbg_qp(qp, "too many sge's: %d\n", rqe->num_sge);
			if (srq)
				spin_unlock_irqrestore(&srq->lock, flags);
			return NULL;
		}
		if (!srq) {
			qp->rq_get++;
		} else {
			if (srq->armed) {
				/* Test SRQ limit */
				u32 off = (srq->rq_get + srq->limit) %
					  srq->num_rqe;
				struct siw_rqe *rqe2 = &srq->recvq[off];

				if (!(rqe2->flags & SIW_WQE_VALID)) {
					srq->armed = false;
					srq_event = true;
				}
			}
			srq->rq_get++;
		}
	}
out:
	if (srq) {
		spin_unlock_irqrestore(&srq->lock, flags);
		if (srq_event)
			siw_srq_event(srq, IB_EVENT_SRQ_LIMIT_REACHED);
	}
	return wqe;
}

/*
 * siw_proc_send:
 *
 * Process one incoming SEND and place data into memory referenced by
 * receive wqe.
 *
 * Function supports partially received sends (suspending/resuming
 * current receive wqe processing)
 *
 * return value:
 *	0:       reached the end of a DDP segment
 *	-EAGAIN: to be called again to finish the DDP segment
 */
int siw_proc_send(struct siw_qp *qp)
{
	struct siw_rx_stream *srx = &qp->rx_stream;
	struct siw_rx_fpdu *frx = &qp->rx_untagged;
	struct siw_wqe *wqe;
	u32 data_bytes; /* all data bytes available */
	u32 rcvd_bytes; /* sum of data bytes rcvd */
	int rv = 0;

	if (frx->first_ddp_seg) {
		wqe = siw_rqe_get(qp);
		if (unlikely(!wqe)) {
			siw_init_terminate(qp, TERM_ERROR_LAYER_DDP,
					   DDP_ETYPE_UNTAGGED_BUF,
					   DDP_ECODE_UT_INVALID_MSN_NOBUF, 0);
			return -ENOENT;
		}
	} else {
		wqe = rx_wqe(frx);
	}
	if (srx->state == SIW_GET_DATA_START) {
		rv = siw_send_check_ntoh(srx, frx);
		if (unlikely(rv)) {
			siw_qp_event(qp, IB_EVENT_QP_FATAL);
			return rv;
		}
		if (!srx->fpdu_part_rem) /* zero length SEND */
			return 0;
	}
	data_bytes = min(srx->fpdu_part_rem, srx->skb_new);
	rcvd_bytes = 0;

	/* A zero length SEND will skip below loop */
	while (data_bytes) {
		struct ib_pd *pd;
		struct siw_mem **mem, *mem_p;
		struct siw_sge *sge;
		u32 sge_bytes; /* data bytes avail for SGE */

		sge = &wqe->rqe.sge[frx->sge_idx];

		if (!sge->length) {
			/* just skip empty sge's */
			frx->sge_idx++;
			frx->sge_off = 0;
			frx->pbl_idx = 0;
			continue;
		}
		sge_bytes = min(data_bytes, sge->length - frx->sge_off);
		mem = &wqe->mem[frx->sge_idx];

		/*
		 * check with QP's PD if no SRQ present, SRQ's PD otherwise
		 */
		pd = qp->srq == NULL ? qp->pd : qp->srq->base_srq.pd;

		rv = siw_check_sge(pd, sge, mem, IB_ACCESS_LOCAL_WRITE,
				   frx->sge_off, sge_bytes);
		if (unlikely(rv)) {
			siw_init_terminate(qp, TERM_ERROR_LAYER_DDP,
					   DDP_ETYPE_CATASTROPHIC,
					   DDP_ECODE_CATASTROPHIC, 0);

			siw_qp_event(qp, IB_EVENT_QP_ACCESS_ERR);
			break;
		}
		mem_p = *mem;
		if (mem_p->mem_obj == NULL)
			rv = siw_rx_kva(srx,
				(void *)(uintptr_t)(sge->laddr + frx->sge_off),
				sge_bytes);
		else if (!mem_p->is_pbl)
			rv = siw_rx_umem(srx, mem_p->umem,
					 sge->laddr + frx->sge_off, sge_bytes);
		else
			rv = siw_rx_pbl(srx, &frx->pbl_idx, mem_p,
					sge->laddr + frx->sge_off, sge_bytes);

		if (unlikely(rv != sge_bytes)) {
			wqe->processed += rcvd_bytes;

			siw_init_terminate(qp, TERM_ERROR_LAYER_DDP,
					   DDP_ETYPE_CATASTROPHIC,
					   DDP_ECODE_CATASTROPHIC, 0);
			return -EINVAL;
		}
		frx->sge_off += rv;

		if (frx->sge_off == sge->length) {
			frx->sge_idx++;
			frx->sge_off = 0;
			frx->pbl_idx = 0;
		}
		data_bytes -= rv;
		rcvd_bytes += rv;

		srx->fpdu_part_rem -= rv;
		srx->fpdu_part_rcvd += rv;
	}
	wqe->processed += rcvd_bytes;

	if (!srx->fpdu_part_rem)
		return 0;

	return (rv < 0) ? rv : -EAGAIN;
}

/*
 * siw_proc_write:
 *
 * Place incoming WRITE after referencing and checking target buffer

 * Function supports partially received WRITEs (suspending/resuming
 * current receive processing)
 *
 * return value:
 *	0:       reached the end of a DDP segment
 *	-EAGAIN: to be called again to finish the DDP segment
 */
int siw_proc_write(struct siw_qp *qp)
{
	struct siw_rx_stream *srx = &qp->rx_stream;
	struct siw_rx_fpdu *frx = &qp->rx_tagged;
	struct siw_mem *mem;
	int bytes, rv;

	if (srx->state == SIW_GET_DATA_START) {
		if (!srx->fpdu_part_rem) /* zero length WRITE */
			return 0;

		rv = siw_write_check_ntoh(srx, frx);
		if (unlikely(rv)) {
			siw_qp_event(qp, IB_EVENT_QP_FATAL);
			return rv;
		}
	}
	bytes = min(srx->fpdu_part_rem, srx->skb_new);

	if (frx->first_ddp_seg) {
		struct siw_wqe *wqe = rx_wqe(frx);

		rx_mem(frx) = siw_mem_id2obj(qp->sdev, srx->ddp_stag >> 8);
		if (unlikely(!rx_mem(frx))) {
			siw_dbg_qp(qp,
				   "sink stag not found/invalid, stag 0x%08x\n",
				   srx->ddp_stag);

			siw_init_terminate(qp, TERM_ERROR_LAYER_DDP,
					   DDP_ETYPE_TAGGED_BUF,
					   DDP_ECODE_T_INVALID_STAG, 0);
			return -EINVAL;
		}
		wqe->rqe.num_sge = 1;
		rx_type(wqe) = SIW_OP_WRITE;
		wqe->wr_status = SIW_WR_INPROGRESS;
	}
	mem = rx_mem(frx);

	/*
	 * Check if application re-registered memory with different
	 * key field of STag.
	 */
	if (unlikely(mem->stag != srx->ddp_stag)) {
		siw_init_terminate(qp, TERM_ERROR_LAYER_DDP,
				   DDP_ETYPE_TAGGED_BUF,
				   DDP_ECODE_T_INVALID_STAG, 0);
		return -EINVAL;
	}
	rv = siw_check_mem(qp->pd, mem, srx->ddp_to + srx->fpdu_part_rcvd,
			   IB_ACCESS_REMOTE_WRITE, bytes);
	if (unlikely(rv)) {
		siw_init_terminate(qp, TERM_ERROR_LAYER_DDP,
				   DDP_ETYPE_TAGGED_BUF, siw_tagged_error(-rv),
				   0);

		siw_qp_event(qp, IB_EVENT_QP_ACCESS_ERR);

		return -EINVAL;
	}

	if (mem->mem_obj == NULL)
		rv = siw_rx_kva(srx,
			(void *)(uintptr_t)(srx->ddp_to + srx->fpdu_part_rcvd),
			bytes);
	else if (!mem->is_pbl)
		rv = siw_rx_umem(srx, mem->umem,
				 srx->ddp_to + srx->fpdu_part_rcvd, bytes);
	else
		rv = siw_rx_pbl(srx, &frx->pbl_idx, mem,
				srx->ddp_to + srx->fpdu_part_rcvd, bytes);

	if (unlikely(rv != bytes)) {
		siw_init_terminate(qp, TERM_ERROR_LAYER_DDP,
				   DDP_ETYPE_CATASTROPHIC,
				   DDP_ECODE_CATASTROPHIC, 0);
		return -EINVAL;
	}
	srx->fpdu_part_rem -= rv;
	srx->fpdu_part_rcvd += rv;

	if (!srx->fpdu_part_rem) {
		srx->ddp_to += srx->fpdu_part_rcvd;
		return 0;
	}
	return -EAGAIN;
}

/*
 * Inbound RREQ's cannot carry user data.
 */
int siw_proc_rreq(struct siw_qp *qp)
{
	struct siw_rx_stream *srx = &qp->rx_stream;

	if (!srx->fpdu_part_rem)
		return 0;

	pr_warn("siw: [QP %u]: rreq with mpa len %d\n", qp_id(qp),
		be16_to_cpu(srx->hdr.ctrl.mpa_len));

	return -EPROTO;
}

/*
 * siw_init_rresp:
 *
 * Process inbound RDMA READ REQ. Produce a pseudo READ RESPONSE WQE.
 * Put it at the tail of the IRQ, if there is another WQE currently in
 * transmit processing. If not, make it the current WQE to be processed
 * and schedule transmit processing.
 *
 * Can be called from softirq context and from process
 * context (RREAD socket loopback case!)
 *
 * return value:
 *	0:      success,
 *		failure code otherwise
 */

static int siw_init_rresp(struct siw_qp *qp, struct siw_rx_stream *srx)
{
	struct siw_wqe *tx_work = tx_wqe(qp);
	struct siw_sqe *resp;

	uint64_t raddr = be64_to_cpu(srx->hdr.rreq.sink_to),
		 laddr = be64_to_cpu(srx->hdr.rreq.source_to);
	uint32_t length = be32_to_cpu(srx->hdr.rreq.read_size),
		 lkey = be32_to_cpu(srx->hdr.rreq.source_stag),
		 rkey = be32_to_cpu(srx->hdr.rreq.sink_stag),
		 msn = be32_to_cpu(srx->hdr.rreq.ddp_msn);

	int run_sq = 1, rv = 0;
	unsigned long flags;

	if (unlikely(msn != srx->ddp_msn[RDMAP_UNTAGGED_QN_RDMA_READ])) {
		siw_init_terminate(qp, TERM_ERROR_LAYER_DDP,
				   DDP_ETYPE_UNTAGGED_BUF,
				   DDP_ECODE_UT_INVALID_MSN_RANGE, 0);
		return -EPROTO;
	}
	spin_lock_irqsave(&qp->sq_lock, flags);

	if (unlikely(!qp->attrs.irq_size)) {
		run_sq = 0;
		goto error_irq;
	}
	if (tx_work->wr_status == SIW_WR_IDLE) {
		/*
		 * immediately schedule READ response w/o
		 * consuming IRQ entry: IRQ must be empty.
		 */
		tx_work->processed = 0;
		tx_work->mem[0] = NULL;
		tx_work->wr_status = SIW_WR_QUEUED;
		resp = &tx_work->sqe;
	} else {
		resp = irq_alloc_free(qp);
		run_sq = 0;
	}
	if (likely(resp)) {
		resp->opcode = SIW_OP_READ_RESPONSE;

		resp->sge[0].length = length;
		resp->sge[0].laddr = laddr;
		resp->sge[0].lkey = lkey;

		/* Keep aside message sequence number for potential
		 * error reporting during Read Response generation.
		 */
		resp->sge[1].length = msn;

		resp->raddr = raddr;
		resp->rkey = rkey;
		resp->num_sge = length ? 1 : 0;

		/* RRESP now valid as current TX wqe or placed into IRQ */
		smp_store_mb(resp->flags, SIW_WQE_VALID);
	} else {
error_irq:
		pr_warn("siw: [QP %u]: IRQ exceeded or null, size %d\n",
			qp_id(qp), qp->attrs.irq_size);

		siw_init_terminate(qp, TERM_ERROR_LAYER_RDMAP,
				   RDMAP_ETYPE_REMOTE_OPERATION,
				   RDMAP_ECODE_CATASTROPHIC_STREAM, 0);
		rv = -EPROTO;
	}

	spin_unlock_irqrestore(&qp->sq_lock, flags);

	if (run_sq)
		rv = siw_sq_start(qp);

	return rv;
}

/*
 * Only called at start of Read.Resonse processing.
 * Transfer pending Read from tip of ORQ into currrent rx wqe,
 * but keep ORQ entry valid until Read.Response processing done.
 * No Queue locking needed.
 */
static int siw_orqe_start_rx(struct siw_qp *qp)
{
	struct siw_sqe *orqe;
	struct siw_wqe *wqe = NULL;

	if (unlikely(!qp->attrs.orq_size))
		return -EPROTO;

	/* make sure ORQ indices are current */
	smp_mb();

	orqe = orq_get_current(qp);
	if (READ_ONCE(orqe->flags) & SIW_WQE_VALID) {
		/* RRESP is a TAGGED RDMAP operation */
		wqe = rx_wqe(&qp->rx_tagged);
		wqe->sqe.id = orqe->id;
		wqe->sqe.opcode = orqe->opcode;
		wqe->sqe.sge[0].laddr = orqe->sge[0].laddr;
		wqe->sqe.sge[0].lkey = orqe->sge[0].lkey;
		wqe->sqe.sge[0].length = orqe->sge[0].length;
		wqe->sqe.flags = orqe->flags;
		wqe->sqe.num_sge = 1;
		wqe->bytes = orqe->sge[0].length;
		wqe->processed = 0;
		wqe->mem[0] = NULL;
		/* make sure WQE is completely written before valid */
		smp_wmb();
		wqe->wr_status = SIW_WR_INPROGRESS;

		return 0;
	}
	return -EPROTO;
}

/*
 * siw_proc_rresp:
 *
 * Place incoming RRESP data into memory referenced by RREQ WQE
 * which is at the tip of the ORQ
 *
 * Function supports partially received RRESP's (suspending/resuming
 * current receive processing)
 */
int siw_proc_rresp(struct siw_qp *qp)
{
	struct siw_rx_stream *srx = &qp->rx_stream;
	struct siw_rx_fpdu *frx = &qp->rx_tagged;
	struct siw_wqe *wqe = rx_wqe(frx);
	struct siw_mem **mem, *mem_p;
	struct siw_sge *sge;
	int bytes, rv;

	if (frx->first_ddp_seg) {
		if (unlikely(wqe->wr_status != SIW_WR_IDLE)) {
			pr_warn("siw: [QP %u]: proc RRESP: status %d, op %d\n",
				qp_id(qp), wqe->wr_status, wqe->sqe.opcode);
			rv = -EPROTO;
			goto error_term;
		}
		/*
		 * fetch pending RREQ from orq
		 */
		rv = siw_orqe_start_rx(qp);
		if (rv) {
			pr_warn("siw: [QP %u]: ORQ empty, size %d\n",
				qp_id(qp), qp->attrs.orq_size);
			goto error_term;
		}
		rv = siw_rresp_check_ntoh(srx, frx);
		if (unlikely(rv)) {
			siw_qp_event(qp, IB_EVENT_QP_FATAL);
			return rv;
		}
	} else {
		if (unlikely(wqe->wr_status != SIW_WR_INPROGRESS)) {
			pr_warn("siw: [QP %u]: resume RRESP: status %d\n",
				qp_id(qp), wqe->wr_status);
			rv = -EPROTO;
			goto error_term;
		}
	}
	if (!srx->fpdu_part_rem) /* zero length RRESPONSE */
		return 0;

	sge = wqe->sqe.sge; /* there is only one */
	mem = &wqe->mem[0];

	if (!(*mem)) {
		/*
		 * check target memory which resolves memory on first fragment
		 */
		rv = siw_check_sge(qp->pd, sge, mem, IB_ACCESS_LOCAL_WRITE, 0,
				   wqe->bytes);
		if (unlikely(rv)) {
			siw_dbg_qp(qp, "target mem check: %d\n", rv);
			wqe->wc_status = SIW_WC_LOC_PROT_ERR;

			siw_init_terminate(qp, TERM_ERROR_LAYER_DDP,
					   DDP_ETYPE_TAGGED_BUF,
					   siw_tagged_error(-rv), 0);

			siw_qp_event(qp, IB_EVENT_QP_ACCESS_ERR);

			return -EINVAL;
		}
	}
	mem_p = *mem;

	bytes = min(srx->fpdu_part_rem, srx->skb_new);

	if (mem_p->mem_obj == NULL)
		rv = siw_rx_kva(srx,
			(void *)(uintptr_t)(sge->laddr + wqe->processed),
			bytes);
	else if (!mem_p->is_pbl)
		rv = siw_rx_umem(srx, mem_p->umem, sge->laddr + wqe->processed,
				 bytes);
	else
		rv = siw_rx_pbl(srx, &frx->pbl_idx, mem_p,
				sge->laddr + wqe->processed, bytes);
	if (rv != bytes) {
		wqe->wc_status = SIW_WC_GENERAL_ERR;
		rv = -EINVAL;
		goto error_term;
	}
	srx->fpdu_part_rem -= rv;
	srx->fpdu_part_rcvd += rv;
	wqe->processed += rv;

	if (!srx->fpdu_part_rem) {
		srx->ddp_to += srx->fpdu_part_rcvd;
		return 0;
	}
	return -EAGAIN;

error_term:
	siw_init_terminate(qp, TERM_ERROR_LAYER_DDP, DDP_ETYPE_CATASTROPHIC,
			   DDP_ECODE_CATASTROPHIC, 0);
	return rv;
}

int siw_proc_terminate(struct siw_qp *qp)
{
	struct siw_rx_stream *srx = &qp->rx_stream;
	struct sk_buff *skb = srx->skb;
	struct iwarp_terminate *term = &srx->hdr.terminate;
	union iwarp_hdr term_info;
	u8 *infop = (u8 *)&term_info;
	enum rdma_opcode op;
	u16 to_copy = sizeof(struct iwarp_ctrl);

	pr_warn("siw: got TERMINATE. layer %d, type %d, code %d\n",
		__rdmap_term_layer(term), __rdmap_term_etype(term),
		__rdmap_term_ecode(term));

	if (be32_to_cpu(term->ddp_qn) != RDMAP_UNTAGGED_QN_TERMINATE ||
	    be32_to_cpu(term->ddp_msn) !=
		    qp->rx_stream.ddp_msn[RDMAP_UNTAGGED_QN_TERMINATE] ||
	    be32_to_cpu(term->ddp_mo) != 0) {
		pr_warn("siw: rx bogus TERM [QN x%08x, MSN x%08x, MO x%08x]\n",
			be32_to_cpu(term->ddp_qn), be32_to_cpu(term->ddp_msn),
			be32_to_cpu(term->ddp_mo));
		return -ECONNRESET;
	}
	/*
	 * Receive remaining pieces of TERM if indicated
	 */
	if (!term->flag_m)
		return -ECONNRESET;

	/* Do not take the effort to reassemble a network fragmented
	 * TERM message
	 */
	if (srx->skb_new < sizeof(struct iwarp_ctrl_tagged))
		return -ECONNRESET;

	memset(infop, 0, sizeof(term_info));

	skb_copy_bits(skb, srx->skb_offset, infop, to_copy);

	op = __rdmap_get_opcode(&term_info.ctrl);
	if (op >= RDMAP_TERMINATE)
		goto out;

	infop += to_copy;
	srx->skb_offset += to_copy;
	srx->skb_new -= to_copy;
	srx->skb_copied += to_copy;
	srx->fpdu_part_rcvd += to_copy;
	srx->fpdu_part_rem -= to_copy;

	to_copy = iwarp_pktinfo[op].hdr_len - to_copy;

	/* Again, no network fragmented TERM's */
	if (to_copy + MPA_CRC_SIZE > srx->skb_new)
		return -ECONNRESET;

	skb_copy_bits(skb, srx->skb_offset, infop, to_copy);

	if (term->flag_r) {
		siw_dbg_qp(qp, "TERM reports RDMAP hdr type %u, len %u (%s)\n",
			   op, be16_to_cpu(term_info.ctrl.mpa_len),
			   term->flag_m ? "valid" : "invalid");
	} else if (term->flag_d) {
		siw_dbg_qp(qp, "TERM reports DDP hdr type %u, len %u (%s)\n",
			   op, be16_to_cpu(term_info.ctrl.mpa_len),
			   term->flag_m ? "valid" : "invalid");
	}
out:
	srx->skb_new -= to_copy;
	srx->skb_offset += to_copy;
	srx->skb_copied += to_copy;
	srx->fpdu_part_rcvd += to_copy;
	srx->fpdu_part_rem -= to_copy;

	return -ECONNRESET;
}

static int siw_get_trailer(struct siw_qp *qp, struct siw_rx_stream *srx)
{
	struct sk_buff *skb = srx->skb;
	u8 *tbuf = (u8 *)&srx->trailer.crc - srx->pad;
	__wsum crc_in, crc_own = 0;

	siw_dbg_qp(qp, "expected %d, available %d, pad %u\n",
		   srx->fpdu_part_rem, srx->skb_new, srx->pad);

	if (srx->skb_new < srx->fpdu_part_rem)
		return -EAGAIN;

	skb_copy_bits(skb, srx->skb_offset, tbuf, srx->fpdu_part_rem);

	if (srx->mpa_crc_hd && srx->pad)
		crypto_shash_update(srx->mpa_crc_hd, tbuf, srx->pad);

	srx->skb_new -= srx->fpdu_part_rem;
	srx->skb_offset += srx->fpdu_part_rem;
	srx->skb_copied += srx->fpdu_part_rem;

	if (!srx->mpa_crc_hd)
		return 0;

	/*
	 * CRC32 is computed, transmitted and received directly in NBO,
	 * so there's never a reason to convert byte order.
	 */
	crypto_shash_final(srx->mpa_crc_hd, (u8 *)&crc_own);
	crc_in = (__force __wsum)srx->trailer.crc;

	if (unlikely(crc_in != crc_own)) {
		pr_warn("siw: crc error. in: %08x, own %08x, op %u\n",
			crc_in, crc_own, qp->rx_stream.rdmap_op);

		siw_init_terminate(qp, TERM_ERROR_LAYER_LLP,
				   LLP_ETYPE_MPA,
				   LLP_ECODE_RECEIVED_CRC, 0);
		return -EINVAL;
	}
	return 0;
}

#define MIN_DDP_HDR sizeof(struct iwarp_ctrl_tagged)

static int siw_get_hdr(struct siw_rx_stream *srx)
{
	struct sk_buff *skb = srx->skb;
	struct siw_qp *qp = rx_qp(srx);
	struct iwarp_ctrl *c_hdr = &srx->hdr.ctrl;
	struct siw_rx_fpdu *frx;
	u8 opcode;
	int bytes;

	if (srx->fpdu_part_rcvd < MIN_DDP_HDR) {
		/*
		 * copy a mimimum sized (tagged) DDP frame control part
		 */
		bytes = min_t(int, srx->skb_new,
			      MIN_DDP_HDR - srx->fpdu_part_rcvd);

		skb_copy_bits(skb, srx->skb_offset,
			      (char *)c_hdr + srx->fpdu_part_rcvd, bytes);

		srx->fpdu_part_rcvd += bytes;

		srx->skb_new -= bytes;
		srx->skb_offset += bytes;
		srx->skb_copied += bytes;

		if (srx->fpdu_part_rcvd < MIN_DDP_HDR)
			return -EAGAIN;

		if (unlikely(__ddp_get_version(c_hdr) != DDP_VERSION)) {
			enum ddp_etype etype;
			enum ddp_ecode ecode;

			pr_warn("siw: received ddp version unsupported %d\n",
				__ddp_get_version(c_hdr));

			if (c_hdr->ddp_rdmap_ctrl & DDP_FLAG_TAGGED) {
				etype = DDP_ETYPE_TAGGED_BUF;
				ecode = DDP_ECODE_T_VERSION;
			} else {
				etype = DDP_ETYPE_UNTAGGED_BUF;
				ecode = DDP_ECODE_UT_VERSION;
			}
			siw_init_terminate(rx_qp(srx), TERM_ERROR_LAYER_DDP,
					   etype, ecode, 0);
			return -EINVAL;
		}
		if (unlikely(__rdmap_get_version(c_hdr) != RDMAP_VERSION)) {
			pr_warn("siw: received rdmap version unsupported %d\n",
				__rdmap_get_version(c_hdr));

			siw_init_terminate(rx_qp(srx), TERM_ERROR_LAYER_RDMAP,
					   RDMAP_ETYPE_REMOTE_OPERATION,
					   RDMAP_ECODE_VERSION, 0);
			return -EINVAL;
		}
		opcode = __rdmap_get_opcode(c_hdr);

		if (opcode > RDMAP_TERMINATE) {
			pr_warn("siw: received unknown packet type %u\n",
				opcode);

			siw_init_terminate(rx_qp(srx), TERM_ERROR_LAYER_RDMAP,
					   RDMAP_ETYPE_REMOTE_OPERATION,
					   RDMAP_ECODE_OPCODE, 0);
			return -EINVAL;
		}
		siw_dbg_qp(rx_qp(srx), "new header, opcode %u\n", opcode);
	} else {
		opcode = __rdmap_get_opcode(c_hdr);
	}
	set_rx_fpdu_context(qp, opcode);
	frx = qp->rx_fpdu;

	/*
	 * Figure out len of current hdr: variable length of
	 * iwarp hdr may force us to copy hdr information in
	 * two steps. Only tagged DDP messages are already
	 * completely received.
	 */
	if (iwarp_pktinfo[opcode].hdr_len > sizeof(struct iwarp_ctrl_tagged)) {
		bytes = iwarp_pktinfo[opcode].hdr_len - MIN_DDP_HDR;

		if (srx->skb_new < bytes)
			return -EAGAIN;

		skb_copy_bits(skb, srx->skb_offset,
			      (char *)c_hdr + srx->fpdu_part_rcvd, bytes);

		srx->fpdu_part_rcvd += bytes;

		srx->skb_new -= bytes;
		srx->skb_offset += bytes;
		srx->skb_copied += bytes;
	}

	/*
	 * DDP/RDMAP header receive completed. Check if the current
	 * DDP segment starts a new RDMAP message or continues a previously
	 * started RDMAP message.
	 *
	 * Alternating reception of DDP segments (or FPDUs) from incomplete
	 * tagged and untagged RDMAP messages is supported, as long as
	 * the current tagged or untagged message gets eventually completed
	 * w/o intersection from another message of the same type
	 * (tagged/untagged). E.g., a WRITE can get intersected by a SEND,
	 * but not by a READ RESPONSE etc.
	 */
	if (srx->mpa_crc_hd) {
		/*
		 * Restart CRC computation
		 */
		crypto_shash_init(srx->mpa_crc_hd);
		crypto_shash_update(srx->mpa_crc_hd, (u8 *)c_hdr,
				    srx->fpdu_part_rcvd);
	}
	if (frx->more_ddp_segs) {
		frx->first_ddp_seg = 0;
		if (frx->prev_rdmap_op != opcode) {
			pr_warn("siw: packet intersection: %u : %u\n",
				frx->prev_rdmap_op, opcode);
			/*
			 * The last inbound RDMA operation of same type
			 * (tagged or untagged) is left unfinished.
			 * To complete it in error, make it the current
			 * operation again, even with the header already
			 * overwritten. For error handling, only the opcode
			 * and current rx context are relevant.
			 */
			set_rx_fpdu_context(qp, frx->prev_rdmap_op);
			__rdmap_set_opcode(c_hdr, frx->prev_rdmap_op);
			return -EPROTO;
		}
	} else {
		frx->prev_rdmap_op = opcode;
		frx->first_ddp_seg = 1;
	}
	frx->more_ddp_segs = c_hdr->ddp_rdmap_ctrl & DDP_FLAG_LAST ? 0 : 1;

	return 0;
}

static int siw_check_tx_fence(struct siw_qp *qp)
{
	struct siw_wqe *tx_waiting = tx_wqe(qp);
	struct siw_sqe *rreq;
	int resume_tx = 0, rv = 0;
	unsigned long flags;

	spin_lock_irqsave(&qp->orq_lock, flags);

	/* free current orq entry */
	rreq = orq_get_current(qp);
	WRITE_ONCE(rreq->flags, 0);

	qp->orq_get++;

	if (qp->tx_ctx.orq_fence) {
		if (unlikely(tx_waiting->wr_status != SIW_WR_QUEUED)) {
			pr_warn("siw: [QP %u]: fence resume: bad status %d\n",
				qp_id(qp), tx_waiting->wr_status);
			rv = -EPROTO;
			goto out;
		}
		/* resume SQ processing, if possible */
		if (tx_waiting->sqe.opcode == SIW_OP_READ ||
		    tx_waiting->sqe.opcode == SIW_OP_READ_LOCAL_INV) {

			/* SQ processing was stopped because of a full ORQ */
			rreq = orq_get_free(qp);
			if (unlikely(!rreq)) {
				pr_warn("siw: [QP %u]: no ORQE\n", qp_id(qp));
				rv = -EPROTO;
				goto out;
			}
			siw_read_to_orq(rreq, &tx_waiting->sqe);

			qp->orq_put++;
			qp->tx_ctx.orq_fence = 0;
			resume_tx = 1;

		} else if (siw_orq_empty(qp)) {
			/*
			 * SQ processing was stopped by fenced work request.
			 * Resume since all previous Read's are now completed.
			 */
			qp->tx_ctx.orq_fence = 0;
			resume_tx = 1;
		}
	}
out:
	spin_unlock_irqrestore(&qp->orq_lock, flags);

	if (resume_tx)
		rv = siw_sq_start(qp);

	return rv;
}

/*
 * siw_rdmap_complete()
 *
 * Complete processing of an RDMA message after receiving all
 * DDP segmens or ABort processing after encountering error case.
 *
 *   o SENDs + RRESPs will need for completion,
 *   o RREQs need for  READ RESPONSE initialization
 *   o WRITEs need memory dereferencing
 *
 * TODO: Failed WRITEs need local error to be surfaced.
 */
static int siw_rdmap_complete(struct siw_qp *qp, int error)
{
	struct siw_rx_stream *srx = &qp->rx_stream;
	struct siw_wqe *wqe = rx_wqe(qp->rx_fpdu);
	enum siw_wc_status wc_status = wqe->wc_status;
	u8 opcode = __rdmap_get_opcode(&srx->hdr.ctrl);
	int rv = 0;

	switch (opcode) {
	case RDMAP_SEND_SE:
	case RDMAP_SEND_SE_INVAL:
		wqe->rqe.flags |= SIW_WQE_SOLICITED;
		fallthrough;

	case RDMAP_SEND:
	case RDMAP_SEND_INVAL:
		if (wqe->wr_status == SIW_WR_IDLE)
			break;

		srx->ddp_msn[RDMAP_UNTAGGED_QN_SEND]++;

		if (error != 0 && wc_status == SIW_WC_SUCCESS)
			wc_status = SIW_WC_GENERAL_ERR;
		/*
		 * Handle STag invalidation request
		 */
		if (wc_status == SIW_WC_SUCCESS &&
		    (opcode == RDMAP_SEND_INVAL ||
		     opcode == RDMAP_SEND_SE_INVAL)) {
			rv = siw_invalidate_stag(qp->pd, srx->inval_stag);
			if (rv) {
				siw_init_terminate(
					qp, TERM_ERROR_LAYER_RDMAP,
					rv == -EACCES ?
						RDMAP_ETYPE_REMOTE_PROTECTION :
						RDMAP_ETYPE_REMOTE_OPERATION,
					RDMAP_ECODE_CANNOT_INVALIDATE, 0);

				wc_status = SIW_WC_REM_INV_REQ_ERR;
			}
			rv = siw_rqe_complete(qp, &wqe->rqe, wqe->processed,
					      rv ? 0 : srx->inval_stag,
					      wc_status);
		} else {
			rv = siw_rqe_complete(qp, &wqe->rqe, wqe->processed,
					      0, wc_status);
		}
		siw_wqe_put_mem(wqe, SIW_OP_RECEIVE);
		break;

	case RDMAP_RDMA_READ_RESP:
		if (wqe->wr_status == SIW_WR_IDLE)
			break;

		if (error != 0) {
			if ((srx->state == SIW_GET_HDR &&
			     qp->rx_fpdu->first_ddp_seg) || error == -ENODATA)
				/* possible RREQ in ORQ left untouched */
				break;

			if (wc_status == SIW_WC_SUCCESS)
				wc_status = SIW_WC_GENERAL_ERR;
		} else if (rdma_is_kernel_res(&qp->base_qp.res) &&
			   rx_type(wqe) == SIW_OP_READ_LOCAL_INV) {
			/*
			 * Handle any STag invalidation request
			 */
			rv = siw_invalidate_stag(qp->pd, wqe->sqe.sge[0].lkey);
			if (rv) {
				siw_init_terminate(qp, TERM_ERROR_LAYER_RDMAP,
						   RDMAP_ETYPE_CATASTROPHIC,
						   RDMAP_ECODE_UNSPECIFIED, 0);

				if (wc_status == SIW_WC_SUCCESS) {
					wc_status = SIW_WC_GENERAL_ERR;
					error = rv;
				}
			}
		}
		/*
		 * All errors turn the wqe into signalled.
		 */
		if ((wqe->sqe.flags & SIW_WQE_SIGNALLED) || error != 0)
			rv = siw_sqe_complete(qp, &wqe->sqe, wqe->processed,
					      wc_status);
		siw_wqe_put_mem(wqe, SIW_OP_READ);

		if (!error) {
			rv = siw_check_tx_fence(qp);
		} else {
			/* Disable current ORQ element */
			if (qp->attrs.orq_size)
				WRITE_ONCE(orq_get_current(qp)->flags, 0);
		}
		break;

	case RDMAP_RDMA_READ_REQ:
		if (!error) {
			rv = siw_init_rresp(qp, srx);
			srx->ddp_msn[RDMAP_UNTAGGED_QN_RDMA_READ]++;
		}
		break;

	case RDMAP_RDMA_WRITE:
		if (wqe->wr_status == SIW_WR_IDLE)
			break;

		/*
		 * Free References from memory object if
		 * attached to receive context (inbound WRITE).
		 * While a zero-length WRITE is allowed,
		 * no memory reference got created.
		 */
		if (rx_mem(&qp->rx_tagged)) {
			siw_mem_put(rx_mem(&qp->rx_tagged));
			rx_mem(&qp->rx_tagged) = NULL;
		}
		break;

	default:
		break;
	}
	wqe->wr_status = SIW_WR_IDLE;

	return rv;
}

/*
 * siw_tcp_rx_data()
 *
 * Main routine to consume inbound TCP payload
 *
 * @rd_desc:	read descriptor
 * @skb:	socket buffer
 * @off:	offset in skb
 * @len:	skb->len - offset : payload in skb
 */
int siw_tcp_rx_data(read_descriptor_t *rd_desc, struct sk_buff *skb,
		    unsigned int off, size_t len)
{
	struct siw_qp *qp = rd_desc->arg.data;
	struct siw_rx_stream *srx = &qp->rx_stream;
	int rv;

	srx->skb = skb;
	srx->skb_new = skb->len - off;
	srx->skb_offset = off;
	srx->skb_copied = 0;

	siw_dbg_qp(qp, "new data, len %d\n", srx->skb_new);

	while (srx->skb_new) {
		int run_completion = 1;

		if (unlikely(srx->rx_suspend)) {
			/* Do not process any more data */
			srx->skb_copied += srx->skb_new;
			break;
		}
		switch (srx->state) {
		case SIW_GET_HDR:
			rv = siw_get_hdr(srx);
			if (!rv) {
				srx->fpdu_part_rem =
					be16_to_cpu(srx->hdr.ctrl.mpa_len) -
					srx->fpdu_part_rcvd + MPA_HDR_SIZE;

				if (srx->fpdu_part_rem)
					srx->pad = -srx->fpdu_part_rem & 0x3;
				else
					srx->pad = 0;

				srx->state = SIW_GET_DATA_START;
				srx->fpdu_part_rcvd = 0;
			}
			break;

		case SIW_GET_DATA_MORE:
			/*
			 * Another data fragment of the same DDP segment.
			 * Setting first_ddp_seg = 0 avoids repeating
			 * initializations that shall occur only once per
			 * DDP segment.
			 */
			qp->rx_fpdu->first_ddp_seg = 0;
			fallthrough;

		case SIW_GET_DATA_START:
			/*
			 * Headers will be checked by the opcode-specific
			 * data receive function below.
			 */
			rv = iwarp_pktinfo[qp->rx_stream.rdmap_op].rx_data(qp);
			if (!rv) {
				int mpa_len =
					be16_to_cpu(srx->hdr.ctrl.mpa_len)
					+ MPA_HDR_SIZE;

				srx->fpdu_part_rem = (-mpa_len & 0x3)
						      + MPA_CRC_SIZE;
				srx->fpdu_part_rcvd = 0;
				srx->state = SIW_GET_TRAILER;
			} else {
				if (unlikely(rv == -ECONNRESET))
					run_completion = 0;
				else
					srx->state = SIW_GET_DATA_MORE;
			}
			break;

		case SIW_GET_TRAILER:
			/*
			 * read CRC + any padding
			 */
			rv = siw_get_trailer(qp, srx);
			if (likely(!rv)) {
				/*
				 * FPDU completed.
				 * complete RDMAP message if last fragment
				 */
				srx->state = SIW_GET_HDR;
				srx->fpdu_part_rcvd = 0;

				if (!(srx->hdr.ctrl.ddp_rdmap_ctrl &
				      DDP_FLAG_LAST))
					/* more frags */
					break;

				rv = siw_rdmap_complete(qp, 0);
				run_completion = 0;
			}
			break;

		default:
			pr_warn("QP[%u]: RX out of state\n", qp_id(qp));
			rv = -EPROTO;
			run_completion = 0;
		}
		if (unlikely(rv != 0 && rv != -EAGAIN)) {
			if ((srx->state > SIW_GET_HDR ||
			     qp->rx_fpdu->more_ddp_segs) && run_completion)
				siw_rdmap_complete(qp, rv);

			siw_dbg_qp(qp, "rx error %d, rx state %d\n", rv,
				   srx->state);

			siw_qp_cm_drop(qp, 1);

			break;
		}
		if (rv) {
			siw_dbg_qp(qp, "fpdu fragment, state %d, missing %d\n",
				   srx->state, srx->fpdu_part_rem);
			break;
		}
	}
	return srx->skb_copied;
}
