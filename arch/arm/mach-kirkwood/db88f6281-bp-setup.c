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
#include <linux/mtd/partitions.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
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
	MPP0_NF_IO2,
	MPP1_NF_IO3,
	MPP2_NF_IO4,
	MPP3_NF_IO5,
	MPP4_NF_IO6,
	MPP5_NF_IO7,
	MPP18_NF_IO0,
	MPP19_NF_IO1,
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

	kirkwood_nand_init(ARRAY_AND_SIZE(db88f6281_nand_parts), 25);
	kirkwood_ehci_init();
	kirkwood_ge00_init(&db88f6281_ge00_data);
	kirkwood_sata_init(&db88f6281_sata_data);
	kirkwood_uart0_init();
	kirkwood_sdio_init(&db88f6281_mvsdio_data);
}

static int __init db88f6281_pci_init(void)
{
	if (machine_is_db88f6281_bp()) {
		u32 dev, rev;

		kirkwood_pcie_id(&dev, &rev);
		if (dev == MV88F6282_DEV_ID)
			kirkwood_pcie_init(KW_PCIE1 | KW_PCIE0);
		else
			kirkwood_pcie_init(KW_PCIE0);
	}
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
