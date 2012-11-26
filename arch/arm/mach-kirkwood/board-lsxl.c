/*
 * Copyright 2012 (C), Michael Walle <michael@walle.cc>
 *
 * arch/arm/mach-kirkwood/board-lsxl.c
 *
 * Buffalo Linkstation LS-XHL and LS-CHLv2 init for drivers not
 * converted to flattened device tree yet.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mv643xx_eth.h>
#include <linux/gpio.h>
#include <linux/gpio-fan.h>
#include "common.h"
#include "mpp.h"

static struct mv643xx_eth_platform_data lsxl_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

static struct mv643xx_eth_platform_data lsxl_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static unsigned int lsxl_mpp_config[] __initdata = {
	MPP10_GPO,	/* HDD Power Enable */
	MPP11_GPIO,	/* USB Vbus Enable */
	MPP18_GPO,	/* FAN High Enable# */
	MPP19_GPO,	/* FAN Low Enable# */
	MPP36_GPIO,	/* Function Blue LED */
	MPP37_GPIO,	/* Alarm LED */
	MPP38_GPIO,	/* Info LED */
	MPP39_GPIO,	/* Power LED */
	MPP40_GPIO,	/* Fan Lock */
	MPP41_GPIO,	/* Function Button */
	MPP42_GPIO,	/* Power Switch */
	MPP43_GPIO,	/* Power Auto Switch */
	MPP48_GPIO,	/* Function Red LED */
	0
};

#define LSXL_GPIO_FAN_HIGH	18
#define LSXL_GPIO_FAN_LOW	19
#define LSXL_GPIO_FAN_LOCK	40

static struct gpio_fan_alarm lsxl_alarm = {
	.gpio = LSXL_GPIO_FAN_LOCK,
};

static struct gpio_fan_speed lsxl_speeds[] = {
	{
		.rpm = 0,
		.ctrl_val = 3,
	}, {
		.rpm = 1500,
		.ctrl_val = 1,
	}, {
		.rpm = 3250,
		.ctrl_val = 2,
	}, {
		.rpm = 5000,
		.ctrl_val = 0,
	}
};

static int lsxl_gpio_list[] = {
	LSXL_GPIO_FAN_HIGH, LSXL_GPIO_FAN_LOW,
};

static struct gpio_fan_platform_data lsxl_fan_data = {
	.num_ctrl = ARRAY_SIZE(lsxl_gpio_list),
	.ctrl = lsxl_gpio_list,
	.alarm = &lsxl_alarm,
	.num_speed = ARRAY_SIZE(lsxl_speeds),
	.speed = lsxl_speeds,
};

static struct platform_device lsxl_fan_device = {
	.name = "gpio-fan",
	.id = -1,
	.num_resources = 0,
	.dev = {
		.platform_data = &lsxl_fan_data,
	},
};

/*
 * On the LS-XHL/LS-CHLv2, the shutdown process is following:
 * - Userland monitors key events until the power switch goes to off position
 * - The board reboots
 * - U-boot starts and goes into an idle mode waiting for the user
 *   to move the switch to ON position
 *
 */
static void lsxl_power_off(void)
{
	kirkwood_restart('h', NULL);
}

#define LSXL_GPIO_HDD_POWER 10
#define LSXL_GPIO_USB_POWER 11

void __init lsxl_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_mpp_conf(lsxl_mpp_config);

	/* usb and sata power on */
	gpio_set_value(LSXL_GPIO_USB_POWER, 1);
	gpio_set_value(LSXL_GPIO_HDD_POWER, 1);

	kirkwood_ehci_init();
	kirkwood_ge00_init(&lsxl_ge00_data);
	kirkwood_ge01_init(&lsxl_ge01_data);
	platform_device_register(&lsxl_fan_device);

	/* register power-off method */
	pm_power_off = lsxl_power_off;
}
