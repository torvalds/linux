/*
 * (c) 2017 Stefano Stabellini <stefano@aporeto.com>
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
 */

#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/radix-tree.h>
#include <linux/module.h>
#include <linux/semaphore.h>
#include <linux/wait.h>

#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/interface/io/pvcalls.h>

struct pvcalls_back_global {
	struct list_head frontends;
	struct semaphore frontends_lock;
} pvcalls_back_global;

static int pvcalls_back_probe(struct xenbus_device *dev,
			      const struct xenbus_device_id *id)
{
	return 0;
}

static void pvcalls_back_changed(struct xenbus_device *dev,
				 enum xenbus_state frontend_state)
{
}

static int pvcalls_back_remove(struct xenbus_device *dev)
{
	return 0;
}

static int pvcalls_back_uevent(struct xenbus_device *xdev,
			       struct kobj_uevent_env *env)
{
	return 0;
}

static const struct xenbus_device_id pvcalls_back_ids[] = {
	{ "pvcalls" },
	{ "" }
};

static struct xenbus_driver pvcalls_back_driver = {
	.ids = pvcalls_back_ids,
	.probe = pvcalls_back_probe,
	.remove = pvcalls_back_remove,
	.uevent = pvcalls_back_uevent,
	.otherend_changed = pvcalls_back_changed,
};

static int __init pvcalls_back_init(void)
{
	int ret;

	if (!xen_domain())
		return -ENODEV;

	ret = xenbus_register_backend(&pvcalls_back_driver);
	if (ret < 0)
		return ret;

	sema_init(&pvcalls_back_global.frontends_lock, 1);
	INIT_LIST_HEAD(&pvcalls_back_global.frontends);
	return 0;
}
module_init(pvcalls_back_init);
