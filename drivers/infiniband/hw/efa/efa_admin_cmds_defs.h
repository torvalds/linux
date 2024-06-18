/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018-2024 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef _EFA_ADMIN_CMDS_H_
#define _EFA_ADMIN_CMDS_H_

#define EFA_ADMIN_API_VERSION_MAJOR          0
#define EFA_ADMIN_API_VERSION_MINOR          1

/* EFA admin queue opcodes */
enum efa_admin_aq_opcode {
	EFA_ADMIN_CREATE_QP                         = 1,
	EFA_ADMIN_MODIFY_QP                         = 2,
	EFA_ADMIN_QUERY_QP                          = 3,
	EFA_ADMIN_DESTROY_QP                        = 4,
	EFA_ADMIN_CREATE_AH                         = 5,
	EFA_ADMIN_DESTROY_AH                        = 6,
	EFA_ADMIN_REG_MR                            = 7,
	EFA_ADMIN_DEREG_MR                          = 8,
	EFA_ADMIN_CREATE_CQ                         = 9,
	EFA_ADMIN_DESTROY_CQ                        = 10,
	EFA_ADMIN_GET_FEATURE                       = 11,
	EFA_ADMIN_SET_FEATURE                       = 12,
	EFA_ADMIN_GET_STATS                         = 13,
	EFA_ADMIN_ALLOC_PD                          = 14,
	EFA_ADMIN_DEALLOC_PD                        = 15,
	EFA_ADMIN_ALLOC_UAR                         = 16,
	EFA_ADMIN_DEALLOC_UAR                       = 17,
	EFA_ADMIN_CREATE_EQ                         = 18,
	EFA_ADMIN_DESTROY_EQ                        = 19,
	EFA_ADMIN_MAX_OPCODE                        = 19,
};

enum efa_admin_aq_feature_id {
	EFA_ADMIN_DEVICE_ATTR                       = 1,
	EFA_ADMIN_AENQ_CONFIG                       = 2,
	EFA_ADMIN_NETWORK_ATTR                      = 3,
	EFA_ADMIN_QUEUE_ATTR                        = 4,
	EFA_ADMIN_HW_HINTS                          = 5,
	EFA_ADMIN_HOST_INFO                         = 6,
	EFA_ADMIN_EVENT_QUEUE_ATTR                  = 7,
};

/* QP transport type */
enum efa_admin_qp_type {
	/* Unreliable Datagram */
	EFA_ADMIN_QP_TYPE_UD                        = 1,
	/* Scalable Reliable Datagram */
	EFA_ADMIN_QP_TYPE_SRD                       = 2,
};

/* QP state */
enum efa_admin_qp_state {
	EFA_ADMIN_QP_STATE_RESET                    = 0,
	EFA_ADMIN_QP_STATE_INIT                     = 1,
	EFA_ADMIN_QP_STATE_RTR                      = 2,
	EFA_ADMIN_QP_STATE_RTS                      = 3,
	EFA_ADMIN_QP_STATE_SQD                      = 4,
	EFA_ADMIN_QP_STATE_SQE                      = 5,
	EFA_ADMIN_QP_STATE_ERR                      = 6,
};

enum efa_admin_get_stats_type {
	EFA_ADMIN_GET_STATS_TYPE_BASIC              = 0,
	EFA_ADMIN_GET_STATS_TYPE_MESSAGES           = 1,
	EFA_ADMIN_GET_STATS_TYPE_RDMA_READ          = 2,
	EFA_ADMIN_GET_STATS_TYPE_RDMA_WRITE         = 3,
};

enum efa_admin_get_stats_scope {
	EFA_ADMIN_GET_STATS_SCOPE_ALL               = 0,
	EFA_ADMIN_GET_STATS_SCOPE_QUEUE             = 1,
};

/*
 * QP allocation sizes, converted by fabric QueuePair (QP) create command
 * from QP capabilities.
 */
struct efa_admin_qp_alloc_size {
	/* Send descriptor ring size in bytes */
	u32 send_queue_ring_size;

	/* Max number of WQEs that can be outstanding on send queue. */
	u32 send_queue_depth;

	/*
	 * Recv descriptor ring size in bytes, sufficient for user-provided
	 * number of WQEs
	 */
	u32 recv_queue_ring_size;

	/* Max number of WQEs that can be outstanding on recv queue */
	u32 recv_queue_depth;
};

struct efa_admin_create_qp_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* Protection Domain associated with this QP */
	u16 pd;

	/* QP type */
	u8 qp_type;

	/*
	 * 0 : sq_virt - If set, SQ ring base address is
	 *    virtual (IOVA returned by MR registration)
	 * 1 : rq_virt - If set, RQ ring base address is
	 *    virtual (IOVA returned by MR registration)
	 * 2 : unsolicited_write_recv - If set, work requests
	 *    will not be consumed for incoming RDMA write with
	 *    immediate
	 * 7:3 : reserved - MBZ
	 */
	u8 flags;

	/*
	 * Send queue (SQ) ring base physical address. This field is not
	 * used if this is a Low Latency Queue(LLQ).
	 */
	u64 sq_base_addr;

	/* Receive queue (RQ) ring base address. */
	u64 rq_base_addr;

	/* Index of CQ to be associated with Send Queue completions */
	u32 send_cq_idx;

	/* Index of CQ to be associated with Recv Queue completions */
	u32 recv_cq_idx;

	/*
	 * Memory registration key for the SQ ring, used only when not in
	 * LLQ mode and base address is virtual
	 */
	u32 sq_l_key;

	/*
	 * Memory registration key for the RQ ring, used only when base
	 * address is virtual
	 */
	u32 rq_l_key;

	/* Requested QP allocation sizes */
	struct efa_admin_qp_alloc_size qp_alloc_size;

	/* UAR number */
	u16 uar;

	/* MBZ */
	u16 reserved;

	/* MBZ */
	u32 reserved2;
};

struct efa_admin_create_qp_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;

	/*
	 * Opaque handle to be used for consequent admin operations on the
	 * QP
	 */
	u32 qp_handle;

	/*
	 * QP number in the given EFA virtual device. Least-significant bits (as
	 * needed according to max_qp) carry unique QP ID
	 */
	u16 qp_num;

	/* MBZ */
	u16 reserved;

	/* Index of sub-CQ for Send Queue completions */
	u16 send_sub_cq_idx;

	/* Index of sub-CQ for Receive Queue completions */
	u16 recv_sub_cq_idx;

	/* SQ doorbell address, as offset to PCIe DB BAR */
	u32 sq_db_offset;

	/* RQ doorbell address, as offset to PCIe DB BAR */
	u32 rq_db_offset;

	/*
	 * low latency send queue ring base address as an offset to PCIe
	 * MMIO LLQ_MEM BAR
	 */
	u32 llq_descriptors_offset;
};

struct efa_admin_modify_qp_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/*
	 * Mask indicating which fields should be updated
	 * 0 : qp_state
	 * 1 : cur_qp_state
	 * 2 : qkey
	 * 3 : sq_psn
	 * 4 : sq_drained_async_notify
	 * 5 : rnr_retry
	 * 31:6 : reserved
	 */
	u32 modify_mask;

	/* QP handle returned by create_qp command */
	u32 qp_handle;

	/* QP state */
	u32 qp_state;

	/* Override current QP state (before applying the transition) */
	u32 cur_qp_state;

	/* QKey */
	u32 qkey;

	/* SQ PSN */
	u32 sq_psn;

	/* Enable async notification when SQ is drained */
	u8 sq_drained_async_notify;

	/* Number of RNR retries (valid only for SRD QPs) */
	u8 rnr_retry;

	/* MBZ */
	u16 reserved2;
};

struct efa_admin_modify_qp_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;
};

struct efa_admin_query_qp_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* QP handle returned by create_qp command */
	u32 qp_handle;
};

struct efa_admin_query_qp_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;

	/* QP state */
	u32 qp_state;

	/* QKey */
	u32 qkey;

	/* SQ PSN */
	u32 sq_psn;

	/* Indicates that draining is in progress */
	u8 sq_draining;

	/* Number of RNR retries (valid only for SRD QPs) */
	u8 rnr_retry;

	/* MBZ */
	u16 reserved2;
};

struct efa_admin_destroy_qp_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* QP handle returned by create_qp command */
	u32 qp_handle;
};

struct efa_admin_destroy_qp_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;
};

/*
 * Create Address Handle command parameters. Must not be called more than
 * once for the same destination
 */
struct efa_admin_create_ah_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* Destination address in network byte order */
	u8 dest_addr[16];

	/* PD number */
	u16 pd;

	/* MBZ */
	u16 reserved;
};

struct efa_admin_create_ah_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;

	/* Target interface address handle (opaque) */
	u16 ah;

	/* MBZ */
	u16 reserved;
};

struct efa_admin_destroy_ah_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* Target interface address handle (opaque) */
	u16 ah;

	/* PD number */
	u16 pd;
};

struct efa_admin_destroy_ah_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;
};

/*
 * Registration of MemoryRegion, required for QP working with Virtual
 * Addresses. In standard verbs semantics, region length is limited to 2GB
 * space, but EFA offers larger MR support for large memory space, to ease
 * on users working with very large datasets (i.e. full GPU memory mapping).
 */
struct efa_admin_reg_mr_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* Protection Domain */
	u16 pd;

	/* MBZ */
	u16 reserved16_w1;

	/* Physical Buffer List, each element is page-aligned. */
	union {
		/*
		 * Inline array of guest-physical page addresses of user
		 * memory pages (optimization for short region
		 * registrations)
		 */
		u64 inline_pbl_array[4];

		/* points to PBL (direct or indirect, chained if needed) */
		struct efa_admin_ctrl_buff_info pbl;
	} pbl;

	/* Memory region length, in bytes. */
	u64 mr_length;

	/*
	 * flags and page size
	 * 4:0 : phys_page_size_shift - page size is (1 <<
	 *    phys_page_size_shift). Page size is used for
	 *    building the Virtual to Physical address mapping
	 * 6:5 : reserved - MBZ
	 * 7 : mem_addr_phy_mode_en - Enable bit for physical
	 *    memory registration (no translation), can be used
	 *    only by privileged clients. If set, PBL must
	 *    contain a single entry.
	 */
	u8 flags;

	/*
	 * permissions
	 * 0 : local_write_enable - Local write permissions:
	 *    must be set for RQ buffers and buffers posted for
	 *    RDMA Read requests
	 * 1 : remote_write_enable - Remote write
	 *    permissions: must be set to enable RDMA write to
	 *    the region
	 * 2 : remote_read_enable - Remote read permissions:
	 *    must be set to enable RDMA read from the region
	 * 7:3 : reserved2 - MBZ
	 */
	u8 permissions;

	/* MBZ */
	u16 reserved16_w5;

	/* number of pages in PBL (redundant, could be calculated) */
	u32 page_num;

	/*
	 * IO Virtual Address associated with this MR. If
	 * mem_addr_phy_mode_en is set, contains the physical address of
	 * the region.
	 */
	u64 iova;
};

struct efa_admin_reg_mr_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;

	/*
	 * L_Key, to be used in conjunction with local buffer references in
	 * SQ and RQ WQE, or with virtual RQ/CQ rings
	 */
	u32 l_key;

	/*
	 * R_Key, to be used in RDMA messages to refer to remotely accessed
	 * memory region
	 */
	u32 r_key;

	/*
	 * Mask indicating which fields have valid values
	 * 0 : recv_ic_id
	 * 1 : rdma_read_ic_id
	 * 2 : rdma_recv_ic_id
	 */
	u8 validity;

	/*
	 * Physical interconnect used by the device to reach the MR for receive
	 * operation
	 */
	u8 recv_ic_id;

	/*
	 * Physical interconnect used by the device to reach the MR for RDMA
	 * read operation
	 */
	u8 rdma_read_ic_id;

	/*
	 * Physical interconnect used by the device to reach the MR for RDMA
	 * write receive
	 */
	u8 rdma_recv_ic_id;
};

struct efa_admin_dereg_mr_cmd {
	/* Common Admin Queue descriptor */
	struct efa_admin_aq_common_desc aq_common_desc;

	/* L_Key, memory region's l_key */
	u32 l_key;
};

struct efa_admin_dereg_mr_resp {
	/* Common Admin Queue completion descriptor */
	struct efa_admin_acq_common_desc acq_common_desc;
};

struct efa_admin_create_cq_cmd {
	struct efa_admin_aq_common_desc aq_common_desc;

	/*
	 * 4:0 : reserved5 - MBZ
	 * 5 : interrupt_mode_enabled - if set, cq operates
	 *    in interrupt mode (i.e. CQ events and EQ elements
	 *    are generated), otherwise - polling
	 * 6 : virt - If set, ring base address is virtual
	 *    (IOVA returned by MR registration)
	 * 7 : reserved6 - MBZ
	 */
	u8 cq_caps_1;

	/*
	 * 4:0 : cq_entry_size_words - size of CQ entry in
	 *    32-bit words, valid values: 4, 8.
	 * 5 : set_src_addr - If set, source address will be
	 *    filled on RX completions from unknown senders.
	 *    Requires 8 words CQ entry size.
	 * 7:6 : reserved7 - MBZ
	 */
	u8 cq_caps_2;

	/* completion queue depth in # of entries. must be power of 2 */
	u16 cq_depth;

	/* EQ number assigned to this cq */
	u16 eqn;

	/* MBZ */
	u16 reserved;

	/*
	 * CQ ring base address, virtual or physical depending on 'virt'
	 * flag
	 */
	struct efa_common_mem_addr cq_ba;

	/*
	 * Memory registration key for the ring, used only when base
	 * address is virtual
	 */
	u32 l_key;

	/*
	 * number of sub cqs - must be equal to sub_cqs_per_cq of queue
	 * attributes.
	 */
	u16 num_sub_cqs;

	/* UAR number */
	u16 uar;
};

struct efa_admin_create_cq_resp {
	struct efa_admin_acq_common_desc acq_common_desc;

	u16 cq_idx;

	/* actual cq depth in number of entries */
	u16 cq_actual_depth;

	/* CQ doorbell address, as offset to PCIe DB BAR */
	u32 db_offset;

	/*
	 * 0 : db_valid - If set, doorbell offset is valid.
	 *    Always set when interrupts are requested.
	 */
	u32 flags;
};

struct efa_admin_destroy_cq_cmd {
	struct efa_admin_aq_common_desc aq_common_desc;

	u16 cq_idx;

	/* MBZ */
	u16 reserved1;
};

struct efa_admin_destroy_cq_resp {
	struct efa_admin_acq_common_desc acq_common_desc;
};

/*
 * EFA AQ Get Statistics command. Extended statistics are placed in control
 * buffer pointed by AQ entry
 */
struct efa_admin_aq_get_stats_cmd {
	struct efa_admin_aq_common_desc aq_common_descriptor;

	union {
		/* command specific inline data */
		u32 inline_data_w1[3];

		struct efa_admin_ctrl_buff_info control_buffer;
	} u;

	/* stats type as defined in enum efa_admin_get_stats_type */
	u8 type;

	/* stats scope defined in enum efa_admin_get_stats_scope */
	u8 scope;

	u16 scope_modifier;
};

struct efa_admin_basic_stats {
	u64 tx_bytes;

	u64 tx_pkts;

	u64 rx_bytes;

	u64 rx_pkts;

	u64 rx_drops;
};

struct efa_admin_messages_stats {
	u64 send_bytes;

	u64 send_wrs;

	u64 recv_bytes;

	u64 recv_wrs;
};

struct efa_admin_rdma_read_stats {
	u64 read_wrs;

	u64 read_bytes;

	u64 read_wr_err;

	u64 read_resp_bytes;
};

struct efa_admin_rdma_write_stats {
	u64 write_wrs;

	u64 write_bytes;

	u64 write_wr_err;

	u64 write_recv_bytes;
};

struct efa_admin_acq_get_stats_resp {
	struct efa_admin_acq_common_desc acq_common_desc;

	union {
		struct efa_admin_basic_stats basic_stats;

		struct efa_admin_messages_stats messages_stats;

		struct efa_admin_rdma_read_stats rdma_read_stats;

		struct efa_admin_rdma_write_stats rdma_write_stats;
	} u;
};

struct efa_admin_get_set_feature_common_desc {
	/* MBZ */
	u8 reserved0;

	/* as appears in efa_admin_aq_feature_id */
	u8 feature_id;

	/* MBZ */
	u16 reserved16;
};

struct efa_admin_feature_device_attr_desc {
	/* Bitmap of efa_admin_aq_feature_id */
	u64 supported_features;

	/* Bitmap of supported page sizes in MR registrations */
	u64 page_size_cap;

	u32 fw_version;

	u32 admin_api_version;

	u32 device_version;

	/* Bar used for SQ and RQ doorbells */
	u16 db_bar;

	/* Indicates how many bits are used on physical address access */
	u8 phys_addr_width;

	/* Indicates how many bits are used on virtual address access */
	u8 virt_addr_width;

	/*
	 * 0 : rdma_read - If set, RDMA Read is supported on
	 *    TX queues
	 * 1 : rnr_retry - If set, RNR retry is supported on
	 *    modify QP command
	 * 2 : data_polling_128 - If set, 128 bytes data
	 *    polling is supported
	 * 3 : rdma_write - If set, RDMA Write is supported
	 *    on TX queues
	 * 4 : unsolicited_write_recv - If set, unsolicited
	 *    write with imm. receive is supported
	 * 31:5 : reserved - MBZ
	 */
	u32 device_caps;

	/* Max RDMA transfer size in bytes */
	u32 max_rdma_size;
};

struct efa_admin_feature_queue_attr_desc {
	/* The maximum number of queue pairs supported */
	u32 max_qp;

	/* Maximum number of WQEs per Send Queue */
	u32 max_sq_depth;

	/* Maximum size of data that can be sent inline in a Send WQE */
	u32 inline_buf_size;

	/* Maximum number of buffer descriptors per Recv Queue */
	u32 max_rq_depth;

	/* The maximum number of completion queues supported per VF */
	u32 max_cq;

	/* Maximum number of CQEs per Completion Queue */
	u32 max_cq_depth;

	/* Number of sub-CQs to be created for each CQ */
	u16 sub_cqs_per_cq;

	/* Minimum number of WQEs per SQ */
	u16 min_sq_depth;

	/* Maximum number of SGEs (buffers) allowed for a single send WQE */
	u16 max_wr_send_sges;

	/* Maximum number of SGEs allowed for a single recv WQE */
	u16 max_wr_recv_sges;

	/* The maximum number of memory regions supported */
	u32 max_mr;

	/* The maximum number of pages can be registered */
	u32 max_mr_pages;

	/* The maximum number of protection domains supported */
	u32 max_pd;

	/* The maximum number of address handles supported */
	u32 max_ah;

	/* The maximum size of LLQ in bytes */
	u32 max_llq_size;

	/* Maximum number of SGEs for a single RDMA read/write WQE */
	u16 max_wr_rdma_sges;

	/*
	 * Maximum number of bytes that can be written to SQ between two
	 * consecutive doorbells (in units of 64B). Driver must ensure that only
	 * complete WQEs are written to queue before issuing a doorbell.
	 * Examples: max_tx_batch=16 and WQE size = 64B, means up to 16 WQEs can
	 * be written to SQ between two consecutive doorbells. max_tx_batch=11
	 * and WQE size = 128B, means up to 5 WQEs can be written to SQ between
	 * two consecutive doorbells. Zero means unlimited.
	 */
	u16 max_tx_batch;
};

struct efa_admin_event_queue_attr_desc {
	/* The maximum number of event queues supported */
	u32 max_eq;

	/* Maximum number of EQEs per Event Queue */
	u32 max_eq_depth;

	/* Supported events bitmask */
	u32 event_bitmask;
};

struct efa_admin_feature_aenq_desc {
	/* bitmask for AENQ groups the device can report */
	u32 supported_groups;

	/* bitmask for AENQ groups to report */
	u32 enabled_groups;
};

struct efa_admin_feature_network_attr_desc {
	/* Raw address data in network byte order */
	u8 addr[16];

	/* max packet payload size in bytes */
	u32 mtu;
};

/*
 * When hint value is 0, hints capabilities are not supported or driver
 * should use its own predefined value
 */
struct efa_admin_hw_hints {
	/* value in ms */
	u16 mmio_read_timeout;

	/* value in ms */
	u16 driver_watchdog_timeout;

	/* value in ms */
	u16 admin_completion_timeout;

	/* poll interval in ms */
	u16 poll_interval;
};

struct efa_admin_get_feature_cmd {
	struct efa_admin_aq_common_desc aq_common_descriptor;

	struct efa_admin_ctrl_buff_info control_buffer;

	struct efa_admin_get_set_feature_common_desc feature_common;

	u32 raw[11];
};

struct efa_admin_get_feature_resp {
	struct efa_admin_acq_common_desc acq_common_desc;

	union {
		u32 raw[14];

		struct efa_admin_feature_device_attr_desc device_attr;

		struct efa_admin_feature_aenq_desc aenq;

		struct efa_admin_feature_network_attr_desc network_attr;

		struct efa_admin_feature_queue_attr_desc queue_attr;

		struct efa_admin_event_queue_attr_desc event_queue_attr;

		struct efa_admin_hw_hints hw_hints;
	} u;
};

struct efa_admin_set_feature_cmd {
	struct efa_admin_aq_common_desc aq_common_descriptor;

	struct efa_admin_ctrl_buff_info control_buffer;

	struct efa_admin_get_set_feature_common_desc feature_common;

	union {
		u32 raw[11];

		/* AENQ configuration */
		struct efa_admin_feature_aenq_desc aenq;
	} u;
};

struct efa_admin_set_feature_resp {
	struct efa_admin_acq_common_desc acq_common_desc;

	union {
		u32 raw[14];
	} u;
};

struct efa_admin_alloc_pd_cmd {
	struct efa_admin_aq_common_desc aq_common_descriptor;
};

struct efa_admin_alloc_pd_resp {
	struct efa_admin_acq_common_desc acq_common_desc;

	/* PD number */
	u16 pd;

	/* MBZ */
	u16 reserved;
};

struct efa_admin_dealloc_pd_cmd {
	struct efa_admin_aq_common_desc aq_common_descriptor;

	/* PD number */
	u16 pd;

	/* MBZ */
	u16 reserved;
};

struct efa_admin_dealloc_pd_resp {
	struct efa_admin_acq_common_desc acq_common_desc;
};

struct efa_admin_alloc_uar_cmd {
	struct efa_admin_aq_common_desc aq_common_descriptor;
};

struct efa_admin_alloc_uar_resp {
	struct efa_admin_acq_common_desc acq_common_desc;

	/* UAR number */
	u16 uar;

	/* MBZ */
	u16 reserved;
};

struct efa_admin_dealloc_uar_cmd {
	struct efa_admin_aq_common_desc aq_common_descriptor;

	/* UAR number */
	u16 uar;

	/* MBZ */
	u16 reserved;
};

struct efa_admin_dealloc_uar_resp {
	struct efa_admin_acq_common_desc acq_common_desc;
};

struct efa_admin_create_eq_cmd {
	struct efa_admin_aq_common_desc aq_common_descriptor;

	/* Size of the EQ in entries, must be power of 2 */
	u16 depth;

	/* MSI-X table entry index */
	u8 msix_vec;

	/*
	 * 4:0 : entry_size_words - size of EQ entry in
	 *    32-bit words
	 * 7:5 : reserved - MBZ
	 */
	u8 caps;

	/* EQ ring base address */
	struct efa_common_mem_addr ba;

	/*
	 * Enabled events on this EQ
	 * 0 : completion_events - Enable completion events
	 * 31:1 : reserved - MBZ
	 */
	u32 event_bitmask;

	/* MBZ */
	u32 reserved;
};

struct efa_admin_create_eq_resp {
	struct efa_admin_acq_common_desc acq_common_desc;

	/* EQ number */
	u16 eqn;

	/* MBZ */
	u16 reserved;
};

struct efa_admin_destroy_eq_cmd {
	struct efa_admin_aq_common_desc aq_common_descriptor;

	/* EQ number */
	u16 eqn;

	/* MBZ */
	u16 reserved;
};

struct efa_admin_destroy_eq_resp {
	struct efa_admin_acq_common_desc acq_common_desc;
};

/* asynchronous event notification groups */
enum efa_admin_aenq_group {
	EFA_ADMIN_FATAL_ERROR                       = 1,
	EFA_ADMIN_WARNING                           = 2,
	EFA_ADMIN_NOTIFICATION                      = 3,
	EFA_ADMIN_KEEP_ALIVE                        = 4,
	EFA_ADMIN_AENQ_GROUPS_NUM                   = 5,
};

struct efa_admin_mmio_req_read_less_resp {
	u16 req_id;

	u16 reg_off;

	/* value is valid when poll is cleared */
	u32 reg_val;
};

enum efa_admin_os_type {
	EFA_ADMIN_OS_LINUX                          = 0,
};

struct efa_admin_host_info {
	/* OS distribution string format */
	u8 os_dist_str[128];

	/* Defined in enum efa_admin_os_type */
	u32 os_type;

	/* Kernel version string format */
	u8 kernel_ver_str[32];

	/* Kernel version numeric format */
	u32 kernel_ver;

	/*
	 * 7:0 : driver_module_type
	 * 15:8 : driver_sub_minor
	 * 23:16 : driver_minor
	 * 31:24 : driver_major
	 */
	u32 driver_ver;

	/*
	 * Device's Bus, Device and Function
	 * 2:0 : function
	 * 7:3 : device
	 * 15:8 : bus
	 */
	u16 bdf;

	/*
	 * Spec version
	 * 7:0 : spec_minor
	 * 15:8 : spec_major
	 */
	u16 spec_ver;

	/*
	 * 0 : intree - Intree driver
	 * 1 : gdr - GPUDirect RDMA supported
	 * 31:2 : reserved2
	 */
	u32 flags;
};

/* create_qp_cmd */
#define EFA_ADMIN_CREATE_QP_CMD_SQ_VIRT_MASK                BIT(0)
#define EFA_ADMIN_CREATE_QP_CMD_RQ_VIRT_MASK                BIT(1)
#define EFA_ADMIN_CREATE_QP_CMD_UNSOLICITED_WRITE_RECV_MASK BIT(2)

/* modify_qp_cmd */
#define EFA_ADMIN_MODIFY_QP_CMD_QP_STATE_MASK               BIT(0)
#define EFA_ADMIN_MODIFY_QP_CMD_CUR_QP_STATE_MASK           BIT(1)
#define EFA_ADMIN_MODIFY_QP_CMD_QKEY_MASK                   BIT(2)
#define EFA_ADMIN_MODIFY_QP_CMD_SQ_PSN_MASK                 BIT(3)
#define EFA_ADMIN_MODIFY_QP_CMD_SQ_DRAINED_ASYNC_NOTIFY_MASK BIT(4)
#define EFA_ADMIN_MODIFY_QP_CMD_RNR_RETRY_MASK              BIT(5)

/* reg_mr_cmd */
#define EFA_ADMIN_REG_MR_CMD_PHYS_PAGE_SIZE_SHIFT_MASK      GENMASK(4, 0)
#define EFA_ADMIN_REG_MR_CMD_MEM_ADDR_PHY_MODE_EN_MASK      BIT(7)
#define EFA_ADMIN_REG_MR_CMD_LOCAL_WRITE_ENABLE_MASK        BIT(0)
#define EFA_ADMIN_REG_MR_CMD_REMOTE_WRITE_ENABLE_MASK       BIT(1)
#define EFA_ADMIN_REG_MR_CMD_REMOTE_READ_ENABLE_MASK        BIT(2)

/* reg_mr_resp */
#define EFA_ADMIN_REG_MR_RESP_RECV_IC_ID_MASK               BIT(0)
#define EFA_ADMIN_REG_MR_RESP_RDMA_READ_IC_ID_MASK          BIT(1)
#define EFA_ADMIN_REG_MR_RESP_RDMA_RECV_IC_ID_MASK          BIT(2)

/* create_cq_cmd */
#define EFA_ADMIN_CREATE_CQ_CMD_INTERRUPT_MODE_ENABLED_MASK BIT(5)
#define EFA_ADMIN_CREATE_CQ_CMD_VIRT_MASK                   BIT(6)
#define EFA_ADMIN_CREATE_CQ_CMD_CQ_ENTRY_SIZE_WORDS_MASK    GENMASK(4, 0)
#define EFA_ADMIN_CREATE_CQ_CMD_SET_SRC_ADDR_MASK           BIT(5)

/* create_cq_resp */
#define EFA_ADMIN_CREATE_CQ_RESP_DB_VALID_MASK              BIT(0)

/* feature_device_attr_desc */
#define EFA_ADMIN_FEATURE_DEVICE_ATTR_DESC_RDMA_READ_MASK   BIT(0)
#define EFA_ADMIN_FEATURE_DEVICE_ATTR_DESC_RNR_RETRY_MASK   BIT(1)
#define EFA_ADMIN_FEATURE_DEVICE_ATTR_DESC_DATA_POLLING_128_MASK BIT(2)
#define EFA_ADMIN_FEATURE_DEVICE_ATTR_DESC_RDMA_WRITE_MASK  BIT(3)
#define EFA_ADMIN_FEATURE_DEVICE_ATTR_DESC_UNSOLICITED_WRITE_RECV_MASK BIT(4)

/* create_eq_cmd */
#define EFA_ADMIN_CREATE_EQ_CMD_ENTRY_SIZE_WORDS_MASK       GENMASK(4, 0)
#define EFA_ADMIN_CREATE_EQ_CMD_VIRT_MASK                   BIT(6)
#define EFA_ADMIN_CREATE_EQ_CMD_COMPLETION_EVENTS_MASK      BIT(0)

/* host_info */
#define EFA_ADMIN_HOST_INFO_DRIVER_MODULE_TYPE_MASK         GENMASK(7, 0)
#define EFA_ADMIN_HOST_INFO_DRIVER_SUB_MINOR_MASK           GENMASK(15, 8)
#define EFA_ADMIN_HOST_INFO_DRIVER_MINOR_MASK               GENMASK(23, 16)
#define EFA_ADMIN_HOST_INFO_DRIVER_MAJOR_MASK               GENMASK(31, 24)
#define EFA_ADMIN_HOST_INFO_FUNCTION_MASK                   GENMASK(2, 0)
#define EFA_ADMIN_HOST_INFO_DEVICE_MASK                     GENMASK(7, 3)
#define EFA_ADMIN_HOST_INFO_BUS_MASK                        GENMASK(15, 8)
#define EFA_ADMIN_HOST_INFO_SPEC_MINOR_MASK                 GENMASK(7, 0)
#define EFA_ADMIN_HOST_INFO_SPEC_MAJOR_MASK                 GENMASK(15, 8)
#define EFA_ADMIN_HOST_INFO_INTREE_MASK                     BIT(0)
#define EFA_ADMIN_HOST_INFO_GDR_MASK                        BIT(1)

#endif /* _EFA_ADMIN_CMDS_H_ */
