/*
 * arch/arm/mach-orion5x/wrt350n-v2-setup.c
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
#include <linux/leds.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <net/dsa.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <mach/orion5x.h>
#include "common.h"
#include "mpp.h"

/*
 * LEDs attached to GPIO
 */
static struct gpio_led wrt350n_v2_led_pins[] = {
	{
		.name		= "wrt350nv2:green:power",
		.gpio		= 0,
		.active_low	= 1,
	}, {
		.name		= "wrt350nv2:green:security",
		.gpio		= 1,
		.active_low	= 1,
	}, {
		.name		= "wrt350nv2:orange:power",
		.gpio		= 5,
		.active_low	= 1,
	}, {
		.name		= "wrt350nv2:green:usb",
		.gpio		= 6,
		.active_low	= 1,
	}, {
		.name		= "wrt350nv2:green:wireless",
		.gpio		= 7,
		.active_low	= 1,
	},
};

static struct gpio_led_platform_data wrt350n_v2_led_data = {
	.leds		= wrt350n_v2_led_pins,
	.num_leds	= ARRAY_SIZE(wrt350n_v2_led_pins),
};

static struct platform_device wrt350n_v2_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &wrt350n_v2_led_data,
	},
};

/*
 * Buttons attached to GPIO
 */
static struct gpio_keys_button wrt350n_v2_buttons[] = {
	{
		.code		= KEY_RESTART,
		.gpio		= 3,
		.desc		= "Reset Button",
		.active_low	= 1,
	}, {
		.code		= KEY_WLAN,
		.gpio		= 2,
		.desc		= "WPS Button",
		.active_low	= 1,
	},
};

static struct gpio_keys_platform_data wrt350n_v2_button_data = {
	.buttons	= wrt350n_v2_buttons,
	.nbuttons	= ARRAY_SIZE(wrt350n_v2_buttons),
};

static struct platform_device wrt350n_v2_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &wrt350n_v2_button_data,
	},
};

/*
 * General setup
 */
static struct orion5x_mpp_mode wrt350n_v2_mpp_modes[] __initdata = {
	{  0, MPP_GPIO },		/* Power LED green (0=on) */
	{  1, MPP_GPIO },		/* Security LED (0=on) */
	{  2, MPP_GPIO },		/* Internal Button (0=on) */
	{  3, MPP_GPIO },		/* Reset Button (0=on) */
	{  4, MPP_GPIO },		/* PCI int */
	{  5, MPP_GPIO },		/* Power LED orange (0=on) */
	{  6, MPP_GPIO },		/* USB LED (0=on) */
	{  7, MPP_GPIO },		/* Wireless LED (0=on) */
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
#define WRT350N_V2_NOR_BOOT_BASE	0xf4000000
#define WRT350N_V2_NOR_BOOT_SIZE	SZ_8M

static struct mtd_partition wrt350n_v2_nor_flash_partitions[] = {
	{
		.name		= "kernel",
		.offset		= 0x00000000,
		.size		= 0x00760000,
	}, {
		.name		= "rootfs",
		.offset		= 0x001a0000,
		.size		= 0x005c0000,
	}, {
		.name		= "lang",
		.offset		= 0x00760000,
		.size		= 0x00040000,
	}, {
		.name		= "nvram",
		.offset		= 0x007a0000,
		.size		= 0x00020000,
	}, {
		.name		= "u-boot",
		.offset		= 0x007c0000,
		.size		= 0x00040000,
	},
};

static struct physmap_flash_data wrt350n_v2_nor_flash_data = {
	.width		= 1,
	.parts		= wrt350n_v2_nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(wrt350n_v2_nor_flash_partitions),
};

static struct resource wrt350n_v2_nor_flash_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= WRT350N_V2_NOR_BOOT_BASE,
	.end		= WRT350N_V2_NOR_BOOT_BASE + WRT350N_V2_NOR_BOOT_SIZE - 1,
};

static struct platform_device wrt350n_v2_nor_flash = {
	.name			= "physmap-flash",
	.id			= 0,
	.dev		= {
		.platform_data	= &wrt350n_v2_nor_flash_data,
	},
	.num_resources		= 1,
	.resource		= &wrt350n_v2_nor_flash_resource,
};

static struct mv643xx_eth_platform_data wrt350n_v2_eth_data = {
	.phy_addr	= MV643XX_ETH_PHY_NONE,
	.speed		= SPEED_1000,
	.duplex		= DUPLEX_FULL,
};

static struct dsa_chip_data wrt350n_v2_switch_chip_data = {
	.port_names[0]	= "lan2",
	.port_names[1]	= "lan1",
	.port_names[2]	= "wan",
	.port_names[3]	= "cpu",
	.port_names[5]	= "lan3",
	.port_names[7]	= "lan4",
};

static struct dsa_platform_data wrt350n_v2_switch_plat_data = {
	.nr_chips	= 1,
	.chip		= &wrt350n_v2_switch_chip_data,
};

static void __init wrt350n_v2_init(void)
{
	/*
	 * Setup basic Orion functions. Need to be called early.
	 */
	orion5x_init();

	orion5x_mpp_conf(wrt350n_v2_mpp_modes);

	/*
	 * Configure peripherals.
	 */
	orion5x_ehci0_init();
	orion5x_eth_init(&wrt350n_v2_eth_data);
	orion5x_eth_switch_init(&wrt350n_v2_switch_plat_data, NO_IRQ);
	orion5x_uart0_init();

	orion5x_setup_dev_boot_win(WRT350N_V2_NOR_BOOT_BASE,
				   WRT350N_V2_NOR_BOOT_SIZE);
	platform_device_register(&wrt350n_v2_nor_flash);
	platform_device_register(&wrt350n_v2_leds);
	platform_device_register(&wrt350n_v2_button_device);
}

static int __init wrt350n_v2_pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
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

static struct hw_pci wrt350n_v2_pci __initdata = {
	.nr_controllers	= 2,
	.swizzle	= pci_std_swizzle,
	.setup		= orion5x_pci_sys_setup,
	.scan		= orion5x_pci_sys_scan_bus,
	.map_irq	= wrt350n_v2_pci_map_irq,
};

static int __init wrt350n_v2_pci_init(void)
{
	if (machine_is_wrt350n_v2())
		pci_common_init(&wrt350n_v2_pci);

	return 0;
}
subsys_initcall(wrt350n_v2_pci_init);

MACHINE_START(WRT350N_V2, "Linksys WRT350N v2")
	/* Maintainer: Lennert Buytenhek <buytenh@marvell.com> */
	.phys_io	= ORION5X_REGS_PHYS_BASE,
	.io_pg_offst	= ((ORION5X_REGS_VIRT_BASE) >> 18) & 0xFFFC,
	.boot_params	= 0x00000100,
	.init_machine	= wrt350n_v2_init,
	.map_io		= orion5x_map_io,
	.init_irq	= orion5x_init_irq,
	.timer		= &orion5x_timer,
	.fixup		= tag_fixup_mem32,
MACHINE_END
