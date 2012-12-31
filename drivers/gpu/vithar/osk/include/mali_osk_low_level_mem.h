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
 * Physical address
 */
typedef u64 osk_phy_addr;

/**
 * CPU virtual address
 */
typedef void *osk_virt_addr;

/**
 * Physical page allocator 
 */
typedef struct osk_phy_allocator osk_phy_allocator;

/**
 * Dedicated physical page allocator
 */
typedef struct oskp_phy_os_allocator oskp_phy_os_allocator;
/**
 * OS physical page allocator
 */
typedef struct oskp_phy_dedicated_allocator oskp_phy_dedicated_allocator;

/**
 * @brief Initialize a physical page allocator
 *
 * The physical page allocator is responsible for allocating physical memory pages of
 * OSK_PAGE_SIZE bytes each. Pages are allocated through the OS or from a reserved
 * memory region.
 *
 * Physical page allocation through the OS
 *
 * If \a mem is 0, upto \a nr_pages of pages may be allocated through the OS for use
 * by a user process. OSs that require allocating CPU virtual address space in order
 * to allocate physical pages must observe that the CPU virtual address space is 
 * allocated for the current user process and that the physical allocator must always 
 * be used with this same user process.
 *
 * If \a mem is 0, and \a nr_pages is 0, a variable number of pages may be allocated
 * through the OS for use by the kernel (only limited by the available OS memory).
 * Allocated pages may be mapped into the kernel using osk_kmap(). The use case for
 * this type of physical allocator is the allocation of physical pages for MMU page
 * tables. OSs that require allocating CPU virtual address space in order
 * to allocate physical pages must likely manage a list of fixed size virtual
 * address regions against which pages are committed as more pages are allocated.
 *
 * Physical page allocation from a reserved memory region
 *
 * If \a mem is not 0, \a mem specifies the physical start address of a physically 
 * contiguous memory region, from which \a nr_pages of pages may be allocated, for
 * use by a user process. The start address is aligned to OSK_PAGE_SIZE bytes. 
 * The memory region must not be in use by the OS and solely for use by the physical 
 * allocator. OSs that require allocating CPU virtual address space in order
 * to allocate physical pages must observe that the CPU virtual address space is 
 * allocated for the current user process and that the physical allocator must always 
 * be used with this same user process.
 *
 * @param[out] allocator physical allocator to initialize  
 * @param[in] mem        Set \a mem to 0 if physical pages should be allocated through the OS,
 *                       otherwise \a mem represents the physical address of a reserved
 *                       memory region from which pages should be allocated. The physical 
 *                       address must be OSK_PAGE_SIZE aligned.
 * @param[in] nr_pages   maximum number of physical pages that can be allocated.
 *                       If nr_pages > 0, pages are for use in user space.
 *                       If nr_pages is 0, a variable number number of pages can be allocated
 *                       (limited by the available pages from the OS) but the pages are
 *                       for use by the kernel and \a mem must be set to 0
 *                       (to enable allocating physical pages through the OS).
 * @param[in] name		 name of the reserved memory region
 * @return OSK_ERR_NONE if successful. Any other value indicates failure.
 */
OSK_STATIC_INLINE osk_error osk_phy_allocator_init(osk_phy_allocator * const allocator, osk_phy_addr mem, u32 nr_pages, const char* name) CHECK_RESULT;

OSK_STATIC_INLINE osk_error oskp_phy_os_allocator_init(oskp_phy_os_allocator * const allocator,
                                                       osk_phy_addr mem, u32 nr_pages) CHECK_RESULT;
OSK_STATIC_INLINE osk_error oskp_phy_dedicated_allocator_init(oskp_phy_dedicated_allocator * const allocator,
                                                              osk_phy_addr mem, u32 nr_pages, const char* name) CHECK_RESULT;
OSK_STATIC_INLINE osk_error oskp_phy_dedicated_allocator_request_memory(osk_phy_addr mem,u32 nr_pages, const char* name) CHECK_RESULT;


/**
 * @brief Terminate a physical page allocator
 *
 * Frees any resources necessary to manage the physical allocator. Any physical pages that
 * were allocated or mapped by the allocator must have been freed and unmapped earlier.
 *
 * Allocating and mapping pages using the terminated allocator is prohibited until the
 * the \a allocator is reinitailized with osk_phy_allocator_init().
 *
 * @param[in] allocator initialized physical allocator
 */
OSK_STATIC_INLINE void osk_phy_allocator_term(osk_phy_allocator *allocator);

OSK_STATIC_INLINE void oskp_phy_os_allocator_term(oskp_phy_os_allocator *allocator);
OSK_STATIC_INLINE void oskp_phy_dedicated_allocator_term(oskp_phy_dedicated_allocator *allocator);
OSK_STATIC_INLINE void oskp_phy_dedicated_allocator_release_memory(osk_phy_addr mem,u32 nr_pages);

/**
 * @brief Allocate physical pages
 *
 * Allocates \a nr_pages physical pages of OSK_PAGE_SIZE each using the physical
 * allocator \a allocator and stores the physical address of each allocated page
 * in the \a pages array.
 *
 * If the physical allocator was initialized to allocate pages for use by a user
 * process, the pages need to be allocated in the same user space context as the
 * physical allocator was initialized in.
 *
 * This function may block and cannot be used from ISR context.
 *
 * @param[in] allocator initialized physical allocator
 * @param[in] nr_pages  number of physical pages to allocate
 * @param[out] pages    array of \a nr_pages elements storing the physical
 *                      address of an allocated page
 * @return The number of pages successfully allocated,
 * which might be lower than requested, including zero pages.
 */
OSK_STATIC_INLINE u32 osk_phy_pages_alloc(osk_phy_allocator *allocator, u32 nr_pages, osk_phy_addr *pages) CHECK_RESULT;

OSK_STATIC_INLINE u32 oskp_phy_os_pages_alloc(oskp_phy_os_allocator *allocator,
                                                    u32 nr_pages, osk_phy_addr *pages) CHECK_RESULT;
OSK_STATIC_INLINE u32 oskp_phy_dedicated_pages_alloc(oskp_phy_dedicated_allocator *allocator,
                                                           u32 nr_pages, osk_phy_addr *pages) CHECK_RESULT;

/**
 * @brief Free physical pages
 *
 * Frees physical pages previously allocated by osk_phy_pages_alloc(). The same
 * arguments used for the allocation need to be specified when freeing them.
 *
 * Freeing individual pages of a set of pages allocated by osk_phy_pages_alloc()
 * is not allowed.
 *
 * If the physical allocator was initialized to allocate pages for use by a user
 * process, the pages need to be freed in the same user space context as the
 * physical allocator was initialized in.
 *
 * The contents of the \a pages array is undefined after osk_phy_pages_free has
 * freed the pages.
 *
 * @param[in] allocator initialized physical allocator
 * @param[in] nr_pages  number of physical pages to free (as used during the allocation)
 * @param[in] pages     array of \a nr_pages storing the physical address of an
 *                      allocated page (as used during the allocation).
 */
OSK_STATIC_INLINE void osk_phy_pages_free(osk_phy_allocator *allocator, u32 nr_pages, osk_phy_addr *pages);

OSK_STATIC_INLINE void oskp_phy_os_pages_free(oskp_phy_os_allocator *allocator,
                                              u32 nr_pages, osk_phy_addr *pages);
OSK_STATIC_INLINE void oskp_phy_dedicated_pages_free(oskp_phy_dedicated_allocator *allocator,
                                                     u32 nr_pages, osk_phy_addr *pages);
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

typedef enum osk_kmap_slot {
	OSK_KMAP_SLOT_0 = 0,
	OSK_KMAP_SLOT_1 = 1
} osk_kmap_slot;

/**
 * @brief Map a physical page into the kernel
 *
 * Maps a physical page that was previously allocated by osk_phy_pages_alloc()
 * with a physical allocator setup for allocating pages for use by the kernel,
 * @see osk_phy_allocator_init().
 *
 * Notes:
 * - Uses a very small set of predefined slots
 * - Used for mapping a single page (in each slot) for a very short duration
 *   using one of the defined slots.
 * -  The caller must not sleep until after the osk_kunmap_atomic is called.
 * - It may be assumed that osk_k[un]map_atomic will not fail.
 *
 * @param[in] page  physical address of the page to unmap
 * @param[in] slot  mapping slot. If more than one mapping is active at the
 *                  same time then the mappings must have different slots
 *                  specified. Must be wither OSK_KMAP_SLOT_0 or
 *                  OSK_KMAP_SLOT_1.
 * @return CPU virtual address in the kernel, NULL in case of a failure.
 */

OSK_STATIC_INLINE void *osk_kmap_atomic(osk_phy_addr page, osk_kmap_slot slot) CHECK_RESULT;

/**
 * @brief Unmap a physical page from the kernel
 *
 * Unmaps a previously mapped physical page (with osk_kmap_atomic) from the kernel.
 *
 * @param[in] page      physical address of the page to unmap
 * @param[in] mapping   virtual address of the mapping to unmap
 * @param[in] slot      mapping slot. Must be the same as what was passed into osk_kmap_atomic.
 */

OSK_STATIC_INLINE void osk_kunmap_atomic(osk_phy_addr page, void * mapping, osk_kmap_slot slot);

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
#include "mali_osk_low_level_dedicated_mem.h"
#include <osk/mali_osk_arch_low_level_mem.h>

typedef enum oskp_phy_allocator_type
{
	OSKP_PHY_ALLOCATOR_OS,
	OSKP_PHY_ALLOCATOR_DEDICATED
} oskp_phy_allocator_type;

struct osk_phy_allocator
{
	oskp_phy_allocator_type type;
	union {
		struct oskp_phy_dedicated_allocator dedicated;
		struct oskp_phy_os_allocator        os;
	} data;
};


OSK_STATIC_INLINE osk_error osk_phy_allocator_init(osk_phy_allocator * const allocator, osk_phy_addr mem, u32 nr_pages, const char* name)
{
	OSK_ASSERT(allocator);
	if (mem == 0)
	{
		allocator->type = OSKP_PHY_ALLOCATOR_OS;
		return oskp_phy_os_allocator_init(&allocator->data.os, mem, nr_pages);
	}
	else
	{
		allocator->type = OSKP_PHY_ALLOCATOR_DEDICATED;
		return oskp_phy_dedicated_allocator_init(&allocator->data.dedicated, mem, nr_pages, name);
	}
}

OSK_STATIC_INLINE void osk_phy_allocator_term(osk_phy_allocator *allocator)
{
	OSK_ASSERT(allocator);
	if (allocator->type == OSKP_PHY_ALLOCATOR_OS)
	{
		oskp_phy_os_allocator_term(&allocator->data.os);
	}
	else
	{
		oskp_phy_dedicated_allocator_term(&allocator->data.dedicated);
	}
}

OSK_STATIC_INLINE u32 osk_phy_pages_alloc(osk_phy_allocator *allocator, u32 nr_pages, osk_phy_addr *pages)
{
	OSK_ASSERT(allocator);
	OSK_ASSERT(pages);
	if (allocator->type != OSKP_PHY_ALLOCATOR_OS && allocator->type != OSKP_PHY_ALLOCATOR_DEDICATED)
	{
		return 0;
	}
	if (allocator->type == OSKP_PHY_ALLOCATOR_OS)
	{
		return oskp_phy_os_pages_alloc(&allocator->data.os, nr_pages, pages);
	}
	else
	{
		return oskp_phy_dedicated_pages_alloc(&allocator->data.dedicated, nr_pages, pages);
	}
}

OSK_STATIC_INLINE void osk_phy_pages_free(osk_phy_allocator *allocator, u32 nr_pages, osk_phy_addr *pages)
{
	OSK_ASSERT(allocator);
	OSK_ASSERT(pages);
	if (allocator->type == OSKP_PHY_ALLOCATOR_OS)
	{
		oskp_phy_os_pages_free(&allocator->data.os, nr_pages, pages);
	}
	else
	{
		oskp_phy_dedicated_pages_free(&allocator->data.dedicated, nr_pages, pages);
	}
}

#ifdef __cplusplus
}
#endif

#endif /* _OSK_LOW_LEVEL_MEM_ */
