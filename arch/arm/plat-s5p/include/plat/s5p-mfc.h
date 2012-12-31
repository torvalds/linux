/* linux/arch/arm/plat-s5p/include/plat/s5p-mfc.h
 *
 * Copyright 2011 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com/
 *
 * Header file for s5p mfc support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _S5P_MFC_H
#define _S5P_MFC_H

#include <linux/platform_device.h>

#define MFC_PARENT_CLK_NAME	"mout_mfc0"
#define MFC_CLKNAME		"sclk_mfc"
#define MFC_GATE_CLK_NAME	"mfc"

extern unsigned int mfc_clk_rate;
void s5p_mfc_setname(struct platform_device *pdev,char *name);

int exynos4_mfc_setup_clock(struct device *dev,
		unsigned long clock_rate);

#endif /* _S5P_MFC_H */
