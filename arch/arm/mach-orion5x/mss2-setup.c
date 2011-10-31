/*
 * Maxtor Shared Storage II Board Setup
 *
 * Maintainer: Sylver Bruneau <sylver.bruneau@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/mtd/physmap.h>
#include <linux/mv643xx_eth.h>
#include <linux/leds.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/ata_platform.h>
#include <linux/gpio.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <mach/orion5x.h>
#include <mach/bridge-regs.h>
#include "common.h"
#include "mpp.h"

#define MSS2_NOR_BOOT_BASE	0xff800000
#define MSS2_NOR_BOOT_SIZE	SZ_256K

/*****************************************************************************
 * Maxtor Shared Storage II Info
 ****************************************************************************/

/*
 * Maxtor Shared Storage II hardware :
 * - Marvell 88F5182-A2 C500
 * - Marvell 88E1111 Gigabit Ethernet PHY
 * - RTC M41T81 (@0x68) on I2C bus
 * - 256KB NOR flash
 * - 64MB of RAM
 */

/*****************************************************************************
 * 256KB NOR Flash on BOOT Device
 ****************************************************************************/

static struct physmap_flash_data mss2_nor_flash_data = {
	.width		= 1,
};

static struct resource mss2_nor_flash_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= MSS2_NOR_BOOT_BASE,
	.end		= MSS2_NOR_BOOT_BASE + MSS2_NOR_BOOT_SIZE - 1,
};

static struct platform_device mss2_nor_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &mss2_nor_flash_data,
	},
	.resource	= &mss2_nor_flash_resource,
	.num_resources	= 1,
};

/****************************************************************************
 * PCI setup
 ****************************************************************************/
static int __init mss2_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	/*
	 * Check for devices with hard-wired IRQs.
	 */
	irq = orion5x_pci_map_irq(dev, slot, pin);
	if (irq != -1)
		return irq;

	return -1;
}

static struct hw_pci mss2_pci __initdata = {
	.nr_controllers = 2,
	.swizzle	= pci_std_swizzle,
	.setup		= orion5x_pci_sys_setup,
	.scan		= orion5x_pci_sys_scan_bus,
	.map_irq	= mss2_pci_map_irq,
};

static int __init mss2_pci_init(void)
{
	if (machine_is_mss2())
		pci_common_init(&mss2_pci);

	return 0;
}
subsys_initcall(mss2_pci_init);


/*****************************************************************************
 * Ethernet
 ****************************************************************************/

static struct mv643xx_eth_platform_data mss2_eth_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

/*****************************************************************************
 * SATA
 ****************************************************************************/

static struct mv_sata_platform_data mss2_sata_data = {
	.n_ports	= 2,
};

/*****************************************************************************
 * GPIO buttons
 ****************************************************************************/

#define MSS2_GPIO_KEY_RESET	12
#define MSS2_GPIO_KEY_POWER	11

static struct gpio_keys_button mss2_buttons[] = {
	{
		.code		= KEY_POWER,
		.gpio		= MSS2_GPIO_KEY_POWER,
		.desc		= "Power",
		.active_low	= 1,
	}, {
		.code		= KEY_RESTART,
		.gpio		= MSS2_GPIO_KEY_RESET,
		.desc		= "Reset",
		.active_low	= 1,
	},
};

static struct gpio_keys_platform_data mss2_button_data = {
	.buttons	= mss2_buttons,
	.nbuttons	= ARRAY_SIZE(mss2_buttons),
};

static struct platform_device mss2_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev		= {
		.platform_data	= &mss2_button_data,
	},
};

/*****************************************************************************
 * RTC m41t81 on I2C bus
 ****************************************************************************/

#define MSS2_GPIO_RTC_IRQ	3

static struct i2c_board_info __initdata mss2_i2c_rtc = {
	I2C_BOARD_INFO("m41t81", 0x68),
};

/*****************************************************************************
 * MSS2 power off method
 ****************************************************************************/
/*
 * On the Maxtor Shared Storage II, the shutdown process is the following :
 * - Userland modifies U-boot env to tell U-boot to go idle at next boot
 * - The board reboots
 * - U-boot starts and go into an idle mode until the user press "power"
 */
static void mss2_power_off(void)
{
	u32 reg;

	/*
	 * Enable and issue soft reset
	 */
	reg = readl(RSTOUTn_MASK);
	reg |= 1 << 2;
	writel(reg, RSTOUTn_MASK);

	reg = readl(CPU_SOFT_RESET);
	reg |= 1;
	writel(reg, CPU_SOFT_RESET);
}

/****************************************************************************
 * General Setup
 ****************************************************************************/
static unsigned int mss2_mpp_modes[] __initdata = {
	MPP0_GPIO,		/* Power LED */
	MPP1_GPIO,		/* Error LED */
	MPP2_UNUSED,
	MPP3_GPIO,		/* RTC interrupt */
	MPP4_GPIO,		/* HDD ind. (Single/Dual)*/
	MPP5_GPIO,		/* HD0 5V control */
	MPP6_GPIO,		/* HD0 12V control */
	MPP7_GPIO,		/* HD1 5V control */
	MPP8_GPIO,		/* HD1 12V control */
	MPP9_UNUSED,
	MPP10_GPIO,		/* Fan control */
	MPP11_GPIO,		/* Power button */
	MPP12_GPIO,		/* Reset button */
	MPP13_UNUSED,
	MPP14_SATA_LED,		/* SATA 0 active */
	MPP15_SATA_LED,		/* SATA 1 active */
	MPP16_UNUSED,
	MPP17_UNUSED,
	MPP18_UNUSED,
	MPP19_UNUSED,
	0,
};

static void __init mss2_init(void)
{
	/* Setup basic Orion functions. Need to be called early. */
	orion5x_init();

	orion5x_mpp_conf(mss2_mpp_modes);

	/*
	 * MPP[20] Unused
	 * MPP[21] PCI clock
	 * MPP[22] USB 0 over current
	 * MPP[23] USB 1 over current
	 */

	/*
	 * Configure peripherals.
	 */
	orion5x_ehci0_init();
	orion5x_ehci1_init();
	orion5x_eth_init(&mss2_eth_data);
	orion5x_i2c_init();
	orion5x_sata_init(&mss2_sata_data);
	orion5x_uart0_init();
	orion5x_xor_init();

	orion5x_setup_dev_boot_win(MSS2_NOR_BOOT_BASE, MSS2_NOR_BOOT_SIZE);
	platform_device_register(&mss2_nor_flash);

	platform_device_register(&mss2_button_device);

	if (gpio_request(MSS2_GPIO_RTC_IRQ, "rtc") == 0) {
		if (gpio_direction_input(MSS2_GPIO_RTC_IRQ) == 0)
			mss2_i2c_rtc.irq = gpio_to_irq(MSS2_GPIO_RTC_IRQ);
		else
			gpio_free(MSS2_GPIO_RTC_IRQ);
	}
	i2c_register_board_info(0, &mss2_i2c_rtc, 1);

	/* register mss2 specific power-off method */
	pm_power_off = mss2_power_off;
}

MACHINE_START(MSS2, "Maxtor Shared Storage II")
	/* Maintainer: Sylver Bruneau <sylver.bruneau@googlemail.com> */
	.atag_offset	= 0x100,
	.init_machine	= mss2_init,
	.map_io		= orion5x_map_io,
	.init_early	= orion5x_init_early,
	.init_irq	= orion5x_init_irq,
	.timer		= &orion5x_timer,
	.fixup		= tag_fixup_mem32
MACHINE_END
