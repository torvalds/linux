/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2017 Intel Corporation
 */

#include <linux/prime_numbers.h>

#include "gem/i915_gem_pm.h"
#include "gt/intel_reset.h"
#include "i915_selftest.h"

#include "gem/selftests/igt_gem_utils.h"
#include "selftests/i915_random.h"
#include "selftests/igt_flush_test.h"
#include "selftests/igt_live_test.h"
#include "selftests/igt_reset.h"
#include "selftests/igt_spinner.h"
#include "selftests/mock_drm.h"
#include "selftests/mock_gem_device.h"

#include "huge_gem_object.h"
#include "igt_gem_utils.h"

#define DW_PER_PAGE (PAGE_SIZE / sizeof(u32))

static int live_nop_switch(void *arg)
{
	const unsigned int nctx = 1024;
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	struct i915_gem_context **ctx;
	enum intel_engine_id id;
	intel_wakeref_t wakeref;
	struct igt_live_test t;
	struct drm_file *file;
	unsigned long n;
	int err = -ENODEV;

	/*
	 * Create as many contexts as we can feasibly get away with
	 * and check we can switch between them rapidly.
	 *
	 * Serves as very simple stress test for submission and HW switching
	 * between contexts.
	 */

	if (!DRIVER_CAPS(i915)->has_logical_contexts)
		return 0;

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	ctx = kcalloc(nctx, sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		err = -ENOMEM;
		goto out_unlock;
	}

	for (n = 0; n < nctx; n++) {
		ctx[n] = live_context(i915, file);
		if (IS_ERR(ctx[n])) {
			err = PTR_ERR(ctx[n]);
			goto out_unlock;
		}
	}

	for_each_engine(engine, i915, id) {
		struct i915_request *rq;
		unsigned long end_time, prime;
		ktime_t times[2] = {};

		times[0] = ktime_get_raw();
		for (n = 0; n < nctx; n++) {
			rq = igt_request_alloc(ctx[n], engine);
			if (IS_ERR(rq)) {
				err = PTR_ERR(rq);
				goto out_unlock;
			}
			i915_request_add(rq);
		}
		if (i915_request_wait(rq,
				      I915_WAIT_LOCKED,
				      HZ / 5) < 0) {
			pr_err("Failed to populated %d contexts\n", nctx);
			i915_gem_set_wedged(i915);
			err = -EIO;
			goto out_unlock;
		}

		times[1] = ktime_get_raw();

		pr_info("Populated %d contexts on %s in %lluns\n",
			nctx, engine->name, ktime_to_ns(times[1] - times[0]));

		err = igt_live_test_begin(&t, i915, __func__, engine->name);
		if (err)
			goto out_unlock;

		end_time = jiffies + i915_selftest.timeout_jiffies;
		for_each_prime_number_from(prime, 2, 8192) {
			times[1] = ktime_get_raw();

			for (n = 0; n < prime; n++) {
				rq = igt_request_alloc(ctx[n % nctx], engine);
				if (IS_ERR(rq)) {
					err = PTR_ERR(rq);
					goto out_unlock;
				}

				/*
				 * This space is left intentionally blank.
				 *
				 * We do not actually want to perform any
				 * action with this request, we just want
				 * to measure the latency in allocation
				 * and submission of our breadcrumbs -
				 * ensuring that the bare request is sufficient
				 * for the system to work (i.e. proper HEAD
				 * tracking of the rings, interrupt handling,
				 * etc). It also gives us the lowest bounds
				 * for latency.
				 */

				i915_request_add(rq);
			}
			if (i915_request_wait(rq,
					      I915_WAIT_LOCKED,
					      HZ / 5) < 0) {
				pr_err("Switching between %ld contexts timed out\n",
				       prime);
				i915_gem_set_wedged(i915);
				break;
			}

			times[1] = ktime_sub(ktime_get_raw(), times[1]);
			if (prime == 2)
				times[0] = times[1];

			if (__igt_timeout(end_time, NULL))
				break;
		}

		err = igt_live_test_end(&t);
		if (err)
			goto out_unlock;

		pr_info("Switch latencies on %s: 1 = %lluns, %lu = %lluns\n",
			engine->name,
			ktime_to_ns(times[0]),
			prime - 1, div64_u64(ktime_to_ns(times[1]), prime - 1));
	}

out_unlock:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	mock_file_free(i915, file);
	return err;
}

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
				(gen < 6 ? MI_USE_GGTT : 0);
			*cmd++ = 0;
			*cmd++ = offset;
			*cmd++ = value;
		} else {
			*cmd++ = MI_STORE_DWORD_IMM | MI_MEM_VIRTUAL;
			*cmd++ = offset;
			*cmd++ = value;
		}
		offset += PAGE_SIZE;
	}
	*cmd = MI_BATCH_BUFFER_END;
	i915_gem_object_flush_map(obj);
	i915_gem_object_unpin_map(obj);

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
	struct i915_address_space *vm = ctx->vm ?: &i915->ggtt.vm;
	struct i915_request *rq;
	struct i915_vma *vma;
	struct i915_vma *batch;
	unsigned int flags;
	int err;

	GEM_BUG_ON(obj->base.size > vm->total);
	GEM_BUG_ON(!intel_engine_can_store_dword(engine));

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	i915_gem_object_lock(obj);
	err = i915_gem_object_set_to_gtt_domain(obj, false);
	i915_gem_object_unlock(obj);
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

	rq = igt_request_alloc(ctx, engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_batch;
	}

	flags = 0;
	if (INTEL_GEN(vm->i915) <= 5)
		flags |= I915_DISPATCH_SECURE;

	err = engine->emit_bb_start(rq,
				    batch->node.start, batch->node.size,
				    flags);
	if (err)
		goto err_request;

	i915_vma_lock(batch);
	err = i915_vma_move_to_active(batch, rq, 0);
	i915_vma_unlock(batch);
	if (err)
		goto skip_request;

	i915_vma_lock(vma);
	err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(vma);
	if (err)
		goto skip_request;

	i915_request_add(rq);

	i915_vma_unpin(batch);
	i915_vma_close(batch);
	i915_vma_put(batch);

	i915_vma_unpin(vma);

	return 0;

skip_request:
	i915_request_skip(rq, err);
err_request:
	i915_request_add(rq);
err_batch:
	i915_vma_unpin(batch);
	i915_vma_put(batch);
err_vma:
	i915_vma_unpin(vma);
	return err;
}

static int cpu_fill(struct drm_i915_gem_object *obj, u32 value)
{
	const bool has_llc = HAS_LLC(to_i915(obj->base.dev));
	unsigned int n, m, need_flush;
	int err;

	err = i915_gem_object_prepare_write(obj, &need_flush);
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

	i915_gem_object_finish_access(obj);
	obj->read_domains = I915_GEM_DOMAIN_GTT | I915_GEM_DOMAIN_CPU;
	obj->write_domain = 0;
	return 0;
}

static noinline int cpu_check(struct drm_i915_gem_object *obj,
			      unsigned int idx, unsigned int max)
{
	unsigned int n, m, needs_flush;
	int err;

	err = i915_gem_object_prepare_read(obj, &needs_flush);
	if (err)
		return err;

	for (n = 0; n < real_page_count(obj); n++) {
		u32 *map;

		map = kmap_atomic(i915_gem_object_get_page(obj, n));
		if (needs_flush & CLFLUSH_BEFORE)
			drm_clflush_virt_range(map, PAGE_SIZE);

		for (m = 0; m < max; m++) {
			if (map[m] != m) {
				pr_err("%pS: Invalid value at object %d page %d/%ld, offset %d/%d: found %x expected %x\n",
				       __builtin_return_address(0), idx,
				       n, real_page_count(obj), m, max,
				       map[m], m);
				err = -EINVAL;
				goto out_unmap;
			}
		}

		for (; m < DW_PER_PAGE; m++) {
			if (map[m] != STACK_MAGIC) {
				pr_err("%pS: Invalid value at object %d page %d, offset %d: found %x expected %x (uninitialised)\n",
				       __builtin_return_address(0), idx, n, m,
				       map[m], STACK_MAGIC);
				err = -EINVAL;
				goto out_unmap;
			}
		}

out_unmap:
		kunmap_atomic(map);
		if (err)
			break;
	}

	i915_gem_object_finish_access(obj);
	return err;
}

static int file_add_object(struct drm_file *file,
			    struct drm_i915_gem_object *obj)
{
	int err;

	GEM_BUG_ON(obj->base.handle_count);

	/* tie the object to the drm_file for easy reaping */
	err = idr_alloc(&file->object_idr, &obj->base, 1, 0, GFP_KERNEL);
	if (err < 0)
		return  err;

	i915_gem_object_get(obj);
	obj->base.handle_count++;
	return 0;
}

static struct drm_i915_gem_object *
create_test_object(struct i915_gem_context *ctx,
		   struct drm_file *file,
		   struct list_head *objects)
{
	struct drm_i915_gem_object *obj;
	struct i915_address_space *vm = ctx->vm ?: &ctx->i915->ggtt.vm;
	u64 size;
	int err;

	size = min(vm->total / 2, 1024ull * DW_PER_PAGE * PAGE_SIZE);
	size = round_down(size, DW_PER_PAGE * PAGE_SIZE);

	obj = huge_gem_object(ctx->i915, DW_PER_PAGE * PAGE_SIZE, size);
	if (IS_ERR(obj))
		return obj;

	err = file_add_object(file, obj);
	i915_gem_object_put(obj);
	if (err)
		return ERR_PTR(err);

	err = cpu_fill(obj, STACK_MAGIC);
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
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = -ENODEV;

	/*
	 * Create a few different contexts (with different mm) and write
	 * through each ctx/mm using the GPU making sure those writes end
	 * up in the expected pages of our obj.
	 */

	if (!DRIVER_CAPS(i915)->has_logical_contexts)
		return 0;

	for_each_engine(engine, i915, id) {
		struct drm_i915_gem_object *obj = NULL;
		unsigned long ncontexts, ndwords, dw;
		struct igt_live_test t;
		struct drm_file *file;
		IGT_TIMEOUT(end_time);
		LIST_HEAD(objects);

		if (!intel_engine_can_store_dword(engine))
			continue;

		if (!engine->context_size)
			continue; /* No logical context support in HW */

		file = mock_file(i915);
		if (IS_ERR(file))
			return PTR_ERR(file);

		mutex_lock(&i915->drm.struct_mutex);

		err = igt_live_test_begin(&t, i915, __func__, engine->name);
		if (err)
			goto out_unlock;

		ncontexts = 0;
		ndwords = 0;
		dw = 0;
		while (!time_after(jiffies, end_time)) {
			struct i915_gem_context *ctx;
			intel_wakeref_t wakeref;

			ctx = live_context(i915, file);
			if (IS_ERR(ctx)) {
				err = PTR_ERR(ctx);
				goto out_unlock;
			}

			if (!obj) {
				obj = create_test_object(ctx, file, &objects);
				if (IS_ERR(obj)) {
					err = PTR_ERR(obj);
					goto out_unlock;
				}
			}

			with_intel_runtime_pm(&i915->runtime_pm, wakeref)
				err = gpu_fill(obj, ctx, engine, dw);
			if (err) {
				pr_err("Failed to fill dword %lu [%lu/%lu] with gpu (%s) in ctx %u [full-ppgtt? %s], err=%d\n",
				       ndwords, dw, max_dwords(obj),
				       engine->name, ctx->hw_id,
				       yesno(!!ctx->vm), err);
				goto out_unlock;
			}

			if (++dw == max_dwords(obj)) {
				obj = NULL;
				dw = 0;
			}

			ndwords++;
			ncontexts++;
		}

		pr_info("Submitted %lu contexts to %s, filling %lu dwords\n",
			ncontexts, engine->name, ndwords);

		ncontexts = dw = 0;
		list_for_each_entry(obj, &objects, st_link) {
			unsigned int rem =
				min_t(unsigned int, ndwords - dw, max_dwords(obj));

			err = cpu_check(obj, ncontexts++, rem);
			if (err)
				break;

			dw += rem;
		}

out_unlock:
		if (igt_live_test_end(&t))
			err = -EIO;
		mutex_unlock(&i915->drm.struct_mutex);

		mock_file_free(i915, file);
		if (err)
			return err;
	}

	return 0;
}

static int igt_shared_ctx_exec(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_gem_context *parent;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_live_test t;
	struct drm_file *file;
	int err = 0;

	/*
	 * Create a few different contexts with the same mm and write
	 * through each ctx using the GPU making sure those writes end
	 * up in the expected pages of our obj.
	 */
	if (!DRIVER_CAPS(i915)->has_logical_contexts)
		return 0;

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&i915->drm.struct_mutex);

	parent = live_context(i915, file);
	if (IS_ERR(parent)) {
		err = PTR_ERR(parent);
		goto out_unlock;
	}

	if (!parent->vm) { /* not full-ppgtt; nothing to share */
		err = 0;
		goto out_unlock;
	}

	err = igt_live_test_begin(&t, i915, __func__, "");
	if (err)
		goto out_unlock;

	for_each_engine(engine, i915, id) {
		unsigned long ncontexts, ndwords, dw;
		struct drm_i915_gem_object *obj = NULL;
		IGT_TIMEOUT(end_time);
		LIST_HEAD(objects);

		if (!intel_engine_can_store_dword(engine))
			continue;

		dw = 0;
		ndwords = 0;
		ncontexts = 0;
		while (!time_after(jiffies, end_time)) {
			struct i915_gem_context *ctx;
			intel_wakeref_t wakeref;

			ctx = kernel_context(i915);
			if (IS_ERR(ctx)) {
				err = PTR_ERR(ctx);
				goto out_test;
			}

			__assign_ppgtt(ctx, parent->vm);

			if (!obj) {
				obj = create_test_object(parent, file, &objects);
				if (IS_ERR(obj)) {
					err = PTR_ERR(obj);
					kernel_context_close(ctx);
					goto out_test;
				}
			}

			err = 0;
			with_intel_runtime_pm(&i915->runtime_pm, wakeref)
				err = gpu_fill(obj, ctx, engine, dw);
			if (err) {
				pr_err("Failed to fill dword %lu [%lu/%lu] with gpu (%s) in ctx %u [full-ppgtt? %s], err=%d\n",
				       ndwords, dw, max_dwords(obj),
				       engine->name, ctx->hw_id,
				       yesno(!!ctx->vm), err);
				kernel_context_close(ctx);
				goto out_test;
			}

			if (++dw == max_dwords(obj)) {
				obj = NULL;
				dw = 0;
			}

			ndwords++;
			ncontexts++;

			kernel_context_close(ctx);
		}
		pr_info("Submitted %lu contexts to %s, filling %lu dwords\n",
			ncontexts, engine->name, ndwords);

		ncontexts = dw = 0;
		list_for_each_entry(obj, &objects, st_link) {
			unsigned int rem =
				min_t(unsigned int, ndwords - dw, max_dwords(obj));

			err = cpu_check(obj, ncontexts++, rem);
			if (err)
				goto out_test;

			dw += rem;
		}
	}
out_test:
	if (igt_live_test_end(&t))
		err = -EIO;
out_unlock:
	mutex_unlock(&i915->drm.struct_mutex);

	mock_file_free(i915, file);
	return err;
}

static struct i915_vma *rpcs_query_batch(struct i915_vma *vma)
{
	struct drm_i915_gem_object *obj;
	u32 *cmd;
	int err;

	if (INTEL_GEN(vma->vm->i915) < 8)
		return ERR_PTR(-EINVAL);

	obj = i915_gem_object_create_internal(vma->vm->i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	cmd = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto err;
	}

	*cmd++ = MI_STORE_REGISTER_MEM_GEN8;
	*cmd++ = i915_mmio_reg_offset(GEN8_R_PWR_CLK_STATE);
	*cmd++ = lower_32_bits(vma->node.start);
	*cmd++ = upper_32_bits(vma->node.start);
	*cmd = MI_BATCH_BUFFER_END;

	__i915_gem_object_flush_map(obj, 0, 64);
	i915_gem_object_unpin_map(obj);

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

static int
emit_rpcs_query(struct drm_i915_gem_object *obj,
		struct intel_context *ce,
		struct i915_request **rq_out)
{
	struct i915_request *rq;
	struct i915_vma *batch;
	struct i915_vma *vma;
	int err;

	GEM_BUG_ON(!intel_engine_can_store_dword(ce->engine));

	vma = i915_vma_instance(obj, ce->gem_context->vm, NULL);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	i915_gem_object_lock(obj);
	err = i915_gem_object_set_to_gtt_domain(obj, false);
	i915_gem_object_unlock(obj);
	if (err)
		return err;

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		return err;

	batch = rpcs_query_batch(vma);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto err_vma;
	}

	rq = i915_request_create(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_batch;
	}

	err = rq->engine->emit_bb_start(rq,
					batch->node.start, batch->node.size,
					0);
	if (err)
		goto err_request;

	i915_vma_lock(batch);
	err = i915_vma_move_to_active(batch, rq, 0);
	i915_vma_unlock(batch);
	if (err)
		goto skip_request;

	i915_vma_lock(vma);
	err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(vma);
	if (err)
		goto skip_request;

	i915_vma_unpin(batch);
	i915_vma_close(batch);
	i915_vma_put(batch);

	i915_vma_unpin(vma);

	*rq_out = i915_request_get(rq);

	i915_request_add(rq);

	return 0;

skip_request:
	i915_request_skip(rq, err);
err_request:
	i915_request_add(rq);
err_batch:
	i915_vma_unpin(batch);
	i915_vma_put(batch);
err_vma:
	i915_vma_unpin(vma);

	return err;
}

#define TEST_IDLE	BIT(0)
#define TEST_BUSY	BIT(1)
#define TEST_RESET	BIT(2)

static int
__sseu_prepare(struct drm_i915_private *i915,
	       const char *name,
	       unsigned int flags,
	       struct intel_context *ce,
	       struct igt_spinner **spin)
{
	struct i915_request *rq;
	int ret;

	*spin = NULL;
	if (!(flags & (TEST_BUSY | TEST_RESET)))
		return 0;

	*spin = kzalloc(sizeof(**spin), GFP_KERNEL);
	if (!*spin)
		return -ENOMEM;

	ret = igt_spinner_init(*spin, i915);
	if (ret)
		goto err_free;

	rq = igt_spinner_create_request(*spin,
					ce->gem_context,
					ce->engine,
					MI_NOOP);
	if (IS_ERR(rq)) {
		ret = PTR_ERR(rq);
		goto err_fini;
	}

	i915_request_add(rq);

	if (!igt_wait_for_spinner(*spin, rq)) {
		pr_err("%s: Spinner failed to start!\n", name);
		ret = -ETIMEDOUT;
		goto err_end;
	}

	return 0;

err_end:
	igt_spinner_end(*spin);
err_fini:
	igt_spinner_fini(*spin);
err_free:
	kfree(fetch_and_zero(spin));
	return ret;
}

static int
__read_slice_count(struct drm_i915_private *i915,
		   struct intel_context *ce,
		   struct drm_i915_gem_object *obj,
		   struct igt_spinner *spin,
		   u32 *rpcs)
{
	struct i915_request *rq = NULL;
	u32 s_mask, s_shift;
	unsigned int cnt;
	u32 *buf, val;
	long ret;

	ret = emit_rpcs_query(obj, ce, &rq);
	if (ret)
		return ret;

	if (spin)
		igt_spinner_end(spin);

	ret = i915_request_wait(rq, I915_WAIT_LOCKED, MAX_SCHEDULE_TIMEOUT);
	i915_request_put(rq);
	if (ret < 0)
		return ret;

	buf = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(buf)) {
		ret = PTR_ERR(buf);
		return ret;
	}

	if (INTEL_GEN(i915) >= 11) {
		s_mask = GEN11_RPCS_S_CNT_MASK;
		s_shift = GEN11_RPCS_S_CNT_SHIFT;
	} else {
		s_mask = GEN8_RPCS_S_CNT_MASK;
		s_shift = GEN8_RPCS_S_CNT_SHIFT;
	}

	val = *buf;
	cnt = (val & s_mask) >> s_shift;
	*rpcs = val;

	i915_gem_object_unpin_map(obj);

	return cnt;
}

static int
__check_rpcs(const char *name, u32 rpcs, int slices, unsigned int expected,
	     const char *prefix, const char *suffix)
{
	if (slices == expected)
		return 0;

	if (slices < 0) {
		pr_err("%s: %s read slice count failed with %d%s\n",
		       name, prefix, slices, suffix);
		return slices;
	}

	pr_err("%s: %s slice count %d is not %u%s\n",
	       name, prefix, slices, expected, suffix);

	pr_info("RPCS=0x%x; %u%sx%u%s\n",
		rpcs, slices,
		(rpcs & GEN8_RPCS_S_CNT_ENABLE) ? "*" : "",
		(rpcs & GEN8_RPCS_SS_CNT_MASK) >> GEN8_RPCS_SS_CNT_SHIFT,
		(rpcs & GEN8_RPCS_SS_CNT_ENABLE) ? "*" : "");

	return -EINVAL;
}

static int
__sseu_finish(struct drm_i915_private *i915,
	      const char *name,
	      unsigned int flags,
	      struct intel_context *ce,
	      struct drm_i915_gem_object *obj,
	      unsigned int expected,
	      struct igt_spinner *spin)
{
	unsigned int slices = hweight32(ce->engine->sseu.slice_mask);
	u32 rpcs = 0;
	int ret = 0;

	if (flags & TEST_RESET) {
		ret = i915_reset_engine(ce->engine, "sseu");
		if (ret)
			goto out;
	}

	ret = __read_slice_count(i915, ce, obj,
				 flags & TEST_RESET ? NULL : spin, &rpcs);
	ret = __check_rpcs(name, rpcs, ret, expected, "Context", "!");
	if (ret)
		goto out;

	ret = __read_slice_count(i915, ce->engine->kernel_context, obj,
				 NULL, &rpcs);
	ret = __check_rpcs(name, rpcs, ret, slices, "Kernel context", "!");

out:
	if (spin)
		igt_spinner_end(spin);

	if ((flags & TEST_IDLE) && ret == 0) {
		ret = i915_gem_wait_for_idle(i915,
					     I915_WAIT_LOCKED,
					     MAX_SCHEDULE_TIMEOUT);
		if (ret)
			return ret;

		ret = __read_slice_count(i915, ce, obj, NULL, &rpcs);
		ret = __check_rpcs(name, rpcs, ret, expected,
				   "Context", " after idle!");
	}

	return ret;
}

static int
__sseu_test(struct drm_i915_private *i915,
	    const char *name,
	    unsigned int flags,
	    struct intel_context *ce,
	    struct drm_i915_gem_object *obj,
	    struct intel_sseu sseu)
{
	struct igt_spinner *spin = NULL;
	int ret;

	ret = __sseu_prepare(i915, name, flags, ce, &spin);
	if (ret)
		return ret;

	ret = __intel_context_reconfigure_sseu(ce, sseu);
	if (ret)
		goto out_spin;

	ret = __sseu_finish(i915, name, flags, ce, obj,
			    hweight32(sseu.slice_mask), spin);

out_spin:
	if (spin) {
		igt_spinner_end(spin);
		igt_spinner_fini(spin);
		kfree(spin);
	}
	return ret;
}

static int
__igt_ctx_sseu(struct drm_i915_private *i915,
	       const char *name,
	       unsigned int flags)
{
	struct intel_engine_cs *engine = i915->engine[RCS0];
	struct intel_sseu default_sseu = engine->sseu;
	struct drm_i915_gem_object *obj;
	struct i915_gem_context *ctx;
	struct intel_context *ce;
	struct intel_sseu pg_sseu;
	intel_wakeref_t wakeref;
	struct drm_file *file;
	int ret;

	if (INTEL_GEN(i915) < 9)
		return 0;

	if (!RUNTIME_INFO(i915)->sseu.has_slice_pg)
		return 0;

	if (hweight32(default_sseu.slice_mask) < 2)
		return 0;

	/*
	 * Gen11 VME friendly power-gated configuration with half enabled
	 * sub-slices.
	 */
	pg_sseu = default_sseu;
	pg_sseu.slice_mask = 1;
	pg_sseu.subslice_mask =
		~(~0 << (hweight32(default_sseu.subslice_mask) / 2));

	pr_info("SSEU subtest '%s', flags=%x, def_slices=%u, pg_slices=%u\n",
		name, flags, hweight32(default_sseu.slice_mask),
		hweight32(pg_sseu.slice_mask));

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	if (flags & TEST_RESET)
		igt_global_reset_lock(i915);

	mutex_lock(&i915->drm.struct_mutex);

	ctx = live_context(i915, file);
	if (IS_ERR(ctx)) {
		ret = PTR_ERR(ctx);
		goto out_unlock;
	}
	i915_gem_context_clear_bannable(ctx); /* to reset and beyond! */

	obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto out_unlock;
	}

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	ce = i915_gem_context_get_engine(ctx, RCS0);
	if (IS_ERR(ce)) {
		ret = PTR_ERR(ce);
		goto out_rpm;
	}

	ret = intel_context_pin(ce);
	if (ret)
		goto out_context;

	/* First set the default mask. */
	ret = __sseu_test(i915, name, flags, ce, obj, default_sseu);
	if (ret)
		goto out_fail;

	/* Then set a power-gated configuration. */
	ret = __sseu_test(i915, name, flags, ce, obj, pg_sseu);
	if (ret)
		goto out_fail;

	/* Back to defaults. */
	ret = __sseu_test(i915, name, flags, ce, obj, default_sseu);
	if (ret)
		goto out_fail;

	/* One last power-gated configuration for the road. */
	ret = __sseu_test(i915, name, flags, ce, obj, pg_sseu);
	if (ret)
		goto out_fail;

out_fail:
	if (igt_flush_test(i915, I915_WAIT_LOCKED))
		ret = -EIO;

	intel_context_unpin(ce);
out_context:
	intel_context_put(ce);
out_rpm:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	i915_gem_object_put(obj);

out_unlock:
	mutex_unlock(&i915->drm.struct_mutex);

	if (flags & TEST_RESET)
		igt_global_reset_unlock(i915);

	mock_file_free(i915, file);

	if (ret)
		pr_err("%s: Failed with %d!\n", name, ret);

	return ret;
}

static int igt_ctx_sseu(void *arg)
{
	struct {
		const char *name;
		unsigned int flags;
	} *phase, phases[] = {
		{ .name = "basic", .flags = 0 },
		{ .name = "idle", .flags = TEST_IDLE },
		{ .name = "busy", .flags = TEST_BUSY },
		{ .name = "busy-reset", .flags = TEST_BUSY | TEST_RESET },
		{ .name = "busy-idle", .flags = TEST_BUSY | TEST_IDLE },
		{ .name = "reset-idle", .flags = TEST_RESET | TEST_IDLE },
	};
	unsigned int i;
	int ret = 0;

	for (i = 0, phase = phases; ret == 0 && i < ARRAY_SIZE(phases);
	     i++, phase++)
		ret = __igt_ctx_sseu(arg, phase->name, phase->flags);

	return ret;
}

static int igt_ctx_readonly(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj = NULL;
	struct i915_address_space *vm;
	struct i915_gem_context *ctx;
	unsigned long idx, ndwords, dw;
	struct igt_live_test t;
	struct drm_file *file;
	I915_RND_STATE(prng);
	IGT_TIMEOUT(end_time);
	LIST_HEAD(objects);
	int err = -ENODEV;

	/*
	 * Create a few read-only objects (with the occasional writable object)
	 * and try to write into these object checking that the GPU discards
	 * any write to a read-only object.
	 */

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&i915->drm.struct_mutex);

	err = igt_live_test_begin(&t, i915, __func__, "");
	if (err)
		goto out_unlock;

	ctx = live_context(i915, file);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto out_unlock;
	}

	vm = ctx->vm ?: &i915->mm.aliasing_ppgtt->vm;
	if (!vm || !vm->has_read_only) {
		err = 0;
		goto out_unlock;
	}

	ndwords = 0;
	dw = 0;
	while (!time_after(jiffies, end_time)) {
		struct intel_engine_cs *engine;
		unsigned int id;

		for_each_engine(engine, i915, id) {
			intel_wakeref_t wakeref;

			if (!intel_engine_can_store_dword(engine))
				continue;

			if (!obj) {
				obj = create_test_object(ctx, file, &objects);
				if (IS_ERR(obj)) {
					err = PTR_ERR(obj);
					goto out_unlock;
				}

				if (prandom_u32_state(&prng) & 1)
					i915_gem_object_set_readonly(obj);
			}

			err = 0;
			with_intel_runtime_pm(&i915->runtime_pm, wakeref)
				err = gpu_fill(obj, ctx, engine, dw);
			if (err) {
				pr_err("Failed to fill dword %lu [%lu/%lu] with gpu (%s) in ctx %u [full-ppgtt? %s], err=%d\n",
				       ndwords, dw, max_dwords(obj),
				       engine->name, ctx->hw_id,
				       yesno(!!ctx->vm), err);
				goto out_unlock;
			}

			if (++dw == max_dwords(obj)) {
				obj = NULL;
				dw = 0;
			}
			ndwords++;
		}
	}
	pr_info("Submitted %lu dwords (across %u engines)\n",
		ndwords, RUNTIME_INFO(i915)->num_engines);

	dw = 0;
	idx = 0;
	list_for_each_entry(obj, &objects, st_link) {
		unsigned int rem =
			min_t(unsigned int, ndwords - dw, max_dwords(obj));
		unsigned int num_writes;

		num_writes = rem;
		if (i915_gem_object_is_readonly(obj))
			num_writes = 0;

		err = cpu_check(obj, idx++, num_writes);
		if (err)
			break;

		dw += rem;
	}

out_unlock:
	if (igt_live_test_end(&t))
		err = -EIO;
	mutex_unlock(&i915->drm.struct_mutex);

	mock_file_free(i915, file);
	return err;
}

static int check_scratch(struct i915_gem_context *ctx, u64 offset)
{
	struct drm_mm_node *node =
		__drm_mm_interval_first(&ctx->vm->mm,
					offset, offset + sizeof(u32) - 1);
	if (!node || node->start > offset)
		return 0;

	GEM_BUG_ON(offset >= node->start + node->size);

	pr_err("Target offset 0x%08x_%08x overlaps with a node in the mm!\n",
	       upper_32_bits(offset), lower_32_bits(offset));
	return -EINVAL;
}

static int write_to_scratch(struct i915_gem_context *ctx,
			    struct intel_engine_cs *engine,
			    u64 offset, u32 value)
{
	struct drm_i915_private *i915 = ctx->i915;
	struct drm_i915_gem_object *obj;
	struct i915_request *rq;
	struct i915_vma *vma;
	u32 *cmd;
	int err;

	GEM_BUG_ON(offset < I915_GTT_PAGE_SIZE);

	obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	cmd = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto err;
	}

	*cmd++ = MI_STORE_DWORD_IMM_GEN4;
	if (INTEL_GEN(i915) >= 8) {
		*cmd++ = lower_32_bits(offset);
		*cmd++ = upper_32_bits(offset);
	} else {
		*cmd++ = 0;
		*cmd++ = offset;
	}
	*cmd++ = value;
	*cmd = MI_BATCH_BUFFER_END;
	__i915_gem_object_flush_map(obj, 0, 64);
	i915_gem_object_unpin_map(obj);

	vma = i915_vma_instance(obj, ctx->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER | PIN_OFFSET_FIXED);
	if (err)
		goto err;

	err = check_scratch(ctx, offset);
	if (err)
		goto err_unpin;

	rq = igt_request_alloc(ctx, engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_unpin;
	}

	err = engine->emit_bb_start(rq, vma->node.start, vma->node.size, 0);
	if (err)
		goto err_request;

	i915_vma_lock(vma);
	err = i915_vma_move_to_active(vma, rq, 0);
	i915_vma_unlock(vma);
	if (err)
		goto skip_request;

	i915_vma_unpin(vma);
	i915_vma_close(vma);
	i915_vma_put(vma);

	i915_request_add(rq);

	return 0;

skip_request:
	i915_request_skip(rq, err);
err_request:
	i915_request_add(rq);
err_unpin:
	i915_vma_unpin(vma);
err:
	i915_gem_object_put(obj);
	return err;
}

static int read_from_scratch(struct i915_gem_context *ctx,
			     struct intel_engine_cs *engine,
			     u64 offset, u32 *value)
{
	struct drm_i915_private *i915 = ctx->i915;
	struct drm_i915_gem_object *obj;
	const u32 RCS_GPR0 = 0x2600; /* not all engines have their own GPR! */
	const u32 result = 0x100;
	struct i915_request *rq;
	struct i915_vma *vma;
	u32 *cmd;
	int err;

	GEM_BUG_ON(offset < I915_GTT_PAGE_SIZE);

	obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	cmd = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto err;
	}

	memset(cmd, POISON_INUSE, PAGE_SIZE);
	if (INTEL_GEN(i915) >= 8) {
		*cmd++ = MI_LOAD_REGISTER_MEM_GEN8;
		*cmd++ = RCS_GPR0;
		*cmd++ = lower_32_bits(offset);
		*cmd++ = upper_32_bits(offset);
		*cmd++ = MI_STORE_REGISTER_MEM_GEN8;
		*cmd++ = RCS_GPR0;
		*cmd++ = result;
		*cmd++ = 0;
	} else {
		*cmd++ = MI_LOAD_REGISTER_MEM;
		*cmd++ = RCS_GPR0;
		*cmd++ = offset;
		*cmd++ = MI_STORE_REGISTER_MEM;
		*cmd++ = RCS_GPR0;
		*cmd++ = result;
	}
	*cmd = MI_BATCH_BUFFER_END;

	i915_gem_object_flush_map(obj);
	i915_gem_object_unpin_map(obj);

	vma = i915_vma_instance(obj, ctx->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER | PIN_OFFSET_FIXED);
	if (err)
		goto err;

	err = check_scratch(ctx, offset);
	if (err)
		goto err_unpin;

	rq = igt_request_alloc(ctx, engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_unpin;
	}

	err = engine->emit_bb_start(rq, vma->node.start, vma->node.size, 0);
	if (err)
		goto err_request;

	i915_vma_lock(vma);
	err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(vma);
	if (err)
		goto skip_request;

	i915_vma_unpin(vma);
	i915_vma_close(vma);

	i915_request_add(rq);

	i915_gem_object_lock(obj);
	err = i915_gem_object_set_to_cpu_domain(obj, false);
	i915_gem_object_unlock(obj);
	if (err)
		goto err;

	cmd = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto err;
	}

	*value = cmd[result / sizeof(*cmd)];
	i915_gem_object_unpin_map(obj);
	i915_gem_object_put(obj);

	return 0;

skip_request:
	i915_request_skip(rq, err);
err_request:
	i915_request_add(rq);
err_unpin:
	i915_vma_unpin(vma);
err:
	i915_gem_object_put(obj);
	return err;
}

static int igt_vm_isolation(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_gem_context *ctx_a, *ctx_b;
	struct intel_engine_cs *engine;
	intel_wakeref_t wakeref;
	struct igt_live_test t;
	struct drm_file *file;
	I915_RND_STATE(prng);
	unsigned long count;
	unsigned int id;
	u64 vm_total;
	int err;

	if (INTEL_GEN(i915) < 7)
		return 0;

	/*
	 * The simple goal here is that a write into one context is not
	 * observed in a second (separate page tables and scratch).
	 */

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&i915->drm.struct_mutex);

	err = igt_live_test_begin(&t, i915, __func__, "");
	if (err)
		goto out_unlock;

	ctx_a = live_context(i915, file);
	if (IS_ERR(ctx_a)) {
		err = PTR_ERR(ctx_a);
		goto out_unlock;
	}

	ctx_b = live_context(i915, file);
	if (IS_ERR(ctx_b)) {
		err = PTR_ERR(ctx_b);
		goto out_unlock;
	}

	/* We can only test vm isolation, if the vm are distinct */
	if (ctx_a->vm == ctx_b->vm)
		goto out_unlock;

	vm_total = ctx_a->vm->total;
	GEM_BUG_ON(ctx_b->vm->total != vm_total);
	vm_total -= I915_GTT_PAGE_SIZE;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	count = 0;
	for_each_engine(engine, i915, id) {
		IGT_TIMEOUT(end_time);
		unsigned long this = 0;

		if (!intel_engine_can_store_dword(engine))
			continue;

		while (!__igt_timeout(end_time, NULL)) {
			u32 value = 0xc5c5c5c5;
			u64 offset;

			div64_u64_rem(i915_prandom_u64_state(&prng),
				      vm_total, &offset);
			offset &= -sizeof(u32);
			offset += I915_GTT_PAGE_SIZE;

			err = write_to_scratch(ctx_a, engine,
					       offset, 0xdeadbeef);
			if (err == 0)
				err = read_from_scratch(ctx_b, engine,
							offset, &value);
			if (err)
				goto out_rpm;

			if (value) {
				pr_err("%s: Read %08x from scratch (offset 0x%08x_%08x), after %lu reads!\n",
				       engine->name, value,
				       upper_32_bits(offset),
				       lower_32_bits(offset),
				       this);
				err = -EINVAL;
				goto out_rpm;
			}

			this++;
		}
		count += this;
	}
	pr_info("Checked %lu scratch offsets across %d engines\n",
		count, RUNTIME_INFO(i915)->num_engines);

out_rpm:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
out_unlock:
	if (igt_live_test_end(&t))
		err = -EIO;
	mutex_unlock(&i915->drm.struct_mutex);

	mock_file_free(i915, file);
	return err;
}

static __maybe_unused const char *
__engine_name(struct drm_i915_private *i915, intel_engine_mask_t engines)
{
	struct intel_engine_cs *engine;
	intel_engine_mask_t tmp;

	if (engines == ALL_ENGINES)
		return "all";

	for_each_engine_masked(engine, i915, engines, tmp)
		return engine->name;

	return "none";
}

static bool skip_unused_engines(struct intel_context *ce, void *data)
{
	return !ce->state;
}

static void mock_barrier_task(void *data)
{
	unsigned int *counter = data;

	++*counter;
}

static int mock_context_barrier(void *arg)
{
#undef pr_fmt
#define pr_fmt(x) "context_barrier_task():" # x
	struct drm_i915_private *i915 = arg;
	struct i915_gem_context *ctx;
	struct i915_request *rq;
	unsigned int counter;
	int err;

	/*
	 * The context barrier provides us with a callback after it emits
	 * a request; useful for retiring old state after loading new.
	 */

	mutex_lock(&i915->drm.struct_mutex);

	ctx = mock_context(i915, "mock");
	if (!ctx) {
		err = -ENOMEM;
		goto unlock;
	}

	counter = 0;
	err = context_barrier_task(ctx, 0,
				   NULL, NULL, mock_barrier_task, &counter);
	if (err) {
		pr_err("Failed at line %d, err=%d\n", __LINE__, err);
		goto out;
	}
	if (counter == 0) {
		pr_err("Did not retire immediately with 0 engines\n");
		err = -EINVAL;
		goto out;
	}

	counter = 0;
	err = context_barrier_task(ctx, ALL_ENGINES,
				   skip_unused_engines,
				   NULL,
				   mock_barrier_task,
				   &counter);
	if (err) {
		pr_err("Failed at line %d, err=%d\n", __LINE__, err);
		goto out;
	}
	if (counter == 0) {
		pr_err("Did not retire immediately for all unused engines\n");
		err = -EINVAL;
		goto out;
	}

	rq = igt_request_alloc(ctx, i915->engine[RCS0]);
	if (IS_ERR(rq)) {
		pr_err("Request allocation failed!\n");
		goto out;
	}
	i915_request_add(rq);

	counter = 0;
	context_barrier_inject_fault = BIT(RCS0);
	err = context_barrier_task(ctx, ALL_ENGINES,
				   NULL, NULL, mock_barrier_task, &counter);
	context_barrier_inject_fault = 0;
	if (err == -ENXIO)
		err = 0;
	else
		pr_err("Did not hit fault injection!\n");
	if (counter != 0) {
		pr_err("Invoked callback on error!\n");
		err = -EIO;
	}
	if (err)
		goto out;

	counter = 0;
	err = context_barrier_task(ctx, ALL_ENGINES,
				   skip_unused_engines,
				   NULL,
				   mock_barrier_task,
				   &counter);
	if (err) {
		pr_err("Failed at line %d, err=%d\n", __LINE__, err);
		goto out;
	}
	mock_device_flush(i915);
	if (counter == 0) {
		pr_err("Did not retire on each active engines\n");
		err = -EINVAL;
		goto out;
	}

out:
	mock_context_close(ctx);
unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
#undef pr_fmt
#define pr_fmt(x) x
}

int i915_gem_context_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(mock_context_barrier),
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

int i915_gem_context_live_selftests(struct drm_i915_private *dev_priv)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_nop_switch),
		SUBTEST(igt_ctx_exec),
		SUBTEST(igt_ctx_readonly),
		SUBTEST(igt_ctx_sseu),
		SUBTEST(igt_shared_ctx_exec),
		SUBTEST(igt_vm_isolation),
	};

	if (i915_terminally_wedged(dev_priv))
		return 0;

	return i915_subtests(tests, dev_priv);
}
