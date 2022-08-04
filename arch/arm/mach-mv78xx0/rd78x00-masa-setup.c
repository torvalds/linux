// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/mach-mv78x00/rd78x00-masa-setup.c
 *
 * Marvell RD-78x00-mASA Development Board Setup
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/ethtool.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include "mv78xx0.h"
#include "common.h"

static struct mv643xx_eth_platform_data rd78x00_masa_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static struct mv643xx_eth_platform_data rd78x00_masa_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(9),
};

static struct mv643xx_eth_platform_data rd78x00_masa_ge10_data = {
};

static struct mv643xx_eth_platform_data rd78x00_masa_ge11_data = {
};

static struct mv_sata_platform_data rd78x00_masa_sata_data = {
	.n_ports	= 2,
};

static void __init rd78x00_masa_init(void)
{
	/*
	 * Basic MV78x00 setup. Needs to be called early.
	 */
	mv78xx0_init();

	/*
	 * Partition on-chip peripherals between the two CPU cores.
	 */
	if (mv78xx0_core_index() == 0) {
		mv78xx0_ehci0_init();
		mv78xx0_ehci1_init();
		mv78xx0_ge00_init(&rd78x00_masa_ge00_data);
		mv78xx0_ge10_init(&rd78x00_masa_ge10_data);
		mv78xx0_sata_init(&rd78x00_masa_sata_data);
		mv78xx0_uart0_init();
		mv78xx0_uart2_init();
	} else {
		mv78xx0_ehci2_init();
		mv78xx0_ge01_init(&rd78x00_masa_ge01_data);
		mv78xx0_ge11_init(&rd78x00_masa_ge11_data);
		mv78xx0_uart1_init();
		mv78xx0_uart3_init();
	}
}

static int __init rd78x00_pci_init(void)
{
	/*
	 * Assign all PCIe devices to CPU core #0.
	 */
	if (machine_is_rd78x00_masa() && mv78xx0_core_index() == 0)
		mv78xx0_pcie_init(1, 1);

	return 0;
}
subsys_initcall(rd78x00_pci_init);

MACHINE_START(RD78X00_MASA, "Marvell RD-78x00-MASA Development Board")
	/* Maintainer: Lennert Buytenhek <buytenh@marvell.com> */
	.atag_offset	= 0x100,
	.nr_irqs	= MV78XX0_NR_IRQS,
	.init_machine	= rd78x00_masa_init,
	.map_io		= mv78xx0_map_io,
	.init_early	= mv78xx0_init_early,
	.init_irq	= mv78xx0_init_irq,
	.init_time	= mv78xx0_timer_init,
	.restart	= mv78xx0_restart,
MACHINE_END
