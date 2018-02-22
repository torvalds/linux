/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved.
 */

#include <rdma/ib_verbs.h>
#include <rdma/restrack.h>
#include <linux/mutex.h>
#include <linux/sched/task.h>
#include <linux/pid_namespace.h>

void rdma_restrack_init(struct rdma_restrack_root *res)
{
	init_rwsem(&res->rwsem);
}

void rdma_restrack_clean(struct rdma_restrack_root *res)
{
	WARN_ON_ONCE(!hash_empty(res->hash));
}

int rdma_restrack_count(struct rdma_restrack_root *res,
			enum rdma_restrack_type type,
			struct pid_namespace *ns)
{
	struct rdma_restrack_entry *e;
	u32 cnt = 0;

	down_read(&res->rwsem);
	hash_for_each_possible(res->hash, e, node, type) {
		if (ns == &init_pid_ns ||
		    (!rdma_is_kernel_res(e) &&
		     ns == task_active_pid_ns(e->task)))
			cnt++;
	}
	up_read(&res->rwsem);
	return cnt;
}
EXPORT_SYMBOL(rdma_restrack_count);

static void set_kern_name(struct rdma_restrack_entry *res)
{
	enum rdma_restrack_type type = res->type;
	struct ib_qp *qp;

	if (type != RDMA_RESTRACK_QP)
		/* PD and CQ types already have this name embedded in */
		return;

	qp = container_of(res, struct ib_qp, res);
	if (!qp->pd) {
		WARN_ONCE(true, "XRC QPs are not supported\n");
		/* Survive, despite the programmer's error */
		res->kern_name = " ";
		return;
	}

	res->kern_name = qp->pd->res.kern_name;
}

static struct ib_device *res_to_dev(struct rdma_restrack_entry *res)
{
	enum rdma_restrack_type type = res->type;
	struct ib_device *dev;
	struct ib_pd *pd;
	struct ib_cq *cq;
	struct ib_qp *qp;

	switch (type) {
	case RDMA_RESTRACK_PD:
		pd = container_of(res, struct ib_pd, res);
		dev = pd->device;
		break;
	case RDMA_RESTRACK_CQ:
		cq = container_of(res, struct ib_cq, res);
		dev = cq->device;
		break;
	case RDMA_RESTRACK_QP:
		qp = container_of(res, struct ib_qp, res);
		dev = qp->device;
		break;
	default:
		WARN_ONCE(true, "Wrong resource tracking type %u\n", type);
		return NULL;
	}

	return dev;
}

static bool res_is_user(struct rdma_restrack_entry *res)
{
	switch (res->type) {
	case RDMA_RESTRACK_PD:
		return container_of(res, struct ib_pd, res)->uobject;
	case RDMA_RESTRACK_CQ:
		return container_of(res, struct ib_cq, res)->uobject;
	case RDMA_RESTRACK_QP:
		return container_of(res, struct ib_qp, res)->uobject;
	default:
		WARN_ONCE(true, "Wrong resource tracking type %u\n", res->type);
		return false;
	}
}

void rdma_restrack_add(struct rdma_restrack_entry *res)
{
	struct ib_device *dev = res_to_dev(res);

	if (!dev)
		return;

	if (res_is_user(res)) {
		get_task_struct(current);
		res->task = current;
		res->kern_name = NULL;
	} else {
		set_kern_name(res);
		res->task = NULL;
	}

	kref_init(&res->kref);
	init_completion(&res->comp);
	res->valid = true;

	down_write(&dev->res.rwsem);
	hash_add(dev->res.hash, &res->node, res->type);
	up_write(&dev->res.rwsem);
}
EXPORT_SYMBOL(rdma_restrack_add);

int __must_check rdma_restrack_get(struct rdma_restrack_entry *res)
{
	return kref_get_unless_zero(&res->kref);
}
EXPORT_SYMBOL(rdma_restrack_get);

static void restrack_release(struct kref *kref)
{
	struct rdma_restrack_entry *res;

	res = container_of(kref, struct rdma_restrack_entry, kref);
	complete(&res->comp);
}

int rdma_restrack_put(struct rdma_restrack_entry *res)
{
	return kref_put(&res->kref, restrack_release);
}
EXPORT_SYMBOL(rdma_restrack_put);

void rdma_restrack_del(struct rdma_restrack_entry *res)
{
	struct ib_device *dev;

	if (!res->valid)
		return;

	dev = res_to_dev(res);
	if (!dev)
		return;

	rdma_restrack_put(res);

	wait_for_completion(&res->comp);

	down_write(&dev->res.rwsem);
	hash_del(&res->node);
	res->valid = false;
	if (res->task)
		put_task_struct(res->task);
	up_write(&dev->res.rwsem);
}
EXPORT_SYMBOL(rdma_restrack_del);
