/*
 * arch/arm/mach-kirkwood/board-iconnect.c
 *
 * Iomega i-connect Board Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/mtd/partitions.h>
#include <linux/mv643xx_eth.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include "common.h"
#include "mpp.h"

static struct mv643xx_eth_platform_data iconnect_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(11),
};

static unsigned int iconnect_mpp_config[] __initdata = {
	MPP12_GPIO,
	MPP35_GPIO,
	MPP41_GPIO,
	MPP42_GPIO,
	MPP43_GPIO,
	MPP44_GPIO,
	MPP45_GPIO,
	MPP46_GPIO,
	MPP47_GPIO,
	MPP48_GPIO,
	0
};

static struct mtd_partition iconnect_nand_parts[] = {
	{
		.name = "flash",
		.offset = 0,
		.size = MTDPART_SIZ_FULL,
	},
};

/* yikes... theses are the original input buttons */
/* but I'm not convinced by the sw event choices  */
static struct gpio_keys_button iconnect_buttons[] = {
	{
		.type		= EV_SW,
		.code		= SW_LID,
		.gpio		= 12,
		.desc		= "Reset Button",
		.active_low	= 1,
		.debounce_interval = 100,
	}, {
		.type		= EV_SW,
		.code		= SW_TABLET_MODE,
		.gpio		= 35,
		.desc		= "OTB Button",
		.active_low	= 1,
		.debounce_interval = 100,
	},
};

static struct gpio_keys_platform_data iconnect_button_data = {
	.buttons	= iconnect_buttons,
	.nbuttons	= ARRAY_SIZE(iconnect_buttons),
};

static struct platform_device iconnect_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev        = {
		.platform_data  = &iconnect_button_data,
	},
};

void __init iconnect_init(void)
{
	kirkwood_mpp_conf(iconnect_mpp_config);
	kirkwood_nand_init(ARRAY_AND_SIZE(iconnect_nand_parts), 25);

	kirkwood_ehci_init();
	kirkwood_ge00_init(&iconnect_ge00_data);

	platform_device_register(&iconnect_button_device);
}

static int __init iconnect_pci_init(void)
{
	if (of_machine_is_compatible("iom,iconnect"))
		kirkwood_pcie_init(KW_PCIE0);
	return 0;
}
subsys_initcall(iconnect_pci_init);
