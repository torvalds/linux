// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include "rdma_core.h"
#include "uverbs.h"
#include <rdma/uverbs_std_types.h>
#include "restrack.h"

static int uverbs_free_dmah(struct ib_uobject *uobject,
			    enum rdma_remove_reason why,
			    struct uverbs_attr_bundle *attrs)
{
	struct ib_dmah *dmah = uobject->object;
	int ret;

	if (atomic_read(&dmah->usecnt))
		return -EBUSY;

	ret = dmah->device->ops.dealloc_dmah(dmah, attrs);
	if (ret)
		return ret;

	rdma_restrack_del(&dmah->res);
	kfree(dmah);
	return 0;
}

static int UVERBS_HANDLER(UVERBS_METHOD_DMAH_ALLOC)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj =
		uverbs_attr_get(attrs, UVERBS_ATTR_ALLOC_DMAH_HANDLE)
			->obj_attr.uobject;
	struct ib_device *ib_dev = attrs->context->device;
	struct ib_dmah *dmah;
	int ret;

	dmah = rdma_zalloc_drv_obj(ib_dev, ib_dmah);
	if (!dmah)
		return -ENOMEM;

	if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_ALLOC_DMAH_CPU_ID)) {
		ret = uverbs_copy_from(&dmah->cpu_id, attrs,
				       UVERBS_ATTR_ALLOC_DMAH_CPU_ID);
		if (ret)
			goto err;

		if (!cpumask_test_cpu(dmah->cpu_id, current->cpus_ptr)) {
			ret = -EPERM;
			goto err;
		}

		dmah->valid_fields |= BIT(IB_DMAH_CPU_ID_EXISTS);
	}

	if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_ALLOC_DMAH_TPH_MEM_TYPE)) {
		dmah->mem_type = uverbs_attr_get_enum_id(attrs,
					UVERBS_ATTR_ALLOC_DMAH_TPH_MEM_TYPE);
		dmah->valid_fields |= BIT(IB_DMAH_MEM_TYPE_EXISTS);
	}

	if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_ALLOC_DMAH_PH)) {
		ret = uverbs_copy_from(&dmah->ph, attrs,
				       UVERBS_ATTR_ALLOC_DMAH_PH);
		if (ret)
			goto err;

		/* Per PCIe spec 6.2-1.0, only the lowest two bits are applicable */
		if (dmah->ph & 0xFC) {
			ret = -EINVAL;
			goto err;
		}

		dmah->valid_fields |= BIT(IB_DMAH_PH_EXISTS);
	}

	dmah->device = ib_dev;
	dmah->uobject = uobj;
	atomic_set(&dmah->usecnt, 0);

	rdma_restrack_new(&dmah->res, RDMA_RESTRACK_DMAH);
	rdma_restrack_set_name(&dmah->res, NULL);

	ret = ib_dev->ops.alloc_dmah(dmah, attrs);
	if (ret) {
		rdma_restrack_put(&dmah->res);
		goto err;
	}

	uobj->object = dmah;
	rdma_restrack_add(&dmah->res);
	uverbs_finalize_uobj_create(attrs, UVERBS_ATTR_ALLOC_DMAH_HANDLE);
	return 0;
err:
	kfree(dmah);
	return ret;
}

static const struct uverbs_attr_spec uverbs_dmah_mem_type[] = {
	[TPH_MEM_TYPE_VM] = {
		.type = UVERBS_ATTR_TYPE_PTR_IN,
		UVERBS_ATTR_NO_DATA(),
	},
	[TPH_MEM_TYPE_PM] = {
		.type = UVERBS_ATTR_TYPE_PTR_IN,
		UVERBS_ATTR_NO_DATA(),
	},
};

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_DMAH_ALLOC,
	UVERBS_ATTR_IDR(UVERBS_ATTR_ALLOC_DMAH_HANDLE,
			UVERBS_OBJECT_DMAH,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_ALLOC_DMAH_CPU_ID,
			   UVERBS_ATTR_TYPE(u32),
			   UA_OPTIONAL),
	UVERBS_ATTR_ENUM_IN(UVERBS_ATTR_ALLOC_DMAH_TPH_MEM_TYPE,
			    uverbs_dmah_mem_type,
			    UA_OPTIONAL),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_ALLOC_DMAH_PH,
			   UVERBS_ATTR_TYPE(u8),
			   UA_OPTIONAL));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(
	UVERBS_METHOD_DMAH_FREE,
	UVERBS_ATTR_IDR(UVERBS_ATTR_FREE_DMA_HANDLE,
			UVERBS_OBJECT_DMAH,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(UVERBS_OBJECT_DMAH,
			    UVERBS_TYPE_ALLOC_IDR(uverbs_free_dmah),
			    &UVERBS_METHOD(UVERBS_METHOD_DMAH_ALLOC),
			    &UVERBS_METHOD(UVERBS_METHOD_DMAH_FREE));

const struct uapi_definition uverbs_def_obj_dmah[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(UVERBS_OBJECT_DMAH,
				      UAPI_DEF_OBJ_NEEDS_FN(dealloc_dmah),
				      UAPI_DEF_OBJ_NEEDS_FN(alloc_dmah)),
	{}
};
