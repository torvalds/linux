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
#include <asm/system_info.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <mach/pxa3xx-regs.h>
#include "mfp-pxa300.h"
#include "colibri.h"
#include <linux/platform_data/mmc-pxamci.h>
#include <linux/platform_data/video-pxafb.h>
#include <linux/platform_data/mtd-nand-pxa3xx.h>

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
	pxa_set_fb_info(NULL, &sharp_lq43_info);
}
#endif

#if defined(CONFIG_MTD_NAND_PXA3xx) || defined(CONFIG_MTD_NAND_PXA3xx_MODULE)
static struct mtd_partition colibri_nand_partitions[] = {
	{
		.name        = "bootloader",
		.offset      = 0,
		.size        = SZ_512K,
		.mask_flags  = MTD_WRITEABLE, /* force read-only */
	},
	{
		.name        = "kernel",
		.offset      = MTDPART_OFS_APPEND,
		.size        = SZ_4M,
		.mask_flags  = MTD_WRITEABLE, /* force read-only */
	},
	{
		.name        = "reserved",
		.offset      = MTDPART_OFS_APPEND,
		.size        = SZ_1M,
		.mask_flags  = MTD_WRITEABLE, /* force read-only */
	},
	{
		.name        = "fs",
		.offset      = MTDPART_OFS_APPEND,
		.size        = MTDPART_SIZ_FULL,
	},
};

static struct pxa3xx_nand_platform_data colibri_nand_info = {
	.enable_arbiter	= 1,
	.keep_config	= 1,
	.num_cs		= 1,
	.parts[0]	= colibri_nand_partitions,
	.nr_parts[0]	= ARRAY_SIZE(colibri_nand_partitions),
};

void __init colibri_pxa3xx_init_nand(void)
{
	pxa3xx_set_nand_info(&colibri_nand_info);
}
#endif

