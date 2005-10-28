/*
 * linux/arch/arm/mach-sa1100/cerf.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Apr-2003 : Removed some old PDA crud [FB]
 * Oct-2003 : Added uart2 resource [FB]
 * Jan-2004 : Removed io map for flash [FB]
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/setup.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include <asm/arch/cerf.h>
#include <asm/arch/mcp.h>
#include "generic.h"

static struct resource cerfuart2_resources[] = {
	[0] = {
		.start	= 0x80030000,
		.end	= 0x8003ffff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device cerfuart2_device = {
	.name		= "sa11x0-uart",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(cerfuart2_resources),
	.resource	= cerfuart2_resources,
};

static struct platform_device *cerf_devices[] __initdata = {
	&cerfuart2_device,
};

#ifdef CONFIG_SA1100_CERF_FLASH_32MB
#  define CERF_FLASH_SIZE	0x02000000
#elif defined CONFIG_SA1100_CERF_FLASH_16MB
#  define CERF_FLASH_SIZE	0x01000000
#elif defined CONFIG_SA1100_CERF_FLASH_8MB
#  define CERF_FLASH_SIZE	0x00800000
#else
#  error "Undefined flash size for CERF"
#endif

static struct mtd_partition cerf_partitions[] = {
	{
		.name		= "Bootloader",
		.size		= 0x00020000,
		.offset		= 0x00000000,
	}, {
		.name		= "Params",
		.size		= 0x00040000,
		.offset		= 0x00020000,
	}, {
		.name		= "Kernel",
		.size		= 0x00100000,
		.offset		= 0x00060000,
	}, {
		.name		= "Filesystem",
		.size		= CERF_FLASH_SIZE-0x00160000,
		.offset		= 0x00160000,
	}
};

static struct flash_platform_data cerf_flash_data = {
	.map_name	= "cfi_probe",
	.parts		= cerf_partitions,
	.nr_parts	= ARRAY_SIZE(cerf_partitions),
};

static struct resource cerf_flash_resource = {
	.start		= SA1100_CS0_PHYS,
	.end		= SA1100_CS0_PHYS + SZ_32M - 1,
	.flags		= IORESOURCE_MEM,
};

static void __init cerf_init_irq(void)
{
	sa1100_init_irq();
	set_irq_type(CERF_ETH_IRQ, IRQT_RISING);
}

static struct map_desc cerf_io_desc[] __initdata = {
  	{	/* Crystal Ethernet Chip */
		.virtual	=  0xf0000000,
		.pfn		= __phys_to_pfn(0x08000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}
};

static void __init cerf_map_io(void)
{
	sa1100_map_io();
	iotable_init(cerf_io_desc, ARRAY_SIZE(cerf_io_desc));

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 2); /* disable this and the uart2 device for sa1100_fir */
	sa1100_register_uart(2, 1);

	/* set some GPDR bits here while it's safe */
	GPDR |= CERF_GPIO_CF_RESET;
}

static struct mcp_plat_data cerf_mcp_data = {
	.mccr0		= MCCR0_ADM,
	.sclk_rate	= 11981000,
};

static void __init cerf_init(void)
{
	platform_add_devices(cerf_devices, ARRAY_SIZE(cerf_devices));
	sa11x0_set_flash_data(&cerf_flash_data, &cerf_flash_resource, 1);
	sa11x0_set_mcp_data(&cerf_mcp_data);
}

MACHINE_START(CERF, "Intrinsyc CerfBoard/CerfCube")
	/* Maintainer: support@intrinsyc.com */
	.phys_ram	= 0xc0000000,
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xf8000000) >> 18) & 0xfffc,
	.map_io		= cerf_map_io,
	.init_irq	= cerf_init_irq,
	.timer		= &sa1100_timer,
	.init_machine	= cerf_init,
MACHINE_END
