// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2019 Mellanox Technologies. All rights reserved.
 */
#include <rdma/ib_verbs.h>
#include <rdma/rdma_counter.h>

#include "core_priv.h"
#include "restrack.h"

#define ALL_AUTO_MODE_MASKS (RDMA_COUNTER_MASK_QP_TYPE | RDMA_COUNTER_MASK_PID)

static int __counter_set_mode(struct rdma_port_counter *port_counter,
			      enum rdma_nl_counter_mode new_mode,
			      enum rdma_nl_counter_mask new_mask)
{
	if (new_mode == RDMA_COUNTER_MODE_AUTO) {
		if (new_mask & (~ALL_AUTO_MODE_MASKS))
			return -EINVAL;
		if (port_counter->num_counters)
			return -EBUSY;
	}

	port_counter->mode.mode = new_mode;
	port_counter->mode.mask = new_mask;
	return 0;
}

/*
 * rdma_counter_set_auto_mode() - Turn on/off per-port auto mode
 *
 * @dev: Device to operate
 * @port: Port to use
 * @mask: Mask to configure
 * @extack: Message to the user
 *
 * Return 0 on success. If counter mode wasn't changed then it is considered
 * as success as well.
 * Return -EBUSY when changing to auto mode while there are bounded counters.
 *
 */
int rdma_counter_set_auto_mode(struct ib_device *dev, u32 port,
			       enum rdma_nl_counter_mask mask,
			       struct netlink_ext_ack *extack)
{
	struct rdma_port_counter *port_counter;
	enum rdma_nl_counter_mode mode;
	int ret;

	port_counter = &dev->port_data[port].port_counter;
	if (!port_counter->hstats)
		return -EOPNOTSUPP;

	mutex_lock(&port_counter->lock);
	if (mask)
		mode = RDMA_COUNTER_MODE_AUTO;
	else
		mode = (port_counter->num_counters) ? RDMA_COUNTER_MODE_MANUAL :
						      RDMA_COUNTER_MODE_NONE;

	if (port_counter->mode.mode == mode &&
	    port_counter->mode.mask == mask) {
		ret = 0;
		goto out;
	}

	ret = __counter_set_mode(port_counter, mode, mask);

out:
	mutex_unlock(&port_counter->lock);
	if (ret == -EBUSY)
		NL_SET_ERR_MSG(
			extack,
			"Modifying auto mode is not allowed when there is a bound QP");
	return ret;
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

static struct rdma_counter *alloc_and_bind(struct ib_device *dev, u32 port,
					   struct ib_qp *qp,
					   enum rdma_nl_counter_mode mode)
{
	struct rdma_port_counter *port_counter;
	struct rdma_counter *counter;
	int ret;

	if (!dev->ops.counter_dealloc || !dev->ops.counter_alloc_stats)
		return NULL;

	counter = kzalloc(sizeof(*counter), GFP_KERNEL);
	if (!counter)
		return NULL;

	counter->device    = dev;
	counter->port      = port;

	rdma_restrack_new(&counter->res, RDMA_RESTRACK_COUNTER);
	counter->stats = dev->ops.counter_alloc_stats(counter);
	if (!counter->stats)
		goto err_stats;

	port_counter = &dev->port_data[port].port_counter;
	mutex_lock(&port_counter->lock);
	switch (mode) {
	case RDMA_COUNTER_MODE_MANUAL:
		ret = __counter_set_mode(port_counter, RDMA_COUNTER_MODE_MANUAL,
					 0);
		if (ret) {
			mutex_unlock(&port_counter->lock);
			goto err_mode;
		}
		break;
	case RDMA_COUNTER_MODE_AUTO:
		auto_mode_init_counter(counter, qp, port_counter->mode.mask);
		break;
	default:
		ret = -EOPNOTSUPP;
		mutex_unlock(&port_counter->lock);
		goto err_mode;
	}

	port_counter->num_counters++;
	mutex_unlock(&port_counter->lock);

	counter->mode.mode = mode;
	kref_init(&counter->kref);
	mutex_init(&counter->lock);

	ret = __rdma_counter_bind_qp(counter, qp);
	if (ret)
		goto err_mode;

	rdma_restrack_parent_name(&counter->res, &qp->res);
	rdma_restrack_add(&counter->res);
	return counter;

err_mode:
	rdma_free_hw_stats_struct(counter->stats);
err_stats:
	rdma_restrack_put(&counter->res);
	kfree(counter);
	return NULL;
}

static void rdma_counter_free(struct rdma_counter *counter)
{
	struct rdma_port_counter *port_counter;

	port_counter = &counter->device->port_data[counter->port].port_counter;
	mutex_lock(&port_counter->lock);
	port_counter->num_counters--;
	if (!port_counter->num_counters &&
	    (port_counter->mode.mode == RDMA_COUNTER_MODE_MANUAL))
		__counter_set_mode(port_counter, RDMA_COUNTER_MODE_NONE, 0);

	mutex_unlock(&port_counter->lock);

	rdma_restrack_del(&counter->res);
	rdma_free_hw_stats_struct(counter->stats);
	kfree(counter);
}

static bool auto_mode_match(struct ib_qp *qp, struct rdma_counter *counter,
			    enum rdma_nl_counter_mask auto_mask)
{
	struct auto_mode_param *param = &counter->mode.param;
	bool match = true;

	if (auto_mask & RDMA_COUNTER_MASK_QP_TYPE)
		match &= (param->qp_type == qp->qp_type);

	if (auto_mask & RDMA_COUNTER_MASK_PID)
		match &= (task_pid_nr(counter->res.task) ==
			  task_pid_nr(qp->res.task));

	return match;
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

static void counter_history_stat_update(struct rdma_counter *counter)
{
	struct ib_device *dev = counter->device;
	struct rdma_port_counter *port_counter;
	int i;

	port_counter = &dev->port_data[counter->port].port_counter;
	if (!port_counter->hstats)
		return;

	rdma_counter_query_stats(counter);

	for (i = 0; i < counter->stats->num_counters; i++)
		port_counter->hstats->value[i] += counter->stats->value[i];
}

/*
 * rdma_get_counter_auto_mode - Find the counter that @qp should be bound
 *     with in auto mode
 *
 * Return: The counter (with ref-count increased) if found
 */
static struct rdma_counter *rdma_get_counter_auto_mode(struct ib_qp *qp,
						       u32 port)
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

static void counter_release(struct kref *kref)
{
	struct rdma_counter *counter;

	counter = container_of(kref, struct rdma_counter, kref);
	counter_history_stat_update(counter);
	counter->device->ops.counter_dealloc(counter);
	rdma_counter_free(counter);
}

/*
 * rdma_counter_bind_qp_auto - Check and bind the QP to a counter base on
 *   the auto-mode rule
 */
int rdma_counter_bind_qp_auto(struct ib_qp *qp, u32 port)
{
	struct rdma_port_counter *port_counter;
	struct ib_device *dev = qp->device;
	struct rdma_counter *counter;
	int ret;

	if (!rdma_restrack_is_tracked(&qp->res) || rdma_is_kernel_res(&qp->res))
		return 0;

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
		counter = alloc_and_bind(dev, port, qp, RDMA_COUNTER_MODE_AUTO);
		if (!counter)
			return -ENOMEM;
	}

	return 0;
}

/*
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
					   u32 port, u32 index)
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

/*
 * rdma_counter_get_hwstat_value() - Get the sum value of all counters on a
 *   specific port, including the running ones and history data
 */
u64 rdma_counter_get_hwstat_value(struct ib_device *dev, u32 port, u32 index)
{
	struct rdma_port_counter *port_counter;
	u64 sum;

	port_counter = &dev->port_data[port].port_counter;
	if (!port_counter->hstats)
		return 0;

	sum = get_running_counters_hwstat_sum(dev, port, index);
	sum += port_counter->hstats->value[index];

	return sum;
}

static struct ib_qp *rdma_counter_get_qp(struct ib_device *dev, u32 qp_num)
{
	struct rdma_restrack_entry *res = NULL;
	struct ib_qp *qp = NULL;

	res = rdma_restrack_get_byid(dev, RDMA_RESTRACK_QP, qp_num);
	if (IS_ERR(res))
		return NULL;

	qp = container_of(res, struct ib_qp, res);
	if (qp->qp_type == IB_QPT_RAW_PACKET && !capable(CAP_NET_RAW))
		goto err;

	return qp;

err:
	rdma_restrack_put(res);
	return NULL;
}

static struct rdma_counter *rdma_get_counter_by_id(struct ib_device *dev,
						   u32 counter_id)
{
	struct rdma_restrack_entry *res;
	struct rdma_counter *counter;

	res = rdma_restrack_get_byid(dev, RDMA_RESTRACK_COUNTER, counter_id);
	if (IS_ERR(res))
		return NULL;

	counter = container_of(res, struct rdma_counter, res);
	kref_get(&counter->kref);
	rdma_restrack_put(res);

	return counter;
}

/*
 * rdma_counter_bind_qpn() - Bind QP @qp_num to counter @counter_id
 */
int rdma_counter_bind_qpn(struct ib_device *dev, u32 port,
			  u32 qp_num, u32 counter_id)
{
	struct rdma_port_counter *port_counter;
	struct rdma_counter *counter;
	struct ib_qp *qp;
	int ret;

	port_counter = &dev->port_data[port].port_counter;
	if (port_counter->mode.mode == RDMA_COUNTER_MODE_AUTO)
		return -EINVAL;

	qp = rdma_counter_get_qp(dev, qp_num);
	if (!qp)
		return -ENOENT;

	counter = rdma_get_counter_by_id(dev, counter_id);
	if (!counter) {
		ret = -ENOENT;
		goto err;
	}

	if (rdma_is_kernel_res(&counter->res) != rdma_is_kernel_res(&qp->res)) {
		ret = -EINVAL;
		goto err_task;
	}

	if ((counter->device != qp->device) || (counter->port != qp->port)) {
		ret = -EINVAL;
		goto err_task;
	}

	ret = __rdma_counter_bind_qp(counter, qp);
	if (ret)
		goto err_task;

	rdma_restrack_put(&qp->res);
	return 0;

err_task:
	kref_put(&counter->kref, counter_release);
err:
	rdma_restrack_put(&qp->res);
	return ret;
}

/*
 * rdma_counter_bind_qpn_alloc() - Alloc a counter and bind QP @qp_num to it
 *   The id of new counter is returned in @counter_id
 */
int rdma_counter_bind_qpn_alloc(struct ib_device *dev, u32 port,
				u32 qp_num, u32 *counter_id)
{
	struct rdma_port_counter *port_counter;
	struct rdma_counter *counter;
	struct ib_qp *qp;
	int ret;

	if (!rdma_is_port_valid(dev, port))
		return -EINVAL;

	port_counter = &dev->port_data[port].port_counter;
	if (!port_counter->hstats)
		return -EOPNOTSUPP;

	if (port_counter->mode.mode == RDMA_COUNTER_MODE_AUTO)
		return -EINVAL;

	qp = rdma_counter_get_qp(dev, qp_num);
	if (!qp)
		return -ENOENT;

	if (rdma_is_port_valid(dev, qp->port) && (qp->port != port)) {
		ret = -EINVAL;
		goto err;
	}

	counter = alloc_and_bind(dev, port, qp, RDMA_COUNTER_MODE_MANUAL);
	if (!counter) {
		ret = -ENOMEM;
		goto err;
	}

	if (counter_id)
		*counter_id = counter->id;

	rdma_restrack_put(&qp->res);
	return 0;

err:
	rdma_restrack_put(&qp->res);
	return ret;
}

/*
 * rdma_counter_unbind_qpn() - Unbind QP @qp_num from a counter
 */
int rdma_counter_unbind_qpn(struct ib_device *dev, u32 port,
			    u32 qp_num, u32 counter_id)
{
	struct rdma_port_counter *port_counter;
	struct ib_qp *qp;
	int ret;

	if (!rdma_is_port_valid(dev, port))
		return -EINVAL;

	qp = rdma_counter_get_qp(dev, qp_num);
	if (!qp)
		return -ENOENT;

	if (rdma_is_port_valid(dev, qp->port) && (qp->port != port)) {
		ret = -EINVAL;
		goto out;
	}

	port_counter = &dev->port_data[port].port_counter;
	if (!qp->counter || qp->counter->id != counter_id ||
	    port_counter->mode.mode != RDMA_COUNTER_MODE_MANUAL) {
		ret = -EINVAL;
		goto out;
	}

	ret = rdma_counter_unbind_qp(qp, false);

out:
	rdma_restrack_put(&qp->res);
	return ret;
}

int rdma_counter_get_mode(struct ib_device *dev, u32 port,
			  enum rdma_nl_counter_mode *mode,
			  enum rdma_nl_counter_mask *mask)
{
	struct rdma_port_counter *port_counter;

	port_counter = &dev->port_data[port].port_counter;
	*mode = port_counter->mode.mode;
	*mask = port_counter->mode.mask;

	return 0;
}

void rdma_counter_init(struct ib_device *dev)
{
	struct rdma_port_counter *port_counter;
	u32 port, i;

	if (!dev->port_data)
		return;

	rdma_for_each_port(dev, port) {
		port_counter = &dev->port_data[port].port_counter;
		port_counter->mode.mode = RDMA_COUNTER_MODE_NONE;
		mutex_init(&port_counter->lock);

		if (!dev->ops.alloc_hw_port_stats)
			continue;

		port_counter->hstats = dev->ops.alloc_hw_port_stats(dev, port);
		if (!port_counter->hstats)
			goto fail;
	}

	return;

fail:
	for (i = port; i >= rdma_start_port(dev); i--) {
		port_counter = &dev->port_data[port].port_counter;
		rdma_free_hw_stats_struct(port_counter->hstats);
		port_counter->hstats = NULL;
		mutex_destroy(&port_counter->lock);
	}
}

void rdma_counter_release(struct ib_device *dev)
{
	struct rdma_port_counter *port_counter;
	u32 port;

	rdma_for_each_port(dev, port) {
		port_counter = &dev->port_data[port].port_counter;
		rdma_free_hw_stats_struct(port_counter->hstats);
		mutex_destroy(&port_counter->lock);
	}
}
