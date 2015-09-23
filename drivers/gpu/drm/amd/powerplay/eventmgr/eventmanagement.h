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
#ifndef _EVENT_MANAGEMENT_H_
#define _EVENT_MANAGEMENT_H_

#include "eventmgr.h"

int pem_init_event_action_chains(struct pp_eventmgr *eventmgr);
int pem_excute_event_chain(struct pp_eventmgr *eventmgr, const struct action_chain *event_chain, struct pem_event_data *event_data);
const struct action_chain *pem_get_suspend_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_initialize_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_uninitialize_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_power_source_change_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_resume_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_hibernate_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_thermal_notification_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_vbios_notification_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_enter_thermal_state_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_exit_thermal_state_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_enable_powerplay_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_disable_powerplay_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_enable_overdrive_test_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_disable_overdrive_test_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_enable_gfx_clock_gating_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_disable_gfx_clock_gating_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_enable_cgpg_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_disable_cgpg_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_complete_init_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_screen_on_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_screen_off_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_pre_suspend_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_get_pre_resume_action_chain(struct pp_eventmgr *eventmgr);

extern const struct action_chain *pem_enable_user_state_action_chain(struct pp_eventmgr *eventmgr);
extern const struct action_chain *pem_readjust_power_state_action_chain(struct pp_eventmgr *eventmgr);
const struct action_chain *pem_display_config_change_action_chain(struct pp_eventmgr *eventmgr);


#endif /* _EVENT_MANAGEMENT_H_ */
