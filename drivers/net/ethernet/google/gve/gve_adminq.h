/* SPDX-License-Identifier: (GPL-2.0 OR MIT)
 * Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

#ifndef _GVE_ADMINQ_H
#define _GVE_ADMINQ_H

#include <linux/build_bug.h>

/* Admin queue opcodes */
enum gve_adminq_opcodes {
	GVE_ADMINQ_DESCRIBE_DEVICE		= 0x1,
	GVE_ADMINQ_CONFIGURE_DEVICE_RESOURCES	= 0x2,
	GVE_ADMINQ_REGISTER_PAGE_LIST		= 0x3,
	GVE_ADMINQ_UNREGISTER_PAGE_LIST		= 0x4,
	GVE_ADMINQ_CREATE_TX_QUEUE		= 0x5,
	GVE_ADMINQ_CREATE_RX_QUEUE		= 0x6,
	GVE_ADMINQ_DESTROY_TX_QUEUE		= 0x7,
	GVE_ADMINQ_DESTROY_RX_QUEUE		= 0x8,
	GVE_ADMINQ_DECONFIGURE_DEVICE_RESOURCES	= 0x9,
	GVE_ADMINQ_SET_DRIVER_PARAMETER		= 0xB,
	GVE_ADMINQ_REPORT_STATS			= 0xC,
	GVE_ADMINQ_REPORT_LINK_SPEED		= 0xD,
	GVE_ADMINQ_GET_PTYPE_MAP		= 0xE,
};

/* Admin queue status codes */
enum gve_adminq_statuses {
	GVE_ADMINQ_COMMAND_UNSET			= 0x0,
	GVE_ADMINQ_COMMAND_PASSED			= 0x1,
	GVE_ADMINQ_COMMAND_ERROR_ABORTED		= 0xFFFFFFF0,
	GVE_ADMINQ_COMMAND_ERROR_ALREADY_EXISTS		= 0xFFFFFFF1,
	GVE_ADMINQ_COMMAND_ERROR_CANCELLED		= 0xFFFFFFF2,
	GVE_ADMINQ_COMMAND_ERROR_DATALOSS		= 0xFFFFFFF3,
	GVE_ADMINQ_COMMAND_ERROR_DEADLINE_EXCEEDED	= 0xFFFFFFF4,
	GVE_ADMINQ_COMMAND_ERROR_FAILED_PRECONDITION	= 0xFFFFFFF5,
	GVE_ADMINQ_COMMAND_ERROR_INTERNAL_ERROR		= 0xFFFFFFF6,
	GVE_ADMINQ_COMMAND_ERROR_INVALID_ARGUMENT	= 0xFFFFFFF7,
	GVE_ADMINQ_COMMAND_ERROR_NOT_FOUND		= 0xFFFFFFF8,
	GVE_ADMINQ_COMMAND_ERROR_OUT_OF_RANGE		= 0xFFFFFFF9,
	GVE_ADMINQ_COMMAND_ERROR_PERMISSION_DENIED	= 0xFFFFFFFA,
	GVE_ADMINQ_COMMAND_ERROR_UNAUTHENTICATED	= 0xFFFFFFFB,
	GVE_ADMINQ_COMMAND_ERROR_RESOURCE_EXHAUSTED	= 0xFFFFFFFC,
	GVE_ADMINQ_COMMAND_ERROR_UNAVAILABLE		= 0xFFFFFFFD,
	GVE_ADMINQ_COMMAND_ERROR_UNIMPLEMENTED		= 0xFFFFFFFE,
	GVE_ADMINQ_COMMAND_ERROR_UNKNOWN_ERROR		= 0xFFFFFFFF,
};

#define GVE_ADMINQ_DEVICE_DESCRIPTOR_VERSION 1

/* All AdminQ command structs should be naturally packed. The static_assert
 * calls make sure this is the case at compile time.
 */

struct gve_adminq_describe_device {
	__be64 device_descriptor_addr;
	__be32 device_descriptor_version;
	__be32 available_length;
};

static_assert(sizeof(struct gve_adminq_describe_device) == 16);

struct gve_device_descriptor {
	__be64 max_registered_pages;
	__be16 reserved1;
	__be16 tx_queue_entries;
	__be16 rx_queue_entries;
	__be16 default_num_queues;
	__be16 mtu;
	__be16 counters;
	__be16 tx_pages_per_qpl;
	__be16 rx_pages_per_qpl;
	u8  mac[ETH_ALEN];
	__be16 num_device_options;
	__be16 total_length;
	u8  reserved2[6];
};

static_assert(sizeof(struct gve_device_descriptor) == 40);

struct gve_device_option {
	__be16 option_id;
	__be16 option_length;
	__be32 required_features_mask;
};

static_assert(sizeof(struct gve_device_option) == 8);

struct gve_device_option_gqi_rda {
	__be32 supported_features_mask;
};

static_assert(sizeof(struct gve_device_option_gqi_rda) == 4);

struct gve_device_option_gqi_qpl {
	__be32 supported_features_mask;
};

static_assert(sizeof(struct gve_device_option_gqi_qpl) == 4);

struct gve_device_option_dqo_rda {
	__be32 supported_features_mask;
	__be16 tx_comp_ring_entries;
	__be16 rx_buff_ring_entries;
};

static_assert(sizeof(struct gve_device_option_dqo_rda) == 8);

struct gve_device_option_jumbo_frames {
	__be32 supported_features_mask;
	__be16 max_mtu;
	u8 padding[2];
};

static_assert(sizeof(struct gve_device_option_jumbo_frames) == 8);

/* Terminology:
 *
 * RDA - Raw DMA Addressing - Buffers associated with SKBs are directly DMA
 *       mapped and read/updated by the device.
 *
 * QPL - Queue Page Lists - Driver uses bounce buffers which are DMA mapped with
 *       the device for read/write and data is copied from/to SKBs.
 */
enum gve_dev_opt_id {
	GVE_DEV_OPT_ID_GQI_RAW_ADDRESSING = 0x1,
	GVE_DEV_OPT_ID_GQI_RDA = 0x2,
	GVE_DEV_OPT_ID_GQI_QPL = 0x3,
	GVE_DEV_OPT_ID_DQO_RDA = 0x4,
	GVE_DEV_OPT_ID_JUMBO_FRAMES = 0x8,
};

enum gve_dev_opt_req_feat_mask {
	GVE_DEV_OPT_REQ_FEAT_MASK_GQI_RAW_ADDRESSING = 0x0,
	GVE_DEV_OPT_REQ_FEAT_MASK_GQI_RDA = 0x0,
	GVE_DEV_OPT_REQ_FEAT_MASK_GQI_QPL = 0x0,
	GVE_DEV_OPT_REQ_FEAT_MASK_DQO_RDA = 0x0,
	GVE_DEV_OPT_REQ_FEAT_MASK_JUMBO_FRAMES = 0x0,
};

enum gve_sup_feature_mask {
	GVE_SUP_JUMBO_FRAMES_MASK = 1 << 2,
};

#define GVE_DEV_OPT_LEN_GQI_RAW_ADDRESSING 0x0

struct gve_adminq_configure_device_resources {
	__be64 counter_array;
	__be64 irq_db_addr;
	__be32 num_counters;
	__be32 num_irq_dbs;
	__be32 irq_db_stride;
	__be32 ntfy_blk_msix_base_idx;
	u8 queue_format;
	u8 padding[7];
};

static_assert(sizeof(struct gve_adminq_configure_device_resources) == 40);

struct gve_adminq_register_page_list {
	__be32 page_list_id;
	__be32 num_pages;
	__be64 page_address_list_addr;
};

static_assert(sizeof(struct gve_adminq_register_page_list) == 16);

struct gve_adminq_unregister_page_list {
	__be32 page_list_id;
};

static_assert(sizeof(struct gve_adminq_unregister_page_list) == 4);

#define GVE_RAW_ADDRESSING_QPL_ID 0xFFFFFFFF

struct gve_adminq_create_tx_queue {
	__be32 queue_id;
	__be32 reserved;
	__be64 queue_resources_addr;
	__be64 tx_ring_addr;
	__be32 queue_page_list_id;
	__be32 ntfy_id;
	__be64 tx_comp_ring_addr;
	__be16 tx_ring_size;
	__be16 tx_comp_ring_size;
	u8 padding[4];
};

static_assert(sizeof(struct gve_adminq_create_tx_queue) == 48);

struct gve_adminq_create_rx_queue {
	__be32 queue_id;
	__be32 index;
	__be32 reserved;
	__be32 ntfy_id;
	__be64 queue_resources_addr;
	__be64 rx_desc_ring_addr;
	__be64 rx_data_ring_addr;
	__be32 queue_page_list_id;
	__be16 rx_ring_size;
	__be16 packet_buffer_size;
	__be16 rx_buff_ring_size;
	u8 enable_rsc;
	u8 padding[5];
};

static_assert(sizeof(struct gve_adminq_create_rx_queue) == 56);

/* Queue resources that are shared with the device */
struct gve_queue_resources {
	union {
		struct {
			__be32 db_index;	/* Device -> Guest */
			__be32 counter_index;	/* Device -> Guest */
		};
		u8 reserved[64];
	};
};

static_assert(sizeof(struct gve_queue_resources) == 64);

struct gve_adminq_destroy_tx_queue {
	__be32 queue_id;
};

static_assert(sizeof(struct gve_adminq_destroy_tx_queue) == 4);

struct gve_adminq_destroy_rx_queue {
	__be32 queue_id;
};

static_assert(sizeof(struct gve_adminq_destroy_rx_queue) == 4);

/* GVE Set Driver Parameter Types */
enum gve_set_driver_param_types {
	GVE_SET_PARAM_MTU	= 0x1,
};

struct gve_adminq_set_driver_parameter {
	__be32 parameter_type;
	u8 reserved[4];
	__be64 parameter_value;
};

static_assert(sizeof(struct gve_adminq_set_driver_parameter) == 16);

struct gve_adminq_report_stats {
	__be64 stats_report_len;
	__be64 stats_report_addr;
	__be64 interval;
};

static_assert(sizeof(struct gve_adminq_report_stats) == 24);

struct gve_adminq_report_link_speed {
	__be64 link_speed_address;
};

static_assert(sizeof(struct gve_adminq_report_link_speed) == 8);

struct stats {
	__be32 stat_name;
	__be32 queue_id;
	__be64 value;
};

static_assert(sizeof(struct stats) == 16);

struct gve_stats_report {
	__be64 written_count;
	struct stats stats[];
};

static_assert(sizeof(struct gve_stats_report) == 8);

enum gve_stat_names {
	// stats from gve
	TX_WAKE_CNT			= 1,
	TX_STOP_CNT			= 2,
	TX_FRAMES_SENT			= 3,
	TX_BYTES_SENT			= 4,
	TX_LAST_COMPLETION_PROCESSED	= 5,
	RX_NEXT_EXPECTED_SEQUENCE	= 6,
	RX_BUFFERS_POSTED		= 7,
	TX_TIMEOUT_CNT			= 8,
	// stats from NIC
	RX_QUEUE_DROP_CNT		= 65,
	RX_NO_BUFFERS_POSTED		= 66,
	RX_DROPS_PACKET_OVER_MRU	= 67,
	RX_DROPS_INVALID_CHECKSUM	= 68,
};

enum gve_l3_type {
	/* Must be zero so zero initialized LUT is unknown. */
	GVE_L3_TYPE_UNKNOWN = 0,
	GVE_L3_TYPE_OTHER,
	GVE_L3_TYPE_IPV4,
	GVE_L3_TYPE_IPV6,
};

enum gve_l4_type {
	/* Must be zero so zero initialized LUT is unknown. */
	GVE_L4_TYPE_UNKNOWN = 0,
	GVE_L4_TYPE_OTHER,
	GVE_L4_TYPE_TCP,
	GVE_L4_TYPE_UDP,
	GVE_L4_TYPE_ICMP,
	GVE_L4_TYPE_SCTP,
};

/* These are control path types for PTYPE which are the same as the data path
 * types.
 */
struct gve_ptype_entry {
	u8 l3_type;
	u8 l4_type;
};

struct gve_ptype_map {
	struct gve_ptype_entry ptypes[1 << 10]; /* PTYPES are always 10 bits. */
};

struct gve_adminq_get_ptype_map {
	__be64 ptype_map_len;
	__be64 ptype_map_addr;
};

union gve_adminq_command {
	struct {
		__be32 opcode;
		__be32 status;
		union {
			struct gve_adminq_configure_device_resources
						configure_device_resources;
			struct gve_adminq_create_tx_queue create_tx_queue;
			struct gve_adminq_create_rx_queue create_rx_queue;
			struct gve_adminq_destroy_tx_queue destroy_tx_queue;
			struct gve_adminq_destroy_rx_queue destroy_rx_queue;
			struct gve_adminq_describe_device describe_device;
			struct gve_adminq_register_page_list reg_page_list;
			struct gve_adminq_unregister_page_list unreg_page_list;
			struct gve_adminq_set_driver_parameter set_driver_param;
			struct gve_adminq_report_stats report_stats;
			struct gve_adminq_report_link_speed report_link_speed;
			struct gve_adminq_get_ptype_map get_ptype_map;
		};
	};
	u8 reserved[64];
};

static_assert(sizeof(union gve_adminq_command) == 64);

int gve_adminq_alloc(struct device *dev, struct gve_priv *priv);
void gve_adminq_free(struct device *dev, struct gve_priv *priv);
void gve_adminq_release(struct gve_priv *priv);
int gve_adminq_describe_device(struct gve_priv *priv);
int gve_adminq_configure_device_resources(struct gve_priv *priv,
					  dma_addr_t counter_array_bus_addr,
					  u32 num_counters,
					  dma_addr_t db_array_bus_addr,
					  u32 num_ntfy_blks);
int gve_adminq_deconfigure_device_resources(struct gve_priv *priv);
int gve_adminq_create_tx_queues(struct gve_priv *priv, u32 num_queues);
int gve_adminq_destroy_tx_queues(struct gve_priv *priv, u32 queue_id);
int gve_adminq_create_rx_queues(struct gve_priv *priv, u32 num_queues);
int gve_adminq_destroy_rx_queues(struct gve_priv *priv, u32 queue_id);
int gve_adminq_register_page_list(struct gve_priv *priv,
				  struct gve_queue_page_list *qpl);
int gve_adminq_unregister_page_list(struct gve_priv *priv, u32 page_list_id);
int gve_adminq_set_mtu(struct gve_priv *priv, u64 mtu);
int gve_adminq_report_stats(struct gve_priv *priv, u64 stats_report_len,
			    dma_addr_t stats_report_addr, u64 interval);
int gve_adminq_report_link_speed(struct gve_priv *priv);

struct gve_ptype_lut;
int gve_adminq_get_ptype_map_dqo(struct gve_priv *priv,
				 struct gve_ptype_lut *ptype_lut);

#endif /* _GVE_ADMINQ_H */
