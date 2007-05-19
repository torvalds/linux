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
#include "host.h"
#include "sysfs.h"

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
		ret = mmc_register_host_class();
		if (ret)
			mmc_unregister_bus();
	}
	return ret;
}

static void __exit mmc_exit(void)
{
	mmc_unregister_host_class();
	mmc_unregister_bus();
	destroy_workqueue(workqueue);
}

module_init(mmc_init);
module_exit(mmc_exit);
