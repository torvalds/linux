// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2019 Mellanox Technologies. All rights reserved.
 */
#include <rdma/ib_verbs.h>
#include <rdma/rdma_counter.h>

#include "core_priv.h"
#include "restrack.h"

#define ALL_AUTO_MODE_MASKS (RDMA_COUNTER_MASK_QP_TYPE)

static int __counter_set_mode(struct rdma_counter_mode *curr,
			      enum rdma_nl_counter_mode new_mode,
			      enum rdma_nl_counter_mask new_mask)
{
	if ((new_mode == RDMA_COUNTER_MODE_AUTO) &&
	    ((new_mask & (~ALL_AUTO_MODE_MASKS)) ||
	     (curr->mode != RDMA_COUNTER_MODE_NONE)))
		return -EINVAL;

	curr->mode = new_mode;
	curr->mask = new_mask;
	return 0;
}

/**
 * rdma_counter_set_auto_mode() - Turn on/off per-port auto mode
 *
 * When @on is true, the @mask must be set
 */
int rdma_counter_set_auto_mode(struct ib_device *dev, u8 port,
			       bool on, enum rdma_nl_counter_mask mask)
{
	struct rdma_port_counter *port_counter;
	int ret;

	port_counter = &dev->port_data[port].port_counter;
	mutex_lock(&port_counter->lock);
	if (on) {
		ret = __counter_set_mode(&port_counter->mode,
					 RDMA_COUNTER_MODE_AUTO, mask);
	} else {
		if (port_counter->mode.mode != RDMA_COUNTER_MODE_AUTO) {
			ret = -EINVAL;
			goto out;
		}
		ret = __counter_set_mode(&port_counter->mode,
					 RDMA_COUNTER_MODE_NONE, 0);
	}

out:
	mutex_unlock(&port_counter->lock);
	return ret;
}

static struct rdma_counter *rdma_counter_alloc(struct ib_device *dev, u8 port,
					       enum rdma_nl_counter_mode mode)
{
	struct rdma_counter *counter;

	if (!dev->ops.counter_dealloc || !dev->ops.counter_alloc_stats)
		return NULL;

	counter = kzalloc(sizeof(*counter), GFP_KERNEL);
	if (!counter)
		return NULL;

	counter->device    = dev;
	counter->port      = port;
	counter->res.type  = RDMA_RESTRACK_COUNTER;
	counter->stats     = dev->ops.counter_alloc_stats(counter);
	if (!counter->stats)
		goto err_stats;

	counter->mode.mode = mode;
	kref_init(&counter->kref);
	mutex_init(&counter->lock);

	return counter;

err_stats:
	kfree(counter);
	return NULL;
}

static void rdma_counter_free(struct rdma_counter *counter)
{
	rdma_restrack_del(&counter->res);
	kfree(counter->stats);
	kfree(counter);
}

static void auto_mode_init_counter(struct rdma_counter *counter,
				   const struct ib_qp *qp,
				   enum rdma_nl_counter_mask new_mask)
{
	struct auto_mode_param *param = &counter->mode.param;

	counter->mode.mode = RDMA_COUNTER_MODE_AUTO;
	counter->mode.mask = new_mask;

	if (new_mask & RDMA_COUNTER_MASK_QP_TYPE)
		param->qp_type = qp->qp_type;
}

static bool auto_mode_match(struct ib_qp *qp, struct rdma_counter *counter,
			    enum rdma_nl_counter_mask auto_mask)
{
	struct auto_mode_param *param = &counter->mode.param;
	bool match = true;

	if (rdma_is_kernel_res(&counter->res) != rdma_is_kernel_res(&qp->res))
		return false;

	/* Ensure that counter belong to right PID */
	if (!rdma_is_kernel_res(&counter->res) &&
	    !rdma_is_kernel_res(&qp->res) &&
	    (task_pid_vnr(counter->res.task) != current->pid))
		return false;

	if (auto_mask & RDMA_COUNTER_MASK_QP_TYPE)
		match &= (param->qp_type == qp->qp_type);

	return match;
}

static int __rdma_counter_bind_qp(struct rdma_counter *counter,
				  struct ib_qp *qp)
{
	int ret;

	if (qp->counter)
		return -EINVAL;

	if (!qp->device->ops.counter_bind_qp)
		return -EOPNOTSUPP;

	mutex_lock(&counter->lock);
	ret = qp->device->ops.counter_bind_qp(counter, qp);
	mutex_unlock(&counter->lock);

	return ret;
}

static int __rdma_counter_unbind_qp(struct ib_qp *qp)
{
	struct rdma_counter *counter = qp->counter;
	int ret;

	if (!qp->device->ops.counter_unbind_qp)
		return -EOPNOTSUPP;

	mutex_lock(&counter->lock);
	ret = qp->device->ops.counter_unbind_qp(qp);
	mutex_unlock(&counter->lock);

	return ret;
}

static void counter_history_stat_update(const struct rdma_counter *counter)
{
	struct ib_device *dev = counter->device;
	struct rdma_port_counter *port_counter;
	int i;

	port_counter = &dev->port_data[counter->port].port_counter;
	if (!port_counter->hstats)
		return;

	for (i = 0; i < counter->stats->num_counters; i++)
		port_counter->hstats->value[i] += counter->stats->value[i];
}

/**
 * rdma_get_counter_auto_mode - Find the counter that @qp should be bound
 *     with in auto mode
 *
 * Return: The counter (with ref-count increased) if found
 */
static struct rdma_counter *rdma_get_counter_auto_mode(struct ib_qp *qp,
						       u8 port)
{
	struct rdma_port_counter *port_counter;
	struct rdma_counter *counter = NULL;
	struct ib_device *dev = qp->device;
	struct rdma_restrack_entry *res;
	struct rdma_restrack_root *rt;
	unsigned long id = 0;

	port_counter = &dev->port_data[port].port_counter;
	rt = &dev->res[RDMA_RESTRACK_COUNTER];
	xa_lock(&rt->xa);
	xa_for_each(&rt->xa, id, res) {
		if (!rdma_is_visible_in_pid_ns(res))
			continue;

		counter = container_of(res, struct rdma_counter, res);
		if ((counter->device != qp->device) || (counter->port != port))
			goto next;

		if (auto_mode_match(qp, counter, port_counter->mode.mask))
			break;
next:
		counter = NULL;
	}

	if (counter && !kref_get_unless_zero(&counter->kref))
		counter = NULL;

	xa_unlock(&rt->xa);
	return counter;
}

static void rdma_counter_res_add(struct rdma_counter *counter,
				 struct ib_qp *qp)
{
	if (rdma_is_kernel_res(&qp->res)) {
		rdma_restrack_set_task(&counter->res, qp->res.kern_name);
		rdma_restrack_kadd(&counter->res);
	} else {
		rdma_restrack_attach_task(&counter->res, qp->res.task);
		rdma_restrack_uadd(&counter->res);
	}
}

static void counter_release(struct kref *kref)
{
	struct rdma_counter *counter;

	counter = container_of(kref, struct rdma_counter, kref);
	counter_history_stat_update(counter);
	counter->device->ops.counter_dealloc(counter);
	rdma_counter_free(counter);
}

/**
 * rdma_counter_bind_qp_auto - Check and bind the QP to a counter base on
 *   the auto-mode rule
 */
int rdma_counter_bind_qp_auto(struct ib_qp *qp, u8 port)
{
	struct rdma_port_counter *port_counter;
	struct ib_device *dev = qp->device;
	struct rdma_counter *counter;
	int ret;

	if (!rdma_is_port_valid(dev, port))
		return -EINVAL;

	port_counter = &dev->port_data[port].port_counter;
	if (port_counter->mode.mode != RDMA_COUNTER_MODE_AUTO)
		return 0;

	counter = rdma_get_counter_auto_mode(qp, port);
	if (counter) {
		ret = __rdma_counter_bind_qp(counter, qp);
		if (ret) {
			kref_put(&counter->kref, counter_release);
			return ret;
		}
	} else {
		counter = rdma_counter_alloc(dev, port, RDMA_COUNTER_MODE_AUTO);
		if (!counter)
			return -ENOMEM;

		auto_mode_init_counter(counter, qp, port_counter->mode.mask);

		ret = __rdma_counter_bind_qp(counter, qp);
		if (ret) {
			rdma_counter_free(counter);
			return ret;
		}

		rdma_counter_res_add(counter, qp);
	}

	return 0;
}

/**
 * rdma_counter_unbind_qp - Unbind a qp from a counter
 * @force:
 *   true - Decrease the counter ref-count anyway (e.g., qp destroy)
 */
int rdma_counter_unbind_qp(struct ib_qp *qp, bool force)
{
	struct rdma_counter *counter = qp->counter;
	int ret;

	if (!counter)
		return -EINVAL;

	ret = __rdma_counter_unbind_qp(qp);
	if (ret && !force)
		return ret;

	kref_put(&counter->kref, counter_release);
	return 0;
}

int rdma_counter_query_stats(struct rdma_counter *counter)
{
	struct ib_device *dev = counter->device;
	int ret;

	if (!dev->ops.counter_update_stats)
		return -EINVAL;

	mutex_lock(&counter->lock);
	ret = dev->ops.counter_update_stats(counter);
	mutex_unlock(&counter->lock);

	return ret;
}

static u64 get_running_counters_hwstat_sum(struct ib_device *dev,
					   u8 port, u32 index)
{
	struct rdma_restrack_entry *res;
	struct rdma_restrack_root *rt;
	struct rdma_counter *counter;
	unsigned long id = 0;
	u64 sum = 0;

	rt = &dev->res[RDMA_RESTRACK_COUNTER];
	xa_lock(&rt->xa);
	xa_for_each(&rt->xa, id, res) {
		if (!rdma_restrack_get(res))
			continue;

		xa_unlock(&rt->xa);

		counter = container_of(res, struct rdma_counter, res);
		if ((counter->device != dev) || (counter->port != port) ||
		    rdma_counter_query_stats(counter))
			goto next;

		sum += counter->stats->value[index];

next:
		xa_lock(&rt->xa);
		rdma_restrack_put(res);
	}

	xa_unlock(&rt->xa);
	return sum;
}

/**
 * rdma_counter_get_hwstat_value() - Get the sum value of all counters on a
 *   specific port, including the running ones and history data
 */
u64 rdma_counter_get_hwstat_value(struct ib_device *dev, u8 port, u32 index)
{
	struct rdma_port_counter *port_counter;
	u64 sum;

	port_counter = &dev->port_data[port].port_counter;
	sum = get_running_counters_hwstat_sum(dev, port, index);
	sum += port_counter->hstats->value[index];

	return sum;
}

void rdma_counter_init(struct ib_device *dev)
{
	struct rdma_port_counter *port_counter;
	u32 port;

	if (!dev->ops.alloc_hw_stats || !dev->port_data)
		return;

	rdma_for_each_port(dev, port) {
		port_counter = &dev->port_data[port].port_counter;
		port_counter->mode.mode = RDMA_COUNTER_MODE_NONE;
		mutex_init(&port_counter->lock);

		port_counter->hstats = dev->ops.alloc_hw_stats(dev, port);
		if (!port_counter->hstats)
			goto fail;
	}

	return;

fail:
	rdma_for_each_port(dev, port) {
		port_counter = &dev->port_data[port].port_counter;
		kfree(port_counter->hstats);
		port_counter->hstats = NULL;
	}

	return;
}

void rdma_counter_release(struct ib_device *dev)
{
	struct rdma_port_counter *port_counter;
	u32 port;

	if (!dev->ops.alloc_hw_stats)
		return;

	rdma_for_each_port(dev, port) {
		port_counter = &dev->port_data[port].port_counter;
		kfree(port_counter->hstats);
	}
}
