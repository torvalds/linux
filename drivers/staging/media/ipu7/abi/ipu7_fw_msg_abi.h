/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 - 2025 Intel Corporation
 */

#ifndef IPU7_FW_MSG_ABI_H
#define IPU7_FW_MSG_ABI_H

#include "ipu7_fw_common_abi.h"

#pragma pack(push, 1)
enum ipu7_msg_type {
	IPU_MSG_TYPE_RESERVED = IA_GOFO_MSG_TYPE_RESERVED,
	IPU_MSG_TYPE_INDIRECT = IA_GOFO_MSG_TYPE_INDIRECT,
	IPU_MSG_TYPE_DEV_LOG = IA_GOFO_MSG_TYPE_LOG,
	IPU_MSG_TYPE_GENERAL_ERR = IA_GOFO_MSG_TYPE_GENERAL_ERR,
	IPU_MSG_TYPE_DEV_OPEN = 4,
	IPU_MSG_TYPE_DEV_OPEN_ACK = 5,
	IPU_MSG_TYPE_GRAPH_OPEN = 6,
	IPU_MSG_TYPE_GRAPH_OPEN_ACK = 7,
	IPU_MSG_TYPE_TASK_REQ = 8,
	IPU_MSG_TYPE_TASK_DONE = 9,
	IPU_MSG_TYPE_GRAPH_CLOSE = 10,
	IPU_MSG_TYPE_GRAPH_CLOSE_ACK = 11,
	IPU_MSG_TYPE_DEV_CLOSE = 12,
	IPU_MSG_TYPE_DEV_CLOSE_ACK = 13,
	IPU_MSG_TYPE_TERM_EVENT = 14,
	IPU_MSG_TYPE_N,
};

#define IPU_MSG_MAX_NODE_TERMS		(64U)
#define IPU_MSG_MAX_FRAGS		(7U)

enum ipu7_msg_node_type {
	IPU_MSG_NODE_TYPE_PAD = 0,
	IPU_MSG_NODE_TYPE_BASE,
	IPU_MSG_NODE_TYPE_N
};

#define IPU_MSG_NODE_MAX_DEVICES	(128U)
#define DEB_NUM_UINT32	(IPU_MSG_NODE_MAX_DEVICES / (sizeof(u32) * 8U))

typedef u32 ipu7_msg_teb_t[2];
typedef u32 ipu7_msg_deb_t[DEB_NUM_UINT32];

#define IPU_MSG_NODE_MAX_ROUTE_ENABLES	(128U)
#define RBM_NUM_UINT32	(IPU_MSG_NODE_MAX_ROUTE_ENABLES / (sizeof(u32) * 8U))

typedef u32 ipu7_msg_rbm_t[RBM_NUM_UINT32];

enum ipu7_msg_node_profile_type {
	IPU_MSG_NODE_PROFILE_TYPE_PAD = 0,
	IPU_MSG_NODE_PROFILE_TYPE_BASE,
	IPU_MSG_NODE_PROFILE_TYPE_CB,
	IPU_MSG_NODE_PROFILE_TYPE_N
};

struct ipu7_msg_node_profile {
	struct ia_gofo_tlv_header tlv_header;
	ipu7_msg_teb_t teb;
};

struct ipu7_msg_cb_profile {
	struct ipu7_msg_node_profile profile_base;
	ipu7_msg_deb_t deb;
	ipu7_msg_rbm_t rbm;
	ipu7_msg_rbm_t reb;
};

#define IPU_MSG_NODE_MAX_PROFILES	(2U)
#define IPU_MSG_NODE_DEF_PROFILE_IDX	(0U)
#define IPU_MSG_NODE_RSRC_ID_EXT_IP	(0xffU)

#define IPU_MSG_NODE_DONT_CARE_TEB_HI	(0xffffffffU)
#define IPU_MSG_NODE_DONT_CARE_TEB_LO	(0xffffffffU)
#define IPU_MSG_NODE_RSRC_ID_IS		(0xfeU)

struct ipu7_msg_node {
	struct ia_gofo_tlv_header tlv_header;
	u8 node_rsrc_id;
	u8 node_ctx_id;
	u8 num_frags;
	u8 reserved[1];
	struct ia_gofo_tlv_list profiles_list;
	struct ia_gofo_tlv_list terms_list;
	struct ia_gofo_tlv_list node_options;
};

enum ipu7_msg_node_option_types {
	IPU_MSG_NODE_OPTION_TYPES_PADDING = 0,
	IPU_MSG_NODE_OPTION_TYPES_N
};

#pragma pack(pop)

#pragma pack(push, 1)

enum ipu7_msg_link_type {
	IPU_MSG_LINK_TYPE_PAD = 0,
	IPU_MSG_LINK_TYPE_GENERIC = 1,
	IPU_MSG_LINK_TYPE_N
};

enum ipu7_msg_link_option_types {
	IPU_MSG_LINK_OPTION_TYPES_PADDING = 0,
	IPU_MSG_LINK_OPTION_TYPES_CMPRS = 1,
	IPU_MSG_LINK_OPTION_TYPES_N
};

enum ipu7_msg_link_cmprs_option_bit_depth {
	IPU_MSG_LINK_CMPRS_OPTION_8BPP = 0,
	IPU_MSG_LINK_CMPRS_OPTION_10BPP = 1,
	IPU_MSG_LINK_CMPRS_OPTION_12BPP = 2,
};

#define IPU_MSG_LINK_CMPRS_SPACE_SAVING_DENOM		(128U)
#define IPU_MSG_LINK_CMPRS_LOSSY_CFG_PAYLOAD_SIZE	(5U)
#define IPU_MSG_LINK_CMPRS_SPACE_SAVING_NUM_MAX \
	(IPU_MSG_LINK_CMPRS_SPACE_SAVING_DENOM - 1U)

struct ipu7_msg_link_cmprs_plane_desc {
	u8 plane_enable;
	u8 cmprs_enable;
	u8 encoder_plane_id;
	u8 decoder_plane_id;
	u8 cmprs_is_lossy;
	u8 cmprs_is_footprint;
	u8 bit_depth;
	u8 space_saving_numerator;
	u32 pixels_offset;
	u32 ts_offset;
	u32 tile_row_to_tile_row_stride;
	u32 rows_of_tiles;
	u32 lossy_cfg[IPU_MSG_LINK_CMPRS_LOSSY_CFG_PAYLOAD_SIZE];
};

#define IPU_MSG_LINK_CMPRS_MAX_PLANES		(2U)
#define IPU_MSG_LINK_CMPRS_NO_ALIGN_INTERVAL	(0U)
#define IPU_MSG_LINK_CMPRS_MIN_ALIGN_INTERVAL	(16U)
#define IPU_MSG_LINK_CMPRS_MAX_ALIGN_INTERVAL	(1024U)
struct ipu7_msg_link_cmprs_option {
	struct ia_gofo_tlv_header header;
	u32 cmprs_buf_size;
	u16 align_interval;
	u8 reserved[2];
	struct ipu7_msg_link_cmprs_plane_desc plane_descs[2];
};

struct ipu7_msg_link_ep {
	u8 node_ctx_id;
	u8 term_id;
};

struct ipu7_msg_link_ep_pair {
	struct ipu7_msg_link_ep ep_src;
	struct ipu7_msg_link_ep ep_dst;
};

#define IPU_MSG_LINK_FOREIGN_KEY_NONE		(65535U)
#define IPU_MSG_LINK_FOREIGN_KEY_MAX		(64U)
#define IPU_MSG_LINK_PBK_ID_DONT_CARE		(255U)
#define IPU_MSG_LINK_PBK_SLOT_ID_DONT_CARE	(255U)
#define IPU_MSG_LINK_TERM_ID_DONT_CARE		(0xffU)

struct ipu7_msg_link {
	struct ia_gofo_tlv_header tlv_header;
	struct ipu7_msg_link_ep_pair endpoints;
	u16 foreign_key;
	u8 streaming_mode;
	u8 pbk_id;
	u8 pbk_slot_id;
	u8 delayed_link;
	u8 reserved[2];
	struct ia_gofo_tlv_list link_options;
};

#pragma pack(pop)

enum ipu7_msg_dev_state {
	IPU_MSG_DEV_STATE_CLOSED = 0,
	IPU_MSG_DEV_STATE_OPEN_WAIT = 1,
	IPU_MSG_DEV_STATE_OPEN = 2,
	IPU_MSG_DEV_STATE_CLOSE_WAIT = 3,
	IPU_MSG_DEV_STATE_N
};

enum ipu7_msg_graph_state {
	IPU_MSG_GRAPH_STATE_CLOSED = 0,
	IPU_MSG_GRAPH_STATE_OPEN_WAIT = 1,
	IPU_MSG_GRAPH_STATE_OPEN = 2,
	IPU_MSG_GRAPH_STATE_CLOSE_WAIT = 3,
	IPU_MSG_GRAPH_STATE_N
};

enum ipu7_msg_task_state {
	IPU_MSG_TASK_STATE_DONE = 0,
	IPU_MSG_TASK_STATE_WAIT_DONE = 1,
	IPU_MSG_TASK_STATE_N
};

enum ipu7_msg_err_groups {
	IPU_MSG_ERR_GROUP_RESERVED = IA_GOFO_MSG_ERR_GROUP_RESERVED,
	IPU_MSG_ERR_GROUP_GENERAL = IA_GOFO_MSG_ERR_GROUP_GENERAL,
	IPU_MSG_ERR_GROUP_DEVICE = 2,
	IPU_MSG_ERR_GROUP_GRAPH = 3,
	IPU_MSG_ERR_GROUP_TASK = 4,
	IPU_MSG_ERR_GROUP_N,
};

#pragma pack(push, 1)
struct ipu7_msg_task {
	struct ia_gofo_msg_header header;
	u8 graph_id;
	u8 profile_idx;
	u8 node_ctx_id;
	u8 frame_id;
	u8 frag_id;
	u8 req_done_msg;
	u8 req_done_irq;
	u8 reserved[1];
	ipu7_msg_teb_t payload_reuse_bm;
	ia_gofo_addr_t term_buffers[IPU_MSG_MAX_NODE_TERMS];
};

struct ipu7_msg_task_done {
	struct ia_gofo_msg_header_ack header;
	u8 graph_id;
	u8 frame_id;
	u8 node_ctx_id;
	u8 profile_idx;
	u8 frag_id;
	u8 reserved[3];
};

enum ipu7_msg_err_task {
	IPU_MSG_ERR_TASK_OK = IA_GOFO_MSG_ERR_OK,
	IPU_MSG_ERR_TASK_GRAPH_ID = 1,
	IPU_MSG_ERR_TASK_NODE_CTX_ID = 2,
	IPU_MSG_ERR_TASK_PROFILE_IDX = 3,
	IPU_MSG_ERR_TASK_CTX_MEMORY_TASK = 4,
	IPU_MSG_ERR_TASK_TERM_PAYLOAD_PTR = 5,
	IPU_MSG_ERR_TASK_FRAME_ID = 6,
	IPU_MSG_ERR_TASK_FRAG_ID = 7,
	IPU_MSG_ERR_TASK_EXEC_EXT = 8,
	IPU_MSG_ERR_TASK_EXEC_SBX = 9,
	IPU_MSG_ERR_TASK_EXEC_INT = 10,
	IPU_MSG_ERR_TASK_EXEC_UNKNOWN = 11,
	IPU_MSG_ERR_TASK_PRE_EXEC = 12,
	IPU_MSG_ERR_TASK_N
};

#pragma pack(pop)

#pragma pack(push, 1)
enum ipu7_msg_term_type {
	IPU_MSG_TERM_TYPE_PAD = 0,
	IPU_MSG_TERM_TYPE_BASE,
	IPU_MSG_TERM_TYPE_N,
};

#define IPU_MSG_TERM_EVENT_TYPE_NONE		0U
#define IPU_MSG_TERM_EVENT_TYPE_PROGRESS	1U
#define IPU_MSG_TERM_EVENT_TYPE_N	(IPU_MSG_TERM_EVENT_TYPE_PROGRESS + 1U)

struct ipu7_msg_term {
	struct ia_gofo_tlv_header tlv_header;
	u8 term_id;
	u8 event_req_bm;
	u8 reserved[2];
	u32 payload_size;
	struct ia_gofo_tlv_list term_options;
};

enum ipu7_msg_term_option_types {
	IPU_MSG_TERM_OPTION_TYPES_PADDING = 0,
	IPU_MSG_TERM_OPTION_TYPES_N
};

struct ipu7_msg_term_event {
	struct ia_gofo_msg_header header;
	u8 graph_id;
	u8 frame_id;
	u8 node_ctx_id;
	u8 profile_idx;
	u8 frag_id;
	u8 term_id;
	u8 event_type;
	u8 reserved[1];
	u64 event_ts;
};

#pragma pack(pop)

#pragma pack(push, 1)
#define IPU_MSG_DEVICE_SEND_MSG_ENABLED		1U
#define IPU_MSG_DEVICE_SEND_MSG_DISABLED	0U

#define IPU_MSG_DEVICE_OPEN_SEND_RESP		BIT(0)
#define IPU_MSG_DEVICE_OPEN_SEND_IRQ		BIT(1)

#define IPU_MSG_DEVICE_CLOSE_SEND_RESP		BIT(0)
#define IPU_MSG_DEVICE_CLOSE_SEND_IRQ		BIT(1)

struct ipu7_msg_dev_open {
	struct ia_gofo_msg_header header;
	u32 max_graphs;
	u8 dev_msg_map;
	u8 enable_power_gating;
	u8 reserved[2];
};

struct ipu7_msg_dev_open_ack {
	struct ia_gofo_msg_header_ack header;
};

struct ipu7_msg_dev_close {
	struct ia_gofo_msg_header header;
	u8 dev_msg_map;
	u8 reserved[7];
};

struct ipu7_msg_dev_close_ack {
	struct ia_gofo_msg_header_ack header;
};

enum ipu7_msg_err_device {
	IPU_MSG_ERR_DEVICE_OK = IA_GOFO_MSG_ERR_OK,
	IPU_MSG_ERR_DEVICE_MAX_GRAPHS = 1,
	IPU_MSG_ERR_DEVICE_MSG_MAP = 2,
	IPU_MSG_ERR_DEVICE_N
};

#pragma pack(pop)

#pragma pack(push, 1)
#define IPU_MSG_GRAPH_ID_UNKNOWN	(0xffU)
#define IPU_MSG_GRAPH_SEND_MSG_ENABLED	1U
#define IPU_MSG_GRAPH_SEND_MSG_DISABLED	0U

#define IPU_MSG_GRAPH_OPEN_SEND_RESP	BIT(0)
#define IPU_MSG_GRAPH_OPEN_SEND_IRQ	BIT(1)

#define IPU_MSG_GRAPH_CLOSE_SEND_RESP	BIT(0)
#define IPU_MSG_GRAPH_CLOSE_SEND_IRQ	BIT(1)

struct ipu7_msg_graph_open {
	struct ia_gofo_msg_header header;
	struct ia_gofo_tlv_list nodes;
	struct ia_gofo_tlv_list links;
	u8 graph_id;
	u8 graph_msg_map;
	u8 reserved[6];
};

enum ipu7_msg_graph_ack_option_types {
	IPU_MSG_GRAPH_ACK_OPTION_TYPES_PADDING = 0,
	IPU_MSG_GRAPH_ACK_TASK_Q_INFO,
	IPU_MSG_GRAPH_ACK_OPTION_TYPES_N
};

struct ipu7_msg_graph_open_ack_task_q_info {
	struct ia_gofo_tlv_header header;
	u8 node_ctx_id;
	u8 q_id;
	u8 reserved[2];
};

struct ipu7_msg_graph_open_ack {
	struct ia_gofo_msg_header_ack header;
	u8 graph_id;
	u8 reserved[7];
};

struct ipu7_msg_graph_close {
	struct ia_gofo_msg_header header;
	u8 graph_id;
	u8 graph_msg_map;
	u8 reserved[6];
};

struct ipu7_msg_graph_close_ack {
	struct ia_gofo_msg_header_ack header;
	u8 graph_id;
	u8 reserved[7];
};

enum ipu7_msg_err_graph {
	IPU_MSG_ERR_GRAPH_OK = IA_GOFO_MSG_ERR_OK,
	IPU_MSG_ERR_GRAPH_GRAPH_STATE = 1,
	IPU_MSG_ERR_GRAPH_MAX_GRAPHS = 2,
	IPU_MSG_ERR_GRAPH_GRAPH_ID = 3,
	IPU_MSG_ERR_GRAPH_NODE_CTX_ID = 4,
	IPU_MSG_ERR_GRAPH_NODE_RSRC_ID = 5,
	IPU_MSG_ERR_GRAPH_PROFILE_IDX = 6,
	IPU_MSG_ERR_GRAPH_TERM_ID = 7,
	IPU_MSG_ERR_GRAPH_TERM_PAYLOAD_SIZE = 8,
	IPU_MSG_ERR_GRAPH_LINK_NODE_CTX_ID = 9,
	IPU_MSG_ERR_GRAPH_LINK_TERM_ID = 10,
	IPU_MSG_ERR_GRAPH_PROFILE_TYPE = 11,
	IPU_MSG_ERR_GRAPH_NUM_FRAGS = 12,
	IPU_MSG_ERR_GRAPH_QUEUE_ID_USAGE = 13,
	IPU_MSG_ERR_GRAPH_QUEUE_OPEN = 14,
	IPU_MSG_ERR_GRAPH_QUEUE_CLOSE = 15,
	IPU_MSG_ERR_GRAPH_QUEUE_ID_TASK_REQ_MISMATCH = 16,
	IPU_MSG_ERR_GRAPH_CTX_MEMORY_FGRAPH = 17,
	IPU_MSG_ERR_GRAPH_CTX_MEMORY_NODE = 18,
	IPU_MSG_ERR_GRAPH_CTX_MEMORY_NODE_PROFILE = 19,
	IPU_MSG_ERR_GRAPH_CTX_MEMORY_TERM = 20,
	IPU_MSG_ERR_GRAPH_CTX_MEMORY_LINK = 21,
	IPU_MSG_ERR_GRAPH_CTX_MSG_MAP = 22,
	IPU_MSG_ERR_GRAPH_CTX_FOREIGN_KEY = 23,
	IPU_MSG_ERR_GRAPH_CTX_STREAMING_MODE = 24,
	IPU_MSG_ERR_GRAPH_CTX_PBK_RSRC = 25,
	IPU_MSG_ERR_GRAPH_UNSUPPORTED_EVENT_TYPE = 26,
	IPU_MSG_ERR_GRAPH_TOO_MANY_EVENTS = 27,
	IPU_MSG_ERR_GRAPH_CTX_MEMORY_CMPRS = 28,
	IPU_MSG_ERR_GRAPH_CTX_CMPRS_ALIGN_INTERVAL = 29,
	IPU_MSG_ERR_GRAPH_CTX_CMPRS_PLANE_ID = 30,
	IPU_MSG_ERR_GRAPH_CTX_CMPRS_UNSUPPORTED_MODE = 31,
	IPU_MSG_ERR_GRAPH_CTX_CMPRS_BIT_DEPTH = 32,
	IPU_MSG_ERR_GRAPH_CTX_CMPRS_STRIDE_ALIGNMENT = 33,
	IPU_MSG_ERR_GRAPH_CTX_CMPRS_SUB_BUFFER_ALIGNMENT = 34,
	IPU_MSG_ERR_GRAPH_CTX_CMPRS_LAYOUT_ORDER = 35,
	IPU_MSG_ERR_GRAPH_CTX_CMPRS_LAYOUT_OVERLAP = 36,
	IPU_MSG_ERR_GRAPH_CTX_CMPRS_BUFFER_TOO_SMALL = 37,
	IPU_MSG_ERR_GRAPH_CTX_DELAYED_LINK = 38,
	IPU_MSG_ERR_GRAPH_N
};

#pragma pack(pop)

#define FWPS_MSG_ABI_MAX_INPUT_QUEUES	(60U)
#define FWPS_MSG_ABI_MAX_OUTPUT_QUEUES	(2U)
#define FWPS_MSG_ABI_MAX_QUEUES \
	(FWPS_MSG_ABI_MAX_OUTPUT_QUEUES + FWPS_MSG_ABI_MAX_INPUT_QUEUES)

#define FWPS_MSG_ABI_OUT_ACK_QUEUE_ID	(IA_GOFO_MSG_ABI_OUT_ACK_QUEUE_ID)
#define FWPS_MSG_ABI_OUT_LOG_QUEUE_ID	(IA_GOFO_MSG_ABI_OUT_LOG_QUEUE_ID)
#if (FWPS_MSG_ABI_OUT_LOG_QUEUE_ID >= FWPS_MSG_ABI_MAX_OUTPUT_QUEUES)
#error "Maximum output queues configuration is too small to fit ACK and LOG \
queues"
#endif
#define FWPS_MSG_ABI_IN_DEV_QUEUE_ID	(IA_GOFO_MSG_ABI_IN_DEV_QUEUE_ID)
#define FWPS_MSG_ABI_IN_RESERVED_QUEUE_ID	(3U)
#define FWPS_MSG_ABI_IN_FIRST_TASK_QUEUE_ID \
	(FWPS_MSG_ABI_IN_RESERVED_QUEUE_ID + 1U)

#if (FWPS_MSG_ABI_IN_FIRST_TASK_QUEUE_ID >= FWPS_MSG_ABI_MAX_INPUT_QUEUES)
#error "Maximum queues configuration is too small to fit minimum number of \
useful queues"
#endif

#define FWPS_MSG_ABI_IN_LAST_TASK_QUEUE_ID	(FWPS_MSG_ABI_MAX_QUEUES - 1U)
#define FWPS_MSG_ABI_IN_MAX_TASK_QUEUES \
	(FWPS_MSG_ABI_IN_LAST_TASK_QUEUE_ID - \
	FWPS_MSG_ABI_IN_FIRST_TASK_QUEUE_ID + 1U)
#define FWPS_MSG_ABI_OUT_FIRST_QUEUE_ID		(FWPS_MSG_ABI_OUT_ACK_QUEUE_ID)
#define FWPS_MSG_ABI_OUT_LAST_QUEUE_ID	(FWPS_MSG_ABI_MAX_OUTPUT_QUEUES - 1U)
#define FWPS_MSG_ABI_IN_FIRST_QUEUE_ID		(FWPS_MSG_ABI_IN_DEV_QUEUE_ID)
#define FWPS_MSG_ABI_IN_LAST_QUEUE_ID	(FWPS_MSG_ABI_IN_LAST_TASK_QUEUE_ID)

#define FWPS_MSG_HOST2FW_MAX_SIZE	(2U * 1024U)
#define FWPS_MSG_FW2HOST_MAX_SIZE	(256U)

#endif
