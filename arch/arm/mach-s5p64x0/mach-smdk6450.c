/* linux/arch/arm/mach-s5p64x0/mach-smdk6450.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
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
#include <linux/gpio.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <mach/hardware.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/i2c.h>

#include <plat/regs-serial.h>
#include <plat/gpio-cfg.h>
#include <plat/s5p6450.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/iic.h>
#include <plat/pll.h>
#include <plat/adc.h>
#include <plat/ts.h>

#define SMDK6450_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				S3C2410_UCON_RXILEVEL |		\
				S3C2410_UCON_TXIRQMODE |	\
				S3C2410_UCON_RXIRQMODE |	\
				S3C2410_UCON_RXFIFO_TOI |	\
				S3C2443_UCON_RXERR_IRQEN)

#define SMDK6450_ULCON_DEFAULT	S3C2410_LCON_CS8

#define SMDK6450_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				S3C2440_UFCON_TXTRIG16 |	\
				S3C2410_UFCON_RXTRIG8)

static struct s3c2410_uartcfg smdk6450_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= SMDK6450_UCON_DEFAULT,
		.ulcon		= SMDK6450_ULCON_DEFAULT,
		.ufcon		= SMDK6450_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= SMDK6450_UCON_DEFAULT,
		.ulcon		= SMDK6450_ULCON_DEFAULT,
		.ufcon		= SMDK6450_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= SMDK6450_UCON_DEFAULT,
		.ulcon		= SMDK6450_ULCON_DEFAULT,
		.ufcon		= SMDK6450_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= SMDK6450_UCON_DEFAULT,
		.ulcon		= SMDK6450_ULCON_DEFAULT,
		.ufcon		= SMDK6450_UFCON_DEFAULT,
	},
#if CONFIG_SERIAL_SAMSUNG_UARTS > 4
	[4] = {
		.hwport		= 4,
		.flags		= 0,
		.ucon		= SMDK6450_UCON_DEFAULT,
		.ulcon		= SMDK6450_ULCON_DEFAULT,
		.ufcon		= SMDK6450_UFCON_DEFAULT,
	},
#endif
#if CONFIG_SERIAL_SAMSUNG_UARTS > 5
	[5] = {
		.hwport		= 5,
		.flags		= 0,
		.ucon		= SMDK6450_UCON_DEFAULT,
		.ulcon		= SMDK6450_ULCON_DEFAULT,
		.ufcon		= SMDK6450_UFCON_DEFAULT,
	},
#endif
};

static struct platform_device *smdk6450_devices[] __initdata = {
	&s3c_device_adc,
	&s3c_device_rtc,
	&s3c_device_i2c0,
	&s3c_device_i2c1,
	&s3c_device_ts,
	&s3c_device_wdt,
	&samsung_asoc_dma,
	&s5p6450_device_iis0,
	/* s5p6450_device_spi0 will be added */
};

static struct s3c2410_platform_i2c s5p6450_i2c0_data __initdata = {
	.flags		= 0,
	.slave_addr	= 0x10,
	.frequency	= 100*1000,
	.sda_delay	= 100,
	.cfg_gpio	= s5p6450_i2c0_cfg_gpio,
};

static struct s3c2410_platform_i2c s5p6450_i2c1_data __initdata = {
	.flags		= 0,
	.bus_num	= 1,
	.slave_addr	= 0x10,
	.frequency	= 100*1000,
	.sda_delay	= 100,
	.cfg_gpio	= s5p6450_i2c1_cfg_gpio,
};

static struct i2c_board_info smdk6450_i2c_devs0[] __initdata = {
	{ I2C_BOARD_INFO("wm8580", 0x1b), },
	{ I2C_BOARD_INFO("24c08", 0x50), },	/* Samsung KS24C080C EEPROM */
};

static struct i2c_board_info smdk6450_i2c_devs1[] __initdata = {
	{ I2C_BOARD_INFO("24c128", 0x57), },/* Samsung S524AD0XD1 EEPROM */
};

static struct s3c2410_ts_mach_info s3c_ts_platform __initdata = {
	.delay			= 10000,
	.presc			= 49,
	.oversampling_shift	= 2,
};

static void __init smdk6450_map_io(void)
{
	s5p_init_io(NULL, 0, S5P64X0_SYS_ID);
	s3c24xx_init_clocks(19200000);
	s3c24xx_init_uarts(smdk6450_uartcfgs, ARRAY_SIZE(smdk6450_uartcfgs));
}

static void __init smdk6450_machine_init(void)
{
	s3c24xx_ts_set_platdata(&s3c_ts_platform);

	s3c_i2c0_set_platdata(&s5p6450_i2c0_data);
	s3c_i2c1_set_platdata(&s5p6450_i2c1_data);
	i2c_register_board_info(0, smdk6450_i2c_devs0,
			ARRAY_SIZE(smdk6450_i2c_devs0));
	i2c_register_board_info(1, smdk6450_i2c_devs1,
			ARRAY_SIZE(smdk6450_i2c_devs1));

	platform_add_devices(smdk6450_devices, ARRAY_SIZE(smdk6450_devices));
}

MACHINE_START(SMDK6450, "SMDK6450")
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	.boot_params	= S5P64X0_PA_SDRAM + 0x100,

	.init_irq	= s5p6450_init_irq,
	.map_io		= smdk6450_map_io,
	.init_machine	= smdk6450_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END
