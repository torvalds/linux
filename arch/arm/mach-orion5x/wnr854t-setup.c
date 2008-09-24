/*
 * arch/arm/mach-orion5x/wnr854t-setup.c
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
#include <linux/delay.h>
#include <linux/mtd/physmap.h>
#include <linux/mv643xx_eth.h>
#include <linux/ethtool.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <mach/orion5x.h>
#include "common.h"
#include "mpp.h"

static struct orion5x_mpp_mode wnr854t_mpp_modes[] __initdata = {
	{  0, MPP_GPIO },		/* Power LED green (0=on) */
	{  1, MPP_GPIO },		/* Reset Button (0=off) */
	{  2, MPP_GPIO },		/* Power LED blink (0=off) */
	{  3, MPP_GPIO },		/* WAN Status LED amber (0=off) */
	{  4, MPP_GPIO },		/* PCI int */
	{  5, MPP_GPIO },		/* ??? */
	{  6, MPP_GPIO },		/* ??? */
	{  7, MPP_GPIO },		/* ??? */
	{  8, MPP_UNUSED },		/* ??? */
	{  9, MPP_GIGE },		/* GE_RXERR */
	{ 10, MPP_UNUSED },		/* ??? */
	{ 11, MPP_UNUSED },		/* ??? */
	{ 12, MPP_GIGE },		/* GE_TXD[4] */
	{ 13, MPP_GIGE },		/* GE_TXD[5] */
	{ 14, MPP_GIGE },		/* GE_TXD[6] */
	{ 15, MPP_GIGE },		/* GE_TXD[7] */
	{ 16, MPP_GIGE },		/* GE_RXD[4] */
	{ 17, MPP_GIGE },		/* GE_RXD[5] */
	{ 18, MPP_GIGE },		/* GE_RXD[6] */
	{ 19, MPP_GIGE },		/* GE_RXD[7] */
	{ -1 },
};

/*
 * 8M NOR flash Device bus boot chip select
 */
#define WNR854T_NOR_BOOT_BASE	0xf4000000
#define WNR854T_NOR_BOOT_SIZE	SZ_8M

static struct mtd_partition wnr854t_nor_flash_partitions[] = {
	{
		.name		= "kernel",
		.offset		= 0x00000000,
		.size		= 0x00100000,
	}, {
		.name		= "rootfs",
		.offset		= 0x00100000,
		.size		= 0x00660000,
	}, {
		.name		= "uboot",
		.offset		= 0x00760000,
		.size		= 0x00040000,
	},
};

static struct physmap_flash_data wnr854t_nor_flash_data = {
	.width		= 2,
	.parts		= wnr854t_nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(wnr854t_nor_flash_partitions),
};

static struct resource wnr854t_nor_flash_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= WNR854T_NOR_BOOT_BASE,
	.end		= WNR854T_NOR_BOOT_BASE + WNR854T_NOR_BOOT_SIZE - 1,
};

static struct platform_device wnr854t_nor_flash = {
	.name			= "physmap-flash",
	.id			= 0,
	.dev		= {
		.platform_data	= &wnr854t_nor_flash_data,
	},
	.num_resources		= 1,
	.resource		= &wnr854t_nor_flash_resource,
};

static struct mv643xx_eth_platform_data wnr854t_eth_data = {
	.phy_addr	= -1,
	.speed		= SPEED_1000,
	.duplex		= DUPLEX_FULL,
};

static void __init wnr854t_init(void)
{
	/*
	 * Setup basic Orion functions. Need to be called early.
	 */
	orion5x_init();

	orion5x_mpp_conf(wnr854t_mpp_modes);

	/*
	 * Configure peripherals.
	 */
	orion5x_eth_init(&wnr854t_eth_data);
	orion5x_uart0_init();

	orion5x_setup_dev_boot_win(WNR854T_NOR_BOOT_BASE,
				   WNR854T_NOR_BOOT_SIZE);
	platform_device_register(&wnr854t_nor_flash);
}

static int __init wnr854t_pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	/*
	 * Check for devices with hard-wired IRQs.
	 */
	irq = orion5x_pci_map_irq(dev, slot, pin);
	if (irq != -1)
		return irq;

	/*
	 * Mini-PCI slot.
	 */
	if (slot == 7)
		return gpio_to_irq(4);

	return -1;
}

static struct hw_pci wnr854t_pci __initdata = {
	.nr_controllers	= 2,
	.swizzle	= pci_std_swizzle,
	.setup		= orion5x_pci_sys_setup,
	.scan		= orion5x_pci_sys_scan_bus,
	.map_irq	= wnr854t_pci_map_irq,
};

static int __init wnr854t_pci_init(void)
{
	if (machine_is_wnr854t())
		pci_common_init(&wnr854t_pci);

	return 0;
}
subsys_initcall(wnr854t_pci_init);

MACHINE_START(WNR854T, "Netgear WNR854T")
	/* Maintainer: Imre Kaloz <kaloz@openwrt.org> */
	.phys_io	= ORION5X_REGS_PHYS_BASE,
	.io_pg_offst	= ((ORION5X_REGS_VIRT_BASE) >> 18) & 0xFFFC,
	.boot_params	= 0x00000100,
	.init_machine	= wnr854t_init,
	.map_io		= orion5x_map_io,
	.init_irq	= orion5x_init_irq,
	.timer		= &orion5x_timer,
	.fixup		= tag_fixup_mem32,
MACHINE_END
