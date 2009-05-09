/*
 * arch/arm/mach-kirkwood/db88f6281-bp-setup.c
 *
 * Marvell DB-88F6281-BP Development Board Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include <plat/orion_nand.h>
#include <plat/mvsdio.h>
#include "common.h"
#include "mpp.h"

static struct mtd_partition db88f6281_nand_parts[] = {
	{
		.name = "u-boot",
		.offset = 0,
		.size = SZ_1M
	}, {
		.name = "uImage",
		.offset = MTDPART_OFS_NXTBLK,
		.size = SZ_4M
	}, {
		.name = "root",
		.offset = MTDPART_OFS_NXTBLK,
		.size = MTDPART_SIZ_FULL
	},
};

static struct resource db88f6281_nand_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= KIRKWOOD_NAND_MEM_PHYS_BASE,
	.end		= KIRKWOOD_NAND_MEM_PHYS_BASE +
			  KIRKWOOD_NAND_MEM_SIZE - 1,
};

static struct orion_nand_data db88f6281_nand_data = {
	.parts		= db88f6281_nand_parts,
	.nr_parts	= ARRAY_SIZE(db88f6281_nand_parts),
	.cle		= 0,
	.ale		= 1,
	.width		= 8,
	.chip_delay	= 25,
};

static struct platform_device db88f6281_nand_flash = {
	.name		= "orion_nand",
	.id		= -1,
	.dev		= {
		.platform_data	= &db88f6281_nand_data,
	},
	.resource	= &db88f6281_nand_resource,
	.num_resources	= 1,
};

static struct mv643xx_eth_platform_data db88f6281_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static struct mv_sata_platform_data db88f6281_sata_data = {
	.n_ports	= 2,
};

static struct mvsdio_platform_data db88f6281_mvsdio_data = {
	.gpio_write_protect	= 37,
	.gpio_card_detect	= 38,
};

static unsigned int db88f6281_mpp_config[] __initdata = {
	MPP37_GPIO,
	MPP38_GPIO,
	0
};

static void __init db88f6281_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();
	kirkwood_mpp_conf(db88f6281_mpp_config);

	kirkwood_ehci_init();
	kirkwood_ge00_init(&db88f6281_ge00_data);
	kirkwood_sata_init(&db88f6281_sata_data);
	kirkwood_uart0_init();
	kirkwood_sdio_init(&db88f6281_mvsdio_data);
	
	platform_device_register(&db88f6281_nand_flash);
}

static int __init db88f6281_pci_init(void)
{
	if (machine_is_db88f6281_bp())
		kirkwood_pcie_init();

	return 0;
}
subsys_initcall(db88f6281_pci_init);

MACHINE_START(DB88F6281_BP, "Marvell DB-88F6281-BP Development Board")
	/* Maintainer: Saeed Bishara <saeed@marvell.com> */
	.phys_io	= KIRKWOOD_REGS_PHYS_BASE,
	.io_pg_offst	= ((KIRKWOOD_REGS_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.init_machine	= db88f6281_init,
	.map_io		= kirkwood_map_io,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
MACHINE_END
