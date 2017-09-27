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

#include "eventmgr.h"
#include "eventsubchains.h"
#include "eventtasks.h"
#include "hardwaremanager.h"

const pem_event_action reset_display_phy_access_tasks[] = {
	pem_task_reset_display_phys_access,
	NULL
};

const pem_event_action broadcast_power_policy_tasks[] = {
	/* PEM_Task_BroadcastPowerPolicyChange, */
	NULL
};

const pem_event_action unregister_interrupt_tasks[] = {
	pem_task_unregister_interrupts,
	NULL
};

/* Disable GFX Voltage Islands Power Gating */
const pem_event_action disable_gfx_voltage_island_powergating_tasks[] = {
	pem_task_disable_voltage_island_power_gating,
	NULL
};

const pem_event_action disable_gfx_clockgating_tasks[] = {
	pem_task_disable_gfx_clock_gating,
	NULL
};

const pem_event_action block_adjust_power_state_tasks[] = {
	pem_task_block_adjust_power_state,
	NULL
};


const pem_event_action unblock_adjust_power_state_tasks[] = {
	pem_task_unblock_adjust_power_state,
	NULL
};

const pem_event_action set_performance_state_tasks[] = {
	pem_task_set_performance_state,
	NULL
};

const pem_event_action get_2d_performance_state_tasks[] = {
	pem_task_get_2D_performance_state_id,
	NULL
};

const pem_event_action conditionally_force3D_performance_state_tasks[] = {
	pem_task_conditionally_force_3d_performance_state,
	NULL
};

const pem_event_action process_vbios_eventinfo_tasks[] = {
	/* PEM_Task_ProcessVbiosEventInfo,*/
	NULL
};

const pem_event_action enable_dynamic_state_management_tasks[] = {
	/* PEM_Task_ResetBAPMPolicyChangedFlag,*/
	pem_task_get_boot_state_id,
	pem_task_enable_dynamic_state_management,
	pem_task_register_interrupts,
	NULL
};

const pem_event_action enable_clock_power_gatings_tasks[] = {
	pem_task_enable_clock_power_gatings_tasks,
	pem_task_powerdown_uvd_tasks,
	pem_task_powerdown_vce_tasks,
	NULL
};

const pem_event_action setup_asic_tasks[] = {
	pem_task_setup_asic,
	NULL
};

const pem_event_action power_budget_tasks[] = {
	/* TODO
	 * PEM_Task_PowerBudgetWaiverAvailable,
	 * PEM_Task_PowerBudgetWarningMessage,
	 * PEM_Task_PruneStatesBasedOnPowerBudget,
	*/
	NULL
};

const pem_event_action system_config_tasks[] = {
	/* PEM_Task_PruneStatesBasedOnSystemConfig,*/
	NULL
};


const pem_event_action conditionally_force_3d_performance_state_tasks[] = {
	pem_task_conditionally_force_3d_performance_state,
	NULL
};

const pem_event_action ungate_all_display_phys_tasks[] = {
	/* PEM_Task_GetDisplayPhyAccessInfo */
	NULL
};

const pem_event_action uninitialize_display_phy_access_tasks[] = {
	/* PEM_Task_UninitializeDisplayPhysAccess, */
	NULL
};

const pem_event_action disable_gfx_voltage_island_power_gating_tasks[] = {
	/* PEM_Task_DisableVoltageIslandPowerGating, */
	NULL
};

const pem_event_action disable_gfx_clock_gating_tasks[] = {
	pem_task_disable_gfx_clock_gating,
	NULL
};

const pem_event_action set_boot_state_tasks[] = {
	pem_task_get_boot_state_id,
	pem_task_set_boot_state,
	NULL
};

const pem_event_action adjust_power_state_tasks[] = {
	pem_task_notify_hw_mgr_display_configuration_change,
	pem_task_adjust_power_state,
	pem_task_notify_smc_display_config_after_power_state_adjustment,
	pem_task_update_allowed_performance_levels,
	/* to do pem_task_Enable_disable_bapm, */
	NULL
};

const pem_event_action disable_dynamic_state_management_tasks[] = {
	pem_task_unregister_interrupts,
	pem_task_get_boot_state_id,
	pem_task_disable_dynamic_state_management,
	NULL
};

const pem_event_action disable_clock_power_gatings_tasks[] = {
	pem_task_disable_clock_power_gatings_tasks,
	NULL
};

const pem_event_action cleanup_asic_tasks[] = {
	/* PEM_Task_DisableFPS,*/
	pem_task_cleanup_asic,
	NULL
};

const pem_event_action prepare_for_pnp_stop_tasks[] = {
	/* PEM_Task_PrepareForPnpStop,*/
	NULL
};

const pem_event_action set_power_source_tasks[] = {
	pem_task_set_power_source,
	pem_task_notify_hw_of_power_source,
	NULL
};

const pem_event_action set_power_saving_state_tasks[] = {
	pem_task_reset_power_saving_state,
	pem_task_get_power_saving_state,
	pem_task_set_power_saving_state,
	/* PEM_Task_ResetODDCState,
	 * PEM_Task_GetODDCState,
	 * PEM_Task_SetODDCState,*/
	NULL
};

const pem_event_action enable_disable_fps_tasks[] = {
	/* PEM_Task_EnableDisableFPS,*/
	NULL
};

const pem_event_action set_nbmcu_state_tasks[] = {
	/* PEM_Task_NBMCUStateChange,*/
	NULL
};

const pem_event_action reset_hardware_dc_notification_tasks[] = {
	/* PEM_Task_ResetHardwareDCNotification,*/
	NULL
};


const pem_event_action notify_smu_suspend_tasks[] = {
	/* PEM_Task_NotifySMUSuspend,*/
	NULL
};

const pem_event_action disable_smc_firmware_ctf_tasks[] = {
	pem_task_disable_smc_firmware_ctf,
	NULL
};

const pem_event_action disable_fps_tasks[] = {
	/* PEM_Task_DisableFPS,*/
	NULL
};

const pem_event_action vari_bright_suspend_tasks[] = {
	/* PEM_Task_VariBright_Suspend,*/
	NULL
};

const pem_event_action reset_fan_speed_to_default_tasks[] = {
	/* PEM_Task_ResetFanSpeedToDefault,*/
	NULL
};

const pem_event_action power_down_asic_tasks[] = {
	/* PEM_Task_DisableFPS,*/
	pem_task_power_down_asic,
	NULL
};

const pem_event_action disable_stutter_mode_tasks[] = {
	/* PEM_Task_DisableStutterMode,*/
	NULL
};

const pem_event_action set_connected_standby_tasks[] = {
	/* PEM_Task_SetConnectedStandby,*/
	NULL
};

const pem_event_action block_hw_access_tasks[] = {
	pem_task_block_hw_access,
	NULL
};

const pem_event_action unblock_hw_access_tasks[] = {
	pem_task_un_block_hw_access,
	NULL
};

const pem_event_action resume_connected_standby_tasks[] = {
	/* PEM_Task_ResumeConnectedStandby,*/
	NULL
};

const pem_event_action notify_smu_resume_tasks[] = {
	/* PEM_Task_NotifySMUResume,*/
	NULL
};

const pem_event_action reset_display_configCounter_tasks[] = {
	pem_task_reset_display_phys_access,
	NULL
};

const pem_event_action update_dal_configuration_tasks[] = {
	/* PEM_Task_CheckVBlankTime,*/
	NULL
};

const pem_event_action vari_bright_resume_tasks[] = {
	/* PEM_Task_VariBright_Resume,*/
	NULL
};

const pem_event_action notify_hw_power_source_tasks[] = {
	pem_task_notify_hw_of_power_source,
	NULL
};

const pem_event_action process_vbios_event_info_tasks[] = {
	/* PEM_Task_ProcessVbiosEventInfo,*/
	NULL
};

const pem_event_action enable_gfx_clock_gating_tasks[] = {
	pem_task_enable_gfx_clock_gating,
	NULL
};

const pem_event_action enable_gfx_voltage_island_power_gating_tasks[] = {
	pem_task_enable_voltage_island_power_gating,
	NULL
};

const pem_event_action reset_clock_gating_tasks[] = {
	/* PEM_Task_ResetClockGating*/
	NULL
};

const pem_event_action notify_smu_vpu_recovery_end_tasks[] = {
	/* PEM_Task_NotifySmuVPURecoveryEnd,*/
	NULL
};

const pem_event_action disable_vpu_cap_tasks[] = {
	/* PEM_Task_DisableVPUCap,*/
	NULL
};

const pem_event_action execute_escape_sequence_tasks[] = {
	/* PEM_Task_ExecuteEscapesequence,*/
	NULL
};

const pem_event_action notify_power_state_change_tasks[] = {
	pem_task_notify_power_state_change,
	NULL
};

const pem_event_action enable_cgpg_tasks[] = {
	pem_task_enable_cgpg,
	NULL
};

const pem_event_action disable_cgpg_tasks[] = {
	pem_task_disable_cgpg,
	NULL
};

const pem_event_action enable_user_2d_performance_tasks[] = {
	/* PEM_Task_SetUser2DPerformanceFlag,*/
	/* PEM_Task_UpdateUser2DPerformanceEnableEvents,*/
	NULL
};

const pem_event_action add_user_2d_performance_state_tasks[] = {
	/* PEM_Task_Get2DPerformanceTemplate,*/
	/* PEM_Task_AllocateNewPowerStateMemory,*/
	/* PEM_Task_CopyNewPowerStateInfo,*/
	/* PEM_Task_UpdateNewPowerStateClocks,*/
	/* PEM_Task_UpdateNewPowerStateUser2DPerformanceFlag,*/
	/* PEM_Task_AddPowerState,*/
	/* PEM_Task_ReleaseNewPowerStateMemory,*/
	NULL
};

const pem_event_action delete_user_2d_performance_state_tasks[] = {
	/* PEM_Task_GetCurrentUser2DPerformanceStateID,*/
	/* PEM_Task_DeletePowerState,*/
	/* PEM_Task_SetCurrentUser2DPerformanceStateID,*/
	NULL
};

const pem_event_action disable_user_2d_performance_tasks[] = {
	/* PEM_Task_ResetUser2DPerformanceFlag,*/
	/* PEM_Task_UpdateUser2DPerformanceDisableEvents,*/
	NULL
};

const pem_event_action enable_stutter_mode_tasks[] = {
	pem_task_enable_stutter_mode,
	NULL
};

const pem_event_action enable_disable_bapm_tasks[] = {
	/*PEM_Task_EnableDisableBAPM,*/
	NULL
};

const pem_event_action reset_boot_state_tasks[] = {
	pem_task_reset_boot_state,
	NULL
};

const pem_event_action create_new_user_performance_state_tasks[] = {
	pem_task_create_user_performance_state,
	NULL
};

const pem_event_action initialize_thermal_controller_tasks[] = {
	pem_task_initialize_thermal_controller,
	NULL
};

const pem_event_action uninitialize_thermal_controller_tasks[] = {
	pem_task_uninitialize_thermal_controller,
	NULL
};

const pem_event_action set_cpu_power_state[] = {
	pem_task_set_cpu_power_state,
	NULL
};