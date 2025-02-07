/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_CORE_H__
#define __IRIS_CORE_H__

#include <linux/types.h>
#include <media/v4l2-device.h>

#include "iris_platform_common.h"

struct icc_info {
	const char		*name;
	u32			bw_min_kbps;
	u32			bw_max_kbps;
};

/**
 * struct iris_core - holds core parameters valid for all instances
 *
 * @dev: reference to device structure
 * @reg_base: IO memory base address
 * @irq: iris irq
 * @v4l2_dev: a holder for v4l2 device structure
 * @vdev_dec: iris video device structure for decoder
 * @icc_tbl: table of iris interconnects
 * @icc_count: count of iris interconnects
 * @pmdomain_tbl: table of iris power domains
 * @opp_pmdomain_tbl: table of opp power domains
 * @clock_tbl: table of iris clocks
 * @clk_count: count of iris clocks
 * @resets: table of iris reset clocks
 * @iris_platform_data: a structure for platform data
 */

struct iris_core {
	struct device				*dev;
	void __iomem				*reg_base;
	int					irq;
	struct v4l2_device			v4l2_dev;
	struct video_device			*vdev_dec;
	struct icc_bulk_data			*icc_tbl;
	u32					icc_count;
	struct dev_pm_domain_list		*pmdomain_tbl;
	struct dev_pm_domain_list		*opp_pmdomain_tbl;
	struct clk_bulk_data			*clock_tbl;
	u32					clk_count;
	struct reset_control_bulk_data		*resets;
	const struct iris_platform_data		*iris_platform_data;
};

#endif
