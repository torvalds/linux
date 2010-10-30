/*
 * arch/arm/mach-kirkwood/netspace_v2-setup.c
 *
 * LaCie Network Space v2 board setup
 *
 * Copyright (C) 2009 Simon Guinot <sguinot@lacie.com>
 * Copyright (C) 2009 BenoÃ®t Canet <benoit.canet@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/leds.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include <mach/leds-ns2.h>
#include "common.h"
#include "mpp.h"
#include "lacie_v2-common.h"

/*****************************************************************************
 * Ethernet
 ****************************************************************************/

static struct mv643xx_eth_platform_data netspace_v2_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

/*****************************************************************************
 * SATA
 ****************************************************************************/

static struct mv_sata_platform_data netspace_v2_sata_data = {
	.n_ports	= 2,
};

/*****************************************************************************
 * GPIO keys
 ****************************************************************************/

#define NETSPACE_V2_PUSH_BUTTON		32

static struct gpio_keys_button netspace_v2_buttons[] = {
	[0] = {
		.code		= KEY_POWER,
		.gpio		= NETSPACE_V2_PUSH_BUTTON,
		.desc		= "Power push button",
		.active_low	= 0,
	},
};

static struct gpio_keys_platform_data netspace_v2_button_data = {
	.buttons	= netspace_v2_buttons,
	.nbuttons	= ARRAY_SIZE(netspace_v2_buttons),
};

static struct platform_device netspace_v2_gpio_buttons = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev		= {
		.platform_data 	= &netspace_v2_button_data,
	},
};

/*****************************************************************************
 * GPIO LEDs
 ****************************************************************************/

#define NETSPACE_V2_GPIO_RED_LED	12

static struct gpio_led netspace_v2_gpio_led_pins[] = {
	{
		.name	= "ns_v2:red:fail",
		.gpio	= NETSPACE_V2_GPIO_RED_LED,
	},
};

static struct gpio_led_platform_data netspace_v2_gpio_leds_data = {
	.num_leds	= ARRAY_SIZE(netspace_v2_gpio_led_pins),
	.leds		= netspace_v2_gpio_led_pins,
};

static struct platform_device netspace_v2_gpio_leds = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &netspace_v2_gpio_leds_data,
	},
};

/*****************************************************************************
 * Dual-GPIO CPLD LEDs
 ****************************************************************************/

#define NETSPACE_V2_GPIO_BLUE_LED_SLOW	29
#define NETSPACE_V2_GPIO_BLUE_LED_CMD	30

static struct ns2_led netspace_v2_led_pins[] = {
	{
		.name	= "ns_v2:blue:sata",
		.cmd	= NETSPACE_V2_GPIO_BLUE_LED_CMD,
		.slow	= NETSPACE_V2_GPIO_BLUE_LED_SLOW,
	},
};

static struct ns2_led_platform_data netspace_v2_leds_data = {
	.num_leds	= ARRAY_SIZE(netspace_v2_led_pins),
	.leds		= netspace_v2_led_pins,
};

static struct platform_device netspace_v2_leds = {
	.name		= "leds-ns2",
	.id		= -1,
	.dev		= {
		.platform_data	= &netspace_v2_leds_data,
	},
};

/*****************************************************************************
 * General Setup
 ****************************************************************************/

static unsigned int netspace_v2_mpp_config[] __initdata = {
	MPP0_SPI_SCn,
	MPP1_SPI_MOSI,
	MPP2_SPI_SCK,
	MPP3_SPI_MISO,
	MPP4_NF_IO6,
	MPP5_NF_IO7,
	MPP6_SYSRST_OUTn,
	MPP7_GPO,		/* Fan speed (bit 1) */
	MPP8_TW0_SDA,
	MPP9_TW0_SCK,
	MPP10_UART0_TXD,
	MPP11_UART0_RXD,
	MPP12_GPO,		/* Red led */
	MPP14_GPIO,		/* USB fuse */
	MPP16_GPIO,		/* SATA 0 power */
	MPP17_GPIO,		/* SATA 1 power */
	MPP18_NF_IO0,
	MPP19_NF_IO1,
	MPP20_SATA1_ACTn,
	MPP21_SATA0_ACTn,
	MPP22_GPIO,		/* Fan speed (bit 0) */
	MPP23_GPIO,		/* Fan power */
	MPP24_GPIO,		/* USB mode select */
	MPP25_GPIO,		/* Fan rotation fail */
	MPP26_GPIO,		/* USB device vbus */
	MPP28_GPIO,		/* USB enable host vbus */
	MPP29_GPIO,		/* Blue led (slow register) */
	MPP30_GPIO,		/* Blue led (command register) */
	MPP31_GPIO,		/* Board power off */
	MPP32_GPIO, 		/* Power button (0 = Released, 1 = Pushed) */
	MPP33_GPO,		/* Fan speed (bit 2) */
	0
};

#define NETSPACE_V2_GPIO_POWER_OFF	31

static void netspace_v2_power_off(void)
{
	gpio_set_value(NETSPACE_V2_GPIO_POWER_OFF, 1);
}

static void __init netspace_v2_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();
	kirkwood_mpp_conf(netspace_v2_mpp_config);

	if (machine_is_netspace_max_v2())
		lacie_v2_hdd_power_init(2);
	else
		lacie_v2_hdd_power_init(1);

	kirkwood_ehci_init();
	kirkwood_ge00_init(&netspace_v2_ge00_data);
	kirkwood_sata_init(&netspace_v2_sata_data);
	kirkwood_uart0_init();
	lacie_v2_register_flash();
	lacie_v2_register_i2c_devices();

	platform_device_register(&netspace_v2_leds);
	platform_device_register(&netspace_v2_gpio_leds);
	platform_device_register(&netspace_v2_gpio_buttons);

	if (gpio_request(NETSPACE_V2_GPIO_POWER_OFF, "power-off") == 0 &&
	    gpio_direction_output(NETSPACE_V2_GPIO_POWER_OFF, 0) == 0)
		pm_power_off = netspace_v2_power_off;
	else
		pr_err("netspace_v2: failed to configure power-off GPIO\n");
}

#ifdef CONFIG_MACH_NETSPACE_V2
MACHINE_START(NETSPACE_V2, "LaCie Network Space v2")
	.boot_params	= 0x00000100,
	.init_machine	= netspace_v2_init,
	.map_io		= kirkwood_map_io,
	.init_irq	= kirkwood_init_irq,
	.timer		= &lacie_v2_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_INETSPACE_V2
MACHINE_START(INETSPACE_V2, "LaCie Internet Space v2")
	.boot_params	= 0x00000100,
	.init_machine	= netspace_v2_init,
	.map_io		= kirkwood_map_io,
	.init_irq	= kirkwood_init_irq,
	.timer		= &lacie_v2_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_NETSPACE_MAX_V2
MACHINE_START(NETSPACE_MAX_V2, "LaCie Network Space Max v2")
	.boot_params	= 0x00000100,
	.init_machine	= netspace_v2_init,
	.map_io		= kirkwood_map_io,
	.init_irq	= kirkwood_init_irq,
	.timer		= &lacie_v2_timer,
MACHINE_END
#endif
