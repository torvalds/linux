/*
 * arch/arm/mach-orion5x/net2big-setup.c
 *
 * LaCie 2Big Network NAS setup
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
#include <linux/mtd/physmap.h>
#include <linux/mv643xx_eth.h>
#include <linux/leds.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/ata_platform.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/orion5x.h>
#include "common.h"
#include "mpp.h"

/*****************************************************************************
 * LaCie 2Big Network Info
 ****************************************************************************/

/*
 * 512KB NOR flash Device bus boot chip select
 */

#define NET2BIG_NOR_BOOT_BASE		0xfff80000
#define NET2BIG_NOR_BOOT_SIZE		SZ_512K

/*****************************************************************************
 * 512KB NOR Flash on Boot Device
 ****************************************************************************/

/*
 * TODO: Check write support on flash MX29LV400CBTC-70G
 */

static struct mtd_partition net2big_partitions[] = {
	{
		.name		= "Full512kb",
		.size		= MTDPART_SIZ_FULL,
		.offset		= 0x00000000,
		.mask_flags	= MTD_WRITEABLE,
	},
};

static struct physmap_flash_data net2big_nor_flash_data = {
	.width		= 1,
	.parts		= net2big_partitions,
	.nr_parts	= ARRAY_SIZE(net2big_partitions),
};

static struct resource net2big_nor_flash_resource = {
	.flags			= IORESOURCE_MEM,
	.start			= NET2BIG_NOR_BOOT_BASE,
	.end			= NET2BIG_NOR_BOOT_BASE
					+ NET2BIG_NOR_BOOT_SIZE - 1,
};

static struct platform_device net2big_nor_flash = {
	.name			= "physmap-flash",
	.id			= 0,
	.dev		= {
		.platform_data	= &net2big_nor_flash_data,
	},
	.num_resources		= 1,
	.resource		= &net2big_nor_flash_resource,
};

/*****************************************************************************
 * Ethernet
 ****************************************************************************/

static struct mv643xx_eth_platform_data net2big_eth_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

/*****************************************************************************
 * I2C devices
 ****************************************************************************/

/*
 * i2c addr | chip         | description
 * 0x32     | Ricoh 5C372b | RTC
 * 0x50     | HT24LC08     | eeprom (1kB)
 */
static struct i2c_board_info __initdata net2big_i2c_devices[] = {
	{
		I2C_BOARD_INFO("rs5c372b", 0x32),
	}, {
		I2C_BOARD_INFO("24c08", 0x50),
	},
};

/*****************************************************************************
 * SATA
 ****************************************************************************/

static struct mv_sata_platform_data net2big_sata_data = {
	.n_ports	= 2,
};

#define NET2BIG_GPIO_SATA_POWER_REQ	19
#define NET2BIG_GPIO_SATA0_POWER	23
#define NET2BIG_GPIO_SATA1_POWER	25

static void __init net2big_sata_power_init(void)
{
	int err;

	/* Configure GPIOs over MPP max number. */
	orion_gpio_set_valid(NET2BIG_GPIO_SATA0_POWER, 1);
	orion_gpio_set_valid(NET2BIG_GPIO_SATA1_POWER, 1);

	err = gpio_request(NET2BIG_GPIO_SATA0_POWER, "SATA0 power status");
	if (err == 0) {
		err = gpio_direction_input(NET2BIG_GPIO_SATA0_POWER);
		if (err)
			gpio_free(NET2BIG_GPIO_SATA0_POWER);
	}
	if (err) {
		pr_err("net2big: failed to setup SATA0 power GPIO\n");
		return;
	}

	err = gpio_request(NET2BIG_GPIO_SATA1_POWER, "SATA1 power status");
	if (err == 0) {
		err = gpio_direction_input(NET2BIG_GPIO_SATA1_POWER);
		if (err)
			gpio_free(NET2BIG_GPIO_SATA1_POWER);
	}
	if (err) {
		pr_err("net2big: failed to setup SATA1 power GPIO\n");
		goto err_free_1;
	}

	err = gpio_request(NET2BIG_GPIO_SATA_POWER_REQ, "SATA power request");
	if (err == 0) {
		err = gpio_direction_output(NET2BIG_GPIO_SATA_POWER_REQ, 0);
		if (err)
			gpio_free(NET2BIG_GPIO_SATA_POWER_REQ);
	}
	if (err) {
		pr_err("net2big: failed to setup SATA power request GPIO\n");
		goto err_free_2;
	}

	if (gpio_get_value(NET2BIG_GPIO_SATA0_POWER) &&
		gpio_get_value(NET2BIG_GPIO_SATA1_POWER)) {
		return;
	}

	/*
	 * SATA power up on both disk is done by pulling high the CPLD power
	 * request line. The 300ms delay is related to the CPLD clock and is
	 * needed to be sure that the CPLD has take into account the low line
	 * status.
	 */
	msleep(300);
	gpio_set_value(NET2BIG_GPIO_SATA_POWER_REQ, 1);
	pr_info("net2big: power up SATA hard disks\n");

	return;

err_free_2:
	gpio_free(NET2BIG_GPIO_SATA1_POWER);
err_free_1:
	gpio_free(NET2BIG_GPIO_SATA0_POWER);

	return;
}

/*****************************************************************************
 * GPIO LEDs
 ****************************************************************************/

/*
 * The power front LEDs (blue and red) and SATA red LEDs are controlled via a
 * single GPIO line and are compatible with the leds-gpio driver.
 *
 * The SATA blue LEDs have some hardware blink capabilities which are detailed
 * in the following array:
 *
 * SATAx blue LED | SATAx activity | LED state
 *                |                |
 *       0        |       0        |  blink (rate 300ms)
 *       1        |       0        |  off
 *       ?        |       1        |  on
 *
 * Notes: The blue and the red front LED's can't be on at the same time.
 *        Blue LED have priority.
 */

#define NET2BIG_GPIO_PWR_RED_LED	6
#define NET2BIG_GPIO_PWR_BLUE_LED	16
#define NET2BIG_GPIO_PWR_LED_BLINK_STOP	7

#define NET2BIG_GPIO_SATA0_RED_LED	11
#define NET2BIG_GPIO_SATA1_RED_LED	10

#define NET2BIG_GPIO_SATA0_BLUE_LED	17
#define NET2BIG_GPIO_SATA1_BLUE_LED	13

static struct gpio_led net2big_leds[] = {
	{
		.name = "net2big:red:power",
		.gpio = NET2BIG_GPIO_PWR_RED_LED,
	},
	{
		.name = "net2big:blue:power",
		.gpio = NET2BIG_GPIO_PWR_BLUE_LED,
	},
	{
		.name = "net2big:red:sata0",
		.gpio = NET2BIG_GPIO_SATA0_RED_LED,
	},
	{
		.name = "net2big:red:sata1",
		.gpio = NET2BIG_GPIO_SATA1_RED_LED,
	},
};

static struct gpio_led_platform_data net2big_led_data = {
	.num_leds = ARRAY_SIZE(net2big_leds),
	.leds = net2big_leds,
};

static struct platform_device net2big_gpio_leds = {
	.name           = "leds-gpio",
	.id             = -1,
	.dev            = {
		.platform_data  = &net2big_led_data,
	},
};

static void __init net2big_gpio_leds_init(void)
{
	int err;

	/* Stop initial CPLD slow red/blue blinking on power LED. */
	err = gpio_request(NET2BIG_GPIO_PWR_LED_BLINK_STOP,
			   "Power LED blink stop");
	if (err == 0) {
		err = gpio_direction_output(NET2BIG_GPIO_PWR_LED_BLINK_STOP, 1);
		if (err)
			gpio_free(NET2BIG_GPIO_PWR_LED_BLINK_STOP);
	}
	if (err)
		pr_err("net2big: failed to setup power LED blink GPIO\n");

	/*
	 * Configure SATA0 and SATA1 blue LEDs to blink in relation with the
	 * hard disk activity.
	 */
	err = gpio_request(NET2BIG_GPIO_SATA0_BLUE_LED,
			   "SATA0 blue LED control");
	if (err == 0) {
		err = gpio_direction_output(NET2BIG_GPIO_SATA0_BLUE_LED, 1);
		if (err)
			gpio_free(NET2BIG_GPIO_SATA0_BLUE_LED);
	}
	if (err)
		pr_err("net2big: failed to setup SATA0 blue LED GPIO\n");

	err = gpio_request(NET2BIG_GPIO_SATA1_BLUE_LED,
			   "SATA1 blue LED control");
	if (err == 0) {
		err = gpio_direction_output(NET2BIG_GPIO_SATA1_BLUE_LED, 1);
		if (err)
			gpio_free(NET2BIG_GPIO_SATA1_BLUE_LED);
	}
	if (err)
		pr_err("net2big: failed to setup SATA1 blue LED GPIO\n");

	platform_device_register(&net2big_gpio_leds);
}

/****************************************************************************
 * GPIO keys
 ****************************************************************************/

#define NET2BIG_GPIO_PUSH_BUTTON	18
#define NET2BIG_GPIO_POWER_SWITCH_ON	8
#define NET2BIG_GPIO_POWER_SWITCH_OFF	9

#define NET2BIG_SWITCH_POWER_ON		0x1
#define NET2BIG_SWITCH_POWER_OFF	0x2

static struct gpio_keys_button net2big_buttons[] = {
	{
		.type		= EV_SW,
		.code		= NET2BIG_SWITCH_POWER_OFF,
		.gpio		= NET2BIG_GPIO_POWER_SWITCH_OFF,
		.desc		= "Power rocker switch (auto|off)",
		.active_low	= 0,
	},
	{
		.type		= EV_SW,
		.code		= NET2BIG_SWITCH_POWER_ON,
		.gpio		= NET2BIG_GPIO_POWER_SWITCH_ON,
		.desc		= "Power rocker switch (on|auto)",
		.active_low	= 0,
	},
	{
		.type		= EV_KEY,
		.code		= KEY_POWER,
		.gpio		= NET2BIG_GPIO_PUSH_BUTTON,
		.desc		= "Front Push Button",
		.active_low	= 0,
	},
};

static struct gpio_keys_platform_data net2big_button_data = {
	.buttons	= net2big_buttons,
	.nbuttons	= ARRAY_SIZE(net2big_buttons),
};

static struct platform_device net2big_gpio_buttons = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev		= {
		.platform_data	= &net2big_button_data,
	},
};

/*****************************************************************************
 * General Setup
 ****************************************************************************/

static struct orion5x_mpp_mode net2big_mpp_modes[] __initdata = {
	{  0, MPP_GPIO },	/* Raid mode (bit 0) */
	{  1, MPP_GPIO },	/* USB port 2 fuse (0 = Fail, 1 = Ok) */
	{  2, MPP_GPIO },	/* Raid mode (bit 1) */
	{  3, MPP_GPIO },	/* Board ID (bit 0) */
	{  4, MPP_GPIO },	/* Fan activity (0 = Off, 1 = On) */
	{  5, MPP_GPIO },	/* Fan fail detection */
	{  6, MPP_GPIO },	/* Red front LED (0 = Off, 1 = On) */
	{  7, MPP_GPIO },	/* Disable initial blinking on front LED */
	{  8, MPP_GPIO },	/* Rear power switch (on|auto) */
	{  9, MPP_GPIO },	/* Rear power switch (auto|off) */
	{ 10, MPP_GPIO },	/* SATA 1 red LED (0 = Off, 1 = On) */
	{ 11, MPP_GPIO },	/* SATA 0 red LED (0 = Off, 1 = On) */
	{ 12, MPP_GPIO },	/* Board ID (bit 1) */
	{ 13, MPP_GPIO },	/* SATA 1 blue LED blink control */
	{ 14, MPP_SATA_LED },
	{ 15, MPP_SATA_LED },
	{ 16, MPP_GPIO },	/* Blue front LED control */
	{ 17, MPP_GPIO },	/* SATA 0 blue LED blink control */
	{ 18, MPP_GPIO },	/* Front button (0 = Released, 1 = Pushed ) */
	{ 19, MPP_GPIO },	/* SATA{0,1} power On/Off request */
	{ -1 }
	/* 22: USB port 1 fuse (0 = Fail, 1 = Ok) */
	/* 23: SATA 0 power status */
	/* 24: Board power off */
	/* 25: SATA 1 power status */
};

#define NET2BIG_GPIO_POWER_OFF		24

static void net2big_power_off(void)
{
	gpio_set_value(NET2BIG_GPIO_POWER_OFF, 1);
}

static void __init net2big_init(void)
{
	/*
	 * Setup basic Orion functions. Need to be called early.
	 */
	orion5x_init();

	orion5x_mpp_conf(net2big_mpp_modes);

	/*
	 * Configure peripherals.
	 */
	orion5x_ehci0_init();
	orion5x_ehci1_init();
	orion5x_eth_init(&net2big_eth_data);
	orion5x_i2c_init();
	orion5x_uart0_init();
	orion5x_xor_init();

	net2big_sata_power_init();
	orion5x_sata_init(&net2big_sata_data);

	orion5x_setup_dev_boot_win(NET2BIG_NOR_BOOT_BASE,
				   NET2BIG_NOR_BOOT_SIZE);
	platform_device_register(&net2big_nor_flash);

	platform_device_register(&net2big_gpio_buttons);
	net2big_gpio_leds_init();

	i2c_register_board_info(0, net2big_i2c_devices,
				ARRAY_SIZE(net2big_i2c_devices));

	orion_gpio_set_valid(NET2BIG_GPIO_POWER_OFF, 1);

	if (gpio_request(NET2BIG_GPIO_POWER_OFF, "power-off") == 0 &&
	    gpio_direction_output(NET2BIG_GPIO_POWER_OFF, 0) == 0)
		pm_power_off = net2big_power_off;
	else
		pr_err("net2big: failed to configure power-off GPIO\n");

	pr_notice("net2big: Flash writing is not yet supported.\n");
}

/* Warning: LaCie use a wrong mach-type (0x20e=526) in their bootloader. */
MACHINE_START(NET2BIG, "LaCie 2Big Network")
	.boot_params	= 0x00000100,
	.init_machine	= net2big_init,
	.map_io		= orion5x_map_io,
	.init_early	= orion5x_init_early,
	.init_irq	= orion5x_init_irq,
	.timer		= &orion5x_timer,
	.fixup		= tag_fixup_mem32,
MACHINE_END

