/*
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 *
 * S5PV210 camera interface GPIO configuration.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <plat/gpio-cfg.h>
#include <plat/camport.h>
#include <mach/gpio-samsung.h>

int s5pv210_fimc_setup_gpio(enum s5p_camport_id id)
{
	u32 gpio8, gpio5;
	int ret;

	switch (id) {
	case S5P_CAMPORT_A:
		gpio8 = S5PV210_GPE0(0);
		gpio5 = S5PV210_GPE1(0);
		break;

	case S5P_CAMPORT_B:
		gpio8 = S5PV210_GPJ0(0);
		gpio5 = S5PV210_GPJ1(0);
		break;

	default:
		WARN(1, "Wrong camport id: %d\n", id);
		return -EINVAL;
	}

	ret = s3c_gpio_cfgall_range(gpio8, 8, S3C_GPIO_SFN(2),
				    S3C_GPIO_PULL_UP);
	if (ret)
		return ret;

	return s3c_gpio_cfgall_range(gpio5, 5, S3C_GPIO_SFN(2),
				     S3C_GPIO_PULL_UP);
}
