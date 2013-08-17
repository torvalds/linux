/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/export.h>

#include <plat/devs.h>
#include <plat/gpio-cfg.h>

#include <mach/irqs.h>
#include <mach/hs-iic.h>
#include <plat/iic.h>
#include <mach/regs-gpio.h>

#include "board-odroidxu.h"

#if defined(CONFIG_TOUCHSCREEN_SOLOMON_MT)
#include	<linux/input/touch-pdata.h>
#include	<linux/input/odroidxu-touch.h>

static void    odroidxu_gpio_init   (void)
{
    // IRQ GPIO PULL-UP
    s3c_gpio_setpull(EXYNOS5410_GPX1(0), S3C_GPIO_PULL_UP);
}

static	struct	touch_pdata		odroidxu_touch_pdata = {
	.name			= "odroidxu-ts",	/* input name define */
	.irq_gpio		= EXYNOS5410_GPX0(5),	/* irq gpio define */
	.reset_gpio		= EXYNOS5410_GPA0(5),	/* reset gpio define */
	.reset_level	= 0,				/* reset level setting (1 = High reset, 0 = Low reset) */
	.irq_mode		= IRQ_MODE_NORMAL,	/* IRQ_MODE_THREAD, IRQ_MODE_NORMAL, IRQ_MODE_POLLING */
	.irq_flags		= IRQF_TRIGGER_FALLING | IRQF_DISABLED,

	.abs_max_x		= SENSE_DATA_MAX,
	.abs_max_y		= DRIVE_DATA_MAX,

	.area_max		= 10, 
	.press_max		= PRESSURE_MAX, 
	.id_max			= TRACKING_ID_MAX,
	
	.vendor			= 0x16B4, 
	.product		= 0x0702, 
	.version		= 0x0100,

#if defined(CONFIG_ANDROID_PARANOID_NETWORK)
	.max_fingers	= MAX_FINGERS,
#else
    // Ubuntu is single touch used
	.max_fingers	= 1,
#endif	
	.gpio_init      = odroidxu_gpio_init,
	.touch_work		= odroidxu_work,
	.early_probe	= odroidxu_early_probe,
	.probe			= odroidxu_probe,
	.enable			= odroidxu_enable,
	.disable		= odroidxu_disable,
	.i2c_read		= odroidxu_i2c_read,
	.calibration	= odroidxu_calibration,
};

#endif

#if defined(CONFIG_TOUCHSCREEN_SOLOMON_MT)
struct s3c2410_platform_i2c i2c_data_odroidxu  __initdata = {
		.bus_num        = 0,
		.flags          = 0,
		.slave_addr     = 0x10,
		.frequency      = 400*1000,
		.sda_delay      = 100,
};

static struct i2c_board_info i2c_devs_touch[] __initdata = {
	{
		I2C_BOARD_INFO(I2C_TOUCH_NAME, (0x48)),
		.platform_data = &odroidxu_touch_pdata,
	},
};

#endif

static struct platform_device *odroidxu_input_devices[] __initdata = {
#if defined(CONFIG_TOUCHSCREEN_SOLOMON_MT)
	&s3c_device_i2c0,
#endif
};

void __init exynos5_odroidxu_input_init(void)
{
#if defined(CONFIG_TOUCHSCREEN_SOLOMON_MT)
	s3c_i2c0_set_platdata(&i2c_data_odroidxu);
	i2c_register_board_info(0, i2c_devs_touch, ARRAY_SIZE(i2c_devs_touch));
#endif
	platform_add_devices(odroidxu_input_devices, ARRAY_SIZE(odroidxu_input_devices));
}
