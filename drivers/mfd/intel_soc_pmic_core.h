/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel SoC PMIC MFD Driver
 *
 * Copyright (C) 2012-2014 Intel Corporation. All rights reserved.
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
	const struct regmap_config *regmap_config;
	const struct regmap_irq_chip *irq_chip;
};

extern struct intel_soc_pmic_config intel_soc_pmic_config_byt_crc;
extern struct intel_soc_pmic_config intel_soc_pmic_config_cht_crc;

#endif	/* __INTEL_SOC_PMIC_CORE_H__ */
