/*
 * Copyright © 2017 Intel Corporation
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

#include "../i915_selftest.h"

#include <linux/prime_numbers.h>

#include "mock_drm.h"
#include "i915_random.h"

static const unsigned int page_sizes[] = {
	I915_GTT_PAGE_SIZE_2M,
	I915_GTT_PAGE_SIZE_64K,
	I915_GTT_PAGE_SIZE_4K,
};

static unsigned int get_largest_page_size(struct drm_i915_private *i915,
					  u64 rem)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(page_sizes); ++i) {
		unsigned int page_size = page_sizes[i];

		if (HAS_PAGE_SIZES(i915, page_size) && rem >= page_size)
			return page_size;
	}

	return 0;
}

static void huge_pages_free_pages(struct sg_table *st)
{
	struct scatterlist *sg;

	for (sg = st->sgl; sg; sg = __sg_next(sg)) {
		if (sg_page(sg))
			__free_pages(sg_page(sg), get_order(sg->length));
	}

	sg_free_table(st);
	kfree(st);
}

static int get_huge_pages(struct drm_i915_gem_object *obj)
{
#define GFP (GFP_KERNEL | __GFP_NOWARN | __GFP_NORETRY)
	unsigned int page_mask = obj->mm.page_mask;
	struct sg_table *st;
	struct scatterlist *sg;
	unsigned int sg_page_sizes;
	u64 rem;

	st = kmalloc(sizeof(*st), GFP);
	if (!st)
		return -ENOMEM;

	if (sg_alloc_table(st, obj->base.size >> PAGE_SHIFT, GFP)) {
		kfree(st);
		return -ENOMEM;
	}

	rem = obj->base.size;
	sg = st->sgl;
	st->nents = 0;
	sg_page_sizes = 0;

	/*
	 * Our goal here is simple, we want to greedily fill the object from
	 * largest to smallest page-size, while ensuring that we use *every*
	 * page-size as per the given page-mask.
	 */
	do {
		unsigned int bit = ilog2(page_mask);
		unsigned int page_size = BIT(bit);
		int order = get_order(page_size);

		do {
			struct page *page;

			GEM_BUG_ON(order >= MAX_ORDER);
			page = alloc_pages(GFP | __GFP_ZERO, order);
			if (!page)
				goto err;

			sg_set_page(sg, page, page_size, 0);
			sg_page_sizes |= page_size;
			st->nents++;

			rem -= page_size;
			if (!rem) {
				sg_mark_end(sg);
				break;
			}

			sg = __sg_next(sg);
		} while ((rem - ((page_size-1) & page_mask)) >= page_size);

		page_mask &= (page_size-1);
	} while (page_mask);

	if (i915_gem_gtt_prepare_pages(obj, st))
		goto err;

	obj->mm.madv = I915_MADV_DONTNEED;

	GEM_BUG_ON(sg_page_sizes != obj->mm.page_mask);
	__i915_gem_object_set_pages(obj, st, sg_page_sizes);

	return 0;

err:
	sg_set_page(sg, NULL, 0, 0);
	sg_mark_end(sg);
	huge_pages_free_pages(st);

	return -ENOMEM;
}

static void put_huge_pages(struct drm_i915_gem_object *obj,
			   struct sg_table *pages)
{
	i915_gem_gtt_finish_pages(obj, pages);
	huge_pages_free_pages(pages);

	obj->mm.dirty = false;
	obj->mm.madv = I915_MADV_WILLNEED;
}

static const struct drm_i915_gem_object_ops huge_page_ops = {
	.flags = I915_GEM_OBJECT_HAS_STRUCT_PAGE |
		 I915_GEM_OBJECT_IS_SHRINKABLE,
	.get_pages = get_huge_pages,
	.put_pages = put_huge_pages,
};

static struct drm_i915_gem_object *
huge_pages_object(struct drm_i915_private *i915,
		  u64 size,
		  unsigned int page_mask)
{
	struct drm_i915_gem_object *obj;

	GEM_BUG_ON(!size);
	GEM_BUG_ON(!IS_ALIGNED(size, BIT(__ffs(page_mask))));

	if (size >> PAGE_SHIFT > INT_MAX)
		return ERR_PTR(-E2BIG);

	if (overflows_type(size, obj->base.size))
		return ERR_PTR(-E2BIG);

	obj = i915_gem_object_alloc(i915);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	drm_gem_private_object_init(&i915->drm, &obj->base, size);
	i915_gem_object_init(obj, &huge_page_ops);

	obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	obj->base.read_domains = I915_GEM_DOMAIN_CPU;
	obj->cache_level = I915_CACHE_NONE;

	obj->mm.page_mask = page_mask;

	return obj;
}

static int fake_get_huge_pages(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	const u64 max_len = rounddown_pow_of_two(UINT_MAX);
	struct sg_table *st;
	struct scatterlist *sg;
	unsigned int sg_page_sizes;
	u64 rem;

	st = kmalloc(sizeof(*st), GFP);
	if (!st)
		return -ENOMEM;

	if (sg_alloc_table(st, obj->base.size >> PAGE_SHIFT, GFP)) {
		kfree(st);
		return -ENOMEM;
	}

	/* Use optimal page sized chunks to fill in the sg table */
	rem = obj->base.size;
	sg = st->sgl;
	st->nents = 0;
	sg_page_sizes = 0;
	do {
		unsigned int page_size = get_largest_page_size(i915, rem);
		unsigned int len = min(page_size * div_u64(rem, page_size),
				       max_len);

		GEM_BUG_ON(!page_size);

		sg->offset = 0;
		sg->length = len;
		sg_dma_len(sg) = len;
		sg_dma_address(sg) = page_size;

		sg_page_sizes |= len;

		st->nents++;

		rem -= len;
		if (!rem) {
			sg_mark_end(sg);
			break;
		}

		sg = sg_next(sg);
	} while (1);

	obj->mm.madv = I915_MADV_DONTNEED;

	__i915_gem_object_set_pages(obj, st, sg_page_sizes);

	return 0;
}

static int fake_get_huge_pages_single(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct sg_table *st;
	struct scatterlist *sg;
	unsigned int page_size;

	st = kmalloc(sizeof(*st), GFP);
	if (!st)
		return -ENOMEM;

	if (sg_alloc_table(st, 1, GFP)) {
		kfree(st);
		return -ENOMEM;
	}

	sg = st->sgl;
	st->nents = 1;

	page_size = get_largest_page_size(i915, obj->base.size);
	GEM_BUG_ON(!page_size);

	sg->offset = 0;
	sg->length = obj->base.size;
	sg_dma_len(sg) = obj->base.size;
	sg_dma_address(sg) = page_size;

	obj->mm.madv = I915_MADV_DONTNEED;

	__i915_gem_object_set_pages(obj, st, sg->length);

	return 0;
#undef GFP
}

static void fake_free_huge_pages(struct drm_i915_gem_object *obj,
				 struct sg_table *pages)
{
	sg_free_table(pages);
	kfree(pages);
}

static void fake_put_huge_pages(struct drm_i915_gem_object *obj,
				struct sg_table *pages)
{
	fake_free_huge_pages(obj, pages);
	obj->mm.dirty = false;
	obj->mm.madv = I915_MADV_WILLNEED;
}

static const struct drm_i915_gem_object_ops fake_ops = {
	.flags = I915_GEM_OBJECT_IS_SHRINKABLE,
	.get_pages = fake_get_huge_pages,
	.put_pages = fake_put_huge_pages,
};

static const struct drm_i915_gem_object_ops fake_ops_single = {
	.flags = I915_GEM_OBJECT_IS_SHRINKABLE,
	.get_pages = fake_get_huge_pages_single,
	.put_pages = fake_put_huge_pages,
};

static struct drm_i915_gem_object *
fake_huge_pages_object(struct drm_i915_private *i915, u64 size, bool single)
{
	struct drm_i915_gem_object *obj;

	GEM_BUG_ON(!size);
	GEM_BUG_ON(!IS_ALIGNED(size, I915_GTT_PAGE_SIZE));

	if (size >> PAGE_SHIFT > UINT_MAX)
		return ERR_PTR(-E2BIG);

	if (overflows_type(size, obj->base.size))
		return ERR_PTR(-E2BIG);

	obj = i915_gem_object_alloc(i915);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	drm_gem_private_object_init(&i915->drm, &obj->base, size);

	if (single)
		i915_gem_object_init(obj, &fake_ops_single);
	else
		i915_gem_object_init(obj, &fake_ops);

	obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	obj->base.read_domains = I915_GEM_DOMAIN_CPU;
	obj->cache_level = I915_CACHE_NONE;

	return obj;
}

static int igt_check_page_sizes(struct i915_vma *vma)
{
	struct drm_i915_private *i915 = to_i915(vma->obj->base.dev);
	unsigned int supported = INTEL_INFO(i915)->page_sizes;
	struct drm_i915_gem_object *obj = vma->obj;
	int err = 0;

	if (!HAS_PAGE_SIZES(i915, vma->page_sizes.sg)) {
		pr_err("unsupported page_sizes.sg=%u, supported=%u\n",
		       vma->page_sizes.sg & ~supported, supported);
		err = -EINVAL;
	}

	if (!HAS_PAGE_SIZES(i915, vma->page_sizes.gtt)) {
		pr_err("unsupported page_sizes.gtt=%u, supported=%u\n",
		       vma->page_sizes.gtt & ~supported, supported);
		err = -EINVAL;
	}

	if (vma->page_sizes.phys != obj->mm.page_sizes.phys) {
		pr_err("vma->page_sizes.phys(%u) != obj->mm.page_sizes.phys(%u)\n",
		       vma->page_sizes.phys, obj->mm.page_sizes.phys);
		err = -EINVAL;
	}

	if (vma->page_sizes.sg != obj->mm.page_sizes.sg) {
		pr_err("vma->page_sizes.sg(%u) != obj->mm.page_sizes.sg(%u)\n",
		       vma->page_sizes.sg, obj->mm.page_sizes.sg);
		err = -EINVAL;
	}

	if (obj->mm.page_sizes.gtt) {
		pr_err("obj->page_sizes.gtt(%u) should never be set\n",
		       obj->mm.page_sizes.gtt);
		err = -EINVAL;
	}

	return err;
}

static int igt_mock_exhaust_device_supported_pages(void *arg)
{
	struct i915_hw_ppgtt *ppgtt = arg;
	struct drm_i915_private *i915 = ppgtt->base.i915;
	unsigned int saved_mask = INTEL_INFO(i915)->page_sizes;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int i, j, single;
	int err;

	/*
	 * Sanity check creating objects with every valid page support
	 * combination for our mock device.
	 */

	for (i = 1; i < BIT(ARRAY_SIZE(page_sizes)); i++) {
		unsigned int combination = 0;

		for (j = 0; j < ARRAY_SIZE(page_sizes); j++) {
			if (i & BIT(j))
				combination |= page_sizes[j];
		}

		mkwrite_device_info(i915)->page_sizes = combination;

		for (single = 0; single <= 1; ++single) {
			obj = fake_huge_pages_object(i915, combination, !!single);
			if (IS_ERR(obj)) {
				err = PTR_ERR(obj);
				goto out_device;
			}

			if (obj->base.size != combination) {
				pr_err("obj->base.size=%zu, expected=%u\n",
				       obj->base.size, combination);
				err = -EINVAL;
				goto out_put;
			}

			vma = i915_vma_instance(obj, &ppgtt->base, NULL);
			if (IS_ERR(vma)) {
				err = PTR_ERR(vma);
				goto out_put;
			}

			err = i915_vma_pin(vma, 0, 0, PIN_USER);
			if (err)
				goto out_close;

			err = igt_check_page_sizes(vma);

			if (vma->page_sizes.sg != combination) {
				pr_err("page_sizes.sg=%u, expected=%u\n",
				       vma->page_sizes.sg, combination);
				err = -EINVAL;
			}

			i915_vma_unpin(vma);
			i915_vma_close(vma);

			i915_gem_object_put(obj);

			if (err)
				goto out_device;
		}
	}

	goto out_device;

out_close:
	i915_vma_close(vma);
out_put:
	i915_gem_object_put(obj);
out_device:
	mkwrite_device_info(i915)->page_sizes = saved_mask;

	return err;
}

static int igt_mock_ppgtt_misaligned_dma(void *arg)
{
	struct i915_hw_ppgtt *ppgtt = arg;
	struct drm_i915_private *i915 = ppgtt->base.i915;
	unsigned long supported = INTEL_INFO(i915)->page_sizes;
	struct drm_i915_gem_object *obj;
	int bit;
	int err;

	/*
	 * Sanity check dma misalignment for huge pages -- the dma addresses we
	 * insert into the paging structures need to always respect the page
	 * size alignment.
	 */

	bit = ilog2(I915_GTT_PAGE_SIZE_64K);

	for_each_set_bit_from(bit, &supported,
			      ilog2(I915_GTT_MAX_PAGE_SIZE) + 1) {
		IGT_TIMEOUT(end_time);
		unsigned int page_size = BIT(bit);
		unsigned int flags = PIN_USER | PIN_OFFSET_FIXED;
		unsigned int offset;
		unsigned int size =
			round_up(page_size, I915_GTT_PAGE_SIZE_2M) << 1;
		struct i915_vma *vma;

		obj = fake_huge_pages_object(i915, size, true);
		if (IS_ERR(obj))
			return PTR_ERR(obj);

		if (obj->base.size != size) {
			pr_err("obj->base.size=%zu, expected=%u\n",
			       obj->base.size, size);
			err = -EINVAL;
			goto out_put;
		}

		err = i915_gem_object_pin_pages(obj);
		if (err)
			goto out_put;

		/* Force the page size for this object */
		obj->mm.page_sizes.sg = page_size;

		vma = i915_vma_instance(obj, &ppgtt->base, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto out_unpin;
		}

		err = i915_vma_pin(vma, 0, 0, flags);
		if (err) {
			i915_vma_close(vma);
			goto out_unpin;
		}


		err = igt_check_page_sizes(vma);

		if (vma->page_sizes.gtt != page_size) {
			pr_err("page_sizes.gtt=%u, expected %u\n",
			       vma->page_sizes.gtt, page_size);
			err = -EINVAL;
		}

		i915_vma_unpin(vma);

		if (err) {
			i915_vma_close(vma);
			goto out_unpin;
		}

		/*
		 * Try all the other valid offsets until the next
		 * boundary -- should always fall back to using 4K
		 * pages.
		 */
		for (offset = 4096; offset < page_size; offset += 4096) {
			err = i915_vma_unbind(vma);
			if (err) {
				i915_vma_close(vma);
				goto out_unpin;
			}

			err = i915_vma_pin(vma, 0, 0, flags | offset);
			if (err) {
				i915_vma_close(vma);
				goto out_unpin;
			}

			err = igt_check_page_sizes(vma);

			if (vma->page_sizes.gtt != I915_GTT_PAGE_SIZE_4K) {
				pr_err("page_sizes.gtt=%u, expected %lu\n",
				       vma->page_sizes.gtt, I915_GTT_PAGE_SIZE_4K);
				err = -EINVAL;
			}

			i915_vma_unpin(vma);

			if (err) {
				i915_vma_close(vma);
				goto out_unpin;
			}

			if (igt_timeout(end_time,
					"%s timed out at offset %x with page-size %x\n",
					__func__, offset, page_size))
				break;
		}

		i915_vma_close(vma);

		i915_gem_object_unpin_pages(obj);
		i915_gem_object_put(obj);
	}

	return 0;

out_unpin:
	i915_gem_object_unpin_pages(obj);
out_put:
	i915_gem_object_put(obj);

	return err;
}

static void close_object_list(struct list_head *objects,
			      struct i915_hw_ppgtt *ppgtt)
{
	struct drm_i915_gem_object *obj, *on;

	list_for_each_entry_safe(obj, on, objects, st_link) {
		struct i915_vma *vma;

		vma = i915_vma_instance(obj, &ppgtt->base, NULL);
		if (!IS_ERR(vma))
			i915_vma_close(vma);

		list_del(&obj->st_link);
		i915_gem_object_unpin_pages(obj);
		i915_gem_object_put(obj);
	}
}

static int igt_mock_ppgtt_huge_fill(void *arg)
{
	struct i915_hw_ppgtt *ppgtt = arg;
	struct drm_i915_private *i915 = ppgtt->base.i915;
	unsigned long max_pages = ppgtt->base.total >> PAGE_SHIFT;
	unsigned long page_num;
	bool single = false;
	LIST_HEAD(objects);
	IGT_TIMEOUT(end_time);
	int err = -ENODEV;

	for_each_prime_number_from(page_num, 1, max_pages) {
		struct drm_i915_gem_object *obj;
		u64 size = page_num << PAGE_SHIFT;
		struct i915_vma *vma;
		unsigned int expected_gtt = 0;
		int i;

		obj = fake_huge_pages_object(i915, size, single);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			break;
		}

		if (obj->base.size != size) {
			pr_err("obj->base.size=%zd, expected=%llu\n",
			       obj->base.size, size);
			i915_gem_object_put(obj);
			err = -EINVAL;
			break;
		}

		err = i915_gem_object_pin_pages(obj);
		if (err) {
			i915_gem_object_put(obj);
			break;
		}

		list_add(&obj->st_link, &objects);

		vma = i915_vma_instance(obj, &ppgtt->base, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			break;
		}

		err = i915_vma_pin(vma, 0, 0, PIN_USER);
		if (err)
			break;

		err = igt_check_page_sizes(vma);
		if (err) {
			i915_vma_unpin(vma);
			break;
		}

		/*
		 * Figure out the expected gtt page size knowing that we go from
		 * largest to smallest page size sg chunks, and that we align to
		 * the largest page size.
		 */
		for (i = 0; i < ARRAY_SIZE(page_sizes); ++i) {
			unsigned int page_size = page_sizes[i];

			if (HAS_PAGE_SIZES(i915, page_size) &&
			    size >= page_size) {
				expected_gtt |= page_size;
				size &= page_size-1;
			}
		}

		GEM_BUG_ON(!expected_gtt);
		GEM_BUG_ON(size);

		if (expected_gtt & I915_GTT_PAGE_SIZE_4K)
			expected_gtt &= ~I915_GTT_PAGE_SIZE_64K;

		i915_vma_unpin(vma);

		if (vma->page_sizes.sg & I915_GTT_PAGE_SIZE_64K) {
			if (!IS_ALIGNED(vma->node.start,
					I915_GTT_PAGE_SIZE_2M)) {
				pr_err("node.start(%llx) not aligned to 2M\n",
				       vma->node.start);
				err = -EINVAL;
				break;
			}

			if (!IS_ALIGNED(vma->node.size,
					I915_GTT_PAGE_SIZE_2M)) {
				pr_err("node.size(%llx) not aligned to 2M\n",
				       vma->node.size);
				err = -EINVAL;
				break;
			}
		}

		if (vma->page_sizes.gtt != expected_gtt) {
			pr_err("gtt=%u, expected=%u, size=%zd, single=%s\n",
			       vma->page_sizes.gtt, expected_gtt,
			       obj->base.size, yesno(!!single));
			err = -EINVAL;
			break;
		}

		if (igt_timeout(end_time,
				"%s timed out at size %zd\n",
				__func__, obj->base.size))
			break;

		single = !single;
	}

	close_object_list(&objects, ppgtt);

	if (err == -ENOMEM || err == -ENOSPC)
		err = 0;

	return err;
}

static int igt_mock_ppgtt_64K(void *arg)
{
	struct i915_hw_ppgtt *ppgtt = arg;
	struct drm_i915_private *i915 = ppgtt->base.i915;
	struct drm_i915_gem_object *obj;
	const struct object_info {
		unsigned int size;
		unsigned int gtt;
		unsigned int offset;
	} objects[] = {
		/* Cases with forced padding/alignment */
		{
			.size = SZ_64K,
			.gtt = I915_GTT_PAGE_SIZE_64K,
			.offset = 0,
		},
		{
			.size = SZ_64K + SZ_4K,
			.gtt = I915_GTT_PAGE_SIZE_4K,
			.offset = 0,
		},
		{
			.size = SZ_64K - SZ_4K,
			.gtt = I915_GTT_PAGE_SIZE_4K,
			.offset = 0,
		},
		{
			.size = SZ_2M,
			.gtt = I915_GTT_PAGE_SIZE_64K,
			.offset = 0,
		},
		{
			.size = SZ_2M - SZ_4K,
			.gtt = I915_GTT_PAGE_SIZE_4K,
			.offset = 0,
		},
		{
			.size = SZ_2M + SZ_4K,
			.gtt = I915_GTT_PAGE_SIZE_64K | I915_GTT_PAGE_SIZE_4K,
			.offset = 0,
		},
		{
			.size = SZ_2M + SZ_64K,
			.gtt = I915_GTT_PAGE_SIZE_64K,
			.offset = 0,
		},
		{
			.size = SZ_2M - SZ_64K,
			.gtt = I915_GTT_PAGE_SIZE_64K,
			.offset = 0,
		},
		/* Try without any forced padding/alignment */
		{
			.size = SZ_64K,
			.offset = SZ_2M,
			.gtt = I915_GTT_PAGE_SIZE_4K,
		},
		{
			.size = SZ_128K,
			.offset = SZ_2M - SZ_64K,
			.gtt = I915_GTT_PAGE_SIZE_4K,
		},
	};
	struct i915_vma *vma;
	int i, single;
	int err;

	/*
	 * Sanity check some of the trickiness with 64K pages -- either we can
	 * safely mark the whole page-table(2M block) as 64K, or we have to
	 * always fallback to 4K.
	 */

	if (!HAS_PAGE_SIZES(i915, I915_GTT_PAGE_SIZE_64K))
		return 0;

	for (i = 0; i < ARRAY_SIZE(objects); ++i) {
		unsigned int size = objects[i].size;
		unsigned int expected_gtt = objects[i].gtt;
		unsigned int offset = objects[i].offset;
		unsigned int flags = PIN_USER;

		for (single = 0; single <= 1; single++) {
			obj = fake_huge_pages_object(i915, size, !!single);
			if (IS_ERR(obj))
				return PTR_ERR(obj);

			err = i915_gem_object_pin_pages(obj);
			if (err)
				goto out_object_put;

			/*
			 * Disable 2M pages -- We only want to use 64K/4K pages
			 * for this test.
			 */
			obj->mm.page_sizes.sg &= ~I915_GTT_PAGE_SIZE_2M;

			vma = i915_vma_instance(obj, &ppgtt->base, NULL);
			if (IS_ERR(vma)) {
				err = PTR_ERR(vma);
				goto out_object_unpin;
			}

			if (offset)
				flags |= PIN_OFFSET_FIXED | offset;

			err = i915_vma_pin(vma, 0, 0, flags);
			if (err)
				goto out_vma_close;

			err = igt_check_page_sizes(vma);
			if (err)
				goto out_vma_unpin;

			if (!offset && vma->page_sizes.sg & I915_GTT_PAGE_SIZE_64K) {
				if (!IS_ALIGNED(vma->node.start,
						I915_GTT_PAGE_SIZE_2M)) {
					pr_err("node.start(%llx) not aligned to 2M\n",
					       vma->node.start);
					err = -EINVAL;
					goto out_vma_unpin;
				}

				if (!IS_ALIGNED(vma->node.size,
						I915_GTT_PAGE_SIZE_2M)) {
					pr_err("node.size(%llx) not aligned to 2M\n",
					       vma->node.size);
					err = -EINVAL;
					goto out_vma_unpin;
				}
			}

			if (vma->page_sizes.gtt != expected_gtt) {
				pr_err("gtt=%u, expected=%u, i=%d, single=%s\n",
				       vma->page_sizes.gtt, expected_gtt, i,
				       yesno(!!single));
				err = -EINVAL;
				goto out_vma_unpin;
			}

			i915_vma_unpin(vma);
			i915_vma_close(vma);

			i915_gem_object_unpin_pages(obj);
			i915_gem_object_put(obj);
		}
	}

	return 0;

out_vma_unpin:
	i915_vma_unpin(vma);
out_vma_close:
	i915_vma_close(vma);
out_object_unpin:
	i915_gem_object_unpin_pages(obj);
out_object_put:
	i915_gem_object_put(obj);

	return err;
}

static struct i915_vma *
gpu_write_dw(struct i915_vma *vma, u64 offset, u32 val)
{
	struct drm_i915_private *i915 = to_i915(vma->obj->base.dev);
	const int gen = INTEL_GEN(vma->vm->i915);
	unsigned int count = vma->size >> PAGE_SHIFT;
	struct drm_i915_gem_object *obj;
	struct i915_vma *batch;
	unsigned int size;
	u32 *cmd;
	int n;
	int err;

	size = (1 + 4 * count) * sizeof(u32);
	size = round_up(size, PAGE_SIZE);
	obj = i915_gem_object_create_internal(i915, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	cmd = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto err;
	}

	offset += vma->node.start;

	for (n = 0; n < count; n++) {
		if (gen >= 8) {
			*cmd++ = MI_STORE_DWORD_IMM_GEN4;
			*cmd++ = lower_32_bits(offset);
			*cmd++ = upper_32_bits(offset);
			*cmd++ = val;
		} else if (gen >= 4) {
			*cmd++ = MI_STORE_DWORD_IMM_GEN4 |
				(gen < 6 ? 1 << 22 : 0);
			*cmd++ = 0;
			*cmd++ = offset;
			*cmd++ = val;
		} else {
			*cmd++ = MI_STORE_DWORD_IMM | 1 << 22;
			*cmd++ = offset;
			*cmd++ = val;
		}

		offset += PAGE_SIZE;
	}

	*cmd = MI_BATCH_BUFFER_END;

	i915_gem_object_unpin_map(obj);

	err = i915_gem_object_set_to_gtt_domain(obj, false);
	if (err)
		goto err;

	batch = i915_vma_instance(obj, vma->vm, NULL);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto err;
	}

	err = i915_vma_pin(batch, 0, 0, PIN_USER);
	if (err)
		goto err;

	return batch;

err:
	i915_gem_object_put(obj);

	return ERR_PTR(err);
}

static int gpu_write(struct i915_vma *vma,
		     struct i915_gem_context *ctx,
		     struct intel_engine_cs *engine,
		     u32 dword,
		     u32 value)
{
	struct drm_i915_gem_request *rq;
	struct i915_vma *batch;
	int flags = 0;
	int err;

	GEM_BUG_ON(!intel_engine_can_store_dword(engine));

	err = i915_gem_object_set_to_gtt_domain(vma->obj, true);
	if (err)
		return err;

	rq = i915_gem_request_alloc(engine, ctx);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	batch = gpu_write_dw(vma, dword * sizeof(u32), value);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto err_request;
	}

	i915_vma_move_to_active(batch, rq, 0);
	i915_gem_object_set_active_reference(batch->obj);
	i915_vma_unpin(batch);
	i915_vma_close(batch);

	err = engine->emit_bb_start(rq,
				    batch->node.start, batch->node.size,
				    flags);
	if (err)
		goto err_request;

	i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);

	reservation_object_lock(vma->resv, NULL);
	reservation_object_add_excl_fence(vma->resv, &rq->fence);
	reservation_object_unlock(vma->resv);

err_request:
	__i915_add_request(rq, err == 0);

	return err;
}

static int cpu_check(struct drm_i915_gem_object *obj, u32 dword, u32 val)
{
	unsigned int needs_flush;
	unsigned long n;
	int err;

	err = i915_gem_obj_prepare_shmem_read(obj, &needs_flush);
	if (err)
		return err;

	for (n = 0; n < obj->base.size >> PAGE_SHIFT; ++n) {
		u32 *ptr = kmap_atomic(i915_gem_object_get_page(obj, n));

		if (needs_flush & CLFLUSH_BEFORE)
			drm_clflush_virt_range(ptr, PAGE_SIZE);

		if (ptr[dword] != val) {
			pr_err("n=%lu ptr[%u]=%u, val=%u\n",
			       n, dword, ptr[dword], val);
			kunmap_atomic(ptr);
			err = -EINVAL;
			break;
		}

		kunmap_atomic(ptr);
	}

	i915_gem_obj_finish_shmem_access(obj);

	return err;
}

static int igt_write_huge(struct i915_gem_context *ctx,
			  struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_address_space *vm = ctx->ppgtt ? &ctx->ppgtt->base : &i915->ggtt.base;
	static struct intel_engine_cs *engines[I915_NUM_ENGINES];
	struct intel_engine_cs *engine;
	I915_RND_STATE(prng);
	IGT_TIMEOUT(end_time);
	struct i915_vma *vma;
	unsigned int flags = PIN_USER | PIN_OFFSET_FIXED;
	unsigned int max_page_size;
	unsigned int id;
	u64 max;
	u64 num;
	u64 size;
	int *order;
	int i, n;
	int err = 0;

	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));

	size = obj->base.size;
	if (obj->mm.page_sizes.sg & I915_GTT_PAGE_SIZE_64K)
		size = round_up(size, I915_GTT_PAGE_SIZE_2M);

	max_page_size = rounddown_pow_of_two(obj->mm.page_sizes.sg);
	max = div_u64((vm->total - size), max_page_size);

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	n = 0;
	for_each_engine(engine, i915, id) {
		if (!intel_engine_can_store_dword(engine)) {
			pr_info("store-dword-imm not supported on engine=%u\n", id);
			continue;
		}
		engines[n++] = engine;
	}

	if (!n)
		return 0;

	/*
	 * To keep things interesting when alternating between engines in our
	 * randomized order, lets also make feeding to the same engine a few
	 * times in succession a possibility by enlarging the permutation array.
	 */
	order = i915_random_order(n * I915_NUM_ENGINES, &prng);
	if (!order)
		return -ENOMEM;

	/*
	 * Try various offsets until we timeout -- we want to avoid
	 * issues hidden by effectively always using offset = 0.
	 */
	i = 0;
	for_each_prime_number_from(num, 0, max) {
		u64 offset = num * max_page_size;
		u32 dword;

		err = i915_vma_unbind(vma);
		if (err)
			goto out_vma_close;

		err = i915_vma_pin(vma, size, max_page_size, flags | offset);
		if (err) {
			/*
			 * The ggtt may have some pages reserved so
			 * refrain from erroring out.
			 */
			if (err == -ENOSPC && i915_is_ggtt(vm)) {
				err = 0;
				continue;
			}

			goto out_vma_close;
		}

		err = igt_check_page_sizes(vma);
		if (err)
			goto out_vma_unpin;

		dword = offset_in_page(num) / 4;

		engine = engines[order[i] % n];
		i = (i + 1) % (n * I915_NUM_ENGINES);

		err = gpu_write(vma, ctx, engine, dword, num + 1);
		if (err) {
			pr_err("gpu-write failed at offset=%llx", offset);
			goto out_vma_unpin;
		}

		err = cpu_check(obj, dword, num + 1);
		if (err) {
			pr_err("cpu-check failed at offset=%llx", offset);
			goto out_vma_unpin;
		}

		i915_vma_unpin(vma);

		if (igt_timeout(end_time,
				"%s timed out on engine=%u at offset=%llx, max_page_size=%x\n",
				__func__, engine->id, offset, max_page_size))
			break;
	}

out_vma_unpin:
	if (i915_vma_is_pinned(vma))
		i915_vma_unpin(vma);
out_vma_close:
	i915_vma_close(vma);
	kfree(order);

	return err;
}

static int igt_ppgtt_exhaust_huge(void *arg)
{
	struct i915_gem_context *ctx = arg;
	struct drm_i915_private *i915 = ctx->i915;
	unsigned long supported = INTEL_INFO(i915)->page_sizes;
	static unsigned int pages[ARRAY_SIZE(page_sizes)];
	struct drm_i915_gem_object *obj;
	unsigned int size_mask;
	unsigned int page_mask;
	int n, i;
	int err = -ENODEV;

	if (supported == I915_GTT_PAGE_SIZE_4K)
		return 0;

	/*
	 * Sanity check creating objects with a varying mix of page sizes --
	 * ensuring that our writes lands in the right place.
	 */

	n = 0;
	for_each_set_bit(i, &supported, ilog2(I915_GTT_MAX_PAGE_SIZE) + 1)
		pages[n++] = BIT(i);

	for (size_mask = 2; size_mask < BIT(n); size_mask++) {
		unsigned int size = 0;

		for (i = 0; i < n; i++) {
			if (size_mask & BIT(i))
				size |= pages[i];
		}

		/*
		 * For our page mask we want to enumerate all the page-size
		 * combinations which will fit into our chosen object size.
		 */
		for (page_mask = 2; page_mask <= size_mask; page_mask++) {
			unsigned int page_sizes = 0;

			for (i = 0; i < n; i++) {
				if (page_mask & BIT(i))
					page_sizes |= pages[i];
			}

			/*
			 * Ensure that we can actually fill the given object
			 * with our chosen page mask.
			 */
			if (!IS_ALIGNED(size, BIT(__ffs(page_sizes))))
				continue;

			obj = huge_pages_object(i915, size, page_sizes);
			if (IS_ERR(obj)) {
				err = PTR_ERR(obj);
				goto out_device;
			}

			err = i915_gem_object_pin_pages(obj);
			if (err) {
				i915_gem_object_put(obj);

				if (err == -ENOMEM) {
					pr_info("unable to get pages, size=%u, pages=%u\n",
						size, page_sizes);
					err = 0;
					break;
				}

				pr_err("pin_pages failed, size=%u, pages=%u\n",
				       size_mask, page_mask);

				goto out_device;
			}

			/* Force the page-size for the gtt insertion */
			obj->mm.page_sizes.sg = page_sizes;

			err = igt_write_huge(ctx, obj);
			if (err) {
				pr_err("exhaust write-huge failed with size=%u\n",
				       size);
				goto out_unpin;
			}

			i915_gem_object_unpin_pages(obj);
			i915_gem_object_put(obj);
		}
	}

	goto out_device;

out_unpin:
	i915_gem_object_unpin_pages(obj);
	i915_gem_object_put(obj);
out_device:
	mkwrite_device_info(i915)->page_sizes = supported;

	return err;
}

static int igt_ppgtt_internal_huge(void *arg)
{
	struct i915_gem_context *ctx = arg;
	struct drm_i915_private *i915 = ctx->i915;
	struct drm_i915_gem_object *obj;
	static const unsigned int sizes[] = {
		SZ_64K,
		SZ_128K,
		SZ_256K,
		SZ_512K,
		SZ_1M,
		SZ_2M,
	};
	int i;
	int err;

	/*
	 * Sanity check that the HW uses huge pages correctly through internal
	 * -- ensure that our writes land in the right place.
	 */

	for (i = 0; i < ARRAY_SIZE(sizes); ++i) {
		unsigned int size = sizes[i];

		obj = i915_gem_object_create_internal(i915, size);
		if (IS_ERR(obj))
			return PTR_ERR(obj);

		err = i915_gem_object_pin_pages(obj);
		if (err)
			goto out_put;

		if (obj->mm.page_sizes.phys < I915_GTT_PAGE_SIZE_64K) {
			pr_info("internal unable to allocate huge-page(s) with size=%u\n",
				size);
			goto out_unpin;
		}

		err = igt_write_huge(ctx, obj);
		if (err) {
			pr_err("internal write-huge failed with size=%u\n",
			       size);
			goto out_unpin;
		}

		i915_gem_object_unpin_pages(obj);
		i915_gem_object_put(obj);
	}

	return 0;

out_unpin:
	i915_gem_object_unpin_pages(obj);
out_put:
	i915_gem_object_put(obj);

	return err;
}

static inline bool igt_can_allocate_thp(struct drm_i915_private *i915)
{
	return i915->mm.gemfs && has_transparent_hugepage();
}

static int igt_ppgtt_gemfs_huge(void *arg)
{
	struct i915_gem_context *ctx = arg;
	struct drm_i915_private *i915 = ctx->i915;
	struct drm_i915_gem_object *obj;
	static const unsigned int sizes[] = {
		SZ_2M,
		SZ_4M,
		SZ_8M,
		SZ_16M,
		SZ_32M,
	};
	int i;
	int err;

	/*
	 * Sanity check that the HW uses huge pages correctly through gemfs --
	 * ensure that our writes land in the right place.
	 */

	if (!igt_can_allocate_thp(i915)) {
		pr_info("missing THP support, skipping\n");
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(sizes); ++i) {
		unsigned int size = sizes[i];

		obj = i915_gem_object_create(i915, size);
		if (IS_ERR(obj))
			return PTR_ERR(obj);

		err = i915_gem_object_pin_pages(obj);
		if (err)
			goto out_put;

		if (obj->mm.page_sizes.phys < I915_GTT_PAGE_SIZE_2M) {
			pr_info("finishing test early, gemfs unable to allocate huge-page(s) with size=%u\n",
				size);
			goto out_unpin;
		}

		err = igt_write_huge(ctx, obj);
		if (err) {
			pr_err("gemfs write-huge failed with size=%u\n",
			       size);
			goto out_unpin;
		}

		i915_gem_object_unpin_pages(obj);
		i915_gem_object_put(obj);
	}

	return 0;

out_unpin:
	i915_gem_object_unpin_pages(obj);
out_put:
	i915_gem_object_put(obj);

	return err;
}

static int igt_ppgtt_pin_update(void *arg)
{
	struct i915_gem_context *ctx = arg;
	struct drm_i915_private *dev_priv = ctx->i915;
	unsigned long supported = INTEL_INFO(dev_priv)->page_sizes;
	struct i915_hw_ppgtt *ppgtt = ctx->ppgtt;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	unsigned int flags = PIN_USER | PIN_OFFSET_FIXED;
	int first, last;
	int err;

	/*
	 * Make sure there's no funny business when doing a PIN_UPDATE -- in the
	 * past we had a subtle issue with being able to incorrectly do multiple
	 * alloc va ranges on the same object when doing a PIN_UPDATE, which
	 * resulted in some pretty nasty bugs, though only when using
	 * huge-gtt-pages.
	 */

	if (!USES_FULL_48BIT_PPGTT(dev_priv)) {
		pr_info("48b PPGTT not supported, skipping\n");
		return 0;
	}

	first = ilog2(I915_GTT_PAGE_SIZE_64K);
	last = ilog2(I915_GTT_PAGE_SIZE_2M);

	for_each_set_bit_from(first, &supported, last + 1) {
		unsigned int page_size = BIT(first);

		obj = i915_gem_object_create_internal(dev_priv, page_size);
		if (IS_ERR(obj))
			return PTR_ERR(obj);

		vma = i915_vma_instance(obj, &ppgtt->base, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto out_put;
		}

		err = i915_vma_pin(vma, SZ_2M, 0, flags);
		if (err)
			goto out_close;

		if (vma->page_sizes.sg < page_size) {
			pr_info("Unable to allocate page-size %x, finishing test early\n",
				page_size);
			goto out_unpin;
		}

		err = igt_check_page_sizes(vma);
		if (err)
			goto out_unpin;

		if (vma->page_sizes.gtt != page_size) {
			dma_addr_t addr = i915_gem_object_get_dma_address(obj, 0);

			/*
			 * The only valid reason for this to ever fail would be
			 * if the dma-mapper screwed us over when we did the
			 * dma_map_sg(), since it has the final say over the dma
			 * address.
			 */
			if (IS_ALIGNED(addr, page_size)) {
				pr_err("page_sizes.gtt=%u, expected=%u\n",
				       vma->page_sizes.gtt, page_size);
				err = -EINVAL;
			} else {
				pr_info("dma address misaligned, finishing test early\n");
			}

			goto out_unpin;
		}

		err = i915_vma_bind(vma, I915_CACHE_NONE, PIN_UPDATE);
		if (err)
			goto out_unpin;

		i915_vma_unpin(vma);
		i915_vma_close(vma);

		i915_gem_object_put(obj);
	}

	obj = i915_gem_object_create_internal(dev_priv, PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	vma = i915_vma_instance(obj, &ppgtt->base, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto out_put;
	}

	err = i915_vma_pin(vma, 0, 0, flags);
	if (err)
		goto out_close;

	/*
	 * Make sure we don't end up with something like where the pde is still
	 * pointing to the 2M page, and the pt we just filled-in is dangling --
	 * we can check this by writing to the first page where it would then
	 * land in the now stale 2M page.
	 */

	err = gpu_write(vma, ctx, dev_priv->engine[RCS], 0, 0xdeadbeaf);
	if (err)
		goto out_unpin;

	err = cpu_check(obj, 0, 0xdeadbeaf);

out_unpin:
	i915_vma_unpin(vma);
out_close:
	i915_vma_close(vma);
out_put:
	i915_gem_object_put(obj);

	return err;
}

static int igt_tmpfs_fallback(void *arg)
{
	struct i915_gem_context *ctx = arg;
	struct drm_i915_private *i915 = ctx->i915;
	struct vfsmount *gemfs = i915->mm.gemfs;
	struct i915_address_space *vm = ctx->ppgtt ? &ctx->ppgtt->base : &i915->ggtt.base;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	u32 *vaddr;
	int err = 0;

	/*
	 * Make sure that we don't burst into a ball of flames upon falling back
	 * to tmpfs, which we rely on if on the off-chance we encouter a failure
	 * when setting up gemfs.
	 */

	i915->mm.gemfs = NULL;

	obj = i915_gem_object_create(i915, PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto out_restore;
	}

	vaddr = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		goto out_put;
	}
	*vaddr = 0xdeadbeaf;

	i915_gem_object_unpin_map(obj);

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto out_put;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		goto out_close;

	err = igt_check_page_sizes(vma);

	i915_vma_unpin(vma);
out_close:
	i915_vma_close(vma);
out_put:
	i915_gem_object_put(obj);
out_restore:
	i915->mm.gemfs = gemfs;

	return err;
}

static int igt_shrink_thp(void *arg)
{
	struct i915_gem_context *ctx = arg;
	struct drm_i915_private *i915 = ctx->i915;
	struct i915_address_space *vm = ctx->ppgtt ? &ctx->ppgtt->base : &i915->ggtt.base;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	unsigned int flags = PIN_USER;
	int err;

	/*
	 * Sanity check shrinking huge-paged object -- make sure nothing blows
	 * up.
	 */

	if (!igt_can_allocate_thp(i915)) {
		pr_info("missing THP support, skipping\n");
		return 0;
	}

	obj = i915_gem_object_create(i915, SZ_2M);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto out_put;
	}

	err = i915_vma_pin(vma, 0, 0, flags);
	if (err)
		goto out_close;

	if (obj->mm.page_sizes.phys < I915_GTT_PAGE_SIZE_2M) {
		pr_info("failed to allocate THP, finishing test early\n");
		goto out_unpin;
	}

	err = igt_check_page_sizes(vma);
	if (err)
		goto out_unpin;

	err = gpu_write(vma, ctx, i915->engine[RCS], 0, 0xdeadbeaf);
	if (err)
		goto out_unpin;

	i915_vma_unpin(vma);

	/*
	 * Now that the pages are *unpinned* shrink-all should invoke
	 * shmem to truncate our pages.
	 */
	i915_gem_shrink_all(i915);
	if (!IS_ERR_OR_NULL(obj->mm.pages)) {
		pr_err("shrink-all didn't truncate the pages\n");
		err = -EINVAL;
		goto out_close;
	}

	if (obj->mm.page_sizes.sg || obj->mm.page_sizes.phys) {
		pr_err("residual page-size bits left\n");
		err = -EINVAL;
		goto out_close;
	}

	err = i915_vma_pin(vma, 0, 0, flags);
	if (err)
		goto out_close;

	err = cpu_check(obj, 0, 0xdeadbeaf);

out_unpin:
	i915_vma_unpin(vma);
out_close:
	i915_vma_close(vma);
out_put:
	i915_gem_object_put(obj);

	return err;
}

int i915_gem_huge_page_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_mock_exhaust_device_supported_pages),
		SUBTEST(igt_mock_ppgtt_misaligned_dma),
		SUBTEST(igt_mock_ppgtt_huge_fill),
		SUBTEST(igt_mock_ppgtt_64K),
	};
	int saved_ppgtt = i915_modparams.enable_ppgtt;
	struct drm_i915_private *dev_priv;
	struct pci_dev *pdev;
	struct i915_hw_ppgtt *ppgtt;
	int err;

	dev_priv = mock_gem_device();
	if (!dev_priv)
		return -ENOMEM;

	/* Pretend to be a device which supports the 48b PPGTT */
	i915_modparams.enable_ppgtt = 3;

	pdev = dev_priv->drm.pdev;
	dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(39));

	mutex_lock(&dev_priv->drm.struct_mutex);
	ppgtt = i915_ppgtt_create(dev_priv, ERR_PTR(-ENODEV), "mock");
	if (IS_ERR(ppgtt)) {
		err = PTR_ERR(ppgtt);
		goto out_unlock;
	}

	if (!i915_vm_is_48bit(&ppgtt->base)) {
		pr_err("failed to create 48b PPGTT\n");
		err = -EINVAL;
		goto out_close;
	}

	/* If we were ever hit this then it's time to mock the 64K scratch */
	if (!i915_vm_has_scratch_64K(&ppgtt->base)) {
		pr_err("PPGTT missing 64K scratch page\n");
		err = -EINVAL;
		goto out_close;
	}

	err = i915_subtests(tests, ppgtt);

out_close:
	i915_ppgtt_close(&ppgtt->base);
	i915_ppgtt_put(ppgtt);

out_unlock:
	mutex_unlock(&dev_priv->drm.struct_mutex);

	i915_modparams.enable_ppgtt = saved_ppgtt;

	drm_dev_unref(&dev_priv->drm);

	return err;
}

int i915_gem_huge_page_live_selftests(struct drm_i915_private *dev_priv)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_shrink_thp),
		SUBTEST(igt_ppgtt_pin_update),
		SUBTEST(igt_tmpfs_fallback),
		SUBTEST(igt_ppgtt_exhaust_huge),
		SUBTEST(igt_ppgtt_gemfs_huge),
		SUBTEST(igt_ppgtt_internal_huge),
	};
	struct drm_file *file;
	struct i915_gem_context *ctx;
	int err;

	if (!USES_PPGTT(dev_priv)) {
		pr_info("PPGTT not supported, skipping live-selftests\n");
		return 0;
	}

	file = mock_file(dev_priv);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&dev_priv->drm.struct_mutex);

	ctx = live_context(dev_priv, file);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto out_unlock;
	}

	err = i915_subtests(tests, ctx);

out_unlock:
	mutex_unlock(&dev_priv->drm.struct_mutex);

	mock_file_free(dev_priv, file);

	return err;
}
