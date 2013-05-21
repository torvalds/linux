/* linux/arch/arm/mach-s5pv210/mach-torbreck.c
 *
 * Copyright (c) 2010 aESOP Community
 *		http://www.aesop.or.kr/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/serial_core.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <mach/map.h>
#include <mach/regs-clock.h>

#include <plat/regs-serial.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <linux/platform_data/i2c-s3c2410.h>
#include <plat/samsung-time.h>

#include "common.h"

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define TORBRECK_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define TORBRECK_ULCON_DEFAULT	S3C2410_LCON_CS8

#define TORBRECK_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

static struct s3c2410_uartcfg torbreck_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= TORBRECK_UCON_DEFAULT,
		.ulcon		= TORBRECK_ULCON_DEFAULT,
		.ufcon		= TORBRECK_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= TORBRECK_UCON_DEFAULT,
		.ulcon		= TORBRECK_ULCON_DEFAULT,
		.ufcon		= TORBRECK_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= TORBRECK_UCON_DEFAULT,
		.ulcon		= TORBRECK_ULCON_DEFAULT,
		.ufcon		= TORBRECK_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= TORBRECK_UCON_DEFAULT,
		.ulcon		= TORBRECK_ULCON_DEFAULT,
		.ufcon		= TORBRECK_UFCON_DEFAULT,
	},
};

static struct platform_device *torbreck_devices[] __initdata = {
	&s5pv210_device_iis0,
	&s3c_device_cfcon,
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc1,
	&s3c_device_hsmmc2,
	&s3c_device_hsmmc3,
	&s3c_device_i2c0,
	&s3c_device_i2c1,
	&s3c_device_i2c2,
	&s3c_device_rtc,
	&s3c_device_wdt,
};

static struct i2c_board_info torbreck_i2c_devs0[] __initdata = {
	/* To Be Updated */
};

static struct i2c_board_info torbreck_i2c_devs1[] __initdata = {
	/* To Be Updated */
};

static struct i2c_board_info torbreck_i2c_devs2[] __initdata = {
	/* To Be Updated */
};

static void __init torbreck_map_io(void)
{
	s5pv210_init_io(NULL, 0);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(torbreck_uartcfgs, ARRAY_SIZE(torbreck_uartcfgs));
	samsung_set_timer_source(SAMSUNG_PWM3, SAMSUNG_PWM4);
}

static void __init torbreck_machine_init(void)
{
	s3c_i2c0_set_platdata(NULL);
	s3c_i2c1_set_platdata(NULL);
	s3c_i2c2_set_platdata(NULL);
	i2c_register_board_info(0, torbreck_i2c_devs0,
			ARRAY_SIZE(torbreck_i2c_devs0));
	i2c_register_board_info(1, torbreck_i2c_devs1,
			ARRAY_SIZE(torbreck_i2c_devs1));
	i2c_register_board_info(2, torbreck_i2c_devs2,
			ARRAY_SIZE(torbreck_i2c_devs2));

	platform_add_devices(torbreck_devices, ARRAY_SIZE(torbreck_devices));
}

MACHINE_START(TORBRECK, "TORBRECK")
	/* Maintainer: Hyunchul Ko <ghcstop@gmail.com> */
	.atag_offset	= 0x100,
	.init_irq	= s5pv210_init_irq,
	.map_io		= torbreck_map_io,
	.init_machine	= torbreck_machine_init,
	.init_time	= samsung_timer_init,
	.restart	= s5pv210_restart,
MACHINE_END
