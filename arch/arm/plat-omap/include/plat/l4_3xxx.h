/*
 * arch/arm/plat-omap/include/mach/l4_3xxx.h - L4 firewall definitions
 *
 * Copyright (C) 2009 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#ifndef __ARCH_ARM_PLAT_OMAP_INCLUDE_MACH_L4_3XXX_H
#define __ARCH_ARM_PLAT_OMAP_INCLUDE_MACH_L4_3XXX_H

/* L4 CORE */
#define OMAP3_L4_CORE_FW_I2C1_REGION				21
#define OMAP3_L4_CORE_FW_I2C1_TA_REGION				22
#define OMAP3_L4_CORE_FW_I2C2_REGION				23
#define OMAP3_L4_CORE_FW_I2C2_TA_REGION				24
#define OMAP3_L4_CORE_FW_I2C3_REGION				73
#define OMAP3_L4_CORE_FW_I2C3_TA_REGION				74

/* Display Sub system (DSS) */
#define OMAP3_L4_CORE_FW_DSS_PROT_GROUP				2

#define OMAP3_L4_CORE_FW_DSS_DSI_REGION				104
#define OMAP3ES1_L4_CORE_FW_DSS_CORE_REGION			3
#define OMAP3_L4_CORE_FW_DSS_CORE_REGION			4
#define OMAP3_L4_CORE_FW_DSS_DISPC_REGION			4
#define OMAP3_L4_CORE_FW_DSS_RFBI_REGION			5
#define OMAP3_L4_CORE_FW_DSS_VENC_REGION			6
#define OMAP3_L4_CORE_FW_DSS_TA_REGION				7
#endif
