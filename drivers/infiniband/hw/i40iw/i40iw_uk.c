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
#include "i40iw_status.h"
#include "i40iw_d.h"
#include "i40iw_user.h"
#include "i40iw_register.h"

static u32 nop_signature = 0x55550000;

/**
 * i40iw_nop_1 - insert a nop wqe and move head. no post work
 * @qp: hw qp ptr
 */
static enum i40iw_status_code i40iw_nop_1(struct i40iw_qp_uk *qp)
{
	u64 header, *wqe;
	u64 *wqe_0 = NULL;
	u32 wqe_idx, peek_head;
	bool signaled = false;

	if (!qp->sq_ring.head)
		return I40IW_ERR_PARAM;

	wqe_idx = I40IW_RING_GETCURRENT_HEAD(qp->sq_ring);
	wqe = qp->sq_base[wqe_idx].elem;

	qp->sq_wrtrk_array[wqe_idx].wqe_size = I40IW_QP_WQE_MIN_SIZE;

	peek_head = (qp->sq_ring.head + 1) % qp->sq_ring.size;
	wqe_0 = qp->sq_base[peek_head].elem;
	if (peek_head)
		wqe_0[3] = LS_64(!qp->swqe_polarity, I40IWQPSQ_VALID);
	else
		wqe_0[3] = LS_64(qp->swqe_polarity, I40IWQPSQ_VALID);

	set_64bit_val(wqe, 0, 0);
	set_64bit_val(wqe, 8, 0);
	set_64bit_val(wqe, 16, 0);

	header = LS_64(I40IWQP_OP_NOP, I40IWQPSQ_OPCODE) |
	    LS_64(signaled, I40IWQPSQ_SIGCOMPL) |
	    LS_64(qp->swqe_polarity, I40IWQPSQ_VALID) | nop_signature++;

	wmb();	/* Memory barrier to ensure data is written before valid bit is set */

	set_64bit_val(wqe, 24, header);
	return 0;
}

/**
 * i40iw_qp_post_wr - post wr to hrdware
 * @qp: hw qp ptr
 */
void i40iw_qp_post_wr(struct i40iw_qp_uk *qp)
{
	u64 temp;
	u32 hw_sq_tail;
	u32 sw_sq_head;

	mb(); /* valid bit is written and loads completed before reading shadow */

	/* read the doorbell shadow area */
	get_64bit_val(qp->shadow_area, 0, &temp);

	hw_sq_tail = (u32)RS_64(temp, I40IW_QP_DBSA_HW_SQ_TAIL);
	sw_sq_head = I40IW_RING_GETCURRENT_HEAD(qp->sq_ring);
	if (sw_sq_head != hw_sq_tail) {
		if (sw_sq_head > qp->initial_ring.head) {
			if ((hw_sq_tail >= qp->initial_ring.head) &&
			    (hw_sq_tail < sw_sq_head)) {
				writel(qp->qp_id, qp->wqe_alloc_reg);
			}
		} else if (sw_sq_head != qp->initial_ring.head) {
			if ((hw_sq_tail >= qp->initial_ring.head) ||
			    (hw_sq_tail < sw_sq_head)) {
				writel(qp->qp_id, qp->wqe_alloc_reg);
			}
		}
	}

	qp->initial_ring.head = qp->sq_ring.head;
}

/**
 * i40iw_qp_get_next_send_wqe - return next wqe ptr
 * @qp: hw qp ptr
 * @wqe_idx: return wqe index
 * @wqe_size: size of sq wqe
 * @total_size: work request length
 * @wr_id: work request id
 */
u64 *i40iw_qp_get_next_send_wqe(struct i40iw_qp_uk *qp,
				u32 *wqe_idx,
				u8 wqe_size,
				u32 total_size,
				u64 wr_id
				)
{
	u64 *wqe = NULL;
	u64 wqe_ptr;
	u32 peek_head = 0;
	u16 offset;
	enum i40iw_status_code ret_code = 0;
	u8 nop_wqe_cnt = 0, i;
	u64 *wqe_0 = NULL;

	*wqe_idx = I40IW_RING_GETCURRENT_HEAD(qp->sq_ring);

	if (!*wqe_idx)
		qp->swqe_polarity = !qp->swqe_polarity;
	wqe_ptr = (uintptr_t)qp->sq_base[*wqe_idx].elem;
	offset = (u16)(wqe_ptr) & 0x7F;
	if ((offset + wqe_size) > I40IW_QP_WQE_MAX_SIZE) {
		nop_wqe_cnt = (u8)(I40IW_QP_WQE_MAX_SIZE - offset) / I40IW_QP_WQE_MIN_SIZE;
		for (i = 0; i < nop_wqe_cnt; i++) {
			i40iw_nop_1(qp);
			I40IW_RING_MOVE_HEAD(qp->sq_ring, ret_code);
			if (ret_code)
				return NULL;
		}

		*wqe_idx = I40IW_RING_GETCURRENT_HEAD(qp->sq_ring);
		if (!*wqe_idx)
			qp->swqe_polarity = !qp->swqe_polarity;
	}

	if (((*wqe_idx & 3) == 1) && (wqe_size == I40IW_WQE_SIZE_64)) {
		i40iw_nop_1(qp);
		I40IW_RING_MOVE_HEAD(qp->sq_ring, ret_code);
		if (ret_code)
			return NULL;
		*wqe_idx = I40IW_RING_GETCURRENT_HEAD(qp->sq_ring);
		if (!*wqe_idx)
			qp->swqe_polarity = !qp->swqe_polarity;
	}
	I40IW_RING_MOVE_HEAD_BY_COUNT(qp->sq_ring,
				      wqe_size / I40IW_QP_WQE_MIN_SIZE, ret_code);
	if (ret_code)
		return NULL;

	wqe = qp->sq_base[*wqe_idx].elem;

	peek_head = I40IW_RING_GETCURRENT_HEAD(qp->sq_ring);
	wqe_0 = qp->sq_base[peek_head].elem;

	if (((peek_head & 3) == 1) || ((peek_head & 3) == 3)) {
		if (RS_64(wqe_0[3], I40IWQPSQ_VALID) != !qp->swqe_polarity)
			wqe_0[3] = LS_64(!qp->swqe_polarity, I40IWQPSQ_VALID);
	}

	qp->sq_wrtrk_array[*wqe_idx].wrid = wr_id;
	qp->sq_wrtrk_array[*wqe_idx].wr_len = total_size;
	qp->sq_wrtrk_array[*wqe_idx].wqe_size = wqe_size;
	return wqe;
}

/**
 * i40iw_set_fragment - set fragment in wqe
 * @wqe: wqe for setting fragment
 * @offset: offset value
 * @sge: sge length and stag
 */
static void i40iw_set_fragment(u64 *wqe, u32 offset, struct i40iw_sge *sge)
{
	if (sge) {
		set_64bit_val(wqe, offset, LS_64(sge->tag_off, I40IWQPSQ_FRAG_TO));
		set_64bit_val(wqe, (offset + 8),
			      (LS_64(sge->len, I40IWQPSQ_FRAG_LEN) |
			       LS_64(sge->stag, I40IWQPSQ_FRAG_STAG)));
	}
}

/**
 * i40iw_qp_get_next_recv_wqe - get next qp's rcv wqe
 * @qp: hw qp ptr
 * @wqe_idx: return wqe index
 */
u64 *i40iw_qp_get_next_recv_wqe(struct i40iw_qp_uk *qp, u32 *wqe_idx)
{
	u64 *wqe = NULL;
	enum i40iw_status_code ret_code;

	if (I40IW_RING_FULL_ERR(qp->rq_ring))
		return NULL;

	I40IW_ATOMIC_RING_MOVE_HEAD(qp->rq_ring, *wqe_idx, ret_code);
	if (ret_code)
		return NULL;
	if (!*wqe_idx)
		qp->rwqe_polarity = !qp->rwqe_polarity;
	/* rq_wqe_size_multiplier is no of qwords in one rq wqe */
	wqe = qp->rq_base[*wqe_idx * (qp->rq_wqe_size_multiplier >> 2)].elem;

	return wqe;
}

/**
 * i40iw_rdma_write - rdma write operation
 * @qp: hw qp ptr
 * @info: post sq information
 * @post_sq: flag to post sq
 */
static enum i40iw_status_code i40iw_rdma_write(struct i40iw_qp_uk *qp,
					       struct i40iw_post_sq_info *info,
					       bool post_sq)
{
	u64 header;
	u64 *wqe;
	struct i40iw_rdma_write *op_info;
	u32 i, wqe_idx;
	u32 total_size = 0, byte_off;
	enum i40iw_status_code ret_code;
	bool read_fence = false;
	u8 wqe_size;

	op_info = &info->op.rdma_write;
	if (op_info->num_lo_sges > qp->max_sq_frag_cnt)
		return I40IW_ERR_INVALID_FRAG_COUNT;

	for (i = 0; i < op_info->num_lo_sges; i++)
		total_size += op_info->lo_sg_list[i].len;

	if (total_size > I40IW_MAX_OUTBOUND_MESSAGE_SIZE)
		return I40IW_ERR_QP_INVALID_MSG_SIZE;

	read_fence |= info->read_fence;

	ret_code = i40iw_fragcnt_to_wqesize_sq(op_info->num_lo_sges, &wqe_size);
	if (ret_code)
		return ret_code;

	wqe = i40iw_qp_get_next_send_wqe(qp, &wqe_idx, wqe_size, total_size, info->wr_id);
	if (!wqe)
		return I40IW_ERR_QP_TOOMANY_WRS_POSTED;
	set_64bit_val(wqe, 16,
		      LS_64(op_info->rem_addr.tag_off, I40IWQPSQ_FRAG_TO));
	if (!op_info->rem_addr.stag)
		return I40IW_ERR_BAD_STAG;

	header = LS_64(op_info->rem_addr.stag, I40IWQPSQ_REMSTAG) |
		 LS_64(I40IWQP_OP_RDMA_WRITE, I40IWQPSQ_OPCODE) |
		 LS_64((op_info->num_lo_sges > 1 ?  (op_info->num_lo_sges - 1) : 0), I40IWQPSQ_ADDFRAGCNT) |
		 LS_64(read_fence, I40IWQPSQ_READFENCE) |
		 LS_64(info->local_fence, I40IWQPSQ_LOCALFENCE) |
		 LS_64(info->signaled, I40IWQPSQ_SIGCOMPL) |
		 LS_64(qp->swqe_polarity, I40IWQPSQ_VALID);

	i40iw_set_fragment(wqe, 0, op_info->lo_sg_list);

	for (i = 1, byte_off = 32; i < op_info->num_lo_sges; i++) {
		i40iw_set_fragment(wqe, byte_off, &op_info->lo_sg_list[i]);
		byte_off += 16;
	}

	wmb(); /* make sure WQE is populated before valid bit is set */

	set_64bit_val(wqe, 24, header);

	if (post_sq)
		i40iw_qp_post_wr(qp);

	return 0;
}

/**
 * i40iw_rdma_read - rdma read command
 * @qp: hw qp ptr
 * @info: post sq information
 * @inv_stag: flag for inv_stag
 * @post_sq: flag to post sq
 */
static enum i40iw_status_code i40iw_rdma_read(struct i40iw_qp_uk *qp,
					      struct i40iw_post_sq_info *info,
					      bool inv_stag,
					      bool post_sq)
{
	u64 *wqe;
	struct i40iw_rdma_read *op_info;
	u64 header;
	u32 wqe_idx;
	enum i40iw_status_code ret_code;
	u8 wqe_size;
	bool local_fence = false;

	op_info = &info->op.rdma_read;
	ret_code = i40iw_fragcnt_to_wqesize_sq(1, &wqe_size);
	if (ret_code)
		return ret_code;
	wqe = i40iw_qp_get_next_send_wqe(qp, &wqe_idx, wqe_size, op_info->lo_addr.len, info->wr_id);
	if (!wqe)
		return I40IW_ERR_QP_TOOMANY_WRS_POSTED;
	local_fence |= info->local_fence;

	set_64bit_val(wqe, 16, LS_64(op_info->rem_addr.tag_off, I40IWQPSQ_FRAG_TO));
	header = LS_64(op_info->rem_addr.stag, I40IWQPSQ_REMSTAG) |
		 LS_64((inv_stag ? I40IWQP_OP_RDMA_READ_LOC_INV : I40IWQP_OP_RDMA_READ), I40IWQPSQ_OPCODE) |
		 LS_64(info->read_fence, I40IWQPSQ_READFENCE) |
		 LS_64(local_fence, I40IWQPSQ_LOCALFENCE) |
		 LS_64(info->signaled, I40IWQPSQ_SIGCOMPL) |
		 LS_64(qp->swqe_polarity, I40IWQPSQ_VALID);

	i40iw_set_fragment(wqe, 0, &op_info->lo_addr);

	wmb(); /* make sure WQE is populated before valid bit is set */

	set_64bit_val(wqe, 24, header);
	if (post_sq)
		i40iw_qp_post_wr(qp);

	return 0;
}

/**
 * i40iw_send - rdma send command
 * @qp: hw qp ptr
 * @info: post sq information
 * @stag_to_inv: stag_to_inv value
 * @post_sq: flag to post sq
 */
static enum i40iw_status_code i40iw_send(struct i40iw_qp_uk *qp,
					 struct i40iw_post_sq_info *info,
					 u32 stag_to_inv,
					 bool post_sq)
{
	u64 *wqe;
	struct i40iw_post_send *op_info;
	u64 header;
	u32 i, wqe_idx, total_size = 0, byte_off;
	enum i40iw_status_code ret_code;
	bool read_fence = false;
	u8 wqe_size;

	op_info = &info->op.send;
	if (qp->max_sq_frag_cnt < op_info->num_sges)
		return I40IW_ERR_INVALID_FRAG_COUNT;

	for (i = 0; i < op_info->num_sges; i++)
		total_size += op_info->sg_list[i].len;
	ret_code = i40iw_fragcnt_to_wqesize_sq(op_info->num_sges, &wqe_size);
	if (ret_code)
		return ret_code;

	wqe = i40iw_qp_get_next_send_wqe(qp, &wqe_idx, wqe_size, total_size, info->wr_id);
	if (!wqe)
		return I40IW_ERR_QP_TOOMANY_WRS_POSTED;

	read_fence |= info->read_fence;
	set_64bit_val(wqe, 16, 0);
	header = LS_64(stag_to_inv, I40IWQPSQ_REMSTAG) |
		 LS_64(info->op_type, I40IWQPSQ_OPCODE) |
		 LS_64((op_info->num_sges > 1 ? (op_info->num_sges - 1) : 0),
		       I40IWQPSQ_ADDFRAGCNT) |
		 LS_64(read_fence, I40IWQPSQ_READFENCE) |
		 LS_64(info->local_fence, I40IWQPSQ_LOCALFENCE) |
		 LS_64(info->signaled, I40IWQPSQ_SIGCOMPL) |
		 LS_64(qp->swqe_polarity, I40IWQPSQ_VALID);

	i40iw_set_fragment(wqe, 0, op_info->sg_list);

	for (i = 1, byte_off = 32; i < op_info->num_sges; i++) {
		i40iw_set_fragment(wqe, byte_off, &op_info->sg_list[i]);
		byte_off += 16;
	}

	wmb(); /* make sure WQE is populated before valid bit is set */

	set_64bit_val(wqe, 24, header);
	if (post_sq)
		i40iw_qp_post_wr(qp);

	return 0;
}

/**
 * i40iw_inline_rdma_write - inline rdma write operation
 * @qp: hw qp ptr
 * @info: post sq information
 * @post_sq: flag to post sq
 */
static enum i40iw_status_code i40iw_inline_rdma_write(struct i40iw_qp_uk *qp,
						      struct i40iw_post_sq_info *info,
						      bool post_sq)
{
	u64 *wqe;
	u8 *dest, *src;
	struct i40iw_inline_rdma_write *op_info;
	u64 header = 0;
	u32 wqe_idx;
	enum i40iw_status_code ret_code;
	bool read_fence = false;
	u8 wqe_size;

	op_info = &info->op.inline_rdma_write;
	if (op_info->len > I40IW_MAX_INLINE_DATA_SIZE)
		return I40IW_ERR_INVALID_INLINE_DATA_SIZE;

	ret_code = i40iw_inline_data_size_to_wqesize(op_info->len, &wqe_size);
	if (ret_code)
		return ret_code;

	wqe = i40iw_qp_get_next_send_wqe(qp, &wqe_idx, wqe_size, op_info->len, info->wr_id);
	if (!wqe)
		return I40IW_ERR_QP_TOOMANY_WRS_POSTED;

	read_fence |= info->read_fence;
	set_64bit_val(wqe, 16,
		      LS_64(op_info->rem_addr.tag_off, I40IWQPSQ_FRAG_TO));

	header = LS_64(op_info->rem_addr.stag, I40IWQPSQ_REMSTAG) |
		 LS_64(I40IWQP_OP_RDMA_WRITE, I40IWQPSQ_OPCODE) |
		 LS_64(op_info->len, I40IWQPSQ_INLINEDATALEN) |
		 LS_64(1, I40IWQPSQ_INLINEDATAFLAG) |
		 LS_64(read_fence, I40IWQPSQ_READFENCE) |
		 LS_64(info->local_fence, I40IWQPSQ_LOCALFENCE) |
		 LS_64(info->signaled, I40IWQPSQ_SIGCOMPL) |
		 LS_64(qp->swqe_polarity, I40IWQPSQ_VALID);

	dest = (u8 *)wqe;
	src = (u8 *)(op_info->data);

	if (op_info->len <= 16) {
		memcpy(dest, src, op_info->len);
	} else {
		memcpy(dest, src, 16);
		src += 16;
		dest = (u8 *)wqe + 32;
		memcpy(dest, src, op_info->len - 16);
	}

	wmb(); /* make sure WQE is populated before valid bit is set */

	set_64bit_val(wqe, 24, header);

	if (post_sq)
		i40iw_qp_post_wr(qp);

	return 0;
}

/**
 * i40iw_inline_send - inline send operation
 * @qp: hw qp ptr
 * @info: post sq information
 * @stag_to_inv: remote stag
 * @post_sq: flag to post sq
 */
static enum i40iw_status_code i40iw_inline_send(struct i40iw_qp_uk *qp,
						struct i40iw_post_sq_info *info,
						u32 stag_to_inv,
						bool post_sq)
{
	u64 *wqe;
	u8 *dest, *src;
	struct i40iw_post_inline_send *op_info;
	u64 header;
	u32 wqe_idx;
	enum i40iw_status_code ret_code;
	bool read_fence = false;
	u8 wqe_size;

	op_info = &info->op.inline_send;
	if (op_info->len > I40IW_MAX_INLINE_DATA_SIZE)
		return I40IW_ERR_INVALID_INLINE_DATA_SIZE;

	ret_code = i40iw_inline_data_size_to_wqesize(op_info->len, &wqe_size);
	if (ret_code)
		return ret_code;

	wqe = i40iw_qp_get_next_send_wqe(qp, &wqe_idx, wqe_size, op_info->len, info->wr_id);
	if (!wqe)
		return I40IW_ERR_QP_TOOMANY_WRS_POSTED;

	read_fence |= info->read_fence;
	header = LS_64(stag_to_inv, I40IWQPSQ_REMSTAG) |
	    LS_64(info->op_type, I40IWQPSQ_OPCODE) |
	    LS_64(op_info->len, I40IWQPSQ_INLINEDATALEN) |
	    LS_64(1, I40IWQPSQ_INLINEDATAFLAG) |
	    LS_64(read_fence, I40IWQPSQ_READFENCE) |
	    LS_64(info->local_fence, I40IWQPSQ_LOCALFENCE) |
	    LS_64(info->signaled, I40IWQPSQ_SIGCOMPL) |
	    LS_64(qp->swqe_polarity, I40IWQPSQ_VALID);

	dest = (u8 *)wqe;
	src = (u8 *)(op_info->data);

	if (op_info->len <= 16) {
		memcpy(dest, src, op_info->len);
	} else {
		memcpy(dest, src, 16);
		src += 16;
		dest = (u8 *)wqe + 32;
		memcpy(dest, src, op_info->len - 16);
	}

	wmb(); /* make sure WQE is populated before valid bit is set */

	set_64bit_val(wqe, 24, header);

	if (post_sq)
		i40iw_qp_post_wr(qp);

	return 0;
}

/**
 * i40iw_stag_local_invalidate - stag invalidate operation
 * @qp: hw qp ptr
 * @info: post sq information
 * @post_sq: flag to post sq
 */
static enum i40iw_status_code i40iw_stag_local_invalidate(struct i40iw_qp_uk *qp,
							  struct i40iw_post_sq_info *info,
							  bool post_sq)
{
	u64 *wqe;
	struct i40iw_inv_local_stag *op_info;
	u64 header;
	u32 wqe_idx;
	bool local_fence = false;

	op_info = &info->op.inv_local_stag;
	local_fence = info->local_fence;

	wqe = i40iw_qp_get_next_send_wqe(qp, &wqe_idx, I40IW_QP_WQE_MIN_SIZE, 0, info->wr_id);
	if (!wqe)
		return I40IW_ERR_QP_TOOMANY_WRS_POSTED;
	set_64bit_val(wqe, 0, 0);
	set_64bit_val(wqe, 8,
		      LS_64(op_info->target_stag, I40IWQPSQ_LOCSTAG));
	set_64bit_val(wqe, 16, 0);
	header = LS_64(I40IW_OP_TYPE_INV_STAG, I40IWQPSQ_OPCODE) |
	    LS_64(info->read_fence, I40IWQPSQ_READFENCE) |
	    LS_64(local_fence, I40IWQPSQ_LOCALFENCE) |
	    LS_64(info->signaled, I40IWQPSQ_SIGCOMPL) |
	    LS_64(qp->swqe_polarity, I40IWQPSQ_VALID);

	wmb(); /* make sure WQE is populated before valid bit is set */

	set_64bit_val(wqe, 24, header);

	if (post_sq)
		i40iw_qp_post_wr(qp);

	return 0;
}

/**
 * i40iw_mw_bind - Memory Window bind operation
 * @qp: hw qp ptr
 * @info: post sq information
 * @post_sq: flag to post sq
 */
static enum i40iw_status_code i40iw_mw_bind(struct i40iw_qp_uk *qp,
					    struct i40iw_post_sq_info *info,
					    bool post_sq)
{
	u64 *wqe;
	struct i40iw_bind_window *op_info;
	u64 header;
	u32 wqe_idx;
	bool local_fence = false;

	op_info = &info->op.bind_window;

	local_fence |= info->local_fence;
	wqe = i40iw_qp_get_next_send_wqe(qp, &wqe_idx, I40IW_QP_WQE_MIN_SIZE, 0, info->wr_id);
	if (!wqe)
		return I40IW_ERR_QP_TOOMANY_WRS_POSTED;
	set_64bit_val(wqe, 0, (uintptr_t)op_info->va);
	set_64bit_val(wqe, 8,
		      LS_64(op_info->mr_stag, I40IWQPSQ_PARENTMRSTAG) |
		      LS_64(op_info->mw_stag, I40IWQPSQ_MWSTAG));
	set_64bit_val(wqe, 16, op_info->bind_length);
	header = LS_64(I40IW_OP_TYPE_BIND_MW, I40IWQPSQ_OPCODE) |
	    LS_64(((op_info->enable_reads << 2) |
		   (op_info->enable_writes << 3)),
		  I40IWQPSQ_STAGRIGHTS) |
	    LS_64((op_info->addressing_type == I40IW_ADDR_TYPE_VA_BASED ?  1 : 0),
		  I40IWQPSQ_VABASEDTO) |
	    LS_64(info->read_fence, I40IWQPSQ_READFENCE) |
	    LS_64(local_fence, I40IWQPSQ_LOCALFENCE) |
	    LS_64(info->signaled, I40IWQPSQ_SIGCOMPL) |
	    LS_64(qp->swqe_polarity, I40IWQPSQ_VALID);

	wmb(); /* make sure WQE is populated before valid bit is set */

	set_64bit_val(wqe, 24, header);

	if (post_sq)
		i40iw_qp_post_wr(qp);

	return 0;
}

/**
 * i40iw_post_receive - post receive wqe
 * @qp: hw qp ptr
 * @info: post rq information
 */
static enum i40iw_status_code i40iw_post_receive(struct i40iw_qp_uk *qp,
						 struct i40iw_post_rq_info *info)
{
	u64 *wqe;
	u64 header;
	u32 total_size = 0, wqe_idx, i, byte_off;

	if (qp->max_rq_frag_cnt < info->num_sges)
		return I40IW_ERR_INVALID_FRAG_COUNT;
	for (i = 0; i < info->num_sges; i++)
		total_size += info->sg_list[i].len;
	wqe = i40iw_qp_get_next_recv_wqe(qp, &wqe_idx);
	if (!wqe)
		return I40IW_ERR_QP_TOOMANY_WRS_POSTED;

	qp->rq_wrid_array[wqe_idx] = info->wr_id;
	set_64bit_val(wqe, 16, 0);

	header = LS_64((info->num_sges > 1 ? (info->num_sges - 1) : 0),
		       I40IWQPSQ_ADDFRAGCNT) |
	    LS_64(qp->rwqe_polarity, I40IWQPSQ_VALID);

	i40iw_set_fragment(wqe, 0, info->sg_list);

	for (i = 1, byte_off = 32; i < info->num_sges; i++) {
		i40iw_set_fragment(wqe, byte_off, &info->sg_list[i]);
		byte_off += 16;
	}

	wmb(); /* make sure WQE is populated before valid bit is set */

	set_64bit_val(wqe, 24, header);

	return 0;
}

/**
 * i40iw_cq_request_notification - cq notification request (door bell)
 * @cq: hw cq
 * @cq_notify: notification type
 */
static void i40iw_cq_request_notification(struct i40iw_cq_uk *cq,
					  enum i40iw_completion_notify cq_notify)
{
	u64 temp_val;
	u16 sw_cq_sel;
	u8 arm_next_se = 0;
	u8 arm_next = 0;
	u8 arm_seq_num;

	get_64bit_val(cq->shadow_area, 32, &temp_val);
	arm_seq_num = (u8)RS_64(temp_val, I40IW_CQ_DBSA_ARM_SEQ_NUM);
	arm_seq_num++;

	sw_cq_sel = (u16)RS_64(temp_val, I40IW_CQ_DBSA_SW_CQ_SELECT);
	arm_next_se = (u8)RS_64(temp_val, I40IW_CQ_DBSA_ARM_NEXT_SE);
	arm_next_se |= 1;
	if (cq_notify == IW_CQ_COMPL_EVENT)
		arm_next = 1;
	temp_val = LS_64(arm_seq_num, I40IW_CQ_DBSA_ARM_SEQ_NUM) |
	    LS_64(sw_cq_sel, I40IW_CQ_DBSA_SW_CQ_SELECT) |
	    LS_64(arm_next_se, I40IW_CQ_DBSA_ARM_NEXT_SE) |
	    LS_64(arm_next, I40IW_CQ_DBSA_ARM_NEXT);

	set_64bit_val(cq->shadow_area, 32, temp_val);

	wmb(); /* make sure WQE is populated before valid bit is set */

	writel(cq->cq_id, cq->cqe_alloc_reg);
}

/**
 * i40iw_cq_post_entries - update tail in shadow memory
 * @cq: hw cq
 * @count: # of entries processed
 */
static enum i40iw_status_code i40iw_cq_post_entries(struct i40iw_cq_uk *cq,
						    u8 count)
{
	I40IW_RING_MOVE_TAIL_BY_COUNT(cq->cq_ring, count);
	set_64bit_val(cq->shadow_area, 0,
		      I40IW_RING_GETCURRENT_HEAD(cq->cq_ring));
	return 0;
}

/**
 * i40iw_cq_poll_completion - get cq completion info
 * @cq: hw cq
 * @info: cq poll information returned
 */
static enum i40iw_status_code i40iw_cq_poll_completion(struct i40iw_cq_uk *cq,
						       struct i40iw_cq_poll_info *info)
{
	u64 comp_ctx, qword0, qword2, qword3, wqe_qword;
	u64 *cqe, *sw_wqe;
	struct i40iw_qp_uk *qp;
	struct i40iw_ring *pring = NULL;
	u32 wqe_idx, q_type, array_idx = 0;
	enum i40iw_status_code ret_code = 0;
	bool move_cq_head = true;
	u8 polarity;
	u8 addl_wqes = 0;

	if (cq->avoid_mem_cflct)
		cqe = (u64 *)I40IW_GET_CURRENT_EXTENDED_CQ_ELEMENT(cq);
	else
		cqe = (u64 *)I40IW_GET_CURRENT_CQ_ELEMENT(cq);

	get_64bit_val(cqe, 24, &qword3);
	polarity = (u8)RS_64(qword3, I40IW_CQ_VALID);

	if (polarity != cq->polarity)
		return I40IW_ERR_QUEUE_EMPTY;

	q_type = (u8)RS_64(qword3, I40IW_CQ_SQ);
	info->error = (bool)RS_64(qword3, I40IW_CQ_ERROR);
	if (info->error) {
		info->comp_status = I40IW_COMPL_STATUS_FLUSHED;
		info->major_err = (bool)RS_64(qword3, I40IW_CQ_MAJERR);
		info->minor_err = (bool)RS_64(qword3, I40IW_CQ_MINERR);
	} else {
		info->comp_status = I40IW_COMPL_STATUS_SUCCESS;
	}

	get_64bit_val(cqe, 0, &qword0);
	get_64bit_val(cqe, 16, &qword2);

	info->tcp_seq_num = (u32)RS_64(qword0, I40IWCQ_TCPSEQNUM);

	info->qp_id = (u32)RS_64(qword2, I40IWCQ_QPID);

	get_64bit_val(cqe, 8, &comp_ctx);

	info->solicited_event = (bool)RS_64(qword3, I40IWCQ_SOEVENT);
	info->is_srq = (bool)RS_64(qword3, I40IWCQ_SRQ);

	qp = (struct i40iw_qp_uk *)(unsigned long)comp_ctx;
	if (!qp) {
		ret_code = I40IW_ERR_QUEUE_DESTROYED;
		goto exit;
	}
	wqe_idx = (u32)RS_64(qword3, I40IW_CQ_WQEIDX);
	info->qp_handle = (i40iw_qp_handle)(unsigned long)qp;

	if (q_type == I40IW_CQE_QTYPE_RQ) {
		array_idx = (wqe_idx * 4) / qp->rq_wqe_size_multiplier;
		if (info->comp_status == I40IW_COMPL_STATUS_FLUSHED) {
			info->wr_id = qp->rq_wrid_array[qp->rq_ring.tail];
			array_idx = qp->rq_ring.tail;
		} else {
			info->wr_id = qp->rq_wrid_array[array_idx];
		}

		info->op_type = I40IW_OP_TYPE_REC;
		if (qword3 & I40IWCQ_STAG_MASK) {
			info->stag_invalid_set = true;
			info->inv_stag = (u32)RS_64(qword2, I40IWCQ_INVSTAG);
		} else {
			info->stag_invalid_set = false;
		}
		info->bytes_xfered = (u32)RS_64(qword0, I40IWCQ_PAYLDLEN);
		I40IW_RING_SET_TAIL(qp->rq_ring, array_idx + 1);
		pring = &qp->rq_ring;
	} else {
		if (qp->first_sq_wq) {
			qp->first_sq_wq = false;
			if (!wqe_idx && (qp->sq_ring.head == qp->sq_ring.tail)) {
				I40IW_RING_MOVE_HEAD_NOCHECK(cq->cq_ring);
				I40IW_RING_MOVE_TAIL(cq->cq_ring);
				set_64bit_val(cq->shadow_area, 0,
					      I40IW_RING_GETCURRENT_HEAD(cq->cq_ring));
				memset(info, 0, sizeof(struct i40iw_cq_poll_info));
				return i40iw_cq_poll_completion(cq, info);
			}
		}

		if (info->comp_status != I40IW_COMPL_STATUS_FLUSHED) {
			info->wr_id = qp->sq_wrtrk_array[wqe_idx].wrid;
			info->bytes_xfered = qp->sq_wrtrk_array[wqe_idx].wr_len;

			info->op_type = (u8)RS_64(qword3, I40IWCQ_OP);
			sw_wqe = qp->sq_base[wqe_idx].elem;
			get_64bit_val(sw_wqe, 24, &wqe_qword);

			addl_wqes = qp->sq_wrtrk_array[wqe_idx].wqe_size / I40IW_QP_WQE_MIN_SIZE;
			I40IW_RING_SET_TAIL(qp->sq_ring, (wqe_idx + addl_wqes));
		} else {
			do {
				u8 op_type;
				u32 tail;

				tail = qp->sq_ring.tail;
				sw_wqe = qp->sq_base[tail].elem;
				get_64bit_val(sw_wqe, 24, &wqe_qword);
				op_type = (u8)RS_64(wqe_qword, I40IWQPSQ_OPCODE);
				info->op_type = op_type;
				addl_wqes = qp->sq_wrtrk_array[tail].wqe_size / I40IW_QP_WQE_MIN_SIZE;
				I40IW_RING_SET_TAIL(qp->sq_ring, (tail + addl_wqes));
				if (op_type != I40IWQP_OP_NOP) {
					info->wr_id = qp->sq_wrtrk_array[tail].wrid;
					info->bytes_xfered = qp->sq_wrtrk_array[tail].wr_len;
					break;
				}
			} while (1);
		}
		pring = &qp->sq_ring;
	}

	ret_code = 0;

exit:
	if (!ret_code &&
	    (info->comp_status == I40IW_COMPL_STATUS_FLUSHED))
		if (pring && (I40IW_RING_MORE_WORK(*pring)))
			move_cq_head = false;

	if (move_cq_head) {
		I40IW_RING_MOVE_HEAD_NOCHECK(cq->cq_ring);

		if (I40IW_RING_GETCURRENT_HEAD(cq->cq_ring) == 0)
			cq->polarity ^= 1;

		I40IW_RING_MOVE_TAIL(cq->cq_ring);
		set_64bit_val(cq->shadow_area, 0,
			      I40IW_RING_GETCURRENT_HEAD(cq->cq_ring));
	} else {
		if (info->is_srq)
			return ret_code;
		qword3 &= ~I40IW_CQ_WQEIDX_MASK;
		qword3 |= LS_64(pring->tail, I40IW_CQ_WQEIDX);
		set_64bit_val(cqe, 24, qword3);
	}

	return ret_code;
}

/**
 * i40iw_get_wqe_shift - get shift count for maximum wqe size
 * @sge: Maximum Scatter Gather Elements wqe
 * @inline_data: Maximum inline data size
 * @shift: Returns the shift needed based on sge
 *
 * Shift can be used to left shift the wqe size based on number of SGEs and inlind data size.
 * For 1 SGE or inline data <= 16, shift = 0 (wqe size of 32 bytes).
 * For 2 or 3 SGEs or inline data <= 48, shift = 1 (wqe size of 64 bytes).
 * Shift of 2 otherwise (wqe size of 128 bytes).
 */
void i40iw_get_wqe_shift(u32 sge, u32 inline_data, u8 *shift)
{
	*shift = 0;
	if (sge > 1 || inline_data > 16)
		*shift = (sge < 4 && inline_data <= 48) ? 1 : 2;
}

/*
 * i40iw_get_sqdepth - get SQ depth (quantas)
 * @sq_size: SQ size
 * @shift: shift which determines size of WQE
 * @sqdepth: depth of SQ
 *
 */
enum i40iw_status_code i40iw_get_sqdepth(u32 sq_size, u8 shift, u32 *sqdepth)
{
	*sqdepth = roundup_pow_of_two((sq_size << shift) + I40IW_SQ_RSVD);

	if (*sqdepth < (I40IW_QP_SW_MIN_WQSIZE << shift))
		*sqdepth = I40IW_QP_SW_MIN_WQSIZE << shift;
	else if (*sqdepth > I40IW_QP_SW_MAX_SQ_QUANTAS)
		return I40IW_ERR_INVALID_SIZE;

	return 0;
}

/*
 * i40iw_get_rq_depth - get RQ depth (quantas)
 * @rq_size: RQ size
 * @shift: shift which determines size of WQE
 * @rqdepth: depth of RQ
 *
 */
enum i40iw_status_code i40iw_get_rqdepth(u32 rq_size, u8 shift, u32 *rqdepth)
{
	*rqdepth = roundup_pow_of_two((rq_size << shift) + I40IW_RQ_RSVD);

	if (*rqdepth < (I40IW_QP_SW_MIN_WQSIZE << shift))
		*rqdepth = I40IW_QP_SW_MIN_WQSIZE << shift;
	else if (*rqdepth > I40IW_QP_SW_MAX_RQ_QUANTAS)
		return I40IW_ERR_INVALID_SIZE;

	return 0;
}

static const struct i40iw_qp_uk_ops iw_qp_uk_ops = {
	.iw_qp_post_wr = i40iw_qp_post_wr,
	.iw_rdma_write = i40iw_rdma_write,
	.iw_rdma_read = i40iw_rdma_read,
	.iw_send = i40iw_send,
	.iw_inline_rdma_write = i40iw_inline_rdma_write,
	.iw_inline_send = i40iw_inline_send,
	.iw_stag_local_invalidate = i40iw_stag_local_invalidate,
	.iw_mw_bind = i40iw_mw_bind,
	.iw_post_receive = i40iw_post_receive,
	.iw_post_nop = i40iw_nop
};

static const struct i40iw_cq_ops iw_cq_ops = {
	.iw_cq_request_notification = i40iw_cq_request_notification,
	.iw_cq_poll_completion = i40iw_cq_poll_completion,
	.iw_cq_post_entries = i40iw_cq_post_entries,
	.iw_cq_clean = i40iw_clean_cq
};

static const struct i40iw_device_uk_ops iw_device_uk_ops = {
	.iwarp_cq_uk_init = i40iw_cq_uk_init,
	.iwarp_qp_uk_init = i40iw_qp_uk_init,
};

/**
 * i40iw_qp_uk_init - initialize shared qp
 * @qp: hw qp (user and kernel)
 * @info: qp initialization info
 *
 * initializes the vars used in both user and kernel mode.
 * size of the wqe depends on numbers of max. fragements
 * allowed. Then size of wqe * the number of wqes should be the
 * amount of memory allocated for sq and rq. If srq is used,
 * then rq_base will point to one rq wqe only (not the whole
 * array of wqes)
 */
enum i40iw_status_code i40iw_qp_uk_init(struct i40iw_qp_uk *qp,
					struct i40iw_qp_uk_init_info *info)
{
	enum i40iw_status_code ret_code = 0;
	u32 sq_ring_size;
	u8 sqshift, rqshift;

	if (info->max_sq_frag_cnt > I40IW_MAX_WQ_FRAGMENT_COUNT)
		return I40IW_ERR_INVALID_FRAG_COUNT;

	if (info->max_rq_frag_cnt > I40IW_MAX_WQ_FRAGMENT_COUNT)
		return I40IW_ERR_INVALID_FRAG_COUNT;
	i40iw_get_wqe_shift(info->max_sq_frag_cnt, info->max_inline_data, &sqshift);

	qp->sq_base = info->sq;
	qp->rq_base = info->rq;
	qp->shadow_area = info->shadow_area;
	qp->sq_wrtrk_array = info->sq_wrtrk_array;
	qp->rq_wrid_array = info->rq_wrid_array;

	qp->wqe_alloc_reg = info->wqe_alloc_reg;
	qp->qp_id = info->qp_id;
	qp->sq_size = info->sq_size;
	qp->max_sq_frag_cnt = info->max_sq_frag_cnt;
	sq_ring_size = qp->sq_size << sqshift;

	I40IW_RING_INIT(qp->sq_ring, sq_ring_size);
	I40IW_RING_INIT(qp->initial_ring, sq_ring_size);
	I40IW_RING_MOVE_HEAD(qp->sq_ring, ret_code);
	I40IW_RING_MOVE_TAIL(qp->sq_ring);
	I40IW_RING_MOVE_HEAD(qp->initial_ring, ret_code);
	qp->swqe_polarity = 1;
	qp->first_sq_wq = true;
	qp->swqe_polarity_deferred = 1;
	qp->rwqe_polarity = 0;

	if (!qp->use_srq) {
		qp->rq_size = info->rq_size;
		qp->max_rq_frag_cnt = info->max_rq_frag_cnt;
		I40IW_RING_INIT(qp->rq_ring, qp->rq_size);
		switch (info->abi_ver) {
		case 4:
			i40iw_get_wqe_shift(info->max_rq_frag_cnt, 0, &rqshift);
			break;
		case 5: /* fallthrough until next ABI version */
		default:
			rqshift = I40IW_MAX_RQ_WQE_SHIFT;
			break;
		}
		qp->rq_wqe_size = rqshift;
		qp->rq_wqe_size_multiplier = 4 << rqshift;
	}
	qp->ops = iw_qp_uk_ops;

	return ret_code;
}

/**
 * i40iw_cq_uk_init - initialize shared cq (user and kernel)
 * @cq: hw cq
 * @info: hw cq initialization info
 */
enum i40iw_status_code i40iw_cq_uk_init(struct i40iw_cq_uk *cq,
					struct i40iw_cq_uk_init_info *info)
{
	if ((info->cq_size < I40IW_MIN_CQ_SIZE) ||
	    (info->cq_size > I40IW_MAX_CQ_SIZE))
		return I40IW_ERR_INVALID_SIZE;
	cq->cq_base = (struct i40iw_cqe *)info->cq_base;
	cq->cq_id = info->cq_id;
	cq->cq_size = info->cq_size;
	cq->cqe_alloc_reg = info->cqe_alloc_reg;
	cq->shadow_area = info->shadow_area;
	cq->avoid_mem_cflct = info->avoid_mem_cflct;

	I40IW_RING_INIT(cq->cq_ring, cq->cq_size);
	cq->polarity = 1;
	cq->ops = iw_cq_ops;

	return 0;
}

/**
 * i40iw_device_init_uk - setup routines for iwarp shared device
 * @dev: iwarp shared (user and kernel)
 */
void i40iw_device_init_uk(struct i40iw_dev_uk *dev)
{
	dev->ops_uk = iw_device_uk_ops;
}

/**
 * i40iw_clean_cq - clean cq entries
 * @queue: completion context
 * @cq: cq to clean
 */
void i40iw_clean_cq(void *queue, struct i40iw_cq_uk *cq)
{
	u64 *cqe;
	u64 qword3, comp_ctx;
	u32 cq_head;
	u8 polarity, temp;

	cq_head = cq->cq_ring.head;
	temp = cq->polarity;
	do {
		if (cq->avoid_mem_cflct)
			cqe = (u64 *)&(((struct i40iw_extended_cqe *)cq->cq_base)[cq_head]);
		else
			cqe = (u64 *)&cq->cq_base[cq_head];
		get_64bit_val(cqe, 24, &qword3);
		polarity = (u8)RS_64(qword3, I40IW_CQ_VALID);

		if (polarity != temp)
			break;

		get_64bit_val(cqe, 8, &comp_ctx);
		if ((void *)(unsigned long)comp_ctx == queue)
			set_64bit_val(cqe, 8, 0);

		cq_head = (cq_head + 1) % cq->cq_ring.size;
		if (!cq_head)
			temp ^= 1;
	} while (true);
}

/**
 * i40iw_nop - send a nop
 * @qp: hw qp ptr
 * @wr_id: work request id
 * @signaled: flag if signaled for completion
 * @post_sq: flag to post sq
 */
enum i40iw_status_code i40iw_nop(struct i40iw_qp_uk *qp,
				 u64 wr_id,
				 bool signaled,
				 bool post_sq)
{
	u64 header, *wqe;
	u32 wqe_idx;

	wqe = i40iw_qp_get_next_send_wqe(qp, &wqe_idx, I40IW_QP_WQE_MIN_SIZE, 0, wr_id);
	if (!wqe)
		return I40IW_ERR_QP_TOOMANY_WRS_POSTED;
	set_64bit_val(wqe, 0, 0);
	set_64bit_val(wqe, 8, 0);
	set_64bit_val(wqe, 16, 0);

	header = LS_64(I40IWQP_OP_NOP, I40IWQPSQ_OPCODE) |
	    LS_64(signaled, I40IWQPSQ_SIGCOMPL) |
	    LS_64(qp->swqe_polarity, I40IWQPSQ_VALID);

	wmb(); /* make sure WQE is populated before valid bit is set */

	set_64bit_val(wqe, 24, header);
	if (post_sq)
		i40iw_qp_post_wr(qp);

	return 0;
}

/**
 * i40iw_fragcnt_to_wqesize_sq - calculate wqe size based on fragment count for SQ
 * @frag_cnt: number of fragments
 * @wqe_size: size of sq wqe returned
 */
enum i40iw_status_code i40iw_fragcnt_to_wqesize_sq(u32 frag_cnt, u8 *wqe_size)
{
	switch (frag_cnt) {
	case 0:
	case 1:
		*wqe_size = I40IW_QP_WQE_MIN_SIZE;
		break;
	case 2:
	case 3:
		*wqe_size = 64;
		break;
	case 4:
	case 5:
		*wqe_size = 96;
		break;
	case 6:
	case 7:
		*wqe_size = 128;
		break;
	default:
		return I40IW_ERR_INVALID_FRAG_COUNT;
	}

	return 0;
}

/**
 * i40iw_fragcnt_to_wqesize_rq - calculate wqe size based on fragment count for RQ
 * @frag_cnt: number of fragments
 * @wqe_size: size of rq wqe returned
 */
enum i40iw_status_code i40iw_fragcnt_to_wqesize_rq(u32 frag_cnt, u8 *wqe_size)
{
	switch (frag_cnt) {
	case 0:
	case 1:
		*wqe_size = 32;
		break;
	case 2:
	case 3:
		*wqe_size = 64;
		break;
	case 4:
	case 5:
	case 6:
	case 7:
		*wqe_size = 128;
		break;
	default:
		return I40IW_ERR_INVALID_FRAG_COUNT;
	}

	return 0;
}

/**
 * i40iw_inline_data_size_to_wqesize - based on inline data, wqe size
 * @data_size: data size for inline
 * @wqe_size: size of sq wqe returned
 */
enum i40iw_status_code i40iw_inline_data_size_to_wqesize(u32 data_size,
							 u8 *wqe_size)
{
	if (data_size > I40IW_MAX_INLINE_DATA_SIZE)
		return I40IW_ERR_INVALID_INLINE_DATA_SIZE;

	if (data_size <= 16)
		*wqe_size = I40IW_QP_WQE_MIN_SIZE;
	else
		*wqe_size = 64;

	return 0;
}
