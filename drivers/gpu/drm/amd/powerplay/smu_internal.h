/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __SMU_INTERNAL_H__
#define __SMU_INTERNAL_H__

#include "amdgpu_smu.h"

#define smu_init_microcode(smu) \
	((smu)->ppt_funcs->init_microcode ? (smu)->ppt_funcs->init_microcode((smu)) : 0)
#define smu_init_smc_tables(smu) \
	((smu)->ppt_funcs->init_smc_tables ? (smu)->ppt_funcs->init_smc_tables((smu)) : 0)
#define smu_fini_smc_tables(smu) \
	((smu)->ppt_funcs->fini_smc_tables ? (smu)->ppt_funcs->fini_smc_tables((smu)) : 0)
#define smu_init_power(smu) \
	((smu)->ppt_funcs->init_power ? (smu)->ppt_funcs->init_power((smu)) : 0)
#define smu_fini_power(smu) \
	((smu)->ppt_funcs->fini_power ? (smu)->ppt_funcs->fini_power((smu)) : 0)

#define smu_setup_pptable(smu) \
	((smu)->ppt_funcs->setup_pptable ? (smu)->ppt_funcs->setup_pptable((smu)) : 0)
#define smu_powergate_sdma(smu, gate) \
	((smu)->ppt_funcs->powergate_sdma ? (smu)->ppt_funcs->powergate_sdma((smu), (gate)) : 0)
#define smu_powergate_vcn(smu, gate) \
	((smu)->ppt_funcs->powergate_vcn ? (smu)->ppt_funcs->powergate_vcn((smu), (gate)) : 0)
#define smu_powergate_jpeg(smu, gate) \
	((smu)->ppt_funcs->powergate_jpeg ? (smu)->ppt_funcs->powergate_jpeg((smu), (gate)) : 0)

#define smu_get_vbios_bootup_values(smu) \
	((smu)->ppt_funcs->get_vbios_bootup_values ? (smu)->ppt_funcs->get_vbios_bootup_values((smu)) : 0)
#define smu_get_clk_info_from_vbios(smu) \
	((smu)->ppt_funcs->get_clk_info_from_vbios ? (smu)->ppt_funcs->get_clk_info_from_vbios((smu)) : 0)
#define smu_check_pptable(smu) \
	((smu)->ppt_funcs->check_pptable ? (smu)->ppt_funcs->check_pptable((smu)) : 0)
#define smu_parse_pptable(smu) \
	((smu)->ppt_funcs->parse_pptable ? (smu)->ppt_funcs->parse_pptable((smu)) : 0)
#define smu_populate_smc_tables(smu) \
	((smu)->ppt_funcs->populate_smc_tables ? (smu)->ppt_funcs->populate_smc_tables((smu)) : 0)
#define smu_check_fw_version(smu) \
	((smu)->ppt_funcs->check_fw_version ? (smu)->ppt_funcs->check_fw_version((smu)) : 0)
#define smu_write_pptable(smu) \
	((smu)->ppt_funcs->write_pptable ? (smu)->ppt_funcs->write_pptable((smu)) : 0)
#define smu_set_min_dcef_deep_sleep(smu) \
	((smu)->ppt_funcs->set_min_dcef_deep_sleep ? (smu)->ppt_funcs->set_min_dcef_deep_sleep((smu)) : 0)
#define smu_set_driver_table_location(smu) \
	((smu)->ppt_funcs->set_driver_table_location ? (smu)->ppt_funcs->set_driver_table_location((smu)) : 0)
#define smu_set_tool_table_location(smu) \
	((smu)->ppt_funcs->set_tool_table_location ? (smu)->ppt_funcs->set_tool_table_location((smu)) : 0)
#define smu_notify_memory_pool_location(smu) \
	((smu)->ppt_funcs->notify_memory_pool_location ? (smu)->ppt_funcs->notify_memory_pool_location((smu)) : 0)
#define smu_gfx_off_control(smu, enable) \
	((smu)->ppt_funcs->gfx_off_control ? (smu)->ppt_funcs->gfx_off_control((smu), (enable)) : 0)

#define smu_set_last_dcef_min_deep_sleep_clk(smu) \
	((smu)->ppt_funcs->set_last_dcef_min_deep_sleep_clk ? (smu)->ppt_funcs->set_last_dcef_min_deep_sleep_clk((smu)) : 0)
#define smu_system_features_control(smu, en) \
	((smu)->ppt_funcs->system_features_control ? (smu)->ppt_funcs->system_features_control((smu), (en)) : 0)
#define smu_init_max_sustainable_clocks(smu) \
	((smu)->ppt_funcs->init_max_sustainable_clocks ? (smu)->ppt_funcs->init_max_sustainable_clocks((smu)) : 0)
#define smu_set_default_od_settings(smu, initialize) \
	((smu)->ppt_funcs->set_default_od_settings ? (smu)->ppt_funcs->set_default_od_settings((smu), (initialize)) : 0)

int smu_send_smc_msg(struct smu_context *smu, enum smu_message_type msg);

#define smu_send_smc_msg_with_param(smu, msg, param) \
	((smu)->ppt_funcs->send_smc_msg_with_param? (smu)->ppt_funcs->send_smc_msg_with_param((smu), (msg), (param)) : 0)
#define smu_read_smc_arg(smu, arg) \
	((smu)->ppt_funcs->read_smc_arg? (smu)->ppt_funcs->read_smc_arg((smu), (arg)) : 0)
#define smu_alloc_dpm_context(smu) \
	((smu)->ppt_funcs->alloc_dpm_context ? (smu)->ppt_funcs->alloc_dpm_context((smu)) : 0)
#define smu_init_display_count(smu, count) \
	((smu)->ppt_funcs->init_display_count ? (smu)->ppt_funcs->init_display_count((smu), (count)) : 0)
#define smu_feature_set_allowed_mask(smu) \
	((smu)->ppt_funcs->set_allowed_mask? (smu)->ppt_funcs->set_allowed_mask((smu)) : 0)
#define smu_feature_get_enabled_mask(smu, mask, num) \
	((smu)->ppt_funcs->get_enabled_mask? (smu)->ppt_funcs->get_enabled_mask((smu), (mask), (num)) : 0)
#define smu_is_dpm_running(smu) \
	((smu)->ppt_funcs->is_dpm_running ? (smu)->ppt_funcs->is_dpm_running((smu)) : 0)
#define smu_notify_display_change(smu) \
	((smu)->ppt_funcs->notify_display_change? (smu)->ppt_funcs->notify_display_change((smu)) : 0)
#define smu_store_powerplay_table(smu) \
	((smu)->ppt_funcs->store_powerplay_table ? (smu)->ppt_funcs->store_powerplay_table((smu)) : 0)
#define smu_check_powerplay_table(smu) \
	((smu)->ppt_funcs->check_powerplay_table ? (smu)->ppt_funcs->check_powerplay_table((smu)) : 0)
#define smu_append_powerplay_table(smu) \
	((smu)->ppt_funcs->append_powerplay_table ? (smu)->ppt_funcs->append_powerplay_table((smu)) : 0)
#define smu_set_default_dpm_table(smu) \
	((smu)->ppt_funcs->set_default_dpm_table ? (smu)->ppt_funcs->set_default_dpm_table((smu)) : 0)
#define smu_populate_umd_state_clk(smu) \
	((smu)->ppt_funcs->populate_umd_state_clk ? (smu)->ppt_funcs->populate_umd_state_clk((smu)) : 0)
#define smu_set_default_od8_settings(smu) \
	((smu)->ppt_funcs->set_default_od8_settings ? (smu)->ppt_funcs->set_default_od8_settings((smu)) : 0)

#define smu_get_current_clk_freq(smu, clk_id, value) \
	((smu)->ppt_funcs->get_current_clk_freq? (smu)->ppt_funcs->get_current_clk_freq((smu), (clk_id), (value)) : 0)

#define smu_tables_init(smu, tab) \
	((smu)->ppt_funcs->tables_init ? (smu)->ppt_funcs->tables_init((smu), (tab)) : 0)
#define smu_set_thermal_fan_table(smu) \
	((smu)->ppt_funcs->set_thermal_fan_table ? (smu)->ppt_funcs->set_thermal_fan_table((smu)) : 0)
#define smu_start_thermal_control(smu) \
	((smu)->ppt_funcs->start_thermal_control? (smu)->ppt_funcs->start_thermal_control((smu)) : 0)
#define smu_stop_thermal_control(smu) \
	((smu)->ppt_funcs->stop_thermal_control? (smu)->ppt_funcs->stop_thermal_control((smu)) : 0)

#define smu_smc_read_sensor(smu, sensor, data, size) \
	((smu)->ppt_funcs->read_sensor? (smu)->ppt_funcs->read_sensor((smu), (sensor), (data), (size)) : -EINVAL)

#define smu_pre_display_config_changed(smu) \
	((smu)->ppt_funcs->pre_display_config_changed ? (smu)->ppt_funcs->pre_display_config_changed((smu)) : 0)
#define smu_display_config_changed(smu) \
	((smu)->ppt_funcs->display_config_changed ? (smu)->ppt_funcs->display_config_changed((smu)) : 0)
#define smu_apply_clocks_adjust_rules(smu) \
	((smu)->ppt_funcs->apply_clocks_adjust_rules ? (smu)->ppt_funcs->apply_clocks_adjust_rules((smu)) : 0)
#define smu_notify_smc_display_config(smu) \
	((smu)->ppt_funcs->notify_smc_display_config ? (smu)->ppt_funcs->notify_smc_display_config((smu)) : 0)
#define smu_force_dpm_limit_value(smu, highest) \
	((smu)->ppt_funcs->force_dpm_limit_value ? (smu)->ppt_funcs->force_dpm_limit_value((smu), (highest)) : 0)
#define smu_unforce_dpm_levels(smu) \
	((smu)->ppt_funcs->unforce_dpm_levels ? (smu)->ppt_funcs->unforce_dpm_levels((smu)) : 0)
#define smu_get_profiling_clk_mask(smu, level, sclk_mask, mclk_mask, soc_mask) \
	((smu)->ppt_funcs->get_profiling_clk_mask ? (smu)->ppt_funcs->get_profiling_clk_mask((smu), (level), (sclk_mask), (mclk_mask), (soc_mask)) : 0)
#define smu_set_cpu_power_state(smu) \
	((smu)->ppt_funcs->set_cpu_power_state ? (smu)->ppt_funcs->set_cpu_power_state((smu)) : 0)

#define smu_msg_get_index(smu, msg) \
	((smu)->ppt_funcs? ((smu)->ppt_funcs->get_smu_msg_index? (smu)->ppt_funcs->get_smu_msg_index((smu), (msg)) : -EINVAL) : -EINVAL)
#define smu_clk_get_index(smu, msg) \
	((smu)->ppt_funcs? ((smu)->ppt_funcs->get_smu_clk_index? (smu)->ppt_funcs->get_smu_clk_index((smu), (msg)) : -EINVAL) : -EINVAL)
#define smu_feature_get_index(smu, msg) \
	((smu)->ppt_funcs? ((smu)->ppt_funcs->get_smu_feature_index? (smu)->ppt_funcs->get_smu_feature_index((smu), (msg)) : -EINVAL) : -EINVAL)
#define smu_table_get_index(smu, tab) \
	((smu)->ppt_funcs? ((smu)->ppt_funcs->get_smu_table_index? (smu)->ppt_funcs->get_smu_table_index((smu), (tab)) : -EINVAL) : -EINVAL)
#define smu_power_get_index(smu, src) \
	((smu)->ppt_funcs? ((smu)->ppt_funcs->get_smu_power_index? (smu)->ppt_funcs->get_smu_power_index((smu), (src)) : -EINVAL) : -EINVAL)
#define smu_workload_get_type(smu, profile) \
	((smu)->ppt_funcs? ((smu)->ppt_funcs->get_workload_type? (smu)->ppt_funcs->get_workload_type((smu), (profile)) : -EINVAL) : -EINVAL)
#define smu_run_btc(smu) \
	((smu)->ppt_funcs? ((smu)->ppt_funcs->run_btc? (smu)->ppt_funcs->run_btc((smu)) : 0) : 0)
#define smu_get_allowed_feature_mask(smu, feature_mask, num) \
	((smu)->ppt_funcs? ((smu)->ppt_funcs->get_allowed_feature_mask? (smu)->ppt_funcs->get_allowed_feature_mask((smu), (feature_mask), (num)) : 0) : 0)


#define smu_store_cc6_data(smu, st, cc6_dis, pst_dis, pst_sw_dis) \
	((smu)->ppt_funcs->store_cc6_data ? (smu)->ppt_funcs->store_cc6_data((smu), (st), (cc6_dis), (pst_dis), (pst_sw_dis)) : 0)

#define smu_get_dal_power_level(smu, clocks) \
	((smu)->ppt_funcs->get_dal_power_level ? (smu)->ppt_funcs->get_dal_power_level((smu), (clocks)) : 0)
#define smu_get_perf_level(smu, designation, level) \
	((smu)->ppt_funcs->get_perf_level ? (smu)->ppt_funcs->get_perf_level((smu), (designation), (level)) : 0)
#define smu_get_current_shallow_sleep_clocks(smu, clocks) \
	((smu)->ppt_funcs->get_current_shallow_sleep_clocks ? (smu)->ppt_funcs->get_current_shallow_sleep_clocks((smu), (clocks)) : 0)

#define smu_dpm_set_uvd_enable(smu, enable) \
	((smu)->ppt_funcs->dpm_set_uvd_enable ? (smu)->ppt_funcs->dpm_set_uvd_enable((smu), (enable)) : 0)
#define smu_dpm_set_vce_enable(smu, enable) \
	((smu)->ppt_funcs->dpm_set_vce_enable ? (smu)->ppt_funcs->dpm_set_vce_enable((smu), (enable)) : 0)
#define smu_dpm_set_jpeg_enable(smu, enable) \
	((smu)->ppt_funcs->dpm_set_jpeg_enable ? (smu)->ppt_funcs->dpm_set_jpeg_enable((smu), (enable)) : 0)

#define smu_set_watermarks_table(smu, tab, clock_ranges) \
	((smu)->ppt_funcs->set_watermarks_table ? (smu)->ppt_funcs->set_watermarks_table((smu), (tab), (clock_ranges)) : 0)
#define smu_get_current_clk_freq_by_table(smu, clk_type, value) \
	((smu)->ppt_funcs->get_current_clk_freq_by_table ? (smu)->ppt_funcs->get_current_clk_freq_by_table((smu), (clk_type), (value)) : 0)
#define smu_thermal_temperature_range_update(smu, range, rw) \
	((smu)->ppt_funcs->thermal_temperature_range_update? (smu)->ppt_funcs->thermal_temperature_range_update((smu), (range), (rw)) : 0)
#define smu_get_thermal_temperature_range(smu, range) \
	((smu)->ppt_funcs->get_thermal_temperature_range? (smu)->ppt_funcs->get_thermal_temperature_range((smu), (range)) : 0)
#define smu_register_irq_handler(smu) \
	((smu)->ppt_funcs->register_irq_handler ? (smu)->ppt_funcs->register_irq_handler(smu) : 0)

#define smu_get_dpm_ultimate_freq(smu, param, min, max) \
		((smu)->ppt_funcs->get_dpm_ultimate_freq ? (smu)->ppt_funcs->get_dpm_ultimate_freq((smu), (param), (min), (max)) : 0)

#define smu_asic_set_performance_level(smu, level) \
	((smu)->ppt_funcs->set_performance_level? (smu)->ppt_funcs->set_performance_level((smu), (level)) : -EINVAL);
#define smu_dump_pptable(smu) \
	((smu)->ppt_funcs->dump_pptable ? (smu)->ppt_funcs->dump_pptable((smu)) : 0)
#define smu_get_dpm_clk_limited(smu, clk_type, dpm_level, freq) \
		((smu)->ppt_funcs->get_dpm_clk_limited ? (smu)->ppt_funcs->get_dpm_clk_limited((smu), (clk_type), (dpm_level), (freq)) : -EINVAL)

#define smu_set_soft_freq_limited_range(smu, clk_type, min, max) \
		((smu)->ppt_funcs->set_soft_freq_limited_range ? (smu)->ppt_funcs->set_soft_freq_limited_range((smu), (clk_type), (min), (max)) : -EINVAL)

#define smu_override_pcie_parameters(smu) \
		((smu)->ppt_funcs->override_pcie_parameters ? (smu)->ppt_funcs->override_pcie_parameters((smu)) : 0)

#define smu_update_pcie_parameters(smu, pcie_gen_cap, pcie_width_cap) \
		((smu)->ppt_funcs->update_pcie_parameters ? (smu)->ppt_funcs->update_pcie_parameters((smu), (pcie_gen_cap), (pcie_width_cap)) : 0)

#define smu_disable_umc_cdr_12gbps_workaround(smu) \
	((smu)->ppt_funcs->disable_umc_cdr_12gbps_workaround ? (smu)->ppt_funcs->disable_umc_cdr_12gbps_workaround((smu)) : 0)

#endif
