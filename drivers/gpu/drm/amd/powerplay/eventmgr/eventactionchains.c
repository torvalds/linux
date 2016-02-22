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
#include "eventactionchains.h"
#include "eventsubchains.h"

static const pem_event_action *initialize_event[] = {
	block_adjust_power_state_tasks,
	power_budget_tasks,
	system_config_tasks,
	setup_asic_tasks,
	enable_dynamic_state_management_tasks,
	enable_clock_power_gatings_tasks,
	get_2d_performance_state_tasks,
	set_performance_state_tasks,
	initialize_thermal_controller_tasks,
	conditionally_force_3d_performance_state_tasks,
	process_vbios_eventinfo_tasks,
	broadcast_power_policy_tasks,
	NULL
};

const struct action_chain initialize_action_chain = {
	"Initialize",
	initialize_event
};

static const pem_event_action *uninitialize_event[] = {
	ungate_all_display_phys_tasks,
	uninitialize_display_phy_access_tasks,
	disable_gfx_voltage_island_power_gating_tasks,
	disable_gfx_clock_gating_tasks,
	set_boot_state_tasks,
	adjust_power_state_tasks,
	disable_dynamic_state_management_tasks,
	disable_clock_power_gatings_tasks,
	cleanup_asic_tasks,
	prepare_for_pnp_stop_tasks,
	NULL
};

const struct action_chain uninitialize_action_chain = {
	"Uninitialize",
	uninitialize_event
};

static const pem_event_action *power_source_change_event_pp_enabled[] = {
	set_power_source_tasks,
	set_power_saving_state_tasks,
	adjust_power_state_tasks,
	enable_disable_fps_tasks,
	set_nbmcu_state_tasks,
	broadcast_power_policy_tasks,
	NULL
};

const struct action_chain power_source_change_action_chain_pp_enabled = {
	"Power source change - PowerPlay enabled",
	power_source_change_event_pp_enabled
};

static const pem_event_action *power_source_change_event_pp_disabled[] = {
	set_power_source_tasks,
	set_nbmcu_state_tasks,
	NULL
};

const struct action_chain power_source_changes_action_chain_pp_disabled = {
	"Power source change - PowerPlay disabled",
	power_source_change_event_pp_disabled
};

static const pem_event_action *power_source_change_event_hardware_dc[] = {
	set_power_source_tasks,
	set_power_saving_state_tasks,
	adjust_power_state_tasks,
	enable_disable_fps_tasks,
	reset_hardware_dc_notification_tasks,
	set_nbmcu_state_tasks,
	broadcast_power_policy_tasks,
	NULL
};

const struct action_chain power_source_change_action_chain_hardware_dc = {
	"Power source change - with Hardware DC switching",
	power_source_change_event_hardware_dc
};

static const pem_event_action *suspend_event[] = {
	reset_display_phy_access_tasks,
	unregister_interrupt_tasks,
	disable_gfx_voltage_island_power_gating_tasks,
	disable_gfx_clock_gating_tasks,
	notify_smu_suspend_tasks,
	disable_smc_firmware_ctf_tasks,
	set_boot_state_tasks,
	adjust_power_state_tasks,
	disable_fps_tasks,
	vari_bright_suspend_tasks,
	reset_fan_speed_to_default_tasks,
	power_down_asic_tasks,
	disable_stutter_mode_tasks,
	set_connected_standby_tasks,
	block_hw_access_tasks,
	NULL
};

const struct action_chain suspend_action_chain = {
	"Suspend",
	suspend_event
};

static const pem_event_action *resume_event[] = {
	unblock_hw_access_tasks,
	resume_connected_standby_tasks,
	notify_smu_resume_tasks,
	reset_display_configCounter_tasks,
	update_dal_configuration_tasks,
	vari_bright_resume_tasks,
	block_adjust_power_state_tasks,
	setup_asic_tasks,
	enable_stutter_mode_tasks, /*must do this in boot state and before SMC is started */
	enable_dynamic_state_management_tasks,
	enable_clock_power_gatings_tasks,
	enable_disable_bapm_tasks,
	initialize_thermal_controller_tasks,
	reset_boot_state_tasks,
	adjust_power_state_tasks,
	enable_disable_fps_tasks,
	notify_hw_power_source_tasks,
	process_vbios_event_info_tasks,
	enable_gfx_clock_gating_tasks,
	enable_gfx_voltage_island_power_gating_tasks,
	reset_clock_gating_tasks,
	notify_smu_vpu_recovery_end_tasks,
	disable_vpu_cap_tasks,
	execute_escape_sequence_tasks,
	NULL
};


const struct action_chain resume_action_chain = {
	"resume",
	resume_event
};

static const pem_event_action *complete_init_event[] = {
	adjust_power_state_tasks,
	enable_gfx_clock_gating_tasks,
	enable_gfx_voltage_island_power_gating_tasks,
	notify_power_state_change_tasks,
	NULL
};

const struct action_chain complete_init_action_chain = {
	"complete init",
	complete_init_event
};

static const pem_event_action *enable_gfx_clock_gating_event[] = {
	enable_gfx_clock_gating_tasks,
	NULL
};

const struct action_chain enable_gfx_clock_gating_action_chain = {
	"enable gfx clock gate",
	enable_gfx_clock_gating_event
};

static const pem_event_action *disable_gfx_clock_gating_event[] = {
	disable_gfx_clock_gating_tasks,
	NULL
};

const struct action_chain disable_gfx_clock_gating_action_chain = {
	"disable gfx clock gate",
	disable_gfx_clock_gating_event
};

static const pem_event_action *enable_cgpg_event[] = {
	enable_cgpg_tasks,
	NULL
};

const struct action_chain enable_cgpg_action_chain = {
	"eable cg pg",
	enable_cgpg_event
};

static const pem_event_action *disable_cgpg_event[] = {
	disable_cgpg_tasks,
	NULL
};

const struct action_chain disable_cgpg_action_chain = {
	"disable cg pg",
	disable_cgpg_event
};


/* Enable user _2d performance and activate */

static const pem_event_action *enable_user_state_event[] = {
	create_new_user_performance_state_tasks,
	adjust_power_state_tasks,
	NULL
};

const struct action_chain enable_user_state_action_chain = {
	"Enable user state",
	enable_user_state_event
};

static const pem_event_action *enable_user_2d_performance_event[] = {
	enable_user_2d_performance_tasks,
	add_user_2d_performance_state_tasks,
	set_performance_state_tasks,
	adjust_power_state_tasks,
	delete_user_2d_performance_state_tasks,
	NULL
};

const struct action_chain enable_user_2d_performance_action_chain = {
	"enable_user_2d_performance_event_activate",
	enable_user_2d_performance_event
};


static const pem_event_action *disable_user_2d_performance_event[] = {
	disable_user_2d_performance_tasks,
	delete_user_2d_performance_state_tasks,
	NULL
};

const struct action_chain disable_user_2d_performance_action_chain = {
	"disable_user_2d_performance_event",
	disable_user_2d_performance_event
};


static const pem_event_action *display_config_change_event[] = {
	/* countDisplayConfigurationChangeEventTasks, */
	unblock_adjust_power_state_tasks,
	set_cpu_power_state,
	notify_hw_power_source_tasks,
	/* updateDALConfigurationTasks,
	variBrightDisplayConfigurationChangeTasks, */
	adjust_power_state_tasks,
	/*enableDisableFPSTasks,
	setNBMCUStateTasks,
	notifyPCIEDeviceReadyTasks,*/
	NULL
};

const struct action_chain display_config_change_action_chain = {
	"Display configuration change",
	display_config_change_event
};

static const pem_event_action *readjust_power_state_event[] = {
	adjust_power_state_tasks,
	NULL
};

const struct action_chain readjust_power_state_action_chain = {
	"re-adjust power state",
	readjust_power_state_event
};

