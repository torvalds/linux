/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for FPGA Accelerated Function Unit (AFU) Driver
 *
 * Copyright (C) 2017-2018 Intel Corporation, Inc.
 *
 * Authors:
 *     Wu Hao <hao.wu@intel.com>
 *     Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *     Joseph Grecco <joe.grecco@intel.com>
 *     Enno Luebbers <enno.luebbers@intel.com>
 *     Tim Whisonant <tim.whisonant@intel.com>
 *     Ananda Ravuri <ananda.ravuri@intel.com>
 *     Henry Mitchel <henry.mitchel@intel.com>
 */

#ifndef __DFL_AFU_H
#define __DFL_AFU_H

#include <linux/mm.h>

#include "dfl.h"

/**
 * struct dfl_afu_mmio_region - afu mmio region data structure
 *
 * @index: region index.
 * @flags: region flags (access permission).
 * @size: region size.
 * @offset: region offset from start of the device fd.
 * @phys: region's physical address.
 * @node: node to add to afu feature dev's region list.
 */
struct dfl_afu_mmio_region {
	u32 index;
	u32 flags;
	u64 size;
	u64 offset;
	u64 phys;
	struct list_head node;
};

/**
 * struct dfl_afu - afu device data structure
 *
 * @region_cur_offset: current region offset from start to the device fd.
 * @num_regions: num of mmio regions.
 * @regions: the mmio region linked list of this afu feature device.
 * @num_umsgs: num of umsgs.
 * @pdata: afu platform device's pdata.
 */
struct dfl_afu {
	u64 region_cur_offset;
	int num_regions;
	u8 num_umsgs;
	struct list_head regions;

	struct dfl_feature_platform_data *pdata;
};

void afu_mmio_region_init(struct dfl_feature_platform_data *pdata);
int afu_mmio_region_add(struct dfl_feature_platform_data *pdata,
			u32 region_index, u64 region_size, u64 phys, u32 flags);
void afu_mmio_region_destroy(struct dfl_feature_platform_data *pdata);
int afu_mmio_region_get_by_index(struct dfl_feature_platform_data *pdata,
				 u32 region_index,
				 struct dfl_afu_mmio_region *pregion);
int afu_mmio_region_get_by_offset(struct dfl_feature_platform_data *pdata,
				  u64 offset, u64 size,
				  struct dfl_afu_mmio_region *pregion);
#endif
