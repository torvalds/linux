/*
 * Data Technology Inc. ESPT-GIGA board support
 *
 * Copyright (C) 2008, 2009 Renesas Solutions Corp.
 * Copyright (C) 2008, 2009 Nobuhiro Iwamatsu <iwamatsu.nobuhiro@renesas.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/mtd/physmap.h>
#include <linux/io.h>
#include <linux/sh_eth.h>
#include <linux/sh_intc.h>
#include <asm/machvec.h>
#include <asm/sizes.h>

/* NOR Flash */
static struct mtd_partition espt_nor_flash_partitions[] = {
	{
		.name = "U-Boot",
		.offset = 0,
		.size = (2 * SZ_128K),
		.mask_flags = MTD_WRITEABLE,	/* Read-only */
	}, {
		.name = "Linux-Kernel",
		.offset = MTDPART_OFS_APPEND,
		.size = (20 * SZ_128K),
	}, {
		.name = "Root Filesystem",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data espt_nor_flash_data = {
	.width = 2,
	.parts = espt_nor_flash_partitions,
	.nr_parts = ARRAY_SIZE(espt_nor_flash_partitions),
};

static struct resource espt_nor_flash_resources[] = {
	[0] = {
		.name = "NOR Flash",
		.start = 0,
		.end = SZ_8M - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device espt_nor_flash_device = {
	.name = "physmap-flash",
	.resource = espt_nor_flash_resources,
	.num_resources = ARRAY_SIZE(espt_nor_flash_resources),
	.dev = {
		.platform_data = &espt_nor_flash_data,
	},
};

/* SH-Ether */
static struct resource sh_eth_resources[] = {
	{
		.start  = 0xFEE00800,   /* use eth1 */
		.end    = 0xFEE00F7C - 1,
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = 0xFEE01800,   /* TSU */
		.end    = 0xFEE01FFF,
		.flags  = IORESOURCE_MEM,
	}, {

		.start  = evt2irq(0x920),   /* irq number */
		.flags  = IORESOURCE_IRQ,
	},
};

static struct sh_eth_plat_data sh7763_eth_pdata = {
	.phy = 0,
	.edmac_endian = EDMAC_LITTLE_ENDIAN,
	.register_type = SH_ETH_REG_GIGABIT,
	.phy_interface = PHY_INTERFACE_MODE_MII,
};

static struct platform_device espt_eth_device = {
	.name       = "sh7763-gether",
	.resource   = sh_eth_resources,
	.num_resources  = ARRAY_SIZE(sh_eth_resources),
	.dev        = {
		.platform_data = &sh7763_eth_pdata,
	},
};

static struct platform_device *espt_devices[] __initdata = {
	&espt_nor_flash_device,
	&espt_eth_device,
};

static int __init espt_devices_setup(void)
{
	return platform_add_devices(espt_devices,
				    ARRAY_SIZE(espt_devices));
}
device_initcall(espt_devices_setup);

static struct sh_machine_vector mv_espt __initmv = {
	.mv_name = "ESPT-GIGA",
};
