/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */


#ifndef __DRIVERS_CLK_QCOM_VDD_LEVEL_CPU_H
#define __DRIVERS_CLK_QCOM_VDD_LEVEL_CPU_H

#include <linux/regulator/consumer.h>
#include <dt-bindings/regulator/qcom,rpm-smd-regulator.h>
#include <dt-bindings/regulator/qcom,rpmh-regulator-levels.h>

enum vdd_levels {
	VDD_NONE,
	VDD_MIN,		/* MIN_SVS */
	VDD_LOWER,		/* LOW_SVS / SVS2 */
	VDD_LOW,		/* SVS */
	VDD_LOW_L1,		/* SVS_L1 */
	VDD_NOMINAL,		/* NOM */
	VDD_HIGH,		/* TURBO */
	VDD_NUM,
};

static int vdd_corner[] = {
	[VDD_NONE]    = 0,
	[VDD_MIN]     = RPMH_REGULATOR_LEVEL_MIN_SVS,
	[VDD_LOWER]   = RPMH_REGULATOR_LEVEL_LOW_SVS,
	[VDD_LOW]     = RPMH_REGULATOR_LEVEL_SVS,
	[VDD_LOW_L1]  = RPMH_REGULATOR_LEVEL_SVS_L1,
	[VDD_NOMINAL] = RPMH_REGULATOR_LEVEL_NOM,
	[VDD_HIGH]    = RPMH_REGULATOR_LEVEL_TURBO,
};

enum vdd_hf_pll_levels {
	VDD_HF_PLL_OFF,
	VDD_HF_PLL_SVS,
	VDD_HF_PLL_NOM,
	VDD_HF_PLL_TUR,
	VDD_HF_PLL_NUM,
};

static int vdd_hf_levels[] = {
	0,	 0,				/* VDD_HF_PLL_OFF */
	1800000, RPM_SMD_REGULATOR_LEVEL_SVS,	/* VDD_HF_PLL_SVS */
	1800000, RPM_SMD_REGULATOR_LEVEL_NOM,	/* VDD_HF_PLL_NOM */
	1800000, RPM_SMD_REGULATOR_LEVEL_TURBO,	/* VDD_HF_PLL_TUR */
};

#endif
