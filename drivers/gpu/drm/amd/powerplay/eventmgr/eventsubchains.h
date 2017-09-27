/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 *
 */

#ifndef _EVENT_SUB_CHAINS_H_
#define _EVENT_SUB_CHAINS_H_

#include "eventmgr.h"

extern const pem_event_action reset_display_phy_access_tasks[];
extern const pem_event_action broadcast_power_policy_tasks[];
extern const pem_event_action unregister_interrupt_tasks[];
extern const pem_event_action disable_GFX_voltage_island_powergating_tasks[];
extern const pem_event_action disable_GFX_clockgating_tasks[];
extern const pem_event_action block_adjust_power_state_tasks[];
extern const pem_event_action unblock_adjust_power_state_tasks[];
extern const pem_event_action set_performance_state_tasks[];
extern const pem_event_action get_2D_performance_state_tasks[];
extern const pem_event_action conditionally_force3D_performance_state_tasks[];
extern const pem_event_action process_vbios_eventinfo_tasks[];
extern const pem_event_action enable_dynamic_state_management_tasks[];
extern const pem_event_action enable_clock_power_gatings_tasks[];
extern const pem_event_action conditionally_force3D_performance_state_tasks[];
extern const pem_event_action setup_asic_tasks[];
extern const pem_event_action power_budget_tasks[];
extern const pem_event_action system_config_tasks[];
extern const pem_event_action get_2d_performance_state_tasks[];
extern const pem_event_action conditionally_force_3d_performance_state_tasks[];
extern const pem_event_action ungate_all_display_phys_tasks[];
extern const pem_event_action uninitialize_display_phy_access_tasks[];
extern const pem_event_action disable_gfx_voltage_island_power_gating_tasks[];
extern const pem_event_action disable_gfx_clock_gating_tasks[];
extern const pem_event_action set_boot_state_tasks[];
extern const pem_event_action adjust_power_state_tasks[];
extern const pem_event_action disable_dynamic_state_management_tasks[];
extern const pem_event_action disable_clock_power_gatings_tasks[];
extern const pem_event_action cleanup_asic_tasks[];
extern const pem_event_action prepare_for_pnp_stop_tasks[];
extern const pem_event_action set_power_source_tasks[];
extern const pem_event_action set_power_saving_state_tasks[];
extern const pem_event_action enable_disable_fps_tasks[];
extern const pem_event_action set_nbmcu_state_tasks[];
extern const pem_event_action reset_hardware_dc_notification_tasks[];
extern const pem_event_action notify_smu_suspend_tasks[];
extern const pem_event_action disable_smc_firmware_ctf_tasks[];
extern const pem_event_action disable_fps_tasks[];
extern const pem_event_action vari_bright_suspend_tasks[];
extern const pem_event_action reset_fan_speed_to_default_tasks[];
extern const pem_event_action power_down_asic_tasks[];
extern const pem_event_action disable_stutter_mode_tasks[];
extern const pem_event_action set_connected_standby_tasks[];
extern const pem_event_action block_hw_access_tasks[];
extern const pem_event_action unblock_hw_access_tasks[];
extern const pem_event_action resume_connected_standby_tasks[];
extern const pem_event_action notify_smu_resume_tasks[];
extern const pem_event_action reset_display_configCounter_tasks[];
extern const pem_event_action update_dal_configuration_tasks[];
extern const pem_event_action vari_bright_resume_tasks[];
extern const pem_event_action notify_hw_power_source_tasks[];
extern const pem_event_action process_vbios_event_info_tasks[];
extern const pem_event_action enable_gfx_clock_gating_tasks[];
extern const pem_event_action enable_gfx_voltage_island_power_gating_tasks[];
extern const pem_event_action reset_clock_gating_tasks[];
extern const pem_event_action notify_smu_vpu_recovery_end_tasks[];
extern const pem_event_action disable_vpu_cap_tasks[];
extern const pem_event_action execute_escape_sequence_tasks[];
extern const pem_event_action notify_power_state_change_tasks[];
extern const pem_event_action enable_cgpg_tasks[];
extern const pem_event_action disable_cgpg_tasks[];
extern const pem_event_action enable_user_2d_performance_tasks[];
extern const pem_event_action add_user_2d_performance_state_tasks[];
extern const pem_event_action delete_user_2d_performance_state_tasks[];
extern const pem_event_action disable_user_2d_performance_tasks[];
extern const pem_event_action enable_stutter_mode_tasks[];
extern const pem_event_action enable_disable_bapm_tasks[];
extern const pem_event_action reset_boot_state_tasks[];
extern const pem_event_action create_new_user_performance_state_tasks[];
extern const pem_event_action initialize_thermal_controller_tasks[];
extern const pem_event_action uninitialize_thermal_controller_tasks[];
extern const pem_event_action set_cpu_power_state[];
#endif /* _EVENT_SUB_CHAINS_H_ */
