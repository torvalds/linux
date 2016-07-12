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

#ifndef I40IW_TYPE_H
#define I40IW_TYPE_H
#include "i40iw_user.h"
#include "i40iw_hmc.h"
#include "i40iw_vf.h"
#include "i40iw_virtchnl.h"

struct i40iw_cqp_sq_wqe {
	u64 buf[I40IW_CQP_WQE_SIZE];
};

struct i40iw_sc_aeqe {
	u64 buf[I40IW_AEQE_SIZE];
};

struct i40iw_ceqe {
	u64 buf[I40IW_CEQE_SIZE];
};

struct i40iw_cqp_ctx {
	u64 buf[I40IW_CQP_CTX_SIZE];
};

struct i40iw_cq_shadow_area {
	u64 buf[I40IW_SHADOW_AREA_SIZE];
};

struct i40iw_sc_dev;
struct i40iw_hmc_info;
struct i40iw_dev_pestat;

struct i40iw_cqp_ops;
struct i40iw_ccq_ops;
struct i40iw_ceq_ops;
struct i40iw_aeq_ops;
struct i40iw_mr_ops;
struct i40iw_cqp_misc_ops;
struct i40iw_pd_ops;
struct i40iw_priv_qp_ops;
struct i40iw_priv_cq_ops;
struct i40iw_hmc_ops;

enum i40iw_resource_indicator_type {
	I40IW_RSRC_INDICATOR_TYPE_ADAPTER = 0,
	I40IW_RSRC_INDICATOR_TYPE_CQ,
	I40IW_RSRC_INDICATOR_TYPE_QP,
	I40IW_RSRC_INDICATOR_TYPE_SRQ
};

enum i40iw_hdrct_flags {
	DDP_LEN_FLAG = 0x80,
	DDP_HDR_FLAG = 0x40,
	RDMA_HDR_FLAG = 0x20
};

enum i40iw_term_layers {
	LAYER_RDMA = 0,
	LAYER_DDP = 1,
	LAYER_MPA = 2
};

enum i40iw_term_error_types {
	RDMAP_REMOTE_PROT = 1,
	RDMAP_REMOTE_OP = 2,
	DDP_CATASTROPHIC = 0,
	DDP_TAGGED_BUFFER = 1,
	DDP_UNTAGGED_BUFFER = 2,
	DDP_LLP = 3
};

enum i40iw_term_rdma_errors {
	RDMAP_INV_STAG = 0x00,
	RDMAP_INV_BOUNDS = 0x01,
	RDMAP_ACCESS = 0x02,
	RDMAP_UNASSOC_STAG = 0x03,
	RDMAP_TO_WRAP = 0x04,
	RDMAP_INV_RDMAP_VER = 0x05,
	RDMAP_UNEXPECTED_OP = 0x06,
	RDMAP_CATASTROPHIC_LOCAL = 0x07,
	RDMAP_CATASTROPHIC_GLOBAL = 0x08,
	RDMAP_CANT_INV_STAG = 0x09,
	RDMAP_UNSPECIFIED = 0xff
};

enum i40iw_term_ddp_errors {
	DDP_CATASTROPHIC_LOCAL = 0x00,
	DDP_TAGGED_INV_STAG = 0x00,
	DDP_TAGGED_BOUNDS = 0x01,
	DDP_TAGGED_UNASSOC_STAG = 0x02,
	DDP_TAGGED_TO_WRAP = 0x03,
	DDP_TAGGED_INV_DDP_VER = 0x04,
	DDP_UNTAGGED_INV_QN = 0x01,
	DDP_UNTAGGED_INV_MSN_NO_BUF = 0x02,
	DDP_UNTAGGED_INV_MSN_RANGE = 0x03,
	DDP_UNTAGGED_INV_MO = 0x04,
	DDP_UNTAGGED_INV_TOO_LONG = 0x05,
	DDP_UNTAGGED_INV_DDP_VER = 0x06
};

enum i40iw_term_mpa_errors {
	MPA_CLOSED = 0x01,
	MPA_CRC = 0x02,
	MPA_MARKER = 0x03,
	MPA_REQ_RSP = 0x04,
};

enum i40iw_flush_opcode {
	FLUSH_INVALID = 0,
	FLUSH_PROT_ERR,
	FLUSH_REM_ACCESS_ERR,
	FLUSH_LOC_QP_OP_ERR,
	FLUSH_REM_OP_ERR,
	FLUSH_LOC_LEN_ERR,
	FLUSH_GENERAL_ERR,
	FLUSH_FATAL_ERR
};

enum i40iw_term_eventtypes {
	TERM_EVENT_QP_FATAL,
	TERM_EVENT_QP_ACCESS_ERR
};

struct i40iw_terminate_hdr {
	u8 layer_etype;
	u8 error_code;
	u8 hdrct;
	u8 rsvd;
};

enum i40iw_debug_flag {
	I40IW_DEBUG_NONE	= 0x00000000,
	I40IW_DEBUG_ERR		= 0x00000001,
	I40IW_DEBUG_INIT	= 0x00000002,
	I40IW_DEBUG_DEV		= 0x00000004,
	I40IW_DEBUG_CM		= 0x00000008,
	I40IW_DEBUG_VERBS	= 0x00000010,
	I40IW_DEBUG_PUDA	= 0x00000020,
	I40IW_DEBUG_ILQ		= 0x00000040,
	I40IW_DEBUG_IEQ		= 0x00000080,
	I40IW_DEBUG_QP		= 0x00000100,
	I40IW_DEBUG_CQ		= 0x00000200,
	I40IW_DEBUG_MR		= 0x00000400,
	I40IW_DEBUG_PBLE	= 0x00000800,
	I40IW_DEBUG_WQE		= 0x00001000,
	I40IW_DEBUG_AEQ		= 0x00002000,
	I40IW_DEBUG_CQP		= 0x00004000,
	I40IW_DEBUG_HMC		= 0x00008000,
	I40IW_DEBUG_USER	= 0x00010000,
	I40IW_DEBUG_VIRT	= 0x00020000,
	I40IW_DEBUG_DCB		= 0x00040000,
	I40IW_DEBUG_CQE		= 0x00800000,
	I40IW_DEBUG_ALL		= 0xFFFFFFFF
};

enum i40iw_hw_stat_index_32b {
	I40IW_HW_STAT_INDEX_IP4RXDISCARD = 0,
	I40IW_HW_STAT_INDEX_IP4RXTRUNC,
	I40IW_HW_STAT_INDEX_IP4TXNOROUTE,
	I40IW_HW_STAT_INDEX_IP6RXDISCARD,
	I40IW_HW_STAT_INDEX_IP6RXTRUNC,
	I40IW_HW_STAT_INDEX_IP6TXNOROUTE,
	I40IW_HW_STAT_INDEX_TCPRTXSEG,
	I40IW_HW_STAT_INDEX_TCPRXOPTERR,
	I40IW_HW_STAT_INDEX_TCPRXPROTOERR,
	I40IW_HW_STAT_INDEX_MAX_32
};

enum i40iw_hw_stat_index_64b {
	I40IW_HW_STAT_INDEX_IP4RXOCTS = 0,
	I40IW_HW_STAT_INDEX_IP4RXPKTS,
	I40IW_HW_STAT_INDEX_IP4RXFRAGS,
	I40IW_HW_STAT_INDEX_IP4RXMCPKTS,
	I40IW_HW_STAT_INDEX_IP4TXOCTS,
	I40IW_HW_STAT_INDEX_IP4TXPKTS,
	I40IW_HW_STAT_INDEX_IP4TXFRAGS,
	I40IW_HW_STAT_INDEX_IP4TXMCPKTS,
	I40IW_HW_STAT_INDEX_IP6RXOCTS,
	I40IW_HW_STAT_INDEX_IP6RXPKTS,
	I40IW_HW_STAT_INDEX_IP6RXFRAGS,
	I40IW_HW_STAT_INDEX_IP6RXMCPKTS,
	I40IW_HW_STAT_INDEX_IP6TXOCTS,
	I40IW_HW_STAT_INDEX_IP6TXPKTS,
	I40IW_HW_STAT_INDEX_IP6TXFRAGS,
	I40IW_HW_STAT_INDEX_IP6TXMCPKTS,
	I40IW_HW_STAT_INDEX_TCPRXSEGS,
	I40IW_HW_STAT_INDEX_TCPTXSEG,
	I40IW_HW_STAT_INDEX_RDMARXRDS,
	I40IW_HW_STAT_INDEX_RDMARXSNDS,
	I40IW_HW_STAT_INDEX_RDMARXWRS,
	I40IW_HW_STAT_INDEX_RDMATXRDS,
	I40IW_HW_STAT_INDEX_RDMATXSNDS,
	I40IW_HW_STAT_INDEX_RDMATXWRS,
	I40IW_HW_STAT_INDEX_RDMAVBND,
	I40IW_HW_STAT_INDEX_RDMAVINV,
	I40IW_HW_STAT_INDEX_MAX_64
};

struct i40iw_dev_hw_stat_offsets {
	u32 stat_offset_32[I40IW_HW_STAT_INDEX_MAX_32];
	u32 stat_offset_64[I40IW_HW_STAT_INDEX_MAX_64];
};

struct i40iw_dev_hw_stats {
	u64 stat_value_32[I40IW_HW_STAT_INDEX_MAX_32];
	u64 stat_value_64[I40IW_HW_STAT_INDEX_MAX_64];
};

struct i40iw_device_pestat_ops {
	void (*iw_hw_stat_init)(struct i40iw_dev_pestat *, u8, struct i40iw_hw *, bool);
	void (*iw_hw_stat_read_32)(struct i40iw_dev_pestat *, enum i40iw_hw_stat_index_32b, u64 *);
	void (*iw_hw_stat_read_64)(struct i40iw_dev_pestat *, enum i40iw_hw_stat_index_64b, u64 *);
	void (*iw_hw_stat_read_all)(struct i40iw_dev_pestat *, struct i40iw_dev_hw_stats *);
	void (*iw_hw_stat_refresh_all)(struct i40iw_dev_pestat *);
};

struct i40iw_dev_pestat {
	struct i40iw_hw *hw;
	struct i40iw_device_pestat_ops ops;
	struct i40iw_dev_hw_stats hw_stats;
	struct i40iw_dev_hw_stats last_read_hw_stats;
	struct i40iw_dev_hw_stat_offsets hw_stat_offsets;
	struct timer_list stats_timer;
	spinlock_t stats_lock; /* rdma stats lock */
};

struct i40iw_hw {
	u8 __iomem *hw_addr;
	void *dev_context;
	struct i40iw_hmc_info hmc;
};

struct i40iw_pfpdu {
	struct list_head rxlist;
	u32 rcv_nxt;
	u32 fps;
	u32 max_fpdu_data;
	bool mode;
	bool mpa_crc_err;
	u64 total_ieq_bufs;
	u64 fpdu_processed;
	u64 bad_seq_num;
	u64 crc_err;
	u64 no_tx_bufs;
	u64 tx_err;
	u64 out_of_order;
	u64 pmode_count;
};

struct i40iw_sc_pd {
	u32 size;
	struct i40iw_sc_dev *dev;
	u16 pd_id;
};

struct i40iw_cqp_quanta {
	u64 elem[I40IW_CQP_WQE_SIZE];
};

struct i40iw_sc_cqp {
	u32 size;
	u64 sq_pa;
	u64 host_ctx_pa;
	void *back_cqp;
	struct i40iw_sc_dev *dev;
	enum i40iw_status_code (*process_cqp_sds)(struct i40iw_sc_dev *,
						  struct i40iw_update_sds_info *);
	struct i40iw_dma_mem sdbuf;
	struct i40iw_ring sq_ring;
	struct i40iw_cqp_quanta *sq_base;
	u64 *host_ctx;
	u64 *scratch_array;
	u32 cqp_id;
	u32 sq_size;
	u32 hw_sq_size;
	u8 struct_ver;
	u8 polarity;
	bool en_datacenter_tcp;
	u8 hmc_profile;
	u8 enabled_vf_count;
	u8 timeout_count;
};

struct i40iw_sc_aeq {
	u32 size;
	u64 aeq_elem_pa;
	struct i40iw_sc_dev *dev;
	struct i40iw_sc_aeqe *aeqe_base;
	void *pbl_list;
	u32 elem_cnt;
	struct i40iw_ring aeq_ring;
	bool virtual_map;
	u8 pbl_chunk_size;
	u32 first_pm_pbl_idx;
	u8 polarity;
};

struct i40iw_sc_ceq {
	u32 size;
	u64 ceq_elem_pa;
	struct i40iw_sc_dev *dev;
	struct i40iw_ceqe *ceqe_base;
	void *pbl_list;
	u32 ceq_id;
	u32 elem_cnt;
	struct i40iw_ring ceq_ring;
	bool virtual_map;
	u8 pbl_chunk_size;
	bool tph_en;
	u8 tph_val;
	u32 first_pm_pbl_idx;
	u8 polarity;
};

struct i40iw_sc_cq {
	struct i40iw_cq_uk cq_uk;
	u64 cq_pa;
	u64 shadow_area_pa;
	struct i40iw_sc_dev *dev;
	void *pbl_list;
	void *back_cq;
	u32 ceq_id;
	u32 shadow_read_threshold;
	bool ceqe_mask;
	bool virtual_map;
	u8 pbl_chunk_size;
	u8 cq_type;
	bool ceq_id_valid;
	bool tph_en;
	u8 tph_val;
	u32 first_pm_pbl_idx;
	bool check_overflow;
};

struct i40iw_sc_qp {
	struct i40iw_qp_uk qp_uk;
	u64 sq_pa;
	u64 rq_pa;
	u64 hw_host_ctx_pa;
	u64 shadow_area_pa;
	u64 q2_pa;
	struct i40iw_sc_dev *dev;
	struct i40iw_sc_pd *pd;
	u64 *hw_host_ctx;
	void *llp_stream_handle;
	void *back_qp;
	struct i40iw_pfpdu pfpdu;
	u8 *q2_buf;
	u64 qp_compl_ctx;
	u16 qs_handle;
	u16 exception_lan_queue;
	u16 push_idx;
	u8 sq_tph_val;
	u8 rq_tph_val;
	u8 qp_state;
	u8 qp_type;
	u8 hw_sq_size;
	u8 hw_rq_size;
	u8 src_mac_addr_idx;
	bool sq_tph_en;
	bool rq_tph_en;
	bool rcv_tph_en;
	bool xmit_tph_en;
	bool virtual_map;
	bool flush_sq;
	bool flush_rq;
	bool sq_flush;
	enum i40iw_flush_opcode flush_code;
	enum i40iw_term_eventtypes eventtype;
	u8 term_flags;
};

struct i40iw_hmc_fpm_misc {
	u32 max_ceqs;
	u32 max_sds;
	u32 xf_block_size;
	u32 q1_block_size;
	u32 ht_multiplier;
	u32 timer_bucket;
};

struct i40iw_vchnl_if {
	enum i40iw_status_code (*vchnl_recv)(struct i40iw_sc_dev *, u32, u8 *, u16);
	enum i40iw_status_code (*vchnl_send)(struct i40iw_sc_dev *dev, u32, u8 *, u16);
};

#define I40IW_VCHNL_MAX_VF_MSG_SIZE 512

struct i40iw_vchnl_vf_msg_buffer {
	struct i40iw_virtchnl_op_buf vchnl_msg;
	char parm_buffer[I40IW_VCHNL_MAX_VF_MSG_SIZE - 1];
};

struct i40iw_vfdev {
	struct i40iw_sc_dev *pf_dev;
	u8 *hmc_info_mem;
	struct i40iw_dev_pestat dev_pestat;
	struct i40iw_hmc_pble_info *pble_info;
	struct i40iw_hmc_info hmc_info;
	struct i40iw_vchnl_vf_msg_buffer vf_msg_buffer;
	u64 fpm_query_buf_pa;
	u64 *fpm_query_buf;
	u32 vf_id;
	u32 msg_count;
	bool pf_hmc_initialized;
	u16 pmf_index;
	u16 iw_vf_idx;		/* VF Device table index */
	bool stats_initialized;
};

struct i40iw_sc_dev {
	struct list_head cqp_cmd_head;	/* head of the CQP command list */
	spinlock_t cqp_lock; /* cqp list sync */
	struct i40iw_dev_uk dev_uk;
	struct i40iw_dev_pestat dev_pestat;
	struct i40iw_dma_mem vf_fpm_query_buf[I40IW_MAX_PE_ENABLED_VF_COUNT];
	u64 fpm_query_buf_pa;
	u64 fpm_commit_buf_pa;
	u64 *fpm_query_buf;
	u64 *fpm_commit_buf;
	void *back_dev;
	struct i40iw_hw *hw;
	u8 __iomem *db_addr;
	struct i40iw_hmc_info *hmc_info;
	struct i40iw_hmc_pble_info *pble_info;
	struct i40iw_vfdev *vf_dev[I40IW_MAX_PE_ENABLED_VF_COUNT];
	struct i40iw_sc_cqp *cqp;
	struct i40iw_sc_aeq *aeq;
	struct i40iw_sc_ceq *ceq[I40IW_CEQ_MAX_COUNT];
	struct i40iw_sc_cq *ccq;
	struct i40iw_cqp_ops *cqp_ops;
	struct i40iw_ccq_ops *ccq_ops;
	struct i40iw_ceq_ops *ceq_ops;
	struct i40iw_aeq_ops *aeq_ops;
	struct i40iw_pd_ops *iw_pd_ops;
	struct i40iw_priv_qp_ops *iw_priv_qp_ops;
	struct i40iw_priv_cq_ops *iw_priv_cq_ops;
	struct i40iw_mr_ops *mr_ops;
	struct i40iw_cqp_misc_ops *cqp_misc_ops;
	struct i40iw_hmc_ops *hmc_ops;
	struct i40iw_vchnl_if vchnl_if;
	u32 ilq_count;
	struct i40iw_virt_mem ilq_mem;
	struct i40iw_puda_rsrc *ilq;
	u32 ieq_count;
	struct i40iw_virt_mem ieq_mem;
	struct i40iw_puda_rsrc *ieq;

	const struct i40iw_vf_cqp_ops *iw_vf_cqp_ops;

	struct i40iw_hmc_fpm_misc hmc_fpm_misc;
	u16 qs_handle;
	u32 debug_mask;
	u16 exception_lan_queue;
	u8 hmc_fn_id;
	bool is_pf;
	bool vchnl_up;
	u8 vf_id;
	wait_queue_head_t vf_reqs;
	u64 cqp_cmd_stats[OP_SIZE_CQP_STAT_ARRAY];
	struct i40iw_vchnl_vf_msg_buffer vchnl_vf_msg_buf;
	u8 hw_rev;
};

struct i40iw_modify_cq_info {
	u64 cq_pa;
	struct i40iw_cqe *cq_base;
	void *pbl_list;
	u32 ceq_id;
	u32 cq_size;
	u32 shadow_read_threshold;
	bool virtual_map;
	u8 pbl_chunk_size;
	bool check_overflow;
	bool cq_resize;
	bool ceq_change;
	bool check_overflow_change;
	u32 first_pm_pbl_idx;
	bool ceq_valid;
};

struct i40iw_create_qp_info {
	u8 next_iwarp_state;
	bool ord_valid;
	bool tcp_ctx_valid;
	bool cq_num_valid;
	bool static_rsrc;
	bool arp_cache_idx_valid;
};

struct i40iw_modify_qp_info {
	u64 rx_win0;
	u64 rx_win1;
	u16 new_mss;
	u8 next_iwarp_state;
	u8 termlen;
	bool ord_valid;
	bool tcp_ctx_valid;
	bool cq_num_valid;
	bool static_rsrc;
	bool arp_cache_idx_valid;
	bool reset_tcp_conn;
	bool remove_hash_idx;
	bool dont_send_term;
	bool dont_send_fin;
	bool cached_var_valid;
	bool mss_change;
	bool force_loopback;
};

struct i40iw_ccq_cqe_info {
	struct i40iw_sc_cqp *cqp;
	u64 scratch;
	u32 op_ret_val;
	u16 maj_err_code;
	u16 min_err_code;
	u8 op_code;
	bool error;
};

struct i40iw_l2params {
	u16 qs_handle_list[I40IW_MAX_USER_PRIORITY];
	u16 mss;
};

struct i40iw_device_init_info {
	u64 fpm_query_buf_pa;
	u64 fpm_commit_buf_pa;
	u64 *fpm_query_buf;
	u64 *fpm_commit_buf;
	struct i40iw_hw *hw;
	void __iomem *bar0;
	enum i40iw_status_code (*vchnl_send)(struct i40iw_sc_dev *, u32, u8 *, u16);
	u16 qs_handle;
	u16 exception_lan_queue;
	u8 hmc_fn_id;
	bool is_pf;
	u32 debug_mask;
};

enum i40iw_cqp_hmc_profile {
	I40IW_HMC_PROFILE_DEFAULT = 1,
	I40IW_HMC_PROFILE_FAVOR_VF = 2,
	I40IW_HMC_PROFILE_EQUAL = 3,
};

struct i40iw_cqp_init_info {
	u64 cqp_compl_ctx;
	u64 host_ctx_pa;
	u64 sq_pa;
	struct i40iw_sc_dev *dev;
	struct i40iw_cqp_quanta *sq;
	u64 *host_ctx;
	u64 *scratch_array;
	u32 sq_size;
	u8 struct_ver;
	bool en_datacenter_tcp;
	u8 hmc_profile;
	u8 enabled_vf_count;
};

struct i40iw_ceq_init_info {
	u64 ceqe_pa;
	struct i40iw_sc_dev *dev;
	u64 *ceqe_base;
	void *pbl_list;
	u32 elem_cnt;
	u32 ceq_id;
	bool virtual_map;
	u8 pbl_chunk_size;
	bool tph_en;
	u8 tph_val;
	u32 first_pm_pbl_idx;
};

struct i40iw_aeq_init_info {
	u64 aeq_elem_pa;
	struct i40iw_sc_dev *dev;
	u32 *aeqe_base;
	void *pbl_list;
	u32 elem_cnt;
	bool virtual_map;
	u8 pbl_chunk_size;
	u32 first_pm_pbl_idx;
};

struct i40iw_ccq_init_info {
	u64 cq_pa;
	u64 shadow_area_pa;
	struct i40iw_sc_dev *dev;
	struct i40iw_cqe *cq_base;
	u64 *shadow_area;
	void *pbl_list;
	u32 num_elem;
	u32 ceq_id;
	u32 shadow_read_threshold;
	bool ceqe_mask;
	bool ceq_id_valid;
	bool tph_en;
	u8 tph_val;
	bool avoid_mem_cflct;
	bool virtual_map;
	u8 pbl_chunk_size;
	u32 first_pm_pbl_idx;
};

struct i40iwarp_offload_info {
	u16 rcv_mark_offset;
	u16 snd_mark_offset;
	u16 pd_id;
	u8 ddp_ver;
	u8 rdmap_ver;
	u8 ord_size;
	u8 ird_size;
	bool wr_rdresp_en;
	bool rd_enable;
	bool snd_mark_en;
	bool rcv_mark_en;
	bool bind_en;
	bool fast_reg_en;
	bool priv_mode_en;
	bool lsmm_present;
	u8 iwarp_mode;
	bool align_hdrs;
	bool rcv_no_mpa_crc;

	u8 last_byte_sent;
};

struct i40iw_tcp_offload_info {
	bool ipv4;
	bool no_nagle;
	bool insert_vlan_tag;
	bool time_stamp;
	u8 cwnd_inc_limit;
	bool drop_ooo_seg;
	u8 dup_ack_thresh;
	u8 ttl;
	u8 src_mac_addr_idx;
	bool avoid_stretch_ack;
	u8 tos;
	u16 src_port;
	u16 dst_port;
	u32 dest_ip_addr0;
	u32 dest_ip_addr1;
	u32 dest_ip_addr2;
	u32 dest_ip_addr3;
	u32 snd_mss;
	u16 vlan_tag;
	u16 arp_idx;
	u32 flow_label;
	bool wscale;
	u8 tcp_state;
	u8 snd_wscale;
	u8 rcv_wscale;
	u32 time_stamp_recent;
	u32 time_stamp_age;
	u32 snd_nxt;
	u32 snd_wnd;
	u32 rcv_nxt;
	u32 rcv_wnd;
	u32 snd_max;
	u32 snd_una;
	u32 srtt;
	u32 rtt_var;
	u32 ss_thresh;
	u32 cwnd;
	u32 snd_wl1;
	u32 snd_wl2;
	u32 max_snd_window;
	u8 rexmit_thresh;
	u32 local_ipaddr0;
	u32 local_ipaddr1;
	u32 local_ipaddr2;
	u32 local_ipaddr3;
	bool ignore_tcp_opt;
	bool ignore_tcp_uns_opt;
};

struct i40iw_qp_host_ctx_info {
	u64 qp_compl_ctx;
	struct i40iw_tcp_offload_info *tcp_info;
	struct i40iwarp_offload_info *iwarp_info;
	u32 send_cq_num;
	u32 rcv_cq_num;
	u16 push_idx;
	bool push_mode_en;
	bool tcp_info_valid;
	bool iwarp_info_valid;
	bool err_rq_idx_valid;
	u16 err_rq_idx;
};

struct i40iw_aeqe_info {
	u64 compl_ctx;
	u32 qp_cq_id;
	u16 ae_id;
	u16 wqe_idx;
	u8 tcp_state;
	u8 iwarp_state;
	bool qp;
	bool cq;
	bool sq;
	bool in_rdrsp_wr;
	bool out_rdrsp;
	u8 q2_data_written;
	bool aeqe_overflow;
};

struct i40iw_allocate_stag_info {
	u64 total_len;
	u32 chunk_size;
	u32 stag_idx;
	u32 page_size;
	u16 pd_id;
	u16 access_rights;
	bool remote_access;
	bool use_hmc_fcn_index;
	u8 hmc_fcn_index;
	bool use_pf_rid;
};

struct i40iw_reg_ns_stag_info {
	u64 reg_addr_pa;
	u64 fbo;
	void *va;
	u64 total_len;
	u32 page_size;
	u32 chunk_size;
	u32 first_pm_pbl_index;
	enum i40iw_addressing_type addr_type;
	i40iw_stag_index stag_idx;
	u16 access_rights;
	u16 pd_id;
	i40iw_stag_key stag_key;
	bool use_hmc_fcn_index;
	u8 hmc_fcn_index;
	bool use_pf_rid;
};

struct i40iw_fast_reg_stag_info {
	u64 wr_id;
	u64 reg_addr_pa;
	u64 fbo;
	void *va;
	u64 total_len;
	u32 page_size;
	u32 chunk_size;
	u32 first_pm_pbl_index;
	enum i40iw_addressing_type addr_type;
	i40iw_stag_index stag_idx;
	u16 access_rights;
	u16 pd_id;
	i40iw_stag_key stag_key;
	bool local_fence;
	bool read_fence;
	bool signaled;
	bool use_hmc_fcn_index;
	u8 hmc_fcn_index;
	bool use_pf_rid;
	bool defer_flag;
};

struct i40iw_dealloc_stag_info {
	u32 stag_idx;
	u16 pd_id;
	bool mr;
	bool dealloc_pbl;
};

struct i40iw_register_shared_stag {
	void *va;
	enum i40iw_addressing_type addr_type;
	i40iw_stag_index new_stag_idx;
	i40iw_stag_index parent_stag_idx;
	u32 access_rights;
	u16 pd_id;
	i40iw_stag_key new_stag_key;
};

struct i40iw_qp_init_info {
	struct i40iw_qp_uk_init_info qp_uk_init_info;
	struct i40iw_sc_pd *pd;
	u64 *host_ctx;
	u8 *q2;
	u64 sq_pa;
	u64 rq_pa;
	u64 host_ctx_pa;
	u64 q2_pa;
	u64 shadow_area_pa;
	u8 sq_tph_val;
	u8 rq_tph_val;
	u8 type;
	bool sq_tph_en;
	bool rq_tph_en;
	bool rcv_tph_en;
	bool xmit_tph_en;
	bool virtual_map;
};

struct i40iw_cq_init_info {
	struct i40iw_sc_dev *dev;
	u64 cq_base_pa;
	u64 shadow_area_pa;
	u32 ceq_id;
	u32 shadow_read_threshold;
	bool virtual_map;
	bool ceqe_mask;
	u8 pbl_chunk_size;
	u32 first_pm_pbl_idx;
	bool ceq_id_valid;
	bool tph_en;
	u8 tph_val;
	u8 type;
	struct i40iw_cq_uk_init_info cq_uk_init_info;
};

struct i40iw_upload_context_info {
	u64 buf_pa;
	bool freeze_qp;
	bool raw_format;
	u32 qp_id;
	u8 qp_type;
};

struct i40iw_add_arp_cache_entry_info {
	u8 mac_addr[6];
	u32 reach_max;
	u16 arp_index;
	bool permanent;
};

struct i40iw_apbvt_info {
	u16 port;
	bool add;
};

enum i40iw_quad_entry_type {
	I40IW_QHASH_TYPE_TCP_ESTABLISHED = 1,
	I40IW_QHASH_TYPE_TCP_SYN,
};

enum i40iw_quad_hash_manage_type {
	I40IW_QHASH_MANAGE_TYPE_DELETE = 0,
	I40IW_QHASH_MANAGE_TYPE_ADD,
	I40IW_QHASH_MANAGE_TYPE_MODIFY
};

struct i40iw_qhash_table_info {
	enum i40iw_quad_hash_manage_type manage;
	enum i40iw_quad_entry_type entry_type;
	bool vlan_valid;
	bool ipv4_valid;
	u8 mac_addr[6];
	u16 vlan_id;
	u16 qs_handle;
	u32 qp_num;
	u32 dest_ip[4];
	u32 src_ip[4];
	u16 dest_port;
	u16 src_port;
};

struct i40iw_local_mac_ipaddr_entry_info {
	u8 mac_addr[6];
	u8 entry_idx;
};

struct i40iw_cqp_manage_push_page_info {
	u32 push_idx;
	u16 qs_handle;
	u8 free_page;
};

struct i40iw_qp_flush_info {
	u16 sq_minor_code;
	u16 sq_major_code;
	u16 rq_minor_code;
	u16 rq_major_code;
	u16 ae_code;
	u8 ae_source;
	bool sq;
	bool rq;
	bool userflushcode;
	bool generate_ae;
};

struct i40iw_cqp_commit_fpm_values {
	u64 qp_base;
	u64 cq_base;
	u32 hte_base;
	u32 arp_base;
	u32 apbvt_inuse_base;
	u32 mr_base;
	u32 xf_base;
	u32 xffl_base;
	u32 q1_base;
	u32 q1fl_base;
	u32 fsimc_base;
	u32 fsiav_base;
	u32 pbl_base;

	u32 qp_cnt;
	u32 cq_cnt;
	u32 hte_cnt;
	u32 arp_cnt;
	u32 mr_cnt;
	u32 xf_cnt;
	u32 xffl_cnt;
	u32 q1_cnt;
	u32 q1fl_cnt;
	u32 fsimc_cnt;
	u32 fsiav_cnt;
	u32 pbl_cnt;
};

struct i40iw_cqp_query_fpm_values {
	u16 first_pe_sd_index;
	u32 qp_objsize;
	u32 cq_objsize;
	u32 hte_objsize;
	u32 arp_objsize;
	u32 mr_objsize;
	u32 xf_objsize;
	u32 q1_objsize;
	u32 fsimc_objsize;
	u32 fsiav_objsize;

	u32 qp_max;
	u32 cq_max;
	u32 hte_max;
	u32 arp_max;
	u32 mr_max;
	u32 xf_max;
	u32 xffl_max;
	u32 q1_max;
	u32 q1fl_max;
	u32 fsimc_max;
	u32 fsiav_max;
	u32 pbl_max;
};

struct i40iw_cqp_ops {
	enum i40iw_status_code (*cqp_init)(struct i40iw_sc_cqp *,
					   struct i40iw_cqp_init_info *);
	enum i40iw_status_code (*cqp_create)(struct i40iw_sc_cqp *, bool, u16 *, u16 *);
	void (*cqp_post_sq)(struct i40iw_sc_cqp *);
	u64 *(*cqp_get_next_send_wqe)(struct i40iw_sc_cqp *, u64 scratch);
	enum i40iw_status_code (*cqp_destroy)(struct i40iw_sc_cqp *);
	enum i40iw_status_code (*poll_for_cqp_op_done)(struct i40iw_sc_cqp *, u8,
						       struct i40iw_ccq_cqe_info *);
};

struct i40iw_ccq_ops {
	enum i40iw_status_code (*ccq_init)(struct i40iw_sc_cq *,
					   struct i40iw_ccq_init_info *);
	enum i40iw_status_code (*ccq_create)(struct i40iw_sc_cq *, u64, bool, bool);
	enum i40iw_status_code (*ccq_destroy)(struct i40iw_sc_cq *, u64, bool);
	enum i40iw_status_code (*ccq_create_done)(struct i40iw_sc_cq *);
	enum i40iw_status_code (*ccq_get_cqe_info)(struct i40iw_sc_cq *,
						   struct i40iw_ccq_cqe_info *);
	void (*ccq_arm)(struct i40iw_sc_cq *);
};

struct i40iw_ceq_ops {
	enum i40iw_status_code (*ceq_init)(struct i40iw_sc_ceq *,
					   struct i40iw_ceq_init_info *);
	enum i40iw_status_code (*ceq_create)(struct i40iw_sc_ceq *, u64, bool);
	enum i40iw_status_code (*cceq_create_done)(struct i40iw_sc_ceq *);
	enum i40iw_status_code (*cceq_destroy_done)(struct i40iw_sc_ceq *);
	enum i40iw_status_code (*cceq_create)(struct i40iw_sc_ceq *, u64);
	enum i40iw_status_code (*ceq_destroy)(struct i40iw_sc_ceq *, u64, bool);
	void *(*process_ceq)(struct i40iw_sc_dev *, struct i40iw_sc_ceq *);
};

struct i40iw_aeq_ops {
	enum i40iw_status_code (*aeq_init)(struct i40iw_sc_aeq *,
					   struct i40iw_aeq_init_info *);
	enum i40iw_status_code (*aeq_create)(struct i40iw_sc_aeq *, u64, bool);
	enum i40iw_status_code (*aeq_destroy)(struct i40iw_sc_aeq *, u64, bool);
	enum i40iw_status_code (*get_next_aeqe)(struct i40iw_sc_aeq *,
						struct i40iw_aeqe_info *);
	enum i40iw_status_code (*repost_aeq_entries)(struct i40iw_sc_dev *, u32);
	enum i40iw_status_code (*aeq_create_done)(struct i40iw_sc_aeq *);
	enum i40iw_status_code (*aeq_destroy_done)(struct i40iw_sc_aeq *);
};

struct i40iw_pd_ops {
	void (*pd_init)(struct i40iw_sc_dev *, struct i40iw_sc_pd *, u16);
};

struct i40iw_priv_qp_ops {
	enum i40iw_status_code (*qp_init)(struct i40iw_sc_qp *, struct i40iw_qp_init_info *);
	enum i40iw_status_code (*qp_create)(struct i40iw_sc_qp *,
					    struct i40iw_create_qp_info *, u64, bool);
	enum i40iw_status_code (*qp_modify)(struct i40iw_sc_qp *,
					    struct i40iw_modify_qp_info *, u64, bool);
	enum i40iw_status_code (*qp_destroy)(struct i40iw_sc_qp *, u64, bool, bool, bool);
	enum i40iw_status_code (*qp_flush_wqes)(struct i40iw_sc_qp *,
						struct i40iw_qp_flush_info *, u64, bool);
	enum i40iw_status_code (*qp_upload_context)(struct i40iw_sc_dev *,
						    struct i40iw_upload_context_info *,
						    u64, bool);
	enum i40iw_status_code (*qp_setctx)(struct i40iw_sc_qp *, u64 *,
					    struct i40iw_qp_host_ctx_info *);

	void (*qp_send_lsmm)(struct i40iw_sc_qp *, void *, u32, i40iw_stag);
	void (*qp_send_lsmm_nostag)(struct i40iw_sc_qp *, void *, u32);
	void (*qp_send_rtt)(struct i40iw_sc_qp *, bool);
	enum i40iw_status_code (*qp_post_wqe0)(struct i40iw_sc_qp *, u8);
	enum i40iw_status_code (*iw_mr_fast_register)(struct i40iw_sc_qp *,
						      struct i40iw_fast_reg_stag_info *,
						      bool);
};

struct i40iw_priv_cq_ops {
	enum i40iw_status_code (*cq_init)(struct i40iw_sc_cq *, struct i40iw_cq_init_info *);
	enum i40iw_status_code (*cq_create)(struct i40iw_sc_cq *, u64, bool, bool);
	enum i40iw_status_code (*cq_destroy)(struct i40iw_sc_cq *, u64, bool);
	enum i40iw_status_code (*cq_modify)(struct i40iw_sc_cq *,
					    struct i40iw_modify_cq_info *, u64, bool);
};

struct i40iw_mr_ops {
	enum i40iw_status_code (*alloc_stag)(struct i40iw_sc_dev *,
					     struct i40iw_allocate_stag_info *, u64, bool);
	enum i40iw_status_code (*mr_reg_non_shared)(struct i40iw_sc_dev *,
						    struct i40iw_reg_ns_stag_info *,
						    u64, bool);
	enum i40iw_status_code (*mr_reg_shared)(struct i40iw_sc_dev *,
						struct i40iw_register_shared_stag *,
						u64, bool);
	enum i40iw_status_code (*dealloc_stag)(struct i40iw_sc_dev *,
					       struct i40iw_dealloc_stag_info *,
					       u64, bool);
	enum i40iw_status_code (*query_stag)(struct i40iw_sc_dev *, u64, u32, bool);
	enum i40iw_status_code (*mw_alloc)(struct i40iw_sc_dev *, u64, u32, u16, bool);
};

struct i40iw_cqp_misc_ops {
	enum i40iw_status_code (*manage_push_page)(struct i40iw_sc_cqp *,
						   struct i40iw_cqp_manage_push_page_info *,
						   u64, bool);
	enum i40iw_status_code (*manage_hmc_pm_func_table)(struct i40iw_sc_cqp *,
							   u64, u8, bool, bool);
	enum i40iw_status_code (*set_hmc_resource_profile)(struct i40iw_sc_cqp *,
							   u64, u8, u8, bool, bool);
	enum i40iw_status_code (*commit_fpm_values)(struct i40iw_sc_cqp *, u64, u8,
						    struct i40iw_dma_mem *, bool, u8);
	enum i40iw_status_code (*query_fpm_values)(struct i40iw_sc_cqp *, u64, u8,
						   struct i40iw_dma_mem *, bool, u8);
	enum i40iw_status_code (*static_hmc_pages_allocated)(struct i40iw_sc_cqp *,
							     u64, u8, bool, bool);
	enum i40iw_status_code (*add_arp_cache_entry)(struct i40iw_sc_cqp *,
						      struct i40iw_add_arp_cache_entry_info *,
						      u64, bool);
	enum i40iw_status_code (*del_arp_cache_entry)(struct i40iw_sc_cqp *, u64, u16, bool);
	enum i40iw_status_code (*query_arp_cache_entry)(struct i40iw_sc_cqp *, u64, u16, bool);
	enum i40iw_status_code (*manage_apbvt_entry)(struct i40iw_sc_cqp *,
						     struct i40iw_apbvt_info *, u64, bool);
	enum i40iw_status_code (*manage_qhash_table_entry)(struct i40iw_sc_cqp *,
							   struct i40iw_qhash_table_info *, u64, bool);
	enum i40iw_status_code (*alloc_local_mac_ipaddr_table_entry)(struct i40iw_sc_cqp *, u64, bool);
	enum i40iw_status_code (*add_local_mac_ipaddr_entry)(struct i40iw_sc_cqp *,
							     struct i40iw_local_mac_ipaddr_entry_info *,
							     u64, bool);
	enum i40iw_status_code (*del_local_mac_ipaddr_entry)(struct i40iw_sc_cqp *, u64, u8, u8, bool);
	enum i40iw_status_code (*cqp_nop)(struct i40iw_sc_cqp *, u64, bool);
	enum i40iw_status_code (*commit_fpm_values_done)(struct i40iw_sc_cqp
							  *);
	enum i40iw_status_code (*query_fpm_values_done)(struct i40iw_sc_cqp *);
	enum i40iw_status_code (*manage_hmc_pm_func_table_done)(struct i40iw_sc_cqp *);
	enum i40iw_status_code (*update_suspend_qp)(struct i40iw_sc_cqp *, struct i40iw_sc_qp *, u64);
	enum i40iw_status_code (*update_resume_qp)(struct i40iw_sc_cqp *, struct i40iw_sc_qp *, u64);
};

struct i40iw_hmc_ops {
	enum i40iw_status_code (*init_iw_hmc)(struct i40iw_sc_dev *, u8);
	enum i40iw_status_code (*parse_fpm_query_buf)(u64 *, struct i40iw_hmc_info *,
						      struct i40iw_hmc_fpm_misc *);
	enum i40iw_status_code (*configure_iw_fpm)(struct i40iw_sc_dev *, u8);
	enum i40iw_status_code (*parse_fpm_commit_buf)(u64 *, struct i40iw_hmc_obj_info *, u32 *sd);
	enum i40iw_status_code (*create_hmc_object)(struct i40iw_sc_dev *dev,
						    struct i40iw_hmc_create_obj_info *);
	enum i40iw_status_code (*del_hmc_object)(struct i40iw_sc_dev *dev,
						 struct i40iw_hmc_del_obj_info *,
						 bool reset);
	enum i40iw_status_code (*pf_init_vfhmc)(struct i40iw_sc_dev *, u8, u32 *);
	enum i40iw_status_code (*vf_configure_vffpm)(struct i40iw_sc_dev *, u32 *);
};

struct cqp_info {
	union {
		struct {
			struct i40iw_sc_qp *qp;
			struct i40iw_create_qp_info info;
			u64 scratch;
		} qp_create;

		struct {
			struct i40iw_sc_qp *qp;
			struct i40iw_modify_qp_info info;
			u64 scratch;
		} qp_modify;

		struct {
			struct i40iw_sc_qp *qp;
			u64 scratch;
			bool remove_hash_idx;
			bool ignore_mw_bnd;
		} qp_destroy;

		struct {
			struct i40iw_sc_cq *cq;
			u64 scratch;
			bool check_overflow;
		} cq_create;

		struct {
			struct i40iw_sc_cq *cq;
			u64 scratch;
		} cq_destroy;

		struct {
			struct i40iw_sc_dev *dev;
			struct i40iw_allocate_stag_info info;
			u64 scratch;
		} alloc_stag;

		struct {
			struct i40iw_sc_dev *dev;
			u64 scratch;
			u32 mw_stag_index;
			u16 pd_id;
		} mw_alloc;

		struct {
			struct i40iw_sc_dev *dev;
			struct i40iw_reg_ns_stag_info info;
			u64 scratch;
		} mr_reg_non_shared;

		struct {
			struct i40iw_sc_dev *dev;
			struct i40iw_dealloc_stag_info info;
			u64 scratch;
		} dealloc_stag;

		struct {
			struct i40iw_sc_cqp *cqp;
			struct i40iw_local_mac_ipaddr_entry_info info;
			u64 scratch;
		} add_local_mac_ipaddr_entry;

		struct {
			struct i40iw_sc_cqp *cqp;
			struct i40iw_add_arp_cache_entry_info info;
			u64 scratch;
		} add_arp_cache_entry;

		struct {
			struct i40iw_sc_cqp *cqp;
			u64 scratch;
			u8 entry_idx;
			u8 ignore_ref_count;
		} del_local_mac_ipaddr_entry;

		struct {
			struct i40iw_sc_cqp *cqp;
			u64 scratch;
			u16 arp_index;
		} del_arp_cache_entry;

		struct {
			struct i40iw_sc_cqp *cqp;
			struct i40iw_manage_vf_pble_info info;
			u64 scratch;
		} manage_vf_pble_bp;

		struct {
			struct i40iw_sc_cqp *cqp;
			struct i40iw_cqp_manage_push_page_info info;
			u64 scratch;
		} manage_push_page;

		struct {
			struct i40iw_sc_dev *dev;
			struct i40iw_upload_context_info info;
			u64 scratch;
		} qp_upload_context;

		struct {
			struct i40iw_sc_cqp *cqp;
			u64 scratch;
		} alloc_local_mac_ipaddr_entry;

		struct {
			struct i40iw_sc_dev *dev;
			struct i40iw_hmc_fcn_info info;
			u64 scratch;
		} manage_hmc_pm;

		struct {
			struct i40iw_sc_ceq *ceq;
			u64 scratch;
		} ceq_create;

		struct {
			struct i40iw_sc_ceq *ceq;
			u64 scratch;
		} ceq_destroy;

		struct {
			struct i40iw_sc_aeq *aeq;
			u64 scratch;
		} aeq_create;

		struct {
			struct i40iw_sc_aeq *aeq;
			u64 scratch;
		} aeq_destroy;

		struct {
			struct i40iw_sc_qp *qp;
			struct i40iw_qp_flush_info info;
			u64 scratch;
		} qp_flush_wqes;

		struct {
			struct i40iw_sc_cqp *cqp;
			void *fpm_values_va;
			u64 fpm_values_pa;
			u8 hmc_fn_id;
			u64 scratch;
		} query_fpm_values;

		struct {
			struct i40iw_sc_cqp *cqp;
			void *fpm_values_va;
			u64 fpm_values_pa;
			u8 hmc_fn_id;
			u64 scratch;
		} commit_fpm_values;

		struct {
			struct i40iw_sc_cqp *cqp;
			struct i40iw_apbvt_info info;
			u64 scratch;
		} manage_apbvt_entry;

		struct {
			struct i40iw_sc_cqp *cqp;
			struct i40iw_qhash_table_info info;
			u64 scratch;
		} manage_qhash_table_entry;

		struct {
			struct i40iw_sc_dev *dev;
			struct i40iw_update_sds_info info;
			u64 scratch;
		} update_pe_sds;

		struct {
			struct i40iw_sc_cqp *cqp;
			struct i40iw_sc_qp *qp;
			u64 scratch;
		} suspend_resume;
	} u;
};

struct cqp_commands_info {
	struct list_head cqp_cmd_entry;
	u8 cqp_cmd;
	u8 post_sq;
	struct cqp_info in;
};

struct i40iw_virtchnl_work_info {
	void (*callback_fcn)(void *vf_dev);
	void *worker_vf_dev;
};

#endif
