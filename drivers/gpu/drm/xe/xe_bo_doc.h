/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_BO_DOC_H_
#define _XE_BO_DOC_H_

/**
 * DOC: Buffer Objects (BO)
 *
 * BO management
 * =============
 *
 * TTM manages (placement, eviction, etc...) all BOs in XE.
 *
 * BO creation
 * ===========
 *
 * Create a chunk of memory which can be used by the GPU. Placement rules
 * (sysmem or vram region) passed in upon creation. TTM handles placement of BO
 * and can trigger eviction of other BOs to make space for the new BO.
 *
 * Kernel BOs
 * ----------
 *
 * A kernel BO is created as part of driver load (e.g. uC firmware images, GuC
 * ADS, etc...) or a BO created as part of a user operation which requires
 * a kernel BO (e.g. engine state, memory for page tables, etc...). These BOs
 * are typically mapped in the GGTT (any kernel BOs aside memory for page tables
 * are in the GGTT), are pinned (can't move or be evicted at runtime), have a
 * vmap (XE can access the memory via xe_map layer) and have contiguous physical
 * memory.
 *
 * More details of why kernel BOs are pinned and contiguous below.
 *
 * User BOs
 * --------
 *
 * A user BO is created via the DRM_IOCTL_XE_GEM_CREATE IOCTL. Once it is
 * created the BO can be mmap'd (via DRM_IOCTL_XE_GEM_MMAP_OFFSET) for user
 * access and it can be bound for GPU access (via DRM_IOCTL_XE_VM_BIND). All
 * user BOs are evictable and user BOs are never pinned by XE. The allocation of
 * the backing store can be deferred from creation time until first use which is
 * either mmap, bind, or pagefault.
 *
 * Private BOs
 * ~~~~~~~~~~~
 *
 * A private BO is a user BO created with a valid VM argument passed into the
 * create IOCTL. If a BO is private it cannot be exported via prime FD and
 * mappings can only be created for the BO within the VM it is tied to. Lastly,
 * the BO dma-resv slots / lock point to the VM's dma-resv slots / lock (all
 * private BOs to a VM share common dma-resv slots / lock).
 *
 * External BOs
 * ~~~~~~~~~~~~
 *
 * An external BO is a user BO created with a NULL VM argument passed into the
 * create IOCTL. An external BO can be shared with different UMDs / devices via
 * prime FD and the BO can be mapped into multiple VMs. An external BO has its
 * own unique dma-resv slots / lock. An external BO will be in an array of all
 * VMs which has a mapping of the BO. This allows VMs to lookup and lock all
 * external BOs mapped in the VM as needed.
 *
 * BO placement
 * ~~~~~~~~~~~~
 *
 * When a user BO is created, a mask of valid placements is passed indicating
 * which memory regions are considered valid.
 *
 * The memory region information is available via query uAPI (TODO: add link).
 *
 * BO validation
 * =============
 *
 * BO validation (ttm_bo_validate) refers to ensuring a BO has a valid
 * placement. If a BO was swapped to temporary storage, a validation call will
 * trigger a move back to a valid (location where GPU can access BO) placement.
 * Validation of a BO may evict other BOs to make room for the BO being
 * validated.
 *
 * BO eviction / moving
 * ====================
 *
 * All eviction (or in other words, moving a BO from one memory location to
 * another) is routed through TTM with a callback into XE.
 *
 * Runtime eviction
 * ----------------
 *
 * Runtime evictions refers to during normal operations where TTM decides it
 * needs to move a BO. Typically this is because TTM needs to make room for
 * another BO and the evicted BO is first BO on LRU list that is not locked.
 *
 * An example of this is a new BO which can only be placed in VRAM but there is
 * not space in VRAM. There could be multiple BOs which have sysmem and VRAM
 * placement rules which currently reside in VRAM, TTM trigger a will move of
 * one (or multiple) of these BO(s) until there is room in VRAM to place the new
 * BO. The evicted BO(s) are valid but still need new bindings before the BO
 * used again (exec or compute mode rebind worker).
 *
 * Another example would be, TTM can't find a BO to evict which has another
 * valid placement. In this case TTM will evict one (or multiple) unlocked BO(s)
 * to a temporary unreachable (invalid) placement. The evicted BO(s) are invalid
 * and before next use need to be moved to a valid placement and rebound.
 *
 * In both cases, moves of these BOs are scheduled behind the fences in the BO's
 * dma-resv slots.
 *
 * WW locking tries to ensures if 2 VMs use 51% of the memory forward progress
 * is made on both VMs.
 *
 * Runtime eviction uses per a GT migration engine (TODO: link to migration
 * engine doc) to do a GPU memcpy from one location to another.
 *
 * Rebinds after runtime eviction
 * ------------------------------
 *
 * When BOs are moved, every mapping (VMA) of the BO needs to rebound before
 * the BO is used again. Every VMA is added to an evicted list of its VM when
 * the BO is moved. This is safe because of the VM locking structure (TODO: link
 * to VM locking doc). On the next use of a VM (exec or compute mode rebind
 * worker) the evicted VMA list is checked and rebinds are triggered. In the
 * case of faulting VM, the rebind is done in the page fault handler.
 *
 * Suspend / resume eviction of VRAM
 * ---------------------------------
 *
 * During device suspend / resume VRAM may lose power which means the contents
 * of VRAM's memory is blown away. Thus BOs present in VRAM at the time of
 * suspend must be moved to sysmem in order for their contents to be saved.
 *
 * A simple TTM call (ttm_resource_manager_evict_all) can move all non-pinned
 * (user) BOs to sysmem. External BOs that are pinned need to be manually
 * evicted with a simple loop + xe_bo_evict call. It gets a little trickier
 * with kernel BOs.
 *
 * Some kernel BOs are used by the GT migration engine to do moves, thus we
 * can't move all of the BOs via the GT migration engine. For simplity, use a
 * TTM memcpy (CPU) to move any kernel (pinned) BO on either suspend or resume.
 *
 * Some kernel BOs need to be restored to the exact same physical location. TTM
 * makes this rather easy but the caveat is the memory must be contiguous. Again
 * for simplity, we enforce that all kernel (pinned) BOs are contiguous and
 * restored to the same physical location.
 *
 * Pinned external BOs in VRAM are restored on resume via the GPU.
 *
 * Rebinds after suspend / resume
 * ------------------------------
 *
 * Most kernel BOs have GGTT mappings which must be restored during the resume
 * process. All user BOs are rebound after validation on their next use.
 *
 * Future work
 * ===========
 *
 * Trim the list of BOs which is saved / restored via TTM memcpy on suspend /
 * resume. All we really need to save / restore via TTM memcpy is the memory
 * required for the GuC to load and the memory for the GT migrate engine to
 * operate.
 *
 * Do not require kernel BOs to be contiguous in physical memory / restored to
 * the same physical address on resume. In all likelihood the only memory that
 * needs to be restored to the same physical address is memory used for page
 * tables. All of that memory is allocated 1 page at time so the contiguous
 * requirement isn't needed. Some work on the vmap code would need to be done if
 * kernel BOs are not contiguous too.
 *
 * Make some kernel BO evictable rather than pinned. An example of this would be
 * engine state, in all likelihood if the dma-slots of these BOs where properly
 * used rather than pinning we could safely evict + rebind these BOs as needed.
 *
 * Some kernel BOs do not need to be restored on resume (e.g. GuC ADS as that is
 * repopulated on resume), add flag to mark such objects as no save / restore.
 */

#endif
