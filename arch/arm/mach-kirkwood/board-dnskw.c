/*
 * Copyright 2012 (C), Jamie Lentin <jm@lentin.co.uk>
 *
 * arch/arm/mach-kirkwood/board-dnskw.c
 *
 * D-link DNS-320 & DNS-325 NAS Init for drivers not converted to
 * flattened device tree yet.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/gpio-fan.h>
#include <linux/leds.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/kirkwood.h>
#include <mach/bridge-regs.h>
#include "common.h"
#include "mpp.h"

static struct mv643xx_eth_platform_data dnskw_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static struct mv_sata_platform_data dnskw_sata_data = {
	.n_ports	= 2,
};

static unsigned int dnskw_mpp_config[] __initdata = {
	MPP13_UART1_TXD,	/* Custom ... */
	MPP14_UART1_RXD,	/* ... Controller (DNS-320 only) */
	MPP20_SATA1_ACTn,	/* LED: White Right HDD */
	MPP21_SATA0_ACTn,	/* LED: White Left HDD */
	MPP24_GPIO,
	MPP25_GPIO,
	MPP26_GPIO,	/* LED: Power */
	MPP27_GPIO,	/* LED: Red Right HDD */
	MPP28_GPIO,	/* LED: Red Left HDD */
	MPP29_GPIO,	/* LED: Red USB (DNS-325 only) */
	MPP30_GPIO,
	MPP31_GPIO,
	MPP32_GPIO,
	MPP33_GPO,
	MPP34_GPIO,	/* Button: Front power */
	MPP35_GPIO,	/* LED: Red USB (DNS-320 only) */
	MPP36_GPIO,	/* Power: Turn off board */
	MPP37_GPIO,	/* Power: Turn back on after power failure */
	MPP38_GPIO,
	MPP39_GPIO,	/* Power: SATA0 */
	MPP40_GPIO,	/* Power: SATA1 */
	MPP41_GPIO,	/* SATA0 present */
	MPP42_GPIO,	/* SATA1 present */
	MPP43_GPIO,	/* LED: White USB */
	MPP44_GPIO,	/* Fan: Tachometer Pin */
	MPP45_GPIO,	/* Fan: high speed */
	MPP46_GPIO,	/* Fan: low speed */
	MPP47_GPIO,	/* Button: Back unmount */
	MPP48_GPIO,	/* Button: Back reset */
	MPP49_GPIO,	/* Temp Alarm (DNS-325) Pin of U5 (DNS-320) */
	0
};

static struct gpio_led dns325_led_pins[] = {
	{
		.name	= "dns325:white:power",
		.gpio	= 26,
		.active_low = 1,
		.default_trigger = "default-on",
	},
	{
		.name	= "dns325:white:usb",
		.gpio	= 43,
		.active_low = 1,
	},
	{
		.name	= "dns325:red:l_hdd",
		.gpio	= 28,
		.active_low = 1,
	},
	{
		.name	= "dns325:red:r_hdd",
		.gpio	= 27,
		.active_low = 1,
	},
	{
		.name	= "dns325:red:usb",
		.gpio	= 29,
		.active_low = 1,
	},
};

static struct gpio_led_platform_data dns325_led_data = {
	.num_leds	= ARRAY_SIZE(dns325_led_pins),
	.leds		= dns325_led_pins,
};

static struct platform_device dns325_led_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &dns325_led_data,
	},
};

static struct gpio_led dns320_led_pins[] = {
	{
		.name	= "dns320:blue:power",
		.gpio	= 26,
		.active_low = 1,
		.default_trigger = "default-on",
	},
	{
		.name	= "dns320:blue:usb",
		.gpio	= 43,
		.active_low = 1,
	},
	{
		.name	= "dns320:orange:l_hdd",
		.gpio	= 28,
		.active_low = 1,
	},
	{
		.name	= "dns320:orange:r_hdd",
		.gpio	= 27,
		.active_low = 1,
	},
	{
		.name	= "dns320:orange:usb",
		.gpio	= 35,
		.active_low = 1,
	},
};

static struct gpio_led_platform_data dns320_led_data = {
	.num_leds	= ARRAY_SIZE(dns320_led_pins),
	.leds		= dns320_led_pins,
};

static struct platform_device dns320_led_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &dns320_led_data,
	},
};

static struct i2c_board_info dns325_i2c_board_info[] __initdata = {
	{
		I2C_BOARD_INFO("lm75", 0x48),
	},
	/* Something at 0x0c also */
};

static struct gpio_keys_button dnskw_button_pins[] = {
	{
		.code		= KEY_POWER,
		.gpio		= 34,
		.desc		= "Power button",
		.active_low	= 1,
	},
	{
		.code		= KEY_EJECTCD,
		.gpio		= 47,
		.desc		= "USB unmount button",
		.active_low	= 1,
	},
	{
		.code		= KEY_RESTART,
		.gpio		= 48,
		.desc		= "Reset button",
		.active_low	= 1,
	},
};

static struct gpio_keys_platform_data dnskw_button_data = {
	.buttons	= dnskw_button_pins,
	.nbuttons	= ARRAY_SIZE(dnskw_button_pins),
};

static struct platform_device dnskw_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &dnskw_button_data,
	}
};

/* Fan: ADDA AD045HB-G73 40mm 6000rpm@5v */
static struct gpio_fan_speed dnskw_fan_speed[] = {
	{    0,  0 },
	{ 3000,	 1 },
	{ 6000,	 2 },
};
static unsigned dnskw_fan_pins[] = {46, 45};

static struct gpio_fan_platform_data dnskw_fan_data = {
	.num_ctrl	= ARRAY_SIZE(dnskw_fan_pins),
	.ctrl		= dnskw_fan_pins,
	.num_speed	= ARRAY_SIZE(dnskw_fan_speed),
	.speed		= dnskw_fan_speed,
};

static struct platform_device dnskw_fan_device = {
	.name	= "gpio-fan",
	.id	= -1,
	.dev	= {
		.platform_data	= &dnskw_fan_data,
	},
};

static void dnskw_power_off(void)
{
	gpio_set_value(36, 1);
}

/* Register any GPIO for output and set the value */
static void __init dnskw_gpio_register(unsigned gpio, char *name, int def)
{
	if (gpio_request(gpio, name) == 0 &&
	    gpio_direction_output(gpio, 0) == 0) {
		gpio_set_value(gpio, def);
		if (gpio_export(gpio, 0) != 0)
			pr_err("dnskw: Failed to export GPIO %s\n", name);
	} else
		pr_err("dnskw: Failed to register %s\n", name);
}

void __init dnskw_init(void)
{
	kirkwood_mpp_conf(dnskw_mpp_config);

	kirkwood_ehci_init();
	kirkwood_ge00_init(&dnskw_ge00_data);
	kirkwood_sata_init(&dnskw_sata_data);
	kirkwood_i2c_init();

	platform_device_register(&dnskw_button_device);
	platform_device_register(&dnskw_fan_device);

	if (of_machine_is_compatible("dlink,dns-325")) {
		i2c_register_board_info(0, dns325_i2c_board_info,
					ARRAY_SIZE(dns325_i2c_board_info));
		platform_device_register(&dns325_led_device);

	} else if (of_machine_is_compatible("dlink,dns-320"))
		platform_device_register(&dns320_led_device);

	/* Register power-off GPIO. */
	if (gpio_request(36, "dnskw:power:off") == 0
	    && gpio_direction_output(36, 0) == 0)
		pm_power_off = dnskw_power_off;
	else
		pr_err("dnskw: failed to configure power-off GPIO\n");

	/* Ensure power is supplied to both HDDs */
	dnskw_gpio_register(39, "dnskw:power:sata0", 1);
	dnskw_gpio_register(40, "dnskw:power:sata1", 1);

	/* Set NAS to turn back on after a power failure */
	dnskw_gpio_register(37, "dnskw:power:recover", 1);
}
