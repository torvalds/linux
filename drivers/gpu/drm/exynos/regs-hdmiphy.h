/*
 *
 *  regs-hdmiphy.h
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 *
 * HDMI-PHY register header file for Samsung HDMI driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef SAMSUNG_REGS_HDMIPHY_H
#define SAMSUNG_REGS_HDMIPHY_H

#define HDMIPHY_REG_COUNT	32

/*
 * Register part
*/
#define HDMIPHY_MODE_SET_DONE	0x1f

/*
 * Bit definition part
 */

/* HDMIPHY_MODE_SET_DONE */
#define HDMIPHY_MODE_EN		(1 << 7)

/* hdmiphy pmu control bits */
#define PMU_HDMI_PHY_CONTROL_MASK	1
#define PMU_HDMI_PHY_ENABLE		1
#define PMU_HDMI_PHY_DISABLE		0

#endif /* SAMSUNG_REGS_HDMIPHY_H */
