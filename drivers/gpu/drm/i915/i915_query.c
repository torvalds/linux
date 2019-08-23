/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include <linux/nospec.h>

#include "i915_drv.h"
#include "i915_query.h"
#include <uapi/drm/i915_drm.h>

static int copy_query_item(void *query_hdr, size_t query_sz,
			   u32 total_length,
			   struct drm_i915_query_item *query_item)
{
	if (query_item->length == 0)
		return total_length;

	if (query_item->length < total_length)
		return -EINVAL;

	if (copy_from_user(query_hdr, u64_to_user_ptr(query_item->data_ptr),
			   query_sz))
		return -EFAULT;

	if (!access_ok(u64_to_user_ptr(query_item->data_ptr),
		       total_length))
		return -EFAULT;

	return 0;
}

static int query_topology_info(struct drm_i915_private *dev_priv,
			       struct drm_i915_query_item *query_item)
{
	const struct sseu_dev_info *sseu = &RUNTIME_INFO(dev_priv)->sseu;
	struct drm_i915_query_topology_info topo;
	u32 slice_length, subslice_length, eu_length, total_length;
	int ret;

	if (query_item->flags != 0)
		return -EINVAL;

	if (sseu->max_slices == 0)
		return -ENODEV;

	BUILD_BUG_ON(sizeof(u8) != sizeof(sseu->slice_mask));

	slice_length = sizeof(sseu->slice_mask);
	subslice_length = sseu->max_slices * sseu->ss_stride;
	eu_length = sseu->max_slices * sseu->max_subslices * sseu->eu_stride;
	total_length = sizeof(topo) + slice_length + subslice_length +
		       eu_length;

	ret = copy_query_item(&topo, sizeof(topo), total_length,
			      query_item);
	if (ret != 0)
		return ret;

	if (topo.flags != 0)
		return -EINVAL;

	memset(&topo, 0, sizeof(topo));
	topo.max_slices = sseu->max_slices;
	topo.max_subslices = sseu->max_subslices;
	topo.max_eus_per_subslice = sseu->max_eus_per_subslice;

	topo.subslice_offset = slice_length;
	topo.subslice_stride = sseu->ss_stride;
	topo.eu_offset = slice_length + subslice_length;
	topo.eu_stride = sseu->eu_stride;

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

static int
query_engine_info(struct drm_i915_private *i915,
		  struct drm_i915_query_item *query_item)
{
	struct drm_i915_query_engine_info __user *query_ptr =
				u64_to_user_ptr(query_item->data_ptr);
	struct drm_i915_engine_info __user *info_ptr;
	struct drm_i915_query_engine_info query;
	struct drm_i915_engine_info info = { };
	struct intel_engine_cs *engine;
	int len, ret;

	if (query_item->flags)
		return -EINVAL;

	len = sizeof(struct drm_i915_query_engine_info) +
	      RUNTIME_INFO(i915)->num_engines *
	      sizeof(struct drm_i915_engine_info);

	ret = copy_query_item(&query, sizeof(query), len, query_item);
	if (ret != 0)
		return ret;

	if (query.num_engines || query.rsvd[0] || query.rsvd[1] ||
	    query.rsvd[2])
		return -EINVAL;

	info_ptr = &query_ptr->engines[0];

	for_each_uabi_engine(engine, i915) {
		info.engine.engine_class = engine->uabi_class;
		info.engine.engine_instance = engine->uabi_instance;
		info.capabilities = engine->uabi_capabilities;

		if (__copy_to_user(info_ptr, &info, sizeof(info)))
			return -EFAULT;

		query.num_engines++;
		info_ptr++;
	}

	if (__copy_to_user(query_ptr, &query, sizeof(query)))
		return -EFAULT;

	return len;
}

static int (* const i915_query_funcs[])(struct drm_i915_private *dev_priv,
					struct drm_i915_query_item *query_item) = {
	query_topology_info,
	query_engine_info,
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
