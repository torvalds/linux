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
#include <linux/kxtf9.h>
#include <linux/platform_device.h>

#include <linux/regulator/consumer.h>

#include <mach/gpio.h>

#include "gpio-names.h"

static struct regulator *stingray_bmp085_regulator;

#define KXTF9_IRQ_GPIO	TEGRA_GPIO_PV3

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
static struct regulator *stingray_kxtf9_regulator;
static int stingray_kxtf9_regulator_init(void)
{
/* TO DO: Update regulator functions
	struct regulator *reg;
	reg = regulator_get(NULL, "vhvio");
	if (IS_ERR(reg))
		return PTR_ERR(reg);
	stingray_kxtf9_regulator = reg;
*/
	return 0;
}

static void stingray_kxtf9_regulator_exit(void)
{
	/*regulator_put(stingray_kxtf9_regulator);*/
}

static int stingray_kxtf9_power_on(void)
{
	/*return regulator_enable(stingray_kxtf9_regulator);*/
	return 0;
}

static int stingray_kxtf9_power_off(void)
{
/*
	if (stingray_kxtf9_regulator)
		return regulator_disable(stingray_kxtf9_regulator);
*/
	return 0;
}

static int stingray_kxtf9_gpio_level(void)
{
	/* TO DO: Fill in with GPIO level check functions */
	return 0;
}


struct kxtf9_platform_data stingray_kxtf9_pdata = {
	.init = stingray_kxtf9_regulator_init,
	.exit = stingray_kxtf9_regulator_exit,
	.power_on = stingray_kxtf9_power_on,
	.power_off = stingray_kxtf9_power_off,

	.min_interval	= 2,
	.poll_interval	= 200,

	.g_range	= KXTF9_G_8G,

	.axis_map_x	= 1,
	.axis_map_y	= 0,
	.axis_map_z	= 2,

	.negate_x	= 0,
	.negate_y	= 0,
	.negate_z	= 1,

	.data_odr_init		= ODR12_5,
	.ctrl_reg1_init		= RES_12BIT | KXTF9_G_2G | TPE | WUFE | TDTE,
	.int_ctrl_init		= IEA | IEN,
	.tilt_timer_init	= 0x03,
	.engine_odr_init	= OTP12_5 | OWUF50 | OTDT400,
	.wuf_timer_init		= 0x0A,
	.wuf_thresh_init	= 0x20,
	.tdt_timer_init		= 0x78,
	.tdt_h_thresh_init	= 0xB6,
	.tdt_l_thresh_init	= 0x1A,
	.tdt_tap_timer_init	= 0xA2,
	.tdt_total_timer_init	= 0x24,
	.tdt_latency_timer_init	= 0x28,
	.tdt_window_timer_init	= 0xA0,

	.gpio = stingray_kxtf9_gpio_level,
	.gesture = 0,
	.sensitivity_low = {
		  0x50, 0xFF, 0xB8, 0x32, 0x09, 0x0A, 0xA0,
	},
	.sensitivity_medium = {
		  0x50, 0xFF, 0x68, 0x32, 0x09, 0x0A, 0xA0,
	},
	.sensitivity_high = {
		  0x78, 0xB6, 0x1A, 0xA2, 0x24, 0x28, 0xA0,
	},
};
static void stingray_kxtf9_init(void)
{
	tegra_gpio_enable(KXTF9_IRQ_GPIO);
	gpio_request(KXTF9_IRQ_GPIO, "kxtf9_irq");
	gpio_direction_input(KXTF9_IRQ_GPIO);
}
static struct i2c_board_info __initdata stingray_i2c_bus4_sensor_info[] = {
	{
		I2C_BOARD_INFO("kxtf9", 0x0F),
		.platform_data = &stingray_kxtf9_pdata,
		.irq = TEGRA_GPIO_TO_IRQ(KXTF9_IRQ_GPIO),
	},
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
	stingray_kxtf9_init();
	i2c_register_board_info(3, stingray_i2c_bus4_sensor_info,
		ARRAY_SIZE(stingray_i2c_bus4_sensor_info));
	return i2c_register_board_info(0, stingray_i2c_bus1_sensor_info,
		ARRAY_SIZE(stingray_i2c_bus1_sensor_info));
}
