/*
 * arch/arm/mach-orion5x/d2net-setup.c
 *
 * LaCie d2Network and Big Disk Network NAS setup
 *
 * Copyright (C) 2009 Simon Guinot <sguinot@lacie.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
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
#include <plat/orion-gpio.h>
#include "common.h"
#include "mpp.h"

/*****************************************************************************
 * LaCie d2 Network Info
 ****************************************************************************/

/*
 * 512KB NOR flash Device bus boot chip select
 */

#define D2NET_NOR_BOOT_BASE		0xfff80000
#define D2NET_NOR_BOOT_SIZE		SZ_512K

/*****************************************************************************
 * 512KB NOR Flash on Boot Device
 ****************************************************************************/

/*
 * TODO: Check write support on flash MX29LV400CBTC-70G
 */

static struct mtd_partition d2net_partitions[] = {
	{
		.name		= "Full512kb",
		.size		= MTDPART_SIZ_FULL,
		.offset		= 0,
		.mask_flags	= MTD_WRITEABLE,
	},
};

static struct physmap_flash_data d2net_nor_flash_data = {
	.width		= 1,
	.parts		= d2net_partitions,
	.nr_parts	= ARRAY_SIZE(d2net_partitions),
};

static struct resource d2net_nor_flash_resource = {
	.flags			= IORESOURCE_MEM,
	.start			= D2NET_NOR_BOOT_BASE,
	.end			= D2NET_NOR_BOOT_BASE
					+ D2NET_NOR_BOOT_SIZE - 1,
};

static struct platform_device d2net_nor_flash = {
	.name			= "physmap-flash",
	.id			= 0,
	.dev		= {
		.platform_data	= &d2net_nor_flash_data,
	},
	.num_resources		= 1,
	.resource		= &d2net_nor_flash_resource,
};

/*****************************************************************************
 * Ethernet
 ****************************************************************************/

static struct mv643xx_eth_platform_data d2net_eth_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

/*****************************************************************************
 * I2C devices
 ****************************************************************************/

/*
 * i2c addr | chip         | description
 * 0x32     | Ricoh 5C372b | RTC
 * 0x3e     | GMT G762     | PWM fan controller
 * 0x50     | HT24LC08     | eeprom (1kB)
 *
 * TODO: Add G762 support to the g760a driver.
 */
static struct i2c_board_info __initdata d2net_i2c_devices[] = {
	{
		I2C_BOARD_INFO("rs5c372b", 0x32),
	}, {
		I2C_BOARD_INFO("24c08", 0x50),
	},
};

/*****************************************************************************
 * SATA
 ****************************************************************************/

static struct mv_sata_platform_data d2net_sata_data = {
	.n_ports	= 2,
};

#define D2NET_GPIO_SATA0_POWER	3
#define D2NET_GPIO_SATA1_POWER	12

static void __init d2net_sata_power_init(void)
{
	int err;

	err = gpio_request(D2NET_GPIO_SATA0_POWER, "SATA0 power");
	if (err == 0) {
		err = gpio_direction_output(D2NET_GPIO_SATA0_POWER, 1);
		if (err)
			gpio_free(D2NET_GPIO_SATA0_POWER);
	}
	if (err)
		pr_err("d2net: failed to configure SATA0 power GPIO\n");

	err = gpio_request(D2NET_GPIO_SATA1_POWER, "SATA1 power");
	if (err == 0) {
		err = gpio_direction_output(D2NET_GPIO_SATA1_POWER, 1);
		if (err)
			gpio_free(D2NET_GPIO_SATA1_POWER);
	}
	if (err)
		pr_err("d2net: failed to configure SATA1 power GPIO\n");
}

/*****************************************************************************
 * GPIO LED's
 ****************************************************************************/

/*
 * The blue front LED is wired to the CPLD and can blink in relation with the
 * SATA activity.
 *
 * The following array detail the different LED registers and the combination
 * of their possible values:
 *
 * led_off   | blink_ctrl | SATA active | LED state
 *           |            |             |
 *    1      |     x      |      x      |  off
 *    0      |     0      |      0      |  off
 *    0      |     1      |      0      |  blink (rate 300ms)
 *    0      |     x      |      1      |  on
 *
 * Notes: The blue and the red front LED's can't be on at the same time.
 *        Red LED have priority.
 */

#define D2NET_GPIO_RED_LED		6
#define D2NET_GPIO_BLUE_LED_BLINK_CTRL	16
#define D2NET_GPIO_BLUE_LED_OFF		23

static struct gpio_led d2net_leds[] = {
	{
		.name = "d2net:blue:sata",
		.default_trigger = "default-on",
		.gpio = D2NET_GPIO_BLUE_LED_OFF,
		.active_low = 1,
	},
	{
		.name = "d2net:red:fail",
		.gpio = D2NET_GPIO_RED_LED,
	},
};

static struct gpio_led_platform_data d2net_led_data = {
	.num_leds = ARRAY_SIZE(d2net_leds),
	.leds = d2net_leds,
};

static struct platform_device d2net_gpio_leds = {
	.name           = "leds-gpio",
	.id             = -1,
	.dev            = {
		.platform_data  = &d2net_led_data,
	},
};

static void __init d2net_gpio_leds_init(void)
{
	int err;

	/* Configure GPIO over MPP max number. */
	orion_gpio_set_valid(D2NET_GPIO_BLUE_LED_OFF, 1);

	/* Configure register blink_ctrl to allow SATA activity LED blinking. */
	err = gpio_request(D2NET_GPIO_BLUE_LED_BLINK_CTRL, "blue LED blink");
	if (err == 0) {
		err = gpio_direction_output(D2NET_GPIO_BLUE_LED_BLINK_CTRL, 1);
		if (err)
			gpio_free(D2NET_GPIO_BLUE_LED_BLINK_CTRL);
	}
	if (err)
		pr_err("d2net: failed to configure blue LED blink GPIO\n");

	platform_device_register(&d2net_gpio_leds);
}

/****************************************************************************
 * GPIO keys
 ****************************************************************************/

#define D2NET_GPIO_PUSH_BUTTON		18
#define D2NET_GPIO_POWER_SWITCH_ON	8
#define D2NET_GPIO_POWER_SWITCH_OFF	9

#define D2NET_SWITCH_POWER_ON		0x1
#define D2NET_SWITCH_POWER_OFF		0x2

static struct gpio_keys_button d2net_buttons[] = {
	{
		.type		= EV_SW,
		.code		= D2NET_SWITCH_POWER_OFF,
		.gpio		= D2NET_GPIO_POWER_SWITCH_OFF,
		.desc		= "Power rocker switch (auto|off)",
		.active_low	= 0,
	},
	{
		.type		= EV_SW,
		.code		= D2NET_SWITCH_POWER_ON,
		.gpio		= D2NET_GPIO_POWER_SWITCH_ON,
		.desc		= "Power rocker switch (on|auto)",
		.active_low	= 0,
	},
	{
		.type		= EV_KEY,
		.code		= KEY_POWER,
		.gpio		= D2NET_GPIO_PUSH_BUTTON,
		.desc		= "Front Push Button",
		.active_low	= 0,
	},
};

static struct gpio_keys_platform_data d2net_button_data = {
	.buttons	= d2net_buttons,
	.nbuttons	= ARRAY_SIZE(d2net_buttons),
};

static struct platform_device d2net_gpio_buttons = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev		= {
		.platform_data	= &d2net_button_data,
	},
};

/*****************************************************************************
 * General Setup
 ****************************************************************************/

static unsigned int d2net_mpp_modes[] __initdata = {
	MPP0_GPIO,	/* Board ID (bit 0) */
	MPP1_GPIO,	/* Board ID (bit 1) */
	MPP2_GPIO,	/* Board ID (bit 2) */
	MPP3_GPIO,	/* SATA 0 power */
	MPP4_UNUSED,
	MPP5_GPIO,	/* Fan fail detection */
	MPP6_GPIO,	/* Red front LED */
	MPP7_UNUSED,
	MPP8_GPIO,	/* Rear power switch (on|auto) */
	MPP9_GPIO,	/* Rear power switch (auto|off) */
	MPP10_UNUSED,
	MPP11_UNUSED,
	MPP12_GPIO,	/* SATA 1 power */
	MPP13_UNUSED,
	MPP14_SATA_LED,	/* SATA 0 active */
	MPP15_SATA_LED,	/* SATA 1 active */
	MPP16_GPIO,	/* Blue front LED blink control */
	MPP17_UNUSED,
	MPP18_GPIO,	/* Front button (0 = Released, 1 = Pushed ) */
	MPP19_UNUSED,
	0,
	/* 22: USB port 1 fuse (0 = Fail, 1 = Ok) */
	/* 23: Blue front LED off */
	/* 24: Inhibit board power off (0 = Disabled, 1 = Enabled) */
};

#define D2NET_GPIO_INHIBIT_POWER_OFF    24

static void __init d2net_init(void)
{
	/*
	 * Setup basic Orion functions. Need to be called early.
	 */
	orion5x_init();

	orion5x_mpp_conf(d2net_mpp_modes);

	/*
	 * Configure peripherals.
	 */
	orion5x_ehci0_init();
	orion5x_eth_init(&d2net_eth_data);
	orion5x_i2c_init();
	orion5x_uart0_init();

	d2net_sata_power_init();
	orion5x_sata_init(&d2net_sata_data);

	mvebu_mbus_add_window("devbus-boot", D2NET_NOR_BOOT_BASE,
			      D2NET_NOR_BOOT_SIZE);
	platform_device_register(&d2net_nor_flash);

	platform_device_register(&d2net_gpio_buttons);

	d2net_gpio_leds_init();

	pr_notice("d2net: Flash write are not yet supported.\n");

	i2c_register_board_info(0, d2net_i2c_devices,
				ARRAY_SIZE(d2net_i2c_devices));

	orion_gpio_set_valid(D2NET_GPIO_INHIBIT_POWER_OFF, 1);
}

/* Warning: LaCie use a wrong mach-type (0x20e=526) in their bootloader. */

#ifdef CONFIG_MACH_D2NET
MACHINE_START(D2NET, "LaCie d2 Network")
	.atag_offset	= 0x100,
	.init_machine	= d2net_init,
	.map_io		= orion5x_map_io,
	.init_early	= orion5x_init_early,
	.init_irq	= orion5x_init_irq,
	.init_time	= orion5x_timer_init,
	.fixup		= tag_fixup_mem32,
	.restart	= orion5x_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_BIGDISK
MACHINE_START(BIGDISK, "LaCie Big Disk Network")
	.atag_offset	= 0x100,
	.init_machine	= d2net_init,
	.map_io		= orion5x_map_io,
	.init_early	= orion5x_init_early,
	.init_irq	= orion5x_init_irq,
	.init_time	= orion5x_timer_init,
	.fixup		= tag_fixup_mem32,
	.restart	= orion5x_restart,
MACHINE_END
#endif

