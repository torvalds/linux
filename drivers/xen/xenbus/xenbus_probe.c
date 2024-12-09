/******************************************************************************
 * Talks to Xen Store to figure out what devices we have.
 *
 * Copyright (C) 2005 Rusty Russell, IBM Corporation
 * Copyright (C) 2005 Mike Wray, Hewlett-Packard
 * Copyright (C) 2005, 2006 XenSource Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define dev_fmt pr_fmt

#define DPRINTK(fmt, args...)				\
	pr_debug("xenbus_probe (%s:%d) " fmt ".\n",	\
		 __func__, __LINE__, ##args)

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <asm/page.h>
#include <asm/xen/hypervisor.h>

#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/xen-ops.h>
#include <xen/page.h>

#include <xen/hvm.h>

#include "xenbus.h"


static int xs_init_irq = -1;
int xen_store_evtchn;
EXPORT_SYMBOL_GPL(xen_store_evtchn);

struct xenstore_domain_interface *xen_store_interface;
EXPORT_SYMBOL_GPL(xen_store_interface);

#define XS_INTERFACE_READY \
	((xen_store_interface != NULL) && \
	 (xen_store_interface->connection == XENSTORE_CONNECTED))

enum xenstore_init xen_store_domain_type;
EXPORT_SYMBOL_GPL(xen_store_domain_type);

static unsigned long xen_store_gfn;

static BLOCKING_NOTIFIER_HEAD(xenstore_chain);

/* If something in array of ids matches this device, return it. */
static const struct xenbus_device_id *
match_device(const struct xenbus_device_id *arr, struct xenbus_device *dev)
{
	for (; *arr->devicetype != '\0'; arr++) {
		if (!strcmp(arr->devicetype, dev->devicetype))
			return arr;
	}
	return NULL;
}

int xenbus_match(struct device *_dev, const struct device_driver *_drv)
{
	const struct xenbus_driver *drv = to_xenbus_driver(_drv);

	if (!drv->ids)
		return 0;

	return match_device(drv->ids, to_xenbus_device(_dev)) != NULL;
}
EXPORT_SYMBOL_GPL(xenbus_match);


static void free_otherend_details(struct xenbus_device *dev)
{
	kfree(dev->otherend);
	dev->otherend = NULL;
}


static void free_otherend_watch(struct xenbus_device *dev)
{
	if (dev->otherend_watch.node) {
		unregister_xenbus_watch(&dev->otherend_watch);
		kfree(dev->otherend_watch.node);
		dev->otherend_watch.node = NULL;
	}
}


static int talk_to_otherend(struct xenbus_device *dev)
{
	struct xenbus_driver *drv = to_xenbus_driver(dev->dev.driver);

	free_otherend_watch(dev);
	free_otherend_details(dev);

	return drv->read_otherend_details(dev);
}



static int watch_otherend(struct xenbus_device *dev)
{
	struct xen_bus_type *bus =
		container_of(dev->dev.bus, struct xen_bus_type, bus);

	return xenbus_watch_pathfmt(dev, &dev->otherend_watch,
				    bus->otherend_will_handle,
				    bus->otherend_changed,
				    "%s/%s", dev->otherend, "state");
}


int xenbus_read_otherend_details(struct xenbus_device *xendev,
				 char *id_node, char *path_node)
{
	int err = xenbus_gather(XBT_NIL, xendev->nodename,
				id_node, "%i", &xendev->otherend_id,
				path_node, NULL, &xendev->otherend,
				NULL);
	if (err) {
		xenbus_dev_fatal(xendev, err,
				 "reading other end details from %s",
				 xendev->nodename);
		return err;
	}
	if (strlen(xendev->otherend) == 0 ||
	    !xenbus_exists(XBT_NIL, xendev->otherend, "")) {
		xenbus_dev_fatal(xendev, -ENOENT,
				 "unable to read other end from %s.  "
				 "missing or inaccessible.",
				 xendev->nodename);
		free_otherend_details(xendev);
		return -ENOENT;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xenbus_read_otherend_details);

void xenbus_otherend_changed(struct xenbus_watch *watch,
			     const char *path, const char *token,
			     int ignore_on_shutdown)
{
	struct xenbus_device *dev =
		container_of(watch, struct xenbus_device, otherend_watch);
	struct xenbus_driver *drv = to_xenbus_driver(dev->dev.driver);
	enum xenbus_state state;

	/* Protect us against watches firing on old details when the otherend
	   details change, say immediately after a resume. */
	if (!dev->otherend ||
	    strncmp(dev->otherend, path, strlen(dev->otherend))) {
		dev_dbg(&dev->dev, "Ignoring watch at %s\n", path);
		return;
	}

	state = xenbus_read_driver_state(dev->otherend);

	dev_dbg(&dev->dev, "state is %d, (%s), %s, %s\n",
		state, xenbus_strstate(state), dev->otherend_watch.node, path);

	/*
	 * Ignore xenbus transitions during shutdown. This prevents us doing
	 * work that can fail e.g., when the rootfs is gone.
	 */
	if (system_state > SYSTEM_RUNNING) {
		if (ignore_on_shutdown && (state == XenbusStateClosing))
			xenbus_frontend_closed(dev);
		return;
	}

	if (drv->otherend_changed)
		drv->otherend_changed(dev, state);
}
EXPORT_SYMBOL_GPL(xenbus_otherend_changed);

#define XENBUS_SHOW_STAT(name)						\
static ssize_t name##_show(struct device *_dev,				\
			   struct device_attribute *attr,		\
			   char *buf)					\
{									\
	struct xenbus_device *dev = to_xenbus_device(_dev);		\
									\
	return sprintf(buf, "%d\n", atomic_read(&dev->name));		\
}									\
static DEVICE_ATTR_RO(name)

XENBUS_SHOW_STAT(event_channels);
XENBUS_SHOW_STAT(events);
XENBUS_SHOW_STAT(spurious_events);
XENBUS_SHOW_STAT(jiffies_eoi_delayed);

static ssize_t spurious_threshold_show(struct device *_dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct xenbus_device *dev = to_xenbus_device(_dev);

	return sprintf(buf, "%d\n", dev->spurious_threshold);
}

static ssize_t spurious_threshold_store(struct device *_dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct xenbus_device *dev = to_xenbus_device(_dev);
	unsigned int val;
	ssize_t ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	dev->spurious_threshold = val;

	return count;
}

static DEVICE_ATTR_RW(spurious_threshold);

static struct attribute *xenbus_attrs[] = {
	&dev_attr_event_channels.attr,
	&dev_attr_events.attr,
	&dev_attr_spurious_events.attr,
	&dev_attr_jiffies_eoi_delayed.attr,
	&dev_attr_spurious_threshold.attr,
	NULL
};

static const struct attribute_group xenbus_group = {
	.name = "xenbus",
	.attrs = xenbus_attrs,
};

int xenbus_dev_probe(struct device *_dev)
{
	struct xenbus_device *dev = to_xenbus_device(_dev);
	struct xenbus_driver *drv = to_xenbus_driver(_dev->driver);
	const struct xenbus_device_id *id;
	int err;

	DPRINTK("%s", dev->nodename);

	if (!drv->probe) {
		err = -ENODEV;
		goto fail;
	}

	id = match_device(drv->ids, dev);
	if (!id) {
		err = -ENODEV;
		goto fail;
	}

	err = talk_to_otherend(dev);
	if (err) {
		dev_warn(&dev->dev, "talk_to_otherend on %s failed.\n",
			 dev->nodename);
		return err;
	}

	if (!try_module_get(drv->driver.owner)) {
		dev_warn(&dev->dev, "failed to acquire module reference on '%s'\n",
			 drv->driver.name);
		err = -ESRCH;
		goto fail;
	}

	down(&dev->reclaim_sem);
	err = drv->probe(dev, id);
	up(&dev->reclaim_sem);
	if (err)
		goto fail_put;

	err = watch_otherend(dev);
	if (err) {
		dev_warn(&dev->dev, "watch_otherend on %s failed.\n",
		       dev->nodename);
		goto fail_remove;
	}

	dev->spurious_threshold = 1;
	if (sysfs_create_group(&dev->dev.kobj, &xenbus_group))
		dev_warn(&dev->dev, "sysfs_create_group on %s failed.\n",
			 dev->nodename);

	return 0;
fail_remove:
	if (drv->remove) {
		down(&dev->reclaim_sem);
		drv->remove(dev);
		up(&dev->reclaim_sem);
	}
fail_put:
	module_put(drv->driver.owner);
fail:
	xenbus_dev_error(dev, err, "xenbus_dev_probe on %s", dev->nodename);
	return err;
}
EXPORT_SYMBOL_GPL(xenbus_dev_probe);

void xenbus_dev_remove(struct device *_dev)
{
	struct xenbus_device *dev = to_xenbus_device(_dev);
	struct xenbus_driver *drv = to_xenbus_driver(_dev->driver);

	DPRINTK("%s", dev->nodename);

	sysfs_remove_group(&dev->dev.kobj, &xenbus_group);

	free_otherend_watch(dev);

	if (drv->remove) {
		down(&dev->reclaim_sem);
		drv->remove(dev);
		up(&dev->reclaim_sem);
	}

	module_put(drv->driver.owner);

	free_otherend_details(dev);

	/*
	 * If the toolstack has forced the device state to closing then set
	 * the state to closed now to allow it to be cleaned up.
	 * Similarly, if the driver does not support re-bind, set the
	 * closed.
	 */
	if (!drv->allow_rebind ||
	    xenbus_read_driver_state(dev->nodename) == XenbusStateClosing)
		xenbus_switch_state(dev, XenbusStateClosed);
}
EXPORT_SYMBOL_GPL(xenbus_dev_remove);

int xenbus_register_driver_common(struct xenbus_driver *drv,
				  struct xen_bus_type *bus,
				  struct module *owner, const char *mod_name)
{
	drv->driver.name = drv->name ? drv->name : drv->ids[0].devicetype;
	drv->driver.bus = &bus->bus;
	drv->driver.owner = owner;
	drv->driver.mod_name = mod_name;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(xenbus_register_driver_common);

void xenbus_unregister_driver(struct xenbus_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(xenbus_unregister_driver);

struct xb_find_info {
	struct xenbus_device *dev;
	const char *nodename;
};

static int cmp_dev(struct device *dev, void *data)
{
	struct xenbus_device *xendev = to_xenbus_device(dev);
	struct xb_find_info *info = data;

	if (!strcmp(xendev->nodename, info->nodename)) {
		info->dev = xendev;
		get_device(dev);
		return 1;
	}
	return 0;
}

static struct xenbus_device *xenbus_device_find(const char *nodename,
						struct bus_type *bus)
{
	struct xb_find_info info = { .dev = NULL, .nodename = nodename };

	bus_for_each_dev(bus, NULL, &info, cmp_dev);
	return info.dev;
}

static int cleanup_dev(struct device *dev, void *data)
{
	struct xenbus_device *xendev = to_xenbus_device(dev);
	struct xb_find_info *info = data;
	int len = strlen(info->nodename);

	DPRINTK("%s", info->nodename);

	/* Match the info->nodename path, or any subdirectory of that path. */
	if (strncmp(xendev->nodename, info->nodename, len))
		return 0;

	/* If the node name is longer, ensure it really is a subdirectory. */
	if ((strlen(xendev->nodename) > len) && (xendev->nodename[len] != '/'))
		return 0;

	info->dev = xendev;
	get_device(dev);
	return 1;
}

static void xenbus_cleanup_devices(const char *path, struct bus_type *bus)
{
	struct xb_find_info info = { .nodename = path };

	do {
		info.dev = NULL;
		bus_for_each_dev(bus, NULL, &info, cleanup_dev);
		if (info.dev) {
			device_unregister(&info.dev->dev);
			put_device(&info.dev->dev);
		}
	} while (info.dev);
}

static void xenbus_dev_release(struct device *dev)
{
	if (dev)
		kfree(to_xenbus_device(dev));
}

static ssize_t nodename_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", to_xenbus_device(dev)->nodename);
}
static DEVICE_ATTR_RO(nodename);

static ssize_t devtype_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", to_xenbus_device(dev)->devicetype);
}
static DEVICE_ATTR_RO(devtype);

static ssize_t modalias_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s:%s\n", dev->bus->name,
		       to_xenbus_device(dev)->devicetype);
}
static DEVICE_ATTR_RO(modalias);

static ssize_t state_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n",
			xenbus_strstate(to_xenbus_device(dev)->state));
}
static DEVICE_ATTR_RO(state);

static struct attribute *xenbus_dev_attrs[] = {
	&dev_attr_nodename.attr,
	&dev_attr_devtype.attr,
	&dev_attr_modalias.attr,
	&dev_attr_state.attr,
	NULL,
};

static const struct attribute_group xenbus_dev_group = {
	.attrs = xenbus_dev_attrs,
};

const struct attribute_group *xenbus_dev_groups[] = {
	&xenbus_dev_group,
	NULL,
};
EXPORT_SYMBOL_GPL(xenbus_dev_groups);

int xenbus_probe_node(struct xen_bus_type *bus,
		      const char *type,
		      const char *nodename)
{
	char devname[XEN_BUS_ID_SIZE];
	int err;
	struct xenbus_device *xendev;
	size_t stringlen;
	char *tmpstring;

	enum xenbus_state state = xenbus_read_driver_state(nodename);

	if (state != XenbusStateInitialising) {
		/* Device is not new, so ignore it.  This can happen if a
		   device is going away after switching to Closed.  */
		return 0;
	}

	stringlen = strlen(nodename) + 1 + strlen(type) + 1;
	xendev = kzalloc(sizeof(*xendev) + stringlen, GFP_KERNEL);
	if (!xendev)
		return -ENOMEM;

	xendev->state = XenbusStateInitialising;

	/* Copy the strings into the extra space. */

	tmpstring = (char *)(xendev + 1);
	strcpy(tmpstring, nodename);
	xendev->nodename = tmpstring;

	tmpstring += strlen(tmpstring) + 1;
	strcpy(tmpstring, type);
	xendev->devicetype = tmpstring;
	init_completion(&xendev->down);

	xendev->dev.bus = &bus->bus;
	xendev->dev.release = xenbus_dev_release;

	err = bus->get_bus_id(devname, xendev->nodename);
	if (err)
		goto fail;

	dev_set_name(&xendev->dev, "%s", devname);
	sema_init(&xendev->reclaim_sem, 1);

	/* Register with generic device framework. */
	err = device_register(&xendev->dev);
	if (err) {
		put_device(&xendev->dev);
		xendev = NULL;
		goto fail;
	}

	return 0;
fail:
	kfree(xendev);
	return err;
}
EXPORT_SYMBOL_GPL(xenbus_probe_node);

static int xenbus_probe_device_type(struct xen_bus_type *bus, const char *type)
{
	int err = 0;
	char **dir;
	unsigned int dir_n = 0;
	int i;

	dir = xenbus_directory(XBT_NIL, bus->root, type, &dir_n);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	for (i = 0; i < dir_n; i++) {
		err = bus->probe(bus, type, dir[i]);
		if (err)
			break;
	}

	kfree(dir);
	return err;
}

int xenbus_probe_devices(struct xen_bus_type *bus)
{
	int err = 0;
	char **dir;
	unsigned int i, dir_n;

	dir = xenbus_directory(XBT_NIL, bus->root, "", &dir_n);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	for (i = 0; i < dir_n; i++) {
		err = xenbus_probe_device_type(bus, dir[i]);
		if (err)
			break;
	}

	kfree(dir);
	return err;
}
EXPORT_SYMBOL_GPL(xenbus_probe_devices);

static unsigned int char_count(const char *str, char c)
{
	unsigned int i, ret = 0;

	for (i = 0; str[i]; i++)
		if (str[i] == c)
			ret++;
	return ret;
}

static int strsep_len(const char *str, char c, unsigned int len)
{
	unsigned int i;

	for (i = 0; str[i]; i++)
		if (str[i] == c) {
			if (len == 0)
				return i;
			len--;
		}
	return (len == 0) ? i : -ERANGE;
}

void xenbus_dev_changed(const char *node, struct xen_bus_type *bus)
{
	int exists, rootlen;
	struct xenbus_device *dev;
	char type[XEN_BUS_ID_SIZE];
	const char *p, *root;

	if (char_count(node, '/') < 2)
		return;

	exists = xenbus_exists(XBT_NIL, node, "");
	if (!exists) {
		xenbus_cleanup_devices(node, &bus->bus);
		return;
	}

	/* backend/<type>/... or device/<type>/... */
	p = strchr(node, '/') + 1;
	snprintf(type, XEN_BUS_ID_SIZE, "%.*s", (int)strcspn(p, "/"), p);
	type[XEN_BUS_ID_SIZE-1] = '\0';

	rootlen = strsep_len(node, '/', bus->levels);
	if (rootlen < 0)
		return;
	root = kasprintf(GFP_KERNEL, "%.*s", rootlen, node);
	if (!root)
		return;

	dev = xenbus_device_find(root, &bus->bus);
	if (!dev)
		xenbus_probe_node(bus, type, root);
	else
		put_device(&dev->dev);

	kfree(root);
}
EXPORT_SYMBOL_GPL(xenbus_dev_changed);

int xenbus_dev_suspend(struct device *dev)
{
	int err = 0;
	struct xenbus_driver *drv;
	struct xenbus_device *xdev
		= container_of(dev, struct xenbus_device, dev);

	DPRINTK("%s", xdev->nodename);

	if (dev->driver == NULL)
		return 0;
	drv = to_xenbus_driver(dev->driver);
	if (drv->suspend)
		err = drv->suspend(xdev);
	if (err)
		dev_warn(dev, "suspend failed: %i\n", err);
	return 0;
}
EXPORT_SYMBOL_GPL(xenbus_dev_suspend);

int xenbus_dev_resume(struct device *dev)
{
	int err;
	struct xenbus_driver *drv;
	struct xenbus_device *xdev
		= container_of(dev, struct xenbus_device, dev);

	DPRINTK("%s", xdev->nodename);

	if (dev->driver == NULL)
		return 0;
	drv = to_xenbus_driver(dev->driver);
	err = talk_to_otherend(xdev);
	if (err) {
		dev_warn(dev, "resume (talk_to_otherend) failed: %i\n", err);
		return err;
	}

	xdev->state = XenbusStateInitialising;

	if (drv->resume) {
		err = drv->resume(xdev);
		if (err) {
			dev_warn(dev, "resume failed: %i\n", err);
			return err;
		}
	}

	err = watch_otherend(xdev);
	if (err) {
		dev_warn(dev, "resume (watch_otherend) failed: %d\n", err);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xenbus_dev_resume);

int xenbus_dev_cancel(struct device *dev)
{
	/* Do nothing */
	DPRINTK("cancel");
	return 0;
}
EXPORT_SYMBOL_GPL(xenbus_dev_cancel);

/* A flag to determine if xenstored is 'ready' (i.e. has started) */
int xenstored_ready;


int register_xenstore_notifier(struct notifier_block *nb)
{
	int ret = 0;

	if (xenstored_ready > 0)
		ret = nb->notifier_call(nb, 0, NULL);
	else
		blocking_notifier_chain_register(&xenstore_chain, nb);

	return ret;
}
EXPORT_SYMBOL_GPL(register_xenstore_notifier);

void unregister_xenstore_notifier(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&xenstore_chain, nb);
}
EXPORT_SYMBOL_GPL(unregister_xenstore_notifier);

static void xenbus_probe(void)
{
	xenstored_ready = 1;

	if (!xen_store_interface)
		xen_store_interface = memremap(xen_store_gfn << XEN_PAGE_SHIFT,
					       XEN_PAGE_SIZE, MEMREMAP_WB);
	/*
	 * Now it is safe to free the IRQ used for xenstore late
	 * initialization. No need to unbind: it is about to be
	 * bound again from xb_init_comms. Note that calling
	 * unbind_from_irqhandler now would result in xen_evtchn_close()
	 * being called and the event channel not being enabled again
	 * afterwards, resulting in missed event notifications.
	 */
	if (xs_init_irq >= 0)
		free_irq(xs_init_irq, &xb_waitq);

	/*
	 * In the HVM case, xenbus_init() deferred its call to
	 * xs_init() in case callbacks were not operational yet.
	 * So do it now.
	 */
	if (xen_store_domain_type == XS_HVM)
		xs_init();

	/* Notify others that xenstore is up */
	blocking_notifier_call_chain(&xenstore_chain, 0, NULL);
}

/*
 * Returns true when XenStore init must be deferred in order to
 * allow the PCI platform device to be initialised, before we
 * can actually have event channel interrupts working.
 */
static bool xs_hvm_defer_init_for_callback(void)
{
#ifdef CONFIG_XEN_PVHVM
	return xen_store_domain_type == XS_HVM &&
		!xen_have_vector_callback;
#else
	return false;
#endif
}

static int xenbus_probe_thread(void *unused)
{
	DEFINE_WAIT(w);

	/*
	 * We actually just want to wait for *any* trigger of xb_waitq,
	 * and run xenbus_probe() the moment it occurs.
	 */
	prepare_to_wait(&xb_waitq, &w, TASK_INTERRUPTIBLE);
	schedule();
	finish_wait(&xb_waitq, &w);

	DPRINTK("probing");
	xenbus_probe();
	return 0;
}

static int __init xenbus_probe_initcall(void)
{
	if (!xen_domain())
		return -ENODEV;

	/*
	 * Probe XenBus here in the XS_PV case, and also XS_HVM unless we
	 * need to wait for the platform PCI device to come up or
	 * xen_store_interface is not ready.
	 */
	if (xen_store_domain_type == XS_PV ||
	    (xen_store_domain_type == XS_HVM &&
	     !xs_hvm_defer_init_for_callback() &&
	     XS_INTERFACE_READY))
		xenbus_probe();

	/*
	 * For XS_LOCAL or when xen_store_interface is not ready, spawn a
	 * thread which will wait for xenstored or a xenstore-stubdom to be
	 * started, then probe.  It will be triggered when communication
	 * starts happening, by waiting on xb_waitq.
	 */
	if (xen_store_domain_type == XS_LOCAL || !XS_INTERFACE_READY) {
		struct task_struct *probe_task;

		probe_task = kthread_run(xenbus_probe_thread, NULL,
					 "xenbus_probe");
		if (IS_ERR(probe_task))
			return PTR_ERR(probe_task);
	}
	return 0;
}
device_initcall(xenbus_probe_initcall);

int xen_set_callback_via(uint64_t via)
{
	struct xen_hvm_param a;
	int ret;

	a.domid = DOMID_SELF;
	a.index = HVM_PARAM_CALLBACK_IRQ;
	a.value = via;

	ret = HYPERVISOR_hvm_op(HVMOP_set_param, &a);
	if (ret)
		return ret;

	/*
	 * If xenbus_probe_initcall() deferred the xenbus_probe()
	 * due to the callback not functioning yet, we can do it now.
	 */
	if (!xenstored_ready && xs_hvm_defer_init_for_callback())
		xenbus_probe();

	return ret;
}
EXPORT_SYMBOL_GPL(xen_set_callback_via);

/* Set up event channel for xenstored which is run as a local process
 * (this is normally used only in dom0)
 */
static int __init xenstored_local_init(void)
{
	int err = -ENOMEM;
	unsigned long page = 0;
	struct evtchn_alloc_unbound alloc_unbound;

	/* Allocate Xenstore page */
	page = get_zeroed_page(GFP_KERNEL);
	if (!page)
		goto out_err;

	xen_store_gfn = virt_to_gfn((void *)page);

	/* Next allocate a local port which xenstored can bind to */
	alloc_unbound.dom        = DOMID_SELF;
	alloc_unbound.remote_dom = DOMID_SELF;

	err = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound,
					  &alloc_unbound);
	if (err == -ENOSYS)
		goto out_err;

	BUG_ON(err);
	xen_store_evtchn = alloc_unbound.port;

	return 0;

 out_err:
	if (page != 0)
		free_page(page);
	return err;
}

static int xenbus_resume_cb(struct notifier_block *nb,
			    unsigned long action, void *data)
{
	int err = 0;

	if (xen_hvm_domain()) {
		uint64_t v = 0;

		err = hvm_get_parameter(HVM_PARAM_STORE_EVTCHN, &v);
		if (!err && v)
			xen_store_evtchn = v;
		else
			pr_warn("Cannot update xenstore event channel: %d\n",
				err);
	} else
		xen_store_evtchn = xen_start_info->store_evtchn;

	return err;
}

static struct notifier_block xenbus_resume_nb = {
	.notifier_call = xenbus_resume_cb,
};

static irqreturn_t xenbus_late_init(int irq, void *unused)
{
	int err;
	uint64_t v = 0;

	err = hvm_get_parameter(HVM_PARAM_STORE_PFN, &v);
	if (err || !v || !~v)
		return IRQ_HANDLED;
	xen_store_gfn = (unsigned long)v;

	wake_up(&xb_waitq);
	return IRQ_HANDLED;
}

static int __init xenbus_init(void)
{
	int err;
	uint64_t v = 0;
	bool wait = false;
	xen_store_domain_type = XS_UNKNOWN;

	if (!xen_domain())
		return -ENODEV;

	xenbus_ring_ops_init();

	if (xen_pv_domain())
		xen_store_domain_type = XS_PV;
	if (xen_hvm_domain())
		xen_store_domain_type = XS_HVM;
	if (xen_hvm_domain() && xen_initial_domain())
		xen_store_domain_type = XS_LOCAL;
	if (xen_pv_domain() && !xen_start_info->store_evtchn)
		xen_store_domain_type = XS_LOCAL;
	if (xen_pv_domain() && xen_start_info->store_evtchn)
		xenstored_ready = 1;

	switch (xen_store_domain_type) {
	case XS_LOCAL:
		err = xenstored_local_init();
		if (err)
			goto out_error;
		xen_store_interface = gfn_to_virt(xen_store_gfn);
		break;
	case XS_PV:
		xen_store_evtchn = xen_start_info->store_evtchn;
		xen_store_gfn = xen_start_info->store_mfn;
		xen_store_interface = gfn_to_virt(xen_store_gfn);
		break;
	case XS_HVM:
		err = hvm_get_parameter(HVM_PARAM_STORE_EVTCHN, &v);
		if (err)
			goto out_error;
		xen_store_evtchn = (int)v;
		err = hvm_get_parameter(HVM_PARAM_STORE_PFN, &v);
		if (err)
			goto out_error;
		/*
		 * Uninitialized hvm_params are zero and return no error.
		 * Although it is theoretically possible to have
		 * HVM_PARAM_STORE_PFN set to zero on purpose, in reality it is
		 * not zero when valid. If zero, it means that Xenstore hasn't
		 * been properly initialized. Instead of attempting to map a
		 * wrong guest physical address return error.
		 *
		 * Also recognize all bits set as an invalid/uninitialized value.
		 */
		if (!v) {
			err = -ENOENT;
			goto out_error;
		}
		if (v == ~0ULL) {
			wait = true;
		} else {
			/* Avoid truncation on 32-bit. */
#if BITS_PER_LONG == 32
			if (v > ULONG_MAX) {
				pr_err("%s: cannot handle HVM_PARAM_STORE_PFN=%llx > ULONG_MAX\n",
				       __func__, v);
				err = -EINVAL;
				goto out_error;
			}
#endif
			xen_store_gfn = (unsigned long)v;
			xen_store_interface =
				memremap(xen_store_gfn << XEN_PAGE_SHIFT,
					 XEN_PAGE_SIZE, MEMREMAP_WB);
			if (!xen_store_interface) {
				pr_err("%s: cannot map HVM_PARAM_STORE_PFN=%llx\n",
				       __func__, v);
				err = -EINVAL;
				goto out_error;
			}
			if (xen_store_interface->connection != XENSTORE_CONNECTED)
				wait = true;
		}
		if (wait) {
			err = bind_evtchn_to_irqhandler(xen_store_evtchn,
							xenbus_late_init,
							0, "xenstore_late_init",
							&xb_waitq);
			if (err < 0) {
				pr_err("xenstore_late_init couldn't bind irq err=%d\n",
				       err);
				goto out_error;
			}

			xs_init_irq = err;
		}
		break;
	default:
		pr_warn("Xenstore state unknown\n");
		break;
	}

	/*
	 * HVM domains may not have a functional callback yet. In that
	 * case let xs_init() be called from xenbus_probe(), which will
	 * get invoked at an appropriate time.
	 */
	if (xen_store_domain_type != XS_HVM) {
		err = xs_init();
		if (err) {
			pr_warn("Error initializing xenstore comms: %i\n", err);
			goto out_error;
		}
	}

	if ((xen_store_domain_type != XS_LOCAL) &&
	    (xen_store_domain_type != XS_UNKNOWN))
		xen_resume_notifier_register(&xenbus_resume_nb);

#ifdef CONFIG_XEN_COMPAT_XENFS
	/*
	 * Create xenfs mountpoint in /proc for compatibility with
	 * utilities that expect to find "xenbus" under "/proc/xen".
	 */
	proc_create_mount_point("xen");
#endif
	return 0;

out_error:
	xen_store_domain_type = XS_UNKNOWN;
	return err;
}

postcore_initcall(xenbus_init);

MODULE_LICENSE("GPL");
