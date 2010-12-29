/*
 *  linux/arch/arm/mach-pxa/littleton.c
 *
 *  Support for the Marvell Littleton Development Platform.
 *
 *  Author:	Jason Chagas (largely modified code)
 *  Created:	Nov 20, 2006
 *  Copyright:	(C) Copyright 2006 Marvell International Ltd.
 *
 *  2007-11-22  modified to align with latest kernel
 *              eric miao <eric.miao@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/smc91x.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/mfd/da903x.h>
#include <linux/i2c/max732x.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/pxa300.h>
#include <mach/pxafb.h>
#include <mach/mmc.h>
#include <plat/pxa27x_keypad.h>
#include <mach/littleton.h>
#include <plat/i2c.h>
#include <plat/pxa3xx_nand.h>

#include "generic.h"

#define GPIO_MMC1_CARD_DETECT	mfp_to_gpio(MFP_PIN_GPIO15)

/* Littleton MFP configurations */
static mfp_cfg_t littleton_mfp_cfg[] __initdata = {
	/* LCD */
	GPIO54_LCD_LDD_0,
	GPIO55_LCD_LDD_1,
	GPIO56_LCD_LDD_2,
	GPIO57_LCD_LDD_3,
	GPIO58_LCD_LDD_4,
	GPIO59_LCD_LDD_5,
	GPIO60_LCD_LDD_6,
	GPIO61_LCD_LDD_7,
	GPIO62_LCD_LDD_8,
	GPIO63_LCD_LDD_9,
	GPIO64_LCD_LDD_10,
	GPIO65_LCD_LDD_11,
	GPIO66_LCD_LDD_12,
	GPIO67_LCD_LDD_13,
	GPIO68_LCD_LDD_14,
	GPIO69_LCD_LDD_15,
	GPIO70_LCD_LDD_16,
	GPIO71_LCD_LDD_17,
	GPIO72_LCD_FCLK,
	GPIO73_LCD_LCLK,
	GPIO74_LCD_PCLK,
	GPIO75_LCD_BIAS,

	/* SSP2 */
	GPIO25_SSP2_SCLK,
	GPIO27_SSP2_TXD,
	GPIO17_GPIO,	/* SFRM as chip-select */

	/* Debug Ethernet */
	GPIO90_GPIO,

	/* Keypad */
	GPIO107_KP_DKIN_0,
	GPIO108_KP_DKIN_1,
	GPIO115_KP_MKIN_0,
	GPIO116_KP_MKIN_1,
	GPIO117_KP_MKIN_2,
	GPIO118_KP_MKIN_3,
	GPIO119_KP_MKIN_4,
	GPIO120_KP_MKIN_5,
	GPIO121_KP_MKOUT_0,
	GPIO122_KP_MKOUT_1,
	GPIO123_KP_MKOUT_2,
	GPIO124_KP_MKOUT_3,
	GPIO125_KP_MKOUT_4,

	/* MMC1 */
	GPIO3_MMC1_DAT0,
	GPIO4_MMC1_DAT1,
	GPIO5_MMC1_DAT2,
	GPIO6_MMC1_DAT3,
	GPIO7_MMC1_CLK,
	GPIO8_MMC1_CMD,
	GPIO15_GPIO, /* card detect */

	/* UART3 */
	GPIO107_UART3_CTS,
	GPIO108_UART3_RTS,
	GPIO109_UART3_TXD,
	GPIO110_UART3_RXD,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= (LITTLETON_ETH_PHYS + 0x300),
		.end	= (LITTLETON_ETH_PHYS + 0xfffff),
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_GPIO(mfp_to_gpio(MFP_PIN_GPIO90)),
		.end	= IRQ_GPIO(mfp_to_gpio(MFP_PIN_GPIO90)),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	}
};

static struct smc91x_platdata littleton_smc91x_info = {
	.flags	= SMC91X_USE_8BIT | SMC91X_USE_16BIT |
		  SMC91X_NOWAIT | SMC91X_USE_DMA,
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
	.dev		= {
		.platform_data = &littleton_smc91x_info,
	},
};

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static struct pxafb_mode_info tpo_tdo24mtea1_modes[] = {
	[0] = {
		/* VGA */
		.pixclock	= 38250,
		.xres		= 480,
		.yres		= 640,
		.bpp		= 16,
		.hsync_len	= 8,
		.left_margin	= 8,
		.right_margin	= 24,
		.vsync_len	= 2,
		.upper_margin	= 2,
		.lower_margin	= 4,
		.sync		= 0,
	},
	[1] = {
		/* QVGA */
		.pixclock	= 153000,
		.xres		= 240,
		.yres		= 320,
		.bpp		= 16,
		.hsync_len	= 8,
		.left_margin	= 8,
		.right_margin	= 88,
		.vsync_len	= 2,
		.upper_margin	= 2,
		.lower_margin	= 2,
		.sync		= 0,
	},
};

static struct pxafb_mach_info littleton_lcd_info = {
	.modes			= tpo_tdo24mtea1_modes,
	.num_modes		= 2,
	.lcd_conn		= LCD_COLOR_TFT_16BPP,
};

static void littleton_init_lcd(void)
{
	set_pxa_fb_info(&littleton_lcd_info);
}
#else
static inline void littleton_init_lcd(void) {};
#endif /* CONFIG_FB_PXA || CONFIG_FB_PXA_MODULE */

#if defined(CONFIG_SPI_PXA2XX) || defined(CONFIG_SPI_PXA2XX_MODULE)
static struct pxa2xx_spi_master littleton_spi_info = {
	.num_chipselect		= 1,
};

static struct pxa2xx_spi_chip littleton_tdo24m_chip = {
	.rx_threshold	= 1,
	.tx_threshold	= 1,
	.gpio_cs	= LITTLETON_GPIO_LCD_CS,
};

static struct spi_board_info littleton_spi_devices[] __initdata = {
	{
		.modalias	= "tdo24m",
		.max_speed_hz	= 1000000,
		.bus_num	= 2,
		.chip_select	= 0,
		.controller_data= &littleton_tdo24m_chip,
	},
};

static void __init littleton_init_spi(void)
{
	pxa2xx_set_spi_info(2, &littleton_spi_info);
	spi_register_board_info(ARRAY_AND_SIZE(littleton_spi_devices));
}
#else
static inline void littleton_init_spi(void) {}
#endif

#if defined(CONFIG_KEYBOARD_PXA27x) || defined(CONFIG_KEYBOARD_PXA27x_MODULE)
static unsigned int littleton_matrix_key_map[] = {
	/* KEY(row, col, key_code) */
	KEY(1, 3, KEY_0), KEY(0, 0, KEY_1), KEY(1, 0, KEY_2), KEY(2, 0, KEY_3),
	KEY(0, 1, KEY_4), KEY(1, 1, KEY_5), KEY(2, 1, KEY_6), KEY(0, 2, KEY_7),
	KEY(1, 2, KEY_8), KEY(2, 2, KEY_9),

	KEY(0, 3, KEY_KPASTERISK), 	/* * */
	KEY(2, 3, KEY_KPDOT), 		/* # */

	KEY(5, 4, KEY_ENTER),

	KEY(5, 0, KEY_UP),
	KEY(5, 1, KEY_DOWN),
	KEY(5, 2, KEY_LEFT),
	KEY(5, 3, KEY_RIGHT),
	KEY(3, 2, KEY_HOME),
	KEY(4, 1, KEY_END),
	KEY(3, 3, KEY_BACK),

	KEY(4, 0, KEY_SEND),
	KEY(4, 2, KEY_VOLUMEUP),
	KEY(4, 3, KEY_VOLUMEDOWN),

	KEY(3, 0, KEY_F22),	/* soft1 */
	KEY(3, 1, KEY_F23),	/* soft2 */
};

static struct pxa27x_keypad_platform_data littleton_keypad_info = {
	.matrix_key_rows	= 6,
	.matrix_key_cols	= 5,
	.matrix_key_map		= littleton_matrix_key_map,
	.matrix_key_map_size	= ARRAY_SIZE(littleton_matrix_key_map),

	.enable_rotary0		= 1,
	.rotary0_up_key		= KEY_UP,
	.rotary0_down_key	= KEY_DOWN,

	.debounce_interval	= 30,
};
static void __init littleton_init_keypad(void)
{
	pxa_set_keypad_info(&littleton_keypad_info);
}
#else
static inline void littleton_init_keypad(void) {}
#endif

#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
static struct pxamci_platform_data littleton_mci_platform_data = {
	.detect_delay_ms	= 200,
	.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34,
	.gpio_card_detect	= GPIO_MMC1_CARD_DETECT,
	.gpio_card_ro		= -1,
	.gpio_power		= -1,
};

static void __init littleton_init_mmc(void)
{
	pxa_set_mci_info(&littleton_mci_platform_data);
}
#else
static inline void littleton_init_mmc(void) {}
#endif

#if defined(CONFIG_MTD_NAND_PXA3xx) || defined(CONFIG_MTD_NAND_PXA3xx_MODULE)
static struct mtd_partition littleton_nand_partitions[] = {
	[0] = {
		.name        = "Bootloader",
		.offset      = 0,
		.size        = 0x060000,
		.mask_flags  = MTD_WRITEABLE, /* force read-only */
	},
	[1] = {
		.name        = "Kernel",
		.offset      = 0x060000,
		.size        = 0x200000,
		.mask_flags  = MTD_WRITEABLE, /* force read-only */
	},
	[2] = {
		.name        = "Filesystem",
		.offset      = 0x0260000,
		.size        = 0x3000000,     /* 48M - rootfs */
	},
	[3] = {
		.name        = "MassStorage",
		.offset      = 0x3260000,
		.size        = 0x3d40000,
	},
	[4] = {
		.name        = "BBT",
		.offset      = 0x6FA0000,
		.size        = 0x80000,
		.mask_flags  = MTD_WRITEABLE,  /* force read-only */
	},
	/* NOTE: we reserve some blocks at the end of the NAND flash for
	 * bad block management, and the max number of relocation blocks
	 * differs on different platforms. Please take care with it when
	 * defining the partition table.
	 */
};

static struct pxa3xx_nand_platform_data littleton_nand_info = {
	.enable_arbiter	= 1,
	.parts		= littleton_nand_partitions,
	.nr_parts	= ARRAY_SIZE(littleton_nand_partitions),
};

static void __init littleton_init_nand(void)
{
	pxa3xx_set_nand_info(&littleton_nand_info);
}
#else
static inline void littleton_init_nand(void) {}
#endif /* CONFIG_MTD_NAND_PXA3xx || CONFIG_MTD_NAND_PXA3xx_MODULE */

#if defined(CONFIG_I2C_PXA) || defined(CONFIG_I2C_PXA_MODULE)
static struct led_info littleton_da9034_leds[] = {
	[0] = {
		.name	= "littleton:keypad1",
		.flags	= DA9034_LED_RAMP,
	},
	[1] = {
		.name	= "littleton:keypad2",
		.flags	= DA9034_LED_RAMP,
	},
	[2] = {
		.name	= "littleton:vibra",
		.flags	= 0,
	},
};

static struct da9034_touch_pdata littleton_da9034_touch = {
	.x_inverted     = 1,
	.interval_ms    = 20,
};

static struct da903x_subdev_info littleton_da9034_subdevs[] = {
	{
		.name		= "da903x-led",
		.id		= DA9034_ID_LED_1,
		.platform_data	= &littleton_da9034_leds[0],
	}, {
		.name		= "da903x-led",
		.id		= DA9034_ID_LED_2,
		.platform_data	= &littleton_da9034_leds[1],
	}, {
		.name		= "da903x-led",
		.id		= DA9034_ID_VIBRA,
		.platform_data	= &littleton_da9034_leds[2],
	}, {
		.name		= "da903x-backlight",
		.id		= DA9034_ID_WLED,
	}, {
		.name		= "da9034-touch",
		.id		= DA9034_ID_TOUCH,
		.platform_data	= &littleton_da9034_touch,
	},
};

static struct da903x_platform_data littleton_da9034_info = {
	.num_subdevs	= ARRAY_SIZE(littleton_da9034_subdevs),
	.subdevs	= littleton_da9034_subdevs,
};

static struct max732x_platform_data littleton_max7320_info = {
	.gpio_base	= EXT0_GPIO_BASE,
};

static struct i2c_board_info littleton_i2c_info[] = {
	[0] = {
		.type		= "da9034",
		.addr		= 0x34,
		.platform_data	= &littleton_da9034_info,
		.irq		= gpio_to_irq(mfp_to_gpio(MFP_PIN_GPIO18)),
	},
	[1] = {
		.type		= "max7320",
		.addr		= 0x50,
		.platform_data	= &littleton_max7320_info,
	},
};

static void __init littleton_init_i2c(void)
{
	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, ARRAY_AND_SIZE(littleton_i2c_info));
}
#else
static inline void littleton_init_i2c(void) {}
#endif /* CONFIG_I2C_PXA || CONFIG_I2C_PXA_MODULE */

static void __init littleton_init(void)
{
	/* initialize MFP configurations */
	pxa3xx_mfp_config(ARRAY_AND_SIZE(littleton_mfp_cfg));

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	/*
	 * Note: we depend bootloader set the correct
	 * value to MSC register for SMC91x.
	 */
	platform_device_register(&smc91x_device);

	littleton_init_spi();
	littleton_init_i2c();
	littleton_init_mmc();
	littleton_init_lcd();
	littleton_init_keypad();
	littleton_init_nand();
}

MACHINE_START(LITTLETON, "Marvell Form Factor Development Platform (aka Littleton)")
	.boot_params	= 0xa0000100,
	.map_io		= pxa_map_io,
	.nr_irqs	= LITTLETON_NR_IRQS,
	.init_irq	= pxa3xx_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= littleton_init,
MACHINE_END
