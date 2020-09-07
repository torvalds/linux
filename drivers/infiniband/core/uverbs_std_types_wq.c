// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2020, Mellanox Technologies inc.  All rights reserved.
 */

#include <rdma/uverbs_std_types.h>
#include "rdma_core.h"
#include "uverbs.h"

static int uverbs_free_wq(struct ib_uobject *uobject,
			  enum rdma_remove_reason why,
			  struct uverbs_attr_bundle *attrs)
{
	struct ib_wq *wq = uobject->object;
	struct ib_uwq_object *uwq =
		container_of(uobject, struct ib_uwq_object, uevent.uobject);
	int ret;

	ret = ib_destroy_wq_user(wq, &attrs->driver_udata);
	if (ib_is_destroy_retryable(ret, why, uobject))
		return ret;

	ib_uverbs_release_uevent(&uwq->uevent);
	return ret;
}

static int UVERBS_HANDLER(UVERBS_METHOD_WQ_CREATE)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uwq_object *obj = container_of(
		uverbs_attr_get_uobject(attrs, UVERBS_ATTR_CREATE_WQ_HANDLE),
		typeof(*obj), uevent.uobject);
	struct ib_pd *pd =
		uverbs_attr_get_obj(attrs, UVERBS_ATTR_CREATE_WQ_PD_HANDLE);
	struct ib_cq *cq =
		uverbs_attr_get_obj(attrs, UVERBS_ATTR_CREATE_WQ_CQ_HANDLE);
	struct ib_wq_init_attr wq_init_attr = {};
	struct ib_wq *wq;
	u64 user_handle;
	int ret;

	ret = uverbs_get_flags32(&wq_init_attr.create_flags, attrs,
				 UVERBS_ATTR_CREATE_WQ_FLAGS,
				 IB_UVERBS_WQ_FLAGS_CVLAN_STRIPPING |
				 IB_UVERBS_WQ_FLAGS_SCATTER_FCS |
				 IB_UVERBS_WQ_FLAGS_DELAY_DROP |
				 IB_UVERBS_WQ_FLAGS_PCI_WRITE_END_PADDING);
	if (!ret)
		ret = uverbs_copy_from(&wq_init_attr.max_sge, attrs,
			       UVERBS_ATTR_CREATE_WQ_MAX_SGE);
	if (!ret)
		ret = uverbs_copy_from(&wq_init_attr.max_wr, attrs,
				       UVERBS_ATTR_CREATE_WQ_MAX_WR);
	if (!ret)
		ret = uverbs_copy_from(&user_handle, attrs,
				       UVERBS_ATTR_CREATE_WQ_USER_HANDLE);
	if (!ret)
		ret = uverbs_get_const(&wq_init_attr.wq_type, attrs,
				       UVERBS_ATTR_CREATE_WQ_TYPE);
	if (ret)
		return ret;

	if (wq_init_attr.wq_type != IB_WQT_RQ)
		return -EINVAL;

	obj->uevent.event_file = ib_uverbs_get_async_event(attrs,
					UVERBS_ATTR_CREATE_WQ_EVENT_FD);
	obj->uevent.uobject.user_handle = user_handle;
	INIT_LIST_HEAD(&obj->uevent.event_list);
	wq_init_attr.event_handler = ib_uverbs_wq_event_handler;
	wq_init_attr.wq_context = attrs->ufile;
	wq_init_attr.cq = cq;

	wq = pd->device->ops.create_wq(pd, &wq_init_attr, &attrs->driver_udata);
	if (IS_ERR(wq)) {
		ret = PTR_ERR(wq);
		goto err;
	}

	obj->uevent.uobject.object = wq;
	wq->wq_type = wq_init_attr.wq_type;
	wq->cq = cq;
	wq->pd = pd;
	wq->device = pd->device;
	wq->wq_context = wq_init_attr.wq_context;
	atomic_set(&wq->usecnt, 0);
	atomic_inc(&pd->usecnt);
	atomic_inc(&cq->usecnt);
	wq->uobject = obj;
	uverbs_finalize_uobj_create(attrs, UVERBS_ATTR_CREATE_WQ_HANDLE);

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_CREATE_WQ_RESP_MAX_WR,
			     &wq_init_attr.max_wr,
			     sizeof(wq_init_attr.max_wr));
	if (ret)
		return ret;

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_CREATE_WQ_RESP_MAX_SGE,
			     &wq_init_attr.max_sge,
			     sizeof(wq_init_attr.max_sge));
	if (ret)
		return ret;

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_CREATE_WQ_RESP_WQ_NUM,
			     &wq->wq_num,
			     sizeof(wq->wq_num));
	return ret;

err:
	if (obj->uevent.event_file)
		uverbs_uobject_put(&obj->uevent.event_file->uobj);
	return ret;
};

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_WQ_CREATE,
	UVERBS_ATTR_IDR(UVERBS_ATTR_CREATE_WQ_HANDLE,
			UVERBS_OBJECT_WQ,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_IDR(UVERBS_ATTR_CREATE_WQ_PD_HANDLE,
			UVERBS_OBJECT_PD,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_CONST_IN(UVERBS_ATTR_CREATE_WQ_TYPE,
			     enum ib_wq_type,
			     UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_CREATE_WQ_USER_HANDLE,
			   UVERBS_ATTR_TYPE(u64),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_CREATE_WQ_MAX_WR,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_CREATE_WQ_MAX_SGE,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY),
	UVERBS_ATTR_FLAGS_IN(UVERBS_ATTR_CREATE_WQ_FLAGS,
			     enum ib_uverbs_wq_flags,
			     UA_MANDATORY),
	UVERBS_ATTR_IDR(UVERBS_ATTR_CREATE_WQ_CQ_HANDLE,
			UVERBS_OBJECT_CQ,
			UVERBS_ACCESS_READ,
			UA_OPTIONAL),
	UVERBS_ATTR_FD(UVERBS_ATTR_CREATE_WQ_EVENT_FD,
		       UVERBS_OBJECT_ASYNC_EVENT,
		       UVERBS_ACCESS_READ,
		       UA_OPTIONAL),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_CREATE_WQ_RESP_MAX_WR,
			    UVERBS_ATTR_TYPE(u32),
			    UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_CREATE_WQ_RESP_MAX_SGE,
			    UVERBS_ATTR_TYPE(u32),
			    UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_CREATE_WQ_RESP_WQ_NUM,
			   UVERBS_ATTR_TYPE(u32),
			   UA_OPTIONAL),
	UVERBS_ATTR_UHW());

static int UVERBS_HANDLER(UVERBS_METHOD_WQ_DESTROY)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj =
		uverbs_attr_get_uobject(attrs, UVERBS_ATTR_DESTROY_WQ_HANDLE);
	struct ib_uwq_object *obj =
		container_of(uobj, struct ib_uwq_object, uevent.uobject);

	return uverbs_copy_to(attrs, UVERBS_ATTR_DESTROY_WQ_RESP,
			      &obj->uevent.events_reported,
			      sizeof(obj->uevent.events_reported));
}

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_WQ_DESTROY,
	UVERBS_ATTR_IDR(UVERBS_ATTR_DESTROY_WQ_HANDLE,
			UVERBS_OBJECT_WQ,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_DESTROY_WQ_RESP,
			    UVERBS_ATTR_TYPE(u32),
			    UA_MANDATORY));


DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_WQ,
	UVERBS_TYPE_ALLOC_IDR_SZ(sizeof(struct ib_uwq_object), uverbs_free_wq),
	&UVERBS_METHOD(UVERBS_METHOD_WQ_CREATE),
	&UVERBS_METHOD(UVERBS_METHOD_WQ_DESTROY)
);

const struct uapi_definition uverbs_def_obj_wq[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(UVERBS_OBJECT_WQ,
				      UAPI_DEF_OBJ_NEEDS_FN(destroy_wq)),
	{}
};
