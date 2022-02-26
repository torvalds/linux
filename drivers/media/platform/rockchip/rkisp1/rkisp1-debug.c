// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - Base driver
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *
 * Based on Rockchip ISP1 driver by Rockchip Electronics Co., Ltd.
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#include <linux/debugfs.h>
#include <linux/device.h>

#include "rkisp1-common.h"

void rkisp1_debug_init(struct rkisp1_device *rkisp1)
{
	struct rkisp1_debug *debug = &rkisp1->debug;

	debug->debugfs_dir = debugfs_create_dir(dev_name(rkisp1->dev), NULL);

	debugfs_create_ulong("data_loss", 0444, debug->debugfs_dir,
			     &debug->data_loss);
	debugfs_create_ulong("outform_size_err", 0444,  debug->debugfs_dir,
			     &debug->outform_size_error);
	debugfs_create_ulong("img_stabilization_size_error", 0444,
			     debug->debugfs_dir,
			     &debug->img_stabilization_size_error);
	debugfs_create_ulong("inform_size_error", 0444,  debug->debugfs_dir,
			     &debug->inform_size_error);
	debugfs_create_ulong("irq_delay", 0444,  debug->debugfs_dir,
			     &debug->irq_delay);
	debugfs_create_ulong("mipi_error", 0444, debug->debugfs_dir,
			     &debug->mipi_error);
	debugfs_create_ulong("stats_error", 0444, debug->debugfs_dir,
			     &debug->stats_error);
	debugfs_create_ulong("mp_stop_timeout", 0444, debug->debugfs_dir,
			     &debug->stop_timeout[RKISP1_MAINPATH]);
	debugfs_create_ulong("sp_stop_timeout", 0444, debug->debugfs_dir,
			     &debug->stop_timeout[RKISP1_SELFPATH]);
	debugfs_create_ulong("mp_frame_drop", 0444, debug->debugfs_dir,
			     &debug->frame_drop[RKISP1_MAINPATH]);
	debugfs_create_ulong("sp_frame_drop", 0444, debug->debugfs_dir,
			     &debug->frame_drop[RKISP1_SELFPATH]);
}

void rkisp1_debug_cleanup(struct rkisp1_device *rkisp1)
{
	debugfs_remove_recursive(rkisp1->debug.debugfs_dir);
}
