/*
 * arch/arm/mach-mv78xx0/db78x00-bp-setup.c
 *
 * Marvell DB-78x00-BP Development Board Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/ethtool.h>
#include <linux/i2c.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include "mv78xx0.h"
#include "common.h"

static struct mv643xx_eth_platform_data db78x00_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static struct mv643xx_eth_platform_data db78x00_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(9),
};

static struct mv643xx_eth_platform_data db78x00_ge10_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(10),
};

static struct mv643xx_eth_platform_data db78x00_ge11_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(11),
};

static struct mv_sata_platform_data db78x00_sata_data = {
	.n_ports	= 2,
};

static struct i2c_board_info __initdata db78x00_i2c_rtc = {
	I2C_BOARD_INFO("ds1338", 0x68),
};


static void __init db78x00_init(void)
{
	/*
	 * Basic MV78xx0 setup. Needs to be called early.
	 */
	mv78xx0_init();

	/*
	 * Partition on-chip peripherals between the two CPU cores.
	 */
	if (mv78xx0_core_index() == 0) {
		mv78xx0_ehci0_init();
		mv78xx0_ehci1_init();
		mv78xx0_ehci2_init();
		mv78xx0_ge00_init(&db78x00_ge00_data);
		mv78xx0_ge01_init(&db78x00_ge01_data);
		mv78xx0_ge10_init(&db78x00_ge10_data);
		mv78xx0_ge11_init(&db78x00_ge11_data);
		mv78xx0_sata_init(&db78x00_sata_data);
		mv78xx0_uart0_init();
		mv78xx0_uart2_init();
		mv78xx0_i2c_init();
		i2c_register_board_info(0, &db78x00_i2c_rtc, 1);
	} else {
		mv78xx0_uart1_init();
		mv78xx0_uart3_init();
	}
}

static int __init db78x00_pci_init(void)
{
	if (machine_is_db78x00_bp()) {
		/*
		 * Assign the x16 PCIe slot on the board to CPU core
		 * #0, and let CPU core #1 have the four x1 slots.
		 */
		if (mv78xx0_core_index() == 0)
			mv78xx0_pcie_init(0, 1);
		else
			mv78xx0_pcie_init(1, 0);
	}

	return 0;
}
subsys_initcall(db78x00_pci_init);

MACHINE_START(DB78X00_BP, "Marvell DB-78x00-BP Development Board")
	/* Maintainer: Lennert Buytenhek <buytenh@marvell.com> */
	.atag_offset	= 0x100,
	.nr_irqs	= MV78XX0_NR_IRQS,
	.init_machine	= db78x00_init,
	.map_io		= mv78xx0_map_io,
	.init_early	= mv78xx0_init_early,
	.init_irq	= mv78xx0_init_irq,
	.init_time	= mv78xx0_timer_init,
	.restart	= mv78xx0_restart,
MACHINE_END
