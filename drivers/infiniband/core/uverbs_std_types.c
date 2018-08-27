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
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>
#include <linux/bug.h>
#include <linux/file.h>
#include <rdma/restrack.h>
#include "rdma_core.h"
#include "uverbs.h"

static int uverbs_free_ah(struct ib_uobject *uobject,
			  enum rdma_remove_reason why)
{
	return rdma_destroy_ah((struct ib_ah *)uobject->object);
}

static int uverbs_free_flow(struct ib_uobject *uobject,
			    enum rdma_remove_reason why)
{
	struct ib_flow *flow = (struct ib_flow *)uobject->object;
	struct ib_uflow_object *uflow =
		container_of(uobject, struct ib_uflow_object, uobject);
	struct ib_qp *qp = flow->qp;
	int ret;

	ret = flow->device->destroy_flow(flow);
	if (!ret) {
		if (qp)
			atomic_dec(&qp->usecnt);
		ib_uverbs_flow_resources_free(uflow->resources);
	}

	return ret;
}

static int uverbs_free_mw(struct ib_uobject *uobject,
			  enum rdma_remove_reason why)
{
	return uverbs_dealloc_mw((struct ib_mw *)uobject->object);
}

static int uverbs_free_qp(struct ib_uobject *uobject,
			  enum rdma_remove_reason why)
{
	struct ib_qp *qp = uobject->object;
	struct ib_uqp_object *uqp =
		container_of(uobject, struct ib_uqp_object, uevent.uobject);
	int ret;

	/*
	 * If this is a user triggered destroy then do not allow destruction
	 * until the user cleans up all the mcast bindings. Unlike in other
	 * places we forcibly clean up the mcast attachments for !DESTROY
	 * because the mcast attaches are not ubojects and will not be
	 * destroyed by anything else during cleanup processing.
	 */
	if (why == RDMA_REMOVE_DESTROY) {
		if (!list_empty(&uqp->mcast_list))
			return -EBUSY;
	} else if (qp == qp->real_qp) {
		ib_uverbs_detach_umcast(qp, uqp);
	}

	ret = ib_destroy_qp(qp);
	if (ib_is_destroy_retryable(ret, why, uobject))
		return ret;

	if (uqp->uxrcd)
		atomic_dec(&uqp->uxrcd->refcnt);

	ib_uverbs_release_uevent(uobject->context->ufile, &uqp->uevent);
	return ret;
}

static int uverbs_free_rwq_ind_tbl(struct ib_uobject *uobject,
				   enum rdma_remove_reason why)
{
	struct ib_rwq_ind_table *rwq_ind_tbl = uobject->object;
	struct ib_wq **ind_tbl = rwq_ind_tbl->ind_tbl;
	int ret;

	ret = ib_destroy_rwq_ind_table(rwq_ind_tbl);
	if (ib_is_destroy_retryable(ret, why, uobject))
		return ret;

	kfree(ind_tbl);
	return ret;
}

static int uverbs_free_wq(struct ib_uobject *uobject,
			  enum rdma_remove_reason why)
{
	struct ib_wq *wq = uobject->object;
	struct ib_uwq_object *uwq =
		container_of(uobject, struct ib_uwq_object, uevent.uobject);
	int ret;

	ret = ib_destroy_wq(wq);
	if (ib_is_destroy_retryable(ret, why, uobject))
		return ret;

	ib_uverbs_release_uevent(uobject->context->ufile, &uwq->uevent);
	return ret;
}

static int uverbs_free_srq(struct ib_uobject *uobject,
			   enum rdma_remove_reason why)
{
	struct ib_srq *srq = uobject->object;
	struct ib_uevent_object *uevent =
		container_of(uobject, struct ib_uevent_object, uobject);
	enum ib_srq_type  srq_type = srq->srq_type;
	int ret;

	ret = ib_destroy_srq(srq);
	if (ib_is_destroy_retryable(ret, why, uobject))
		return ret;

	if (srq_type == IB_SRQT_XRC) {
		struct ib_usrq_object *us =
			container_of(uevent, struct ib_usrq_object, uevent);

		atomic_dec(&us->uxrcd->refcnt);
	}

	ib_uverbs_release_uevent(uobject->context->ufile, uevent);
	return ret;
}

static int uverbs_free_xrcd(struct ib_uobject *uobject,
			    enum rdma_remove_reason why)
{
	struct ib_xrcd *xrcd = uobject->object;
	struct ib_uxrcd_object *uxrcd =
		container_of(uobject, struct ib_uxrcd_object, uobject);
	int ret;

	ret = ib_destroy_usecnt(&uxrcd->refcnt, why, uobject);
	if (ret)
		return ret;

	mutex_lock(&uobject->context->ufile->device->xrcd_tree_mutex);
	ret = ib_uverbs_dealloc_xrcd(uobject, xrcd, why);
	mutex_unlock(&uobject->context->ufile->device->xrcd_tree_mutex);

	return ret;
}

static int uverbs_free_pd(struct ib_uobject *uobject,
			  enum rdma_remove_reason why)
{
	struct ib_pd *pd = uobject->object;
	int ret;

	ret = ib_destroy_usecnt(&pd->usecnt, why, uobject);
	if (ret)
		return ret;

	ib_dealloc_pd((struct ib_pd *)uobject->object);
	return 0;
}

static int uverbs_hot_unplug_completion_event_file(struct ib_uobject *uobj,
						   enum rdma_remove_reason why)
{
	struct ib_uverbs_completion_event_file *comp_event_file =
		container_of(uobj, struct ib_uverbs_completion_event_file,
			     uobj);
	struct ib_uverbs_event_queue *event_queue = &comp_event_file->ev_queue;

	spin_lock_irq(&event_queue->lock);
	event_queue->is_closed = 1;
	spin_unlock_irq(&event_queue->lock);

	if (why == RDMA_REMOVE_DRIVER_REMOVE) {
		wake_up_interruptible(&event_queue->poll_wait);
		kill_fasync(&event_queue->async_queue, SIGIO, POLL_IN);
	}
	return 0;
};

int uverbs_destroy_def_handler(struct ib_uverbs_file *file,
			       struct uverbs_attr_bundle *attrs)
{
	return 0;
}
EXPORT_SYMBOL(uverbs_destroy_def_handler);

DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_COMP_CHANNEL,
	UVERBS_TYPE_ALLOC_FD(sizeof(struct ib_uverbs_completion_event_file),
			     uverbs_hot_unplug_completion_event_file,
			     &uverbs_event_fops,
			     "[infinibandevent]",
			     O_RDONLY));

DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_QP,
	UVERBS_TYPE_ALLOC_IDR_SZ(sizeof(struct ib_uqp_object), uverbs_free_qp));

DECLARE_UVERBS_NAMED_OBJECT(UVERBS_OBJECT_MW,
			    UVERBS_TYPE_ALLOC_IDR(uverbs_free_mw));

DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_SRQ,
	UVERBS_TYPE_ALLOC_IDR_SZ(sizeof(struct ib_usrq_object),
				 uverbs_free_srq));

DECLARE_UVERBS_NAMED_OBJECT(UVERBS_OBJECT_AH,
			    UVERBS_TYPE_ALLOC_IDR(uverbs_free_ah));

DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_FLOW,
	UVERBS_TYPE_ALLOC_IDR_SZ(sizeof(struct ib_uflow_object),
				 uverbs_free_flow));

DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_WQ,
	UVERBS_TYPE_ALLOC_IDR_SZ(sizeof(struct ib_uwq_object), uverbs_free_wq));

DECLARE_UVERBS_NAMED_OBJECT(UVERBS_OBJECT_RWQ_IND_TBL,
			    UVERBS_TYPE_ALLOC_IDR(uverbs_free_rwq_ind_tbl));

DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_XRCD,
	UVERBS_TYPE_ALLOC_IDR_SZ(sizeof(struct ib_uxrcd_object),
				 uverbs_free_xrcd));

DECLARE_UVERBS_NAMED_OBJECT(UVERBS_OBJECT_PD,
			    UVERBS_TYPE_ALLOC_IDR(uverbs_free_pd));

DECLARE_UVERBS_GLOBAL_METHODS(UVERBS_OBJECT_DEVICE);

DECLARE_UVERBS_OBJECT_TREE(uverbs_default_objects,
			   &UVERBS_OBJECT(UVERBS_OBJECT_DEVICE),
			   &UVERBS_OBJECT(UVERBS_OBJECT_PD),
			   &UVERBS_OBJECT(UVERBS_OBJECT_MR),
			   &UVERBS_OBJECT(UVERBS_OBJECT_COMP_CHANNEL),
			   &UVERBS_OBJECT(UVERBS_OBJECT_CQ),
			   &UVERBS_OBJECT(UVERBS_OBJECT_QP),
			   &UVERBS_OBJECT(UVERBS_OBJECT_AH),
			   &UVERBS_OBJECT(UVERBS_OBJECT_MW),
			   &UVERBS_OBJECT(UVERBS_OBJECT_SRQ),
			   &UVERBS_OBJECT(UVERBS_OBJECT_FLOW),
			   &UVERBS_OBJECT(UVERBS_OBJECT_WQ),
			   &UVERBS_OBJECT(UVERBS_OBJECT_RWQ_IND_TBL),
			   &UVERBS_OBJECT(UVERBS_OBJECT_XRCD),
			   &UVERBS_OBJECT(UVERBS_OBJECT_FLOW_ACTION),
			   &UVERBS_OBJECT(UVERBS_OBJECT_DM),
			   &UVERBS_OBJECT(UVERBS_OBJECT_COUNTERS));

const struct uverbs_object_tree_def *uverbs_default_get_objects(void)
{
	return &uverbs_default_objects;
}
