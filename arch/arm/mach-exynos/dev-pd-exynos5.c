/* linux/arch/arm/mach-exynos/dev-pd-exynos5.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS5 - Power Domain support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#include <mach/regs-pmu5.h>
#include <mach/regs-clock.h>

#include <plat/pd.h>

struct platform_device exynos5_device_pd[] = {
	[PD_MFC] = {
		.name		= "samsung-pd",
		.id		= PD_MFC,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.init		= exynos_pd_init,
				.enable		= exynos_pd_enable,
				.disable	= exynos_pd_disable,
				.base		= EXYNOS5_MFC_CONFIGURATION,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= EXYNOS5_CLKGATE_IP_MFC,
					.clksrc_base	= EXYNOS5_CLKSRC_TOP3,
				},
			},
		},
	},
	[PD_G3D] = {
		.name		= "samsung-pd",
		.id		= PD_G3D,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.init		= exynos_pd_init,
				.enable		= exynos_pd_enable,
				.disable	= exynos_pd_disable,
				.base		= EXYNOS5_G3D_CONFIGURATION,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= EXYNOS5_CLKGATE_IP_G3D,
					.clksrc_base	= EXYNOS5_CLKSRC_TOP3,
				},
			},
		},
	},
	[PD_GPS] = {
		.name		= "samsung-pd",
		.id		= PD_GPS,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.init		= exynos_pd_init,
				.enable		= exynos_pd_enable,
				.disable	= exynos_pd_disable,
				.base		= EXYNOS5_GPS_CONFIGURATION,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= EXYNOS5_CLKGATE_IP_GPS,
					.clksrc_base	= EXYNOS5_CLKSRC_TOP3,
				},
			},
		},
	},
	[PD_ISP] = {
		.name		= "samsung-pd",
		.id		= PD_ISP,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.init		= exynos_pd_init,
				.enable		= exynos_pd_enable,
				.disable	= exynos_pd_disable,
				.base		= EXYNOS5_ISP_CONFIGURATION,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= NULL,
					.clksrc_base	= EXYNOS5_CLKSRC_TOP3,
				},
			},
		},
	},
	[PD_GSCL] = {
		.name		= "samsung-pd",
		.id		= PD_GSCL,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.init		= exynos_pd_init,
				.enable		= exynos_pd_enable,
				.disable	= exynos_pd_disable,
				.base		= EXYNOS5_GSCL_CONFIGURATION,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= EXYNOS5_CLKGATE_IP_GSCL,
					.clksrc_base	= EXYNOS5_CLKSRC_TOP3,
				},
			},
		},
	},
	[PD_DISP1] = {
		.name		= "samsung-pd",
		.id		= PD_DISP1,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.init		= exynos_pd_init,
				.enable		= exynos_pd_enable,
				.disable	= exynos_pd_disable,
				.base		= EXYNOS5_DISP1_CONFIGURATION,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= EXYNOS5_CLKGATE_IP_DISP1,
					.clksrc_base	= EXYNOS5_CLKSRC_TOP3,
				},
			},
		},
	},
};
