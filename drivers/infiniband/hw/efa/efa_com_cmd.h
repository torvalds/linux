/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018-2025 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef _EFA_COM_CMD_H_
#define _EFA_COM_CMD_H_

#include "efa_com.h"

#define EFA_GID_SIZE 16

struct efa_com_create_qp_params {
	u64 rq_base_addr;
	u32 send_cq_idx;
	u32 recv_cq_idx;
	/*
	 * Send descriptor ring size in bytes,
	 * sufficient for user-provided number of WQEs and SGL size
	 */
	u32 sq_ring_size_in_bytes;
	/* Max number of WQEs that will be posted on send queue */
	u32 sq_depth;
	/* Recv descriptor ring size in bytes */
	u32 rq_ring_size_in_bytes;
	u32 rq_depth;
	u16 pd;
	u16 uarn;
	u8 qp_type;
	u8 sl;
	u8 unsolicited_write_recv : 1;
};

struct efa_com_create_qp_result {
	u32 qp_handle;
	u32 qp_num;
	u32 sq_db_offset;
	u32 rq_db_offset;
	u32 llq_descriptors_offset;
	u16 send_sub_cq_idx;
	u16 recv_sub_cq_idx;
};

struct efa_com_modify_qp_params {
	u32 modify_mask;
	u32 qp_handle;
	u32 qp_state;
	u32 cur_qp_state;
	u32 qkey;
	u32 sq_psn;
	u8 sq_drained_async_notify;
	u8 rnr_retry;
};

struct efa_com_query_qp_params {
	u32 qp_handle;
};

struct efa_com_query_qp_result {
	u32 qp_state;
	u32 qkey;
	u32 sq_draining;
	u32 sq_psn;
	u8 rnr_retry;
};

struct efa_com_destroy_qp_params {
	u32 qp_handle;
};

struct efa_com_create_cq_params {
	/* cq physical base address in OS memory */
	dma_addr_t dma_addr;
	/* completion queue depth in # of entries */
	u16 sub_cq_depth;
	u16 num_sub_cqs;
	u16 uarn;
	u16 eqn;
	u8 entry_size_in_bytes;
	u8 interrupt_mode_enabled : 1;
	u8 set_src_addr : 1;
};

struct efa_com_create_cq_result {
	/* cq identifier */
	u16 cq_idx;
	/* actual cq depth in # of entries */
	u16 actual_depth;
	u32 db_off;
	bool db_valid;
};

struct efa_com_destroy_cq_params {
	u16 cq_idx;
};

struct efa_com_create_ah_params {
	u16 pdn;
	/* Destination address in network byte order */
	u8 dest_addr[EFA_GID_SIZE];
};

struct efa_com_create_ah_result {
	u16 ah;
};

struct efa_com_destroy_ah_params {
	u16 ah;
	u16 pdn;
};

struct efa_com_get_device_attr_result {
	u8 addr[EFA_GID_SIZE];
	u64 page_size_cap;
	u64 max_mr_pages;
	u64 guid;
	u32 mtu;
	u32 fw_version;
	u32 admin_api_version;
	u32 device_version;
	u32 supported_features;
	u32 phys_addr_width;
	u32 virt_addr_width;
	u32 max_qp;
	u32 max_sq_depth; /* wqes */
	u32 max_rq_depth; /* wqes */
	u32 max_cq;
	u32 max_cq_depth; /* cqes */
	u32 inline_buf_size;
	u32 max_mr;
	u32 max_pd;
	u32 max_ah;
	u32 max_llq_size;
	u32 max_rdma_size;
	u32 device_caps;
	u32 max_eq;
	u32 max_eq_depth;
	u32 event_bitmask; /* EQ events bitmask */
	u16 sub_cqs_per_cq;
	u16 max_sq_sge;
	u16 max_rq_sge;
	u16 max_wr_rdma_sge;
	u16 max_tx_batch;
	u16 min_sq_depth;
	u16 max_link_speed_gbps;
	u8 db_bar;
};

struct efa_com_get_hw_hints_result {
	u16 mmio_read_timeout;
	u16 driver_watchdog_timeout;
	u16 admin_completion_timeout;
	u16 poll_interval;
	u32 reserved[4];
};

struct efa_com_mem_addr {
	u32 mem_addr_low;
	u32 mem_addr_high;
};

/* Used at indirect mode page list chunks for chaining */
struct efa_com_ctrl_buff_info {
	/* indicates length of the buffer pointed by control_buffer_address. */
	u32 length;
	/* points to control buffer (direct or indirect) */
	struct efa_com_mem_addr address;
};

struct efa_com_reg_mr_params {
	/* Memory region length, in bytes. */
	u64 mr_length_in_bytes;
	/* IO Virtual Address associated with this MR. */
	u64 iova;
	/* words 8:15: Physical Buffer List, each element is page-aligned. */
	union {
		/*
		 * Inline array of physical addresses of app pages
		 * (optimization for short region reservations)
		 */
		u64 inline_pbl_array[4];
		/*
		 * Describes the next physically contiguous chunk of indirect
		 * page list. A page list contains physical addresses of command
		 * data pages. Data pages are 4KB; page list chunks are
		 * variable-sized.
		 */
		struct efa_com_ctrl_buff_info pbl;
	} pbl;
	/* number of pages in PBL (redundant, could be calculated) */
	u32 page_num;
	/* Protection Domain */
	u16 pd;
	/*
	 * phys_page_size_shift - page size is (1 << phys_page_size_shift)
	 * Page size is used for building the Virtual to Physical
	 * address mapping
	 */
	u8 page_shift;
	/* see permissions field of struct efa_admin_reg_mr_cmd */
	u8 permissions;
	u8 inline_pbl;
	u8 indirect;
};

struct efa_com_mr_interconnect_info {
	u16 recv_ic_id;
	u16 rdma_read_ic_id;
	u16 rdma_recv_ic_id;
	u8 recv_ic_id_valid : 1;
	u8 rdma_read_ic_id_valid : 1;
	u8 rdma_recv_ic_id_valid : 1;
};

struct efa_com_reg_mr_result {
	/*
	 * To be used in conjunction with local buffers references in SQ and
	 * RQ WQE
	 */
	u32 l_key;
	/*
	 * To be used in incoming RDMA semantics messages to refer to remotely
	 * accessed memory region
	 */
	u32 r_key;
	struct efa_com_mr_interconnect_info ic_info;
};

struct efa_com_dereg_mr_params {
	u32 l_key;
};

struct efa_com_alloc_pd_result {
	u16 pdn;
};

struct efa_com_dealloc_pd_params {
	u16 pdn;
};

struct efa_com_alloc_uar_result {
	u16 uarn;
};

struct efa_com_dealloc_uar_params {
	u16 uarn;
};

struct efa_com_get_stats_params {
	/* see enum efa_admin_get_stats_type */
	u8 type;
	/* see enum efa_admin_get_stats_scope */
	u8 scope;
	u16 scope_modifier;
};

struct efa_com_basic_stats {
	u64 tx_bytes;
	u64 tx_pkts;
	u64 rx_bytes;
	u64 rx_pkts;
	u64 rx_drops;
};

struct efa_com_messages_stats {
	u64 send_bytes;
	u64 send_wrs;
	u64 recv_bytes;
	u64 recv_wrs;
};

struct efa_com_rdma_read_stats {
	u64 read_wrs;
	u64 read_bytes;
	u64 read_wr_err;
	u64 read_resp_bytes;
};

struct efa_com_rdma_write_stats {
	u64 write_wrs;
	u64 write_bytes;
	u64 write_wr_err;
	u64 write_recv_bytes;
};

struct efa_com_network_stats {
	u64 retrans_bytes;
	u64 retrans_pkts;
	u64 retrans_timeout_events;
	u64 unresponsive_remote_events;
	u64 impaired_remote_conn_events;
};

union efa_com_get_stats_result {
	struct efa_com_basic_stats basic_stats;
	struct efa_com_messages_stats messages_stats;
	struct efa_com_rdma_read_stats rdma_read_stats;
	struct efa_com_rdma_write_stats rdma_write_stats;
	struct efa_com_network_stats network_stats;
};

int efa_com_create_qp(struct efa_com_dev *edev,
		      struct efa_com_create_qp_params *params,
		      struct efa_com_create_qp_result *res);
int efa_com_modify_qp(struct efa_com_dev *edev,
		      struct efa_com_modify_qp_params *params);
int efa_com_query_qp(struct efa_com_dev *edev,
		     struct efa_com_query_qp_params *params,
		     struct efa_com_query_qp_result *result);
int efa_com_destroy_qp(struct efa_com_dev *edev,
		       struct efa_com_destroy_qp_params *params);
int efa_com_create_cq(struct efa_com_dev *edev,
		      struct efa_com_create_cq_params *params,
		      struct efa_com_create_cq_result *result);
int efa_com_destroy_cq(struct efa_com_dev *edev,
		       struct efa_com_destroy_cq_params *params);
int efa_com_register_mr(struct efa_com_dev *edev,
			struct efa_com_reg_mr_params *params,
			struct efa_com_reg_mr_result *result);
int efa_com_dereg_mr(struct efa_com_dev *edev,
		     struct efa_com_dereg_mr_params *params);
int efa_com_create_ah(struct efa_com_dev *edev,
		      struct efa_com_create_ah_params *params,
		      struct efa_com_create_ah_result *result);
int efa_com_destroy_ah(struct efa_com_dev *edev,
		       struct efa_com_destroy_ah_params *params);
int efa_com_get_device_attr(struct efa_com_dev *edev,
			    struct efa_com_get_device_attr_result *result);
int efa_com_get_hw_hints(struct efa_com_dev *edev,
			 struct efa_com_get_hw_hints_result *result);
bool
efa_com_check_supported_feature_id(struct efa_com_dev *edev,
				   enum efa_admin_aq_feature_id feature_id);
int efa_com_set_feature_ex(struct efa_com_dev *edev,
			   struct efa_admin_set_feature_resp *set_resp,
			   struct efa_admin_set_feature_cmd *set_cmd,
			   enum efa_admin_aq_feature_id feature_id,
			   dma_addr_t control_buf_dma_addr,
			   u32 control_buff_size);
int efa_com_set_aenq_config(struct efa_com_dev *edev, u32 groups);
int efa_com_alloc_pd(struct efa_com_dev *edev,
		     struct efa_com_alloc_pd_result *result);
int efa_com_dealloc_pd(struct efa_com_dev *edev,
		       struct efa_com_dealloc_pd_params *params);
int efa_com_alloc_uar(struct efa_com_dev *edev,
		      struct efa_com_alloc_uar_result *result);
int efa_com_dealloc_uar(struct efa_com_dev *edev,
			struct efa_com_dealloc_uar_params *params);
int efa_com_get_stats(struct efa_com_dev *edev,
		      struct efa_com_get_stats_params *params,
		      union efa_com_get_stats_result *result);

#endif /* _EFA_COM_CMD_H_ */
