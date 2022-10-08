/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#ifndef __LINUX_RGA_HW_CONFIG_H_
#define __LINUX_RGA_HW_CONFIG_H_

#include "rga_drv.h"

enum rga_mmu {
	RGA_NONE_MMU	= 0,
	RGA_MMU		= 1,
	RGA_IOMMU	= 2,
};

struct rga_win_data {
	const char *name;
	const uint32_t *raster_formats;
	const uint32_t *fbc_formats;
	const uint32_t *tile_formats;
	uint32_t num_of_raster_formats;
	uint32_t num_of_fbc_formats;
	uint32_t num_of_tile_formats;

	const unsigned int supported_rotations;
	const unsigned int scale_up_mode;
	const unsigned int scale_down_mode;
	const unsigned int rd_mode;

};

struct rga_rect {
	int width;
	int height;
};

struct rga_rect_range {
	struct rga_rect min;
	struct rga_rect max;
};

struct rga_hw_data {
	uint32_t version;
	uint32_t feature;

	uint32_t csc_r2y_mode;
	uint32_t csc_y2r_mode;

	struct rga_rect_range input_range;
	struct rga_rect_range output_range;

	unsigned int max_upscale_factor;
	unsigned int max_downscale_factor;

	uint32_t byte_stride_align;
	uint32_t max_byte_stride;

	const struct rga_win_data *win;
	unsigned int win_size;

	enum rga_mmu mmu;
};

extern const struct rga_hw_data rga3_data;
extern const struct rga_hw_data rga2e_data;
extern const struct rga_hw_data rga2e_1106_data;
extern const struct rga_hw_data rga2e_iommu_data;

/* Returns false if in range, true otherwise */
static inline bool rga_hw_out_of_range(const struct rga_rect_range *range, int width, int height)
{
	return (width > range->max.width || height > range->max.height ||
		width < range->min.width || height < range->min.height);
}

#endif /* __LINUX_RGA_HW_CONFIG_H_ */
