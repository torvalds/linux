// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <rdma/uverbs_std_types.h>
#include "rdma_core.h"
#include "uverbs.h"
#include "restrack.h"

static int uverbs_free_comp_cntr(struct ib_uobject *uobject, enum rdma_remove_reason why,
				 struct uverbs_attr_bundle *attrs)
{
	struct ib_comp_cntr *cc = uobject->object;
	int ret;

	if (atomic_read(&cc->usecnt))
		return -EBUSY;

	rdma_restrack_begin_del(&cc->res);
	ret = cc->device->ops.destroy_comp_cntr(cc);
	if (ret) {
		rdma_restrack_abort_del(&cc->res);
		return ret;
	}

	rdma_restrack_commit_del(&cc->res);
	kfree(cc);
	return 0;
}

static int UVERBS_HANDLER(UVERBS_METHOD_COMP_CNTR_CREATE)(struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj = uverbs_attr_get_uobject(attrs,
							  UVERBS_ATTR_CREATE_COMP_CNTR_HANDLE);
	struct ib_device *ib_dev = attrs->context->device;
	struct ib_comp_cntr *cc;
	int ret;

	if (!ib_dev->ops.create_comp_cntr ||
	    !ib_dev->ops.destroy_comp_cntr ||
	    !ib_dev->ops.qp_attach_comp_cntr)
		return -EOPNOTSUPP;

	cc = rdma_zalloc_drv_obj(ib_dev, ib_comp_cntr);
	if (!cc)
		return -ENOMEM;

	cc->device = ib_dev;
	cc->uobject = uobj;

	rdma_restrack_new(&cc->res, RDMA_RESTRACK_COMP_CNTR);
	rdma_restrack_set_name(&cc->res, NULL);

	ret = ib_dev->ops.create_comp_cntr(cc, attrs);
	if (ret)
		goto err_free;

	uobj->object = cc;
	rdma_restrack_add(&cc->res);
	uverbs_finalize_uobj_create(attrs, UVERBS_ATTR_CREATE_COMP_CNTR_HANDLE);
	return 0;

err_free:
	rdma_restrack_put(&cc->res);
	kfree(cc);
	return ret;
}

static int UVERBS_HANDLER(UVERBS_METHOD_COMP_CNTR_MODIFY)(struct uverbs_attr_bundle *attrs)
{
	struct ib_comp_cntr *cc = uverbs_attr_get_obj(attrs, UVERBS_ATTR_MODIFY_COMP_CNTR_HANDLE);
	enum ib_comp_cntr_modify_op op;
	enum ib_comp_cntr_entry entry;
	u64 value;
	int ret;

	if (!cc->device->ops.modify_comp_cntr)
		return -EOPNOTSUPP;

	ret = uverbs_get_const(&entry, attrs, UVERBS_ATTR_MODIFY_COMP_CNTR_ENTRY);
	if (ret)
		return ret;

	ret = uverbs_get_const(&op, attrs, UVERBS_ATTR_MODIFY_COMP_CNTR_OP);
	if (ret)
		return ret;

	ret = uverbs_copy_from(&value, attrs, UVERBS_ATTR_MODIFY_COMP_CNTR_VALUE);
	if (ret)
		return ret;

	return cc->device->ops.modify_comp_cntr(cc, entry, op, value);
}

static int UVERBS_HANDLER(UVERBS_METHOD_COMP_CNTR_READ)(struct uverbs_attr_bundle *attrs)
{
	struct ib_comp_cntr *cc = uverbs_attr_get_obj(attrs, UVERBS_ATTR_READ_COMP_CNTR_HANDLE);
	enum ib_comp_cntr_entry entry;
	u64 value = 0;
	int ret;

	if (!cc->device->ops.read_comp_cntr)
		return -EOPNOTSUPP;

	ret = uverbs_get_const(&entry, attrs, UVERBS_ATTR_READ_COMP_CNTR_ENTRY);
	if (ret)
		return ret;

	ret = cc->device->ops.read_comp_cntr(cc, entry, &value);
	if (ret)
		return ret;

	return uverbs_copy_to(attrs, UVERBS_ATTR_READ_COMP_CNTR_RESP_VALUE, &value, sizeof(value));
}

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_COMP_CNTR_CREATE,
	UVERBS_ATTR_IDR(UVERBS_ATTR_CREATE_COMP_CNTR_HANDLE,
			UVERBS_OBJECT_COMP_CNTR,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(
	UVERBS_METHOD_COMP_CNTR_DESTROY,
	UVERBS_ATTR_IDR(UVERBS_ATTR_DESTROY_COMP_CNTR_HANDLE,
			UVERBS_OBJECT_COMP_CNTR,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_COMP_CNTR_MODIFY,
	UVERBS_ATTR_IDR(UVERBS_ATTR_MODIFY_COMP_CNTR_HANDLE,
			UVERBS_OBJECT_COMP_CNTR,
			UVERBS_ACCESS_WRITE,
			UA_MANDATORY),
	UVERBS_ATTR_CONST_IN(UVERBS_ATTR_MODIFY_COMP_CNTR_ENTRY,
			     enum ib_uverbs_comp_cntr_entry,
			     UA_MANDATORY),
	UVERBS_ATTR_CONST_IN(UVERBS_ATTR_MODIFY_COMP_CNTR_OP,
			     enum ib_uverbs_comp_cntr_modify_op,
			     UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_MODIFY_COMP_CNTR_VALUE,
			   UVERBS_ATTR_TYPE(u64),
			   UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_COMP_CNTR_READ,
	UVERBS_ATTR_IDR(UVERBS_ATTR_READ_COMP_CNTR_HANDLE,
			UVERBS_OBJECT_COMP_CNTR,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_CONST_IN(UVERBS_ATTR_READ_COMP_CNTR_ENTRY,
			     enum ib_uverbs_comp_cntr_entry,
			     UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_READ_COMP_CNTR_RESP_VALUE,
			    UVERBS_ATTR_TYPE(u64),
			    UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_COMP_CNTR,
	UVERBS_TYPE_ALLOC_IDR(uverbs_free_comp_cntr),
	&UVERBS_METHOD(UVERBS_METHOD_COMP_CNTR_CREATE),
	&UVERBS_METHOD(UVERBS_METHOD_COMP_CNTR_DESTROY),
	&UVERBS_METHOD(UVERBS_METHOD_COMP_CNTR_MODIFY),
	&UVERBS_METHOD(UVERBS_METHOD_COMP_CNTR_READ));

const struct uapi_definition uverbs_def_obj_comp_cntr[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(UVERBS_OBJECT_COMP_CNTR,
				      UAPI_DEF_OBJ_NEEDS_FN(destroy_comp_cntr)),
	{}
};
