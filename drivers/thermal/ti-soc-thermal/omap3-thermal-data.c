/*
 * OMAP3 thermal driver.
 *
 * Copyright (C) 2011-2012 Texas Instruments Inc.
 * Copyright (C) 2014 Pavel Machek <pavel@ucw.cz>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * Note
 * http://www.ti.com/lit/er/sprz278f/sprz278f.pdf "Advisory
 * 3.1.1.186 MMC OCP Clock Not Gated When Thermal Sensor Is Used"
 *
 * Also TI says:
 * Just be careful when you try to make thermal policy like decisions
 * based on this sensor. Placement of the sensor w.r.t the actual logic
 * generating heat has to be a factor as well. If you are just looking
 * for an approximation temperature (thermometerish kind), you might be
 * ok with this. I am not sure we'd find any TI data around this.. just a
 * heads up.
 */

#include "ti-thermal.h"
#include "ti-bandgap.h"

/*
 * OMAP34XX has one instance of thermal sensor for MPU
 * need to describe the individual bit fields
 */
static struct temp_sensor_registers
omap34xx_mpu_temp_sensor_registers = {
	.temp_sensor_ctrl = 0,
	.bgap_soc_mask = BIT(8),
	.bgap_eocz_mask = BIT(7),
	.bgap_dtemp_mask = 0x7f,

	.bgap_mode_ctrl = 0,
	.mode_ctrl_mask = BIT(9),
};

/* Thresholds and limits for OMAP34XX MPU temperature sensor */
static struct temp_sensor_data omap34xx_mpu_temp_sensor_data = {
	.min_freq = 32768,
	.max_freq = 32768,
	.max_temp = 125000,
	.min_temp = -40000,
	.hyst_val = 5000,
};

/*
 * Temperature values in milli degree celsius
 */
static const int
omap34xx_adc_to_temp[128] = {
	-40000, -40000, -40000, -40000, -40000, -39000, -38000, -36000,
	-34000, -32000, -31000,	-29000, -28000, -26000, -25000, -24000,
	-22000, -21000, -19000, -18000, -17000, -15000,	-14000, -12000,
	-11000, -9000, -8000, -7000, -5000, -4000, -2000, -1000, 0000,
	1000, 3000, 4000, 5000, 7000, 8000, 10000, 11000, 13000, 14000,
	15000, 17000, 18000, 20000, 21000, 22000, 24000, 25000, 27000,
	28000, 30000, 31000, 32000, 34000, 35000, 37000, 38000, 39000,
	41000, 42000, 44000, 45000, 47000, 48000, 49000, 51000, 52000,
	53000, 55000, 56000, 58000, 59000, 60000, 62000, 63000, 65000,
	66000, 67000, 69000, 70000, 72000, 73000, 74000, 76000, 77000,
	79000, 80000, 81000, 83000, 84000, 85000, 87000, 88000, 89000,
	91000, 92000, 94000, 95000, 96000, 98000, 99000, 100000,
	102000, 103000, 105000, 106000, 107000, 109000, 110000, 111000,
	113000, 114000, 116000, 117000, 118000, 120000, 121000, 122000,
	124000, 124000, 125000, 125000, 125000, 125000,	125000
};

/* OMAP34XX data */
const struct ti_bandgap_data omap34xx_data = {
	.features = TI_BANDGAP_FEATURE_CLK_CTRL | TI_BANDGAP_FEATURE_UNRELIABLE,
	.fclock_name = "ts_fck",
	.div_ck_name = "ts_fck",
	.conv_table = omap34xx_adc_to_temp,
	.adc_start_val = 0,
	.adc_end_val = 127,
	.expose_sensor = ti_thermal_expose_sensor,
	.remove_sensor = ti_thermal_remove_sensor,

	.sensors = {
		{
		.registers = &omap34xx_mpu_temp_sensor_registers,
		.ts_data = &omap34xx_mpu_temp_sensor_data,
		.domain = "cpu",
		.slope_pcb = 0,
		.constant_pcb = 20000,
		.register_cooling = NULL,
		.unregister_cooling = NULL,
		},
	},
	.sensor_count = 1,
};

/*
 * OMAP36XX has one instance of thermal sensor for MPU
 * need to describe the individual bit fields
 */
static struct temp_sensor_registers
omap36xx_mpu_temp_sensor_registers = {
	.temp_sensor_ctrl = 0,
	.bgap_soc_mask = BIT(9),
	.bgap_eocz_mask = BIT(8),
	.bgap_dtemp_mask = 0xFF,

	.bgap_mode_ctrl = 0,
	.mode_ctrl_mask = BIT(10),
};

/* Thresholds and limits for OMAP36XX MPU temperature sensor */
static struct temp_sensor_data omap36xx_mpu_temp_sensor_data = {
	.min_freq = 32768,
	.max_freq = 32768,
	.max_temp = 125000,
	.min_temp = -40000,
	.hyst_val = 5000,
};

/*
 * Temperature values in milli degree celsius
 */
static const int
omap36xx_adc_to_temp[128] = {
	-40000, -40000, -40000, -40000, -40000, -40000, -40000, -40000,
	-40000, -40000, -40000,	-40000, -40000, -38000, -35000, -34000,
	-32000, -30000, -28000, -26000, -24000, -22000,	-20000, -18500,
	-17000, -15000, -13500, -12000, -10000, -8000, -6500, -5000, -3500,
	-1500, 0, 2000, 3500, 5000, 6500, 8500, 10000, 12000, 13500,
	15000, 17000, 19000, 21000, 23000, 25000, 27000, 28500, 30000,
	32000, 33500, 35000, 37000, 38500, 40000, 42000, 43500, 45000,
	47000, 48500, 50000, 52000, 53500, 55000, 57000, 58500, 60000,
	62000, 64000, 66000, 68000, 70000, 71500, 73500, 75000, 77000,
	78500, 80000, 82000, 83500, 85000, 87000, 88500, 90000, 92000,
	93500, 95000, 97000, 98500, 100000, 102000, 103500, 105000, 107000,
	109000, 111000, 113000, 115000, 117000, 118500, 120000, 122000,
	123500, 125000, 125000, 125000, 125000, 125000, 125000, 125000,
	125000, 125000, 125000, 125000, 125000, 125000, 125000, 125000,
	125000, 125000, 125000, 125000, 125000, 125000,	125000
};

/* OMAP36XX data */
const struct ti_bandgap_data omap36xx_data = {
	.features = TI_BANDGAP_FEATURE_CLK_CTRL | TI_BANDGAP_FEATURE_UNRELIABLE,
	.fclock_name = "ts_fck",
	.div_ck_name = "ts_fck",
	.conv_table = omap36xx_adc_to_temp,
	.adc_start_val = 0,
	.adc_end_val = 127,
	.expose_sensor = ti_thermal_expose_sensor,
	.remove_sensor = ti_thermal_remove_sensor,

	.sensors = {
		{
		.registers = &omap36xx_mpu_temp_sensor_registers,
		.ts_data = &omap36xx_mpu_temp_sensor_data,
		.domain = "cpu",
		.slope_pcb = 0,
		.constant_pcb = 20000,
		.register_cooling = NULL,
		.unregister_cooling = NULL,
		},
	},
	.sensor_count = 1,
};
