/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include <linux/nospec.h>

#include "i915_drv.h"
#include "i915_perf.h"
#include "i915_query.h"
#include "gt/intel_engine_user.h"
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

	return 0;
}

static int fill_topology_info(const struct sseu_dev_info *sseu,
			      struct drm_i915_query_item *query_item,
			      const u8 *subslice_mask)
{
	struct drm_i915_query_topology_info topo;
	u32 slice_length, subslice_length, eu_length, total_length;
	int ret;

	BUILD_BUG_ON(sizeof(u8) != sizeof(sseu->slice_mask));

	if (sseu->max_slices == 0)
		return -ENODEV;

	slice_length = sizeof(sseu->slice_mask);
	subslice_length = sseu->max_slices * sseu->ss_stride;
	eu_length = sseu->max_slices * sseu->max_subslices * sseu->eu_stride;
	total_length = sizeof(topo) + slice_length + subslice_length +
		       eu_length;

	ret = copy_query_item(&topo, sizeof(topo), total_length, query_item);

	if (ret != 0)
		return ret;

	memset(&topo, 0, sizeof(topo));
	topo.max_slices = sseu->max_slices;
	topo.max_subslices = sseu->max_subslices;
	topo.max_eus_per_subslice = sseu->max_eus_per_subslice;

	topo.subslice_offset = slice_length;
	topo.subslice_stride = sseu->ss_stride;
	topo.eu_offset = slice_length + subslice_length;
	topo.eu_stride = sseu->eu_stride;

	if (copy_to_user(u64_to_user_ptr(query_item->data_ptr),
			 &topo, sizeof(topo)))
		return -EFAULT;

	if (copy_to_user(u64_to_user_ptr(query_item->data_ptr + sizeof(topo)),
			 &sseu->slice_mask, slice_length))
		return -EFAULT;

	if (copy_to_user(u64_to_user_ptr(query_item->data_ptr +
					 sizeof(topo) + slice_length),
			 subslice_mask, subslice_length))
		return -EFAULT;

	if (copy_to_user(u64_to_user_ptr(query_item->data_ptr +
					 sizeof(topo) +
					 slice_length + subslice_length),
			 sseu->eu_mask, eu_length))
		return -EFAULT;

	return total_length;
}

static int query_topology_info(struct drm_i915_private *dev_priv,
			       struct drm_i915_query_item *query_item)
{
	const struct sseu_dev_info *sseu = &to_gt(dev_priv)->info.sseu;

	if (query_item->flags != 0)
		return -EINVAL;

	return fill_topology_info(sseu, query_item, sseu->subslice_mask);
}

static int query_geometry_subslices(struct drm_i915_private *i915,
				    struct drm_i915_query_item *query_item)
{
	const struct sseu_dev_info *sseu;
	struct intel_engine_cs *engine;
	struct i915_engine_class_instance classinstance;

	if (GRAPHICS_VER_FULL(i915) < IP_VER(12, 50))
		return -ENODEV;

	classinstance = *((struct i915_engine_class_instance *)&query_item->flags);

	engine = intel_engine_lookup_user(i915, (u8)classinstance.engine_class,
					  (u8)classinstance.engine_instance);

	if (!engine)
		return -EINVAL;

	if (engine->class != RENDER_CLASS)
		return -EINVAL;

	sseu = &engine->gt->info.sseu;

	return fill_topology_info(sseu, query_item, sseu->geometry_subslice_mask);
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
	unsigned int num_uabi_engines = 0;
	struct intel_engine_cs *engine;
	int len, ret;

	if (query_item->flags)
		return -EINVAL;

	for_each_uabi_engine(engine, i915)
		num_uabi_engines++;

	len = struct_size(query_ptr, engines, num_uabi_engines);

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
		info.flags = I915_ENGINE_INFO_HAS_LOGICAL_INSTANCE;
		info.capabilities = engine->uabi_capabilities;
		info.logical_instance = ilog2(engine->logical_mask);

		if (copy_to_user(info_ptr, &info, sizeof(info)))
			return -EFAULT;

		query.num_engines++;
		info_ptr++;
	}

	if (copy_to_user(query_ptr, &query, sizeof(query)))
		return -EFAULT;

	return len;
}

static int can_copy_perf_config_registers_or_number(u32 user_n_regs,
						    u64 user_regs_ptr,
						    u32 kernel_n_regs)
{
	/*
	 * We'll just put the number of registers, and won't copy the
	 * register.
	 */
	if (user_n_regs == 0)
		return 0;

	if (user_n_regs < kernel_n_regs)
		return -EINVAL;

	return 0;
}

static int copy_perf_config_registers_or_number(const struct i915_oa_reg *kernel_regs,
						u32 kernel_n_regs,
						u64 user_regs_ptr,
						u32 *user_n_regs)
{
	u32 __user *p = u64_to_user_ptr(user_regs_ptr);
	u32 r;

	if (*user_n_regs == 0) {
		*user_n_regs = kernel_n_regs;
		return 0;
	}

	*user_n_regs = kernel_n_regs;

	if (!user_write_access_begin(p, 2 * sizeof(u32) * kernel_n_regs))
		return -EFAULT;

	for (r = 0; r < kernel_n_regs; r++, p += 2) {
		unsafe_put_user(i915_mmio_reg_offset(kernel_regs[r].addr),
				p, Efault);
		unsafe_put_user(kernel_regs[r].value, p + 1, Efault);
	}
	user_write_access_end();
	return 0;
Efault:
	user_write_access_end();
	return -EFAULT;
}

static int query_perf_config_data(struct drm_i915_private *i915,
				  struct drm_i915_query_item *query_item,
				  bool use_uuid)
{
	struct drm_i915_query_perf_config __user *user_query_config_ptr =
		u64_to_user_ptr(query_item->data_ptr);
	struct drm_i915_perf_oa_config __user *user_config_ptr =
		u64_to_user_ptr(query_item->data_ptr +
				sizeof(struct drm_i915_query_perf_config));
	struct drm_i915_perf_oa_config user_config;
	struct i915_perf *perf = &i915->perf;
	struct i915_oa_config *oa_config;
	char uuid[UUID_STRING_LEN + 1];
	u64 config_id;
	u32 flags, total_size;
	int ret;

	if (!perf->i915)
		return -ENODEV;

	total_size =
		sizeof(struct drm_i915_query_perf_config) +
		sizeof(struct drm_i915_perf_oa_config);

	if (query_item->length == 0)
		return total_size;

	if (query_item->length < total_size) {
		DRM_DEBUG("Invalid query config data item size=%u expected=%u\n",
			  query_item->length, total_size);
		return -EINVAL;
	}

	if (get_user(flags, &user_query_config_ptr->flags))
		return -EFAULT;

	if (flags != 0)
		return -EINVAL;

	if (use_uuid) {
		struct i915_oa_config *tmp;
		int id;

		BUILD_BUG_ON(sizeof(user_query_config_ptr->uuid) >= sizeof(uuid));

		memset(&uuid, 0, sizeof(uuid));
		if (copy_from_user(uuid, user_query_config_ptr->uuid,
				     sizeof(user_query_config_ptr->uuid)))
			return -EFAULT;

		oa_config = NULL;
		rcu_read_lock();
		idr_for_each_entry(&perf->metrics_idr, tmp, id) {
			if (!strcmp(tmp->uuid, uuid)) {
				oa_config = i915_oa_config_get(tmp);
				break;
			}
		}
		rcu_read_unlock();
	} else {
		if (get_user(config_id, &user_query_config_ptr->config))
			return -EFAULT;

		oa_config = i915_perf_get_oa_config(perf, config_id);
	}
	if (!oa_config)
		return -ENOENT;

	if (copy_from_user(&user_config, user_config_ptr, sizeof(user_config))) {
		ret = -EFAULT;
		goto out;
	}

	ret = can_copy_perf_config_registers_or_number(user_config.n_boolean_regs,
						       user_config.boolean_regs_ptr,
						       oa_config->b_counter_regs_len);
	if (ret)
		goto out;

	ret = can_copy_perf_config_registers_or_number(user_config.n_flex_regs,
						       user_config.flex_regs_ptr,
						       oa_config->flex_regs_len);
	if (ret)
		goto out;

	ret = can_copy_perf_config_registers_or_number(user_config.n_mux_regs,
						       user_config.mux_regs_ptr,
						       oa_config->mux_regs_len);
	if (ret)
		goto out;

	ret = copy_perf_config_registers_or_number(oa_config->b_counter_regs,
						   oa_config->b_counter_regs_len,
						   user_config.boolean_regs_ptr,
						   &user_config.n_boolean_regs);
	if (ret)
		goto out;

	ret = copy_perf_config_registers_or_number(oa_config->flex_regs,
						   oa_config->flex_regs_len,
						   user_config.flex_regs_ptr,
						   &user_config.n_flex_regs);
	if (ret)
		goto out;

	ret = copy_perf_config_registers_or_number(oa_config->mux_regs,
						   oa_config->mux_regs_len,
						   user_config.mux_regs_ptr,
						   &user_config.n_mux_regs);
	if (ret)
		goto out;

	memcpy(user_config.uuid, oa_config->uuid, sizeof(user_config.uuid));

	if (copy_to_user(user_config_ptr, &user_config, sizeof(user_config))) {
		ret = -EFAULT;
		goto out;
	}

	ret = total_size;

out:
	i915_oa_config_put(oa_config);
	return ret;
}

static size_t sizeof_perf_config_list(size_t count)
{
	return sizeof(struct drm_i915_query_perf_config) + sizeof(u64) * count;
}

static size_t sizeof_perf_metrics(struct i915_perf *perf)
{
	struct i915_oa_config *tmp;
	size_t i;
	int id;

	i = 1;
	rcu_read_lock();
	idr_for_each_entry(&perf->metrics_idr, tmp, id)
		i++;
	rcu_read_unlock();

	return sizeof_perf_config_list(i);
}

static int query_perf_config_list(struct drm_i915_private *i915,
				  struct drm_i915_query_item *query_item)
{
	struct drm_i915_query_perf_config __user *user_query_config_ptr =
		u64_to_user_ptr(query_item->data_ptr);
	struct i915_perf *perf = &i915->perf;
	u64 *oa_config_ids = NULL;
	int alloc, n_configs;
	u32 flags;
	int ret;

	if (!perf->i915)
		return -ENODEV;

	if (query_item->length == 0)
		return sizeof_perf_metrics(perf);

	if (get_user(flags, &user_query_config_ptr->flags))
		return -EFAULT;

	if (flags != 0)
		return -EINVAL;

	n_configs = 1;
	do {
		struct i915_oa_config *tmp;
		u64 *ids;
		int id;

		ids = krealloc(oa_config_ids,
			       n_configs * sizeof(*oa_config_ids),
			       GFP_KERNEL);
		if (!ids)
			return -ENOMEM;

		alloc = fetch_and_zero(&n_configs);

		ids[n_configs++] = 1ull; /* reserved for test_config */
		rcu_read_lock();
		idr_for_each_entry(&perf->metrics_idr, tmp, id) {
			if (n_configs < alloc)
				ids[n_configs] = id;
			n_configs++;
		}
		rcu_read_unlock();

		oa_config_ids = ids;
	} while (n_configs > alloc);

	if (query_item->length < sizeof_perf_config_list(n_configs)) {
		DRM_DEBUG("Invalid query config list item size=%u expected=%zu\n",
			  query_item->length,
			  sizeof_perf_config_list(n_configs));
		kfree(oa_config_ids);
		return -EINVAL;
	}

	if (put_user(n_configs, &user_query_config_ptr->config)) {
		kfree(oa_config_ids);
		return -EFAULT;
	}

	ret = copy_to_user(user_query_config_ptr + 1,
			   oa_config_ids,
			   n_configs * sizeof(*oa_config_ids));
	kfree(oa_config_ids);
	if (ret)
		return -EFAULT;

	return sizeof_perf_config_list(n_configs);
}

static int query_perf_config(struct drm_i915_private *i915,
			     struct drm_i915_query_item *query_item)
{
	switch (query_item->flags) {
	case DRM_I915_QUERY_PERF_CONFIG_LIST:
		return query_perf_config_list(i915, query_item);
	case DRM_I915_QUERY_PERF_CONFIG_DATA_FOR_UUID:
		return query_perf_config_data(i915, query_item, true);
	case DRM_I915_QUERY_PERF_CONFIG_DATA_FOR_ID:
		return query_perf_config_data(i915, query_item, false);
	default:
		return -EINVAL;
	}
}

static int query_memregion_info(struct drm_i915_private *i915,
				struct drm_i915_query_item *query_item)
{
	struct drm_i915_query_memory_regions __user *query_ptr =
		u64_to_user_ptr(query_item->data_ptr);
	struct drm_i915_memory_region_info __user *info_ptr =
		&query_ptr->regions[0];
	struct drm_i915_memory_region_info info = { };
	struct drm_i915_query_memory_regions query;
	struct intel_memory_region *mr;
	u32 total_length;
	int ret, id, i;

	if (query_item->flags != 0)
		return -EINVAL;

	total_length = sizeof(query);
	for_each_memory_region(mr, i915, id) {
		if (mr->private)
			continue;

		total_length += sizeof(info);
	}

	ret = copy_query_item(&query, sizeof(query), total_length, query_item);
	if (ret != 0)
		return ret;

	if (query.num_regions)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(query.rsvd); i++) {
		if (query.rsvd[i])
			return -EINVAL;
	}

	for_each_memory_region(mr, i915, id) {
		if (mr->private)
			continue;

		info.region.memory_class = mr->type;
		info.region.memory_instance = mr->instance;
		info.probed_size = mr->total;
		info.unallocated_size = mr->avail;

		if (__copy_to_user(info_ptr, &info, sizeof(info)))
			return -EFAULT;

		query.num_regions++;
		info_ptr++;
	}

	if (__copy_to_user(query_ptr, &query, sizeof(query)))
		return -EFAULT;

	return total_length;
}

static int query_hwconfig_blob(struct drm_i915_private *i915,
			       struct drm_i915_query_item *query_item)
{
	struct intel_gt *gt = to_gt(i915);
	struct intel_hwconfig *hwconfig = &gt->info.hwconfig;

	if (!hwconfig->size || !hwconfig->ptr)
		return -ENODEV;

	if (query_item->length == 0)
		return hwconfig->size;

	if (query_item->length < hwconfig->size)
		return -EINVAL;

	if (copy_to_user(u64_to_user_ptr(query_item->data_ptr),
			 hwconfig->ptr, hwconfig->size))
		return -EFAULT;

	return hwconfig->size;
}

static int (* const i915_query_funcs[])(struct drm_i915_private *dev_priv,
					struct drm_i915_query_item *query_item) = {
	query_topology_info,
	query_engine_info,
	query_perf_config,
	query_memregion_info,
	query_hwconfig_blob,
	query_geometry_subslices,
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
