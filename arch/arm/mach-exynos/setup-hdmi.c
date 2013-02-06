/*
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundationr
 */

#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <plat/gpio-cfg.h>

void s5p_hdmi_cfg_hpd(bool enable)
{
	if (enable)
		s3c_gpio_cfgpin(GPIO_HDMI_HPD, S3C_GPIO_SFN(3));
	else
		s3c_gpio_cfgpin(GPIO_HDMI_HPD, S3C_GPIO_SFN(0xf));

	s3c_gpio_setpull(GPIO_HDMI_HPD, S3C_GPIO_PULL_DOWN);
}

int s5p_hdmi_get_hpd(void)
{
	return !!gpio_get_value(GPIO_HDMI_HPD);
}
