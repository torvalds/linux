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

#include "mock_gem_device.h"
#include "mock_dmabuf.h"

static int igt_dmabuf_export(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	struct dma_buf *dmabuf;

	obj = i915_gem_object_create(i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	dmabuf = i915_gem_prime_export(&i915->drm, &obj->base, 0);
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
	struct drm_i915_gem_object *obj;
	struct drm_gem_object *import;
	struct dma_buf *dmabuf;
	int err;

	obj = i915_gem_object_create(i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	dmabuf = i915_gem_prime_export(&i915->drm, &obj->base, 0);
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

	if (import != &obj->base) {
		pr_err("i915_gem_prime_import created a new object!\n");
		err = -EINVAL;
		goto out_import;
	}

	err = 0;
out_import:
	i915_gem_object_put(to_intel_bo(import));
out_dmabuf:
	dma_buf_put(dmabuf);
out:
	i915_gem_object_put(obj);
	return err;
}

static int igt_dmabuf_import(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	struct dma_buf *dmabuf;
	void *obj_map, *dma_map;
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

	dma_map = dma_buf_vmap(dmabuf);
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
	dma_buf_vunmap(dmabuf, dma_map);
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
	void *ptr;
	int err;

	dmabuf = mock_dmabuf(1);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	ptr = dma_buf_vmap(dmabuf);
	if (!ptr) {
		pr_err("dma_buf_vmap failed\n");
		err = -ENOMEM;
		goto err_dmabuf;
	}

	memset(ptr, 0xc5, PAGE_SIZE);
	dma_buf_vunmap(dmabuf, ptr);

	obj = to_intel_bo(i915_gem_prime_import(&i915->drm, dmabuf));
	if (IS_ERR(obj)) {
		pr_err("i915_gem_prime_import failed with err=%d\n",
		       (int)PTR_ERR(obj));
		err = PTR_ERR(obj);
		goto err_dmabuf;
	}

	dma_buf_put(dmabuf);

	err = i915_gem_object_pin_pages(obj);
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
	void *ptr;
	int err;

	obj = i915_gem_object_create(i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	dmabuf = i915_gem_prime_export(&i915->drm, &obj->base, 0);
	if (IS_ERR(dmabuf)) {
		pr_err("i915_gem_prime_export failed with err=%d\n",
		       (int)PTR_ERR(dmabuf));
		err = PTR_ERR(dmabuf);
		goto err_obj;
	}
	i915_gem_object_put(obj);

	ptr = dma_buf_vmap(dmabuf);
	if (IS_ERR(ptr)) {
		err = PTR_ERR(ptr);
		pr_err("dma_buf_vmap failed with err=%d\n", err);
		goto out;
	}

	if (memchr_inv(ptr, 0, dmabuf->size)) {
		pr_err("Exported object not initialiased to zero!\n");
		err = -EINVAL;
		goto out;
	}

	memset(ptr, 0xc5, dmabuf->size);

	err = 0;
	dma_buf_vunmap(dmabuf, ptr);
out:
	dma_buf_put(dmabuf);
	return err;

err_obj:
	i915_gem_object_put(obj);
	return err;
}

static int igt_dmabuf_export_kmap(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	struct dma_buf *dmabuf;
	void *ptr;
	int err;

	obj = i915_gem_object_create(i915, 2*PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	dmabuf = i915_gem_prime_export(&i915->drm, &obj->base, 0);
	i915_gem_object_put(obj);
	if (IS_ERR(dmabuf)) {
		err = PTR_ERR(dmabuf);
		pr_err("i915_gem_prime_export failed with err=%d\n", err);
		return err;
	}

	ptr = dma_buf_kmap(dmabuf, 0);
	if (!ptr) {
		pr_err("dma_buf_kmap failed\n");
		err = -ENOMEM;
		goto err;
	}

	if (memchr_inv(ptr, 0, PAGE_SIZE)) {
		dma_buf_kunmap(dmabuf, 0, ptr);
		pr_err("Exported page[0] not initialiased to zero!\n");
		err = -EINVAL;
		goto err;
	}

	memset(ptr, 0xc5, PAGE_SIZE);
	dma_buf_kunmap(dmabuf, 0, ptr);

	ptr = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(ptr)) {
		err = PTR_ERR(ptr);
		pr_err("i915_gem_object_pin_map failed with err=%d\n", err);
		goto err;
	}
	memset(ptr + PAGE_SIZE, 0xaa, PAGE_SIZE);
	i915_gem_object_unpin_map(obj);

	ptr = dma_buf_kmap(dmabuf, 1);
	if (!ptr) {
		pr_err("dma_buf_kmap failed\n");
		err = -ENOMEM;
		goto err;
	}

	if (memchr_inv(ptr, 0xaa, PAGE_SIZE)) {
		dma_buf_kunmap(dmabuf, 1, ptr);
		pr_err("Exported page[1] not set to 0xaa!\n");
		err = -EINVAL;
		goto err;
	}

	memset(ptr, 0xc5, PAGE_SIZE);
	dma_buf_kunmap(dmabuf, 1, ptr);

	ptr = dma_buf_kmap(dmabuf, 0);
	if (!ptr) {
		pr_err("dma_buf_kmap failed\n");
		err = -ENOMEM;
		goto err;
	}
	if (memchr_inv(ptr, 0xc5, PAGE_SIZE)) {
		dma_buf_kunmap(dmabuf, 0, ptr);
		pr_err("Exported page[0] did not retain 0xc5!\n");
		err = -EINVAL;
		goto err;
	}
	dma_buf_kunmap(dmabuf, 0, ptr);

	ptr = dma_buf_kmap(dmabuf, 2);
	if (ptr) {
		pr_err("Erroneously kmapped beyond the end of the object!\n");
		dma_buf_kunmap(dmabuf, 2, ptr);
		err = -EINVAL;
		goto err;
	}

	ptr = dma_buf_kmap(dmabuf, -1);
	if (ptr) {
		pr_err("Erroneously kmapped before the start of the object!\n");
		dma_buf_kunmap(dmabuf, -1, ptr);
		err = -EINVAL;
		goto err;
	}

	err = 0;
err:
	dma_buf_put(dmabuf);
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
		SUBTEST(igt_dmabuf_export_kmap),
	};
	struct drm_i915_private *i915;
	int err;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	err = i915_subtests(tests, i915);

	drm_dev_unref(&i915->drm);
	return err;
}

int i915_gem_dmabuf_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_dmabuf_export),
	};

	return i915_subtests(tests, i915);
}
