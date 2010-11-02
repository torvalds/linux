/* linux/arch/arm/mach-s5pv310/mach-universal_c210.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/gpio_keys.h>
#include <linux/gpio.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <plat/regs-serial.h>
#include <plat/s5pv310.h>
#include <plat/cpu.h>
#include <plat/devs.h>

#include <mach/map.h>

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define UNIVERSAL_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define UNIVERSAL_ULCON_DEFAULT	S3C2410_LCON_CS8

#define UNIVERSAL_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG256 |	\
				 S5PV210_UFCON_RXTRIG256)

static struct s3c2410_uartcfg universal_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.ucon		= UNIVERSAL_UCON_DEFAULT,
		.ulcon		= UNIVERSAL_ULCON_DEFAULT,
		.ufcon		= UNIVERSAL_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.ucon		= UNIVERSAL_UCON_DEFAULT,
		.ulcon		= UNIVERSAL_ULCON_DEFAULT,
		.ufcon		= UNIVERSAL_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.ucon		= UNIVERSAL_UCON_DEFAULT,
		.ulcon		= UNIVERSAL_ULCON_DEFAULT,
		.ufcon		= UNIVERSAL_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.ucon		= UNIVERSAL_UCON_DEFAULT,
		.ulcon		= UNIVERSAL_ULCON_DEFAULT,
		.ufcon		= UNIVERSAL_UFCON_DEFAULT,
	},
};

static struct gpio_keys_button universal_gpio_keys_tables[] = {
	{
		.code			= KEY_VOLUMEUP,
		.gpio			= S5PV310_GPX2(0),	/* XEINT16 */
		.desc			= "gpio-keys: KEY_VOLUMEUP",
		.type			= EV_KEY,
		.active_low		= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_VOLUMEDOWN,
		.gpio			= S5PV310_GPX2(1),	/* XEINT17 */
		.desc			= "gpio-keys: KEY_VOLUMEDOWN",
		.type			= EV_KEY,
		.active_low		= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_CONFIG,
		.gpio			= S5PV310_GPX2(2),	/* XEINT18 */
		.desc			= "gpio-keys: KEY_CONFIG",
		.type			= EV_KEY,
		.active_low		= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_CAMERA,
		.gpio			= S5PV310_GPX2(3),	/* XEINT19 */
		.desc			= "gpio-keys: KEY_CAMERA",
		.type			= EV_KEY,
		.active_low		= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_OK,
		.gpio			= S5PV310_GPX3(5),	/* XEINT29 */
		.desc			= "gpio-keys: KEY_OK",
		.type			= EV_KEY,
		.active_low		= 1,
		.debounce_interval	= 1,
	},
};

static struct gpio_keys_platform_data universal_gpio_keys_data = {
	.buttons	= universal_gpio_keys_tables,
	.nbuttons	= ARRAY_SIZE(universal_gpio_keys_tables),
};

static struct platform_device universal_gpio_keys = {
	.name			= "gpio-keys",
	.dev			= {
		.platform_data	= &universal_gpio_keys_data,
	},
};

/* I2C0 */
static struct i2c_board_info i2c0_devs[] __initdata = {
	/* Camera, To be updated */
};

/* I2C1 */
static struct i2c_board_info i2c1_devs[] __initdata = {
	/* Gyro, To be updated */
};

static struct platform_device *universal_devices[] __initdata = {
	&universal_gpio_keys,
	&s5p_device_onenand,
};

static void __init universal_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(universal_uartcfgs, ARRAY_SIZE(universal_uartcfgs));
}

static void __init universal_machine_init(void)
{
	i2c_register_board_info(0, i2c0_devs, ARRAY_SIZE(i2c0_devs));
	i2c_register_board_info(1, i2c1_devs, ARRAY_SIZE(i2c1_devs));

	/* Last */
	platform_add_devices(universal_devices, ARRAY_SIZE(universal_devices));
}

MACHINE_START(UNIVERSAL_C210, "UNIVERSAL_C210")
	/* Maintainer: Kyungmin Park <kyungmin.park@samsung.com> */
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= s5pv310_init_irq,
	.map_io		= universal_map_io,
	.init_machine	= universal_machine_init,
	.timer		= &s5pv310_timer,
MACHINE_END
