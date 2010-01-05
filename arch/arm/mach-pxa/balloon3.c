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

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/ioport.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/types.h>

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
#include <mach/pxafb.h>
#include <mach/mmc.h>
#include <mach/udc.h>
#include <mach/pxa27x-udc.h>
#include <mach/irda.h>
#include <mach/ohci.h>

#include <plat/i2c.h>

#include "generic.h"
#include "devices.h"

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

	return strict_strtoul(arg, 0, &balloon3_features_present);
}
early_param("balloon3_features", parse_balloon3_features);

static void balloon3_mask_irq(unsigned int irq)
{
	int balloon3_irq = (irq - BALLOON3_IRQ(0));
	balloon3_irq_enabled &= ~(1 << balloon3_irq);
	__raw_writel(~balloon3_irq_enabled, BALLOON3_INT_CONTROL_REG);
}

static void balloon3_unmask_irq(unsigned int irq)
{
	int balloon3_irq = (irq - BALLOON3_IRQ(0));
	balloon3_irq_enabled |= (1 << balloon3_irq);
	__raw_writel(~balloon3_irq_enabled, BALLOON3_INT_CONTROL_REG);
}

static struct irq_chip balloon3_irq_chip = {
	.name		= "FPGA",
	.ack		= balloon3_mask_irq,
	.mask		= balloon3_mask_irq,
	.unmask		= balloon3_unmask_irq,
};

static void balloon3_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned long pending = __raw_readl(BALLOON3_INT_CONTROL_REG) &
					balloon3_irq_enabled;

	do {
		/* clear useless edge notification */
		if (desc->chip->ack)
			desc->chip->ack(BALLOON3_AUX_NIRQ);
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
		set_irq_chip(irq, &balloon3_irq_chip);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	set_irq_chained_handler(BALLOON3_AUX_NIRQ, balloon3_irq_handler);
	set_irq_type(BALLOON3_AUX_NIRQ, IRQ_TYPE_EDGE_FALLING);

	pr_debug("%s: chained handler installed - irq %d automatically "
		"enabled\n", __func__, BALLOON3_AUX_NIRQ);
}

static void balloon3_backlight_power(int on)
{
	pr_debug("%s: power is %s\n", __func__, on ? "on" : "off");
	gpio_set_value(BALLOON3_GPIO_RUN_BACKLIGHT, on);
}

static unsigned long balloon3_lcd_pin_config[] = {
	/* LCD - 16bpp Active TFT */
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
	GPIO74_LCD_FCLK,
	GPIO75_LCD_LCLK,
	GPIO76_LCD_PCLK,
	GPIO77_LCD_BIAS,

	GPIO99_GPIO,		/* Backlight */
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

static struct pxafb_mach_info balloon3_pxafb_info = {
	.modes			= balloon3_lcd_modes,
	.num_modes		= ARRAY_SIZE(balloon3_lcd_modes),
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL,
	.pxafb_backlight_power	= balloon3_backlight_power,
};

static unsigned long balloon3_mmc_pin_config[] = {
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
};

static void balloon3_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data *p_d = dev->platform_data;

	if ((1 << vdd) & p_d->ocr_mask) {
		pr_debug("%s: on\n", __func__);
		/* FIXME something to prod here? */
	} else {
		pr_debug("%s: off\n", __func__);
		/* FIXME something to prod here? */
	}
}

static struct pxamci_platform_data balloon3_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.setpower 	= balloon3_mci_setpower,
};

static int balloon3_udc_is_connected(void)
{
	pr_debug("%s: udc connected\n", __func__);
	return 1;
}

static void balloon3_udc_command(int cmd)
{
	switch (cmd) {
	case PXA2XX_UDC_CMD_CONNECT:
		UP2OCR |= (UP2OCR_DPPUE + UP2OCR_DPPUBE);
		pr_debug("%s: connect\n", __func__);
		break;
	case PXA2XX_UDC_CMD_DISCONNECT:
		UP2OCR &= ~UP2OCR_DPPUE;
		pr_debug("%s: disconnect\n", __func__);
		break;
	}
}

static struct pxa2xx_udc_mach_info balloon3_udc_info = {
	.udc_is_connected = balloon3_udc_is_connected,
	.udc_command      = balloon3_udc_command,
};

static struct pxaficp_platform_data balloon3_ficp_platform_data = {
	.transceiver_cap  = IR_SIRMODE | IR_FIRMODE | IR_OFF,
};

static unsigned long balloon3_ohci_pin_config[] = {
	GPIO88_USBH1_PWR,
	GPIO89_USBH1_PEN,
};

static struct pxaohci_platform_data balloon3_ohci_platform_data = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT_ALL | POWER_CONTROL_LOW | POWER_SENSE_LOW,
};

static unsigned long balloon3_pin_config[] __initdata = {
	/* Select BTUART 'COM1/ttyS0' as IO option for pins 42/43/44/45 */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,
	GPIO45_BTUART_RTS,

	/* Wakeup GPIO */
	GPIO1_GPIO | WAKEUP_ON_EDGE_BOTH,

	/* NAND & IDLE LED GPIOs */
	GPIO9_GPIO,
	GPIO10_GPIO,
};

static struct gpio_led balloon3_gpio_leds[] = {
	{
		.name			= "balloon3:green:idle",
		.default_trigger	= "heartbeat",
		.gpio			= BALLOON3_GPIO_LED_IDLE,
		.active_low		= 1,
	},
	{
		.name			= "balloon3:green:nand",
		.default_trigger	= "nand-disk",
		.gpio			= BALLOON3_GPIO_LED_NAND,
		.active_low		= 1,
	},
};

static struct gpio_led_platform_data balloon3_gpio_leds_platform_data = {
	.leds		= balloon3_gpio_leds,
	.num_leds	= ARRAY_SIZE(balloon3_gpio_leds),
};

static struct platform_device balloon3led_device = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &balloon3_gpio_leds_platform_data,
	},
};

static void __init balloon3_init(void)
{
	pr_info("Initialising Balloon3\n");

	/* system bus arbiter setting
	 * - Core_Park
	 * - LCD_wt:DMA_wt:CORE_Wt = 2:3:4
	 */
	ARB_CNTRL = ARB_CORE_PARK | 0x234;

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	pxa_set_i2c_info(NULL);
	if (balloon3_has(BALLOON3_FEATURE_AUDIO))
		pxa_set_ac97_info(NULL);

	if (balloon3_has(BALLOON3_FEATURE_TOPPOLY)) {
		pxa2xx_mfp_config(ARRAY_AND_SIZE(balloon3_lcd_pin_config));
		gpio_request(BALLOON3_GPIO_RUN_BACKLIGHT,
			"LCD Backlight Power");
		gpio_direction_output(BALLOON3_GPIO_RUN_BACKLIGHT, 1);
		set_pxa_fb_info(&balloon3_pxafb_info);
	}

	if (balloon3_has(BALLOON3_FEATURE_MMC)) {
		pxa2xx_mfp_config(ARRAY_AND_SIZE(balloon3_mmc_pin_config));
		pxa_set_mci_info(&balloon3_mci_platform_data);
	}
	pxa_set_ficp_info(&balloon3_ficp_platform_data);
	if (balloon3_has(BALLOON3_FEATURE_OHCI)) {
		pxa2xx_mfp_config(ARRAY_AND_SIZE(balloon3_ohci_pin_config));
		pxa_set_ohci_info(&balloon3_ohci_platform_data);
	}
	pxa_set_udc_info(&balloon3_udc_info);

	pxa2xx_mfp_config(ARRAY_AND_SIZE(balloon3_pin_config));

	platform_device_register(&balloon3led_device);
}

static struct map_desc balloon3_io_desc[] __initdata = {
	{	/* CPLD/FPGA */
		.virtual	=  BALLOON3_FPGA_VIRT,
		.pfn		= __phys_to_pfn(BALLOON3_FPGA_PHYS),
		.length		= BALLOON3_FPGA_LENGTH,
		.type		= MT_DEVICE,
	},
};

static void __init balloon3_map_io(void)
{
	pxa_map_io();
	iotable_init(balloon3_io_desc, ARRAY_SIZE(balloon3_io_desc));
}

MACHINE_START(BALLOON3, "Balloon3")
	/* Maintainer: Nick Bane. */
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.map_io		= balloon3_map_io,
	.init_irq	= balloon3_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= balloon3_init,
	.boot_params	= PHYS_OFFSET + 0x100,
MACHINE_END
