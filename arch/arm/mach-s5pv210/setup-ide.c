/* linux/arch/arm/mach-s5pv210/setup-ide.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5PV210 setup information for IDE
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/gpio.h>

#include <plat/gpio-cfg.h>

void s5pv210_ide_setup_gpio(void)
{
	unsigned int gpio = 0;

	for (gpio = S5PV210_GPJ0(0); gpio <= S5PV210_GPJ0(7); gpio++) {
		/* CF_Add[0 - 2], CF_IORDY, CF_INTRQ, CF_DMARQ, CF_DMARST,
			CF_DMACK */
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(4));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	for (gpio = S5PV210_GPJ2(0); gpio <= S5PV210_GPJ2(7); gpio++) {
		/*CF_Data[0 - 7] */
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(4));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	for (gpio = S5PV210_GPJ3(0); gpio <= S5PV210_GPJ3(7); gpio++) {
		/* CF_Data[8 - 15] */
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(4));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	for (gpio = S5PV210_GPJ4(0); gpio <= S5PV210_GPJ4(3); gpio++) {
		/* CF_CS0, CF_CS1, CF_IORD, CF_IOWR */
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(4));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}
}
