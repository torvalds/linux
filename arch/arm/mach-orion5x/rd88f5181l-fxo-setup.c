// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/mach-orion5x/rd88f5181l-fxo-setup.c
 *
 * Marvell Orion-VoIP FXO Reference Design Setup
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/mtd/physmap.h>
#include <linux/mv643xx_eth.h>
#include <linux/ethtool.h>
#include <linux/platform_data/dsa.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include "common.h"
#include "mpp.h"
#include "orion5x.h"

/*****************************************************************************
 * RD-88F5181L FXO Info
 ****************************************************************************/
/*
 * 8M NOR flash Device bus boot chip select
 */
#define RD88F5181L_FXO_NOR_BOOT_BASE		0xff800000
#define RD88F5181L_FXO_NOR_BOOT_SIZE		SZ_8M


/*****************************************************************************
 * 8M NOR Flash on Device bus Boot chip select
 ****************************************************************************/
static struct physmap_flash_data rd88f5181l_fxo_nor_boot_flash_data = {
	.width		= 1,
};

static struct resource rd88f5181l_fxo_nor_boot_flash_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= RD88F5181L_FXO_NOR_BOOT_BASE,
	.end		= RD88F5181L_FXO_NOR_BOOT_BASE +
			  RD88F5181L_FXO_NOR_BOOT_SIZE - 1,
};

static struct platform_device rd88f5181l_fxo_nor_boot_flash = {
	.name			= "physmap-flash",
	.id			= 0,
	.dev		= {
		.platform_data	= &rd88f5181l_fxo_nor_boot_flash_data,
	},
	.num_resources		= 1,
	.resource		= &rd88f5181l_fxo_nor_boot_flash_resource,
};


/*****************************************************************************
 * General Setup
 ****************************************************************************/
static unsigned int rd88f5181l_fxo_mpp_modes[] __initdata = {
	MPP0_GPIO,		/* LED1 CardBus LED (front panel) */
	MPP1_GPIO,		/* PCI_intA */
	MPP2_GPIO,		/* Hard Reset / Factory Init*/
	MPP3_GPIO,		/* FXS or DAA select */
	MPP4_GPIO,		/* LED6 - phone LED (front panel) */
	MPP5_GPIO,		/* LED5 - phone LED (front panel) */
	MPP6_PCI_CLK,		/* CPU PCI refclk */
	MPP7_PCI_CLK,		/* PCI/PCIe refclk */
	MPP8_GPIO,		/* CardBus reset */
	MPP9_GPIO,		/* GE_RXERR */
	MPP10_GPIO,		/* LED2 MiniPCI LED (front panel) */
	MPP11_GPIO,		/* Lifeline control */
	MPP12_GIGE,		/* GE_TXD[4] */
	MPP13_GIGE,		/* GE_TXD[5] */
	MPP14_GIGE,		/* GE_TXD[6] */
	MPP15_GIGE,		/* GE_TXD[7] */
	MPP16_GIGE,		/* GE_RXD[4] */
	MPP17_GIGE,		/* GE_RXD[5] */
	MPP18_GIGE,		/* GE_RXD[6] */
	MPP19_GIGE,		/* GE_RXD[7] */
	0,
};

static struct mv643xx_eth_platform_data rd88f5181l_fxo_eth_data = {
	.phy_addr	= MV643XX_ETH_PHY_NONE,
	.speed		= SPEED_1000,
	.duplex		= DUPLEX_FULL,
};

static struct dsa_chip_data rd88f5181l_fxo_switch_chip_data = {
	.port_names[0]	= "lan2",
	.port_names[1]	= "lan1",
	.port_names[2]	= "wan",
	.port_names[3]	= "cpu",
	.port_names[5]	= "lan4",
	.port_names[7]	= "lan3",
};

static void __init rd88f5181l_fxo_init(void)
{
	/*
	 * Setup basic Orion functions. Need to be called early.
	 */
	orion5x_init();

	orion5x_mpp_conf(rd88f5181l_fxo_mpp_modes);

	/*
	 * Configure peripherals.
	 */
	orion5x_ehci0_init();
	orion5x_eth_init(&rd88f5181l_fxo_eth_data);
	orion5x_eth_switch_init(&rd88f5181l_fxo_switch_chip_data);
	orion5x_uart0_init();

	mvebu_mbus_add_window_by_id(ORION_MBUS_DEVBUS_BOOT_TARGET,
				    ORION_MBUS_DEVBUS_BOOT_ATTR,
				    RD88F5181L_FXO_NOR_BOOT_BASE,
				    RD88F5181L_FXO_NOR_BOOT_SIZE);
	platform_device_register(&rd88f5181l_fxo_nor_boot_flash);
}

static int __init
rd88f5181l_fxo_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	/*
	 * Check for devices with hard-wired IRQs.
	 */
	irq = orion5x_pci_map_irq(dev, slot, pin);
	if (irq != -1)
		return irq;

	/*
	 * Mini-PCI / Cardbus slot.
	 */
	return gpio_to_irq(1);
}

static struct hw_pci rd88f5181l_fxo_pci __initdata = {
	.nr_controllers	= 2,
	.setup		= orion5x_pci_sys_setup,
	.scan		= orion5x_pci_sys_scan_bus,
	.map_irq	= rd88f5181l_fxo_pci_map_irq,
};

static int __init rd88f5181l_fxo_pci_init(void)
{
	if (machine_is_rd88f5181l_fxo()) {
		orion5x_pci_set_cardbus_mode();
		pci_common_init(&rd88f5181l_fxo_pci);
	}

	return 0;
}
subsys_initcall(rd88f5181l_fxo_pci_init);

MACHINE_START(RD88F5181L_FXO, "Marvell Orion-VoIP FXO Reference Design")
	/* Maintainer: Nicolas Pitre <nico@marvell.com> */
	.atag_offset	= 0x100,
	.nr_irqs	= ORION5X_NR_IRQS,
	.init_machine	= rd88f5181l_fxo_init,
	.map_io		= orion5x_map_io,
	.init_early	= orion5x_init_early,
	.init_irq	= orion5x_init_irq,
	.init_time	= orion5x_timer_init,
	.fixup		= tag_fixup_mem32,
	.restart	= orion5x_restart,
MACHINE_END
