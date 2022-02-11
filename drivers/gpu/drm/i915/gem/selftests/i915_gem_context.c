/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2017 Intel Corporation
 */

#include <linux/prime_numbers.h>

#include "gem/i915_gem_pm.h"
#include "gt/intel_engine_pm.h"
#include "gt/intel_engine_regs.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_requests.h"
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
	struct igt_live_test t;
	struct file *file;
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

	ctx = kcalloc(nctx, sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		err = -ENOMEM;
		goto out_file;
	}

	for (n = 0; n < nctx; n++) {
		ctx[n] = live_context(i915, file);
		if (IS_ERR(ctx[n])) {
			err = PTR_ERR(ctx[n]);
			goto out_file;
		}
	}

	for_each_uabi_engine(engine, i915) {
		struct i915_request *rq = NULL;
		unsigned long end_time, prime;
		ktime_t times[2] = {};

		times[0] = ktime_get_raw();
		for (n = 0; n < nctx; n++) {
			struct i915_request *this;

			this = igt_request_alloc(ctx[n], engine);
			if (IS_ERR(this)) {
				err = PTR_ERR(this);
				goto out_file;
			}
			if (rq) {
				i915_request_await_dma_fence(this, &rq->fence);
				i915_request_put(rq);
			}
			rq = i915_request_get(this);
			i915_request_add(this);
		}
		if (i915_request_wait(rq, 0, 10 * HZ) < 0) {
			pr_err("Failed to populated %d contexts\n", nctx);
			intel_gt_set_wedged(to_gt(i915));
			i915_request_put(rq);
			err = -EIO;
			goto out_file;
		}
		i915_request_put(rq);

		times[1] = ktime_get_raw();

		pr_info("Populated %d contexts on %s in %lluns\n",
			nctx, engine->name, ktime_to_ns(times[1] - times[0]));

		err = igt_live_test_begin(&t, i915, __func__, engine->name);
		if (err)
			goto out_file;

		end_time = jiffies + i915_selftest.timeout_jiffies;
		for_each_prime_number_from(prime, 2, 8192) {
			times[1] = ktime_get_raw();

			rq = NULL;
			for (n = 0; n < prime; n++) {
				struct i915_request *this;

				this = igt_request_alloc(ctx[n % nctx], engine);
				if (IS_ERR(this)) {
					err = PTR_ERR(this);
					goto out_file;
				}

				if (rq) { /* Force submission order */
					i915_request_await_dma_fence(this, &rq->fence);
					i915_request_put(rq);
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

				rq = i915_request_get(this);
				i915_request_add(this);
			}
			GEM_BUG_ON(!rq);
			if (i915_request_wait(rq, 0, HZ / 5) < 0) {
				pr_err("Switching between %ld contexts timed out\n",
				       prime);
				intel_gt_set_wedged(to_gt(i915));
				i915_request_put(rq);
				break;
			}
			i915_request_put(rq);

			times[1] = ktime_sub(ktime_get_raw(), times[1]);
			if (prime == 2)
				times[0] = times[1];

			if (__igt_timeout(end_time, NULL))
				break;
		}

		err = igt_live_test_end(&t);
		if (err)
			goto out_file;

		pr_info("Switch latencies on %s: 1 = %lluns, %lu = %lluns\n",
			engine->name,
			ktime_to_ns(times[0]),
			prime - 1, div64_u64(ktime_to_ns(times[1]), prime - 1));
	}

out_file:
	fput(file);
	return err;
}

struct parallel_switch {
	struct task_struct *tsk;
	struct intel_context *ce[2];
};

static int __live_parallel_switch1(void *data)
{
	struct parallel_switch *arg = data;
	IGT_TIMEOUT(end_time);
	unsigned long count;

	count = 0;
	do {
		struct i915_request *rq = NULL;
		int err, n;

		err = 0;
		for (n = 0; !err && n < ARRAY_SIZE(arg->ce); n++) {
			struct i915_request *prev = rq;

			rq = i915_request_create(arg->ce[n]);
			if (IS_ERR(rq)) {
				i915_request_put(prev);
				return PTR_ERR(rq);
			}

			i915_request_get(rq);
			if (prev) {
				err = i915_request_await_dma_fence(rq, &prev->fence);
				i915_request_put(prev);
			}

			i915_request_add(rq);
		}
		if (i915_request_wait(rq, 0, HZ / 5) < 0)
			err = -ETIME;
		i915_request_put(rq);
		if (err)
			return err;

		count++;
	} while (!__igt_timeout(end_time, NULL));

	pr_info("%s: %lu switches (sync)\n", arg->ce[0]->engine->name, count);
	return 0;
}

static int __live_parallel_switchN(void *data)
{
	struct parallel_switch *arg = data;
	struct i915_request *rq = NULL;
	IGT_TIMEOUT(end_time);
	unsigned long count;
	int n;

	count = 0;
	do {
		for (n = 0; n < ARRAY_SIZE(arg->ce); n++) {
			struct i915_request *prev = rq;
			int err = 0;

			rq = i915_request_create(arg->ce[n]);
			if (IS_ERR(rq)) {
				i915_request_put(prev);
				return PTR_ERR(rq);
			}

			i915_request_get(rq);
			if (prev) {
				err = i915_request_await_dma_fence(rq, &prev->fence);
				i915_request_put(prev);
			}

			i915_request_add(rq);
			if (err) {
				i915_request_put(rq);
				return err;
			}
		}

		count++;
	} while (!__igt_timeout(end_time, NULL));
	i915_request_put(rq);

	pr_info("%s: %lu switches (many)\n", arg->ce[0]->engine->name, count);
	return 0;
}

static int live_parallel_switch(void *arg)
{
	struct drm_i915_private *i915 = arg;
	static int (* const func[])(void *arg) = {
		__live_parallel_switch1,
		__live_parallel_switchN,
		NULL,
	};
	struct parallel_switch *data = NULL;
	struct i915_gem_engines *engines;
	struct i915_gem_engines_iter it;
	int (* const *fn)(void *arg);
	struct i915_gem_context *ctx;
	struct intel_context *ce;
	struct file *file;
	int n, m, count;
	int err = 0;

	/*
	 * Check we can process switches on all engines simultaneously.
	 */

	if (!DRIVER_CAPS(i915)->has_logical_contexts)
		return 0;

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	ctx = live_context(i915, file);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto out_file;
	}

	engines = i915_gem_context_lock_engines(ctx);
	count = engines->num_engines;

	data = kcalloc(count, sizeof(*data), GFP_KERNEL);
	if (!data) {
		i915_gem_context_unlock_engines(ctx);
		err = -ENOMEM;
		goto out_file;
	}

	m = 0; /* Use the first context as our template for the engines */
	for_each_gem_engine(ce, engines, it) {
		err = intel_context_pin(ce);
		if (err) {
			i915_gem_context_unlock_engines(ctx);
			goto out;
		}
		data[m++].ce[0] = intel_context_get(ce);
	}
	i915_gem_context_unlock_engines(ctx);

	/* Clone the same set of engines into the other contexts */
	for (n = 1; n < ARRAY_SIZE(data->ce); n++) {
		ctx = live_context(i915, file);
		if (IS_ERR(ctx)) {
			err = PTR_ERR(ctx);
			goto out;
		}

		for (m = 0; m < count; m++) {
			if (!data[m].ce[0])
				continue;

			ce = intel_context_create(data[m].ce[0]->engine);
			if (IS_ERR(ce))
				goto out;

			err = intel_context_pin(ce);
			if (err) {
				intel_context_put(ce);
				goto out;
			}

			data[m].ce[n] = ce;
		}
	}

	for (fn = func; !err && *fn; fn++) {
		struct igt_live_test t;
		int n;

		err = igt_live_test_begin(&t, i915, __func__, "");
		if (err)
			break;

		for (n = 0; n < count; n++) {
			if (!data[n].ce[0])
				continue;

			data[n].tsk = kthread_run(*fn, &data[n],
						  "igt/parallel:%s",
						  data[n].ce[0]->engine->name);
			if (IS_ERR(data[n].tsk)) {
				err = PTR_ERR(data[n].tsk);
				break;
			}
			get_task_struct(data[n].tsk);
		}

		yield(); /* start all threads before we kthread_stop() */

		for (n = 0; n < count; n++) {
			int status;

			if (IS_ERR_OR_NULL(data[n].tsk))
				continue;

			status = kthread_stop(data[n].tsk);
			if (status && !err)
				err = status;

			put_task_struct(data[n].tsk);
			data[n].tsk = NULL;
		}

		if (igt_live_test_end(&t))
			err = -EIO;
	}

out:
	for (n = 0; n < count; n++) {
		for (m = 0; m < ARRAY_SIZE(data->ce); m++) {
			if (!data[n].ce[m])
				continue;

			intel_context_unpin(data[n].ce[m]);
			intel_context_put(data[n].ce[m]);
		}
	}
	kfree(data);
out_file:
	fput(file);
	return err;
}

static unsigned long real_page_count(struct drm_i915_gem_object *obj)
{
	return huge_gem_object_phys_size(obj) >> PAGE_SHIFT;
}

static unsigned long fake_page_count(struct drm_i915_gem_object *obj)
{
	return huge_gem_object_dma_size(obj) >> PAGE_SHIFT;
}

static int gpu_fill(struct intel_context *ce,
		    struct drm_i915_gem_object *obj,
		    unsigned int dw)
{
	struct i915_vma *vma;
	int err;

	GEM_BUG_ON(obj->base.size > ce->vm->total);
	GEM_BUG_ON(!intel_engine_can_store_dword(ce->engine));

	vma = i915_vma_instance(obj, ce->vm, NULL);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	err = i915_vma_pin(vma, 0, 0, PIN_HIGH | PIN_USER);
	if (err)
		return err;

	/*
	 * Within the GTT the huge objects maps every page onto
	 * its 1024 real pages (using phys_pfn = dma_pfn % 1024).
	 * We set the nth dword within the page using the nth
	 * mapping via the GTT - this should exercise the GTT mapping
	 * whilst checking that each context provides a unique view
	 * into the object.
	 */
	err = igt_gpu_fill_dw(ce, vma,
			      (dw * real_page_count(obj)) << PAGE_SHIFT |
			      (dw * sizeof(u32)),
			      real_page_count(obj),
			      dw);
	i915_vma_unpin(vma);

	return err;
}

static int cpu_fill(struct drm_i915_gem_object *obj, u32 value)
{
	const bool has_llc = HAS_LLC(to_i915(obj->base.dev));
	unsigned int n, m, need_flush;
	int err;

	i915_gem_object_lock(obj, NULL);
	err = i915_gem_object_prepare_write(obj, &need_flush);
	if (err)
		goto out;

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
out:
	i915_gem_object_unlock(obj);
	return err;
}

static noinline int cpu_check(struct drm_i915_gem_object *obj,
			      unsigned int idx, unsigned int max)
{
	unsigned int n, m, needs_flush;
	int err;

	i915_gem_object_lock(obj, NULL);
	err = i915_gem_object_prepare_read(obj, &needs_flush);
	if (err)
		goto out_unlock;

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
out_unlock:
	i915_gem_object_unlock(obj);
	return err;
}

static int file_add_object(struct file *file, struct drm_i915_gem_object *obj)
{
	int err;

	GEM_BUG_ON(obj->base.handle_count);

	/* tie the object to the drm_file for easy reaping */
	err = idr_alloc(&to_drm_file(file)->object_idr,
			&obj->base, 1, 0, GFP_KERNEL);
	if (err < 0)
		return err;

	i915_gem_object_get(obj);
	obj->base.handle_count++;
	return 0;
}

static struct drm_i915_gem_object *
create_test_object(struct i915_address_space *vm,
		   struct file *file,
		   struct list_head *objects)
{
	struct drm_i915_gem_object *obj;
	u64 size;
	int err;

	/* Keep in GEM's good graces */
	intel_gt_retire_requests(vm->gt);

	size = min(vm->total / 2, 1024ull * DW_PER_PAGE * PAGE_SIZE);
	size = round_down(size, DW_PER_PAGE * PAGE_SIZE);

	obj = huge_gem_object(vm->i915, DW_PER_PAGE * PAGE_SIZE, size);
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

static void throttle_release(struct i915_request **q, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (IS_ERR_OR_NULL(q[i]))
			continue;

		i915_request_put(fetch_and_zero(&q[i]));
	}
}

static int throttle(struct intel_context *ce,
		    struct i915_request **q, int count)
{
	int i;

	if (!IS_ERR_OR_NULL(q[0])) {
		if (i915_request_wait(q[0],
				      I915_WAIT_INTERRUPTIBLE,
				      MAX_SCHEDULE_TIMEOUT) < 0)
			return -EINTR;

		i915_request_put(q[0]);
	}

	for (i = 0; i < count - 1; i++)
		q[i] = q[i + 1];

	q[i] = intel_context_create_request(ce);
	if (IS_ERR(q[i]))
		return PTR_ERR(q[i]);

	i915_request_get(q[i]);
	i915_request_add(q[i]);

	return 0;
}

static int igt_ctx_exec(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	int err = -ENODEV;

	/*
	 * Create a few different contexts (with different mm) and write
	 * through each ctx/mm using the GPU making sure those writes end
	 * up in the expected pages of our obj.
	 */

	if (!DRIVER_CAPS(i915)->has_logical_contexts)
		return 0;

	for_each_uabi_engine(engine, i915) {
		struct drm_i915_gem_object *obj = NULL;
		unsigned long ncontexts, ndwords, dw;
		struct i915_request *tq[5] = {};
		struct igt_live_test t;
		IGT_TIMEOUT(end_time);
		LIST_HEAD(objects);
		struct file *file;

		if (!intel_engine_can_store_dword(engine))
			continue;

		if (!engine->context_size)
			continue; /* No logical context support in HW */

		file = mock_file(i915);
		if (IS_ERR(file))
			return PTR_ERR(file);

		err = igt_live_test_begin(&t, i915, __func__, engine->name);
		if (err)
			goto out_file;

		ncontexts = 0;
		ndwords = 0;
		dw = 0;
		while (!time_after(jiffies, end_time)) {
			struct i915_gem_context *ctx;
			struct intel_context *ce;

			ctx = kernel_context(i915, NULL);
			if (IS_ERR(ctx)) {
				err = PTR_ERR(ctx);
				goto out_file;
			}

			ce = i915_gem_context_get_engine(ctx, engine->legacy_idx);
			GEM_BUG_ON(IS_ERR(ce));

			if (!obj) {
				obj = create_test_object(ce->vm, file, &objects);
				if (IS_ERR(obj)) {
					err = PTR_ERR(obj);
					intel_context_put(ce);
					kernel_context_close(ctx);
					goto out_file;
				}
			}

			err = gpu_fill(ce, obj, dw);
			if (err) {
				pr_err("Failed to fill dword %lu [%lu/%lu] with gpu (%s) [full-ppgtt? %s], err=%d\n",
				       ndwords, dw, max_dwords(obj),
				       engine->name,
				       yesno(i915_gem_context_has_full_ppgtt(ctx)),
				       err);
				intel_context_put(ce);
				kernel_context_close(ctx);
				goto out_file;
			}

			err = throttle(ce, tq, ARRAY_SIZE(tq));
			if (err) {
				intel_context_put(ce);
				kernel_context_close(ctx);
				goto out_file;
			}

			if (++dw == max_dwords(obj)) {
				obj = NULL;
				dw = 0;
			}

			ndwords++;
			ncontexts++;

			intel_context_put(ce);
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
				break;

			dw += rem;
		}

out_file:
		throttle_release(tq, ARRAY_SIZE(tq));
		if (igt_live_test_end(&t))
			err = -EIO;

		fput(file);
		if (err)
			return err;

		i915_gem_drain_freed_objects(i915);
	}

	return 0;
}

static int igt_shared_ctx_exec(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_request *tq[5] = {};
	struct i915_gem_context *parent;
	struct intel_engine_cs *engine;
	struct igt_live_test t;
	struct file *file;
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

	parent = live_context(i915, file);
	if (IS_ERR(parent)) {
		err = PTR_ERR(parent);
		goto out_file;
	}

	if (!parent->vm) { /* not full-ppgtt; nothing to share */
		err = 0;
		goto out_file;
	}

	err = igt_live_test_begin(&t, i915, __func__, "");
	if (err)
		goto out_file;

	for_each_uabi_engine(engine, i915) {
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
			struct intel_context *ce;

			ctx = kernel_context(i915, parent->vm);
			if (IS_ERR(ctx)) {
				err = PTR_ERR(ctx);
				goto out_test;
			}

			ce = i915_gem_context_get_engine(ctx, engine->legacy_idx);
			GEM_BUG_ON(IS_ERR(ce));

			if (!obj) {
				obj = create_test_object(parent->vm,
							 file, &objects);
				if (IS_ERR(obj)) {
					err = PTR_ERR(obj);
					intel_context_put(ce);
					kernel_context_close(ctx);
					goto out_test;
				}
			}

			err = gpu_fill(ce, obj, dw);
			if (err) {
				pr_err("Failed to fill dword %lu [%lu/%lu] with gpu (%s) [full-ppgtt? %s], err=%d\n",
				       ndwords, dw, max_dwords(obj),
				       engine->name,
				       yesno(i915_gem_context_has_full_ppgtt(ctx)),
				       err);
				intel_context_put(ce);
				kernel_context_close(ctx);
				goto out_test;
			}

			err = throttle(ce, tq, ARRAY_SIZE(tq));
			if (err) {
				intel_context_put(ce);
				kernel_context_close(ctx);
				goto out_test;
			}

			if (++dw == max_dwords(obj)) {
				obj = NULL;
				dw = 0;
			}

			ndwords++;
			ncontexts++;

			intel_context_put(ce);
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

		i915_gem_drain_freed_objects(i915);
	}
out_test:
	throttle_release(tq, ARRAY_SIZE(tq));
	if (igt_live_test_end(&t))
		err = -EIO;
out_file:
	fput(file);
	return err;
}

static int rpcs_query_batch(struct drm_i915_gem_object *rpcs, struct i915_vma *vma)
{
	u32 *cmd;

	GEM_BUG_ON(GRAPHICS_VER(vma->vm->i915) < 8);

	cmd = i915_gem_object_pin_map(rpcs, I915_MAP_WB);
	if (IS_ERR(cmd))
		return PTR_ERR(cmd);

	*cmd++ = MI_STORE_REGISTER_MEM_GEN8;
	*cmd++ = i915_mmio_reg_offset(GEN8_R_PWR_CLK_STATE(RENDER_RING_BASE));
	*cmd++ = lower_32_bits(vma->node.start);
	*cmd++ = upper_32_bits(vma->node.start);
	*cmd = MI_BATCH_BUFFER_END;

	__i915_gem_object_flush_map(rpcs, 0, 64);
	i915_gem_object_unpin_map(rpcs);

	intel_gt_chipset_flush(vma->vm->gt);

	return 0;
}

static int
emit_rpcs_query(struct drm_i915_gem_object *obj,
		struct intel_context *ce,
		struct i915_request **rq_out)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_request *rq;
	struct i915_gem_ww_ctx ww;
	struct i915_vma *batch;
	struct i915_vma *vma;
	struct drm_i915_gem_object *rpcs;
	int err;

	GEM_BUG_ON(!intel_engine_can_store_dword(ce->engine));

	if (GRAPHICS_VER(i915) < 8)
		return -EINVAL;

	vma = i915_vma_instance(obj, ce->vm, NULL);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	rpcs = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(rpcs))
		return PTR_ERR(rpcs);

	batch = i915_vma_instance(rpcs, ce->vm, NULL);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto err_put;
	}

	i915_gem_ww_ctx_init(&ww, false);
retry:
	err = i915_gem_object_lock(obj, &ww);
	if (!err)
		err = i915_gem_object_lock(rpcs, &ww);
	if (!err)
		err = i915_gem_object_set_to_gtt_domain(obj, false);
	if (!err)
		err = i915_vma_pin_ww(vma, &ww, 0, 0, PIN_USER);
	if (err)
		goto err_put;

	err = i915_vma_pin_ww(batch, &ww, 0, 0, PIN_USER);
	if (err)
		goto err_vma;

	err = rpcs_query_batch(rpcs, vma);
	if (err)
		goto err_batch;

	rq = i915_request_create(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_batch;
	}

	err = i915_request_await_object(rq, batch->obj, false);
	if (err == 0)
		err = i915_vma_move_to_active(batch, rq, 0);
	if (err)
		goto skip_request;

	err = i915_request_await_object(rq, vma->obj, true);
	if (err == 0)
		err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	if (err)
		goto skip_request;

	if (rq->engine->emit_init_breadcrumb) {
		err = rq->engine->emit_init_breadcrumb(rq);
		if (err)
			goto skip_request;
	}

	err = rq->engine->emit_bb_start(rq,
					batch->node.start, batch->node.size,
					0);
	if (err)
		goto skip_request;

	*rq_out = i915_request_get(rq);

skip_request:
	if (err)
		i915_request_set_error_once(rq, err);
	i915_request_add(rq);
err_batch:
	i915_vma_unpin(batch);
err_vma:
	i915_vma_unpin(vma);
err_put:
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);
	i915_gem_object_put(rpcs);
	return err;
}

#define TEST_IDLE	BIT(0)
#define TEST_BUSY	BIT(1)
#define TEST_RESET	BIT(2)

static int
__sseu_prepare(const char *name,
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

	ret = igt_spinner_init(*spin, ce->engine->gt);
	if (ret)
		goto err_free;

	rq = igt_spinner_create_request(*spin, ce, MI_NOOP);
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
__read_slice_count(struct intel_context *ce,
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

	ret = i915_request_wait(rq, 0, MAX_SCHEDULE_TIMEOUT);
	i915_request_put(rq);
	if (ret < 0)
		return ret;

	buf = i915_gem_object_pin_map_unlocked(obj, I915_MAP_WB);
	if (IS_ERR(buf)) {
		ret = PTR_ERR(buf);
		return ret;
	}

	if (GRAPHICS_VER(ce->engine->i915) >= 11) {
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
__sseu_finish(const char *name,
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
		ret = intel_engine_reset(ce->engine, "sseu");
		if (ret)
			goto out;
	}

	ret = __read_slice_count(ce, obj,
				 flags & TEST_RESET ? NULL : spin, &rpcs);
	ret = __check_rpcs(name, rpcs, ret, expected, "Context", "!");
	if (ret)
		goto out;

	ret = __read_slice_count(ce->engine->kernel_context, obj, NULL, &rpcs);
	ret = __check_rpcs(name, rpcs, ret, slices, "Kernel context", "!");

out:
	if (spin)
		igt_spinner_end(spin);

	if ((flags & TEST_IDLE) && ret == 0) {
		ret = igt_flush_test(ce->engine->i915);
		if (ret)
			return ret;

		ret = __read_slice_count(ce, obj, NULL, &rpcs);
		ret = __check_rpcs(name, rpcs, ret, expected,
				   "Context", " after idle!");
	}

	return ret;
}

static int
__sseu_test(const char *name,
	    unsigned int flags,
	    struct intel_context *ce,
	    struct drm_i915_gem_object *obj,
	    struct intel_sseu sseu)
{
	struct igt_spinner *spin = NULL;
	int ret;

	intel_engine_pm_get(ce->engine);

	ret = __sseu_prepare(name, flags, ce, &spin);
	if (ret)
		goto out_pm;

	ret = intel_context_reconfigure_sseu(ce, sseu);
	if (ret)
		goto out_spin;

	ret = __sseu_finish(name, flags, ce, obj,
			    hweight32(sseu.slice_mask), spin);

out_spin:
	if (spin) {
		igt_spinner_end(spin);
		igt_spinner_fini(spin);
		kfree(spin);
	}
out_pm:
	intel_engine_pm_put(ce->engine);
	return ret;
}

static int
__igt_ctx_sseu(struct drm_i915_private *i915,
	       const char *name,
	       unsigned int flags)
{
	struct drm_i915_gem_object *obj;
	int inst = 0;
	int ret = 0;

	if (GRAPHICS_VER(i915) < 9)
		return 0;

	if (flags & TEST_RESET)
		igt_global_reset_lock(to_gt(i915));

	obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto out_unlock;
	}

	do {
		struct intel_engine_cs *engine;
		struct intel_context *ce;
		struct intel_sseu pg_sseu;

		engine = intel_engine_lookup_user(i915,
						  I915_ENGINE_CLASS_RENDER,
						  inst++);
		if (!engine)
			break;

		if (hweight32(engine->sseu.slice_mask) < 2)
			continue;

		if (!engine->gt->info.sseu.has_slice_pg)
			continue;

		/*
		 * Gen11 VME friendly power-gated configuration with
		 * half enabled sub-slices.
		 */
		pg_sseu = engine->sseu;
		pg_sseu.slice_mask = 1;
		pg_sseu.subslice_mask =
			~(~0 << (hweight32(engine->sseu.subslice_mask) / 2));

		pr_info("%s: SSEU subtest '%s', flags=%x, def_slices=%u, pg_slices=%u\n",
			engine->name, name, flags,
			hweight32(engine->sseu.slice_mask),
			hweight32(pg_sseu.slice_mask));

		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			ret = PTR_ERR(ce);
			goto out_put;
		}

		ret = intel_context_pin(ce);
		if (ret)
			goto out_ce;

		/* First set the default mask. */
		ret = __sseu_test(name, flags, ce, obj, engine->sseu);
		if (ret)
			goto out_unpin;

		/* Then set a power-gated configuration. */
		ret = __sseu_test(name, flags, ce, obj, pg_sseu);
		if (ret)
			goto out_unpin;

		/* Back to defaults. */
		ret = __sseu_test(name, flags, ce, obj, engine->sseu);
		if (ret)
			goto out_unpin;

		/* One last power-gated configuration for the road. */
		ret = __sseu_test(name, flags, ce, obj, pg_sseu);
		if (ret)
			goto out_unpin;

out_unpin:
		intel_context_unpin(ce);
out_ce:
		intel_context_put(ce);
	} while (!ret);

	if (igt_flush_test(i915))
		ret = -EIO;

out_put:
	i915_gem_object_put(obj);

out_unlock:
	if (flags & TEST_RESET)
		igt_global_reset_unlock(to_gt(i915));

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
	unsigned long idx, ndwords, dw, num_engines;
	struct drm_i915_gem_object *obj = NULL;
	struct i915_request *tq[5] = {};
	struct i915_gem_engines_iter it;
	struct i915_address_space *vm;
	struct i915_gem_context *ctx;
	struct intel_context *ce;
	struct igt_live_test t;
	I915_RND_STATE(prng);
	IGT_TIMEOUT(end_time);
	LIST_HEAD(objects);
	struct file *file;
	int err = -ENODEV;

	/*
	 * Create a few read-only objects (with the occasional writable object)
	 * and try to write into these object checking that the GPU discards
	 * any write to a read-only object.
	 */

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	err = igt_live_test_begin(&t, i915, __func__, "");
	if (err)
		goto out_file;

	ctx = live_context(i915, file);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto out_file;
	}

	vm = ctx->vm ?: &i915->ggtt.alias->vm;
	if (!vm || !vm->has_read_only) {
		err = 0;
		goto out_file;
	}

	num_engines = 0;
	for_each_gem_engine(ce, i915_gem_context_lock_engines(ctx), it)
		if (intel_engine_can_store_dword(ce->engine))
			num_engines++;
	i915_gem_context_unlock_engines(ctx);

	ndwords = 0;
	dw = 0;
	while (!time_after(jiffies, end_time)) {
		for_each_gem_engine(ce,
				    i915_gem_context_lock_engines(ctx), it) {
			if (!intel_engine_can_store_dword(ce->engine))
				continue;

			if (!obj) {
				obj = create_test_object(ce->vm, file, &objects);
				if (IS_ERR(obj)) {
					err = PTR_ERR(obj);
					i915_gem_context_unlock_engines(ctx);
					goto out_file;
				}

				if (prandom_u32_state(&prng) & 1)
					i915_gem_object_set_readonly(obj);
			}

			err = gpu_fill(ce, obj, dw);
			if (err) {
				pr_err("Failed to fill dword %lu [%lu/%lu] with gpu (%s) [full-ppgtt? %s], err=%d\n",
				       ndwords, dw, max_dwords(obj),
				       ce->engine->name,
				       yesno(i915_gem_context_has_full_ppgtt(ctx)),
				       err);
				i915_gem_context_unlock_engines(ctx);
				goto out_file;
			}

			err = throttle(ce, tq, ARRAY_SIZE(tq));
			if (err) {
				i915_gem_context_unlock_engines(ctx);
				goto out_file;
			}

			if (++dw == max_dwords(obj)) {
				obj = NULL;
				dw = 0;
			}
			ndwords++;
		}
		i915_gem_context_unlock_engines(ctx);
	}
	pr_info("Submitted %lu dwords (across %lu engines)\n",
		ndwords, num_engines);

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

out_file:
	throttle_release(tq, ARRAY_SIZE(tq));
	if (igt_live_test_end(&t))
		err = -EIO;

	fput(file);
	return err;
}

static int check_scratch(struct i915_address_space *vm, u64 offset)
{
	struct drm_mm_node *node;

	mutex_lock(&vm->mutex);
	node = __drm_mm_interval_first(&vm->mm,
				       offset, offset + sizeof(u32) - 1);
	mutex_unlock(&vm->mutex);
	if (!node || node->start > offset)
		return 0;

	GEM_BUG_ON(offset >= node->start + node->size);

	pr_err("Target offset 0x%08x_%08x overlaps with a node in the mm!\n",
	       upper_32_bits(offset), lower_32_bits(offset));
	return -EINVAL;
}

static int write_to_scratch(struct i915_gem_context *ctx,
			    struct intel_engine_cs *engine,
			    struct drm_i915_gem_object *obj,
			    u64 offset, u32 value)
{
	struct drm_i915_private *i915 = ctx->i915;
	struct i915_address_space *vm;
	struct i915_request *rq;
	struct i915_vma *vma;
	u32 *cmd;
	int err;

	GEM_BUG_ON(offset < I915_GTT_PAGE_SIZE);

	err = check_scratch(ctx->vm, offset);
	if (err)
		return err;

	cmd = i915_gem_object_pin_map_unlocked(obj, I915_MAP_WB);
	if (IS_ERR(cmd))
		return PTR_ERR(cmd);

	*cmd++ = MI_STORE_DWORD_IMM_GEN4;
	if (GRAPHICS_VER(i915) >= 8) {
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

	intel_gt_chipset_flush(engine->gt);

	vm = i915_gem_context_get_eb_vm(ctx);
	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto out_vm;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER | PIN_OFFSET_FIXED);
	if (err)
		goto out_vm;

	rq = igt_request_alloc(ctx, engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_unpin;
	}

	i915_vma_lock(vma);
	err = i915_request_await_object(rq, vma->obj, false);
	if (err == 0)
		err = i915_vma_move_to_active(vma, rq, 0);
	i915_vma_unlock(vma);
	if (err)
		goto skip_request;

	if (rq->engine->emit_init_breadcrumb) {
		err = rq->engine->emit_init_breadcrumb(rq);
		if (err)
			goto skip_request;
	}

	err = engine->emit_bb_start(rq, vma->node.start, vma->node.size, 0);
	if (err)
		goto skip_request;

	i915_vma_unpin(vma);

	i915_request_add(rq);

	goto out_vm;
skip_request:
	i915_request_set_error_once(rq, err);
	i915_request_add(rq);
err_unpin:
	i915_vma_unpin(vma);
out_vm:
	i915_vm_put(vm);

	if (!err)
		err = i915_gem_object_wait(obj, 0, MAX_SCHEDULE_TIMEOUT);

	return err;
}

static int read_from_scratch(struct i915_gem_context *ctx,
			     struct intel_engine_cs *engine,
			     struct drm_i915_gem_object *obj,
			     u64 offset, u32 *value)
{
	struct drm_i915_private *i915 = ctx->i915;
	struct i915_address_space *vm;
	const u32 result = 0x100;
	struct i915_request *rq;
	struct i915_vma *vma;
	unsigned int flags;
	u32 *cmd;
	int err;

	GEM_BUG_ON(offset < I915_GTT_PAGE_SIZE);

	err = check_scratch(ctx->vm, offset);
	if (err)
		return err;

	if (GRAPHICS_VER(i915) >= 8) {
		const u32 GPR0 = engine->mmio_base + 0x600;

		vm = i915_gem_context_get_eb_vm(ctx);
		vma = i915_vma_instance(obj, vm, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto out_vm;
		}

		err = i915_vma_pin(vma, 0, 0, PIN_USER | PIN_OFFSET_FIXED);
		if (err)
			goto out_vm;

		cmd = i915_gem_object_pin_map_unlocked(obj, I915_MAP_WB);
		if (IS_ERR(cmd)) {
			err = PTR_ERR(cmd);
			goto err_unpin;
		}

		memset(cmd, POISON_INUSE, PAGE_SIZE);
		*cmd++ = MI_LOAD_REGISTER_MEM_GEN8;
		*cmd++ = GPR0;
		*cmd++ = lower_32_bits(offset);
		*cmd++ = upper_32_bits(offset);
		*cmd++ = MI_STORE_REGISTER_MEM_GEN8;
		*cmd++ = GPR0;
		*cmd++ = result;
		*cmd++ = 0;
		*cmd = MI_BATCH_BUFFER_END;

		i915_gem_object_flush_map(obj);
		i915_gem_object_unpin_map(obj);

		flags = 0;
	} else {
		const u32 reg = engine->mmio_base + 0x420;

		/* hsw: register access even to 3DPRIM! is protected */
		vm = i915_vm_get(&engine->gt->ggtt->vm);
		vma = i915_vma_instance(obj, vm, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto out_vm;
		}

		err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL);
		if (err)
			goto out_vm;

		cmd = i915_gem_object_pin_map_unlocked(obj, I915_MAP_WB);
		if (IS_ERR(cmd)) {
			err = PTR_ERR(cmd);
			goto err_unpin;
		}

		memset(cmd, POISON_INUSE, PAGE_SIZE);
		*cmd++ = MI_LOAD_REGISTER_MEM;
		*cmd++ = reg;
		*cmd++ = offset;
		*cmd++ = MI_STORE_REGISTER_MEM | MI_USE_GGTT;
		*cmd++ = reg;
		*cmd++ = vma->node.start + result;
		*cmd = MI_BATCH_BUFFER_END;

		i915_gem_object_flush_map(obj);
		i915_gem_object_unpin_map(obj);

		flags = I915_DISPATCH_SECURE;
	}

	intel_gt_chipset_flush(engine->gt);

	rq = igt_request_alloc(ctx, engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_unpin;
	}

	i915_vma_lock(vma);
	err = i915_request_await_object(rq, vma->obj, true);
	if (err == 0)
		err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(vma);
	if (err)
		goto skip_request;

	if (rq->engine->emit_init_breadcrumb) {
		err = rq->engine->emit_init_breadcrumb(rq);
		if (err)
			goto skip_request;
	}

	err = engine->emit_bb_start(rq, vma->node.start, vma->node.size, flags);
	if (err)
		goto skip_request;

	i915_vma_unpin(vma);

	i915_request_add(rq);

	i915_gem_object_lock(obj, NULL);
	err = i915_gem_object_set_to_cpu_domain(obj, false);
	i915_gem_object_unlock(obj);
	if (err)
		goto out_vm;

	cmd = i915_gem_object_pin_map_unlocked(obj, I915_MAP_WB);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto out_vm;
	}

	*value = cmd[result / sizeof(*cmd)];
	i915_gem_object_unpin_map(obj);

	goto out_vm;
skip_request:
	i915_request_set_error_once(rq, err);
	i915_request_add(rq);
err_unpin:
	i915_vma_unpin(vma);
out_vm:
	i915_vm_put(vm);

	if (!err)
		err = i915_gem_object_wait(obj, 0, MAX_SCHEDULE_TIMEOUT);

	return err;
}

static int check_scratch_page(struct i915_gem_context *ctx, u32 *out)
{
	struct i915_address_space *vm;
	u32 *vaddr;
	int err = 0;

	vm = ctx->vm;
	if (!vm)
		return -ENODEV;

	if (!vm->scratch[0]) {
		pr_err("No scratch page!\n");
		return -EINVAL;
	}

	vaddr = __px_vaddr(vm->scratch[0]);

	memcpy(out, vaddr, sizeof(*out));
	if (memchr_inv(vaddr, *out, PAGE_SIZE)) {
		pr_err("Inconsistent initial state of scratch page!\n");
		err = -EINVAL;
	}

	return err;
}

static int igt_vm_isolation(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_gem_context *ctx_a, *ctx_b;
	struct drm_i915_gem_object *obj_a, *obj_b;
	unsigned long num_engines, count;
	struct intel_engine_cs *engine;
	struct igt_live_test t;
	I915_RND_STATE(prng);
	struct file *file;
	u64 vm_total;
	u32 expected;
	int err;

	if (GRAPHICS_VER(i915) < 7)
		return 0;

	/*
	 * The simple goal here is that a write into one context is not
	 * observed in a second (separate page tables and scratch).
	 */

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	err = igt_live_test_begin(&t, i915, __func__, "");
	if (err)
		goto out_file;

	ctx_a = live_context(i915, file);
	if (IS_ERR(ctx_a)) {
		err = PTR_ERR(ctx_a);
		goto out_file;
	}

	ctx_b = live_context(i915, file);
	if (IS_ERR(ctx_b)) {
		err = PTR_ERR(ctx_b);
		goto out_file;
	}

	/* We can only test vm isolation, if the vm are distinct */
	if (ctx_a->vm == ctx_b->vm)
		goto out_file;

	/* Read the initial state of the scratch page */
	err = check_scratch_page(ctx_a, &expected);
	if (err)
		goto out_file;

	err = check_scratch_page(ctx_b, &expected);
	if (err)
		goto out_file;

	vm_total = ctx_a->vm->total;
	GEM_BUG_ON(ctx_b->vm->total != vm_total);

	obj_a = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(obj_a)) {
		err = PTR_ERR(obj_a);
		goto out_file;
	}

	obj_b = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(obj_b)) {
		err = PTR_ERR(obj_b);
		goto put_a;
	}

	count = 0;
	num_engines = 0;
	for_each_uabi_engine(engine, i915) {
		IGT_TIMEOUT(end_time);
		unsigned long this = 0;

		if (!intel_engine_can_store_dword(engine))
			continue;

		/* Not all engines have their own GPR! */
		if (GRAPHICS_VER(i915) < 8 && engine->class != RENDER_CLASS)
			continue;

		while (!__igt_timeout(end_time, NULL)) {
			u32 value = 0xc5c5c5c5;
			u64 offset;

			/* Leave enough space at offset 0 for the batch */
			offset = igt_random_offset(&prng,
						   I915_GTT_PAGE_SIZE, vm_total,
						   sizeof(u32), alignof_dword);

			err = write_to_scratch(ctx_a, engine, obj_a,
					       offset, 0xdeadbeef);
			if (err == 0)
				err = read_from_scratch(ctx_b, engine, obj_b,
							offset, &value);
			if (err)
				goto put_b;

			if (value != expected) {
				pr_err("%s: Read %08x from scratch (offset 0x%08x_%08x), after %lu reads!\n",
				       engine->name, value,
				       upper_32_bits(offset),
				       lower_32_bits(offset),
				       this);
				err = -EINVAL;
				goto put_b;
			}

			this++;
		}
		count += this;
		num_engines++;
	}
	pr_info("Checked %lu scratch offsets across %lu engines\n",
		count, num_engines);

put_b:
	i915_gem_object_put(obj_b);
put_a:
	i915_gem_object_put(obj_a);
out_file:
	if (igt_live_test_end(&t))
		err = -EIO;
	fput(file);
	return err;
}

int i915_gem_context_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_nop_switch),
		SUBTEST(live_parallel_switch),
		SUBTEST(igt_ctx_exec),
		SUBTEST(igt_ctx_readonly),
		SUBTEST(igt_ctx_sseu),
		SUBTEST(igt_shared_ctx_exec),
		SUBTEST(igt_vm_isolation),
	};

	if (intel_gt_is_wedged(to_gt(i915)))
		return 0;

	return i915_live_subtests(tests, i915);
}
