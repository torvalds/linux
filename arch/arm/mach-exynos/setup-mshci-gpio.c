/* linux/arch/arm/mach-exynos/setup-mshci-gpio.c
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS4 - Helper functions for setting up MSHCI device(s) GPIO (HSMMC)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/delay.h>

#include <mach/gpio.h>
#include <mach/map.h>
#include <plat/gpio-cfg.h>
#include <plat/mshci.h>
#include <plat/cpu.h>

#define GPK0DRV	(S5P_VA_GPIO2 + 0x4C)
#define GPK1DRV	(S5P_VA_GPIO2 + 0x6C)
#define GPK2DRV	(S5P_VA_GPIO2 + 0x8C)
#define GPK3DRV	(S5P_VA_GPIO2 + 0xAC)

#define DIV_FSYS3	(S5P_VA_CMU + 0x0C54C)

void exynos4_setup_mshci_cfg_gpio(struct platform_device *dev, int width)
{
	unsigned int gpio;
	struct s3c_mshci_platdata *pdata = dev->dev.platform_data;

	/* early_printk("exynos4_setup_mshci_cfg_gpio\n"); */

	/* Set all the necessary GPG0/GPG1 pins to special-function 2 */
	for (gpio = EXYNOS4_GPK0(0); gpio < EXYNOS4_GPK0(2); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}

	/* if CDn pin is used as eMMC_EN pin, it might make a problem
	   So, a built-in type eMMC is embedded, it dose not set CDn pin */
	if (pdata->cd_type != S3C_MSHCI_CD_PERMANENT) {
		s3c_gpio_cfgpin(EXYNOS4_GPK0(2), S3C_GPIO_SFN(3));
		s3c_gpio_setpull(EXYNOS4_GPK0(2), S3C_GPIO_PULL_NONE);
	}

	switch (width) {
	case 8:
		for (gpio = EXYNOS4_GPK1(3); gpio <= EXYNOS4_GPK1(6); gpio++) {
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(4));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		}
		__raw_writel(0x2AAA, GPK1DRV);
	case 4:
		/* GPK[3:6] special-funtion 2 */
		for (gpio = EXYNOS4_GPK0(3); gpio <= EXYNOS4_GPK0(6); gpio++) {
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		}
		__raw_writel(0x2AAA, GPK0DRV);
		break;
	case 1:
		/* GPK[3] special-funtion 2 */
		for (gpio = EXYNOS4_GPK0(3); gpio < EXYNOS4_GPK0(4); gpio++) {
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		}
		__raw_writel(0xAA, GPK0DRV);
	default:
		break;
	}
}

void exynos4_setup_mshci_cfg_ddr(struct platform_device *dev, int ddr)
{
	if (ddr) {
#ifdef CONFIG_EXYNOS4_MSHC_EPLL_45MHZ
		__raw_writel(0x00, DIV_FSYS3);
#elif defined(CONFIG_EXYNOS4_MSHC_VPLL_46MHZ)
		__raw_writel(0x01, DIV_FSYS3);
#else
#ifdef CONFIG_EXYNOS4_MSHC_SUPPORT_PQPRIME_EPLL
		if (soc_is_exynos4412() &&
				(samsung_rev() >= EXYNOS4412_REV_2_0))
			/* This is code for support PegasusQ Prime dynamically.
			 * PegasusQ Prime use EPLL rather than MPLL */
			__raw_writel(0x00, DIV_FSYS3);
		else
#endif /* ifdef CONFIG_EXYNOS4_MSHC_SUPPORT_PQPRIME_EPLL */
		if ((soc_is_exynos4412() || soc_is_exynos4212()) &&
				samsung_rev() >= EXYNOS4412_REV_1_0)
			__raw_writel(0x1, DIV_FSYS3);
		else
			__raw_writel(0x05, DIV_FSYS3);
#endif
	} else {
#ifdef CONFIG_EXYNOS4_MSHC_EPLL_45MHZ
		__raw_writel(0x01, DIV_FSYS3);
#elif defined(CONFIG_EXYNOS4_MSHC_VPLL_46MHZ)
		__raw_writel(0x03, DIV_FSYS3);
#else
#ifdef CONFIG_EXYNOS4_MSHC_SUPPORT_PQPRIME_EPLL
		if (soc_is_exynos4412() &&
				(samsung_rev() >= EXYNOS4412_REV_2_0))
			/* This is code for support PegasusQ Prime dynamically.
			 * PegasusQ Prime use EPLL rather than MPLL */
			__raw_writel(0x01, DIV_FSYS3);
		else
#endif /* ifdef CONFIG_EXYNOS4_MSHC_SUPPORT_PQPRIME_EPLL */
		if ((soc_is_exynos4412() || soc_is_exynos4212()) &&
				samsung_rev() >= EXYNOS4412_REV_1_0)
			__raw_writel(0x3, DIV_FSYS3);
		else
			__raw_writel(0xb, DIV_FSYS3);
#endif
	}
}

void exynos4_setup_mshci_init_card(struct platform_device *dev)
{
	/*
	 * Reset moviNAND for re-init.
	 * output/low for eMMC_EN and input/pull-none for others
	 * and then wait 10ms.
	 */
	__raw_writel(0x100, S5P_VA_GPIO2 + 0x40);
	__raw_writel(0, S5P_VA_GPIO2 + 0x44);
	__raw_writel(0, S5P_VA_GPIO2 + 0x48);
	__raw_writel(0, S5P_VA_GPIO2 + 0x60);
	__raw_writel(0, S5P_VA_GPIO2 + 0x64);
	__raw_writel(0, S5P_VA_GPIO2 + 0x68);
	mdelay(100);

	/* set data buswidth 8 */
	exynos4_setup_mshci_cfg_gpio(dev, 8);

	/* power to moviNAND on */
	gpio_set_value(EXYNOS4_GPK0(2), 1);

	/* to wait a pull-up resistance ready */
	mdelay(10);
}

void exynos4_setup_mshci_set_power(struct platform_device *dev, int en)
{
	struct s3c_mshci_platdata *pdata = dev->dev.platform_data;
	unsigned int gpio = 0;

	if (pdata->int_power_gpio) {
		if (en) {
#if defined(CONFIG_MACH_Q1_BD)
			mdelay(20);
#elif defined(CONFIG_MACH_PX)
			mdelay(10);
#endif
			/*CMD/CLK*/
			for (gpio = EXYNOS4_GPK0(0); gpio < EXYNOS4_GPK0(2);
					gpio++) {
				s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
				s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
			}
			/*DAT[0]~[3]*/
			for (gpio = EXYNOS4_GPK0(3); gpio <= EXYNOS4_GPK0(6);
					gpio++) {
				s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
				s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
			}
			/*DAT[4]~[7]*/
			for (gpio = EXYNOS4_GPK1(3); gpio <= EXYNOS4_GPK1(6);
					gpio++) {
				s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(4));
				s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
			}

			gpio_set_value(pdata->int_power_gpio, 1);
			pr_info("%s : internal MMC Card ON samsung-mshc.\n",
					__func__);
		} else {
#if defined(CONFIG_MACH_M0_CTC)
			s3c_gpio_cfgpin(pdata->int_power_gpio, S3C_GPIO_OUTPUT);
			s3c_gpio_setpull(pdata->int_power_gpio,
					S3C_GPIO_PULL_NONE);
#endif
			gpio_set_value(pdata->int_power_gpio, 0);

			/*CMD/CLK*/
			for (gpio = EXYNOS4_GPK0(0); gpio < EXYNOS4_GPK0(2);
					gpio++) {
				s3c_gpio_cfgpin(gpio, S3C_GPIO_INPUT);
				s3c_gpio_setpull(gpio, S3C_GPIO_PULL_DOWN);
			}
			/*DAT[0]~[3]*/
			for (gpio = EXYNOS4_GPK0(3); gpio <= EXYNOS4_GPK0(6);
					gpio++) {
				s3c_gpio_cfgpin(gpio, S3C_GPIO_INPUT);
				s3c_gpio_setpull(gpio, S3C_GPIO_PULL_DOWN);
			}
			/*DAT[4]~[7]*/
			for (gpio = EXYNOS4_GPK1(3); gpio <= EXYNOS4_GPK1(6);
					gpio++) {
				s3c_gpio_cfgpin(gpio, S3C_GPIO_INPUT);
				s3c_gpio_setpull(gpio, S3C_GPIO_PULL_DOWN);
			}
			pr_info("%s : internal MMC Card OFF samsung-mshc.\n",
					__func__);
			mdelay(50);
		}
	}
}

void exynos4_setup_mshci_shutdown()
{
        /* to reset eMMC card, VDD of eMMC should be off over 1ms */
	__raw_writel(0x100, S5P_VA_GPIO2 + 0x40);
	__raw_writel(0, S5P_VA_GPIO2 + 0x44);
	__raw_writel(0, S5P_VA_GPIO2 + 0x48);
	__raw_writel(0, S5P_VA_GPIO2 + 0x60);
	__raw_writel(0, S5P_VA_GPIO2 + 0x64);
	__raw_writel(0, S5P_VA_GPIO2 + 0x68);
	mdelay(10);
}
