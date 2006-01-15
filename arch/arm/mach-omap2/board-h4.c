/*
 * linux/arch/arm/mach-omap/omap2/board-h4.c
 *
 * Copyright (C) 2005 Nokia Corporation
 * Author: Paul Mundt <paul.mundt@nokia.com>
 *
 * Modified from mach-omap/omap1/board-generic.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/delay.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <asm/arch/gpio.h>
#include <asm/arch/mux.h>
#include <asm/arch/usb.h>
#include <asm/arch/board.h>
#include <asm/arch/common.h>
#include <asm/arch/prcm.h>

#include <asm/io.h>
#include <asm/delay.h>

static struct mtd_partition h4_partitions[] = {
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

static struct flash_platform_data h4_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
	.parts		= h4_partitions,
	.nr_parts	= ARRAY_SIZE(h4_partitions),
};

static struct resource h4_flash_resource = {
	.start		= H4_CS0_BASE,
	.end		= H4_CS0_BASE + SZ_64M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device h4_flash_device = {
	.name		= "omapflash",
	.id		= 0,
	.dev		= {
		.platform_data	= &h4_flash_data,
	},
	.num_resources	= 1,
	.resource	= &h4_flash_resource,
};

static struct resource h4_smc91x_resources[] = {
	[0] = {
		.start  = OMAP24XX_ETHR_START,          /* Physical */
		.end    = OMAP24XX_ETHR_START + 0xf,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = OMAP_GPIO_IRQ(OMAP24XX_ETHR_GPIO_IRQ),
		.end    = OMAP_GPIO_IRQ(OMAP24XX_ETHR_GPIO_IRQ),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device h4_smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(h4_smc91x_resources),
	.resource	= h4_smc91x_resources,
};

static struct platform_device *h4_devices[] __initdata = {
	&h4_smc91x_device,
	&h4_flash_device,
};

static inline void __init h4_init_smc91x(void)
{
	/* Make sure CS1 timings are correct */
	GPMC_CONFIG1_1 = 0x00011200;
	GPMC_CONFIG2_1 = 0x001f1f01;
	GPMC_CONFIG3_1 = 0x00080803;
	GPMC_CONFIG4_1 = 0x1c091c09;
	GPMC_CONFIG5_1 = 0x041f1f1f;
	GPMC_CONFIG6_1 = 0x000004c4;
	GPMC_CONFIG7_1 = 0x00000f40 | (0x08000000 >> 24);
	udelay(100);

	omap_cfg_reg(M15_24XX_GPIO92);
	if (omap_request_gpio(OMAP24XX_ETHR_GPIO_IRQ) < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for smc91x IRQ\n",
			OMAP24XX_ETHR_GPIO_IRQ);
		return;
	}
	omap_set_gpio_direction(OMAP24XX_ETHR_GPIO_IRQ, 1);
}

static void __init omap_h4_init_irq(void)
{
	omap_init_irq();
	omap_gpio_init();
	h4_init_smc91x();
}

static struct omap_uart_config h4_uart_config __initdata = {
	.enabled_uarts = ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_mmc_config h4_mmc_config __initdata = {
	.mmc [0] = {
		.enabled	= 1,
		.wire4		= 1,
		.wp_pin		= -1,
		.power_pin	= -1,
		.switch_pin	= -1,
	},
};

static struct omap_lcd_config h4_lcd_config __initdata = {
	.panel_name	= "h4",
	.ctrl_name	= "internal",
};

static struct omap_board_config_kernel h4_config[] = {
	{ OMAP_TAG_UART,	&h4_uart_config },
	{ OMAP_TAG_MMC,		&h4_mmc_config },
	{ OMAP_TAG_LCD,		&h4_lcd_config },
};

static void __init omap_h4_init(void)
{
	/*
	 * Make sure the serial ports are muxed on at this point.
	 * You have to mux them off in device drivers later on
	 * if not needed.
	 */
	platform_add_devices(h4_devices, ARRAY_SIZE(h4_devices));
	omap_board_config = h4_config;
	omap_board_config_size = ARRAY_SIZE(h4_config);
	omap_serial_init();
}

static void __init omap_h4_map_io(void)
{
	omap_map_common_io();
}

MACHINE_START(OMAP_H4, "OMAP2420 H4 board")
	/* Maintainer: Paul Mundt <paul.mundt@nokia.com> */
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap_h4_map_io,
	.init_irq	= omap_h4_init_irq,
	.init_machine	= omap_h4_init,
	.timer		= &omap_timer,
MACHINE_END
