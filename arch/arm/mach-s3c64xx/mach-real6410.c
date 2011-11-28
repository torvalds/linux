/* linux/arch/arm/mach-s3c64xx/mach-real6410.c
 *
 * Copyright 2010 Darius Augulis <augulis.darius@gmail.com>
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/dm9000.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/types.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/map.h>
#include <mach/regs-gpio.h>
#include <mach/regs-modem.h>
#include <mach/regs-srom.h>

#include <plat/s3c6410.h>
#include <plat/adc.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/fb.h>
#include <plat/nand.h>
#include <plat/regs-serial.h>
#include <plat/ts.h>
#include <plat/regs-fb-v4.h>

#include <video/platform_lcd.h>

#define UCON S3C2410_UCON_DEFAULT
#define ULCON (S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB)
#define UFCON (S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE)

static struct s3c2410_uartcfg real6410_uartcfgs[] __initdata = {
	[0] = {
		.hwport	= 0,
		.flags	= 0,
		.ucon	= UCON,
		.ulcon	= ULCON,
		.ufcon	= UFCON,
	},
	[1] = {
		.hwport	= 1,
		.flags	= 0,
		.ucon	= UCON,
		.ulcon	= ULCON,
		.ufcon	= UFCON,
	},
	[2] = {
		.hwport	= 2,
		.flags	= 0,
		.ucon	= UCON,
		.ulcon	= ULCON,
		.ufcon	= UFCON,
	},
	[3] = {
		.hwport	= 3,
		.flags	= 0,
		.ucon	= UCON,
		.ulcon	= ULCON,
		.ufcon	= UFCON,
	},
};

/* DM9000AEP 10/100 ethernet controller */

static struct resource real6410_dm9k_resource[] = {
	[0] = {
		.start	= S3C64XX_PA_XM0CSN1,
		.end	= S3C64XX_PA_XM0CSN1 + 1,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start	= S3C64XX_PA_XM0CSN1 + 4,
		.end	= S3C64XX_PA_XM0CSN1 + 5,
		.flags	= IORESOURCE_MEM
	},
	[2] = {
		.start	= S3C_EINT(7),
		.end	= S3C_EINT(7),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL
	}
};

static struct dm9000_plat_data real6410_dm9k_pdata = {
	.flags		= (DM9000_PLATF_16BITONLY | DM9000_PLATF_NO_EEPROM),
};

static struct platform_device real6410_device_eth = {
	.name		= "dm9000",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(real6410_dm9k_resource),
	.resource	= real6410_dm9k_resource,
	.dev		= {
		.platform_data	= &real6410_dm9k_pdata,
	},
};

static struct s3c_fb_pd_win real6410_fb_win[] = {
	{
		.win_mode	= {	/* 4.3" 480x272 */
			.left_margin	= 3,
			.right_margin	= 2,
			.upper_margin	= 1,
			.lower_margin	= 1,
			.hsync_len	= 40,
			.vsync_len	= 1,
			.xres		= 480,
			.yres		= 272,
		},
		.max_bpp	= 32,
		.default_bpp	= 16,
	}, {
		.win_mode	= {	/* 7.0" 800x480 */
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
	},
};

static struct s3c_fb_platdata real6410_lcd_pdata __initdata = {
	.setup_gpio	= s3c64xx_fb_gpio_setup_24bpp,
	.win[0]		= &real6410_fb_win[0],
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
};

static struct mtd_partition real6410_nand_part[] = {
	[0] = {
		.name	= "uboot",
		.size	= SZ_1M,
		.offset	= 0,
	},
	[1] = {
		.name	= "kernel",
		.size	= SZ_2M,
		.offset	= SZ_1M,
	},
	[2] = {
		.name	= "rootfs",
		.size	= MTDPART_SIZ_FULL,
		.offset	= SZ_1M + SZ_2M,
	},
};

static struct s3c2410_nand_set real6410_nand_sets[] = {
	[0] = {
		.name		= "nand",
		.nr_chips	= 1,
		.nr_partitions	= ARRAY_SIZE(real6410_nand_part),
		.partitions	= real6410_nand_part,
	},
};

static struct s3c2410_platform_nand real6410_nand_info = {
	.tacls		= 25,
	.twrph0		= 55,
	.twrph1		= 40,
	.nr_sets	= ARRAY_SIZE(real6410_nand_sets),
	.sets		= real6410_nand_sets,
};

static struct platform_device *real6410_devices[] __initdata = {
	&real6410_device_eth,
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc1,
	&s3c_device_fb,
	&s3c_device_nand,
	&s3c_device_adc,
	&s3c_device_ts,
	&s3c_device_ohci,
};

static void __init real6410_map_io(void)
{
	u32 tmp;

	s3c64xx_init_io(NULL, 0);
	s3c24xx_init_clocks(12000000);
	s3c24xx_init_uarts(real6410_uartcfgs, ARRAY_SIZE(real6410_uartcfgs));

	/* set the LCD type */
	tmp = __raw_readl(S3C64XX_SPCON);
	tmp &= ~S3C64XX_SPCON_LCD_SEL_MASK;
	tmp |= S3C64XX_SPCON_LCD_SEL_RGB;
	__raw_writel(tmp, S3C64XX_SPCON);

	/* remove the LCD bypass */
	tmp = __raw_readl(S3C64XX_MODEM_MIFPCON);
	tmp &= ~MIFPCON_LCD_BYPASS;
	__raw_writel(tmp, S3C64XX_MODEM_MIFPCON);
}

/*
 * real6410_features string
 *
 * 0-9 LCD configuration
 *
 */
static char real6410_features_str[12] __initdata = "0";

static int __init real6410_features_setup(char *str)
{
	if (str)
		strlcpy(real6410_features_str, str,
			sizeof(real6410_features_str));
	return 1;
}

__setup("real6410=", real6410_features_setup);

#define FEATURE_SCREEN (1 << 0)

struct real6410_features_t {
	int done;
	int lcd_index;
};

static void real6410_parse_features(
		struct real6410_features_t *features,
		const char *features_str)
{
	const char *fp = features_str;

	features->done = 0;
	features->lcd_index = 0;

	while (*fp) {
		char f = *fp++;

		switch (f) {
		case '0'...'9':	/* tft screen */
			if (features->done & FEATURE_SCREEN) {
				printk(KERN_INFO "REAL6410: '%c' ignored, "
					"screen type already set\n", f);
			} else {
				int li = f - '0';
				if (li >= ARRAY_SIZE(real6410_fb_win))
					printk(KERN_INFO "REAL6410: '%c' out "
						"of range LCD mode\n", f);
				else {
					features->lcd_index = li;
				}
			}
			features->done |= FEATURE_SCREEN;
			break;
		}
	}
}

static void __init real6410_machine_init(void)
{
	u32 cs1;
	struct real6410_features_t features = { 0 };

	printk(KERN_INFO "REAL6410: Option string real6410=%s\n",
			real6410_features_str);

	/* Parse the feature string */
	real6410_parse_features(&features, real6410_features_str);

	real6410_lcd_pdata.win[0] = &real6410_fb_win[features.lcd_index];

	printk(KERN_INFO "REAL6410: selected LCD display is %dx%d\n",
		real6410_lcd_pdata.win[0]->win_mode.xres,
		real6410_lcd_pdata.win[0]->win_mode.yres);

	s3c_fb_set_platdata(&real6410_lcd_pdata);
	s3c_nand_set_platdata(&real6410_nand_info);
	s3c24xx_ts_set_platdata(NULL);

	/* configure nCS1 width to 16 bits */

	cs1 = __raw_readl(S3C64XX_SROM_BW) &
		~(S3C64XX_SROM_BW__CS_MASK << S3C64XX_SROM_BW__NCS1__SHIFT);
	cs1 |= ((1 << S3C64XX_SROM_BW__DATAWIDTH__SHIFT) |
		(1 << S3C64XX_SROM_BW__WAITENABLE__SHIFT) |
		(1 << S3C64XX_SROM_BW__BYTEENABLE__SHIFT)) <<
			S3C64XX_SROM_BW__NCS1__SHIFT;
	__raw_writel(cs1, S3C64XX_SROM_BW);

	/* set timing for nCS1 suitable for ethernet chip */

	__raw_writel((0 << S3C64XX_SROM_BCX__PMC__SHIFT) |
		(6 << S3C64XX_SROM_BCX__TACP__SHIFT) |
		(4 << S3C64XX_SROM_BCX__TCAH__SHIFT) |
		(1 << S3C64XX_SROM_BCX__TCOH__SHIFT) |
		(13 << S3C64XX_SROM_BCX__TACC__SHIFT) |
		(4 << S3C64XX_SROM_BCX__TCOS__SHIFT) |
		(0 << S3C64XX_SROM_BCX__TACS__SHIFT), S3C64XX_SROM_BC1);

	gpio_request(S3C64XX_GPF(15), "LCD power");

	platform_add_devices(real6410_devices, ARRAY_SIZE(real6410_devices));
}

MACHINE_START(REAL6410, "REAL6410")
	/* Maintainer: Darius Augulis <augulis.darius@gmail.com> */
	.atag_offset	= 0x100,

	.init_irq	= s3c6410_init_irq,
	.map_io		= real6410_map_io,
	.init_machine	= real6410_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END
