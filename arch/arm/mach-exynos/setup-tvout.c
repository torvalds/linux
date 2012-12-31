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
	s3c_gpio_cfgpin(HDMI_GPX(7), S3C_GPIO_SFN(0x3));
	s3c_gpio_setpull(HDMI_GPX(7), S3C_GPIO_PULL_DOWN);
}

void s5p_int_src_ext_hpd(struct platform_device *pdev)
{
	s3c_gpio_cfgpin(HDMI_GPX(7), S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(HDMI_GPX(7), S3C_GPIO_PULL_DOWN);
}

int s5p_hpd_read_gpio(struct platform_device *pdev)
{
	return gpio_get_value(HDMI_GPX(7));
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
	s3c_gpio_cfgpin(HDMI_GPX(6), S3C_GPIO_SFN(0x3));
	s3c_gpio_setpull(HDMI_GPX(6), S3C_GPIO_PULL_NONE);
}

#ifdef CONFIG_VIDEO_EXYNOS_TV
void s5p_tv_setup(void)
{
	/* direct HPD to HDMI chip */
	gpio_request(HDMI_GPX(7), "hpd-plug");
	gpio_direction_input(HDMI_GPX(7));
	s3c_gpio_cfgpin(HDMI_GPX(7), S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(HDMI_GPX(7), S3C_GPIO_PULL_NONE);

	/* HDMI CEC */
	gpio_request(HDMI_GPX(6), "hdmi-cec");
	gpio_direction_input(HDMI_GPX(6));
	s3c_gpio_cfgpin(HDMI_GPX(6), S3C_GPIO_SFN(0x3));
	s3c_gpio_setpull(HDMI_GPX(6), S3C_GPIO_PULL_NONE);
}
#endif
