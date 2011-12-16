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
#include <linux/irq.h>
#include <linux/mtd/partitions.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/ethtool.h>
#include <net/dsa.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include <plat/mvsdio.h>
#include "common.h"
#include "mpp.h"

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

static struct mv643xx_eth_platform_data rd88f6281_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_NONE,
	.speed		= SPEED_1000,
	.duplex		= DUPLEX_FULL,
};

static struct dsa_chip_data rd88f6281_switch_chip_data = {
	.port_names[0]	= "lan1",
	.port_names[1]	= "lan2",
	.port_names[2]	= "lan3",
	.port_names[3]	= "lan4",
	.port_names[5]	= "cpu",
};

static struct dsa_platform_data rd88f6281_switch_plat_data = {
	.nr_chips	= 1,
	.chip		= &rd88f6281_switch_chip_data,
};

static struct mv643xx_eth_platform_data rd88f6281_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(11),
};

static struct mv_sata_platform_data rd88f6281_sata_data = {
	.n_ports	= 2,
};

static struct mvsdio_platform_data rd88f6281_mvsdio_data = {
	.gpio_card_detect = 28,
};

static unsigned int rd88f6281_mpp_config[] __initdata = {
	MPP28_GPIO,
	0
};

static void __init rd88f6281_init(void)
{
	u32 dev, rev;

	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();
	kirkwood_mpp_conf(rd88f6281_mpp_config);

	kirkwood_nand_init(ARRAY_AND_SIZE(rd88f6281_nand_parts), 25);
	kirkwood_ehci_init();

	kirkwood_ge00_init(&rd88f6281_ge00_data);
	kirkwood_pcie_id(&dev, &rev);
	if (rev == MV88F6281_REV_A0) {
		rd88f6281_switch_chip_data.sw_addr = 10;
		kirkwood_ge01_init(&rd88f6281_ge01_data);
	} else {
		rd88f6281_switch_chip_data.port_names[4] = "wan";
	}
	kirkwood_ge00_switch_init(&rd88f6281_switch_plat_data, NO_IRQ);

	kirkwood_sata_init(&rd88f6281_sata_data);
	kirkwood_sdio_init(&rd88f6281_mvsdio_data);
	kirkwood_uart0_init();
}

static int __init rd88f6281_pci_init(void)
{
	if (machine_is_rd88f6281())
		kirkwood_pcie_init(KW_PCIE0);

	return 0;
}
subsys_initcall(rd88f6281_pci_init);

MACHINE_START(RD88F6281, "Marvell RD-88F6281 Reference Board")
	/* Maintainer: Saeed Bishara <saeed@marvell.com> */
	.atag_offset	= 0x100,
	.init_machine	= rd88f6281_init,
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
MACHINE_END
