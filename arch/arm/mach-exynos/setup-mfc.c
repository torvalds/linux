/* linux/arch/arm/mach-exynos/setup-mfc.c
 *
 * Copyright (c) 2009-2011 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com
 *
 * Base Exynos4 MFC configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/clk.h>

#include <plat/fb.h>
#include <plat/gpio-cfg.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/s5p-mfc.h>
#include <plat/devs.h>

#include <mach/regs-clock.h>
#include <mach/map.h>

unsigned int mfc_clk_rate;
int exynos4_mfc_setup_clock(struct device *dev,
		unsigned long clock_rate)
{
	mfc_clk_rate = clock_rate;

	return 0;
}

static struct s5p_mfc_platdata default_mfc_pd __initdata = {
	.clock_rate = 200 * MHZ,
};

void __init s5p_mfc_set_platdata(struct s5p_mfc_platdata *pd)
{
	if (!pd)
		pd = &default_mfc_pd;

	s3c_set_platdata(pd, sizeof(struct s5p_mfc_platdata),
				&s5p_device_mfc);
}

void s5p_mfc_setname(struct platform_device *pdev, char *name)
{
	pdev->name = name;
}
