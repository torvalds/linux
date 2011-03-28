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
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/gpio.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>
#include <linux/spi/orion_spi.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
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

	orion_gpio_set_valid(RD88F6192_GPIO_USB_VBUS, 1);
	if (gpio_request(RD88F6192_GPIO_USB_VBUS, "USB VBUS") != 0 ||
	    gpio_direction_output(RD88F6192_GPIO_USB_VBUS, 1) != 0)
		pr_err("RD-88F6192-NAS: failed to setup USB VBUS GPIO\n");

	kirkwood_ehci_init();
	kirkwood_ge00_init(&rd88f6192_ge00_data);
	kirkwood_sata_init(&rd88f6192_sata_data);
	spi_register_board_info(rd88F6192_spi_slave_info,
				ARRAY_SIZE(rd88F6192_spi_slave_info));
	kirkwood_spi_init();
	kirkwood_uart0_init();
}

static int __init rd88f6192_pci_init(void)
{
	if (machine_is_rd88f6192_nas())
		kirkwood_pcie_init(KW_PCIE0);

	return 0;
}
subsys_initcall(rd88f6192_pci_init);

MACHINE_START(RD88F6192_NAS, "Marvell RD-88F6192-NAS Development Board")
	/* Maintainer: Saeed Bishara <saeed@marvell.com> */
	.boot_params	= 0x00000100,
	.init_machine	= rd88f6192_init,
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
MACHINE_END
