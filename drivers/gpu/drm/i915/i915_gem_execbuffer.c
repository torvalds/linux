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
#include <linux/uaccess.h>

#include <drm/drmP.h>
#include <drm/i915_drm.h>

#include "i915_drv.h"
#include "i915_gem_dmabuf.h"
#include "i915_trace.h"
#include "intel_drv.h"
#include "intel_frontbuffer.h"

#define DBG_USE_CPU_RELOC 0 /* -1 force GTT relocs; 1 force CPU relocs */

#define  __EXEC_OBJECT_HAS_PIN		(1<<31)
#define  __EXEC_OBJECT_HAS_FENCE	(1<<30)
#define  __EXEC_OBJECT_NEEDS_MAP	(1<<29)
#define  __EXEC_OBJECT_NEEDS_BIAS	(1<<28)
#define  __EXEC_OBJECT_INTERNAL_FLAGS (0xf<<28) /* all of the above */

#define BATCH_OFFSET_BIAS (256*1024)

struct i915_execbuffer_params {
	struct drm_device               *dev;
	struct drm_file                 *file;
	struct i915_vma			*batch;
	u32				dispatch_flags;
	u32				args_batch_start_offset;
	struct intel_engine_cs          *engine;
	struct i915_gem_context         *ctx;
	struct drm_i915_gem_request     *request;
};

struct eb_vmas {
	struct drm_i915_private *i915;
	struct list_head vmas;
	int and;
	union {
		struct i915_vma *lut[0];
		struct hlist_head buckets[0];
	};
};

static struct eb_vmas *
eb_create(struct drm_i915_private *i915,
	  struct drm_i915_gem_execbuffer2 *args)
{
	struct eb_vmas *eb = NULL;

	if (args->flags & I915_EXEC_HANDLE_LUT) {
		unsigned size = args->buffer_count;
		size *= sizeof(struct i915_vma *);
		size += sizeof(struct eb_vmas);
		eb = kmalloc(size, GFP_TEMPORARY | __GFP_NOWARN | __GFP_NORETRY);
	}

	if (eb == NULL) {
		unsigned size = args->buffer_count;
		unsigned count = PAGE_SIZE / sizeof(struct hlist_head) / 2;
		BUILD_BUG_ON_NOT_POWER_OF_2(PAGE_SIZE / sizeof(struct hlist_head));
		while (count > 2*size)
			count >>= 1;
		eb = kzalloc(count*sizeof(struct hlist_head) +
			     sizeof(struct eb_vmas),
			     GFP_TEMPORARY);
		if (eb == NULL)
			return eb;

		eb->and = count - 1;
	} else
		eb->and = -args->buffer_count;

	eb->i915 = i915;
	INIT_LIST_HEAD(&eb->vmas);
	return eb;
}

static void
eb_reset(struct eb_vmas *eb)
{
	if (eb->and >= 0)
		memset(eb->buckets, 0, (eb->and+1)*sizeof(struct hlist_head));
}

static struct i915_vma *
eb_get_batch(struct eb_vmas *eb)
{
	struct i915_vma *vma = list_entry(eb->vmas.prev, typeof(*vma), exec_list);

	/*
	 * SNA is doing fancy tricks with compressing batch buffers, which leads
	 * to negative relocation deltas. Usually that works out ok since the
	 * relocate address is still positive, except when the batch is placed
	 * very low in the GTT. Ensure this doesn't happen.
	 *
	 * Note that actual hangs have only been observed on gen7, but for
	 * paranoia do it everywhere.
	 */
	if ((vma->exec_entry->flags & EXEC_OBJECT_PINNED) == 0)
		vma->exec_entry->flags |= __EXEC_OBJECT_NEEDS_BIAS;

	return vma;
}

static int
eb_lookup_vmas(struct eb_vmas *eb,
	       struct drm_i915_gem_exec_object2 *exec,
	       const struct drm_i915_gem_execbuffer2 *args,
	       struct i915_address_space *vm,
	       struct drm_file *file)
{
	struct drm_i915_gem_object *obj;
	struct list_head objects;
	int i, ret;

	INIT_LIST_HEAD(&objects);
	spin_lock(&file->table_lock);
	/* Grab a reference to the object and release the lock so we can lookup
	 * or create the VMA without using GFP_ATOMIC */
	for (i = 0; i < args->buffer_count; i++) {
		obj = to_intel_bo(idr_find(&file->object_idr, exec[i].handle));
		if (obj == NULL) {
			spin_unlock(&file->table_lock);
			DRM_DEBUG("Invalid object handle %d at index %d\n",
				   exec[i].handle, i);
			ret = -ENOENT;
			goto err;
		}

		if (!list_empty(&obj->obj_exec_link)) {
			spin_unlock(&file->table_lock);
			DRM_DEBUG("Object %p [handle %d, index %d] appears more than once in object list\n",
				   obj, exec[i].handle, i);
			ret = -EINVAL;
			goto err;
		}

		i915_gem_object_get(obj);
		list_add_tail(&obj->obj_exec_link, &objects);
	}
	spin_unlock(&file->table_lock);

	i = 0;
	while (!list_empty(&objects)) {
		struct i915_vma *vma;

		obj = list_first_entry(&objects,
				       struct drm_i915_gem_object,
				       obj_exec_link);

		/*
		 * NOTE: We can leak any vmas created here when something fails
		 * later on. But that's no issue since vma_unbind can deal with
		 * vmas which are not actually bound. And since only
		 * lookup_or_create exists as an interface to get at the vma
		 * from the (obj, vm) we don't run the risk of creating
		 * duplicated vmas for the same vm.
		 */
		vma = i915_gem_obj_lookup_or_create_vma(obj, vm, NULL);
		if (unlikely(IS_ERR(vma))) {
			DRM_DEBUG("Failed to lookup VMA\n");
			ret = PTR_ERR(vma);
			goto err;
		}

		/* Transfer ownership from the objects list to the vmas list. */
		list_add_tail(&vma->exec_list, &eb->vmas);
		list_del_init(&obj->obj_exec_link);

		vma->exec_entry = &exec[i];
		if (eb->and < 0) {
			eb->lut[i] = vma;
		} else {
			uint32_t handle = args->flags & I915_EXEC_HANDLE_LUT ? i : exec[i].handle;
			vma->exec_handle = handle;
			hlist_add_head(&vma->exec_node,
				       &eb->buckets[handle & eb->and]);
		}
		++i;
	}

	return 0;


err:
	while (!list_empty(&objects)) {
		obj = list_first_entry(&objects,
				       struct drm_i915_gem_object,
				       obj_exec_link);
		list_del_init(&obj->obj_exec_link);
		i915_gem_object_put(obj);
	}
	/*
	 * Objects already transfered to the vmas list will be unreferenced by
	 * eb_destroy.
	 */

	return ret;
}

static struct i915_vma *eb_get_vma(struct eb_vmas *eb, unsigned long handle)
{
	if (eb->and < 0) {
		if (handle >= -eb->and)
			return NULL;
		return eb->lut[handle];
	} else {
		struct hlist_head *head;
		struct i915_vma *vma;

		head = &eb->buckets[handle & eb->and];
		hlist_for_each_entry(vma, head, exec_node) {
			if (vma->exec_handle == handle)
				return vma;
		}
		return NULL;
	}
}

static void
i915_gem_execbuffer_unreserve_vma(struct i915_vma *vma)
{
	struct drm_i915_gem_exec_object2 *entry;

	if (!drm_mm_node_allocated(&vma->node))
		return;

	entry = vma->exec_entry;

	if (entry->flags & __EXEC_OBJECT_HAS_FENCE)
		i915_vma_unpin_fence(vma);

	if (entry->flags & __EXEC_OBJECT_HAS_PIN)
		__i915_vma_unpin(vma);

	entry->flags &= ~(__EXEC_OBJECT_HAS_FENCE | __EXEC_OBJECT_HAS_PIN);
}

static void eb_destroy(struct eb_vmas *eb)
{
	while (!list_empty(&eb->vmas)) {
		struct i915_vma *vma;

		vma = list_first_entry(&eb->vmas,
				       struct i915_vma,
				       exec_list);
		list_del_init(&vma->exec_list);
		i915_gem_execbuffer_unreserve_vma(vma);
		i915_vma_put(vma);
	}
	kfree(eb);
}

static inline int use_cpu_reloc(struct drm_i915_gem_object *obj)
{
	if (!i915_gem_object_has_struct_page(obj))
		return false;

	if (DBG_USE_CPU_RELOC)
		return DBG_USE_CPU_RELOC > 0;

	return (HAS_LLC(obj->base.dev) ||
		obj->base.write_domain == I915_GEM_DOMAIN_CPU ||
		obj->cache_level != I915_CACHE_NONE);
}

/* Used to convert any address to canonical form.
 * Starting from gen8, some commands (e.g. STATE_BASE_ADDRESS,
 * MI_LOAD_REGISTER_MEM and others, see Broadwell PRM Vol2a) require the
 * addresses to be in a canonical form:
 * "GraphicsAddress[63:48] are ignored by the HW and assumed to be in correct
 * canonical form [63:48] == [47]."
 */
#define GEN8_HIGH_ADDRESS_BIT 47
static inline uint64_t gen8_canonical_addr(uint64_t address)
{
	return sign_extend64(address, GEN8_HIGH_ADDRESS_BIT);
}

static inline uint64_t gen8_noncanonical_addr(uint64_t address)
{
	return address & ((1ULL << (GEN8_HIGH_ADDRESS_BIT + 1)) - 1);
}

static inline uint64_t
relocation_target(const struct drm_i915_gem_relocation_entry *reloc,
		  uint64_t target_offset)
{
	return gen8_canonical_addr((int)reloc->delta + target_offset);
}

struct reloc_cache {
	struct drm_i915_private *i915;
	struct drm_mm_node node;
	unsigned long vaddr;
	unsigned int page;
	bool use_64bit_reloc;
};

static void reloc_cache_init(struct reloc_cache *cache,
			     struct drm_i915_private *i915)
{
	cache->page = -1;
	cache->vaddr = 0;
	cache->i915 = i915;
	cache->use_64bit_reloc = INTEL_GEN(cache->i915) >= 8;
	cache->node.allocated = false;
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

static void reloc_cache_fini(struct reloc_cache *cache)
{
	void *vaddr;

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
			struct i915_ggtt *ggtt = &cache->i915->ggtt;

			ggtt->base.clear_range(&ggtt->base,
					       cache->node.start,
					       cache->node.size,
					       true);
			drm_mm_remove_node(&cache->node);
		} else {
			i915_vma_unpin((struct i915_vma *)cache->node.mm);
		}
	}
}

static void *reloc_kmap(struct drm_i915_gem_object *obj,
			struct reloc_cache *cache,
			int page)
{
	void *vaddr;

	if (cache->vaddr) {
		kunmap_atomic(unmask_page(cache->vaddr));
	} else {
		unsigned int flushes;
		int ret;

		ret = i915_gem_obj_prepare_shmem_write(obj, &flushes);
		if (ret)
			return ERR_PTR(ret);

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
			 int page)
{
	struct i915_ggtt *ggtt = &cache->i915->ggtt;
	unsigned long offset;
	void *vaddr;

	if (cache->node.allocated) {
		wmb();
		ggtt->base.insert_page(&ggtt->base,
				       i915_gem_object_get_dma_address(obj, page),
				       cache->node.start, I915_CACHE_NONE, 0);
		cache->page = page;
		return unmask_page(cache->vaddr);
	}

	if (cache->vaddr) {
		io_mapping_unmap_atomic(unmask_page(cache->vaddr));
	} else {
		struct i915_vma *vma;
		int ret;

		if (use_cpu_reloc(obj))
			return NULL;

		ret = i915_gem_object_set_to_gtt_domain(obj, true);
		if (ret)
			return ERR_PTR(ret);

		vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0,
					       PIN_MAPPABLE | PIN_NONBLOCK);
		if (IS_ERR(vma)) {
			memset(&cache->node, 0, sizeof(cache->node));
			ret = drm_mm_insert_node_in_range_generic
				(&ggtt->base.mm, &cache->node,
				 4096, 0, 0,
				 0, ggtt->mappable_end,
				 DRM_MM_SEARCH_DEFAULT,
				 DRM_MM_CREATE_DEFAULT);
			if (ret) /* no inactive aperture space, use cpu reloc */
				return NULL;
		} else {
			ret = i915_vma_put_fence(vma);
			if (ret) {
				i915_vma_unpin(vma);
				return ERR_PTR(ret);
			}

			cache->node.start = vma->node.start;
			cache->node.mm = (void *)vma;
		}
	}

	offset = cache->node.start;
	if (cache->node.allocated) {
		ggtt->base.insert_page(&ggtt->base,
				       i915_gem_object_get_dma_address(obj, page),
				       offset, I915_CACHE_NONE, 0);
	} else {
		offset += page << PAGE_SHIFT;
	}

	vaddr = io_mapping_map_atomic_wc(&cache->i915->ggtt.mappable, offset);
	cache->page = page;
	cache->vaddr = (unsigned long)vaddr;

	return vaddr;
}

static void *reloc_vaddr(struct drm_i915_gem_object *obj,
			 struct reloc_cache *cache,
			 int page)
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

		/* Writes to the same cacheline are serialised by the CPU
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

static int
relocate_entry(struct drm_i915_gem_object *obj,
	       const struct drm_i915_gem_relocation_entry *reloc,
	       struct reloc_cache *cache,
	       u64 target_offset)
{
	u64 offset = reloc->offset;
	bool wide = cache->use_64bit_reloc;
	void *vaddr;

	target_offset = relocation_target(reloc, target_offset);
repeat:
	vaddr = reloc_vaddr(obj, cache, offset >> PAGE_SHIFT);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

	clflush_write32(vaddr + offset_in_page(offset),
			lower_32_bits(target_offset),
			cache->vaddr);

	if (wide) {
		offset += sizeof(u32);
		target_offset >>= 32;
		wide = false;
		goto repeat;
	}

	return 0;
}

static bool object_is_idle(struct drm_i915_gem_object *obj)
{
	unsigned long active = i915_gem_object_get_active(obj);
	int idx;

	for_each_active(active, idx) {
		if (!i915_gem_active_is_idle(&obj->last_read[idx],
					     &obj->base.dev->struct_mutex))
			return false;
	}

	return true;
}

static int
i915_gem_execbuffer_relocate_entry(struct drm_i915_gem_object *obj,
				   struct eb_vmas *eb,
				   struct drm_i915_gem_relocation_entry *reloc,
				   struct reloc_cache *cache)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_gem_object *target_obj;
	struct drm_i915_gem_object *target_i915_obj;
	struct i915_vma *target_vma;
	uint64_t target_offset;
	int ret;

	/* we've already hold a reference to all valid objects */
	target_vma = eb_get_vma(eb, reloc->target_handle);
	if (unlikely(target_vma == NULL))
		return -ENOENT;
	target_i915_obj = target_vma->obj;
	target_obj = &target_vma->obj->base;

	target_offset = gen8_canonical_addr(target_vma->node.start);

	/* Sandybridge PPGTT errata: We need a global gtt mapping for MI and
	 * pipe_control writes because the gpu doesn't properly redirect them
	 * through the ppgtt for non_secure batchbuffers. */
	if (unlikely(IS_GEN6(dev) &&
	    reloc->write_domain == I915_GEM_DOMAIN_INSTRUCTION)) {
		ret = i915_vma_bind(target_vma, target_i915_obj->cache_level,
				    PIN_GLOBAL);
		if (WARN_ONCE(ret, "Unexpected failure to bind target VMA!"))
			return ret;
	}

	/* Validate that the target is in a valid r/w GPU domain */
	if (unlikely(reloc->write_domain & (reloc->write_domain - 1))) {
		DRM_DEBUG("reloc with multiple write domains: "
			  "obj %p target %d offset %d "
			  "read %08x write %08x",
			  obj, reloc->target_handle,
			  (int) reloc->offset,
			  reloc->read_domains,
			  reloc->write_domain);
		return -EINVAL;
	}
	if (unlikely((reloc->write_domain | reloc->read_domains)
		     & ~I915_GEM_GPU_DOMAINS)) {
		DRM_DEBUG("reloc with read/write non-GPU domains: "
			  "obj %p target %d offset %d "
			  "read %08x write %08x",
			  obj, reloc->target_handle,
			  (int) reloc->offset,
			  reloc->read_domains,
			  reloc->write_domain);
		return -EINVAL;
	}

	target_obj->pending_read_domains |= reloc->read_domains;
	target_obj->pending_write_domain |= reloc->write_domain;

	/* If the relocation already has the right value in it, no
	 * more work needs to be done.
	 */
	if (target_offset == reloc->presumed_offset)
		return 0;

	/* Check that the relocation address is valid... */
	if (unlikely(reloc->offset >
		     obj->base.size - (cache->use_64bit_reloc ? 8 : 4))) {
		DRM_DEBUG("Relocation beyond object bounds: "
			  "obj %p target %d offset %d size %d.\n",
			  obj, reloc->target_handle,
			  (int) reloc->offset,
			  (int) obj->base.size);
		return -EINVAL;
	}
	if (unlikely(reloc->offset & 3)) {
		DRM_DEBUG("Relocation not 4-byte aligned: "
			  "obj %p target %d offset %d.\n",
			  obj, reloc->target_handle,
			  (int) reloc->offset);
		return -EINVAL;
	}

	/* We can't wait for rendering with pagefaults disabled */
	if (pagefault_disabled() && !object_is_idle(obj))
		return -EFAULT;

	ret = relocate_entry(obj, reloc, cache, target_offset);
	if (ret)
		return ret;

	/* and update the user's relocation entry */
	reloc->presumed_offset = target_offset;
	return 0;
}

static int
i915_gem_execbuffer_relocate_vma(struct i915_vma *vma,
				 struct eb_vmas *eb)
{
#define N_RELOC(x) ((x) / sizeof(struct drm_i915_gem_relocation_entry))
	struct drm_i915_gem_relocation_entry stack_reloc[N_RELOC(512)];
	struct drm_i915_gem_relocation_entry __user *user_relocs;
	struct drm_i915_gem_exec_object2 *entry = vma->exec_entry;
	struct reloc_cache cache;
	int remain, ret = 0;

	user_relocs = u64_to_user_ptr(entry->relocs_ptr);
	reloc_cache_init(&cache, eb->i915);

	remain = entry->relocation_count;
	while (remain) {
		struct drm_i915_gem_relocation_entry *r = stack_reloc;
		int count = remain;
		if (count > ARRAY_SIZE(stack_reloc))
			count = ARRAY_SIZE(stack_reloc);
		remain -= count;

		if (__copy_from_user_inatomic(r, user_relocs, count*sizeof(r[0]))) {
			ret = -EFAULT;
			goto out;
		}

		do {
			u64 offset = r->presumed_offset;

			ret = i915_gem_execbuffer_relocate_entry(vma->obj, eb, r, &cache);
			if (ret)
				goto out;

			if (r->presumed_offset != offset &&
			    __put_user(r->presumed_offset,
				       &user_relocs->presumed_offset)) {
				ret = -EFAULT;
				goto out;
			}

			user_relocs++;
			r++;
		} while (--count);
	}

out:
	reloc_cache_fini(&cache);
	return ret;
#undef N_RELOC
}

static int
i915_gem_execbuffer_relocate_vma_slow(struct i915_vma *vma,
				      struct eb_vmas *eb,
				      struct drm_i915_gem_relocation_entry *relocs)
{
	const struct drm_i915_gem_exec_object2 *entry = vma->exec_entry;
	struct reloc_cache cache;
	int i, ret = 0;

	reloc_cache_init(&cache, eb->i915);
	for (i = 0; i < entry->relocation_count; i++) {
		ret = i915_gem_execbuffer_relocate_entry(vma->obj, eb, &relocs[i], &cache);
		if (ret)
			break;
	}
	reloc_cache_fini(&cache);

	return ret;
}

static int
i915_gem_execbuffer_relocate(struct eb_vmas *eb)
{
	struct i915_vma *vma;
	int ret = 0;

	/* This is the fast path and we cannot handle a pagefault whilst
	 * holding the struct mutex lest the user pass in the relocations
	 * contained within a mmaped bo. For in such a case we, the page
	 * fault handler would call i915_gem_fault() and we would try to
	 * acquire the struct mutex again. Obviously this is bad and so
	 * lockdep complains vehemently.
	 */
	pagefault_disable();
	list_for_each_entry(vma, &eb->vmas, exec_list) {
		ret = i915_gem_execbuffer_relocate_vma(vma, eb);
		if (ret)
			break;
	}
	pagefault_enable();

	return ret;
}

static bool only_mappable_for_reloc(unsigned int flags)
{
	return (flags & (EXEC_OBJECT_NEEDS_FENCE | __EXEC_OBJECT_NEEDS_MAP)) ==
		__EXEC_OBJECT_NEEDS_MAP;
}

static int
i915_gem_execbuffer_reserve_vma(struct i915_vma *vma,
				struct intel_engine_cs *engine,
				bool *need_reloc)
{
	struct drm_i915_gem_object *obj = vma->obj;
	struct drm_i915_gem_exec_object2 *entry = vma->exec_entry;
	uint64_t flags;
	int ret;

	flags = PIN_USER;
	if (entry->flags & EXEC_OBJECT_NEEDS_GTT)
		flags |= PIN_GLOBAL;

	if (!drm_mm_node_allocated(&vma->node)) {
		/* Wa32bitGeneralStateOffset & Wa32bitInstructionBaseOffset,
		 * limit address to the first 4GBs for unflagged objects.
		 */
		if ((entry->flags & EXEC_OBJECT_SUPPORTS_48B_ADDRESS) == 0)
			flags |= PIN_ZONE_4G;
		if (entry->flags & __EXEC_OBJECT_NEEDS_MAP)
			flags |= PIN_GLOBAL | PIN_MAPPABLE;
		if (entry->flags & __EXEC_OBJECT_NEEDS_BIAS)
			flags |= BATCH_OFFSET_BIAS | PIN_OFFSET_BIAS;
		if (entry->flags & EXEC_OBJECT_PINNED)
			flags |= entry->offset | PIN_OFFSET_FIXED;
		if ((flags & PIN_MAPPABLE) == 0)
			flags |= PIN_HIGH;
	}

	ret = i915_vma_pin(vma,
			   entry->pad_to_size,
			   entry->alignment,
			   flags);
	if ((ret == -ENOSPC || ret == -E2BIG) &&
	    only_mappable_for_reloc(entry->flags))
		ret = i915_vma_pin(vma,
				   entry->pad_to_size,
				   entry->alignment,
				   flags & ~PIN_MAPPABLE);
	if (ret)
		return ret;

	entry->flags |= __EXEC_OBJECT_HAS_PIN;

	if (entry->flags & EXEC_OBJECT_NEEDS_FENCE) {
		ret = i915_vma_get_fence(vma);
		if (ret)
			return ret;

		if (i915_vma_pin_fence(vma))
			entry->flags |= __EXEC_OBJECT_HAS_FENCE;
	}

	if (entry->offset != vma->node.start) {
		entry->offset = vma->node.start;
		*need_reloc = true;
	}

	if (entry->flags & EXEC_OBJECT_WRITE) {
		obj->base.pending_read_domains = I915_GEM_DOMAIN_RENDER;
		obj->base.pending_write_domain = I915_GEM_DOMAIN_RENDER;
	}

	return 0;
}

static bool
need_reloc_mappable(struct i915_vma *vma)
{
	struct drm_i915_gem_exec_object2 *entry = vma->exec_entry;

	if (entry->relocation_count == 0)
		return false;

	if (!i915_vma_is_ggtt(vma))
		return false;

	/* See also use_cpu_reloc() */
	if (HAS_LLC(vma->obj->base.dev))
		return false;

	if (vma->obj->base.write_domain == I915_GEM_DOMAIN_CPU)
		return false;

	return true;
}

static bool
eb_vma_misplaced(struct i915_vma *vma)
{
	struct drm_i915_gem_exec_object2 *entry = vma->exec_entry;

	WARN_ON(entry->flags & __EXEC_OBJECT_NEEDS_MAP &&
		!i915_vma_is_ggtt(vma));

	if (entry->alignment &&
	    vma->node.start & (entry->alignment - 1))
		return true;

	if (vma->node.size < entry->pad_to_size)
		return true;

	if (entry->flags & EXEC_OBJECT_PINNED &&
	    vma->node.start != entry->offset)
		return true;

	if (entry->flags & __EXEC_OBJECT_NEEDS_BIAS &&
	    vma->node.start < BATCH_OFFSET_BIAS)
		return true;

	/* avoid costly ping-pong once a batch bo ended up non-mappable */
	if (entry->flags & __EXEC_OBJECT_NEEDS_MAP &&
	    !i915_vma_is_map_and_fenceable(vma))
		return !only_mappable_for_reloc(entry->flags);

	if ((entry->flags & EXEC_OBJECT_SUPPORTS_48B_ADDRESS) == 0 &&
	    (vma->node.start + vma->node.size - 1) >> 32)
		return true;

	return false;
}

static int
i915_gem_execbuffer_reserve(struct intel_engine_cs *engine,
			    struct list_head *vmas,
			    struct i915_gem_context *ctx,
			    bool *need_relocs)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	struct i915_address_space *vm;
	struct list_head ordered_vmas;
	struct list_head pinned_vmas;
	bool has_fenced_gpu_access = INTEL_GEN(engine->i915) < 4;
	int retry;

	vm = list_first_entry(vmas, struct i915_vma, exec_list)->vm;

	INIT_LIST_HEAD(&ordered_vmas);
	INIT_LIST_HEAD(&pinned_vmas);
	while (!list_empty(vmas)) {
		struct drm_i915_gem_exec_object2 *entry;
		bool need_fence, need_mappable;

		vma = list_first_entry(vmas, struct i915_vma, exec_list);
		obj = vma->obj;
		entry = vma->exec_entry;

		if (ctx->flags & CONTEXT_NO_ZEROMAP)
			entry->flags |= __EXEC_OBJECT_NEEDS_BIAS;

		if (!has_fenced_gpu_access)
			entry->flags &= ~EXEC_OBJECT_NEEDS_FENCE;
		need_fence =
			entry->flags & EXEC_OBJECT_NEEDS_FENCE &&
			i915_gem_object_is_tiled(obj);
		need_mappable = need_fence || need_reloc_mappable(vma);

		if (entry->flags & EXEC_OBJECT_PINNED)
			list_move_tail(&vma->exec_list, &pinned_vmas);
		else if (need_mappable) {
			entry->flags |= __EXEC_OBJECT_NEEDS_MAP;
			list_move(&vma->exec_list, &ordered_vmas);
		} else
			list_move_tail(&vma->exec_list, &ordered_vmas);

		obj->base.pending_read_domains = I915_GEM_GPU_DOMAINS & ~I915_GEM_DOMAIN_COMMAND;
		obj->base.pending_write_domain = 0;
	}
	list_splice(&ordered_vmas, vmas);
	list_splice(&pinned_vmas, vmas);

	/* Attempt to pin all of the buffers into the GTT.
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
	retry = 0;
	do {
		int ret = 0;

		/* Unbind any ill-fitting objects or pin. */
		list_for_each_entry(vma, vmas, exec_list) {
			if (!drm_mm_node_allocated(&vma->node))
				continue;

			if (eb_vma_misplaced(vma))
				ret = i915_vma_unbind(vma);
			else
				ret = i915_gem_execbuffer_reserve_vma(vma,
								      engine,
								      need_relocs);
			if (ret)
				goto err;
		}

		/* Bind fresh objects */
		list_for_each_entry(vma, vmas, exec_list) {
			if (drm_mm_node_allocated(&vma->node))
				continue;

			ret = i915_gem_execbuffer_reserve_vma(vma, engine,
							      need_relocs);
			if (ret)
				goto err;
		}

err:
		if (ret != -ENOSPC || retry++)
			return ret;

		/* Decrement pin count for bound objects */
		list_for_each_entry(vma, vmas, exec_list)
			i915_gem_execbuffer_unreserve_vma(vma);

		ret = i915_gem_evict_vm(vm, true);
		if (ret)
			return ret;
	} while (1);
}

static int
i915_gem_execbuffer_relocate_slow(struct drm_device *dev,
				  struct drm_i915_gem_execbuffer2 *args,
				  struct drm_file *file,
				  struct intel_engine_cs *engine,
				  struct eb_vmas *eb,
				  struct drm_i915_gem_exec_object2 *exec,
				  struct i915_gem_context *ctx)
{
	struct drm_i915_gem_relocation_entry *reloc;
	struct i915_address_space *vm;
	struct i915_vma *vma;
	bool need_relocs;
	int *reloc_offset;
	int i, total, ret;
	unsigned count = args->buffer_count;

	vm = list_first_entry(&eb->vmas, struct i915_vma, exec_list)->vm;

	/* We may process another execbuffer during the unlock... */
	while (!list_empty(&eb->vmas)) {
		vma = list_first_entry(&eb->vmas, struct i915_vma, exec_list);
		list_del_init(&vma->exec_list);
		i915_gem_execbuffer_unreserve_vma(vma);
		i915_vma_put(vma);
	}

	mutex_unlock(&dev->struct_mutex);

	total = 0;
	for (i = 0; i < count; i++)
		total += exec[i].relocation_count;

	reloc_offset = drm_malloc_ab(count, sizeof(*reloc_offset));
	reloc = drm_malloc_ab(total, sizeof(*reloc));
	if (reloc == NULL || reloc_offset == NULL) {
		drm_free_large(reloc);
		drm_free_large(reloc_offset);
		mutex_lock(&dev->struct_mutex);
		return -ENOMEM;
	}

	total = 0;
	for (i = 0; i < count; i++) {
		struct drm_i915_gem_relocation_entry __user *user_relocs;
		u64 invalid_offset = (u64)-1;
		int j;

		user_relocs = u64_to_user_ptr(exec[i].relocs_ptr);

		if (copy_from_user(reloc+total, user_relocs,
				   exec[i].relocation_count * sizeof(*reloc))) {
			ret = -EFAULT;
			mutex_lock(&dev->struct_mutex);
			goto err;
		}

		/* As we do not update the known relocation offsets after
		 * relocating (due to the complexities in lock handling),
		 * we need to mark them as invalid now so that we force the
		 * relocation processing next time. Just in case the target
		 * object is evicted and then rebound into its old
		 * presumed_offset before the next execbuffer - if that
		 * happened we would make the mistake of assuming that the
		 * relocations were valid.
		 */
		for (j = 0; j < exec[i].relocation_count; j++) {
			if (__copy_to_user(&user_relocs[j].presumed_offset,
					   &invalid_offset,
					   sizeof(invalid_offset))) {
				ret = -EFAULT;
				mutex_lock(&dev->struct_mutex);
				goto err;
			}
		}

		reloc_offset[i] = total;
		total += exec[i].relocation_count;
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret) {
		mutex_lock(&dev->struct_mutex);
		goto err;
	}

	/* reacquire the objects */
	eb_reset(eb);
	ret = eb_lookup_vmas(eb, exec, args, vm, file);
	if (ret)
		goto err;

	need_relocs = (args->flags & I915_EXEC_NO_RELOC) == 0;
	ret = i915_gem_execbuffer_reserve(engine, &eb->vmas, ctx,
					  &need_relocs);
	if (ret)
		goto err;

	list_for_each_entry(vma, &eb->vmas, exec_list) {
		int offset = vma->exec_entry - exec;
		ret = i915_gem_execbuffer_relocate_vma_slow(vma, eb,
							    reloc + reloc_offset[offset]);
		if (ret)
			goto err;
	}

	/* Leave the user relocations as are, this is the painfully slow path,
	 * and we want to avoid the complication of dropping the lock whilst
	 * having buffers reserved in the aperture and so causing spurious
	 * ENOSPC for random operations.
	 */

err:
	drm_free_large(reloc);
	drm_free_large(reloc_offset);
	return ret;
}

static unsigned int eb_other_engines(struct drm_i915_gem_request *req)
{
	unsigned int mask;

	mask = ~intel_engine_flag(req->engine) & I915_BO_ACTIVE_MASK;
	mask <<= I915_BO_ACTIVE_SHIFT;

	return mask;
}

static int
i915_gem_execbuffer_move_to_gpu(struct drm_i915_gem_request *req,
				struct list_head *vmas)
{
	const unsigned int other_rings = eb_other_engines(req);
	struct i915_vma *vma;
	int ret;

	list_for_each_entry(vma, vmas, exec_list) {
		struct drm_i915_gem_object *obj = vma->obj;
		struct reservation_object *resv;

		if (obj->flags & other_rings) {
			ret = i915_gem_request_await_object
				(req, obj, obj->base.pending_write_domain);
			if (ret)
				return ret;
		}

		resv = i915_gem_object_get_dmabuf_resv(obj);
		if (resv) {
			ret = i915_sw_fence_await_reservation
				(&req->submit, resv, &i915_fence_ops,
				 obj->base.pending_write_domain, 10*HZ,
				 GFP_KERNEL | __GFP_NOWARN);
			if (ret < 0)
				return ret;
		}

		if (obj->base.write_domain & I915_GEM_DOMAIN_CPU)
			i915_gem_clflush_object(obj, false);
	}

	/* Unconditionally flush any chipset caches (for streaming writes). */
	i915_gem_chipset_flush(req->engine->i915);

	/* Unconditionally invalidate GPU caches and TLBs. */
	return req->engine->emit_flush(req, EMIT_INVALIDATE);
}

static bool
i915_gem_check_execbuffer(struct drm_i915_gem_execbuffer2 *exec)
{
	if (exec->flags & __I915_EXEC_UNKNOWN_FLAGS)
		return false;

	/* Kernel clipping was a DRI1 misfeature */
	if (exec->num_cliprects || exec->cliprects_ptr)
		return false;

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

static int
validate_exec_list(struct drm_device *dev,
		   struct drm_i915_gem_exec_object2 *exec,
		   int count)
{
	unsigned relocs_total = 0;
	unsigned relocs_max = UINT_MAX / sizeof(struct drm_i915_gem_relocation_entry);
	unsigned invalid_flags;
	int i;

	/* INTERNAL flags must not overlap with external ones */
	BUILD_BUG_ON(__EXEC_OBJECT_INTERNAL_FLAGS & ~__EXEC_OBJECT_UNKNOWN_FLAGS);

	invalid_flags = __EXEC_OBJECT_UNKNOWN_FLAGS;
	if (USES_FULL_PPGTT(dev))
		invalid_flags |= EXEC_OBJECT_NEEDS_GTT;

	for (i = 0; i < count; i++) {
		char __user *ptr = u64_to_user_ptr(exec[i].relocs_ptr);
		int length; /* limited by fault_in_pages_readable() */

		if (exec[i].flags & invalid_flags)
			return -EINVAL;

		/* Offset can be used as input (EXEC_OBJECT_PINNED), reject
		 * any non-page-aligned or non-canonical addresses.
		 */
		if (exec[i].flags & EXEC_OBJECT_PINNED) {
			if (exec[i].offset !=
			    gen8_canonical_addr(exec[i].offset & PAGE_MASK))
				return -EINVAL;
		}

		/* From drm_mm perspective address space is continuous,
		 * so from this point we're always using non-canonical
		 * form internally.
		 */
		exec[i].offset = gen8_noncanonical_addr(exec[i].offset);

		if (exec[i].alignment && !is_power_of_2(exec[i].alignment))
			return -EINVAL;

		/* pad_to_size was once a reserved field, so sanitize it */
		if (exec[i].flags & EXEC_OBJECT_PAD_TO_SIZE) {
			if (offset_in_page(exec[i].pad_to_size))
				return -EINVAL;
		} else {
			exec[i].pad_to_size = 0;
		}

		/* First check for malicious input causing overflow in
		 * the worst case where we need to allocate the entire
		 * relocation tree as a single array.
		 */
		if (exec[i].relocation_count > relocs_max - relocs_total)
			return -EINVAL;
		relocs_total += exec[i].relocation_count;

		length = exec[i].relocation_count *
			sizeof(struct drm_i915_gem_relocation_entry);
		/*
		 * We must check that the entire relocation array is safe
		 * to read, but since we may need to update the presumed
		 * offsets during execution, check for full write access.
		 */
		if (!access_ok(VERIFY_WRITE, ptr, length))
			return -EFAULT;

		if (likely(!i915.prefault_disable)) {
			if (fault_in_pages_readable(ptr, length))
				return -EFAULT;
		}
	}

	return 0;
}

static struct i915_gem_context *
i915_gem_validate_context(struct drm_device *dev, struct drm_file *file,
			  struct intel_engine_cs *engine, const u32 ctx_id)
{
	struct i915_gem_context *ctx;
	struct i915_ctx_hang_stats *hs;

	ctx = i915_gem_context_lookup(file->driver_priv, ctx_id);
	if (IS_ERR(ctx))
		return ctx;

	hs = &ctx->hang_stats;
	if (hs->banned) {
		DRM_DEBUG("Context %u tried to submit while banned\n", ctx_id);
		return ERR_PTR(-EIO);
	}

	return ctx;
}

static bool gpu_write_needs_clflush(struct drm_i915_gem_object *obj)
{
	return !(obj->cache_level == I915_CACHE_NONE ||
		 obj->cache_level == I915_CACHE_WT);
}

void i915_vma_move_to_active(struct i915_vma *vma,
			     struct drm_i915_gem_request *req,
			     unsigned int flags)
{
	struct drm_i915_gem_object *obj = vma->obj;
	const unsigned int idx = req->engine->id;

	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));

	obj->dirty = 1; /* be paranoid  */

	/* Add a reference if we're newly entering the active list.
	 * The order in which we add operations to the retirement queue is
	 * vital here: mark_active adds to the start of the callback list,
	 * such that subsequent callbacks are called first. Therefore we
	 * add the active reference first and queue for it to be dropped
	 * *last*.
	 */
	if (!i915_gem_object_is_active(obj))
		i915_gem_object_get(obj);
	i915_gem_object_set_active(obj, idx);
	i915_gem_active_set(&obj->last_read[idx], req);

	if (flags & EXEC_OBJECT_WRITE) {
		i915_gem_active_set(&obj->last_write, req);

		intel_fb_obj_invalidate(obj, ORIGIN_CS);

		/* update for the implicit flush after a batch */
		obj->base.write_domain &= ~I915_GEM_GPU_DOMAINS;
		if (!obj->cache_dirty && gpu_write_needs_clflush(obj))
			obj->cache_dirty = true;
	}

	if (flags & EXEC_OBJECT_NEEDS_FENCE)
		i915_gem_active_set(&vma->last_fence, req);

	i915_vma_set_active(vma, idx);
	i915_gem_active_set(&vma->last_read[idx], req);
	list_move_tail(&vma->vm_link, &vma->vm->active_list);
}

static void eb_export_fence(struct drm_i915_gem_object *obj,
			    struct drm_i915_gem_request *req,
			    unsigned int flags)
{
	struct reservation_object *resv;

	resv = i915_gem_object_get_dmabuf_resv(obj);
	if (!resv)
		return;

	/* Ignore errors from failing to allocate the new fence, we can't
	 * handle an error right now. Worst case should be missed
	 * synchronisation leading to rendering corruption.
	 */
	ww_mutex_lock(&resv->lock, NULL);
	if (flags & EXEC_OBJECT_WRITE)
		reservation_object_add_excl_fence(resv, &req->fence);
	else if (reservation_object_reserve_shared(resv) == 0)
		reservation_object_add_shared_fence(resv, &req->fence);
	ww_mutex_unlock(&resv->lock);
}

static void
i915_gem_execbuffer_move_to_active(struct list_head *vmas,
				   struct drm_i915_gem_request *req)
{
	struct i915_vma *vma;

	list_for_each_entry(vma, vmas, exec_list) {
		struct drm_i915_gem_object *obj = vma->obj;
		u32 old_read = obj->base.read_domains;
		u32 old_write = obj->base.write_domain;

		obj->base.write_domain = obj->base.pending_write_domain;
		if (obj->base.write_domain)
			vma->exec_entry->flags |= EXEC_OBJECT_WRITE;
		else
			obj->base.pending_read_domains |= obj->base.read_domains;
		obj->base.read_domains = obj->base.pending_read_domains;

		i915_vma_move_to_active(vma, req, vma->exec_entry->flags);
		eb_export_fence(obj, req, vma->exec_entry->flags);
		trace_i915_gem_object_change_domain(obj, old_read, old_write);
	}
}

static int
i915_reset_gen7_sol_offsets(struct drm_i915_gem_request *req)
{
	struct intel_ring *ring = req->ring;
	int ret, i;

	if (!IS_GEN7(req->i915) || req->engine->id != RCS) {
		DRM_DEBUG("sol reset is gen7/rcs only\n");
		return -EINVAL;
	}

	ret = intel_ring_begin(req, 4 * 3);
	if (ret)
		return ret;

	for (i = 0; i < 4; i++) {
		intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(1));
		intel_ring_emit_reg(ring, GEN7_SO_WRITE_OFFSET(i));
		intel_ring_emit(ring, 0);
	}

	intel_ring_advance(ring);

	return 0;
}

static struct i915_vma *
i915_gem_execbuffer_parse(struct intel_engine_cs *engine,
			  struct drm_i915_gem_exec_object2 *shadow_exec_entry,
			  struct drm_i915_gem_object *batch_obj,
			  struct eb_vmas *eb,
			  u32 batch_start_offset,
			  u32 batch_len,
			  bool is_master)
{
	struct drm_i915_gem_object *shadow_batch_obj;
	struct i915_vma *vma;
	int ret;

	shadow_batch_obj = i915_gem_batch_pool_get(&engine->batch_pool,
						   PAGE_ALIGN(batch_len));
	if (IS_ERR(shadow_batch_obj))
		return ERR_CAST(shadow_batch_obj);

	ret = intel_engine_cmd_parser(engine,
				      batch_obj,
				      shadow_batch_obj,
				      batch_start_offset,
				      batch_len,
				      is_master);
	if (ret) {
		if (ret == -EACCES) /* unhandled chained batch */
			vma = NULL;
		else
			vma = ERR_PTR(ret);
		goto out;
	}

	vma = i915_gem_object_ggtt_pin(shadow_batch_obj, NULL, 0, 0, 0);
	if (IS_ERR(vma))
		goto out;

	memset(shadow_exec_entry, 0, sizeof(*shadow_exec_entry));

	vma->exec_entry = shadow_exec_entry;
	vma->exec_entry->flags = __EXEC_OBJECT_HAS_PIN;
	i915_gem_object_get(shadow_batch_obj);
	list_add_tail(&vma->exec_list, &eb->vmas);

out:
	i915_gem_object_unpin_pages(shadow_batch_obj);
	return vma;
}

static int
execbuf_submit(struct i915_execbuffer_params *params,
	       struct drm_i915_gem_execbuffer2 *args,
	       struct list_head *vmas)
{
	u64 exec_start, exec_len;
	int ret;

	ret = i915_gem_execbuffer_move_to_gpu(params->request, vmas);
	if (ret)
		return ret;

	ret = i915_switch_context(params->request);
	if (ret)
		return ret;

	if (args->flags & I915_EXEC_CONSTANTS_MASK) {
		DRM_DEBUG("I915_EXEC_CONSTANTS_* unsupported\n");
		return -EINVAL;
	}

	if (args->flags & I915_EXEC_GEN7_SOL_RESET) {
		ret = i915_reset_gen7_sol_offsets(params->request);
		if (ret)
			return ret;
	}

	exec_len   = args->batch_len;
	exec_start = params->batch->node.start +
		     params->args_batch_start_offset;

	if (exec_len == 0)
		exec_len = params->batch->size - params->args_batch_start_offset;

	ret = params->engine->emit_bb_start(params->request,
					    exec_start, exec_len,
					    params->dispatch_flags);
	if (ret)
		return ret;

	trace_i915_gem_ring_dispatch(params->request, params->dispatch_flags);

	i915_gem_execbuffer_move_to_active(vmas, params->request);

	return 0;
}

/**
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

		engine = &dev_priv->engine[_VCS(bsd_idx)];
	} else {
		engine = &dev_priv->engine[user_ring_map[user_ring_id]];
	}

	if (!intel_engine_initialized(engine)) {
		DRM_DEBUG("execbuf with invalid ring: %u\n", user_ring_id);
		return NULL;
	}

	return engine;
}

static int
i915_gem_do_execbuffer(struct drm_device *dev, void *data,
		       struct drm_file *file,
		       struct drm_i915_gem_execbuffer2 *args,
		       struct drm_i915_gem_exec_object2 *exec)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	struct eb_vmas *eb;
	struct drm_i915_gem_exec_object2 shadow_exec_entry;
	struct intel_engine_cs *engine;
	struct i915_gem_context *ctx;
	struct i915_address_space *vm;
	struct i915_execbuffer_params params_master; /* XXX: will be removed later */
	struct i915_execbuffer_params *params = &params_master;
	const u32 ctx_id = i915_execbuffer2_get_context_id(*args);
	u32 dispatch_flags;
	int ret;
	bool need_relocs;

	if (!i915_gem_check_execbuffer(args))
		return -EINVAL;

	ret = validate_exec_list(dev, exec, args->buffer_count);
	if (ret)
		return ret;

	dispatch_flags = 0;
	if (args->flags & I915_EXEC_SECURE) {
		if (!drm_is_current_master(file) || !capable(CAP_SYS_ADMIN))
		    return -EPERM;

		dispatch_flags |= I915_DISPATCH_SECURE;
	}
	if (args->flags & I915_EXEC_IS_PINNED)
		dispatch_flags |= I915_DISPATCH_PINNED;

	engine = eb_select_engine(dev_priv, file, args);
	if (!engine)
		return -EINVAL;

	if (args->buffer_count < 1) {
		DRM_DEBUG("execbuf with %d buffers\n", args->buffer_count);
		return -EINVAL;
	}

	if (args->flags & I915_EXEC_RESOURCE_STREAMER) {
		if (!HAS_RESOURCE_STREAMER(dev)) {
			DRM_DEBUG("RS is only allowed for Haswell, Gen8 and above\n");
			return -EINVAL;
		}
		if (engine->id != RCS) {
			DRM_DEBUG("RS is not available on %s\n",
				 engine->name);
			return -EINVAL;
		}

		dispatch_flags |= I915_DISPATCH_RS;
	}

	/* Take a local wakeref for preparing to dispatch the execbuf as
	 * we expect to access the hardware fairly frequently in the
	 * process. Upon first dispatch, we acquire another prolonged
	 * wakeref that we hold until the GPU has been idle for at least
	 * 100ms.
	 */
	intel_runtime_pm_get(dev_priv);

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		goto pre_mutex_err;

	ctx = i915_gem_validate_context(dev, file, engine, ctx_id);
	if (IS_ERR(ctx)) {
		mutex_unlock(&dev->struct_mutex);
		ret = PTR_ERR(ctx);
		goto pre_mutex_err;
	}

	i915_gem_context_get(ctx);

	if (ctx->ppgtt)
		vm = &ctx->ppgtt->base;
	else
		vm = &ggtt->base;

	memset(&params_master, 0x00, sizeof(params_master));

	eb = eb_create(dev_priv, args);
	if (eb == NULL) {
		i915_gem_context_put(ctx);
		mutex_unlock(&dev->struct_mutex);
		ret = -ENOMEM;
		goto pre_mutex_err;
	}

	/* Look up object handles */
	ret = eb_lookup_vmas(eb, exec, args, vm, file);
	if (ret)
		goto err;

	/* take note of the batch buffer before we might reorder the lists */
	params->batch = eb_get_batch(eb);

	/* Move the objects en-masse into the GTT, evicting if necessary. */
	need_relocs = (args->flags & I915_EXEC_NO_RELOC) == 0;
	ret = i915_gem_execbuffer_reserve(engine, &eb->vmas, ctx,
					  &need_relocs);
	if (ret)
		goto err;

	/* The objects are in their final locations, apply the relocations. */
	if (need_relocs)
		ret = i915_gem_execbuffer_relocate(eb);
	if (ret) {
		if (ret == -EFAULT) {
			ret = i915_gem_execbuffer_relocate_slow(dev, args, file,
								engine,
								eb, exec, ctx);
			BUG_ON(!mutex_is_locked(&dev->struct_mutex));
		}
		if (ret)
			goto err;
	}

	/* Set the pending read domains for the batch buffer to COMMAND */
	if (params->batch->obj->base.pending_write_domain) {
		DRM_DEBUG("Attempting to use self-modifying batch buffer\n");
		ret = -EINVAL;
		goto err;
	}
	if (args->batch_start_offset > params->batch->size ||
	    args->batch_len > params->batch->size - args->batch_start_offset) {
		DRM_DEBUG("Attempting to use out-of-bounds batch\n");
		ret = -EINVAL;
		goto err;
	}

	params->args_batch_start_offset = args->batch_start_offset;
	if (intel_engine_needs_cmd_parser(engine) && args->batch_len) {
		struct i915_vma *vma;

		vma = i915_gem_execbuffer_parse(engine, &shadow_exec_entry,
						params->batch->obj,
						eb,
						args->batch_start_offset,
						args->batch_len,
						drm_is_current_master(file));
		if (IS_ERR(vma)) {
			ret = PTR_ERR(vma);
			goto err;
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
			dispatch_flags |= I915_DISPATCH_SECURE;
			params->args_batch_start_offset = 0;
			params->batch = vma;
		}
	}

	params->batch->obj->base.pending_read_domains |= I915_GEM_DOMAIN_COMMAND;

	/* snb/ivb/vlv conflate the "batch in ppgtt" bit with the "non-secure
	 * batch" bit. Hence we need to pin secure batches into the global gtt.
	 * hsw should have this fixed, but bdw mucks it up again. */
	if (dispatch_flags & I915_DISPATCH_SECURE) {
		struct drm_i915_gem_object *obj = params->batch->obj;
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
		vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, 0);
		if (IS_ERR(vma)) {
			ret = PTR_ERR(vma);
			goto err;
		}

		params->batch = vma;
	}

	/* Allocate a request for this batch buffer nice and early. */
	params->request = i915_gem_request_alloc(engine, ctx);
	if (IS_ERR(params->request)) {
		ret = PTR_ERR(params->request);
		goto err_batch_unpin;
	}

	/* Whilst this request exists, batch_obj will be on the
	 * active_list, and so will hold the active reference. Only when this
	 * request is retired will the the batch_obj be moved onto the
	 * inactive_list and lose its active reference. Hence we do not need
	 * to explicitly hold another reference here.
	 */
	params->request->batch = params->batch;

	ret = i915_gem_request_add_to_client(params->request, file);
	if (ret)
		goto err_request;

	/*
	 * Save assorted stuff away to pass through to *_submission().
	 * NB: This data should be 'persistent' and not local as it will
	 * kept around beyond the duration of the IOCTL once the GPU
	 * scheduler arrives.
	 */
	params->dev                     = dev;
	params->file                    = file;
	params->engine                    = engine;
	params->dispatch_flags          = dispatch_flags;
	params->ctx                     = ctx;

	ret = execbuf_submit(params, args, &eb->vmas);
err_request:
	__i915_add_request(params->request, ret == 0);

err_batch_unpin:
	/*
	 * FIXME: We crucially rely upon the active tracking for the (ppgtt)
	 * batch vma for correctness. For less ugly and less fragility this
	 * needs to be adjusted to also track the ggtt batch vma properly as
	 * active.
	 */
	if (dispatch_flags & I915_DISPATCH_SECURE)
		i915_vma_unpin(params->batch);
err:
	/* the request owns the ref now */
	i915_gem_context_put(ctx);
	eb_destroy(eb);

	mutex_unlock(&dev->struct_mutex);

pre_mutex_err:
	/* intel_gpu_busy should also get a ref, so it will free when the device
	 * is really idle. */
	intel_runtime_pm_put(dev_priv);
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

	if (args->buffer_count < 1) {
		DRM_DEBUG("execbuf with %d buffers\n", args->buffer_count);
		return -EINVAL;
	}

	/* Copy in the exec list from userland */
	exec_list = drm_malloc_ab(sizeof(*exec_list), args->buffer_count);
	exec2_list = drm_malloc_ab(sizeof(*exec2_list), args->buffer_count);
	if (exec_list == NULL || exec2_list == NULL) {
		DRM_DEBUG("Failed to allocate exec list for %d buffers\n",
			  args->buffer_count);
		drm_free_large(exec_list);
		drm_free_large(exec2_list);
		return -ENOMEM;
	}
	ret = copy_from_user(exec_list,
			     u64_to_user_ptr(args->buffers_ptr),
			     sizeof(*exec_list) * args->buffer_count);
	if (ret != 0) {
		DRM_DEBUG("copy %d exec entries failed %d\n",
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
	i915_execbuffer2_set_context_id(exec2, 0);

	ret = i915_gem_do_execbuffer(dev, data, file, &exec2, exec2_list);
	if (!ret) {
		struct drm_i915_gem_exec_object __user *user_exec_list =
			u64_to_user_ptr(args->buffers_ptr);

		/* Copy the new buffer offsets back to the user's exec list. */
		for (i = 0; i < args->buffer_count; i++) {
			exec2_list[i].offset =
				gen8_canonical_addr(exec2_list[i].offset);
			ret = __copy_to_user(&user_exec_list[i].offset,
					     &exec2_list[i].offset,
					     sizeof(user_exec_list[i].offset));
			if (ret) {
				ret = -EFAULT;
				DRM_DEBUG("failed to copy %d exec entries "
					  "back to user (%d)\n",
					  args->buffer_count, ret);
				break;
			}
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

	if (args->buffer_count < 1 ||
	    args->buffer_count > UINT_MAX / sizeof(*exec2_list)) {
		DRM_DEBUG("execbuf2 with %d buffers\n", args->buffer_count);
		return -EINVAL;
	}

	if (args->rsvd2 != 0) {
		DRM_DEBUG("dirty rvsd2 field\n");
		return -EINVAL;
	}

	exec2_list = drm_malloc_gfp(args->buffer_count,
				    sizeof(*exec2_list),
				    GFP_TEMPORARY);
	if (exec2_list == NULL) {
		DRM_DEBUG("Failed to allocate exec list for %d buffers\n",
			  args->buffer_count);
		return -ENOMEM;
	}
	ret = copy_from_user(exec2_list,
			     u64_to_user_ptr(args->buffers_ptr),
			     sizeof(*exec2_list) * args->buffer_count);
	if (ret != 0) {
		DRM_DEBUG("copy %d exec entries failed %d\n",
			  args->buffer_count, ret);
		drm_free_large(exec2_list);
		return -EFAULT;
	}

	ret = i915_gem_do_execbuffer(dev, data, file, args, exec2_list);
	if (!ret) {
		/* Copy the new buffer offsets back to the user's exec list. */
		struct drm_i915_gem_exec_object2 __user *user_exec_list =
				   u64_to_user_ptr(args->buffers_ptr);
		int i;

		for (i = 0; i < args->buffer_count; i++) {
			exec2_list[i].offset =
				gen8_canonical_addr(exec2_list[i].offset);
			ret = __copy_to_user(&user_exec_list[i].offset,
					     &exec2_list[i].offset,
					     sizeof(user_exec_list[i].offset));
			if (ret) {
				ret = -EFAULT;
				DRM_DEBUG("failed to copy %d exec entries "
					  "back to user\n",
					  args->buffer_count);
				break;
			}
		}
	}

	drm_free_large(exec2_list);
	return ret;
}
