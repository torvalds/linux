/*
 * Copyright (C) 2010, 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_MEM_OS_H__
#define __MALI_KERNEL_MEM_OS_H__

/**
 * @brief Creates an object that manages allocating OS memory
 *
 * Creates an object that provides an interface to allocate OS memory and
 * have it mapped into the Mali virtual memory space.
 *
 * The object exposes pointers to
 * - allocate OS memory
 * - allocate Mali page tables in OS memory
 * - destroy the object
 *
 * Allocations from OS memory are of type mali_physical_memory_allocation
 * which provides a function to release the allocation.
 *
 * @param max_allocation max. number of bytes that can be allocated from OS memory
 * @param cpu_usage_adjust value to add to mali physical addresses to obtain CPU physical addresses
 * @param name description of the allocator
 * @return pointer to mali_physical_memory_allocator object. NULL on failure.
 **/
mali_physical_memory_allocator * mali_os_allocator_create(u32 max_allocation, u32 cpu_usage_adjust, const char *name);

#endif /* __MALI_KERNEL_MEM_OS_H__ */


