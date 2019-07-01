/* SPDX-License-Identifier: (GPL-2.0 OR MIT)
 * Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2019 Google, Inc.
 */

#ifndef _GVE_ADMINQ_H
#define _GVE_ADMINQ_H

#include <linux/build_bug.h>

/* Admin queue opcodes */
enum gve_adminq_opcodes {
	GVE_ADMINQ_DESCRIBE_DEVICE		= 0x1,
	GVE_ADMINQ_CONFIGURE_DEVICE_RESOURCES	= 0x2,
	GVE_ADMINQ_DECONFIGURE_DEVICE_RESOURCES	= 0x9,
	GVE_ADMINQ_SET_DRIVER_PARAMETER		= 0xB,
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

struct device_option {
	__be32 option_id;
	__be32 option_length;
};

static_assert(sizeof(struct device_option) == 8);

struct gve_adminq_configure_device_resources {
	__be64 counter_array;
	__be64 irq_db_addr;
	__be32 num_counters;
	__be32 num_irq_dbs;
	__be32 irq_db_stride;
	__be32 ntfy_blk_msix_base_idx;
};

static_assert(sizeof(struct gve_adminq_configure_device_resources) == 32);

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

union gve_adminq_command {
	struct {
		__be32 opcode;
		__be32 status;
		union {
			struct gve_adminq_configure_device_resources
						configure_device_resources;
			struct gve_adminq_describe_device describe_device;
			struct gve_adminq_set_driver_parameter set_driver_param;
		};
	};
	u8 reserved[64];
};

static_assert(sizeof(union gve_adminq_command) == 64);

int gve_adminq_alloc(struct device *dev, struct gve_priv *priv);
void gve_adminq_free(struct device *dev, struct gve_priv *priv);
void gve_adminq_release(struct gve_priv *priv);
int gve_adminq_execute_cmd(struct gve_priv *priv,
			   union gve_adminq_command *cmd_orig);
int gve_adminq_describe_device(struct gve_priv *priv);
int gve_adminq_configure_device_resources(struct gve_priv *priv,
					  dma_addr_t counter_array_bus_addr,
					  u32 num_counters,
					  dma_addr_t db_array_bus_addr,
					  u32 num_ntfy_blks);
int gve_adminq_deconfigure_device_resources(struct gve_priv *priv);
int gve_adminq_set_mtu(struct gve_priv *priv, u64 mtu);
#endif /* _GVE_ADMINQ_H */
