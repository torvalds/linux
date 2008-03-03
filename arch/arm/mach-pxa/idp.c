/*
 *  linux/arch/arm/mach-pxa/idp.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  Copyright (c) 2001 Cliff Brake, Accelent Systems Inc.
 *
 *  2001-09-13: Cliff Brake <cbrake@accelent.com>
 *              Initial code
 *
 *  2005-02-15: Cliff Brake <cliff.brake@gmail.com>
 *  		<http://www.vibren.com> <http://bec-systems.com>
 *              Updated for 2.6 kernel
 *
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/fb.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/pxa2xx-gpio.h>
#include <asm/arch/idp.h>
#include <asm/arch/pxafb.h>
#include <asm/arch/bitfield.h>
#include <asm/arch/mmc.h>

#include "generic.h"
#include "devices.h"

/* TODO:
 * - add pxa2xx_audio_ops_t device structure
 * - Ethernet interrupt
 */

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= (IDP_ETH_PHYS + 0x300),
		.end	= (IDP_ETH_PHYS + 0xfffff),
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_GPIO(4),
		.end	= IRQ_GPIO(4),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static void idp_backlight_power(int on)
{
	if (on) {
		IDP_CPLD_LCD |= (1<<1);
	} else {
		IDP_CPLD_LCD &= ~(1<<1);
	}
}

static void idp_vlcd(int on)
{
	if (on) {
		IDP_CPLD_LCD |= (1<<2);
	} else {
		IDP_CPLD_LCD &= ~(1<<2);
	}
}

static void idp_lcd_power(int on, struct fb_var_screeninfo *var)
{
	if (on) {
		IDP_CPLD_LCD |= (1<<0);
	} else {
		IDP_CPLD_LCD &= ~(1<<0);
	}

	/* call idp_vlcd for now as core driver does not support
	 * both power and vlcd hooks.  Note, this is not technically
	 * the correct sequence, but seems to work.  Disclaimer:
	 * this may eventually damage the display.
	 */

	idp_vlcd(on);
}

static struct pxafb_mode_info sharp_lm8v31_mode = {
	.pixclock	= 270000,
	.xres		= 640,
	.yres		= 480,
	.bpp		= 16,
	.hsync_len	= 1,
	.left_margin	= 3,
	.right_margin	= 3,
	.vsync_len	= 1,
	.upper_margin	= 0,
	.lower_margin	= 0,
	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.cmap_greyscale	= 0,
};

static struct pxafb_mach_info sharp_lm8v31 = {
	.modes          = &sharp_lm8v31_mode,
	.num_modes      = 1,
	.cmap_inverse	= 0,
	.cmap_static	= 0,
	.lccr0		= LCCR0_SDS,
	.lccr3		= LCCR3_PCP | LCCR3_Acb(255),
	.pxafb_backlight_power = &idp_backlight_power,
	.pxafb_lcd_power = &idp_lcd_power
};

static int idp_mci_init(struct device *dev, irq_handler_t idp_detect_int, void *data)
{
	/* setup GPIO for PXA25x MMC controller	*/
	pxa_gpio_mode(GPIO6_MMCCLK_MD);
	pxa_gpio_mode(GPIO8_MMCCS0_MD);

	return 0;
}

static struct pxamci_platform_data idp_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.init 		= idp_mci_init,
};

static void __init idp_init(void)
{
	printk("idp_init()\n");

	platform_device_register(&smc91x_device);
	//platform_device_register(&mst_audio_device);
	set_pxa_fb_info(&sharp_lm8v31);
	pxa_set_mci_info(&idp_mci_platform_data);
}

static void __init idp_init_irq(void)
{

	pxa25x_init_irq();

	set_irq_type(TOUCH_PANEL_IRQ, TOUCH_PANEL_IRQ_EDGE);
}

static struct map_desc idp_io_desc[] __initdata = {
  	{
		.virtual	=  IDP_COREVOLT_VIRT,
		.pfn		= __phys_to_pfn(IDP_COREVOLT_PHYS),
		.length		= IDP_COREVOLT_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	=  IDP_CPLD_VIRT,
		.pfn		= __phys_to_pfn(IDP_CPLD_PHYS),
		.length		= IDP_CPLD_SIZE,
		.type		= MT_DEVICE
	}
};

static void __init idp_map_io(void)
{
	pxa_map_io();
	iotable_init(idp_io_desc, ARRAY_SIZE(idp_io_desc));

	// serial ports 2 & 3
	pxa_gpio_mode(GPIO42_BTRXD_MD);
	pxa_gpio_mode(GPIO43_BTTXD_MD);
	pxa_gpio_mode(GPIO44_BTCTS_MD);
	pxa_gpio_mode(GPIO45_BTRTS_MD);
	pxa_gpio_mode(GPIO46_STRXD_MD);
	pxa_gpio_mode(GPIO47_STTXD_MD);

}


MACHINE_START(PXA_IDP, "Vibren PXA255 IDP")
	/* Maintainer: Vibren Technologies */
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.map_io		= idp_map_io,
	.init_irq	= idp_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= idp_init,
MACHINE_END
