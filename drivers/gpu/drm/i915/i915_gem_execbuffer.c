/*
 * Copyright © 2008,2010 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

#include <linux/dma_remapping.h>
#include <linux/reservation.h>
#include <linux/sync_file.h>
#include <linux/uaccess.h>

#include <drm/drmP.h>
#include <drm/drm_syncobj.h>
#include <drm/i915_drm.h>

#include "i915_drv.h"
#include "i915_gem_clflush.h"
#include "i915_trace.h"
#include "intel_drv.h"
#include "intel_frontbuffer.h"

enum {
	FORCE_CPU_RELOC = 1,
	FORCE_GTT_RELOC,
	FORCE_GPU_RELOC,
#define DBG_FORCE_RELOC 0 /* choose one of the above! */
};

#define __EXEC_OBJECT_HAS_REF		BIT(31)
#define __EXEC_OBJECT_HAS_PIN		BIT(30)
#define __EXEC_OBJECT_HAS_FENCE		BIT(29)
#define __EXEC_OBJECT_NEEDS_MAP		BIT(28)
#define __EXEC_OBJECT_NEEDS_BIAS	BIT(27)
#define __EXEC_OBJECT_INTERNAL_FLAGS	(~0u << 27) /* all of the above */
#define __EXEC_OBJECT_RESERVED (__EXEC_OBJECT_HAS_PIN | __EXEC_OBJECT_HAS_FENCE)

#define __EXEC_HAS_RELOC	BIT(31)
#define __EXEC_VALIDATED	BIT(30)
#define __EXEC_INTERNAL_FLAGS	(~0u << 30)
#define UPDATE			PIN_OFFSET_FIXED

#define BATCH_OFFSET_BIAS (256*1024)

#define __I915_EXEC_ILLEGAL_FLAGS \
	(__I915_EXEC_UNKNOWN_FLAGS | I915_EXEC_CONSTANTS_MASK)

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

struct i915_execbuffer {
	struct drm_i915_private *i915; /** i915 backpointer */
	struct drm_file *file; /** per-file lookup tables and limits */
	struct drm_i915_gem_execbuffer2 *args; /** ioctl parameters */
	struct drm_i915_gem_exec_object2 *exec; /** ioctl execobj[] */
	struct i915_vma **vma;
	unsigned int *flags;

	struct intel_engine_cs *engine; /** engine to queue the request to */
	struct i915_gem_context *ctx; /** context for building the request */
	struct i915_address_space *vm; /** GTT and vma for the request */

	struct i915_request *request; /** our request to build */
	struct i915_vma *batch; /** identity of the batch obj/vma */

	/** actual size of execobj[] as we may extend it for the cmdparser */
	unsigned int buffer_count;

	/** list of vma not yet bound during reservation phase */
	struct list_head unbound;

	/** list of vma that have execobj.relocation_count */
	struct list_head relocs;

	/**
	 * Track the most recently used object for relocations, as we
	 * frequently have to perform multiple relocations within the same
	 * obj/page
	 */
	struct reloc_cache {
		struct drm_mm_node node; /** temporary GTT binding */
		unsigned long vaddr; /** Current kmap address */
		unsigned long page; /** Currently mapped page index */
		unsigned int gen; /** Cached value of INTEL_GEN */
		bool use_64bit_reloc : 1;
		bool has_llc : 1;
		bool has_fence : 1;
		bool needs_unfenced : 1;

		struct i915_request *rq;
		u32 *rq_cmd;
		unsigned int rq_size;
	} reloc_cache;

	u64 invalid_flags; /** Set of execobj.flags that are invalid */
	u32 context_flags; /** Set of execobj.flags to insert from the ctx */

	u32 batch_start_offset; /** Location within object of batch */
	u32 batch_len; /** Length of batch within object */
	u32 batch_flags; /** Flags composed for emit_bb_start() */

	/**
	 * Indicate either the size of the hastable used to resolve
	 * relocation handles, or if negative that we are using a direct
	 * index into the execobj[].
	 */
	int lut_size;
	struct hlist_head *buckets; /** ht for relocation handles */
};

#define exec_entry(EB, VMA) (&(EB)->exec[(VMA)->exec_flags - (EB)->flags])

/*
 * Used to convert any address to canonical form.
 * Starting from gen8, some commands (e.g. STATE_BASE_ADDRESS,
 * MI_LOAD_REGISTER_MEM and others, see Broadwell PRM Vol2a) require the
 * addresses to be in a canonical form:
 * "GraphicsAddress[63:48] are ignored by the HW and assumed to be in correct
 * canonical form [63:48] == [47]."
 */
#define GEN8_HIGH_ADDRESS_BIT 47
static inline u64 gen8_canonical_addr(u64 address)
{
	return sign_extend64(address, GEN8_HIGH_ADDRESS_BIT);
}

static inline u64 gen8_noncanonical_addr(u64 address)
{
	return address & GENMASK_ULL(GEN8_HIGH_ADDRESS_BIT, 0);
}

static inline bool eb_use_cmdparser(const struct i915_execbuffer *eb)
{
	return intel_engine_needs_cmd_parser(eb->engine) && eb->batch_len;
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
	    (vma->node.start + vma->node.size - 1) >> 32)
		return true;

	if (flags & __EXEC_OBJECT_NEEDS_MAP &&
	    !i915_vma_is_map_and_fenceable(vma))
		return true;

	return false;
}

static inline bool
eb_pin_vma(struct i915_execbuffer *eb,
	   const struct drm_i915_gem_exec_object2 *entry,
	   struct i915_vma *vma)
{
	unsigned int exec_flags = *vma->exec_flags;
	u64 pin_flags;

	if (vma->node.size)
		pin_flags = vma->node.start;
	else
		pin_flags = entry->offset & PIN_OFFSET_MASK;

	pin_flags |= PIN_USER | PIN_NOEVICT | PIN_OFFSET_FIXED;
	if (unlikely(exec_flags & EXEC_OBJECT_NEEDS_GTT))
		pin_flags |= PIN_GLOBAL;

	if (unlikely(i915_vma_pin(vma, 0, 0, pin_flags)))
		return false;

	if (unlikely(exec_flags & EXEC_OBJECT_NEEDS_FENCE)) {
		if (unlikely(i915_vma_pin_fence(vma))) {
			i915_vma_unpin(vma);
			return false;
		}

		if (vma->fence)
			exec_flags |= __EXEC_OBJECT_HAS_FENCE;
	}

	*vma->exec_flags = exec_flags | __EXEC_OBJECT_HAS_PIN;
	return !eb_vma_misplaced(entry, vma, exec_flags);
}

static inline void __eb_unreserve_vma(struct i915_vma *vma, unsigned int flags)
{
	GEM_BUG_ON(!(flags & __EXEC_OBJECT_HAS_PIN));

	if (unlikely(flags & __EXEC_OBJECT_HAS_FENCE))
		__i915_vma_unpin_fence(vma);

	__i915_vma_unpin(vma);
}

static inline void
eb_unreserve_vma(struct i915_vma *vma, unsigned int *flags)
{
	if (!(*flags & __EXEC_OBJECT_HAS_PIN))
		return;

	__eb_unreserve_vma(vma, *flags);
	*flags &= ~__EXEC_OBJECT_RESERVED;
}

static int
eb_validate_vma(struct i915_execbuffer *eb,
		struct drm_i915_gem_exec_object2 *entry,
		struct i915_vma *vma)
{
	if (unlikely(entry->flags & eb->invalid_flags))
		return -EINVAL;

	if (unlikely(entry->alignment && !is_power_of_2(entry->alignment)))
		return -EINVAL;

	/*
	 * Offset can be used as input (EXEC_OBJECT_PINNED), reject
	 * any non-page-aligned or non-canonical addresses.
	 */
	if (unlikely(entry->flags & EXEC_OBJECT_PINNED &&
		     entry->offset != gen8_canonical_addr(entry->offset & PAGE_MASK)))
		return -EINVAL;

	/* pad_to_size was once a reserved field, so sanitize it */
	if (entry->flags & EXEC_OBJECT_PAD_TO_SIZE) {
		if (unlikely(offset_in_page(entry->pad_to_size)))
			return -EINVAL;
	} else {
		entry->pad_to_size = 0;
	}

	if (unlikely(vma->exec_flags)) {
		DRM_DEBUG("Object [handle %d, index %d] appears more than once in object list\n",
			  entry->handle, (int)(entry - eb->exec));
		return -EINVAL;
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

	if (!(entry->flags & EXEC_OBJECT_PINNED))
		entry->flags |= eb->context_flags;

	return 0;
}

static int
eb_add_vma(struct i915_execbuffer *eb,
	   unsigned int i, unsigned batch_idx,
	   struct i915_vma *vma)
{
	struct drm_i915_gem_exec_object2 *entry = &eb->exec[i];
	int err;

	GEM_BUG_ON(i915_vma_is_closed(vma));

	if (!(eb->args->flags & __EXEC_VALIDATED)) {
		err = eb_validate_vma(eb, entry, vma);
		if (unlikely(err))
			return err;
	}

	if (eb->lut_size > 0) {
		vma->exec_handle = entry->handle;
		hlist_add_head(&vma->exec_node,
			       &eb->buckets[hash_32(entry->handle,
						    eb->lut_size)]);
	}

	if (entry->relocation_count)
		list_add_tail(&vma->reloc_link, &eb->relocs);

	/*
	 * Stash a pointer from the vma to execobj, so we can query its flags,
	 * size, alignment etc as provided by the user. Also we stash a pointer
	 * to the vma inside the execobj so that we can use a direct lookup
	 * to find the right target VMA when doing relocations.
	 */
	eb->vma[i] = vma;
	eb->flags[i] = entry->flags;
	vma->exec_flags = &eb->flags[i];

	/*
	 * SNA is doing fancy tricks with compressing batch buffers, which leads
	 * to negative relocation deltas. Usually that works out ok since the
	 * relocate address is still positive, except when the batch is placed
	 * very low in the GTT. Ensure this doesn't happen.
	 *
	 * Note that actual hangs have only been observed on gen7, but for
	 * paranoia do it everywhere.
	 */
	if (i == batch_idx) {
		if (entry->relocation_count &&
		    !(eb->flags[i] & EXEC_OBJECT_PINNED))
			eb->flags[i] |= __EXEC_OBJECT_NEEDS_BIAS;
		if (eb->reloc_cache.has_fence)
			eb->flags[i] |= EXEC_OBJECT_NEEDS_FENCE;

		eb->batch = vma;
	}

	err = 0;
	if (eb_pin_vma(eb, entry, vma)) {
		if (entry->offset != vma->node.start) {
			entry->offset = vma->node.start | UPDATE;
			eb->args->flags |= __EXEC_HAS_RELOC;
		}
	} else {
		eb_unreserve_vma(vma, vma->exec_flags);

		list_add_tail(&vma->exec_link, &eb->unbound);
		if (drm_mm_node_allocated(&vma->node))
			err = i915_vma_unbind(vma);
		if (unlikely(err))
			vma->exec_flags = NULL;
	}
	return err;
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

static int eb_reserve_vma(const struct i915_execbuffer *eb,
			  struct i915_vma *vma)
{
	struct drm_i915_gem_exec_object2 *entry = exec_entry(eb, vma);
	unsigned int exec_flags = *vma->exec_flags;
	u64 pin_flags;
	int err;

	pin_flags = PIN_USER | PIN_NONBLOCK;
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

	if (exec_flags & EXEC_OBJECT_PINNED) {
		pin_flags |= entry->offset | PIN_OFFSET_FIXED;
		pin_flags &= ~PIN_NONBLOCK; /* force overlapping checks */
	} else if (exec_flags & __EXEC_OBJECT_NEEDS_BIAS) {
		pin_flags |= BATCH_OFFSET_BIAS | PIN_OFFSET_BIAS;
	}

	err = i915_vma_pin(vma,
			   entry->pad_to_size, entry->alignment,
			   pin_flags);
	if (err)
		return err;

	if (entry->offset != vma->node.start) {
		entry->offset = vma->node.start | UPDATE;
		eb->args->flags |= __EXEC_HAS_RELOC;
	}

	if (unlikely(exec_flags & EXEC_OBJECT_NEEDS_FENCE)) {
		err = i915_vma_pin_fence(vma);
		if (unlikely(err)) {
			i915_vma_unpin(vma);
			return err;
		}

		if (vma->fence)
			exec_flags |= __EXEC_OBJECT_HAS_FENCE;
	}

	*vma->exec_flags = exec_flags | __EXEC_OBJECT_HAS_PIN;
	GEM_BUG_ON(eb_vma_misplaced(entry, vma, exec_flags));

	return 0;
}

static int eb_reserve(struct i915_execbuffer *eb)
{
	const unsigned int count = eb->buffer_count;
	struct list_head last;
	struct i915_vma *vma;
	unsigned int i, pass;
	int err;

	/*
	 * Attempt to pin all of the buffers into the GTT.
	 * This is done in 3 phases:
	 *
	 * 1a. Unbind all objects that do not match the GTT constraints for
	 *     the execbuffer (fenceable, mappable, alignment etc).
	 * 1b. Increment pin count for already bound objects.
	 * 2.  Bind new objects.
	 * 3.  Decrement pin count.
	 *
	 * This avoid unnecessary unbinding of later objects in order to make
	 * room for the earlier objects *unless* we need to defragment.
	 */

	pass = 0;
	err = 0;
	do {
		list_for_each_entry(vma, &eb->unbound, exec_link) {
			err = eb_reserve_vma(eb, vma);
			if (err)
				break;
		}
		if (err != -ENOSPC)
			return err;

		/* Resort *all* the objects into priority order */
		INIT_LIST_HEAD(&eb->unbound);
		INIT_LIST_HEAD(&last);
		for (i = 0; i < count; i++) {
			unsigned int flags = eb->flags[i];
			struct i915_vma *vma = eb->vma[i];

			if (flags & EXEC_OBJECT_PINNED &&
			    flags & __EXEC_OBJECT_HAS_PIN)
				continue;

			eb_unreserve_vma(vma, &eb->flags[i]);

			if (flags & EXEC_OBJECT_PINNED)
				list_add(&vma->exec_link, &eb->unbound);
			else if (flags & __EXEC_OBJECT_NEEDS_MAP)
				list_add_tail(&vma->exec_link, &eb->unbound);
			else
				list_add_tail(&vma->exec_link, &last);
		}
		list_splice_tail(&last, &eb->unbound);

		switch (pass++) {
		case 0:
			break;

		case 1:
			/* Too fragmented, unbind everything and retry */
			err = i915_gem_evict_vm(eb->vm);
			if (err)
				return err;
			break;

		default:
			return -ENOSPC;
		}
	} while (1);
}

static unsigned int eb_batch_index(const struct i915_execbuffer *eb)
{
	if (eb->args->flags & I915_EXEC_BATCH_FIRST)
		return 0;
	else
		return eb->buffer_count - 1;
}

static int eb_select_context(struct i915_execbuffer *eb)
{
	struct i915_gem_context *ctx;

	ctx = i915_gem_context_lookup(eb->file->driver_priv, eb->args->rsvd1);
	if (unlikely(!ctx))
		return -ENOENT;

	eb->ctx = ctx;
	eb->vm = ctx->ppgtt ? &ctx->ppgtt->vm : &eb->i915->ggtt.vm;

	eb->context_flags = 0;
	if (ctx->flags & CONTEXT_NO_ZEROMAP)
		eb->context_flags |= __EXEC_OBJECT_NEEDS_BIAS;

	return 0;
}

static int eb_lookup_vmas(struct i915_execbuffer *eb)
{
	struct radix_tree_root *handles_vma = &eb->ctx->handles_vma;
	struct drm_i915_gem_object *obj;
	unsigned int i, batch;
	int err;

	if (unlikely(i915_gem_context_is_closed(eb->ctx)))
		return -ENOENT;

	if (unlikely(i915_gem_context_is_banned(eb->ctx)))
		return -EIO;

	INIT_LIST_HEAD(&eb->relocs);
	INIT_LIST_HEAD(&eb->unbound);

	batch = eb_batch_index(eb);

	for (i = 0; i < eb->buffer_count; i++) {
		u32 handle = eb->exec[i].handle;
		struct i915_lut_handle *lut;
		struct i915_vma *vma;

		vma = radix_tree_lookup(handles_vma, handle);
		if (likely(vma))
			goto add_vma;

		obj = i915_gem_object_lookup(eb->file, handle);
		if (unlikely(!obj)) {
			err = -ENOENT;
			goto err_vma;
		}

		vma = i915_vma_instance(obj, eb->vm, NULL);
		if (unlikely(IS_ERR(vma))) {
			err = PTR_ERR(vma);
			goto err_obj;
		}

		lut = kmem_cache_alloc(eb->i915->luts, GFP_KERNEL);
		if (unlikely(!lut)) {
			err = -ENOMEM;
			goto err_obj;
		}

		err = radix_tree_insert(handles_vma, handle, vma);
		if (unlikely(err)) {
			kmem_cache_free(eb->i915->luts, lut);
			goto err_obj;
		}

		/* transfer ref to ctx */
		if (!vma->open_count++)
			i915_vma_reopen(vma);
		list_add(&lut->obj_link, &obj->lut_list);
		list_add(&lut->ctx_link, &eb->ctx->handles_list);
		lut->ctx = eb->ctx;
		lut->handle = handle;

add_vma:
		err = eb_add_vma(eb, i, batch, vma);
		if (unlikely(err))
			goto err_vma;

		GEM_BUG_ON(vma != eb->vma[i]);
		GEM_BUG_ON(vma->exec_flags != &eb->flags[i]);
		GEM_BUG_ON(drm_mm_node_allocated(&vma->node) &&
			   eb_vma_misplaced(&eb->exec[i], vma, eb->flags[i]));
	}

	eb->args->flags |= __EXEC_VALIDATED;
	return eb_reserve(eb);

err_obj:
	i915_gem_object_put(obj);
err_vma:
	eb->vma[i] = NULL;
	return err;
}

static struct i915_vma *
eb_get_vma(const struct i915_execbuffer *eb, unsigned long handle)
{
	if (eb->lut_size < 0) {
		if (handle >= -eb->lut_size)
			return NULL;
		return eb->vma[handle];
	} else {
		struct hlist_head *head;
		struct i915_vma *vma;

		head = &eb->buckets[hash_32(handle, eb->lut_size)];
		hlist_for_each_entry(vma, head, exec_node) {
			if (vma->exec_handle == handle)
				return vma;
		}
		return NULL;
	}
}

static void eb_release_vmas(const struct i915_execbuffer *eb)
{
	const unsigned int count = eb->buffer_count;
	unsigned int i;

	for (i = 0; i < count; i++) {
		struct i915_vma *vma = eb->vma[i];
		unsigned int flags = eb->flags[i];

		if (!vma)
			break;

		GEM_BUG_ON(vma->exec_flags != &eb->flags[i]);
		vma->exec_flags = NULL;
		eb->vma[i] = NULL;

		if (flags & __EXEC_OBJECT_HAS_PIN)
			__eb_unreserve_vma(vma, flags);

		if (flags & __EXEC_OBJECT_HAS_REF)
			i915_vma_put(vma);
	}
}

static void eb_reset_vmas(const struct i915_execbuffer *eb)
{
	eb_release_vmas(eb);
	if (eb->lut_size > 0)
		memset(eb->buckets, 0,
		       sizeof(struct hlist_head) << eb->lut_size);
}

static void eb_destroy(const struct i915_execbuffer *eb)
{
	GEM_BUG_ON(eb->reloc_cache.rq);

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
	cache->gen = INTEL_GEN(i915);
	cache->has_llc = HAS_LLC(i915);
	cache->use_64bit_reloc = HAS_64BIT_RELOC(i915);
	cache->has_fence = cache->gen < 4;
	cache->needs_unfenced = INTEL_INFO(i915)->unfenced_needs_alignment;
	cache->node.allocated = false;
	cache->rq = NULL;
	cache->rq_size = 0;
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
	return &i915->ggtt;
}

static void reloc_gpu_flush(struct reloc_cache *cache)
{
	GEM_BUG_ON(cache->rq_size >= cache->rq->batch->obj->base.size / sizeof(u32));
	cache->rq_cmd[cache->rq_size] = MI_BATCH_BUFFER_END;
	i915_gem_object_unpin_map(cache->rq->batch->obj);
	i915_gem_chipset_flush(cache->rq->i915);

	i915_request_add(cache->rq);
	cache->rq = NULL;
}

static void reloc_cache_reset(struct reloc_cache *cache)
{
	void *vaddr;

	if (cache->rq)
		reloc_gpu_flush(cache);

	if (!cache->vaddr)
		return;

	vaddr = unmask_page(cache->vaddr);
	if (cache->vaddr & KMAP) {
		if (cache->vaddr & CLFLUSH_AFTER)
			mb();

		kunmap_atomic(vaddr);
		i915_gem_obj_finish_shmem_access((struct drm_i915_gem_object *)cache->node.mm);
	} else {
		wmb();
		io_mapping_unmap_atomic((void __iomem *)vaddr);
		if (cache->node.allocated) {
			struct i915_ggtt *ggtt = cache_to_ggtt(cache);

			ggtt->vm.clear_range(&ggtt->vm,
					     cache->node.start,
					     cache->node.size);
			drm_mm_remove_node(&cache->node);
		} else {
			i915_vma_unpin((struct i915_vma *)cache->node.mm);
		}
	}

	cache->vaddr = 0;
	cache->page = -1;
}

static void *reloc_kmap(struct drm_i915_gem_object *obj,
			struct reloc_cache *cache,
			unsigned long page)
{
	void *vaddr;

	if (cache->vaddr) {
		kunmap_atomic(unmask_page(cache->vaddr));
	} else {
		unsigned int flushes;
		int err;

		err = i915_gem_obj_prepare_shmem_write(obj, &flushes);
		if (err)
			return ERR_PTR(err);

		BUILD_BUG_ON(KMAP & CLFLUSH_FLAGS);
		BUILD_BUG_ON((KMAP | CLFLUSH_FLAGS) & PAGE_MASK);

		cache->vaddr = flushes | KMAP;
		cache->node.mm = (void *)obj;
		if (flushes)
			mb();
	}

	vaddr = kmap_atomic(i915_gem_object_get_dirty_page(obj, page));
	cache->vaddr = unmask_flags(cache->vaddr) | (unsigned long)vaddr;
	cache->page = page;

	return vaddr;
}

static void *reloc_iomap(struct drm_i915_gem_object *obj,
			 struct reloc_cache *cache,
			 unsigned long page)
{
	struct i915_ggtt *ggtt = cache_to_ggtt(cache);
	unsigned long offset;
	void *vaddr;

	if (cache->vaddr) {
		io_mapping_unmap_atomic((void __force __iomem *) unmask_page(cache->vaddr));
	} else {
		struct i915_vma *vma;
		int err;

		if (use_cpu_reloc(cache, obj))
			return NULL;

		err = i915_gem_object_set_to_gtt_domain(obj, true);
		if (err)
			return ERR_PTR(err);

		vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0,
					       PIN_MAPPABLE |
					       PIN_NONBLOCK |
					       PIN_NONFAULT);
		if (IS_ERR(vma)) {
			memset(&cache->node, 0, sizeof(cache->node));
			err = drm_mm_insert_node_in_range
				(&ggtt->vm.mm, &cache->node,
				 PAGE_SIZE, 0, I915_COLOR_UNEVICTABLE,
				 0, ggtt->mappable_end,
				 DRM_MM_INSERT_LOW);
			if (err) /* no inactive aperture space, use cpu reloc */
				return NULL;
		} else {
			err = i915_vma_put_fence(vma);
			if (err) {
				i915_vma_unpin(vma);
				return ERR_PTR(err);
			}

			cache->node.start = vma->node.start;
			cache->node.mm = (void *)vma;
		}
	}

	offset = cache->node.start;
	if (cache->node.allocated) {
		wmb();
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

static void *reloc_vaddr(struct drm_i915_gem_object *obj,
			 struct reloc_cache *cache,
			 unsigned long page)
{
	void *vaddr;

	if (cache->page == page) {
		vaddr = unmask_page(cache->vaddr);
	} else {
		vaddr = NULL;
		if ((cache->vaddr & KMAP) == 0)
			vaddr = reloc_iomap(obj, cache, page);
		if (!vaddr)
			vaddr = reloc_kmap(obj, cache, page);
	}

	return vaddr;
}

static void clflush_write32(u32 *addr, u32 value, unsigned int flushes)
{
	if (unlikely(flushes & (CLFLUSH_BEFORE | CLFLUSH_AFTER))) {
		if (flushes & CLFLUSH_BEFORE) {
			clflushopt(addr);
			mb();
		}

		*addr = value;

		/*
		 * Writes to the same cacheline are serialised by the CPU
		 * (including clflush). On the write path, we only require
		 * that it hits memory in an orderly fashion and place
		 * mb barriers at the start and end of the relocation phase
		 * to ensure ordering of clflush wrt to the system.
		 */
		if (flushes & CLFLUSH_AFTER)
			clflushopt(addr);
	} else
		*addr = value;
}

static int __reloc_gpu_alloc(struct i915_execbuffer *eb,
			     struct i915_vma *vma,
			     unsigned int len)
{
	struct reloc_cache *cache = &eb->reloc_cache;
	struct drm_i915_gem_object *obj;
	struct i915_request *rq;
	struct i915_vma *batch;
	u32 *cmd;
	int err;

	GEM_BUG_ON(vma->obj->write_domain & I915_GEM_DOMAIN_CPU);

	obj = i915_gem_batch_pool_get(&eb->engine->batch_pool, PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	cmd = i915_gem_object_pin_map(obj,
				      cache->has_llc ?
				      I915_MAP_FORCE_WB :
				      I915_MAP_FORCE_WC);
	i915_gem_object_unpin_pages(obj);
	if (IS_ERR(cmd))
		return PTR_ERR(cmd);

	err = i915_gem_object_set_to_wc_domain(obj, false);
	if (err)
		goto err_unmap;

	batch = i915_vma_instance(obj, vma->vm, NULL);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto err_unmap;
	}

	err = i915_vma_pin(batch, 0, 0, PIN_USER | PIN_NONBLOCK);
	if (err)
		goto err_unmap;

	rq = i915_request_alloc(eb->engine, eb->ctx);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_unpin;
	}

	err = i915_request_await_object(rq, vma->obj, true);
	if (err)
		goto err_request;

	err = eb->engine->emit_bb_start(rq,
					batch->node.start, PAGE_SIZE,
					cache->gen > 5 ? 0 : I915_DISPATCH_SECURE);
	if (err)
		goto err_request;

	GEM_BUG_ON(!reservation_object_test_signaled_rcu(batch->resv, true));
	err = i915_vma_move_to_active(batch, rq, 0);
	if (err)
		goto skip_request;

	err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	if (err)
		goto skip_request;

	rq->batch = batch;
	i915_vma_unpin(batch);

	cache->rq = rq;
	cache->rq_cmd = cmd;
	cache->rq_size = 0;

	/* Return with batch mapping (cmd) still pinned */
	return 0;

skip_request:
	i915_request_skip(rq, err);
err_request:
	i915_request_add(rq);
err_unpin:
	i915_vma_unpin(batch);
err_unmap:
	i915_gem_object_unpin_map(obj);
	return err;
}

static u32 *reloc_gpu(struct i915_execbuffer *eb,
		      struct i915_vma *vma,
		      unsigned int len)
{
	struct reloc_cache *cache = &eb->reloc_cache;
	u32 *cmd;

	if (cache->rq_size > PAGE_SIZE/sizeof(u32) - (len + 1))
		reloc_gpu_flush(cache);

	if (unlikely(!cache->rq)) {
		int err;

		/* If we need to copy for the cmdparser, we will stall anyway */
		if (eb_use_cmdparser(eb))
			return ERR_PTR(-EWOULDBLOCK);

		if (!intel_engine_can_store_dword(eb->engine))
			return ERR_PTR(-ENODEV);

		err = __reloc_gpu_alloc(eb, vma, len);
		if (unlikely(err))
			return ERR_PTR(err);
	}

	cmd = cache->rq_cmd + cache->rq_size;
	cache->rq_size += len;

	return cmd;
}

static u64
relocate_entry(struct i915_vma *vma,
	       const struct drm_i915_gem_relocation_entry *reloc,
	       struct i915_execbuffer *eb,
	       const struct i915_vma *target)
{
	u64 offset = reloc->offset;
	u64 target_offset = relocation_target(reloc, target);
	bool wide = eb->reloc_cache.use_64bit_reloc;
	void *vaddr;

	if (!eb->reloc_cache.vaddr &&
	    (DBG_FORCE_RELOC == FORCE_GPU_RELOC ||
	     !reservation_object_test_signaled_rcu(vma->resv, true))) {
		const unsigned int gen = eb->reloc_cache.gen;
		unsigned int len;
		u32 *batch;
		u64 addr;

		if (wide)
			len = offset & 7 ? 8 : 5;
		else if (gen >= 4)
			len = 4;
		else
			len = 3;

		batch = reloc_gpu(eb, vma, len);
		if (IS_ERR(batch))
			goto repeat;

		addr = gen8_canonical_addr(vma->node.start + offset);
		if (wide) {
			if (offset & 7) {
				*batch++ = MI_STORE_DWORD_IMM_GEN4;
				*batch++ = lower_32_bits(addr);
				*batch++ = upper_32_bits(addr);
				*batch++ = lower_32_bits(target_offset);

				addr = gen8_canonical_addr(addr + 4);

				*batch++ = MI_STORE_DWORD_IMM_GEN4;
				*batch++ = lower_32_bits(addr);
				*batch++ = upper_32_bits(addr);
				*batch++ = upper_32_bits(target_offset);
			} else {
				*batch++ = (MI_STORE_DWORD_IMM_GEN4 | (1 << 21)) + 1;
				*batch++ = lower_32_bits(addr);
				*batch++ = upper_32_bits(addr);
				*batch++ = lower_32_bits(target_offset);
				*batch++ = upper_32_bits(target_offset);
			}
		} else if (gen >= 6) {
			*batch++ = MI_STORE_DWORD_IMM_GEN4;
			*batch++ = 0;
			*batch++ = addr;
			*batch++ = target_offset;
		} else if (gen >= 4) {
			*batch++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
			*batch++ = 0;
			*batch++ = addr;
			*batch++ = target_offset;
		} else {
			*batch++ = MI_STORE_DWORD_IMM | MI_MEM_VIRTUAL;
			*batch++ = addr;
			*batch++ = target_offset;
		}

		goto out;
	}

repeat:
	vaddr = reloc_vaddr(vma->obj, &eb->reloc_cache, offset >> PAGE_SHIFT);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

	clflush_write32(vaddr + offset_in_page(offset),
			lower_32_bits(target_offset),
			eb->reloc_cache.vaddr);

	if (wide) {
		offset += sizeof(u32);
		target_offset >>= 32;
		wide = false;
		goto repeat;
	}

out:
	return target->node.start | UPDATE;
}

static u64
eb_relocate_entry(struct i915_execbuffer *eb,
		  struct i915_vma *vma,
		  const struct drm_i915_gem_relocation_entry *reloc)
{
	struct i915_vma *target;
	int err;

	/* we've already hold a reference to all valid objects */
	target = eb_get_vma(eb, reloc->target_handle);
	if (unlikely(!target))
		return -ENOENT;

	/* Validate that the target is in a valid r/w GPU domain */
	if (unlikely(reloc->write_domain & (reloc->write_domain - 1))) {
		DRM_DEBUG("reloc with multiple write domains: "
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
		DRM_DEBUG("reloc with read/write non-GPU domains: "
			  "target %d offset %d "
			  "read %08x write %08x",
			  reloc->target_handle,
			  (int) reloc->offset,
			  reloc->read_domains,
			  reloc->write_domain);
		return -EINVAL;
	}

	if (reloc->write_domain) {
		*target->exec_flags |= EXEC_OBJECT_WRITE;

		/*
		 * Sandybridge PPGTT errata: We need a global gtt mapping
		 * for MI and pipe_control writes because the gpu doesn't
		 * properly redirect them through the ppgtt for non_secure
		 * batchbuffers.
		 */
		if (reloc->write_domain == I915_GEM_DOMAIN_INSTRUCTION &&
		    IS_GEN6(eb->i915)) {
			err = i915_vma_bind(target, target->obj->cache_level,
					    PIN_GLOBAL);
			if (WARN_ONCE(err,
				      "Unexpected failure to bind target VMA!"))
				return err;
		}
	}

	/*
	 * If the relocation already has the right value in it, no
	 * more work needs to be done.
	 */
	if (!DBG_FORCE_RELOC &&
	    gen8_canonical_addr(target->node.start) == reloc->presumed_offset)
		return 0;

	/* Check that the relocation address is valid... */
	if (unlikely(reloc->offset >
		     vma->size - (eb->reloc_cache.use_64bit_reloc ? 8 : 4))) {
		DRM_DEBUG("Relocation beyond object bounds: "
			  "target %d offset %d size %d.\n",
			  reloc->target_handle,
			  (int)reloc->offset,
			  (int)vma->size);
		return -EINVAL;
	}
	if (unlikely(reloc->offset & 3)) {
		DRM_DEBUG("Relocation not 4-byte aligned: "
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
	*vma->exec_flags &= ~EXEC_OBJECT_ASYNC;

	/* and update the user's relocation entry */
	return relocate_entry(vma, reloc, eb, target);
}

static int eb_relocate_vma(struct i915_execbuffer *eb, struct i915_vma *vma)
{
#define N_RELOC(x) ((x) / sizeof(struct drm_i915_gem_relocation_entry))
	struct drm_i915_gem_relocation_entry stack[N_RELOC(512)];
	struct drm_i915_gem_relocation_entry __user *urelocs;
	const struct drm_i915_gem_exec_object2 *entry = exec_entry(eb, vma);
	unsigned int remain;

	urelocs = u64_to_user_ptr(entry->relocs_ptr);
	remain = entry->relocation_count;
	if (unlikely(remain > N_RELOC(ULONG_MAX)))
		return -EINVAL;

	/*
	 * We must check that the entire relocation array is safe
	 * to read. However, if the array is not writable the user loses
	 * the updated relocation values.
	 */
	if (unlikely(!access_ok(VERIFY_READ, urelocs, remain*sizeof(*urelocs))))
		return -EFAULT;

	do {
		struct drm_i915_gem_relocation_entry *r = stack;
		unsigned int count =
			min_t(unsigned int, remain, ARRAY_SIZE(stack));
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
			u64 offset = eb_relocate_entry(eb, vma, r);

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
					   &urelocs[r-stack].presumed_offset);
			}
		} while (r++, --count);
		urelocs += ARRAY_SIZE(stack);
	} while (remain);
out:
	reloc_cache_reset(&eb->reloc_cache);
	return remain;
}

static int
eb_relocate_vma_slow(struct i915_execbuffer *eb, struct i915_vma *vma)
{
	const struct drm_i915_gem_exec_object2 *entry = exec_entry(eb, vma);
	struct drm_i915_gem_relocation_entry *relocs =
		u64_to_ptr(typeof(*relocs), entry->relocs_ptr);
	unsigned int i;
	int err;

	for (i = 0; i < entry->relocation_count; i++) {
		u64 offset = eb_relocate_entry(eb, vma, &relocs[i]);

		if ((s64)offset < 0) {
			err = (int)offset;
			goto err;
		}
	}
	err = 0;
err:
	reloc_cache_reset(&eb->reloc_cache);
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
	if (!access_ok(VERIFY_READ, addr, size))
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
	const unsigned int count = eb->buffer_count;
	unsigned int i;
	int err;

	for (i = 0; i < count; i++) {
		const unsigned int nreloc = eb->exec[i].relocation_count;
		struct drm_i915_gem_relocation_entry __user *urelocs;
		struct drm_i915_gem_relocation_entry *relocs;
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
			kvfree(relocs);
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
					     len)) {
				kvfree(relocs);
				err = -EFAULT;
				goto err;
			}

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
		user_access_begin();
		for (copied = 0; copied < nreloc; copied++)
			unsafe_put_user(-1,
					&urelocs[copied].presumed_offset,
					end_user);
end_user:
		user_access_end();

		eb->exec[i].relocs_ptr = (uintptr_t)relocs;
	}

	return 0;

err:
	while (i--) {
		struct drm_i915_gem_relocation_entry *relocs =
			u64_to_ptr(typeof(*relocs), eb->exec[i].relocs_ptr);
		if (eb->exec[i].relocation_count)
			kvfree(relocs);
	}
	return err;
}

static int eb_prefault_relocations(const struct i915_execbuffer *eb)
{
	const unsigned int count = eb->buffer_count;
	unsigned int i;

	if (unlikely(i915_modparams.prefault_disable))
		return 0;

	for (i = 0; i < count; i++) {
		int err;

		err = check_relocations(&eb->exec[i]);
		if (err)
			return err;
	}

	return 0;
}

static noinline int eb_relocate_slow(struct i915_execbuffer *eb)
{
	struct drm_device *dev = &eb->i915->drm;
	bool have_copy = false;
	struct i915_vma *vma;
	int err = 0;

repeat:
	if (signal_pending(current)) {
		err = -ERESTARTSYS;
		goto out;
	}

	/* We may process another execbuffer during the unlock... */
	eb_reset_vmas(eb);
	mutex_unlock(&dev->struct_mutex);

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
	if (err) {
		mutex_lock(&dev->struct_mutex);
		goto out;
	}

	/* A frequent cause for EAGAIN are currently unavailable client pages */
	flush_workqueue(eb->i915->mm.userptr_wq);

	err = i915_mutex_lock_interruptible(dev);
	if (err) {
		mutex_lock(&dev->struct_mutex);
		goto out;
	}

	/* reacquire the objects */
	err = eb_lookup_vmas(eb);
	if (err)
		goto err;

	GEM_BUG_ON(!eb->batch);

	list_for_each_entry(vma, &eb->relocs, reloc_link) {
		if (!have_copy) {
			pagefault_disable();
			err = eb_relocate_vma(eb, vma);
			pagefault_enable();
			if (err)
				goto repeat;
		} else {
			err = eb_relocate_vma_slow(eb, vma);
			if (err)
				goto err;
		}
	}

	/*
	 * Leave the user relocations as are, this is the painfully slow path,
	 * and we want to avoid the complication of dropping the lock whilst
	 * having buffers reserved in the aperture and so causing spurious
	 * ENOSPC for random operations.
	 */

err:
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

static int eb_relocate(struct i915_execbuffer *eb)
{
	if (eb_lookup_vmas(eb))
		goto slow;

	/* The objects are in their final locations, apply the relocations. */
	if (eb->args->flags & __EXEC_HAS_RELOC) {
		struct i915_vma *vma;

		list_for_each_entry(vma, &eb->relocs, reloc_link) {
			if (eb_relocate_vma(eb, vma))
				goto slow;
		}
	}

	return 0;

slow:
	return eb_relocate_slow(eb);
}

static int eb_move_to_gpu(struct i915_execbuffer *eb)
{
	const unsigned int count = eb->buffer_count;
	unsigned int i;
	int err;

	for (i = 0; i < count; i++) {
		unsigned int flags = eb->flags[i];
		struct i915_vma *vma = eb->vma[i];
		struct drm_i915_gem_object *obj = vma->obj;

		if (flags & EXEC_OBJECT_CAPTURE) {
			struct i915_capture_list *capture;

			capture = kmalloc(sizeof(*capture), GFP_KERNEL);
			if (unlikely(!capture))
				return -ENOMEM;

			capture->next = eb->request->capture_list;
			capture->vma = eb->vma[i];
			eb->request->capture_list = capture;
		}

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
		 */
		if (unlikely(obj->cache_dirty & ~obj->cache_coherent)) {
			if (i915_gem_clflush_object(obj, 0))
				flags &= ~EXEC_OBJECT_ASYNC;
		}

		if (flags & EXEC_OBJECT_ASYNC)
			continue;

		err = i915_request_await_object
			(eb->request, obj, flags & EXEC_OBJECT_WRITE);
		if (err)
			return err;
	}

	for (i = 0; i < count; i++) {
		unsigned int flags = eb->flags[i];
		struct i915_vma *vma = eb->vma[i];

		err = i915_vma_move_to_active(vma, eb->request, flags);
		if (unlikely(err)) {
			i915_request_skip(eb->request, err);
			return err;
		}

		__eb_unreserve_vma(vma, flags);
		vma->exec_flags = NULL;

		if (unlikely(flags & __EXEC_OBJECT_HAS_REF))
			i915_vma_put(vma);
	}
	eb->exec = NULL;

	/* Unconditionally flush any chipset caches (for streaming writes). */
	i915_gem_chipset_flush(eb->i915);

	return 0;
}

static bool i915_gem_check_execbuffer(struct drm_i915_gem_execbuffer2 *exec)
{
	if (exec->flags & __I915_EXEC_ILLEGAL_FLAGS)
		return false;

	/* Kernel clipping was a DRI1 misfeature */
	if (!(exec->flags & I915_EXEC_FENCE_ARRAY)) {
		if (exec->num_cliprects || exec->cliprects_ptr)
			return false;
	}

	if (exec->DR4 == 0xffffffff) {
		DRM_DEBUG("UXA submitting garbage DR4, fixing up\n");
		exec->DR4 = 0;
	}
	if (exec->DR1 || exec->DR4)
		return false;

	if ((exec->batch_start_offset | exec->batch_len) & 0x7)
		return false;

	return true;
}

static int i915_reset_gen7_sol_offsets(struct i915_request *rq)
{
	u32 *cs;
	int i;

	if (!IS_GEN7(rq->i915) || rq->engine->id != RCS) {
		DRM_DEBUG("sol reset is gen7/rcs only\n");
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

static struct i915_vma *eb_parse(struct i915_execbuffer *eb, bool is_master)
{
	struct drm_i915_gem_object *shadow_batch_obj;
	struct i915_vma *vma;
	int err;

	shadow_batch_obj = i915_gem_batch_pool_get(&eb->engine->batch_pool,
						   PAGE_ALIGN(eb->batch_len));
	if (IS_ERR(shadow_batch_obj))
		return ERR_CAST(shadow_batch_obj);

	err = intel_engine_cmd_parser(eb->engine,
				      eb->batch->obj,
				      shadow_batch_obj,
				      eb->batch_start_offset,
				      eb->batch_len,
				      is_master);
	if (err) {
		if (err == -EACCES) /* unhandled chained batch */
			vma = NULL;
		else
			vma = ERR_PTR(err);
		goto out;
	}

	vma = i915_gem_object_ggtt_pin(shadow_batch_obj, NULL, 0, 0, 0);
	if (IS_ERR(vma))
		goto out;

	eb->vma[eb->buffer_count] = i915_vma_get(vma);
	eb->flags[eb->buffer_count] =
		__EXEC_OBJECT_HAS_PIN | __EXEC_OBJECT_HAS_REF;
	vma->exec_flags = &eb->flags[eb->buffer_count];
	eb->buffer_count++;

out:
	i915_gem_object_unpin_pages(shadow_batch_obj);
	return vma;
}

static void
add_to_client(struct i915_request *rq, struct drm_file *file)
{
	rq->file_priv = file->driver_priv;
	list_add_tail(&rq->client_link, &rq->file_priv->mm.request_list);
}

static int eb_submit(struct i915_execbuffer *eb)
{
	int err;

	err = eb_move_to_gpu(eb);
	if (err)
		return err;

	if (eb->args->flags & I915_EXEC_GEN7_SOL_RESET) {
		err = i915_reset_gen7_sol_offsets(eb->request);
		if (err)
			return err;
	}

	err = eb->engine->emit_bb_start(eb->request,
					eb->batch->node.start +
					eb->batch_start_offset,
					eb->batch_len,
					eb->batch_flags);
	if (err)
		return err;

	return 0;
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
		file_priv->bsd_engine = atomic_fetch_xor(1,
			 &dev_priv->mm.bsd_engine_dispatch_index);

	return file_priv->bsd_engine;
}

#define I915_USER_RINGS (4)

static const enum intel_engine_id user_ring_map[I915_USER_RINGS + 1] = {
	[I915_EXEC_DEFAULT]	= RCS,
	[I915_EXEC_RENDER]	= RCS,
	[I915_EXEC_BLT]		= BCS,
	[I915_EXEC_BSD]		= VCS,
	[I915_EXEC_VEBOX]	= VECS
};

static struct intel_engine_cs *
eb_select_engine(struct drm_i915_private *dev_priv,
		 struct drm_file *file,
		 struct drm_i915_gem_execbuffer2 *args)
{
	unsigned int user_ring_id = args->flags & I915_EXEC_RING_MASK;
	struct intel_engine_cs *engine;

	if (user_ring_id > I915_USER_RINGS) {
		DRM_DEBUG("execbuf with unknown ring: %u\n", user_ring_id);
		return NULL;
	}

	if ((user_ring_id != I915_EXEC_BSD) &&
	    ((args->flags & I915_EXEC_BSD_MASK) != 0)) {
		DRM_DEBUG("execbuf with non bsd ring but with invalid "
			  "bsd dispatch flags: %d\n", (int)(args->flags));
		return NULL;
	}

	if (user_ring_id == I915_EXEC_BSD && HAS_BSD2(dev_priv)) {
		unsigned int bsd_idx = args->flags & I915_EXEC_BSD_MASK;

		if (bsd_idx == I915_EXEC_BSD_DEFAULT) {
			bsd_idx = gen8_dispatch_bsd_engine(dev_priv, file);
		} else if (bsd_idx >= I915_EXEC_BSD_RING1 &&
			   bsd_idx <= I915_EXEC_BSD_RING2) {
			bsd_idx >>= I915_EXEC_BSD_SHIFT;
			bsd_idx--;
		} else {
			DRM_DEBUG("execbuf with unknown bsd ring: %u\n",
				  bsd_idx);
			return NULL;
		}

		engine = dev_priv->engine[_VCS(bsd_idx)];
	} else {
		engine = dev_priv->engine[user_ring_map[user_ring_id]];
	}

	if (!engine) {
		DRM_DEBUG("execbuf with invalid ring: %u\n", user_ring_id);
		return NULL;
	}

	return engine;
}

static void
__free_fence_array(struct drm_syncobj **fences, unsigned int n)
{
	while (n--)
		drm_syncobj_put(ptr_mask_bits(fences[n], 2));
	kvfree(fences);
}

static struct drm_syncobj **
get_fence_array(struct drm_i915_gem_execbuffer2 *args,
		struct drm_file *file)
{
	const unsigned long nfences = args->num_cliprects;
	struct drm_i915_gem_exec_fence __user *user;
	struct drm_syncobj **fences;
	unsigned long n;
	int err;

	if (!(args->flags & I915_EXEC_FENCE_ARRAY))
		return NULL;

	/* Check multiplication overflow for access_ok() and kvmalloc_array() */
	BUILD_BUG_ON(sizeof(size_t) > sizeof(unsigned long));
	if (nfences > min_t(unsigned long,
			    ULONG_MAX / sizeof(*user),
			    SIZE_MAX / sizeof(*fences)))
		return ERR_PTR(-EINVAL);

	user = u64_to_user_ptr(args->cliprects_ptr);
	if (!access_ok(VERIFY_READ, user, nfences * sizeof(*user)))
		return ERR_PTR(-EFAULT);

	fences = kvmalloc_array(nfences, sizeof(*fences),
				__GFP_NOWARN | GFP_KERNEL);
	if (!fences)
		return ERR_PTR(-ENOMEM);

	for (n = 0; n < nfences; n++) {
		struct drm_i915_gem_exec_fence fence;
		struct drm_syncobj *syncobj;

		if (__copy_from_user(&fence, user++, sizeof(fence))) {
			err = -EFAULT;
			goto err;
		}

		if (fence.flags & __I915_EXEC_FENCE_UNKNOWN_FLAGS) {
			err = -EINVAL;
			goto err;
		}

		syncobj = drm_syncobj_find(file, fence.handle);
		if (!syncobj) {
			DRM_DEBUG("Invalid syncobj handle provided\n");
			err = -ENOENT;
			goto err;
		}

		BUILD_BUG_ON(~(ARCH_KMALLOC_MINALIGN - 1) &
			     ~__I915_EXEC_FENCE_UNKNOWN_FLAGS);

		fences[n] = ptr_pack_bits(syncobj, fence.flags, 2);
	}

	return fences;

err:
	__free_fence_array(fences, n);
	return ERR_PTR(err);
}

static void
put_fence_array(struct drm_i915_gem_execbuffer2 *args,
		struct drm_syncobj **fences)
{
	if (fences)
		__free_fence_array(fences, args->num_cliprects);
}

static int
await_fence_array(struct i915_execbuffer *eb,
		  struct drm_syncobj **fences)
{
	const unsigned int nfences = eb->args->num_cliprects;
	unsigned int n;
	int err;

	for (n = 0; n < nfences; n++) {
		struct drm_syncobj *syncobj;
		struct dma_fence *fence;
		unsigned int flags;

		syncobj = ptr_unpack_bits(fences[n], &flags, 2);
		if (!(flags & I915_EXEC_FENCE_WAIT))
			continue;

		fence = drm_syncobj_fence_get(syncobj);
		if (!fence)
			return -EINVAL;

		err = i915_request_await_dma_fence(eb->request, fence);
		dma_fence_put(fence);
		if (err < 0)
			return err;
	}

	return 0;
}

static void
signal_fence_array(struct i915_execbuffer *eb,
		   struct drm_syncobj **fences)
{
	const unsigned int nfences = eb->args->num_cliprects;
	struct dma_fence * const fence = &eb->request->fence;
	unsigned int n;

	for (n = 0; n < nfences; n++) {
		struct drm_syncobj *syncobj;
		unsigned int flags;

		syncobj = ptr_unpack_bits(fences[n], &flags, 2);
		if (!(flags & I915_EXEC_FENCE_SIGNAL))
			continue;

		drm_syncobj_replace_fence(syncobj, fence);
	}
}

static int
i915_gem_do_execbuffer(struct drm_device *dev,
		       struct drm_file *file,
		       struct drm_i915_gem_execbuffer2 *args,
		       struct drm_i915_gem_exec_object2 *exec,
		       struct drm_syncobj **fences)
{
	struct i915_execbuffer eb;
	struct dma_fence *in_fence = NULL;
	struct sync_file *out_fence = NULL;
	int out_fence_fd = -1;
	int err;

	BUILD_BUG_ON(__EXEC_INTERNAL_FLAGS & ~__I915_EXEC_ILLEGAL_FLAGS);
	BUILD_BUG_ON(__EXEC_OBJECT_INTERNAL_FLAGS &
		     ~__EXEC_OBJECT_UNKNOWN_FLAGS);

	eb.i915 = to_i915(dev);
	eb.file = file;
	eb.args = args;
	if (DBG_FORCE_RELOC || !(args->flags & I915_EXEC_NO_RELOC))
		args->flags |= __EXEC_HAS_RELOC;

	eb.exec = exec;
	eb.vma = (struct i915_vma **)(exec + args->buffer_count + 1);
	eb.vma[0] = NULL;
	eb.flags = (unsigned int *)(eb.vma + args->buffer_count + 1);

	eb.invalid_flags = __EXEC_OBJECT_UNKNOWN_FLAGS;
	if (USES_FULL_PPGTT(eb.i915))
		eb.invalid_flags |= EXEC_OBJECT_NEEDS_GTT;
	reloc_cache_init(&eb.reloc_cache, eb.i915);

	eb.buffer_count = args->buffer_count;
	eb.batch_start_offset = args->batch_start_offset;
	eb.batch_len = args->batch_len;

	eb.batch_flags = 0;
	if (args->flags & I915_EXEC_SECURE) {
		if (!drm_is_current_master(file) || !capable(CAP_SYS_ADMIN))
		    return -EPERM;

		eb.batch_flags |= I915_DISPATCH_SECURE;
	}
	if (args->flags & I915_EXEC_IS_PINNED)
		eb.batch_flags |= I915_DISPATCH_PINNED;

	eb.engine = eb_select_engine(eb.i915, file, args);
	if (!eb.engine)
		return -EINVAL;

	if (args->flags & I915_EXEC_RESOURCE_STREAMER) {
		if (!HAS_RESOURCE_STREAMER(eb.i915)) {
			DRM_DEBUG("RS is only allowed for Haswell, Gen8 and above\n");
			return -EINVAL;
		}
		if (eb.engine->id != RCS) {
			DRM_DEBUG("RS is not available on %s\n",
				 eb.engine->name);
			return -EINVAL;
		}

		eb.batch_flags |= I915_DISPATCH_RS;
	}

	if (args->flags & I915_EXEC_FENCE_IN) {
		in_fence = sync_file_get_fence(lower_32_bits(args->rsvd2));
		if (!in_fence)
			return -EINVAL;
	}

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

	/*
	 * Take a local wakeref for preparing to dispatch the execbuf as
	 * we expect to access the hardware fairly frequently in the
	 * process. Upon first dispatch, we acquire another prolonged
	 * wakeref that we hold until the GPU has been idle for at least
	 * 100ms.
	 */
	intel_runtime_pm_get(eb.i915);

	err = i915_mutex_lock_interruptible(dev);
	if (err)
		goto err_rpm;

	err = eb_relocate(&eb);
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

	if (unlikely(*eb.batch->exec_flags & EXEC_OBJECT_WRITE)) {
		DRM_DEBUG("Attempting to use self-modifying batch buffer\n");
		err = -EINVAL;
		goto err_vma;
	}
	if (eb.batch_start_offset > eb.batch->size ||
	    eb.batch_len > eb.batch->size - eb.batch_start_offset) {
		DRM_DEBUG("Attempting to use out-of-bounds batch\n");
		err = -EINVAL;
		goto err_vma;
	}

	if (eb_use_cmdparser(&eb)) {
		struct i915_vma *vma;

		vma = eb_parse(&eb, drm_is_current_master(file));
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto err_vma;
		}

		if (vma) {
			/*
			 * Batch parsed and accepted:
			 *
			 * Set the DISPATCH_SECURE bit to remove the NON_SECURE
			 * bit from MI_BATCH_BUFFER_START commands issued in
			 * the dispatch_execbuffer implementations. We
			 * specifically don't want that set on batches the
			 * command parser has accepted.
			 */
			eb.batch_flags |= I915_DISPATCH_SECURE;
			eb.batch_start_offset = 0;
			eb.batch = vma;
		}
	}

	if (eb.batch_len == 0)
		eb.batch_len = eb.batch->size - eb.batch_start_offset;

	/*
	 * snb/ivb/vlv conflate the "batch in ppgtt" bit with the "non-secure
	 * batch" bit. Hence we need to pin secure batches into the global gtt.
	 * hsw should have this fixed, but bdw mucks it up again. */
	if (eb.batch_flags & I915_DISPATCH_SECURE) {
		struct i915_vma *vma;

		/*
		 * So on first glance it looks freaky that we pin the batch here
		 * outside of the reservation loop. But:
		 * - The batch is already pinned into the relevant ppgtt, so we
		 *   already have the backing storage fully allocated.
		 * - No other BO uses the global gtt (well contexts, but meh),
		 *   so we don't really have issues with multiple objects not
		 *   fitting due to fragmentation.
		 * So this is actually safe.
		 */
		vma = i915_gem_object_ggtt_pin(eb.batch->obj, NULL, 0, 0, 0);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto err_vma;
		}

		eb.batch = vma;
	}

	/* All GPU relocation batches must be submitted prior to the user rq */
	GEM_BUG_ON(eb.reloc_cache.rq);

	/* Allocate a request for this batch buffer nice and early. */
	eb.request = i915_request_alloc(eb.engine, eb.ctx);
	if (IS_ERR(eb.request)) {
		err = PTR_ERR(eb.request);
		goto err_batch_unpin;
	}

	if (in_fence) {
		err = i915_request_await_dma_fence(eb.request, in_fence);
		if (err < 0)
			goto err_request;
	}

	if (fences) {
		err = await_fence_array(&eb, fences);
		if (err)
			goto err_request;
	}

	if (out_fence_fd != -1) {
		out_fence = sync_file_create(&eb.request->fence);
		if (!out_fence) {
			err = -ENOMEM;
			goto err_request;
		}
	}

	/*
	 * Whilst this request exists, batch_obj will be on the
	 * active_list, and so will hold the active reference. Only when this
	 * request is retired will the the batch_obj be moved onto the
	 * inactive_list and lose its active reference. Hence we do not need
	 * to explicitly hold another reference here.
	 */
	eb.request->batch = eb.batch;

	trace_i915_request_queue(eb.request, eb.batch_flags);
	err = eb_submit(&eb);
err_request:
	i915_request_add(eb.request);
	add_to_client(eb.request, file);

	if (fences)
		signal_fence_array(&eb, fences);

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

err_batch_unpin:
	if (eb.batch_flags & I915_DISPATCH_SECURE)
		i915_vma_unpin(eb.batch);
err_vma:
	if (eb.exec)
		eb_release_vmas(&eb);
	mutex_unlock(&dev->struct_mutex);
err_rpm:
	intel_runtime_pm_put(eb.i915);
	i915_gem_context_put(eb.ctx);
err_destroy:
	eb_destroy(&eb);
err_out_fence:
	if (out_fence_fd != -1)
		put_unused_fd(out_fence_fd);
err_in_fence:
	dma_fence_put(in_fence);
	return err;
}

static size_t eb_element_size(void)
{
	return (sizeof(struct drm_i915_gem_exec_object2) +
		sizeof(struct i915_vma *) +
		sizeof(unsigned int));
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

/*
 * Legacy execbuffer just creates an exec2 list from the original exec object
 * list array and passes it to the real function.
 */
int
i915_gem_execbuffer_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file)
{
	struct drm_i915_gem_execbuffer *args = data;
	struct drm_i915_gem_execbuffer2 exec2;
	struct drm_i915_gem_exec_object *exec_list = NULL;
	struct drm_i915_gem_exec_object2 *exec2_list = NULL;
	const size_t count = args->buffer_count;
	unsigned int i;
	int err;

	if (!check_buffer_count(count)) {
		DRM_DEBUG("execbuf2 with %zd buffers\n", count);
		return -EINVAL;
	}

	exec2.buffers_ptr = args->buffers_ptr;
	exec2.buffer_count = args->buffer_count;
	exec2.batch_start_offset = args->batch_start_offset;
	exec2.batch_len = args->batch_len;
	exec2.DR1 = args->DR1;
	exec2.DR4 = args->DR4;
	exec2.num_cliprects = args->num_cliprects;
	exec2.cliprects_ptr = args->cliprects_ptr;
	exec2.flags = I915_EXEC_RENDER;
	i915_execbuffer2_set_context_id(exec2, 0);

	if (!i915_gem_check_execbuffer(&exec2))
		return -EINVAL;

	/* Copy in the exec list from userland */
	exec_list = kvmalloc_array(count, sizeof(*exec_list),
				   __GFP_NOWARN | GFP_KERNEL);
	exec2_list = kvmalloc_array(count + 1, eb_element_size(),
				    __GFP_NOWARN | GFP_KERNEL);
	if (exec_list == NULL || exec2_list == NULL) {
		DRM_DEBUG("Failed to allocate exec list for %d buffers\n",
			  args->buffer_count);
		kvfree(exec_list);
		kvfree(exec2_list);
		return -ENOMEM;
	}
	err = copy_from_user(exec_list,
			     u64_to_user_ptr(args->buffers_ptr),
			     sizeof(*exec_list) * count);
	if (err) {
		DRM_DEBUG("copy %d exec entries failed %d\n",
			  args->buffer_count, err);
		kvfree(exec_list);
		kvfree(exec2_list);
		return -EFAULT;
	}

	for (i = 0; i < args->buffer_count; i++) {
		exec2_list[i].handle = exec_list[i].handle;
		exec2_list[i].relocation_count = exec_list[i].relocation_count;
		exec2_list[i].relocs_ptr = exec_list[i].relocs_ptr;
		exec2_list[i].alignment = exec_list[i].alignment;
		exec2_list[i].offset = exec_list[i].offset;
		if (INTEL_GEN(to_i915(dev)) < 4)
			exec2_list[i].flags = EXEC_OBJECT_NEEDS_FENCE;
		else
			exec2_list[i].flags = 0;
	}

	err = i915_gem_do_execbuffer(dev, file, &exec2, exec2_list, NULL);
	if (exec2.flags & __EXEC_HAS_RELOC) {
		struct drm_i915_gem_exec_object __user *user_exec_list =
			u64_to_user_ptr(args->buffers_ptr);

		/* Copy the new buffer offsets back to the user's exec list. */
		for (i = 0; i < args->buffer_count; i++) {
			if (!(exec2_list[i].offset & UPDATE))
				continue;

			exec2_list[i].offset =
				gen8_canonical_addr(exec2_list[i].offset & PIN_OFFSET_MASK);
			exec2_list[i].offset &= PIN_OFFSET_MASK;
			if (__copy_to_user(&user_exec_list[i].offset,
					   &exec2_list[i].offset,
					   sizeof(user_exec_list[i].offset)))
				break;
		}
	}

	kvfree(exec_list);
	kvfree(exec2_list);
	return err;
}

int
i915_gem_execbuffer2_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file)
{
	struct drm_i915_gem_execbuffer2 *args = data;
	struct drm_i915_gem_exec_object2 *exec2_list;
	struct drm_syncobj **fences = NULL;
	const size_t count = args->buffer_count;
	int err;

	if (!check_buffer_count(count)) {
		DRM_DEBUG("execbuf2 with %zd buffers\n", count);
		return -EINVAL;
	}

	if (!i915_gem_check_execbuffer(args))
		return -EINVAL;

	/* Allocate an extra slot for use by the command parser */
	exec2_list = kvmalloc_array(count + 1, eb_element_size(),
				    __GFP_NOWARN | GFP_KERNEL);
	if (exec2_list == NULL) {
		DRM_DEBUG("Failed to allocate exec list for %zd buffers\n",
			  count);
		return -ENOMEM;
	}
	if (copy_from_user(exec2_list,
			   u64_to_user_ptr(args->buffers_ptr),
			   sizeof(*exec2_list) * count)) {
		DRM_DEBUG("copy %zd exec entries failed\n", count);
		kvfree(exec2_list);
		return -EFAULT;
	}

	if (args->flags & I915_EXEC_FENCE_ARRAY) {
		fences = get_fence_array(args, file);
		if (IS_ERR(fences)) {
			kvfree(exec2_list);
			return PTR_ERR(fences);
		}
	}

	err = i915_gem_do_execbuffer(dev, file, args, exec2_list, fences);

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
		user_access_begin();
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
		user_access_end();
	}

	args->flags &= ~__I915_EXEC_UNKNOWN_FLAGS;
	put_fence_array(args, fences);
	kvfree(exec2_list);
	return err;
}
