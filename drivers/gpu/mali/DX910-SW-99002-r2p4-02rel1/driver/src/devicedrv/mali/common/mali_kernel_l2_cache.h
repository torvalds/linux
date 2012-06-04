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
#include "mali_kernel_subsystem.h"
extern struct mali_kernel_subsystem mali_subsystem_l2_cache;

_mali_osk_errcode_t mali_kernel_l2_cache_invalidate_all(void);
_mali_osk_errcode_t mali_kernel_l2_cache_invalidate_page(u32 page);

void mali_kernel_l2_cache_do_enable(void);
void mali_kernel_l2_cache_set_perf_counters(u32 src0, u32 src1, int force_reset);
void mali_kernel_l2_cache_get_perf_counters(u32 *src0, u32 *val0, u32 *src1, u32 *val1);

#endif /* __MALI_KERNEL_L2_CACHE_H__ */
