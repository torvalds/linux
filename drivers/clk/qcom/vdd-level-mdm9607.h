/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __DRIVERS_CLK_QCOM_VDD_LEVEL_MDM9607_H
#define __DRIVERS_CLK_QCOM_VDD_LEVEL_MDM9607_H

#include <linux/regulator/consumer.h>
#include <dt-bindings/regulator/qcom,rpmh-regulator-levels.h>

enum vdd_levels {
	VDD_NONE,
	VDD_LOWER,		/* SVS2 */
	VDD_LOW,		/* SVS */
	VDD_NOMINAL,		/* NOM */
	VDD_HIGH,		/* TURBO */
	VDD_NUM,
};

static int vdd_corner[] = {
	[VDD_NONE]    = 0,
	[VDD_LOWER]   = RPMH_REGULATOR_LEVEL_SVS,
	[VDD_LOW]     = RPMH_REGULATOR_LEVEL_SVS_L1,
	[VDD_NOMINAL] = RPMH_REGULATOR_LEVEL_NOM,
	[VDD_HIGH]    = RPMH_REGULATOR_LEVEL_TURBO,
};

#endif
