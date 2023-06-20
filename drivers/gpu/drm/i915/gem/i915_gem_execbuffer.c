/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2008,2010 Intel Corporation
 */

#include <linux/dma-resv.h>
#include <linux/highmem.h>
#include <linux/intel-iommu.h>
#include <linux/sync_file.h>
#include <linux/uaccess.h>

#include <drm/drm_syncobj.h>

#include "display/intel_frontbuffer.h"

#include "gem/i915_gem_ioctls.h"
#include "gt/intel_context.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_buffer_pool.h"
#include "gt/intel_gt_pm.h"
#include "gt/intel_ring.h"

#include "pxp/intel_pxp.h"

#include "i915_cmd_parser.h"
#include "i915_drv.h"
#include "i915_file_private.h"
#include "i915_gem_clflush.h"
#include "i915_gem_context.h"
#include "i915_gem_evict.h"
#include "i915_gem_ioctls.h"
#include "i915_trace.h"
#include "i915_user_extensions.h"

struct eb_vma {
	struct i915_vma *vma;
	unsigned int flags;

	/** This vma's place in the execbuf reservation list */
	struct drm_i915_gem_exec_object2 *exec;
	struct list_head bind_link;
	struct list_head reloc_link;

	struct hlist_node node;
	u32 handle;
};

enum {
	FORCE_CPU_RELOC = 1,
	FORCE_GTT_RELOC,
	FORCE_GPU_RELOC,
#define DBG_FORCE_RELOC 0 /* choose one of the above! */
};

/* __EXEC_OBJECT_NO_RESERVE is BIT(31), defined in i915_vma.h */
#define __EXEC_OBJECT_HAS_PIN		BIT(30)
#define __EXEC_OBJECT_HAS_FENCE		BIT(29)
#define __EXEC_OBJECT_USERPTR_INIT	BIT(28)
#define __EXEC_OBJECT_NEEDS_MAP		BIT(27)
#define __EXEC_OBJECT_NEEDS_BIAS	BIT(26)
#define __EXEC_OBJECT_INTERNAL_FLAGS	(~0u << 26) /* all of the above + */
#define __EXEC_OBJECT_RESERVED (__EXEC_OBJECT_HAS_PIN | __EXEC_OBJECT_HAS_FENCE)

#define __EXEC_HAS_RELOC	BIT(31)
#define __EXEC_ENGINE_PINNED	BIT(30)
#define __EXEC_USERPTR_USED	BIT(29)
#define __EXEC_INTERNAL_FLAGS	(~0u << 29)
#define UPDATE			PIN_OFFSET_FIXED

#define BATCH_OFFSET_BIAS (256*1024)

#define __I915_EXEC_ILLEGAL_FLAGS \
	(__I915_EXEC_UNKNOWN_FLAGS | \
	 I915_EXEC_CONSTANTS_MASK  | \
	 I915_EXEC_RESOURCE_STREAMER)

/* Catch emission of unexpected errors for CI! */
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
#undef EINVAL
#define EINVAL ({ \
	DRM_DEBUG_DRIVER("EINVAL at %s:%d\n", __func__, __LINE__); \
	22; \
})
#endif

/**
 * DOC: User command execution
 *
 * Userspace submits commands to be executed on the GPU as an instruction
 * stream within a GEM object we call a batchbuffer. This instructions may
 * refer to other GEM objects containing auxiliary state such as kernels,
 * samplers, render targets and even secondary batchbuffers. Userspace does
 * not know where in the GPU memory these objects reside and so before the
 * batchbuffer is passed to the GPU for execution, those addresses in the
 * batchbuffer and auxiliary objects are updated. This is known as relocation,
 * or patching. To try and avoid having to relocate each object on the next
 * execution, userspace is told the location of those objects in this pass,
 * but this remains just a hint as the kernel may choose a new location for
 * any object in the future.
 *
 * At the level of talking to the hardware, submitting a batchbuffer for the
 * GPU to execute is to add content to a buffer from which the HW
 * command streamer is reading.
 *
 * 1. Add a command to load the HW context. For Logical Ring Contexts, i.e.
 *    Execlists, this command is not placed on the same buffer as the
 *    remaining items.
 *
 * 2. Add a command to invalidate caches to the buffer.
 *
 * 3. Add a batchbuffer start command to the buffer; the start command is
 *    essentially a token together with the GPU address of the batchbuffer
 *    to be executed.
 *
 * 4. Add a pipeline flush to the buffer.
 *
 * 5. Add a memory write command to the buffer to record when the GPU
 *    is done executing the batchbuffer. The memory write writes the
 *    global sequence number of the request, ``i915_request::global_seqno``;
 *    the i915 driver uses the current value in the register to determine
 *    if the GPU has completed the batchbuffer.
 *
 * 6. Add a user interrupt command to the buffer. This command instructs
 *    the GPU to issue an interrupt when the command, pipeline flush and
 *    memory write are completed.
 *
 * 7. Inform the hardware of the additional commands added to the buffer
 *    (by updating the tail pointer).
 *
 * Processing an execbuf ioctl is conceptually split up into a few phases.
 *
 * 1. Validation - Ensure all the pointers, handles and flags are valid.
 * 2. Reservation - Assign GPU address space for every object
 * 3. Relocation - Update any addresses to point to the final locations
 * 4. Serialisation - Order the request with respect to its dependencies
 * 5. Construction - Construct a request to execute the batchbuffer
 * 6. Submission (at some point in the future execution)
 *
 * Reserving resources for the execbuf is the most complicated phase. We
 * neither want to have to migrate the object in the address space, nor do
 * we want to have to update any relocations pointing to this object. Ideally,
 * we want to leave the object where it is and for all the existing relocations
 * to match. If the object is given a new address, or if userspace thinks the
 * object is elsewhere, we have to parse all the relocation entries and update
 * the addresses. Userspace can set the I915_EXEC_NORELOC flag to hint that
 * all the target addresses in all of its objects match the value in the
 * relocation entries and that they all match the presumed offsets given by the
 * list of execbuffer objects. Using this knowledge, we know that if we haven't
 * moved any buffers, all the relocation entries are valid and we can skip
 * the update. (If userspace is wrong, the likely outcome is an impromptu GPU
 * hang.) The requirement for using I915_EXEC_NO_RELOC are:
 *
 *      The addresses written in the objects must match the corresponding
 *      reloc.presumed_offset which in turn must match the corresponding
 *      execobject.offset.
 *
 *      Any render targets written to in the batch must be flagged with
 *      EXEC_OBJECT_WRITE.
 *
 *      To avoid stalling, execobject.offset should match the current
 *      address of that object within the active context.
 *
 * The reservation is done is multiple phases. First we try and keep any
 * object already bound in its current location - so as long as meets the
 * constraints imposed by the new execbuffer. Any object left unbound after the
 * first pass is then fitted into any available idle space. If an object does
 * not fit, all objects are removed from the reservation and the process rerun
 * after sorting the objects into a priority order (more difficult to fit
 * objects are tried first). Failing that, the entire VM is cleared and we try
 * to fit the execbuf once last time before concluding that it simply will not
 * fit.
 *
 * A small complication to all of this is that we allow userspace not only to
 * specify an alignment and a size for the object in the address space, but
 * we also allow userspace to specify the exact offset. This objects are
 * simpler to place (the location is known a priori) all we have to do is make
 * sure the space is available.
 *
 * Once all the objects are in place, patching up the buried pointers to point
 * to the final locations is a fairly simple job of walking over the relocation
 * entry arrays, looking up the right address and rewriting the value into
 * the object. Simple! ... The relocation entries are stored in user memory
 * and so to access them we have to copy them into a local buffer. That copy
 * has to avoid taking any pagefaults as they may lead back to a GEM object
 * requiring the struct_mutex (i.e. recursive deadlock). So once again we split
 * the relocation into multiple passes. First we try to do everything within an
 * atomic context (avoid the pagefaults) which requires that we never wait. If
 * we detect that we may wait, or if we need to fault, then we have to fallback
 * to a slower path. The slowpath has to drop the mutex. (Can you hear alarm
 * bells yet?) Dropping the mutex means that we lose all the state we have
 * built up so far for the execbuf and we must reset any global data. However,
 * we do leave the objects pinned in their final locations - which is a
 * potential issue for concurrent execbufs. Once we have left the mutex, we can
 * allocate and copy all the relocation entries into a large array at our
 * leisure, reacquire the mutex, reclaim all the objects and other state and
 * then proceed to update any incorrect addresses with the objects.
 *
 * As we process the relocation entries, we maintain a record of whether the
 * object is being written to. Using NORELOC, we expect userspace to provide
 * this information instead. We also check whether we can skip the relocation
 * by comparing the expected value inside the relocation entry with the target's
 * final address. If they differ, we have to map the current object and rewrite
 * the 4 or 8 byte pointer within.
 *
 * Serialising an execbuf is quite simple according to the rules of the GEM
 * ABI. Execution within each context is ordered by the order of submission.
 * Writes to any GEM object are in order of submission and are exclusive. Reads
 * from a GEM object are unordered with respect to other reads, but ordered by
 * writes. A write submitted after a read cannot occur before the read, and
 * similarly any read submitted after a write cannot occur before the write.
 * Writes are ordered between engines such that only one write occurs at any
 * time (completing any reads beforehand) - using semaphores where available
 * and CPU serialisation otherwise. Other GEM access obey the same rules, any
 * write (either via mmaps using set-domain, or via pwrite) must flush all GPU
 * reads before starting, and any read (either using set-domain or pread) must
 * flush all GPU writes before starting. (Note we only employ a barrier before,
 * we currently rely on userspace not concurrently starting a new execution
 * whilst reading or writing to an object. This may be an advantage or not
 * depending on how much you trust userspace not to shoot themselves in the
 * foot.) Serialisation may just result in the request being inserted into
 * a DAG awaiting its turn, but most simple is to wait on the CPU until
 * all dependencies are resolved.
 *
 * After all of that, is just a matter of closing the request and handing it to
 * the hardware (well, leaving it in a queue to be executed). However, we also
 * offer the ability for batchbuffers to be run with elevated privileges so
 * that they access otherwise hidden registers. (Used to adjust L3 cache etc.)
 * Before any batch is given extra privileges we first must check that it
 * contains no nefarious instructions, we check that each instruction is from
 * our whitelist and all registers are also from an allowed list. We first
 * copy the user's batchbuffer to a shadow (so that the user doesn't have
 * access to it, either by the CPU or GPU as we scan it) and then parse each
 * instruction. If everything is ok, we set a flag telling the hardware to run
 * the batchbuffer in trusted mode, otherwise the ioctl is rejected.
 */

struct eb_fence {
	struct drm_syncobj *syncobj; /* Use with ptr_mask_bits() */
	struct dma_fence *dma_fence;
	u64 value;
	struct dma_fence_chain *chain_fence;
};

struct i915_execbuffer {
	struct drm_i915_private *i915; /** i915 backpointer */
	struct drm_file *file; /** per-file lookup tables and limits */
	struct drm_i915_gem_execbuffer2 *args; /** ioctl parameters */
	struct drm_i915_gem_exec_object2 *exec; /** ioctl execobj[] */
	struct eb_vma *vma;

	struct intel_gt *gt; /* gt for the execbuf */
	struct intel_context *context; /* logical state for the request */
	struct i915_gem_context *gem_context; /** caller's context */

	/** our requests to build */
	struct i915_request *requests[MAX_ENGINE_INSTANCE + 1];
	/** identity of the batch obj/vma */
	struct eb_vma *batches[MAX_ENGINE_INSTANCE + 1];
	struct i915_vma *trampoline; /** trampoline used for chaining */

	/** used for excl fence in dma_resv objects when > 1 BB submitted */
	struct dma_fence *composite_fence;

	/** actual size of execobj[] as we may extend it for the cmdparser */
	unsigned int buffer_count;

	/* number of batches in execbuf IOCTL */
	unsigned int num_batches;

	/** list of vma not yet bound during reservation phase */
	struct list_head unbound;

	/** list of vma that have execobj.relocation_count */
	struct list_head relocs;

	struct i915_gem_ww_ctx ww;

	/**
	 * Track the most recently used object for relocations, as we
	 * frequently have to perform multiple relocations within the same
	 * obj/page
	 */
	struct reloc_cache {
		struct drm_mm_node node; /** temporary GTT binding */
		unsigned long vaddr; /** Current kmap address */
		unsigned long page; /** Currently mapped page index */
		unsigned int graphics_ver; /** Cached value of GRAPHICS_VER */
		bool use_64bit_reloc : 1;
		bool has_llc : 1;
		bool has_fence : 1;
		bool needs_unfenced : 1;
	} reloc_cache;

	u64 invalid_flags; /** Set of execobj.flags that are invalid */

	/** Length of batch within object */
	u64 batch_len[MAX_ENGINE_INSTANCE + 1];
	u32 batch_start_offset; /** Location within object of batch */
	u32 batch_flags; /** Flags composed for emit_bb_start() */
	struct intel_gt_buffer_pool_node *batch_pool; /** pool node for batch buffer */

	/**
	 * Indicate either the size of the hastable used to resolve
	 * relocation handles, or if negative that we are using a direct
	 * index into the execobj[].
	 */
	int lut_size;
	struct hlist_head *buckets; /** ht for relocation handles */

	struct eb_fence *fences;
	unsigned long num_fences;
#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)
	struct i915_capture_list *capture_lists[MAX_ENGINE_INSTANCE + 1];
#endif
};

static int eb_parse(struct i915_execbuffer *eb);
static int eb_pin_engine(struct i915_execbuffer *eb, bool throttle);
static void eb_unpin_engine(struct i915_execbuffer *eb);
static void eb_capture_release(struct i915_execbuffer *eb);

static inline bool eb_use_cmdparser(const struct i915_execbuffer *eb)
{
	return intel_engine_requires_cmd_parser(eb->context->engine) ||
		(intel_engine_using_cmd_parser(eb->context->engine) &&
		 eb->args->batch_len);
}

static int eb_create(struct i915_execbuffer *eb)
{
	if (!(eb->args->flags & I915_EXEC_HANDLE_LUT)) {
		unsigned int size = 1 + ilog2(eb->buffer_count);

		/*
		 * Without a 1:1 association between relocation handles and
		 * the execobject[] index, we instead create a hashtable.
		 * We size it dynamically based on available memory, starting
		 * first with 1:1 assocative hash and scaling back until
		 * the allocation succeeds.
		 *
		 * Later on we use a positive lut_size to indicate we are
		 * using this hashtable, and a negative value to indicate a
		 * direct lookup.
		 */
		do {
			gfp_t flags;

			/* While we can still reduce the allocation size, don't
			 * raise a warning and allow the allocation to fail.
			 * On the last pass though, we want to try as hard
			 * as possible to perform the allocation and warn
			 * if it fails.
			 */
			flags = GFP_KERNEL;
			if (size > 1)
				flags |= __GFP_NORETRY | __GFP_NOWARN;

			eb->buckets = kzalloc(sizeof(struct hlist_head) << size,
					      flags);
			if (eb->buckets)
				break;
		} while (--size);

		if (unlikely(!size))
			return -ENOMEM;

		eb->lut_size = size;
	} else {
		eb->lut_size = -eb->buffer_count;
	}

	return 0;
}

static bool
eb_vma_misplaced(const struct drm_i915_gem_exec_object2 *entry,
		 const struct i915_vma *vma,
		 unsigned int flags)
{
	if (vma->node.size < entry->pad_to_size)
		return true;

	if (entry->alignment && !IS_ALIGNED(vma->node.start, entry->alignment))
		return true;

	if (flags & EXEC_OBJECT_PINNED &&
	    vma->node.start != entry->offset)
		return true;

	if (flags & __EXEC_OBJECT_NEEDS_BIAS &&
	    vma->node.start < BATCH_OFFSET_BIAS)
		return true;

	if (!(flags & EXEC_OBJECT_SUPPORTS_48B_ADDRESS) &&
	    (vma->node.start + vma->node.size + 4095) >> 32)
		return true;

	if (flags & __EXEC_OBJECT_NEEDS_MAP &&
	    !i915_vma_is_map_and_fenceable(vma))
		return true;

	return false;
}

static u64 eb_pin_flags(const struct drm_i915_gem_exec_object2 *entry,
			unsigned int exec_flags)
{
	u64 pin_flags = 0;

	if (exec_flags & EXEC_OBJECT_NEEDS_GTT)
		pin_flags |= PIN_GLOBAL;

	/*
	 * Wa32bitGeneralStateOffset & Wa32bitInstructionBaseOffset,
	 * limit address to the first 4GBs for unflagged objects.
	 */
	if (!(exec_flags & EXEC_OBJECT_SUPPORTS_48B_ADDRESS))
		pin_flags |= PIN_ZONE_4G;

	if (exec_flags & __EXEC_OBJECT_NEEDS_MAP)
		pin_flags |= PIN_MAPPABLE;

	if (exec_flags & EXEC_OBJECT_PINNED)
		pin_flags |= entry->offset | PIN_OFFSET_FIXED;
	else if (exec_flags & __EXEC_OBJECT_NEEDS_BIAS)
		pin_flags |= BATCH_OFFSET_BIAS | PIN_OFFSET_BIAS;

	return pin_flags;
}

static inline int
eb_pin_vma(struct i915_execbuffer *eb,
	   const struct drm_i915_gem_exec_object2 *entry,
	   struct eb_vma *ev)
{
	struct i915_vma *vma = ev->vma;
	u64 pin_flags;
	int err;

	if (vma->node.size)
		pin_flags = vma->node.start;
	else
		pin_flags = entry->offset & PIN_OFFSET_MASK;

	pin_flags |= PIN_USER | PIN_NOEVICT | PIN_OFFSET_FIXED | PIN_VALIDATE;
	if (unlikely(ev->flags & EXEC_OBJECT_NEEDS_GTT))
		pin_flags |= PIN_GLOBAL;

	/* Attempt to reuse the current location if available */
	err = i915_vma_pin_ww(vma, &eb->ww, 0, 0, pin_flags);
	if (err == -EDEADLK)
		return err;

	if (unlikely(err)) {
		if (entry->flags & EXEC_OBJECT_PINNED)
			return err;

		/* Failing that pick any _free_ space if suitable */
		err = i915_vma_pin_ww(vma, &eb->ww,
					     entry->pad_to_size,
					     entry->alignment,
					     eb_pin_flags(entry, ev->flags) |
					     PIN_USER | PIN_NOEVICT | PIN_VALIDATE);
		if (unlikely(err))
			return err;
	}

	if (unlikely(ev->flags & EXEC_OBJECT_NEEDS_FENCE)) {
		err = i915_vma_pin_fence(vma);
		if (unlikely(err))
			return err;

		if (vma->fence)
			ev->flags |= __EXEC_OBJECT_HAS_FENCE;
	}

	ev->flags |= __EXEC_OBJECT_HAS_PIN;
	if (eb_vma_misplaced(entry, vma, ev->flags))
		return -EBADSLT;

	return 0;
}

static inline void
eb_unreserve_vma(struct eb_vma *ev)
{
	if (unlikely(ev->flags & __EXEC_OBJECT_HAS_FENCE))
		__i915_vma_unpin_fence(ev->vma);

	ev->flags &= ~__EXEC_OBJECT_RESERVED;
}

static int
eb_validate_vma(struct i915_execbuffer *eb,
		struct drm_i915_gem_exec_object2 *entry,
		struct i915_vma *vma)
{
	/* Relocations are disallowed for all platforms after TGL-LP.  This
	 * also covers all platforms with local memory.
	 */
	if (entry->relocation_count &&
	    GRAPHICS_VER(eb->i915) >= 12 && !IS_TIGERLAKE(eb->i915))
		return -EINVAL;

	if (unlikely(entry->flags & eb->invalid_flags))
		return -EINVAL;

	if (unlikely(entry->alignment &&
		     !is_power_of_2_u64(entry->alignment)))
		return -EINVAL;

	/*
	 * Offset can be used as input (EXEC_OBJECT_PINNED), reject
	 * any non-page-aligned or non-canonical addresses.
	 */
	if (unlikely(entry->flags & EXEC_OBJECT_PINNED &&
		     entry->offset != gen8_canonical_addr(entry->offset & I915_GTT_PAGE_MASK)))
		return -EINVAL;

	/* pad_to_size was once a reserved field, so sanitize it */
	if (entry->flags & EXEC_OBJECT_PAD_TO_SIZE) {
		if (unlikely(offset_in_page(entry->pad_to_size)))
			return -EINVAL;
	} else {
		entry->pad_to_size = 0;
	}
	/*
	 * From drm_mm perspective address space is continuous,
	 * so from this point we're always using non-canonical
	 * form internally.
	 */
	entry->offset = gen8_noncanonical_addr(entry->offset);

	if (!eb->reloc_cache.has_fence) {
		entry->flags &= ~EXEC_OBJECT_NEEDS_FENCE;
	} else {
		if ((entry->flags & EXEC_OBJECT_NEEDS_FENCE ||
		     eb->reloc_cache.needs_unfenced) &&
		    i915_gem_object_is_tiled(vma->obj))
			entry->flags |= EXEC_OBJECT_NEEDS_GTT | __EXEC_OBJECT_NEEDS_MAP;
	}

	return 0;
}

static inline bool
is_batch_buffer(struct i915_execbuffer *eb, unsigned int buffer_idx)
{
	return eb->args->flags & I915_EXEC_BATCH_FIRST ?
		buffer_idx < eb->num_batches :
		buffer_idx >= eb->args->buffer_count - eb->num_batches;
}

static int
eb_add_vma(struct i915_execbuffer *eb,
	   unsigned int *current_batch,
	   unsigned int i,
	   struct i915_vma *vma)
{
	struct drm_i915_private *i915 = eb->i915;
	struct drm_i915_gem_exec_object2 *entry = &eb->exec[i];
	struct eb_vma *ev = &eb->vma[i];

	ev->vma = vma;
	ev->exec = entry;
	ev->flags = entry->flags;

	if (eb->lut_size > 0) {
		ev->handle = entry->handle;
		hlist_add_head(&ev->node,
			       &eb->buckets[hash_32(entry->handle,
						    eb->lut_size)]);
	}

	if (entry->relocation_count)
		list_add_tail(&ev->reloc_link, &eb->relocs);

	/*
	 * SNA is doing fancy tricks with compressing batch buffers, which leads
	 * to negative relocation deltas. Usually that works out ok since the
	 * relocate address is still positive, except when the batch is placed
	 * very low in the GTT. Ensure this doesn't happen.
	 *
	 * Note that actual hangs have only been observed on gen7, but for
	 * paranoia do it everywhere.
	 */
	if (is_batch_buffer(eb, i)) {
		if (entry->relocation_count &&
		    !(ev->flags & EXEC_OBJECT_PINNED))
			ev->flags |= __EXEC_OBJECT_NEEDS_BIAS;
		if (eb->reloc_cache.has_fence)
			ev->flags |= EXEC_OBJECT_NEEDS_FENCE;

		eb->batches[*current_batch] = ev;

		if (unlikely(ev->flags & EXEC_OBJECT_WRITE)) {
			drm_dbg(&i915->drm,
				"Attempting to use self-modifying batch buffer\n");
			return -EINVAL;
		}

		if (range_overflows_t(u64,
				      eb->batch_start_offset,
				      eb->args->batch_len,
				      ev->vma->size)) {
			drm_dbg(&i915->drm, "Attempting to use out-of-bounds batch\n");
			return -EINVAL;
		}

		if (eb->args->batch_len == 0)
			eb->batch_len[*current_batch] = ev->vma->size -
				eb->batch_start_offset;
		else
			eb->batch_len[*current_batch] = eb->args->batch_len;
		if (unlikely(eb->batch_len[*current_batch] == 0)) { /* impossible! */
			drm_dbg(&i915->drm, "Invalid batch length\n");
			return -EINVAL;
		}

		++*current_batch;
	}

	return 0;
}

static inline int use_cpu_reloc(const struct reloc_cache *cache,
				const struct drm_i915_gem_object *obj)
{
	if (!i915_gem_object_has_struct_page(obj))
		return false;

	if (DBG_FORCE_RELOC == FORCE_CPU_RELOC)
		return true;

	if (DBG_FORCE_RELOC == FORCE_GTT_RELOC)
		return false;

	return (cache->has_llc ||
		obj->cache_dirty ||
		obj->cache_level != I915_CACHE_NONE);
}

static int eb_reserve_vma(struct i915_execbuffer *eb,
			  struct eb_vma *ev,
			  u64 pin_flags)
{
	struct drm_i915_gem_exec_object2 *entry = ev->exec;
	struct i915_vma *vma = ev->vma;
	int err;

	if (drm_mm_node_allocated(&vma->node) &&
	    eb_vma_misplaced(entry, vma, ev->flags)) {
		err = i915_vma_unbind(vma);
		if (err)
			return err;
	}

	err = i915_vma_pin_ww(vma, &eb->ww,
			   entry->pad_to_size, entry->alignment,
			   eb_pin_flags(entry, ev->flags) | pin_flags);
	if (err)
		return err;

	if (entry->offset != vma->node.start) {
		entry->offset = vma->node.start | UPDATE;
		eb->args->flags |= __EXEC_HAS_RELOC;
	}

	if (unlikely(ev->flags & EXEC_OBJECT_NEEDS_FENCE)) {
		err = i915_vma_pin_fence(vma);
		if (unlikely(err))
			return err;

		if (vma->fence)
			ev->flags |= __EXEC_OBJECT_HAS_FENCE;
	}

	ev->flags |= __EXEC_OBJECT_HAS_PIN;
	GEM_BUG_ON(eb_vma_misplaced(entry, vma, ev->flags));

	return 0;
}

static bool eb_unbind(struct i915_execbuffer *eb, bool force)
{
	const unsigned int count = eb->buffer_count;
	unsigned int i;
	struct list_head last;
	bool unpinned = false;

	/* Resort *all* the objects into priority order */
	INIT_LIST_HEAD(&eb->unbound);
	INIT_LIST_HEAD(&last);

	for (i = 0; i < count; i++) {
		struct eb_vma *ev = &eb->vma[i];
		unsigned int flags = ev->flags;

		if (!force && flags & EXEC_OBJECT_PINNED &&
		    flags & __EXEC_OBJECT_HAS_PIN)
			continue;

		unpinned = true;
		eb_unreserve_vma(ev);

		if (flags & EXEC_OBJECT_PINNED)
			/* Pinned must have their slot */
			list_add(&ev->bind_link, &eb->unbound);
		else if (flags & __EXEC_OBJECT_NEEDS_MAP)
			/* Map require the lowest 256MiB (aperture) */
			list_add_tail(&ev->bind_link, &eb->unbound);
		else if (!(flags & EXEC_OBJECT_SUPPORTS_48B_ADDRESS))
			/* Prioritise 4GiB region for restricted bo */
			list_add(&ev->bind_link, &last);
		else
			list_add_tail(&ev->bind_link, &last);
	}

	list_splice_tail(&last, &eb->unbound);
	return unpinned;
}

static int eb_reserve(struct i915_execbuffer *eb)
{
	struct eb_vma *ev;
	unsigned int pass;
	int err = 0;
	bool unpinned;

	/*
	 * Attempt to pin all of the buffers into the GTT.
	 * This is done in 2 phases:
	 *
	 * 1. Unbind all objects that do not match the GTT constraints for
	 *    the execbuffer (fenceable, mappable, alignment etc).
	 * 2. Bind new objects.
	 *
	 * This avoid unnecessary unbinding of later objects in order to make
	 * room for the earlier objects *unless* we need to defragment.
	 *
	 * Defragmenting is skipped if all objects are pinned at a fixed location.
	 */
	for (pass = 0; pass <= 2; pass++) {
		int pin_flags = PIN_USER | PIN_VALIDATE;

		if (pass == 0)
			pin_flags |= PIN_NONBLOCK;

		if (pass >= 1)
			unpinned = eb_unbind(eb, pass == 2);

		if (pass == 2) {
			err = mutex_lock_interruptible(&eb->context->vm->mutex);
			if (!err) {
				err = i915_gem_evict_vm(eb->context->vm, &eb->ww);
				mutex_unlock(&eb->context->vm->mutex);
			}
			if (err)
				return err;
		}

		list_for_each_entry(ev, &eb->unbound, bind_link) {
			err = eb_reserve_vma(eb, ev, pin_flags);
			if (err)
				break;
		}

		if (err != -ENOSPC)
			break;
	}

	return err;
}

static int eb_select_context(struct i915_execbuffer *eb)
{
	struct i915_gem_context *ctx;

	ctx = i915_gem_context_lookup(eb->file->driver_priv, eb->args->rsvd1);
	if (unlikely(IS_ERR(ctx)))
		return PTR_ERR(ctx);

	eb->gem_context = ctx;
	if (i915_gem_context_has_full_ppgtt(ctx))
		eb->invalid_flags |= EXEC_OBJECT_NEEDS_GTT;

	return 0;
}

static int __eb_add_lut(struct i915_execbuffer *eb,
			u32 handle, struct i915_vma *vma)
{
	struct i915_gem_context *ctx = eb->gem_context;
	struct i915_lut_handle *lut;
	int err;

	lut = i915_lut_handle_alloc();
	if (unlikely(!lut))
		return -ENOMEM;

	i915_vma_get(vma);
	if (!atomic_fetch_inc(&vma->open_count))
		i915_vma_reopen(vma);
	lut->handle = handle;
	lut->ctx = ctx;

	/* Check that the context hasn't been closed in the meantime */
	err = -EINTR;
	if (!mutex_lock_interruptible(&ctx->lut_mutex)) {
		if (likely(!i915_gem_context_is_closed(ctx)))
			err = radix_tree_insert(&ctx->handles_vma, handle, vma);
		else
			err = -ENOENT;
		if (err == 0) { /* And nor has this handle */
			struct drm_i915_gem_object *obj = vma->obj;

			spin_lock(&obj->lut_lock);
			if (idr_find(&eb->file->object_idr, handle) == obj) {
				list_add(&lut->obj_link, &obj->lut_list);
			} else {
				radix_tree_delete(&ctx->handles_vma, handle);
				err = -ENOENT;
			}
			spin_unlock(&obj->lut_lock);
		}
		mutex_unlock(&ctx->lut_mutex);
	}
	if (unlikely(err))
		goto err;

	return 0;

err:
	i915_vma_close(vma);
	i915_vma_put(vma);
	i915_lut_handle_free(lut);
	return err;
}

static struct i915_vma *eb_lookup_vma(struct i915_execbuffer *eb, u32 handle)
{
	struct i915_address_space *vm = eb->context->vm;

	do {
		struct drm_i915_gem_object *obj;
		struct i915_vma *vma;
		int err;

		rcu_read_lock();
		vma = radix_tree_lookup(&eb->gem_context->handles_vma, handle);
		if (likely(vma && vma->vm == vm))
			vma = i915_vma_tryget(vma);
		rcu_read_unlock();
		if (likely(vma))
			return vma;

		obj = i915_gem_object_lookup(eb->file, handle);
		if (unlikely(!obj))
			return ERR_PTR(-ENOENT);

		/*
		 * If the user has opted-in for protected-object tracking, make
		 * sure the object encryption can be used.
		 * We only need to do this when the object is first used with
		 * this context, because the context itself will be banned when
		 * the protected objects become invalid.
		 */
		if (i915_gem_context_uses_protected_content(eb->gem_context) &&
		    i915_gem_object_is_protected(obj)) {
			err = intel_pxp_key_check(&vm->gt->pxp, obj, true);
			if (err) {
				i915_gem_object_put(obj);
				return ERR_PTR(err);
			}
		}

		vma = i915_vma_instance(obj, vm, NULL);
		if (IS_ERR(vma)) {
			i915_gem_object_put(obj);
			return vma;
		}

		err = __eb_add_lut(eb, handle, vma);
		if (likely(!err))
			return vma;

		i915_gem_object_put(obj);
		if (err != -EEXIST)
			return ERR_PTR(err);
	} while (1);
}

static int eb_lookup_vmas(struct i915_execbuffer *eb)
{
	unsigned int i, current_batch = 0;
	int err = 0;

	INIT_LIST_HEAD(&eb->relocs);

	for (i = 0; i < eb->buffer_count; i++) {
		struct i915_vma *vma;

		vma = eb_lookup_vma(eb, eb->exec[i].handle);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto err;
		}

		err = eb_validate_vma(eb, &eb->exec[i], vma);
		if (unlikely(err)) {
			i915_vma_put(vma);
			goto err;
		}

		err = eb_add_vma(eb, &current_batch, i, vma);
		if (err)
			return err;

		if (i915_gem_object_is_userptr(vma->obj)) {
			err = i915_gem_object_userptr_submit_init(vma->obj);
			if (err) {
				if (i + 1 < eb->buffer_count) {
					/*
					 * Execbuffer code expects last vma entry to be NULL,
					 * since we already initialized this entry,
					 * set the next value to NULL or we mess up
					 * cleanup handling.
					 */
					eb->vma[i + 1].vma = NULL;
				}

				return err;
			}

			eb->vma[i].flags |= __EXEC_OBJECT_USERPTR_INIT;
			eb->args->flags |= __EXEC_USERPTR_USED;
		}
	}

	return 0;

err:
	eb->vma[i].vma = NULL;
	return err;
}

static int eb_lock_vmas(struct i915_execbuffer *eb)
{
	unsigned int i;
	int err;

	for (i = 0; i < eb->buffer_count; i++) {
		struct eb_vma *ev = &eb->vma[i];
		struct i915_vma *vma = ev->vma;

		err = i915_gem_object_lock(vma->obj, &eb->ww);
		if (err)
			return err;
	}

	return 0;
}

static int eb_validate_vmas(struct i915_execbuffer *eb)
{
	unsigned int i;
	int err;

	INIT_LIST_HEAD(&eb->unbound);

	err = eb_lock_vmas(eb);
	if (err)
		return err;

	for (i = 0; i < eb->buffer_count; i++) {
		struct drm_i915_gem_exec_object2 *entry = &eb->exec[i];
		struct eb_vma *ev = &eb->vma[i];
		struct i915_vma *vma = ev->vma;

		err = eb_pin_vma(eb, entry, ev);
		if (err == -EDEADLK)
			return err;

		if (!err) {
			if (entry->offset != vma->node.start) {
				entry->offset = vma->node.start | UPDATE;
				eb->args->flags |= __EXEC_HAS_RELOC;
			}
		} else {
			eb_unreserve_vma(ev);

			list_add_tail(&ev->bind_link, &eb->unbound);
			if (drm_mm_node_allocated(&vma->node)) {
				err = i915_vma_unbind(vma);
				if (err)
					return err;
			}
		}

		/* Reserve enough slots to accommodate composite fences */
		err = dma_resv_reserve_fences(vma->obj->base.resv, eb->num_batches);
		if (err)
			return err;

		GEM_BUG_ON(drm_mm_node_allocated(&vma->node) &&
			   eb_vma_misplaced(&eb->exec[i], vma, ev->flags));
	}

	if (!list_empty(&eb->unbound))
		return eb_reserve(eb);

	return 0;
}

static struct eb_vma *
eb_get_vma(const struct i915_execbuffer *eb, unsigned long handle)
{
	if (eb->lut_size < 0) {
		if (handle >= -eb->lut_size)
			return NULL;
		return &eb->vma[handle];
	} else {
		struct hlist_head *head;
		struct eb_vma *ev;

		head = &eb->buckets[hash_32(handle, eb->lut_size)];
		hlist_for_each_entry(ev, head, node) {
			if (ev->handle == handle)
				return ev;
		}
		return NULL;
	}
}

static void eb_release_vmas(struct i915_execbuffer *eb, bool final)
{
	const unsigned int count = eb->buffer_count;
	unsigned int i;

	for (i = 0; i < count; i++) {
		struct eb_vma *ev = &eb->vma[i];
		struct i915_vma *vma = ev->vma;

		if (!vma)
			break;

		eb_unreserve_vma(ev);

		if (final)
			i915_vma_put(vma);
	}

	eb_capture_release(eb);
	eb_unpin_engine(eb);
}

static void eb_destroy(const struct i915_execbuffer *eb)
{
	if (eb->lut_size > 0)
		kfree(eb->buckets);
}

static inline u64
relocation_target(const struct drm_i915_gem_relocation_entry *reloc,
		  const struct i915_vma *target)
{
	return gen8_canonical_addr((int)reloc->delta + target->node.start);
}

static void reloc_cache_init(struct reloc_cache *cache,
			     struct drm_i915_private *i915)
{
	cache->page = -1;
	cache->vaddr = 0;
	/* Must be a variable in the struct to allow GCC to unroll. */
	cache->graphics_ver = GRAPHICS_VER(i915);
	cache->has_llc = HAS_LLC(i915);
	cache->use_64bit_reloc = HAS_64BIT_RELOC(i915);
	cache->has_fence = cache->graphics_ver < 4;
	cache->needs_unfenced = INTEL_INFO(i915)->unfenced_needs_alignment;
	cache->node.flags = 0;
}

static inline void *unmask_page(unsigned long p)
{
	return (void *)(uintptr_t)(p & PAGE_MASK);
}

static inline unsigned int unmask_flags(unsigned long p)
{
	return p & ~PAGE_MASK;
}

#define KMAP 0x4 /* after CLFLUSH_FLAGS */

static inline struct i915_ggtt *cache_to_ggtt(struct reloc_cache *cache)
{
	struct drm_i915_private *i915 =
		container_of(cache, struct i915_execbuffer, reloc_cache)->i915;
	return to_gt(i915)->ggtt;
}

static void reloc_cache_unmap(struct reloc_cache *cache)
{
	void *vaddr;

	if (!cache->vaddr)
		return;

	vaddr = unmask_page(cache->vaddr);
	if (cache->vaddr & KMAP)
		kunmap_atomic(vaddr);
	else
		io_mapping_unmap_atomic((void __iomem *)vaddr);
}

static void reloc_cache_remap(struct reloc_cache *cache,
			      struct drm_i915_gem_object *obj)
{
	void *vaddr;

	if (!cache->vaddr)
		return;

	if (cache->vaddr & KMAP) {
		struct page *page = i915_gem_object_get_page(obj, cache->page);

		vaddr = kmap_atomic(page);
		cache->vaddr = unmask_flags(cache->vaddr) |
			(unsigned long)vaddr;
	} else {
		struct i915_ggtt *ggtt = cache_to_ggtt(cache);
		unsigned long offset;

		offset = cache->node.start;
		if (!drm_mm_node_allocated(&cache->node))
			offset += cache->page << PAGE_SHIFT;

		cache->vaddr = (unsigned long)
			io_mapping_map_atomic_wc(&ggtt->iomap, offset);
	}
}

static void reloc_cache_reset(struct reloc_cache *cache, struct i915_execbuffer *eb)
{
	void *vaddr;

	if (!cache->vaddr)
		return;

	vaddr = unmask_page(cache->vaddr);
	if (cache->vaddr & KMAP) {
		struct drm_i915_gem_object *obj =
			(struct drm_i915_gem_object *)cache->node.mm;
		if (cache->vaddr & CLFLUSH_AFTER)
			mb();

		kunmap_atomic(vaddr);
		i915_gem_object_finish_access(obj);
	} else {
		struct i915_ggtt *ggtt = cache_to_ggtt(cache);

		intel_gt_flush_ggtt_writes(ggtt->vm.gt);
		io_mapping_unmap_atomic((void __iomem *)vaddr);

		if (drm_mm_node_allocated(&cache->node)) {
			ggtt->vm.clear_range(&ggtt->vm,
					     cache->node.start,
					     cache->node.size);
			mutex_lock(&ggtt->vm.mutex);
			drm_mm_remove_node(&cache->node);
			mutex_unlock(&ggtt->vm.mutex);
		} else {
			i915_vma_unpin((struct i915_vma *)cache->node.mm);
		}
	}

	cache->vaddr = 0;
	cache->page = -1;
}

static void *reloc_kmap(struct drm_i915_gem_object *obj,
			struct reloc_cache *cache,
			unsigned long pageno)
{
	void *vaddr;
	struct page *page;

	if (cache->vaddr) {
		kunmap_atomic(unmask_page(cache->vaddr));
	} else {
		unsigned int flushes;
		int err;

		err = i915_gem_object_prepare_write(obj, &flushes);
		if (err)
			return ERR_PTR(err);

		BUILD_BUG_ON(KMAP & CLFLUSH_FLAGS);
		BUILD_BUG_ON((KMAP | CLFLUSH_FLAGS) & PAGE_MASK);

		cache->vaddr = flushes | KMAP;
		cache->node.mm = (void *)obj;
		if (flushes)
			mb();
	}

	page = i915_gem_object_get_page(obj, pageno);
	if (!obj->mm.dirty)
		set_page_dirty(page);

	vaddr = kmap_atomic(page);
	cache->vaddr = unmask_flags(cache->vaddr) | (unsigned long)vaddr;
	cache->page = pageno;

	return vaddr;
}

static void *reloc_iomap(struct i915_vma *batch,
			 struct i915_execbuffer *eb,
			 unsigned long page)
{
	struct drm_i915_gem_object *obj = batch->obj;
	struct reloc_cache *cache = &eb->reloc_cache;
	struct i915_ggtt *ggtt = cache_to_ggtt(cache);
	unsigned long offset;
	void *vaddr;

	if (cache->vaddr) {
		intel_gt_flush_ggtt_writes(ggtt->vm.gt);
		io_mapping_unmap_atomic((void __force __iomem *) unmask_page(cache->vaddr));
	} else {
		struct i915_vma *vma = ERR_PTR(-ENODEV);
		int err;

		if (i915_gem_object_is_tiled(obj))
			return ERR_PTR(-EINVAL);

		if (use_cpu_reloc(cache, obj))
			return NULL;

		err = i915_gem_object_set_to_gtt_domain(obj, true);
		if (err)
			return ERR_PTR(err);

		/*
		 * i915_gem_object_ggtt_pin_ww may attempt to remove the batch
		 * VMA from the object list because we no longer pin.
		 *
		 * Only attempt to pin the batch buffer to ggtt if the current batch
		 * is not inside ggtt, or the batch buffer is not misplaced.
		 */
		if (!i915_is_ggtt(batch->vm) ||
		    !i915_vma_misplaced(batch, 0, 0, PIN_MAPPABLE)) {
			vma = i915_gem_object_ggtt_pin_ww(obj, &eb->ww, NULL, 0, 0,
							  PIN_MAPPABLE |
							  PIN_NONBLOCK /* NOWARN */ |
							  PIN_NOEVICT);
		}

		if (vma == ERR_PTR(-EDEADLK))
			return vma;

		if (IS_ERR(vma)) {
			memset(&cache->node, 0, sizeof(cache->node));
			mutex_lock(&ggtt->vm.mutex);
			err = drm_mm_insert_node_in_range
				(&ggtt->vm.mm, &cache->node,
				 PAGE_SIZE, 0, I915_COLOR_UNEVICTABLE,
				 0, ggtt->mappable_end,
				 DRM_MM_INSERT_LOW);
			mutex_unlock(&ggtt->vm.mutex);
			if (err) /* no inactive aperture space, use cpu reloc */
				return NULL;
		} else {
			cache->node.start = vma->node.start;
			cache->node.mm = (void *)vma;
		}
	}

	offset = cache->node.start;
	if (drm_mm_node_allocated(&cache->node)) {
		ggtt->vm.insert_page(&ggtt->vm,
				     i915_gem_object_get_dma_address(obj, page),
				     offset, I915_CACHE_NONE, 0);
	} else {
		offset += page << PAGE_SHIFT;
	}

	vaddr = (void __force *)io_mapping_map_atomic_wc(&ggtt->iomap,
							 offset);
	cache->page = page;
	cache->vaddr = (unsigned long)vaddr;

	return vaddr;
}

static void *reloc_vaddr(struct i915_vma *vma,
			 struct i915_execbuffer *eb,
			 unsigned long page)
{
	struct reloc_cache *cache = &eb->reloc_cache;
	void *vaddr;

	if (cache->page == page) {
		vaddr = unmask_page(cache->vaddr);
	} else {
		vaddr = NULL;
		if ((cache->vaddr & KMAP) == 0)
			vaddr = reloc_iomap(vma, eb, page);
		if (!vaddr)
			vaddr = reloc_kmap(vma->obj, cache, page);
	}

	return vaddr;
}

static void clflush_write32(u32 *addr, u32 value, unsigned int flushes)
{
	if (unlikely(flushes & (CLFLUSH_BEFORE | CLFLUSH_AFTER))) {
		if (flushes & CLFLUSH_BEFORE)
			drm_clflush_virt_range(addr, sizeof(*addr));

		*addr = value;

		/*
		 * Writes to the same cacheline are serialised by the CPU
		 * (including clflush). On the write path, we only require
		 * that it hits memory in an orderly fashion and place
		 * mb barriers at the start and end of the relocation phase
		 * to ensure ordering of clflush wrt to the system.
		 */
		if (flushes & CLFLUSH_AFTER)
			drm_clflush_virt_range(addr, sizeof(*addr));
	} else
		*addr = value;
}

static u64
relocate_entry(struct i915_vma *vma,
	       const struct drm_i915_gem_relocation_entry *reloc,
	       struct i915_execbuffer *eb,
	       const struct i915_vma *target)
{
	u64 target_addr = relocation_target(reloc, target);
	u64 offset = reloc->offset;
	bool wide = eb->reloc_cache.use_64bit_reloc;
	void *vaddr;

repeat:
	vaddr = reloc_vaddr(vma, eb,
			    offset >> PAGE_SHIFT);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

	GEM_BUG_ON(!IS_ALIGNED(offset, sizeof(u32)));
	clflush_write32(vaddr + offset_in_page(offset),
			lower_32_bits(target_addr),
			eb->reloc_cache.vaddr);

	if (wide) {
		offset += sizeof(u32);
		target_addr >>= 32;
		wide = false;
		goto repeat;
	}

	return target->node.start | UPDATE;
}

static u64
eb_relocate_entry(struct i915_execbuffer *eb,
		  struct eb_vma *ev,
		  const struct drm_i915_gem_relocation_entry *reloc)
{
	struct drm_i915_private *i915 = eb->i915;
	struct eb_vma *target;
	int err;

	/* we've already hold a reference to all valid objects */
	target = eb_get_vma(eb, reloc->target_handle);
	if (unlikely(!target))
		return -ENOENT;

	/* Validate that the target is in a valid r/w GPU domain */
	if (unlikely(reloc->write_domain & (reloc->write_domain - 1))) {
		drm_dbg(&i915->drm, "reloc with multiple write domains: "
			  "target %d offset %d "
			  "read %08x write %08x",
			  reloc->target_handle,
			  (int) reloc->offset,
			  reloc->read_domains,
			  reloc->write_domain);
		return -EINVAL;
	}
	if (unlikely((reloc->write_domain | reloc->read_domains)
		     & ~I915_GEM_GPU_DOMAINS)) {
		drm_dbg(&i915->drm, "reloc with read/write non-GPU domains: "
			  "target %d offset %d "
			  "read %08x write %08x",
			  reloc->target_handle,
			  (int) reloc->offset,
			  reloc->read_domains,
			  reloc->write_domain);
		return -EINVAL;
	}

	if (reloc->write_domain) {
		target->flags |= EXEC_OBJECT_WRITE;

		/*
		 * Sandybridge PPGTT errata: We need a global gtt mapping
		 * for MI and pipe_control writes because the gpu doesn't
		 * properly redirect them through the ppgtt for non_secure
		 * batchbuffers.
		 */
		if (reloc->write_domain == I915_GEM_DOMAIN_INSTRUCTION &&
		    GRAPHICS_VER(eb->i915) == 6 &&
		    !i915_vma_is_bound(target->vma, I915_VMA_GLOBAL_BIND)) {
			struct i915_vma *vma = target->vma;

			reloc_cache_unmap(&eb->reloc_cache);
			mutex_lock(&vma->vm->mutex);
			err = i915_vma_bind(target->vma,
					    target->vma->obj->cache_level,
					    PIN_GLOBAL, NULL, NULL);
			mutex_unlock(&vma->vm->mutex);
			reloc_cache_remap(&eb->reloc_cache, ev->vma->obj);
			if (err)
				return err;
		}
	}

	/*
	 * If the relocation already has the right value in it, no
	 * more work needs to be done.
	 */
	if (!DBG_FORCE_RELOC &&
	    gen8_canonical_addr(target->vma->node.start) == reloc->presumed_offset)
		return 0;

	/* Check that the relocation address is valid... */
	if (unlikely(reloc->offset >
		     ev->vma->size - (eb->reloc_cache.use_64bit_reloc ? 8 : 4))) {
		drm_dbg(&i915->drm, "Relocation beyond object bounds: "
			  "target %d offset %d size %d.\n",
			  reloc->target_handle,
			  (int)reloc->offset,
			  (int)ev->vma->size);
		return -EINVAL;
	}
	if (unlikely(reloc->offset & 3)) {
		drm_dbg(&i915->drm, "Relocation not 4-byte aligned: "
			  "target %d offset %d.\n",
			  reloc->target_handle,
			  (int)reloc->offset);
		return -EINVAL;
	}

	/*
	 * If we write into the object, we need to force the synchronisation
	 * barrier, either with an asynchronous clflush or if we executed the
	 * patching using the GPU (though that should be serialised by the
	 * timeline). To be completely sure, and since we are required to
	 * do relocations we are already stalling, disable the user's opt
	 * out of our synchronisation.
	 */
	ev->flags &= ~EXEC_OBJECT_ASYNC;

	/* and update the user's relocation entry */
	return relocate_entry(ev->vma, reloc, eb, target->vma);
}

static int eb_relocate_vma(struct i915_execbuffer *eb, struct eb_vma *ev)
{
#define N_RELOC(x) ((x) / sizeof(struct drm_i915_gem_relocation_entry))
	struct drm_i915_gem_relocation_entry stack[N_RELOC(512)];
	const struct drm_i915_gem_exec_object2 *entry = ev->exec;
	struct drm_i915_gem_relocation_entry __user *urelocs =
		u64_to_user_ptr(entry->relocs_ptr);
	unsigned long remain = entry->relocation_count;

	if (unlikely(remain > N_RELOC(ULONG_MAX)))
		return -EINVAL;

	/*
	 * We must check that the entire relocation array is safe
	 * to read. However, if the array is not writable the user loses
	 * the updated relocation values.
	 */
	if (unlikely(!access_ok(urelocs, remain * sizeof(*urelocs))))
		return -EFAULT;

	do {
		struct drm_i915_gem_relocation_entry *r = stack;
		unsigned int count =
			min_t(unsigned long, remain, ARRAY_SIZE(stack));
		unsigned int copied;

		/*
		 * This is the fast path and we cannot handle a pagefault
		 * whilst holding the struct mutex lest the user pass in the
		 * relocations contained within a mmaped bo. For in such a case
		 * we, the page fault handler would call i915_gem_fault() and
		 * we would try to acquire the struct mutex again. Obviously
		 * this is bad and so lockdep complains vehemently.
		 */
		pagefault_disable();
		copied = __copy_from_user_inatomic(r, urelocs, count * sizeof(r[0]));
		pagefault_enable();
		if (unlikely(copied)) {
			remain = -EFAULT;
			goto out;
		}

		remain -= count;
		do {
			u64 offset = eb_relocate_entry(eb, ev, r);

			if (likely(offset == 0)) {
			} else if ((s64)offset < 0) {
				remain = (int)offset;
				goto out;
			} else {
				/*
				 * Note that reporting an error now
				 * leaves everything in an inconsistent
				 * state as we have *already* changed
				 * the relocation value inside the
				 * object. As we have not changed the
				 * reloc.presumed_offset or will not
				 * change the execobject.offset, on the
				 * call we may not rewrite the value
				 * inside the object, leaving it
				 * dangling and causing a GPU hang. Unless
				 * userspace dynamically rebuilds the
				 * relocations on each execbuf rather than
				 * presume a static tree.
				 *
				 * We did previously check if the relocations
				 * were writable (access_ok), an error now
				 * would be a strange race with mprotect,
				 * having already demonstrated that we
				 * can read from this userspace address.
				 */
				offset = gen8_canonical_addr(offset & ~UPDATE);
				__put_user(offset,
					   &urelocs[r - stack].presumed_offset);
			}
		} while (r++, --count);
		urelocs += ARRAY_SIZE(stack);
	} while (remain);
out:
	reloc_cache_reset(&eb->reloc_cache, eb);
	return remain;
}

static int
eb_relocate_vma_slow(struct i915_execbuffer *eb, struct eb_vma *ev)
{
	const struct drm_i915_gem_exec_object2 *entry = ev->exec;
	struct drm_i915_gem_relocation_entry *relocs =
		u64_to_ptr(typeof(*relocs), entry->relocs_ptr);
	unsigned int i;
	int err;

	for (i = 0; i < entry->relocation_count; i++) {
		u64 offset = eb_relocate_entry(eb, ev, &relocs[i]);

		if ((s64)offset < 0) {
			err = (int)offset;
			goto err;
		}
	}
	err = 0;
err:
	reloc_cache_reset(&eb->reloc_cache, eb);
	return err;
}

static int check_relocations(const struct drm_i915_gem_exec_object2 *entry)
{
	const char __user *addr, *end;
	unsigned long size;
	char __maybe_unused c;

	size = entry->relocation_count;
	if (size == 0)
		return 0;

	if (size > N_RELOC(ULONG_MAX))
		return -EINVAL;

	addr = u64_to_user_ptr(entry->relocs_ptr);
	size *= sizeof(struct drm_i915_gem_relocation_entry);
	if (!access_ok(addr, size))
		return -EFAULT;

	end = addr + size;
	for (; addr < end; addr += PAGE_SIZE) {
		int err = __get_user(c, addr);
		if (err)
			return err;
	}
	return __get_user(c, end - 1);
}

static int eb_copy_relocations(const struct i915_execbuffer *eb)
{
	struct drm_i915_gem_relocation_entry *relocs;
	const unsigned int count = eb->buffer_count;
	unsigned int i;
	int err;

	for (i = 0; i < count; i++) {
		const unsigned int nreloc = eb->exec[i].relocation_count;
		struct drm_i915_gem_relocation_entry __user *urelocs;
		unsigned long size;
		unsigned long copied;

		if (nreloc == 0)
			continue;

		err = check_relocations(&eb->exec[i]);
		if (err)
			goto err;

		urelocs = u64_to_user_ptr(eb->exec[i].relocs_ptr);
		size = nreloc * sizeof(*relocs);

		relocs = kvmalloc_array(size, 1, GFP_KERNEL);
		if (!relocs) {
			err = -ENOMEM;
			goto err;
		}

		/* copy_from_user is limited to < 4GiB */
		copied = 0;
		do {
			unsigned int len =
				min_t(u64, BIT_ULL(31), size - copied);

			if (__copy_from_user((char *)relocs + copied,
					     (char __user *)urelocs + copied,
					     len))
				goto end;

			copied += len;
		} while (copied < size);

		/*
		 * As we do not update the known relocation offsets after
		 * relocating (due to the complexities in lock handling),
		 * we need to mark them as invalid now so that we force the
		 * relocation processing next time. Just in case the target
		 * object is evicted and then rebound into its old
		 * presumed_offset before the next execbuffer - if that
		 * happened we would make the mistake of assuming that the
		 * relocations were valid.
		 */
		if (!user_access_begin(urelocs, size))
			goto end;

		for (copied = 0; copied < nreloc; copied++)
			unsafe_put_user(-1,
					&urelocs[copied].presumed_offset,
					end_user);
		user_access_end();

		eb->exec[i].relocs_ptr = (uintptr_t)relocs;
	}

	return 0;

end_user:
	user_access_end();
end:
	kvfree(relocs);
	err = -EFAULT;
err:
	while (i--) {
		relocs = u64_to_ptr(typeof(*relocs), eb->exec[i].relocs_ptr);
		if (eb->exec[i].relocation_count)
			kvfree(relocs);
	}
	return err;
}

static int eb_prefault_relocations(const struct i915_execbuffer *eb)
{
	const unsigned int count = eb->buffer_count;
	unsigned int i;

	for (i = 0; i < count; i++) {
		int err;

		err = check_relocations(&eb->exec[i]);
		if (err)
			return err;
	}

	return 0;
}

static int eb_reinit_userptr(struct i915_execbuffer *eb)
{
	const unsigned int count = eb->buffer_count;
	unsigned int i;
	int ret;

	if (likely(!(eb->args->flags & __EXEC_USERPTR_USED)))
		return 0;

	for (i = 0; i < count; i++) {
		struct eb_vma *ev = &eb->vma[i];

		if (!i915_gem_object_is_userptr(ev->vma->obj))
			continue;

		ret = i915_gem_object_userptr_submit_init(ev->vma->obj);
		if (ret)
			return ret;

		ev->flags |= __EXEC_OBJECT_USERPTR_INIT;
	}

	return 0;
}

static noinline int eb_relocate_parse_slow(struct i915_execbuffer *eb)
{
	bool have_copy = false;
	struct eb_vma *ev;
	int err = 0;

repeat:
	if (signal_pending(current)) {
		err = -ERESTARTSYS;
		goto out;
	}

	/* We may process another execbuffer during the unlock... */
	eb_release_vmas(eb, false);
	i915_gem_ww_ctx_fini(&eb->ww);

	/*
	 * We take 3 passes through the slowpatch.
	 *
	 * 1 - we try to just prefault all the user relocation entries and
	 * then attempt to reuse the atomic pagefault disabled fast path again.
	 *
	 * 2 - we copy the user entries to a local buffer here outside of the
	 * local and allow ourselves to wait upon any rendering before
	 * relocations
	 *
	 * 3 - we already have a local copy of the relocation entries, but
	 * were interrupted (EAGAIN) whilst waiting for the objects, try again.
	 */
	if (!err) {
		err = eb_prefault_relocations(eb);
	} else if (!have_copy) {
		err = eb_copy_relocations(eb);
		have_copy = err == 0;
	} else {
		cond_resched();
		err = 0;
	}

	if (!err)
		err = eb_reinit_userptr(eb);

	i915_gem_ww_ctx_init(&eb->ww, true);
	if (err)
		goto out;

	/* reacquire the objects */
repeat_validate:
	err = eb_pin_engine(eb, false);
	if (err)
		goto err;

	err = eb_validate_vmas(eb);
	if (err)
		goto err;

	GEM_BUG_ON(!eb->batches[0]);

	list_for_each_entry(ev, &eb->relocs, reloc_link) {
		if (!have_copy) {
			err = eb_relocate_vma(eb, ev);
			if (err)
				break;
		} else {
			err = eb_relocate_vma_slow(eb, ev);
			if (err)
				break;
		}
	}

	if (err == -EDEADLK)
		goto err;

	if (err && !have_copy)
		goto repeat;

	if (err)
		goto err;

	/* as last step, parse the command buffer */
	err = eb_parse(eb);
	if (err)
		goto err;

	/*
	 * Leave the user relocations as are, this is the painfully slow path,
	 * and we want to avoid the complication of dropping the lock whilst
	 * having buffers reserved in the aperture and so causing spurious
	 * ENOSPC for random operations.
	 */

err:
	if (err == -EDEADLK) {
		eb_release_vmas(eb, false);
		err = i915_gem_ww_ctx_backoff(&eb->ww);
		if (!err)
			goto repeat_validate;
	}

	if (err == -EAGAIN)
		goto repeat;

out:
	if (have_copy) {
		const unsigned int count = eb->buffer_count;
		unsigned int i;

		for (i = 0; i < count; i++) {
			const struct drm_i915_gem_exec_object2 *entry =
				&eb->exec[i];
			struct drm_i915_gem_relocation_entry *relocs;

			if (!entry->relocation_count)
				continue;

			relocs = u64_to_ptr(typeof(*relocs), entry->relocs_ptr);
			kvfree(relocs);
		}
	}

	return err;
}

static int eb_relocate_parse(struct i915_execbuffer *eb)
{
	int err;
	bool throttle = true;

retry:
	err = eb_pin_engine(eb, throttle);
	if (err) {
		if (err != -EDEADLK)
			return err;

		goto err;
	}

	/* only throttle once, even if we didn't need to throttle */
	throttle = false;

	err = eb_validate_vmas(eb);
	if (err == -EAGAIN)
		goto slow;
	else if (err)
		goto err;

	/* The objects are in their final locations, apply the relocations. */
	if (eb->args->flags & __EXEC_HAS_RELOC) {
		struct eb_vma *ev;

		list_for_each_entry(ev, &eb->relocs, reloc_link) {
			err = eb_relocate_vma(eb, ev);
			if (err)
				break;
		}

		if (err == -EDEADLK)
			goto err;
		else if (err)
			goto slow;
	}

	if (!err)
		err = eb_parse(eb);

err:
	if (err == -EDEADLK) {
		eb_release_vmas(eb, false);
		err = i915_gem_ww_ctx_backoff(&eb->ww);
		if (!err)
			goto retry;
	}

	return err;

slow:
	err = eb_relocate_parse_slow(eb);
	if (err)
		/*
		 * If the user expects the execobject.offset and
		 * reloc.presumed_offset to be an exact match,
		 * as for using NO_RELOC, then we cannot update
		 * the execobject.offset until we have completed
		 * relocation.
		 */
		eb->args->flags &= ~__EXEC_HAS_RELOC;

	return err;
}

/*
 * Using two helper loops for the order of which requests / batches are created
 * and added the to backend. Requests are created in order from the parent to
 * the last child. Requests are added in the reverse order, from the last child
 * to parent. This is done for locking reasons as the timeline lock is acquired
 * during request creation and released when the request is added to the
 * backend. To make lockdep happy (see intel_context_timeline_lock) this must be
 * the ordering.
 */
#define for_each_batch_create_order(_eb, _i) \
	for ((_i) = 0; (_i) < (_eb)->num_batches; ++(_i))
#define for_each_batch_add_order(_eb, _i) \
	BUILD_BUG_ON(!typecheck(int, _i)); \
	for ((_i) = (_eb)->num_batches - 1; (_i) >= 0; --(_i))

static struct i915_request *
eb_find_first_request_added(struct i915_execbuffer *eb)
{
	int i;

	for_each_batch_add_order(eb, i)
		if (eb->requests[i])
			return eb->requests[i];

	GEM_BUG_ON("Request not found");

	return NULL;
}

#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)

/* Stage with GFP_KERNEL allocations before we enter the signaling critical path */
static void eb_capture_stage(struct i915_execbuffer *eb)
{
	const unsigned int count = eb->buffer_count;
	unsigned int i = count, j;

	while (i--) {
		struct eb_vma *ev = &eb->vma[i];
		struct i915_vma *vma = ev->vma;
		unsigned int flags = ev->flags;

		if (!(flags & EXEC_OBJECT_CAPTURE))
			continue;

		for_each_batch_create_order(eb, j) {
			struct i915_capture_list *capture;

			capture = kmalloc(sizeof(*capture), GFP_KERNEL);
			if (!capture)
				continue;

			capture->next = eb->capture_lists[j];
			capture->vma_res = i915_vma_resource_get(vma->resource);
			eb->capture_lists[j] = capture;
		}
	}
}

/* Commit once we're in the critical path */
static void eb_capture_commit(struct i915_execbuffer *eb)
{
	unsigned int j;

	for_each_batch_create_order(eb, j) {
		struct i915_request *rq = eb->requests[j];

		if (!rq)
			break;

		rq->capture_list = eb->capture_lists[j];
		eb->capture_lists[j] = NULL;
	}
}

/*
 * Release anything that didn't get committed due to errors.
 * The capture_list will otherwise be freed at request retire.
 */
static void eb_capture_release(struct i915_execbuffer *eb)
{
	unsigned int j;

	for_each_batch_create_order(eb, j) {
		if (eb->capture_lists[j]) {
			i915_request_free_capture_list(eb->capture_lists[j]);
			eb->capture_lists[j] = NULL;
		}
	}
}

static void eb_capture_list_clear(struct i915_execbuffer *eb)
{
	memset(eb->capture_lists, 0, sizeof(eb->capture_lists));
}

#else

static void eb_capture_stage(struct i915_execbuffer *eb)
{
}

static void eb_capture_commit(struct i915_execbuffer *eb)
{
}

static void eb_capture_release(struct i915_execbuffer *eb)
{
}

static void eb_capture_list_clear(struct i915_execbuffer *eb)
{
}

#endif

static int eb_move_to_gpu(struct i915_execbuffer *eb)
{
	const unsigned int count = eb->buffer_count;
	unsigned int i = count;
	int err = 0, j;

	while (i--) {
		struct eb_vma *ev = &eb->vma[i];
		struct i915_vma *vma = ev->vma;
		unsigned int flags = ev->flags;
		struct drm_i915_gem_object *obj = vma->obj;

		assert_vma_held(vma);

		/*
		 * If the GPU is not _reading_ through the CPU cache, we need
		 * to make sure that any writes (both previous GPU writes from
		 * before a change in snooping levels and normal CPU writes)
		 * caught in that cache are flushed to main memory.
		 *
		 * We want to say
		 *   obj->cache_dirty &&
		 *   !(obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_READ)
		 * but gcc's optimiser doesn't handle that as well and emits
		 * two jumps instead of one. Maybe one day...
		 *
		 * FIXME: There is also sync flushing in set_pages(), which
		 * serves a different purpose(some of the time at least).
		 *
		 * We should consider:
		 *
		 *   1. Rip out the async flush code.
		 *
		 *   2. Or make the sync flushing use the async clflush path
		 *   using mandatory fences underneath. Currently the below
		 *   async flush happens after we bind the object.
		 */
		if (unlikely(obj->cache_dirty & ~obj->cache_coherent)) {
			if (i915_gem_clflush_object(obj, 0))
				flags &= ~EXEC_OBJECT_ASYNC;
		}

		/* We only need to await on the first request */
		if (err == 0 && !(flags & EXEC_OBJECT_ASYNC)) {
			err = i915_request_await_object
				(eb_find_first_request_added(eb), obj,
				 flags & EXEC_OBJECT_WRITE);
		}

		for_each_batch_add_order(eb, j) {
			if (err)
				break;
			if (!eb->requests[j])
				continue;

			err = _i915_vma_move_to_active(vma, eb->requests[j],
						       j ? NULL :
						       eb->composite_fence ?
						       eb->composite_fence :
						       &eb->requests[j]->fence,
						       flags | __EXEC_OBJECT_NO_RESERVE);
		}
	}

#ifdef CONFIG_MMU_NOTIFIER
	if (!err && (eb->args->flags & __EXEC_USERPTR_USED)) {
		read_lock(&eb->i915->mm.notifier_lock);

		/*
		 * count is always at least 1, otherwise __EXEC_USERPTR_USED
		 * could not have been set
		 */
		for (i = 0; i < count; i++) {
			struct eb_vma *ev = &eb->vma[i];
			struct drm_i915_gem_object *obj = ev->vma->obj;

			if (!i915_gem_object_is_userptr(obj))
				continue;

			err = i915_gem_object_userptr_submit_done(obj);
			if (err)
				break;
		}

		read_unlock(&eb->i915->mm.notifier_lock);
	}
#endif

	if (unlikely(err))
		goto err_skip;

	/* Unconditionally flush any chipset caches (for streaming writes). */
	intel_gt_chipset_flush(eb->gt);
	eb_capture_commit(eb);

	return 0;

err_skip:
	for_each_batch_create_order(eb, j) {
		if (!eb->requests[j])
			break;

		i915_request_set_error_once(eb->requests[j], err);
	}
	return err;
}

static int i915_gem_check_execbuffer(struct drm_i915_gem_execbuffer2 *exec)
{
	if (exec->flags & __I915_EXEC_ILLEGAL_FLAGS)
		return -EINVAL;

	/* Kernel clipping was a DRI1 misfeature */
	if (!(exec->flags & (I915_EXEC_FENCE_ARRAY |
			     I915_EXEC_USE_EXTENSIONS))) {
		if (exec->num_cliprects || exec->cliprects_ptr)
			return -EINVAL;
	}

	if (exec->DR4 == 0xffffffff) {
		DRM_DEBUG("UXA submitting garbage DR4, fixing up\n");
		exec->DR4 = 0;
	}
	if (exec->DR1 || exec->DR4)
		return -EINVAL;

	if ((exec->batch_start_offset | exec->batch_len) & 0x7)
		return -EINVAL;

	return 0;
}

static int i915_reset_gen7_sol_offsets(struct i915_request *rq)
{
	u32 *cs;
	int i;

	if (GRAPHICS_VER(rq->engine->i915) != 7 || rq->engine->id != RCS0) {
		drm_dbg(&rq->engine->i915->drm, "sol reset is gen7/rcs only\n");
		return -EINVAL;
	}

	cs = intel_ring_begin(rq, 4 * 2 + 2);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_LOAD_REGISTER_IMM(4);
	for (i = 0; i < 4; i++) {
		*cs++ = i915_mmio_reg_offset(GEN7_SO_WRITE_OFFSET(i));
		*cs++ = 0;
	}
	*cs++ = MI_NOOP;
	intel_ring_advance(rq, cs);

	return 0;
}

static struct i915_vma *
shadow_batch_pin(struct i915_execbuffer *eb,
		 struct drm_i915_gem_object *obj,
		 struct i915_address_space *vm,
		 unsigned int flags)
{
	struct i915_vma *vma;
	int err;

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma))
		return vma;

	err = i915_vma_pin_ww(vma, &eb->ww, 0, 0, flags | PIN_VALIDATE);
	if (err)
		return ERR_PTR(err);

	return vma;
}

static struct i915_vma *eb_dispatch_secure(struct i915_execbuffer *eb, struct i915_vma *vma)
{
	/*
	 * snb/ivb/vlv conflate the "batch in ppgtt" bit with the "non-secure
	 * batch" bit. Hence we need to pin secure batches into the global gtt.
	 * hsw should have this fixed, but bdw mucks it up again. */
	if (eb->batch_flags & I915_DISPATCH_SECURE)
		return i915_gem_object_ggtt_pin_ww(vma->obj, &eb->ww, NULL, 0, 0, PIN_VALIDATE);

	return NULL;
}

static int eb_parse(struct i915_execbuffer *eb)
{
	struct drm_i915_private *i915 = eb->i915;
	struct intel_gt_buffer_pool_node *pool = eb->batch_pool;
	struct i915_vma *shadow, *trampoline, *batch;
	unsigned long len;
	int err;

	if (!eb_use_cmdparser(eb)) {
		batch = eb_dispatch_secure(eb, eb->batches[0]->vma);
		if (IS_ERR(batch))
			return PTR_ERR(batch);

		goto secure_batch;
	}

	if (intel_context_is_parallel(eb->context))
		return -EINVAL;

	len = eb->batch_len[0];
	if (!CMDPARSER_USES_GGTT(eb->i915)) {
		/*
		 * ppGTT backed shadow buffers must be mapped RO, to prevent
		 * post-scan tampering
		 */
		if (!eb->context->vm->has_read_only) {
			drm_dbg(&i915->drm,
				"Cannot prevent post-scan tampering without RO capable vm\n");
			return -EINVAL;
		}
	} else {
		len += I915_CMD_PARSER_TRAMPOLINE_SIZE;
	}
	if (unlikely(len < eb->batch_len[0])) /* last paranoid check of overflow */
		return -EINVAL;

	if (!pool) {
		pool = intel_gt_get_buffer_pool(eb->gt, len,
						I915_MAP_WB);
		if (IS_ERR(pool))
			return PTR_ERR(pool);
		eb->batch_pool = pool;
	}

	err = i915_gem_object_lock(pool->obj, &eb->ww);
	if (err)
		return err;

	shadow = shadow_batch_pin(eb, pool->obj, eb->context->vm, PIN_USER);
	if (IS_ERR(shadow))
		return PTR_ERR(shadow);

	intel_gt_buffer_pool_mark_used(pool);
	i915_gem_object_set_readonly(shadow->obj);
	shadow->private = pool;

	trampoline = NULL;
	if (CMDPARSER_USES_GGTT(eb->i915)) {
		trampoline = shadow;

		shadow = shadow_batch_pin(eb, pool->obj,
					  &eb->gt->ggtt->vm,
					  PIN_GLOBAL);
		if (IS_ERR(shadow))
			return PTR_ERR(shadow);

		shadow->private = pool;

		eb->batch_flags |= I915_DISPATCH_SECURE;
	}

	batch = eb_dispatch_secure(eb, shadow);
	if (IS_ERR(batch))
		return PTR_ERR(batch);

	err = dma_resv_reserve_fences(shadow->obj->base.resv, 1);
	if (err)
		return err;

	err = intel_engine_cmd_parser(eb->context->engine,
				      eb->batches[0]->vma,
				      eb->batch_start_offset,
				      eb->batch_len[0],
				      shadow, trampoline);
	if (err)
		return err;

	eb->batches[0] = &eb->vma[eb->buffer_count++];
	eb->batches[0]->vma = i915_vma_get(shadow);
	eb->batches[0]->flags = __EXEC_OBJECT_HAS_PIN;

	eb->trampoline = trampoline;
	eb->batch_start_offset = 0;

secure_batch:
	if (batch) {
		if (intel_context_is_parallel(eb->context))
			return -EINVAL;

		eb->batches[0] = &eb->vma[eb->buffer_count++];
		eb->batches[0]->flags = __EXEC_OBJECT_HAS_PIN;
		eb->batches[0]->vma = i915_vma_get(batch);
	}
	return 0;
}

static int eb_request_submit(struct i915_execbuffer *eb,
			     struct i915_request *rq,
			     struct i915_vma *batch,
			     u64 batch_len)
{
	int err;

	if (intel_context_nopreempt(rq->context))
		__set_bit(I915_FENCE_FLAG_NOPREEMPT, &rq->fence.flags);

	if (eb->args->flags & I915_EXEC_GEN7_SOL_RESET) {
		err = i915_reset_gen7_sol_offsets(rq);
		if (err)
			return err;
	}

	/*
	 * After we completed waiting for other engines (using HW semaphores)
	 * then we can signal that this request/batch is ready to run. This
	 * allows us to determine if the batch is still waiting on the GPU
	 * or actually running by checking the breadcrumb.
	 */
	if (rq->context->engine->emit_init_breadcrumb) {
		err = rq->context->engine->emit_init_breadcrumb(rq);
		if (err)
			return err;
	}

	err = rq->context->engine->emit_bb_start(rq,
						 batch->node.start +
						 eb->batch_start_offset,
						 batch_len,
						 eb->batch_flags);
	if (err)
		return err;

	if (eb->trampoline) {
		GEM_BUG_ON(intel_context_is_parallel(rq->context));
		GEM_BUG_ON(eb->batch_start_offset);
		err = rq->context->engine->emit_bb_start(rq,
							 eb->trampoline->node.start +
							 batch_len, 0, 0);
		if (err)
			return err;
	}

	return 0;
}

static int eb_submit(struct i915_execbuffer *eb)
{
	unsigned int i;
	int err;

	err = eb_move_to_gpu(eb);

	for_each_batch_create_order(eb, i) {
		if (!eb->requests[i])
			break;

		trace_i915_request_queue(eb->requests[i], eb->batch_flags);
		if (!err)
			err = eb_request_submit(eb, eb->requests[i],
						eb->batches[i]->vma,
						eb->batch_len[i]);
	}

	return err;
}

static int num_vcs_engines(struct drm_i915_private *i915)
{
	return hweight_long(VDBOX_MASK(to_gt(i915)));
}

/*
 * Find one BSD ring to dispatch the corresponding BSD command.
 * The engine index is returned.
 */
static unsigned int
gen8_dispatch_bsd_engine(struct drm_i915_private *dev_priv,
			 struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	/* Check whether the file_priv has already selected one ring. */
	if ((int)file_priv->bsd_engine < 0)
		file_priv->bsd_engine =
			get_random_int() % num_vcs_engines(dev_priv);

	return file_priv->bsd_engine;
}

static const enum intel_engine_id user_ring_map[] = {
	[I915_EXEC_DEFAULT]	= RCS0,
	[I915_EXEC_RENDER]	= RCS0,
	[I915_EXEC_BLT]		= BCS0,
	[I915_EXEC_BSD]		= VCS0,
	[I915_EXEC_VEBOX]	= VECS0
};

static struct i915_request *eb_throttle(struct i915_execbuffer *eb, struct intel_context *ce)
{
	struct intel_ring *ring = ce->ring;
	struct intel_timeline *tl = ce->timeline;
	struct i915_request *rq;

	/*
	 * Completely unscientific finger-in-the-air estimates for suitable
	 * maximum user request size (to avoid blocking) and then backoff.
	 */
	if (intel_ring_update_space(ring) >= PAGE_SIZE)
		return NULL;

	/*
	 * Find a request that after waiting upon, there will be at least half
	 * the ring available. The hysteresis allows us to compete for the
	 * shared ring and should mean that we sleep less often prior to
	 * claiming our resources, but not so long that the ring completely
	 * drains before we can submit our next request.
	 */
	list_for_each_entry(rq, &tl->requests, link) {
		if (rq->ring != ring)
			continue;

		if (__intel_ring_space(rq->postfix,
				       ring->emit, ring->size) > ring->size / 2)
			break;
	}
	if (&rq->link == &tl->requests)
		return NULL; /* weird, we will check again later for real */

	return i915_request_get(rq);
}

static int eb_pin_timeline(struct i915_execbuffer *eb, struct intel_context *ce,
			   bool throttle)
{
	struct intel_timeline *tl;
	struct i915_request *rq = NULL;

	/*
	 * Take a local wakeref for preparing to dispatch the execbuf as
	 * we expect to access the hardware fairly frequently in the
	 * process, and require the engine to be kept awake between accesses.
	 * Upon dispatch, we acquire another prolonged wakeref that we hold
	 * until the timeline is idle, which in turn releases the wakeref
	 * taken on the engine, and the parent device.
	 */
	tl = intel_context_timeline_lock(ce);
	if (IS_ERR(tl))
		return PTR_ERR(tl);

	intel_context_enter(ce);
	if (throttle)
		rq = eb_throttle(eb, ce);
	intel_context_timeline_unlock(tl);

	if (rq) {
		bool nonblock = eb->file->filp->f_flags & O_NONBLOCK;
		long timeout = nonblock ? 0 : MAX_SCHEDULE_TIMEOUT;

		if (i915_request_wait(rq, I915_WAIT_INTERRUPTIBLE,
				      timeout) < 0) {
			i915_request_put(rq);

			/*
			 * Error path, cannot use intel_context_timeline_lock as
			 * that is user interruptable and this clean up step
			 * must be done.
			 */
			mutex_lock(&ce->timeline->mutex);
			intel_context_exit(ce);
			mutex_unlock(&ce->timeline->mutex);

			if (nonblock)
				return -EWOULDBLOCK;
			else
				return -EINTR;
		}
		i915_request_put(rq);
	}

	return 0;
}

static int eb_pin_engine(struct i915_execbuffer *eb, bool throttle)
{
	struct intel_context *ce = eb->context, *child;
	int err;
	int i = 0, j = 0;

	GEM_BUG_ON(eb->args->flags & __EXEC_ENGINE_PINNED);

	if (unlikely(intel_context_is_banned(ce)))
		return -EIO;

	/*
	 * Pinning the contexts may generate requests in order to acquire
	 * GGTT space, so do this first before we reserve a seqno for
	 * ourselves.
	 */
	err = intel_context_pin_ww(ce, &eb->ww);
	if (err)
		return err;
	for_each_child(ce, child) {
		err = intel_context_pin_ww(child, &eb->ww);
		GEM_BUG_ON(err);	/* perma-pinned should incr a counter */
	}

	for_each_child(ce, child) {
		err = eb_pin_timeline(eb, child, throttle);
		if (err)
			goto unwind;
		++i;
	}
	err = eb_pin_timeline(eb, ce, throttle);
	if (err)
		goto unwind;

	eb->args->flags |= __EXEC_ENGINE_PINNED;
	return 0;

unwind:
	for_each_child(ce, child) {
		if (j++ < i) {
			mutex_lock(&child->timeline->mutex);
			intel_context_exit(child);
			mutex_unlock(&child->timeline->mutex);
		}
	}
	for_each_child(ce, child)
		intel_context_unpin(child);
	intel_context_unpin(ce);
	return err;
}

static void eb_unpin_engine(struct i915_execbuffer *eb)
{
	struct intel_context *ce = eb->context, *child;

	if (!(eb->args->flags & __EXEC_ENGINE_PINNED))
		return;

	eb->args->flags &= ~__EXEC_ENGINE_PINNED;

	for_each_child(ce, child) {
		mutex_lock(&child->timeline->mutex);
		intel_context_exit(child);
		mutex_unlock(&child->timeline->mutex);

		intel_context_unpin(child);
	}

	mutex_lock(&ce->timeline->mutex);
	intel_context_exit(ce);
	mutex_unlock(&ce->timeline->mutex);

	intel_context_unpin(ce);
}

static unsigned int
eb_select_legacy_ring(struct i915_execbuffer *eb)
{
	struct drm_i915_private *i915 = eb->i915;
	struct drm_i915_gem_execbuffer2 *args = eb->args;
	unsigned int user_ring_id = args->flags & I915_EXEC_RING_MASK;

	if (user_ring_id != I915_EXEC_BSD &&
	    (args->flags & I915_EXEC_BSD_MASK)) {
		drm_dbg(&i915->drm,
			"execbuf with non bsd ring but with invalid "
			"bsd dispatch flags: %d\n", (int)(args->flags));
		return -1;
	}

	if (user_ring_id == I915_EXEC_BSD && num_vcs_engines(i915) > 1) {
		unsigned int bsd_idx = args->flags & I915_EXEC_BSD_MASK;

		if (bsd_idx == I915_EXEC_BSD_DEFAULT) {
			bsd_idx = gen8_dispatch_bsd_engine(i915, eb->file);
		} else if (bsd_idx >= I915_EXEC_BSD_RING1 &&
			   bsd_idx <= I915_EXEC_BSD_RING2) {
			bsd_idx >>= I915_EXEC_BSD_SHIFT;
			bsd_idx--;
		} else {
			drm_dbg(&i915->drm,
				"execbuf with unknown bsd ring: %u\n",
				bsd_idx);
			return -1;
		}

		return _VCS(bsd_idx);
	}

	if (user_ring_id >= ARRAY_SIZE(user_ring_map)) {
		drm_dbg(&i915->drm, "execbuf with unknown ring: %u\n",
			user_ring_id);
		return -1;
	}

	return user_ring_map[user_ring_id];
}

static int
eb_select_engine(struct i915_execbuffer *eb)
{
	struct intel_context *ce, *child;
	unsigned int idx;
	int err;

	if (i915_gem_context_user_engines(eb->gem_context))
		idx = eb->args->flags & I915_EXEC_RING_MASK;
	else
		idx = eb_select_legacy_ring(eb);

	ce = i915_gem_context_get_engine(eb->gem_context, idx);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	if (intel_context_is_parallel(ce)) {
		if (eb->buffer_count < ce->parallel.number_children + 1) {
			intel_context_put(ce);
			return -EINVAL;
		}
		if (eb->batch_start_offset || eb->args->batch_len) {
			intel_context_put(ce);
			return -EINVAL;
		}
	}
	eb->num_batches = ce->parallel.number_children + 1;

	for_each_child(ce, child)
		intel_context_get(child);
	intel_gt_pm_get(ce->engine->gt);

	if (!test_bit(CONTEXT_ALLOC_BIT, &ce->flags)) {
		err = intel_context_alloc_state(ce);
		if (err)
			goto err;
	}
	for_each_child(ce, child) {
		if (!test_bit(CONTEXT_ALLOC_BIT, &child->flags)) {
			err = intel_context_alloc_state(child);
			if (err)
				goto err;
		}
	}

	/*
	 * ABI: Before userspace accesses the GPU (e.g. execbuffer), report
	 * EIO if the GPU is already wedged.
	 */
	err = intel_gt_terminally_wedged(ce->engine->gt);
	if (err)
		goto err;

	if (!i915_vm_tryget(ce->vm)) {
		err = -ENOENT;
		goto err;
	}

	eb->context = ce;
	eb->gt = ce->engine->gt;

	/*
	 * Make sure engine pool stays alive even if we call intel_context_put
	 * during ww handling. The pool is destroyed when last pm reference
	 * is dropped, which breaks our -EDEADLK handling.
	 */
	return err;

err:
	intel_gt_pm_put(ce->engine->gt);
	for_each_child(ce, child)
		intel_context_put(child);
	intel_context_put(ce);
	return err;
}

static void
eb_put_engine(struct i915_execbuffer *eb)
{
	struct intel_context *child;

	i915_vm_put(eb->context->vm);
	intel_gt_pm_put(eb->gt);
	for_each_child(eb->context, child)
		intel_context_put(child);
	intel_context_put(eb->context);
}

static void
__free_fence_array(struct eb_fence *fences, unsigned int n)
{
	while (n--) {
		drm_syncobj_put(ptr_mask_bits(fences[n].syncobj, 2));
		dma_fence_put(fences[n].dma_fence);
		dma_fence_chain_free(fences[n].chain_fence);
	}
	kvfree(fences);
}

static int
add_timeline_fence_array(struct i915_execbuffer *eb,
			 const struct drm_i915_gem_execbuffer_ext_timeline_fences *timeline_fences)
{
	struct drm_i915_gem_exec_fence __user *user_fences;
	u64 __user *user_values;
	struct eb_fence *f;
	u64 nfences;
	int err = 0;

	nfences = timeline_fences->fence_count;
	if (!nfences)
		return 0;

	/* Check multiplication overflow for access_ok() and kvmalloc_array() */
	BUILD_BUG_ON(sizeof(size_t) > sizeof(unsigned long));
	if (nfences > min_t(unsigned long,
			    ULONG_MAX / sizeof(*user_fences),
			    SIZE_MAX / sizeof(*f)) - eb->num_fences)
		return -EINVAL;

	user_fences = u64_to_user_ptr(timeline_fences->handles_ptr);
	if (!access_ok(user_fences, nfences * sizeof(*user_fences)))
		return -EFAULT;

	user_values = u64_to_user_ptr(timeline_fences->values_ptr);
	if (!access_ok(user_values, nfences * sizeof(*user_values)))
		return -EFAULT;

	f = krealloc(eb->fences,
		     (eb->num_fences + nfences) * sizeof(*f),
		     __GFP_NOWARN | GFP_KERNEL);
	if (!f)
		return -ENOMEM;

	eb->fences = f;
	f += eb->num_fences;

	BUILD_BUG_ON(~(ARCH_KMALLOC_MINALIGN - 1) &
		     ~__I915_EXEC_FENCE_UNKNOWN_FLAGS);

	while (nfences--) {
		struct drm_i915_gem_exec_fence user_fence;
		struct drm_syncobj *syncobj;
		struct dma_fence *fence = NULL;
		u64 point;

		if (__copy_from_user(&user_fence,
				     user_fences++,
				     sizeof(user_fence)))
			return -EFAULT;

		if (user_fence.flags & __I915_EXEC_FENCE_UNKNOWN_FLAGS)
			return -EINVAL;

		if (__get_user(point, user_values++))
			return -EFAULT;

		syncobj = drm_syncobj_find(eb->file, user_fence.handle);
		if (!syncobj) {
			DRM_DEBUG("Invalid syncobj handle provided\n");
			return -ENOENT;
		}

		fence = drm_syncobj_fence_get(syncobj);

		if (!fence && user_fence.flags &&
		    !(user_fence.flags & I915_EXEC_FENCE_SIGNAL)) {
			DRM_DEBUG("Syncobj handle has no fence\n");
			drm_syncobj_put(syncobj);
			return -EINVAL;
		}

		if (fence)
			err = dma_fence_chain_find_seqno(&fence, point);

		if (err && !(user_fence.flags & I915_EXEC_FENCE_SIGNAL)) {
			DRM_DEBUG("Syncobj handle missing requested point %llu\n", point);
			dma_fence_put(fence);
			drm_syncobj_put(syncobj);
			return err;
		}

		/*
		 * A point might have been signaled already and
		 * garbage collected from the timeline. In this case
		 * just ignore the point and carry on.
		 */
		if (!fence && !(user_fence.flags & I915_EXEC_FENCE_SIGNAL)) {
			drm_syncobj_put(syncobj);
			continue;
		}

		/*
		 * For timeline syncobjs we need to preallocate chains for
		 * later signaling.
		 */
		if (point != 0 && user_fence.flags & I915_EXEC_FENCE_SIGNAL) {
			/*
			 * Waiting and signaling the same point (when point !=
			 * 0) would break the timeline.
			 */
			if (user_fence.flags & I915_EXEC_FENCE_WAIT) {
				DRM_DEBUG("Trying to wait & signal the same timeline point.\n");
				dma_fence_put(fence);
				drm_syncobj_put(syncobj);
				return -EINVAL;
			}

			f->chain_fence = dma_fence_chain_alloc();
			if (!f->chain_fence) {
				drm_syncobj_put(syncobj);
				dma_fence_put(fence);
				return -ENOMEM;
			}
		} else {
			f->chain_fence = NULL;
		}

		f->syncobj = ptr_pack_bits(syncobj, user_fence.flags, 2);
		f->dma_fence = fence;
		f->value = point;
		f++;
		eb->num_fences++;
	}

	return 0;
}

static int add_fence_array(struct i915_execbuffer *eb)
{
	struct drm_i915_gem_execbuffer2 *args = eb->args;
	struct drm_i915_gem_exec_fence __user *user;
	unsigned long num_fences = args->num_cliprects;
	struct eb_fence *f;

	if (!(args->flags & I915_EXEC_FENCE_ARRAY))
		return 0;

	if (!num_fences)
		return 0;

	/* Check multiplication overflow for access_ok() and kvmalloc_array() */
	BUILD_BUG_ON(sizeof(size_t) > sizeof(unsigned long));
	if (num_fences > min_t(unsigned long,
			       ULONG_MAX / sizeof(*user),
			       SIZE_MAX / sizeof(*f) - eb->num_fences))
		return -EINVAL;

	user = u64_to_user_ptr(args->cliprects_ptr);
	if (!access_ok(user, num_fences * sizeof(*user)))
		return -EFAULT;

	f = krealloc(eb->fences,
		     (eb->num_fences + num_fences) * sizeof(*f),
		     __GFP_NOWARN | GFP_KERNEL);
	if (!f)
		return -ENOMEM;

	eb->fences = f;
	f += eb->num_fences;
	while (num_fences--) {
		struct drm_i915_gem_exec_fence user_fence;
		struct drm_syncobj *syncobj;
		struct dma_fence *fence = NULL;

		if (__copy_from_user(&user_fence, user++, sizeof(user_fence)))
			return -EFAULT;

		if (user_fence.flags & __I915_EXEC_FENCE_UNKNOWN_FLAGS)
			return -EINVAL;

		syncobj = drm_syncobj_find(eb->file, user_fence.handle);
		if (!syncobj) {
			DRM_DEBUG("Invalid syncobj handle provided\n");
			return -ENOENT;
		}

		if (user_fence.flags & I915_EXEC_FENCE_WAIT) {
			fence = drm_syncobj_fence_get(syncobj);
			if (!fence) {
				DRM_DEBUG("Syncobj handle has no fence\n");
				drm_syncobj_put(syncobj);
				return -EINVAL;
			}
		}

		BUILD_BUG_ON(~(ARCH_KMALLOC_MINALIGN - 1) &
			     ~__I915_EXEC_FENCE_UNKNOWN_FLAGS);

		f->syncobj = ptr_pack_bits(syncobj, user_fence.flags, 2);
		f->dma_fence = fence;
		f->value = 0;
		f->chain_fence = NULL;
		f++;
		eb->num_fences++;
	}

	return 0;
}

static void put_fence_array(struct eb_fence *fences, int num_fences)
{
	if (fences)
		__free_fence_array(fences, num_fences);
}

static int
await_fence_array(struct i915_execbuffer *eb,
		  struct i915_request *rq)
{
	unsigned int n;
	int err;

	for (n = 0; n < eb->num_fences; n++) {
		struct drm_syncobj *syncobj;
		unsigned int flags;

		syncobj = ptr_unpack_bits(eb->fences[n].syncobj, &flags, 2);

		if (!eb->fences[n].dma_fence)
			continue;

		err = i915_request_await_dma_fence(rq, eb->fences[n].dma_fence);
		if (err < 0)
			return err;
	}

	return 0;
}

static void signal_fence_array(const struct i915_execbuffer *eb,
			       struct dma_fence * const fence)
{
	unsigned int n;

	for (n = 0; n < eb->num_fences; n++) {
		struct drm_syncobj *syncobj;
		unsigned int flags;

		syncobj = ptr_unpack_bits(eb->fences[n].syncobj, &flags, 2);
		if (!(flags & I915_EXEC_FENCE_SIGNAL))
			continue;

		if (eb->fences[n].chain_fence) {
			drm_syncobj_add_point(syncobj,
					      eb->fences[n].chain_fence,
					      fence,
					      eb->fences[n].value);
			/*
			 * The chain's ownership is transferred to the
			 * timeline.
			 */
			eb->fences[n].chain_fence = NULL;
		} else {
			drm_syncobj_replace_fence(syncobj, fence);
		}
	}
}

static int
parse_timeline_fences(struct i915_user_extension __user *ext, void *data)
{
	struct i915_execbuffer *eb = data;
	struct drm_i915_gem_execbuffer_ext_timeline_fences timeline_fences;

	if (copy_from_user(&timeline_fences, ext, sizeof(timeline_fences)))
		return -EFAULT;

	return add_timeline_fence_array(eb, &timeline_fences);
}

static void retire_requests(struct intel_timeline *tl, struct i915_request *end)
{
	struct i915_request *rq, *rn;

	list_for_each_entry_safe(rq, rn, &tl->requests, link)
		if (rq == end || !i915_request_retire(rq))
			break;
}

static int eb_request_add(struct i915_execbuffer *eb, struct i915_request *rq,
			  int err, bool last_parallel)
{
	struct intel_timeline * const tl = i915_request_timeline(rq);
	struct i915_sched_attr attr = {};
	struct i915_request *prev;

	lockdep_assert_held(&tl->mutex);
	lockdep_unpin_lock(&tl->mutex, rq->cookie);

	trace_i915_request_add(rq);

	prev = __i915_request_commit(rq);

	/* Check that the context wasn't destroyed before submission */
	if (likely(!intel_context_is_closed(eb->context))) {
		attr = eb->gem_context->sched;
	} else {
		/* Serialise with context_close via the add_to_timeline */
		i915_request_set_error_once(rq, -ENOENT);
		__i915_request_skip(rq);
		err = -ENOENT; /* override any transient errors */
	}

	if (intel_context_is_parallel(eb->context)) {
		if (err) {
			__i915_request_skip(rq);
			set_bit(I915_FENCE_FLAG_SKIP_PARALLEL,
				&rq->fence.flags);
		}
		if (last_parallel)
			set_bit(I915_FENCE_FLAG_SUBMIT_PARALLEL,
				&rq->fence.flags);
	}

	__i915_request_queue(rq, &attr);

	/* Try to clean up the client's timeline after submitting the request */
	if (prev)
		retire_requests(tl, prev);

	mutex_unlock(&tl->mutex);

	return err;
}

static int eb_requests_add(struct i915_execbuffer *eb, int err)
{
	int i;

	/*
	 * We iterate in reverse order of creation to release timeline mutexes in
	 * same order.
	 */
	for_each_batch_add_order(eb, i) {
		struct i915_request *rq = eb->requests[i];

		if (!rq)
			continue;
		err |= eb_request_add(eb, rq, err, i == 0);
	}

	return err;
}

static const i915_user_extension_fn execbuf_extensions[] = {
	[DRM_I915_GEM_EXECBUFFER_EXT_TIMELINE_FENCES] = parse_timeline_fences,
};

static int
parse_execbuf2_extensions(struct drm_i915_gem_execbuffer2 *args,
			  struct i915_execbuffer *eb)
{
	if (!(args->flags & I915_EXEC_USE_EXTENSIONS))
		return 0;

	/* The execbuf2 extension mechanism reuses cliprects_ptr. So we cannot
	 * have another flag also using it at the same time.
	 */
	if (eb->args->flags & I915_EXEC_FENCE_ARRAY)
		return -EINVAL;

	if (args->num_cliprects != 0)
		return -EINVAL;

	return i915_user_extensions(u64_to_user_ptr(args->cliprects_ptr),
				    execbuf_extensions,
				    ARRAY_SIZE(execbuf_extensions),
				    eb);
}

static void eb_requests_get(struct i915_execbuffer *eb)
{
	unsigned int i;

	for_each_batch_create_order(eb, i) {
		if (!eb->requests[i])
			break;

		i915_request_get(eb->requests[i]);
	}
}

static void eb_requests_put(struct i915_execbuffer *eb)
{
	unsigned int i;

	for_each_batch_create_order(eb, i) {
		if (!eb->requests[i])
			break;

		i915_request_put(eb->requests[i]);
	}
}

static struct sync_file *
eb_composite_fence_create(struct i915_execbuffer *eb, int out_fence_fd)
{
	struct sync_file *out_fence = NULL;
	struct dma_fence_array *fence_array;
	struct dma_fence **fences;
	unsigned int i;

	GEM_BUG_ON(!intel_context_is_parent(eb->context));

	fences = kmalloc_array(eb->num_batches, sizeof(*fences), GFP_KERNEL);
	if (!fences)
		return ERR_PTR(-ENOMEM);

	for_each_batch_create_order(eb, i) {
		fences[i] = &eb->requests[i]->fence;
		__set_bit(I915_FENCE_FLAG_COMPOSITE,
			  &eb->requests[i]->fence.flags);
	}

	fence_array = dma_fence_array_create(eb->num_batches,
					     fences,
					     eb->context->parallel.fence_context,
					     eb->context->parallel.seqno++,
					     false);
	if (!fence_array) {
		kfree(fences);
		return ERR_PTR(-ENOMEM);
	}

	/* Move ownership to the dma_fence_array created above */
	for_each_batch_create_order(eb, i)
		dma_fence_get(fences[i]);

	if (out_fence_fd != -1) {
		out_fence = sync_file_create(&fence_array->base);
		/* sync_file now owns fence_arry, drop creation ref */
		dma_fence_put(&fence_array->base);
		if (!out_fence)
			return ERR_PTR(-ENOMEM);
	}

	eb->composite_fence = &fence_array->base;

	return out_fence;
}

static struct sync_file *
eb_fences_add(struct i915_execbuffer *eb, struct i915_request *rq,
	      struct dma_fence *in_fence, int out_fence_fd)
{
	struct sync_file *out_fence = NULL;
	int err;

	if (unlikely(eb->gem_context->syncobj)) {
		struct dma_fence *fence;

		fence = drm_syncobj_fence_get(eb->gem_context->syncobj);
		err = i915_request_await_dma_fence(rq, fence);
		dma_fence_put(fence);
		if (err)
			return ERR_PTR(err);
	}

	if (in_fence) {
		if (eb->args->flags & I915_EXEC_FENCE_SUBMIT)
			err = i915_request_await_execution(rq, in_fence);
		else
			err = i915_request_await_dma_fence(rq, in_fence);
		if (err < 0)
			return ERR_PTR(err);
	}

	if (eb->fences) {
		err = await_fence_array(eb, rq);
		if (err)
			return ERR_PTR(err);
	}

	if (intel_context_is_parallel(eb->context)) {
		out_fence = eb_composite_fence_create(eb, out_fence_fd);
		if (IS_ERR(out_fence))
			return ERR_PTR(-ENOMEM);
	} else if (out_fence_fd != -1) {
		out_fence = sync_file_create(&rq->fence);
		if (!out_fence)
			return ERR_PTR(-ENOMEM);
	}

	return out_fence;
}

static struct intel_context *
eb_find_context(struct i915_execbuffer *eb, unsigned int context_number)
{
	struct intel_context *child;

	if (likely(context_number == 0))
		return eb->context;

	for_each_child(eb->context, child)
		if (!--context_number)
			return child;

	GEM_BUG_ON("Context not found");

	return NULL;
}

static struct sync_file *
eb_requests_create(struct i915_execbuffer *eb, struct dma_fence *in_fence,
		   int out_fence_fd)
{
	struct sync_file *out_fence = NULL;
	unsigned int i;

	for_each_batch_create_order(eb, i) {
		/* Allocate a request for this batch buffer nice and early. */
		eb->requests[i] = i915_request_create(eb_find_context(eb, i));
		if (IS_ERR(eb->requests[i])) {
			out_fence = ERR_CAST(eb->requests[i]);
			eb->requests[i] = NULL;
			return out_fence;
		}

		/*
		 * Only the first request added (committed to backend) has to
		 * take the in fences into account as all subsequent requests
		 * will have fences inserted inbetween them.
		 */
		if (i + 1 == eb->num_batches) {
			out_fence = eb_fences_add(eb, eb->requests[i],
						  in_fence, out_fence_fd);
			if (IS_ERR(out_fence))
				return out_fence;
		}

		/*
		 * Not really on stack, but we don't want to call
		 * kfree on the batch_snapshot when we put it, so use the
		 * _onstack interface.
		 */
		if (eb->batches[i]->vma)
			eb->requests[i]->batch_res =
				i915_vma_resource_get(eb->batches[i]->vma->resource);
		if (eb->batch_pool) {
			GEM_BUG_ON(intel_context_is_parallel(eb->context));
			intel_gt_buffer_pool_mark_active(eb->batch_pool,
							 eb->requests[i]);
		}
	}

	return out_fence;
}

static int
i915_gem_do_execbuffer(struct drm_device *dev,
		       struct drm_file *file,
		       struct drm_i915_gem_execbuffer2 *args,
		       struct drm_i915_gem_exec_object2 *exec)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct i915_execbuffer eb;
	struct dma_fence *in_fence = NULL;
	struct sync_file *out_fence = NULL;
	int out_fence_fd = -1;
	int err;

	BUILD_BUG_ON(__EXEC_INTERNAL_FLAGS & ~__I915_EXEC_ILLEGAL_FLAGS);
	BUILD_BUG_ON(__EXEC_OBJECT_INTERNAL_FLAGS &
		     ~__EXEC_OBJECT_UNKNOWN_FLAGS);

	eb.i915 = i915;
	eb.file = file;
	eb.args = args;
	if (DBG_FORCE_RELOC || !(args->flags & I915_EXEC_NO_RELOC))
		args->flags |= __EXEC_HAS_RELOC;

	eb.exec = exec;
	eb.vma = (struct eb_vma *)(exec + args->buffer_count + 1);
	eb.vma[0].vma = NULL;
	eb.batch_pool = NULL;

	eb.invalid_flags = __EXEC_OBJECT_UNKNOWN_FLAGS;
	reloc_cache_init(&eb.reloc_cache, eb.i915);

	eb.buffer_count = args->buffer_count;
	eb.batch_start_offset = args->batch_start_offset;
	eb.trampoline = NULL;

	eb.fences = NULL;
	eb.num_fences = 0;

	eb_capture_list_clear(&eb);

	memset(eb.requests, 0, sizeof(struct i915_request *) *
	       ARRAY_SIZE(eb.requests));
	eb.composite_fence = NULL;

	eb.batch_flags = 0;
	if (args->flags & I915_EXEC_SECURE) {
		if (GRAPHICS_VER(i915) >= 11)
			return -ENODEV;

		/* Return -EPERM to trigger fallback code on old binaries. */
		if (!HAS_SECURE_BATCHES(i915))
			return -EPERM;

		if (!drm_is_current_master(file) || !capable(CAP_SYS_ADMIN))
			return -EPERM;

		eb.batch_flags |= I915_DISPATCH_SECURE;
	}
	if (args->flags & I915_EXEC_IS_PINNED)
		eb.batch_flags |= I915_DISPATCH_PINNED;

	err = parse_execbuf2_extensions(args, &eb);
	if (err)
		goto err_ext;

	err = add_fence_array(&eb);
	if (err)
		goto err_ext;

#define IN_FENCES (I915_EXEC_FENCE_IN | I915_EXEC_FENCE_SUBMIT)
	if (args->flags & IN_FENCES) {
		if ((args->flags & IN_FENCES) == IN_FENCES)
			return -EINVAL;

		in_fence = sync_file_get_fence(lower_32_bits(args->rsvd2));
		if (!in_fence) {
			err = -EINVAL;
			goto err_ext;
		}
	}
#undef IN_FENCES

	if (args->flags & I915_EXEC_FENCE_OUT) {
		out_fence_fd = get_unused_fd_flags(O_CLOEXEC);
		if (out_fence_fd < 0) {
			err = out_fence_fd;
			goto err_in_fence;
		}
	}

	err = eb_create(&eb);
	if (err)
		goto err_out_fence;

	GEM_BUG_ON(!eb.lut_size);

	err = eb_select_context(&eb);
	if (unlikely(err))
		goto err_destroy;

	err = eb_select_engine(&eb);
	if (unlikely(err))
		goto err_context;

	err = eb_lookup_vmas(&eb);
	if (err) {
		eb_release_vmas(&eb, true);
		goto err_engine;
	}

	i915_gem_ww_ctx_init(&eb.ww, true);

	err = eb_relocate_parse(&eb);
	if (err) {
		/*
		 * If the user expects the execobject.offset and
		 * reloc.presumed_offset to be an exact match,
		 * as for using NO_RELOC, then we cannot update
		 * the execobject.offset until we have completed
		 * relocation.
		 */
		args->flags &= ~__EXEC_HAS_RELOC;
		goto err_vma;
	}

	ww_acquire_done(&eb.ww.ctx);
	eb_capture_stage(&eb);

	out_fence = eb_requests_create(&eb, in_fence, out_fence_fd);
	if (IS_ERR(out_fence)) {
		err = PTR_ERR(out_fence);
		out_fence = NULL;
		if (eb.requests[0])
			goto err_request;
		else
			goto err_vma;
	}

	err = eb_submit(&eb);

err_request:
	eb_requests_get(&eb);
	err = eb_requests_add(&eb, err);

	if (eb.fences)
		signal_fence_array(&eb, eb.composite_fence ?
				   eb.composite_fence :
				   &eb.requests[0]->fence);

	if (out_fence) {
		if (err == 0) {
			fd_install(out_fence_fd, out_fence->file);
			args->rsvd2 &= GENMASK_ULL(31, 0); /* keep in-fence */
			args->rsvd2 |= (u64)out_fence_fd << 32;
			out_fence_fd = -1;
		} else {
			fput(out_fence->file);
		}
	}

	if (unlikely(eb.gem_context->syncobj)) {
		drm_syncobj_replace_fence(eb.gem_context->syncobj,
					  eb.composite_fence ?
					  eb.composite_fence :
					  &eb.requests[0]->fence);
	}

	if (!out_fence && eb.composite_fence)
		dma_fence_put(eb.composite_fence);

	eb_requests_put(&eb);

err_vma:
	eb_release_vmas(&eb, true);
	WARN_ON(err == -EDEADLK);
	i915_gem_ww_ctx_fini(&eb.ww);

	if (eb.batch_pool)
		intel_gt_buffer_pool_put(eb.batch_pool);
err_engine:
	eb_put_engine(&eb);
err_context:
	i915_gem_context_put(eb.gem_context);
err_destroy:
	eb_destroy(&eb);
err_out_fence:
	if (out_fence_fd != -1)
		put_unused_fd(out_fence_fd);
err_in_fence:
	dma_fence_put(in_fence);
err_ext:
	put_fence_array(eb.fences, eb.num_fences);
	return err;
}

static size_t eb_element_size(void)
{
	return sizeof(struct drm_i915_gem_exec_object2) + sizeof(struct eb_vma);
}

static bool check_buffer_count(size_t count)
{
	const size_t sz = eb_element_size();

	/*
	 * When using LUT_HANDLE, we impose a limit of INT_MAX for the lookup
	 * array size (see eb_create()). Otherwise, we can accept an array as
	 * large as can be addressed (though use large arrays at your peril)!
	 */

	return !(count < 1 || count > INT_MAX || count > SIZE_MAX / sz - 1);
}

int
i915_gem_execbuffer2_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct drm_i915_gem_execbuffer2 *args = data;
	struct drm_i915_gem_exec_object2 *exec2_list;
	const size_t count = args->buffer_count;
	int err;

	if (!check_buffer_count(count)) {
		drm_dbg(&i915->drm, "execbuf2 with %zd buffers\n", count);
		return -EINVAL;
	}

	err = i915_gem_check_execbuffer(args);
	if (err)
		return err;

	/* Allocate extra slots for use by the command parser */
	exec2_list = kvmalloc_array(count + 2, eb_element_size(),
				    __GFP_NOWARN | GFP_KERNEL);
	if (exec2_list == NULL) {
		drm_dbg(&i915->drm, "Failed to allocate exec list for %zd buffers\n",
			count);
		return -ENOMEM;
	}
	if (copy_from_user(exec2_list,
			   u64_to_user_ptr(args->buffers_ptr),
			   sizeof(*exec2_list) * count)) {
		drm_dbg(&i915->drm, "copy %zd exec entries failed\n", count);
		kvfree(exec2_list);
		return -EFAULT;
	}

	err = i915_gem_do_execbuffer(dev, file, args, exec2_list);

	/*
	 * Now that we have begun execution of the batchbuffer, we ignore
	 * any new error after this point. Also given that we have already
	 * updated the associated relocations, we try to write out the current
	 * object locations irrespective of any error.
	 */
	if (args->flags & __EXEC_HAS_RELOC) {
		struct drm_i915_gem_exec_object2 __user *user_exec_list =
			u64_to_user_ptr(args->buffers_ptr);
		unsigned int i;

		/* Copy the new buffer offsets back to the user's exec list. */
		/*
		 * Note: count * sizeof(*user_exec_list) does not overflow,
		 * because we checked 'count' in check_buffer_count().
		 *
		 * And this range already got effectively checked earlier
		 * when we did the "copy_from_user()" above.
		 */
		if (!user_write_access_begin(user_exec_list,
					     count * sizeof(*user_exec_list)))
			goto end;

		for (i = 0; i < args->buffer_count; i++) {
			if (!(exec2_list[i].offset & UPDATE))
				continue;

			exec2_list[i].offset =
				gen8_canonical_addr(exec2_list[i].offset & PIN_OFFSET_MASK);
			unsafe_put_user(exec2_list[i].offset,
					&user_exec_list[i].offset,
					end_user);
		}
end_user:
		user_write_access_end();
end:;
	}

	args->flags &= ~__I915_EXEC_UNKNOWN_FLAGS;
	kvfree(exec2_list);
	return err;
}
