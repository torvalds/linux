/*
 * arch/arm/mach-kirkwood/mv88f6281gtw_ge-setup.c
 *
 * Marvell 88F6281 GTW GE Board Setup
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
#include <linux/timer.h>
#include <linux/mv643xx_eth.h>
#include <linux/ethtool.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>
#include <linux/spi/orion_spi.h>
#include <net/dsa.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <mach/kirkwood.h>
#include "common.h"

static struct mv643xx_eth_platform_data mv88f6281gtw_ge_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_NONE,
	.speed		= SPEED_1000,
	.duplex		= DUPLEX_FULL,
};

static struct dsa_chip_data mv88f6281gtw_ge_switch_chip_data = {
	.port_names[0]	= "lan1",
	.port_names[1]	= "lan2",
	.port_names[2]	= "lan3",
	.port_names[3]	= "lan4",
	.port_names[4]	= "wan",
	.port_names[5]	= "cpu",
};

static struct dsa_platform_data mv88f6281gtw_ge_switch_plat_data = {
	.nr_chips	= 1,
	.chip		= &mv88f6281gtw_ge_switch_chip_data,
};

static const struct flash_platform_data mv88f6281gtw_ge_spi_slave_data = {
	.type		= "mx25l12805d",
};

static struct spi_board_info __initdata mv88f6281gtw_ge_spi_slave_info[] = {
	{
		.modalias	= "m25p80",
		.platform_data	= &mv88f6281gtw_ge_spi_slave_data,
		.irq		= -1,
		.max_speed_hz	= 50000000,
		.bus_num	= 0,
		.chip_select	= 0,
	},
};

static void __init mv88f6281gtw_ge_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();

	kirkwood_ehci_init();
	kirkwood_ge00_init(&mv88f6281gtw_ge_ge00_data);
	kirkwood_ge00_switch_init(&mv88f6281gtw_ge_switch_plat_data, NO_IRQ);
	spi_register_board_info(mv88f6281gtw_ge_spi_slave_info,
				ARRAY_SIZE(mv88f6281gtw_ge_spi_slave_info));
	kirkwood_spi_init();
	kirkwood_uart0_init();
}

static int __init mv88f6281gtw_ge_pci_init(void)
{
	if (machine_is_mv88f6281gtw_ge())
		kirkwood_pcie_init();

	return 0;
}
subsys_initcall(mv88f6281gtw_ge_pci_init);

MACHINE_START(MV88F6281GTW_GE, "Marvell 88F6281 GTW GE Board")
	/* Maintainer: Lennert Buytenhek <buytenh@marvell.com> */
	.phys_io	= KIRKWOOD_REGS_PHYS_BASE,
	.io_pg_offst	= ((KIRKWOOD_REGS_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.init_machine	= mv88f6281gtw_ge_init,
	.map_io		= kirkwood_map_io,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
MACHINE_END
