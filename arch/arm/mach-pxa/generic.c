/*
 *  linux/arch/arm/mach-pxa/generic.c
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * Code common to all PXA machines.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Since this file should be linked before any other machine specific file,
 * the __initcall() here will be executed first.  This serves as default
 * initialization stuff for PXA machines which can be overridden later if
 * need be.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/pm.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/udc.h>
#include <asm/arch/pxafb.h>
#include <asm/arch/mmc.h>

#include "generic.h"

/*
 * Handy function to set GPIO alternate functions
 */

void pxa_gpio_mode(int gpio_mode)
{
	unsigned long flags;
	int gpio = gpio_mode & GPIO_MD_MASK_NR;
	int fn = (gpio_mode & GPIO_MD_MASK_FN) >> 8;
	int gafr;

	local_irq_save(flags);
	if (gpio_mode & GPIO_DFLT_LOW)
		GPCR(gpio) = GPIO_bit(gpio);
	else if (gpio_mode & GPIO_DFLT_HIGH)
		GPSR(gpio) = GPIO_bit(gpio);
	if (gpio_mode & GPIO_MD_MASK_DIR)
		GPDR(gpio) |= GPIO_bit(gpio);
	else
		GPDR(gpio) &= ~GPIO_bit(gpio);
	gafr = GAFR(gpio) & ~(0x3 << (((gpio) & 0xf)*2));
	GAFR(gpio) = gafr |  (fn  << (((gpio) & 0xf)*2));
	local_irq_restore(flags);
}

EXPORT_SYMBOL(pxa_gpio_mode);

/*
 * Routine to safely enable or disable a clock in the CKEN
 */
void pxa_set_cken(int clock, int enable)
{
	unsigned long flags;
	local_irq_save(flags);

	if (enable)
		CKEN |= clock;
	else
		CKEN &= ~clock;

	local_irq_restore(flags);
}

EXPORT_SYMBOL(pxa_set_cken);

/*
 * Intel PXA2xx internal register mapping.
 *
 * Note 1: not all PXA2xx variants implement all those addresses.
 *
 * Note 2: virtual 0xfffe0000-0xffffffff is reserved for the vector table
 *         and cache flush area.
 */
static struct map_desc standard_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf2000000, 0x40000000, 0x02000000, MT_DEVICE }, /* Devs */
  { 0xf4000000, 0x44000000, 0x00100000, MT_DEVICE }, /* LCD */
  { 0xf6000000, 0x48000000, 0x00100000, MT_DEVICE }, /* Mem Ctl */
  { 0xf8000000, 0x4c000000, 0x00100000, MT_DEVICE }, /* USB host */
  { 0xfa000000, 0x50000000, 0x00100000, MT_DEVICE }, /* Camera */
  { 0xfe000000, 0x58000000, 0x00100000, MT_DEVICE }, /* IMem ctl */
  { 0xff000000, 0x00000000, 0x00100000, MT_DEVICE }  /* UNCACHED_PHYS_0 */
};

void __init pxa_map_io(void)
{
	iotable_init(standard_io_desc, ARRAY_SIZE(standard_io_desc));
	get_clk_frequency_khz(1);
}


static struct resource pxamci_resources[] = {
	[0] = {
		.start	= 0x41100000,
		.end	= 0x41100fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_MMC,
		.end	= IRQ_MMC,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 pxamci_dmamask = 0xffffffffUL;

static struct platform_device pxamci_device = {
	.name		= "pxa2xx-mci",
	.id		= -1,
	.dev		= {
		.dma_mask = &pxamci_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(pxamci_resources),
	.resource	= pxamci_resources,
};

void __init pxa_set_mci_info(struct pxamci_platform_data *info)
{
	pxamci_device.dev.platform_data = info;
}


static struct pxa2xx_udc_mach_info pxa_udc_info;

void __init pxa_set_udc_info(struct pxa2xx_udc_mach_info *info)
{
	memcpy(&pxa_udc_info, info, sizeof *info);
}

static struct resource pxa2xx_udc_resources[] = {
	[0] = {
		.start	= 0x40600000,
		.end	= 0x4060ffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USB,
		.end	= IRQ_USB,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 udc_dma_mask = ~(u32)0;

static struct platform_device udc_device = {
	.name		= "pxa2xx-udc",
	.id		= -1,
	.resource	= pxa2xx_udc_resources,
	.num_resources	= ARRAY_SIZE(pxa2xx_udc_resources),
	.dev		=  {
		.platform_data	= &pxa_udc_info,
		.dma_mask	= &udc_dma_mask,
	}
};

static struct pxafb_mach_info pxa_fb_info;

void __init set_pxa_fb_info(struct pxafb_mach_info *hard_pxa_fb_info)
{
	memcpy(&pxa_fb_info,hard_pxa_fb_info,sizeof(struct pxafb_mach_info));
}

static struct resource pxafb_resources[] = {
	[0] = {
		.start	= 0x44000000,
		.end	= 0x4400ffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_LCD,
		.end	= IRQ_LCD,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 fb_dma_mask = ~(u64)0;

static struct platform_device pxafb_device = {
	.name		= "pxa2xx-fb",
	.id		= -1,
	.dev		= {
 		.platform_data	= &pxa_fb_info,
		.dma_mask	= &fb_dma_mask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(pxafb_resources),
	.resource	= pxafb_resources,
};

static struct platform_device ffuart_device = {
	.name		= "pxa2xx-uart",
	.id		= 0,
};
static struct platform_device btuart_device = {
	.name		= "pxa2xx-uart",
	.id		= 1,
};
static struct platform_device stuart_device = {
	.name		= "pxa2xx-uart",
	.id		= 2,
};

static struct platform_device *devices[] __initdata = {
	&pxamci_device,
	&udc_device,
	&pxafb_device,
	&ffuart_device,
	&btuart_device,
	&stuart_device,
};

static int __init pxa_init(void)
{
	return platform_add_devices(devices, ARRAY_SIZE(devices));
}

subsys_initcall(pxa_init);
