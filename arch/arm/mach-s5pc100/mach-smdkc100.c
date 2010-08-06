/* linux/arch/arm/mach-s5pc100/mach-smdkc100.c
 *
 * Copyright 2009 Samsung Electronics Co.
 * Author: Byungho Min <bhmin@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/fb.h>
#include <linux/delay.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/map.h>
#include <mach/regs-fb.h>
#include <video/platform_lcd.h>

#include <asm/irq.h>
#include <asm/mach-types.h>

#include <plat/regs-serial.h>
#include <plat/gpio-cfg.h>

#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/s5pc100.h>
#include <plat/fb.h>
#include <plat/iic.h>

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define S5PC100_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define S5PC100_ULCON_DEFAULT	S3C2410_LCON_CS8

#define S5PC100_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S3C2440_UFCON_RXTRIG8 |	\
				 S3C2440_UFCON_TXTRIG16)

static struct s3c2410_uartcfg smdkc100_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = S5PC100_UCON_DEFAULT,
		.ulcon	     = S5PC100_ULCON_DEFAULT,
		.ufcon	     = S5PC100_UFCON_DEFAULT,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = S5PC100_UCON_DEFAULT,
		.ulcon	     = S5PC100_ULCON_DEFAULT,
		.ufcon	     = S5PC100_UFCON_DEFAULT,
	},
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = S5PC100_UCON_DEFAULT,
		.ulcon	     = S5PC100_ULCON_DEFAULT,
		.ufcon	     = S5PC100_UFCON_DEFAULT,
	},
	[3] = {
		.hwport	     = 3,
		.flags	     = 0,
		.ucon	     = S5PC100_UCON_DEFAULT,
		.ulcon	     = S5PC100_ULCON_DEFAULT,
		.ufcon	     = S5PC100_UFCON_DEFAULT,
	},
};

/* I2C0 */
static struct i2c_board_info i2c_devs0[] __initdata = {
};

/* I2C1 */
static struct i2c_board_info i2c_devs1[] __initdata = {
};

/* LCD power controller */
static void smdkc100_lcd_power_set(struct plat_lcd_data *pd,
				   unsigned int power)
{
	/* backlight */
	gpio_direction_output(S5PC100_GPD(0), power);

	if (power) {
		/* module reset */
		gpio_direction_output(S5PC100_GPH0(6), 1);
		mdelay(100);
		gpio_direction_output(S5PC100_GPH0(6), 0);
		mdelay(10);
		gpio_direction_output(S5PC100_GPH0(6), 1);
		mdelay(10);
	}
}

static struct plat_lcd_data smdkc100_lcd_power_data = {
	.set_power	= smdkc100_lcd_power_set,
};

static struct platform_device smdkc100_lcd_powerdev = {
	.name			= "platform-lcd",
	.dev.parent		= &s3c_device_fb.dev,
	.dev.platform_data	= &smdkc100_lcd_power_data,
};

/* Frame Buffer */
static struct s3c_fb_pd_win smdkc100_fb_win0 = {
	/* this is to ensure we use win0 */
	.win_mode	= {
		.pixclock = 1000000000000ULL / ((8+13+3+800)*(7+5+1+480)*80),
		.left_margin	= 8,
		.right_margin	= 13,
		.upper_margin	= 7,
		.lower_margin	= 5,
		.hsync_len	= 3,
		.vsync_len	= 1,
		.xres		= 800,
		.yres		= 480,
	},
	.max_bpp	= 32,
	.default_bpp	= 16,
};

static struct s3c_fb_platdata smdkc100_lcd_pdata __initdata = {
	.win[0]		= &smdkc100_fb_win0,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
	.setup_gpio	= s5pc100_fb_gpio_setup_24bpp,
};

static struct platform_device *smdkc100_devices[] __initdata = {
	&s3c_device_i2c0,
	&s3c_device_i2c1,
	&s3c_device_fb,
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc1,
	&s3c_device_hsmmc2,
	&smdkc100_lcd_powerdev,
	&s5pc100_device_iis0,
	&s5pc100_device_ac97,
};

static void __init smdkc100_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(12000000);
	s3c24xx_init_uarts(smdkc100_uartcfgs, ARRAY_SIZE(smdkc100_uartcfgs));
}

static void __init smdkc100_machine_init(void)
{
	/* I2C */
	s3c_i2c0_set_platdata(NULL);
	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(0, i2c_devs0, ARRAY_SIZE(i2c_devs0));
	i2c_register_board_info(1, i2c_devs1, ARRAY_SIZE(i2c_devs1));

	s3c_fb_set_platdata(&smdkc100_lcd_pdata);

	/* LCD init */
	gpio_request(S5PC100_GPD(0), "GPD");
	gpio_request(S5PC100_GPH0(6), "GPH0");
	smdkc100_lcd_power_set(&smdkc100_lcd_power_data, 0);
	platform_add_devices(smdkc100_devices, ARRAY_SIZE(smdkc100_devices));
}

MACHINE_START(SMDKC100, "SMDKC100")
	/* Maintainer: Byungho Min <bhmin@samsung.com> */
	.phys_io	= S3C_PA_UART & 0xfff00000,
	.io_pg_offst	= (((u32)S3C_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= s5pc100_init_irq,
	.map_io		= smdkc100_map_io,
	.init_machine	= smdkc100_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END
