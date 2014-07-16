/*
 * linux/arch/arm/mach-s5pv210/setup-keypad.c
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <plat/gpio-cfg.h>
#include <mach/gpio-samsung.h>

void samsung_keypad_cfg_gpio(unsigned int rows, unsigned int cols)
{
	/* Set all the necessary GPH3 pins to special-function 3: KP_ROW[x] */
	s3c_gpio_cfgrange_nopull(S5PV210_GPH3(0), rows, S3C_GPIO_SFN(3));

	/* Set all the necessary GPH2 pins to special-function 3: KP_COL[x] */
	s3c_gpio_cfgrange_nopull(S5PV210_GPH2(0), cols, S3C_GPIO_SFN(3));
}
