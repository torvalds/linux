/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_L2_CACHE_H__
#define __MALI_KERNEL_L2_CACHE_H__

#include "mali_osk.h"

struct mali_l2_cache_core;

_mali_osk_errcode_t mali_l2_cache_initialize(void);
void mali_l2_cache_terminate(void);

struct mali_l2_cache_core *mali_l2_cache_create(_mali_osk_resource_t * resource);
void mali_l2_cache_delete(struct mali_l2_cache_core *cache);

u32 mali_l2_cache_get_id(struct mali_l2_cache_core *cache);

mali_bool mali_l2_cache_core_set_counter_src0(struct mali_l2_cache_core *cache, u32 counter);
mali_bool mali_l2_cache_core_set_counter_src1(struct mali_l2_cache_core *cache, u32 counter);
u32 mali_l2_cache_core_get_counter_src0(struct mali_l2_cache_core *cache);
u32 mali_l2_cache_core_get_counter_src1(struct mali_l2_cache_core *cache);
void mali_l2_cache_core_get_counter_values(struct mali_l2_cache_core *cache, u32 *src0, u32 *value0, u32 *src1, u32 *value1);
struct mali_l2_cache_core *mali_l2_cache_core_get_glob_l2_core(u32 index);
u32 mali_l2_cache_core_get_glob_num_l2_cores(void);
u32 mali_l2_cache_core_get_max_num_l2_cores(void);

_mali_osk_errcode_t mali_l2_cache_reset(struct mali_l2_cache_core *cache);

_mali_osk_errcode_t mali_l2_cache_invalidate_all(struct mali_l2_cache_core *cache);
_mali_osk_errcode_t mali_l2_cache_invalidate_pages(struct mali_l2_cache_core *cache, u32 *pages, u32 num_pages);

mali_bool mali_l2_cache_lock_power_state(struct mali_l2_cache_core *cache);
void mali_l2_cache_unlock_power_state(struct mali_l2_cache_core *cache);

#endif /* __MALI_KERNEL_L2_CACHE_H__ */
