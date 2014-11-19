/*
 * exynos_tmu_data.c - Samsung EXYNOS tmu data file
 *
 *  Copyright (C) 2013 Samsung Electronics
 *  Amit Daniel Kachhap <amit.daniel@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "exynos_thermal_common.h"
#include "exynos_tmu.h"
#include "exynos_tmu_data.h"

#if defined(CONFIG_CPU_EXYNOS4210)
static const struct exynos_tmu_registers exynos4210_tmu_registers = {
	.triminfo_data = EXYNOS_TMU_REG_TRIMINFO,
	.triminfo_25_shift = EXYNOS_TRIMINFO_25_SHIFT,
	.triminfo_85_shift = EXYNOS_TRIMINFO_85_SHIFT,
	.tmu_ctrl = EXYNOS_TMU_REG_CONTROL,
	.buf_vref_sel_shift = EXYNOS_TMU_REF_VOLTAGE_SHIFT,
	.buf_vref_sel_mask = EXYNOS_TMU_REF_VOLTAGE_MASK,
	.buf_slope_sel_shift = EXYNOS_TMU_BUF_SLOPE_SEL_SHIFT,
	.buf_slope_sel_mask = EXYNOS_TMU_BUF_SLOPE_SEL_MASK,
	.core_en_shift = EXYNOS_TMU_CORE_EN_SHIFT,
	.tmu_status = EXYNOS_TMU_REG_STATUS,
	.tmu_cur_temp = EXYNOS_TMU_REG_CURRENT_TEMP,
	.threshold_temp = EXYNOS4210_TMU_REG_THRESHOLD_TEMP,
	.threshold_th0 = EXYNOS4210_TMU_REG_TRIG_LEVEL0,
	.tmu_inten = EXYNOS_TMU_REG_INTEN,
	.inten_rise0_shift = EXYNOS_TMU_INTEN_RISE0_SHIFT,
	.inten_rise1_shift = EXYNOS_TMU_INTEN_RISE1_SHIFT,
	.inten_rise2_shift = EXYNOS_TMU_INTEN_RISE2_SHIFT,
	.inten_rise3_shift = EXYNOS_TMU_INTEN_RISE3_SHIFT,
	.tmu_intstat = EXYNOS_TMU_REG_INTSTAT,
	.tmu_intclear = EXYNOS_TMU_REG_INTCLEAR,
	.intclr_rise_mask = EXYNOS4210_TMU_TRIG_LEVEL_MASK,
};

struct exynos_tmu_init_data const exynos4210_default_tmu_data = {
	.tmu_data = {
		{
		.threshold = 80,
		.trigger_levels[0] = 5,
		.trigger_levels[1] = 20,
		.trigger_levels[2] = 30,
		.trigger_enable[0] = true,
		.trigger_enable[1] = true,
		.trigger_enable[2] = true,
		.trigger_enable[3] = false,
		.trigger_type[0] = THROTTLE_ACTIVE,
		.trigger_type[1] = THROTTLE_ACTIVE,
		.trigger_type[2] = SW_TRIP,
		.max_trigger_level = 4,
		.gain = 15,
		.reference_voltage = 7,
		.cal_type = TYPE_ONE_POINT_TRIMMING,
		.min_efuse_value = 40,
		.max_efuse_value = 100,
		.first_point_trim = 25,
		.second_point_trim = 85,
		.default_temp_offset = 50,
		.freq_tab[0] = {
			.freq_clip_max = 800 * 1000,
			.temp_level = 85,
			},
		.freq_tab[1] = {
			.freq_clip_max = 200 * 1000,
			.temp_level = 100,
		},
		.freq_tab_count = 2,
		.type = SOC_ARCH_EXYNOS4210,
		.registers = &exynos4210_tmu_registers,
		.features = TMU_SUPPORT_READY_STATUS,
		},
	},
	.tmu_count = 1,
};
#endif

#if defined(CONFIG_SOC_EXYNOS3250)
static const struct exynos_tmu_registers exynos3250_tmu_registers = {
	.triminfo_data = EXYNOS_TMU_REG_TRIMINFO,
	.triminfo_25_shift = EXYNOS_TRIMINFO_25_SHIFT,
	.triminfo_85_shift = EXYNOS_TRIMINFO_85_SHIFT,
	.tmu_ctrl = EXYNOS_TMU_REG_CONTROL,
	.test_mux_addr_shift = EXYNOS4412_MUX_ADDR_SHIFT,
	.buf_vref_sel_shift = EXYNOS_TMU_REF_VOLTAGE_SHIFT,
	.buf_vref_sel_mask = EXYNOS_TMU_REF_VOLTAGE_MASK,
	.therm_trip_mode_shift = EXYNOS_TMU_TRIP_MODE_SHIFT,
	.therm_trip_mode_mask = EXYNOS_TMU_TRIP_MODE_MASK,
	.therm_trip_en_shift = EXYNOS_TMU_THERM_TRIP_EN_SHIFT,
	.buf_slope_sel_shift = EXYNOS_TMU_BUF_SLOPE_SEL_SHIFT,
	.buf_slope_sel_mask = EXYNOS_TMU_BUF_SLOPE_SEL_MASK,
	.core_en_shift = EXYNOS_TMU_CORE_EN_SHIFT,
	.tmu_status = EXYNOS_TMU_REG_STATUS,
	.tmu_cur_temp = EXYNOS_TMU_REG_CURRENT_TEMP,
	.threshold_th0 = EXYNOS_THD_TEMP_RISE,
	.threshold_th1 = EXYNOS_THD_TEMP_FALL,
	.tmu_inten = EXYNOS_TMU_REG_INTEN,
	.inten_rise0_shift = EXYNOS_TMU_INTEN_RISE0_SHIFT,
	.inten_rise1_shift = EXYNOS_TMU_INTEN_RISE1_SHIFT,
	.inten_rise2_shift = EXYNOS_TMU_INTEN_RISE2_SHIFT,
	.inten_fall0_shift = EXYNOS_TMU_INTEN_FALL0_SHIFT,
	.tmu_intstat = EXYNOS_TMU_REG_INTSTAT,
	.tmu_intclear = EXYNOS_TMU_REG_INTCLEAR,
	.intclr_fall_shift = EXYNOS_TMU_CLEAR_FALL_INT_SHIFT,
	.intclr_rise_shift = EXYNOS_TMU_RISE_INT_SHIFT,
	.intclr_rise_mask = EXYNOS_TMU_RISE_INT_MASK,
	.intclr_fall_mask = EXYNOS_TMU_FALL_INT_MASK,
	.emul_con = EXYNOS_EMUL_CON,
	.emul_temp_shift = EXYNOS_EMUL_DATA_SHIFT,
	.emul_time_shift = EXYNOS_EMUL_TIME_SHIFT,
	.emul_time_mask = EXYNOS_EMUL_TIME_MASK,
};

#define EXYNOS3250_TMU_DATA \
	.threshold_falling = 10, \
	.trigger_levels[0] = 70, \
	.trigger_levels[1] = 95, \
	.trigger_levels[2] = 110, \
	.trigger_levels[3] = 120, \
	.trigger_enable[0] = true, \
	.trigger_enable[1] = true, \
	.trigger_enable[2] = true, \
	.trigger_enable[3] = false, \
	.trigger_type[0] = THROTTLE_ACTIVE, \
	.trigger_type[1] = THROTTLE_ACTIVE, \
	.trigger_type[2] = SW_TRIP, \
	.trigger_type[3] = HW_TRIP, \
	.max_trigger_level = 4, \
	.gain = 8, \
	.reference_voltage = 16, \
	.noise_cancel_mode = 4, \
	.cal_type = TYPE_TWO_POINT_TRIMMING, \
	.efuse_value = 55, \
	.min_efuse_value = 40, \
	.max_efuse_value = 100, \
	.first_point_trim = 25, \
	.second_point_trim = 85, \
	.default_temp_offset = 50, \
	.freq_tab[0] = { \
		.freq_clip_max = 800 * 1000, \
		.temp_level = 70, \
	}, \
	.freq_tab[1] = { \
		.freq_clip_max = 400 * 1000, \
		.temp_level = 95, \
	}, \
	.freq_tab_count = 2, \
	.registers = &exynos3250_tmu_registers, \
	.features = (TMU_SUPPORT_EMULATION | \
			TMU_SUPPORT_FALLING_TRIP | TMU_SUPPORT_READY_STATUS | \
			TMU_SUPPORT_EMUL_TIME)
#endif

#if defined(CONFIG_SOC_EXYNOS3250)
struct exynos_tmu_init_data const exynos3250_default_tmu_data = {
	.tmu_data = {
		{
			EXYNOS3250_TMU_DATA,
			.type = SOC_ARCH_EXYNOS3250,
			.test_mux = EXYNOS4412_MUX_ADDR_VALUE,
		},
	},
	.tmu_count = 1,
};
#endif

#if defined(CONFIG_SOC_EXYNOS4412) || defined(CONFIG_SOC_EXYNOS5250)
static const struct exynos_tmu_registers exynos4412_tmu_registers = {
	.triminfo_data = EXYNOS_TMU_REG_TRIMINFO,
	.triminfo_25_shift = EXYNOS_TRIMINFO_25_SHIFT,
	.triminfo_85_shift = EXYNOS_TRIMINFO_85_SHIFT,
	.triminfo_ctrl = EXYNOS_TMU_TRIMINFO_CON,
	.triminfo_reload_shift = EXYNOS_TRIMINFO_RELOAD_SHIFT,
	.tmu_ctrl = EXYNOS_TMU_REG_CONTROL,
	.test_mux_addr_shift = EXYNOS4412_MUX_ADDR_SHIFT,
	.buf_vref_sel_shift = EXYNOS_TMU_REF_VOLTAGE_SHIFT,
	.buf_vref_sel_mask = EXYNOS_TMU_REF_VOLTAGE_MASK,
	.therm_trip_mode_shift = EXYNOS_TMU_TRIP_MODE_SHIFT,
	.therm_trip_mode_mask = EXYNOS_TMU_TRIP_MODE_MASK,
	.therm_trip_en_shift = EXYNOS_TMU_THERM_TRIP_EN_SHIFT,
	.buf_slope_sel_shift = EXYNOS_TMU_BUF_SLOPE_SEL_SHIFT,
	.buf_slope_sel_mask = EXYNOS_TMU_BUF_SLOPE_SEL_MASK,
	.core_en_shift = EXYNOS_TMU_CORE_EN_SHIFT,
	.tmu_status = EXYNOS_TMU_REG_STATUS,
	.tmu_cur_temp = EXYNOS_TMU_REG_CURRENT_TEMP,
	.threshold_th0 = EXYNOS_THD_TEMP_RISE,
	.threshold_th1 = EXYNOS_THD_TEMP_FALL,
	.tmu_inten = EXYNOS_TMU_REG_INTEN,
	.inten_rise0_shift = EXYNOS_TMU_INTEN_RISE0_SHIFT,
	.inten_rise1_shift = EXYNOS_TMU_INTEN_RISE1_SHIFT,
	.inten_rise2_shift = EXYNOS_TMU_INTEN_RISE2_SHIFT,
	.inten_rise3_shift = EXYNOS_TMU_INTEN_RISE3_SHIFT,
	.inten_fall0_shift = EXYNOS_TMU_INTEN_FALL0_SHIFT,
	.tmu_intstat = EXYNOS_TMU_REG_INTSTAT,
	.tmu_intclear = EXYNOS_TMU_REG_INTCLEAR,
	.intclr_fall_shift = EXYNOS_TMU_CLEAR_FALL_INT_SHIFT,
	.intclr_rise_shift = EXYNOS_TMU_RISE_INT_SHIFT,
	.intclr_rise_mask = EXYNOS_TMU_RISE_INT_MASK,
	.intclr_fall_mask = EXYNOS_TMU_FALL_INT_MASK,
	.emul_con = EXYNOS_EMUL_CON,
	.emul_temp_shift = EXYNOS_EMUL_DATA_SHIFT,
	.emul_time_shift = EXYNOS_EMUL_TIME_SHIFT,
	.emul_time_mask = EXYNOS_EMUL_TIME_MASK,
};

#define EXYNOS4412_TMU_DATA \
	.threshold_falling = 10, \
	.trigger_levels[0] = 70, \
	.trigger_levels[1] = 95, \
	.trigger_levels[2] = 110, \
	.trigger_levels[3] = 120, \
	.trigger_enable[0] = true, \
	.trigger_enable[1] = true, \
	.trigger_enable[2] = true, \
	.trigger_enable[3] = false, \
	.trigger_type[0] = THROTTLE_ACTIVE, \
	.trigger_type[1] = THROTTLE_ACTIVE, \
	.trigger_type[2] = SW_TRIP, \
	.trigger_type[3] = HW_TRIP, \
	.max_trigger_level = 4, \
	.gain = 8, \
	.reference_voltage = 16, \
	.noise_cancel_mode = 4, \
	.cal_type = TYPE_ONE_POINT_TRIMMING, \
	.efuse_value = 55, \
	.min_efuse_value = 40, \
	.max_efuse_value = 100, \
	.first_point_trim = 25, \
	.second_point_trim = 85, \
	.default_temp_offset = 50, \
	.freq_tab[0] = { \
		.freq_clip_max = 1400 * 1000, \
		.temp_level = 70, \
	}, \
	.freq_tab[1] = { \
		.freq_clip_max = 400 * 1000, \
		.temp_level = 95, \
	}, \
	.freq_tab_count = 2, \
	.registers = &exynos4412_tmu_registers, \
	.features = (TMU_SUPPORT_EMULATION | TMU_SUPPORT_TRIM_RELOAD | \
			TMU_SUPPORT_FALLING_TRIP | TMU_SUPPORT_READY_STATUS | \
			TMU_SUPPORT_EMUL_TIME)
#endif

#if defined(CONFIG_SOC_EXYNOS4412)
struct exynos_tmu_init_data const exynos4412_default_tmu_data = {
	.tmu_data = {
		{
			EXYNOS4412_TMU_DATA,
			.type = SOC_ARCH_EXYNOS4412,
			.test_mux = EXYNOS4412_MUX_ADDR_VALUE,
		},
	},
	.tmu_count = 1,
};
#endif

#if defined(CONFIG_SOC_EXYNOS5250)
struct exynos_tmu_init_data const exynos5250_default_tmu_data = {
	.tmu_data = {
		{
			EXYNOS4412_TMU_DATA,
			.type = SOC_ARCH_EXYNOS5250,
		},
	},
	.tmu_count = 1,
};
#endif

#if defined(CONFIG_SOC_EXYNOS5260)
static const struct exynos_tmu_registers exynos5260_tmu_registers = {
	.triminfo_data = EXYNOS_TMU_REG_TRIMINFO,
	.triminfo_25_shift = EXYNOS_TRIMINFO_25_SHIFT,
	.triminfo_85_shift = EXYNOS_TRIMINFO_85_SHIFT,
	.tmu_ctrl = EXYNOS_TMU_REG_CONTROL,
	.tmu_ctrl = EXYNOS_TMU_REG_CONTROL1,
	.buf_vref_sel_shift = EXYNOS_TMU_REF_VOLTAGE_SHIFT,
	.buf_vref_sel_mask = EXYNOS_TMU_REF_VOLTAGE_MASK,
	.therm_trip_mode_shift = EXYNOS_TMU_TRIP_MODE_SHIFT,
	.therm_trip_mode_mask = EXYNOS_TMU_TRIP_MODE_MASK,
	.therm_trip_en_shift = EXYNOS_TMU_THERM_TRIP_EN_SHIFT,
	.buf_slope_sel_shift = EXYNOS_TMU_BUF_SLOPE_SEL_SHIFT,
	.buf_slope_sel_mask = EXYNOS_TMU_BUF_SLOPE_SEL_MASK,
	.core_en_shift = EXYNOS_TMU_CORE_EN_SHIFT,
	.tmu_status = EXYNOS_TMU_REG_STATUS,
	.tmu_cur_temp = EXYNOS_TMU_REG_CURRENT_TEMP,
	.threshold_th0 = EXYNOS_THD_TEMP_RISE,
	.threshold_th1 = EXYNOS_THD_TEMP_FALL,
	.tmu_inten = EXYNOS5260_TMU_REG_INTEN,
	.inten_rise0_shift = EXYNOS_TMU_INTEN_RISE0_SHIFT,
	.inten_rise1_shift = EXYNOS_TMU_INTEN_RISE1_SHIFT,
	.inten_rise2_shift = EXYNOS_TMU_INTEN_RISE2_SHIFT,
	.inten_rise3_shift = EXYNOS_TMU_INTEN_RISE3_SHIFT,
	.inten_fall0_shift = EXYNOS_TMU_INTEN_FALL0_SHIFT,
	.tmu_intstat = EXYNOS5260_TMU_REG_INTSTAT,
	.tmu_intclear = EXYNOS5260_TMU_REG_INTCLEAR,
	.intclr_fall_shift = EXYNOS5420_TMU_CLEAR_FALL_INT_SHIFT,
	.intclr_rise_shift = EXYNOS_TMU_RISE_INT_SHIFT,
	.intclr_rise_mask = EXYNOS5260_TMU_RISE_INT_MASK,
	.intclr_fall_mask = EXYNOS5260_TMU_FALL_INT_MASK,
	.emul_con = EXYNOS5260_EMUL_CON,
	.emul_temp_shift = EXYNOS_EMUL_DATA_SHIFT,
	.emul_time_shift = EXYNOS_EMUL_TIME_SHIFT,
	.emul_time_mask = EXYNOS_EMUL_TIME_MASK,
};

#define __EXYNOS5260_TMU_DATA	\
	.threshold_falling = 10, \
	.trigger_levels[0] = 85, \
	.trigger_levels[1] = 103, \
	.trigger_levels[2] = 110, \
	.trigger_levels[3] = 120, \
	.trigger_enable[0] = true, \
	.trigger_enable[1] = true, \
	.trigger_enable[2] = true, \
	.trigger_enable[3] = false, \
	.trigger_type[0] = THROTTLE_ACTIVE, \
	.trigger_type[1] = THROTTLE_ACTIVE, \
	.trigger_type[2] = SW_TRIP, \
	.trigger_type[3] = HW_TRIP, \
	.max_trigger_level = 4, \
	.gain = 8, \
	.reference_voltage = 16, \
	.noise_cancel_mode = 4, \
	.cal_type = TYPE_ONE_POINT_TRIMMING, \
	.efuse_value = 55, \
	.min_efuse_value = 40, \
	.max_efuse_value = 100, \
	.first_point_trim = 25, \
	.second_point_trim = 85, \
	.default_temp_offset = 50, \
	.freq_tab[0] = { \
		.freq_clip_max = 800 * 1000, \
		.temp_level = 85, \
	}, \
	.freq_tab[1] = { \
		.freq_clip_max = 200 * 1000, \
		.temp_level = 103, \
	}, \
	.freq_tab_count = 2, \
	.registers = &exynos5260_tmu_registers, \

#define EXYNOS5260_TMU_DATA \
	__EXYNOS5260_TMU_DATA \
	.type = SOC_ARCH_EXYNOS5260, \
	.features = (TMU_SUPPORT_EMULATION | TMU_SUPPORT_TRIM_RELOAD | \
			TMU_SUPPORT_FALLING_TRIP | TMU_SUPPORT_READY_STATUS | \
			TMU_SUPPORT_EMUL_TIME)

struct exynos_tmu_init_data const exynos5260_default_tmu_data = {
	.tmu_data = {
		{ EXYNOS5260_TMU_DATA },
		{ EXYNOS5260_TMU_DATA },
		{ EXYNOS5260_TMU_DATA },
		{ EXYNOS5260_TMU_DATA },
		{ EXYNOS5260_TMU_DATA },
	},
	.tmu_count = 5,
};
#endif

#if defined(CONFIG_SOC_EXYNOS5420)
static const struct exynos_tmu_registers exynos5420_tmu_registers = {
	.triminfo_data = EXYNOS_TMU_REG_TRIMINFO,
	.triminfo_25_shift = EXYNOS_TRIMINFO_25_SHIFT,
	.triminfo_85_shift = EXYNOS_TRIMINFO_85_SHIFT,
	.tmu_ctrl = EXYNOS_TMU_REG_CONTROL,
	.buf_vref_sel_shift = EXYNOS_TMU_REF_VOLTAGE_SHIFT,
	.buf_vref_sel_mask = EXYNOS_TMU_REF_VOLTAGE_MASK,
	.therm_trip_mode_shift = EXYNOS_TMU_TRIP_MODE_SHIFT,
	.therm_trip_mode_mask = EXYNOS_TMU_TRIP_MODE_MASK,
	.therm_trip_en_shift = EXYNOS_TMU_THERM_TRIP_EN_SHIFT,
	.buf_slope_sel_shift = EXYNOS_TMU_BUF_SLOPE_SEL_SHIFT,
	.buf_slope_sel_mask = EXYNOS_TMU_BUF_SLOPE_SEL_MASK,
	.core_en_shift = EXYNOS_TMU_CORE_EN_SHIFT,
	.tmu_status = EXYNOS_TMU_REG_STATUS,
	.tmu_cur_temp = EXYNOS_TMU_REG_CURRENT_TEMP,
	.threshold_th0 = EXYNOS_THD_TEMP_RISE,
	.threshold_th1 = EXYNOS_THD_TEMP_FALL,
	.tmu_inten = EXYNOS_TMU_REG_INTEN,
	.inten_rise0_shift = EXYNOS_TMU_INTEN_RISE0_SHIFT,
	.inten_rise1_shift = EXYNOS_TMU_INTEN_RISE1_SHIFT,
	.inten_rise2_shift = EXYNOS_TMU_INTEN_RISE2_SHIFT,
	/* INTEN_RISE3 Not availble in exynos5420 */
	.inten_rise3_shift = EXYNOS_TMU_INTEN_RISE3_SHIFT,
	.inten_fall0_shift = EXYNOS_TMU_INTEN_FALL0_SHIFT,
	.tmu_intstat = EXYNOS_TMU_REG_INTSTAT,
	.tmu_intclear = EXYNOS_TMU_REG_INTCLEAR,
	.intclr_fall_shift = EXYNOS5420_TMU_CLEAR_FALL_INT_SHIFT,
	.intclr_rise_shift = EXYNOS_TMU_RISE_INT_SHIFT,
	.intclr_rise_mask = EXYNOS_TMU_RISE_INT_MASK,
	.intclr_fall_mask = EXYNOS_TMU_FALL_INT_MASK,
	.emul_con = EXYNOS_EMUL_CON,
	.emul_temp_shift = EXYNOS_EMUL_DATA_SHIFT,
	.emul_time_shift = EXYNOS_EMUL_TIME_SHIFT,
	.emul_time_mask = EXYNOS_EMUL_TIME_MASK,
};

#define __EXYNOS5420_TMU_DATA	\
	.threshold_falling = 10, \
	.trigger_levels[0] = 85, \
	.trigger_levels[1] = 103, \
	.trigger_levels[2] = 110, \
	.trigger_levels[3] = 120, \
	.trigger_enable[0] = true, \
	.trigger_enable[1] = true, \
	.trigger_enable[2] = true, \
	.trigger_enable[3] = false, \
	.trigger_type[0] = THROTTLE_ACTIVE, \
	.trigger_type[1] = THROTTLE_ACTIVE, \
	.trigger_type[2] = SW_TRIP, \
	.trigger_type[3] = HW_TRIP, \
	.max_trigger_level = 4, \
	.gain = 8, \
	.reference_voltage = 16, \
	.noise_cancel_mode = 4, \
	.cal_type = TYPE_ONE_POINT_TRIMMING, \
	.efuse_value = 55, \
	.min_efuse_value = 40, \
	.max_efuse_value = 100, \
	.first_point_trim = 25, \
	.second_point_trim = 85, \
	.default_temp_offset = 50, \
	.freq_tab[0] = { \
		.freq_clip_max = 800 * 1000, \
		.temp_level = 85, \
	}, \
	.freq_tab[1] = { \
		.freq_clip_max = 200 * 1000, \
		.temp_level = 103, \
	}, \
	.freq_tab_count = 2, \
	.registers = &exynos5420_tmu_registers, \

#define EXYNOS5420_TMU_DATA \
	__EXYNOS5420_TMU_DATA \
	.type = SOC_ARCH_EXYNOS5250, \
	.features = (TMU_SUPPORT_EMULATION | TMU_SUPPORT_TRIM_RELOAD | \
			TMU_SUPPORT_FALLING_TRIP | TMU_SUPPORT_READY_STATUS | \
			TMU_SUPPORT_EMUL_TIME)

#define EXYNOS5420_TMU_DATA_SHARED \
	__EXYNOS5420_TMU_DATA \
	.type = SOC_ARCH_EXYNOS5420_TRIMINFO, \
	.features = (TMU_SUPPORT_EMULATION | TMU_SUPPORT_TRIM_RELOAD | \
			TMU_SUPPORT_FALLING_TRIP | TMU_SUPPORT_READY_STATUS | \
			TMU_SUPPORT_EMUL_TIME | TMU_SUPPORT_ADDRESS_MULTIPLE)

struct exynos_tmu_init_data const exynos5420_default_tmu_data = {
	.tmu_data = {
		{ EXYNOS5420_TMU_DATA },
		{ EXYNOS5420_TMU_DATA },
		{ EXYNOS5420_TMU_DATA_SHARED },
		{ EXYNOS5420_TMU_DATA_SHARED },
		{ EXYNOS5420_TMU_DATA_SHARED },
	},
	.tmu_count = 5,
};
#endif

#if defined(CONFIG_SOC_EXYNOS5440)
static const struct exynos_tmu_registers exynos5440_tmu_registers = {
	.triminfo_data = EXYNOS5440_TMU_S0_7_TRIM,
	.triminfo_25_shift = EXYNOS_TRIMINFO_25_SHIFT,
	.triminfo_85_shift = EXYNOS_TRIMINFO_85_SHIFT,
	.tmu_ctrl = EXYNOS5440_TMU_S0_7_CTRL,
	.buf_vref_sel_shift = EXYNOS_TMU_REF_VOLTAGE_SHIFT,
	.buf_vref_sel_mask = EXYNOS_TMU_REF_VOLTAGE_MASK,
	.therm_trip_mode_shift = EXYNOS_TMU_TRIP_MODE_SHIFT,
	.therm_trip_mode_mask = EXYNOS_TMU_TRIP_MODE_MASK,
	.therm_trip_en_shift = EXYNOS_TMU_THERM_TRIP_EN_SHIFT,
	.buf_slope_sel_shift = EXYNOS_TMU_BUF_SLOPE_SEL_SHIFT,
	.buf_slope_sel_mask = EXYNOS_TMU_BUF_SLOPE_SEL_MASK,
	.calib_mode_shift = EXYNOS_TMU_CALIB_MODE_SHIFT,
	.calib_mode_mask = EXYNOS_TMU_CALIB_MODE_MASK,
	.core_en_shift = EXYNOS_TMU_CORE_EN_SHIFT,
	.tmu_status = EXYNOS5440_TMU_S0_7_STATUS,
	.tmu_cur_temp = EXYNOS5440_TMU_S0_7_TEMP,
	.threshold_th0 = EXYNOS5440_TMU_S0_7_TH0,
	.threshold_th1 = EXYNOS5440_TMU_S0_7_TH1,
	.threshold_th2 = EXYNOS5440_TMU_S0_7_TH2,
	.threshold_th3_l0_shift = EXYNOS5440_TMU_TH_RISE4_SHIFT,
	.tmu_inten = EXYNOS5440_TMU_S0_7_IRQEN,
	.inten_rise0_shift = EXYNOS5440_TMU_INTEN_RISE0_SHIFT,
	.inten_rise1_shift = EXYNOS5440_TMU_INTEN_RISE1_SHIFT,
	.inten_rise2_shift = EXYNOS5440_TMU_INTEN_RISE2_SHIFT,
	.inten_rise3_shift = EXYNOS5440_TMU_INTEN_RISE3_SHIFT,
	.inten_fall0_shift = EXYNOS5440_TMU_INTEN_FALL0_SHIFT,
	.tmu_intstat = EXYNOS5440_TMU_S0_7_IRQ,
	.tmu_intclear = EXYNOS5440_TMU_S0_7_IRQ,
	.intclr_fall_shift = EXYNOS5440_TMU_CLEAR_FALL_INT_SHIFT,
	.intclr_rise_shift = EXYNOS5440_TMU_RISE_INT_SHIFT,
	.intclr_rise_mask = EXYNOS5440_TMU_RISE_INT_MASK,
	.intclr_fall_mask = EXYNOS5440_TMU_FALL_INT_MASK,
	.tmu_irqstatus = EXYNOS5440_TMU_IRQ_STATUS,
	.emul_con = EXYNOS5440_TMU_S0_7_DEBUG,
	.emul_temp_shift = EXYNOS_EMUL_DATA_SHIFT,
	.tmu_pmin = EXYNOS5440_TMU_PMIN,
};

#define EXYNOS5440_TMU_DATA \
	.trigger_levels[0] = 100, \
	.trigger_levels[4] = 105, \
	.trigger_enable[0] = 1, \
	.trigger_type[0] = SW_TRIP, \
	.trigger_type[4] = HW_TRIP, \
	.max_trigger_level = 5, \
	.gain = 5, \
	.reference_voltage = 16, \
	.noise_cancel_mode = 4, \
	.cal_type = TYPE_ONE_POINT_TRIMMING, \
	.cal_mode = 0, \
	.efuse_value = 0x5b2d, \
	.min_efuse_value = 16, \
	.max_efuse_value = 76, \
	.first_point_trim = 25, \
	.second_point_trim = 70, \
	.default_temp_offset = 25, \
	.type = SOC_ARCH_EXYNOS5440, \
	.registers = &exynos5440_tmu_registers, \
	.features = (TMU_SUPPORT_EMULATION | TMU_SUPPORT_FALLING_TRIP | \
			TMU_SUPPORT_MULTI_INST | TMU_SUPPORT_ADDRESS_MULTIPLE),

struct exynos_tmu_init_data const exynos5440_default_tmu_data = {
	.tmu_data = {
		{ EXYNOS5440_TMU_DATA } ,
		{ EXYNOS5440_TMU_DATA } ,
		{ EXYNOS5440_TMU_DATA } ,
	},
	.tmu_count = 3,
};
#endif
