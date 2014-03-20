/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





#ifndef _KBASE_MEM_LOWLEVEL_H
#define _KBASE_MEM_LOWLEVEL_H

#ifndef _KBASE_H_
#error "Don't include this file directly, use mali_kbase.h instead"
#endif

/**
 * @brief Flags for kbase_phy_allocator_pages_alloc
 */
#define KBASE_PHY_PAGES_FLAG_DEFAULT (0)	/** Default allocation flag */
#define KBASE_PHY_PAGES_FLAG_CLEAR   (1 << 0)	/** Clear the pages after allocation */
#define KBASE_PHY_PAGES_FLAG_POISON  (1 << 1)	/** Fill the memory with a poison value */

#define KBASE_PHY_PAGES_SUPPORTED_FLAGS (KBASE_PHY_PAGES_FLAG_DEFAULT|KBASE_PHY_PAGES_FLAG_CLEAR|KBASE_PHY_PAGES_FLAG_POISON)

#define KBASE_PHY_PAGES_POISON_VALUE  0xFD /** Value to fill the memory with when KBASE_PHY_PAGES_FLAG_POISON is set */

/**
 * A pointer to a cache synchronization function, either kbase_sync_to_cpu()
 * or kbase_sync_to_memory().
 */
typedef void (*kbase_sync_kmem_fn) (phys_addr_t, void *, size_t);

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
 * @param[in] sz     size of the area, <= PAGE_SIZE.
 */
void kbase_sync_to_memory(phys_addr_t paddr, void *vaddr, size_t sz);

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
 * @param[in] sz     size of the area, <= PAGE_SIZE.
 */
void kbase_sync_to_cpu(phys_addr_t paddr, void *vaddr, size_t sz);

#endif				/* _KBASE_LOWLEVEL_H */
