/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Intel Corporation */

#ifndef IPU7_BOOT_H
#define IPU7_BOOT_H

#include <linux/bits.h>

#include "ipu7-fw-com.h"

#define IPU7_BOOT_MSG_VER_MAX_ENTRIES	3U

#define IPU7_BOOT_STATE_CRITICAL(s)	(((s) & 0xffff0000U) == 0xdead0000U)
#define IPU7_BOOT_STATE_READY(s)	((s) == 0x57a7e100U)
#define IPU7_BOOT_STATE_INACTIVE(s)	((s) == 0x57a7e300U)

#define IPU7_FWLOG_MAX_LOGGER_SOURCES		(64U)

#define IPU7_LOGGER_CFG_CHANNEL_ENABLE_SYSCOM	BIT(1)

/* Shared by the insys and psys subsystem configurations */
struct ipu7_fw_logger_config {
	u8 use_source_severity;
	u8 source_severity[IPU7_FWLOG_MAX_LOGGER_SOURCES];
	u8 use_channels_enable_bitmask;
	u8 channels_enable_bitmask;
	u8 padding[1];
	u32 hw_printf_buffer_base_addr;
	u32 hw_printf_buffer_size_bytes;
};

struct ipu7_wdt_abi {
	u32 wdt_timer1_us;
	u32 wdt_timer2_us;
};

struct ipu7_boot_abi_version {
	u8 patch;
	u8 subminor;
	u8 minor;
	u8 major;
};

struct ipu7_boot_abi_msg_versions {
	u8 num_versions;
	u8 reserved[3];
	struct ipu7_boot_abi_version versions[IPU7_BOOT_MSG_VER_MAX_ENTRIES];
};

struct ipu7_boot_abi_cfg {
	u32 length;
	struct ipu7_boot_abi_version config_version;
	struct ipu7_boot_abi_msg_versions client_version_support;
	u32 pkg_dir;
	u32 subsys_config;
	u32 uc_tile_frequency;
	u16 checksum;
	u8 uc_tile_frequency_units;
	u8 padding[1];
	u32 reserved[58];
	struct ipu7_fw_com_config fw_com_config;
} __packed;

int ipu6_ipu7_init_boot_config(struct ipu6_bus_device *adev,
			       struct ipu7_fw_com_context *fwctx,
			       struct ipu7_fw_com_queue_config *qconfigs,
			       int num_queues, u32 uc_freq,
			       dma_addr_t subsys_config, u8 major);
void ipu6_ipu7_release_boot_config(struct ipu6_bus_device *adev,
				   struct ipu7_fw_com_context *fwctx);
int ipu6_ipu7_boot_start_fw(const struct ipu6_bus_device *adev,
			    struct ipu7_fw_com_context *fwctx);
int ipu6_ipu7_boot_stop_fw(const struct ipu6_bus_device *adev);
u32 ipu6_ipu7_boot_get_state(const struct ipu6_bus_device *adev);

#endif
