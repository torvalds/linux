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
	.bgap_eocz_mask = OMAP5430_BGAP_TEMP_SENSOR_EOCZ_MASK,
	.bgap_dtemp_mask = OMAP5430_BGAP_TEMP_SENSOR_DTEMP_MASK,

	.bgap_mask_ctrl = OMAP5430_BGAP_CTRL_OFFSET,
	.mask_hot_mask = OMAP5430_MASK_HOT_MPU_MASK,
	.mask_cold_mask = OMAP5430_MASK_COLD_MPU_MASK,


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
	.bgap_eocz_mask = OMAP5430_BGAP_TEMP_SENSOR_EOCZ_MASK,
	.bgap_dtemp_mask = OMAP5430_BGAP_TEMP_SENSOR_DTEMP_MASK,

	.bgap_mask_ctrl = OMAP5430_BGAP_CTRL_OFFSET,
	.mask_hot_mask = OMAP5430_MASK_HOT_GPU_MASK,
	.mask_cold_mask = OMAP5430_MASK_COLD_GPU_MASK,

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
	.status_hot_mask = OMAP5430_HOT_GPU_FLAG_MASK,
	.status_cold_mask = OMAP5430_COLD_GPU_FLAG_MASK,

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
	.bgap_eocz_mask = OMAP5430_BGAP_TEMP_SENSOR_EOCZ_MASK,
	.bgap_dtemp_mask = OMAP5430_BGAP_TEMP_SENSOR_DTEMP_MASK,

	.bgap_mask_ctrl = OMAP5430_BGAP_CTRL_OFFSET,
	.mask_hot_mask = OMAP5430_MASK_HOT_CORE_MASK,
	.mask_cold_mask = OMAP5430_MASK_COLD_CORE_MASK,

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

/*
 * OMAP54xx ES2.0 : Temperature values in milli degree celsius
 * ADC code values from 540 to 945
 */
static int
omap5430_adc_to_temp[
	OMAP5430_ADC_END_VALUE - OMAP5430_ADC_START_VALUE + 1] = {
	/* Index 540 - 549 */
	-40000, -40000, -40000, -40000, -39800, -39400, -39000, -38600, -38200,
	-37800,
	/* Index 550 - 559 */
	-37400, -37000, -36600, -36200, -35800, -35300, -34700, -34200, -33800,
	-33400,
	/* Index 560 - 569 */
	-33000, -32600, -32200, -31800, -31400, -31000, -30600, -30200, -29800,
	-29400,
	/* Index 570 - 579 */
	-29000, -28600, -28200, -27700, -27100, -26600, -26200, -25800, -25400,
	-25000,
	/* Index 580 - 589 */
	-24600, -24200, -23800, -23400, -23000, -22600, -22200, -21600, -21400,
	-21000,
	/* Index 590 - 599 */
	-20500, -19900, -19400, -19000, -18600, -18200, -17800, -17400, -17000,
	-16600,
	/* Index 600 - 609 */
	-16200, -15800, -15400, -15000, -14600, -14200, -13800,	-13400, -13000,
	-12500,
	/* Index 610 - 619 */
	-11900, -11400, -11000, -10600, -10200, -9800, -9400, -9000, -8600,
	-8200,
	/* Index 620 - 629 */
	-7800, -7400, -7000, -6600, -6200, -5800, -5400, -5000, -4500, -3900,
	/* Index 630 - 639 */
	-3400, -3000, -2600, -2200, -1800, -1400, -1000, -600, -200, 200,
	/* Index 640 - 649 */
	600, 1000, 1400, 1800, 2200, 2600, 3000, 3400, 3900, 4500,
	/* Index 650 - 659 */
	5000, 5400, 5800, 6200, 6600, 7000, 7400, 7800, 8200, 8600,
	/* Index 660 - 669 */
	9000, 9400, 9800, 10200, 10600, 11000, 11400, 11800, 12200, 12700,
	/* Index 670 - 679 */
	13300, 13800, 14200, 14600, 15000, 15400, 15800, 16200, 16600, 17000,
	/* Index 680 - 689 */
	17400, 17800, 18200, 18600, 19000, 19400, 19800, 20200, 20600, 21100,
	/* Index 690 - 699 */
	21400, 21900, 22500, 23000, 23400, 23800, 24200, 24600, 25000, 25400,
	/* Index 700 - 709 */
	25800, 26200, 26600, 27000, 27400, 27800, 28200, 28600, 29000, 29400,
	/* Index 710 - 719 */
	29800, 30200, 30600, 31000, 31400, 31900, 32500, 33000, 33400, 33800,
	/* Index 720 - 729 */
	34200, 34600, 35000, 35400, 35800, 36200, 36600, 37000, 37400, 37800,
	/* Index 730 - 739 */
	38200, 38600, 39000, 39400, 39800, 40200, 40600, 41000, 41400, 41800,
	/* Index 740 - 749 */
	42200, 42600, 43100, 43700, 44200, 44600, 45000, 45400, 45800, 46200,
	/* Index 750 - 759 */
	46600, 47000, 47400, 47800, 48200, 48600, 49000, 49400, 49800, 50200,
	/* Index 760 - 769 */
	50600, 51000, 51400, 51800, 52200, 52600, 53000, 53400, 53800, 54200,
	/* Index 770 - 779 */
	54600, 55000, 55400, 55900, 56500, 57000, 57400, 57800, 58200, 58600,
	/* Index 780 - 789 */
	59000, 59400, 59800, 60200, 60600, 61000, 61400, 61800, 62200, 62600,
	/* Index 790 - 799 */
	63000, 63400, 63800, 64200, 64600, 65000, 65400, 65800, 66200, 66600,
	/* Index 800 - 809 */
	67000, 67400, 67800, 68200, 68600, 69000, 69400, 69800, 70200, 70600,
	/* Index 810 - 819 */
	71000, 71500, 72100, 72600, 73000, 73400, 73800, 74200, 74600, 75000,
	/* Index 820 - 829 */
	75400, 75800, 76200, 76600, 77000, 77400, 77800, 78200, 78600, 79000,
	/* Index 830 - 839 */
	79400, 79800, 80200, 80600, 81000, 81400, 81800, 82200, 82600, 83000,
	/* Index 840 - 849 */
	83400, 83800, 84200, 84600, 85000, 85400, 85800, 86200, 86600, 87000,
	/* Index 850 - 859 */
	87400, 87800, 88200, 88600, 89000, 89400, 89800, 90200, 90600, 91000,
	/* Index 860 - 869 */
	91400, 91800, 92200, 92600, 93000, 93400, 93800, 94200, 94600, 95000,
	/* Index 870 - 879 */
	95400, 95800, 96200, 96600, 97000, 97500, 98100, 98600, 99000, 99400,
	/* Index 880 - 889 */
	99800, 100200, 100600, 101000, 101400, 101800, 102200, 102600, 103000,
	103400,
	/* Index 890 - 899 */
	103800, 104200, 104600, 105000, 105400, 105800, 106200, 106600, 107000,
	107400,
	/* Index 900 - 909 */
	107800, 108200, 108600, 109000, 109400, 109800, 110200, 110600, 111000,
	111400,
	/* Index 910 - 919 */
	111800, 112200, 112600, 113000, 113400, 113800, 114200, 114600, 115000,
	115400,
	/* Index 920 - 929 */
	115800, 116200, 116600, 117000, 117400, 117800, 118200, 118600, 119000,
	119400,
	/* Index 930 - 939 */
	119800, 120200, 120600, 121000, 121400, 121800, 122400, 122600, 123000,
	123400,
	/* Index 940 - 945 */
	123800, 1242000, 124600, 124900, 125000, 125000,
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
