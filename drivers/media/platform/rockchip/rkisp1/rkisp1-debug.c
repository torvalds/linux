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
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>

#include "rkisp1-common.h"
#include "rkisp1-regs.h"

#define RKISP1_DEBUG_DATA_COUNT_BINS	32
#define RKISP1_DEBUG_DATA_COUNT_STEP	(4096 / RKISP1_DEBUG_DATA_COUNT_BINS)

static int rkisp1_debug_input_status_show(struct seq_file *m, void *p)
{
	struct rkisp1_device *rkisp1 = m->private;
	u16 data_count[RKISP1_DEBUG_DATA_COUNT_BINS] = { };
	unsigned int hsync_count = 0;
	unsigned int vsync_count = 0;
	unsigned int i;
	u32 data;
	u32 val;
	int ret;

	ret = pm_runtime_get_if_in_use(rkisp1->dev);
	if (ret <= 0)
		return ret ? : -ENODATA;

	/* Sample the ISP input port status 10000 times with a 1µs interval. */
	for (i = 0; i < 10000; ++i) {
		val = rkisp1_read(rkisp1, RKISP1_CIF_ISP_FLAGS_SHD);

		data = (val & RKISP1_CIF_ISP_FLAGS_SHD_S_DATA_MASK)
		     >> RKISP1_CIF_ISP_FLAGS_SHD_S_DATA_SHIFT;
		data_count[data / RKISP1_DEBUG_DATA_COUNT_STEP]++;

		if (val & RKISP1_CIF_ISP_FLAGS_SHD_S_HSYNC)
			hsync_count++;
		if (val & RKISP1_CIF_ISP_FLAGS_SHD_S_VSYNC)
			vsync_count++;

		udelay(1);
	}

	pm_runtime_put(rkisp1->dev);

	seq_printf(m, "vsync: %u, hsync: %u\n", vsync_count, hsync_count);
	seq_puts(m, "data:\n");
	for (i = 0; i < ARRAY_SIZE(data_count); ++i)
		seq_printf(m, "- [%04u:%04u]: %u\n",
			   i * RKISP1_DEBUG_DATA_COUNT_STEP,
			   (i + 1) * RKISP1_DEBUG_DATA_COUNT_STEP - 1,
			   data_count[i]);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(rkisp1_debug_input_status);

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
	debugfs_create_file("input_status", 0444, debug->debugfs_dir, rkisp1,
			    &rkisp1_debug_input_status_fops);
}

void rkisp1_debug_cleanup(struct rkisp1_device *rkisp1)
{
	debugfs_remove_recursive(rkisp1->debug.debugfs_dir);
}
