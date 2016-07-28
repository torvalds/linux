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

#ifndef I40IW_USER_H
#define I40IW_USER_H

enum i40iw_device_capabilities_const {
	I40IW_WQE_SIZE =			4,
	I40IW_CQP_WQE_SIZE =			8,
	I40IW_CQE_SIZE =			4,
	I40IW_EXTENDED_CQE_SIZE =		8,
	I40IW_AEQE_SIZE =			2,
	I40IW_CEQE_SIZE =			1,
	I40IW_CQP_CTX_SIZE =			8,
	I40IW_SHADOW_AREA_SIZE =		8,
	I40IW_CEQ_MAX_COUNT =			256,
	I40IW_QUERY_FPM_BUF_SIZE =		128,
	I40IW_COMMIT_FPM_BUF_SIZE =		128,
	I40IW_MIN_IW_QP_ID =			1,
	I40IW_MAX_IW_QP_ID =			262143,
	I40IW_MIN_CEQID =			0,
	I40IW_MAX_CEQID =			256,
	I40IW_MIN_CQID =			0,
	I40IW_MAX_CQID =			131071,
	I40IW_MIN_AEQ_ENTRIES =			1,
	I40IW_MAX_AEQ_ENTRIES =			524287,
	I40IW_MIN_CEQ_ENTRIES =			1,
	I40IW_MAX_CEQ_ENTRIES =			131071,
	I40IW_MIN_CQ_SIZE =			1,
	I40IW_MAX_CQ_SIZE =			1048575,
	I40IW_MAX_AEQ_ALLOCATE_COUNT =		255,
	I40IW_DB_ID_ZERO =			0,
	I40IW_MAX_WQ_FRAGMENT_COUNT =		3,
	I40IW_MAX_SGE_RD =			1,
	I40IW_MAX_OUTBOUND_MESSAGE_SIZE =	2147483647,
	I40IW_MAX_INBOUND_MESSAGE_SIZE =	2147483647,
	I40IW_MAX_PUSH_PAGE_COUNT =		4096,
	I40IW_MAX_PE_ENABLED_VF_COUNT =		32,
	I40IW_MAX_VF_FPM_ID =			47,
	I40IW_MAX_VF_PER_PF =			127,
	I40IW_MAX_SQ_PAYLOAD_SIZE =		2145386496,
	I40IW_MAX_INLINE_DATA_SIZE =		48,
	I40IW_MAX_PUSHMODE_INLINE_DATA_SIZE =	48,
	I40IW_MAX_IRD_SIZE =			32,
	I40IW_QPCTX_ENCD_MAXIRD =		3,
	I40IW_MAX_WQ_ENTRIES =			2048,
	I40IW_MAX_ORD_SIZE =			32,
	I40IW_Q2_BUFFER_SIZE =			(248 + 100),
	I40IW_QP_CTX_SIZE =			248
};

#define i40iw_handle void *
#define i40iw_adapter_handle i40iw_handle
#define i40iw_qp_handle i40iw_handle
#define i40iw_cq_handle i40iw_handle
#define i40iw_srq_handle i40iw_handle
#define i40iw_pd_id i40iw_handle
#define i40iw_stag_handle i40iw_handle
#define i40iw_stag_index u32
#define i40iw_stag u32
#define i40iw_stag_key u8

#define i40iw_tagged_offset u64
#define i40iw_access_privileges u32
#define i40iw_physical_fragment u64
#define i40iw_address_list u64 *

#define I40IW_CREATE_STAG(index, key)       (((index) << 8) + (key))

#define I40IW_STAG_KEY_FROM_STAG(stag)      ((stag) && 0x000000FF)

#define I40IW_STAG_INDEX_FROM_STAG(stag)    (((stag) && 0xFFFFFF00) >> 8)

#define	I40IW_MAX_MR_SIZE	0x10000000000L

struct i40iw_qp_uk;
struct i40iw_cq_uk;
struct i40iw_srq_uk;
struct i40iw_qp_uk_init_info;
struct i40iw_cq_uk_init_info;
struct i40iw_srq_uk_init_info;

struct i40iw_sge {
	i40iw_tagged_offset tag_off;
	u32 len;
	i40iw_stag stag;
};

#define i40iw_sgl struct i40iw_sge *

struct i40iw_ring {
	u32 head;
	u32 tail;
	u32 size;
};

struct i40iw_cqe {
	u64 buf[I40IW_CQE_SIZE];
};

struct i40iw_extended_cqe {
	u64 buf[I40IW_EXTENDED_CQE_SIZE];
};

struct i40iw_wqe {
	u64 buf[I40IW_WQE_SIZE];
};

struct i40iw_qp_uk_ops;

enum i40iw_addressing_type {
	I40IW_ADDR_TYPE_ZERO_BASED = 0,
	I40IW_ADDR_TYPE_VA_BASED = 1,
};

#define I40IW_ACCESS_FLAGS_LOCALREAD		0x01
#define I40IW_ACCESS_FLAGS_LOCALWRITE		0x02
#define I40IW_ACCESS_FLAGS_REMOTEREAD_ONLY	0x04
#define I40IW_ACCESS_FLAGS_REMOTEREAD		0x05
#define I40IW_ACCESS_FLAGS_REMOTEWRITE_ONLY	0x08
#define I40IW_ACCESS_FLAGS_REMOTEWRITE		0x0a
#define I40IW_ACCESS_FLAGS_BIND_WINDOW		0x10
#define I40IW_ACCESS_FLAGS_ALL			0x1F

#define I40IW_OP_TYPE_RDMA_WRITE	0
#define I40IW_OP_TYPE_RDMA_READ		1
#define I40IW_OP_TYPE_SEND		3
#define I40IW_OP_TYPE_SEND_INV		4
#define I40IW_OP_TYPE_SEND_SOL		5
#define I40IW_OP_TYPE_SEND_SOL_INV	6
#define I40IW_OP_TYPE_REC		7
#define I40IW_OP_TYPE_BIND_MW		8
#define I40IW_OP_TYPE_FAST_REG_NSMR	9
#define I40IW_OP_TYPE_INV_STAG		10
#define I40IW_OP_TYPE_RDMA_READ_INV_STAG 11
#define I40IW_OP_TYPE_NOP		12

enum i40iw_completion_status {
	I40IW_COMPL_STATUS_SUCCESS = 0,
	I40IW_COMPL_STATUS_FLUSHED,
	I40IW_COMPL_STATUS_INVALID_WQE,
	I40IW_COMPL_STATUS_QP_CATASTROPHIC,
	I40IW_COMPL_STATUS_REMOTE_TERMINATION,
	I40IW_COMPL_STATUS_INVALID_STAG,
	I40IW_COMPL_STATUS_BASE_BOUND_VIOLATION,
	I40IW_COMPL_STATUS_ACCESS_VIOLATION,
	I40IW_COMPL_STATUS_INVALID_PD_ID,
	I40IW_COMPL_STATUS_WRAP_ERROR,
	I40IW_COMPL_STATUS_STAG_INVALID_PDID,
	I40IW_COMPL_STATUS_RDMA_READ_ZERO_ORD,
	I40IW_COMPL_STATUS_QP_NOT_PRIVLEDGED,
	I40IW_COMPL_STATUS_STAG_NOT_INVALID,
	I40IW_COMPL_STATUS_INVALID_PHYS_BUFFER_SIZE,
	I40IW_COMPL_STATUS_INVALID_PHYS_BUFFER_ENTRY,
	I40IW_COMPL_STATUS_INVALID_FBO,
	I40IW_COMPL_STATUS_INVALID_LENGTH,
	I40IW_COMPL_STATUS_INVALID_ACCESS,
	I40IW_COMPL_STATUS_PHYS_BUFFER_LIST_TOO_LONG,
	I40IW_COMPL_STATUS_INVALID_VIRT_ADDRESS,
	I40IW_COMPL_STATUS_INVALID_REGION,
	I40IW_COMPL_STATUS_INVALID_WINDOW,
	I40IW_COMPL_STATUS_INVALID_TOTAL_LENGTH
};

enum i40iw_completion_notify {
	IW_CQ_COMPL_EVENT = 0,
	IW_CQ_COMPL_SOLICITED = 1
};

struct i40iw_post_send {
	i40iw_sgl sg_list;
	u32 num_sges;
};

struct i40iw_post_inline_send {
	void *data;
	u32 len;
};

struct i40iw_post_send_w_inv {
	i40iw_sgl sg_list;
	u32 num_sges;
	i40iw_stag remote_stag_to_inv;
};

struct i40iw_post_inline_send_w_inv {
	void *data;
	u32 len;
	i40iw_stag remote_stag_to_inv;
};

struct i40iw_rdma_write {
	i40iw_sgl lo_sg_list;
	u32 num_lo_sges;
	struct i40iw_sge rem_addr;
};

struct i40iw_inline_rdma_write {
	void *data;
	u32 len;
	struct i40iw_sge rem_addr;
};

struct i40iw_rdma_read {
	struct i40iw_sge lo_addr;
	struct i40iw_sge rem_addr;
};

struct i40iw_bind_window {
	i40iw_stag mr_stag;
	u64 bind_length;
	void *va;
	enum i40iw_addressing_type addressing_type;
	bool enable_reads;
	bool enable_writes;
	i40iw_stag mw_stag;
};

struct i40iw_inv_local_stag {
	i40iw_stag target_stag;
};

struct i40iw_post_sq_info {
	u64 wr_id;
	u8 op_type;
	bool signaled;
	bool read_fence;
	bool local_fence;
	bool inline_data;
	bool defer_flag;
	union {
		struct i40iw_post_send send;
		struct i40iw_post_send send_w_sol;
		struct i40iw_post_send_w_inv send_w_inv;
		struct i40iw_post_send_w_inv send_w_sol_inv;
		struct i40iw_rdma_write rdma_write;
		struct i40iw_rdma_read rdma_read;
		struct i40iw_rdma_read rdma_read_inv;
		struct i40iw_bind_window bind_window;
		struct i40iw_inv_local_stag inv_local_stag;
		struct i40iw_inline_rdma_write inline_rdma_write;
		struct i40iw_post_inline_send inline_send;
		struct i40iw_post_inline_send inline_send_w_sol;
		struct i40iw_post_inline_send_w_inv inline_send_w_inv;
		struct i40iw_post_inline_send_w_inv inline_send_w_sol_inv;
	} op;
};

struct i40iw_post_rq_info {
	u64 wr_id;
	i40iw_sgl sg_list;
	u32 num_sges;
};

struct i40iw_cq_poll_info {
	u64 wr_id;
	i40iw_qp_handle qp_handle;
	u32 bytes_xfered;
	u32 tcp_seq_num;
	u32 qp_id;
	i40iw_stag inv_stag;
	enum i40iw_completion_status comp_status;
	u16 major_err;
	u16 minor_err;
	u8 op_type;
	bool stag_invalid_set;
	bool push_dropped;
	bool error;
	bool is_srq;
	bool solicited_event;
};

struct i40iw_qp_uk_ops {
	void (*iw_qp_post_wr)(struct i40iw_qp_uk *);
	void (*iw_qp_ring_push_db)(struct i40iw_qp_uk *, u32);
	enum i40iw_status_code (*iw_rdma_write)(struct i40iw_qp_uk *,
						struct i40iw_post_sq_info *, bool);
	enum i40iw_status_code (*iw_rdma_read)(struct i40iw_qp_uk *,
					       struct i40iw_post_sq_info *, bool, bool);
	enum i40iw_status_code (*iw_send)(struct i40iw_qp_uk *,
					  struct i40iw_post_sq_info *, u32, bool);
	enum i40iw_status_code (*iw_inline_rdma_write)(struct i40iw_qp_uk *,
						       struct i40iw_post_sq_info *, bool);
	enum i40iw_status_code (*iw_inline_send)(struct i40iw_qp_uk *,
						 struct i40iw_post_sq_info *, u32, bool);
	enum i40iw_status_code (*iw_stag_local_invalidate)(struct i40iw_qp_uk *,
							   struct i40iw_post_sq_info *, bool);
	enum i40iw_status_code (*iw_mw_bind)(struct i40iw_qp_uk *,
					     struct i40iw_post_sq_info *, bool);
	enum i40iw_status_code (*iw_post_receive)(struct i40iw_qp_uk *,
						  struct i40iw_post_rq_info *);
	enum i40iw_status_code (*iw_post_nop)(struct i40iw_qp_uk *, u64, bool, bool);
};

struct i40iw_cq_ops {
	void (*iw_cq_request_notification)(struct i40iw_cq_uk *,
					   enum i40iw_completion_notify);
	enum i40iw_status_code (*iw_cq_poll_completion)(struct i40iw_cq_uk *,
							struct i40iw_cq_poll_info *, bool);
	enum i40iw_status_code (*iw_cq_post_entries)(struct i40iw_cq_uk *, u8 count);
	void (*iw_cq_clean)(void *, struct i40iw_cq_uk *);
};

struct i40iw_dev_uk;

struct i40iw_device_uk_ops {
	enum i40iw_status_code (*iwarp_cq_uk_init)(struct i40iw_cq_uk *,
						   struct i40iw_cq_uk_init_info *);
	enum i40iw_status_code (*iwarp_qp_uk_init)(struct i40iw_qp_uk *,
						   struct i40iw_qp_uk_init_info *);
};

struct i40iw_dev_uk {
	struct i40iw_device_uk_ops ops_uk;
};

struct i40iw_sq_uk_wr_trk_info {
	u64 wrid;
	u32 wr_len;
	u8 wqe_size;
	u8 reserved[3];
};

struct i40iw_qp_quanta {
	u64 elem[I40IW_WQE_SIZE];
};

struct i40iw_qp_uk {
	struct i40iw_qp_quanta *sq_base;
	struct i40iw_qp_quanta *rq_base;
	u32 __iomem *wqe_alloc_reg;
	struct i40iw_sq_uk_wr_trk_info *sq_wrtrk_array;
	u64 *rq_wrid_array;
	u64 *shadow_area;
	u32 *push_db;
	u64 *push_wqe;
	struct i40iw_ring sq_ring;
	struct i40iw_ring rq_ring;
	struct i40iw_ring initial_ring;
	u32 qp_id;
	u32 sq_size;
	u32 rq_size;
	u32 max_sq_frag_cnt;
	u32 max_rq_frag_cnt;
	struct i40iw_qp_uk_ops ops;
	bool use_srq;
	u8 swqe_polarity;
	u8 swqe_polarity_deferred;
	u8 rwqe_polarity;
	u8 rq_wqe_size;
	u8 rq_wqe_size_multiplier;
	bool deferred_flag;
};

struct i40iw_cq_uk {
	struct i40iw_cqe *cq_base;
	u32 __iomem *cqe_alloc_reg;
	u64 *shadow_area;
	u32 cq_id;
	u32 cq_size;
	struct i40iw_ring cq_ring;
	u8 polarity;
	bool avoid_mem_cflct;

	struct i40iw_cq_ops ops;
};

struct i40iw_qp_uk_init_info {
	struct i40iw_qp_quanta *sq;
	struct i40iw_qp_quanta *rq;
	u32 __iomem *wqe_alloc_reg;
	u64 *shadow_area;
	struct i40iw_sq_uk_wr_trk_info *sq_wrtrk_array;
	u64 *rq_wrid_array;
	u32 *push_db;
	u64 *push_wqe;
	u32 qp_id;
	u32 sq_size;
	u32 rq_size;
	u32 max_sq_frag_cnt;
	u32 max_rq_frag_cnt;
	u32 max_inline_data;

};

struct i40iw_cq_uk_init_info {
	u32 __iomem *cqe_alloc_reg;
	struct i40iw_cqe *cq_base;
	u64 *shadow_area;
	u32 cq_size;
	u32 cq_id;
	bool avoid_mem_cflct;
};

void i40iw_device_init_uk(struct i40iw_dev_uk *dev);

void i40iw_qp_post_wr(struct i40iw_qp_uk *qp);
u64 *i40iw_qp_get_next_send_wqe(struct i40iw_qp_uk *qp, u32 *wqe_idx,
				u8 wqe_size,
				u32 total_size,
				u64 wr_id
				);
u64 *i40iw_qp_get_next_recv_wqe(struct i40iw_qp_uk *qp, u32 *wqe_idx);
u64 *i40iw_qp_get_next_srq_wqe(struct i40iw_srq_uk *srq, u32 *wqe_idx);

enum i40iw_status_code i40iw_cq_uk_init(struct i40iw_cq_uk *cq,
					struct i40iw_cq_uk_init_info *info);
enum i40iw_status_code i40iw_qp_uk_init(struct i40iw_qp_uk *qp,
					struct i40iw_qp_uk_init_info *info);

void i40iw_clean_cq(void *queue, struct i40iw_cq_uk *cq);
enum i40iw_status_code i40iw_nop(struct i40iw_qp_uk *qp, u64 wr_id,
				 bool signaled, bool post_sq);
enum i40iw_status_code i40iw_fragcnt_to_wqesize_sq(u32 frag_cnt, u8 *wqe_size);
enum i40iw_status_code i40iw_fragcnt_to_wqesize_rq(u32 frag_cnt, u8 *wqe_size);
enum i40iw_status_code i40iw_inline_data_size_to_wqesize(u32 data_size,
							 u8 *wqe_size);
enum i40iw_status_code i40iw_get_wqe_shift(u32 wqdepth, u32 sge, u32 inline_data, u8 *shift);
#endif
