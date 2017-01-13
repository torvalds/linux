/*
 * Hardware definitions for Voipac PXA270
 *
 * Copyright (C) 2010
 * Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/usb/gpio_vbus.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/onenand.h>
#include <linux/dm9000.h>
#include <linux/ucb1400.h>
#include <linux/ata_platform.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/max1586.h>
#include <linux/i2c/pxa-i2c.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "pxa27x.h"
#include <mach/audio.h>
#include <mach/vpac270.h>
#include <linux/platform_data/mmc-pxamci.h>
#include <linux/platform_data/video-pxafb.h>
#include <linux/platform_data/usb-ohci-pxa27x.h>
#include "pxa27x-udc.h"
#include "udc.h"
#include <linux/platform_data/ata-pxa.h>

#include "generic.h"
#include "devices.h"

/******************************************************************************
 * Pin configuration
 ******************************************************************************/
static unsigned long vpac270_pin_config[] __initdata = {
	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
	GPIO53_GPIO,	/* SD detect */
	GPIO52_GPIO,	/* SD r/o switch */

	/* GPIO KEYS */
	GPIO1_GPIO,	/* USER BTN */

	/* LEDs */
	GPIO15_GPIO,	/* orange led */

	/* FFUART */
	GPIO34_FFUART_RXD,
	GPIO39_FFUART_TXD,
	GPIO27_FFUART_RTS,
	GPIO100_FFUART_CTS,
	GPIO33_FFUART_DSR,
	GPIO40_FFUART_DTR,
	GPIO10_FFUART_DCD,
	GPIO38_FFUART_RI,

	/* LCD */
	GPIO58_LCD_LDD_0,
	GPIO59_LCD_LDD_1,
	GPIO60_LCD_LDD_2,
	GPIO61_LCD_LDD_3,
	GPIO62_LCD_LDD_4,
	GPIO63_LCD_LDD_5,
	GPIO64_LCD_LDD_6,
	GPIO65_LCD_LDD_7,
	GPIO66_LCD_LDD_8,
	GPIO67_LCD_LDD_9,
	GPIO68_LCD_LDD_10,
	GPIO69_LCD_LDD_11,
	GPIO70_LCD_LDD_12,
	GPIO71_LCD_LDD_13,
	GPIO72_LCD_LDD_14,
	GPIO73_LCD_LDD_15,
	GPIO86_LCD_LDD_16,
	GPIO87_LCD_LDD_17,
	GPIO74_LCD_FCLK,
	GPIO75_LCD_LCLK,
	GPIO76_LCD_PCLK,
	GPIO77_LCD_BIAS,

	/* PCMCIA */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO85_nPCE_1,
	GPIO54_nPCE_2,
	GPIO55_nPREG,
	GPIO57_nIOIS16,
	GPIO56_nPWAIT,
	GPIO104_PSKTSEL,
	GPIO84_GPIO,	/* PCMCIA CD */
	GPIO35_GPIO,	/* PCMCIA RDY */
	GPIO107_GPIO,	/* PCMCIA PPEN */
	GPIO11_GPIO,	/* PCMCIA RESET */
	GPIO17_GPIO,	/* CF CD */
	GPIO12_GPIO,	/* CF RDY */
	GPIO16_GPIO,	/* CF RESET */

	/* UHC */
	GPIO88_USBH1_PWR,
	GPIO89_USBH1_PEN,
	GPIO119_USBH2_PWR,
	GPIO120_USBH2_PEN,

	/* UDC */
	GPIO41_GPIO,

	/* Ethernet */
	GPIO114_GPIO,	/* IRQ */

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO95_AC97_nRESET,
	GPIO98_AC97_SYSCLK,
	GPIO113_GPIO,	/* TS IRQ */

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* IDE */
	GPIO36_GPIO,	/* IDE IRQ */
	GPIO80_DREQ_1,
};

/******************************************************************************
 * NOR Flash
 ******************************************************************************/
#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static struct mtd_partition vpac270_nor_partitions[] = {
	{
		.name		= "Flash",
		.offset		= 0x00000000,
		.size		= MTDPART_SIZ_FULL,
	}
};

static struct physmap_flash_data vpac270_flash_data[] = {
	{
		.width		= 2,	/* bankwidth in bytes */
		.parts		= vpac270_nor_partitions,
		.nr_parts	= ARRAY_SIZE(vpac270_nor_partitions)
	}
};

static struct resource vpac270_flash_resource = {
	.start	= PXA_CS0_PHYS,
	.end	= PXA_CS0_PHYS + SZ_64M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device vpac270_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.resource	= &vpac270_flash_resource,
	.num_resources	= 1,
	.dev 		= {
		.platform_data = vpac270_flash_data,
	},
};
static void __init vpac270_nor_init(void)
{
	platform_device_register(&vpac270_flash);
}
#else
static inline void vpac270_nor_init(void) {}
#endif

/******************************************************************************
 * OneNAND Flash
 ******************************************************************************/
#if defined(CONFIG_MTD_ONENAND) || defined(CONFIG_MTD_ONENAND_MODULE)
static struct mtd_partition vpac270_onenand_partitions[] = {
	{
		.name		= "Flash",
		.offset		= 0x00000000,
		.size		= MTDPART_SIZ_FULL,
	}
};

static struct onenand_platform_data vpac270_onenand_info = {
	.parts		= vpac270_onenand_partitions,
	.nr_parts	= ARRAY_SIZE(vpac270_onenand_partitions),
};

static struct resource vpac270_onenand_resources[] = {
	[0] = {
		.start	= PXA_CS0_PHYS,
		.end	= PXA_CS0_PHYS + SZ_1M,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device vpac270_onenand = {
	.name		= "onenand-flash",
	.id		= -1,
	.resource	= vpac270_onenand_resources,
	.num_resources	= ARRAY_SIZE(vpac270_onenand_resources),
	.dev		= {
		.platform_data	= &vpac270_onenand_info,
	},
};

static void __init vpac270_onenand_init(void)
{
	platform_device_register(&vpac270_onenand);
}
#else
static void __init vpac270_onenand_init(void) {}
#endif

/******************************************************************************
 * SD/MMC card controller
 ******************************************************************************/
#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
static struct pxamci_platform_data vpac270_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34,
	.gpio_power		= -1,
	.gpio_card_detect	= GPIO53_VPAC270_SD_DETECT_N,
	.gpio_card_ro		= GPIO52_VPAC270_SD_READONLY,
	.detect_delay_ms	= 200,
};

static void __init vpac270_mmc_init(void)
{
	pxa_set_mci_info(&vpac270_mci_platform_data);
}
#else
static inline void vpac270_mmc_init(void) {}
#endif

/******************************************************************************
 * GPIO keys
 ******************************************************************************/
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button vpac270_pxa_buttons[] = {
	{KEY_POWER, GPIO1_VPAC270_USER_BTN, 0, "USER BTN"},
};

static struct gpio_keys_platform_data vpac270_pxa_keys_data = {
	.buttons	= vpac270_pxa_buttons,
	.nbuttons	= ARRAY_SIZE(vpac270_pxa_buttons),
};

static struct platform_device vpac270_pxa_keys = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data = &vpac270_pxa_keys_data,
	},
};

static void __init vpac270_keys_init(void)
{
	platform_device_register(&vpac270_pxa_keys);
}
#else
static inline void vpac270_keys_init(void) {}
#endif

/******************************************************************************
 * LED
 ******************************************************************************/
#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
struct gpio_led vpac270_gpio_leds[] = {
{
	.name			= "vpac270:orange:user",
	.default_trigger	= "none",
	.gpio			= GPIO15_VPAC270_LED_ORANGE,
	.active_low		= 1,
}
};

static struct gpio_led_platform_data vpac270_gpio_led_info = {
	.leds		= vpac270_gpio_leds,
	.num_leds	= ARRAY_SIZE(vpac270_gpio_leds),
};

static struct platform_device vpac270_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &vpac270_gpio_led_info,
	}
};

static void __init vpac270_leds_init(void)
{
	platform_device_register(&vpac270_leds);
}
#else
static inline void vpac270_leds_init(void) {}
#endif

/******************************************************************************
 * USB Host
 ******************************************************************************/
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static int vpac270_ohci_init(struct device *dev)
{
	UP2OCR = UP2OCR_HXS | UP2OCR_HXOE | UP2OCR_DPPDE | UP2OCR_DMPDE;
	return 0;
}

static struct pxaohci_platform_data vpac270_ohci_info = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT1 | ENABLE_PORT2 |
			POWER_CONTROL_LOW | POWER_SENSE_LOW,
	.init		= vpac270_ohci_init,
};

static void __init vpac270_uhc_init(void)
{
	pxa_set_ohci_info(&vpac270_ohci_info);
}
#else
static inline void vpac270_uhc_init(void) {}
#endif

/******************************************************************************
 * USB Gadget
 ******************************************************************************/
#if defined(CONFIG_USB_PXA27X)||defined(CONFIG_USB_PXA27X_MODULE)
static struct gpio_vbus_mach_info vpac270_gpio_vbus_info = {
	.gpio_vbus		= GPIO41_VPAC270_UDC_DETECT,
	.gpio_pullup		= -1,
};

static struct platform_device vpac270_gpio_vbus = {
	.name	= "gpio-vbus",
	.id	= -1,
	.dev	= {
		.platform_data	= &vpac270_gpio_vbus_info,
	},
};

static void vpac270_udc_command(int cmd)
{
	if (cmd == PXA2XX_UDC_CMD_CONNECT)
		UP2OCR = UP2OCR_HXOE | UP2OCR_DPPUE;
	else if (cmd == PXA2XX_UDC_CMD_DISCONNECT)
		UP2OCR = UP2OCR_HXOE;
}

static struct pxa2xx_udc_mach_info vpac270_udc_info __initdata = {
	.udc_command		= vpac270_udc_command,
	.gpio_pullup		= -1,
};

static void __init vpac270_udc_init(void)
{
	pxa_set_udc_info(&vpac270_udc_info);
	platform_device_register(&vpac270_gpio_vbus);
}
#else
static inline void vpac270_udc_init(void) {}
#endif

/******************************************************************************
 * Ethernet
 ******************************************************************************/
#if defined(CONFIG_DM9000) || defined(CONFIG_DM9000_MODULE)
static struct resource vpac270_dm9000_resources[] = {
	[0] = {
		.start	= PXA_CS2_PHYS + 0x300,
		.end	= PXA_CS2_PHYS + 0x303,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= PXA_CS2_PHYS + 0x304,
		.end	= PXA_CS2_PHYS + 0x343,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= PXA_GPIO_TO_IRQ(GPIO114_VPAC270_ETH_IRQ),
		.end	= PXA_GPIO_TO_IRQ(GPIO114_VPAC270_ETH_IRQ),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
};

static struct dm9000_plat_data vpac270_dm9000_platdata = {
	.flags		= DM9000_PLATF_32BITONLY,
};

static struct platform_device vpac270_dm9000_device = {
	.name		= "dm9000",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(vpac270_dm9000_resources),
	.resource	= vpac270_dm9000_resources,
	.dev		= {
		.platform_data = &vpac270_dm9000_platdata,
	}
};

static void __init vpac270_eth_init(void)
{
	platform_device_register(&vpac270_dm9000_device);
}
#else
static inline void vpac270_eth_init(void) {}
#endif

/******************************************************************************
 * Audio and Touchscreen
 ******************************************************************************/
#if	defined(CONFIG_TOUCHSCREEN_UCB1400) || \
	defined(CONFIG_TOUCHSCREEN_UCB1400_MODULE)
static pxa2xx_audio_ops_t vpac270_ac97_pdata = {
	.reset_gpio	= 95,
};

static struct ucb1400_pdata vpac270_ucb1400_pdata = {
	.irq		= PXA_GPIO_TO_IRQ(GPIO113_VPAC270_TS_IRQ),
};

static struct platform_device vpac270_ucb1400_device = {
	.name		= "ucb1400_core",
	.id		= -1,
	.dev		= {
		.platform_data = &vpac270_ucb1400_pdata,
	},
};

static void __init vpac270_ts_init(void)
{
	pxa_set_ac97_info(&vpac270_ac97_pdata);
	platform_device_register(&vpac270_ucb1400_device);
}
#else
static inline void vpac270_ts_init(void) {}
#endif

/******************************************************************************
 * RTC
 ******************************************************************************/
#if defined(CONFIG_RTC_DRV_DS1307) || defined(CONFIG_RTC_DRV_DS1307_MODULE)
static struct i2c_board_info __initdata vpac270_i2c_devs[] = {
	{
		I2C_BOARD_INFO("ds1339", 0x68),
	},
};

static void __init vpac270_rtc_init(void)
{
	i2c_register_board_info(0, ARRAY_AND_SIZE(vpac270_i2c_devs));
}
#else
static inline void vpac270_rtc_init(void) {}
#endif

/******************************************************************************
 * Framebuffer
 ******************************************************************************/
#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static struct pxafb_mode_info vpac270_lcd_modes[] = {
{
	.pixclock	= 57692,
	.xres		= 640,
	.yres		= 480,
	.bpp		= 32,
	.depth		= 18,

	.left_margin	= 144,
	.right_margin	= 32,
	.upper_margin	= 13,
	.lower_margin	= 30,

	.hsync_len	= 32,
	.vsync_len	= 2,

	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
}, {	/* CRT 640x480 */
	.pixclock	= 35000,
	.xres		= 640,
	.yres		= 480,
	.bpp		= 16,
	.depth		= 16,

	.left_margin	= 96,
	.right_margin	= 48,
	.upper_margin	= 33,
	.lower_margin	= 10,

	.hsync_len	= 48,
	.vsync_len	= 1,

	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
}, {	/* CRT 800x600 H=30kHz V=48HZ */
	.pixclock	= 25000,
	.xres		= 800,
	.yres		= 600,
	.bpp		= 16,
	.depth		= 16,

	.left_margin	= 50,
	.right_margin	= 1,
	.upper_margin	= 21,
	.lower_margin	= 12,

	.hsync_len	= 8,
	.vsync_len	= 1,

	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
}, {	/* CRT 1024x768 H=40kHz V=50Hz */
	.pixclock	= 15000,
	.xres		= 1024,
	.yres		= 768,
	.bpp		= 16,
	.depth		= 16,

	.left_margin	= 220,
	.right_margin	= 8,
	.upper_margin	= 33,
	.lower_margin	= 2,

	.hsync_len	= 48,
	.vsync_len	= 1,

	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
}
};

static struct pxafb_mach_info vpac270_lcd_screen = {
	.modes		= vpac270_lcd_modes,
	.num_modes	= ARRAY_SIZE(vpac270_lcd_modes),
	.lcd_conn	= LCD_COLOR_TFT_18BPP,
};

static void vpac270_lcd_power(int on, struct fb_var_screeninfo *info)
{
	gpio_set_value(GPIO81_VPAC270_BKL_ON, on);
}

static void __init vpac270_lcd_init(void)
{
	int ret;

	ret = gpio_request(GPIO81_VPAC270_BKL_ON, "BKL-ON");
	if (ret) {
		pr_err("Requesting BKL-ON GPIO failed!\n");
		goto err;
	}

	ret = gpio_direction_output(GPIO81_VPAC270_BKL_ON, 1);
	if (ret) {
		pr_err("Setting BKL-ON GPIO direction failed!\n");
		goto err2;
	}

	vpac270_lcd_screen.pxafb_lcd_power = vpac270_lcd_power;
	pxa_set_fb_info(NULL, &vpac270_lcd_screen);
	return;

err2:
	gpio_free(GPIO81_VPAC270_BKL_ON);
err:
	return;
}
#else
static inline void vpac270_lcd_init(void) {}
#endif

/******************************************************************************
 * PATA IDE
 ******************************************************************************/
#if defined(CONFIG_PATA_PXA) || defined(CONFIG_PATA_PXA_MODULE)
static struct pata_pxa_pdata vpac270_pata_pdata = {
	.reg_shift	= 1,
	.dma_dreq	= 1,
	.irq_flags	= IRQF_TRIGGER_RISING,
};

static struct resource vpac270_ide_resources[] = {
	[0] = {	/* I/O Base address */
	       .start	= PXA_CS3_PHYS + 0x120,
	       .end	= PXA_CS3_PHYS + 0x13f,
	       .flags	= IORESOURCE_MEM
	},
	[1] = {	/* CTL Base address */
	       .start	= PXA_CS3_PHYS + 0x15c,
	       .end	= PXA_CS3_PHYS + 0x15f,
	       .flags	= IORESOURCE_MEM
	},
	[2] = {	/* DMA Base address */
	       .start	= PXA_CS3_PHYS + 0x20,
	       .end	= PXA_CS3_PHYS + 0x2f,
	       .flags	= IORESOURCE_DMA
	},
	[3] = {	/* IDE IRQ pin */
	       .start	= PXA_GPIO_TO_IRQ(GPIO36_VPAC270_IDE_IRQ),
	       .end	= PXA_GPIO_TO_IRQ(GPIO36_VPAC270_IDE_IRQ),
	       .flags	= IORESOURCE_IRQ
	}
};

static struct platform_device vpac270_ide_device = {
	.name		= "pata_pxa",
	.num_resources	= ARRAY_SIZE(vpac270_ide_resources),
	.resource	= vpac270_ide_resources,
	.dev		= {
		.platform_data	= &vpac270_pata_pdata,
		.coherent_dma_mask	= 0xffffffff,
	}
};

static void __init vpac270_ide_init(void)
{
	platform_device_register(&vpac270_ide_device);
}
#else
static inline void vpac270_ide_init(void) {}
#endif

/******************************************************************************
 * Core power regulator
 ******************************************************************************/
#if defined(CONFIG_REGULATOR_MAX1586) || \
    defined(CONFIG_REGULATOR_MAX1586_MODULE)
static struct regulator_consumer_supply vpac270_max1587a_consumers[] = {
	REGULATOR_SUPPLY("vcc_core", NULL),
};

static struct regulator_init_data vpac270_max1587a_v3_info = {
	.constraints = {
		.name		= "vcc_core range",
		.min_uV		= 900000,
		.max_uV		= 1705000,
		.always_on	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE,
	},
	.consumer_supplies	= vpac270_max1587a_consumers,
	.num_consumer_supplies	= ARRAY_SIZE(vpac270_max1587a_consumers),
};

static struct max1586_subdev_data vpac270_max1587a_subdevs[] = {
	{
		.name		= "vcc_core",
		.id		= MAX1586_V3,
		.platform_data	= &vpac270_max1587a_v3_info,
	}
};

static struct max1586_platform_data vpac270_max1587a_info = {
	.subdevs     = vpac270_max1587a_subdevs,
	.num_subdevs = ARRAY_SIZE(vpac270_max1587a_subdevs),
	.v3_gain     = MAX1586_GAIN_R24_3k32, /* 730..1550 mV */
};

static struct i2c_board_info __initdata vpac270_pi2c_board_info[] = {
	{
		I2C_BOARD_INFO("max1586", 0x14),
		.platform_data	= &vpac270_max1587a_info,
	},
};

static void __init vpac270_pmic_init(void)
{
	i2c_register_board_info(1, ARRAY_AND_SIZE(vpac270_pi2c_board_info));
}
#else
static inline void vpac270_pmic_init(void) {}
#endif


/******************************************************************************
 * Machine init
 ******************************************************************************/
static void __init vpac270_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(vpac270_pin_config));

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	pxa_set_i2c_info(NULL);
	pxa27x_set_i2c_power_info(NULL);

	vpac270_pmic_init();
	vpac270_lcd_init();
	vpac270_mmc_init();
	vpac270_nor_init();
	vpac270_onenand_init();
	vpac270_leds_init();
	vpac270_keys_init();
	vpac270_uhc_init();
	vpac270_udc_init();
	vpac270_eth_init();
	vpac270_ts_init();
	vpac270_rtc_init();
	vpac270_ide_init();

	regulator_has_full_constraints();
}

MACHINE_START(VPAC270, "Voipac PXA270")
	.atag_offset	= 0x100,
	.map_io		= pxa27x_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa27x_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.init_time	= pxa_timer_init,
	.init_machine	= vpac270_init,
	.restart	= pxa_restart,
MACHINE_END
