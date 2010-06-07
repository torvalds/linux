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
#include <linux/l3g4200d.h>
#include <linux/max9635.h>
#include <linux/akm8975.h>
#include <linux/platform_device.h>

#include <linux/regulator/consumer.h>

#include <mach/gpio.h>

#include "gpio-names.h"

#define KXTF9_IRQ_GPIO		TEGRA_GPIO_PV3
#define MAX9635_IRQ_GPIO	TEGRA_GPIO_PV1
#define BMP085_IRQ_GPIO		TEGRA_GPIO_PW0
#define L3G4200D_IRQ_GPIO	TEGRA_GPIO_PH2
#define AKM8975_IRQ_GPIO	TEGRA_GPIO_PQ2

static struct regulator *stingray_bmp085_regulator;
static int stingray_bmp085_init(void)
{
	/*struct regulator *reg;*/

	tegra_gpio_enable(BMP085_IRQ_GPIO);
	gpio_request(BMP085_IRQ_GPIO, "bmp085_irq");
	gpio_direction_input(BMP085_IRQ_GPIO);

/*TO DO add regulator calls in once regulator FW is ready
	reg = regulator_get(NULL, "vhvio");
	if (IS_ERR(reg))
		return PTR_ERR(reg);
	stingray_bmp085_regulator = reg;*/
	stingray_bmp085_regulator = NULL;

	return 0;
}

static void stingray_bmp085_exit(void)
{
	if (stingray_bmp085_regulator)
		regulator_put(stingray_bmp085_regulator);
	gpio_free(BMP085_IRQ_GPIO);
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
	.min_p = 95000,
	.max_p = 125000,
	.fuzz = 5,
	.flat = 5,

	.init = stingray_bmp085_init,
	.exit = stingray_bmp085_exit,
	.power_on = stingray_bmp085_power_on,
	.power_off = stingray_bmp085_power_off,

};
static struct regulator *stingray_kxtf9_regulator;
static int stingray_kxtf9_regulator_init(void)
{
/*TO DO add regulator calls in once regulator FW is ready
	struct regulator *reg;
	reg = regulator_get(NULL, "vhvio");
	if (IS_ERR(reg))
		return PTR_ERR(reg);
	stingray_kxtf9_regulator = reg;
*/
	stingray_kxtf9_regulator = NULL;
	return 0;
}

static void stingray_kxtf9_regulator_exit(void)
{
	if (stingray_kxtf9_regulator)
		regulator_put(stingray_kxtf9_regulator);
}

static int stingray_kxtf9_power_on(void)
{
	if (stingray_kxtf9_regulator)
		return regulator_enable(stingray_kxtf9_regulator);

	return 0;
}

static int stingray_kxtf9_power_off(void)
{

	if (stingray_kxtf9_regulator)
		return regulator_disable(stingray_kxtf9_regulator);

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

static struct regulator *stingray_max9635_regulator;
static int stingray_max9635_power_on(void)
{
	if (stingray_max9635_regulator)
		return regulator_enable(stingray_max9635_regulator);
	return 0;
}
static int stingray_max9635_power_off(void)
{
	if (stingray_max9635_regulator)
		return regulator_disable(stingray_max9635_regulator);
	return 0;
}
struct max9635_als_zone_data stingray_zone_data[] = {
	{ 0x00, 0x57},
	{ 0x58, 0x95},
	{ 0xa3, 0xa4},
	{ 0xa5, 0xab},
	{ 0xac, 0xef},
};

struct max9635_platform_data stingray_max9635_pdata = {
	.configure = 0x4c,
	.threshold_timer = 0x00,
	.def_low_threshold = 0x58,
	.def_high_threshold = 0x95,
	.lens_percent_t = 100,
	.als_lux_table = stingray_zone_data,
	.num_of_zones = ARRAY_SIZE(stingray_zone_data),
	.power_on = stingray_max9635_power_on,
	.power_off = stingray_max9635_power_off
};

static int stingray_max9635_init(void)
{
	tegra_gpio_enable(MAX9635_IRQ_GPIO);
	gpio_request(MAX9635_IRQ_GPIO, "max9635_irq");
	gpio_direction_input(MAX9635_IRQ_GPIO);

/*TO DO add regulator calls in once regulator FW is ready
	struct regulator *reg;
	reg = regulator_get(NULL, "vhvio");
	if (IS_ERR(reg))
		return PTR_ERR(reg);
	stingray_max9635_regulator = reg;
*/
	stingray_max9635_regulator = NULL;
	return 0;
}

static struct regulator *stingray_l3g4200d_regulator;
static int stingray_l3g4200d_init(void)
{
	tegra_gpio_enable(L3G4200D_IRQ_GPIO);
	gpio_request(L3G4200D_IRQ_GPIO, "l3g4200d_irq");
	gpio_direction_input(L3G4200D_IRQ_GPIO);

	/* TO DO: Add regulator init code here as well
	struct regulator *reg;
	reg = regulator_get(NULL, "vhvio");
	if (IS_ERR(reg))
		return PTR_ERR(reg);
	stingray_max9635_regulator = reg;
*/
	stingray_l3g4200d_regulator = NULL;
	return 0;
}

static void stingray_l3g4200d_exit(void)
{
	if (stingray_l3g4200d_regulator)
		regulator_put(stingray_l3g4200d_regulator);

	gpio_free(L3G4200D_IRQ_GPIO);
}
static int stingray_l3g4200d_power_on(void)
{
	if (stingray_l3g4200d_regulator)
		return regulator_enable(stingray_l3g4200d_regulator);
	return 0;
}
static int stingray_l3g4200d_power_off(void)
{
	if (stingray_l3g4200d_regulator)
		return regulator_disable(stingray_l3g4200d_regulator);
	return 0;
}
struct l3g4200d_platform_data stingray_gyro_pdata = {
	.poll_interval = 200,
	.min_interval = 0,

	.g_range = 0,

	.ctrl_reg_1 = 0xbf,
	.ctrl_reg_2 = 0x00,
	.ctrl_reg_3 = 0x00,
	.ctrl_reg_4 = 0x00,
	.ctrl_reg_5 = 0x00,
	.int_config = 0x00,
	.int_source = 0x00,
	.int_th_x_h = 0x00,
	.int_th_x_l = 0x00,
	.int_th_y_h = 0x00,
	.int_th_y_l = 0x00,
	.int_th_z_h = 0x00,
	.int_th_z_l = 0x00,
	.int_duration = 0x00,

	.axis_map_x = 0,
	.axis_map_y = 0,
	.axis_map_z = 0,

	.negate_x = 0,
	.negate_y = 0,
	.negate_z = 0,

	.exit = stingray_l3g4200d_exit,
	.power_on = stingray_l3g4200d_power_on,
	.power_off = stingray_l3g4200d_power_off,

};

static struct regulator *stingray_akm8975_regulator;

static int stingray_akm8975_init(void)
{
	/*struct regulator *reg;*/

	tegra_gpio_enable(AKM8975_IRQ_GPIO);
	gpio_request(AKM8975_IRQ_GPIO, "akm8975");
	gpio_direction_input(AKM8975_IRQ_GPIO);

/*TO DO: add regulator calls later
	reg = regulator_get(NULL, "vhvio");
	if (IS_ERR(reg))
		return PTR_ERR(reg);
	stingray_akm8975_regulator = reg;*/
	stingray_akm8975_regulator = NULL;

	return 0;
}

static void stingray_akm8975_exit(void)
{
	if (stingray_akm8975_regulator)
		regulator_put(stingray_akm8975_regulator);
	gpio_free(AKM8975_IRQ_GPIO);
	return;
}

static int stingray_akm8975_power_on(void)
{
	if (stingray_akm8975_regulator)
		regulator_put(stingray_akm8975_regulator);
	return 0;
}

static int stingray_akm8975_power_off(void)
{
	if (stingray_akm8975_regulator)
		regulator_put(stingray_akm8975_regulator);
	return 0;
}

struct akm8975_platform_data stingray_akm8975_pdata = {
	.init = stingray_akm8975_init,
	.exit = stingray_akm8975_exit,
	.power_on = stingray_akm8975_power_on,
	.power_off = stingray_akm8975_power_off,
};

static struct i2c_board_info __initdata stingray_i2c_bus4_sensor_info[] = {
	{
		I2C_BOARD_INFO("akm8975", 0x0C),
		.platform_data = &stingray_akm8975_pdata,
		.irq = TEGRA_GPIO_TO_IRQ(AKM8975_IRQ_GPIO),
	},
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
		.irq = TEGRA_GPIO_TO_IRQ(BMP085_IRQ_GPIO),
	 },
	{
		 I2C_BOARD_INFO(MAX9635_NAME, 0x4b),
		.platform_data = &stingray_max9635_pdata,
		.irq = TEGRA_GPIO_TO_IRQ(MAX9635_IRQ_GPIO),
	 },
};
static struct i2c_board_info __initdata stingray_i2c_bus3_sensor_info[] = {
	 {
		I2C_BOARD_INFO(L3G4200D_NAME, 0x68),
		.platform_data = &stingray_gyro_pdata,
		.irq = TEGRA_GPIO_TO_IRQ(L3G4200D_IRQ_GPIO),
	 },
};

int __init stingray_sensors_init(void)
{
	stingray_bmp085_init();
	stingray_kxtf9_init();
	stingray_max9635_init();
	stingray_l3g4200d_init();
	stingray_akm8975_init();

	i2c_register_board_info(3, stingray_i2c_bus4_sensor_info,
		ARRAY_SIZE(stingray_i2c_bus4_sensor_info));
	i2c_register_board_info(2, stingray_i2c_bus3_sensor_info,
		ARRAY_SIZE(stingray_i2c_bus3_sensor_info));
	return i2c_register_board_info(0, stingray_i2c_bus1_sensor_info,
		ARRAY_SIZE(stingray_i2c_bus1_sensor_info));
}
