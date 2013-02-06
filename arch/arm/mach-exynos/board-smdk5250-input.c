/* linux/arch/arm/mach-exynos/board-smdk5250-input.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/gpio.h>
#include <linux/gpio_event.h>
#include <linux/i2c.h>
#include <linux/input.h>

#include <plat/gpio-cfg.h>
#include <plat/devs.h>
#include <plat/iic.h>

#include <mach/irqs.h>

#include "board-smdk5250.h"

static struct gpio_event_direct_entry smdk5250_keypad_key_map[] = {
	{
		.gpio   = EXYNOS5_GPX0(0),
		.code   = KEY_POWER,
	}
};

static struct gpio_event_input_info smdk5250_keypad_key_info = {
	.info.func              = gpio_event_input_func,
	.info.no_suspend        = true,
	.debounce_time.tv64	= 5 * NSEC_PER_MSEC,
	.type                   = EV_KEY,
	.keymap                 = smdk5250_keypad_key_map,
	.keymap_size            = ARRAY_SIZE(smdk5250_keypad_key_map)
};

static struct gpio_event_info *smdk5250_input_info[] = {
	&smdk5250_keypad_key_info.info,
};

static struct gpio_event_platform_data smdk5250_input_data = {
	.names  = {
		"smdk5250-keypad",
		NULL,
	},
	.info           = smdk5250_input_info,
	.info_count     = ARRAY_SIZE(smdk5250_input_info),
};

static struct platform_device smdk5250_input_device = {
	.name   = GPIO_EVENT_DEV_NAME,
	.id     = 0,
	.dev    = {
		.platform_data = &smdk5250_input_data,
	},
};

static void __init smdk5250_gpio_power_init(void)
{
	int err = 0;

	err = gpio_request_one(EXYNOS5_GPX0(0), 0, "GPX0");
	if (err) {
		printk(KERN_ERR "failed to request GPX0 for "
				"suspend/resume control\n");
		return;
	}
	s3c_gpio_setpull(EXYNOS5_GPX0(0), S3C_GPIO_PULL_NONE);

	gpio_free(EXYNOS5_GPX0(0));
}

#ifdef CONFIG_WAKEUP_ASSIST
static struct platform_device wakeup_assist_device = {
	.name = "wakeup_assist",
};
#endif

struct egalax_i2c_platform_data {
	unsigned int gpio_int;
	unsigned int gpio_en;
	unsigned int gpio_rst;
};

static struct egalax_i2c_platform_data exynos5_egalax_data = {
	.gpio_int	= EXYNOS5_GPX3(1),
};

static struct i2c_board_info i2c_devs7[] __initdata = {
	{
		I2C_BOARD_INFO("egalax_i2c", 0x04),
		.irq		= IRQ_EINT(25),
		.platform_data	= &exynos5_egalax_data,
	},
};

static struct platform_device *smdk5250_input_devices[] __initdata = {
	&s3c_device_i2c7,
	&smdk5250_input_device,
#ifdef CONFIG_WAKEUP_ASSIST
	&wakeup_assist_device,
#endif
};

void __init exynos5_smdk5250_input_init(void)
{
	s3c_i2c7_set_platdata(NULL);
	i2c_register_board_info(7, i2c_devs7, ARRAY_SIZE(i2c_devs7));

	smdk5250_gpio_power_init();

	platform_add_devices(smdk5250_input_devices,
			ARRAY_SIZE(smdk5250_input_devices));
}
