/*
 *
 * (C) COPYRIGHT 2010, 2012-2017 ARM Limited. All rights reserved.
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





/**
 * @file mali_kbase_mem_linux.h
 * Base kernel memory APIs, Linux implementation.
 */

#ifndef _KBASE_MEM_LINUX_H_
#define _KBASE_MEM_LINUX_H_

/** A HWC dump mapping */
struct kbase_hwc_dma_mapping {
	void       *cpu_va;
	dma_addr_t  dma_pa;
	size_t      size;
};

struct kbase_va_region *kbase_mem_alloc(struct kbase_context *kctx,
		u64 va_pages, u64 commit_pages, u64 extent, u64 *flags,
		u64 *gpu_va);
int kbase_mem_query(struct kbase_context *kctx, u64 gpu_addr, int query, u64 *const pages);
int kbase_mem_import(struct kbase_context *kctx, enum base_mem_import_type type,
		void __user *phandle, u32 padding, u64 *gpu_va, u64 *va_pages,
		u64 *flags);
u64 kbase_mem_alias(struct kbase_context *kctx, u64 *flags, u64 stride, u64 nents, struct base_mem_aliasing_info *ai, u64 *num_pages);
int kbase_mem_flags_change(struct kbase_context *kctx, u64 gpu_addr, unsigned int flags, unsigned int mask);

/**
 * kbase_mem_commit - Change the physical backing size of a region
 *
 * @kctx: The kernel context
 * @gpu_addr: Handle to the memory region
 * @new_pages: Number of physical pages to back the region with
 *
 * Return: 0 on success or error code
 */
int kbase_mem_commit(struct kbase_context *kctx, u64 gpu_addr, u64 new_pages);

int kbase_mmap(struct file *file, struct vm_area_struct *vma);

/**
 * kbase_mem_evictable_init - Initialize the Ephemeral memory the eviction
 * mechanism.
 * @kctx: The kbase context to initialize.
 *
 * Return: Zero on success or -errno on failure.
 */
int kbase_mem_evictable_init(struct kbase_context *kctx);

/**
 * kbase_mem_evictable_deinit - De-initialize the Ephemeral memory eviction
 * mechanism.
 * @kctx: The kbase context to de-initialize.
 */
void kbase_mem_evictable_deinit(struct kbase_context *kctx);

/**
 * kbase_mem_grow_gpu_mapping - Grow the GPU mapping of an allocation
 * @kctx:      Context the region belongs to
 * @reg:       The GPU region
 * @new_pages: The number of pages after the grow
 * @old_pages: The number of pages before the grow
 *
 * Return: 0 on success, -errno on error.
 *
 * Expand the GPU mapping to encompass the new psychical pages which have
 * been added to the allocation.
 *
 * Note: Caller must be holding the region lock.
 */
int kbase_mem_grow_gpu_mapping(struct kbase_context *kctx,
		struct kbase_va_region *reg,
		u64 new_pages, u64 old_pages);

/**
 * kbase_mem_evictable_make - Make a physical allocation eligible for eviction
 * @gpu_alloc: The physical allocation to make evictable
 *
 * Return: 0 on success, -errno on error.
 *
 * Take the provided region and make all the physical pages within it
 * reclaimable by the kernel, updating the per-process VM stats as well.
 * Remove any CPU mappings (as these can't be removed in the shrinker callback
 * as mmap_lock might already be taken) but leave the GPU mapping intact as
 * and until the shrinker reclaims the allocation.
 *
 * Note: Must be called with the region lock of the containing context.
 */
int kbase_mem_evictable_make(struct kbase_mem_phy_alloc *gpu_alloc);

/**
 * kbase_mem_evictable_unmake - Remove a physical allocations eligibility for
 * eviction.
 * @alloc: The physical allocation to remove eviction eligibility from.
 *
 * Return: True if the allocation had its backing restored and false if
 * it hasn't.
 *
 * Make the physical pages in the region no longer reclaimable and update the
 * per-process stats, if the shrinker has already evicted the memory then
 * re-allocate it if the region is still alive.
 *
 * Note: Must be called with the region lock of the containing context.
 */
bool kbase_mem_evictable_unmake(struct kbase_mem_phy_alloc *alloc);

struct kbase_vmap_struct {
	u64 gpu_addr;
	struct kbase_mem_phy_alloc *cpu_alloc;
	struct kbase_mem_phy_alloc *gpu_alloc;
	phys_addr_t *cpu_pages;
	phys_addr_t *gpu_pages;
	void *addr;
	size_t size;
	bool is_cached;
};


/**
 * kbase_vmap_prot - Map a GPU VA range into the kernel safely, only if the
 * requested access permissions are supported
 * @kctx:         Context the VA range belongs to
 * @gpu_addr:     Start address of VA range
 * @size:         Size of VA range
 * @prot_request: Flags indicating how the caller will then access the memory
 * @map:          Structure to be given to kbase_vunmap() on freeing
 *
 * Return: Kernel-accessible CPU pointer to the VA range, or NULL on error
 *
 * Map a GPU VA Range into the kernel. The VA range must be contained within a
 * GPU memory region. Appropriate CPU cache-flushing operations are made as
 * required, dependent on the CPU mapping for the memory region.
 *
 * This is safer than using kmap() on the pages directly,
 * because the pages here are refcounted to prevent freeing (and hence reuse
 * elsewhere in the system) until an kbase_vunmap()
 *
 * The flags in @prot_request should use KBASE_REG_{CPU,GPU}_{RD,WR}, to check
 * whether the region should allow the intended access, and return an error if
 * disallowed. This is essential for security of imported memory, particularly
 * a user buf from SHM mapped into the process as RO. In that case, write
 * access must be checked if the intention is for kernel to write to the
 * memory.
 *
 * The checks are also there to help catch access errors on memory where
 * security is not a concern: imported memory that is always RW, and memory
 * that was allocated and owned by the process attached to @kctx. In this case,
 * it helps to identify memory that was was mapped with the wrong access type.
 *
 * Note: KBASE_REG_GPU_{RD,WR} flags are currently supported for legacy cases
 * where either the security of memory is solely dependent on those flags, or
 * when userspace code was expecting only the GPU to access the memory (e.g. HW
 * workarounds).
 *
 */
void *kbase_vmap_prot(struct kbase_context *kctx, u64 gpu_addr, size_t size,
		      unsigned long prot_request, struct kbase_vmap_struct *map);

/**
 * kbase_vmap - Map a GPU VA range into the kernel safely
 * @kctx:     Context the VA range belongs to
 * @gpu_addr: Start address of VA range
 * @size:     Size of VA range
 * @map:      Structure to be given to kbase_vunmap() on freeing
 *
 * Return: Kernel-accessible CPU pointer to the VA range, or NULL on error
 *
 * Map a GPU VA Range into the kernel. The VA range must be contained within a
 * GPU memory region. Appropriate CPU cache-flushing operations are made as
 * required, dependent on the CPU mapping for the memory region.
 *
 * This is safer than using kmap() on the pages directly,
 * because the pages here are refcounted to prevent freeing (and hence reuse
 * elsewhere in the system) until an kbase_vunmap()
 *
 * kbase_vmap_prot() should be used in preference, since kbase_vmap() makes no
 * checks to ensure the security of e.g. imported user bufs from RO SHM.
 */
void *kbase_vmap(struct kbase_context *kctx, u64 gpu_addr, size_t size,
		struct kbase_vmap_struct *map);

/**
 * kbase_vunmap - Unmap a GPU VA range from the kernel
 * @kctx: Context the VA range belongs to
 * @map:  Structure describing the mapping from the corresponding kbase_vmap()
 *        call
 *
 * Unmaps a GPU VA range from the kernel, given its @map structure obtained
 * from kbase_vmap(). Appropriate CPU cache-flushing operations are made as
 * required, dependent on the CPU mapping for the memory region.
 *
 * The reference taken on pages during kbase_vmap() is released.
 */
void kbase_vunmap(struct kbase_context *kctx, struct kbase_vmap_struct *map);

/** @brief Allocate memory from kernel space and map it onto the GPU
 *
 * @param kctx   The context used for the allocation/mapping
 * @param size   The size of the allocation in bytes
 * @param handle An opaque structure used to contain the state needed to free the memory
 * @return the VA for kernel space and GPU MMU
 */
void *kbase_va_alloc(struct kbase_context *kctx, u32 size, struct kbase_hwc_dma_mapping *handle);

/** @brief Free/unmap memory allocated by kbase_va_alloc
 *
 * @param kctx   The context used for the allocation/mapping
 * @param handle An opaque structure returned by the kbase_va_alloc function.
 */
void kbase_va_free(struct kbase_context *kctx, struct kbase_hwc_dma_mapping *handle);

extern const struct vm_operations_struct kbase_vm_ops;

#endif				/* _KBASE_MEM_LINUX_H_ */
