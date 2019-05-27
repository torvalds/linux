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

#include "../i915_selftest.h"

#include "igt_flush_test.h"
#include "mock_gem_device.h"
#include "huge_gem_object.h"

static int igt_gem_object(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	int err = -ENOMEM;

	/* Basic test to ensure we can create an object */

	obj = i915_gem_object_create(i915, PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		pr_err("i915_gem_object_create failed, err=%d\n", err);
		goto out;
	}

	err = 0;
	i915_gem_object_put(obj);
out:
	return err;
}

static int igt_phys_object(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	int err;

	/* Create an object and bind it to a contiguous set of physical pages,
	 * i.e. exercise the i915_gem_object_phys API.
	 */

	obj = i915_gem_object_create(i915, PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		pr_err("i915_gem_object_create failed, err=%d\n", err);
		goto out;
	}

	mutex_lock(&i915->drm.struct_mutex);
	err = i915_gem_object_attach_phys(obj, PAGE_SIZE);
	mutex_unlock(&i915->drm.struct_mutex);
	if (err) {
		pr_err("i915_gem_object_attach_phys failed, err=%d\n", err);
		goto out_obj;
	}

	if (obj->ops != &i915_gem_phys_ops) {
		pr_err("i915_gem_object_attach_phys did not create a phys object\n");
		err = -EINVAL;
		goto out_obj;
	}

	if (!atomic_read(&obj->mm.pages_pin_count)) {
		pr_err("i915_gem_object_attach_phys did not pin its phys pages\n");
		err = -EINVAL;
		goto out_obj;
	}

	/* Make the object dirty so that put_pages must do copy back the data */
	mutex_lock(&i915->drm.struct_mutex);
	err = i915_gem_object_set_to_gtt_domain(obj, true);
	mutex_unlock(&i915->drm.struct_mutex);
	if (err) {
		pr_err("i915_gem_object_set_to_gtt_domain failed with err=%d\n",
		       err);
		goto out_obj;
	}

out_obj:
	i915_gem_object_put(obj);
out:
	return err;
}

static int igt_gem_huge(void *arg)
{
	const unsigned int nreal = 509; /* just to be awkward */
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	unsigned int n;
	int err;

	/* Basic sanitycheck of our huge fake object allocation */

	obj = huge_gem_object(i915,
			      nreal * PAGE_SIZE,
			      i915->ggtt.vm.total + PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	err = i915_gem_object_pin_pages(obj);
	if (err) {
		pr_err("Failed to allocate %u pages (%lu total), err=%d\n",
		       nreal, obj->base.size / PAGE_SIZE, err);
		goto out;
	}

	for (n = 0; n < obj->base.size / PAGE_SIZE; n++) {
		if (i915_gem_object_get_page(obj, n) !=
		    i915_gem_object_get_page(obj, n % nreal)) {
			pr_err("Page lookup mismatch at index %u [%u]\n",
			       n, n % nreal);
			err = -EINVAL;
			goto out_unpin;
		}
	}

out_unpin:
	i915_gem_object_unpin_pages(obj);
out:
	i915_gem_object_put(obj);
	return err;
}

struct tile {
	unsigned int width;
	unsigned int height;
	unsigned int stride;
	unsigned int size;
	unsigned int tiling;
	unsigned int swizzle;
};

static u64 swizzle_bit(unsigned int bit, u64 offset)
{
	return (offset & BIT_ULL(bit)) >> (bit - 6);
}

static u64 tiled_offset(const struct tile *tile, u64 v)
{
	u64 x, y;

	if (tile->tiling == I915_TILING_NONE)
		return v;

	y = div64_u64_rem(v, tile->stride, &x);
	v = div64_u64_rem(y, tile->height, &y) * tile->stride * tile->height;

	if (tile->tiling == I915_TILING_X) {
		v += y * tile->width;
		v += div64_u64_rem(x, tile->width, &x) << tile->size;
		v += x;
	} else if (tile->width == 128) {
		const unsigned int ytile_span = 16;
		const unsigned int ytile_height = 512;

		v += y * ytile_span;
		v += div64_u64_rem(x, ytile_span, &x) * ytile_height;
		v += x;
	} else {
		const unsigned int ytile_span = 32;
		const unsigned int ytile_height = 256;

		v += y * ytile_span;
		v += div64_u64_rem(x, ytile_span, &x) * ytile_height;
		v += x;
	}

	switch (tile->swizzle) {
	case I915_BIT_6_SWIZZLE_9:
		v ^= swizzle_bit(9, v);
		break;
	case I915_BIT_6_SWIZZLE_9_10:
		v ^= swizzle_bit(9, v) ^ swizzle_bit(10, v);
		break;
	case I915_BIT_6_SWIZZLE_9_11:
		v ^= swizzle_bit(9, v) ^ swizzle_bit(11, v);
		break;
	case I915_BIT_6_SWIZZLE_9_10_11:
		v ^= swizzle_bit(9, v) ^ swizzle_bit(10, v) ^ swizzle_bit(11, v);
		break;
	}

	return v;
}

static int check_partial_mapping(struct drm_i915_gem_object *obj,
				 const struct tile *tile,
				 unsigned long end_time)
{
	const unsigned int nreal = obj->scratch / PAGE_SIZE;
	const unsigned long npages = obj->base.size / PAGE_SIZE;
	struct i915_vma *vma;
	unsigned long page;
	int err;

	if (igt_timeout(end_time,
			"%s: timed out before tiling=%d stride=%d\n",
			__func__, tile->tiling, tile->stride))
		return -EINTR;

	err = i915_gem_object_set_tiling(obj, tile->tiling, tile->stride);
	if (err) {
		pr_err("Failed to set tiling mode=%u, stride=%u, err=%d\n",
		       tile->tiling, tile->stride, err);
		return err;
	}

	GEM_BUG_ON(i915_gem_object_get_tiling(obj) != tile->tiling);
	GEM_BUG_ON(i915_gem_object_get_stride(obj) != tile->stride);

	for_each_prime_number_from(page, 1, npages) {
		struct i915_ggtt_view view =
			compute_partial_view(obj, page, MIN_CHUNK_PAGES);
		u32 __iomem *io;
		struct page *p;
		unsigned int n;
		u64 offset;
		u32 *cpu;

		GEM_BUG_ON(view.partial.size > nreal);
		cond_resched();

		err = i915_gem_object_set_to_gtt_domain(obj, true);
		if (err) {
			pr_err("Failed to flush to GTT write domain; err=%d\n",
			       err);
			return err;
		}

		vma = i915_gem_object_ggtt_pin(obj, &view, 0, 0, PIN_MAPPABLE);
		if (IS_ERR(vma)) {
			pr_err("Failed to pin partial view: offset=%lu; err=%d\n",
			       page, (int)PTR_ERR(vma));
			return PTR_ERR(vma);
		}

		n = page - view.partial.offset;
		GEM_BUG_ON(n >= view.partial.size);

		io = i915_vma_pin_iomap(vma);
		i915_vma_unpin(vma);
		if (IS_ERR(io)) {
			pr_err("Failed to iomap partial view: offset=%lu; err=%d\n",
			       page, (int)PTR_ERR(io));
			return PTR_ERR(io);
		}

		iowrite32(page, io + n * PAGE_SIZE/sizeof(*io));
		i915_vma_unpin_iomap(vma);

		offset = tiled_offset(tile, page << PAGE_SHIFT);
		if (offset >= obj->base.size)
			continue;

		flush_write_domain(obj, ~I915_GEM_DOMAIN_CPU);

		p = i915_gem_object_get_page(obj, offset >> PAGE_SHIFT);
		cpu = kmap(p) + offset_in_page(offset);
		drm_clflush_virt_range(cpu, sizeof(*cpu));
		if (*cpu != (u32)page) {
			pr_err("Partial view for %lu [%u] (offset=%llu, size=%u [%llu, row size %u], fence=%d, tiling=%d, stride=%d) misalignment, expected write to page (%llu + %u [0x%llx]) of 0x%x, found 0x%x\n",
			       page, n,
			       view.partial.offset,
			       view.partial.size,
			       vma->size >> PAGE_SHIFT,
			       tile->tiling ? tile_row_pages(obj) : 0,
			       vma->fence ? vma->fence->id : -1, tile->tiling, tile->stride,
			       offset >> PAGE_SHIFT,
			       (unsigned int)offset_in_page(offset),
			       offset,
			       (u32)page, *cpu);
			err = -EINVAL;
		}
		*cpu = 0;
		drm_clflush_virt_range(cpu, sizeof(*cpu));
		kunmap(p);
		if (err)
			return err;

		i915_vma_destroy(vma);
	}

	return 0;
}

static int igt_partial_tiling(void *arg)
{
	const unsigned int nreal = 1 << 12; /* largest tile row x2 */
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	intel_wakeref_t wakeref;
	int tiling;
	int err;

	/* We want to check the page mapping and fencing of a large object
	 * mmapped through the GTT. The object we create is larger than can
	 * possibly be mmaped as a whole, and so we must use partial GGTT vma.
	 * We then check that a write through each partial GGTT vma ends up
	 * in the right set of pages within the object, and with the expected
	 * tiling, which we verify by manual swizzling.
	 */

	obj = huge_gem_object(i915,
			      nreal << PAGE_SHIFT,
			      (1 + next_prime_number(i915->ggtt.vm.total >> PAGE_SHIFT)) << PAGE_SHIFT);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	err = i915_gem_object_pin_pages(obj);
	if (err) {
		pr_err("Failed to allocate %u pages (%lu total), err=%d\n",
		       nreal, obj->base.size / PAGE_SIZE, err);
		goto out;
	}

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(i915);

	if (1) {
		IGT_TIMEOUT(end);
		struct tile tile;

		tile.height = 1;
		tile.width = 1;
		tile.size = 0;
		tile.stride = 0;
		tile.swizzle = I915_BIT_6_SWIZZLE_NONE;
		tile.tiling = I915_TILING_NONE;

		err = check_partial_mapping(obj, &tile, end);
		if (err && err != -EINTR)
			goto out_unlock;
	}

	for (tiling = I915_TILING_X; tiling <= I915_TILING_Y; tiling++) {
		IGT_TIMEOUT(end);
		unsigned int max_pitch;
		unsigned int pitch;
		struct tile tile;

		if (i915->quirks & QUIRK_PIN_SWIZZLED_PAGES)
			/*
			 * The swizzling pattern is actually unknown as it
			 * varies based on physical address of each page.
			 * See i915_gem_detect_bit_6_swizzle().
			 */
			break;

		tile.tiling = tiling;
		switch (tiling) {
		case I915_TILING_X:
			tile.swizzle = i915->mm.bit_6_swizzle_x;
			break;
		case I915_TILING_Y:
			tile.swizzle = i915->mm.bit_6_swizzle_y;
			break;
		}

		GEM_BUG_ON(tile.swizzle == I915_BIT_6_SWIZZLE_UNKNOWN);
		if (tile.swizzle == I915_BIT_6_SWIZZLE_9_17 ||
		    tile.swizzle == I915_BIT_6_SWIZZLE_9_10_17)
			continue;

		if (INTEL_GEN(i915) <= 2) {
			tile.height = 16;
			tile.width = 128;
			tile.size = 11;
		} else if (tile.tiling == I915_TILING_Y &&
			   HAS_128_BYTE_Y_TILING(i915)) {
			tile.height = 32;
			tile.width = 128;
			tile.size = 12;
		} else {
			tile.height = 8;
			tile.width = 512;
			tile.size = 12;
		}

		if (INTEL_GEN(i915) < 4)
			max_pitch = 8192 / tile.width;
		else if (INTEL_GEN(i915) < 7)
			max_pitch = 128 * I965_FENCE_MAX_PITCH_VAL / tile.width;
		else
			max_pitch = 128 * GEN7_FENCE_MAX_PITCH_VAL / tile.width;

		for (pitch = max_pitch; pitch; pitch >>= 1) {
			tile.stride = tile.width * pitch;
			err = check_partial_mapping(obj, &tile, end);
			if (err == -EINTR)
				goto next_tiling;
			if (err)
				goto out_unlock;

			if (pitch > 2 && INTEL_GEN(i915) >= 4) {
				tile.stride = tile.width * (pitch - 1);
				err = check_partial_mapping(obj, &tile, end);
				if (err == -EINTR)
					goto next_tiling;
				if (err)
					goto out_unlock;
			}

			if (pitch < max_pitch && INTEL_GEN(i915) >= 4) {
				tile.stride = tile.width * (pitch + 1);
				err = check_partial_mapping(obj, &tile, end);
				if (err == -EINTR)
					goto next_tiling;
				if (err)
					goto out_unlock;
			}
		}

		if (INTEL_GEN(i915) >= 4) {
			for_each_prime_number(pitch, max_pitch) {
				tile.stride = tile.width * pitch;
				err = check_partial_mapping(obj, &tile, end);
				if (err == -EINTR)
					goto next_tiling;
				if (err)
					goto out_unlock;
			}
		}

next_tiling: ;
	}

out_unlock:
	intel_runtime_pm_put(i915, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	i915_gem_object_unpin_pages(obj);
out:
	i915_gem_object_put(obj);
	return err;
}

static int make_obj_busy(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_request *rq;
	struct i915_vma *vma;
	int err;

	vma = i915_vma_instance(obj, &i915->ggtt.vm, NULL);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		return err;

	rq = i915_request_create(i915->engine[RCS0]->kernel_context);
	if (IS_ERR(rq)) {
		i915_vma_unpin(vma);
		return PTR_ERR(rq);
	}

	err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);

	i915_request_add(rq);

	__i915_gem_object_release_unless_active(obj);
	i915_vma_unpin(vma);

	return err;
}

static bool assert_mmap_offset(struct drm_i915_private *i915,
			       unsigned long size,
			       int expected)
{
	struct drm_i915_gem_object *obj;
	int err;

	obj = i915_gem_object_create_internal(i915, size);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	err = i915_gem_object_create_mmap_offset(obj);
	i915_gem_object_put(obj);

	return err == expected;
}

static void disable_retire_worker(struct drm_i915_private *i915)
{
	i915_gem_shrinker_unregister(i915);

	intel_gt_pm_get(i915);

	cancel_delayed_work_sync(&i915->gem.retire_work);
	flush_work(&i915->gem.idle_work);
}

static void restore_retire_worker(struct drm_i915_private *i915)
{
	intel_gt_pm_put(i915);

	mutex_lock(&i915->drm.struct_mutex);
	igt_flush_test(i915, I915_WAIT_LOCKED);
	mutex_unlock(&i915->drm.struct_mutex);

	i915_gem_shrinker_register(i915);
}

static int igt_mmap_offset_exhaustion(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_mm *mm = &i915->drm.vma_offset_manager->vm_addr_space_mm;
	struct drm_i915_gem_object *obj;
	struct drm_mm_node resv, *hole;
	u64 hole_start, hole_end;
	int loop, err;

	/* Disable background reaper */
	disable_retire_worker(i915);
	GEM_BUG_ON(!i915->gt.awake);

	/* Trim the device mmap space to only a page */
	memset(&resv, 0, sizeof(resv));
	drm_mm_for_each_hole(hole, mm, hole_start, hole_end) {
		resv.start = hole_start;
		resv.size = hole_end - hole_start - 1; /* PAGE_SIZE units */
		err = drm_mm_reserve_node(mm, &resv);
		if (err) {
			pr_err("Failed to trim VMA manager, err=%d\n", err);
			goto out_park;
		}
		break;
	}

	/* Just fits! */
	if (!assert_mmap_offset(i915, PAGE_SIZE, 0)) {
		pr_err("Unable to insert object into single page hole\n");
		err = -EINVAL;
		goto out;
	}

	/* Too large */
	if (!assert_mmap_offset(i915, 2*PAGE_SIZE, -ENOSPC)) {
		pr_err("Unexpectedly succeeded in inserting too large object into single page hole\n");
		err = -EINVAL;
		goto out;
	}

	/* Fill the hole, further allocation attempts should then fail */
	obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto out;
	}

	err = i915_gem_object_create_mmap_offset(obj);
	if (err) {
		pr_err("Unable to insert object into reclaimed hole\n");
		goto err_obj;
	}

	if (!assert_mmap_offset(i915, PAGE_SIZE, -ENOSPC)) {
		pr_err("Unexpectedly succeeded in inserting object into no holes!\n");
		err = -EINVAL;
		goto err_obj;
	}

	i915_gem_object_put(obj);

	/* Now fill with busy dead objects that we expect to reap */
	for (loop = 0; loop < 3; loop++) {
		intel_wakeref_t wakeref;

		if (i915_terminally_wedged(i915))
			break;

		obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			goto out;
		}

		err = 0;
		mutex_lock(&i915->drm.struct_mutex);
		with_intel_runtime_pm(i915, wakeref)
			err = make_obj_busy(obj);
		mutex_unlock(&i915->drm.struct_mutex);
		if (err) {
			pr_err("[loop %d] Failed to busy the object\n", loop);
			goto err_obj;
		}

		/* NB we rely on the _active_ reference to access obj now */
		GEM_BUG_ON(!i915_gem_object_is_active(obj));
		err = i915_gem_object_create_mmap_offset(obj);
		if (err) {
			pr_err("[loop %d] i915_gem_object_create_mmap_offset failed with err=%d\n",
			       loop, err);
			goto out;
		}
	}

out:
	drm_mm_remove_node(&resv);
out_park:
	restore_retire_worker(i915);
	return err;
err_obj:
	i915_gem_object_put(obj);
	goto out;
}

int i915_gem_object_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_gem_object),
		SUBTEST(igt_phys_object),
	};
	struct drm_i915_private *i915;
	int err;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	err = i915_subtests(tests, i915);

	drm_dev_put(&i915->drm);
	return err;
}

int i915_gem_object_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_gem_huge),
		SUBTEST(igt_partial_tiling),
		SUBTEST(igt_mmap_offset_exhaustion),
	};

	return i915_subtests(tests, i915);
}
