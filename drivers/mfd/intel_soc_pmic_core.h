/*
 * intel_soc_pmic_core.h - Intel SoC PMIC MFD Driver
 *
 * Copyright (C) 2012-2014 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Yang, Bin <bin.yang@intel.com>
 * Author: Zhu, Lejun <lejun.zhu@linux.intel.com>
 */

#ifndef __INTEL_SOC_PMIC_CORE_H__
#define __INTEL_SOC_PMIC_CORE_H__

struct intel_soc_pmic_config {
	unsigned long irq_flags;
	struct mfd_cell *cell_dev;
	int n_cell_devs;
	struct regmap_config *regmap_config;
	struct regmap_irq_chip *irq_chip;
};

extern struct intel_soc_pmic_config intel_soc_pmic_config_crc;

#endif	/* __INTEL_SOC_PMIC_CORE_H__ */
