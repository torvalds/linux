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

int i915_gem_gtt_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_ppgtt_alloc),
	};

	return i915_subtests(tests, i915);
}
