/*
 * Thunderbolt bus support
 *
 * Copyright (C) 2017, Intel Corporation
 * Author:  Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "tb.h"

static DEFINE_IDA(tb_domain_ida);

struct bus_type tb_bus_type = {
	.name = "thunderbolt",
};

static void tb_domain_release(struct device *dev)
{
	struct tb *tb = container_of(dev, struct tb, dev);

	tb_ctl_free(tb->ctl);
	destroy_workqueue(tb->wq);
	ida_simple_remove(&tb_domain_ida, tb->index);
	mutex_destroy(&tb->lock);
	kfree(tb);
}

struct device_type tb_domain_type = {
	.name = "thunderbolt_domain",
	.release = tb_domain_release,
};

/**
 * tb_domain_alloc() - Allocate a domain
 * @nhi: Pointer to the host controller
 * @privsize: Size of the connection manager private data
 *
 * Allocates and initializes a new Thunderbolt domain. Connection
 * managers are expected to call this and then fill in @cm_ops
 * accordingly.
 *
 * Call tb_domain_put() to release the domain before it has been added
 * to the system.
 *
 * Return: allocated domain structure on %NULL in case of error
 */
struct tb *tb_domain_alloc(struct tb_nhi *nhi, size_t privsize)
{
	struct tb *tb;

	/*
	 * Make sure the structure sizes map with that the hardware
	 * expects because bit-fields are being used.
	 */
	BUILD_BUG_ON(sizeof(struct tb_regs_switch_header) != 5 * 4);
	BUILD_BUG_ON(sizeof(struct tb_regs_port_header) != 8 * 4);
	BUILD_BUG_ON(sizeof(struct tb_regs_hop) != 2 * 4);

	tb = kzalloc(sizeof(*tb) + privsize, GFP_KERNEL);
	if (!tb)
		return NULL;

	tb->nhi = nhi;
	mutex_init(&tb->lock);

	tb->index = ida_simple_get(&tb_domain_ida, 0, 0, GFP_KERNEL);
	if (tb->index < 0)
		goto err_free;

	tb->wq = alloc_ordered_workqueue("thunderbolt%d", 0, tb->index);
	if (!tb->wq)
		goto err_remove_ida;

	tb->dev.parent = &nhi->pdev->dev;
	tb->dev.bus = &tb_bus_type;
	tb->dev.type = &tb_domain_type;
	dev_set_name(&tb->dev, "domain%d", tb->index);
	device_initialize(&tb->dev);

	return tb;

err_remove_ida:
	ida_simple_remove(&tb_domain_ida, tb->index);
err_free:
	kfree(tb);

	return NULL;
}

static void tb_domain_event_cb(void *data, enum tb_cfg_pkg_type type,
			       const void *buf, size_t size)
{
	struct tb *tb = data;

	if (!tb->cm_ops->handle_event) {
		tb_warn(tb, "domain does not have event handler\n");
		return;
	}

	tb->cm_ops->handle_event(tb, type, buf, size);
}

/**
 * tb_domain_add() - Add domain to the system
 * @tb: Domain to add
 *
 * Starts the domain and adds it to the system. Hotplugging devices will
 * work after this has been returned successfully. In order to remove
 * and release the domain after this function has been called, call
 * tb_domain_remove().
 *
 * Return: %0 in case of success and negative errno in case of error
 */
int tb_domain_add(struct tb *tb)
{
	int ret;

	if (WARN_ON(!tb->cm_ops))
		return -EINVAL;

	mutex_lock(&tb->lock);

	tb->ctl = tb_ctl_alloc(tb->nhi, tb_domain_event_cb, tb);
	if (!tb->ctl) {
		ret = -ENOMEM;
		goto err_unlock;
	}

	/*
	 * tb_schedule_hotplug_handler may be called as soon as the config
	 * channel is started. Thats why we have to hold the lock here.
	 */
	tb_ctl_start(tb->ctl);

	ret = device_add(&tb->dev);
	if (ret)
		goto err_ctl_stop;

	/* Start the domain */
	if (tb->cm_ops->start) {
		ret = tb->cm_ops->start(tb);
		if (ret)
			goto err_domain_del;
	}

	/* This starts event processing */
	mutex_unlock(&tb->lock);

	return 0;

err_domain_del:
	device_del(&tb->dev);
err_ctl_stop:
	tb_ctl_stop(tb->ctl);
err_unlock:
	mutex_unlock(&tb->lock);

	return ret;
}

/**
 * tb_domain_remove() - Removes and releases a domain
 * @tb: Domain to remove
 *
 * Stops the domain, removes it from the system and releases all
 * resources once the last reference has been released.
 */
void tb_domain_remove(struct tb *tb)
{
	mutex_lock(&tb->lock);
	if (tb->cm_ops->stop)
		tb->cm_ops->stop(tb);
	/* Stop the domain control traffic */
	tb_ctl_stop(tb->ctl);
	mutex_unlock(&tb->lock);

	flush_workqueue(tb->wq);
	device_unregister(&tb->dev);
}

/**
 * tb_domain_suspend_noirq() - Suspend a domain
 * @tb: Domain to suspend
 *
 * Suspends all devices in the domain and stops the control channel.
 */
int tb_domain_suspend_noirq(struct tb *tb)
{
	int ret = 0;

	/*
	 * The control channel interrupt is left enabled during suspend
	 * and taking the lock here prevents any events happening before
	 * we actually have stopped the domain and the control channel.
	 */
	mutex_lock(&tb->lock);
	if (tb->cm_ops->suspend_noirq)
		ret = tb->cm_ops->suspend_noirq(tb);
	if (!ret)
		tb_ctl_stop(tb->ctl);
	mutex_unlock(&tb->lock);

	return ret;
}

/**
 * tb_domain_resume_noirq() - Resume a domain
 * @tb: Domain to resume
 *
 * Re-starts the control channel, and resumes all devices connected to
 * the domain.
 */
int tb_domain_resume_noirq(struct tb *tb)
{
	int ret = 0;

	mutex_lock(&tb->lock);
	tb_ctl_start(tb->ctl);
	if (tb->cm_ops->resume_noirq)
		ret = tb->cm_ops->resume_noirq(tb);
	mutex_unlock(&tb->lock);

	return ret;
}

int tb_domain_init(void)
{
	return bus_register(&tb_bus_type);
}

void tb_domain_exit(void)
{
	bus_unregister(&tb_bus_type);
	ida_destroy(&tb_domain_ida);
}
