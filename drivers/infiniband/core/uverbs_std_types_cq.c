/*
 * Copyright (c) 2017, Mellanox Technologies inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <rdma/uverbs_std_types.h>
#include "rdma_core.h"
#include "uverbs.h"
#include "restrack.h"

static int uverbs_free_cq(struct ib_uobject *uobject,
			  enum rdma_remove_reason why,
			  struct uverbs_attr_bundle *attrs)
{
	struct ib_cq *cq = uobject->object;
	struct ib_uverbs_event_queue *ev_queue = cq->cq_context;
	struct ib_ucq_object *ucq =
		container_of(uobject, struct ib_ucq_object, uevent.uobject);
	int ret;

	ret = ib_destroy_cq_user(cq, &attrs->driver_udata);
	if (ret)
		return ret;

	ib_uverbs_release_ucq(
		ev_queue ? container_of(ev_queue,
					struct ib_uverbs_completion_event_file,
					ev_queue) :
			   NULL,
		ucq);
	return 0;
}

static int UVERBS_HANDLER(UVERBS_METHOD_CQ_CREATE)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_ucq_object *obj = container_of(
		uverbs_attr_get_uobject(attrs, UVERBS_ATTR_CREATE_CQ_HANDLE),
		typeof(*obj), uevent.uobject);
	struct ib_uverbs_completion_event_file *ev_file = NULL;
	struct ib_device *ib_dev = attrs->context->device;
	struct ib_umem_dmabuf *umem_dmabuf;
	struct ib_cq_init_attr attr = {};
	struct ib_uobject *ev_file_uobj;
	struct ib_umem *umem = NULL;
	u64 buffer_length;
	u64 buffer_offset;
	struct ib_cq *cq;
	u64 user_handle;
	u64 buffer_va;
	int buffer_fd;
	int ret;

	if ((!ib_dev->ops.create_cq && !ib_dev->ops.create_cq_umem) || !ib_dev->ops.destroy_cq)
		return -EOPNOTSUPP;

	ret = uverbs_copy_from(&attr.comp_vector, attrs,
			       UVERBS_ATTR_CREATE_CQ_COMP_VECTOR);
	if (!ret)
		ret = uverbs_copy_from(&attr.cqe, attrs,
				       UVERBS_ATTR_CREATE_CQ_CQE);
	if (!ret)
		ret = uverbs_copy_from(&user_handle, attrs,
				       UVERBS_ATTR_CREATE_CQ_USER_HANDLE);
	if (ret)
		return ret;

	ret = uverbs_get_flags32(&attr.flags, attrs,
				 UVERBS_ATTR_CREATE_CQ_FLAGS,
				 IB_UVERBS_CQ_FLAGS_TIMESTAMP_COMPLETION |
					 IB_UVERBS_CQ_FLAGS_IGNORE_OVERRUN);
	if (ret)
		return ret;

	ev_file_uobj = uverbs_attr_get_uobject(attrs, UVERBS_ATTR_CREATE_CQ_COMP_CHANNEL);
	if (!IS_ERR(ev_file_uobj)) {
		ev_file = container_of(ev_file_uobj,
				       struct ib_uverbs_completion_event_file,
				       uobj);
		uverbs_uobject_get(ev_file_uobj);
	}

	obj->uevent.event_file = ib_uverbs_get_async_event(
		attrs, UVERBS_ATTR_CREATE_CQ_EVENT_FD);

	if (attr.comp_vector >= attrs->ufile->device->num_comp_vectors) {
		ret = -EINVAL;
		goto err_event_file;
	}

	INIT_LIST_HEAD(&obj->comp_list);
	INIT_LIST_HEAD(&obj->uevent.event_list);

	if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_VA)) {

		ret = uverbs_copy_from(&buffer_va, attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_VA);
		if (ret)
			goto err_event_file;

		ret = uverbs_copy_from(&buffer_length, attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_LENGTH);
		if (ret)
			goto err_event_file;

		if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_FD) ||
		    uverbs_attr_is_valid(attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_OFFSET) ||
		    !ib_dev->ops.create_cq_umem) {
			ret = -EINVAL;
			goto err_event_file;
		}

		umem = ib_umem_get(ib_dev, buffer_va, buffer_length, IB_ACCESS_LOCAL_WRITE);
		if (IS_ERR(umem)) {
			ret = PTR_ERR(umem);
			goto err_event_file;
		}
	} else if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_FD)) {

		ret = uverbs_get_raw_fd(&buffer_fd, attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_FD);
		if (ret)
			goto err_event_file;

		ret = uverbs_copy_from(&buffer_offset, attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_OFFSET);
		if (ret)
			goto err_event_file;

		ret = uverbs_copy_from(&buffer_length, attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_LENGTH);
		if (ret)
			goto err_event_file;

		if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_VA) ||
		    !ib_dev->ops.create_cq_umem) {
			ret = -EINVAL;
			goto err_event_file;
		}

		umem_dmabuf = ib_umem_dmabuf_get_pinned(ib_dev, buffer_offset, buffer_length,
							buffer_fd, IB_ACCESS_LOCAL_WRITE);
		if (IS_ERR(umem_dmabuf)) {
			ret = PTR_ERR(umem_dmabuf);
			goto err_event_file;
		}
		umem = &umem_dmabuf->umem;
	} else if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_OFFSET) ||
		   uverbs_attr_is_valid(attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_LENGTH) ||
		   !ib_dev->ops.create_cq) {
		ret = -EINVAL;
		goto err_event_file;
	}

	cq = rdma_zalloc_drv_obj(ib_dev, ib_cq);
	if (!cq) {
		ret = -ENOMEM;
		ib_umem_release(umem);
		goto err_event_file;
	}

	cq->device        = ib_dev;
	cq->uobject       = obj;
	cq->comp_handler  = ib_uverbs_comp_handler;
	cq->event_handler = ib_uverbs_cq_event_handler;
	cq->cq_context    = ev_file ? &ev_file->ev_queue : NULL;
	atomic_set(&cq->usecnt, 0);

	rdma_restrack_new(&cq->res, RDMA_RESTRACK_CQ);
	rdma_restrack_set_name(&cq->res, NULL);

	ret = umem ? ib_dev->ops.create_cq_umem(cq, &attr, umem, attrs) :
		ib_dev->ops.create_cq(cq, &attr, attrs);
	if (ret)
		goto err_free;

	obj->uevent.uobject.object = cq;
	obj->uevent.uobject.user_handle = user_handle;
	rdma_restrack_add(&cq->res);
	uverbs_finalize_uobj_create(attrs, UVERBS_ATTR_CREATE_CQ_HANDLE);

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_CREATE_CQ_RESP_CQE, &cq->cqe,
			     sizeof(cq->cqe));
	return ret;

err_free:
	ib_umem_release(umem);
	rdma_restrack_put(&cq->res);
	kfree(cq);
err_event_file:
	if (obj->uevent.event_file)
		uverbs_uobject_put(&obj->uevent.event_file->uobj);
	if (ev_file)
		uverbs_uobject_put(ev_file_uobj);
	return ret;
};

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_CQ_CREATE,
	UVERBS_ATTR_IDR(UVERBS_ATTR_CREATE_CQ_HANDLE,
			UVERBS_OBJECT_CQ,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_CREATE_CQ_CQE,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_CREATE_CQ_USER_HANDLE,
			   UVERBS_ATTR_TYPE(u64),
			   UA_MANDATORY),
	UVERBS_ATTR_FD(UVERBS_ATTR_CREATE_CQ_COMP_CHANNEL,
		       UVERBS_OBJECT_COMP_CHANNEL,
		       UVERBS_ACCESS_READ,
		       UA_OPTIONAL),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_CREATE_CQ_COMP_VECTOR,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY),
	UVERBS_ATTR_FLAGS_IN(UVERBS_ATTR_CREATE_CQ_FLAGS,
			     enum ib_uverbs_ex_create_cq_flags),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_CREATE_CQ_RESP_CQE,
			    UVERBS_ATTR_TYPE(u32),
			    UA_MANDATORY),
	UVERBS_ATTR_FD(UVERBS_ATTR_CREATE_CQ_EVENT_FD,
		       UVERBS_OBJECT_ASYNC_EVENT,
		       UVERBS_ACCESS_READ,
		       UA_OPTIONAL),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_CREATE_CQ_BUFFER_VA,
			   UVERBS_ATTR_TYPE(u64),
			   UA_OPTIONAL),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_CREATE_CQ_BUFFER_LENGTH,
			   UVERBS_ATTR_TYPE(u64),
			   UA_OPTIONAL),
	UVERBS_ATTR_RAW_FD(UVERBS_ATTR_CREATE_CQ_BUFFER_FD,
			   UA_OPTIONAL),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_CREATE_CQ_BUFFER_OFFSET,
			   UVERBS_ATTR_TYPE(u64),
			   UA_OPTIONAL),
	UVERBS_ATTR_UHW());

static int UVERBS_HANDLER(UVERBS_METHOD_CQ_DESTROY)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj =
		uverbs_attr_get_uobject(attrs, UVERBS_ATTR_DESTROY_CQ_HANDLE);
	struct ib_ucq_object *obj =
		container_of(uobj, struct ib_ucq_object, uevent.uobject);
	struct ib_uverbs_destroy_cq_resp resp = {
		.comp_events_reported = obj->comp_events_reported,
		.async_events_reported = obj->uevent.events_reported
	};

	return uverbs_copy_to(attrs, UVERBS_ATTR_DESTROY_CQ_RESP, &resp,
			      sizeof(resp));
}

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_CQ_DESTROY,
	UVERBS_ATTR_IDR(UVERBS_ATTR_DESTROY_CQ_HANDLE,
			UVERBS_OBJECT_CQ,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_DESTROY_CQ_RESP,
			    UVERBS_ATTR_TYPE(struct ib_uverbs_destroy_cq_resp),
			    UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_CQ,
	UVERBS_TYPE_ALLOC_IDR_SZ(sizeof(struct ib_ucq_object), uverbs_free_cq),
	&UVERBS_METHOD(UVERBS_METHOD_CQ_CREATE),
	&UVERBS_METHOD(UVERBS_METHOD_CQ_DESTROY)
);

const struct uapi_definition uverbs_def_obj_cq[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(UVERBS_OBJECT_CQ,
				      UAPI_DEF_OBJ_NEEDS_FN(destroy_cq)),
	{}
};
