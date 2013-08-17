/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_osk_low_level_mem.h
 *
 * Defines the kernel low level memory abstraction layer for the base
 * driver.
 */

#ifndef _OSK_LOW_LEVEL_MEM_H_
#define _OSK_LOW_LEVEL_MEM_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_osk_api
 * @{
 */

/**
 * @addtogroup osklowlevelmem Low level memory
 *
 * Provides functions to allocate physical memory and ensure cache coherency.
 *
 * @{
 */

/**
 * CPU virtual address
 */
typedef void *osk_virt_addr;

/**
 * @brief Map a physical page into the kernel
 *
 * Maps a physical page that was previously allocated by osk_phy_pages_alloc()
 * with a physical allocator setup for allocating pages for use by the kernel,
 * @see osk_phy_allocator_init().
 *
 * Notes:
 * - Kernel virtual memory is limited. Limit the number of pages mapped into
 *   the kernel and limit the duration of the mapping.
 *
 * @param[in] page  physical address of the page to unmap
 * @return CPU virtual address in the kernel, NULL in case of a failure.
 */
OSK_STATIC_INLINE void *osk_kmap(osk_phy_addr page) CHECK_RESULT;

/**
 * @brief Unmap a physical page from the kernel
 *
 * Unmaps a previously mapped physical page (with osk_kmap) from the kernel.
 *
 * @param[in] page      physical address of the page to unmap
 * @param[in] mapping   virtual address of the mapping to unmap
 */
OSK_STATIC_INLINE void osk_kunmap(osk_phy_addr page, void * mapping);

/**
 * @brief Map a physical page into the kernel
 *
 * Maps a physical page that was previously allocated by osk_phy_pages_alloc()
 * with a physical allocator setup for allocating pages for use by the kernel,
 * @see osk_phy_allocator_init().
 *
 * Notes:
 * @li Used for mapping a single page for a very short duration
 * @li The system only supports limited number of atomic mappings,
 *     so use should be limited
 * @li The caller must not sleep until after the osk_kunmap_atomic is called.
 * @li It may be assumed that osk_k[un]map_atomic will not fail.
 *
 * @param[in] page  physical address of the page to unmap
 * @return CPU virtual address in the kernel, NULL in case of a failure.
 */
OSK_STATIC_INLINE void *osk_kmap_atomic(osk_phy_addr page) CHECK_RESULT;

/**
 * @brief Unmap a physical page from the kernel
 *
 * Unmaps a previously mapped physical page (with osk_kmap_atomic) from the kernel.
 *
 * @param[in] page      physical address of the page to unmap
 * @param[in] mapping   virtual address of the mapping to unmap
 */
OSK_STATIC_INLINE void osk_kunmap_atomic(osk_phy_addr page, void * mapping);

/**
 * A pointer to a cache synchronization function, either osk_sync_to_cpu()
 * or osk_sync_to_memory().
 */
typedef void (*osk_sync_kmem_fn)(osk_phy_addr, osk_virt_addr, size_t);

/**
 * @brief Synchronize a memory area for other system components usage
 *
 * Performs the necessary memory coherency operations on a given memory area,
 * such that after the call, changes in memory are correctly seen by other
 * system components. Any change made to memory after that call may not be seen
 * by other system components.
 *
 * In effect:
 * - all CPUs will perform a cache clean operation on their inner & outer data caches
 * - any write buffers are drained (including that of outer cache controllers)
 *
 * This function waits until all operations have completed.
 *
 * The area is restricted to one page or less and must not cross a page boundary.
 * The offset within the page is aligned to cache line size and size is ensured
 * to be a multiple of the cache line size.
 *
 * Both physical and virtual address of the area need to be provided to support OS
 * cache flushing APIs that either use the virtual or the physical address. When
 * called from OS specific code it is allowed to only provide the address that
 * is actually used by the specific OS and leave the other address as 0.
 *
 * @param[in] paddr  physical address
 * @param[in] vaddr  CPU virtual address valid in the current user VM or the kernel VM
 * @param[in] sz     size of the area, <= OSK_PAGE_SIZE.
 */
OSK_STATIC_INLINE void osk_sync_to_memory(osk_phy_addr paddr, osk_virt_addr vaddr, size_t sz);

/**
 * @brief Synchronize a memory area for CPU usage
 *
 * Performs the necessary memory coherency operations on a given memory area,
 * such that after the call, changes in memory are correctly seen by any CPU.
 * Any change made to this area by any CPU before this call may be lost.
 *
 * In effect:
 * - all CPUs will perform a cache clean & invalidate operation on their inner &
 *   outer data caches.
 *
 * @note Stricly only an invalidate operation is required but by cleaning the cache
 * too we prevent loosing changes made to the memory area due to software bugs. By
 * having these changes cleaned from the cache it allows us to catch the memory
 * area getting corrupted with the help of watch points. In correct operation the
 * clean & invalidate operation would not be more expensive than an invalidate
 * operation. Also note that for security reasons, it is dangerous to expose a
 * cache 'invalidate only' operation to user space.
 *
 * - any read buffers are flushed (including that of outer cache controllers)
 *
 * This function waits until all operations have completed.
 *
 * The area is restricted to one page or less and must not cross a page boundary.
 * The offset within the page is aligned to cache line size and size is ensured
 * to be a multiple of the cache line size.
 *
 * Both physical and virtual address of the area need to be provided to support OS
 * cache flushing APIs that either use the virtual or the physical address. When
 * called from OS specific code it is allowed to only provide the address that
 * is actually used by the specific OS and leave the other address as 0.
 *
 * @param[in] paddr  physical address
 * @param[in] vaddr  CPU virtual address valid in the current user VM or the kernel VM
 * @param[in] sz     size of the area, <= OSK_PAGE_SIZE.
 */
OSK_STATIC_INLINE void osk_sync_to_cpu(osk_phy_addr paddr, osk_virt_addr vaddr, size_t sz);

/** @} */ /* end group osklowlevelmem */

/** @} */ /* end group base_osk_api */

/** @} */ /* end group base_api */

/* pull in the arch header with the implementation  */
#include <osk/mali_osk_arch_low_level_mem.h>

#ifdef __cplusplus
}
#endif

#endif /* _OSK_LOW_LEVEL_MEM_ */
