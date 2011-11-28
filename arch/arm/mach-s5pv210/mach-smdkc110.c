/* linux/arch/arm/mach-s5pv210/mach-smdkc110.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/i2c.h>
#include <linux/sysdev.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <mach/map.h>
#include <mach/regs-clock.h>

#include <plat/regs-serial.h>
#include <plat/s5pv210.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/ata.h>
#include <plat/iic.h>
#include <plat/pm.h>
#include <plat/s5p-time.h>

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define SMDKC110_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define SMDKC110_ULCON_DEFAULT	S3C2410_LCON_CS8

#define SMDKC110_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

static struct s3c2410_uartcfg smdkv210_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= SMDKC110_UCON_DEFAULT,
		.ulcon		= SMDKC110_ULCON_DEFAULT,
		.ufcon		= SMDKC110_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= SMDKC110_UCON_DEFAULT,
		.ulcon		= SMDKC110_ULCON_DEFAULT,
		.ufcon		= SMDKC110_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= SMDKC110_UCON_DEFAULT,
		.ulcon		= SMDKC110_ULCON_DEFAULT,
		.ufcon		= SMDKC110_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= SMDKC110_UCON_DEFAULT,
		.ulcon		= SMDKC110_ULCON_DEFAULT,
		.ufcon		= SMDKC110_UFCON_DEFAULT,
	},
};

static struct s3c_ide_platdata smdkc110_ide_pdata __initdata = {
	.setup_gpio	= s5pv210_ide_setup_gpio,
};

static struct platform_device *smdkc110_devices[] __initdata = {
	&samsung_asoc_dma,
	&s5pv210_device_iis0,
	&s5pv210_device_ac97,
	&s5pv210_device_spdif,
	&s3c_device_cfcon,
	&s3c_device_i2c0,
	&s3c_device_i2c1,
	&s3c_device_i2c2,
	&s3c_device_rtc,
	&s3c_device_wdt,
};

static struct i2c_board_info smdkc110_i2c_devs0[] __initdata = {
	{ I2C_BOARD_INFO("24c08", 0x50), },     /* Samsung S524AD0XD1 */
	{ I2C_BOARD_INFO("wm8580", 0x1b), },
};

static struct i2c_board_info smdkc110_i2c_devs1[] __initdata = {
	/* To Be Updated */
};

static struct i2c_board_info smdkc110_i2c_devs2[] __initdata = {
	/* To Be Updated */
};

static void __init smdkc110_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(smdkv210_uartcfgs, ARRAY_SIZE(smdkv210_uartcfgs));
	s5p_set_timer_source(S5P_PWM3, S5P_PWM4);
}

static void __init smdkc110_machine_init(void)
{
	s3c_pm_init();

	s3c_i2c0_set_platdata(NULL);
	s3c_i2c1_set_platdata(NULL);
	s3c_i2c2_set_platdata(NULL);
	i2c_register_board_info(0, smdkc110_i2c_devs0,
			ARRAY_SIZE(smdkc110_i2c_devs0));
	i2c_register_board_info(1, smdkc110_i2c_devs1,
			ARRAY_SIZE(smdkc110_i2c_devs1));
	i2c_register_board_info(2, smdkc110_i2c_devs2,
			ARRAY_SIZE(smdkc110_i2c_devs2));

	s3c_ide_set_platdata(&smdkc110_ide_pdata);

	platform_add_devices(smdkc110_devices, ARRAY_SIZE(smdkc110_devices));
}

MACHINE_START(SMDKC110, "SMDKC110")
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	.atag_offset	= 0x100,
	.init_irq	= s5pv210_init_irq,
	.map_io		= smdkc110_map_io,
	.init_machine	= smdkc110_machine_init,
	.timer		= &s5p_timer,
MACHINE_END
