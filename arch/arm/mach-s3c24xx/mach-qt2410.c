/* linux/arch/arm/mach-s3c2410/mach-qt2410.c
 *
 * Copyright (C) 2006 by OpenMoko, Inc.
 * Author: Harald Welte <laforge@openmoko.org>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <linux/platform_data/leds-s3c24xx.h>
#include <mach/regs-lcd.h>
#include <mach/fb.h>
#include <linux/platform_data/mtd-nand-s3c2410.h>
#include <linux/platform_data/usb-s3c2410_udc.h>
#include <linux/platform_data/i2c-s3c2410.h>
#include <mach/gpio-samsung.h>

#include <plat/gpio-cfg.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/pm.h>
#include <plat/samsung-time.h>

#include "common.h"
#include "common-smdk.h"

static struct map_desc qt2410_iodesc[] __initdata = {
	{ 0xe0000000, __phys_to_pfn(S3C2410_CS3+0x01000000), SZ_1M, MT_DEVICE }
};

#define UCON S3C2410_UCON_DEFAULT
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg smdk2410_uartcfgs[] = {
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
	}
};

/* LCD driver info */

static struct s3c2410fb_display qt2410_lcd_cfg[] __initdata = {
	{
		/* Configuration for 640x480 SHARP LQ080V3DG01 */
		.lcdcon5 = S3C2410_LCDCON5_FRM565 |
			   S3C2410_LCDCON5_INVVLINE |
			   S3C2410_LCDCON5_INVVFRAME |
			   S3C2410_LCDCON5_PWREN |
			   S3C2410_LCDCON5_HWSWP,

		.type		= S3C2410_LCDCON1_TFT,
		.width		= 640,
		.height		= 480,

		.pixclock	= 40000, /* HCLK/4 */
		.xres		= 640,
		.yres		= 480,
		.bpp		= 16,
		.left_margin	= 44,
		.right_margin	= 116,
		.hsync_len	= 96,
		.upper_margin	= 19,
		.lower_margin	= 11,
		.vsync_len	= 15,
	},
	{
		/* Configuration for 480x640 toppoly TD028TTEC1 */
		.lcdcon5 = S3C2410_LCDCON5_FRM565 |
			   S3C2410_LCDCON5_INVVLINE |
			   S3C2410_LCDCON5_INVVFRAME |
			   S3C2410_LCDCON5_PWREN |
			   S3C2410_LCDCON5_HWSWP,

		.type		= S3C2410_LCDCON1_TFT,
		.width		= 480,
		.height		= 640,
		.pixclock	= 40000, /* HCLK/4 */
		.xres		= 480,
		.yres		= 640,
		.bpp		= 16,
		.left_margin	= 8,
		.right_margin	= 24,
		.hsync_len	= 8,
		.upper_margin	= 2,
		.lower_margin	= 4,
		.vsync_len	= 2,
	},
	{
		/* Config for 240x320 LCD */
		.lcdcon5 = S3C2410_LCDCON5_FRM565 |
			   S3C2410_LCDCON5_INVVLINE |
			   S3C2410_LCDCON5_INVVFRAME |
			   S3C2410_LCDCON5_PWREN |
			   S3C2410_LCDCON5_HWSWP,

		.type		= S3C2410_LCDCON1_TFT,
		.width		= 240,
		.height		= 320,
		.pixclock	= 100000, /* HCLK/10 */
		.xres		= 240,
		.yres		= 320,
		.bpp		= 16,
		.left_margin	= 13,
		.right_margin	= 8,
		.hsync_len	= 4,
		.upper_margin	= 2,
		.lower_margin	= 7,
		.vsync_len	= 4,
	},
};


static struct s3c2410fb_mach_info qt2410_fb_info __initdata = {
	.displays 	= qt2410_lcd_cfg,
	.num_displays 	= ARRAY_SIZE(qt2410_lcd_cfg),
	.default_display = 0,

	.lpcsel		= ((0xCE6) & ~7) | 1<<4,
};

/* CS8900 */

static struct resource qt2410_cs89x0_resources[] = {
	[0] = DEFINE_RES_MEM(0x19000000, 17),
	[1] = DEFINE_RES_IRQ(IRQ_EINT9),
};

static struct platform_device qt2410_cs89x0 = {
	.name		= "cirrus-cs89x0",
	.num_resources	= ARRAY_SIZE(qt2410_cs89x0_resources),
	.resource	= qt2410_cs89x0_resources,
};

/* LED */

static struct s3c24xx_led_platdata qt2410_pdata_led = {
	.gpio		= S3C2410_GPB(0),
	.flags		= S3C24XX_LEDF_ACTLOW | S3C24XX_LEDF_TRISTATE,
	.name		= "led",
	.def_trigger	= "timer",
};

static struct platform_device qt2410_led = {
	.name		= "s3c24xx_led",
	.id		= 0,
	.dev		= {
		.platform_data = &qt2410_pdata_led,
	},
};

/* SPI */

static struct spi_gpio_platform_data spi_gpio_cfg = {
	.sck		= S3C2410_GPG(7),
	.mosi		= S3C2410_GPG(6),
	.miso		= S3C2410_GPG(5),
};

static struct platform_device qt2410_spi = {
	.name		= "spi-gpio",
	.id		= 1,
	.dev.platform_data = &spi_gpio_cfg,
};

/* Board devices */

static struct platform_device *qt2410_devices[] __initdata = {
	&s3c_device_ohci,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c0,
	&s3c_device_iis,
	&s3c_device_sdi,
	&s3c_device_usbgadget,
	&qt2410_spi,
	&qt2410_cs89x0,
	&qt2410_led,
};

static struct mtd_partition __initdata qt2410_nand_part[] = {
	[0] = {
		.name	= "U-Boot",
		.size	= 0x30000,
		.offset	= 0,
	},
	[1] = {
		.name	= "U-Boot environment",
		.offset = 0x30000,
		.size	= 0x4000,
	},
	[2] = {
		.name	= "kernel",
		.offset = 0x34000,
		.size	= SZ_2M,
	},
	[3] = {
		.name	= "initrd",
		.offset	= 0x234000,
		.size	= SZ_4M,
	},
	[4] = {
		.name	= "jffs2",
		.offset = 0x634000,
		.size	= 0x39cc000,
	},
};

static struct s3c2410_nand_set __initdata qt2410_nand_sets[] = {
	[0] = {
		.name		= "NAND",
		.nr_chips	= 1,
		.nr_partitions	= ARRAY_SIZE(qt2410_nand_part),
		.partitions	= qt2410_nand_part,
	},
};

/* choose a set of timings which should suit most 512Mbit
 * chips and beyond.
 */

static struct s3c2410_platform_nand __initdata qt2410_nand_info = {
	.tacls		= 20,
	.twrph0		= 60,
	.twrph1		= 20,
	.nr_sets	= ARRAY_SIZE(qt2410_nand_sets),
	.sets		= qt2410_nand_sets,
	.ecc_mode       = NAND_ECC_SOFT,
};

/* UDC */

static struct s3c2410_udc_mach_info qt2410_udc_cfg = {
};

static char tft_type = 's';

static int __init qt2410_tft_setup(char *str)
{
	tft_type = str[0];
	return 1;
}

__setup("tft=", qt2410_tft_setup);

static void __init qt2410_map_io(void)
{
	s3c24xx_init_io(qt2410_iodesc, ARRAY_SIZE(qt2410_iodesc));
	s3c24xx_init_uarts(smdk2410_uartcfgs, ARRAY_SIZE(smdk2410_uartcfgs));
	samsung_set_timer_source(SAMSUNG_PWM3, SAMSUNG_PWM4);
}

static void __init qt2410_init_time(void)
{
	s3c2410_init_clocks(12000000);
	samsung_timer_init();
}

static void __init qt2410_machine_init(void)
{
	s3c_nand_set_platdata(&qt2410_nand_info);

	switch (tft_type) {
	case 'p': /* production */
		qt2410_fb_info.default_display = 1;
		break;
	case 'b': /* big */
		qt2410_fb_info.default_display = 0;
		break;
	case 's': /* small */
	default:
		qt2410_fb_info.default_display = 2;
		break;
	}
	s3c24xx_fb_set_platdata(&qt2410_fb_info);

	/* set initial state of the LED GPIO */
	WARN_ON(gpio_request_one(S3C2410_GPB(0), GPIOF_OUT_INIT_HIGH, NULL));
	gpio_free(S3C2410_GPB(0));

	s3c24xx_udc_set_platdata(&qt2410_udc_cfg);
	s3c_i2c0_set_platdata(NULL);

	WARN_ON(gpio_request(S3C2410_GPB(5), "spi cs"));
	gpio_direction_output(S3C2410_GPB(5), 1);

	platform_add_devices(qt2410_devices, ARRAY_SIZE(qt2410_devices));
	s3c_pm_init();
}

MACHINE_START(QT2410, "QT2410")
	.atag_offset	= 0x100,
	.map_io		= qt2410_map_io,
	.init_irq	= s3c2410_init_irq,
	.init_machine	= qt2410_machine_init,
	.init_time	= qt2410_init_time,
MACHINE_END
