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

#include <linux/i2c.h>
#include <linux/i2c/twl.h>

#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>

#include <plat/i2c.h>
#include <plat/mcspi.h>

#include "common-board-devices.h"

static struct i2c_board_info __initdata pmic_i2c_board_info = {
	.addr		= 0x48,
	.flags		= I2C_CLIENT_WAKE,
};

void __init omap_pmic_init(int bus, u32 clkrate,
			   const char *pmic_type, int pmic_irq,
			   struct twl4030_platform_data *pmic_data)
{
	strncpy(pmic_i2c_board_info.type, pmic_type,
		sizeof(pmic_i2c_board_info.type));
	pmic_i2c_board_info.irq = pmic_irq;
	pmic_i2c_board_info.platform_data = pmic_data;

	omap_register_i2c_bus(bus, clkrate, &pmic_i2c_board_info, 1);
}

static struct omap2_mcspi_device_config ads7846_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,	/* 0: slave, 1: master */
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

	err = gpio_request(gpio_pendown, "TS PenDown");
	if (err) {
		pr_err("Could not obtain gpio for TS PenDown: %d\n", err);
		return;
	}

	gpio_direction_input(gpio_pendown);
	gpio_export(gpio_pendown, 0);

	if (gpio_debounce)
		gpio_set_debounce(gpio_pendown, gpio_debounce);

	ads7846_config.gpio_pendown = gpio_pendown;

	spi_bi->bus_num	= bus_num;
	spi_bi->irq	= OMAP_GPIO_IRQ(gpio_pendown);

	if (board_pdata)
		spi_bi->platform_data = board_pdata;

	spi_register_board_info(&ads7846_spi_board_info, 1);
}
