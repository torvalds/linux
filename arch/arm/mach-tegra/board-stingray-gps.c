/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/gpio.h>
#include <linux/gps-gpio-brcm4750.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include "gpio-names.h"

#define STINGRAY_GPS_RESET	TEGRA_GPIO_PH0
#define STINGRAY_GPS_STANDBY  TEGRA_GPIO_PH1

static void stingray_gps_reset_gpio(unsigned int gpio_val)
{
	pr_info("%s: setting GPS Reset GPIO to %d", __func__, gpio_val);
	gpio_set_value(STINGRAY_GPS_RESET, gpio_val);
}

static void stingray_gps_standby_gpio(unsigned int gpio_val)
{
	pr_info("%s: setting GPS standby GPIO to %d", __func__, gpio_val);
	gpio_set_value(STINGRAY_GPS_STANDBY, gpio_val);
}

static void stingray_gps_gpio_release(void)
{
	gpio_free(STINGRAY_GPS_RESET);
	gpio_free(STINGRAY_GPS_STANDBY);
}

static void stingray_gps_gpio_init(void)
{
	tegra_gpio_enable(STINGRAY_GPS_RESET);
	gpio_request(STINGRAY_GPS_RESET, "gps_rst");
	gpio_direction_output(STINGRAY_GPS_RESET, 0);

	tegra_gpio_enable(STINGRAY_GPS_STANDBY);
	gpio_request(STINGRAY_GPS_STANDBY, "gps_stdby");
	gpio_direction_output(STINGRAY_GPS_STANDBY, 0);
}

struct gps_gpio_brcm4750_platform_data stingray_gps_gpio_data = {
	.set_reset_gpio = stingray_gps_reset_gpio,
	.set_standby_gpio = stingray_gps_standby_gpio,
	.free_gpio = stingray_gps_gpio_release,
};

static struct platform_device stingray_gps_device = {
	.name	= GPS_GPIO_DRIVER_NAME,
	.id		= -1,
	.dev	= {
	.platform_data = &stingray_gps_gpio_data,
	},
};

void __init stingray_gps_init(void)
{
	stingray_gps_gpio_init();
	platform_device_register(&stingray_gps_device);
}
