/*
 * arch/arm/mach-kirkwood/netxbig_v2-setup.c
 *
 * LaCie 2Big and 5Big Network v2 board setup
 *
 * Copyright (C) 2010 Simon Guinot <sguinot@lacie.com>
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
#include <mach/leds-netxbig.h>
#include "common.h"
#include "mpp.h"
#include "lacie_v2-common.h"

/*****************************************************************************
 * Ethernet
 ****************************************************************************/

static struct mv643xx_eth_platform_data netxbig_v2_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static struct mv643xx_eth_platform_data netxbig_v2_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

/*****************************************************************************
 * SATA
 ****************************************************************************/

static struct mv_sata_platform_data netxbig_v2_sata_data = {
	.n_ports	= 2,
};

/*****************************************************************************
 * GPIO keys
 ****************************************************************************/

#define NETXBIG_V2_GPIO_SWITCH_POWER_ON		13
#define NETXBIG_V2_GPIO_SWITCH_POWER_OFF	15
#define NETXBIG_V2_GPIO_FUNC_BUTTON		34

#define NETXBIG_V2_SWITCH_POWER_ON		0x1
#define NETXBIG_V2_SWITCH_POWER_OFF		0x2

static struct gpio_keys_button netxbig_v2_buttons[] = {
	[0] = {
		.type           = EV_SW,
		.code           = NETXBIG_V2_SWITCH_POWER_ON,
		.gpio           = NETXBIG_V2_GPIO_SWITCH_POWER_ON,
		.desc           = "Back power switch (on|auto)",
		.active_low     = 1,
	},
	[1] = {
		.type           = EV_SW,
		.code           = NETXBIG_V2_SWITCH_POWER_OFF,
		.gpio           = NETXBIG_V2_GPIO_SWITCH_POWER_OFF,
		.desc           = "Back power switch (auto|off)",
		.active_low     = 1,
	},
	[2] = {
		.code		= KEY_OPTION,
		.gpio		= NETXBIG_V2_GPIO_FUNC_BUTTON,
		.desc		= "Function button",
		.active_low	= 1,
	},
};

static struct gpio_keys_platform_data netxbig_v2_button_data = {
	.buttons	= netxbig_v2_buttons,
	.nbuttons	= ARRAY_SIZE(netxbig_v2_buttons),
};

static struct platform_device netxbig_v2_gpio_buttons = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev		= {
		.platform_data	= &netxbig_v2_button_data,
	},
};

/*****************************************************************************
 * GPIO extension LEDs
 ****************************************************************************/

/*
 * The LEDs are controlled by a CPLD and can be configured through a GPIO
 * extension bus:
 *
 * - address register : bit [0-2] -> GPIO [47-49]
 * - data register    : bit [0-2] -> GPIO [44-46]
 * - enable register  : GPIO 29
 */

static int netxbig_v2_gpio_ext_addr[] = { 47, 48, 49 };
static int netxbig_v2_gpio_ext_data[] = { 44, 45, 46 };

static struct netxbig_gpio_ext netxbig_v2_gpio_ext = {
	.addr		= netxbig_v2_gpio_ext_addr,
	.num_addr	= ARRAY_SIZE(netxbig_v2_gpio_ext_addr),
	.data		= netxbig_v2_gpio_ext_data,
	.num_data	= ARRAY_SIZE(netxbig_v2_gpio_ext_data),
	.enable		= 29,
};

/*
 * Address register selection:
 *
 * addr | register
 * ----------------------------
 *   0  | front LED
 *   1  | front LED brightness
 *   2  | SATA LED brightness
 *   3  | SATA0 LED
 *   4  | SATA1 LED
 *   5  | SATA2 LED
 *   6  | SATA3 LED
 *   7  | SATA4 LED
 *
 * Data register configuration:
 *
 * data | LED brightness
 * -------------------------------------------------
 *   0  | min (off)
 *   -  | -
 *   7  | max
 *
 * data | front LED mode
 * -------------------------------------------------
 *   0  | fix off
 *   1  | fix blue on
 *   2  | fix red on
 *   3  | blink blue on=1 sec and blue off=1 sec
 *   4  | blink red on=1 sec and red off=1 sec
 *   5  | blink blue on=2.5 sec and red on=0.5 sec
 *   6  | blink blue on=1 sec and red on=1 sec
 *   7  | blink blue on=0.5 sec and blue off=2.5 sec
 *
 * data | SATA LED mode
 * -------------------------------------------------
 *   0  | fix off
 *   1  | SATA activity blink
 *   2  | fix red on
 *   3  | blink blue on=1 sec and blue off=1 sec
 *   4  | blink red on=1 sec and red off=1 sec
 *   5  | blink blue on=2.5 sec and red on=0.5 sec
 *   6  | blink blue on=1 sec and red on=1 sec
 *   7  | fix blue on
 */

static int netxbig_v2_red_mled[NETXBIG_LED_MODE_NUM] = {
	[NETXBIG_LED_OFF]	= 0,
	[NETXBIG_LED_ON]	= 2,
	[NETXBIG_LED_SATA]	= NETXBIG_LED_INVALID_MODE,
	[NETXBIG_LED_TIMER1]	= 4,
	[NETXBIG_LED_TIMER2]	= NETXBIG_LED_INVALID_MODE,
};

static int netxbig_v2_blue_pwr_mled[NETXBIG_LED_MODE_NUM] = {
	[NETXBIG_LED_OFF]	= 0,
	[NETXBIG_LED_ON]	= 1,
	[NETXBIG_LED_SATA]	= NETXBIG_LED_INVALID_MODE,
	[NETXBIG_LED_TIMER1]	= 3,
	[NETXBIG_LED_TIMER2]	= 7,
};

static int netxbig_v2_blue_sata_mled[NETXBIG_LED_MODE_NUM] = {
	[NETXBIG_LED_OFF]	= 0,
	[NETXBIG_LED_ON]	= 7,
	[NETXBIG_LED_SATA]	= 1,
	[NETXBIG_LED_TIMER1]	= 3,
	[NETXBIG_LED_TIMER2]	= NETXBIG_LED_INVALID_MODE,
};

static struct netxbig_led_timer netxbig_v2_led_timer[] = {
	[0] = {
		.delay_on	= 500,
		.delay_off	= 500,
		.mode		= NETXBIG_LED_TIMER1,
	},
	[1] = {
		.delay_on	= 500,
		.delay_off	= 1000,
		.mode		= NETXBIG_LED_TIMER2,
	},
};

#define NETXBIG_LED(_name, maddr, mval, baddr)			\
	{ .name		= _name,				\
	  .mode_addr	= maddr,				\
	  .mode_val	= mval,					\
	  .bright_addr	= baddr }

static struct netxbig_led net2big_v2_leds_ctrl[] = {
	NETXBIG_LED("net2big-v2:blue:power", 0, netxbig_v2_blue_pwr_mled,  1),
	NETXBIG_LED("net2big-v2:red:power",  0, netxbig_v2_red_mled,       1),
	NETXBIG_LED("net2big-v2:blue:sata0", 3, netxbig_v2_blue_sata_mled, 2),
	NETXBIG_LED("net2big-v2:red:sata0",  3, netxbig_v2_red_mled,       2),
	NETXBIG_LED("net2big-v2:blue:sata1", 4, netxbig_v2_blue_sata_mled, 2),
	NETXBIG_LED("net2big-v2:red:sata1",  4, netxbig_v2_red_mled,       2),
};

static struct netxbig_led_platform_data net2big_v2_leds_data = {
	.gpio_ext	= &netxbig_v2_gpio_ext,
	.timer		= netxbig_v2_led_timer,
	.num_timer	= ARRAY_SIZE(netxbig_v2_led_timer),
	.leds		= net2big_v2_leds_ctrl,
	.num_leds	= ARRAY_SIZE(net2big_v2_leds_ctrl),
};

static struct netxbig_led net5big_v2_leds_ctrl[] = {
	NETXBIG_LED("net5big-v2:blue:power", 0, netxbig_v2_blue_pwr_mled,  1),
	NETXBIG_LED("net5big-v2:red:power",  0, netxbig_v2_red_mled,       1),
	NETXBIG_LED("net5big-v2:blue:sata0", 3, netxbig_v2_blue_sata_mled, 2),
	NETXBIG_LED("net5big-v2:red:sata0",  3, netxbig_v2_red_mled,       2),
	NETXBIG_LED("net5big-v2:blue:sata1", 4, netxbig_v2_blue_sata_mled, 2),
	NETXBIG_LED("net5big-v2:red:sata1",  4, netxbig_v2_red_mled,       2),
	NETXBIG_LED("net5big-v2:blue:sata2", 5, netxbig_v2_blue_sata_mled, 2),
	NETXBIG_LED("net5big-v2:red:sata2",  5, netxbig_v2_red_mled,       2),
	NETXBIG_LED("net5big-v2:blue:sata3", 6, netxbig_v2_blue_sata_mled, 2),
	NETXBIG_LED("net5big-v2:red:sata3",  6, netxbig_v2_red_mled,       2),
	NETXBIG_LED("net5big-v2:blue:sata4", 7, netxbig_v2_blue_sata_mled, 2),
	NETXBIG_LED("net5big-v2:red:sata5",  7, netxbig_v2_red_mled,       2),
};

static struct netxbig_led_platform_data net5big_v2_leds_data = {
	.gpio_ext	= &netxbig_v2_gpio_ext,
	.timer		= netxbig_v2_led_timer,
	.num_timer	= ARRAY_SIZE(netxbig_v2_led_timer),
	.leds		= net5big_v2_leds_ctrl,
	.num_leds	= ARRAY_SIZE(net5big_v2_leds_ctrl),
};

static struct platform_device netxbig_v2_leds = {
	.name		= "leds-netxbig",
	.id		= -1,
	.dev		= {
		.platform_data	= &net2big_v2_leds_data,
	},
};

/*****************************************************************************
 * General Setup
 ****************************************************************************/

static unsigned int net2big_v2_mpp_config[] __initdata = {
	MPP0_SPI_SCn,
	MPP1_SPI_MOSI,
	MPP2_SPI_SCK,
	MPP3_SPI_MISO,
	MPP6_SYSRST_OUTn,
	MPP7_GPO,		/* Request power-off */
	MPP8_TW0_SDA,
	MPP9_TW0_SCK,
	MPP10_UART0_TXD,
	MPP11_UART0_RXD,
	MPP13_GPIO,		/* Rear power switch (on|auto) */
	MPP14_GPIO,		/* USB fuse alarm */
	MPP15_GPIO,		/* Rear power switch (auto|off) */
	MPP16_GPIO,		/* SATA HDD1 power */
	MPP17_GPIO,		/* SATA HDD2 power */
	MPP20_SATA1_ACTn,
	MPP21_SATA0_ACTn,
	MPP24_GPIO,		/* USB mode select */
	MPP26_GPIO,		/* USB device vbus */
	MPP28_GPIO,		/* USB enable host vbus */
	MPP29_GPIO,		/* GPIO extension ALE */
	MPP34_GPIO,		/* Rear Push button */
	MPP35_GPIO,		/* Inhibit switch power-off */
	MPP36_GPIO,		/* SATA HDD1 presence */
	MPP37_GPIO,		/* SATA HDD2 presence */
	MPP40_GPIO,		/* eSATA presence */
	MPP44_GPIO,		/* GPIO extension (data 0) */
	MPP45_GPIO,		/* GPIO extension (data 1) */
	MPP46_GPIO,		/* GPIO extension (data 2) */
	MPP47_GPIO,		/* GPIO extension (addr 0) */
	MPP48_GPIO,		/* GPIO extension (addr 1) */
	MPP49_GPIO,		/* GPIO extension (addr 2) */
	0
};

static unsigned int net5big_v2_mpp_config[] __initdata = {
	MPP0_SPI_SCn,
	MPP1_SPI_MOSI,
	MPP2_SPI_SCK,
	MPP3_SPI_MISO,
	MPP6_SYSRST_OUTn,
	MPP7_GPO,		/* Request power-off */
	MPP8_TW0_SDA,
	MPP9_TW0_SCK,
	MPP10_UART0_TXD,
	MPP11_UART0_RXD,
	MPP13_GPIO,		/* Rear power switch (on|auto) */
	MPP14_GPIO,		/* USB fuse alarm */
	MPP15_GPIO,		/* Rear power switch (auto|off) */
	MPP16_GPIO,		/* SATA HDD1 power */
	MPP17_GPIO,		/* SATA HDD2 power */
	MPP20_GE1_TXD0,
	MPP21_GE1_TXD1,
	MPP22_GE1_TXD2,
	MPP23_GE1_TXD3,
	MPP24_GE1_RXD0,
	MPP25_GE1_RXD1,
	MPP26_GE1_RXD2,
	MPP27_GE1_RXD3,
	MPP28_GPIO,		/* USB enable host vbus */
	MPP29_GPIO,		/* GPIO extension ALE */
	MPP30_GE1_RXCTL,
	MPP31_GE1_RXCLK,
	MPP32_GE1_TCLKOUT,
	MPP33_GE1_TXCTL,
	MPP34_GPIO,		/* Rear Push button */
	MPP35_GPIO,		/* Inhibit switch power-off */
	MPP36_GPIO,		/* SATA HDD1 presence */
	MPP37_GPIO,		/* SATA HDD2 presence */
	MPP38_GPIO,		/* SATA HDD3 presence */
	MPP39_GPIO,		/* SATA HDD4 presence */
	MPP40_GPIO,		/* SATA HDD5 presence */
	MPP41_GPIO,		/* SATA HDD3 power */
	MPP42_GPIO,		/* SATA HDD4 power */
	MPP43_GPIO,		/* SATA HDD5 power */
	MPP44_GPIO,		/* GPIO extension (data 0) */
	MPP45_GPIO,		/* GPIO extension (data 1) */
	MPP46_GPIO,		/* GPIO extension (data 2) */
	MPP47_GPIO,		/* GPIO extension (addr 0) */
	MPP48_GPIO,		/* GPIO extension (addr 1) */
	MPP49_GPIO,		/* GPIO extension (addr 2) */
	0
};

#define NETXBIG_V2_GPIO_POWER_OFF		7

static void netxbig_v2_power_off(void)
{
	gpio_set_value(NETXBIG_V2_GPIO_POWER_OFF, 1);
}

static void __init netxbig_v2_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();
	if (machine_is_net2big_v2())
		kirkwood_mpp_conf(net2big_v2_mpp_config);
	else
		kirkwood_mpp_conf(net5big_v2_mpp_config);

	if (machine_is_net2big_v2())
		lacie_v2_hdd_power_init(2);
	else
		lacie_v2_hdd_power_init(5);

	kirkwood_ehci_init();
	kirkwood_ge00_init(&netxbig_v2_ge00_data);
	if (machine_is_net5big_v2())
		kirkwood_ge01_init(&netxbig_v2_ge01_data);
	kirkwood_sata_init(&netxbig_v2_sata_data);
	kirkwood_uart0_init();
	lacie_v2_register_flash();
	lacie_v2_register_i2c_devices();

	if (machine_is_net5big_v2())
		netxbig_v2_leds.dev.platform_data = &net5big_v2_leds_data;
	platform_device_register(&netxbig_v2_leds);
	platform_device_register(&netxbig_v2_gpio_buttons);

	if (gpio_request(NETXBIG_V2_GPIO_POWER_OFF, "power-off") == 0 &&
	    gpio_direction_output(NETXBIG_V2_GPIO_POWER_OFF, 0) == 0)
		pm_power_off = netxbig_v2_power_off;
	else
		pr_err("netxbig_v2: failed to configure power-off GPIO\n");
}

#ifdef CONFIG_MACH_NET2BIG_V2
MACHINE_START(NET2BIG_V2, "LaCie 2Big Network v2")
	.atag_offset	= 0x100,
	.init_machine	= netxbig_v2_init,
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_NET5BIG_V2
MACHINE_START(NET5BIG_V2, "LaCie 5Big Network v2")
	.atag_offset	= 0x100,
	.init_machine	= netxbig_v2_init,
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
MACHINE_END
#endif
