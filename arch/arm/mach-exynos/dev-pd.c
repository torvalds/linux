/* linux/arch/arm/mach-exynos/dev-pd.c
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
#include <mach/regs-pmu5.h>
#include <mach/regs-clock.h>

#include <plat/cpu.h>
#include <plat/pd.h>

int exynos_pd_init(struct device *dev)
{
	struct samsung_pd_info *pdata =  dev->platform_data;
	struct exynos_pd_data *data = (struct exynos_pd_data *) pdata->data;

	if (soc_is_exynos4210() && data->read_phy_addr) {
		data->read_base = ioremap(data->read_phy_addr, SZ_4K);
		if (!data->read_base)
			return -ENOMEM;
	}

	return 0;
}

int exynos_pd_enable(struct device *dev)
{
	struct samsung_pd_info *pdata =  dev->platform_data;
	struct exynos_pd_data *data = (struct exynos_pd_data *) pdata->data;
	u32 timeout;
	u32 tmp = 0;

	/*  save IP clock gating register */
	if (data->clk_base) {
		tmp = __raw_readl(data->clk_base);

		/*  enable all the clocks of IPs in the power domain */
		__raw_writel(0xffffffff, data->clk_base);
	}

	__raw_writel(S5P_INT_LOCAL_PWR_EN, pdata->base);

	/* Wait max 1ms */
	timeout = 1000;
	while ((__raw_readl(pdata->base + 0x4) & S5P_INT_LOCAL_PWR_EN)
		!= S5P_INT_LOCAL_PWR_EN) {
		if (timeout == 0) {
			printk(KERN_ERR "Power domain %s enable failed.\n",
				dev_name(dev));
			return -ETIMEDOUT;
		}
		timeout--;
		udelay(1);
	}

	if (data->read_base)
		/* dummy read to check the completion of power-on sequence */
		__raw_readl(data->read_base);

	/* restore IP clock gating register */
	if (data->clk_base)
		__raw_writel(tmp, data->clk_base);

	return 0;
}

int exynos_pd_disable(struct device *dev)
{
	struct samsung_pd_info *pdata =  dev->platform_data;
	struct exynos_pd_data *data = (struct exynos_pd_data *) pdata->data;
	u32 timeout;
	u32 tmp = 0;

	/*  save clock source register */
	if (data->clksrc_base)
		tmp = __raw_readl(data->clksrc_base);

	/*
	 * To ISP power domain off,
	 * first, ISP_ARM power domain be off.
	 */
	if (soc_is_exynos5250() &&
		(pdata->base == EXYNOS5_ISP_CONFIGURATION)) {
		if (!(__raw_readl(EXYNOS5_ISP_ARM_STATUS) & 0x1)) {
			/* Disable ISP_ARM */
			timeout = __raw_readl(EXYNOS5_ISP_ARM_OPTION);
			timeout &= ~EXYNOS5_ISP_ARM_ENABLE;
			__raw_writel(timeout, EXYNOS5_ISP_ARM_OPTION);

			/* ISP_ARM power off */
			__raw_writel(0x0, EXYNOS5_ISP_ARM_CONFIGURATION);

			timeout = 1000;

			while (__raw_readl(EXYNOS5_ISP_ARM_STATUS) & 0x1) {
				if (timeout == 0) {
					printk(KERN_ERR "ISP_ARM power domain can not off\n");
					return -ETIMEDOUT;
				}
				timeout--;
				udelay(1);
			}
			/* CMU_RESET_ISP_ARM off */
			__raw_writel(0x0, EXYNOS5_CMU_RESET_ISP_SYS_PWR_REG);
		}
	}

	__raw_writel(0, pdata->base);

	/* Wait max 1ms */
	timeout = 1000;
	while (__raw_readl(pdata->base + 0x4) & S5P_INT_LOCAL_PWR_EN) {
		if (timeout == 0) {
			printk(KERN_ERR "Power domain %s disable failed.\n",
				dev_name(dev));
			return -ETIMEDOUT;
		}
		timeout--;
		udelay(1);
	}

	/* restore clock source register */
	if (data->clksrc_base)
		__raw_writel(tmp, data->clksrc_base);
	return 0;
}
