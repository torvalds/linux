/* mach-hmt.c - Platform code for Airgoo HMT
 *
 * Copyright 2009 Peter Korsgaard <jacmet@sunsite.dk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/leds.h>
#include <linux/pwm_backlight.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/map.h>

#include <asm/hardware/vic.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <plat/regs-serial.h>
#include <plat/iic.h>
#include <plat/fb.h>
#include <plat/nand.h>

#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/regs-fb-v4.h>

#include "common.h"

#define UCON S3C2410_UCON_DEFAULT
#define ULCON (S3C2410_LCON_CS8 | S3C2410_LCON_PNONE)
#define UFCON (S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE)

static struct s3c2410_uartcfg hmt_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
};

static int hmt_bl_init(struct device *dev)
{
	int ret;

	ret = gpio_request(S3C64XX_GPB(4), "lcd backlight enable");
	if (!ret)
		ret = gpio_direction_output(S3C64XX_GPB(4), 0);

	return ret;
}

static int hmt_bl_notify(struct device *dev, int brightness)
{
	/*
	 * translate from CIELUV/CIELAB L*->brightness, E.G. from
	 * perceived luminance to light output. Assumes range 0..25600
	 */
	if (brightness < 0x800) {
		/* Y = Yn * L / 903.3 */
		brightness = (100*256 * brightness + 231245/2) / 231245;
	} else {
		/* Y = Yn * ((L + 16) / 116 )^3 */
		int t = (brightness*4 + 16*1024 + 58)/116;
		brightness = 25 * ((t * t * t + 0x100000/2) / 0x100000);
	}

	gpio_set_value(S3C64XX_GPB(4), brightness);

	return brightness;
}

static void hmt_bl_exit(struct device *dev)
{
	gpio_free(S3C64XX_GPB(4));
}

static struct platform_pwm_backlight_data hmt_backlight_data = {
	.pwm_id		= 1,
	.max_brightness	= 100 * 256,
	.dft_brightness	= 40 * 256,
	.pwm_period_ns	= 1000000000 / (100 * 256 * 20),
	.init		= hmt_bl_init,
	.notify		= hmt_bl_notify,
	.exit		= hmt_bl_exit,

};

static struct platform_device hmt_backlight_device = {
	.name		= "pwm-backlight",
	.dev		= {
		.parent	= &s3c_device_timer[1].dev,
		.platform_data = &hmt_backlight_data,
	},
};

static struct s3c_fb_pd_win hmt_fb_win0 = {
	.win_mode	= {
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

/* 405566 clocks per frame => 60Hz refresh requires 24333960Hz clock */
static struct s3c_fb_platdata hmt_lcd_pdata __initdata = {
	.setup_gpio	= s3c64xx_fb_gpio_setup_24bpp,
	.win[0]		= &hmt_fb_win0,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
};

static struct mtd_partition hmt_nand_part[] = {
	[0] = {
		.name	= "uboot",
		.size	= SZ_512K,
		.offset	= 0,
	},
	[1] = {
		.name	= "uboot-env1",
		.size	= SZ_256K,
		.offset	= SZ_512K,
	},
	[2] = {
		.name	= "uboot-env2",
		.size	= SZ_256K,
		.offset	= SZ_512K + SZ_256K,
	},
	[3] = {
		.name	= "kernel",
		.size	= SZ_2M,
		.offset	= SZ_1M,
	},
	[4] = {
		.name	= "rootfs",
		.size	= MTDPART_SIZ_FULL,
		.offset	= SZ_1M + SZ_2M,
	},
};

static struct s3c2410_nand_set hmt_nand_sets[] = {
	[0] = {
		.name		= "nand",
		.nr_chips	= 1,
		.nr_partitions	= ARRAY_SIZE(hmt_nand_part),
		.partitions	= hmt_nand_part,
	},
};

static struct s3c2410_platform_nand hmt_nand_info = {
	.tacls		= 25,
	.twrph0		= 55,
	.twrph1		= 40,
	.nr_sets	= ARRAY_SIZE(hmt_nand_sets),
	.sets		= hmt_nand_sets,
};

static struct gpio_led hmt_leds[] = {
	{ /* left function keys */
		.name			= "left:blue",
		.gpio			= S3C64XX_GPO(12),
		.default_trigger	= "default-on",
	},
	{ /* right function keys - red */
		.name			= "right:red",
		.gpio			= S3C64XX_GPO(13),
	},
	{ /* right function keys - green */
		.name			= "right:green",
		.gpio			= S3C64XX_GPO(14),
	},
	{ /* right function keys - blue */
		.name			= "right:blue",
		.gpio			= S3C64XX_GPO(15),
		.default_trigger	= "default-on",
	},
};

static struct gpio_led_platform_data hmt_led_data = {
	.num_leds = ARRAY_SIZE(hmt_leds),
	.leds = hmt_leds,
};

static struct platform_device hmt_leds_device = {
	.name			= "leds-gpio",
	.id			= -1,
	.dev.platform_data	= &hmt_led_data,
};

static struct map_desc hmt_iodesc[] = {};

static struct platform_device *hmt_devices[] __initdata = {
	&s3c_device_i2c0,
	&s3c_device_nand,
	&s3c_device_fb,
	&s3c_device_ohci,
	&s3c_device_timer[1],
	&hmt_backlight_device,
	&hmt_leds_device,
};

static void __init hmt_map_io(void)
{
	s3c64xx_init_io(hmt_iodesc, ARRAY_SIZE(hmt_iodesc));
	s3c24xx_init_clocks(12000000);
	s3c24xx_init_uarts(hmt_uartcfgs, ARRAY_SIZE(hmt_uartcfgs));
}

static void __init hmt_machine_init(void)
{
	s3c_i2c0_set_platdata(NULL);
	s3c_fb_set_platdata(&hmt_lcd_pdata);
	s3c_nand_set_platdata(&hmt_nand_info);

	gpio_request(S3C64XX_GPC(7), "usb power");
	gpio_direction_output(S3C64XX_GPC(7), 0);
	gpio_request(S3C64XX_GPM(0), "usb power");
	gpio_direction_output(S3C64XX_GPM(0), 1);
	gpio_request(S3C64XX_GPK(7), "usb power");
	gpio_direction_output(S3C64XX_GPK(7), 1);
	gpio_request(S3C64XX_GPF(13), "usb power");
	gpio_direction_output(S3C64XX_GPF(13), 1);

	platform_add_devices(hmt_devices, ARRAY_SIZE(hmt_devices));
}

MACHINE_START(HMT, "Airgoo-HMT")
	/* Maintainer: Peter Korsgaard <jacmet@sunsite.dk> */
	.atag_offset	= 0x100,
	.init_irq	= s3c6410_init_irq,
	.handle_irq	= vic_handle_irq,
	.map_io		= hmt_map_io,
	.init_machine	= hmt_machine_init,
	.timer		= &s3c24xx_timer,
	.restart	= s3c64xx_restart,
MACHINE_END
