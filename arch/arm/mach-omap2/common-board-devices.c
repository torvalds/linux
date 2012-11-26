/*
 * common-board-devices.c
 *
 * Copyright (C) 2011 CompuLab, Ltd.
 * Author: Mike Rapoport <mike@compulab.co.il>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>

#include <linux/platform_data/spi-omap2-mcspi.h>

#include "common.h"
#include "common-board-devices.h"

#if defined(CONFIG_TOUCHSCREEN_ADS7846) || \
	defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
static struct omap2_mcspi_device_config ads7846_mcspi_config = {
	.turbo_mode	= 0,
};

static struct ads7846_platform_data ads7846_config = {
	.x_max			= 0x0fff,
	.y_max			= 0x0fff,
	.x_plate_ohms		= 180,
	.pressure_max		= 255,
	.debounce_max		= 10,
	.debounce_tol		= 3,
	.debounce_rep		= 1,
	.gpio_pendown		= -EINVAL,
	.keep_vref_on		= 1,
};

static struct spi_board_info ads7846_spi_board_info __initdata = {
	.modalias		= "ads7846",
	.bus_num		= -EINVAL,
	.chip_select		= 0,
	.max_speed_hz		= 1500000,
	.controller_data	= &ads7846_mcspi_config,
	.irq			= -EINVAL,
	.platform_data		= &ads7846_config,
};

void __init omap_ads7846_init(int bus_num, int gpio_pendown, int gpio_debounce,
			      struct ads7846_platform_data *board_pdata)
{
	struct spi_board_info *spi_bi = &ads7846_spi_board_info;
	int err;

	/*
	 * If a board defines get_pendown_state() function, request the pendown
	 * GPIO and set the GPIO debounce time.
	 * If a board does not define the get_pendown_state() function, then
	 * the ads7846 driver will setup the pendown GPIO itself.
	 */
	if (board_pdata && board_pdata->get_pendown_state) {
		err = gpio_request_one(gpio_pendown, GPIOF_IN, "TSPenDown");
		if (err) {
			pr_err("Couldn't obtain gpio for TSPenDown: %d\n", err);
			return;
		}

		if (gpio_debounce)
			gpio_set_debounce(gpio_pendown, gpio_debounce);

		gpio_export(gpio_pendown, 0);
	}

	spi_bi->bus_num	= bus_num;
	spi_bi->irq	= gpio_to_irq(gpio_pendown);

	ads7846_config.gpio_pendown = gpio_pendown;

	if (board_pdata) {
		board_pdata->gpio_pendown = gpio_pendown;
		board_pdata->gpio_pendown_debounce = gpio_debounce;
		spi_bi->platform_data = board_pdata;
	}

	spi_register_board_info(&ads7846_spi_board_info, 1);
}
#else
void __init omap_ads7846_init(int bus_num, int gpio_pendown, int gpio_debounce,
			      struct ads7846_platform_data *board_pdata)
{
}
#endif
