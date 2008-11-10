/*
 * arch/arm/mach-orion5x/rd88f5181l-ge-setup.c
 *
 * Marvell Orion-VoIP GE Reference Design Setup
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
#include <linux/mv643xx_eth.h>
#include <linux/ethtool.h>
#include <linux/i2c.h>
#include <net/dsa.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/leds.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <mach/orion5x.h>
#include "common.h"
#include "mpp.h"

/*****************************************************************************
 * RD-88F5181L GE Info
 ****************************************************************************/
/*
 * 16M NOR flash Device bus boot chip select
 */
#define RD88F5181L_GE_NOR_BOOT_BASE		0xff000000
#define RD88F5181L_GE_NOR_BOOT_SIZE		SZ_16M


/*****************************************************************************
 * 16M NOR Flash on Device bus Boot chip select
 ****************************************************************************/
static struct physmap_flash_data rd88f5181l_ge_nor_boot_flash_data = {
	.width		= 1,
};

static struct resource rd88f5181l_ge_nor_boot_flash_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= RD88F5181L_GE_NOR_BOOT_BASE,
	.end		= RD88F5181L_GE_NOR_BOOT_BASE +
			  RD88F5181L_GE_NOR_BOOT_SIZE - 1,
};

static struct platform_device rd88f5181l_ge_nor_boot_flash = {
	.name			= "physmap-flash",
	.id			= 0,
	.dev		= {
		.platform_data	= &rd88f5181l_ge_nor_boot_flash_data,
	},
	.num_resources		= 1,
	.resource		= &rd88f5181l_ge_nor_boot_flash_resource,
};


/*****************************************************************************
 * General Setup
 ****************************************************************************/
static struct orion5x_mpp_mode rd88f5181l_ge_mpp_modes[] __initdata = {
	{  0, MPP_GPIO },		/* LED1 */
	{  1, MPP_GPIO },		/* LED5 */
	{  2, MPP_GPIO },		/* LED4 */
	{  3, MPP_GPIO },		/* LED3 */
	{  4, MPP_GPIO },		/* PCI_intA */
	{  5, MPP_GPIO },		/* RTC interrupt */
	{  6, MPP_PCI_CLK },		/* CPU PCI refclk */
	{  7, MPP_PCI_CLK },		/* PCI/PCIe refclk */
	{  8, MPP_GPIO },		/* 88e6131 interrupt */
	{  9, MPP_GPIO },		/* GE_RXERR */
	{ 10, MPP_GPIO },		/* PCI_intB */
	{ 11, MPP_GPIO },		/* LED2 */
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

static struct mv643xx_eth_platform_data rd88f5181l_ge_eth_data = {
	.phy_addr	= MV643XX_ETH_PHY_NONE,
	.speed		= SPEED_1000,
	.duplex		= DUPLEX_FULL,
};

static struct dsa_platform_data rd88f5181l_ge_switch_data = {
	.port_names[0]	= "lan2",
	.port_names[1]	= "lan1",
	.port_names[2]	= "wan",
	.port_names[3]	= "cpu",
	.port_names[5]	= "lan4",
	.port_names[7]	= "lan3",
};

static struct i2c_board_info __initdata rd88f5181l_ge_i2c_rtc = {
	I2C_BOARD_INFO("ds1338", 0x68),
};

static void __init rd88f5181l_ge_init(void)
{
	/*
	 * Setup basic Orion functions. Need to be called early.
	 */
	orion5x_init();

	orion5x_mpp_conf(rd88f5181l_ge_mpp_modes);

	/*
	 * Configure peripherals.
	 */
	orion5x_ehci0_init();
	orion5x_eth_init(&rd88f5181l_ge_eth_data);
	orion5x_eth_switch_init(&rd88f5181l_ge_switch_data, gpio_to_irq(8));
	orion5x_i2c_init();
	orion5x_uart0_init();

	orion5x_setup_dev_boot_win(RD88F5181L_GE_NOR_BOOT_BASE,
				   RD88F5181L_GE_NOR_BOOT_SIZE);
	platform_device_register(&rd88f5181l_ge_nor_boot_flash);

	i2c_register_board_info(0, &rd88f5181l_ge_i2c_rtc, 1);
}

static int __init
rd88f5181l_ge_pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	/*
	 * Check for devices with hard-wired IRQs.
	 */
	irq = orion5x_pci_map_irq(dev, slot, pin);
	if (irq != -1)
		return irq;

	/*
	 * Cardbus slot.
	 */
	if (pin == 1)
		return gpio_to_irq(4);
	else
		return gpio_to_irq(10);
}

static struct hw_pci rd88f5181l_ge_pci __initdata = {
	.nr_controllers	= 2,
	.swizzle	= pci_std_swizzle,
	.setup		= orion5x_pci_sys_setup,
	.scan		= orion5x_pci_sys_scan_bus,
	.map_irq	= rd88f5181l_ge_pci_map_irq,
};

static int __init rd88f5181l_ge_pci_init(void)
{
	if (machine_is_rd88f5181l_ge()) {
		orion5x_pci_set_cardbus_mode();
		pci_common_init(&rd88f5181l_ge_pci);
	}

	return 0;
}
subsys_initcall(rd88f5181l_ge_pci_init);

MACHINE_START(RD88F5181L_GE, "Marvell Orion-VoIP GE Reference Design")
	/* Maintainer: Lennert Buytenhek <buytenh@marvell.com> */
	.phys_io	= ORION5X_REGS_PHYS_BASE,
	.io_pg_offst	= ((ORION5X_REGS_VIRT_BASE) >> 18) & 0xFFFC,
	.boot_params	= 0x00000100,
	.init_machine	= rd88f5181l_ge_init,
	.map_io		= orion5x_map_io,
	.init_irq	= orion5x_init_irq,
	.timer		= &orion5x_timer,
	.fixup		= tag_fixup_mem32,
MACHINE_END
