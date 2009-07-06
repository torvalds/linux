/*
 * linux/arch/arm/mach-pxa/cm-x300.c
 *
 * Support for the CompuLab CM-X300 modules
 *
 * Copyright (C) 2008 CompuLab Ltd.
 *
 * Mike Rapoport <mike@compulab.co.il>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <linux/gpio.h>
#include <linux/dm9000.h>
#include <linux/leds.h>
#include <linux/rtc-v3020.h>

#include <linux/i2c.h>
#include <linux/i2c/pca953x.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/setup.h>

#include <mach/pxa300.h>
#include <mach/pxafb.h>
#include <mach/mmc.h>
#include <mach/ohci.h>
#include <plat/i2c.h>
#include <mach/pxa3xx_nand.h>

#include <asm/mach/map.h>

#include "generic.h"

#define CM_X300_ETH_PHYS	0x08000010

#define GPIO82_MMC2_IRQ		(82)
#define GPIO85_MMC2_WP		(85)

#define	CM_X300_MMC2_IRQ	IRQ_GPIO(GPIO82_MMC2_IRQ)

#define GPIO95_RTC_CS		(95)
#define GPIO96_RTC_WR		(96)
#define GPIO97_RTC_RD		(97)
#define GPIO98_RTC_IO		(98)

static mfp_cfg_t cm_x300_mfp_cfg[] __initdata = {
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
	GPIO72_LCD_FCLK,
	GPIO73_LCD_LCLK,
	GPIO74_LCD_PCLK,
	GPIO75_LCD_BIAS,

	/* BTUART */
	GPIO111_UART2_RTS,
	GPIO112_UART2_RXD | MFP_LPM_EDGE_FALL,
	GPIO113_UART2_TXD,
	GPIO114_UART2_CTS | MFP_LPM_EDGE_BOTH,

	/* STUART */
	GPIO109_UART3_TXD,
	GPIO110_UART3_RXD | MFP_LPM_EDGE_FALL,

	/* AC97 */
	GPIO23_AC97_nACRESET,
	GPIO24_AC97_SYSCLK,
	GPIO29_AC97_BITCLK,
	GPIO25_AC97_SDATA_IN_0,
	GPIO27_AC97_SDATA_OUT,
	GPIO28_AC97_SYNC,

	/* Keypad */
	GPIO115_KP_MKIN_0 | MFP_LPM_EDGE_BOTH,
	GPIO116_KP_MKIN_1 | MFP_LPM_EDGE_BOTH,
	GPIO117_KP_MKIN_2 | MFP_LPM_EDGE_BOTH,
	GPIO118_KP_MKIN_3 | MFP_LPM_EDGE_BOTH,
	GPIO119_KP_MKIN_4 | MFP_LPM_EDGE_BOTH,
	GPIO120_KP_MKIN_5 | MFP_LPM_EDGE_BOTH,
	GPIO2_2_KP_MKIN_6 | MFP_LPM_EDGE_BOTH,
	GPIO3_2_KP_MKIN_7 | MFP_LPM_EDGE_BOTH,
	GPIO121_KP_MKOUT_0,
	GPIO122_KP_MKOUT_1,
	GPIO123_KP_MKOUT_2,
	GPIO124_KP_MKOUT_3,
	GPIO125_KP_MKOUT_4,
	GPIO4_2_KP_MKOUT_5,

	/* MMC1 */
	GPIO3_MMC1_DAT0,
	GPIO4_MMC1_DAT1 | MFP_LPM_EDGE_BOTH,
	GPIO5_MMC1_DAT2,
	GPIO6_MMC1_DAT3,
	GPIO7_MMC1_CLK,
	GPIO8_MMC1_CMD,	/* CMD0 for slot 0 */

	/* MMC2 */
	GPIO9_MMC2_DAT0,
	GPIO10_MMC2_DAT1 | MFP_LPM_EDGE_BOTH,
	GPIO11_MMC2_DAT2,
	GPIO12_MMC2_DAT3,
	GPIO13_MMC2_CLK,
	GPIO14_MMC2_CMD,

	/* FFUART */
	GPIO30_UART1_RXD | MFP_LPM_EDGE_FALL,
	GPIO31_UART1_TXD,
	GPIO32_UART1_CTS,
	GPIO37_UART1_RTS,
	GPIO33_UART1_DCD,
	GPIO34_UART1_DSR | MFP_LPM_EDGE_FALL,
	GPIO35_UART1_RI,
	GPIO36_UART1_DTR,

	/* GPIOs */
	GPIO79_GPIO,			/* LED */
	GPIO82_GPIO | MFP_PULL_HIGH,	/* MMC CD */
	GPIO85_GPIO,			/* MMC WP */
	GPIO99_GPIO,			/* Ethernet IRQ */

	/* RTC GPIOs */
	GPIO95_GPIO,			/* RTC CS */
	GPIO96_GPIO,			/* RTC WR */
	GPIO97_GPIO,			/* RTC RD */
	GPIO98_GPIO,			/* RTC IO */

	/* Standard I2C */
	GPIO21_I2C_SCL,
	GPIO22_I2C_SDA,
};

#if defined(CONFIG_DM9000) || defined(CONFIG_DM9000_MODULE)
static struct resource dm9000_resources[] = {
	[0] = {
		.start	= CM_X300_ETH_PHYS,
		.end	= CM_X300_ETH_PHYS + 0x3,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= CM_X300_ETH_PHYS + 0x4,
		.end	= CM_X300_ETH_PHYS + 0x4 + 500,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= IRQ_GPIO(mfp_to_gpio(MFP_PIN_GPIO99)),
		.end	= IRQ_GPIO(mfp_to_gpio(MFP_PIN_GPIO99)),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct dm9000_plat_data cm_x300_dm9000_platdata = {
	.flags		= DM9000_PLATF_16BITONLY | DM9000_PLATF_NO_EEPROM,
};

static struct platform_device dm9000_device = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(dm9000_resources),
	.resource	= dm9000_resources,
	.dev		= {
		.platform_data = &cm_x300_dm9000_platdata,
	}

};

static void __init cm_x300_init_dm9000(void)
{
	platform_device_register(&dm9000_device);
}
#else
static inline void cm_x300_init_dm9000(void) {}
#endif

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static struct pxafb_mode_info cm_x300_lcd_modes[] = {
	[0] = {
		.pixclock	= 38000,
		.bpp		= 16,
		.xres		= 480,
		.yres		= 640,
		.hsync_len	= 8,
		.vsync_len	= 2,
		.left_margin	= 8,
		.upper_margin	= 0,
		.right_margin	= 24,
		.lower_margin	= 4,
		.cmap_greyscale	= 0,
	},
	[1] = {
		.pixclock	= 153800,
		.bpp		= 16,
		.xres		= 240,
		.yres		= 320,
		.hsync_len	= 8,
		.vsync_len	= 2,
		.left_margin	= 8,
		.upper_margin	= 2,
		.right_margin	= 88,
		.lower_margin	= 2,
		.cmap_greyscale	= 0,
	},
};

static struct pxafb_mach_info cm_x300_lcd = {
	.modes			= cm_x300_lcd_modes,
	.num_modes		= 2,
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL,
};

static void __init cm_x300_init_lcd(void)
{
	set_pxa_fb_info(&cm_x300_lcd);
}
#else
static inline void cm_x300_init_lcd(void) {}
#endif

#if defined(CONFIG_MTD_NAND_PXA3xx) || defined(CONFIG_MTD_NAND_PXA3xx_MODULE)
static struct mtd_partition cm_x300_nand_partitions[] = {
	[0] = {
		.name        = "OBM",
		.offset      = 0,
		.size        = SZ_256K,
		.mask_flags  = MTD_WRITEABLE, /* force read-only */
	},
	[1] = {
		.name        = "U-Boot",
		.offset      = MTDPART_OFS_APPEND,
		.size        = SZ_256K,
		.mask_flags  = MTD_WRITEABLE, /* force read-only */
	},
	[2] = {
		.name        = "Environment",
		.offset      = MTDPART_OFS_APPEND,
		.size        = SZ_256K,
	},
	[3] = {
		.name        = "reserved",
		.offset      = MTDPART_OFS_APPEND,
		.size        = SZ_256K + SZ_1M,
		.mask_flags  = MTD_WRITEABLE, /* force read-only */
	},
	[4] = {
		.name        = "kernel",
		.offset      = MTDPART_OFS_APPEND,
		.size        = SZ_4M,
	},
	[5] = {
		.name        = "fs",
		.offset      = MTDPART_OFS_APPEND,
		.size        = MTDPART_SIZ_FULL,
	},
};

static struct pxa3xx_nand_platform_data cm_x300_nand_info = {
	.enable_arbiter	= 1,
	.keep_config	= 1,
	.parts		= cm_x300_nand_partitions,
	.nr_parts	= ARRAY_SIZE(cm_x300_nand_partitions),
};

static void __init cm_x300_init_nand(void)
{
	pxa3xx_set_nand_info(&cm_x300_nand_info);
}
#else
static inline void cm_x300_init_nand(void) {}
#endif

#if defined(CONFIG_MMC) || defined(CONFIG_MMC_MODULE)
/* The first MMC slot of CM-X300 is hardwired to Libertas card and has
   no detection/ro pins */
static int cm_x300_mci_init(struct device *dev,
			    irq_handler_t cm_x300_detect_int,
			    void *data)
{
	return 0;
}

static void cm_x300_mci_exit(struct device *dev, void *data)
{
}

static struct pxamci_platform_data cm_x300_mci_platform_data = {
	.detect_delay		= 20,
	.ocr_mask		= MMC_VDD_32_33|MMC_VDD_33_34,
	.init 			= cm_x300_mci_init,
	.exit			= cm_x300_mci_exit,
	.gpio_card_detect	= -1,
	.gpio_card_ro		= -1,
	.gpio_power		= -1,
};

static struct pxamci_platform_data cm_x300_mci2_platform_data = {
	.detect_delay		= 20,
	.ocr_mask		= MMC_VDD_32_33|MMC_VDD_33_34,
	.gpio_card_detect	= GPIO82_MMC2_IRQ,
	.gpio_card_ro		= GPIO85_MMC2_WP,
	.gpio_power		= -1,
};

static void __init cm_x300_init_mmc(void)
{
	pxa_set_mci_info(&cm_x300_mci_platform_data);
	pxa3xx_set_mci2_info(&cm_x300_mci2_platform_data);
}
#else
static inline void cm_x300_init_mmc(void) {}
#endif

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static struct pxaohci_platform_data cm_x300_ohci_platform_data = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT1 | ENABLE_PORT2 | POWER_CONTROL_LOW,
};

static void __init cm_x300_init_ohci(void)
{
	pxa_set_ohci_info(&cm_x300_ohci_platform_data);
}
#else
static inline void cm_x300_init_ohci(void) {}
#endif

#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
static struct gpio_led cm_x300_leds[] = {
	[0] = {
		.name = "cm-x300:green",
		.default_trigger = "heartbeat",
		.gpio = 79,
		.active_low = 1,
	},
};

static struct gpio_led_platform_data cm_x300_gpio_led_pdata = {
	.num_leds = ARRAY_SIZE(cm_x300_leds),
	.leds = cm_x300_leds,
};

static struct platform_device cm_x300_led_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data = &cm_x300_gpio_led_pdata,
	},
};

static void __init cm_x300_init_leds(void)
{
	platform_device_register(&cm_x300_led_device);
}
#else
static inline void cm_x300_init_leds(void) {}
#endif

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
/* PCA9555 */
static struct pca953x_platform_data cm_x300_gpio_ext_pdata_0 = {
	.gpio_base = 128,
};

static struct pca953x_platform_data cm_x300_gpio_ext_pdata_1 = {
	.gpio_base = 144,
};

static struct i2c_board_info cm_x300_gpio_ext_info[] = {
	[0] = {
		I2C_BOARD_INFO("pca9555", 0x24),
		.platform_data = &cm_x300_gpio_ext_pdata_0,
	},
	[1] = {
		I2C_BOARD_INFO("pca9555", 0x25),
		.platform_data = &cm_x300_gpio_ext_pdata_1,
	},
};

static void __init cm_x300_init_i2c(void)
{
	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, cm_x300_gpio_ext_info,
				ARRAY_SIZE(cm_x300_gpio_ext_info));
}
#else
static inline void cm_x300_init_i2c(void) {}
#endif

#if defined(CONFIG_RTC_DRV_V3020) || defined(CONFIG_RTC_DRV_V3020_MODULE)
struct v3020_platform_data cm_x300_v3020_pdata = {
	.use_gpio	= 1,
	.gpio_cs	= GPIO95_RTC_CS,
	.gpio_wr	= GPIO96_RTC_WR,
	.gpio_rd	= GPIO97_RTC_RD,
	.gpio_io	= GPIO98_RTC_IO,
};

static struct platform_device cm_x300_rtc_device = {
	.name		= "v3020",
	.id		= -1,
	.dev		= {
		.platform_data = &cm_x300_v3020_pdata,
	}
};

static void __init cm_x300_init_rtc(void)
{
	platform_device_register(&cm_x300_rtc_device);
}
#else
static inline void cm_x300_init_rtc(void) {}
#endif

static void __init cm_x300_init(void)
{
	/* board-processor specific GPIO initialization */
	pxa3xx_mfp_config(ARRAY_AND_SIZE(cm_x300_mfp_cfg));

	cm_x300_init_dm9000();
	cm_x300_init_lcd();
	cm_x300_init_ohci();
	cm_x300_init_mmc();
	cm_x300_init_nand();
	cm_x300_init_leds();
	cm_x300_init_i2c();
	cm_x300_init_rtc();
}

static void __init cm_x300_fixup(struct machine_desc *mdesc, struct tag *tags,
				 char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 2;
	mi->bank[0].start = 0xa0000000;
	mi->bank[0].node = 0;
	mi->bank[0].size = (64*1024*1024);
	mi->bank[1].start = 0xc0000000;
	mi->bank[1].node = 0;
	mi->bank[1].size = (64*1024*1024);
}

MACHINE_START(CM_X300, "CM-X300 module")
	.phys_io	= 0x40000000,
	.boot_params	= 0xa0000100,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.map_io		= pxa_map_io,
	.init_irq	= pxa3xx_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= cm_x300_init,
	.fixup		= cm_x300_fixup,
MACHINE_END
