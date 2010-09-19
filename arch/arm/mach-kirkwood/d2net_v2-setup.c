/*
 * arch/arm/mach-kirkwood/d2net_v2-setup.c
 *
 * LaCie d2 Network Space v2 Board Setup
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
#include <linux/mtd/physmap.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/i2c.h>
#include <linux/i2c/at24.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/leds.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/kirkwood.h>
#include <mach/leds-ns2.h>
#include <plat/time.h>
#include "common.h"
#include "mpp.h"

/*****************************************************************************
 * 512KB SPI Flash on Boot Device
 ****************************************************************************/

static struct mtd_partition d2net_v2_flash_parts[] = {
	{
		.name = "u-boot",
		.size = MTDPART_SIZ_FULL,
		.offset = 0,
		.mask_flags = MTD_WRITEABLE,
	},
};

static const struct flash_platform_data d2net_v2_flash = {
	.type		= "mx25l4005a",
	.name		= "spi_flash",
	.parts		= d2net_v2_flash_parts,
	.nr_parts	= ARRAY_SIZE(d2net_v2_flash_parts),
};

static struct spi_board_info __initdata d2net_v2_spi_slave_info[] = {
	{
		.modalias	= "m25p80",
		.platform_data	= &d2net_v2_flash,
		.irq		= -1,
		.max_speed_hz	= 20000000,
		.bus_num	= 0,
		.chip_select	= 0,
	},
};

/*****************************************************************************
 * Ethernet
 ****************************************************************************/

static struct mv643xx_eth_platform_data d2net_v2_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

/*****************************************************************************
 * I2C devices
 ****************************************************************************/

static struct at24_platform_data at24c04 = {
	.byte_len	= SZ_4K / 8,
	.page_size	= 16,
};

/*
 * i2c addr | chip         | description
 * 0x50     | HT24LC04     | eeprom (512B)
 */

static struct i2c_board_info __initdata d2net_v2_i2c_info[] = {
	{
		I2C_BOARD_INFO("24c04", 0x50),
		.platform_data  = &at24c04,
	}
};

/*****************************************************************************
 * SATA
 ****************************************************************************/

static struct mv_sata_platform_data d2net_v2_sata_data = {
	.n_ports	= 2,
};

#define D2NET_V2_GPIO_SATA0_POWER	16

static void __init d2net_v2_sata_power_init(void)
{
	int err;

	err = gpio_request(D2NET_V2_GPIO_SATA0_POWER, "SATA0 power");
	if (err == 0) {
		err = gpio_direction_output(D2NET_V2_GPIO_SATA0_POWER, 1);
		if (err)
			gpio_free(D2NET_V2_GPIO_SATA0_POWER);
	}
	if (err)
		pr_err("d2net_v2: failed to configure SATA0 power GPIO\n");
}

/*****************************************************************************
 * GPIO keys
 ****************************************************************************/

#define D2NET_V2_GPIO_PUSH_BUTTON          34
#define D2NET_V2_GPIO_POWER_SWITCH_ON      13
#define D2NET_V2_GPIO_POWER_SWITCH_OFF     15

#define D2NET_V2_SWITCH_POWER_ON           0x1
#define D2NET_V2_SWITCH_POWER_OFF          0x2

static struct gpio_keys_button d2net_v2_buttons[] = {
	[0] = {
		.type           = EV_SW,
		.code           = D2NET_V2_SWITCH_POWER_ON,
		.gpio           = D2NET_V2_GPIO_POWER_SWITCH_ON,
		.desc           = "Back power switch (on|auto)",
		.active_low     = 0,
	},
	[1] = {
		.type           = EV_SW,
		.code           = D2NET_V2_SWITCH_POWER_OFF,
		.gpio           = D2NET_V2_GPIO_POWER_SWITCH_OFF,
		.desc           = "Back power switch (auto|off)",
		.active_low     = 0,
	},
	[2] = {
		.code           = KEY_POWER,
		.gpio           = D2NET_V2_GPIO_PUSH_BUTTON,
		.desc           = "Front Push Button",
		.active_low     = 1,
	},
};

static struct gpio_keys_platform_data d2net_v2_button_data = {
	.buttons	= d2net_v2_buttons,
	.nbuttons	= ARRAY_SIZE(d2net_v2_buttons),
};

static struct platform_device d2net_v2_gpio_buttons = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev		= {
		.platform_data	= &d2net_v2_button_data,
	},
};

/*****************************************************************************
 * GPIO LEDs
 ****************************************************************************/

#define D2NET_V2_GPIO_RED_LED		12

static struct gpio_led d2net_v2_gpio_led_pins[] = {
	{
		.name	= "d2net_v2:red:fail",
		.gpio	= D2NET_V2_GPIO_RED_LED,
	},
};

static struct gpio_led_platform_data d2net_v2_gpio_leds_data = {
	.num_leds	= ARRAY_SIZE(d2net_v2_gpio_led_pins),
	.leds		= d2net_v2_gpio_led_pins,
};

static struct platform_device d2net_v2_gpio_leds = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &d2net_v2_gpio_leds_data,
	},
};

/*****************************************************************************
 * Dual-GPIO CPLD LEDs
 ****************************************************************************/

#define D2NET_V2_GPIO_BLUE_LED_SLOW	29
#define D2NET_V2_GPIO_BLUE_LED_CMD	30

static struct ns2_led d2net_v2_led_pins[] = {
	{
		.name	= "d2net_v2:blue:sata",
		.cmd	= D2NET_V2_GPIO_BLUE_LED_CMD,
		.slow	= D2NET_V2_GPIO_BLUE_LED_SLOW,
	},
};

static struct ns2_led_platform_data d2net_v2_leds_data = {
	.num_leds	= ARRAY_SIZE(d2net_v2_led_pins),
	.leds		= d2net_v2_led_pins,
};

static struct platform_device d2net_v2_leds = {
	.name		= "leds-ns2",
	.id		= -1,
	.dev		= {
		.platform_data	= &d2net_v2_leds_data,
	},
};

/*****************************************************************************
 * Timer
 ****************************************************************************/

static void d2net_v2_timer_init(void)
{
	kirkwood_tclk = 166666667;
	orion_time_init(IRQ_KIRKWOOD_BRIDGE, kirkwood_tclk);
}

struct sys_timer d2net_v2_timer = {
	.init = d2net_v2_timer_init,
};

/*****************************************************************************
 * General Setup
 ****************************************************************************/

static unsigned int d2net_v2_mpp_config[] __initdata = {
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
	MPP12_GPO,		/* Red led */
	MPP13_GPIO,		/* Rear power switch (on|auto) */
	MPP14_GPIO,		/* USB fuse */
	MPP15_GPIO,		/* Rear power switch (auto|off) */
	MPP16_GPIO,		/* SATA 0 power */
	MPP21_SATA0_ACTn,
	MPP24_GPIO,		/* USB mode select */
	MPP26_GPIO,		/* USB device vbus */
	MPP28_GPIO,		/* USB enable host vbus */
	MPP29_GPIO,		/* Blue led (slow register) */
	MPP30_GPIO,		/* Blue led (command register) */
	MPP34_GPIO,		/* Power button (1 = Released, 0 = Pushed) */
	MPP35_GPIO,		/* Inhibit power-off */
	0
};

#define D2NET_V2_GPIO_POWER_OFF		7

static void d2net_v2_power_off(void)
{
	gpio_set_value(D2NET_V2_GPIO_POWER_OFF, 1);
}

static void __init d2net_v2_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();
	kirkwood_mpp_conf(d2net_v2_mpp_config);

	d2net_v2_sata_power_init();

	kirkwood_ehci_init();
	kirkwood_ge00_init(&d2net_v2_ge00_data);
	kirkwood_sata_init(&d2net_v2_sata_data);
	kirkwood_uart0_init();
	spi_register_board_info(d2net_v2_spi_slave_info,
				ARRAY_SIZE(d2net_v2_spi_slave_info));
	kirkwood_spi_init();
	kirkwood_i2c_init();
	i2c_register_board_info(0, d2net_v2_i2c_info,
				ARRAY_SIZE(d2net_v2_i2c_info));

	platform_device_register(&d2net_v2_leds);
	platform_device_register(&d2net_v2_gpio_leds);
	platform_device_register(&d2net_v2_gpio_buttons);

	if (gpio_request(D2NET_V2_GPIO_POWER_OFF, "power-off") == 0 &&
	    gpio_direction_output(D2NET_V2_GPIO_POWER_OFF, 0) == 0)
		pm_power_off = d2net_v2_power_off;
	else
		pr_err("d2net_v2: failed to configure power-off GPIO\n");
}

MACHINE_START(D2NET_V2, "LaCie d2 Network v2")
	.phys_io	= KIRKWOOD_REGS_PHYS_BASE,
	.io_pg_offst	= ((KIRKWOOD_REGS_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.init_machine	= d2net_v2_init,
	.map_io		= kirkwood_map_io,
	.init_irq	= kirkwood_init_irq,
	.timer		= &d2net_v2_timer,
MACHINE_END
