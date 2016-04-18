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

#ifndef _EVENT_TASKS_H_
#define _EVENT_TASKS_H_
#include "eventmgr.h"

struct amd_display_configuration;

/* eventtasks_generic.c */
int pem_task_adjust_power_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_power_down_asic(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_get_boot_state_id(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_set_boot_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_reset_boot_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_update_new_power_state_clocks(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_system_shutdown(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_register_interrupts(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_unregister_interrupts(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_enable_dynamic_state_management(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_disable_dynamic_state_management(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_enable_clock_power_gatings_tasks(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_powerdown_uvd_tasks(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_powerdown_vce_tasks(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_disable_clock_power_gatings_tasks(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_start_asic_block_usage(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_stop_asic_block_usage(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_setup_asic(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_cleanup_asic(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_store_dal_configuration (struct pp_eventmgr *eventmgr, const struct amd_display_configuration *display_config);
int pem_task_notify_hw_mgr_display_configuration_change(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_notify_hw_mgr_pre_display_configuration_change(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_block_adjust_power_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_unblock_adjust_power_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_notify_power_state_change(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_block_hw_access(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_un_block_hw_access(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_reset_display_phys_access(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_set_cpu_power_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_notify_smc_display_config_after_power_state_adjustment(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
/*powersaving*/

int pem_task_set_power_source(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_notify_hw_of_power_source(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_get_power_saving_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_reset_power_saving_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_set_power_saving_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_set_screen_state_on(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_set_screen_state_off(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_enable_voltage_island_power_gating(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_disable_voltage_island_power_gating(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_enable_cgpg(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_disable_cgpg(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_enable_gfx_clock_gating(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_disable_gfx_clock_gating(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_enable_stutter_mode(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);

/* performance */
int pem_task_set_performance_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_conditionally_force_3d_performance_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_get_2D_performance_state_id(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_create_user_performance_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_update_allowed_performance_levels(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
/*thermal */
int pem_task_initialize_thermal_controller(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);
int pem_task_uninitialize_thermal_controller(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data);

#endif /* _EVENT_TASKS_H_ */
