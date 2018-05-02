// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2010 Samsung Electronics Co., Ltd.
//		http://www.samsung.com/
//
// GPIO configuration for S3C64XX KeyPad device

#include <linux/gpio.h>
#include <plat/gpio-cfg.h>
#include <plat/keypad.h>
#include <mach/gpio-samsung.h>

void samsung_keypad_cfg_gpio(unsigned int rows, unsigned int cols)
{
	/* Set all the necessary GPK pins to special-function 3: KP_ROW[x] */
	s3c_gpio_cfgrange_nopull(S3C64XX_GPK(8), rows, S3C_GPIO_SFN(3));

	/* Set all the necessary GPL pins to special-function 3: KP_COL[x] */
	s3c_gpio_cfgrange_nopull(S3C64XX_GPL(0), cols, S3C_GPIO_SFN(3));
}
