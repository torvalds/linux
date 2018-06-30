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
 * struct fpga_afu_dma_region - afu DMA region data structure
 *
 * @user_addr: region userspace virtual address.
 * @length: region length.
 * @iova: region IO virtual address.
 * @pages: ptr to pages of this region.
 * @node: rb tree node.
 * @in_use: flag to indicate if this region is in_use.
 */
struct dfl_afu_dma_region {
	u64 user_addr;
	u64 length;
	u64 iova;
	struct page **pages;
	struct rb_node node;
	bool in_use;
};

/**
 * struct dfl_afu - afu device data structure
 *
 * @region_cur_offset: current region offset from start to the device fd.
 * @num_regions: num of mmio regions.
 * @regions: the mmio region linked list of this afu feature device.
 * @dma_regions: root of dma regions rb tree.
 * @num_umsgs: num of umsgs.
 * @pdata: afu platform device's pdata.
 */
struct dfl_afu {
	u64 region_cur_offset;
	int num_regions;
	u8 num_umsgs;
	struct list_head regions;
	struct rb_root dma_regions;

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
void afu_dma_region_init(struct dfl_feature_platform_data *pdata);
void afu_dma_region_destroy(struct dfl_feature_platform_data *pdata);
int afu_dma_map_region(struct dfl_feature_platform_data *pdata,
		       u64 user_addr, u64 length, u64 *iova);
int afu_dma_unmap_region(struct dfl_feature_platform_data *pdata, u64 iova);
struct dfl_afu_dma_region *
afu_dma_region_find(struct dfl_feature_platform_data *pdata,
		    u64 iova, u64 size);
#endif /* __DFL_AFU_H */
