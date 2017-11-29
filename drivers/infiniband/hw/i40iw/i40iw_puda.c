/*******************************************************************************
*
* Copyright (c) 2015-2016 Intel Corporation.  All rights reserved.
*
* This software is available to you under a choice of one of two
* licenses.  You may choose to be licensed under the terms of the GNU
* General Public License (GPL) Version 2, available from the file
* COPYING in the main directory of this source tree, or the
* OpenFabrics.org BSD license below:
*
*   Redistribution and use in source and binary forms, with or
*   without modification, are permitted provided that the following
*   conditions are met:
*
*    - Redistributions of source code must retain the above
*	copyright notice, this list of conditions and the following
*	disclaimer.
*
*    - Redistributions in binary form must reproduce the above
*	copyright notice, this list of conditions and the following
*	disclaimer in the documentation and/or other materials
*	provided with the distribution.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
* BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
* ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
*******************************************************************************/

#include "i40iw_osdep.h"
#include "i40iw_register.h"
#include "i40iw_status.h"
#include "i40iw_hmc.h"

#include "i40iw_d.h"
#include "i40iw_type.h"
#include "i40iw_p.h"
#include "i40iw_puda.h"

static void i40iw_ieq_receive(struct i40iw_sc_vsi *vsi,
			      struct i40iw_puda_buf *buf);
static void i40iw_ieq_tx_compl(struct i40iw_sc_vsi *vsi, void *sqwrid);
static void i40iw_ilq_putback_rcvbuf(struct i40iw_sc_qp *qp, u32 wqe_idx);
static enum i40iw_status_code i40iw_puda_replenish_rq(struct i40iw_puda_rsrc
						      *rsrc, bool initial);
static void i40iw_ieq_cleanup_qp(struct i40iw_puda_rsrc *ieq, struct i40iw_sc_qp *qp);
/**
 * i40iw_puda_get_listbuf - get buffer from puda list
 * @list: list to use for buffers (ILQ or IEQ)
 */
static struct i40iw_puda_buf *i40iw_puda_get_listbuf(struct list_head *list)
{
	struct i40iw_puda_buf *buf = NULL;

	if (!list_empty(list)) {
		buf = (struct i40iw_puda_buf *)list->next;
		list_del((struct list_head *)&buf->list);
	}
	return buf;
}

/**
 * i40iw_puda_get_bufpool - return buffer from resource
 * @rsrc: resource to use for buffer
 */
struct i40iw_puda_buf *i40iw_puda_get_bufpool(struct i40iw_puda_rsrc *rsrc)
{
	struct i40iw_puda_buf *buf = NULL;
	struct list_head *list = &rsrc->bufpool;
	unsigned long	flags;

	spin_lock_irqsave(&rsrc->bufpool_lock, flags);
	buf = i40iw_puda_get_listbuf(list);
	if (buf)
		rsrc->avail_buf_count--;
	else
		rsrc->stats_buf_alloc_fail++;
	spin_unlock_irqrestore(&rsrc->bufpool_lock, flags);
	return buf;
}

/**
 * i40iw_puda_ret_bufpool - return buffer to rsrc list
 * @rsrc: resource to use for buffer
 * @buf: buffe to return to resouce
 */
void i40iw_puda_ret_bufpool(struct i40iw_puda_rsrc *rsrc,
			    struct i40iw_puda_buf *buf)
{
	unsigned long	flags;

	spin_lock_irqsave(&rsrc->bufpool_lock, flags);
	list_add(&buf->list, &rsrc->bufpool);
	spin_unlock_irqrestore(&rsrc->bufpool_lock, flags);
	rsrc->avail_buf_count++;
}

/**
 * i40iw_puda_post_recvbuf - set wqe for rcv buffer
 * @rsrc: resource ptr
 * @wqe_idx: wqe index to use
 * @buf: puda buffer for rcv q
 * @initial: flag if during init time
 */
static void i40iw_puda_post_recvbuf(struct i40iw_puda_rsrc *rsrc, u32 wqe_idx,
				    struct i40iw_puda_buf *buf, bool initial)
{
	u64 *wqe;
	struct i40iw_sc_qp *qp = &rsrc->qp;
	u64 offset24 = 0;

	qp->qp_uk.rq_wrid_array[wqe_idx] = (uintptr_t)buf;
	wqe = qp->qp_uk.rq_base[wqe_idx].elem;
	i40iw_debug(rsrc->dev, I40IW_DEBUG_PUDA,
		    "%s: wqe_idx= %d buf = %p wqe = %p\n", __func__,
		    wqe_idx, buf, wqe);
	if (!initial)
		get_64bit_val(wqe, 24, &offset24);

	offset24 = (offset24) ? 0 : LS_64(1, I40IWQPSQ_VALID);

	set_64bit_val(wqe, 0, buf->mem.pa);
	set_64bit_val(wqe, 8,
		      LS_64(buf->mem.size, I40IWQPSQ_FRAG_LEN));
	i40iw_insert_wqe_hdr(wqe, offset24);
}

/**
 * i40iw_puda_replenish_rq - post rcv buffers
 * @rsrc: resource to use for buffer
 * @initial: flag if during init time
 */
static enum i40iw_status_code i40iw_puda_replenish_rq(struct i40iw_puda_rsrc *rsrc,
						      bool initial)
{
	u32 i;
	u32 invalid_cnt = rsrc->rxq_invalid_cnt;
	struct i40iw_puda_buf *buf = NULL;

	for (i = 0; i < invalid_cnt; i++) {
		buf = i40iw_puda_get_bufpool(rsrc);
		if (!buf)
			return I40IW_ERR_list_empty;
		i40iw_puda_post_recvbuf(rsrc, rsrc->rx_wqe_idx, buf,
					initial);
		rsrc->rx_wqe_idx =
		    ((rsrc->rx_wqe_idx + 1) % rsrc->rq_size);
		rsrc->rxq_invalid_cnt--;
	}
	return 0;
}

/**
 * i40iw_puda_alloc_buf - allocate mem for buffer
 * @dev: iwarp device
 * @length: length of buffer
 */
static struct i40iw_puda_buf *i40iw_puda_alloc_buf(struct i40iw_sc_dev *dev,
						   u32 length)
{
	struct i40iw_puda_buf *buf = NULL;
	struct i40iw_virt_mem buf_mem;
	enum i40iw_status_code ret;

	ret = i40iw_allocate_virt_mem(dev->hw, &buf_mem,
				      sizeof(struct i40iw_puda_buf));
	if (ret) {
		i40iw_debug(dev, I40IW_DEBUG_PUDA,
			    "%s: error mem for buf\n", __func__);
		return NULL;
	}
	buf = (struct i40iw_puda_buf *)buf_mem.va;
	ret = i40iw_allocate_dma_mem(dev->hw, &buf->mem, length, 1);
	if (ret) {
		i40iw_debug(dev, I40IW_DEBUG_PUDA,
			    "%s: error dma mem for buf\n", __func__);
		i40iw_free_virt_mem(dev->hw, &buf_mem);
		return NULL;
	}
	buf->buf_mem.va = buf_mem.va;
	buf->buf_mem.size = buf_mem.size;
	return buf;
}

/**
 * i40iw_puda_dele_buf - delete buffer back to system
 * @dev: iwarp device
 * @buf: buffer to free
 */
static void i40iw_puda_dele_buf(struct i40iw_sc_dev *dev,
				struct i40iw_puda_buf *buf)
{
	i40iw_free_dma_mem(dev->hw, &buf->mem);
	i40iw_free_virt_mem(dev->hw, &buf->buf_mem);
}

/**
 * i40iw_puda_get_next_send_wqe - return next wqe for processing
 * @qp: puda qp for wqe
 * @wqe_idx: wqe index for caller
 */
static u64 *i40iw_puda_get_next_send_wqe(struct i40iw_qp_uk *qp, u32 *wqe_idx)
{
	u64 *wqe = NULL;
	enum i40iw_status_code ret_code = 0;

	*wqe_idx = I40IW_RING_GETCURRENT_HEAD(qp->sq_ring);
	if (!*wqe_idx)
		qp->swqe_polarity = !qp->swqe_polarity;
	I40IW_RING_MOVE_HEAD(qp->sq_ring, ret_code);
	if (ret_code)
		return wqe;
	wqe = qp->sq_base[*wqe_idx].elem;

	return wqe;
}

/**
 * i40iw_puda_poll_info - poll cq for completion
 * @cq: cq for poll
 * @info: info return for successful completion
 */
static enum i40iw_status_code i40iw_puda_poll_info(struct i40iw_sc_cq *cq,
						   struct i40iw_puda_completion_info *info)
{
	u64 qword0, qword2, qword3;
	u64 *cqe;
	u64 comp_ctx;
	bool valid_bit;
	u32 major_err, minor_err;
	bool error;

	cqe = (u64 *)I40IW_GET_CURRENT_CQ_ELEMENT(&cq->cq_uk);
	get_64bit_val(cqe, 24, &qword3);
	valid_bit = (bool)RS_64(qword3, I40IW_CQ_VALID);

	if (valid_bit != cq->cq_uk.polarity)
		return I40IW_ERR_QUEUE_EMPTY;

	i40iw_debug_buf(cq->dev, I40IW_DEBUG_PUDA, "PUDA CQE", cqe, 32);
	error = (bool)RS_64(qword3, I40IW_CQ_ERROR);
	if (error) {
		i40iw_debug(cq->dev, I40IW_DEBUG_PUDA, "%s receive error\n", __func__);
		major_err = (u32)(RS_64(qword3, I40IW_CQ_MAJERR));
		minor_err = (u32)(RS_64(qword3, I40IW_CQ_MINERR));
		info->compl_error = major_err << 16 | minor_err;
		return I40IW_ERR_CQ_COMPL_ERROR;
	}

	get_64bit_val(cqe, 0, &qword0);
	get_64bit_val(cqe, 16, &qword2);

	info->q_type = (u8)RS_64(qword3, I40IW_CQ_SQ);
	info->qp_id = (u32)RS_64(qword2, I40IWCQ_QPID);

	get_64bit_val(cqe, 8, &comp_ctx);
	info->qp = (struct i40iw_qp_uk *)(unsigned long)comp_ctx;
	info->wqe_idx = (u32)RS_64(qword3, I40IW_CQ_WQEIDX);

	if (info->q_type == I40IW_CQE_QTYPE_RQ) {
		info->vlan_valid = (bool)RS_64(qword3, I40IW_VLAN_TAG_VALID);
		info->l4proto = (u8)RS_64(qword2, I40IW_UDA_L4PROTO);
		info->l3proto = (u8)RS_64(qword2, I40IW_UDA_L3PROTO);
		info->payload_len = (u16)RS_64(qword0, I40IW_UDA_PAYLOADLEN);
	}

	return 0;
}

/**
 * i40iw_puda_poll_completion - processes completion for cq
 * @dev: iwarp device
 * @cq: cq getting interrupt
 * @compl_err: return any completion err
 */
enum i40iw_status_code i40iw_puda_poll_completion(struct i40iw_sc_dev *dev,
						  struct i40iw_sc_cq *cq, u32 *compl_err)
{
	struct i40iw_qp_uk *qp;
	struct i40iw_cq_uk *cq_uk = &cq->cq_uk;
	struct i40iw_puda_completion_info info;
	enum i40iw_status_code ret = 0;
	struct i40iw_puda_buf *buf;
	struct i40iw_puda_rsrc *rsrc;
	void *sqwrid;
	u8 cq_type = cq->cq_type;
	unsigned long	flags;

	if ((cq_type == I40IW_CQ_TYPE_ILQ) || (cq_type == I40IW_CQ_TYPE_IEQ)) {
		rsrc = (cq_type == I40IW_CQ_TYPE_ILQ) ? cq->vsi->ilq : cq->vsi->ieq;
	} else {
		i40iw_debug(dev, I40IW_DEBUG_PUDA, "%s qp_type error\n", __func__);
		return I40IW_ERR_BAD_PTR;
	}
	memset(&info, 0, sizeof(info));
	ret = i40iw_puda_poll_info(cq, &info);
	*compl_err = info.compl_error;
	if (ret == I40IW_ERR_QUEUE_EMPTY)
		return ret;
	if (ret)
		goto done;

	qp = info.qp;
	if (!qp || !rsrc) {
		ret = I40IW_ERR_BAD_PTR;
		goto done;
	}

	if (qp->qp_id != rsrc->qp_id) {
		ret = I40IW_ERR_BAD_PTR;
		goto done;
	}

	if (info.q_type == I40IW_CQE_QTYPE_RQ) {
		buf = (struct i40iw_puda_buf *)(uintptr_t)qp->rq_wrid_array[info.wqe_idx];
		/* Get all the tcpip information in the buf header */
		ret = i40iw_puda_get_tcpip_info(&info, buf);
		if (ret) {
			rsrc->stats_rcvd_pkt_err++;
			if (cq_type == I40IW_CQ_TYPE_ILQ) {
				i40iw_ilq_putback_rcvbuf(&rsrc->qp,
							 info.wqe_idx);
			} else {
				i40iw_puda_ret_bufpool(rsrc, buf);
				i40iw_puda_replenish_rq(rsrc, false);
			}
			goto done;
		}

		rsrc->stats_pkt_rcvd++;
		rsrc->compl_rxwqe_idx = info.wqe_idx;
		i40iw_debug(dev, I40IW_DEBUG_PUDA, "%s RQ completion\n", __func__);
		rsrc->receive(rsrc->vsi, buf);
		if (cq_type == I40IW_CQ_TYPE_ILQ)
			i40iw_ilq_putback_rcvbuf(&rsrc->qp, info.wqe_idx);
		else
			i40iw_puda_replenish_rq(rsrc, false);

	} else {
		i40iw_debug(dev, I40IW_DEBUG_PUDA, "%s SQ completion\n", __func__);
		sqwrid = (void *)(uintptr_t)qp->sq_wrtrk_array[info.wqe_idx].wrid;
		I40IW_RING_SET_TAIL(qp->sq_ring, info.wqe_idx);
		rsrc->xmit_complete(rsrc->vsi, sqwrid);
		spin_lock_irqsave(&rsrc->bufpool_lock, flags);
		rsrc->tx_wqe_avail_cnt++;
		spin_unlock_irqrestore(&rsrc->bufpool_lock, flags);
		if (!list_empty(&rsrc->vsi->ilq->txpend))
			i40iw_puda_send_buf(rsrc->vsi->ilq, NULL);
	}

done:
	I40IW_RING_MOVE_HEAD(cq_uk->cq_ring, ret);
	if (I40IW_RING_GETCURRENT_HEAD(cq_uk->cq_ring) == 0)
		cq_uk->polarity = !cq_uk->polarity;
	/* update cq tail in cq shadow memory also */
	I40IW_RING_MOVE_TAIL(cq_uk->cq_ring);
	set_64bit_val(cq_uk->shadow_area, 0,
		      I40IW_RING_GETCURRENT_HEAD(cq_uk->cq_ring));
	return 0;
}

/**
 * i40iw_puda_send - complete send wqe for transmit
 * @qp: puda qp for send
 * @info: buffer information for transmit
 */
enum i40iw_status_code i40iw_puda_send(struct i40iw_sc_qp *qp,
				       struct i40iw_puda_send_info *info)
{
	u64 *wqe;
	u32 iplen, l4len;
	u64 header[2];
	u32 wqe_idx;
	u8 iipt;

	/* number of 32 bits DWORDS in header */
	l4len = info->tcplen >> 2;
	if (info->ipv4) {
		iipt = 3;
		iplen = 5;
	} else {
		iipt = 1;
		iplen = 10;
	}

	wqe = i40iw_puda_get_next_send_wqe(&qp->qp_uk, &wqe_idx);
	if (!wqe)
		return I40IW_ERR_QP_TOOMANY_WRS_POSTED;
	qp->qp_uk.sq_wrtrk_array[wqe_idx].wrid = (uintptr_t)info->scratch;
	/* Third line of WQE descriptor */
	/* maclen is in words */
	header[0] = LS_64((info->maclen >> 1), I40IW_UDA_QPSQ_MACLEN) |
		    LS_64(iplen, I40IW_UDA_QPSQ_IPLEN) | LS_64(1, I40IW_UDA_QPSQ_L4T) |
		    LS_64(iipt, I40IW_UDA_QPSQ_IIPT) |
		    LS_64(l4len, I40IW_UDA_QPSQ_L4LEN);
	/* Forth line of WQE descriptor */
	header[1] = LS_64(I40IW_OP_TYPE_SEND, I40IW_UDA_QPSQ_OPCODE) |
		    LS_64(1, I40IW_UDA_QPSQ_SIGCOMPL) |
		    LS_64(info->doloopback, I40IW_UDA_QPSQ_DOLOOPBACK) |
		    LS_64(qp->qp_uk.swqe_polarity, I40IW_UDA_QPSQ_VALID);

	set_64bit_val(wqe, 0, info->paddr);
	set_64bit_val(wqe, 8, LS_64(info->len, I40IWQPSQ_FRAG_LEN));
	set_64bit_val(wqe, 16, header[0]);

	i40iw_insert_wqe_hdr(wqe, header[1]);

	i40iw_debug_buf(qp->dev, I40IW_DEBUG_PUDA, "PUDA SEND WQE", wqe, 32);
	i40iw_qp_post_wr(&qp->qp_uk);
	return 0;
}

/**
 * i40iw_puda_send_buf - transmit puda buffer
 * @rsrc: resource to use for buffer
 * @buf: puda buffer to transmit
 */
void i40iw_puda_send_buf(struct i40iw_puda_rsrc *rsrc, struct i40iw_puda_buf *buf)
{
	struct i40iw_puda_send_info info;
	enum i40iw_status_code ret = 0;
	unsigned long	flags;

	spin_lock_irqsave(&rsrc->bufpool_lock, flags);
	/* if no wqe available or not from a completion and we have
	 * pending buffers, we must queue new buffer
	 */
	if (!rsrc->tx_wqe_avail_cnt || (buf && !list_empty(&rsrc->txpend))) {
		list_add_tail(&buf->list, &rsrc->txpend);
		spin_unlock_irqrestore(&rsrc->bufpool_lock, flags);
		rsrc->stats_sent_pkt_q++;
		if (rsrc->type == I40IW_PUDA_RSRC_TYPE_ILQ)
			i40iw_debug(rsrc->dev, I40IW_DEBUG_PUDA,
				    "%s: adding to txpend\n", __func__);
		return;
	}
	rsrc->tx_wqe_avail_cnt--;
	/* if we are coming from a completion and have pending buffers
	 * then Get one from pending list
	 */
	if (!buf) {
		buf = i40iw_puda_get_listbuf(&rsrc->txpend);
		if (!buf)
			goto done;
	}

	info.scratch = (void *)buf;
	info.paddr = buf->mem.pa;
	info.len = buf->totallen;
	info.tcplen = buf->tcphlen;
	info.maclen = buf->maclen;
	info.ipv4 = buf->ipv4;
	info.doloopback = (rsrc->type == I40IW_PUDA_RSRC_TYPE_IEQ);

	ret = i40iw_puda_send(&rsrc->qp, &info);
	if (ret) {
		rsrc->tx_wqe_avail_cnt++;
		rsrc->stats_sent_pkt_q++;
		list_add(&buf->list, &rsrc->txpend);
		if (rsrc->type == I40IW_PUDA_RSRC_TYPE_ILQ)
			i40iw_debug(rsrc->dev, I40IW_DEBUG_PUDA,
				    "%s: adding to puda_send\n", __func__);
	} else {
		rsrc->stats_pkt_sent++;
	}
done:
	spin_unlock_irqrestore(&rsrc->bufpool_lock, flags);
}

/**
 * i40iw_puda_qp_setctx - during init, set qp's context
 * @rsrc: qp's resource
 */
static void i40iw_puda_qp_setctx(struct i40iw_puda_rsrc *rsrc)
{
	struct i40iw_sc_qp *qp = &rsrc->qp;
	u64 *qp_ctx = qp->hw_host_ctx;

	set_64bit_val(qp_ctx, 8, qp->sq_pa);
	set_64bit_val(qp_ctx, 16, qp->rq_pa);

	set_64bit_val(qp_ctx, 24,
		      LS_64(qp->hw_rq_size, I40IWQPC_RQSIZE) |
		      LS_64(qp->hw_sq_size, I40IWQPC_SQSIZE));

	set_64bit_val(qp_ctx, 48, LS_64(rsrc->buf_size, I40IW_UDA_QPC_MAXFRAMESIZE));
	set_64bit_val(qp_ctx, 56, 0);
	set_64bit_val(qp_ctx, 64, 1);

	set_64bit_val(qp_ctx, 136,
		      LS_64(rsrc->cq_id, I40IWQPC_TXCQNUM) |
		      LS_64(rsrc->cq_id, I40IWQPC_RXCQNUM));

	set_64bit_val(qp_ctx, 160, LS_64(1, I40IWQPC_PRIVEN));

	set_64bit_val(qp_ctx, 168,
		      LS_64((uintptr_t)qp, I40IWQPC_QPCOMPCTX));

	set_64bit_val(qp_ctx, 176,
		      LS_64(qp->sq_tph_val, I40IWQPC_SQTPHVAL) |
		      LS_64(qp->rq_tph_val, I40IWQPC_RQTPHVAL) |
		      LS_64(qp->qs_handle, I40IWQPC_QSHANDLE));

	i40iw_debug_buf(rsrc->dev, I40IW_DEBUG_PUDA, "PUDA QP CONTEXT",
			qp_ctx, I40IW_QP_CTX_SIZE);
}

/**
 * i40iw_puda_qp_wqe - setup wqe for qp create
 * @rsrc: resource for qp
 */
static enum i40iw_status_code i40iw_puda_qp_wqe(struct i40iw_sc_dev *dev, struct i40iw_sc_qp *qp)
{
	struct i40iw_sc_cqp *cqp;
	u64 *wqe;
	u64 header;
	struct i40iw_ccq_cqe_info compl_info;
	enum i40iw_status_code status = 0;

	cqp = dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, 0);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	set_64bit_val(wqe, 16, qp->hw_host_ctx_pa);
	set_64bit_val(wqe, 40, qp->shadow_area_pa);
	header = qp->qp_uk.qp_id |
		 LS_64(I40IW_CQP_OP_CREATE_QP, I40IW_CQPSQ_OPCODE) |
		 LS_64(I40IW_QP_TYPE_UDA, I40IW_CQPSQ_QP_QPTYPE) |
		 LS_64(1, I40IW_CQPSQ_QP_CQNUMVALID) |
		 LS_64(2, I40IW_CQPSQ_QP_NEXTIWSTATE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_PUDA, "PUDA CQE", wqe, 32);
	i40iw_sc_cqp_post_sq(cqp);
	status = dev->cqp_ops->poll_for_cqp_op_done(dev->cqp,
						    I40IW_CQP_OP_CREATE_QP,
						    &compl_info);
	return status;
}

/**
 * i40iw_puda_qp_create - create qp for resource
 * @rsrc: resource to use for buffer
 */
static enum i40iw_status_code i40iw_puda_qp_create(struct i40iw_puda_rsrc *rsrc)
{
	struct i40iw_sc_qp *qp = &rsrc->qp;
	struct i40iw_qp_uk *ukqp = &qp->qp_uk;
	enum i40iw_status_code ret = 0;
	u32 sq_size, rq_size, t_size;
	struct i40iw_dma_mem *mem;

	sq_size = rsrc->sq_size * I40IW_QP_WQE_MIN_SIZE;
	rq_size = rsrc->rq_size * I40IW_QP_WQE_MIN_SIZE;
	t_size = (sq_size + rq_size + (I40IW_SHADOW_AREA_SIZE << 3) +
		  I40IW_QP_CTX_SIZE);
	/* Get page aligned memory */
	ret =
	    i40iw_allocate_dma_mem(rsrc->dev->hw, &rsrc->qpmem, t_size,
				   I40IW_HW_PAGE_SIZE);
	if (ret) {
		i40iw_debug(rsrc->dev, I40IW_DEBUG_PUDA, "%s: error dma mem\n", __func__);
		return ret;
	}

	mem = &rsrc->qpmem;
	memset(mem->va, 0, t_size);
	qp->hw_sq_size = i40iw_get_encoded_wqe_size(rsrc->sq_size, false);
	qp->hw_rq_size = i40iw_get_encoded_wqe_size(rsrc->rq_size, false);
	qp->pd = &rsrc->sc_pd;
	qp->qp_type = I40IW_QP_TYPE_UDA;
	qp->dev = rsrc->dev;
	qp->back_qp = (void *)rsrc;
	qp->sq_pa = mem->pa;
	qp->rq_pa = qp->sq_pa + sq_size;
	qp->vsi = rsrc->vsi;
	ukqp->sq_base = mem->va;
	ukqp->rq_base = &ukqp->sq_base[rsrc->sq_size];
	ukqp->shadow_area = ukqp->rq_base[rsrc->rq_size].elem;
	qp->shadow_area_pa = qp->rq_pa + rq_size;
	qp->hw_host_ctx = ukqp->shadow_area + I40IW_SHADOW_AREA_SIZE;
	qp->hw_host_ctx_pa =
		qp->shadow_area_pa + (I40IW_SHADOW_AREA_SIZE << 3);
	ukqp->qp_id = rsrc->qp_id;
	ukqp->sq_wrtrk_array = rsrc->sq_wrtrk_array;
	ukqp->rq_wrid_array = rsrc->rq_wrid_array;

	ukqp->qp_id = rsrc->qp_id;
	ukqp->sq_size = rsrc->sq_size;
	ukqp->rq_size = rsrc->rq_size;

	I40IW_RING_INIT(ukqp->sq_ring, ukqp->sq_size);
	I40IW_RING_INIT(ukqp->initial_ring, ukqp->sq_size);
	I40IW_RING_INIT(ukqp->rq_ring, ukqp->rq_size);

	if (qp->pd->dev->is_pf)
		ukqp->wqe_alloc_reg = (u32 __iomem *)(i40iw_get_hw_addr(qp->pd->dev) +
						    I40E_PFPE_WQEALLOC);
	else
		ukqp->wqe_alloc_reg = (u32 __iomem *)(i40iw_get_hw_addr(qp->pd->dev) +
						    I40E_VFPE_WQEALLOC1);

	qp->user_pri = 0;
	i40iw_qp_add_qos(qp);
	i40iw_puda_qp_setctx(rsrc);
	if (rsrc->dev->ceq_valid)
		ret = i40iw_cqp_qp_create_cmd(rsrc->dev, qp);
	else
		ret = i40iw_puda_qp_wqe(rsrc->dev, qp);
	if (ret) {
		i40iw_qp_rem_qos(qp);
		i40iw_free_dma_mem(rsrc->dev->hw, &rsrc->qpmem);
	}
	return ret;
}

/**
 * i40iw_puda_cq_wqe - setup wqe for cq create
 * @rsrc: resource for cq
 */
static enum i40iw_status_code i40iw_puda_cq_wqe(struct i40iw_sc_dev *dev, struct i40iw_sc_cq *cq)
{
	u64 *wqe;
	struct i40iw_sc_cqp *cqp;
	u64 header;
	struct i40iw_ccq_cqe_info compl_info;
	enum i40iw_status_code status = 0;

	cqp = dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, 0);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	set_64bit_val(wqe, 0, cq->cq_uk.cq_size);
	set_64bit_val(wqe, 8, RS_64_1(cq, 1));
	set_64bit_val(wqe, 16,
		      LS_64(cq->shadow_read_threshold,
			    I40IW_CQPSQ_CQ_SHADOW_READ_THRESHOLD));
	set_64bit_val(wqe, 32, cq->cq_pa);

	set_64bit_val(wqe, 40, cq->shadow_area_pa);

	header = cq->cq_uk.cq_id |
	    LS_64(I40IW_CQP_OP_CREATE_CQ, I40IW_CQPSQ_OPCODE) |
	    LS_64(1, I40IW_CQPSQ_CQ_CHKOVERFLOW) |
	    LS_64(1, I40IW_CQPSQ_CQ_ENCEQEMASK) |
	    LS_64(1, I40IW_CQPSQ_CQ_CEQIDVALID) |
	    LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);
	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(dev, I40IW_DEBUG_PUDA, "PUDA CQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	i40iw_sc_cqp_post_sq(dev->cqp);
	status = dev->cqp_ops->poll_for_cqp_op_done(dev->cqp,
						 I40IW_CQP_OP_CREATE_CQ,
						 &compl_info);
	return status;
}

/**
 * i40iw_puda_cq_create - create cq for resource
 * @rsrc: resource for which cq to create
 */
static enum i40iw_status_code i40iw_puda_cq_create(struct i40iw_puda_rsrc *rsrc)
{
	struct i40iw_sc_dev *dev = rsrc->dev;
	struct i40iw_sc_cq *cq = &rsrc->cq;
	enum i40iw_status_code ret = 0;
	u32 tsize, cqsize;
	struct i40iw_dma_mem *mem;
	struct i40iw_cq_init_info info;
	struct i40iw_cq_uk_init_info *init_info = &info.cq_uk_init_info;

	cq->vsi = rsrc->vsi;
	cqsize = rsrc->cq_size * (sizeof(struct i40iw_cqe));
	tsize = cqsize + sizeof(struct i40iw_cq_shadow_area);
	ret = i40iw_allocate_dma_mem(dev->hw, &rsrc->cqmem, tsize,
				     I40IW_CQ0_ALIGNMENT);
	if (ret)
		return ret;

	mem = &rsrc->cqmem;
	memset(&info, 0, sizeof(info));
	info.dev = dev;
	info.type = (rsrc->type == I40IW_PUDA_RSRC_TYPE_ILQ) ?
			 I40IW_CQ_TYPE_ILQ : I40IW_CQ_TYPE_IEQ;
	info.shadow_read_threshold = rsrc->cq_size >> 2;
	info.ceq_id_valid = true;
	info.cq_base_pa = mem->pa;
	info.shadow_area_pa = mem->pa + cqsize;
	init_info->cq_base = mem->va;
	init_info->shadow_area = (u64 *)((u8 *)mem->va + cqsize);
	init_info->cq_size = rsrc->cq_size;
	init_info->cq_id = rsrc->cq_id;
	info.ceqe_mask = true;
	info.ceq_id_valid = true;
	ret = dev->iw_priv_cq_ops->cq_init(cq, &info);
	if (ret)
		goto error;
	if (rsrc->dev->ceq_valid)
		ret = i40iw_cqp_cq_create_cmd(dev, cq);
	else
		ret = i40iw_puda_cq_wqe(dev, cq);
error:
	if (ret)
		i40iw_free_dma_mem(dev->hw, &rsrc->cqmem);
	return ret;
}

/**
 * i40iw_puda_free_qp - free qp for resource
 * @rsrc: resource for which qp to free
 */
static void i40iw_puda_free_qp(struct i40iw_puda_rsrc *rsrc)
{
	enum i40iw_status_code ret;
	struct i40iw_ccq_cqe_info compl_info;
	struct i40iw_sc_dev *dev = rsrc->dev;

	if (rsrc->dev->ceq_valid) {
		i40iw_cqp_qp_destroy_cmd(dev, &rsrc->qp);
		return;
	}

	ret = dev->iw_priv_qp_ops->qp_destroy(&rsrc->qp,
			0, false, true, true);
	if (ret)
		i40iw_debug(dev, I40IW_DEBUG_PUDA,
			    "%s error puda qp destroy wqe\n",
			    __func__);

	if (!ret) {
		ret = dev->cqp_ops->poll_for_cqp_op_done(dev->cqp,
				I40IW_CQP_OP_DESTROY_QP,
				&compl_info);
		if (ret)
			i40iw_debug(dev, I40IW_DEBUG_PUDA,
				    "%s error puda qp destroy failed\n",
				    __func__);
	}
}

/**
 * i40iw_puda_free_cq - free cq for resource
 * @rsrc: resource for which cq to free
 */
static void i40iw_puda_free_cq(struct i40iw_puda_rsrc *rsrc)
{
	enum i40iw_status_code ret;
	struct i40iw_ccq_cqe_info compl_info;
	struct i40iw_sc_dev *dev = rsrc->dev;

	if (rsrc->dev->ceq_valid) {
		i40iw_cqp_cq_destroy_cmd(dev, &rsrc->cq);
		return;
	}
	ret = dev->iw_priv_cq_ops->cq_destroy(&rsrc->cq, 0, true);

	if (ret)
		i40iw_debug(dev, I40IW_DEBUG_PUDA,
			    "%s error ieq cq destroy\n",
			    __func__);

	if (!ret) {
		ret = dev->cqp_ops->poll_for_cqp_op_done(dev->cqp,
				I40IW_CQP_OP_DESTROY_CQ,
				&compl_info);
		if (ret)
			i40iw_debug(dev, I40IW_DEBUG_PUDA,
				    "%s error ieq qp destroy done\n",
				    __func__);
	}
}

/**
 * i40iw_puda_dele_resources - delete all resources during close
 * @dev: iwarp device
 * @type: type of resource to dele
 * @reset: true if reset chip
 */
void i40iw_puda_dele_resources(struct i40iw_sc_vsi *vsi,
			       enum puda_resource_type type,
			       bool reset)
{
	struct i40iw_sc_dev *dev = vsi->dev;
	struct i40iw_puda_rsrc *rsrc;
	struct i40iw_puda_buf *buf = NULL;
	struct i40iw_puda_buf *nextbuf = NULL;
	struct i40iw_virt_mem *vmem;

	switch (type) {
	case I40IW_PUDA_RSRC_TYPE_ILQ:
		rsrc = vsi->ilq;
		vmem = &vsi->ilq_mem;
		break;
	case I40IW_PUDA_RSRC_TYPE_IEQ:
		rsrc = vsi->ieq;
		vmem = &vsi->ieq_mem;
		break;
	default:
		i40iw_debug(dev, I40IW_DEBUG_PUDA, "%s: error resource type = 0x%x\n",
			    __func__, type);
		return;
	}

	switch (rsrc->completion) {
	case PUDA_HASH_CRC_COMPLETE:
		i40iw_free_hash_desc(rsrc->hash_desc);
		/* fall through */
	case PUDA_QP_CREATED:
		if (!reset)
			i40iw_puda_free_qp(rsrc);

		i40iw_free_dma_mem(dev->hw, &rsrc->qpmem);
		/* fallthrough */
	case PUDA_CQ_CREATED:
		if (!reset)
			i40iw_puda_free_cq(rsrc);

		i40iw_free_dma_mem(dev->hw, &rsrc->cqmem);
		break;
	default:
		i40iw_debug(rsrc->dev, I40IW_DEBUG_PUDA, "%s error no resources\n", __func__);
		break;
	}
	/* Free all allocated puda buffers for both tx and rx */
	buf = rsrc->alloclist;
	while (buf) {
		nextbuf = buf->next;
		i40iw_puda_dele_buf(dev, buf);
		buf = nextbuf;
		rsrc->alloc_buf_count--;
	}
	i40iw_free_virt_mem(dev->hw, vmem);
}

/**
 * i40iw_puda_allocbufs - allocate buffers for resource
 * @rsrc: resource for buffer allocation
 * @count: number of buffers to create
 */
static enum i40iw_status_code i40iw_puda_allocbufs(struct i40iw_puda_rsrc *rsrc,
						   u32 count)
{
	u32 i;
	struct i40iw_puda_buf *buf;
	struct i40iw_puda_buf *nextbuf;

	for (i = 0; i < count; i++) {
		buf = i40iw_puda_alloc_buf(rsrc->dev, rsrc->buf_size);
		if (!buf) {
			rsrc->stats_buf_alloc_fail++;
			return I40IW_ERR_NO_MEMORY;
		}
		i40iw_puda_ret_bufpool(rsrc, buf);
		rsrc->alloc_buf_count++;
		if (!rsrc->alloclist) {
			rsrc->alloclist = buf;
		} else {
			nextbuf = rsrc->alloclist;
			rsrc->alloclist = buf;
			buf->next = nextbuf;
		}
	}
	rsrc->avail_buf_count = rsrc->alloc_buf_count;
	return 0;
}

/**
 * i40iw_puda_create_rsrc - create resouce (ilq or ieq)
 * @dev: iwarp device
 * @info: resource information
 */
enum i40iw_status_code i40iw_puda_create_rsrc(struct i40iw_sc_vsi *vsi,
					      struct i40iw_puda_rsrc_info *info)
{
	struct i40iw_sc_dev *dev = vsi->dev;
	enum i40iw_status_code ret = 0;
	struct i40iw_puda_rsrc *rsrc;
	u32 pudasize;
	u32 sqwridsize, rqwridsize;
	struct i40iw_virt_mem *vmem;

	info->count = 1;
	pudasize = sizeof(struct i40iw_puda_rsrc);
	sqwridsize = info->sq_size * sizeof(struct i40iw_sq_uk_wr_trk_info);
	rqwridsize = info->rq_size * 8;
	switch (info->type) {
	case I40IW_PUDA_RSRC_TYPE_ILQ:
		vmem = &vsi->ilq_mem;
		break;
	case I40IW_PUDA_RSRC_TYPE_IEQ:
		vmem = &vsi->ieq_mem;
		break;
	default:
		return I40IW_NOT_SUPPORTED;
	}
	ret =
	    i40iw_allocate_virt_mem(dev->hw, vmem,
				    pudasize + sqwridsize + rqwridsize);
	if (ret)
		return ret;
	rsrc = (struct i40iw_puda_rsrc *)vmem->va;
	spin_lock_init(&rsrc->bufpool_lock);
	if (info->type == I40IW_PUDA_RSRC_TYPE_ILQ) {
		vsi->ilq = (struct i40iw_puda_rsrc *)vmem->va;
		vsi->ilq_count = info->count;
		rsrc->receive = info->receive;
		rsrc->xmit_complete = info->xmit_complete;
	} else {
		vmem = &vsi->ieq_mem;
		vsi->ieq_count = info->count;
		vsi->ieq = (struct i40iw_puda_rsrc *)vmem->va;
		rsrc->receive = i40iw_ieq_receive;
		rsrc->xmit_complete = i40iw_ieq_tx_compl;
	}

	rsrc->type = info->type;
	rsrc->sq_wrtrk_array = (struct i40iw_sq_uk_wr_trk_info *)((u8 *)vmem->va + pudasize);
	rsrc->rq_wrid_array = (u64 *)((u8 *)vmem->va + pudasize + sqwridsize);
	/* Initialize all ieq lists */
	INIT_LIST_HEAD(&rsrc->bufpool);
	INIT_LIST_HEAD(&rsrc->txpend);

	rsrc->tx_wqe_avail_cnt = info->sq_size - 1;
	dev->iw_pd_ops->pd_init(dev, &rsrc->sc_pd, info->pd_id, -1);
	rsrc->qp_id = info->qp_id;
	rsrc->cq_id = info->cq_id;
	rsrc->sq_size = info->sq_size;
	rsrc->rq_size = info->rq_size;
	rsrc->cq_size = info->rq_size + info->sq_size;
	rsrc->buf_size = info->buf_size;
	rsrc->dev = dev;
	rsrc->vsi = vsi;

	ret = i40iw_puda_cq_create(rsrc);
	if (!ret) {
		rsrc->completion = PUDA_CQ_CREATED;
		ret = i40iw_puda_qp_create(rsrc);
	}
	if (ret) {
		i40iw_debug(dev, I40IW_DEBUG_PUDA, "[%s] error qp_create\n",
			    __func__);
		goto error;
	}
	rsrc->completion = PUDA_QP_CREATED;

	ret = i40iw_puda_allocbufs(rsrc, info->tx_buf_cnt + info->rq_size);
	if (ret) {
		i40iw_debug(dev, I40IW_DEBUG_PUDA, "[%s] error alloc_buf\n",
			    __func__);
		goto error;
	}

	rsrc->rxq_invalid_cnt = info->rq_size;
	ret = i40iw_puda_replenish_rq(rsrc, true);
	if (ret)
		goto error;

	if (info->type == I40IW_PUDA_RSRC_TYPE_IEQ) {
		if (!i40iw_init_hash_desc(&rsrc->hash_desc)) {
			rsrc->check_crc = true;
			rsrc->completion = PUDA_HASH_CRC_COMPLETE;
			ret = 0;
		}
	}

	dev->ccq_ops->ccq_arm(&rsrc->cq);
	return ret;
 error:
	i40iw_puda_dele_resources(vsi, info->type, false);

	return ret;
}

/**
 * i40iw_ilq_putback_rcvbuf - ilq buffer to put back on rq
 * @qp: ilq's qp resource
 * @wqe_idx:  wqe index of completed rcvbuf
 */
static void i40iw_ilq_putback_rcvbuf(struct i40iw_sc_qp *qp, u32 wqe_idx)
{
	u64 *wqe;
	u64 offset24;

	wqe = qp->qp_uk.rq_base[wqe_idx].elem;
	get_64bit_val(wqe, 24, &offset24);
	offset24 = (offset24) ? 0 : LS_64(1, I40IWQPSQ_VALID);
	set_64bit_val(wqe, 24, offset24);
}

/**
 * i40iw_ieq_get_fpdu - given length return fpdu length
 * @length: length if fpdu
 */
static u16 i40iw_ieq_get_fpdu_length(u16 length)
{
	u16 fpdu_len;

	fpdu_len = length + I40IW_IEQ_MPA_FRAMING;
	fpdu_len = (fpdu_len + 3) & 0xfffffffc;
	return fpdu_len;
}

/**
 * i40iw_ieq_copy_to_txbuf - copydata from rcv buf to tx buf
 * @buf: rcv buffer with partial
 * @txbuf: tx buffer for sendign back
 * @buf_offset: rcv buffer offset to copy from
 * @txbuf_offset: at offset in tx buf to copy
 * @length: length of data to copy
 */
static void i40iw_ieq_copy_to_txbuf(struct i40iw_puda_buf *buf,
				    struct i40iw_puda_buf *txbuf,
				    u16 buf_offset, u32 txbuf_offset,
				    u32 length)
{
	void *mem1 = (u8 *)buf->mem.va + buf_offset;
	void *mem2 = (u8 *)txbuf->mem.va + txbuf_offset;

	memcpy(mem2, mem1, length);
}

/**
 * i40iw_ieq_setup_tx_buf - setup tx buffer for partial handling
 * @buf: reeive buffer with partial
 * @txbuf: buffer to prepare
 */
static void i40iw_ieq_setup_tx_buf(struct i40iw_puda_buf *buf,
				   struct i40iw_puda_buf *txbuf)
{
	txbuf->maclen = buf->maclen;
	txbuf->tcphlen = buf->tcphlen;
	txbuf->ipv4 = buf->ipv4;
	txbuf->hdrlen = buf->hdrlen;
	i40iw_ieq_copy_to_txbuf(buf, txbuf, 0, 0, buf->hdrlen);
}

/**
 * i40iw_ieq_check_first_buf - check if rcv buffer's seq is in range
 * @buf: receive exception buffer
 * @fps: first partial sequence number
 */
static void i40iw_ieq_check_first_buf(struct i40iw_puda_buf *buf, u32 fps)
{
	u32 offset;

	if (buf->seqnum < fps) {
		offset = fps - buf->seqnum;
		if (offset > buf->datalen)
			return;
		buf->data += offset;
		buf->datalen -= (u16)offset;
		buf->seqnum = fps;
	}
}

/**
 * i40iw_ieq_compl_pfpdu - write txbuf with full fpdu
 * @ieq: ieq resource
 * @rxlist: ieq's received buffer list
 * @pbufl: temporary list for buffers for fpddu
 * @txbuf: tx buffer for fpdu
 * @fpdu_len: total length of fpdu
 */
static void  i40iw_ieq_compl_pfpdu(struct i40iw_puda_rsrc *ieq,
				   struct list_head *rxlist,
				   struct list_head *pbufl,
				   struct i40iw_puda_buf *txbuf,
				   u16 fpdu_len)
{
	struct i40iw_puda_buf *buf;
	u32 nextseqnum;
	u16 txoffset, bufoffset;

	buf = i40iw_puda_get_listbuf(pbufl);
	if (!buf)
		return;
	nextseqnum = buf->seqnum + fpdu_len;
	txbuf->totallen = buf->hdrlen + fpdu_len;
	txbuf->data = (u8 *)txbuf->mem.va + buf->hdrlen;
	i40iw_ieq_setup_tx_buf(buf, txbuf);

	txoffset = buf->hdrlen;
	bufoffset = (u16)(buf->data - (u8 *)buf->mem.va);

	do {
		if (buf->datalen >= fpdu_len) {
			/* copied full fpdu */
			i40iw_ieq_copy_to_txbuf(buf, txbuf, bufoffset, txoffset, fpdu_len);
			buf->datalen -= fpdu_len;
			buf->data += fpdu_len;
			buf->seqnum = nextseqnum;
			break;
		}
		/* copy partial fpdu */
		i40iw_ieq_copy_to_txbuf(buf, txbuf, bufoffset, txoffset, buf->datalen);
		txoffset += buf->datalen;
		fpdu_len -= buf->datalen;
		i40iw_puda_ret_bufpool(ieq, buf);
		buf = i40iw_puda_get_listbuf(pbufl);
		if (!buf)
			return;
		bufoffset = (u16)(buf->data - (u8 *)buf->mem.va);
	} while (1);

	/* last buffer on the list*/
	if (buf->datalen)
		list_add(&buf->list, rxlist);
	else
		i40iw_puda_ret_bufpool(ieq, buf);
}

/**
 * i40iw_ieq_create_pbufl - create buffer list for single fpdu
 * @rxlist: resource list for receive ieq buffes
 * @pbufl: temp. list for buffers for fpddu
 * @buf: first receive buffer
 * @fpdu_len: total length of fpdu
 */
static enum i40iw_status_code i40iw_ieq_create_pbufl(
						     struct i40iw_pfpdu *pfpdu,
						     struct list_head *rxlist,
						     struct list_head *pbufl,
						     struct i40iw_puda_buf *buf,
						     u16 fpdu_len)
{
	enum i40iw_status_code status = 0;
	struct i40iw_puda_buf *nextbuf;
	u32	nextseqnum;
	u16 plen = fpdu_len - buf->datalen;
	bool done = false;

	nextseqnum = buf->seqnum + buf->datalen;
	do {
		nextbuf = i40iw_puda_get_listbuf(rxlist);
		if (!nextbuf) {
			status = I40IW_ERR_list_empty;
			break;
		}
		list_add_tail(&nextbuf->list, pbufl);
		if (nextbuf->seqnum != nextseqnum) {
			pfpdu->bad_seq_num++;
			status = I40IW_ERR_SEQ_NUM;
			break;
		}
		if (nextbuf->datalen >= plen) {
			done = true;
		} else {
			plen -= nextbuf->datalen;
			nextseqnum = nextbuf->seqnum + nextbuf->datalen;
		}

	} while (!done);

	return status;
}

/**
 * i40iw_ieq_handle_partial - process partial fpdu buffer
 * @ieq: ieq resource
 * @pfpdu: partial management per user qp
 * @buf: receive buffer
 * @fpdu_len: fpdu len in the buffer
 */
static enum i40iw_status_code i40iw_ieq_handle_partial(struct i40iw_puda_rsrc *ieq,
						       struct i40iw_pfpdu *pfpdu,
						       struct i40iw_puda_buf *buf,
						       u16 fpdu_len)
{
	enum i40iw_status_code status = 0;
	u8 *crcptr;
	u32 mpacrc;
	u32 seqnum = buf->seqnum;
	struct list_head pbufl;	/* partial buffer list */
	struct i40iw_puda_buf *txbuf = NULL;
	struct list_head *rxlist = &pfpdu->rxlist;

	INIT_LIST_HEAD(&pbufl);
	list_add(&buf->list, &pbufl);

	status = i40iw_ieq_create_pbufl(pfpdu, rxlist, &pbufl, buf, fpdu_len);
	if (status)
		goto error;

	txbuf = i40iw_puda_get_bufpool(ieq);
	if (!txbuf) {
		pfpdu->no_tx_bufs++;
		status = I40IW_ERR_NO_TXBUFS;
		goto error;
	}

	i40iw_ieq_compl_pfpdu(ieq, rxlist, &pbufl, txbuf, fpdu_len);
	i40iw_ieq_update_tcpip_info(txbuf, fpdu_len, seqnum);
	crcptr = txbuf->data + fpdu_len - 4;
	mpacrc = *(u32 *)crcptr;
	if (ieq->check_crc) {
		status = i40iw_ieq_check_mpacrc(ieq->hash_desc, txbuf->data,
						(fpdu_len - 4), mpacrc);
		if (status) {
			i40iw_debug(ieq->dev, I40IW_DEBUG_IEQ,
				    "%s: error bad crc\n", __func__);
			goto error;
		}
	}

	i40iw_debug_buf(ieq->dev, I40IW_DEBUG_IEQ, "IEQ TX BUFFER",
			txbuf->mem.va, txbuf->totallen);
	i40iw_puda_send_buf(ieq, txbuf);
	pfpdu->rcv_nxt = seqnum + fpdu_len;
	return status;
 error:
	while (!list_empty(&pbufl)) {
		buf = (struct i40iw_puda_buf *)(pbufl.prev);
		list_del(&buf->list);
		list_add(&buf->list, rxlist);
	}
	if (txbuf)
		i40iw_puda_ret_bufpool(ieq, txbuf);
	return status;
}

/**
 * i40iw_ieq_process_buf - process buffer rcvd for ieq
 * @ieq: ieq resource
 * @pfpdu: partial management per user qp
 * @buf: receive buffer
 */
static enum i40iw_status_code i40iw_ieq_process_buf(struct i40iw_puda_rsrc *ieq,
						    struct i40iw_pfpdu *pfpdu,
						    struct i40iw_puda_buf *buf)
{
	u16 fpdu_len = 0;
	u16 datalen = buf->datalen;
	u8 *datap = buf->data;
	u8 *crcptr;
	u16 ioffset = 0;
	u32 mpacrc;
	u32 seqnum = buf->seqnum;
	u16 length = 0;
	u16 full = 0;
	bool partial = false;
	struct i40iw_puda_buf *txbuf;
	struct list_head *rxlist = &pfpdu->rxlist;
	enum i40iw_status_code ret = 0;
	enum i40iw_status_code status = 0;

	ioffset = (u16)(buf->data - (u8 *)buf->mem.va);
	while (datalen) {
		fpdu_len = i40iw_ieq_get_fpdu_length(ntohs(*(__be16 *)datap));
		if (fpdu_len > pfpdu->max_fpdu_data) {
			i40iw_debug(ieq->dev, I40IW_DEBUG_IEQ,
				    "%s: error bad fpdu_len\n", __func__);
			status = I40IW_ERR_MPA_CRC;
			list_add(&buf->list, rxlist);
			return status;
		}

		if (datalen < fpdu_len) {
			partial = true;
			break;
		}
		crcptr = datap + fpdu_len - 4;
		mpacrc = *(u32 *)crcptr;
		if (ieq->check_crc)
			ret = i40iw_ieq_check_mpacrc(ieq->hash_desc,
						     datap, fpdu_len - 4, mpacrc);
		if (ret) {
			status = I40IW_ERR_MPA_CRC;
			list_add(&buf->list, rxlist);
			return status;
		}
		full++;
		pfpdu->fpdu_processed++;
		datap += fpdu_len;
		length += fpdu_len;
		datalen -= fpdu_len;
	}
	if (full) {
		/* copy full pdu's in the txbuf and send them out */
		txbuf = i40iw_puda_get_bufpool(ieq);
		if (!txbuf) {
			pfpdu->no_tx_bufs++;
			status = I40IW_ERR_NO_TXBUFS;
			list_add(&buf->list, rxlist);
			return status;
		}
		/* modify txbuf's buffer header */
		i40iw_ieq_setup_tx_buf(buf, txbuf);
		/* copy full fpdu's to new buffer */
		i40iw_ieq_copy_to_txbuf(buf, txbuf, ioffset, buf->hdrlen,
					length);
		txbuf->totallen = buf->hdrlen + length;

		i40iw_ieq_update_tcpip_info(txbuf, length, buf->seqnum);
		i40iw_puda_send_buf(ieq, txbuf);

		if (!datalen) {
			pfpdu->rcv_nxt = buf->seqnum + length;
			i40iw_puda_ret_bufpool(ieq, buf);
			return status;
		}
		buf->data = datap;
		buf->seqnum = seqnum + length;
		buf->datalen = datalen;
		pfpdu->rcv_nxt = buf->seqnum;
	}
	if (partial)
		status = i40iw_ieq_handle_partial(ieq, pfpdu, buf, fpdu_len);

	return status;
}

/**
 * i40iw_ieq_process_fpdus - process fpdu's buffers on its list
 * @qp: qp for which partial fpdus
 * @ieq: ieq resource
 */
static void i40iw_ieq_process_fpdus(struct i40iw_sc_qp *qp,
				    struct i40iw_puda_rsrc *ieq)
{
	struct i40iw_pfpdu *pfpdu = &qp->pfpdu;
	struct list_head *rxlist = &pfpdu->rxlist;
	struct i40iw_puda_buf *buf;
	enum i40iw_status_code status;

	do {
		if (list_empty(rxlist))
			break;
		buf = i40iw_puda_get_listbuf(rxlist);
		if (!buf) {
			i40iw_debug(ieq->dev, I40IW_DEBUG_IEQ,
				    "%s: error no buf\n", __func__);
			break;
		}
		if (buf->seqnum != pfpdu->rcv_nxt) {
			/* This could be out of order or missing packet */
			pfpdu->out_of_order++;
			list_add(&buf->list, rxlist);
			break;
		}
		/* keep processing buffers from the head of the list */
		status = i40iw_ieq_process_buf(ieq, pfpdu, buf);
		if (status == I40IW_ERR_MPA_CRC) {
			pfpdu->mpa_crc_err = true;
			while (!list_empty(rxlist)) {
				buf = i40iw_puda_get_listbuf(rxlist);
				i40iw_puda_ret_bufpool(ieq, buf);
				pfpdu->crc_err++;
			}
			/* create CQP for AE */
			i40iw_ieq_mpa_crc_ae(ieq->dev, qp);
		}
	} while (!status);
}

/**
 * i40iw_ieq_handle_exception - handle qp's exception
 * @ieq: ieq resource
 * @qp: qp receiving excpetion
 * @buf: receive buffer
 */
static void i40iw_ieq_handle_exception(struct i40iw_puda_rsrc *ieq,
				       struct i40iw_sc_qp *qp,
				       struct i40iw_puda_buf *buf)
{
	struct i40iw_puda_buf *tmpbuf = NULL;
	struct i40iw_pfpdu *pfpdu = &qp->pfpdu;
	u32 *hw_host_ctx = (u32 *)qp->hw_host_ctx;
	u32 rcv_wnd = hw_host_ctx[23];
	/* first partial seq # in q2 */
	u32 fps = qp->q2_buf[16];
	struct list_head *rxlist = &pfpdu->rxlist;
	struct list_head *plist;

	pfpdu->total_ieq_bufs++;

	if (pfpdu->mpa_crc_err) {
		pfpdu->crc_err++;
		goto error;
	}
	if (pfpdu->mode && (fps != pfpdu->fps)) {
		/* clean up qp as it is new partial sequence */
		i40iw_ieq_cleanup_qp(ieq, qp);
		i40iw_debug(ieq->dev, I40IW_DEBUG_IEQ,
			    "%s: restarting new partial\n", __func__);
		pfpdu->mode = false;
	}

	if (!pfpdu->mode) {
		i40iw_debug_buf(ieq->dev, I40IW_DEBUG_IEQ, "Q2 BUFFER", (u64 *)qp->q2_buf, 128);
		/* First_Partial_Sequence_Number check */
		pfpdu->rcv_nxt = fps;
		pfpdu->fps = fps;
		pfpdu->mode = true;
		pfpdu->max_fpdu_data = (buf->ipv4) ? (ieq->vsi->mtu - I40IW_MTU_TO_MSS_IPV4) :
				       (ieq->vsi->mtu - I40IW_MTU_TO_MSS_IPV6);
		pfpdu->pmode_count++;
		INIT_LIST_HEAD(rxlist);
		i40iw_ieq_check_first_buf(buf, fps);
	}

	if (!(rcv_wnd >= (buf->seqnum - pfpdu->rcv_nxt))) {
		pfpdu->bad_seq_num++;
		goto error;
	}

	if (!list_empty(rxlist)) {
		tmpbuf = (struct i40iw_puda_buf *)rxlist->next;
		while ((struct list_head *)tmpbuf != rxlist) {
			if ((int)(buf->seqnum - tmpbuf->seqnum) < 0)
				break;
			plist = &tmpbuf->list;
			tmpbuf = (struct i40iw_puda_buf *)plist->next;
		}
		/* Insert buf before tmpbuf */
		list_add_tail(&buf->list, &tmpbuf->list);
	} else {
		list_add_tail(&buf->list, rxlist);
	}
	i40iw_ieq_process_fpdus(qp, ieq);
	return;
 error:
	i40iw_puda_ret_bufpool(ieq, buf);
}

/**
 * i40iw_ieq_receive - received exception buffer
 * @dev: iwarp device
 * @buf: exception buffer received
 */
static void i40iw_ieq_receive(struct i40iw_sc_vsi *vsi,
			      struct i40iw_puda_buf *buf)
{
	struct i40iw_puda_rsrc *ieq = vsi->ieq;
	struct i40iw_sc_qp *qp = NULL;
	u32 wqe_idx = ieq->compl_rxwqe_idx;

	qp = i40iw_ieq_get_qp(vsi->dev, buf);
	if (!qp) {
		ieq->stats_bad_qp_id++;
		i40iw_puda_ret_bufpool(ieq, buf);
	} else {
		i40iw_ieq_handle_exception(ieq, qp, buf);
	}
	/*
	 * ieq->rx_wqe_idx is used by i40iw_puda_replenish_rq()
	 * on which wqe_idx to start replenish rq
	 */
	if (!ieq->rxq_invalid_cnt)
		ieq->rx_wqe_idx = wqe_idx;
	ieq->rxq_invalid_cnt++;
}

/**
 * i40iw_ieq_tx_compl - put back after sending completed exception buffer
 * @vsi: pointer to the vsi structure
 * @sqwrid: pointer to puda buffer
 */
static void i40iw_ieq_tx_compl(struct i40iw_sc_vsi *vsi, void *sqwrid)
{
	struct i40iw_puda_rsrc *ieq = vsi->ieq;
	struct i40iw_puda_buf *buf = (struct i40iw_puda_buf *)sqwrid;

	i40iw_puda_ret_bufpool(ieq, buf);
	if (!list_empty(&ieq->txpend)) {
		buf = i40iw_puda_get_listbuf(&ieq->txpend);
		i40iw_puda_send_buf(ieq, buf);
	}
}

/**
 * i40iw_ieq_cleanup_qp - qp is being destroyed
 * @ieq: ieq resource
 * @qp: all pending fpdu buffers
 */
static void i40iw_ieq_cleanup_qp(struct i40iw_puda_rsrc *ieq, struct i40iw_sc_qp *qp)
{
	struct i40iw_puda_buf *buf;
	struct i40iw_pfpdu *pfpdu = &qp->pfpdu;
	struct list_head *rxlist = &pfpdu->rxlist;

	if (!pfpdu->mode)
		return;
	while (!list_empty(rxlist)) {
		buf = i40iw_puda_get_listbuf(rxlist);
		i40iw_puda_ret_bufpool(ieq, buf);
	}
}
