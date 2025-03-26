// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for FPGA Accelerated Function Unit (AFU) MMIO Region Management
 *
 * Copyright (C) 2017-2018 Intel Corporation, Inc.
 *
 * Authors:
 *   Wu Hao <hao.wu@intel.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 */
#include "dfl-afu.h"

/**
 * afu_mmio_region_init - init function for afu mmio region support
 * @fdata: afu feature dev data
 */
void afu_mmio_region_init(struct dfl_feature_dev_data *fdata)
{
	struct dfl_afu *afu = dfl_fpga_fdata_get_private(fdata);

	INIT_LIST_HEAD(&afu->regions);
}

#define for_each_region(region, afu)	\
	list_for_each_entry((region), &(afu)->regions, node)

static struct dfl_afu_mmio_region *get_region_by_index(struct dfl_afu *afu,
						       u32 region_index)
{
	struct dfl_afu_mmio_region *region;

	for_each_region(region, afu)
		if (region->index == region_index)
			return region;

	return NULL;
}

/**
 * afu_mmio_region_add - add a mmio region to given feature dev.
 *
 * @fdata: afu feature dev data
 * @region_index: region index.
 * @region_size: region size.
 * @phys: region's physical address of this region.
 * @flags: region flags (access permission).
 *
 * Return: 0 on success, negative error code otherwise.
 */
int afu_mmio_region_add(struct dfl_feature_dev_data *fdata,
			u32 region_index, u64 region_size, u64 phys, u32 flags)
{
	struct device *dev = &fdata->dev->dev;
	struct dfl_afu_mmio_region *region;
	struct dfl_afu *afu;
	int ret = 0;

	region = devm_kzalloc(dev, sizeof(*region), GFP_KERNEL);
	if (!region)
		return -ENOMEM;

	region->index = region_index;
	region->size = region_size;
	region->phys = phys;
	region->flags = flags;

	mutex_lock(&fdata->lock);

	afu = dfl_fpga_fdata_get_private(fdata);

	/* check if @index already exists */
	if (get_region_by_index(afu, region_index)) {
		mutex_unlock(&fdata->lock);
		ret = -EEXIST;
		goto exit;
	}

	region_size = PAGE_ALIGN(region_size);
	region->offset = afu->region_cur_offset;
	list_add(&region->node, &afu->regions);

	afu->region_cur_offset += region_size;
	afu->num_regions++;
	mutex_unlock(&fdata->lock);

	return 0;

exit:
	devm_kfree(dev, region);
	return ret;
}

/**
 * afu_mmio_region_destroy - destroy all mmio regions under given feature dev.
 * @fdata: afu feature dev data
 */
void afu_mmio_region_destroy(struct dfl_feature_dev_data *fdata)
{
	struct dfl_afu *afu = dfl_fpga_fdata_get_private(fdata);
	struct dfl_afu_mmio_region *tmp, *region;

	list_for_each_entry_safe(region, tmp, &afu->regions, node)
		devm_kfree(&fdata->dev->dev, region);
}

/**
 * afu_mmio_region_get_by_index - find an afu region by index.
 * @fdata: afu feature dev data
 * @region_index: region index.
 * @pregion: ptr to region for result.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int afu_mmio_region_get_by_index(struct dfl_feature_dev_data *fdata,
				 u32 region_index,
				 struct dfl_afu_mmio_region *pregion)
{
	struct dfl_afu_mmio_region *region;
	struct dfl_afu *afu;
	int ret = 0;

	mutex_lock(&fdata->lock);
	afu = dfl_fpga_fdata_get_private(fdata);
	region = get_region_by_index(afu, region_index);
	if (!region) {
		ret = -EINVAL;
		goto exit;
	}
	*pregion = *region;
exit:
	mutex_unlock(&fdata->lock);
	return ret;
}

/**
 * afu_mmio_region_get_by_offset - find an afu mmio region by offset and size
 *
 * @fdata: afu feature dev data
 * @offset: region offset from start of the device fd.
 * @size: region size.
 * @pregion: ptr to region for result.
 *
 * Find the region which fully contains the region described by input
 * parameters (offset and size) from the feature dev's region linked list.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int afu_mmio_region_get_by_offset(struct dfl_feature_dev_data *fdata,
				  u64 offset, u64 size,
				  struct dfl_afu_mmio_region *pregion)
{
	struct dfl_afu_mmio_region *region;
	struct dfl_afu *afu;
	int ret = 0;

	mutex_lock(&fdata->lock);
	afu = dfl_fpga_fdata_get_private(fdata);
	for_each_region(region, afu)
		if (region->offset <= offset &&
		    region->offset + region->size >= offset + size) {
			*pregion = *region;
			goto exit;
		}
	ret = -EINVAL;
exit:
	mutex_unlock(&fdata->lock);
	return ret;
}
