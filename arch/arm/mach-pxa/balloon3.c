/*
 *  linux/arch/arm/mach-pxa/balloon3.c
 *
 *  Support for Balloonboard.org Balloon3 board.
 *
 *  Author:	Nick Bane, Wookey, Jonathan McDowell
 *  Created:	June, 2006
 *  Copyright:	Toby Churchill Ltd
 *  Derived from mainstone.c, by Nico Pitre
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/ioport.h>
#include <linux/ucb1400.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/types.h>
#include <linux/i2c/pcf857x.h>
#include <linux/i2c/pxa-i2c.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/physmap.h>
#include <linux/regulator/max1586.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/sizes.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/flash.h>

#include <mach/pxa27x.h>
#include <mach/balloon3.h>
#include <mach/audio.h>
#include <linux/platform_data/video-pxafb.h>
#include <linux/platform_data/mmc-pxamci.h>
#include <mach/udc.h>
#include <mach/pxa27x-udc.h>
#include <linux/platform_data/irda-pxaficp.h>
#include <linux/platform_data/usb-ohci-pxa27x.h>

#include "generic.h"
#include "devices.h"

/******************************************************************************
 * Pin configuration
 ******************************************************************************/
static unsigned long balloon3_pin_config[] __initdata = {
	/* Select BTUART 'COM1/ttyS0' as IO option for pins 42/43/44/45 */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,
	GPIO45_BTUART_RTS,

	/* Reset, configured as GPIO wakeup source */
	GPIO1_GPIO | WAKEUP_ON_EDGE_BOTH,
};

/******************************************************************************
 * Compatibility: Parameter parsing
 ******************************************************************************/
static unsigned long balloon3_irq_enabled;

static unsigned long balloon3_features_present =
		(1 << BALLOON3_FEATURE_OHCI) | (1 << BALLOON3_FEATURE_CF) |
		(1 << BALLOON3_FEATURE_AUDIO) |
		(1 << BALLOON3_FEATURE_TOPPOLY);

int balloon3_has(enum balloon3_features feature)
{
	return (balloon3_features_present & (1 << feature)) ? 1 : 0;
}
EXPORT_SYMBOL_GPL(balloon3_has);

int __init parse_balloon3_features(char *arg)
{
	if (!arg)
		return 0;

	return kstrtoul(arg, 0, &balloon3_features_present);
}
early_param("balloon3_features", parse_balloon3_features);

/******************************************************************************
 * Compact Flash slot
 ******************************************************************************/
#if	defined(CONFIG_PCMCIA_PXA2XX) || defined(CONFIG_PCMCIA_PXA2XX_MODULE)
static unsigned long balloon3_cf_pin_config[] __initdata = {
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO85_nPCE_1,
	GPIO54_nPCE_2,
	GPIO79_PSKTSEL,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,
};

static void __init balloon3_cf_init(void)
{
	if (!balloon3_has(BALLOON3_FEATURE_CF))
		return;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(balloon3_cf_pin_config));
}
#else
static inline void balloon3_cf_init(void) {}
#endif

/******************************************************************************
 * NOR Flash
 ******************************************************************************/
#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static struct mtd_partition balloon3_nor_partitions[] = {
	{
		.name		= "Flash",
		.offset		= 0x00000000,
		.size		= MTDPART_SIZ_FULL,
	}
};

static struct physmap_flash_data balloon3_flash_data[] = {
	{
		.width		= 2,	/* bankwidth in bytes */
		.parts		= balloon3_nor_partitions,
		.nr_parts	= ARRAY_SIZE(balloon3_nor_partitions)
	}
};

static struct resource balloon3_flash_resource = {
	.start	= PXA_CS0_PHYS,
	.end	= PXA_CS0_PHYS + SZ_64M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device balloon3_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.resource	= &balloon3_flash_resource,
	.num_resources	= 1,
	.dev 		= {
		.platform_data = balloon3_flash_data,
	},
};
static void __init balloon3_nor_init(void)
{
	platform_device_register(&balloon3_flash);
}
#else
static inline void balloon3_nor_init(void) {}
#endif

/******************************************************************************
 * Audio and Touchscreen
 ******************************************************************************/
#if	defined(CONFIG_TOUCHSCREEN_UCB1400) || \
	defined(CONFIG_TOUCHSCREEN_UCB1400_MODULE)
static unsigned long balloon3_ac97_pin_config[] __initdata = {
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO113_AC97_nRESET,
	GPIO95_GPIO,
};

static struct ucb1400_pdata vpac270_ucb1400_pdata = {
	.irq		= PXA_GPIO_TO_IRQ(BALLOON3_GPIO_CODEC_IRQ),
};


static struct platform_device balloon3_ucb1400_device = {
	.name		= "ucb1400_core",
	.id		= -1,
	.dev		= {
		.platform_data = &vpac270_ucb1400_pdata,
	},
};

static void __init balloon3_ts_init(void)
{
	if (!balloon3_has(BALLOON3_FEATURE_AUDIO))
		return;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(balloon3_ac97_pin_config));
	pxa_set_ac97_info(NULL);
	platform_device_register(&balloon3_ucb1400_device);
}
#else
static inline void balloon3_ts_init(void) {}
#endif

/******************************************************************************
 * Framebuffer
 ******************************************************************************/
#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static unsigned long balloon3_lcd_pin_config[] __initdata = {
	GPIOxx_LCD_TFT_16BPP,
	GPIO99_GPIO,
};

static struct pxafb_mode_info balloon3_lcd_modes[] = {
	{
		.pixclock		= 38000,
		.xres			= 480,
		.yres			= 640,
		.bpp			= 16,
		.hsync_len		= 8,
		.left_margin		= 8,
		.right_margin		= 8,
		.vsync_len		= 2,
		.upper_margin		= 4,
		.lower_margin		= 5,
		.sync			= 0,
	},
};

static struct pxafb_mach_info balloon3_lcd_screen = {
	.modes			= balloon3_lcd_modes,
	.num_modes		= ARRAY_SIZE(balloon3_lcd_modes),
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL,
};

static void balloon3_backlight_power(int on)
{
	gpio_set_value(BALLOON3_GPIO_RUN_BACKLIGHT, on);
}

static void __init balloon3_lcd_init(void)
{
	int ret;

	if (!balloon3_has(BALLOON3_FEATURE_TOPPOLY))
		return;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(balloon3_lcd_pin_config));

	ret = gpio_request(BALLOON3_GPIO_RUN_BACKLIGHT, "BKL-ON");
	if (ret) {
		pr_err("Requesting BKL-ON GPIO failed!\n");
		goto err;
	}

	ret = gpio_direction_output(BALLOON3_GPIO_RUN_BACKLIGHT, 1);
	if (ret) {
		pr_err("Setting BKL-ON GPIO direction failed!\n");
		goto err2;
	}

	balloon3_lcd_screen.pxafb_backlight_power = balloon3_backlight_power;
	pxa_set_fb_info(NULL, &balloon3_lcd_screen);
	return;

err2:
	gpio_free(BALLOON3_GPIO_RUN_BACKLIGHT);
err:
	return;
}
#else
static inline void balloon3_lcd_init(void) {}
#endif

/******************************************************************************
 * SD/MMC card controller
 ******************************************************************************/
#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
static unsigned long balloon3_mmc_pin_config[] __initdata = {
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
};

static struct pxamci_platform_data balloon3_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34,
	.gpio_card_detect	= -1,
	.gpio_card_ro		= -1,
	.gpio_power		= -1,
	.detect_delay_ms	= 200,
};

static void __init balloon3_mmc_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(balloon3_mmc_pin_config));
	pxa_set_mci_info(&balloon3_mci_platform_data);
}
#else
static inline void balloon3_mmc_init(void) {}
#endif

/******************************************************************************
 * USB Gadget
 ******************************************************************************/
#if defined(CONFIG_USB_PXA27X)||defined(CONFIG_USB_PXA27X_MODULE)
static void balloon3_udc_command(int cmd)
{
	if (cmd == PXA2XX_UDC_CMD_CONNECT)
		UP2OCR |= UP2OCR_DPPUE | UP2OCR_DPPUBE;
	else if (cmd == PXA2XX_UDC_CMD_DISCONNECT)
		UP2OCR &= ~UP2OCR_DPPUE;
}

static int balloon3_udc_is_connected(void)
{
	return 1;
}

static struct pxa2xx_udc_mach_info balloon3_udc_info __initdata = {
	.udc_command		= balloon3_udc_command,
	.udc_is_connected	= balloon3_udc_is_connected,
	.gpio_pullup		= -1,
};

static void __init balloon3_udc_init(void)
{
	pxa_set_udc_info(&balloon3_udc_info);
}
#else
static inline void balloon3_udc_init(void) {}
#endif

/******************************************************************************
 * IrDA
 ******************************************************************************/
#if defined(CONFIG_IRDA) || defined(CONFIG_IRDA_MODULE)
static struct pxaficp_platform_data balloon3_ficp_platform_data = {
	.transceiver_cap	= IR_FIRMODE | IR_SIRMODE | IR_OFF,
};

static void __init balloon3_irda_init(void)
{
	pxa_set_ficp_info(&balloon3_ficp_platform_data);
}
#else
static inline void balloon3_irda_init(void) {}
#endif

/******************************************************************************
 * USB Host
 ******************************************************************************/
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static unsigned long balloon3_uhc_pin_config[] __initdata = {
	GPIO88_USBH1_PWR,
	GPIO89_USBH1_PEN,
};

static struct pxaohci_platform_data balloon3_ohci_info = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT_ALL | POWER_CONTROL_LOW | POWER_SENSE_LOW,
};

static void __init balloon3_uhc_init(void)
{
	if (!balloon3_has(BALLOON3_FEATURE_OHCI))
		return;
	pxa2xx_mfp_config(ARRAY_AND_SIZE(balloon3_uhc_pin_config));
	pxa_set_ohci_info(&balloon3_ohci_info);
}
#else
static inline void balloon3_uhc_init(void) {}
#endif

/******************************************************************************
 * LEDs
 ******************************************************************************/
#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
static unsigned long balloon3_led_pin_config[] __initdata = {
	GPIO9_GPIO,	/* NAND activity LED */
	GPIO10_GPIO,	/* Heartbeat LED */
};

struct gpio_led balloon3_gpio_leds[] = {
	{
		.name			= "balloon3:green:idle",
		.default_trigger	= "heartbeat",
		.gpio			= BALLOON3_GPIO_LED_IDLE,
		.active_low		= 1,
	}, {
		.name			= "balloon3:green:nand",
		.default_trigger	= "nand-disk",
		.gpio			= BALLOON3_GPIO_LED_NAND,
		.active_low		= 1,
	},
};

static struct gpio_led_platform_data balloon3_gpio_led_info = {
	.leds		= balloon3_gpio_leds,
	.num_leds	= ARRAY_SIZE(balloon3_gpio_leds),
};

static struct platform_device balloon3_leds = {
	.name	= "leds-gpio",
	.id	= 0,
	.dev	= {
		.platform_data	= &balloon3_gpio_led_info,
	}
};

struct gpio_led balloon3_pcf_gpio_leds[] = {
	{
		.name			= "balloon3:green:led0",
		.gpio			= BALLOON3_PCF_GPIO_LED0,
		.active_low		= 1,
	}, {
		.name			= "balloon3:green:led1",
		.gpio			= BALLOON3_PCF_GPIO_LED1,
		.active_low		= 1,
	}, {
		.name			= "balloon3:orange:led2",
		.gpio			= BALLOON3_PCF_GPIO_LED2,
		.active_low		= 1,
	}, {
		.name			= "balloon3:orange:led3",
		.gpio			= BALLOON3_PCF_GPIO_LED3,
		.active_low		= 1,
	}, {
		.name			= "balloon3:orange:led4",
		.gpio			= BALLOON3_PCF_GPIO_LED4,
		.active_low		= 1,
	}, {
		.name			= "balloon3:orange:led5",
		.gpio			= BALLOON3_PCF_GPIO_LED5,
		.active_low		= 1,
	}, {
		.name			= "balloon3:red:led6",
		.gpio			= BALLOON3_PCF_GPIO_LED6,
		.active_low		= 1,
	}, {
		.name			= "balloon3:red:led7",
		.gpio			= BALLOON3_PCF_GPIO_LED7,
		.active_low		= 1,
	},
};

static struct gpio_led_platform_data balloon3_pcf_gpio_led_info = {
	.leds		= balloon3_pcf_gpio_leds,
	.num_leds	= ARRAY_SIZE(balloon3_pcf_gpio_leds),
};

static struct platform_device balloon3_pcf_leds = {
	.name	= "leds-gpio",
	.id	= 1,
	.dev	= {
		.platform_data	= &balloon3_pcf_gpio_led_info,
	}
};

static void __init balloon3_leds_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(balloon3_led_pin_config));
	platform_device_register(&balloon3_leds);
	platform_device_register(&balloon3_pcf_leds);
}
#else
static inline void balloon3_leds_init(void) {}
#endif

/******************************************************************************
 * FPGA IRQ
 ******************************************************************************/
static void balloon3_mask_irq(struct irq_data *d)
{
	int balloon3_irq = (d->irq - BALLOON3_IRQ(0));
	balloon3_irq_enabled &= ~(1 << balloon3_irq);
	__raw_writel(~balloon3_irq_enabled, BALLOON3_INT_CONTROL_REG);
}

static void balloon3_unmask_irq(struct irq_data *d)
{
	int balloon3_irq = (d->irq - BALLOON3_IRQ(0));
	balloon3_irq_enabled |= (1 << balloon3_irq);
	__raw_writel(~balloon3_irq_enabled, BALLOON3_INT_CONTROL_REG);
}

static struct irq_chip balloon3_irq_chip = {
	.name		= "FPGA",
	.irq_ack	= balloon3_mask_irq,
	.irq_mask	= balloon3_mask_irq,
	.irq_unmask	= balloon3_unmask_irq,
};

static void balloon3_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned long pending = __raw_readl(BALLOON3_INT_CONTROL_REG) &
					balloon3_irq_enabled;
	do {
		/* clear useless edge notification */
		if (desc->irq_data.chip->irq_ack) {
			struct irq_data *d;

			d = irq_get_irq_data(BALLOON3_AUX_NIRQ);
			desc->irq_data.chip->irq_ack(d);
		}

		while (pending) {
			irq = BALLOON3_IRQ(0) + __ffs(pending);
			generic_handle_irq(irq);
			pending &= pending - 1;
		}
		pending = __raw_readl(BALLOON3_INT_CONTROL_REG) &
				balloon3_irq_enabled;
	} while (pending);
}

static void __init balloon3_init_irq(void)
{
	int irq;

	pxa27x_init_irq();
	/* setup extra Balloon3 irqs */
	for (irq = BALLOON3_IRQ(0); irq <= BALLOON3_IRQ(7); irq++) {
		irq_set_chip_and_handler(irq, &balloon3_irq_chip,
					 handle_level_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	irq_set_chained_handler(BALLOON3_AUX_NIRQ, balloon3_irq_handler);
	irq_set_irq_type(BALLOON3_AUX_NIRQ, IRQ_TYPE_EDGE_FALLING);

	pr_debug("%s: chained handler installed - irq %d automatically "
		"enabled\n", __func__, BALLOON3_AUX_NIRQ);
}

/******************************************************************************
 * GPIO expander
 ******************************************************************************/
#if defined(CONFIG_GPIO_PCF857X) || defined(CONFIG_GPIO_PCF857X_MODULE)
static struct pcf857x_platform_data balloon3_pcf857x_pdata = {
	.gpio_base	= BALLOON3_PCF_GPIO_BASE,
	.n_latch	= 0,
	.setup		= NULL,
	.teardown	= NULL,
	.context	= NULL,
};

static struct i2c_board_info __initdata balloon3_i2c_devs[] = {
	{
		I2C_BOARD_INFO("pcf8574a", 0x38),
		.platform_data	= &balloon3_pcf857x_pdata,
	},
};

static void __init balloon3_i2c_init(void)
{
	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, ARRAY_AND_SIZE(balloon3_i2c_devs));
}
#else
static inline void balloon3_i2c_init(void) {}
#endif

/******************************************************************************
 * NAND
 ******************************************************************************/
#if defined(CONFIG_MTD_NAND_PLATFORM)||defined(CONFIG_MTD_NAND_PLATFORM_MODULE)
static void balloon3_nand_cmd_ctl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *this = mtd->priv;
	uint8_t balloon3_ctl_set = 0, balloon3_ctl_clr = 0;

	if (ctrl & NAND_CTRL_CHANGE) {
		if (ctrl & NAND_CLE)
			balloon3_ctl_set |= BALLOON3_NAND_CONTROL_FLCLE;
		else
			balloon3_ctl_clr |= BALLOON3_NAND_CONTROL_FLCLE;

		if (ctrl & NAND_ALE)
			balloon3_ctl_set |= BALLOON3_NAND_CONTROL_FLALE;
		else
			balloon3_ctl_clr |= BALLOON3_NAND_CONTROL_FLALE;

		if (balloon3_ctl_clr)
			__raw_writel(balloon3_ctl_clr,
				BALLOON3_NAND_CONTROL_REG);
		if (balloon3_ctl_set)
			__raw_writel(balloon3_ctl_set,
				BALLOON3_NAND_CONTROL_REG +
				BALLOON3_FPGA_SETnCLR);
	}

	if (cmd != NAND_CMD_NONE)
		writeb(cmd, this->IO_ADDR_W);
}

static void balloon3_nand_select_chip(struct mtd_info *mtd, int chip)
{
	if (chip < 0 || chip > 3)
		return;

	/* Assert all nCE lines */
	__raw_writew(
		BALLOON3_NAND_CONTROL_FLCE0 | BALLOON3_NAND_CONTROL_FLCE1 |
		BALLOON3_NAND_CONTROL_FLCE2 | BALLOON3_NAND_CONTROL_FLCE3,
		BALLOON3_NAND_CONTROL_REG + BALLOON3_FPGA_SETnCLR);

	/* Deassert correct nCE line */
	__raw_writew(BALLOON3_NAND_CONTROL_FLCE0 << chip,
		BALLOON3_NAND_CONTROL_REG);
}

static int balloon3_nand_dev_ready(struct mtd_info *mtd)
{
	return __raw_readl(BALLOON3_NAND_STAT_REG) & BALLOON3_NAND_STAT_RNB;
}

static int balloon3_nand_probe(struct platform_device *pdev)
{
	uint16_t ver;
	int ret;

	__raw_writew(BALLOON3_NAND_CONTROL2_16BIT,
		BALLOON3_NAND_CONTROL2_REG + BALLOON3_FPGA_SETnCLR);

	ver = __raw_readw(BALLOON3_FPGA_VER);
	if (ver < 0x4f08)
		pr_warn("The FPGA code, version 0x%04x, is too old. "
			"NAND support might be broken in this version!", ver);

	/* Power up the NAND chips */
	ret = gpio_request(BALLOON3_GPIO_RUN_NAND, "NAND");
	if (ret)
		goto err1;

	ret = gpio_direction_output(BALLOON3_GPIO_RUN_NAND, 1);
	if (ret)
		goto err2;

	gpio_set_value(BALLOON3_GPIO_RUN_NAND, 1);

	/* Deassert all nCE lines and write protect line */
	__raw_writel(
		BALLOON3_NAND_CONTROL_FLCE0 | BALLOON3_NAND_CONTROL_FLCE1 |
		BALLOON3_NAND_CONTROL_FLCE2 | BALLOON3_NAND_CONTROL_FLCE3 |
		BALLOON3_NAND_CONTROL_FLWP,
		BALLOON3_NAND_CONTROL_REG + BALLOON3_FPGA_SETnCLR);
	return 0;

err2:
	gpio_free(BALLOON3_GPIO_RUN_NAND);
err1:
	return ret;
}

static void balloon3_nand_remove(struct platform_device *pdev)
{
	/* Power down the NAND chips */
	gpio_set_value(BALLOON3_GPIO_RUN_NAND, 0);
	gpio_free(BALLOON3_GPIO_RUN_NAND);
}

static struct mtd_partition balloon3_partition_info[] = {
	[0] = {
		.name	= "Boot",
		.offset	= 0,
		.size	= SZ_4M,
	},
	[1] = {
		.name	= "RootFS",
		.offset	= MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL
	},
};

struct platform_nand_data balloon3_nand_pdata = {
	.chip = {
		.nr_chips	= 4,
		.chip_offset	= 0,
		.nr_partitions	= ARRAY_SIZE(balloon3_partition_info),
		.partitions	= balloon3_partition_info,
		.chip_delay	= 50,
	},
	.ctrl = {
		.hwcontrol	= 0,
		.dev_ready	= balloon3_nand_dev_ready,
		.select_chip	= balloon3_nand_select_chip,
		.cmd_ctrl	= balloon3_nand_cmd_ctl,
		.probe		= balloon3_nand_probe,
		.remove		= balloon3_nand_remove,
	},
};

static struct resource balloon3_nand_resource[] = {
	[0] = {
		.start = BALLOON3_NAND_BASE,
		.end   = BALLOON3_NAND_BASE + 0x4,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device balloon3_nand = {
	.name		= "gen_nand",
	.num_resources	= ARRAY_SIZE(balloon3_nand_resource),
	.resource	= balloon3_nand_resource,
	.id		= -1,
	.dev		= {
		.platform_data = &balloon3_nand_pdata,
	}
};

static void __init balloon3_nand_init(void)
{
	platform_device_register(&balloon3_nand);
}
#else
static inline void balloon3_nand_init(void) {}
#endif

/******************************************************************************
 * Core power regulator
 ******************************************************************************/
#if defined(CONFIG_REGULATOR_MAX1586) || \
    defined(CONFIG_REGULATOR_MAX1586_MODULE)
static struct regulator_consumer_supply balloon3_max1587a_consumers[] = {
	REGULATOR_SUPPLY("vcc_core", NULL),
};

static struct regulator_init_data balloon3_max1587a_v3_info = {
	.constraints = {
		.name		= "vcc_core range",
		.min_uV		= 900000,
		.max_uV		= 1705000,
		.always_on	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE,
	},
	.consumer_supplies	= balloon3_max1587a_consumers,
	.num_consumer_supplies	= ARRAY_SIZE(balloon3_max1587a_consumers),
};

static struct max1586_subdev_data balloon3_max1587a_subdevs[] = {
	{
		.name		= "vcc_core",
		.id		= MAX1586_V3,
		.platform_data	= &balloon3_max1587a_v3_info,
	}
};

static struct max1586_platform_data balloon3_max1587a_info = {
	.subdevs     = balloon3_max1587a_subdevs,
	.num_subdevs = ARRAY_SIZE(balloon3_max1587a_subdevs),
	.v3_gain     = MAX1586_GAIN_R24_3k32, /* 730..1550 mV */
};

static struct i2c_board_info __initdata balloon3_pi2c_board_info[] = {
	{
		I2C_BOARD_INFO("max1586", 0x14),
		.platform_data	= &balloon3_max1587a_info,
	},
};

static void __init balloon3_pmic_init(void)
{
	pxa27x_set_i2c_power_info(NULL);
	i2c_register_board_info(1, ARRAY_AND_SIZE(balloon3_pi2c_board_info));
}
#else
static inline void balloon3_pmic_init(void) {}
#endif

/******************************************************************************
 * Machine init
 ******************************************************************************/
static void __init balloon3_init(void)
{
	ARB_CNTRL = ARB_CORE_PARK | 0x234;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(balloon3_pin_config));

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	balloon3_i2c_init();
	balloon3_irda_init();
	balloon3_lcd_init();
	balloon3_leds_init();
	balloon3_mmc_init();
	balloon3_nand_init();
	balloon3_nor_init();
	balloon3_pmic_init();
	balloon3_ts_init();
	balloon3_udc_init();
	balloon3_uhc_init();
	balloon3_cf_init();
}

static struct map_desc balloon3_io_desc[] __initdata = {
	{	/* CPLD/FPGA */
		.virtual	= (unsigned long)BALLOON3_FPGA_VIRT,
		.pfn		= __phys_to_pfn(BALLOON3_FPGA_PHYS),
		.length		= BALLOON3_FPGA_LENGTH,
		.type		= MT_DEVICE,
	},
};

static void __init balloon3_map_io(void)
{
	pxa27x_map_io();
	iotable_init(balloon3_io_desc, ARRAY_SIZE(balloon3_io_desc));
}

MACHINE_START(BALLOON3, "Balloon3")
	/* Maintainer: Nick Bane. */
	.map_io		= balloon3_map_io,
	.nr_irqs	= BALLOON3_NR_IRQS,
	.init_irq	= balloon3_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.init_time	= pxa_timer_init,
	.init_machine	= balloon3_init,
	.atag_offset	= 0x100,
	.restart	= pxa_restart,
MACHINE_END
