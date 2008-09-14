/*
 * arch/arm/mach-loki/lb88rc8480-setup.c
 *
 * Marvell LB88RC8480 Development Board Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/nand.h>
#include <linux/timer.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/loki.h>
#include "common.h"

#define LB88RC8480_FLASH_BOOT_CS_BASE	0xf8000000
#define LB88RC8480_FLASH_BOOT_CS_SIZE	SZ_128M

#define LB88RC8480_NOR_BOOT_BASE	0xff000000
#define LB88RC8480_NOR_BOOT_SIZE	SZ_16M

static struct mtd_partition lb88rc8480_boot_flash_parts[] = {
	{
		.name	= "kernel",
		.offset	= 0,
		.size	= SZ_2M,
	}, {
		.name	= "root-fs",
		.offset	= SZ_2M,
		.size	= (SZ_8M + SZ_4M + SZ_1M),
	}, {
		.name	= "u-boot",
		.offset	= (SZ_8M + SZ_4M + SZ_2M + SZ_1M),
		.size	= SZ_1M,
	},
};

static struct physmap_flash_data lb88rc8480_boot_flash_data = {
	.parts		= lb88rc8480_boot_flash_parts,
	.nr_parts	= ARRAY_SIZE(lb88rc8480_boot_flash_parts),
	.width		= 1,	/* 8 bit bus width */
};

static struct resource lb88rc8480_boot_flash_resource = {
	.flags	= IORESOURCE_MEM,
	.start	= LB88RC8480_NOR_BOOT_BASE,
	.end	= LB88RC8480_NOR_BOOT_BASE + LB88RC8480_NOR_BOOT_SIZE - 1,
};

static struct platform_device lb88rc8480_boot_flash = {
	.name	= "physmap-flash",
	.id	= 0,
	.dev	= {
		.platform_data	= &lb88rc8480_boot_flash_data,
	},
	.num_resources	= 1,
	.resource	= &lb88rc8480_boot_flash_resource,
};

static struct mv643xx_eth_platform_data lb88rc8480_ge0_data = {
	.phy_addr	= 1,
	.mac_addr	= { 0x00, 0x50, 0x43, 0x11, 0x22, 0x33 },
};

static void __init lb88rc8480_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	loki_init();

	loki_ge0_init(&lb88rc8480_ge0_data);
	loki_sas_init();
	loki_uart0_init();
	loki_uart1_init();

	loki_setup_dev_boot_win(LB88RC8480_FLASH_BOOT_CS_BASE,
				LB88RC8480_FLASH_BOOT_CS_SIZE);
	platform_device_register(&lb88rc8480_boot_flash);
}

MACHINE_START(LB88RC8480, "Marvell LB88RC8480 Development Board")
	/* Maintainer: Ke Wei <kewei@marvell.com> */
	.phys_io	= LOKI_REGS_PHYS_BASE,
	.io_pg_offst	= ((LOKI_REGS_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.init_machine	= lb88rc8480_init,
	.map_io		= loki_map_io,
	.init_irq	= loki_init_irq,
	.timer		= &loki_timer,
MACHINE_END
