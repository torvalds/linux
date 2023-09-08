/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_selftest.h"
#include "gem/i915_gem_context.h"

#include "mock_context.h"
#include "mock_dmabuf.h"
#include "igt_gem_utils.h"
#include "selftests/mock_drm.h"
#include "selftests/mock_gem_device.h"

static int igt_dmabuf_export(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	struct dma_buf *dmabuf;

	obj = i915_gem_object_create_shmem(i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	dmabuf = i915_gem_prime_export(&obj->base, 0);
	i915_gem_object_put(obj);
	if (IS_ERR(dmabuf)) {
		pr_err("i915_gem_prime_export failed with err=%d\n",
		       (int)PTR_ERR(dmabuf));
		return PTR_ERR(dmabuf);
	}

	dma_buf_put(dmabuf);
	return 0;
}

static int igt_dmabuf_import_self(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj, *import_obj;
	struct drm_gem_object *import;
	struct dma_buf *dmabuf;
	int err;

	obj = i915_gem_object_create_shmem(i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	dmabuf = i915_gem_prime_export(&obj->base, 0);
	if (IS_ERR(dmabuf)) {
		pr_err("i915_gem_prime_export failed with err=%d\n",
		       (int)PTR_ERR(dmabuf));
		err = PTR_ERR(dmabuf);
		goto out;
	}

	import = i915_gem_prime_import(&i915->drm, dmabuf);
	if (IS_ERR(import)) {
		pr_err("i915_gem_prime_import failed with err=%d\n",
		       (int)PTR_ERR(import));
		err = PTR_ERR(import);
		goto out_dmabuf;
	}
	import_obj = to_intel_bo(import);

	if (import != &obj->base) {
		pr_err("i915_gem_prime_import created a new object!\n");
		err = -EINVAL;
		goto out_import;
	}

	i915_gem_object_lock(import_obj, NULL);
	err = __i915_gem_object_get_pages(import_obj);
	i915_gem_object_unlock(import_obj);
	if (err) {
		pr_err("Same object dma-buf get_pages failed!\n");
		goto out_import;
	}

	err = 0;
out_import:
	i915_gem_object_put(import_obj);
out_dmabuf:
	dma_buf_put(dmabuf);
out:
	i915_gem_object_put(obj);
	return err;
}

static int igt_dmabuf_import_same_driver_lmem(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_memory_region *lmem = i915->mm.regions[INTEL_REGION_LMEM_0];
	struct drm_i915_gem_object *obj;
	struct drm_gem_object *import;
	struct dma_buf *dmabuf;
	int err;

	if (!lmem)
		return 0;

	force_different_devices = true;

	obj = __i915_gem_object_create_user(i915, PAGE_SIZE, &lmem, 1);
	if (IS_ERR(obj)) {
		pr_err("__i915_gem_object_create_user failed with err=%ld\n",
		       PTR_ERR(obj));
		err = PTR_ERR(obj);
		goto out_ret;
	}

	dmabuf = i915_gem_prime_export(&obj->base, 0);
	if (IS_ERR(dmabuf)) {
		pr_err("i915_gem_prime_export failed with err=%ld\n",
		       PTR_ERR(dmabuf));
		err = PTR_ERR(dmabuf);
		goto out;
	}

	/*
	 * We expect an import of an LMEM-only object to fail with
	 * -EOPNOTSUPP because it can't be migrated to SMEM.
	 */
	import = i915_gem_prime_import(&i915->drm, dmabuf);
	if (!IS_ERR(import)) {
		drm_gem_object_put(import);
		pr_err("i915_gem_prime_import succeeded when it shouldn't have\n");
		err = -EINVAL;
	} else if (PTR_ERR(import) != -EOPNOTSUPP) {
		pr_err("i915_gem_prime_import failed with the wrong err=%ld\n",
		       PTR_ERR(import));
		err = PTR_ERR(import);
	} else {
		err = 0;
	}

	dma_buf_put(dmabuf);
out:
	i915_gem_object_put(obj);
out_ret:
	force_different_devices = false;
	return err;
}

static int verify_access(struct drm_i915_private *i915,
			 struct drm_i915_gem_object *native_obj,
			 struct drm_i915_gem_object *import_obj)
{
	struct i915_gem_engines_iter it;
	struct i915_gem_context *ctx;
	struct intel_context *ce;
	struct i915_vma *vma;
	struct file *file;
	u32 *vaddr;
	int err = 0, i;

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	ctx = live_context(i915, file);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto out_file;
	}

	for_each_gem_engine(ce, i915_gem_context_lock_engines(ctx), it) {
		if (intel_engine_can_store_dword(ce->engine))
			break;
	}
	i915_gem_context_unlock_engines(ctx);
	if (!ce)
		goto out_file;

	vma = i915_vma_instance(import_obj, ce->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto out_file;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		goto out_file;

	err = igt_gpu_fill_dw(ce, vma, 0,
			      vma->size >> PAGE_SHIFT, 0xdeadbeaf);
	i915_vma_unpin(vma);
	if (err)
		goto out_file;

	err = i915_gem_object_wait(import_obj, 0, MAX_SCHEDULE_TIMEOUT);
	if (err)
		goto out_file;

	vaddr = i915_gem_object_pin_map_unlocked(native_obj, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		goto out_file;
	}

	for (i = 0; i < native_obj->base.size / sizeof(u32); i += PAGE_SIZE / sizeof(u32)) {
		if (vaddr[i] != 0xdeadbeaf) {
			pr_err("Data mismatch [%d]=%u\n", i, vaddr[i]);
			err = -EINVAL;
			goto out_file;
		}
	}

out_file:
	fput(file);
	return err;
}

static int igt_dmabuf_import_same_driver(struct drm_i915_private *i915,
					 struct intel_memory_region **regions,
					 unsigned int num_regions)
{
	struct drm_i915_gem_object *obj, *import_obj;
	struct drm_gem_object *import;
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *import_attach;
	struct sg_table *st;
	long timeout;
	int err;

	force_different_devices = true;

	obj = __i915_gem_object_create_user(i915, SZ_8M,
					    regions, num_regions);
	if (IS_ERR(obj)) {
		pr_err("__i915_gem_object_create_user failed with err=%ld\n",
		       PTR_ERR(obj));
		err = PTR_ERR(obj);
		goto out_ret;
	}

	dmabuf = i915_gem_prime_export(&obj->base, 0);
	if (IS_ERR(dmabuf)) {
		pr_err("i915_gem_prime_export failed with err=%ld\n",
		       PTR_ERR(dmabuf));
		err = PTR_ERR(dmabuf);
		goto out;
	}

	import = i915_gem_prime_import(&i915->drm, dmabuf);
	if (IS_ERR(import)) {
		pr_err("i915_gem_prime_import failed with err=%ld\n",
		       PTR_ERR(import));
		err = PTR_ERR(import);
		goto out_dmabuf;
	}
	import_obj = to_intel_bo(import);

	if (import == &obj->base) {
		pr_err("i915_gem_prime_import reused gem object!\n");
		err = -EINVAL;
		goto out_import;
	}

	i915_gem_object_lock(import_obj, NULL);
	err = __i915_gem_object_get_pages(import_obj);
	if (err) {
		pr_err("Different objects dma-buf get_pages failed!\n");
		i915_gem_object_unlock(import_obj);
		goto out_import;
	}

	/*
	 * If the exported object is not in system memory, something
	 * weird is going on. TODO: When p2p is supported, this is no
	 * longer considered weird.
	 */
	if (obj->mm.region != i915->mm.regions[INTEL_REGION_SMEM]) {
		pr_err("Exported dma-buf is not in system memory\n");
		err = -EINVAL;
	}

	i915_gem_object_unlock(import_obj);

	err = verify_access(i915, obj, import_obj);
	if (err)
		goto out_import;

	/* Now try a fake an importer */
	import_attach = dma_buf_attach(dmabuf, obj->base.dev->dev);
	if (IS_ERR(import_attach)) {
		err = PTR_ERR(import_attach);
		goto out_import;
	}

	st = dma_buf_map_attachment_unlocked(import_attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(st)) {
		err = PTR_ERR(st);
		goto out_detach;
	}

	timeout = dma_resv_wait_timeout(dmabuf->resv, DMA_RESV_USAGE_WRITE,
					true, 5 * HZ);
	if (!timeout) {
		pr_err("dmabuf wait for exclusive fence timed out.\n");
		timeout = -ETIME;
	}
	err = timeout > 0 ? 0 : timeout;
	dma_buf_unmap_attachment_unlocked(import_attach, st, DMA_BIDIRECTIONAL);
out_detach:
	dma_buf_detach(dmabuf, import_attach);
out_import:
	i915_gem_object_put(import_obj);
out_dmabuf:
	dma_buf_put(dmabuf);
out:
	i915_gem_object_put(obj);
out_ret:
	force_different_devices = false;
	return err;
}

static int igt_dmabuf_import_same_driver_smem(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_memory_region *smem = i915->mm.regions[INTEL_REGION_SMEM];

	return igt_dmabuf_import_same_driver(i915, &smem, 1);
}

static int igt_dmabuf_import_same_driver_lmem_smem(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_memory_region *regions[2];

	if (!i915->mm.regions[INTEL_REGION_LMEM_0])
		return 0;

	regions[0] = i915->mm.regions[INTEL_REGION_LMEM_0];
	regions[1] = i915->mm.regions[INTEL_REGION_SMEM];
	return igt_dmabuf_import_same_driver(i915, regions, 2);
}

static int igt_dmabuf_import(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	struct dma_buf *dmabuf;
	void *obj_map, *dma_map;
	struct iosys_map map;
	u32 pattern[] = { 0, 0xaa, 0xcc, 0x55, 0xff };
	int err, i;

	dmabuf = mock_dmabuf(1);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	obj = to_intel_bo(i915_gem_prime_import(&i915->drm, dmabuf));
	if (IS_ERR(obj)) {
		pr_err("i915_gem_prime_import failed with err=%d\n",
		       (int)PTR_ERR(obj));
		err = PTR_ERR(obj);
		goto out_dmabuf;
	}

	if (obj->base.dev != &i915->drm) {
		pr_err("i915_gem_prime_import created a non-i915 object!\n");
		err = -EINVAL;
		goto out_obj;
	}

	if (obj->base.size != PAGE_SIZE) {
		pr_err("i915_gem_prime_import is wrong size found %lld, expected %ld\n",
		       (long long)obj->base.size, PAGE_SIZE);
		err = -EINVAL;
		goto out_obj;
	}

	err = dma_buf_vmap_unlocked(dmabuf, &map);
	dma_map = err ? NULL : map.vaddr;
	if (!dma_map) {
		pr_err("dma_buf_vmap failed\n");
		err = -ENOMEM;
		goto out_obj;
	}

	if (0) { /* Can not yet map dmabuf */
		obj_map = i915_gem_object_pin_map(obj, I915_MAP_WB);
		if (IS_ERR(obj_map)) {
			err = PTR_ERR(obj_map);
			pr_err("i915_gem_object_pin_map failed with err=%d\n", err);
			goto out_dma_map;
		}

		for (i = 0; i < ARRAY_SIZE(pattern); i++) {
			memset(dma_map, pattern[i], PAGE_SIZE);
			if (memchr_inv(obj_map, pattern[i], PAGE_SIZE)) {
				err = -EINVAL;
				pr_err("imported vmap not all set to %x!\n", pattern[i]);
				i915_gem_object_unpin_map(obj);
				goto out_dma_map;
			}
		}

		for (i = 0; i < ARRAY_SIZE(pattern); i++) {
			memset(obj_map, pattern[i], PAGE_SIZE);
			if (memchr_inv(dma_map, pattern[i], PAGE_SIZE)) {
				err = -EINVAL;
				pr_err("exported vmap not all set to %x!\n", pattern[i]);
				i915_gem_object_unpin_map(obj);
				goto out_dma_map;
			}
		}

		i915_gem_object_unpin_map(obj);
	}

	err = 0;
out_dma_map:
	dma_buf_vunmap_unlocked(dmabuf, &map);
out_obj:
	i915_gem_object_put(obj);
out_dmabuf:
	dma_buf_put(dmabuf);
	return err;
}

static int igt_dmabuf_import_ownership(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	struct dma_buf *dmabuf;
	struct iosys_map map;
	void *ptr;
	int err;

	dmabuf = mock_dmabuf(1);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	err = dma_buf_vmap_unlocked(dmabuf, &map);
	ptr = err ? NULL : map.vaddr;
	if (!ptr) {
		pr_err("dma_buf_vmap failed\n");
		err = -ENOMEM;
		goto err_dmabuf;
	}

	memset(ptr, 0xc5, PAGE_SIZE);
	dma_buf_vunmap_unlocked(dmabuf, &map);

	obj = to_intel_bo(i915_gem_prime_import(&i915->drm, dmabuf));
	if (IS_ERR(obj)) {
		pr_err("i915_gem_prime_import failed with err=%d\n",
		       (int)PTR_ERR(obj));
		err = PTR_ERR(obj);
		goto err_dmabuf;
	}

	dma_buf_put(dmabuf);

	err = i915_gem_object_pin_pages_unlocked(obj);
	if (err) {
		pr_err("i915_gem_object_pin_pages failed with err=%d\n", err);
		goto out_obj;
	}

	err = 0;
	i915_gem_object_unpin_pages(obj);
out_obj:
	i915_gem_object_put(obj);
	return err;

err_dmabuf:
	dma_buf_put(dmabuf);
	return err;
}

static int igt_dmabuf_export_vmap(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	struct dma_buf *dmabuf;
	struct iosys_map map;
	void *ptr;
	int err;

	obj = i915_gem_object_create_shmem(i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	dmabuf = i915_gem_prime_export(&obj->base, 0);
	if (IS_ERR(dmabuf)) {
		pr_err("i915_gem_prime_export failed with err=%d\n",
		       (int)PTR_ERR(dmabuf));
		err = PTR_ERR(dmabuf);
		goto err_obj;
	}
	i915_gem_object_put(obj);

	err = dma_buf_vmap_unlocked(dmabuf, &map);
	ptr = err ? NULL : map.vaddr;
	if (!ptr) {
		pr_err("dma_buf_vmap failed\n");
		err = -ENOMEM;
		goto out;
	}

	if (memchr_inv(ptr, 0, dmabuf->size)) {
		pr_err("Exported object not initialiased to zero!\n");
		err = -EINVAL;
		goto out;
	}

	memset(ptr, 0xc5, dmabuf->size);

	err = 0;
	dma_buf_vunmap_unlocked(dmabuf, &map);
out:
	dma_buf_put(dmabuf);
	return err;

err_obj:
	i915_gem_object_put(obj);
	return err;
}

int i915_gem_dmabuf_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_dmabuf_export),
		SUBTEST(igt_dmabuf_import_self),
		SUBTEST(igt_dmabuf_import),
		SUBTEST(igt_dmabuf_import_ownership),
		SUBTEST(igt_dmabuf_export_vmap),
	};
	struct drm_i915_private *i915;
	int err;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	err = i915_subtests(tests, i915);

	mock_destroy_device(i915);
	return err;
}

int i915_gem_dmabuf_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_dmabuf_export),
		SUBTEST(igt_dmabuf_import_same_driver_lmem),
		SUBTEST(igt_dmabuf_import_same_driver_smem),
		SUBTEST(igt_dmabuf_import_same_driver_lmem_smem),
	};

	return i915_live_subtests(tests, i915);
}
