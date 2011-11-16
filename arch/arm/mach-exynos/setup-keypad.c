/* linux/arch/arm/mach-exynos4/setup-keypad.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * GPIO configuration for Exynos4 KeyPad device
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/gpio.h>
#include <plat/gpio-cfg.h>

void samsung_keypad_cfg_gpio(unsigned int rows, unsigned int cols)
{
	/* Keypads can be of various combinations, Just making sure */

	if (rows > 8) {
		/* Set all the necessary GPX2 pins: KP_ROW[0~7] */
		s3c_gpio_cfgall_range(EXYNOS4_GPX2(0), 8, S3C_GPIO_SFN(3),
					S3C_GPIO_PULL_UP);

		/* Set all the necessary GPX3 pins: KP_ROW[8~] */
		s3c_gpio_cfgall_range(EXYNOS4_GPX3(0), (rows - 8),
					 S3C_GPIO_SFN(3), S3C_GPIO_PULL_UP);
	} else {
		/* Set all the necessary GPX2 pins: KP_ROW[x] */
		s3c_gpio_cfgall_range(EXYNOS4_GPX2(0), rows, S3C_GPIO_SFN(3),
					S3C_GPIO_PULL_UP);
	}

	/* Set all the necessary GPX1 pins to special-function 3: KP_COL[x] */
	s3c_gpio_cfgrange_nopull(EXYNOS4_GPX1(0), cols, S3C_GPIO_SFN(3));
}
