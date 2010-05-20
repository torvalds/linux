/*
 * Copyright (c) 2010, Motorola, All Rights Reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/bmp085.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <linux/regulator/consumer.h>

#include <mach/gpio.h>

#include "gpio-names.h"

static struct regulator *stingray_bmp085_regulator;

static int stingray_bmp085_init(void)
{
	/*struct regulator *reg;*/

	tegra_gpio_enable(TEGRA_GPIO_PW0);
	gpio_request(TEGRA_GPIO_PW0, "bmp085_irq");
	gpio_direction_input(TEGRA_GPIO_PW0);
/*
	reg = regulator_get(NULL, "vhvio");
	if (IS_ERR(reg))
		return PTR_ERR(reg);*/
	stingray_bmp085_regulator = NULL;/*reg;*/

	return 0;
}

static void stingray_bmp085_exit(void)
{
	if (stingray_bmp085_regulator)
		regulator_put(stingray_bmp085_regulator);
	return;
}
static int stingray_bmp085_power_on(void)
{
	if (stingray_bmp085_regulator)
		return regulator_enable(stingray_bmp085_regulator);
	return 0;
}
static int stingray_bmp085_power_off(void)
{
	if (stingray_bmp085_regulator)
		return regulator_disable(stingray_bmp085_regulator);
	return 0;
}
struct bmp085_platform_data stingray_barom_pdata = {
	.poll_interval = 200,
	.min_interval = 20,

	.init = stingray_bmp085_init,
	.exit = stingray_bmp085_exit,
	.power_on = stingray_bmp085_power_on,
	.power_off = stingray_bmp085_power_off,

};

static struct i2c_board_info __initdata stingray_i2c_bus1_sensor_info[] = {
	{
	 I2C_BOARD_INFO(BMP085_NAME, 0x77),
	 .platform_data = &stingray_barom_pdata,
	 .irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PW0),
	 },
};

int __init stingray_sensors_init(void)
{
	stingray_bmp085_init();
	return i2c_register_board_info(0, stingray_i2c_bus1_sensor_info,
		ARRAY_SIZE(stingray_i2c_bus1_sensor_info));
}
