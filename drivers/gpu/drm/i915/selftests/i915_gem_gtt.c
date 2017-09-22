/*
 * Copyright © 2016 Intel Corporation
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
 */

#include <linux/list_sort.h>
#include <linux/prime_numbers.h>

#include "../i915_selftest.h"
#include "i915_random.h"

#include "mock_context.h"
#include "mock_drm.h"
#include "mock_gem_device.h"

static void fake_free_pages(struct drm_i915_gem_object *obj,
			    struct sg_table *pages)
{
	sg_free_table(pages);
	kfree(pages);
}

static struct sg_table *
fake_get_pages(struct drm_i915_gem_object *obj)
{
#define GFP (GFP_KERNEL | __GFP_NOWARN | __GFP_NORETRY)
#define PFN_BIAS 0x1000
	struct sg_table *pages;
	struct scatterlist *sg;
	typeof(obj->base.size) rem;

	pages = kmalloc(sizeof(*pages), GFP);
	if (!pages)
		return ERR_PTR(-ENOMEM);

	rem = round_up(obj->base.size, BIT(31)) >> 31;
	if (sg_alloc_table(pages, rem, GFP)) {
		kfree(pages);
		return ERR_PTR(-ENOMEM);
	}

	rem = obj->base.size;
	for (sg = pages->sgl; sg; sg = sg_next(sg)) {
		unsigned long len = min_t(typeof(rem), rem, BIT(31));

		GEM_BUG_ON(!len);
		sg_set_page(sg, pfn_to_page(PFN_BIAS), len, 0);
		sg_dma_address(sg) = page_to_phys(sg_page(sg));
		sg_dma_len(sg) = len;

		rem -= len;
	}
	GEM_BUG_ON(rem);

	obj->mm.madv = I915_MADV_DONTNEED;
	return pages;
#undef GFP
}

static void fake_put_pages(struct drm_i915_gem_object *obj,
			   struct sg_table *pages)
{
	fake_free_pages(obj, pages);
	obj->mm.dirty = false;
	obj->mm.madv = I915_MADV_WILLNEED;
}

static const struct drm_i915_gem_object_ops fake_ops = {
	.flags = I915_GEM_OBJECT_IS_SHRINKABLE,
	.get_pages = fake_get_pages,
	.put_pages = fake_put_pages,
};

static struct drm_i915_gem_object *
fake_dma_object(struct drm_i915_private *i915, u64 size)
{
	struct drm_i915_gem_object *obj;

	GEM_BUG_ON(!size);
	GEM_BUG_ON(!IS_ALIGNED(size, I915_GTT_PAGE_SIZE));

	if (overflows_type(size, obj->base.size))
		return ERR_PTR(-E2BIG);

	obj = i915_gem_object_alloc(i915);
	if (!obj)
		goto err;

	drm_gem_private_object_init(&i915->drm, &obj->base, size);
	i915_gem_object_init(obj, &fake_ops);

	obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	obj->base.read_domains = I915_GEM_DOMAIN_CPU;
	obj->cache_level = I915_CACHE_NONE;

	/* Preallocate the "backing storage" */
	if (i915_gem_object_pin_pages(obj))
		goto err_obj;

	i915_gem_object_unpin_pages(obj);
	return obj;

err_obj:
	i915_gem_object_put(obj);
err:
	return ERR_PTR(-ENOMEM);
}

static int igt_ppgtt_alloc(void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	struct i915_hw_ppgtt *ppgtt;
	u64 size, last;
	int err;

	/* Allocate a ppggt and try to fill the entire range */

	if (!USES_PPGTT(dev_priv))
		return 0;

	ppgtt = kzalloc(sizeof(*ppgtt), GFP_KERNEL);
	if (!ppgtt)
		return -ENOMEM;

	mutex_lock(&dev_priv->drm.struct_mutex);
	err = __hw_ppgtt_init(ppgtt, dev_priv);
	if (err)
		goto err_ppgtt;

	if (!ppgtt->base.allocate_va_range)
		goto err_ppgtt_cleanup;

	/* Check we can allocate the entire range */
	for (size = 4096;
	     size <= ppgtt->base.total;
	     size <<= 2) {
		err = ppgtt->base.allocate_va_range(&ppgtt->base, 0, size);
		if (err) {
			if (err == -ENOMEM) {
				pr_info("[1] Ran out of memory for va_range [0 + %llx] [bit %d]\n",
					size, ilog2(size));
				err = 0; /* virtual space too large! */
			}
			goto err_ppgtt_cleanup;
		}

		ppgtt->base.clear_range(&ppgtt->base, 0, size);
	}

	/* Check we can incrementally allocate the entire range */
	for (last = 0, size = 4096;
	     size <= ppgtt->base.total;
	     last = size, size <<= 2) {
		err = ppgtt->base.allocate_va_range(&ppgtt->base,
						    last, size - last);
		if (err) {
			if (err == -ENOMEM) {
				pr_info("[2] Ran out of memory for va_range [%llx + %llx] [bit %d]\n",
					last, size - last, ilog2(size));
				err = 0; /* virtual space too large! */
			}
			goto err_ppgtt_cleanup;
		}
	}

err_ppgtt_cleanup:
	ppgtt->base.cleanup(&ppgtt->base);
err_ppgtt:
	mutex_unlock(&dev_priv->drm.struct_mutex);
	kfree(ppgtt);
	return err;
}

static int lowlevel_hole(struct drm_i915_private *i915,
			 struct i915_address_space *vm,
			 u64 hole_start, u64 hole_end,
			 unsigned long end_time)
{
	I915_RND_STATE(seed_prng);
	unsigned int size;
	struct i915_vma mock_vma;

	memset(&mock_vma, 0, sizeof(struct i915_vma));

	/* Keep creating larger objects until one cannot fit into the hole */
	for (size = 12; (hole_end - hole_start) >> size; size++) {
		I915_RND_SUBSTATE(prng, seed_prng);
		struct drm_i915_gem_object *obj;
		unsigned int *order, count, n;
		u64 hole_size;

		hole_size = (hole_end - hole_start) >> size;
		if (hole_size > KMALLOC_MAX_SIZE / sizeof(u32))
			hole_size = KMALLOC_MAX_SIZE / sizeof(u32);
		count = hole_size;
		do {
			count >>= 1;
			order = i915_random_order(count, &prng);
		} while (!order && count);
		if (!order)
			break;

		GEM_BUG_ON(count * BIT_ULL(size) > vm->total);
		GEM_BUG_ON(hole_start + count * BIT_ULL(size) > hole_end);

		/* Ignore allocation failures (i.e. don't report them as
		 * a test failure) as we are purposefully allocating very
		 * large objects without checking that we have sufficient
		 * memory. We expect to hit -ENOMEM.
		 */

		obj = fake_dma_object(i915, BIT_ULL(size));
		if (IS_ERR(obj)) {
			kfree(order);
			break;
		}

		GEM_BUG_ON(obj->base.size != BIT_ULL(size));

		if (i915_gem_object_pin_pages(obj)) {
			i915_gem_object_put(obj);
			kfree(order);
			break;
		}

		for (n = 0; n < count; n++) {
			u64 addr = hole_start + order[n] * BIT_ULL(size);

			GEM_BUG_ON(addr + BIT_ULL(size) > vm->total);

			if (igt_timeout(end_time,
					"%s timed out before %d/%d\n",
					__func__, n, count)) {
				hole_end = hole_start; /* quit */
				break;
			}

			if (vm->allocate_va_range &&
			    vm->allocate_va_range(vm, addr, BIT_ULL(size)))
				break;

			mock_vma.pages = obj->mm.pages;
			mock_vma.node.size = BIT_ULL(size);
			mock_vma.node.start = addr;

			vm->insert_entries(vm, &mock_vma, I915_CACHE_NONE, 0);
		}
		count = n;

		i915_random_reorder(order, count, &prng);
		for (n = 0; n < count; n++) {
			u64 addr = hole_start + order[n] * BIT_ULL(size);

			GEM_BUG_ON(addr + BIT_ULL(size) > vm->total);
			vm->clear_range(vm, addr, BIT_ULL(size));
		}

		i915_gem_object_unpin_pages(obj);
		i915_gem_object_put(obj);

		kfree(order);
	}

	return 0;
}

static void close_object_list(struct list_head *objects,
			      struct i915_address_space *vm)
{
	struct drm_i915_gem_object *obj, *on;
	int ignored;

	list_for_each_entry_safe(obj, on, objects, st_link) {
		struct i915_vma *vma;

		vma = i915_vma_instance(obj, vm, NULL);
		if (!IS_ERR(vma))
			ignored = i915_vma_unbind(vma);
		/* Only ppgtt vma may be closed before the object is freed */
		if (!IS_ERR(vma) && !i915_vma_is_ggtt(vma))
			i915_vma_close(vma);

		list_del(&obj->st_link);
		i915_gem_object_put(obj);
	}
}

static int fill_hole(struct drm_i915_private *i915,
		     struct i915_address_space *vm,
		     u64 hole_start, u64 hole_end,
		     unsigned long end_time)
{
	const u64 hole_size = hole_end - hole_start;
	struct drm_i915_gem_object *obj;
	const unsigned long max_pages =
		min_t(u64, ULONG_MAX - 1, hole_size/2 >> PAGE_SHIFT);
	const unsigned long max_step = max(int_sqrt(max_pages), 2UL);
	unsigned long npages, prime, flags;
	struct i915_vma *vma;
	LIST_HEAD(objects);
	int err;

	/* Try binding many VMA working inwards from either edge */

	flags = PIN_OFFSET_FIXED | PIN_USER;
	if (i915_is_ggtt(vm))
		flags |= PIN_GLOBAL;

	for_each_prime_number_from(prime, 2, max_step) {
		for (npages = 1; npages <= max_pages; npages *= prime) {
			const u64 full_size = npages << PAGE_SHIFT;
			const struct {
				const char *name;
				u64 offset;
				int step;
			} phases[] = {
				{ "top-down", hole_end, -1, },
				{ "bottom-up", hole_start, 1, },
				{ }
			}, *p;

			obj = fake_dma_object(i915, full_size);
			if (IS_ERR(obj))
				break;

			list_add(&obj->st_link, &objects);

			/* Align differing sized objects against the edges, and
			 * check we don't walk off into the void when binding
			 * them into the GTT.
			 */
			for (p = phases; p->name; p++) {
				u64 offset;

				offset = p->offset;
				list_for_each_entry(obj, &objects, st_link) {
					vma = i915_vma_instance(obj, vm, NULL);
					if (IS_ERR(vma))
						continue;

					if (p->step < 0) {
						if (offset < hole_start + obj->base.size)
							break;
						offset -= obj->base.size;
					}

					err = i915_vma_pin(vma, 0, 0, offset | flags);
					if (err) {
						pr_err("%s(%s) pin (forward) failed with err=%d on size=%lu pages (prime=%lu), offset=%llx\n",
						       __func__, p->name, err, npages, prime, offset);
						goto err;
					}

					if (!drm_mm_node_allocated(&vma->node) ||
					    i915_vma_misplaced(vma, 0, 0, offset | flags)) {
						pr_err("%s(%s) (forward) insert failed: vma.node=%llx + %llx [allocated? %d], expected offset %llx\n",
						       __func__, p->name, vma->node.start, vma->node.size, drm_mm_node_allocated(&vma->node),
						       offset);
						err = -EINVAL;
						goto err;
					}

					i915_vma_unpin(vma);

					if (p->step > 0) {
						if (offset + obj->base.size > hole_end)
							break;
						offset += obj->base.size;
					}
				}

				offset = p->offset;
				list_for_each_entry(obj, &objects, st_link) {
					vma = i915_vma_instance(obj, vm, NULL);
					if (IS_ERR(vma))
						continue;

					if (p->step < 0) {
						if (offset < hole_start + obj->base.size)
							break;
						offset -= obj->base.size;
					}

					if (!drm_mm_node_allocated(&vma->node) ||
					    i915_vma_misplaced(vma, 0, 0, offset | flags)) {
						pr_err("%s(%s) (forward) moved vma.node=%llx + %llx, expected offset %llx\n",
						       __func__, p->name, vma->node.start, vma->node.size,
						       offset);
						err = -EINVAL;
						goto err;
					}

					err = i915_vma_unbind(vma);
					if (err) {
						pr_err("%s(%s) (forward) unbind of vma.node=%llx + %llx failed with err=%d\n",
						       __func__, p->name, vma->node.start, vma->node.size,
						       err);
						goto err;
					}

					if (p->step > 0) {
						if (offset + obj->base.size > hole_end)
							break;
						offset += obj->base.size;
					}
				}

				offset = p->offset;
				list_for_each_entry_reverse(obj, &objects, st_link) {
					vma = i915_vma_instance(obj, vm, NULL);
					if (IS_ERR(vma))
						continue;

					if (p->step < 0) {
						if (offset < hole_start + obj->base.size)
							break;
						offset -= obj->base.size;
					}

					err = i915_vma_pin(vma, 0, 0, offset | flags);
					if (err) {
						pr_err("%s(%s) pin (backward) failed with err=%d on size=%lu pages (prime=%lu), offset=%llx\n",
						       __func__, p->name, err, npages, prime, offset);
						goto err;
					}

					if (!drm_mm_node_allocated(&vma->node) ||
					    i915_vma_misplaced(vma, 0, 0, offset | flags)) {
						pr_err("%s(%s) (backward) insert failed: vma.node=%llx + %llx [allocated? %d], expected offset %llx\n",
						       __func__, p->name, vma->node.start, vma->node.size, drm_mm_node_allocated(&vma->node),
						       offset);
						err = -EINVAL;
						goto err;
					}

					i915_vma_unpin(vma);

					if (p->step > 0) {
						if (offset + obj->base.size > hole_end)
							break;
						offset += obj->base.size;
					}
				}

				offset = p->offset;
				list_for_each_entry_reverse(obj, &objects, st_link) {
					vma = i915_vma_instance(obj, vm, NULL);
					if (IS_ERR(vma))
						continue;

					if (p->step < 0) {
						if (offset < hole_start + obj->base.size)
							break;
						offset -= obj->base.size;
					}

					if (!drm_mm_node_allocated(&vma->node) ||
					    i915_vma_misplaced(vma, 0, 0, offset | flags)) {
						pr_err("%s(%s) (backward) moved vma.node=%llx + %llx [allocated? %d], expected offset %llx\n",
						       __func__, p->name, vma->node.start, vma->node.size, drm_mm_node_allocated(&vma->node),
						       offset);
						err = -EINVAL;
						goto err;
					}

					err = i915_vma_unbind(vma);
					if (err) {
						pr_err("%s(%s) (backward) unbind of vma.node=%llx + %llx failed with err=%d\n",
						       __func__, p->name, vma->node.start, vma->node.size,
						       err);
						goto err;
					}

					if (p->step > 0) {
						if (offset + obj->base.size > hole_end)
							break;
						offset += obj->base.size;
					}
				}
			}

			if (igt_timeout(end_time, "%s timed out (npages=%lu, prime=%lu)\n",
					__func__, npages, prime)) {
				err = -EINTR;
				goto err;
			}
		}

		close_object_list(&objects, vm);
	}

	return 0;

err:
	close_object_list(&objects, vm);
	return err;
}

static int walk_hole(struct drm_i915_private *i915,
		     struct i915_address_space *vm,
		     u64 hole_start, u64 hole_end,
		     unsigned long end_time)
{
	const u64 hole_size = hole_end - hole_start;
	const unsigned long max_pages =
		min_t(u64, ULONG_MAX - 1, hole_size >> PAGE_SHIFT);
	unsigned long flags;
	u64 size;

	/* Try binding a single VMA in different positions within the hole */

	flags = PIN_OFFSET_FIXED | PIN_USER;
	if (i915_is_ggtt(vm))
		flags |= PIN_GLOBAL;

	for_each_prime_number_from(size, 1, max_pages) {
		struct drm_i915_gem_object *obj;
		struct i915_vma *vma;
		u64 addr;
		int err = 0;

		obj = fake_dma_object(i915, size << PAGE_SHIFT);
		if (IS_ERR(obj))
			break;

		vma = i915_vma_instance(obj, vm, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto err_put;
		}

		for (addr = hole_start;
		     addr + obj->base.size < hole_end;
		     addr += obj->base.size) {
			err = i915_vma_pin(vma, 0, 0, addr | flags);
			if (err) {
				pr_err("%s bind failed at %llx + %llx [hole %llx- %llx] with err=%d\n",
				       __func__, addr, vma->size,
				       hole_start, hole_end, err);
				goto err_close;
			}
			i915_vma_unpin(vma);

			if (!drm_mm_node_allocated(&vma->node) ||
			    i915_vma_misplaced(vma, 0, 0, addr | flags)) {
				pr_err("%s incorrect at %llx + %llx\n",
				       __func__, addr, vma->size);
				err = -EINVAL;
				goto err_close;
			}

			err = i915_vma_unbind(vma);
			if (err) {
				pr_err("%s unbind failed at %llx + %llx  with err=%d\n",
				       __func__, addr, vma->size, err);
				goto err_close;
			}

			GEM_BUG_ON(drm_mm_node_allocated(&vma->node));

			if (igt_timeout(end_time,
					"%s timed out at %llx\n",
					__func__, addr)) {
				err = -EINTR;
				goto err_close;
			}
		}

err_close:
		if (!i915_vma_is_ggtt(vma))
			i915_vma_close(vma);
err_put:
		i915_gem_object_put(obj);
		if (err)
			return err;
	}

	return 0;
}

static int pot_hole(struct drm_i915_private *i915,
		    struct i915_address_space *vm,
		    u64 hole_start, u64 hole_end,
		    unsigned long end_time)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	unsigned long flags;
	unsigned int pot;
	int err = 0;

	flags = PIN_OFFSET_FIXED | PIN_USER;
	if (i915_is_ggtt(vm))
		flags |= PIN_GLOBAL;

	obj = i915_gem_object_create_internal(i915, 2 * I915_GTT_PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_obj;
	}

	/* Insert a pair of pages across every pot boundary within the hole */
	for (pot = fls64(hole_end - 1) - 1;
	     pot > ilog2(2 * I915_GTT_PAGE_SIZE);
	     pot--) {
		u64 step = BIT_ULL(pot);
		u64 addr;

		for (addr = round_up(hole_start + I915_GTT_PAGE_SIZE, step) - I915_GTT_PAGE_SIZE;
		     addr <= round_down(hole_end - 2*I915_GTT_PAGE_SIZE, step) - I915_GTT_PAGE_SIZE;
		     addr += step) {
			err = i915_vma_pin(vma, 0, 0, addr | flags);
			if (err) {
				pr_err("%s failed to pin object at %llx in hole [%llx - %llx], with err=%d\n",
				       __func__,
				       addr,
				       hole_start, hole_end,
				       err);
				goto err;
			}

			if (!drm_mm_node_allocated(&vma->node) ||
			    i915_vma_misplaced(vma, 0, 0, addr | flags)) {
				pr_err("%s incorrect at %llx + %llx\n",
				       __func__, addr, vma->size);
				i915_vma_unpin(vma);
				err = i915_vma_unbind(vma);
				err = -EINVAL;
				goto err;
			}

			i915_vma_unpin(vma);
			err = i915_vma_unbind(vma);
			GEM_BUG_ON(err);
		}

		if (igt_timeout(end_time,
				"%s timed out after %d/%d\n",
				__func__, pot, fls64(hole_end - 1) - 1)) {
			err = -EINTR;
			goto err;
		}
	}

err:
	if (!i915_vma_is_ggtt(vma))
		i915_vma_close(vma);
err_obj:
	i915_gem_object_put(obj);
	return err;
}

static int drunk_hole(struct drm_i915_private *i915,
		      struct i915_address_space *vm,
		      u64 hole_start, u64 hole_end,
		      unsigned long end_time)
{
	I915_RND_STATE(prng);
	unsigned int size;
	unsigned long flags;

	flags = PIN_OFFSET_FIXED | PIN_USER;
	if (i915_is_ggtt(vm))
		flags |= PIN_GLOBAL;

	/* Keep creating larger objects until one cannot fit into the hole */
	for (size = 12; (hole_end - hole_start) >> size; size++) {
		struct drm_i915_gem_object *obj;
		unsigned int *order, count, n;
		struct i915_vma *vma;
		u64 hole_size;
		int err;

		hole_size = (hole_end - hole_start) >> size;
		if (hole_size > KMALLOC_MAX_SIZE / sizeof(u32))
			hole_size = KMALLOC_MAX_SIZE / sizeof(u32);
		count = hole_size;
		do {
			count >>= 1;
			order = i915_random_order(count, &prng);
		} while (!order && count);
		if (!order)
			break;

		/* Ignore allocation failures (i.e. don't report them as
		 * a test failure) as we are purposefully allocating very
		 * large objects without checking that we have sufficient
		 * memory. We expect to hit -ENOMEM.
		 */

		obj = fake_dma_object(i915, BIT_ULL(size));
		if (IS_ERR(obj)) {
			kfree(order);
			break;
		}

		vma = i915_vma_instance(obj, vm, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto err_obj;
		}

		GEM_BUG_ON(vma->size != BIT_ULL(size));

		for (n = 0; n < count; n++) {
			u64 addr = hole_start + order[n] * BIT_ULL(size);

			err = i915_vma_pin(vma, 0, 0, addr | flags);
			if (err) {
				pr_err("%s failed to pin object at %llx + %llx in hole [%llx - %llx], with err=%d\n",
				       __func__,
				       addr, BIT_ULL(size),
				       hole_start, hole_end,
				       err);
				goto err;
			}

			if (!drm_mm_node_allocated(&vma->node) ||
			    i915_vma_misplaced(vma, 0, 0, addr | flags)) {
				pr_err("%s incorrect at %llx + %llx\n",
				       __func__, addr, BIT_ULL(size));
				i915_vma_unpin(vma);
				err = i915_vma_unbind(vma);
				err = -EINVAL;
				goto err;
			}

			i915_vma_unpin(vma);
			err = i915_vma_unbind(vma);
			GEM_BUG_ON(err);

			if (igt_timeout(end_time,
					"%s timed out after %d/%d\n",
					__func__, n, count)) {
				err = -EINTR;
				goto err;
			}
		}

err:
		if (!i915_vma_is_ggtt(vma))
			i915_vma_close(vma);
err_obj:
		i915_gem_object_put(obj);
		kfree(order);
		if (err)
			return err;
	}

	return 0;
}

static int __shrink_hole(struct drm_i915_private *i915,
			 struct i915_address_space *vm,
			 u64 hole_start, u64 hole_end,
			 unsigned long end_time)
{
	struct drm_i915_gem_object *obj;
	unsigned long flags = PIN_OFFSET_FIXED | PIN_USER;
	unsigned int order = 12;
	LIST_HEAD(objects);
	int err = 0;
	u64 addr;

	/* Keep creating larger objects until one cannot fit into the hole */
	for (addr = hole_start; addr < hole_end; ) {
		struct i915_vma *vma;
		u64 size = BIT_ULL(order++);

		size = min(size, hole_end - addr);
		obj = fake_dma_object(i915, size);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			break;
		}

		list_add(&obj->st_link, &objects);

		vma = i915_vma_instance(obj, vm, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			break;
		}

		GEM_BUG_ON(vma->size != size);

		err = i915_vma_pin(vma, 0, 0, addr | flags);
		if (err) {
			pr_err("%s failed to pin object at %llx + %llx in hole [%llx - %llx], with err=%d\n",
			       __func__, addr, size, hole_start, hole_end, err);
			break;
		}

		if (!drm_mm_node_allocated(&vma->node) ||
		    i915_vma_misplaced(vma, 0, 0, addr | flags)) {
			pr_err("%s incorrect at %llx + %llx\n",
			       __func__, addr, size);
			i915_vma_unpin(vma);
			err = i915_vma_unbind(vma);
			err = -EINVAL;
			break;
		}

		i915_vma_unpin(vma);
		addr += size;

		if (igt_timeout(end_time,
				"%s timed out at ofset %llx [%llx - %llx]\n",
				__func__, addr, hole_start, hole_end)) {
			err = -EINTR;
			break;
		}
	}

	close_object_list(&objects, vm);
	return err;
}

static int shrink_hole(struct drm_i915_private *i915,
		       struct i915_address_space *vm,
		       u64 hole_start, u64 hole_end,
		       unsigned long end_time)
{
	unsigned long prime;
	int err;

	vm->fault_attr.probability = 999;
	atomic_set(&vm->fault_attr.times, -1);

	for_each_prime_number_from(prime, 0, ULONG_MAX - 1) {
		vm->fault_attr.interval = prime;
		err = __shrink_hole(i915, vm, hole_start, hole_end, end_time);
		if (err)
			break;
	}

	memset(&vm->fault_attr, 0, sizeof(vm->fault_attr));

	return err;
}

static int exercise_ppgtt(struct drm_i915_private *dev_priv,
			  int (*func)(struct drm_i915_private *i915,
				      struct i915_address_space *vm,
				      u64 hole_start, u64 hole_end,
				      unsigned long end_time))
{
	struct drm_file *file;
	struct i915_hw_ppgtt *ppgtt;
	IGT_TIMEOUT(end_time);
	int err;

	if (!USES_FULL_PPGTT(dev_priv))
		return 0;

	file = mock_file(dev_priv);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&dev_priv->drm.struct_mutex);
	ppgtt = i915_ppgtt_create(dev_priv, file->driver_priv, "mock");
	if (IS_ERR(ppgtt)) {
		err = PTR_ERR(ppgtt);
		goto out_unlock;
	}
	GEM_BUG_ON(offset_in_page(ppgtt->base.total));
	GEM_BUG_ON(ppgtt->base.closed);

	err = func(dev_priv, &ppgtt->base, 0, ppgtt->base.total, end_time);

	i915_ppgtt_close(&ppgtt->base);
	i915_ppgtt_put(ppgtt);
out_unlock:
	mutex_unlock(&dev_priv->drm.struct_mutex);

	mock_file_free(dev_priv, file);
	return err;
}

static int igt_ppgtt_fill(void *arg)
{
	return exercise_ppgtt(arg, fill_hole);
}

static int igt_ppgtt_walk(void *arg)
{
	return exercise_ppgtt(arg, walk_hole);
}

static int igt_ppgtt_pot(void *arg)
{
	return exercise_ppgtt(arg, pot_hole);
}

static int igt_ppgtt_drunk(void *arg)
{
	return exercise_ppgtt(arg, drunk_hole);
}

static int igt_ppgtt_lowlevel(void *arg)
{
	return exercise_ppgtt(arg, lowlevel_hole);
}

static int igt_ppgtt_shrink(void *arg)
{
	return exercise_ppgtt(arg, shrink_hole);
}

static int sort_holes(void *priv, struct list_head *A, struct list_head *B)
{
	struct drm_mm_node *a = list_entry(A, typeof(*a), hole_stack);
	struct drm_mm_node *b = list_entry(B, typeof(*b), hole_stack);

	if (a->start < b->start)
		return -1;
	else
		return 1;
}

static int exercise_ggtt(struct drm_i915_private *i915,
			 int (*func)(struct drm_i915_private *i915,
				     struct i915_address_space *vm,
				     u64 hole_start, u64 hole_end,
				     unsigned long end_time))
{
	struct i915_ggtt *ggtt = &i915->ggtt;
	u64 hole_start, hole_end, last = 0;
	struct drm_mm_node *node;
	IGT_TIMEOUT(end_time);
	int err;

	mutex_lock(&i915->drm.struct_mutex);
restart:
	list_sort(NULL, &ggtt->base.mm.hole_stack, sort_holes);
	drm_mm_for_each_hole(node, &ggtt->base.mm, hole_start, hole_end) {
		if (hole_start < last)
			continue;

		if (ggtt->base.mm.color_adjust)
			ggtt->base.mm.color_adjust(node, 0,
						   &hole_start, &hole_end);
		if (hole_start >= hole_end)
			continue;

		err = func(i915, &ggtt->base, hole_start, hole_end, end_time);
		if (err)
			break;

		/* As we have manipulated the drm_mm, the list may be corrupt */
		last = hole_end;
		goto restart;
	}
	mutex_unlock(&i915->drm.struct_mutex);

	return err;
}

static int igt_ggtt_fill(void *arg)
{
	return exercise_ggtt(arg, fill_hole);
}

static int igt_ggtt_walk(void *arg)
{
	return exercise_ggtt(arg, walk_hole);
}

static int igt_ggtt_pot(void *arg)
{
	return exercise_ggtt(arg, pot_hole);
}

static int igt_ggtt_drunk(void *arg)
{
	return exercise_ggtt(arg, drunk_hole);
}

static int igt_ggtt_lowlevel(void *arg)
{
	return exercise_ggtt(arg, lowlevel_hole);
}

static int igt_ggtt_page(void *arg)
{
	const unsigned int count = PAGE_SIZE/sizeof(u32);
	I915_RND_STATE(prng);
	struct drm_i915_private *i915 = arg;
	struct i915_ggtt *ggtt = &i915->ggtt;
	struct drm_i915_gem_object *obj;
	struct drm_mm_node tmp;
	unsigned int *order, n;
	int err;

	mutex_lock(&i915->drm.struct_mutex);

	obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto out_unlock;
	}

	err = i915_gem_object_pin_pages(obj);
	if (err)
		goto out_free;

	memset(&tmp, 0, sizeof(tmp));
	err = drm_mm_insert_node_in_range(&ggtt->base.mm, &tmp,
					  1024 * PAGE_SIZE, 0,
					  I915_COLOR_UNEVICTABLE,
					  0, ggtt->mappable_end,
					  DRM_MM_INSERT_LOW);
	if (err)
		goto out_unpin;

	order = i915_random_order(count, &prng);
	if (!order) {
		err = -ENOMEM;
		goto out_remove;
	}

	for (n = 0; n < count; n++) {
		u64 offset = tmp.start + order[n] * PAGE_SIZE;
		u32 __iomem *vaddr;

		ggtt->base.insert_page(&ggtt->base,
				       i915_gem_object_get_dma_address(obj, 0),
				       offset, I915_CACHE_NONE, 0);

		vaddr = io_mapping_map_atomic_wc(&ggtt->mappable, offset);
		iowrite32(n, vaddr + n);
		io_mapping_unmap_atomic(vaddr);

		wmb();
		ggtt->base.clear_range(&ggtt->base, offset, PAGE_SIZE);
	}

	i915_random_reorder(order, count, &prng);
	for (n = 0; n < count; n++) {
		u64 offset = tmp.start + order[n] * PAGE_SIZE;
		u32 __iomem *vaddr;
		u32 val;

		ggtt->base.insert_page(&ggtt->base,
				       i915_gem_object_get_dma_address(obj, 0),
				       offset, I915_CACHE_NONE, 0);

		vaddr = io_mapping_map_atomic_wc(&ggtt->mappable, offset);
		val = ioread32(vaddr + n);
		io_mapping_unmap_atomic(vaddr);

		ggtt->base.clear_range(&ggtt->base, offset, PAGE_SIZE);

		if (val != n) {
			pr_err("insert page failed: found %d, expected %d\n",
			       val, n);
			err = -EINVAL;
			break;
		}
	}

	kfree(order);
out_remove:
	drm_mm_remove_node(&tmp);
out_unpin:
	i915_gem_object_unpin_pages(obj);
out_free:
	i915_gem_object_put(obj);
out_unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static void track_vma_bind(struct i915_vma *vma)
{
	struct drm_i915_gem_object *obj = vma->obj;

	obj->bind_count++; /* track for eviction later */
	__i915_gem_object_pin_pages(obj);

	vma->pages = obj->mm.pages;
	list_move_tail(&vma->vm_link, &vma->vm->inactive_list);
}

static int exercise_mock(struct drm_i915_private *i915,
			 int (*func)(struct drm_i915_private *i915,
				     struct i915_address_space *vm,
				     u64 hole_start, u64 hole_end,
				     unsigned long end_time))
{
	struct i915_gem_context *ctx;
	struct i915_hw_ppgtt *ppgtt;
	IGT_TIMEOUT(end_time);
	int err;

	ctx = mock_context(i915, "mock");
	if (!ctx)
		return -ENOMEM;

	ppgtt = ctx->ppgtt;
	GEM_BUG_ON(!ppgtt);

	err = func(i915, &ppgtt->base, 0, ppgtt->base.total, end_time);

	mock_context_close(ctx);
	return err;
}

static int igt_mock_fill(void *arg)
{
	return exercise_mock(arg, fill_hole);
}

static int igt_mock_walk(void *arg)
{
	return exercise_mock(arg, walk_hole);
}

static int igt_mock_pot(void *arg)
{
	return exercise_mock(arg, pot_hole);
}

static int igt_mock_drunk(void *arg)
{
	return exercise_mock(arg, drunk_hole);
}

static int igt_gtt_reserve(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj, *on;
	LIST_HEAD(objects);
	u64 total;
	int err;

	/* i915_gem_gtt_reserve() tries to reserve the precise range
	 * for the node, and evicts if it has to. So our test checks that
	 * it can give us the requsted space and prevent overlaps.
	 */

	/* Start by filling the GGTT */
	for (total = 0;
	     total + 2*I915_GTT_PAGE_SIZE <= i915->ggtt.base.total;
	     total += 2*I915_GTT_PAGE_SIZE) {
		struct i915_vma *vma;

		obj = i915_gem_object_create_internal(i915, 2*PAGE_SIZE);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			goto out;
		}

		err = i915_gem_object_pin_pages(obj);
		if (err) {
			i915_gem_object_put(obj);
			goto out;
		}

		list_add(&obj->st_link, &objects);

		vma = i915_vma_instance(obj, &i915->ggtt.base, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto out;
		}

		err = i915_gem_gtt_reserve(&i915->ggtt.base, &vma->node,
					   obj->base.size,
					   total,
					   obj->cache_level,
					   0);
		if (err) {
			pr_err("i915_gem_gtt_reserve (pass 1) failed at %llu/%llu with err=%d\n",
			       total, i915->ggtt.base.total, err);
			goto out;
		}
		track_vma_bind(vma);

		GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
		if (vma->node.start != total ||
		    vma->node.size != 2*I915_GTT_PAGE_SIZE) {
			pr_err("i915_gem_gtt_reserve (pass 1) placement failed, found (%llx + %llx), expected (%llx + %lx)\n",
			       vma->node.start, vma->node.size,
			       total, 2*I915_GTT_PAGE_SIZE);
			err = -EINVAL;
			goto out;
		}
	}

	/* Now we start forcing evictions */
	for (total = I915_GTT_PAGE_SIZE;
	     total + 2*I915_GTT_PAGE_SIZE <= i915->ggtt.base.total;
	     total += 2*I915_GTT_PAGE_SIZE) {
		struct i915_vma *vma;

		obj = i915_gem_object_create_internal(i915, 2*PAGE_SIZE);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			goto out;
		}

		err = i915_gem_object_pin_pages(obj);
		if (err) {
			i915_gem_object_put(obj);
			goto out;
		}

		list_add(&obj->st_link, &objects);

		vma = i915_vma_instance(obj, &i915->ggtt.base, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto out;
		}

		err = i915_gem_gtt_reserve(&i915->ggtt.base, &vma->node,
					   obj->base.size,
					   total,
					   obj->cache_level,
					   0);
		if (err) {
			pr_err("i915_gem_gtt_reserve (pass 2) failed at %llu/%llu with err=%d\n",
			       total, i915->ggtt.base.total, err);
			goto out;
		}
		track_vma_bind(vma);

		GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
		if (vma->node.start != total ||
		    vma->node.size != 2*I915_GTT_PAGE_SIZE) {
			pr_err("i915_gem_gtt_reserve (pass 2) placement failed, found (%llx + %llx), expected (%llx + %lx)\n",
			       vma->node.start, vma->node.size,
			       total, 2*I915_GTT_PAGE_SIZE);
			err = -EINVAL;
			goto out;
		}
	}

	/* And then try at random */
	list_for_each_entry_safe(obj, on, &objects, st_link) {
		struct i915_vma *vma;
		u64 offset;

		vma = i915_vma_instance(obj, &i915->ggtt.base, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto out;
		}

		err = i915_vma_unbind(vma);
		if (err) {
			pr_err("i915_vma_unbind failed with err=%d!\n", err);
			goto out;
		}

		offset = random_offset(0, i915->ggtt.base.total,
				       2*I915_GTT_PAGE_SIZE,
				       I915_GTT_MIN_ALIGNMENT);

		err = i915_gem_gtt_reserve(&i915->ggtt.base, &vma->node,
					   obj->base.size,
					   offset,
					   obj->cache_level,
					   0);
		if (err) {
			pr_err("i915_gem_gtt_reserve (pass 3) failed at %llu/%llu with err=%d\n",
			       total, i915->ggtt.base.total, err);
			goto out;
		}
		track_vma_bind(vma);

		GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
		if (vma->node.start != offset ||
		    vma->node.size != 2*I915_GTT_PAGE_SIZE) {
			pr_err("i915_gem_gtt_reserve (pass 3) placement failed, found (%llx + %llx), expected (%llx + %lx)\n",
			       vma->node.start, vma->node.size,
			       offset, 2*I915_GTT_PAGE_SIZE);
			err = -EINVAL;
			goto out;
		}
	}

out:
	list_for_each_entry_safe(obj, on, &objects, st_link) {
		i915_gem_object_unpin_pages(obj);
		i915_gem_object_put(obj);
	}
	return err;
}

static int igt_gtt_insert(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj, *on;
	struct drm_mm_node tmp = {};
	const struct invalid_insert {
		u64 size;
		u64 alignment;
		u64 start, end;
	} invalid_insert[] = {
		{
			i915->ggtt.base.total + I915_GTT_PAGE_SIZE, 0,
			0, i915->ggtt.base.total,
		},
		{
			2*I915_GTT_PAGE_SIZE, 0,
			0, I915_GTT_PAGE_SIZE,
		},
		{
			-(u64)I915_GTT_PAGE_SIZE, 0,
			0, 4*I915_GTT_PAGE_SIZE,
		},
		{
			-(u64)2*I915_GTT_PAGE_SIZE, 2*I915_GTT_PAGE_SIZE,
			0, 4*I915_GTT_PAGE_SIZE,
		},
		{
			I915_GTT_PAGE_SIZE, I915_GTT_MIN_ALIGNMENT << 1,
			I915_GTT_MIN_ALIGNMENT, I915_GTT_MIN_ALIGNMENT << 1,
		},
		{}
	}, *ii;
	LIST_HEAD(objects);
	u64 total;
	int err;

	/* i915_gem_gtt_insert() tries to allocate some free space in the GTT
	 * to the node, evicting if required.
	 */

	/* Check a couple of obviously invalid requests */
	for (ii = invalid_insert; ii->size; ii++) {
		err = i915_gem_gtt_insert(&i915->ggtt.base, &tmp,
					  ii->size, ii->alignment,
					  I915_COLOR_UNEVICTABLE,
					  ii->start, ii->end,
					  0);
		if (err != -ENOSPC) {
			pr_err("Invalid i915_gem_gtt_insert(.size=%llx, .alignment=%llx, .start=%llx, .end=%llx) succeeded (err=%d)\n",
			       ii->size, ii->alignment, ii->start, ii->end,
			       err);
			return -EINVAL;
		}
	}

	/* Start by filling the GGTT */
	for (total = 0;
	     total + I915_GTT_PAGE_SIZE <= i915->ggtt.base.total;
	     total += I915_GTT_PAGE_SIZE) {
		struct i915_vma *vma;

		obj = i915_gem_object_create_internal(i915, I915_GTT_PAGE_SIZE);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			goto out;
		}

		err = i915_gem_object_pin_pages(obj);
		if (err) {
			i915_gem_object_put(obj);
			goto out;
		}

		list_add(&obj->st_link, &objects);

		vma = i915_vma_instance(obj, &i915->ggtt.base, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto out;
		}

		err = i915_gem_gtt_insert(&i915->ggtt.base, &vma->node,
					  obj->base.size, 0, obj->cache_level,
					  0, i915->ggtt.base.total,
					  0);
		if (err == -ENOSPC) {
			/* maxed out the GGTT space */
			i915_gem_object_put(obj);
			break;
		}
		if (err) {
			pr_err("i915_gem_gtt_insert (pass 1) failed at %llu/%llu with err=%d\n",
			       total, i915->ggtt.base.total, err);
			goto out;
		}
		track_vma_bind(vma);
		__i915_vma_pin(vma);

		GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	}

	list_for_each_entry(obj, &objects, st_link) {
		struct i915_vma *vma;

		vma = i915_vma_instance(obj, &i915->ggtt.base, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto out;
		}

		if (!drm_mm_node_allocated(&vma->node)) {
			pr_err("VMA was unexpectedly evicted!\n");
			err = -EINVAL;
			goto out;
		}

		__i915_vma_unpin(vma);
	}

	/* If we then reinsert, we should find the same hole */
	list_for_each_entry_safe(obj, on, &objects, st_link) {
		struct i915_vma *vma;
		u64 offset;

		vma = i915_vma_instance(obj, &i915->ggtt.base, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto out;
		}

		GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
		offset = vma->node.start;

		err = i915_vma_unbind(vma);
		if (err) {
			pr_err("i915_vma_unbind failed with err=%d!\n", err);
			goto out;
		}

		err = i915_gem_gtt_insert(&i915->ggtt.base, &vma->node,
					  obj->base.size, 0, obj->cache_level,
					  0, i915->ggtt.base.total,
					  0);
		if (err) {
			pr_err("i915_gem_gtt_insert (pass 2) failed at %llu/%llu with err=%d\n",
			       total, i915->ggtt.base.total, err);
			goto out;
		}
		track_vma_bind(vma);

		GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
		if (vma->node.start != offset) {
			pr_err("i915_gem_gtt_insert did not return node to its previous location (the only hole), expected address %llx, found %llx\n",
			       offset, vma->node.start);
			err = -EINVAL;
			goto out;
		}
	}

	/* And then force evictions */
	for (total = 0;
	     total + 2*I915_GTT_PAGE_SIZE <= i915->ggtt.base.total;
	     total += 2*I915_GTT_PAGE_SIZE) {
		struct i915_vma *vma;

		obj = i915_gem_object_create_internal(i915, 2*I915_GTT_PAGE_SIZE);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			goto out;
		}

		err = i915_gem_object_pin_pages(obj);
		if (err) {
			i915_gem_object_put(obj);
			goto out;
		}

		list_add(&obj->st_link, &objects);

		vma = i915_vma_instance(obj, &i915->ggtt.base, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto out;
		}

		err = i915_gem_gtt_insert(&i915->ggtt.base, &vma->node,
					  obj->base.size, 0, obj->cache_level,
					  0, i915->ggtt.base.total,
					  0);
		if (err) {
			pr_err("i915_gem_gtt_insert (pass 3) failed at %llu/%llu with err=%d\n",
			       total, i915->ggtt.base.total, err);
			goto out;
		}
		track_vma_bind(vma);

		GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	}

out:
	list_for_each_entry_safe(obj, on, &objects, st_link) {
		i915_gem_object_unpin_pages(obj);
		i915_gem_object_put(obj);
	}
	return err;
}

int i915_gem_gtt_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_mock_drunk),
		SUBTEST(igt_mock_walk),
		SUBTEST(igt_mock_pot),
		SUBTEST(igt_mock_fill),
		SUBTEST(igt_gtt_reserve),
		SUBTEST(igt_gtt_insert),
	};
	struct drm_i915_private *i915;
	int err;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	mutex_lock(&i915->drm.struct_mutex);
	err = i915_subtests(tests, i915);
	mutex_unlock(&i915->drm.struct_mutex);

	drm_dev_unref(&i915->drm);
	return err;
}

int i915_gem_gtt_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_ppgtt_alloc),
		SUBTEST(igt_ppgtt_lowlevel),
		SUBTEST(igt_ppgtt_drunk),
		SUBTEST(igt_ppgtt_walk),
		SUBTEST(igt_ppgtt_pot),
		SUBTEST(igt_ppgtt_fill),
		SUBTEST(igt_ppgtt_shrink),
		SUBTEST(igt_ggtt_lowlevel),
		SUBTEST(igt_ggtt_drunk),
		SUBTEST(igt_ggtt_walk),
		SUBTEST(igt_ggtt_pot),
		SUBTEST(igt_ggtt_fill),
		SUBTEST(igt_ggtt_page),
	};

	GEM_BUG_ON(offset_in_page(i915->ggtt.base.total));

	return i915_subtests(tests, i915);
}
