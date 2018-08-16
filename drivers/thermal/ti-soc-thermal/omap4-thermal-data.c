/*
 * OMAP4 thermal driver.
 *
 * Copyright (C) 2011-2012 Texas Instruments Inc.
 * Contact:
 *	Eduardo Valentin <eduardo.valentin@ti.com>
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
 */

#include "ti-thermal.h"
#include "ti-bandgap.h"
#include "omap4xxx-bandgap.h"

/*
 * OMAP4430 has one instance of thermal sensor for MPU
 * need to describe the individual bit fields
 */
static struct temp_sensor_registers
omap4430_mpu_temp_sensor_registers = {
	.temp_sensor_ctrl = OMAP4430_TEMP_SENSOR_CTRL_OFFSET,
	.bgap_tempsoff_mask = OMAP4430_BGAP_TEMPSOFF_MASK,
	.bgap_soc_mask = OMAP4430_BGAP_TEMP_SENSOR_SOC_MASK,
	.bgap_eocz_mask = OMAP4430_BGAP_TEMP_SENSOR_EOCZ_MASK,
	.bgap_dtemp_mask = OMAP4430_BGAP_TEMP_SENSOR_DTEMP_MASK,

	.bgap_mode_ctrl = OMAP4430_TEMP_SENSOR_CTRL_OFFSET,
	.mode_ctrl_mask = OMAP4430_SINGLE_MODE_MASK,

	.bgap_efuse = OMAP4430_FUSE_OPP_BGAP,
};

/* Thresholds and limits for OMAP4430 MPU temperature sensor */
static struct temp_sensor_data omap4430_mpu_temp_sensor_data = {
	.min_freq = OMAP4430_MIN_FREQ,
	.max_freq = OMAP4430_MAX_FREQ,
};

/*
 * Temperature values in milli degree celsius
 * ADC code values from 530 to 923
 */
static const int
omap4430_adc_to_temp[OMAP4430_ADC_END_VALUE - OMAP4430_ADC_START_VALUE + 1] = {
	-38000, -35000, -34000, -32000, -30000, -28000, -26000, -24000, -22000,
	-20000, -18000, -17000, -15000, -13000, -12000, -10000, -8000, -6000,
	-5000, -3000, -1000, 0, 2000, 3000, 5000, 6000, 8000, 10000, 12000,
	13000, 15000, 17000, 19000, 21000, 23000, 25000, 27000, 28000, 30000,
	32000, 33000, 35000, 37000, 38000, 40000, 42000, 43000, 45000, 47000,
	48000, 50000, 52000, 53000, 55000, 57000, 58000, 60000, 62000, 64000,
	66000, 68000, 70000, 71000, 73000, 75000, 77000, 78000, 80000, 82000,
	83000, 85000, 87000, 88000, 90000, 92000, 93000, 95000, 97000, 98000,
	100000, 102000, 103000, 105000, 107000, 109000, 111000, 113000, 115000,
	117000, 118000, 120000, 122000, 123000,
};

/* OMAP4430 data */
const struct ti_bandgap_data omap4430_data = {
	.features = TI_BANDGAP_FEATURE_MODE_CONFIG |
			TI_BANDGAP_FEATURE_CLK_CTRL |
			TI_BANDGAP_FEATURE_POWER_SWITCH,
	.fclock_name = "bandgap_fclk",
	.div_ck_name = "bandgap_fclk",
	.conv_table = omap4430_adc_to_temp,
	.adc_start_val = OMAP4430_ADC_START_VALUE,
	.adc_end_val = OMAP4430_ADC_END_VALUE,
	.expose_sensor = ti_thermal_expose_sensor,
	.remove_sensor = ti_thermal_remove_sensor,
	.sensors = {
		{
		.registers = &omap4430_mpu_temp_sensor_registers,
		.ts_data = &omap4430_mpu_temp_sensor_data,
		.domain = "cpu",
		.slope_pcb = OMAP_GRADIENT_SLOPE_W_PCB_4430,
		.constant_pcb = OMAP_GRADIENT_CONST_W_PCB_4430,
		.register_cooling = ti_thermal_register_cpu_cooling,
		.unregister_cooling = ti_thermal_unregister_cpu_cooling,
		},
	},
	.sensor_count = 1,
};
/*
 * OMAP4460 has one instance of thermal sensor for MPU
 * need to describe the individual bit fields
 */
static struct temp_sensor_registers
omap4460_mpu_temp_sensor_registers = {
	.temp_sensor_ctrl = OMAP4460_TEMP_SENSOR_CTRL_OFFSET,
	.bgap_tempsoff_mask = OMAP4460_BGAP_TEMPSOFF_MASK,
	.bgap_soc_mask = OMAP4460_BGAP_TEMP_SENSOR_SOC_MASK,
	.bgap_eocz_mask = OMAP4460_BGAP_TEMP_SENSOR_EOCZ_MASK,
	.bgap_dtemp_mask = OMAP4460_BGAP_TEMP_SENSOR_DTEMP_MASK,

	.bgap_mask_ctrl = OMAP4460_BGAP_CTRL_OFFSET,
	.mask_hot_mask = OMAP4460_MASK_HOT_MASK,
	.mask_cold_mask = OMAP4460_MASK_COLD_MASK,

	.bgap_mode_ctrl = OMAP4460_BGAP_CTRL_OFFSET,
	.mode_ctrl_mask = OMAP4460_SINGLE_MODE_MASK,

	.bgap_counter = OMAP4460_BGAP_COUNTER_OFFSET,
	.counter_mask = OMAP4460_COUNTER_MASK,

	.bgap_threshold = OMAP4460_BGAP_THRESHOLD_OFFSET,
	.threshold_thot_mask = OMAP4460_T_HOT_MASK,
	.threshold_tcold_mask = OMAP4460_T_COLD_MASK,

	.tshut_threshold = OMAP4460_BGAP_TSHUT_OFFSET,
	.tshut_hot_mask = OMAP4460_TSHUT_HOT_MASK,
	.tshut_cold_mask = OMAP4460_TSHUT_COLD_MASK,

	.bgap_status = OMAP4460_BGAP_STATUS_OFFSET,
	.status_hot_mask = OMAP4460_HOT_FLAG_MASK,
	.status_cold_mask = OMAP4460_COLD_FLAG_MASK,

	.bgap_efuse = OMAP4460_FUSE_OPP_BGAP,
};

/* Thresholds and limits for OMAP4460 MPU temperature sensor */
static struct temp_sensor_data omap4460_mpu_temp_sensor_data = {
	.tshut_hot = OMAP4460_TSHUT_HOT,
	.tshut_cold = OMAP4460_TSHUT_COLD,
	.t_hot = OMAP4460_T_HOT,
	.t_cold = OMAP4460_T_COLD,
	.min_freq = OMAP4460_MIN_FREQ,
	.max_freq = OMAP4460_MAX_FREQ,
};

/*
 * Temperature values in milli degree celsius
 * ADC code values from 530 to 923
 */
static const int
omap4460_adc_to_temp[OMAP4460_ADC_END_VALUE - OMAP4460_ADC_START_VALUE + 1] = {
	-40000, -40000, -40000, -40000, -39800, -39400, -39000, -38600, -38200,
	-37800, -37300, -36800, -36400, -36000, -35600, -35200, -34800,
	-34300, -33800, -33400, -33000, -32600, -32200, -31800, -31300,
	-30800, -30400, -30000, -29600, -29200, -28700, -28200, -27800,
	-27400, -27000, -26600, -26200, -25700, -25200, -24800, -24400,
	-24000, -23600, -23200, -22700, -22200, -21800, -21400, -21000,
	-20600, -20200, -19700, -19200, -18800, -18400, -18000, -17600,
	-17200, -16700, -16200, -15800, -15400, -15000, -14600, -14200,
	-13700, -13200, -12800, -12400, -12000, -11600, -11200, -10700,
	-10200, -9800, -9400, -9000, -8600, -8200, -7700, -7200, -6800,
	-6400, -6000, -5600, -5200, -4800, -4300, -3800, -3400, -3000,
	-2600, -2200, -1800, -1300, -800, -400, 0, 400, 800, 1200, 1600,
	2100, 2600, 3000, 3400, 3800, 4200, 4600, 5100, 5600, 6000, 6400,
	6800, 7200, 7600, 8000, 8500, 9000, 9400, 9800, 10200, 10600, 11000,
	11400, 11900, 12400, 12800, 13200, 13600, 14000, 14400, 14800,
	15300, 15800, 16200, 16600, 17000, 17400, 17800, 18200, 18700,
	19200, 19600, 20000, 20400, 20800, 21200, 21600, 22100, 22600,
	23000, 23400, 23800, 24200, 24600, 25000, 25400, 25900, 26400,
	26800, 27200, 27600, 28000, 28400, 28800, 29300, 29800, 30200,
	30600, 31000, 31400, 31800, 32200, 32600, 33100, 33600, 34000,
	34400, 34800, 35200, 35600, 36000, 36400, 36800, 37300, 37800,
	38200, 38600, 39000, 39400, 39800, 40200, 40600, 41100, 41600,
	42000, 42400, 42800, 43200, 43600, 44000, 44400, 44800, 45300,
	45800, 46200, 46600, 47000, 47400, 47800, 48200, 48600, 49000,
	49500, 50000, 50400, 50800, 51200, 51600, 52000, 52400, 52800,
	53200, 53700, 54200, 54600, 55000, 55400, 55800, 56200, 56600,
	57000, 57400, 57800, 58200, 58700, 59200, 59600, 60000, 60400,
	60800, 61200, 61600, 62000, 62400, 62800, 63300, 63800, 64200,
	64600, 65000, 65400, 65800, 66200, 66600, 67000, 67400, 67800,
	68200, 68700, 69200, 69600, 70000, 70400, 70800, 71200, 71600,
	72000, 72400, 72800, 73200, 73600, 74100, 74600, 75000, 75400,
	75800, 76200, 76600, 77000, 77400, 77800, 78200, 78600, 79000,
	79400, 79800, 80300, 80800, 81200, 81600, 82000, 82400, 82800,
	83200, 83600, 84000, 84400, 84800, 85200, 85600, 86000, 86400,
	86800, 87300, 87800, 88200, 88600, 89000, 89400, 89800, 90200,
	90600, 91000, 91400, 91800, 92200, 92600, 93000, 93400, 93800,
	94200, 94600, 95000, 95500, 96000, 96400, 96800, 97200, 97600,
	98000, 98400, 98800, 99200, 99600, 100000, 100400, 100800, 101200,
	101600, 102000, 102400, 102800, 103200, 103600, 104000, 104400,
	104800, 105200, 105600, 106100, 106600, 107000, 107400, 107800,
	108200, 108600, 109000, 109400, 109800, 110200, 110600, 111000,
	111400, 111800, 112200, 112600, 113000, 113400, 113800, 114200,
	114600, 115000, 115400, 115800, 116200, 116600, 117000, 117400,
	117800, 118200, 118600, 119000, 119400, 119800, 120200, 120600,
	121000, 121400, 121800, 122200, 122600, 123000, 123400, 123800, 124200,
	124600, 124900, 125000, 125000, 125000, 125000
};

/* OMAP4460 data */
const struct ti_bandgap_data omap4460_data = {
	.features = TI_BANDGAP_FEATURE_TSHUT |
			TI_BANDGAP_FEATURE_TSHUT_CONFIG |
			TI_BANDGAP_FEATURE_TALERT |
			TI_BANDGAP_FEATURE_MODE_CONFIG |
			TI_BANDGAP_FEATURE_POWER_SWITCH |
			TI_BANDGAP_FEATURE_CLK_CTRL |
			TI_BANDGAP_FEATURE_COUNTER,
	.fclock_name = "bandgap_ts_fclk",
	.div_ck_name = "div_ts_ck",
	.conv_table = omap4460_adc_to_temp,
	.adc_start_val = OMAP4460_ADC_START_VALUE,
	.adc_end_val = OMAP4460_ADC_END_VALUE,
	.expose_sensor = ti_thermal_expose_sensor,
	.remove_sensor = ti_thermal_remove_sensor,
	.report_temperature = ti_thermal_report_sensor_temperature,
	.sensors = {
		{
		.registers = &omap4460_mpu_temp_sensor_registers,
		.ts_data = &omap4460_mpu_temp_sensor_data,
		.domain = "cpu",
		.slope_pcb = OMAP_GRADIENT_SLOPE_W_PCB_4460,
		.constant_pcb = OMAP_GRADIENT_CONST_W_PCB_4460,
		.register_cooling = ti_thermal_register_cpu_cooling,
		.unregister_cooling = ti_thermal_unregister_cpu_cooling,
		},
	},
	.sensor_count = 1,
};

/* OMAP4470 data */
const struct ti_bandgap_data omap4470_data = {
	.features = TI_BANDGAP_FEATURE_TSHUT |
			TI_BANDGAP_FEATURE_TSHUT_CONFIG |
			TI_BANDGAP_FEATURE_TALERT |
			TI_BANDGAP_FEATURE_MODE_CONFIG |
			TI_BANDGAP_FEATURE_POWER_SWITCH |
			TI_BANDGAP_FEATURE_CLK_CTRL |
			TI_BANDGAP_FEATURE_COUNTER,
	.fclock_name = "bandgap_ts_fclk",
	.div_ck_name = "div_ts_ck",
	.conv_table = omap4460_adc_to_temp,
	.adc_start_val = OMAP4460_ADC_START_VALUE,
	.adc_end_val = OMAP4460_ADC_END_VALUE,
	.expose_sensor = ti_thermal_expose_sensor,
	.remove_sensor = ti_thermal_remove_sensor,
	.report_temperature = ti_thermal_report_sensor_temperature,
	.sensors = {
		{
		.registers = &omap4460_mpu_temp_sensor_registers,
		.ts_data = &omap4460_mpu_temp_sensor_data,
		.domain = "cpu",
		.slope_pcb = OMAP_GRADIENT_SLOPE_W_PCB_4470,
		.constant_pcb = OMAP_GRADIENT_CONST_W_PCB_4470,
		.register_cooling = ti_thermal_register_cpu_cooling,
		.unregister_cooling = ti_thermal_unregister_cpu_cooling,
		},
	},
	.sensor_count = 1,
};
