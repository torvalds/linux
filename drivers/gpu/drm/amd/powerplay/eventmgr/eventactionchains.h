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
#ifndef _EVENT_ACTION_CHAINS_H_
#define _EVENT_ACTION_CHAINS_H_
#include "eventmgr.h"

extern const struct action_chain initialize_action_chain;

extern const struct action_chain uninitialize_action_chain;

extern const struct action_chain power_source_change_action_chain_pp_enabled;

extern const struct action_chain power_source_changes_action_chain_pp_disabled;

extern const struct action_chain power_source_change_action_chain_hardware_dc;

extern const struct action_chain suspend_action_chain;

extern const struct action_chain resume_action_chain;

extern const struct action_chain complete_init_action_chain;

extern const struct action_chain enable_gfx_clock_gating_action_chain;

extern const struct action_chain disable_gfx_clock_gating_action_chain;

extern const struct action_chain enable_cgpg_action_chain;

extern const struct action_chain disable_cgpg_action_chain;

extern const struct action_chain enable_user_2d_performance_action_chain;

extern const struct action_chain disable_user_2d_performance_action_chain;

extern const struct action_chain enable_user_state_action_chain;

extern const struct action_chain readjust_power_state_action_chain;

extern const struct action_chain display_config_change_action_chain;

#endif /*_EVENT_ACTION_CHAINS_H_*/

