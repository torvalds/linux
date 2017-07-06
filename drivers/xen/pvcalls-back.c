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

#define PVCALLS_VERSIONS "1"
#define MAX_RING_ORDER XENBUS_MAX_RING_GRANT_ORDER

struct pvcalls_back_global {
	struct list_head frontends;
	struct semaphore frontends_lock;
} pvcalls_back_global;

static int backend_connect(struct xenbus_device *dev)
{
	return 0;
}

static int backend_disconnect(struct xenbus_device *dev)
{
	return 0;
}

static int pvcalls_back_probe(struct xenbus_device *dev,
			      const struct xenbus_device_id *id)
{
	int err, abort;
	struct xenbus_transaction xbt;

again:
	abort = 1;

	err = xenbus_transaction_start(&xbt);
	if (err) {
		pr_warn("%s cannot create xenstore transaction\n", __func__);
		return err;
	}

	err = xenbus_printf(xbt, dev->nodename, "versions", "%s",
			    PVCALLS_VERSIONS);
	if (err) {
		pr_warn("%s write out 'versions' failed\n", __func__);
		goto abort;
	}

	err = xenbus_printf(xbt, dev->nodename, "max-page-order", "%u",
			    MAX_RING_ORDER);
	if (err) {
		pr_warn("%s write out 'max-page-order' failed\n", __func__);
		goto abort;
	}

	err = xenbus_printf(xbt, dev->nodename, "function-calls",
			    XENBUS_FUNCTIONS_CALLS);
	if (err) {
		pr_warn("%s write out 'function-calls' failed\n", __func__);
		goto abort;
	}

	abort = 0;
abort:
	err = xenbus_transaction_end(xbt, abort);
	if (err) {
		if (err == -EAGAIN && !abort)
			goto again;
		pr_warn("%s cannot complete xenstore transaction\n", __func__);
		return err;
	}

	if (abort)
		return -EFAULT;

	xenbus_switch_state(dev, XenbusStateInitWait);

	return 0;
}

static void set_backend_state(struct xenbus_device *dev,
			      enum xenbus_state state)
{
	while (dev->state != state) {
		switch (dev->state) {
		case XenbusStateClosed:
			switch (state) {
			case XenbusStateInitWait:
			case XenbusStateConnected:
				xenbus_switch_state(dev, XenbusStateInitWait);
				break;
			case XenbusStateClosing:
				xenbus_switch_state(dev, XenbusStateClosing);
				break;
			default:
				__WARN();
			}
			break;
		case XenbusStateInitWait:
		case XenbusStateInitialised:
			switch (state) {
			case XenbusStateConnected:
				backend_connect(dev);
				xenbus_switch_state(dev, XenbusStateConnected);
				break;
			case XenbusStateClosing:
			case XenbusStateClosed:
				xenbus_switch_state(dev, XenbusStateClosing);
				break;
			default:
				__WARN();
			}
			break;
		case XenbusStateConnected:
			switch (state) {
			case XenbusStateInitWait:
			case XenbusStateClosing:
			case XenbusStateClosed:
				down(&pvcalls_back_global.frontends_lock);
				backend_disconnect(dev);
				up(&pvcalls_back_global.frontends_lock);
				xenbus_switch_state(dev, XenbusStateClosing);
				break;
			default:
				__WARN();
			}
			break;
		case XenbusStateClosing:
			switch (state) {
			case XenbusStateInitWait:
			case XenbusStateConnected:
			case XenbusStateClosed:
				xenbus_switch_state(dev, XenbusStateClosed);
				break;
			default:
				__WARN();
			}
			break;
		default:
			__WARN();
		}
	}
}

static void pvcalls_back_changed(struct xenbus_device *dev,
				 enum xenbus_state frontend_state)
{
	switch (frontend_state) {
	case XenbusStateInitialising:
		set_backend_state(dev, XenbusStateInitWait);
		break;

	case XenbusStateInitialised:
	case XenbusStateConnected:
		set_backend_state(dev, XenbusStateConnected);
		break;

	case XenbusStateClosing:
		set_backend_state(dev, XenbusStateClosing);
		break;

	case XenbusStateClosed:
		set_backend_state(dev, XenbusStateClosed);
		if (xenbus_dev_is_online(dev))
			break;
		device_unregister(&dev->dev);
		break;
	case XenbusStateUnknown:
		set_backend_state(dev, XenbusStateClosed);
		device_unregister(&dev->dev);
		break;

	default:
		xenbus_dev_fatal(dev, -EINVAL, "saw state %d at frontend",
				 frontend_state);
		break;
	}
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
