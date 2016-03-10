/*
 * Copyright (C) 2013-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_MEMORY_UTIL_H__
#define __MALI_MEMORY_UTIL_H__

u32 mali_allocation_unref(struct mali_mem_allocation **alloc);

void mali_allocation_ref(struct mali_mem_allocation *alloc);

void mali_free_session_allocations(struct mali_session_data *session);

#endif
