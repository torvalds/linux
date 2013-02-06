/* linux/arch/arm/mach-exynos/dev-pd-exynos4.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - Power Domain support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#include <mach/regs-pmu.h>
#include <mach/regs-clock.h>

#include <plat/pd.h>

static u32 exynos_pd_status[8];

static int exynos_pd_save(struct device *dev)
{
	struct samsung_pd_info *pdata = dev->platform_data;
	struct platform_device *pdev = to_platform_device(dev);
	int ret = 0;

	exynos_pd_status[pdev->id] = __raw_readl(pdata->base + 0x4);
	exynos_pd_status[pdev->id] &= S5P_INT_LOCAL_PWR_EN;

#if !defined(CONFIG_CPU_EXYNOS4210)
	pr_debug("%s: %s(%d) exynos4_pd_status = 0x%08x\n",
		__func__, pdev->name, pdev->id, exynos_pd_status[pdev->id]);
#endif

	if (exynos_pd_status[pdev->id] == S5P_INT_LOCAL_PWR_EN)
		ret = exynos_pd_disable(dev);

#if !defined(CONFIG_CPU_EXYNOS4210)
	pr_debug("%s: %s(%d) exynos4 pd status reg = 0x%08x\n",
		__func__, pdev->name, pdev->id, __raw_readl(pdata->base + 0x4));
#endif

	return ret;
}

static int exynos_pd_restore(struct device *dev)
{
	struct samsung_pd_info *pdata = dev->platform_data;
	struct platform_device *pdev = to_platform_device(dev);
	int ret = 0;

#if !defined(CONFIG_CPU_EXYNOS4210)
	pr_debug("%s: %s(%d) exynos4_pd_status = 0x%08x\n",
		__func__, pdev->name, pdev->id, exynos_pd_status[pdev->id]);
#endif

	if (exynos_pd_status[pdev->id] == S5P_INT_LOCAL_PWR_EN)
		ret =  exynos_pd_enable(dev);

	exynos_pd_status[pdev->id] = 0;

#if !defined(CONFIG_CPU_EXYNOS4210)
	pr_debug("%s: %s(%d) exynos4 pd status reg = 0x%08x\n",
		__func__, pdev->name, pdev->id, __raw_readl(pdata->base + 0x4));
#endif

	return ret;
}

struct platform_device exynos4_device_pd[] = {
	[PD_MFC] = {
		.name		= "samsung-pd",
		.id		= PD_MFC,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.init		= exynos_pd_init,
				.enable		= exynos_pd_enable,
				.disable	= exynos_pd_disable,
				.save		= exynos_pd_save,
				.restore	= exynos_pd_restore,
				.base		= S5P_PMU_MFC_CONF,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= EXYNOS4_CLKGATE_IP_MFC,
					.read_phy_addr	= EXYNOS4_PA_MFC,
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
				.save		= exynos_pd_save,
				.restore	= exynos_pd_restore,
				.base		= S5P_PMU_G3D_CONF,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= EXYNOS4_CLKGATE_IP_G3D,
					.read_phy_addr	= EXYNOS4_PA_G3D,
				},
			},
		},
	},
	[PD_LCD0] = {
		.name		= "samsung-pd",
		.id		= PD_LCD0,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.init		= exynos_pd_init,
				.enable		= exynos_pd_enable,
				.disable	= exynos_pd_disable,
				.save		= exynos_pd_save,
				.restore	= exynos_pd_restore,
				.base		= S5P_PMU_LCD0_CONF,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= EXYNOS4_CLKGATE_IP_LCD0,
					.read_phy_addr	= EXYNOS4_PA_FIMD0,
				},
			},
		},
	},
	[PD_LCD1] = {
		.name		= "samsung-pd",
		.id		= PD_LCD1,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.init		= exynos_pd_init,
				.enable		= exynos_pd_enable,
				.disable	= exynos_pd_disable,
				.save		= exynos_pd_save,
				.restore	= exynos_pd_restore,
				.base		= S5P_PMU_LCD1_CONF,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= EXYNOS4_CLKGATE_IP_LCD1,
					.read_phy_addr	= EXYNOS4_PA_FIMD1,
				},
			},
		},
	},
	[PD_TV] = {
		.name		= "samsung-pd",
		.id		= PD_TV,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.init		= exynos_pd_init,
				.enable		= exynos_pd_enable,
				.disable	= exynos_pd_disable,
				.save		= exynos_pd_save,
				.restore	= exynos_pd_restore,
				.base		= S5P_PMU_TV_CONF,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= EXYNOS4_CLKGATE_IP_TV,
					.read_phy_addr	= EXYNOS4_PA_VP,
				},
			},
		},
	},
	[PD_CAM] = {
		.name		= "samsung-pd",
		.id		= PD_CAM,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.init		= exynos_pd_init,
				.enable		= exynos_pd_enable,
				.disable	= exynos_pd_disable,
				.save		= exynos_pd_save,
				.restore	= exynos_pd_restore,
				.base		= S5P_PMU_CAM_CONF,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= EXYNOS4_CLKGATE_IP_CAM,
					.read_phy_addr	= EXYNOS4_PA_FIMC0,
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
				.save		= exynos_pd_save,
				.restore	= exynos_pd_restore,
				.base		= S5P_PMU_GPS_CONF,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= EXYNOS4_CLKGATE_IP_GPS,
					.read_phy_addr	= EXYNOS4_PA_GPS,
				},
			},
		},
	},
	[PD_GPS_ALIVE] = {
		.name		= "samsung-pd",
		.id		= PD_GPS_ALIVE,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.init		= exynos_pd_init,
				.enable		= exynos_pd_enable,
				.disable	= exynos_pd_disable,
				.save		= exynos_pd_save,
				.restore	= exynos_pd_restore,
				.base		= S5P_PMU_GPS_ALIVE_CONF,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= (void __iomem *)NULL,
					.read_phy_addr	= (unsigned long)NULL,
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
				.save		= exynos_pd_save,
				.restore	= exynos_pd_restore,
				.base		= S5P_PMU_ISP_CONF,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= EXYNOS4_CLKGATE_IP_ISP,
					.read_phy_addr	= EXYNOS4_PA_FIMC_IS,
				},
			},
		},
	}, 
	[PD_MAUDIO] = {
		.name		= "samsung-pd",
		.id		= PD_MAUDIO,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.init		= exynos_pd_init,
				.enable		= exynos_pd_enable,
				.disable	= exynos_pd_disable,
				.save		= exynos_pd_save,
				.restore	= exynos_pd_restore,
				.base		= S5P_PMU_MAUDIO_CONF,
				.data		= &(struct exynos_pd_data) {
					.clk_base	= EXYNOS4_CLKGATE_IP_MAUDIO,
					.read_phy_addr	= EXYNOS4_PA_AUDSS,
				},
			},
		},
	}, 
};
