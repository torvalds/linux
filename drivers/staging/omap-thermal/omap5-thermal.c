/*
 * OMAP5 thermal driver.
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

#include "omap-bandgap.h"
#include "omap-thermal.h"

/*
 * omap5430 has one instance of thermal sensor for MPU
 * need to describe the individual bit fields
 */
static struct temp_sensor_registers
omap5430_mpu_temp_sensor_registers = {
	.temp_sensor_ctrl = OMAP5430_TEMP_SENSOR_MPU_OFFSET,
	.bgap_tempsoff_mask = OMAP5430_BGAP_TEMPSOFF_MASK,
	.bgap_soc_mask = OMAP5430_BGAP_TEMP_SENSOR_SOC_MASK,
	.bgap_eocz_mask = OMAP5430_BGAP_TEMP_SENSOR_EOCZ_MASK,
	.bgap_dtemp_mask = OMAP5430_BGAP_TEMP_SENSOR_DTEMP_MASK,

	.bgap_mask_ctrl = OMAP5430_BGAP_CTRL_OFFSET,
	.mask_hot_mask = OMAP5430_MASK_HOT_MPU_MASK,
	.mask_cold_mask = OMAP5430_MASK_COLD_MPU_MASK,

	.bgap_mode_ctrl = OMAP5430_BGAP_COUNTER_MPU_OFFSET,
	.mode_ctrl_mask = OMAP5430_REPEAT_MODE_MASK,

	.bgap_counter = OMAP5430_BGAP_COUNTER_MPU_OFFSET,
	.counter_mask = OMAP5430_COUNTER_MASK,

	.bgap_threshold = OMAP5430_BGAP_THRESHOLD_MPU_OFFSET,
	.threshold_thot_mask = OMAP5430_T_HOT_MASK,
	.threshold_tcold_mask = OMAP5430_T_COLD_MASK,

	.tshut_threshold = OMAP5430_BGAP_TSHUT_MPU_OFFSET,
	.tshut_hot_mask = OMAP5430_TSHUT_HOT_MASK,
	.tshut_cold_mask = OMAP5430_TSHUT_COLD_MASK,

	.bgap_status = OMAP5430_BGAP_STATUS_OFFSET,
	.status_clean_stop_mask = 0x0,
	.status_bgap_alert_mask = OMAP5430_BGAP_ALERT_MASK,
	.status_hot_mask = OMAP5430_HOT_MPU_FLAG_MASK,
	.status_cold_mask = OMAP5430_COLD_MPU_FLAG_MASK,

	.bgap_efuse = OMAP5430_FUSE_OPP_BGAP_MPU,
};

/*
 * omap5430 has one instance of thermal sensor for GPU
 * need to describe the individual bit fields
 */
static struct temp_sensor_registers
omap5430_gpu_temp_sensor_registers = {
	.temp_sensor_ctrl = OMAP5430_TEMP_SENSOR_GPU_OFFSET,
	.bgap_tempsoff_mask = OMAP5430_BGAP_TEMPSOFF_MASK,
	.bgap_soc_mask = OMAP5430_BGAP_TEMP_SENSOR_SOC_MASK,
	.bgap_eocz_mask = OMAP5430_BGAP_TEMP_SENSOR_EOCZ_MASK,
	.bgap_dtemp_mask = OMAP5430_BGAP_TEMP_SENSOR_DTEMP_MASK,

	.bgap_mask_ctrl = OMAP5430_BGAP_CTRL_OFFSET,
	.mask_hot_mask = OMAP5430_MASK_HOT_MM_MASK,
	.mask_cold_mask = OMAP5430_MASK_COLD_MM_MASK,

	.bgap_mode_ctrl = OMAP5430_BGAP_COUNTER_GPU_OFFSET,
	.mode_ctrl_mask = OMAP5430_REPEAT_MODE_MASK,

	.bgap_counter = OMAP5430_BGAP_COUNTER_GPU_OFFSET,
	.counter_mask = OMAP5430_COUNTER_MASK,

	.bgap_threshold = OMAP5430_BGAP_THRESHOLD_GPU_OFFSET,
	.threshold_thot_mask = OMAP5430_T_HOT_MASK,
	.threshold_tcold_mask = OMAP5430_T_COLD_MASK,

	.tshut_threshold = OMAP5430_BGAP_TSHUT_GPU_OFFSET,
	.tshut_hot_mask = OMAP5430_TSHUT_HOT_MASK,
	.tshut_cold_mask = OMAP5430_TSHUT_COLD_MASK,

	.bgap_status = OMAP5430_BGAP_STATUS_OFFSET,
	.status_clean_stop_mask = 0x0,
	.status_bgap_alert_mask = OMAP5430_BGAP_ALERT_MASK,
	.status_hot_mask = OMAP5430_HOT_MM_FLAG_MASK,
	.status_cold_mask = OMAP5430_COLD_MM_FLAG_MASK,

	.bgap_efuse = OMAP5430_FUSE_OPP_BGAP_GPU,
};

/*
 * omap5430 has one instance of thermal sensor for CORE
 * need to describe the individual bit fields
 */
static struct temp_sensor_registers
omap5430_core_temp_sensor_registers = {
	.temp_sensor_ctrl = OMAP5430_TEMP_SENSOR_CORE_OFFSET,
	.bgap_tempsoff_mask = OMAP5430_BGAP_TEMPSOFF_MASK,
	.bgap_soc_mask = OMAP5430_BGAP_TEMP_SENSOR_SOC_MASK,
	.bgap_eocz_mask = OMAP5430_BGAP_TEMP_SENSOR_EOCZ_MASK,
	.bgap_dtemp_mask = OMAP5430_BGAP_TEMP_SENSOR_DTEMP_MASK,

	.bgap_mask_ctrl = OMAP5430_BGAP_CTRL_OFFSET,
	.mask_hot_mask = OMAP5430_MASK_HOT_CORE_MASK,
	.mask_cold_mask = OMAP5430_MASK_COLD_CORE_MASK,

	.bgap_mode_ctrl = OMAP5430_BGAP_COUNTER_CORE_OFFSET,
	.mode_ctrl_mask = OMAP5430_REPEAT_MODE_MASK,

	.bgap_counter = OMAP5430_BGAP_COUNTER_CORE_OFFSET,
	.counter_mask = OMAP5430_COUNTER_MASK,

	.bgap_threshold = OMAP5430_BGAP_THRESHOLD_CORE_OFFSET,
	.threshold_thot_mask = OMAP5430_T_HOT_MASK,
	.threshold_tcold_mask = OMAP5430_T_COLD_MASK,

	.tshut_threshold = OMAP5430_BGAP_TSHUT_CORE_OFFSET,
	.tshut_hot_mask = OMAP5430_TSHUT_HOT_MASK,
	.tshut_cold_mask = OMAP5430_TSHUT_COLD_MASK,

	.bgap_status = OMAP5430_BGAP_STATUS_OFFSET,
	.status_clean_stop_mask = 0x0,
	.status_bgap_alert_mask = OMAP5430_BGAP_ALERT_MASK,
	.status_hot_mask = OMAP5430_HOT_CORE_FLAG_MASK,
	.status_cold_mask = OMAP5430_COLD_CORE_FLAG_MASK,

	.bgap_efuse = OMAP5430_FUSE_OPP_BGAP_CORE,
};

/* Thresholds and limits for OMAP5430 MPU temperature sensor */
static struct temp_sensor_data omap5430_mpu_temp_sensor_data = {
	.tshut_hot = OMAP5430_MPU_TSHUT_HOT,
	.tshut_cold = OMAP5430_MPU_TSHUT_COLD,
	.t_hot = OMAP5430_MPU_T_HOT,
	.t_cold = OMAP5430_MPU_T_COLD,
	.min_freq = OMAP5430_MPU_MIN_FREQ,
	.max_freq = OMAP5430_MPU_MAX_FREQ,
	.max_temp = OMAP5430_MPU_MAX_TEMP,
	.min_temp = OMAP5430_MPU_MIN_TEMP,
	.hyst_val = OMAP5430_MPU_HYST_VAL,
	.adc_start_val = OMAP5430_ADC_START_VALUE,
	.adc_end_val = OMAP5430_ADC_END_VALUE,
	.update_int1 = 1000,
	.update_int2 = 2000,
};

/* Thresholds and limits for OMAP5430 GPU temperature sensor */
static struct temp_sensor_data omap5430_gpu_temp_sensor_data = {
	.tshut_hot = OMAP5430_GPU_TSHUT_HOT,
	.tshut_cold = OMAP5430_GPU_TSHUT_COLD,
	.t_hot = OMAP5430_GPU_T_HOT,
	.t_cold = OMAP5430_GPU_T_COLD,
	.min_freq = OMAP5430_GPU_MIN_FREQ,
	.max_freq = OMAP5430_GPU_MAX_FREQ,
	.max_temp = OMAP5430_GPU_MAX_TEMP,
	.min_temp = OMAP5430_GPU_MIN_TEMP,
	.hyst_val = OMAP5430_GPU_HYST_VAL,
	.adc_start_val = OMAP5430_ADC_START_VALUE,
	.adc_end_val = OMAP5430_ADC_END_VALUE,
	.update_int1 = 1000,
	.update_int2 = 2000,
};

/* Thresholds and limits for OMAP5430 CORE temperature sensor */
static struct temp_sensor_data omap5430_core_temp_sensor_data = {
	.tshut_hot = OMAP5430_CORE_TSHUT_HOT,
	.tshut_cold = OMAP5430_CORE_TSHUT_COLD,
	.t_hot = OMAP5430_CORE_T_HOT,
	.t_cold = OMAP5430_CORE_T_COLD,
	.min_freq = OMAP5430_CORE_MIN_FREQ,
	.max_freq = OMAP5430_CORE_MAX_FREQ,
	.max_temp = OMAP5430_CORE_MAX_TEMP,
	.min_temp = OMAP5430_CORE_MIN_TEMP,
	.hyst_val = OMAP5430_CORE_HYST_VAL,
	.adc_start_val = OMAP5430_ADC_START_VALUE,
	.adc_end_val = OMAP5430_ADC_END_VALUE,
	.update_int1 = 1000,
	.update_int2 = 2000,
};

static const int
omap5430_adc_to_temp[OMAP5430_ADC_END_VALUE - OMAP5430_ADC_START_VALUE + 1] = {
	-40000, -40000, -40000, -40000, -39800, -39400, -39000, -38600,
	-38200, -37800, -37300, -36800,
	-36400, -36000, -35600, -35200, -34800, -34300, -33800, -33400, -33000,
	-32600,
	-32200, -31800, -31300, -30800, -30400, -30000, -29600, -29200, -28700,
	-28200, -27800, -27400, -27000, -26600, -26200, -25700, -25200, -24800,
	-24400, -24000, -23600, -23200, -22700, -22200, -21800, -21400, -21000,
	-20600, -20200, -19700, -19200, -9300, -18400, -18000, -17600, -17200,
	-16700, -16200, -15800, -15400, -15000, -14600, -14200, -13700, -13200,
	-12800, -12400, -12000, -11600, -11200, -10700, -10200, -9800, -9400,
	-9000,
	-8600, -8200, -7700, -7200, -6800, -6400, -6000, -5600, -5200, -4800,
	-4300,
	-3800, -3400, -3000, -2600, -2200, -1800, -1300, -800, -400, 0, 400,
	800,
	1200, 1600, 2100, 2600, 3000, 3400, 3800, 4200, 4600, 5100, 5600, 6000,
	6400, 6800, 7200, 7600, 8000, 8500, 9000, 9400, 9800, 10200, 10800,
	11100,
	11400, 11900, 12400, 12800, 13200, 13600, 14000, 14400, 14800, 15300,
	15800,
	16200, 16600, 17000, 17400, 17800, 18200, 18700, 19200, 19600, 20000,
	20400,
	20800, 21200, 21600, 22100, 22600, 23000, 23400, 23800, 24200, 24600,
	25000,
	25400, 25900, 26400, 26800, 27200, 27600, 28000, 28400, 28800, 29300,
	29800,
	30200, 30600, 31000, 31400, 31800, 32200, 32600, 33100, 33600, 34000,
	34400,
	34800, 35200, 35600, 36000, 36400, 36800, 37300, 37800, 38200, 38600,
	39000,
	39400, 39800, 40200, 40600, 41100, 41600, 42000, 42400, 42800, 43200,
	43600,
	44000, 44400, 44800, 45300, 45800, 46200, 46600, 47000, 47400, 47800,
	48200,
	48600, 49000, 49500, 50000, 50400, 50800, 51200, 51600, 52000, 52400,
	52800,
	53200, 53700, 54200, 54600, 55000, 55400, 55800, 56200, 56600, 57000,
	57400,
	57800, 58200, 58700, 59200, 59600, 60000, 60400, 60800, 61200, 61600,
	62000,
	62400, 62800, 63300, 63800, 64200, 64600, 65000, 65400, 65800, 66200,
	66600,
	67000, 67400, 67800, 68200, 68700, 69200, 69600, 70000, 70400, 70800,
	71200,
	71600, 72000, 72400, 72800, 73200, 73600, 74100, 74600, 75000, 75400,
	75800,
	76200, 76600, 77000, 77400, 77800, 78200, 78600, 79000, 79400, 79800,
	80300,
	80800, 81200, 81600, 82000, 82400, 82800, 83200, 83600, 84000, 84400,
	84800,
	85200, 85600, 86000, 86400, 86800, 87300, 87800, 88200, 88600, 89000,
	89400,
	89800, 90200, 90600, 91000, 91400, 91800, 92200, 92600, 93000, 93400,
	93800,
	94200, 94600, 95000, 95500, 96000, 96400, 96800, 97200, 97600, 98000,
	98400,
	98800, 99200, 99600, 100000, 100400, 100800, 101200, 101600, 102000,
	102400,
	102800, 103200, 103600, 104000, 104400, 104800, 105200, 105600, 106100,
	106600, 107000, 107400, 107800, 108200, 108600, 109000, 109400, 109800,
	110200, 110600, 111000, 111400, 111800, 112200, 112600, 113000, 113400,
	113800, 114200, 114600, 115000, 115400, 115800, 116200, 116600, 117000,
	117400, 117800, 118200, 118600, 119000, 119400, 119800, 120200, 120600,
	121000, 121400, 121800, 122200, 122600, 123000, 123400, 123800, 124200,
	124600, 124900, 125000, 125000, 125000, 125000,
};

const struct omap_bandgap_data omap5430_data = {
	.features = OMAP_BANDGAP_FEATURE_TSHUT_CONFIG |
			OMAP_BANDGAP_FEATURE_TALERT |
			OMAP_BANDGAP_FEATURE_MODE_CONFIG |
			OMAP_BANDGAP_FEATURE_COUNTER |
			OMAP_BANDGAP_FEATURE_CLK_CTRL,
	.fclock_name = "ts_clk_div_ck",
	.div_ck_name = "ts_clk_div_ck",
	.conv_table = omap5430_adc_to_temp,
	.expose_sensor = omap_thermal_expose_sensor,
	.remove_sensor = omap_thermal_remove_sensor,
	.sensors = {
		{
		.registers = &omap5430_mpu_temp_sensor_registers,
		.ts_data = &omap5430_mpu_temp_sensor_data,
		.domain = "cpu",
		.register_cooling = omap_thermal_register_cpu_cooling,
		.unregister_cooling = omap_thermal_unregister_cpu_cooling,
		.slope = OMAP_GRADIENT_SLOPE_5430_CPU,
		.constant = OMAP_GRADIENT_CONST_5430_CPU,
		.slope_pcb = OMAP_GRADIENT_SLOPE_W_PCB_5430_CPU,
		.constant_pcb = OMAP_GRADIENT_CONST_W_PCB_5430_CPU,
		},
		{
		.registers = &omap5430_gpu_temp_sensor_registers,
		.ts_data = &omap5430_gpu_temp_sensor_data,
		.domain = "gpu",
		.slope = OMAP_GRADIENT_SLOPE_5430_GPU,
		.constant = OMAP_GRADIENT_CONST_5430_GPU,
		.slope_pcb = OMAP_GRADIENT_SLOPE_W_PCB_5430_GPU,
		.constant_pcb = OMAP_GRADIENT_CONST_W_PCB_5430_GPU,
		},
		{
		.registers = &omap5430_core_temp_sensor_registers,
		.ts_data = &omap5430_core_temp_sensor_data,
		.domain = "core",
		},
	},
	.sensor_count = 3,
};
