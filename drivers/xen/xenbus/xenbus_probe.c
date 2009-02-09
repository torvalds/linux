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

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/xen/hypervisor.h>

#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/page.h>

#include <xen/hvm.h>

#include "xenbus_comms.h"
#include "xenbus_probe.h"


int xen_store_evtchn;
EXPORT_SYMBOL_GPL(xen_store_evtchn);

struct xenstore_domain_interface *xen_store_interface;
EXPORT_SYMBOL_GPL(xen_store_interface);

static unsigned long xen_store_mfn;

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

int xenbus_match(struct device *_dev, struct device_driver *_drv)
{
	struct xenbus_driver *drv = to_xenbus_driver(_drv);

	if (!drv->ids)
		return 0;

	return match_device(drv->ids, to_xenbus_device(_dev)) != NULL;
}
EXPORT_SYMBOL_GPL(xenbus_match);


int xenbus_uevent(struct device *_dev, struct kobj_uevent_env *env)
{
	struct xenbus_device *dev = to_xenbus_device(_dev);

	if (add_uevent_var(env, "MODALIAS=xen:%s", dev->devicetype))
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_GPL(xenbus_uevent);

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
	struct xen_bus_type *bus = container_of(dev->dev.bus, struct xen_bus_type, bus);

	return xenbus_watch_pathfmt(dev, &dev->otherend_watch, bus->otherend_changed,
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
			     const char **vec, unsigned int len,
			     int ignore_on_shutdown)
{
	struct xenbus_device *dev =
		container_of(watch, struct xenbus_device, otherend_watch);
	struct xenbus_driver *drv = to_xenbus_driver(dev->dev.driver);
	enum xenbus_state state;

	/* Protect us against watches firing on old details when the otherend
	   details change, say immediately after a resume. */
	if (!dev->otherend ||
	    strncmp(dev->otherend, vec[XS_WATCH_PATH],
		    strlen(dev->otherend))) {
		dev_dbg(&dev->dev, "Ignoring watch at %s\n",
			vec[XS_WATCH_PATH]);
		return;
	}

	state = xenbus_read_driver_state(dev->otherend);

	dev_dbg(&dev->dev, "state is %d, (%s), %s, %s\n",
		state, xenbus_strstate(state), dev->otherend_watch.node,
		vec[XS_WATCH_PATH]);

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

	err = drv->probe(dev, id);
	if (err)
		goto fail;

	err = watch_otherend(dev);
	if (err) {
		dev_warn(&dev->dev, "watch_otherend on %s failed.\n",
		       dev->nodename);
		return err;
	}

	return 0;
fail:
	xenbus_dev_error(dev, err, "xenbus_dev_probe on %s", dev->nodename);
	xenbus_switch_state(dev, XenbusStateClosed);
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(xenbus_dev_probe);

int xenbus_dev_remove(struct device *_dev)
{
	struct xenbus_device *dev = to_xenbus_device(_dev);
	struct xenbus_driver *drv = to_xenbus_driver(_dev->driver);

	DPRINTK("%s", dev->nodename);

	free_otherend_watch(dev);
	free_otherend_details(dev);

	if (drv->remove)
		drv->remove(dev);

	xenbus_switch_state(dev, XenbusStateClosed);
	return 0;
}
EXPORT_SYMBOL_GPL(xenbus_dev_remove);

void xenbus_dev_shutdown(struct device *_dev)
{
	struct xenbus_device *dev = to_xenbus_device(_dev);
	unsigned long timeout = 5*HZ;

	DPRINTK("%s", dev->nodename);

	get_device(&dev->dev);
	if (dev->state != XenbusStateConnected) {
		printk(KERN_INFO "%s: %s: %s != Connected, skipping\n", __func__,
		       dev->nodename, xenbus_strstate(dev->state));
		goto out;
	}
	xenbus_switch_state(dev, XenbusStateClosing);
	timeout = wait_for_completion_timeout(&dev->down, timeout);
	if (!timeout)
		printk(KERN_INFO "%s: %s timeout closing device\n",
		       __func__, dev->nodename);
 out:
	put_device(&dev->dev);
}
EXPORT_SYMBOL_GPL(xenbus_dev_shutdown);

int xenbus_register_driver_common(struct xenbus_driver *drv,
				  struct xen_bus_type *bus,
				  struct module *owner,
				  const char *mod_name)
{
	drv->driver.name = drv->name;
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

struct xb_find_info
{
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

struct xenbus_device *xenbus_device_find(const char *nodename,
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

static ssize_t xendev_show_nodename(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", to_xenbus_device(dev)->nodename);
}
static DEVICE_ATTR(nodename, S_IRUSR | S_IRGRP | S_IROTH, xendev_show_nodename, NULL);

static ssize_t xendev_show_devtype(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", to_xenbus_device(dev)->devicetype);
}
static DEVICE_ATTR(devtype, S_IRUSR | S_IRGRP | S_IROTH, xendev_show_devtype, NULL);

static ssize_t xendev_show_modalias(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "xen:%s\n", to_xenbus_device(dev)->devicetype);
}
static DEVICE_ATTR(modalias, S_IRUSR | S_IRGRP | S_IROTH, xendev_show_modalias, NULL);

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

	dev_set_name(&xendev->dev, devname);

	/* Register with generic device framework. */
	err = device_register(&xendev->dev);
	if (err)
		goto fail;

	err = device_create_file(&xendev->dev, &dev_attr_nodename);
	if (err)
		goto fail_unregister;

	err = device_create_file(&xendev->dev, &dev_attr_devtype);
	if (err)
		goto fail_remove_nodename;

	err = device_create_file(&xendev->dev, &dev_attr_modalias);
	if (err)
		goto fail_remove_devtype;

	return 0;
fail_remove_devtype:
	device_remove_file(&xendev->dev, &dev_attr_devtype);
fail_remove_nodename:
	device_remove_file(&xendev->dev, &dev_attr_nodename);
fail_unregister:
	device_unregister(&xendev->dev);
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

	printk(KERN_CRIT "%s type %s\n", __func__, type);

	dir = xenbus_directory(XBT_NIL, bus->root, type, &dir_n);
	if (IS_ERR(dir)) {
		printk(KERN_CRIT "%s failed xenbus_directory\n", __func__);
		return PTR_ERR(dir);
	}

	for (i = 0; i < dir_n; i++) {
		printk(KERN_CRIT "%s %d/%d %s\n", __func__, i+1,dir_n, dir[i]);
		err = bus->probe(bus, type, dir[i]);
		if (err) {
			printk(KERN_CRIT "%s failed\n", __func__);
			break;
		}
	}
	printk("%s done\n", __func__);
	kfree(dir);
	return err;
}

int xenbus_probe_devices(struct xen_bus_type *bus)
{
	int err = 0;
	char **dir;
	unsigned int i, dir_n;

	printk(KERN_CRIT "%s %s\n", __func__, bus->root);

	dir = xenbus_directory(XBT_NIL, bus->root, "", &dir_n);
	if (IS_ERR(dir)) {
		printk(KERN_CRIT "%s failed xenbus_directory\n", __func__);
		return PTR_ERR(dir);
	}

	for (i = 0; i < dir_n; i++) {
		printk(KERN_CRIT "%s %d/%d %s\n", __func__, i+1,dir_n, dir[i]);
		err = xenbus_probe_device_type(bus, dir[i]);
		if (err) {
			printk(KERN_CRIT "%s failed\n", __func__);
			break;
		}
	}
	printk("%s done\n", __func__);
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

int xenbus_dev_suspend(struct device *dev, pm_message_t state)
{
	int err = 0;
	struct xenbus_driver *drv;
	struct xenbus_device *xdev = container_of(dev, struct xenbus_device, dev);

	DPRINTK("%s", xdev->nodename);

	if (dev->driver == NULL)
		return 0;
	drv = to_xenbus_driver(dev->driver);
	if (drv->suspend)
		err = drv->suspend(xdev, state);
	if (err)
		printk(KERN_WARNING
		       "xenbus: suspend %s failed: %i\n", dev_name(dev), err);
	return 0;
}
EXPORT_SYMBOL_GPL(xenbus_dev_suspend);

int xenbus_dev_resume(struct device *dev)
{
	int err;
	struct xenbus_driver *drv;
	struct xenbus_device *xdev = container_of(dev, struct xenbus_device, dev);

	DPRINTK("%s", xdev->nodename);

	if (dev->driver == NULL)
		return 0;
	drv = to_xenbus_driver(dev->driver);
	err = talk_to_otherend(xdev);
	if (err) {
		printk(KERN_WARNING
		       "xenbus: resume (talk_to_otherend) %s failed: %i\n",
		       dev_name(dev), err);
		return err;
	}

	xdev->state = XenbusStateInitialising;

	if (drv->resume) {
		err = drv->resume(xdev);
		if (err) {
			printk(KERN_WARNING
			       "xenbus: resume %s failed: %i\n",
			       dev_name(dev), err);
			return err;
		}
	}

	err = watch_otherend(xdev);
	if (err) {
		printk(KERN_WARNING
		       "xenbus_probe: resume (watch_otherend) %s failed: "
		       "%d.\n", dev_name(dev), err);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xenbus_dev_resume);

/* A flag to determine if xenstored is 'ready' (i.e. has started) */
int xenstored_ready = 0;


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

void xenbus_probe(struct work_struct *unused)
{
	xenstored_ready = 1;

	/* Notify others that xenstore is up */
	blocking_notifier_call_chain(&xenstore_chain, 0, NULL);
}
EXPORT_SYMBOL_GPL(xenbus_probe);

static int __init xenbus_probe_initcall(void)
{
	if (!xen_domain())
		return -ENODEV;

	if (xen_initial_domain() || xen_hvm_domain())
		return 0;

	xenbus_probe(NULL);
	return 0;
}

device_initcall(xenbus_probe_initcall);

static int __init xenbus_init(void)
{
	int err = 0;
	unsigned long page = 0;

	DPRINTK("");

	err = -ENODEV;
	if (!xen_domain())
		goto out_error;

	/*
	 * Domain0 doesn't have a store_evtchn or store_mfn yet.
	 */
	if (xen_initial_domain()) {
		struct evtchn_alloc_unbound alloc_unbound;

		/* Allocate Xenstore page */
		page = get_zeroed_page(GFP_KERNEL);
		if (!page)
			goto out_error;

		xen_store_mfn = xen_start_info->store_mfn =
			pfn_to_mfn(virt_to_phys((void *)page) >>
				   PAGE_SHIFT);

		/* Next allocate a local port which xenstored can bind to */
		alloc_unbound.dom        = DOMID_SELF;
		alloc_unbound.remote_dom = 0;

		err = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound,
						  &alloc_unbound);
		if (err == -ENOSYS)
			goto out_error;

		BUG_ON(err);
		xen_store_evtchn = xen_start_info->store_evtchn =
			alloc_unbound.port;

		xen_store_interface = mfn_to_virt(xen_store_mfn);
	} else {
		if (xen_hvm_domain()) {
			uint64_t v = 0;
			err = hvm_get_parameter(HVM_PARAM_STORE_EVTCHN, &v);
			if (err)
				goto out_error;
			xen_store_evtchn = (int)v;
			err = hvm_get_parameter(HVM_PARAM_STORE_PFN, &v);
			if (err)
				goto out_error;
			xen_store_mfn = (unsigned long)v;
			xen_store_interface = ioremap(xen_store_mfn << PAGE_SHIFT, PAGE_SIZE);
		} else {
			xen_store_evtchn = xen_start_info->store_evtchn;
			xen_store_mfn = xen_start_info->store_mfn;
			xen_store_interface = mfn_to_virt(xen_store_mfn);
			xenstored_ready = 1;
		}
	}

	/* Initialize the interface to xenstore. */
	err = xs_init();
	if (err) {
		printk(KERN_WARNING
		       "XENBUS: Error initializing xenstore comms: %i\n", err);
		goto out_error;
	}

#ifdef CONFIG_XEN_COMPAT_XENFS
	/*
	 * Create xenfs mountpoint in /proc for compatibility with
	 * utilities that expect to find "xenbus" under "/proc/xen".
	 */
	proc_mkdir("xen", NULL);
#endif

	printk(KERN_CRIT "%s ok\n", __func__);
	return 0;

  out_error:
	if (page != 0)
		free_page(page);

	return err;
}

postcore_initcall(xenbus_init);

MODULE_LICENSE("GPL");
