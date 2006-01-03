/*
 * linux/arch/arm/mach-omap1/board-h3.c
 *
 * This file contains OMAP1710 H3 specific code.
 *
 * Copyright (C) 2004 Texas Instruments, Inc.
 * Copyright (C) 2002 MontaVista Software, Inc.
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: RidgeRun, Inc.
 *         Greg Lonnon (glonnon@ridgerun.com) or info@ridgerun.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>

#include <asm/arch/gpio.h>
#include <asm/arch/irqs.h>
#include <asm/arch/mux.h>
#include <asm/arch/tc.h>
#include <asm/arch/usb.h>
#include <asm/arch/common.h>

extern int omap_gpio_init(void);

static struct mtd_partition h3_partitions[] = {
	/* bootloader (U-Boot, etc) in first sector */
	{
	      .name		= "bootloader",
	      .offset		= 0,
	      .size		= SZ_128K,
	      .mask_flags	= MTD_WRITEABLE, /* force read-only */
	},
	/* bootloader params in the next sector */
	{
	      .name		= "params",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= SZ_128K,
	      .mask_flags	= 0,
	},
	/* kernel */
	{
	      .name		= "kernel",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= SZ_2M,
	      .mask_flags	= 0
	},
	/* file system */
	{
	      .name		= "filesystem",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= MTDPART_SIZ_FULL,
	      .mask_flags	= 0
	}
};

static struct flash_platform_data h3_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
	.parts		= h3_partitions,
	.nr_parts	= ARRAY_SIZE(h3_partitions),
};

static struct resource h3_flash_resource = {
	/* This is on CS3, wherever it's mapped */
	.flags		= IORESOURCE_MEM,
};

static struct platform_device flash_device = {
	.name		= "omapflash",
	.id		= 0,
	.dev		= {
		.platform_data	= &h3_flash_data,
	},
	.num_resources	= 1,
	.resource	= &h3_flash_resource,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= OMAP1710_ETHR_START,		/* Physical */
		.end	= OMAP1710_ETHR_START + 0xf,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= OMAP_GPIO_IRQ(40),
		.end	= OMAP_GPIO_IRQ(40),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

#define GPTIMER_BASE		0xFFFB1400
#define GPTIMER_REGS(x)	(0xFFFB1400 + (x * 0x800))
#define GPTIMER_REGS_SIZE	0x46

static struct resource intlat_resources[] = {
	[0] = {
		.start  = GPTIMER_REGS(0),	      /* Physical */
		.end    = GPTIMER_REGS(0) + GPTIMER_REGS_SIZE,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = INT_1610_GPTIMER1,
		.end    = INT_1610_GPTIMER1,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device intlat_device = {
	.name	   = "omap_intlat",
	.id	     = 0,
	.num_resources  = ARRAY_SIZE(intlat_resources),
	.resource       = intlat_resources,
};

static struct platform_device *devices[] __initdata = {
	&flash_device,
        &smc91x_device,
	&intlat_device,
};

static struct omap_usb_config h3_usb_config __initdata = {
	/* usb1 has a Mini-AB port and external isp1301 transceiver */
	.otg	    = 2,

#ifdef CONFIG_USB_GADGET_OMAP
	.hmc_mode       = 19,   /* 0:host(off) 1:dev|otg 2:disabled */
#elif  defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
	/* NONSTANDARD CABLE NEEDED (B-to-Mini-B) */
	.hmc_mode       = 20,   /* 1:dev|otg(off) 1:host 2:disabled */
#endif

	.pins[1]	= 3,
};

static struct omap_mmc_config h3_mmc_config __initdata = {
	.mmc[0] = {
		.enabled 	= 1,
		.power_pin	= -1,   /* tps65010 GPIO4 */
		.switch_pin	= OMAP_MPUIO(1),
	},
};

static struct omap_uart_config h3_uart_config __initdata = {
	.enabled_uarts = ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_lcd_config h3_lcd_config __initdata = {
	.panel_name	= "h3",
	.ctrl_name	= "internal",
};

static struct omap_board_config_kernel h3_config[] = {
	{ OMAP_TAG_USB,		&h3_usb_config },
	{ OMAP_TAG_MMC,		&h3_mmc_config },
	{ OMAP_TAG_UART,	&h3_uart_config },
	{ OMAP_TAG_LCD,		&h3_lcd_config },
};

static void __init h3_init(void)
{
	h3_flash_resource.end = h3_flash_resource.start = omap_cs3_phys();
	h3_flash_resource.end += OMAP_CS3_SIZE - 1;
	(void) platform_add_devices(devices, ARRAY_SIZE(devices));
	omap_board_config = h3_config;
	omap_board_config_size = ARRAY_SIZE(h3_config);
	omap_serial_init();
}

static void __init h3_init_smc91x(void)
{
	omap_cfg_reg(W15_1710_GPIO40);
	if (omap_request_gpio(40) < 0) {
		printk("Error requesting gpio 40 for smc91x irq\n");
		return;
	}
}

void h3_init_irq(void)
{
	omap_init_irq();
	omap_gpio_init();
	h3_init_smc91x();
}

static void __init h3_map_io(void)
{
	omap_map_common_io();
}

MACHINE_START(OMAP_H3, "TI OMAP1710 H3 board")
	/* Maintainer: Texas Instruments, Inc. */
	.phys_ram	= 0x10000000,
	.phys_io	= 0xfff00000,
	.io_pg_offst	= ((0xfef00000) >> 18) & 0xfffc,
	.boot_params	= 0x10000100,
	.map_io		= h3_map_io,
	.init_irq	= h3_init_irq,
	.init_machine	= h3_init,
	.timer		= &omap_timer,
MACHINE_END
