/*
 *	w1_int.c
 *
 * Copyright (c) 2004 Evgeniy Polyakov <zbr@ioremap.net>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/moduleparam.h>

#include "w1.h"
#include "w1_log.h"
#include "w1_netlink.h"
#include "w1_int.h"

static int w1_search_count = -1; /* Default is continual scan */
module_param_named(search_count, w1_search_count, int, 0);

static int w1_enable_pullup = 1;
module_param_named(enable_pullup, w1_enable_pullup, int, 0);

static struct w1_master *w1_alloc_dev(u32 id, int slave_count, int slave_ttl,
				       struct device_driver *driver,
				       struct device *device)
{
	struct w1_master *dev;
	int err;

	/*
	 * We are in process context(kernel thread), so can sleep.
	 */
	dev = kzalloc(sizeof(struct w1_master) + sizeof(struct w1_bus_master), GFP_KERNEL);
	if (!dev) {
		pr_err("Failed to allocate %zd bytes for new w1 device.\n",
			sizeof(struct w1_master));
		return NULL;
	}


	dev->bus_master = (struct w1_bus_master *)(dev + 1);

	dev->owner		= THIS_MODULE;
	dev->max_slave_count	= slave_count;
	dev->slave_count	= 0;
	dev->attempts		= 0;
	dev->initialized	= 0;
	dev->id			= id;
	dev->slave_ttl		= slave_ttl;
	dev->search_count	= w1_search_count;
	dev->enable_pullup	= w1_enable_pullup;

	/* 1 for w1_process to decrement
	 * 1 for __w1_remove_master_device to decrement
	 */
	atomic_set(&dev->refcnt, 2);

	INIT_LIST_HEAD(&dev->slist);
	INIT_LIST_HEAD(&dev->async_list);
	mutex_init(&dev->mutex);
	mutex_init(&dev->bus_mutex);
	mutex_init(&dev->list_mutex);

	memcpy(&dev->dev, device, sizeof(struct device));
	dev_set_name(&dev->dev, "w1_bus_master%u", dev->id);
	snprintf(dev->name, sizeof(dev->name), "w1_bus_master%u", dev->id);
	dev->dev.init_name = dev->name;

	dev->driver = driver;

	dev->seq = 1;

	err = device_register(&dev->dev);
	if (err) {
		pr_err("Failed to register master device. err=%d\n", err);
		put_device(&dev->dev);
		dev = NULL;
	}

	return dev;
}

static void w1_free_dev(struct w1_master *dev)
{
	device_unregister(&dev->dev);
}

/**
 * w1_add_master_device() - registers a new master device
 * @master:	master bus device to register
 */
int w1_add_master_device(struct w1_bus_master *master)
{
	struct w1_master *dev, *entry;
	int retval = 0;
	struct w1_netlink_msg msg;
	int id, found;

	/* validate minimum functionality */
	if (!(master->touch_bit && master->reset_bus) &&
	    !(master->write_bit && master->read_bit) &&
	    !(master->write_byte && master->read_byte && master->reset_bus)) {
		pr_err("w1_add_master_device: invalid function set\n");
		return(-EINVAL);
	}

	/* Lock until the device is added (or not) to w1_masters. */
	mutex_lock(&w1_mlock);
	/* Search for the first available id (starting at 1). */
	id = 0;
	do {
		++id;
		found = 0;
		list_for_each_entry(entry, &w1_masters, w1_master_entry) {
			if (entry->id == id) {
				found = 1;
				break;
			}
		}
	} while (found);

	dev = w1_alloc_dev(id, w1_max_slave_count, w1_max_slave_ttl,
		&w1_master_driver, &w1_master_device);
	if (!dev) {
		mutex_unlock(&w1_mlock);
		return -ENOMEM;
	}

	retval =  w1_create_master_attributes(dev);
	if (retval) {
		mutex_unlock(&w1_mlock);
		goto err_out_free_dev;
	}

	memcpy(dev->bus_master, master, sizeof(struct w1_bus_master));

	dev->initialized = 1;

	dev->thread = kthread_run(&w1_process, dev, "%s", dev->name);
	if (IS_ERR(dev->thread)) {
		retval = PTR_ERR(dev->thread);
		dev_err(&dev->dev,
			 "Failed to create new kernel thread. err=%d\n",
			 retval);
		mutex_unlock(&w1_mlock);
		goto err_out_rm_attr;
	}

	list_add(&dev->w1_master_entry, &w1_masters);
	mutex_unlock(&w1_mlock);

	memset(&msg, 0, sizeof(msg));
	msg.id.mst.id = dev->id;
	msg.type = W1_MASTER_ADD;
	w1_netlink_send(dev, &msg);

	return 0;

#if 0 /* Thread cleanup code, not required currently. */
err_out_kill_thread:
	set_bit(W1_ABORT_SEARCH, &dev->flags);
	kthread_stop(dev->thread);
#endif
err_out_rm_attr:
	w1_destroy_master_attributes(dev);
err_out_free_dev:
	w1_free_dev(dev);

	return retval;
}

void __w1_remove_master_device(struct w1_master *dev)
{
	struct w1_netlink_msg msg;
	struct w1_slave *sl, *sln;

	mutex_lock(&w1_mlock);
	list_del(&dev->w1_master_entry);
	mutex_unlock(&w1_mlock);

	set_bit(W1_ABORT_SEARCH, &dev->flags);
	kthread_stop(dev->thread);

	mutex_lock(&dev->mutex);
	mutex_lock(&dev->list_mutex);
	list_for_each_entry_safe(sl, sln, &dev->slist, w1_slave_entry) {
		mutex_unlock(&dev->list_mutex);
		w1_slave_detach(sl);
		mutex_lock(&dev->list_mutex);
	}
	w1_destroy_master_attributes(dev);
	mutex_unlock(&dev->list_mutex);
	mutex_unlock(&dev->mutex);
	atomic_dec(&dev->refcnt);

	while (atomic_read(&dev->refcnt)) {
		dev_info(&dev->dev, "Waiting for %s to become free: refcnt=%d.\n",
				dev->name, atomic_read(&dev->refcnt));

		if (msleep_interruptible(1000))
			flush_signals(current);
		mutex_lock(&dev->list_mutex);
		w1_process_callbacks(dev);
		mutex_unlock(&dev->list_mutex);
	}
	mutex_lock(&dev->list_mutex);
	w1_process_callbacks(dev);
	mutex_unlock(&dev->list_mutex);

	memset(&msg, 0, sizeof(msg));
	msg.id.mst.id = dev->id;
	msg.type = W1_MASTER_REMOVE;
	w1_netlink_send(dev, &msg);

	w1_free_dev(dev);
}

/**
 * w1_remove_master_device() - unregister a master device
 * @bm:	master bus device to remove
 */
void w1_remove_master_device(struct w1_bus_master *bm)
{
	struct w1_master *dev, *found = NULL;

	list_for_each_entry(dev, &w1_masters, w1_master_entry) {
		if (!dev->initialized)
			continue;

		if (dev->bus_master->data == bm->data) {
			found = dev;
			break;
		}
	}

	if (!found) {
		pr_err("Device doesn't exist.\n");
		return;
	}

	__w1_remove_master_device(found);
}

EXPORT_SYMBOL(w1_add_master_device);
EXPORT_SYMBOL(w1_remove_master_device);
