/*
 * arch/arm/mach-kirkwood/rd88f6192-nas-setup.c
 *
 * Marvell RD-88F6192-NAS Reference Board Setup
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
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>
#include <linux/spi/orion_spi.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <mach/kirkwood.h>
#include "common.h"

#define RD88F6192_GPIO_USB_VBUS		10

static struct mv643xx_eth_platform_data rd88f6192_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static struct mv_sata_platform_data rd88f6192_sata_data = {
	.n_ports	= 2,
};

static const struct flash_platform_data rd88F6192_spi_slave_data = {
	.type		= "m25p128",
};

static struct spi_board_info __initdata rd88F6192_spi_slave_info[] = {
	{
		.modalias	= "m25p80",
		.platform_data	= &rd88F6192_spi_slave_data,
		.irq		= -1,
		.max_speed_hz	= 20000000,
		.bus_num	= 0,
		.chip_select	= 0,
	},
};

static void __init rd88f6192_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();

	kirkwood_ehci_init();
	kirkwood_ge00_init(&rd88f6192_ge00_data);
	kirkwood_rtc_init();
	kirkwood_sata_init(&rd88f6192_sata_data);
	spi_register_board_info(rd88F6192_spi_slave_info,
				ARRAY_SIZE(rd88F6192_spi_slave_info));
	kirkwood_spi_init();
	kirkwood_uart0_init();
	kirkwood_xor0_init();
	kirkwood_xor1_init();
}

static int __init rd88f6192_pci_init(void)
{
	if (machine_is_rd88f6192_nas())
		kirkwood_pcie_init();

	return 0;
}
subsys_initcall(rd88f6192_pci_init);

MACHINE_START(RD88F6192_NAS, "Marvell RD-88F6192-NAS Development Board")
	/* Maintainer: Saeed Bishara <saeed@marvell.com> */
	.phys_io	= KIRKWOOD_REGS_PHYS_BASE,
	.io_pg_offst	= ((KIRKWOOD_REGS_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.init_machine	= rd88f6192_init,
	.map_io		= kirkwood_map_io,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
MACHINE_END
