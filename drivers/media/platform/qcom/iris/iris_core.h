/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_CORE_H__
#define __IRIS_CORE_H__

#include <linux/types.h>
#include <linux/pm_domain.h>
#include <media/v4l2-device.h>

#include "iris_hfi_common.h"
#include "iris_hfi_queue.h"
#include "iris_platform_common.h"
#include "iris_resources.h"
#include "iris_state.h"

struct icc_info {
	const char		*name;
	u32			bw_min_kbps;
	u32			bw_max_kbps;
};

#define IRIS_FW_VERSION_LENGTH		128
#define IFACEQ_CORE_PKT_SIZE		(1024 * 4)

/**
 * struct iris_core - holds core parameters valid for all instances
 *
 * @dev: reference to device structure
 * @reg_base: IO memory base address
 * @irq: iris irq
 * @v4l2_dev: a holder for v4l2 device structure
 * @vdev_dec: iris video device structure for decoder
 * @iris_v4l2_file_ops: iris v4l2 file ops
 * @iris_v4l2_ioctl_ops: iris v4l2 ioctl ops
 * @iris_vb2_ops: iris vb2 ops
 * @icc_tbl: table of iris interconnects
 * @icc_count: count of iris interconnects
 * @pmdomain_tbl: table of iris power domains
 * @opp_pmdomain_tbl: table of opp power domains
 * @clock_tbl: table of iris clocks
 * @clk_count: count of iris clocks
 * @resets: table of iris reset clocks
 * @iris_platform_data: a structure for platform data
 * @state: current state of core
 * @iface_q_table_daddr: device address for interface queue table memory
 * @sfr_daddr: device address for SFR (Sub System Failure Reason) register memory
 * @iface_q_table_vaddr: virtual address for interface queue table memory
 * @sfr_vaddr: virtual address for SFR (Sub System Failure Reason) register memory
 * @command_queue: shared interface queue to send commands to firmware
 * @message_queue: shared interface queue to receive responses from firmware
 * @debug_queue: shared interface queue to receive debug info from firmware
 * @lock: a lock for this strucure
 * @response_packet: a pointer to response packet from fw to driver
 * @header_id: id of packet header
 * @packet_id: id of packet
 * @power: a structure for clock and bw information
 * @hfi_ops: iris hfi command ops
 * @hfi_response_ops: iris hfi response ops
 * @core_init_done: structure of signal completion for system response
 * @intr_status: interrupt status
 * @sys_error_handler: a delayed work for handling system fatal error
 * @instances: a list_head of all instances
 * @inst_fw_caps: an array of supported instance capabilities
 */

struct iris_core {
	struct device				*dev;
	void __iomem				*reg_base;
	int					irq;
	struct v4l2_device			v4l2_dev;
	struct video_device			*vdev_dec;
	const struct v4l2_file_operations	*iris_v4l2_file_ops;
	const struct v4l2_ioctl_ops		*iris_v4l2_ioctl_ops;
	const struct vb2_ops			*iris_vb2_ops;
	struct icc_bulk_data			*icc_tbl;
	u32					icc_count;
	struct dev_pm_domain_list		*pmdomain_tbl;
	struct dev_pm_domain_list		*opp_pmdomain_tbl;
	struct clk_bulk_data			*clock_tbl;
	u32					clk_count;
	struct reset_control_bulk_data		*resets;
	const struct iris_platform_data		*iris_platform_data;
	enum iris_core_state			state;
	dma_addr_t				iface_q_table_daddr;
	dma_addr_t				sfr_daddr;
	void					*iface_q_table_vaddr;
	void					*sfr_vaddr;
	struct iris_iface_q_info		command_queue;
	struct iris_iface_q_info		message_queue;
	struct iris_iface_q_info		debug_queue;
	struct mutex				lock; /* lock for core related operations */
	u8					*response_packet;
	u32					header_id;
	u32					packet_id;
	struct iris_core_power			power;
	const struct iris_hfi_command_ops	*hfi_ops;
	const struct iris_hfi_response_ops	*hfi_response_ops;
	struct completion			core_init_done;
	u32					intr_status;
	struct delayed_work			sys_error_handler;
	struct list_head			instances;
	struct platform_inst_fw_cap		inst_fw_caps[INST_FW_CAP_MAX];
};

int iris_core_init(struct iris_core *core);
void iris_core_deinit(struct iris_core *core);

#endif
