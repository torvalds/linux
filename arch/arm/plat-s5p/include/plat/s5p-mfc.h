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

struct s5p_mfc_platdata {
	int clock_rate;
};

extern unsigned int mfc_clk_rate;
void s5p_mfc_set_platdata(struct s5p_mfc_platdata *pd);
void s5p_mfc_setname(struct platform_device *pdev,char *name);

int exynos4_mfc_setup_clock(struct device *dev,
		unsigned long clock_rate);

#endif /* _S5P_MFC_H */
