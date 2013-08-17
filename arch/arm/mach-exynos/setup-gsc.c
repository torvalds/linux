/* linux/arch/arm/mach-exynos/setup-gsc.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com
 *
 * Base Exynos5 G-Scaler clock configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <mach/regs-clock.h>
#include <mach/map.h>
#include <media/exynos_gscaler.h>

void __init exynos5_gsc_set_pdev_name(int id, char *name)
{
	switch (id) {
	case 0:
		exynos5_device_gsc0.name = name;
		break;
	case 1:
		exynos5_device_gsc1.name = name;
		break;
	case 2:
		exynos5_device_gsc2.name = name;
		break;
	case 3:
		exynos5_device_gsc3.name = name;
		break;
	}
}

void __init exynos5_gsc_set_ip_ver(enum gsc_ip_version ver)
{
	exynos_gsc0_default_data.ip_ver = ver;
	exynos_gsc1_default_data.ip_ver = ver;
	if (soc_is_exynos5250() || soc_is_exynos5410()) {
		exynos_gsc2_default_data.ip_ver = ver;
		exynos_gsc3_default_data.ip_ver = ver;
	}
}
