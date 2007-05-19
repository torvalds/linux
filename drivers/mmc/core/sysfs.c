/*
 *  linux/drivers/mmc/core/sysfs.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  MMC sysfs/driver model support.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/workqueue.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>

#include "bus.h"
#include "sysfs.h"

#define to_mmc_driver(d)	container_of(d, struct mmc_driver, drv)
#define cls_dev_to_mmc_host(d)	container_of(d, struct mmc_host, class_dev)

int mmc_add_attrs(struct mmc_card *card, struct device_attribute *attrs)
{
	int error = 0;
	int i;

	for (i = 0; attr_name(attrs[i]); i++) {
		error = device_create_file(&card->dev, &attrs[i]);
		if (error) {
			while (--i >= 0)
				device_remove_file(&card->dev, &attrs[i]);
			break;
		}
	}

	return error;
}

void mmc_remove_attrs(struct mmc_card *card, struct device_attribute *attrs)
{
	int i;

	for (i = 0; attr_name(attrs[i]); i++)
		device_remove_file(&card->dev, &attrs[i]);
}

static void mmc_host_classdev_release(struct device *dev)
{
	struct mmc_host *host = cls_dev_to_mmc_host(dev);
	kfree(host);
}

static struct class mmc_host_class = {
	.name		= "mmc_host",
	.dev_release	= mmc_host_classdev_release,
};

static DEFINE_IDR(mmc_host_idr);
static DEFINE_SPINLOCK(mmc_host_lock);

/*
 * Internal function. Allocate a new MMC host.
 */
struct mmc_host *mmc_alloc_host_sysfs(int extra, struct device *dev)
{
	struct mmc_host *host;

	host = kmalloc(sizeof(struct mmc_host) + extra, GFP_KERNEL);
	if (host) {
		memset(host, 0, sizeof(struct mmc_host) + extra);

		host->parent = dev;
		host->class_dev.parent = dev;
		host->class_dev.class = &mmc_host_class;
		device_initialize(&host->class_dev);
	}

	return host;
}

/*
 * Internal function. Register a new MMC host with the MMC class.
 */
int mmc_add_host_sysfs(struct mmc_host *host)
{
	int err;

	if (!idr_pre_get(&mmc_host_idr, GFP_KERNEL))
		return -ENOMEM;

	spin_lock(&mmc_host_lock);
	err = idr_get_new(&mmc_host_idr, host, &host->index);
	spin_unlock(&mmc_host_lock);
	if (err)
		return err;

	snprintf(host->class_dev.bus_id, BUS_ID_SIZE,
		 "mmc%d", host->index);

	return device_add(&host->class_dev);
}

/*
 * Internal function. Unregister a MMC host with the MMC class.
 */
void mmc_remove_host_sysfs(struct mmc_host *host)
{
	device_del(&host->class_dev);

	spin_lock(&mmc_host_lock);
	idr_remove(&mmc_host_idr, host->index);
	spin_unlock(&mmc_host_lock);
}

/*
 * Internal function. Free a MMC host.
 */
void mmc_free_host_sysfs(struct mmc_host *host)
{
	put_device(&host->class_dev);
}

static struct workqueue_struct *workqueue;

/*
 * Internal function. Schedule delayed work in the MMC work queue.
 */
int mmc_schedule_delayed_work(struct delayed_work *work, unsigned long delay)
{
	return queue_delayed_work(workqueue, work, delay);
}

/*
 * Internal function. Flush all scheduled work from the MMC work queue.
 */
void mmc_flush_scheduled_work(void)
{
	flush_workqueue(workqueue);
}

static int __init mmc_init(void)
{
	int ret;

	workqueue = create_singlethread_workqueue("kmmcd");
	if (!workqueue)
		return -ENOMEM;

	ret = mmc_register_bus();
	if (ret == 0) {
		ret = class_register(&mmc_host_class);
		if (ret)
			mmc_unregister_bus();
	}
	return ret;
}

static void __exit mmc_exit(void)
{
	class_unregister(&mmc_host_class);
	mmc_unregister_bus();
	destroy_workqueue(workqueue);
}

module_init(mmc_init);
module_exit(mmc_exit);
