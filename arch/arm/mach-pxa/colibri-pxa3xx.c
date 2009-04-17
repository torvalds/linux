/*
 *  arch/arm/mach-pxa/colibri-pxa3xx.c
 *
 *  Common functions for all Toradex PXA3xx modules
 *
 *  Daniel Mack <daniel@caiaq.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/etherdevice.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/sizes.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <mach/pxa3xx-regs.h>
#include <mach/mfp-pxa300.h>
#include <mach/colibri.h>
#include <mach/mmc.h>
#include <mach/pxafb.h>

#include "generic.h"
#include "devices.h"

#if defined(CONFIG_AX88796)
#define ETHER_ADDR_LEN 6
static u8 ether_mac_addr[ETHER_ADDR_LEN];

void __init colibri_pxa3xx_init_eth(struct ax_plat_data *plat_data)
{
	int i;
	u64 serial = ((u64) system_serial_high << 32) | system_serial_low;

	/*
	 * If the bootloader passed in a serial boot tag, which contains a
	 * valid ethernet MAC, pass it to the interface. Toradex ships the
	 * modules with their own bootloader which provides a valid MAC
	 * this way.
	 */

	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		ether_mac_addr[i] = serial & 0xff;
		serial >>= 8;
	}

	if (is_valid_ether_addr(ether_mac_addr)) {
		plat_data->flags |= AXFLG_MAC_FROMPLATFORM;
		plat_data->mac_addr = ether_mac_addr;
		printk(KERN_INFO "%s(): taking MAC from serial boot tag\n",
			__func__);
	} else {
		plat_data->flags |= AXFLG_MAC_FROMDEV;
		printk(KERN_INFO "%s(): no valid serial boot tag found, "
			"taking MAC from device\n", __func__);
	}
}
#endif

#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
static int mmc_detect_pin;

static int colibri_pxa3xx_mci_init(struct device *dev,
				   irq_handler_t colibri_mmc_detect_int,
				   void *data)
{
	int ret;

	ret = gpio_request(mmc_detect_pin, "mmc card detect");
	if (ret)
		return ret;

	gpio_direction_input(mmc_detect_pin);
	ret = request_irq(gpio_to_irq(mmc_detect_pin), colibri_mmc_detect_int,
			  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			  "MMC card detect", data);
	if (ret) {
		gpio_free(mmc_detect_pin);
		return ret;
	}

	return 0;
}

static void colibri_pxa3xx_mci_exit(struct device *dev, void *data)
{
	free_irq(mmc_detect_pin, data);
	gpio_free(gpio_to_irq(mmc_detect_pin));
}

static struct pxamci_platform_data colibri_pxa3xx_mci_platform_data = {
	.detect_delay	= 20,
	.ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
	.init		= colibri_pxa3xx_mci_init,
	.exit		= colibri_pxa3xx_mci_exit,
};

void __init colibri_pxa3xx_init_mmc(mfp_cfg_t *pins, int len, int detect_pin)
{
	pxa3xx_mfp_config(pins, len);
	mmc_detect_pin = detect_pin;
	pxa_set_mci_info(&colibri_pxa3xx_mci_platform_data);
}
#endif /* CONFIG_MMC_PXA || CONFIG_MMC_PXA_MODULE */

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static int lcd_bl_pin;

/*
 * LCD panel (Sharp LQ043T3DX02)
 */
static void colibri_lcd_backlight(int on)
{
	gpio_set_value(lcd_bl_pin, !!on);
}

static struct pxafb_mode_info sharp_lq43_mode = {
	.pixclock	= 101936,
	.xres		= 480,
	.yres		= 272,
	.bpp		= 32,
	.depth		= 18,
	.hsync_len      = 41,
	.left_margin    = 2,
	.right_margin   = 2,
	.vsync_len      = 10,
	.upper_margin   = 2,
	.lower_margin   = 2,
	.sync	   	= 0,
	.cmap_greyscale = 0,
};

static struct pxafb_mach_info sharp_lq43_info = {
	.modes		= &sharp_lq43_mode,
	.num_modes	= 1,
	.cmap_inverse	= 0,
	.cmap_static	= 0,
	.lcd_conn	= LCD_COLOR_TFT_18BPP,
	.pxafb_backlight_power = colibri_lcd_backlight,
};

void __init colibri_pxa3xx_init_lcd(int bl_pin)
{
	lcd_bl_pin = bl_pin;
	gpio_request(bl_pin, "lcd backlight");
	gpio_direction_output(bl_pin, 0);
	set_pxa_fb_info(&sharp_lq43_info);
}
#endif

