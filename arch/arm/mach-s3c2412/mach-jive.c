/* linux/arch/arm/mach-s3c2410/mach-jive.c
 *
 * Copyright 2007 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * http://armlinux.simtec.co.uk/
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
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/syscore_ops.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>

#include <video/ili9320.h>

#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <plat/regs-serial.h>
#include <plat/nand.h>
#include <plat/iic.h>

#include <mach/regs-power.h>
#include <mach/regs-gpio.h>
#include <mach/regs-mem.h>
#include <mach/regs-lcd.h>
#include <mach/fb.h>

#include <asm/mach-types.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <plat/gpio-cfg.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/pm.h>
#include <plat/udc.h>

static struct map_desc jive_iodesc[] __initdata = {
};

#define UCON S3C2410_UCON_DEFAULT
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg jive_uartcfgs[] = {
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

/* Jive flash assignment
 *
 * 0x00000000-0x00028000 : uboot
 * 0x00028000-0x0002c000 : uboot env
 * 0x0002c000-0x00030000 : spare
 * 0x00030000-0x00200000 : zimage A
 * 0x00200000-0x01600000 : cramfs A
 * 0x01600000-0x017d0000 : zimage B
 * 0x017d0000-0x02bd0000 : cramfs B
 * 0x02bd0000-0x03fd0000 : yaffs
 */
static struct mtd_partition __initdata jive_imageA_nand_part[] = {

#ifdef CONFIG_MACH_JIVE_SHOW_BOOTLOADER
	/* Don't allow access to the bootloader from linux */
	{
		.name           = "uboot",
		.offset         = 0,
		.size           = (160 * SZ_1K),
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	},

	/* spare */
        {
                .name           = "spare",
                .offset         = (176 * SZ_1K),
                .size           = (16 * SZ_1K),
        },
#endif

	/* booted images */
        {
		.name		= "kernel (ro)",
		.offset		= (192 * SZ_1K),
		.size		= (SZ_2M) - (192 * SZ_1K),
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
        }, {
                .name           = "root (ro)",
                .offset         = (SZ_2M),
                .size           = (20 * SZ_1M),
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
        },

	/* yaffs */
	{
		.name		= "yaffs",
		.offset		= (44 * SZ_1M),
		.size		= (20 * SZ_1M),
	},

	/* bootloader environment */
	{
                .name		= "env",
		.offset		= (160 * SZ_1K),
		.size		= (16 * SZ_1K),
	},

	/* upgrade images */
        {
		.name		= "zimage",
		.offset		= (22 * SZ_1M),
		.size		= (2 * SZ_1M) - (192 * SZ_1K),
        }, {
		.name		= "cramfs",
		.offset		= (24 * SZ_1M) - (192*SZ_1K),
		.size		= (20 * SZ_1M),
        },
};

static struct mtd_partition __initdata jive_imageB_nand_part[] = {

#ifdef CONFIG_MACH_JIVE_SHOW_BOOTLOADER
	/* Don't allow access to the bootloader from linux */
	{
		.name           = "uboot",
		.offset         = 0,
		.size           = (160 * SZ_1K),
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	},

	/* spare */
        {
                .name           = "spare",
                .offset         = (176 * SZ_1K),
                .size           = (16 * SZ_1K),
        },
#endif

	/* booted images */
        {
		.name           = "kernel (ro)",
		.offset         = (22 * SZ_1M),
		.size           = (2 * SZ_1M) - (192 * SZ_1K),
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
        },
	{
		.name		= "root (ro)",
		.offset		= (24 * SZ_1M) - (192 * SZ_1K),
                .size		= (20 * SZ_1M),
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	},

	/* yaffs */
	{
		.name		= "yaffs",
		.offset		= (44 * SZ_1M),
		.size		= (20 * SZ_1M),
        },

	/* bootloader environment */
	{
		.name		= "env",
		.offset		= (160 * SZ_1K),
		.size		= (16 * SZ_1K),
	},

	/* upgrade images */
	{
		.name		= "zimage",
		.offset		= (192 * SZ_1K),
		.size		= (2 * SZ_1M) - (192 * SZ_1K),
        }, {
		.name		= "cramfs",
		.offset		= (2 * SZ_1M),
		.size		= (20 * SZ_1M),
        },
};

static struct s3c2410_nand_set __initdata jive_nand_sets[] = {
	[0] = {
		.name           = "flash",
		.nr_chips       = 1,
		.nr_partitions  = ARRAY_SIZE(jive_imageA_nand_part),
		.partitions     = jive_imageA_nand_part,
	},
};

static struct s3c2410_platform_nand __initdata jive_nand_info = {
	/* set taken from osiris nand timings, possibly still conservative */
	.tacls		= 30,
	.twrph0		= 55,
	.twrph1		= 40,
	.sets		= jive_nand_sets,
	.nr_sets	= ARRAY_SIZE(jive_nand_sets),
};

static int __init jive_mtdset(char *options)
{
	struct s3c2410_nand_set *nand = &jive_nand_sets[0];
	unsigned long set;

	if (options == NULL || options[0] == '\0')
		return 0;

	if (strict_strtoul(options, 10, &set)) {
		printk(KERN_ERR "failed to parse mtdset=%s\n", options);
		return 0;
	}

	switch (set) {
	case 1:
		nand->nr_partitions = ARRAY_SIZE(jive_imageB_nand_part);
		nand->partitions = jive_imageB_nand_part;
	case 0:
		/* this is already setup in the nand info */
		break;
	default:
		printk(KERN_ERR "Unknown mtd set %ld specified,"
		       "using default.", set);
	}

	return 0;
}

/* parse the mtdset= option given to the kernel command line */
__setup("mtdset=", jive_mtdset);

/* LCD timing and setup */

#define LCD_XRES	 (240)
#define LCD_YRES	 (320)
#define LCD_LEFT_MARGIN  (12)
#define LCD_RIGHT_MARGIN (12)
#define LCD_LOWER_MARGIN (12)
#define LCD_UPPER_MARGIN (12)
#define LCD_VSYNC	 (2)
#define LCD_HSYNC	 (2)

#define LCD_REFRESH	 (60)

#define LCD_HTOT (LCD_HSYNC + LCD_LEFT_MARGIN + LCD_XRES + LCD_RIGHT_MARGIN)
#define LCD_VTOT (LCD_VSYNC + LCD_LOWER_MARGIN + LCD_YRES + LCD_UPPER_MARGIN)

static struct s3c2410fb_display jive_vgg2432a4_display[] = {
	[0] = {
		.width		= LCD_XRES,
		.height		= LCD_YRES,
		.xres		= LCD_XRES,
		.yres		= LCD_YRES,
		.left_margin	= LCD_LEFT_MARGIN,
		.right_margin	= LCD_RIGHT_MARGIN,
		.upper_margin	= LCD_UPPER_MARGIN,
		.lower_margin	= LCD_LOWER_MARGIN,
		.hsync_len	= LCD_HSYNC,
		.vsync_len	= LCD_VSYNC,

		.pixclock	= (1000000000000LL /
				   (LCD_REFRESH * LCD_HTOT * LCD_VTOT)),

		.bpp		= 16,
		.type		= (S3C2410_LCDCON1_TFT16BPP |
				   S3C2410_LCDCON1_TFT),

		.lcdcon5	= (S3C2410_LCDCON5_FRM565 |
				   S3C2410_LCDCON5_INVVLINE |
				   S3C2410_LCDCON5_INVVFRAME |
				   S3C2410_LCDCON5_INVVDEN |
				   S3C2410_LCDCON5_PWREN),
	},
};

/* todo - put into gpio header */

#define S3C2410_GPCCON_MASK(x)	(3 << ((x) * 2))
#define S3C2410_GPDCON_MASK(x)	(3 << ((x) * 2))

static struct s3c2410fb_mach_info jive_lcd_config = {
	.displays	 = jive_vgg2432a4_display,
	.num_displays	 = ARRAY_SIZE(jive_vgg2432a4_display),
	.default_display = 0,

	/* Enable VD[2..7], VD[10..15], VD[18..23] and VCLK, syncs, VDEN
	 * and disable the pull down resistors on pins we are using for LCD
	 * data. */

	.gpcup		= (0xf << 1) | (0x3f << 10),

	.gpccon		= (S3C2410_GPC1_VCLK   | S3C2410_GPC2_VLINE |
			   S3C2410_GPC3_VFRAME | S3C2410_GPC4_VM |
			   S3C2410_GPC10_VD2   | S3C2410_GPC11_VD3 |
			   S3C2410_GPC12_VD4   | S3C2410_GPC13_VD5 |
			   S3C2410_GPC14_VD6   | S3C2410_GPC15_VD7),

	.gpccon_mask	= (S3C2410_GPCCON_MASK(1)  | S3C2410_GPCCON_MASK(2)  |
			   S3C2410_GPCCON_MASK(3)  | S3C2410_GPCCON_MASK(4)  |
			   S3C2410_GPCCON_MASK(10) | S3C2410_GPCCON_MASK(11) |
			   S3C2410_GPCCON_MASK(12) | S3C2410_GPCCON_MASK(13) |
			   S3C2410_GPCCON_MASK(14) | S3C2410_GPCCON_MASK(15)),

	.gpdup		= (0x3f << 2) | (0x3f << 10),

	.gpdcon		= (S3C2410_GPD2_VD10  | S3C2410_GPD3_VD11 |
			   S3C2410_GPD4_VD12  | S3C2410_GPD5_VD13 |
			   S3C2410_GPD6_VD14  | S3C2410_GPD7_VD15 |
			   S3C2410_GPD10_VD18 | S3C2410_GPD11_VD19 |
			   S3C2410_GPD12_VD20 | S3C2410_GPD13_VD21 |
			   S3C2410_GPD14_VD22 | S3C2410_GPD15_VD23),

	.gpdcon_mask	= (S3C2410_GPDCON_MASK(2)  | S3C2410_GPDCON_MASK(3) |
			   S3C2410_GPDCON_MASK(4)  | S3C2410_GPDCON_MASK(5) |
			   S3C2410_GPDCON_MASK(6)  | S3C2410_GPDCON_MASK(7) |
			   S3C2410_GPDCON_MASK(10) | S3C2410_GPDCON_MASK(11)|
			   S3C2410_GPDCON_MASK(12) | S3C2410_GPDCON_MASK(13)|
			   S3C2410_GPDCON_MASK(14) | S3C2410_GPDCON_MASK(15)),
};

/* ILI9320 support. */

static void jive_lcm_reset(unsigned int set)
{
	printk(KERN_DEBUG "%s(%d)\n", __func__, set);

	gpio_set_value(S3C2410_GPG(13), set);
}

#undef LCD_UPPER_MARGIN
#define LCD_UPPER_MARGIN 2

static struct ili9320_platdata jive_lcm_config = {
	.hsize		= LCD_XRES,
	.vsize		= LCD_YRES,

	.reset		= jive_lcm_reset,
	.suspend	= ILI9320_SUSPEND_DEEP,

	.entry_mode	= ILI9320_ENTRYMODE_ID(3) | ILI9320_ENTRYMODE_BGR,
	.display2	= (ILI9320_DISPLAY2_FP(LCD_UPPER_MARGIN) |
			   ILI9320_DISPLAY2_BP(LCD_LOWER_MARGIN)),
	.display3	= 0x0,
	.display4	= 0x0,
	.rgb_if1	= (ILI9320_RGBIF1_RIM_RGB18 |
			   ILI9320_RGBIF1_RM | ILI9320_RGBIF1_CLK_RGBIF),
	.rgb_if2	= ILI9320_RGBIF2_DPL,
	.interface2	= 0x0,
	.interface3	= 0x3,
	.interface4	= (ILI9320_INTERFACE4_RTNE(16) |
			   ILI9320_INTERFACE4_DIVE(1)),
	.interface5	= 0x0,
	.interface6	= 0x0,
};

/* LCD SPI support */

static struct spi_gpio_platform_data jive_lcd_spi = {
	.sck		= S3C2410_GPG(8),
	.mosi		= S3C2410_GPB(8),
	.miso		= SPI_GPIO_NO_MISO,
};

static struct platform_device jive_device_lcdspi = {
	.name		= "spi-gpio",
	.id		= 1,
	.dev.platform_data = &jive_lcd_spi,
};


/* WM8750 audio code SPI definition */

static struct spi_gpio_platform_data jive_wm8750_spi = {
	.sck		= S3C2410_GPB(4),
	.mosi		= S3C2410_GPB(9),
	.miso		= SPI_GPIO_NO_MISO,
};

static struct platform_device jive_device_wm8750 = {
	.name		= "spi-gpio",
	.id		= 2,
	.dev.platform_data = &jive_wm8750_spi,
};

/* JIVE SPI devices. */

static struct spi_board_info __initdata jive_spi_devs[] = {
	[0] = {
		.modalias	= "VGG2432A4",
		.bus_num	= 1,
		.chip_select	= 0,
		.mode		= SPI_MODE_3,	/* CPOL=1, CPHA=1 */
		.max_speed_hz	= 100000,
		.platform_data	= &jive_lcm_config,
		.controller_data = (void *)S3C2410_GPB(7),
	}, {
		.modalias	= "WM8750",
		.bus_num	= 2,
		.chip_select	= 0,
		.mode		= SPI_MODE_0,	/* CPOL=0, CPHA=0 */
		.max_speed_hz	= 100000,
		.controller_data = (void *)S3C2410_GPH(10),
	},
};

/* I2C bus and device configuration. */

static struct s3c2410_platform_i2c jive_i2c_cfg __initdata = {
	.frequency	= 80 * 1000,
	.flags		= S3C_IICFLG_FILTER,
	.sda_delay	= 2,
};

static struct i2c_board_info jive_i2c_devs[] __initdata = {
	[0] = {
		I2C_BOARD_INFO("lis302dl", 0x1c),
		.irq	= IRQ_EINT14,
	},
};

/* The platform devices being used. */

static struct platform_device *jive_devices[] __initdata = {
	&s3c_device_ohci,
	&s3c_device_rtc,
	&s3c_device_wdt,
	&s3c_device_i2c0,
	&s3c_device_lcd,
	&jive_device_lcdspi,
	&jive_device_wm8750,
	&s3c_device_nand,
	&s3c_device_usbgadget,
};

static struct s3c2410_udc_mach_info jive_udc_cfg __initdata = {
	.vbus_pin	= S3C2410_GPG(1),		/* detect is on GPG1 */
};

/* Jive power management device */

#ifdef CONFIG_PM
static int jive_pm_suspend(void)
{
	/* Write the magic value u-boot uses to check for resume into
	 * the INFORM0 register, and ensure INFORM1 is set to the
	 * correct address to resume from. */

	__raw_writel(0x2BED, S3C2412_INFORM0);
	__raw_writel(virt_to_phys(s3c_cpu_resume), S3C2412_INFORM1);

	return 0;
}

static void jive_pm_resume(void)
{
	__raw_writel(0x0, S3C2412_INFORM0);
}

#else
#define jive_pm_suspend NULL
#define jive_pm_resume NULL
#endif

static struct syscore_ops jive_pm_syscore_ops = {
	.suspend	= jive_pm_suspend,
	.resume		= jive_pm_resume,
};

static void __init jive_map_io(void)
{
	s3c24xx_init_io(jive_iodesc, ARRAY_SIZE(jive_iodesc));
	s3c24xx_init_clocks(12000000);
	s3c24xx_init_uarts(jive_uartcfgs, ARRAY_SIZE(jive_uartcfgs));
}

static void jive_power_off(void)
{
	printk(KERN_INFO "powering system down...\n");

	s3c2410_gpio_setpin(S3C2410_GPC(5), 1);
	s3c_gpio_cfgpin(S3C2410_GPC(5), S3C2410_GPIO_OUTPUT);
}

static void __init jive_machine_init(void)
{
	/* register system core operations for managing low level suspend */

	register_syscore_ops(&jive_pm_syscore_ops);

	/* write our sleep configurations for the IO. Pull down all unused
	 * IO, ensure that we have turned off all peripherals we do not
	 * need, and configure the ones we do need. */

	/* Port B sleep */

	__raw_writel(S3C2412_SLPCON_IN(0)   |
		     S3C2412_SLPCON_PULL(1) |
		     S3C2412_SLPCON_HIGH(2) |
		     S3C2412_SLPCON_PULL(3) |
		     S3C2412_SLPCON_PULL(4) |
		     S3C2412_SLPCON_PULL(5) |
		     S3C2412_SLPCON_PULL(6) |
		     S3C2412_SLPCON_HIGH(7) |
		     S3C2412_SLPCON_PULL(8) |
		     S3C2412_SLPCON_PULL(9) |
		     S3C2412_SLPCON_PULL(10), S3C2412_GPBSLPCON);

	/* Port C sleep */

	__raw_writel(S3C2412_SLPCON_PULL(0) |
		     S3C2412_SLPCON_PULL(1) |
		     S3C2412_SLPCON_PULL(2) |
		     S3C2412_SLPCON_PULL(3) |
		     S3C2412_SLPCON_PULL(4) |
		     S3C2412_SLPCON_PULL(5) |
		     S3C2412_SLPCON_LOW(6)  |
		     S3C2412_SLPCON_PULL(6) |
		     S3C2412_SLPCON_PULL(7) |
		     S3C2412_SLPCON_PULL(8) |
		     S3C2412_SLPCON_PULL(9) |
		     S3C2412_SLPCON_PULL(10) |
		     S3C2412_SLPCON_PULL(11) |
		     S3C2412_SLPCON_PULL(12) |
		     S3C2412_SLPCON_PULL(13) |
		     S3C2412_SLPCON_PULL(14) |
		     S3C2412_SLPCON_PULL(15), S3C2412_GPCSLPCON);

	/* Port D sleep */

	__raw_writel(S3C2412_SLPCON_ALL_PULL, S3C2412_GPDSLPCON);

	/* Port F sleep */

	__raw_writel(S3C2412_SLPCON_LOW(0)  |
		     S3C2412_SLPCON_LOW(1)  |
		     S3C2412_SLPCON_LOW(2)  |
		     S3C2412_SLPCON_EINT(3) |
		     S3C2412_SLPCON_EINT(4) |
		     S3C2412_SLPCON_EINT(5) |
		     S3C2412_SLPCON_EINT(6) |
		     S3C2412_SLPCON_EINT(7), S3C2412_GPFSLPCON);

	/* Port G sleep */

	__raw_writel(S3C2412_SLPCON_IN(0)    |
		     S3C2412_SLPCON_IN(1)    |
		     S3C2412_SLPCON_IN(2)    |
		     S3C2412_SLPCON_IN(3)    |
		     S3C2412_SLPCON_IN(4)    |
		     S3C2412_SLPCON_IN(5)    |
		     S3C2412_SLPCON_IN(6)    |
		     S3C2412_SLPCON_IN(7)    |
		     S3C2412_SLPCON_PULL(8)  |
		     S3C2412_SLPCON_PULL(9)  |
		     S3C2412_SLPCON_IN(10)   |
		     S3C2412_SLPCON_PULL(11) |
		     S3C2412_SLPCON_PULL(12) |
		     S3C2412_SLPCON_PULL(13) |
		     S3C2412_SLPCON_IN(14)   |
		     S3C2412_SLPCON_PULL(15), S3C2412_GPGSLPCON);

	/* Port H sleep */

	__raw_writel(S3C2412_SLPCON_PULL(0) |
		     S3C2412_SLPCON_PULL(1) |
		     S3C2412_SLPCON_PULL(2) |
		     S3C2412_SLPCON_PULL(3) |
		     S3C2412_SLPCON_PULL(4) |
		     S3C2412_SLPCON_PULL(5) |
		     S3C2412_SLPCON_PULL(6) |
		     S3C2412_SLPCON_IN(7)   |
		     S3C2412_SLPCON_IN(8)   |
		     S3C2412_SLPCON_PULL(9) |
		     S3C2412_SLPCON_IN(10), S3C2412_GPHSLPCON);

	/* initialise the power management now we've setup everything. */

	s3c_pm_init();

	/** TODO - check that this is after the cmdline option! */
	s3c_nand_set_platdata(&jive_nand_info);

	/* initialise the spi */

	gpio_request(S3C2410_GPG(13), "lcm reset");
	gpio_direction_output(S3C2410_GPG(13), 0);

	gpio_request(S3C2410_GPB(7), "jive spi");
	gpio_direction_output(S3C2410_GPB(7), 1);

	s3c2410_gpio_setpin(S3C2410_GPB(6), 0);
	s3c_gpio_cfgpin(S3C2410_GPB(6), S3C2410_GPIO_OUTPUT);

	s3c2410_gpio_setpin(S3C2410_GPG(8), 1);
	s3c_gpio_cfgpin(S3C2410_GPG(8), S3C2410_GPIO_OUTPUT);

	/* initialise the WM8750 spi */

	gpio_request(S3C2410_GPH(10), "jive wm8750 spi");
	gpio_direction_output(S3C2410_GPH(10), 1);

	/* Turn off suspend on both USB ports, and switch the
	 * selectable USB port to USB device mode. */

	s3c2410_modify_misccr(S3C2410_MISCCR_USBHOST |
			      S3C2410_MISCCR_USBSUSPND0 |
			      S3C2410_MISCCR_USBSUSPND1, 0x0);

	s3c24xx_udc_set_platdata(&jive_udc_cfg);
	s3c24xx_fb_set_platdata(&jive_lcd_config);

	spi_register_board_info(jive_spi_devs, ARRAY_SIZE(jive_spi_devs));

	s3c_i2c0_set_platdata(&jive_i2c_cfg);
	i2c_register_board_info(0, jive_i2c_devs, ARRAY_SIZE(jive_i2c_devs));

	pm_power_off = jive_power_off;

	platform_add_devices(jive_devices, ARRAY_SIZE(jive_devices));
}

MACHINE_START(JIVE, "JIVE")
	/* Maintainer: Ben Dooks <ben-linux@fluff.org> */
	.atag_offset	= 0x100,

	.init_irq	= s3c24xx_init_irq,
	.map_io		= jive_map_io,
	.init_machine	= jive_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END
