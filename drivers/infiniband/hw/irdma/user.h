/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2015 - 2020 Intel Corporation */
#ifndef IRDMA_USER_H
#define IRDMA_USER_H

#define irdma_handle void *
#define irdma_adapter_handle irdma_handle
#define irdma_qp_handle irdma_handle
#define irdma_cq_handle irdma_handle
#define irdma_pd_id irdma_handle
#define irdma_stag_handle irdma_handle
#define irdma_stag_index u32
#define irdma_stag u32
#define irdma_stag_key u8
#define irdma_tagged_offset u64
#define irdma_access_privileges u32
#define irdma_physical_fragment u64
#define irdma_address_list u64 *

#define	IRDMA_MAX_MR_SIZE       0x200000000000ULL

#define IRDMA_ACCESS_FLAGS_LOCALREAD		0x01
#define IRDMA_ACCESS_FLAGS_LOCALWRITE		0x02
#define IRDMA_ACCESS_FLAGS_REMOTEREAD_ONLY	0x04
#define IRDMA_ACCESS_FLAGS_REMOTEREAD		0x05
#define IRDMA_ACCESS_FLAGS_REMOTEWRITE_ONLY	0x08
#define IRDMA_ACCESS_FLAGS_REMOTEWRITE		0x0a
#define IRDMA_ACCESS_FLAGS_BIND_WINDOW		0x10
#define IRDMA_ACCESS_FLAGS_ZERO_BASED		0x20
#define IRDMA_ACCESS_FLAGS_ALL			0x3f

#define IRDMA_OP_TYPE_RDMA_WRITE		0x00
#define IRDMA_OP_TYPE_RDMA_READ			0x01
#define IRDMA_OP_TYPE_SEND			0x03
#define IRDMA_OP_TYPE_SEND_INV			0x04
#define IRDMA_OP_TYPE_SEND_SOL			0x05
#define IRDMA_OP_TYPE_SEND_SOL_INV		0x06
#define IRDMA_OP_TYPE_RDMA_WRITE_SOL		0x0d
#define IRDMA_OP_TYPE_BIND_MW			0x08
#define IRDMA_OP_TYPE_FAST_REG_NSMR		0x09
#define IRDMA_OP_TYPE_INV_STAG			0x0a
#define IRDMA_OP_TYPE_RDMA_READ_INV_STAG	0x0b
#define IRDMA_OP_TYPE_NOP			0x0c
#define IRDMA_OP_TYPE_ATOMIC_FETCH_AND_ADD	0x0f
#define IRDMA_OP_TYPE_ATOMIC_COMPARE_AND_SWAP	0x11
#define IRDMA_OP_TYPE_REC	0x3e
#define IRDMA_OP_TYPE_REC_IMM	0x3f

#define IRDMA_FLUSH_MAJOR_ERR 1
#define IRDMA_SRQFLUSH_RSVD_MAJOR_ERR 0xfffe

/* Async Events codes */
#define IRDMA_AE_AMP_UNALLOCATED_STAG					0x0102
#define IRDMA_AE_AMP_INVALID_STAG					0x0103
#define IRDMA_AE_AMP_BAD_QP						0x0104
#define IRDMA_AE_AMP_BAD_PD						0x0105
#define IRDMA_AE_AMP_BAD_STAG_KEY					0x0106
#define IRDMA_AE_AMP_BAD_STAG_INDEX					0x0107
#define IRDMA_AE_AMP_BOUNDS_VIOLATION					0x0108
#define IRDMA_AE_AMP_RIGHTS_VIOLATION					0x0109
#define IRDMA_AE_AMP_TO_WRAP						0x010a
#define IRDMA_AE_AMP_FASTREG_VALID_STAG					0x010c
#define IRDMA_AE_AMP_FASTREG_MW_STAG					0x010d
#define IRDMA_AE_AMP_FASTREG_INVALID_RIGHTS				0x010e
#define IRDMA_AE_AMP_FASTREG_INVALID_LENGTH				0x0110
#define IRDMA_AE_AMP_INVALIDATE_SHARED					0x0111
#define IRDMA_AE_AMP_INVALIDATE_NO_REMOTE_ACCESS_RIGHTS			0x0112
#define IRDMA_AE_AMP_INVALIDATE_MR_WITH_BOUND_WINDOWS			0x0113
#define IRDMA_AE_AMP_MWBIND_VALID_STAG					0x0114
#define IRDMA_AE_AMP_MWBIND_OF_MR_STAG					0x0115
#define IRDMA_AE_AMP_MWBIND_TO_ZERO_BASED_STAG				0x0116
#define IRDMA_AE_AMP_MWBIND_TO_MW_STAG					0x0117
#define IRDMA_AE_AMP_MWBIND_INVALID_RIGHTS				0x0118
#define IRDMA_AE_AMP_MWBIND_INVALID_BOUNDS				0x0119
#define IRDMA_AE_AMP_MWBIND_TO_INVALID_PARENT				0x011a
#define IRDMA_AE_AMP_MWBIND_BIND_DISABLED				0x011b
#define IRDMA_AE_PRIV_OPERATION_DENIED					0x011c
#define IRDMA_AE_AMP_INVALIDATE_TYPE1_MW				0x011d
#define IRDMA_AE_AMP_MWBIND_ZERO_BASED_TYPE1_MW				0x011e
#define IRDMA_AE_AMP_FASTREG_INVALID_PBL_HPS_CFG			0x011f
#define IRDMA_AE_AMP_MWBIND_WRONG_TYPE					0x0120
#define IRDMA_AE_AMP_FASTREG_PBLE_MISMATCH				0x0121
#define IRDMA_AE_UDA_XMIT_DGRAM_TOO_LONG				0x0132
#define IRDMA_AE_UDA_XMIT_BAD_PD					0x0133
#define IRDMA_AE_UDA_XMIT_DGRAM_TOO_SHORT				0x0134
#define IRDMA_AE_UDA_L4LEN_INVALID					0x0135
#define IRDMA_AE_BAD_CLOSE						0x0201
#define IRDMA_AE_RDMAP_ROE_BAD_LLP_CLOSE				0x0202
#define IRDMA_AE_CQ_OPERATION_ERROR					0x0203
#define IRDMA_AE_RDMA_READ_WHILE_ORD_ZERO				0x0205
#define IRDMA_AE_STAG_ZERO_INVALID					0x0206
#define IRDMA_AE_IB_RREQ_AND_Q1_FULL					0x0207
#define IRDMA_AE_IB_INVALID_REQUEST					0x0208
#define IRDMA_AE_SRQ_LIMIT						0x0209
#define IRDMA_AE_WQE_UNEXPECTED_OPCODE					0x020a
#define IRDMA_AE_WQE_INVALID_PARAMETER					0x020b
#define IRDMA_AE_WQE_INVALID_FRAG_DATA					0x020c
#define IRDMA_AE_IB_REMOTE_ACCESS_ERROR					0x020d
#define IRDMA_AE_IB_REMOTE_OP_ERROR					0x020e
#define IRDMA_AE_SRQ_CATASTROPHIC_ERROR					0x020f
#define IRDMA_AE_WQE_LSMM_TOO_LONG					0x0220
#define IRDMA_AE_ATOMIC_ALIGNMENT					0x0221
#define IRDMA_AE_ATOMIC_MASK						0x0222
#define IRDMA_AE_INVALID_REQUEST					0x0223
#define IRDMA_AE_PCIE_ATOMIC_DISABLE					0x0224
#define IRDMA_AE_DDP_INVALID_MSN_GAP_IN_MSN				0x0301
#define IRDMA_AE_DDP_UBE_DDP_MESSAGE_TOO_LONG_FOR_AVAILABLE_BUFFER	0x0303
#define IRDMA_AE_DDP_UBE_INVALID_DDP_VERSION				0x0304
#define IRDMA_AE_DDP_UBE_INVALID_MO					0x0305
#define IRDMA_AE_DDP_UBE_INVALID_MSN_NO_BUFFER_AVAILABLE		0x0306
#define IRDMA_AE_DDP_UBE_INVALID_QN					0x0307
#define IRDMA_AE_DDP_NO_L_BIT						0x0308
#define IRDMA_AE_RDMAP_ROE_INVALID_RDMAP_VERSION			0x0311
#define IRDMA_AE_RDMAP_ROE_UNEXPECTED_OPCODE				0x0312
#define IRDMA_AE_ROE_INVALID_RDMA_READ_REQUEST				0x0313
#define IRDMA_AE_ROE_INVALID_RDMA_WRITE_OR_READ_RESP			0x0314
#define IRDMA_AE_ROCE_RSP_LENGTH_ERROR					0x0316
#define IRDMA_AE_ROCE_EMPTY_MCG						0x0380
#define IRDMA_AE_ROCE_BAD_MC_IP_ADDR					0x0381
#define IRDMA_AE_ROCE_BAD_MC_QPID					0x0382
#define IRDMA_AE_MCG_QP_PROTOCOL_MISMATCH				0x0383
#define IRDMA_AE_INVALID_ARP_ENTRY					0x0401
#define IRDMA_AE_INVALID_TCP_OPTION_RCVD				0x0402
#define IRDMA_AE_STALE_ARP_ENTRY					0x0403
#define IRDMA_AE_INVALID_AH_ENTRY					0x0406
#define IRDMA_AE_LLP_CLOSE_COMPLETE					0x0501
#define IRDMA_AE_LLP_CONNECTION_RESET					0x0502
#define IRDMA_AE_LLP_FIN_RECEIVED					0x0503
#define IRDMA_AE_LLP_RECEIVED_MARKER_AND_LENGTH_FIELDS_DONT_MATCH	0x0504
#define IRDMA_AE_LLP_RECEIVED_MPA_CRC_ERROR				0x0505
#define IRDMA_AE_LLP_SEGMENT_TOO_SMALL					0x0507
#define IRDMA_AE_LLP_SYN_RECEIVED					0x0508
#define IRDMA_AE_LLP_TERMINATE_RECEIVED					0x0509
#define IRDMA_AE_LLP_TOO_MANY_RETRIES					0x050a
#define IRDMA_AE_LLP_TOO_MANY_KEEPALIVE_RETRIES				0x050b
#define IRDMA_AE_LLP_DOUBT_REACHABILITY					0x050c
#define IRDMA_AE_LLP_CONNECTION_ESTABLISHED				0x050e
#define IRDMA_AE_LLP_TOO_MANY_RNRS					0x050f
#define IRDMA_AE_RESOURCE_EXHAUSTION					0x0520
#define IRDMA_AE_RESET_SENT						0x0601
#define IRDMA_AE_TERMINATE_SENT						0x0602
#define IRDMA_AE_RESET_NOT_SENT						0x0603
#define IRDMA_AE_LCE_QP_CATASTROPHIC					0x0700
#define IRDMA_AE_LCE_FUNCTION_CATASTROPHIC				0x0701
#define IRDMA_AE_LCE_CQ_CATASTROPHIC					0x0702
#define IRDMA_AE_REMOTE_QP_CATASTROPHIC					0x0703
#define IRDMA_AE_LOCAL_QP_CATASTROPHIC					0x0704
#define IRDMA_AE_RCE_QP_CATASTROPHIC					0x0705
#define IRDMA_AE_QP_SUSPEND_COMPLETE					0x0900
#define IRDMA_AE_CQP_DEFERRED_COMPLETE					0x0901
#define IRDMA_AE_ADAPTER_CATASTROPHIC					0x0B0B

enum irdma_device_caps_const {
	IRDMA_WQE_SIZE =			4,
	IRDMA_CQP_WQE_SIZE =			8,
	IRDMA_CQE_SIZE =			4,
	IRDMA_EXTENDED_CQE_SIZE =		8,
	IRDMA_AEQE_SIZE =			2,
	IRDMA_CEQE_SIZE =			1,
	IRDMA_CQP_CTX_SIZE =			8,
	IRDMA_SHADOW_AREA_SIZE =		8,
	IRDMA_QUERY_FPM_BUF_SIZE =		192,
	IRDMA_COMMIT_FPM_BUF_SIZE =		192,
	IRDMA_GATHER_STATS_BUF_SIZE =		1024,
	IRDMA_MIN_IW_QP_ID =			0,
	IRDMA_MAX_IW_QP_ID =			262143,
	IRDMA_MIN_IW_SRQ_ID =			0,
	IRDMA_MIN_CEQID =			0,
	IRDMA_MAX_CEQID =			1023,
	IRDMA_CEQ_MAX_COUNT =			IRDMA_MAX_CEQID + 1,
	IRDMA_MIN_CQID =			0,
	IRDMA_MAX_CQID =			524287,
	IRDMA_MIN_AEQ_ENTRIES =			1,
	IRDMA_MAX_AEQ_ENTRIES =			524287,
	IRDMA_MAX_AEQ_ENTRIES_GEN_3 =           262144,
	IRDMA_MIN_CEQ_ENTRIES =			1,
	IRDMA_MAX_CEQ_ENTRIES =			262143,
	IRDMA_MIN_CQ_SIZE =			1,
	IRDMA_MAX_CQ_SIZE =			1048575,
	IRDMA_DB_ID_ZERO =			0,
	IRDMA_MAX_WQ_FRAGMENT_COUNT =		13,
	IRDMA_MAX_SGE_RD =			13,
	IRDMA_MAX_OUTBOUND_MSG_SIZE =		2147483647,
	IRDMA_MAX_INBOUND_MSG_SIZE =		2147483647,
	IRDMA_MAX_PUSH_PAGE_COUNT =		1024,
	IRDMA_MAX_PE_ENA_VF_COUNT =		32,
	IRDMA_MAX_VF_FPM_ID =			47,
	IRDMA_MAX_SQ_PAYLOAD_SIZE =		2145386496,
	IRDMA_MAX_INLINE_DATA_SIZE =		101,
	IRDMA_MAX_WQ_ENTRIES =			32768,
	IRDMA_Q2_BUF_SIZE =			256,
	IRDMA_QP_CTX_SIZE =			256,
	IRDMA_MAX_PDS =				262144,
	IRDMA_MIN_WQ_SIZE_GEN2 =                8,
};

enum irdma_addressing_type {
	IRDMA_ADDR_TYPE_ZERO_BASED = 0,
	IRDMA_ADDR_TYPE_VA_BASED   = 1,
};

enum irdma_flush_opcode {
	FLUSH_INVALID = 0,
	FLUSH_GENERAL_ERR,
	FLUSH_PROT_ERR,
	FLUSH_REM_ACCESS_ERR,
	FLUSH_LOC_QP_OP_ERR,
	FLUSH_REM_OP_ERR,
	FLUSH_LOC_LEN_ERR,
	FLUSH_FATAL_ERR,
	FLUSH_RETRY_EXC_ERR,
	FLUSH_MW_BIND_ERR,
	FLUSH_REM_INV_REQ_ERR,
	FLUSH_RNR_RETRY_EXC_ERR,
};

enum irdma_qp_event_type {
	IRDMA_QP_EVENT_CATASTROPHIC,
	IRDMA_QP_EVENT_ACCESS_ERR,
	IRDMA_QP_EVENT_REQ_ERR,
};

enum irdma_cmpl_status {
	IRDMA_COMPL_STATUS_SUCCESS = 0,
	IRDMA_COMPL_STATUS_FLUSHED,
	IRDMA_COMPL_STATUS_INVALID_WQE,
	IRDMA_COMPL_STATUS_QP_CATASTROPHIC,
	IRDMA_COMPL_STATUS_REMOTE_TERMINATION,
	IRDMA_COMPL_STATUS_INVALID_STAG,
	IRDMA_COMPL_STATUS_BASE_BOUND_VIOLATION,
	IRDMA_COMPL_STATUS_ACCESS_VIOLATION,
	IRDMA_COMPL_STATUS_INVALID_PD_ID,
	IRDMA_COMPL_STATUS_WRAP_ERROR,
	IRDMA_COMPL_STATUS_STAG_INVALID_PDID,
	IRDMA_COMPL_STATUS_RDMA_READ_ZERO_ORD,
	IRDMA_COMPL_STATUS_QP_NOT_PRIVLEDGED,
	IRDMA_COMPL_STATUS_STAG_NOT_INVALID,
	IRDMA_COMPL_STATUS_INVALID_PHYS_BUF_SIZE,
	IRDMA_COMPL_STATUS_INVALID_PHYS_BUF_ENTRY,
	IRDMA_COMPL_STATUS_INVALID_FBO,
	IRDMA_COMPL_STATUS_INVALID_LEN,
	IRDMA_COMPL_STATUS_INVALID_ACCESS,
	IRDMA_COMPL_STATUS_PHYS_BUF_LIST_TOO_LONG,
	IRDMA_COMPL_STATUS_INVALID_VIRT_ADDRESS,
	IRDMA_COMPL_STATUS_INVALID_REGION,
	IRDMA_COMPL_STATUS_INVALID_WINDOW,
	IRDMA_COMPL_STATUS_INVALID_TOTAL_LEN,
	IRDMA_COMPL_STATUS_UNKNOWN,
};

enum irdma_cmpl_notify {
	IRDMA_CQ_COMPL_EVENT     = 0,
	IRDMA_CQ_COMPL_SOLICITED = 1,
};

enum irdma_qp_caps {
	IRDMA_WRITE_WITH_IMM = 1,
	IRDMA_SEND_WITH_IMM  = 2,
	IRDMA_ROCE	     = 4,
	IRDMA_PUSH_MODE      = 8,
};

struct irdma_srq_uk;
struct irdma_srq_uk_init_info;
struct irdma_qp_uk;
struct irdma_cq_uk;
struct irdma_qp_uk_init_info;
struct irdma_cq_uk_init_info;

struct irdma_ring {
	u32 head;
	u32 tail;
	u32 size;
};

struct irdma_cqe {
	__le64 buf[IRDMA_CQE_SIZE];
};

struct irdma_extended_cqe {
	__le64 buf[IRDMA_EXTENDED_CQE_SIZE];
};

struct irdma_post_send {
	struct ib_sge *sg_list;
	u32 num_sges;
	u32 qkey;
	u32 dest_qp;
	u32 ah_id;
};

struct irdma_post_rq_info {
	u64 wr_id;
	struct ib_sge *sg_list;
	u32 num_sges;
};

struct irdma_rdma_write {
	struct ib_sge *lo_sg_list;
	u32 num_lo_sges;
	struct ib_sge rem_addr;
};

struct irdma_rdma_read {
	struct ib_sge *lo_sg_list;
	u32 num_lo_sges;
	struct ib_sge rem_addr;
};

struct irdma_bind_window {
	irdma_stag mr_stag;
	u64 bind_len;
	void *va;
	enum irdma_addressing_type addressing_type;
	bool ena_reads:1;
	bool ena_writes:1;
	irdma_stag mw_stag;
	bool mem_window_type_1:1;
	bool remote_atomics_en:1;
};

struct irdma_atomic_fetch_add {
	u64 tagged_offset;
	u64 remote_tagged_offset;
	u64 fetch_add_data_bytes;
	u32 stag;
	u32 remote_stag;
};

struct irdma_atomic_compare_swap {
	u64 tagged_offset;
	u64 remote_tagged_offset;
	u64 swap_data_bytes;
	u64 compare_data_bytes;
	u32 stag;
	u32 remote_stag;
};

struct irdma_inv_local_stag {
	irdma_stag target_stag;
};

struct irdma_post_sq_info {
	u64 wr_id;
	u8 op_type;
	u8 l4len;
	bool signaled:1;
	bool read_fence:1;
	bool local_fence:1;
	bool inline_data:1;
	bool imm_data_valid:1;
	bool report_rtt:1;
	bool udp_hdr:1;
	bool defer_flag:1;
	bool remote_atomic_en:1;
	u32 imm_data;
	u32 stag_to_inv;
	union {
		struct irdma_post_send send;
		struct irdma_rdma_write rdma_write;
		struct irdma_rdma_read rdma_read;
		struct irdma_bind_window bind_window;
		struct irdma_inv_local_stag inv_local_stag;
		struct irdma_atomic_fetch_add atomic_fetch_add;
		struct irdma_atomic_compare_swap atomic_compare_swap;
	} op;
};

struct irdma_cq_poll_info {
	u64 wr_id;
	irdma_qp_handle qp_handle;
	u32 bytes_xfered;
	u32 tcp_seq_num_rtt;
	u32 qp_id;
	u32 ud_src_qpn;
	u32 imm_data;
	irdma_stag inv_stag; /* or L_R_Key */
	enum irdma_cmpl_status comp_status;
	u16 major_err;
	u16 minor_err;
	u16 ud_vlan;
	u8 ud_smac[6];
	u8 op_type;
	u8 q_type;
	bool stag_invalid_set:1; /* or L_R_Key set */
	bool error:1;
	bool solicited_event:1;
	bool ipv4:1;
	bool ud_vlan_valid:1;
	bool ud_smac_valid:1;
	bool imm_valid:1;
};

struct qp_err_code {
	enum irdma_flush_opcode flush_code;
	enum irdma_qp_event_type event_type;
};

int irdma_uk_atomic_compare_swap(struct irdma_qp_uk *qp,
				 struct irdma_post_sq_info *info, bool post_sq);
int irdma_uk_atomic_fetch_add(struct irdma_qp_uk *qp,
			      struct irdma_post_sq_info *info, bool post_sq);
int irdma_uk_inline_rdma_write(struct irdma_qp_uk *qp,
			       struct irdma_post_sq_info *info, bool post_sq);
int irdma_uk_inline_send(struct irdma_qp_uk *qp,
			 struct irdma_post_sq_info *info, bool post_sq);
int irdma_uk_post_nop(struct irdma_qp_uk *qp, u64 wr_id, bool signaled,
		      bool post_sq);
int irdma_uk_post_receive(struct irdma_qp_uk *qp,
			  struct irdma_post_rq_info *info);
void irdma_uk_qp_post_wr(struct irdma_qp_uk *qp);
int irdma_uk_rdma_read(struct irdma_qp_uk *qp, struct irdma_post_sq_info *info,
		       bool inv_stag, bool post_sq);
int irdma_uk_rdma_write(struct irdma_qp_uk *qp, struct irdma_post_sq_info *info,
			bool post_sq);
int irdma_uk_send(struct irdma_qp_uk *qp, struct irdma_post_sq_info *info,
		  bool post_sq);
int irdma_uk_stag_local_invalidate(struct irdma_qp_uk *qp,
				   struct irdma_post_sq_info *info,
				   bool post_sq);

struct irdma_wqe_uk_ops {
	void (*iw_copy_inline_data)(u8 *dest, struct ib_sge *sge_list,
				    u32 num_sges, u8 polarity);
	u16 (*iw_inline_data_size_to_quanta)(u32 data_size);
	void (*iw_set_fragment)(__le64 *wqe, u32 offset, struct ib_sge *sge,
				u8 valid);
	void (*iw_set_mw_bind_wqe)(__le64 *wqe,
				   struct irdma_bind_window *op_info);
};

int irdma_uk_cq_poll_cmpl(struct irdma_cq_uk *cq,
			  struct irdma_cq_poll_info *info);
void irdma_uk_cq_request_notification(struct irdma_cq_uk *cq,
				      enum irdma_cmpl_notify cq_notify);
void irdma_uk_cq_resize(struct irdma_cq_uk *cq, void *cq_base, int size);
void irdma_uk_cq_set_resized_cnt(struct irdma_cq_uk *qp, u16 cnt);
void irdma_uk_cq_init(struct irdma_cq_uk *cq,
		      struct irdma_cq_uk_init_info *info);
int irdma_uk_qp_init(struct irdma_qp_uk *qp,
		     struct irdma_qp_uk_init_info *info);
void irdma_uk_calc_shift_wq(struct irdma_qp_uk_init_info *ukinfo, u8 *sq_shift,
			    u8 *rq_shift);
int irdma_uk_calc_depth_shift_sq(struct irdma_qp_uk_init_info *ukinfo,
				 u32 *sq_depth, u8 *sq_shift);
int irdma_uk_calc_depth_shift_rq(struct irdma_qp_uk_init_info *ukinfo,
				 u32 *rq_depth, u8 *rq_shift);
int irdma_uk_srq_init(struct irdma_srq_uk *srq,
		      struct irdma_srq_uk_init_info *info);
int irdma_uk_srq_post_receive(struct irdma_srq_uk *srq,
			      struct irdma_post_rq_info *info);

struct irdma_srq_uk {
	u32 srq_caps;
	struct irdma_qp_quanta *srq_base;
	struct irdma_uk_attrs *uk_attrs;
	__le64 *shadow_area;
	struct irdma_ring srq_ring;
	struct irdma_ring initial_ring;
	u32 srq_id;
	u32 srq_size;
	u32 max_srq_frag_cnt;
	struct irdma_wqe_uk_ops wqe_ops;
	u8 srwqe_polarity;
	u8 wqe_size;
	u8 wqe_size_multiplier;
	u8 deferred_flag;
};

struct irdma_srq_uk_init_info {
	struct irdma_qp_quanta *srq;
	struct irdma_uk_attrs *uk_attrs;
	__le64 *shadow_area;
	u64 *srq_wrid_array;
	u32 srq_id;
	u32 srq_caps;
	u32 srq_size;
	u32 max_srq_frag_cnt;
};

struct irdma_sq_uk_wr_trk_info {
	u64 wrid;
	u32 wr_len;
	u16 quanta;
	u8 reserved[2];
};

struct irdma_qp_quanta {
	__le64 elem[IRDMA_WQE_SIZE];
};

struct irdma_qp_uk {
	struct irdma_qp_quanta *sq_base;
	struct irdma_qp_quanta *rq_base;
	struct irdma_uk_attrs *uk_attrs;
	u32 __iomem *wqe_alloc_db;
	struct irdma_sq_uk_wr_trk_info *sq_wrtrk_array;
	u64 *rq_wrid_array;
	__le64 *shadow_area;
	struct irdma_ring sq_ring;
	struct irdma_ring rq_ring;
	struct irdma_ring initial_ring;
	u32 qp_id;
	u32 qp_caps;
	u32 sq_size;
	u32 rq_size;
	u32 max_sq_frag_cnt;
	u32 max_rq_frag_cnt;
	u32 max_inline_data;
	struct irdma_wqe_uk_ops wqe_ops;
	u16 conn_wqes;
	u8 qp_type;
	u8 swqe_polarity;
	u8 swqe_polarity_deferred;
	u8 rwqe_polarity;
	u8 rq_wqe_size;
	u8 rq_wqe_size_multiplier;
	bool deferred_flag:1;
	bool first_sq_wq:1;
	bool sq_flush_complete:1; /* Indicates flush was seen and SQ was empty after the flush */
	bool rq_flush_complete:1; /* Indicates flush was seen and RQ was empty after the flush */
	bool destroy_pending:1; /* Indicates the QP is being destroyed */
	void *back_qp;
	u8 dbg_rq_flushed;
	struct irdma_srq_uk *srq_uk;
	u8 sq_flush_seen;
	u8 rq_flush_seen;
};

struct irdma_cq_uk {
	struct irdma_cqe *cq_base;
	u32 __iomem *cqe_alloc_db;
	u32 __iomem *cq_ack_db;
	__le64 *shadow_area;
	u32 cq_id;
	u32 cq_size;
	struct irdma_ring cq_ring;
	u8 polarity;
	bool avoid_mem_cflct:1;
};

struct irdma_qp_uk_init_info {
	struct irdma_qp_quanta *sq;
	struct irdma_qp_quanta *rq;
	struct irdma_uk_attrs *uk_attrs;
	u32 __iomem *wqe_alloc_db;
	__le64 *shadow_area;
	struct irdma_sq_uk_wr_trk_info *sq_wrtrk_array;
	u64 *rq_wrid_array;
	u32 qp_id;
	u32 qp_caps;
	u32 sq_size;
	u32 rq_size;
	u32 max_sq_frag_cnt;
	u32 max_rq_frag_cnt;
	u32 max_inline_data;
	u32 sq_depth;
	u32 rq_depth;
	u8 first_sq_wq;
	u8 type;
	u8 sq_shift;
	u8 rq_shift;
	int abi_ver;
	bool legacy_mode;
	struct irdma_srq_uk *srq_uk;
};

struct irdma_cq_uk_init_info {
	u32 __iomem *cqe_alloc_db;
	u32 __iomem *cq_ack_db;
	struct irdma_cqe *cq_base;
	__le64 *shadow_area;
	u32 cq_size;
	u32 cq_id;
	bool avoid_mem_cflct;
};

__le64 *irdma_qp_get_next_send_wqe(struct irdma_qp_uk *qp, u32 *wqe_idx,
				   u16 quanta, u32 total_size,
				   struct irdma_post_sq_info *info);
__le64 *irdma_srq_get_next_recv_wqe(struct irdma_srq_uk *srq, u32 *wqe_idx);
__le64 *irdma_qp_get_next_recv_wqe(struct irdma_qp_uk *qp, u32 *wqe_idx);
void irdma_uk_clean_cq(void *q, struct irdma_cq_uk *cq);
int irdma_nop(struct irdma_qp_uk *qp, u64 wr_id, bool signaled, bool post_sq);
int irdma_fragcnt_to_quanta_sq(u32 frag_cnt, u16 *quanta);
int irdma_fragcnt_to_wqesize_rq(u32 frag_cnt, u16 *wqe_size);
void irdma_get_wqe_shift(struct irdma_uk_attrs *uk_attrs, u32 sge,
			 u32 inline_data, u8 *shift);
int irdma_get_sqdepth(struct irdma_uk_attrs *uk_attrs, u32 sq_size, u8 shift,
		      u32 *wqdepth);
int irdma_get_rqdepth(struct irdma_uk_attrs *uk_attrs, u32 rq_size, u8 shift,
		      u32 *wqdepth);
int irdma_get_srqdepth(struct irdma_uk_attrs *uk_attrs, u32 srq_size, u8 shift,
		       u32 *srqdepth);
void irdma_clr_wqes(struct irdma_qp_uk *qp, u32 qp_wqe_idx);

static inline struct qp_err_code irdma_ae_to_qp_err_code(u16 ae_id)
{
	struct qp_err_code qp_err = {};

	switch (ae_id) {
	case IRDMA_AE_AMP_BOUNDS_VIOLATION:
	case IRDMA_AE_AMP_INVALID_STAG:
	case IRDMA_AE_AMP_RIGHTS_VIOLATION:
	case IRDMA_AE_AMP_UNALLOCATED_STAG:
	case IRDMA_AE_AMP_BAD_PD:
	case IRDMA_AE_AMP_BAD_QP:
	case IRDMA_AE_AMP_BAD_STAG_KEY:
	case IRDMA_AE_AMP_BAD_STAG_INDEX:
	case IRDMA_AE_AMP_TO_WRAP:
	case IRDMA_AE_PRIV_OPERATION_DENIED:
		qp_err.flush_code = FLUSH_PROT_ERR;
		qp_err.event_type = IRDMA_QP_EVENT_ACCESS_ERR;
		break;
	case IRDMA_AE_UDA_XMIT_BAD_PD:
	case IRDMA_AE_WQE_UNEXPECTED_OPCODE:
		qp_err.flush_code = FLUSH_LOC_QP_OP_ERR;
		qp_err.event_type = IRDMA_QP_EVENT_CATASTROPHIC;
		break;
	case IRDMA_AE_UDA_XMIT_DGRAM_TOO_SHORT:
	case IRDMA_AE_UDA_XMIT_DGRAM_TOO_LONG:
	case IRDMA_AE_UDA_L4LEN_INVALID:
	case IRDMA_AE_DDP_UBE_INVALID_MO:
	case IRDMA_AE_DDP_UBE_DDP_MESSAGE_TOO_LONG_FOR_AVAILABLE_BUFFER:
		qp_err.flush_code = FLUSH_LOC_LEN_ERR;
		qp_err.event_type = IRDMA_QP_EVENT_CATASTROPHIC;
		break;
	case IRDMA_AE_AMP_INVALIDATE_NO_REMOTE_ACCESS_RIGHTS:
	case IRDMA_AE_IB_REMOTE_ACCESS_ERROR:
		qp_err.flush_code = FLUSH_REM_ACCESS_ERR;
		qp_err.event_type = IRDMA_QP_EVENT_ACCESS_ERR;
		break;
	case IRDMA_AE_AMP_MWBIND_INVALID_RIGHTS:
	case IRDMA_AE_AMP_MWBIND_BIND_DISABLED:
	case IRDMA_AE_AMP_MWBIND_INVALID_BOUNDS:
	case IRDMA_AE_AMP_MWBIND_VALID_STAG:
		qp_err.flush_code = FLUSH_MW_BIND_ERR;
		qp_err.event_type = IRDMA_QP_EVENT_ACCESS_ERR;
		break;
	case IRDMA_AE_LLP_TOO_MANY_RETRIES:
		qp_err.flush_code = FLUSH_RETRY_EXC_ERR;
		qp_err.event_type = IRDMA_QP_EVENT_CATASTROPHIC;
		break;
	case IRDMA_AE_IB_INVALID_REQUEST:
		qp_err.flush_code = FLUSH_REM_INV_REQ_ERR;
		qp_err.event_type = IRDMA_QP_EVENT_REQ_ERR;
		break;
	case IRDMA_AE_LLP_SEGMENT_TOO_SMALL:
	case IRDMA_AE_LLP_RECEIVED_MPA_CRC_ERROR:
	case IRDMA_AE_ROCE_RSP_LENGTH_ERROR:
	case IRDMA_AE_IB_REMOTE_OP_ERROR:
		qp_err.flush_code = FLUSH_REM_OP_ERR;
		qp_err.event_type = IRDMA_QP_EVENT_CATASTROPHIC;
		break;
	case IRDMA_AE_LLP_TOO_MANY_RNRS:
		qp_err.flush_code = FLUSH_RNR_RETRY_EXC_ERR;
		qp_err.event_type = IRDMA_QP_EVENT_CATASTROPHIC;
		break;
	case IRDMA_AE_LCE_QP_CATASTROPHIC:
	case IRDMA_AE_REMOTE_QP_CATASTROPHIC:
	case IRDMA_AE_LOCAL_QP_CATASTROPHIC:
	case IRDMA_AE_RCE_QP_CATASTROPHIC:
		qp_err.flush_code = FLUSH_FATAL_ERR;
		qp_err.event_type = IRDMA_QP_EVENT_CATASTROPHIC;
		break;
	default:
		qp_err.flush_code = FLUSH_GENERAL_ERR;
		qp_err.event_type = IRDMA_QP_EVENT_CATASTROPHIC;
		break;
	}

	return qp_err;
}
#endif /* IRDMA_USER_H */
