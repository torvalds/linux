/*
 * arch/arm/mach-dove/dove-db-setup.c
 *
 * Marvell DB-MV88AP510-BP Development Board Setup
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
#include <linux/i2c.h>
#include <linux/pci.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/gpio.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/dove.h>
#include "common.h"

static struct mv643xx_eth_platform_data dove_db_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR_DEFAULT,
};

static struct mv_sata_platform_data dove_db_sata_data = {
	.n_ports        = 1,
};

/*****************************************************************************
 * SPI Devices:
 * 	SPI0: 4M Flash ST-M25P32-VMF6P
 ****************************************************************************/
static const struct flash_platform_data dove_db_spi_flash_data = {
	.type		= "m25p64",
};

static struct spi_board_info __initdata dove_db_spi_flash_info[] = {
	{
		.modalias       = "m25p80",
		.platform_data  = &dove_db_spi_flash_data,
		.irq            = -1,
		.max_speed_hz   = 20000000,
		.bus_num        = 0,
		.chip_select    = 0,
	},
};

/*****************************************************************************
 * PCI
 ****************************************************************************/
static int __init dove_db_pci_init(void)
{
	if (machine_is_dove_db())
		dove_pcie_init(1, 1);

	return 0;
}

subsys_initcall(dove_db_pci_init);

/*****************************************************************************
 * Board Init
 ****************************************************************************/
static void __init dove_db_init(void)
{
	/*
	 * Basic Dove setup. Needs to be called early.
	 */
	dove_init();

	dove_ge00_init(&dove_db_ge00_data);
	dove_ehci0_init();
	dove_ehci1_init();
	dove_sata_init(&dove_db_sata_data);
	dove_sdio0_init();
	dove_sdio1_init();
	dove_spi0_init();
	dove_spi1_init();
	dove_uart0_init();
	dove_uart1_init();
	dove_i2c_init();
	spi_register_board_info(dove_db_spi_flash_info,
				ARRAY_SIZE(dove_db_spi_flash_info));
}

MACHINE_START(DOVE_DB, "Marvell DB-MV88AP510-BP Development Board")
	.atag_offset	= 0x100,
	.init_machine	= dove_db_init,
	.map_io		= dove_map_io,
	.init_early	= dove_init_early,
	.init_irq	= dove_init_irq,
	.init_time	= dove_timer_init,
	.restart	= dove_restart,
MACHINE_END
