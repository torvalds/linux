/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2010, 2012-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/**
 * DOC: Base kernel memory APIs, Linux implementation.
 */

#ifndef _KBASE_MEM_LINUX_H_
#define _KBASE_MEM_LINUX_H_

/* A HWC dump mapping */
struct kbase_hwc_dma_mapping {
	void       *cpu_va;
	dma_addr_t  dma_pa;
	size_t      size;
};

/**
 * kbase_mem_alloc - Create a new allocation for GPU
 *
 * @kctx:         The kernel context
 * @va_pages:     The number of pages of virtual address space to reserve
 * @commit_pages: The number of physical pages to allocate upfront
 * @extension:       The number of extra pages to allocate on each GPU fault which
 *                grows the region.
 * @flags:        bitmask of BASE_MEM_* flags to convey special requirements &
 *                properties for the new allocation.
 * @gpu_va:       Start address of the memory region which was allocated from GPU
 *                virtual address space. If the BASE_MEM_FLAG_MAP_FIXED is set
 *                then this parameter shall be provided by the caller.
 * @mmu_sync_info: Indicates whether this call is synchronous wrt MMU ops.
 *
 * Return: 0 on success or error code
 */
struct kbase_va_region *kbase_mem_alloc(struct kbase_context *kctx, u64 va_pages, u64 commit_pages,
					u64 extension, u64 *flags, u64 *gpu_va,
					enum kbase_caller_mmu_sync_info mmu_sync_info);

/**
 * kbase_mem_query - Query properties of a GPU memory region
 *
 * @kctx:     The kernel context
 * @gpu_addr: A GPU address contained within the memory region
 * @query:    The type of query, from KBASE_MEM_QUERY_* flags, which could be
 *            regarding the amount of backing physical memory allocated so far
 *            for the region or the size of the region or the flags associated
 *            with the region.
 * @out:      Pointer to the location to store the result of query.
 *
 * Return: 0 on success or error code
 */
int kbase_mem_query(struct kbase_context *kctx, u64 gpu_addr, u64 query,
		u64 *const out);

/**
 * kbase_mem_import - Import the external memory for use by the GPU
 *
 * @kctx:     The kernel context
 * @type:     Type of external memory
 * @phandle:  Handle to the external memory interpreted as per the type.
 * @padding:  Amount of extra VA pages to append to the imported buffer
 * @gpu_va:   GPU address assigned to the imported external memory
 * @va_pages: Size of the memory region reserved from the GPU address space
 * @flags:    bitmask of BASE_MEM_* flags to convey special requirements &
 *            properties for the new allocation representing the external
 *            memory.
 * Return: 0 on success or error code
 */
int kbase_mem_import(struct kbase_context *kctx, enum base_mem_import_type type,
		void __user *phandle, u32 padding, u64 *gpu_va, u64 *va_pages,
		u64 *flags);

/**
 * kbase_mem_alias - Create a new allocation for GPU, aliasing one or more
 *                   memory regions
 *
 * @kctx:      The kernel context
 * @flags:     bitmask of BASE_MEM_* flags.
 * @stride:    Bytes between start of each memory region
 * @nents:     The number of regions to pack together into the alias
 * @ai:        Pointer to the struct containing the memory aliasing info
 * @num_pages: Number of pages the alias will cover
 *
 * Return: 0 on failure or otherwise the GPU VA for the alias
 */
u64 kbase_mem_alias(struct kbase_context *kctx, u64 *flags, u64 stride, u64 nents, struct base_mem_aliasing_info *ai, u64 *num_pages);

/**
 * kbase_mem_flags_change - Change the flags for a memory region
 *
 * @kctx:     The kernel context
 * @gpu_addr: A GPU address contained within the memory region to modify.
 * @flags:    The new flags to set
 * @mask:     Mask of the flags, from BASE_MEM_*, to modify.
 *
 * Return: 0 on success or error code
 */
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

/**
 * kbase_mem_shrink - Shrink the physical backing size of a region
 *
 * @kctx: The kernel context
 * @reg:  The GPU region
 * @new_pages: Number of physical pages to back the region with
 *
 * Return: 0 on success or error code
 */
int kbase_mem_shrink(struct kbase_context *kctx,
		struct kbase_va_region *reg, u64 new_pages);

/**
 * kbase_context_mmap - Memory map method, gets invoked when mmap system call is
 *                      issued on device file /dev/malixx.
 * @kctx: The kernel context
 * @vma:  Pointer to the struct containing the info where the GPU allocation
 *        will be mapped in virtual address space of CPU.
 *
 * Return: 0 on success or error code
 */
int kbase_context_mmap(struct kbase_context *kctx, struct vm_area_struct *vma);

/**
 * kbase_mem_evictable_init - Initialize the Ephemeral memory eviction
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
 * @mmu_sync_info: Indicates whether this call is synchronous wrt MMU ops.
 *
 * Return: 0 on success, -errno on error.
 *
 * Expand the GPU mapping to encompass the new psychical pages which have
 * been added to the allocation.
 *
 * Note: Caller must be holding the region lock.
 */
int kbase_mem_grow_gpu_mapping(struct kbase_context *kctx,
			       struct kbase_va_region *reg, u64 new_pages,
			       u64 old_pages,
			       enum kbase_caller_mmu_sync_info mmu_sync_info);

/**
 * kbase_mem_evictable_make - Make a physical allocation eligible for eviction
 * @gpu_alloc: The physical allocation to make evictable
 *
 * Return: 0 on success, -errno on error.
 *
 * Take the provided region and make all the physical pages within it
 * reclaimable by the kernel, updating the per-process VM stats as well.
 * Remove any CPU mappings (as these can't be removed in the shrinker callback
 * as mmap_sem/mmap_lock might already be taken) but leave the GPU mapping
 * intact as and until the shrinker reclaims the allocation.
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
	off_t offset_in_page;
	struct kbase_mem_phy_alloc *cpu_alloc;
	struct kbase_mem_phy_alloc *gpu_alloc;
	struct tagged_addr *cpu_pages;
	struct tagged_addr *gpu_pages;
	void *addr;
	size_t size;
	bool sync_needed;
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
 * it helps to identify memory that was mapped with the wrong access type.
 *
 * Note: KBASE_REG_GPU_{RD,WR} flags are currently supported for legacy cases
 * where either the security of memory is solely dependent on those flags, or
 * when userspace code was expecting only the GPU to access the memory (e.g. HW
 * workarounds).
 *
 * All cache maintenance operations shall be ignored if the
 * memory region has been imported.
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
 *
 * Note: All cache maintenance operations shall be ignored if the memory region
 * has been imported.
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
 *
 * Note: All cache maintenance operations shall be ignored if the memory region
 * has been imported.
 */
void kbase_vunmap(struct kbase_context *kctx, struct kbase_vmap_struct *map);

extern const struct vm_operations_struct kbase_vm_ops;

/**
 * kbase_sync_mem_regions - Perform the cache maintenance for the kernel mode
 *                          CPU mapping.
 * @kctx: Context the CPU mapping belongs to.
 * @map:  Structure describing the CPU mapping, setup previously by the
 *        kbase_vmap() call.
 * @dest: Indicates the type of maintenance required (i.e. flush or invalidate)
 *
 * Note: The caller shall ensure that CPU mapping is not revoked & remains
 * active whilst the maintenance is in progress.
 */
void kbase_sync_mem_regions(struct kbase_context *kctx,
		struct kbase_vmap_struct *map, enum kbase_sync_type dest);

/**
 * kbase_mem_shrink_cpu_mapping - Shrink the CPU mapping(s) of an allocation
 * @kctx:      Context the region belongs to
 * @reg:       The GPU region
 * @new_pages: The number of pages after the shrink
 * @old_pages: The number of pages before the shrink
 *
 * Shrink (or completely remove) all CPU mappings which reference the shrunk
 * part of the allocation.
 */
void kbase_mem_shrink_cpu_mapping(struct kbase_context *kctx,
		struct kbase_va_region *reg,
		u64 new_pages, u64 old_pages);

/**
 * kbase_phy_alloc_mapping_term - Terminate the kernel side mapping of a
 *                                physical allocation
 * @kctx:  The kernel base context associated with the mapping
 * @alloc: Pointer to the allocation to terminate
 *
 * This function will unmap the kernel mapping, and free any structures used to
 * track it.
 */
void kbase_phy_alloc_mapping_term(struct kbase_context *kctx,
		struct kbase_mem_phy_alloc *alloc);

/**
 * kbase_phy_alloc_mapping_get - Get a kernel-side CPU pointer to the permanent
 *                               mapping of a physical allocation
 * @kctx:             The kernel base context @gpu_addr will be looked up in
 * @gpu_addr:         The gpu address to lookup for the kernel-side CPU mapping
 * @out_kern_mapping: Pointer to storage for a struct kbase_vmap_struct pointer
 *                    which will be used for a call to
 *                    kbase_phy_alloc_mapping_put()
 *
 * Return: Pointer to a kernel-side accessible location that directly
 *         corresponds to @gpu_addr, or NULL on failure
 *
 * Looks up @gpu_addr to retrieve the CPU pointer that can be used to access
 * that location kernel-side. Only certain kinds of memory have a permanent
 * kernel mapping, refer to the internal functions
 * kbase_reg_needs_kernel_mapping() and kbase_phy_alloc_mapping_init() for more
 * information.
 *
 * If this function succeeds, a CPU access to the returned pointer will access
 * the actual location represented by @gpu_addr. That is, the return value does
 * not require any offset added to it to access the location specified in
 * @gpu_addr
 *
 * The client must take care to either apply any necessary sync operations when
 * accessing the data, or ensure that the enclosing region was coherent with
 * the GPU, or uncached in the CPU.
 *
 * The refcount on the physical allocations backing the region are taken, so
 * that they do not disappear whilst the client is accessing it. Once the
 * client has finished accessing the memory, it must be released with a call to
 * kbase_phy_alloc_mapping_put()
 *
 * Whilst this is expected to execute quickly (the mapping was already setup
 * when the physical allocation was created), the call is not IRQ-safe due to
 * the region lookup involved.
 *
 * An error code may indicate that:
 * - a userside process has freed the allocation, and so @gpu_addr is no longer
 *   valid
 * - the region containing @gpu_addr does not support a permanent kernel mapping
 */
void *kbase_phy_alloc_mapping_get(struct kbase_context *kctx, u64 gpu_addr,
		struct kbase_vmap_struct **out_kern_mapping);

/**
 * kbase_phy_alloc_mapping_put - Put a reference to the kernel-side mapping of a
 *                               physical allocation
 * @kctx:         The kernel base context associated with the mapping
 * @kern_mapping: Pointer to a struct kbase_phy_alloc_mapping pointer obtained
 *                from a call to kbase_phy_alloc_mapping_get()
 *
 * Releases the reference to the allocations backing @kern_mapping that was
 * obtained through a call to kbase_phy_alloc_mapping_get(). This must be used
 * when the client no longer needs to access the kernel-side CPU pointer.
 *
 * If this was the last reference on the underlying physical allocations, they
 * will go through the normal allocation free steps, which also includes an
 * unmap of the permanent kernel mapping for those allocations.
 *
 * Due to these operations, the function is not IRQ-safe. However it is
 * expected to execute quickly in the normal case, i.e. when the region holding
 * the physical allocation is still present.
 */
void kbase_phy_alloc_mapping_put(struct kbase_context *kctx,
		struct kbase_vmap_struct *kern_mapping);

/**
 * kbase_get_cache_line_alignment - Return cache line alignment
 *
 * @kbdev: Device pointer.
 *
 * Helper function to return the maximum cache line alignment considering
 * both CPU and GPU cache sizes.
 *
 * Return: CPU and GPU cache line alignment, in bytes.
 */
u32 kbase_get_cache_line_alignment(struct kbase_device *kbdev);

#if (KERNEL_VERSION(4, 20, 0) > LINUX_VERSION_CODE)
static inline vm_fault_t vmf_insert_pfn_prot(struct vm_area_struct *vma,
			unsigned long addr, unsigned long pfn, pgprot_t pgprot)
{
	int err;

#if ((KERNEL_VERSION(4, 4, 147) >= LINUX_VERSION_CODE) || \
		((KERNEL_VERSION(4, 6, 0) > LINUX_VERSION_CODE) && \
		 (KERNEL_VERSION(4, 5, 0) <= LINUX_VERSION_CODE)))
	if (pgprot_val(pgprot) != pgprot_val(vma->vm_page_prot))
		return VM_FAULT_SIGBUS;

	err = vm_insert_pfn(vma, addr, pfn);
#else
	err = vm_insert_pfn_prot(vma, addr, pfn, pgprot);
#endif

	if (unlikely(err == -ENOMEM))
		return VM_FAULT_OOM;
	if (unlikely(err < 0 && err != -EBUSY))
		return VM_FAULT_SIGBUS;

	return VM_FAULT_NOPAGE;
}
#endif

/**
 * kbase_mem_get_process_mmap_lock - Return the mmap lock for the current process
 *
 * Return: the mmap lock for the current process
 */
static inline struct rw_semaphore *kbase_mem_get_process_mmap_lock(void)
{
#if KERNEL_VERSION(5, 8, 0) > LINUX_VERSION_CODE
	return &current->mm->mmap_sem;
#else /* KERNEL_VERSION(5, 8, 0) > LINUX_VERSION_CODE */
	return &current->mm->mmap_lock;
#endif /* KERNEL_VERSION(5, 8, 0) > LINUX_VERSION_CODE */
}

#endif				/* _KBASE_MEM_LINUX_H_ */
