/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_MEM_MMU_H__
#define __MALI_KERNEL_MEM_MMU_H__

#include "mali_kernel_session_manager.h"

/**
 * Lookup a MMU core by ID.
 * @param id ID of the MMU to find
 * @return NULL if ID not found or valid, non-NULL if a core was found.
 */
void* mali_memory_core_mmu_lookup(u32 id);

/**
 * Set the core pointer of MMU to core owner of MMU
 *
 * @param core Core holding this MMU
 * @param mmu_ptr The MMU whose core pointer needs set to core holding the MMU
 * 
 */
void mali_memory_core_mmu_owner(void *core, void *mmu_ptr);

/**
 * Activate a user session with its address space on the given MMU.
 * If the session can't be activated due to that the MMU is busy and
 * a callback pointer is given, the callback will be called once the MMU becomes idle.
 * If the same callback pointer is registered multiple time it will only be called once.
 * Nested activations are supported.
 * Each call must be matched by a call to @see mali_memory_core_mmu_release_address_space_reference
 *
 * @param mmu The MMU to activate the address space on
 * @param mali_session_data The user session object which address space to activate
 * @param callback Pointer to the function to call when the MMU becomes idle
 * @param callback_arg Argument given to the callback
 * @return 0 if the address space was activated, -EBUSY if the MMU was busy, -EFAULT in all other cases.
 */
int mali_memory_core_mmu_activate_page_table(void* mmu_ptr, struct mali_session_data * mali_session_data, void(*callback)(void*), void * callback_argument);

/**
 * Release a reference to the current active address space.
 * Once the last reference is released any callback(s) registered will be called before the function returns
 *
 * @note Caution must be shown calling this function with locks held due to that callback can be called
 * @param mmu The mmu to release a reference to the active address space of
 */
void mali_memory_core_mmu_release_address_space_reference(void* mmu);

/**
 * Soft reset of MMU - needed after power up
 *
 * @param mmu_ptr The MMU pointer registered with the relevant core
 */
void mali_kernel_mmu_reset(void * mmu_ptr);

void mali_kernel_mmu_force_bus_reset(void * mmu_ptr);

/**
 * Unregister a previously registered callback.
 * @param mmu The MMU to unregister the callback on
 * @param callback The function to unregister
 */
void mali_memory_core_mmu_unregister_callback(void* mmu, void(*callback)(void*));



#endif /* __MALI_KERNEL_MEM_MMU_H__ */
