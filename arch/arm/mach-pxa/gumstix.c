/*
 *  linux/arch/arm/mach-pxa/gumstix.c
 *
 *  Support for the Gumstix motherboards.
 *
 *  Original Author:	Craig Hughes
 *  Created:	Feb 14, 2008
 *  Copyright:	Craig Hughes
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  Implemented based on lubbock.c by Nicolas Pitre and code from Craig
 *  Hughes
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/sizes.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/flash.h>
#include <asm/arch/mmc.h>
#include <asm/arch/udc.h>
#include <asm/arch/gumstix.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/pxa2xx-regs.h>
#include <asm/arch/pxa2xx-gpio.h>

#include "generic.h"

static struct resource flash_resource = {
	.start	= 0x00000000,
	.end	= SZ_64M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct mtd_partition gumstix_partitions[] = {
	{
		.name =		"Bootloader",
		.size =		0x00040000,
		.offset =	0,
		.mask_flags =	MTD_WRITEABLE  /* force read-only */
	} , {
		.name =		"rootfs",
		.size =		MTDPART_SIZ_FULL,
		.offset =	MTDPART_OFS_APPEND
	}
};

static struct flash_platform_data gumstix_flash_data = {
	.map_name	= "cfi_probe",
	.parts		= gumstix_partitions,
	.nr_parts	= ARRAY_SIZE(gumstix_partitions),
	.width		= 2,
};

static struct platform_device gumstix_flash_device = {
	.name		= "pxa2xx-flash",
	.id		= 0,
	.dev = {
		.platform_data = &gumstix_flash_data,
	},
	.resource = &flash_resource,
	.num_resources = 1,
};

static struct platform_device *devices[] __initdata = {
	&gumstix_flash_device,
};

#ifdef CONFIG_MMC_PXA
static struct pxamci_platform_data gumstix_mci_platform_data;

static int gumstix_mci_init(struct device *dev, irq_handler_t detect_int,
				void *data)
{
	pxa_gpio_mode(GPIO6_MMCCLK_MD);
	pxa_gpio_mode(GPIO53_MMCCLK_MD);
	pxa_gpio_mode(GPIO8_MMCCS0_MD);

	return 0;
}

static struct pxamci_platform_data gumstix_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.init		= gumstix_mci_init,
};

static void __init gumstix_mmc_init(void)
{
	pxa_set_mci_info(&gumstix_mci_platform_data);
}
#else
static void __init gumstix_mmc_init(void)
{
	printk(KERN_INFO "Gumstix mmc disabled\n");
}
#endif

#ifdef CONFIG_USB_GADGET_PXA2XX
static struct pxa2xx_udc_mach_info gumstix_udc_info __initdata = {
	.gpio_vbus		= GPIO_GUMSTIX_USB_GPIOn,
	.gpio_pullup		= GPIO_GUMSTIX_USB_GPIOx,
};

static void __init gumstix_udc_init(void)
{
	pxa_set_udc_info(&gumstix_udc_info);
}
#else
static void gumstix_udc_init(void)
{
	printk(KERN_INFO "Gumstix udc is disabled\n");
}
#endif

static void __init gumstix_init(void)
{
	gumstix_udc_init();
	gumstix_mmc_init();
	(void) platform_add_devices(devices, ARRAY_SIZE(devices));
}

MACHINE_START(GUMSTIX, "Gumstix")
	.phys_io	= 0x40000000,
	.boot_params	= 0xa0000100, /* match u-boot bi_boot_params */
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.map_io		= pxa_map_io,
	.init_irq	= pxa25x_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= gumstix_init,
MACHINE_END
