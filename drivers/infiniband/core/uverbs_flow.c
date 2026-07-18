// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
#include "uverbs.h"

struct ib_uflow_resources *flow_resources_alloc(size_t num_specs)
{
	struct ib_uflow_resources *resources;

	resources = kzalloc_obj(*resources);

	if (!resources)
		return NULL;

	if (!num_specs)
		goto out;

	resources->counters =
		kzalloc_objs(*resources->counters, num_specs);
	resources->collection =
		kzalloc_objs(*resources->collection, num_specs);

	if (!resources->counters || !resources->collection)
		goto err;

out:
	resources->max = num_specs;
	return resources;

err:
	kfree(resources->counters);
	kfree(resources);

	return NULL;
}
EXPORT_SYMBOL(flow_resources_alloc);

void ib_uverbs_flow_resources_free(struct ib_uflow_resources *uflow_res)
{
	unsigned int i;

	if (!uflow_res)
		return;

	for (i = 0; i < uflow_res->collection_num; i++)
		atomic_dec(&uflow_res->collection[i]->usecnt);

	for (i = 0; i < uflow_res->counters_num; i++)
		atomic_dec(&uflow_res->counters[i]->usecnt);

	kfree(uflow_res->collection);
	kfree(uflow_res->counters);
	kfree(uflow_res);
}
EXPORT_SYMBOL(ib_uverbs_flow_resources_free);

void flow_resources_add(struct ib_uflow_resources *uflow_res,
			enum ib_flow_spec_type type,
			void *ibobj)
{
	WARN_ON(uflow_res->num >= uflow_res->max);

	switch (type) {
	case IB_FLOW_SPEC_ACTION_HANDLE:
		atomic_inc(&((struct ib_flow_action *)ibobj)->usecnt);
		uflow_res->collection[uflow_res->collection_num++] =
			(struct ib_flow_action *)ibobj;
		break;
	case IB_FLOW_SPEC_ACTION_COUNT:
		atomic_inc(&((struct ib_counters *)ibobj)->usecnt);
		uflow_res->counters[uflow_res->counters_num++] =
			(struct ib_counters *)ibobj;
		break;
	default:
		WARN_ON(1);
	}

	uflow_res->num++;
}
EXPORT_SYMBOL(flow_resources_add);
