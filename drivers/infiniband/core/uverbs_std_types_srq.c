// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2020, Mellanox Technologies inc.  All rights reserved.
 */

#include <rdma/uverbs_std_types.h>
#include "rdma_core.h"
#include "uverbs.h"

static int uverbs_free_srq(struct ib_uobject *uobject,
		    enum rdma_remove_reason why,
		    struct uverbs_attr_bundle *attrs)
{
	struct ib_srq *srq = uobject->object;
	struct ib_uevent_object *uevent =
		container_of(uobject, struct ib_uevent_object, uobject);
	enum ib_srq_type srq_type = srq->srq_type;
	int ret;

	ret = ib_destroy_srq_user(srq, &attrs->driver_udata);
	if (ret)
		return ret;

	if (srq_type == IB_SRQT_XRC) {
		struct ib_usrq_object *us =
			container_of(uobject, struct ib_usrq_object,
				     uevent.uobject);

		atomic_dec(&us->uxrcd->refcnt);
	}

	ib_uverbs_release_uevent(uevent);
	return 0;
}

static int UVERBS_HANDLER(UVERBS_METHOD_SRQ_CREATE)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_usrq_object *obj = container_of(
		uverbs_attr_get_uobject(attrs, UVERBS_ATTR_CREATE_SRQ_HANDLE),
		typeof(*obj), uevent.uobject);
	struct ib_pd *pd =
		uverbs_attr_get_obj(attrs, UVERBS_ATTR_CREATE_SRQ_PD_HANDLE);
	struct ib_srq_init_attr attr = {};
	struct ib_uobject *xrcd_uobj;
	struct ib_srq *srq;
	u64 user_handle;
	int ret;

	ret = uverbs_copy_from(&attr.attr.max_sge, attrs,
			       UVERBS_ATTR_CREATE_SRQ_MAX_SGE);
	if (!ret)
		ret = uverbs_copy_from(&attr.attr.max_wr, attrs,
				       UVERBS_ATTR_CREATE_SRQ_MAX_WR);
	if (!ret)
		ret = uverbs_copy_from(&attr.attr.srq_limit, attrs,
				       UVERBS_ATTR_CREATE_SRQ_LIMIT);
	if (!ret)
		ret = uverbs_copy_from(&user_handle, attrs,
				       UVERBS_ATTR_CREATE_SRQ_USER_HANDLE);
	if (!ret)
		ret = uverbs_get_const(&attr.srq_type, attrs,
				       UVERBS_ATTR_CREATE_SRQ_TYPE);
	if (ret)
		return ret;

	if (ib_srq_has_cq(attr.srq_type)) {
		attr.ext.cq = uverbs_attr_get_obj(attrs,
					UVERBS_ATTR_CREATE_SRQ_CQ_HANDLE);
		if (IS_ERR(attr.ext.cq))
			return PTR_ERR(attr.ext.cq);
	}

	switch (attr.srq_type) {
	case IB_UVERBS_SRQT_XRC:
		xrcd_uobj = uverbs_attr_get_uobject(attrs,
					UVERBS_ATTR_CREATE_SRQ_XRCD_HANDLE);
		if (IS_ERR(xrcd_uobj))
			return PTR_ERR(xrcd_uobj);

		attr.ext.xrc.xrcd = (struct ib_xrcd *)xrcd_uobj->object;
		if (!attr.ext.xrc.xrcd)
			return -EINVAL;
		obj->uxrcd = container_of(xrcd_uobj, struct ib_uxrcd_object,
					  uobject);
		atomic_inc(&obj->uxrcd->refcnt);
		break;
	case IB_UVERBS_SRQT_TM:
		ret = uverbs_copy_from(&attr.ext.tag_matching.max_num_tags,
				       attrs,
				       UVERBS_ATTR_CREATE_SRQ_MAX_NUM_TAGS);
		if (ret)
			return ret;
		break;
	case IB_UVERBS_SRQT_BASIC:
		break;
	default:
		return -EINVAL;
	}

	obj->uevent.event_file = ib_uverbs_get_async_event(attrs,
					UVERBS_ATTR_CREATE_SRQ_EVENT_FD);
	INIT_LIST_HEAD(&obj->uevent.event_list);
	attr.event_handler = ib_uverbs_srq_event_handler;
	obj->uevent.uobject.user_handle = user_handle;

	srq = ib_create_srq_user(pd, &attr, obj, &attrs->driver_udata);
	if (IS_ERR(srq)) {
		ret = PTR_ERR(srq);
		goto err;
	}

	obj->uevent.uobject.object = srq;
	uverbs_finalize_uobj_create(attrs, UVERBS_ATTR_CREATE_SRQ_HANDLE);

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_CREATE_SRQ_RESP_MAX_WR,
			     &attr.attr.max_wr,
			     sizeof(attr.attr.max_wr));
	if (ret)
		return ret;

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_CREATE_SRQ_RESP_MAX_SGE,
			     &attr.attr.max_sge,
			     sizeof(attr.attr.max_sge));
	if (ret)
		return ret;

	if (attr.srq_type == IB_SRQT_XRC) {
		ret = uverbs_copy_to(attrs,
				     UVERBS_ATTR_CREATE_SRQ_RESP_SRQ_NUM,
				     &srq->ext.xrc.srq_num,
				     sizeof(srq->ext.xrc.srq_num));
		if (ret)
			return ret;
	}

	return 0;
err:
	if (obj->uevent.event_file)
		uverbs_uobject_put(&obj->uevent.event_file->uobj);
	if (attr.srq_type == IB_SRQT_XRC)
		atomic_dec(&obj->uxrcd->refcnt);
	return ret;
};

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_SRQ_CREATE,
	UVERBS_ATTR_IDR(UVERBS_ATTR_CREATE_SRQ_HANDLE,
			UVERBS_OBJECT_SRQ,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_IDR(UVERBS_ATTR_CREATE_SRQ_PD_HANDLE,
			UVERBS_OBJECT_PD,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_CONST_IN(UVERBS_ATTR_CREATE_SRQ_TYPE,
			     enum ib_uverbs_srq_type,
			     UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_CREATE_SRQ_USER_HANDLE,
			   UVERBS_ATTR_TYPE(u64),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_CREATE_SRQ_MAX_WR,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_CREATE_SRQ_MAX_SGE,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_CREATE_SRQ_LIMIT,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY),
	UVERBS_ATTR_IDR(UVERBS_ATTR_CREATE_SRQ_XRCD_HANDLE,
			UVERBS_OBJECT_XRCD,
			UVERBS_ACCESS_READ,
			UA_OPTIONAL),
	UVERBS_ATTR_IDR(UVERBS_ATTR_CREATE_SRQ_CQ_HANDLE,
			UVERBS_OBJECT_CQ,
			UVERBS_ACCESS_READ,
			UA_OPTIONAL),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_CREATE_SRQ_MAX_NUM_TAGS,
			   UVERBS_ATTR_TYPE(u32),
			   UA_OPTIONAL),
	UVERBS_ATTR_FD(UVERBS_ATTR_CREATE_SRQ_EVENT_FD,
		       UVERBS_OBJECT_ASYNC_EVENT,
		       UVERBS_ACCESS_READ,
		       UA_OPTIONAL),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_CREATE_SRQ_RESP_MAX_WR,
			    UVERBS_ATTR_TYPE(u32),
			    UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_CREATE_SRQ_RESP_MAX_SGE,
			    UVERBS_ATTR_TYPE(u32),
			    UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_CREATE_SRQ_RESP_SRQ_NUM,
			   UVERBS_ATTR_TYPE(u32),
			   UA_OPTIONAL),
	UVERBS_ATTR_UHW());

static int UVERBS_HANDLER(UVERBS_METHOD_SRQ_DESTROY)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj =
		uverbs_attr_get_uobject(attrs, UVERBS_ATTR_DESTROY_SRQ_HANDLE);
	struct ib_usrq_object *obj =
		container_of(uobj, struct ib_usrq_object, uevent.uobject);
	struct ib_uverbs_destroy_srq_resp resp = {
		.events_reported = obj->uevent.events_reported
	};

	return uverbs_copy_to(attrs, UVERBS_ATTR_DESTROY_SRQ_RESP, &resp,
			      sizeof(resp));
}

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_SRQ_DESTROY,
	UVERBS_ATTR_IDR(UVERBS_ATTR_DESTROY_SRQ_HANDLE,
			UVERBS_OBJECT_SRQ,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_DESTROY_SRQ_RESP,
			    UVERBS_ATTR_TYPE(struct ib_uverbs_destroy_srq_resp),
			    UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_SRQ,
	UVERBS_TYPE_ALLOC_IDR_SZ(sizeof(struct ib_usrq_object),
				 uverbs_free_srq),
	&UVERBS_METHOD(UVERBS_METHOD_SRQ_CREATE),
	&UVERBS_METHOD(UVERBS_METHOD_SRQ_DESTROY)
);

const struct uapi_definition uverbs_def_obj_srq[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(UVERBS_OBJECT_SRQ,
				      UAPI_DEF_OBJ_NEEDS_FN(destroy_srq)),
	{}
};
