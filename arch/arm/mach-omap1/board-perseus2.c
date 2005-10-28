/*
 * linux/arch/arm/mach-omap1/board-perseus2.c
 *
 * Modified from board-generic.c
 *
 * Original OMAP730 support by Jean Pihet <j-pihet@ti.com>
 * Updated for 2.6 by Kevin Hilman <kjh@hilman.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>

#include <asm/arch/tc.h>
#include <asm/arch/gpio.h>
#include <asm/arch/mux.h>
#include <asm/arch/fpga.h>
#include <asm/arch/common.h>

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= H2P2_DBG_FPGA_ETHR_START,	/* Physical */
		.end	= H2P2_DBG_FPGA_ETHR_START + 0xf,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= INT_730_MPU_EXT_NIRQ,
		.end	= 0,
		.flags	= IORESOURCE_IRQ,
	},
};

static int __initdata p2_serial_ports[OMAP_MAX_NR_PORTS] = {1, 1, 0};

static struct mtd_partition p2_partitions[] = {
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
	/* rest of flash is a file system */
	{
	      .name		= "rootfs",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= MTDPART_SIZ_FULL,
	      .mask_flags	= 0
	},
};

static struct flash_platform_data p2_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
	.parts		= p2_partitions,
	.nr_parts	= ARRAY_SIZE(p2_partitions),
};

static struct resource p2_flash_resource = {
	.start		= OMAP_CS0_PHYS,
	.end		= OMAP_CS0_PHYS + SZ_32M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device p2_flash_device = {
	.name		= "omapflash",
	.id		= 0,
	.dev		= {
		.platform_data	= &p2_flash_data,
	},
	.num_resources	= 1,
	.resource	= &p2_flash_resource,
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static struct platform_device *devices[] __initdata = {
	&p2_flash_device,
	&smc91x_device,
};

static void __init omap_perseus2_init(void)
{
	(void) platform_add_devices(devices, ARRAY_SIZE(devices));
}

static void __init perseus2_init_smc91x(void)
{
	fpga_write(1, H2P2_DBG_FPGA_LAN_RESET);
	mdelay(50);
	fpga_write(fpga_read(H2P2_DBG_FPGA_LAN_RESET) & ~1,
		   H2P2_DBG_FPGA_LAN_RESET);
	mdelay(50);
}

void omap_perseus2_init_irq(void)
{
	omap_init_irq();
	omap_gpio_init();
	perseus2_init_smc91x();
}

/* Only FPGA needs to be mapped here. All others are done with ioremap */
static struct map_desc omap_perseus2_io_desc[] __initdata = {
	{
		.virtual	= H2P2_DBG_FPGA_BASE,
		.pfn		= __phys_to_pfn(H2P2_DBG_FPGA_START),
		.length		= H2P2_DBG_FPGA_SIZE,
		.type		= MT_DEVICE
	}
};

static void __init omap_perseus2_map_io(void)
{
	omap_map_common_io();
	iotable_init(omap_perseus2_io_desc,
		     ARRAY_SIZE(omap_perseus2_io_desc));

	/* Early, board-dependent init */

	/*
	 * Hold GSM Reset until needed
	 */
	omap_writew(omap_readw(OMAP730_DSP_M_CTL) & ~1, OMAP730_DSP_M_CTL);

	/*
	 * UARTs -> done automagically by 8250 driver
	 */

	/*
	 * CSx timings, GPIO Mux ... setup
	 */

	/* Flash: CS0 timings setup */
	omap_writel(0x0000fff3, OMAP730_FLASH_CFG_0);
	omap_writel(0x00000088, OMAP730_FLASH_ACFG_0);

	/*
	 * Ethernet support trough the debug board
	 * CS1 timings setup
	 */
	omap_writel(0x0000fff3, OMAP730_FLASH_CFG_1);
	omap_writel(0x00000000, OMAP730_FLASH_ACFG_1);

	/*
	 * Configure MPU_EXT_NIRQ IO in IO_CONF9 register,
	 * It is used as the Ethernet controller interrupt
	 */
	omap_writel(omap_readl(OMAP730_IO_CONF_9) & 0x1FFFFFFF, OMAP730_IO_CONF_9);
	omap_serial_init(p2_serial_ports);
}

MACHINE_START(OMAP_PERSEUS2, "OMAP730 Perseus2")
	/* Maintainer: Kevin Hilman <kjh@hilman.org> */
	.phys_ram	= 0x10000000,
	.phys_io	= 0xfff00000,
	.io_pg_offst	= ((0xfef00000) >> 18) & 0xfffc,
	.boot_params	= 0x10000100,
	.map_io		= omap_perseus2_map_io,
	.init_irq	= omap_perseus2_init_irq,
	.init_machine	= omap_perseus2_init,
	.timer		= &omap_timer,
MACHINE_END
