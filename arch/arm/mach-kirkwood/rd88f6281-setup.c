/*
 * arch/arm/mach-kirkwood/rd88f6281-setup.c
 *
 * Marvell RD-88F6281 Reference Board Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/nand.h>
#include <linux/timer.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/ethtool.h>
#include <net/dsa.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <mach/kirkwood.h>
#include <plat/orion_nand.h>
#include "common.h"

static struct mtd_partition rd88f6281_nand_parts[] = {
	{
		.name = "u-boot",
		.offset = 0,
		.size = SZ_1M
	}, {
		.name = "uImage",
		.offset = MTDPART_OFS_NXTBLK,
		.size = SZ_2M
	}, {
		.name = "root",
		.offset = MTDPART_OFS_NXTBLK,
		.size = MTDPART_SIZ_FULL
	},
};

static struct resource rd88f6281_nand_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= KIRKWOOD_NAND_MEM_PHYS_BASE,
	.end		= KIRKWOOD_NAND_MEM_PHYS_BASE +
			  KIRKWOOD_NAND_MEM_SIZE - 1,
};

static struct orion_nand_data rd88f6281_nand_data = {
	.parts		= rd88f6281_nand_parts,
	.nr_parts	= ARRAY_SIZE(rd88f6281_nand_parts),
	.cle		= 0,
	.ale		= 1,
	.width		= 8,
	.chip_delay	= 25,
};

static struct platform_device rd88f6281_nand_flash = {
	.name		= "orion_nand",
	.id		= -1,
	.dev		= {
		.platform_data	= &rd88f6281_nand_data,
	},
	.resource	= &rd88f6281_nand_resource,
	.num_resources	= 1,
};

static struct mv643xx_eth_platform_data rd88f6281_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_NONE,
	.speed		= SPEED_1000,
	.duplex		= DUPLEX_FULL,
};

static struct dsa_platform_data rd88f6281_switch_data = {
	.port_names[0]	= "lan1",
	.port_names[1]	= "lan2",
	.port_names[2]	= "lan3",
	.port_names[3]	= "lan4",
	.port_names[5]	= "cpu",
};

static struct mv643xx_eth_platform_data rd88f6281_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(11),
};

static struct mv_sata_platform_data rd88f6281_sata_data = {
	.n_ports	= 2,
};

static void __init rd88f6281_init(void)
{
	u32 dev, rev;

	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();

	kirkwood_ehci_init();

	kirkwood_ge00_init(&rd88f6281_ge00_data);
	kirkwood_pcie_id(&dev, &rev);
	if (rev == MV88F6281_REV_A0) {
		rd88f6281_switch_data.sw_addr = 10;
		kirkwood_ge01_init(&rd88f6281_ge01_data);
	} else {
		rd88f6281_switch_data.port_names[4] = "wan";
	}
	kirkwood_ge00_switch_init(&rd88f6281_switch_data, NO_IRQ);

	kirkwood_rtc_init();
	kirkwood_sata_init(&rd88f6281_sata_data);
	kirkwood_uart0_init();

	platform_device_register(&rd88f6281_nand_flash);
}

static int __init rd88f6281_pci_init(void)
{
	if (machine_is_rd88f6281())
		kirkwood_pcie_init();

	return 0;
}
subsys_initcall(rd88f6281_pci_init);

MACHINE_START(RD88F6281, "Marvell RD-88F6281 Reference Board")
	/* Maintainer: Saeed Bishara <saeed@marvell.com> */
	.phys_io	= KIRKWOOD_REGS_PHYS_BASE,
	.io_pg_offst	= ((KIRKWOOD_REGS_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.init_machine	= rd88f6281_init,
	.map_io		= kirkwood_map_io,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
MACHINE_END
