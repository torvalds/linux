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

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_drv.h"

struct change_domains {
	uint32_t invalidate_domains;
	uint32_t flush_domains;
	uint32_t flush_rings;
};

/*
 * Set the next domain for the specified object. This
 * may not actually perform the necessary flushing/invaliding though,
 * as that may want to be batched with other set_domain operations
 *
 * This is (we hope) the only really tricky part of gem. The goal
 * is fairly simple -- track which caches hold bits of the object
 * and make sure they remain coherent. A few concrete examples may
 * help to explain how it works. For shorthand, we use the notation
 * (read_domains, write_domain), e.g. (CPU, CPU) to indicate the
 * a pair of read and write domain masks.
 *
 * Case 1: the batch buffer
 *
 *	1. Allocated
 *	2. Written by CPU
 *	3. Mapped to GTT
 *	4. Read by GPU
 *	5. Unmapped from GTT
 *	6. Freed
 *
 *	Let's take these a step at a time
 *
 *	1. Allocated
 *		Pages allocated from the kernel may still have
 *		cache contents, so we set them to (CPU, CPU) always.
 *	2. Written by CPU (using pwrite)
 *		The pwrite function calls set_domain (CPU, CPU) and
 *		this function does nothing (as nothing changes)
 *	3. Mapped by GTT
 *		This function asserts that the object is not
 *		currently in any GPU-based read or write domains
 *	4. Read by GPU
 *		i915_gem_execbuffer calls set_domain (COMMAND, 0).
 *		As write_domain is zero, this function adds in the
 *		current read domains (CPU+COMMAND, 0).
 *		flush_domains is set to CPU.
 *		invalidate_domains is set to COMMAND
 *		clflush is run to get data out of the CPU caches
 *		then i915_dev_set_domain calls i915_gem_flush to
 *		emit an MI_FLUSH and drm_agp_chipset_flush
 *	5. Unmapped from GTT
 *		i915_gem_object_unbind calls set_domain (CPU, CPU)
 *		flush_domains and invalidate_domains end up both zero
 *		so no flushing/invalidating happens
 *	6. Freed
 *		yay, done
 *
 * Case 2: The shared render buffer
 *
 *	1. Allocated
 *	2. Mapped to GTT
 *	3. Read/written by GPU
 *	4. set_domain to (CPU,CPU)
 *	5. Read/written by CPU
 *	6. Read/written by GPU
 *
 *	1. Allocated
 *		Same as last example, (CPU, CPU)
 *	2. Mapped to GTT
 *		Nothing changes (assertions find that it is not in the GPU)
 *	3. Read/written by GPU
 *		execbuffer calls set_domain (RENDER, RENDER)
 *		flush_domains gets CPU
 *		invalidate_domains gets GPU
 *		clflush (obj)
 *		MI_FLUSH and drm_agp_chipset_flush
 *	4. set_domain (CPU, CPU)
 *		flush_domains gets GPU
 *		invalidate_domains gets CPU
 *		wait_rendering (obj) to make sure all drawing is complete.
 *		This will include an MI_FLUSH to get the data from GPU
 *		to memory
 *		clflush (obj) to invalidate the CPU cache
 *		Another MI_FLUSH in i915_gem_flush (eliminate this somehow?)
 *	5. Read/written by CPU
 *		cache lines are loaded and dirtied
 *	6. Read written by GPU
 *		Same as last GPU access
 *
 * Case 3: The constant buffer
 *
 *	1. Allocated
 *	2. Written by CPU
 *	3. Read by GPU
 *	4. Updated (written) by CPU again
 *	5. Read by GPU
 *
 *	1. Allocated
 *		(CPU, CPU)
 *	2. Written by CPU
 *		(CPU, CPU)
 *	3. Read by GPU
 *		(CPU+RENDER, 0)
 *		flush_domains = CPU
 *		invalidate_domains = RENDER
 *		clflush (obj)
 *		MI_FLUSH
 *		drm_agp_chipset_flush
 *	4. Updated (written) by CPU again
 *		(CPU, CPU)
 *		flush_domains = 0 (no previous write domain)
 *		invalidate_domains = 0 (no new read domains)
 *	5. Read by GPU
 *		(CPU+RENDER, 0)
 *		flush_domains = CPU
 *		invalidate_domains = RENDER
 *		clflush (obj)
 *		MI_FLUSH
 *		drm_agp_chipset_flush
 */
static void
i915_gem_object_set_to_gpu_domain(struct drm_i915_gem_object *obj,
				  struct intel_ring_buffer *ring,
				  struct change_domains *cd)
{
	uint32_t invalidate_domains = 0, flush_domains = 0;

	/*
	 * If the object isn't moving to a new write domain,
	 * let the object stay in multiple read domains
	 */
	if (obj->base.pending_write_domain == 0)
		obj->base.pending_read_domains |= obj->base.read_domains;

	/*
	 * Flush the current write domain if
	 * the new read domains don't match. Invalidate
	 * any read domains which differ from the old
	 * write domain
	 */
	if (obj->base.write_domain &&
	    (((obj->base.write_domain != obj->base.pending_read_domains ||
	       obj->ring != ring)) ||
	     (obj->fenced_gpu_access && !obj->pending_fenced_gpu_access))) {
		flush_domains |= obj->base.write_domain;
		invalidate_domains |=
			obj->base.pending_read_domains & ~obj->base.write_domain;
	}
	/*
	 * Invalidate any read caches which may have
	 * stale data. That is, any new read domains.
	 */
	invalidate_domains |= obj->base.pending_read_domains & ~obj->base.read_domains;
	if ((flush_domains | invalidate_domains) & I915_GEM_DOMAIN_CPU)
		i915_gem_clflush_object(obj);

	/* blow away mappings if mapped through GTT */
	if ((flush_domains | invalidate_domains) & I915_GEM_DOMAIN_GTT)
		i915_gem_release_mmap(obj);

	/* The actual obj->write_domain will be updated with
	 * pending_write_domain after we emit the accumulated flush for all
	 * of our domain changes in execbuffers (which clears objects'
	 * write_domains).  So if we have a current write domain that we
	 * aren't changing, set pending_write_domain to that.
	 */
	if (flush_domains == 0 && obj->base.pending_write_domain == 0)
		obj->base.pending_write_domain = obj->base.write_domain;

	cd->invalidate_domains |= invalidate_domains;
	cd->flush_domains |= flush_domains;
	if (flush_domains & I915_GEM_GPU_DOMAINS)
		cd->flush_rings |= obj->ring->id;
	if (invalidate_domains & I915_GEM_GPU_DOMAINS)
		cd->flush_rings |= ring->id;
}

static int
i915_gem_execbuffer_relocate_entry(struct drm_i915_gem_object *obj,
				   struct drm_file *file_priv,
				   struct drm_i915_gem_exec_object2 *entry,
				   struct drm_i915_gem_relocation_entry *reloc)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_gem_object *target_obj;
	uint32_t target_offset;
	int ret = -EINVAL;

	target_obj = drm_gem_object_lookup(dev, file_priv,
					   reloc->target_handle);
	if (target_obj == NULL)
		return -ENOENT;

	target_offset = to_intel_bo(target_obj)->gtt_offset;

#if WATCH_RELOC
	DRM_INFO("%s: obj %p offset %08x target %d "
		 "read %08x write %08x gtt %08x "
		 "presumed %08x delta %08x\n",
		 __func__,
		 obj,
		 (int) reloc->offset,
		 (int) reloc->target_handle,
		 (int) reloc->read_domains,
		 (int) reloc->write_domain,
		 (int) target_offset,
		 (int) reloc->presumed_offset,
		 reloc->delta);
#endif

	/* The target buffer should have appeared before us in the
	 * exec_object list, so it should have a GTT space bound by now.
	 */
	if (target_offset == 0) {
		DRM_ERROR("No GTT space found for object %d\n",
			  reloc->target_handle);
		goto err;
	}

	/* Validate that the target is in a valid r/w GPU domain */
	if (reloc->write_domain & (reloc->write_domain - 1)) {
		DRM_ERROR("reloc with multiple write domains: "
			  "obj %p target %d offset %d "
			  "read %08x write %08x",
			  obj, reloc->target_handle,
			  (int) reloc->offset,
			  reloc->read_domains,
			  reloc->write_domain);
		goto err;
	}
	if (reloc->write_domain & I915_GEM_DOMAIN_CPU ||
	    reloc->read_domains & I915_GEM_DOMAIN_CPU) {
		DRM_ERROR("reloc with read/write CPU domains: "
			  "obj %p target %d offset %d "
			  "read %08x write %08x",
			  obj, reloc->target_handle,
			  (int) reloc->offset,
			  reloc->read_domains,
			  reloc->write_domain);
		goto err;
	}
	if (reloc->write_domain && target_obj->pending_write_domain &&
	    reloc->write_domain != target_obj->pending_write_domain) {
		DRM_ERROR("Write domain conflict: "
			  "obj %p target %d offset %d "
			  "new %08x old %08x\n",
			  obj, reloc->target_handle,
			  (int) reloc->offset,
			  reloc->write_domain,
			  target_obj->pending_write_domain);
		goto err;
	}

	target_obj->pending_read_domains |= reloc->read_domains;
	target_obj->pending_write_domain |= reloc->write_domain;

	/* If the relocation already has the right value in it, no
	 * more work needs to be done.
	 */
	if (target_offset == reloc->presumed_offset)
		goto out;

	/* Check that the relocation address is valid... */
	if (reloc->offset > obj->base.size - 4) {
		DRM_ERROR("Relocation beyond object bounds: "
			  "obj %p target %d offset %d size %d.\n",
			  obj, reloc->target_handle,
			  (int) reloc->offset,
			  (int) obj->base.size);
		goto err;
	}
	if (reloc->offset & 3) {
		DRM_ERROR("Relocation not 4-byte aligned: "
			  "obj %p target %d offset %d.\n",
			  obj, reloc->target_handle,
			  (int) reloc->offset);
		goto err;
	}

	/* and points to somewhere within the target object. */
	if (reloc->delta >= target_obj->size) {
		DRM_ERROR("Relocation beyond target object bounds: "
			  "obj %p target %d delta %d size %d.\n",
			  obj, reloc->target_handle,
			  (int) reloc->delta,
			  (int) target_obj->size);
		goto err;
	}

	reloc->delta += target_offset;
	if (obj->base.write_domain == I915_GEM_DOMAIN_CPU) {
		uint32_t page_offset = reloc->offset & ~PAGE_MASK;
		char *vaddr;

		vaddr = kmap_atomic(obj->pages[reloc->offset >> PAGE_SHIFT]);
		*(uint32_t *)(vaddr + page_offset) = reloc->delta;
		kunmap_atomic(vaddr);
	} else {
		struct drm_i915_private *dev_priv = dev->dev_private;
		uint32_t __iomem *reloc_entry;
		void __iomem *reloc_page;

		ret = i915_gem_object_set_to_gtt_domain(obj, 1);
		if (ret)
			goto err;

		/* Map the page containing the relocation we're going to perform.  */
		reloc->offset += obj->gtt_offset;
		reloc_page = io_mapping_map_atomic_wc(dev_priv->mm.gtt_mapping,
						      reloc->offset & PAGE_MASK);
		reloc_entry = (uint32_t __iomem *)
			(reloc_page + (reloc->offset & ~PAGE_MASK));
		iowrite32(reloc->delta, reloc_entry);
		io_mapping_unmap_atomic(reloc_page);
	}

	/* and update the user's relocation entry */
	reloc->presumed_offset = target_offset;

out:
	ret = 0;
err:
	drm_gem_object_unreference(target_obj);
	return ret;
}

static int
i915_gem_execbuffer_relocate_object(struct drm_i915_gem_object *obj,
				    struct drm_file *file_priv,
				    struct drm_i915_gem_exec_object2 *entry)
{
	struct drm_i915_gem_relocation_entry __user *user_relocs;
	int i, ret;

	user_relocs = (void __user *)(uintptr_t)entry->relocs_ptr;
	for (i = 0; i < entry->relocation_count; i++) {
		struct drm_i915_gem_relocation_entry reloc;

		if (__copy_from_user_inatomic(&reloc,
					      user_relocs+i,
					      sizeof(reloc)))
			return -EFAULT;

		ret = i915_gem_execbuffer_relocate_entry(obj, file_priv, entry, &reloc);
		if (ret)
			return ret;

		if (__copy_to_user_inatomic(&user_relocs[i].presumed_offset,
					    &reloc.presumed_offset,
					    sizeof(reloc.presumed_offset)))
			return -EFAULT;
	}

	return 0;
}

static int
i915_gem_execbuffer_relocate_object_slow(struct drm_i915_gem_object *obj,
					 struct drm_file *file_priv,
					 struct drm_i915_gem_exec_object2 *entry,
					 struct drm_i915_gem_relocation_entry *relocs)
{
	int i, ret;

	for (i = 0; i < entry->relocation_count; i++) {
		ret = i915_gem_execbuffer_relocate_entry(obj, file_priv, entry, &relocs[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int
i915_gem_execbuffer_relocate(struct drm_device *dev,
			     struct drm_file *file,
			     struct list_head *objects,
			     struct drm_i915_gem_exec_object2 *exec)
{
	struct drm_i915_gem_object *obj;
	int ret;

	list_for_each_entry(obj, objects, exec_list) {
		obj->base.pending_read_domains = 0;
		obj->base.pending_write_domain = 0;
		ret = i915_gem_execbuffer_relocate_object(obj, file, exec++);
		if (ret)
			return ret;
	}

	return 0;
}

static int
i915_gem_execbuffer_reserve(struct intel_ring_buffer *ring,
			    struct drm_file *file,
			    struct list_head *objects,
			    struct drm_i915_gem_exec_object2 *exec)
{
	struct drm_i915_gem_object *obj;
	struct drm_i915_gem_exec_object2 *entry;
	int ret, retry;

	/* Attempt to pin all of the buffers into the GTT.
	 * This is done in 3 phases:
	 *
	 * 1a. Unbind all objects that do not match the GTT constraints for
	 *     the execbuffer (fenceable, mappable, alignment etc).
	 * 1b. Increment pin count for already bound objects.
	 * 2.  Bind new objects.
	 * 3.  Decrement pin count.
	 *
	 * This avoid unnecessary unbinding of later objects in order to makr
	 * room for the earlier objects *unless* we need to defragment.
	 */
	retry = 0;
	do {
		ret = 0;

		/* Unbind any ill-fitting objects or pin. */
		entry = exec;
		list_for_each_entry(obj, objects, exec_list) {
			bool need_fence, need_mappable;

			if (!obj->gtt_space) {
				entry++;
				continue;
			}

			need_fence =
				entry->flags & EXEC_OBJECT_NEEDS_FENCE &&
				obj->tiling_mode != I915_TILING_NONE;
			need_mappable =
				entry->relocation_count ? true : need_fence;

			if ((entry->alignment && obj->gtt_offset & (entry->alignment - 1)) ||
			    (need_mappable && !obj->map_and_fenceable))
				ret = i915_gem_object_unbind(obj);
			else
				ret = i915_gem_object_pin(obj,
							  entry->alignment,
							  need_mappable);
			if (ret)
				goto err;

			entry++;
		}

		/* Bind fresh objects */
		entry = exec;
		list_for_each_entry(obj, objects, exec_list) {
			bool need_fence;

			need_fence =
				entry->flags & EXEC_OBJECT_NEEDS_FENCE &&
				obj->tiling_mode != I915_TILING_NONE;

			if (!obj->gtt_space) {
				bool need_mappable =
					entry->relocation_count ? true : need_fence;

				ret = i915_gem_object_pin(obj,
							  entry->alignment,
							  need_mappable);
				if (ret)
					break;
			}

			if (need_fence) {
				ret = i915_gem_object_get_fence(obj, ring, 1);
				if (ret)
					break;
			} else if (entry->flags & EXEC_OBJECT_NEEDS_FENCE &&
				   obj->tiling_mode == I915_TILING_NONE) {
				/* XXX pipelined! */
				ret = i915_gem_object_put_fence(obj);
				if (ret)
					break;
			}
			obj->pending_fenced_gpu_access = need_fence;

			entry->offset = obj->gtt_offset;
			entry++;
		}

		/* Decrement pin count for bound objects */
		list_for_each_entry(obj, objects, exec_list) {
			if (obj->gtt_space)
				i915_gem_object_unpin(obj);
		}

		if (ret != -ENOSPC || retry > 1)
			return ret;

		/* First attempt, just clear anything that is purgeable.
		 * Second attempt, clear the entire GTT.
		 */
		ret = i915_gem_evict_everything(ring->dev, retry == 0);
		if (ret)
			return ret;

		retry++;
	} while (1);

err:
	obj = list_entry(obj->exec_list.prev,
			 struct drm_i915_gem_object,
			 exec_list);
	while (objects != &obj->exec_list) {
		if (obj->gtt_space)
			i915_gem_object_unpin(obj);

		obj = list_entry(obj->exec_list.prev,
				 struct drm_i915_gem_object,
				 exec_list);
	}

	return ret;
}

static int
i915_gem_execbuffer_relocate_slow(struct drm_device *dev,
				  struct drm_file *file,
				  struct intel_ring_buffer *ring,
				  struct list_head *objects,
				  struct drm_i915_gem_exec_object2 *exec,
				  int count)
{
	struct drm_i915_gem_relocation_entry *reloc;
	struct drm_i915_gem_object *obj;
	int i, total, ret;

	mutex_unlock(&dev->struct_mutex);

	total = 0;
	for (i = 0; i < count; i++)
		total += exec[i].relocation_count;

	reloc = drm_malloc_ab(total, sizeof(*reloc));
	if (reloc == NULL) {
		mutex_lock(&dev->struct_mutex);
		return -ENOMEM;
	}

	total = 0;
	for (i = 0; i < count; i++) {
		struct drm_i915_gem_relocation_entry __user *user_relocs;

		user_relocs = (void __user *)(uintptr_t)exec[i].relocs_ptr;

		if (copy_from_user(reloc+total, user_relocs,
				   exec[i].relocation_count * sizeof(*reloc))) {
			ret = -EFAULT;
			mutex_lock(&dev->struct_mutex);
			goto err;
		}

		total += exec[i].relocation_count;
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret) {
		mutex_lock(&dev->struct_mutex);
		goto err;
	}

	ret = i915_gem_execbuffer_reserve(ring, file, objects, exec);
	if (ret)
		goto err;

	total = 0;
	list_for_each_entry(obj, objects, exec_list) {
		obj->base.pending_read_domains = 0;
		obj->base.pending_write_domain = 0;
		ret = i915_gem_execbuffer_relocate_object_slow(obj, file,
							       exec,
							       reloc + total);
		if (ret)
			goto err;

		total += exec->relocation_count;
		exec++;
	}

	/* Leave the user relocations as are, this is the painfully slow path,
	 * and we want to avoid the complication of dropping the lock whilst
	 * having buffers reserved in the aperture and so causing spurious
	 * ENOSPC for random operations.
	 */

err:
	drm_free_large(reloc);
	return ret;
}

static void
i915_gem_execbuffer_flush(struct drm_device *dev,
			  uint32_t invalidate_domains,
			  uint32_t flush_domains,
			  uint32_t flush_rings)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i;

	if (flush_domains & I915_GEM_DOMAIN_CPU)
		intel_gtt_chipset_flush();

	if ((flush_domains | invalidate_domains) & I915_GEM_GPU_DOMAINS) {
		for (i = 0; i < I915_NUM_RINGS; i++)
			if (flush_rings & (1 << i))
				i915_gem_flush_ring(dev, &dev_priv->ring[i],
						    invalidate_domains,
						    flush_domains);
	}
}

static int
i915_gem_execbuffer_sync_rings(struct drm_i915_gem_object *obj,
			       struct intel_ring_buffer *to)
{
	struct intel_ring_buffer *from = obj->ring;
	u32 seqno;
	int ret, idx;

	if (from == NULL || to == from)
		return 0;

	if (INTEL_INFO(obj->base.dev)->gen < 6)
		return i915_gem_object_wait_rendering(obj, true);

	idx = intel_ring_sync_index(from, to);

	seqno = obj->last_rendering_seqno;
	if (seqno <= from->sync_seqno[idx])
		return 0;

	if (seqno == from->outstanding_lazy_request) {
		struct drm_i915_gem_request *request;

		request = kzalloc(sizeof(*request), GFP_KERNEL);
		if (request == NULL)
			return -ENOMEM;

		ret = i915_add_request(obj->base.dev, NULL, request, from);
		if (ret) {
			kfree(request);
			return ret;
		}

		seqno = request->seqno;
	}

	from->sync_seqno[idx] = seqno;
	return intel_ring_sync(to, from, seqno - 1);
}

static int
i915_gem_execbuffer_move_to_gpu(struct intel_ring_buffer *ring,
				struct list_head *objects)
{
	struct drm_i915_gem_object *obj;
	struct change_domains cd;
	int ret;

	cd.invalidate_domains = 0;
	cd.flush_domains = 0;
	cd.flush_rings = 0;
	list_for_each_entry(obj, objects, exec_list)
		i915_gem_object_set_to_gpu_domain(obj, ring, &cd);

	if (cd.invalidate_domains | cd.flush_domains) {
#if WATCH_EXEC
		DRM_INFO("%s: invalidate_domains %08x flush_domains %08x\n",
			  __func__,
			 cd.invalidate_domains,
			 cd.flush_domains);
#endif
		i915_gem_execbuffer_flush(ring->dev,
					  cd.invalidate_domains,
					  cd.flush_domains,
					  cd.flush_rings);
	}

	list_for_each_entry(obj, objects, exec_list) {
		ret = i915_gem_execbuffer_sync_rings(obj, ring);
		if (ret)
			return ret;
	}

	return 0;
}

static bool
i915_gem_check_execbuffer(struct drm_i915_gem_execbuffer2 *exec)
{
	return ((exec->batch_start_offset | exec->batch_len) & 0x7) == 0;
}

static int
validate_exec_list(struct drm_i915_gem_exec_object2 *exec,
		   int count)
{
	int i;

	for (i = 0; i < count; i++) {
		char __user *ptr = (char __user *)(uintptr_t)exec[i].relocs_ptr;
		int length; /* limited by fault_in_pages_readable() */

		/* First check for malicious input causing overflow */
		if (exec[i].relocation_count >
		    INT_MAX / sizeof(struct drm_i915_gem_relocation_entry))
			return -EINVAL;

		length = exec[i].relocation_count *
			sizeof(struct drm_i915_gem_relocation_entry);
		if (!access_ok(VERIFY_READ, ptr, length))
			return -EFAULT;

		/* we may also need to update the presumed offsets */
		if (!access_ok(VERIFY_WRITE, ptr, length))
			return -EFAULT;

		if (fault_in_pages_readable(ptr, length))
			return -EFAULT;
	}

	return 0;
}

static int
i915_gem_execbuffer_wait_for_flips(struct intel_ring_buffer *ring,
				   struct list_head *objects)
{
	struct drm_i915_gem_object *obj;
	int flips;

	/* Check for any pending flips. As we only maintain a flip queue depth
	 * of 1, we can simply insert a WAIT for the next display flip prior
	 * to executing the batch and avoid stalling the CPU.
	 */
	flips = 0;
	list_for_each_entry(obj, objects, exec_list) {
		if (obj->base.write_domain)
			flips |= atomic_read(&obj->pending_flip);
	}
	if (flips) {
		int plane, flip_mask, ret;

		for (plane = 0; flips >> plane; plane++) {
			if (((flips >> plane) & 1) == 0)
				continue;

			if (plane)
				flip_mask = MI_WAIT_FOR_PLANE_B_FLIP;
			else
				flip_mask = MI_WAIT_FOR_PLANE_A_FLIP;

			ret = intel_ring_begin(ring, 2);
			if (ret)
				return ret;

			intel_ring_emit(ring, MI_WAIT_FOR_EVENT | flip_mask);
			intel_ring_emit(ring, MI_NOOP);
			intel_ring_advance(ring);
		}
	}

	return 0;
}

static void
i915_gem_execbuffer_move_to_active(struct list_head *objects,
				   struct intel_ring_buffer *ring,
				   u32 seqno)
{
	struct drm_i915_gem_object *obj;

	list_for_each_entry(obj, objects, exec_list) {
		obj->base.read_domains = obj->base.pending_read_domains;
		obj->base.write_domain = obj->base.pending_write_domain;
		obj->fenced_gpu_access = obj->pending_fenced_gpu_access;

		i915_gem_object_move_to_active(obj, ring, seqno);
		if (obj->base.write_domain) {
			obj->dirty = 1;
			obj->pending_gpu_write = true;
			list_move_tail(&obj->gpu_write_list,
				       &ring->gpu_write_list);
			intel_mark_busy(ring->dev, obj);
		}

		trace_i915_gem_object_change_domain(obj,
						    obj->base.read_domains,
						    obj->base.write_domain);
	}
}

static void
i915_gem_execbuffer_retire_commands(struct drm_device *dev,
				    struct drm_file *file,
				    struct intel_ring_buffer *ring)
{
	struct drm_i915_gem_request *request;
	u32 flush_domains;

	/*
	 * Ensure that the commands in the batch buffer are
	 * finished before the interrupt fires.
	 *
	 * The sampler always gets flushed on i965 (sigh).
	 */
	flush_domains = 0;
	if (INTEL_INFO(dev)->gen >= 4)
		flush_domains |= I915_GEM_DOMAIN_SAMPLER;

	ring->flush(ring, I915_GEM_DOMAIN_COMMAND, flush_domains);

	/* Add a breadcrumb for the completion of the batch buffer */
	request = kzalloc(sizeof(*request), GFP_KERNEL);
	if (request == NULL || i915_add_request(dev, file, request, ring)) {
		i915_gem_next_request_seqno(dev, ring);
		kfree(request);
	}
}

static int
i915_gem_do_execbuffer(struct drm_device *dev, void *data,
		       struct drm_file *file,
		       struct drm_i915_gem_execbuffer2 *args,
		       struct drm_i915_gem_exec_object2 *exec)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct list_head objects;
	struct drm_i915_gem_object *batch_obj;
	struct drm_clip_rect *cliprects = NULL;
	struct intel_ring_buffer *ring;
	u32 exec_start, exec_len;
	u32 seqno;
	int ret, i;

	if (!i915_gem_check_execbuffer(args)) {
		DRM_ERROR("execbuf with invalid offset/length\n");
		return -EINVAL;
	}

	ret = validate_exec_list(exec, args->buffer_count);
	if (ret)
		return ret;

#if WATCH_EXEC
	DRM_INFO("buffers_ptr %d buffer_count %d len %08x\n",
		  (int) args->buffers_ptr, args->buffer_count, args->batch_len);
#endif
	switch (args->flags & I915_EXEC_RING_MASK) {
	case I915_EXEC_DEFAULT:
	case I915_EXEC_RENDER:
		ring = &dev_priv->ring[RCS];
		break;
	case I915_EXEC_BSD:
		if (!HAS_BSD(dev)) {
			DRM_ERROR("execbuf with invalid ring (BSD)\n");
			return -EINVAL;
		}
		ring = &dev_priv->ring[VCS];
		break;
	case I915_EXEC_BLT:
		if (!HAS_BLT(dev)) {
			DRM_ERROR("execbuf with invalid ring (BLT)\n");
			return -EINVAL;
		}
		ring = &dev_priv->ring[BCS];
		break;
	default:
		DRM_ERROR("execbuf with unknown ring: %d\n",
			  (int)(args->flags & I915_EXEC_RING_MASK));
		return -EINVAL;
	}

	if (args->buffer_count < 1) {
		DRM_ERROR("execbuf with %d buffers\n", args->buffer_count);
		return -EINVAL;
	}

	if (args->num_cliprects != 0) {
		if (ring != &dev_priv->ring[RCS]) {
			DRM_ERROR("clip rectangles are only valid with the render ring\n");
			return -EINVAL;
		}

		cliprects = kmalloc(args->num_cliprects * sizeof(*cliprects),
				    GFP_KERNEL);
		if (cliprects == NULL) {
			ret = -ENOMEM;
			goto pre_mutex_err;
		}

		if (copy_from_user(cliprects,
				     (struct drm_clip_rect __user *)(uintptr_t)
				     args->cliprects_ptr,
				     sizeof(*cliprects)*args->num_cliprects)) {
			ret = -EFAULT;
			goto pre_mutex_err;
		}
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		goto pre_mutex_err;

	if (dev_priv->mm.suspended) {
		mutex_unlock(&dev->struct_mutex);
		ret = -EBUSY;
		goto pre_mutex_err;
	}

	/* Look up object handles */
	INIT_LIST_HEAD(&objects);
	for (i = 0; i < args->buffer_count; i++) {
		struct drm_i915_gem_object *obj;

		obj = to_intel_bo(drm_gem_object_lookup(dev, file,
							exec[i].handle));
		if (obj == NULL) {
			DRM_ERROR("Invalid object handle %d at index %d\n",
				   exec[i].handle, i);
			/* prevent error path from reading uninitialized data */
			ret = -ENOENT;
			goto err;
		}

		if (!list_empty(&obj->exec_list)) {
			DRM_ERROR("Object %p [handle %d, index %d] appears more than once in object list\n",
				   obj, exec[i].handle, i);
			ret = -EINVAL;
			goto err;
		}

		list_add_tail(&obj->exec_list, &objects);
	}

	/* Move the objects en-masse into the GTT, evicting if necessary. */
	ret = i915_gem_execbuffer_reserve(ring, file, &objects, exec);
	if (ret)
		goto err;

	/* The objects are in their final locations, apply the relocations. */
	ret = i915_gem_execbuffer_relocate(dev, file, &objects, exec);
	if (ret) {
		if (ret == -EFAULT) {
			ret = i915_gem_execbuffer_relocate_slow(dev, file, ring,
								&objects, exec,
								args->buffer_count);
			BUG_ON(!mutex_is_locked(&dev->struct_mutex));
		}
		if (ret)
			goto err;
	}

	/* Set the pending read domains for the batch buffer to COMMAND */
	batch_obj = list_entry(objects.prev,
			       struct drm_i915_gem_object,
			       exec_list);
	if (batch_obj->base.pending_write_domain) {
		DRM_ERROR("Attempting to use self-modifying batch buffer\n");
		ret = -EINVAL;
		goto err;
	}
	batch_obj->base.pending_read_domains |= I915_GEM_DOMAIN_COMMAND;

	ret = i915_gem_execbuffer_move_to_gpu(ring, &objects);
	if (ret)
		goto err;

	ret = i915_gem_execbuffer_wait_for_flips(ring, &objects);
	if (ret)
		goto err;

	seqno = i915_gem_next_request_seqno(dev, ring);
	for (i = 0; i < I915_NUM_RINGS-1; i++) {
		if (seqno < ring->sync_seqno[i]) {
			/* The GPU can not handle its semaphore value wrapping,
			 * so every billion or so execbuffers, we need to stall
			 * the GPU in order to reset the counters.
			 */
			ret = i915_gpu_idle(dev);
			if (ret)
				goto err;

			BUG_ON(ring->sync_seqno[i]);
		}
	}

	exec_start = batch_obj->gtt_offset + args->batch_start_offset;
	exec_len = args->batch_len;
	if (cliprects) {
		for (i = 0; i < args->num_cliprects; i++) {
			ret = i915_emit_box(dev, &cliprects[i],
					    args->DR1, args->DR4);
			if (ret)
				goto err;

			ret = ring->dispatch_execbuffer(ring,
							exec_start, exec_len);
			if (ret)
				goto err;
		}
	} else {
		ret = ring->dispatch_execbuffer(ring, exec_start, exec_len);
		if (ret)
			goto err;
	}

	i915_gem_execbuffer_move_to_active(&objects, ring, seqno);
	i915_gem_execbuffer_retire_commands(dev, file, ring);

err:
	while (!list_empty(&objects)) {
		struct drm_i915_gem_object *obj;

		obj = list_first_entry(&objects,
				       struct drm_i915_gem_object,
				       exec_list);
		list_del_init(&obj->exec_list);
		drm_gem_object_unreference(&obj->base);
	}

	mutex_unlock(&dev->struct_mutex);

pre_mutex_err:
	kfree(cliprects);
	return ret;
}

/*
 * Legacy execbuffer just creates an exec2 list from the original exec object
 * list array and passes it to the real function.
 */
int
i915_gem_execbuffer(struct drm_device *dev, void *data,
		    struct drm_file *file)
{
	struct drm_i915_gem_execbuffer *args = data;
	struct drm_i915_gem_execbuffer2 exec2;
	struct drm_i915_gem_exec_object *exec_list = NULL;
	struct drm_i915_gem_exec_object2 *exec2_list = NULL;
	int ret, i;

#if WATCH_EXEC
	DRM_INFO("buffers_ptr %d buffer_count %d len %08x\n",
		  (int) args->buffers_ptr, args->buffer_count, args->batch_len);
#endif

	if (args->buffer_count < 1) {
		DRM_ERROR("execbuf with %d buffers\n", args->buffer_count);
		return -EINVAL;
	}

	/* Copy in the exec list from userland */
	exec_list = drm_malloc_ab(sizeof(*exec_list), args->buffer_count);
	exec2_list = drm_malloc_ab(sizeof(*exec2_list), args->buffer_count);
	if (exec_list == NULL || exec2_list == NULL) {
		DRM_ERROR("Failed to allocate exec list for %d buffers\n",
			  args->buffer_count);
		drm_free_large(exec_list);
		drm_free_large(exec2_list);
		return -ENOMEM;
	}
	ret = copy_from_user(exec_list,
			     (struct drm_i915_relocation_entry __user *)
			     (uintptr_t) args->buffers_ptr,
			     sizeof(*exec_list) * args->buffer_count);
	if (ret != 0) {
		DRM_ERROR("copy %d exec entries failed %d\n",
			  args->buffer_count, ret);
		drm_free_large(exec_list);
		drm_free_large(exec2_list);
		return -EFAULT;
	}

	for (i = 0; i < args->buffer_count; i++) {
		exec2_list[i].handle = exec_list[i].handle;
		exec2_list[i].relocation_count = exec_list[i].relocation_count;
		exec2_list[i].relocs_ptr = exec_list[i].relocs_ptr;
		exec2_list[i].alignment = exec_list[i].alignment;
		exec2_list[i].offset = exec_list[i].offset;
		if (INTEL_INFO(dev)->gen < 4)
			exec2_list[i].flags = EXEC_OBJECT_NEEDS_FENCE;
		else
			exec2_list[i].flags = 0;
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

	ret = i915_gem_do_execbuffer(dev, data, file, &exec2, exec2_list);
	if (!ret) {
		/* Copy the new buffer offsets back to the user's exec list. */
		for (i = 0; i < args->buffer_count; i++)
			exec_list[i].offset = exec2_list[i].offset;
		/* ... and back out to userspace */
		ret = copy_to_user((struct drm_i915_relocation_entry __user *)
				   (uintptr_t) args->buffers_ptr,
				   exec_list,
				   sizeof(*exec_list) * args->buffer_count);
		if (ret) {
			ret = -EFAULT;
			DRM_ERROR("failed to copy %d exec entries "
				  "back to user (%d)\n",
				  args->buffer_count, ret);
		}
	}

	drm_free_large(exec_list);
	drm_free_large(exec2_list);
	return ret;
}

int
i915_gem_execbuffer2(struct drm_device *dev, void *data,
		     struct drm_file *file)
{
	struct drm_i915_gem_execbuffer2 *args = data;
	struct drm_i915_gem_exec_object2 *exec2_list = NULL;
	int ret;

#if WATCH_EXEC
	DRM_INFO("buffers_ptr %d buffer_count %d len %08x\n",
		  (int) args->buffers_ptr, args->buffer_count, args->batch_len);
#endif

	if (args->buffer_count < 1) {
		DRM_ERROR("execbuf2 with %d buffers\n", args->buffer_count);
		return -EINVAL;
	}

	exec2_list = drm_malloc_ab(sizeof(*exec2_list), args->buffer_count);
	if (exec2_list == NULL) {
		DRM_ERROR("Failed to allocate exec list for %d buffers\n",
			  args->buffer_count);
		return -ENOMEM;
	}
	ret = copy_from_user(exec2_list,
			     (struct drm_i915_relocation_entry __user *)
			     (uintptr_t) args->buffers_ptr,
			     sizeof(*exec2_list) * args->buffer_count);
	if (ret != 0) {
		DRM_ERROR("copy %d exec entries failed %d\n",
			  args->buffer_count, ret);
		drm_free_large(exec2_list);
		return -EFAULT;
	}

	ret = i915_gem_do_execbuffer(dev, data, file, args, exec2_list);
	if (!ret) {
		/* Copy the new buffer offsets back to the user's exec list. */
		ret = copy_to_user((struct drm_i915_relocation_entry __user *)
				   (uintptr_t) args->buffers_ptr,
				   exec2_list,
				   sizeof(*exec2_list) * args->buffer_count);
		if (ret) {
			ret = -EFAULT;
			DRM_ERROR("failed to copy %d exec entries "
				  "back to user (%d)\n",
				  args->buffer_count, ret);
		}
	}

	drm_free_large(exec2_list);
	return ret;
}

