/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#ifndef _INTEL_THC_DEV_H_
#define _INTEL_THC_DEV_H_

#include <linux/cdev.h>

#define THC_REGMAP_COMMON_OFFSET  0x10
#define THC_REGMAP_MMIO_OFFSET    0x1000

/**
 * struct thc_device - THC private device struct
 * @thc_regmap: MMIO regmap structure for accessing THC registers
 * @mmio_addr: MMIO registers address
 */
struct thc_device {
	struct device *dev;
	struct regmap *thc_regmap;
	void __iomem *mmio_addr;
};

struct thc_device *thc_dev_init(struct device *device, void __iomem *mem_addr);

#endif /* _INTEL_THC_DEV_H_ */
