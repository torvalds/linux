/* linux/arch/arm/mach-s5pc100/setup-keypad.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * GPIO configuration for S5PC100 KeyPad device
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/gpio.h>
#include <plat/gpio-cfg.h>

void samsung_keypad_cfg_gpio(unsigned int rows, unsigned int cols)
{
	unsigned int gpio;
	unsigned int end;

	/* Set all the necessary GPH3 pins to special-function 3: KP_ROW[x] */
	end = S5PC100_GPH3(rows);
	for (gpio = S5PC100_GPH3(0); gpio < end; gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}

	/* Set all the necessary GPH2 pins to special-function 3: KP_COL[x] */
	end = S5PC100_GPH2(cols);
	for (gpio = S5PC100_GPH2(0); gpio < end; gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}
}
