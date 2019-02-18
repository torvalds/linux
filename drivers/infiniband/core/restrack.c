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

static int rt_xa_alloc_cyclic(struct xarray *xa, u32 *id, void *entry,
			      u32 *next)
{
	int err;

	*id = *next;
	if (*next == U32_MAX)
		*id = 0;

	xa_lock(xa);
	err = __xa_alloc(xa, id, U32_MAX, entry, GFP_KERNEL);
	if (err && *next != U32_MAX) {
		*id = 0;
		err = __xa_alloc(xa, id, *next, entry, GFP_KERNEL);
	}

	if (!err)
		*next = *id + 1;
	xa_unlock(xa);
	return err;
}

/**
 * rdma_restrack_init() - initialize resource tracking
 * @dev:  IB device
 */
void rdma_restrack_init(struct ib_device *dev)
{
	struct rdma_restrack_root *res = &dev->res;
	int i;

	for (i = 0 ; i < RDMA_RESTRACK_MAX; i++)
		xa_init_flags(&res->xa[i], XA_FLAGS_ALLOC);

	init_rwsem(&res->rwsem);
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
	struct rdma_restrack_root *res = &dev->res;
	struct rdma_restrack_entry *e;
	char buf[TASK_COMM_LEN];
	bool found = false;
	const char *owner;
	int i;

	for (i = 0 ; i < RDMA_RESTRACK_MAX; i++) {
		if (!xa_empty(&res->xa[i])) {
			unsigned long index;

			if (!found) {
				pr_err("restrack: %s", CUT_HERE);
				dev_err(&dev->dev, "BUG: RESTRACK detected leak of resources\n");
			}
			xa_for_each(&res->xa[i], index, e) {
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
		xa_destroy(&res->xa[i]);
	}
	if (found)
		pr_err("restrack: %s", CUT_HERE);
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
	struct rdma_restrack_root *res = &dev->res;
	struct rdma_restrack_entry *e;
	unsigned long index = 0;
	u32 cnt = 0;

	down_read(&res->rwsem);
	xa_for_each(&res->xa[type], index, e) {
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
	int ret;

	if (!dev)
		return;

	kref_init(&res->kref);
	init_completion(&res->comp);

	ret = rt_xa_alloc_cyclic(&dev->res.xa[res->type], &res->id, res,
				 &dev->res.next_id[res->type]);

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
	struct rdma_restrack_root *rt = &dev->res;
	struct rdma_restrack_entry *res;

	down_read(&dev->res.rwsem);
	res = xa_load(&rt->xa[type], id);
	if (!res || !rdma_restrack_get(res))
		res = ERR_PTR(-ENOENT);
	up_read(&dev->res.rwsem);

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
	struct ib_device *dev;

	if (!res->valid)
		goto out;

	dev = res_to_dev(res);
	if (!dev)
		return;

	down_write(&dev->res.rwsem);
	xa_erase(&dev->res.xa[res->type], res->id);
	res->valid = false;
	up_write(&dev->res.rwsem);

	rdma_restrack_put(res);
	wait_for_completion(&res->comp);

out:
	if (res->task) {
		put_task_struct(res->task);
		res->task = NULL;
	}
}
EXPORT_SYMBOL(rdma_restrack_del);
