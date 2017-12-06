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

#include "mock_drm.h"
#include "huge_gem_object.h"

#define DW_PER_PAGE (PAGE_SIZE / sizeof(u32))

static struct i915_vma *
gpu_fill_dw(struct i915_vma *vma, u64 offset, unsigned long count, u32 value)
{
	struct drm_i915_gem_object *obj;
	const int gen = INTEL_GEN(vma->vm->i915);
	unsigned long n, size;
	u32 *cmd;
	int err;

	size = (4 * count + 1) * sizeof(u32);
	size = round_up(size, PAGE_SIZE);
	obj = i915_gem_object_create_internal(vma->vm->i915, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	cmd = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto err;
	}

	GEM_BUG_ON(offset + (count - 1) * PAGE_SIZE > vma->node.size);
	offset += vma->node.start;

	for (n = 0; n < count; n++) {
		if (gen >= 8) {
			*cmd++ = MI_STORE_DWORD_IMM_GEN4;
			*cmd++ = lower_32_bits(offset);
			*cmd++ = upper_32_bits(offset);
			*cmd++ = value;
		} else if (gen >= 4) {
			*cmd++ = MI_STORE_DWORD_IMM_GEN4 |
				(gen < 6 ? 1 << 22 : 0);
			*cmd++ = 0;
			*cmd++ = offset;
			*cmd++ = value;
		} else {
			*cmd++ = MI_STORE_DWORD_IMM | 1 << 22;
			*cmd++ = offset;
			*cmd++ = value;
		}
		offset += PAGE_SIZE;
	}
	*cmd = MI_BATCH_BUFFER_END;
	i915_gem_object_unpin_map(obj);

	err = i915_gem_object_set_to_gtt_domain(obj, false);
	if (err)
		goto err;

	vma = i915_vma_instance(obj, vma->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		goto err;

	return vma;

err:
	i915_gem_object_put(obj);
	return ERR_PTR(err);
}

static unsigned long real_page_count(struct drm_i915_gem_object *obj)
{
	return huge_gem_object_phys_size(obj) >> PAGE_SHIFT;
}

static unsigned long fake_page_count(struct drm_i915_gem_object *obj)
{
	return huge_gem_object_dma_size(obj) >> PAGE_SHIFT;
}

static int gpu_fill(struct drm_i915_gem_object *obj,
		    struct i915_gem_context *ctx,
		    struct intel_engine_cs *engine,
		    unsigned int dw)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_address_space *vm =
		ctx->ppgtt ? &ctx->ppgtt->base : &i915->ggtt.base;
	struct drm_i915_gem_request *rq;
	struct i915_vma *vma;
	struct i915_vma *batch;
	unsigned int flags;
	int err;

	GEM_BUG_ON(obj->base.size > vm->total);
	GEM_BUG_ON(!intel_engine_can_store_dword(engine));

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	err = i915_gem_object_set_to_gtt_domain(obj, false);
	if (err)
		return err;

	err = i915_vma_pin(vma, 0, 0, PIN_HIGH | PIN_USER);
	if (err)
		return err;

	/* Within the GTT the huge objects maps every page onto
	 * its 1024 real pages (using phys_pfn = dma_pfn % 1024).
	 * We set the nth dword within the page using the nth
	 * mapping via the GTT - this should exercise the GTT mapping
	 * whilst checking that each context provides a unique view
	 * into the object.
	 */
	batch = gpu_fill_dw(vma,
			    (dw * real_page_count(obj)) << PAGE_SHIFT |
			    (dw * sizeof(u32)),
			    real_page_count(obj),
			    dw);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto err_vma;
	}

	rq = i915_gem_request_alloc(engine, ctx);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_batch;
	}

	err = engine->emit_flush(rq, EMIT_INVALIDATE);
	if (err)
		goto err_request;

	err = i915_switch_context(rq);
	if (err)
		goto err_request;

	flags = 0;
	if (INTEL_GEN(vm->i915) <= 5)
		flags |= I915_DISPATCH_SECURE;

	err = engine->emit_bb_start(rq,
				    batch->node.start, batch->node.size,
				    flags);
	if (err)
		goto err_request;

	i915_vma_move_to_active(batch, rq, 0);
	i915_gem_object_set_active_reference(batch->obj);
	i915_vma_unpin(batch);
	i915_vma_close(batch);

	i915_vma_move_to_active(vma, rq, 0);
	i915_vma_unpin(vma);

	reservation_object_lock(obj->resv, NULL);
	reservation_object_add_excl_fence(obj->resv, &rq->fence);
	reservation_object_unlock(obj->resv);

	__i915_add_request(rq, true);

	return 0;

err_request:
	__i915_add_request(rq, false);
err_batch:
	i915_vma_unpin(batch);
err_vma:
	i915_vma_unpin(vma);
	return err;
}

static int cpu_fill(struct drm_i915_gem_object *obj, u32 value)
{
	const bool has_llc = HAS_LLC(to_i915(obj->base.dev));
	unsigned int n, m, need_flush;
	int err;

	err = i915_gem_obj_prepare_shmem_write(obj, &need_flush);
	if (err)
		return err;

	for (n = 0; n < real_page_count(obj); n++) {
		u32 *map;

		map = kmap_atomic(i915_gem_object_get_page(obj, n));
		for (m = 0; m < DW_PER_PAGE; m++)
			map[m] = value;
		if (!has_llc)
			drm_clflush_virt_range(map, PAGE_SIZE);
		kunmap_atomic(map);
	}

	i915_gem_obj_finish_shmem_access(obj);
	obj->base.read_domains = I915_GEM_DOMAIN_GTT | I915_GEM_DOMAIN_CPU;
	obj->base.write_domain = 0;
	return 0;
}

static int cpu_check(struct drm_i915_gem_object *obj, unsigned int max)
{
	unsigned int n, m, needs_flush;
	int err;

	err = i915_gem_obj_prepare_shmem_read(obj, &needs_flush);
	if (err)
		return err;

	for (n = 0; n < real_page_count(obj); n++) {
		u32 *map;

		map = kmap_atomic(i915_gem_object_get_page(obj, n));
		if (needs_flush & CLFLUSH_BEFORE)
			drm_clflush_virt_range(map, PAGE_SIZE);

		for (m = 0; m < max; m++) {
			if (map[m] != m) {
				pr_err("Invalid value at page %d, offset %d: found %x expected %x\n",
				       n, m, map[m], m);
				err = -EINVAL;
				goto out_unmap;
			}
		}

		for (; m < DW_PER_PAGE; m++) {
			if (map[m] != 0xdeadbeef) {
				pr_err("Invalid value at page %d, offset %d: found %x expected %x\n",
				       n, m, map[m], 0xdeadbeef);
				err = -EINVAL;
				goto out_unmap;
			}
		}

out_unmap:
		kunmap_atomic(map);
		if (err)
			break;
	}

	i915_gem_obj_finish_shmem_access(obj);
	return err;
}

static struct drm_i915_gem_object *
create_test_object(struct i915_gem_context *ctx,
		   struct drm_file *file,
		   struct list_head *objects)
{
	struct drm_i915_gem_object *obj;
	struct i915_address_space *vm =
		ctx->ppgtt ? &ctx->ppgtt->base : &ctx->i915->ggtt.base;
	u64 size;
	u32 handle;
	int err;

	size = min(vm->total / 2, 1024ull * DW_PER_PAGE * PAGE_SIZE);
	size = round_down(size, DW_PER_PAGE * PAGE_SIZE);

	obj = huge_gem_object(ctx->i915, DW_PER_PAGE * PAGE_SIZE, size);
	if (IS_ERR(obj))
		return obj;

	/* tie the handle to the drm_file for easy reaping */
	err = drm_gem_handle_create(file, &obj->base, &handle);
	i915_gem_object_put(obj);
	if (err)
		return ERR_PTR(err);

	err = cpu_fill(obj, 0xdeadbeef);
	if (err) {
		pr_err("Failed to fill object with cpu, err=%d\n",
		       err);
		return ERR_PTR(err);
	}

	list_add_tail(&obj->st_link, objects);
	return obj;
}

static unsigned long max_dwords(struct drm_i915_gem_object *obj)
{
	unsigned long npages = fake_page_count(obj);

	GEM_BUG_ON(!IS_ALIGNED(npages, DW_PER_PAGE));
	return npages / DW_PER_PAGE;
}

static int igt_ctx_exec(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj = NULL;
	struct drm_file *file;
	IGT_TIMEOUT(end_time);
	LIST_HEAD(objects);
	unsigned long ncontexts, ndwords, dw;
	bool first_shared_gtt = true;
	int err;

	/* Create a few different contexts (with different mm) and write
	 * through each ctx/mm using the GPU making sure those writes end
	 * up in the expected pages of our obj.
	 */

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&i915->drm.struct_mutex);

	ncontexts = 0;
	ndwords = 0;
	dw = 0;
	while (!time_after(jiffies, end_time)) {
		struct intel_engine_cs *engine;
		struct i915_gem_context *ctx;
		unsigned int id;

		if (first_shared_gtt) {
			ctx = __create_hw_context(i915, file->driver_priv);
			first_shared_gtt = false;
		} else {
			ctx = i915_gem_create_context(i915, file->driver_priv);
		}
		if (IS_ERR(ctx)) {
			err = PTR_ERR(ctx);
			goto out_unlock;
		}

		for_each_engine(engine, i915, id) {
			if (!intel_engine_can_store_dword(engine))
				continue;

			if (!obj) {
				obj = create_test_object(ctx, file, &objects);
				if (IS_ERR(obj)) {
					err = PTR_ERR(obj);
					goto out_unlock;
				}
			}

			err = gpu_fill(obj, ctx, engine, dw);
			if (err) {
				pr_err("Failed to fill dword %lu [%lu/%lu] with gpu (%s) in ctx %u [full-ppgtt? %s], err=%d\n",
				       ndwords, dw, max_dwords(obj),
				       engine->name, ctx->hw_id,
				       yesno(!!ctx->ppgtt), err);
				goto out_unlock;
			}

			if (++dw == max_dwords(obj)) {
				obj = NULL;
				dw = 0;
			}
			ndwords++;
		}
		ncontexts++;
	}
	pr_info("Submitted %lu contexts (across %u engines), filling %lu dwords\n",
		ncontexts, INTEL_INFO(i915)->num_rings, ndwords);

	dw = 0;
	list_for_each_entry(obj, &objects, st_link) {
		unsigned int rem =
			min_t(unsigned int, ndwords - dw, max_dwords(obj));

		err = cpu_check(obj, rem);
		if (err)
			break;

		dw += rem;
	}

out_unlock:
	mutex_unlock(&i915->drm.struct_mutex);

	mock_file_free(i915, file);
	return err;
}

static int fake_aliasing_ppgtt_enable(struct drm_i915_private *i915)
{
	struct drm_i915_gem_object *obj;
	int err;

	err = i915_gem_init_aliasing_ppgtt(i915);
	if (err)
		return err;

	list_for_each_entry(obj, &i915->mm.bound_list, mm.link) {
		struct i915_vma *vma;

		vma = i915_vma_instance(obj, &i915->ggtt.base, NULL);
		if (IS_ERR(vma))
			continue;

		vma->flags &= ~I915_VMA_LOCAL_BIND;
	}

	return 0;
}

static void fake_aliasing_ppgtt_disable(struct drm_i915_private *i915)
{
	i915_gem_fini_aliasing_ppgtt(i915);
}

int i915_gem_context_live_selftests(struct drm_i915_private *dev_priv)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_ctx_exec),
	};
	bool fake_alias = false;
	int err;

	/* Install a fake aliasing gtt for exercise */
	if (USES_PPGTT(dev_priv) && !dev_priv->mm.aliasing_ppgtt) {
		mutex_lock(&dev_priv->drm.struct_mutex);
		err = fake_aliasing_ppgtt_enable(dev_priv);
		mutex_unlock(&dev_priv->drm.struct_mutex);
		if (err)
			return err;

		GEM_BUG_ON(!dev_priv->mm.aliasing_ppgtt);
		fake_alias = true;
	}

	err = i915_subtests(tests, dev_priv);

	if (fake_alias) {
		mutex_lock(&dev_priv->drm.struct_mutex);
		fake_aliasing_ppgtt_disable(dev_priv);
		mutex_unlock(&dev_priv->drm.struct_mutex);
	}

	return err;
}
