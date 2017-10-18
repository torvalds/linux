/*
 * OMAP3 OPP table definitions.
 *
 * Copyright (C) 2009-2010 Texas Instruments Incorporated - http://www.ti.com/
 *	Nishanth Menon
 *	Kevin Hilman
 * Copyright (C) 2010-2011 Nokia Corporation.
 *      Eduardo Valentin
 *      Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>

#include "soc.h"
#include "control.h"
#include "omap_opp_data.h"
#include "pm.h"

/* 34xx */

/* VDD1 */

#define OMAP3430_VDD_MPU_OPP1_UV		975000
#define OMAP3430_VDD_MPU_OPP2_UV		1075000
#define OMAP3430_VDD_MPU_OPP3_UV		1200000
#define OMAP3430_VDD_MPU_OPP4_UV		1270000
#define OMAP3430_VDD_MPU_OPP5_UV		1350000

struct omap_volt_data omap34xx_vddmpu_volt_data[] = {
	VOLT_DATA_DEFINE(OMAP3430_VDD_MPU_OPP1_UV, OMAP343X_CONTROL_FUSE_OPP1_VDD1, 0xf4, 0x0c),
	VOLT_DATA_DEFINE(OMAP3430_VDD_MPU_OPP2_UV, OMAP343X_CONTROL_FUSE_OPP2_VDD1, 0xf4, 0x0c),
	VOLT_DATA_DEFINE(OMAP3430_VDD_MPU_OPP3_UV, OMAP343X_CONTROL_FUSE_OPP3_VDD1, 0xf9, 0x18),
	VOLT_DATA_DEFINE(OMAP3430_VDD_MPU_OPP4_UV, OMAP343X_CONTROL_FUSE_OPP4_VDD1, 0xf9, 0x18),
	VOLT_DATA_DEFINE(OMAP3430_VDD_MPU_OPP5_UV, OMAP343X_CONTROL_FUSE_OPP5_VDD1, 0xf9, 0x18),
	VOLT_DATA_DEFINE(0, 0, 0, 0),
};

/* VDD2 */

#define OMAP3430_VDD_CORE_OPP1_UV		975000
#define OMAP3430_VDD_CORE_OPP2_UV		1050000
#define OMAP3430_VDD_CORE_OPP3_UV		1150000

struct omap_volt_data omap34xx_vddcore_volt_data[] = {
	VOLT_DATA_DEFINE(OMAP3430_VDD_CORE_OPP1_UV, OMAP343X_CONTROL_FUSE_OPP1_VDD2, 0xf4, 0x0c),
	VOLT_DATA_DEFINE(OMAP3430_VDD_CORE_OPP2_UV, OMAP343X_CONTROL_FUSE_OPP2_VDD2, 0xf4, 0x0c),
	VOLT_DATA_DEFINE(OMAP3430_VDD_CORE_OPP3_UV, OMAP343X_CONTROL_FUSE_OPP3_VDD2, 0xf9, 0x18),
	VOLT_DATA_DEFINE(0, 0, 0, 0),
};

/* 36xx */

/* VDD1 */

#define OMAP3630_VDD_MPU_OPP50_UV		1012500
#define OMAP3630_VDD_MPU_OPP100_UV		1200000
#define OMAP3630_VDD_MPU_OPP120_UV		1325000
#define OMAP3630_VDD_MPU_OPP1G_UV		1375000

struct omap_volt_data omap36xx_vddmpu_volt_data[] = {
	VOLT_DATA_DEFINE(OMAP3630_VDD_MPU_OPP50_UV, OMAP3630_CONTROL_FUSE_OPP50_VDD1, 0xf4, 0x0c),
	VOLT_DATA_DEFINE(OMAP3630_VDD_MPU_OPP100_UV, OMAP3630_CONTROL_FUSE_OPP100_VDD1, 0xf9, 0x16),
	VOLT_DATA_DEFINE(OMAP3630_VDD_MPU_OPP120_UV, OMAP3630_CONTROL_FUSE_OPP120_VDD1, 0xfa, 0x23),
	VOLT_DATA_DEFINE(OMAP3630_VDD_MPU_OPP1G_UV, OMAP3630_CONTROL_FUSE_OPP1G_VDD1, 0xfa, 0x27),
	VOLT_DATA_DEFINE(0, 0, 0, 0),
};

/* VDD2 */

#define OMAP3630_VDD_CORE_OPP50_UV		1000000
#define OMAP3630_VDD_CORE_OPP100_UV		1200000

struct omap_volt_data omap36xx_vddcore_volt_data[] = {
	VOLT_DATA_DEFINE(OMAP3630_VDD_CORE_OPP50_UV, OMAP3630_CONTROL_FUSE_OPP50_VDD2, 0xf4, 0x0c),
	VOLT_DATA_DEFINE(OMAP3630_VDD_CORE_OPP100_UV, OMAP3630_CONTROL_FUSE_OPP100_VDD2, 0xf9, 0x16),
	VOLT_DATA_DEFINE(0, 0, 0, 0),
};
