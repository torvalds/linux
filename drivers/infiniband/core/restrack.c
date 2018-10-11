// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved.
 */

#include <rdma/rdma_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/restrack.h>
#include <linux/mutex.h>
#include <linux/sched/task.h>
#include <linux/pid_namespace.h>

#include "cma_priv.h"

static int fill_res_noop(struct sk_buff *msg,
			 struct rdma_restrack_entry *entry)
{
	return 0;
}

void rdma_restrack_init(struct rdma_restrack_root *res)
{
	init_rwsem(&res->rwsem);
	res->fill_res_entry = fill_res_noop;
}

static const char *type2str(enum rdma_restrack_type type)
{
	static const char * const names[RDMA_RESTRACK_MAX] = {
		[RDMA_RESTRACK_PD] = "PD",
		[RDMA_RESTRACK_CQ] = "CQ",
		[RDMA_RESTRACK_QP] = "QP",
		[RDMA_RESTRACK_CM_ID] = "CM_ID",
		[RDMA_RESTRACK_MR] = "MR",
	};

	return names[type];
};

void rdma_restrack_clean(struct rdma_restrack_root *res)
{
	struct rdma_restrack_entry *e;
	char buf[TASK_COMM_LEN];
	struct ib_device *dev;
	const char *owner;
	int bkt;

	if (hash_empty(res->hash))
		return;

	dev = container_of(res, struct ib_device, res);
	pr_err("restrack: %s", CUT_HERE);
	dev_err(&dev->dev, "BUG: RESTRACK detected leak of resources\n");
	hash_for_each(res->hash, bkt, e, node) {
		if (rdma_is_kernel_res(e)) {
			owner = e->kern_name;
		} else {
			/*
			 * There is no need to call get_task_struct here,
			 * because we can be here only if there are more
			 * get_task_struct() call than put_task_struct().
			 */
			get_task_comm(buf, e->task);
			owner = buf;
		}

		pr_err("restrack: %s %s object allocated by %s is not freed\n",
		       rdma_is_kernel_res(e) ? "Kernel" : "User",
		       type2str(e->type), owner);
	}
	pr_err("restrack: %s", CUT_HERE);
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
	struct ib_pd *pd;

	switch (res->type) {
	case RDMA_RESTRACK_QP:
		pd = container_of(res, struct ib_qp, res)->pd;
		if (!pd) {
			WARN_ONCE(true, "XRC QPs are not supported\n");
			/* Survive, despite the programmer's error */
			res->kern_name = " ";
		}
		break;
	case RDMA_RESTRACK_MR:
		pd = container_of(res, struct ib_mr, res)->pd;
		break;
	default:
		/* Other types set kern_name directly */
		pd = NULL;
		break;
	}

	if (pd)
		res->kern_name = pd->res.kern_name;
}

static struct ib_device *res_to_dev(struct rdma_restrack_entry *res)
{
	switch (res->type) {
	case RDMA_RESTRACK_PD:
		return container_of(res, struct ib_pd, res)->device;
	case RDMA_RESTRACK_CQ:
		return container_of(res, struct ib_cq, res)->device;
	case RDMA_RESTRACK_QP:
		return container_of(res, struct ib_qp, res)->device;
	case RDMA_RESTRACK_CM_ID:
		return container_of(res, struct rdma_id_private,
				    res)->id.device;
	case RDMA_RESTRACK_MR:
		return container_of(res, struct ib_mr, res)->device;
	default:
		WARN_ONCE(true, "Wrong resource tracking type %u\n", res->type);
		return NULL;
	}
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
	case RDMA_RESTRACK_CM_ID:
		return !res->kern_name;
	case RDMA_RESTRACK_MR:
		return container_of(res, struct ib_mr, res)->pd->uobject;
	default:
		WARN_ONCE(true, "Wrong resource tracking type %u\n", res->type);
		return false;
	}
}

void rdma_restrack_set_task(struct rdma_restrack_entry *res,
			    const char *caller)
{
	if (caller) {
		res->kern_name = caller;
		return;
	}

	if (res->task)
		put_task_struct(res->task);
	get_task_struct(current);
	res->task = current;
}
EXPORT_SYMBOL(rdma_restrack_set_task);

void rdma_restrack_add(struct rdma_restrack_entry *res)
{
	struct ib_device *dev = res_to_dev(res);

	if (!dev)
		return;

	if (res->type != RDMA_RESTRACK_CM_ID || !res_is_user(res))
		res->task = NULL;

	if (res_is_user(res)) {
		if (!res->task)
			rdma_restrack_set_task(res, NULL);
		res->kern_name = NULL;
	} else {
		set_kern_name(res);
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
		goto out;

	dev = res_to_dev(res);
	if (!dev)
		return;

	rdma_restrack_put(res);

	wait_for_completion(&res->comp);

	down_write(&dev->res.rwsem);
	hash_del(&res->node);
	res->valid = false;
	up_write(&dev->res.rwsem);

out:
	if (res->task) {
		put_task_struct(res->task);
		res->task = NULL;
	}
}
EXPORT_SYMBOL(rdma_restrack_del);
