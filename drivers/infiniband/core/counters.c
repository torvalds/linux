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
	}
}

void rdma_counter_release(struct ib_device *dev)
{
}
