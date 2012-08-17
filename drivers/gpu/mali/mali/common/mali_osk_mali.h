/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_mali.h
 * Defines the OS abstraction layer which is specific for the Mali kernel device driver (OSK)
 */

#ifndef __MALI_OSK_MALI_H__
#define __MALI_OSK_MALI_H__

#include <mali_osk.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** @addtogroup _mali_osk_miscellaneous
 * @{ */

/** @brief Read the Mali Resource configuration
 *
 * Populates a _mali_arch_resource_t array from configuration settings, which
 * are stored in an OS-specific way.
 *
 * For example, these may be compiled in to a static structure, or read from
 * the filesystem at startup.
 *
 * On failure, do not call _mali_osk_resources_term.
 *
 * @param arch_config a pointer to the store the pointer to the resources
 * @param num_resources the number of resources read
 * @return _MALI_OSK_ERR_OK on success. _MALI_OSK_ERR_NOMEM on allocation
 * error. For other failures, a suitable _mali_osk_errcode_t is returned.
 */
_mali_osk_errcode_t _mali_osk_resources_init( _mali_osk_resource_t **arch_config, u32 *num_resources );

/** @brief Free resources allocated by _mali_osk_resources_init.
 *
 * Frees the _mali_arch_resource_t array allocated by _mali_osk_resources_init
 *
 * @param arch_config a pointer to the stored the pointer to the resources
 * @param num_resources the number of resources in the array
 */
void _mali_osk_resources_term( _mali_osk_resource_t **arch_config, u32 num_resources);
/** @} */ /* end group _mali_osk_miscellaneous */

/** @addtogroup _mali_osk_low_level_memory
 * @{ */

/** @brief Initialize a user-space accessible memory range
 *
 * This initializes a virtual address range such that it is reserved for the
 * current process, but does not map any physical pages into this range.
 *
 * This function may initialize or adjust any members of the
 * mali_memory_allocation \a descriptor supplied, before the physical pages are
 * mapped in with _mali_osk_mem_mapregion_map().
 *
 * The function will always be called with MALI_MEMORY_ALLOCATION_FLAG_MAP_INTO_USERSPACE
 * set in \a descriptor->flags. It is an error to call this function without
 * setting this flag. Otherwise, \a descriptor->flags bits are reserved for
 * future expansion
 *
 * The \a descriptor's process_addr_mapping_info member can be modified to
 * allocate OS-specific information. Note that on input, this will be a
 * ukk_private word from the U/K inteface, as inserted by _mali_ukk_mem_mmap().
 * This is used to pass information from the U/K interface to the OSK interface,
 * if necessary. The precise usage of the process_addr_mapping_info member
 * depends on the U/K implementation of _mali_ukk_mem_mmap().
 *
 * Therefore, the U/K implementation of _mali_ukk_mem_mmap() and the OSK
 * implementation of  _mali_osk_mem_mapregion_init() must agree on the meaning and
 * usage of the ukk_private word and process_addr_mapping_info member.
 *
 * Refer to \ref u_k_api for more information on the U/K interface.
 *
 * On successful return, \a descriptor's mapping member will be correct for
 * use with _mali_osk_mem_mapregion_term() and _mali_osk_mem_mapregion_map().
 *
 * @param descriptor the mali_memory_allocation to initialize.
 */
_mali_osk_errcode_t _mali_osk_mem_mapregion_init( mali_memory_allocation * descriptor );

/** @brief Terminate a user-space accessible memory range
 *
 * This terminates a virtual address range reserved in the current user process,
 * where none, some or all of the virtual address ranges have mappings to
 * physical pages.
 *
 * It will unmap any physical pages that had been mapped into a reserved
 * virtual address range for the current process, and then releases the virtual
 * address range. Any extra book-keeping information or resources allocated
 * during _mali_osk_mem_mapregion_init() will also be released.
 *
 * The \a descriptor itself is not freed - this must be handled by the caller of
 * _mali_osk_mem_mapregion_term().
 *
 * The function will always be called with MALI_MEMORY_ALLOCATION_FLAG_MAP_INTO_USERSPACE
 * set in descriptor->flags. It is an error to call this function without
 * setting this flag. Otherwise, descriptor->flags bits are reserved for
 * future expansion
 *
 * @param descriptor the mali_memory_allocation to terminate.
 */
void _mali_osk_mem_mapregion_term( mali_memory_allocation * descriptor );

/** @brief Map physical pages into a user process's virtual address range
 *
 * This is used to map a number of physically contigous pages into a
 * user-process's virtual address range, which was previously reserved by a
 * call to _mali_osk_mem_mapregion_init().
 *
 * This need not provide a mapping for the entire virtual address range
 * reserved for \a descriptor - it may be used to map single pages per call.
 *
 * The function will always be called with MALI_MEMORY_ALLOCATION_FLAG_MAP_INTO_USERSPACE
 * set in \a descriptor->flags. It is an error to call this function without
 * setting this flag. Otherwise, \a descriptor->flags bits are reserved for
 * future expansion
 *
 * The function may supply \a *phys_addr == \ref MALI_MEMORY_ALLOCATION_OS_ALLOCATED_PHYSADDR_MAGIC.
 * In this case, \a size must be set to \ref _MALI_OSK_CPU_PAGE_SIZE, and the function
 * will allocate the physical page itself. The physical address of the
 * allocated page will be returned through \a phys_addr.
 *
 * It is an error to set \a size != \ref _MALI_OSK_CPU_PAGE_SIZE while
 * \a *phys_addr == \ref MALI_MEMORY_ALLOCATION_OS_ALLOCATED_PHYSADDR_MAGIC,
 * since it is not always possible for OSs to support such a setting through this
 * interface.
 *
 * @note \b IMPORTANT: This code must validate the input parameters. If the
 * range defined by \a offset and \a size is outside the range allocated in
 * \a descriptor, then this function \b MUST not attempt any mapping, and must
 * instead return a suitable \ref _mali_osk_errcode_t \b failure code.
 *
 * @param[in,out] descriptor the mali_memory_allocation representing the
 * user-process's virtual address range to map into.
 *
 * @param[in] offset the offset into the virtual address range. This is only added
 * to the mapping member of the \a descriptor, and not the \a phys_addr parameter.
 * It must be a multiple of \ref _MALI_OSK_CPU_PAGE_SIZE.
 *
 * @param[in,out] phys_addr a pointer to the physical base address to begin the
 * mapping from. If \a size == \ref _MALI_OSK_CPU_PAGE_SIZE and
 * \a *phys_addr == \ref MALI_MEMORY_ALLOCATION_OS_ALLOCATED_PHYSADDR_MAGIC, then this
 * function will allocate the physical page itself, and return the
 * physical address of the page through \a phys_addr, which will be aligned to
 * \ref _MALI_OSK_CPU_PAGE_SIZE. Otherwise, \a *phys_addr must be aligned to
 * \ref _MALI_OSK_CPU_PAGE_SIZE, and is unmodified after the call.
 * \a phys_addr is unaffected by the \a offset parameter.
 *
 * @param[in] size the number of bytes to map in. This must be a multiple of
 * \ref _MALI_OSK_CPU_PAGE_SIZE.
 *
 * @return _MALI_OSK_ERR_OK on sucess, otherwise a _mali_osk_errcode_t value
 * on failure
 *
 * @note could expand to use _mali_osk_mem_mapregion_flags_t instead of
 * \ref MALI_MEMORY_ALLOCATION_OS_ALLOCATED_PHYSADDR_MAGIC, but note that we must
 * also modify the mali process address manager in the mmu/memory engine code.
 */
_mali_osk_errcode_t _mali_osk_mem_mapregion_map( mali_memory_allocation * descriptor, u32 offset, u32 *phys_addr, u32 size );


/** @brief Unmap physical pages from a user process's virtual address range
 *
 * This is used to unmap a number of physically contigous pages from a
 * user-process's virtual address range, which were previously mapped by a
 * call to _mali_osk_mem_mapregion_map(). If the range specified was allocated
 * from OS memory, then that memory will be returned to the OS. Whilst pages
 * will be mapped out, the Virtual address range remains reserved, and at the
 * same base address.
 *
 * When this function is used to unmap pages from OS memory
 * (_mali_osk_mem_mapregion_map() was called with *phys_addr ==
 * \ref MALI_MEMORY_ALLOCATION_OS_ALLOCATED_PHYSADDR_MAGIC), then the \a flags must
 * include \ref _MALI_OSK_MEM_MAPREGION_FLAG_OS_ALLOCATED_PHYSADDR. This is because
 * it is not always easy for an OS implementation to discover whether the
 * memory was OS allocated or not (and so, how it should release the memory).
 *
 * For this reason, only a range of pages of the same allocation type (all OS
 * allocated, or none OS allocacted) may be unmapped in one call. Multiple
 * calls must be made if allocations of these different types exist across the
 * entire region described by the \a descriptor.
 *
 * The function will always be called with MALI_MEMORY_ALLOCATION_FLAG_MAP_INTO_USERSPACE
 * set in \a descriptor->flags. It is an error to call this function without
 * setting this flag. Otherwise, \a descriptor->flags bits are reserved for
 * future expansion
 *
 * @param[in,out] descriptor the mali_memory_allocation representing the
 * user-process's virtual address range to map into.
 *
 * @param[in] offset the offset into the virtual address range. This is only added
 * to the mapping member of the \a descriptor. \a offset must be a multiple of
 * \ref _MALI_OSK_CPU_PAGE_SIZE.
 *
 * @param[in] size the number of bytes to unmap. This must be a multiple of
 * \ref _MALI_OSK_CPU_PAGE_SIZE.
 *
 * @param[in] flags specifies how the memory should be unmapped. For a range
 * of pages that were originally OS allocated, this must have
 * \ref _MALI_OSK_MEM_MAPREGION_FLAG_OS_ALLOCATED_PHYSADDR set.
 */
void _mali_osk_mem_mapregion_unmap( mali_memory_allocation * descriptor, u32 offset, u32 size, _mali_osk_mem_mapregion_flags_t flags );
/** @} */ /* end group _mali_osk_low_level_memory */


#ifdef __cplusplus
}
#endif

#endif /* __MALI_OSK_MALI_H__ */
