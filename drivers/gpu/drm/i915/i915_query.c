/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include <linux/nospec.h>

#include "i915_drv.h"
#include "i915_query.h"
#include <uapi/drm/i915_drm.h>

static int query_topology_info(struct drm_i915_private *dev_priv,
			       struct drm_i915_query_item *query_item)
{
	const struct sseu_dev_info *sseu = &RUNTIME_INFO(dev_priv)->sseu;
	struct drm_i915_query_topology_info topo;
	u32 slice_length, subslice_length, eu_length, total_length;

	if (query_item->flags != 0)
		return -EINVAL;

	if (sseu->max_slices == 0)
		return -ENODEV;

	BUILD_BUG_ON(sizeof(u8) != sizeof(sseu->slice_mask));

	slice_length = sizeof(sseu->slice_mask);
	subslice_length = sseu->max_slices *
		DIV_ROUND_UP(sseu->max_subslices, BITS_PER_BYTE);
	eu_length = sseu->max_slices * sseu->max_subslices *
		DIV_ROUND_UP(sseu->max_eus_per_subslice, BITS_PER_BYTE);

	total_length = sizeof(topo) + slice_length + subslice_length + eu_length;

	if (query_item->length == 0)
		return total_length;

	if (query_item->length < total_length)
		return -EINVAL;

	if (copy_from_user(&topo, u64_to_user_ptr(query_item->data_ptr),
			   sizeof(topo)))
		return -EFAULT;

	if (topo.flags != 0)
		return -EINVAL;

	if (!access_ok(u64_to_user_ptr(query_item->data_ptr),
		       total_length))
		return -EFAULT;

	memset(&topo, 0, sizeof(topo));
	topo.max_slices = sseu->max_slices;
	topo.max_subslices = sseu->max_subslices;
	topo.max_eus_per_subslice = sseu->max_eus_per_subslice;

	topo.subslice_offset = slice_length;
	topo.subslice_stride = DIV_ROUND_UP(sseu->max_subslices, BITS_PER_BYTE);
	topo.eu_offset = slice_length + subslice_length;
	topo.eu_stride =
		DIV_ROUND_UP(sseu->max_eus_per_subslice, BITS_PER_BYTE);

	if (__copy_to_user(u64_to_user_ptr(query_item->data_ptr),
			   &topo, sizeof(topo)))
		return -EFAULT;

	if (__copy_to_user(u64_to_user_ptr(query_item->data_ptr + sizeof(topo)),
			   &sseu->slice_mask, slice_length))
		return -EFAULT;

	if (__copy_to_user(u64_to_user_ptr(query_item->data_ptr +
					   sizeof(topo) + slice_length),
			   sseu->subslice_mask, subslice_length))
		return -EFAULT;

	if (__copy_to_user(u64_to_user_ptr(query_item->data_ptr +
					   sizeof(topo) +
					   slice_length + subslice_length),
			   sseu->eu_mask, eu_length))
		return -EFAULT;

	return total_length;
}

static int (* const i915_query_funcs[])(struct drm_i915_private *dev_priv,
					struct drm_i915_query_item *query_item) = {
	query_topology_info,
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
		unsigned long func_idx;
		int ret;

		if (copy_from_user(&item, user_item_ptr, sizeof(item)))
			return -EFAULT;

		if (item.query_id == 0)
			return -EINVAL;

		if (overflows_type(item.query_id - 1, unsigned long))
			return -EINVAL;

		func_idx = item.query_id - 1;

		ret = -EINVAL;
		if (func_idx < ARRAY_SIZE(i915_query_funcs)) {
			func_idx = array_index_nospec(func_idx,
						      ARRAY_SIZE(i915_query_funcs));
			ret = i915_query_funcs[func_idx](dev_priv, &item);
		}

		/* Only write the length back to userspace if they differ. */
		if (ret != item.length && put_user(ret, &user_item_ptr->length))
			return -EFAULT;
	}

	return 0;
}
