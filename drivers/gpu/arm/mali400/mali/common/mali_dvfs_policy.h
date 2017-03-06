/*
 * Copyright (C) 2010-2012, 2014, 2016-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_DVFS_POLICY_H__
#define __MALI_DVFS_POLICY_H__

#ifdef __cplusplus
extern "C" {
#endif

void mali_dvfs_policy_realize(struct mali_gpu_utilization_data *data, u64 time_period);

_mali_osk_errcode_t mali_dvfs_policy_init(void);

void mali_dvfs_policy_new_period(void);

mali_bool mali_dvfs_policy_enabled(void);

#if defined(CONFIG_MALI400_PROFILING)
void mali_get_current_gpu_clk_item(struct mali_gpu_clk_item *clk_item);
#endif

#ifdef __cplusplus
}
#endif

#endif/* __MALI_DVFS_POLICY_H__ */
