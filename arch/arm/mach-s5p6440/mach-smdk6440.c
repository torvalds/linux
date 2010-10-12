/* linux/arch/arm/mach-s5p6440/mach-smdk6440.c
 *
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/clk.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/map.h>

#include <asm/irq.h>
#include <asm/mach-types.h>

#include <plat/regs-serial.h>

#include <plat/s5p6440.h>
#include <plat/clock.h>
#include <mach/regs-clock.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/iic.h>
#include <plat/pll.h>
#include <plat/adc.h>
#include <plat/ts.h>

#define SMDK6440_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				S3C2410_UCON_RXILEVEL |		\
				S3C2410_UCON_TXIRQMODE |	\
				S3C2410_UCON_RXIRQMODE |	\
				S3C2410_UCON_RXFIFO_TOI |	\
				S3C2443_UCON_RXERR_IRQEN)

#define SMDK6440_ULCON_DEFAULT	S3C2410_LCON_CS8

#define SMDK6440_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				S3C2440_UFCON_TXTRIG16 |	\
				S3C2410_UFCON_RXTRIG8)

static struct s3c2410_uartcfg smdk6440_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = SMDK6440_UCON_DEFAULT,
		.ulcon	     = SMDK6440_ULCON_DEFAULT,
		.ufcon	     = SMDK6440_UFCON_DEFAULT,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = SMDK6440_UCON_DEFAULT,
		.ulcon	     = SMDK6440_ULCON_DEFAULT,
		.ufcon	     = SMDK6440_UFCON_DEFAULT,
	},
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = SMDK6440_UCON_DEFAULT,
		.ulcon	     = SMDK6440_ULCON_DEFAULT,
		.ufcon	     = SMDK6440_UFCON_DEFAULT,
	},
	[3] = {
		.hwport	     = 3,
		.flags	     = 0,
		.ucon	     = SMDK6440_UCON_DEFAULT,
		.ulcon	     = SMDK6440_ULCON_DEFAULT,
		.ufcon	     = SMDK6440_UFCON_DEFAULT,
	},
};

static struct platform_device *smdk6440_devices[] __initdata = {
	&s5p6440_device_iis,
	&s3c_device_adc,
	&s3c_device_rtc,
	&s3c_device_i2c0,
	&s3c_device_i2c1,
	&s3c_device_ts,
	&s3c_device_wdt,
};

static struct i2c_board_info smdk6440_i2c_devs0[] __initdata = {
	{ I2C_BOARD_INFO("24c08", 0x50), },
};

static struct i2c_board_info smdk6440_i2c_devs1[] __initdata = {
	/* To be populated */
};

static struct s3c2410_ts_mach_info s3c_ts_platform __initdata = {
	.delay			= 10000,
	.presc			= 49,
	.oversampling_shift	= 2,
};

static void __init smdk6440_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_SYS_ID);
	s3c24xx_init_clocks(12000000);
	s3c24xx_init_uarts(smdk6440_uartcfgs, ARRAY_SIZE(smdk6440_uartcfgs));
}

static void __init smdk6440_machine_init(void)
{
	s3c24xx_ts_set_platdata(&s3c_ts_platform);

	/* I2C */
	s3c_i2c0_set_platdata(NULL);
	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(0, smdk6440_i2c_devs0,
			ARRAY_SIZE(smdk6440_i2c_devs0));
	i2c_register_board_info(1, smdk6440_i2c_devs1,
			ARRAY_SIZE(smdk6440_i2c_devs1));

	platform_add_devices(smdk6440_devices, ARRAY_SIZE(smdk6440_devices));
}

MACHINE_START(SMDK6440, "SMDK6440")
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	.phys_io	= S3C_PA_UART & 0xfff00000,
	.io_pg_offst	= (((u32)S3C_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S5P_PA_SDRAM + 0x100,

	.init_irq	= s5p6440_init_irq,
	.map_io		= smdk6440_map_io,
	.init_machine	= smdk6440_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END
