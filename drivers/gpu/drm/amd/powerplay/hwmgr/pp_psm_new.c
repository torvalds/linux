/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include "pp_psm_new.h"

int psm_new_init_power_state_table(struct pp_hwmgr *hwmgr)
{
	hwmgr->ps_size = 0;
	hwmgr->num_ps = 0;
	hwmgr->ps = NULL;
	hwmgr->request_ps = NULL;
	hwmgr->current_ps = NULL;
	hwmgr->boot_ps = NULL;
	hwmgr->uvd_ps = NULL;

	return 0;
}

int psm_new_fini_power_state_table(struct pp_hwmgr *hwmgr)
{
	return 0;
}

int psm_new_set_boot_states(struct pp_hwmgr *hwmgr)
{
	return 0;
}

int psm_new_set_performance_states(struct pp_hwmgr *hwmgr)
{
	return 0;
}

int psm_new_set_user_performance_state(struct pp_hwmgr *hwmgr,
					enum PP_StateUILabel label_id,
					struct pp_power_state **state)
{
	return 0;
}

int psm_new_adjust_power_state_dynamic(struct pp_hwmgr *hwmgr,
					bool skip,
					struct pp_power_state *new_ps)
{
	if (skip)
		return 0;

	phm_display_configuration_changed(hwmgr);

	phm_notify_smc_display_config_after_ps_adjustment(hwmgr);

	return 0;
}
