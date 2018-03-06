/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_query.h"
#include <uapi/drm/i915_drm.h>

static int (* const i915_query_funcs[])(struct drm_i915_private *dev_priv,
					struct drm_i915_query_item *query_item) = {
};

int i915_query_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_query *args = data;
	struct drm_i915_query_item __user *user_item_ptr =
		u64_to_user_ptr(args->items_ptr);
	u32 i;

	if (args->flags != 0)
		return -EINVAL;

	for (i = 0; i < args->num_items; i++, user_item_ptr++) {
		struct drm_i915_query_item item;
		u64 func_idx;
		int ret;

		if (copy_from_user(&item, user_item_ptr, sizeof(item)))
			return -EFAULT;

		if (item.query_id == 0)
			return -EINVAL;

		func_idx = item.query_id - 1;

		if (func_idx < ARRAY_SIZE(i915_query_funcs))
			ret = i915_query_funcs[func_idx](dev_priv, &item);
		else
			ret = -EINVAL;

		/* Only write the length back to userspace if they differ. */
		if (ret != item.length && put_user(ret, &user_item_ptr->length))
			return -EFAULT;
	}

	return 0;
}
