/* linux/arch/arm/mach-s5pv310/dev-pd.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5PV310 - Power Domain support
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

#include <plat/pd.h>

static int s5pv310_pd_enable(struct device *dev)
{
	struct samsung_pd_info *pdata =  dev->platform_data;
	u32 timeout;

	__raw_writel(S5P_INT_LOCAL_PWR_EN, pdata->base);

	/* Wait max 1ms */
	timeout = 10;
	while ((__raw_readl(pdata->base + 0x4) & S5P_INT_LOCAL_PWR_EN)
		!= S5P_INT_LOCAL_PWR_EN) {
		if (timeout == 0) {
			printk(KERN_ERR "Power domain %s enable failed.\n",
				dev_name(dev));
			return -ETIMEDOUT;
		}
		timeout--;
		udelay(100);
	}

	return 0;
}

static int s5pv310_pd_disable(struct device *dev)
{
	struct samsung_pd_info *pdata =  dev->platform_data;
	u32 timeout;

	__raw_writel(0, pdata->base);

	/* Wait max 1ms */
	timeout = 10;
	while (__raw_readl(pdata->base + 0x4) & S5P_INT_LOCAL_PWR_EN) {
		if (timeout == 0) {
			printk(KERN_ERR "Power domain %s disable failed.\n",
				dev_name(dev));
			return -ETIMEDOUT;
		}
		timeout--;
		udelay(100);
	}

	return 0;
}

struct platform_device s5pv310_device_pd[] = {
	{
		.name		= "samsung-pd",
		.id		= 0,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.enable		= s5pv310_pd_enable,
				.disable	= s5pv310_pd_disable,
				.base		= S5P_PMU_MFC_CONF,
			},
		},
	}, {
		.name		= "samsung-pd",
		.id		= 1,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.enable		= s5pv310_pd_enable,
				.disable	= s5pv310_pd_disable,
				.base		= S5P_PMU_G3D_CONF,
			},
		},
	}, {
		.name		= "samsung-pd",
		.id		= 2,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.enable		= s5pv310_pd_enable,
				.disable	= s5pv310_pd_disable,
				.base		= S5P_PMU_LCD0_CONF,
			},
		},
	}, {
		.name		= "samsung-pd",
		.id		= 3,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.enable		= s5pv310_pd_enable,
				.disable	= s5pv310_pd_disable,
				.base		= S5P_PMU_LCD1_CONF,
			},
		},
	}, {
		.name		= "samsung-pd",
		.id		= 4,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.enable		= s5pv310_pd_enable,
				.disable	= s5pv310_pd_disable,
				.base		= S5P_PMU_TV_CONF,
			},
		},
	}, {
		.name		= "samsung-pd",
		.id		= 5,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.enable		= s5pv310_pd_enable,
				.disable	= s5pv310_pd_disable,
				.base		= S5P_PMU_CAM_CONF,
			},
		},
	}, {
		.name		= "samsung-pd",
		.id		= 6,
		.dev = {
			.platform_data = &(struct samsung_pd_info) {
				.enable		= s5pv310_pd_enable,
				.disable	= s5pv310_pd_disable,
				.base		= S5P_PMU_GPS_CONF,
			},
		},
	},
};
