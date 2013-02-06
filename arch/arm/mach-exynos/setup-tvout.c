/* linux/arch/arm/mach-exynos/setup-tvout.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Base TVOUT gpio configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <plat/clock.h>
#include <plat/gpio-cfg.h>
#include <mach/regs-clock.h>
#include <mach/regs-gpio.h>
#include <linux/io.h>
#include <mach/map.h>
#include <mach/gpio.h>
#include <plat/tvout.h>
#include <plat/cpu.h>

#if defined(CONFIG_ARCH_EXYNOS4)
#define HDMI_GPX(_nr)	EXYNOS4_GPX3(_nr)
#elif defined(CONFIG_ARCH_EXYNOS5)
#define HDMI_GPX(_nr)	EXYNOS5_GPX3(_nr)
#endif

struct platform_device; /* don't need the contents */

void s5p_int_src_hdmi_hpd(struct platform_device *pdev)
{
	printk(KERN_INFO "%s()\n", __func__);
#ifdef CONFIG_MACH_U1_NA_USCC
	s3c_gpio_cfgpin(GPIO_HDMI_HPD , S3C_GPIO_INPUT);
#else
	s3c_gpio_cfgpin(GPIO_HDMI_HPD, S3C_GPIO_SFN(0x3));
#endif
	s3c_gpio_setpull(GPIO_HDMI_HPD, S3C_GPIO_PULL_DOWN);
}

void s5p_int_src_ext_hpd(struct platform_device *pdev)
{
#ifdef CONFIG_MACH_U1_NA_USCC /* NC */
	printk(KERN_INFO "%s()\n", __func__);
	s3c_gpio_cfgpin(GPIO_HDMI_HPD, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_HDMI_HPD, S3C_GPIO_PULL_DOWN);
#else
	printk(KERN_INFO "%s()\n", __func__);
	s3c_gpio_cfgpin(GPIO_HDMI_HPD, S3C_GPIO_SFN(0xf));
	/* To avoid floating state of the HPD pin *
	 * in the absence of external pull-up     */
#if defined(CONFIG_CPU_EXYNOS4212) || defined(CONFIG_CPU_EXYNOS4412)
	s3c_gpio_setpull(GPIO_HDMI_HPD, S3C_GPIO_PULL_DOWN);
#else
	s3c_gpio_setpull(GPIO_HDMI_HPD, S3C_GPIO_PULL_NONE);
#endif
#endif
}

int s5p_hpd_read_gpio(struct platform_device *pdev)
{
	int ret;
	ret = gpio_get_value(GPIO_HDMI_HPD);
	printk(KERN_INFO "%s(%d)\n", __func__, ret);
	return ret;
}

int s5p_v4l2_hpd_read_gpio(void)
{
	return gpio_get_value(HDMI_GPX(7));
}

void s5p_v4l2_int_src_hdmi_hpd(void)
{
	s3c_gpio_cfgpin(HDMI_GPX(7), S3C_GPIO_SFN(0x3));
	s3c_gpio_setpull(HDMI_GPX(7), S3C_GPIO_PULL_DOWN);
}

void s5p_v4l2_int_src_ext_hpd(void)
{
	s3c_gpio_cfgpin(HDMI_GPX(7), S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(HDMI_GPX(7), S3C_GPIO_PULL_DOWN);
}

void s5p_cec_cfg_gpio(struct platform_device *pdev)
{
#ifdef CONFIG_HDMI_CEC
	s3c_gpio_cfgpin(GPIO_HDMI_CEC, S3C_GPIO_SFN(0x3));
	s3c_gpio_setpull(GPIO_HDMI_CEC, S3C_GPIO_PULL_NONE);
#endif
}

#ifdef CONFIG_VIDEO_EXYNOS_TV
void s5p_tv_setup(void)
{
	int ret;

	/* direct HPD to HDMI chip */
	if (soc_is_exynos4412()) {
		gpio_request(GPIO_HDMI_HPD, "hpd-plug");

		gpio_direction_input(GPIO_HDMI_HPD);
		s3c_gpio_cfgpin(GPIO_HDMI_HPD, S3C_GPIO_SFN(0x3));
		s3c_gpio_setpull(GPIO_HDMI_HPD, S3C_GPIO_PULL_NONE);
	} else if (soc_is_exynos5250()) {
		gpio_request(GPIO_HDMI_HPD, "hpd-plug");
		gpio_direction_input(GPIO_HDMI_HPD);
		s3c_gpio_cfgpin(GPIO_HDMI_HPD, S3C_GPIO_SFN(0x3));
		s3c_gpio_setpull(GPIO_HDMI_HPD, S3C_GPIO_PULL_NONE);

		/* HDMI CEC */
		gpio_request(GPIO_HDMI_CEC, "hdmi-cec");
		gpio_direction_input(GPIO_HDMI_CEC);
		s3c_gpio_cfgpin(GPIO_HDMI_CEC, S3C_GPIO_SFN(0x3));
		s3c_gpio_setpull(GPIO_HDMI_CEC, S3C_GPIO_PULL_NONE);
	} else {
		printk(KERN_ERR "HPD GPIOs are not defined!\n");
	}
}
#endif
