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
#include "restrack.h"

/**
 * rdma_restrack_init() - initialize and allocate resource tracking
 * @dev:  IB device
 *
 * Return: 0 on success
 */
int rdma_restrack_init(struct ib_device *dev)
{
	struct rdma_restrack_root *rt;
	int i;

	dev->res = kcalloc(RDMA_RESTRACK_MAX, sizeof(*rt), GFP_KERNEL);
	if (!dev->res)
		return -ENOMEM;

	rt = dev->res;

	for (i = 0; i < RDMA_RESTRACK_MAX; i++)
		xa_init_flags(&rt[i].xa, XA_FLAGS_ALLOC);

	return 0;
}

static const char *type2str(enum rdma_restrack_type type)
{
	static const char * const names[RDMA_RESTRACK_MAX] = {
		[RDMA_RESTRACK_PD] = "PD",
		[RDMA_RESTRACK_CQ] = "CQ",
		[RDMA_RESTRACK_QP] = "QP",
		[RDMA_RESTRACK_CM_ID] = "CM_ID",
		[RDMA_RESTRACK_MR] = "MR",
		[RDMA_RESTRACK_CTX] = "CTX",
	};

	return names[type];
};

/**
 * rdma_restrack_clean() - clean resource tracking
 * @dev:  IB device
 */
void rdma_restrack_clean(struct ib_device *dev)
{
	struct rdma_restrack_root *rt = dev->res;
	struct rdma_restrack_entry *e;
	char buf[TASK_COMM_LEN];
	bool found = false;
	const char *owner;
	int i;

	for (i = 0 ; i < RDMA_RESTRACK_MAX; i++) {
		struct xarray *xa = &dev->res[i].xa;

		if (!xa_empty(xa)) {
			unsigned long index;

			if (!found) {
				pr_err("restrack: %s", CUT_HERE);
				dev_err(&dev->dev, "BUG: RESTRACK detected leak of resources\n");
			}
			xa_for_each(xa, index, e) {
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
				       rdma_is_kernel_res(e) ? "Kernel" :
							       "User",
				       type2str(e->type), owner);
			}
			found = true;
		}
		xa_destroy(xa);
	}
	if (found)
		pr_err("restrack: %s", CUT_HERE);

	kfree(rt);
}

/**
 * rdma_restrack_count() - the current usage of specific object
 * @dev:  IB device
 * @type: actual type of object to operate
 * @ns:   PID namespace
 */
int rdma_restrack_count(struct ib_device *dev, enum rdma_restrack_type type,
			struct pid_namespace *ns)
{
	struct rdma_restrack_root *rt = &dev->res[type];
	struct rdma_restrack_entry *e;
	XA_STATE(xas, &rt->xa, 0);
	u32 cnt = 0;

	xa_lock(&rt->xa);
	xas_for_each(&xas, e, U32_MAX) {
		if (ns == &init_pid_ns ||
		    (!rdma_is_kernel_res(e) &&
		     ns == task_active_pid_ns(e->task)))
			cnt++;
	}
	xa_unlock(&rt->xa);
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
	case RDMA_RESTRACK_CTX:
		return container_of(res, struct ib_ucontext, res)->device;
	default:
		WARN_ONCE(true, "Wrong resource tracking type %u\n", res->type);
		return NULL;
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

static void rdma_restrack_add(struct rdma_restrack_entry *res)
{
	struct ib_device *dev = res_to_dev(res);
	struct rdma_restrack_root *rt;
	int ret;

	if (!dev)
		return;

	rt = &dev->res[res->type];

	kref_init(&res->kref);
	init_completion(&res->comp);
	if (res->type != RDMA_RESTRACK_QP)
		ret = xa_alloc_cyclic(&rt->xa, &res->id, res, xa_limit_32b,
				&rt->next_id, GFP_KERNEL);
	else {
		/* Special case to ensure that LQPN points to right QP */
		struct ib_qp *qp = container_of(res, struct ib_qp, res);

		ret = xa_insert(&rt->xa, qp->qp_num, res, GFP_KERNEL);
		res->id = ret ? 0 : qp->qp_num;
	}

	if (!ret)
		res->valid = true;
}

/**
 * rdma_restrack_kadd() - add kernel object to the reource tracking database
 * @res:  resource entry
 */
void rdma_restrack_kadd(struct rdma_restrack_entry *res)
{
	res->task = NULL;
	set_kern_name(res);
	res->user = false;
	rdma_restrack_add(res);
}
EXPORT_SYMBOL(rdma_restrack_kadd);

/**
 * rdma_restrack_uadd() - add user object to the reource tracking database
 * @res:  resource entry
 */
void rdma_restrack_uadd(struct rdma_restrack_entry *res)
{
	if (res->type != RDMA_RESTRACK_CM_ID)
		res->task = NULL;

	if (!res->task)
		rdma_restrack_set_task(res, NULL);
	res->kern_name = NULL;

	res->user = true;
	rdma_restrack_add(res);
}
EXPORT_SYMBOL(rdma_restrack_uadd);

int __must_check rdma_restrack_get(struct rdma_restrack_entry *res)
{
	return kref_get_unless_zero(&res->kref);
}
EXPORT_SYMBOL(rdma_restrack_get);

/**
 * rdma_restrack_get_byid() - translate from ID to restrack object
 * @dev: IB device
 * @type: resource track type
 * @id: ID to take a look
 *
 * Return: Pointer to restrack entry or -ENOENT in case of error.
 */
struct rdma_restrack_entry *
rdma_restrack_get_byid(struct ib_device *dev,
		       enum rdma_restrack_type type, u32 id)
{
	struct rdma_restrack_root *rt = &dev->res[type];
	struct rdma_restrack_entry *res;

	xa_lock(&rt->xa);
	res = xa_load(&rt->xa, id);
	if (!res || !rdma_restrack_get(res))
		res = ERR_PTR(-ENOENT);
	xa_unlock(&rt->xa);

	return res;
}
EXPORT_SYMBOL(rdma_restrack_get_byid);

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
	struct rdma_restrack_entry *old;
	struct rdma_restrack_root *rt;
	struct ib_device *dev;

	if (!res->valid)
		goto out;

	dev = res_to_dev(res);
	if (WARN_ON(!dev))
		return;

	rt = &dev->res[res->type];

	old = xa_erase(&rt->xa, res->id);
	WARN_ON(old != res);
	res->valid = false;

	rdma_restrack_put(res);
	wait_for_completion(&res->comp);

out:
	if (res->task) {
		put_task_struct(res->task);
		res->task = NULL;
	}
}
EXPORT_SYMBOL(rdma_restrack_del);
