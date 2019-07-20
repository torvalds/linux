/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved. */

#ifndef __DRIVERS_CLK_QCOM_VDD_LEVEL_H
#define __DRIVERS_CLK_QCOM_VDD_LEVEL_H

#include <linux/regulator/consumer.h>
#include <dt-bindings/regulator/qcom,rpmh-regulator-levels.h>

enum vdd_levels {
	VDD_NONE,
	VDD_MIN,		/* MIN_SVS */
	VDD_LOWER_D1,		/* LOW_SVS_D1 */
	VDD_LOWER,		/* LOW_SVS / SVS2 */
	VDD_LOW,		/* SVS */
	VDD_LOW_L0,		/* SVS_L0 */
	VDD_LOW_L1,		/* SVS_L1 */
	VDD_NOMINAL,		/* NOM */
	VDD_HIGH,		/* TURBO */
	VDD_NUM,
};

static int vdd_corner[] = {
	[VDD_NONE]	= 0,
	[VDD_MIN]	= RPMH_REGULATOR_LEVEL_MIN_SVS,
	[VDD_LOWER_D1]	= RPMH_REGULATOR_LEVEL_LOW_SVS_D1,
	[VDD_LOWER]	= RPMH_REGULATOR_LEVEL_LOW_SVS,
	[VDD_LOW]	= RPMH_REGULATOR_LEVEL_SVS,
	[VDD_LOW_L0]	= RPMH_REGULATOR_LEVEL_SVS_L0,
	[VDD_LOW_L1]	= RPMH_REGULATOR_LEVEL_SVS_L1,
	[VDD_NOMINAL]	= RPMH_REGULATOR_LEVEL_NOM,
	[VDD_HIGH]	= RPMH_REGULATOR_LEVEL_TURBO,
};

#endif
