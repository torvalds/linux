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
#include <linux/sysdev.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <asm/arch/regs-gpio.h>
#include <asm/arch/leds-gpio.h>
#include <asm/plat-s3c/regs-serial.h>
#include <asm/arch/fb.h>
#include <asm/plat-s3c/nand.h>
#include <asm/plat-s3c24xx/udc.h>
#include <asm/arch/spi.h>
#include <asm/arch/spi-gpio.h>

#include <asm/plat-s3c24xx/common-smdk.h>
#include <asm/plat-s3c24xx/devs.h>
#include <asm/plat-s3c24xx/cpu.h>
#include <asm/plat-s3c24xx/pm.h>

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
		.lcdcon1 = S3C2410_LCDCON1_TFT16BPP |
			   S3C2410_LCDCON1_TFT |
			   S3C2410_LCDCON1_CLKVAL(0x01), /* HCLK/4 */

		.lcdcon2 = S3C2410_LCDCON2_VBPD(18) |	/* 19 */
			   S3C2410_LCDCON2_LINEVAL(479) |
			   S3C2410_LCDCON2_VFPD(10) |	/* 11 */
			   S3C2410_LCDCON2_VSPW(14),	/* 15 */

		.lcdcon4 = S3C2410_LCDCON4_MVAL(0) |
			   S3C2410_LCDCON4_HSPW(95),	/* 96 */

		.lcdcon5 = S3C2410_LCDCON5_FRM565 |
			   S3C2410_LCDCON5_INVVLINE |
			   S3C2410_LCDCON5_INVVFRAME |
			   S3C2410_LCDCON5_PWREN |
			   S3C2410_LCDCON5_HWSWP,

		.type		= S3C2410_LCDCON1_TFT,
		.width		= 640,
		.height		= 480,

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
		.lcdcon1 = S3C2410_LCDCON1_TFT16BPP |
			   S3C2410_LCDCON1_TFT |
			   S3C2410_LCDCON1_CLKVAL(0x01), /* HCLK/4 */

		.lcdcon2 = S3C2410_LCDCON2_VBPD(1) |	/* 2 */
			   S3C2410_LCDCON2_LINEVAL(639) |/* 640 */
			   S3C2410_LCDCON2_VFPD(3) |	/* 4 */
			   S3C2410_LCDCON2_VSPW(1),	/* 2 */

		.lcdcon4 = S3C2410_LCDCON4_MVAL(0) |
			   S3C2410_LCDCON4_HSPW(7),	/* 8 */

		.lcdcon5 = S3C2410_LCDCON5_FRM565 |
			   S3C2410_LCDCON5_INVVLINE |
			   S3C2410_LCDCON5_INVVFRAME |
			   S3C2410_LCDCON5_PWREN |
			   S3C2410_LCDCON5_HWSWP,

		.type		= S3C2410_LCDCON1_TFT,
		.width		= 480,
		.height		= 640,
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
		.lcdcon1 = S3C2410_LCDCON1_TFT16BPP |
			   S3C2410_LCDCON1_TFT |
			   S3C2410_LCDCON1_CLKVAL(0x04),

		.lcdcon2 = S3C2410_LCDCON2_VBPD(1) |
			   S3C2410_LCDCON2_LINEVAL(319) |
			   S3C2410_LCDCON2_VFPD(6) |
			   S3C2410_LCDCON2_VSPW(3),

		.lcdcon4 = S3C2410_LCDCON4_MVAL(0) |
			   S3C2410_LCDCON4_HSPW(3),

		.lcdcon5 = S3C2410_LCDCON5_FRM565 |
			   S3C2410_LCDCON5_INVVLINE |
			   S3C2410_LCDCON5_INVVFRAME |
			   S3C2410_LCDCON5_PWREN |
			   S3C2410_LCDCON5_HWSWP,

		.type		= S3C2410_LCDCON1_TFT,
		.width		= 240,
		.height		= 320,
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
	[0] = {
		.start	= 0x19000000,
		.end	= 0x19000000 + 16,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_EINT9,
		.end	= IRQ_EINT9,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device qt2410_cs89x0 = {
	.name		= "cirrus-cs89x0",
	.num_resources	= ARRAY_SIZE(qt2410_cs89x0_resources),
	.resource	= qt2410_cs89x0_resources,
};

/* LED */

static struct s3c24xx_led_platdata qt2410_pdata_led = {
	.gpio		= S3C2410_GPB0,
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

static void spi_gpio_cs(struct s3c2410_spigpio_info *spi, int cs)
{
	switch (cs) {
	case BITBANG_CS_ACTIVE:
		s3c2410_gpio_setpin(S3C2410_GPB5, 0);
		break;
	case BITBANG_CS_INACTIVE:
		s3c2410_gpio_setpin(S3C2410_GPB5, 1);
		break;
	}
}

static struct s3c2410_spigpio_info spi_gpio_cfg = {
	.pin_clk	= S3C2410_GPG7,
	.pin_mosi	= S3C2410_GPG6,
	.pin_miso	= S3C2410_GPG5,
	.chip_select	= &spi_gpio_cs,
};


static struct platform_device qt2410_spi = {
	.name		  = "s3c24xx-spi-gpio",
	.id		  = 1,
	.dev = {
		.platform_data = &spi_gpio_cfg,
	},
};

/* Board devices */

static struct platform_device *qt2410_devices[] __initdata = {
	&s3c_device_usb,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
	&s3c_device_sdi,
	&s3c_device_usbgadget,
	&qt2410_spi,
	&qt2410_cs89x0,
	&qt2410_led,
};

static struct mtd_partition qt2410_nand_part[] = {
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

static struct s3c2410_nand_set qt2410_nand_sets[] = {
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

static struct s3c2410_platform_nand qt2410_nand_info = {
	.tacls		= 20,
	.twrph0		= 60,
	.twrph1		= 20,
	.nr_sets	= ARRAY_SIZE(qt2410_nand_sets),
	.sets		= qt2410_nand_sets,
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
	s3c24xx_init_clocks(12*1000*1000);
	s3c24xx_init_uarts(smdk2410_uartcfgs, ARRAY_SIZE(smdk2410_uartcfgs));
}

static void __init qt2410_machine_init(void)
{
	s3c_device_nand.dev.platform_data = &qt2410_nand_info;

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

	s3c2410_gpio_cfgpin(S3C2410_GPB0, S3C2410_GPIO_OUTPUT);
	s3c2410_gpio_setpin(S3C2410_GPB0, 1);

	s3c24xx_udc_set_platdata(&qt2410_udc_cfg);

	s3c2410_gpio_cfgpin(S3C2410_GPB5, S3C2410_GPIO_OUTPUT);

	platform_add_devices(qt2410_devices, ARRAY_SIZE(qt2410_devices));
	s3c2410_pm_init();
}

MACHINE_START(QT2410, "QT2410")
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= qt2410_map_io,
	.init_irq	= s3c24xx_init_irq,
	.init_machine	= qt2410_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END


