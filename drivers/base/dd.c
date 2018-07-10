// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/base/dd.c - The core device/driver interactions.
 *
 * This file contains the (sometimes tricky) code that controls the
 * interactions between devices and drivers, which primarily includes
 * driver binding and unbinding.
 *
 * All of this code used to exist in drivers/base/bus.c, but was
 * relocated to here in the name of compartmentalization (since it wasn't
 * strictly code just for the 'struct bus_type'.
 *
 * Copyright (c) 2002-5 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 * Copyright (c) 2007-2009 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (c) 2007-2009 Novell Inc.
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/async.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/devinfo.h>

#include "base.h"
#include "power/power.h"

/*
 * Deferred Probe infrastructure.
 *
 * Sometimes driver probe order matters, but the kernel doesn't always have
 * dependency information which means some drivers will get probed before a
 * resource it depends on is available.  For example, an SDHCI driver may
 * first need a GPIO line from an i2c GPIO controller before it can be
 * initialized.  If a required resource is not available yet, a driver can
 * request probing to be deferred by returning -EPROBE_DEFER from its probe hook
 *
 * Deferred probe maintains two lists of devices, a pending list and an active
 * list.  A driver returning -EPROBE_DEFER causes the device to be added to the
 * pending list.  A successful driver probe will trigger moving all devices
 * from the pending to the active list so that the workqueue will eventually
 * retry them.
 *
 * The deferred_probe_mutex must be held any time the deferred_probe_*_list
 * of the (struct device*)->p->deferred_probe pointers are manipulated
 */
static DEFINE_MUTEX(deferred_probe_mutex);
static LIST_HEAD(deferred_probe_pending_list);
static LIST_HEAD(deferred_probe_active_list);
static atomic_t deferred_trigger_count = ATOMIC_INIT(0);
static bool initcalls_done;

/*
 * In some cases, like suspend to RAM or hibernation, It might be reasonable
 * to prohibit probing of devices as it could be unsafe.
 * Once defer_all_probes is true all drivers probes will be forcibly deferred.
 */
static bool defer_all_probes;

/*
 * For initcall_debug, show the deferred probes executed in late_initcall
 * processing.
 */
static void deferred_probe_debug(struct device *dev)
{
	ktime_t calltime, delta, rettime;
	unsigned long long duration;

	printk(KERN_DEBUG "deferred probe %s @ %i\n", dev_name(dev),
	       task_pid_nr(current));
	calltime = ktime_get();
	bus_probe_device(dev);
	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long) ktime_to_ns(delta) >> 10;
	printk(KERN_DEBUG "deferred probe %s returned after %lld usecs\n",
	       dev_name(dev), duration);
}

/*
 * deferred_probe_work_func() - Retry probing devices in the active list.
 */
static void deferred_probe_work_func(struct work_struct *work)
{
	struct device *dev;
	struct device_private *private;
	/*
	 * This block processes every device in the deferred 'active' list.
	 * Each device is removed from the active list and passed to
	 * bus_probe_device() to re-attempt the probe.  The loop continues
	 * until every device in the active list is removed and retried.
	 *
	 * Note: Once the device is removed from the list and the mutex is
	 * released, it is possible for the device get freed by another thread
	 * and cause a illegal pointer dereference.  This code uses
	 * get/put_device() to ensure the device structure cannot disappear
	 * from under our feet.
	 */
	mutex_lock(&deferred_probe_mutex);
	while (!list_empty(&deferred_probe_active_list)) {
		private = list_first_entry(&deferred_probe_active_list,
					typeof(*dev->p), deferred_probe);
		dev = private->device;
		list_del_init(&private->deferred_probe);

		get_device(dev);

		/*
		 * Drop the mutex while probing each device; the probe path may
		 * manipulate the deferred list
		 */
		mutex_unlock(&deferred_probe_mutex);

		/*
		 * Force the device to the end of the dpm_list since
		 * the PM code assumes that the order we add things to
		 * the list is a good order for suspend but deferred
		 * probe makes that very unsafe.
		 */
		device_pm_move_to_tail(dev);

		dev_dbg(dev, "Retrying from deferred list\n");
		if (initcall_debug && !initcalls_done)
			deferred_probe_debug(dev);
		else
			bus_probe_device(dev);

		mutex_lock(&deferred_probe_mutex);

		put_device(dev);
	}
	mutex_unlock(&deferred_probe_mutex);
}
static DECLARE_WORK(deferred_probe_work, deferred_probe_work_func);

static void driver_deferred_probe_add(struct device *dev)
{
	mutex_lock(&deferred_probe_mutex);
	if (list_empty(&dev->p->deferred_probe)) {
		dev_dbg(dev, "Added to deferred list\n");
		list_add_tail(&dev->p->deferred_probe, &deferred_probe_pending_list);
	}
	mutex_unlock(&deferred_probe_mutex);
}

void driver_deferred_probe_del(struct device *dev)
{
	mutex_lock(&deferred_probe_mutex);
	if (!list_empty(&dev->p->deferred_probe)) {
		dev_dbg(dev, "Removed from deferred list\n");
		list_del_init(&dev->p->deferred_probe);
	}
	mutex_unlock(&deferred_probe_mutex);
}

static bool driver_deferred_probe_enable = false;
/**
 * driver_deferred_probe_trigger() - Kick off re-probing deferred devices
 *
 * This functions moves all devices from the pending list to the active
 * list and schedules the deferred probe workqueue to process them.  It
 * should be called anytime a driver is successfully bound to a device.
 *
 * Note, there is a race condition in multi-threaded probe. In the case where
 * more than one device is probing at the same time, it is possible for one
 * probe to complete successfully while another is about to defer. If the second
 * depends on the first, then it will get put on the pending list after the
 * trigger event has already occurred and will be stuck there.
 *
 * The atomic 'deferred_trigger_count' is used to determine if a successful
 * trigger has occurred in the midst of probing a driver. If the trigger count
 * changes in the midst of a probe, then deferred processing should be triggered
 * again.
 */
static void driver_deferred_probe_trigger(void)
{
	if (!driver_deferred_probe_enable)
		return;

	/*
	 * A successful probe means that all the devices in the pending list
	 * should be triggered to be reprobed.  Move all the deferred devices
	 * into the active list so they can be retried by the workqueue
	 */
	mutex_lock(&deferred_probe_mutex);
	atomic_inc(&deferred_trigger_count);
	list_splice_tail_init(&deferred_probe_pending_list,
			      &deferred_probe_active_list);
	mutex_unlock(&deferred_probe_mutex);

	/*
	 * Kick the re-probe thread.  It may already be scheduled, but it is
	 * safe to kick it again.
	 */
	schedule_work(&deferred_probe_work);
}

/**
 * device_block_probing() - Block/defere device's probes
 *
 *	It will disable probing of devices and defer their probes instead.
 */
void device_block_probing(void)
{
	defer_all_probes = true;
	/* sync with probes to avoid races. */
	wait_for_device_probe();
}

/**
 * device_unblock_probing() - Unblock/enable device's probes
 *
 *	It will restore normal behavior and trigger re-probing of deferred
 * devices.
 */
void device_unblock_probing(void)
{
	defer_all_probes = false;
	driver_deferred_probe_trigger();
}

/**
 * deferred_probe_initcall() - Enable probing of deferred devices
 *
 * We don't want to get in the way when the bulk of drivers are getting probed.
 * Instead, this initcall makes sure that deferred probing is delayed until
 * late_initcall time.
 */
static int deferred_probe_initcall(void)
{
	driver_deferred_probe_enable = true;
	driver_deferred_probe_trigger();
	/* Sort as many dependencies as possible before exiting initcalls */
	flush_work(&deferred_probe_work);
	initcalls_done = true;
	return 0;
}
late_initcall(deferred_probe_initcall);

/**
 * device_is_bound() - Check if device is bound to a driver
 * @dev: device to check
 *
 * Returns true if passed device has already finished probing successfully
 * against a driver.
 *
 * This function must be called with the device lock held.
 */
bool device_is_bound(struct device *dev)
{
	return dev->p && klist_node_attached(&dev->p->knode_driver);
}

static void driver_bound(struct device *dev)
{
	if (device_is_bound(dev)) {
		printk(KERN_WARNING "%s: device %s already bound\n",
			__func__, kobject_name(&dev->kobj));
		return;
	}

	pr_debug("driver: '%s': %s: bound to device '%s'\n", dev->driver->name,
		 __func__, dev_name(dev));

	klist_add_tail(&dev->p->knode_driver, &dev->driver->p->klist_devices);
	device_links_driver_bound(dev);

	device_pm_check_callbacks(dev);

	/*
	 * Make sure the device is no longer in one of the deferred lists and
	 * kick off retrying all pending devices
	 */
	driver_deferred_probe_del(dev);
	driver_deferred_probe_trigger();

	if (dev->bus)
		blocking_notifier_call_chain(&dev->bus->p->bus_notifier,
					     BUS_NOTIFY_BOUND_DRIVER, dev);

	kobject_uevent(&dev->kobj, KOBJ_BIND);
}

static ssize_t coredump_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	device_lock(dev);
	dev->driver->coredump(dev);
	device_unlock(dev);

	return count;
}
static DEVICE_ATTR_WO(coredump);

static int driver_sysfs_add(struct device *dev)
{
	int ret;

	if (dev->bus)
		blocking_notifier_call_chain(&dev->bus->p->bus_notifier,
					     BUS_NOTIFY_BIND_DRIVER, dev);

	ret = sysfs_create_link(&dev->driver->p->kobj, &dev->kobj,
				kobject_name(&dev->kobj));
	if (ret)
		goto fail;

	ret = sysfs_create_link(&dev->kobj, &dev->driver->p->kobj,
				"driver");
	if (ret)
		goto rm_dev;

	if (!IS_ENABLED(CONFIG_DEV_COREDUMP) || !dev->driver->coredump ||
	    !device_create_file(dev, &dev_attr_coredump))
		return 0;

	sysfs_remove_link(&dev->kobj, "driver");

rm_dev:
	sysfs_remove_link(&dev->driver->p->kobj,
			  kobject_name(&dev->kobj));

fail:
	return ret;
}

static void driver_sysfs_remove(struct device *dev)
{
	struct device_driver *drv = dev->driver;

	if (drv) {
		if (drv->coredump)
			device_remove_file(dev, &dev_attr_coredump);
		sysfs_remove_link(&drv->p->kobj, kobject_name(&dev->kobj));
		sysfs_remove_link(&dev->kobj, "driver");
	}
}

/**
 * device_bind_driver - bind a driver to one device.
 * @dev: device.
 *
 * Allow manual attachment of a driver to a device.
 * Caller must have already set @dev->driver.
 *
 * Note that this does not modify the bus reference count
 * nor take the bus's rwsem. Please verify those are accounted
 * for before calling this. (It is ok to call with no other effort
 * from a driver's probe() method.)
 *
 * This function must be called with the device lock held.
 */
int device_bind_driver(struct device *dev)
{
	int ret;

	ret = driver_sysfs_add(dev);
	if (!ret)
		driver_bound(dev);
	else if (dev->bus)
		blocking_notifier_call_chain(&dev->bus->p->bus_notifier,
					     BUS_NOTIFY_DRIVER_NOT_BOUND, dev);
	return ret;
}
EXPORT_SYMBOL_GPL(device_bind_driver);

static atomic_t probe_count = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(probe_waitqueue);

static void driver_deferred_probe_add_trigger(struct device *dev,
					      int local_trigger_count)
{
	driver_deferred_probe_add(dev);
	/* Did a trigger occur while probing? Need to re-trigger if yes */
	if (local_trigger_count != atomic_read(&deferred_trigger_count))
		driver_deferred_probe_trigger();
}

static int really_probe(struct device *dev, struct device_driver *drv)
{
	int ret = -EPROBE_DEFER;
	int local_trigger_count = atomic_read(&deferred_trigger_count);
	bool test_remove = IS_ENABLED(CONFIG_DEBUG_TEST_DRIVER_REMOVE) &&
			   !drv->suppress_bind_attrs;

	if (defer_all_probes) {
		/*
		 * Value of defer_all_probes can be set only by
		 * device_defer_all_probes_enable() which, in turn, will call
		 * wait_for_device_probe() right after that to avoid any races.
		 */
		dev_dbg(dev, "Driver %s force probe deferral\n", drv->name);
		driver_deferred_probe_add(dev);
		return ret;
	}

	ret = device_links_check_suppliers(dev);
	if (ret == -EPROBE_DEFER)
		driver_deferred_probe_add_trigger(dev, local_trigger_count);
	if (ret)
		return ret;

	atomic_inc(&probe_count);
	pr_debug("bus: '%s': %s: probing driver %s with device %s\n",
		 drv->bus->name, __func__, drv->name, dev_name(dev));
	WARN_ON(!list_empty(&dev->devres_head));

re_probe:
	dev->driver = drv;

	/* If using pinctrl, bind pins now before probing */
	ret = pinctrl_bind_pins(dev);
	if (ret)
		goto pinctrl_bind_failed;

	ret = dma_configure(dev);
	if (ret)
		goto dma_failed;

	if (driver_sysfs_add(dev)) {
		printk(KERN_ERR "%s: driver_sysfs_add(%s) failed\n",
			__func__, dev_name(dev));
		goto probe_failed;
	}

	if (dev->pm_domain && dev->pm_domain->activate) {
		ret = dev->pm_domain->activate(dev);
		if (ret)
			goto probe_failed;
	}

	if (dev->bus->probe) {
		ret = dev->bus->probe(dev);
		if (ret)
			goto probe_failed;
	} else if (drv->probe) {
		ret = drv->probe(dev);
		if (ret)
			goto probe_failed;
	}

	if (test_remove) {
		test_remove = false;

		if (dev->bus->remove)
			dev->bus->remove(dev);
		else if (drv->remove)
			drv->remove(dev);

		devres_release_all(dev);
		driver_sysfs_remove(dev);
		dev->driver = NULL;
		dev_set_drvdata(dev, NULL);
		if (dev->pm_domain && dev->pm_domain->dismiss)
			dev->pm_domain->dismiss(dev);
		pm_runtime_reinit(dev);

		goto re_probe;
	}

	pinctrl_init_done(dev);

	if (dev->pm_domain && dev->pm_domain->sync)
		dev->pm_domain->sync(dev);

	driver_bound(dev);
	ret = 1;
	pr_debug("bus: '%s': %s: bound device %s to driver %s\n",
		 drv->bus->name, __func__, dev_name(dev), drv->name);
	goto done;

probe_failed:
	dma_deconfigure(dev);
dma_failed:
	if (dev->bus)
		blocking_notifier_call_chain(&dev->bus->p->bus_notifier,
					     BUS_NOTIFY_DRIVER_NOT_BOUND, dev);
pinctrl_bind_failed:
	device_links_no_driver(dev);
	devres_release_all(dev);
	driver_sysfs_remove(dev);
	dev->driver = NULL;
	dev_set_drvdata(dev, NULL);
	if (dev->pm_domain && dev->pm_domain->dismiss)
		dev->pm_domain->dismiss(dev);
	pm_runtime_reinit(dev);
	dev_pm_set_driver_flags(dev, 0);

	switch (ret) {
	case -EPROBE_DEFER:
		/* Driver requested deferred probing */
		dev_dbg(dev, "Driver %s requests probe deferral\n", drv->name);
		driver_deferred_probe_add_trigger(dev, local_trigger_count);
		break;
	case -ENODEV:
	case -ENXIO:
		pr_debug("%s: probe of %s rejects match %d\n",
			 drv->name, dev_name(dev), ret);
		break;
	default:
		/* driver matched but the probe failed */
		printk(KERN_WARNING
		       "%s: probe of %s failed with error %d\n",
		       drv->name, dev_name(dev), ret);
	}
	/*
	 * Ignore errors returned by ->probe so that the next driver can try
	 * its luck.
	 */
	ret = 0;
done:
	atomic_dec(&probe_count);
	wake_up(&probe_waitqueue);
	return ret;
}

/**
 * driver_probe_done
 * Determine if the probe sequence is finished or not.
 *
 * Should somehow figure out how to use a semaphore, not an atomic variable...
 */
int driver_probe_done(void)
{
	pr_debug("%s: probe_count = %d\n", __func__,
		 atomic_read(&probe_count));
	if (atomic_read(&probe_count))
		return -EBUSY;
	return 0;
}

/**
 * wait_for_device_probe
 * Wait for device probing to be completed.
 */
void wait_for_device_probe(void)
{
	/* wait for the deferred probe workqueue to finish */
	flush_work(&deferred_probe_work);

	/* wait for the known devices to complete their probing */
	wait_event(probe_waitqueue, atomic_read(&probe_count) == 0);
	async_synchronize_full();
}
EXPORT_SYMBOL_GPL(wait_for_device_probe);

/**
 * driver_probe_device - attempt to bind device & driver together
 * @drv: driver to bind a device to
 * @dev: device to try to bind to the driver
 *
 * This function returns -ENODEV if the device is not registered,
 * 1 if the device is bound successfully and 0 otherwise.
 *
 * This function must be called with @dev lock held.  When called for a
 * USB interface, @dev->parent lock must be held as well.
 *
 * If the device has a parent, runtime-resume the parent before driver probing.
 */
int driver_probe_device(struct device_driver *drv, struct device *dev)
{
	int ret = 0;

	if (!device_is_registered(dev))
		return -ENODEV;

	pr_debug("bus: '%s': %s: matched device %s with driver %s\n",
		 drv->bus->name, __func__, dev_name(dev), drv->name);

	pm_runtime_get_suppliers(dev);
	if (dev->parent)
		pm_runtime_get_sync(dev->parent);

	pm_runtime_barrier(dev);
	ret = really_probe(dev, drv);
	pm_request_idle(dev);

	if (dev->parent)
		pm_runtime_put(dev->parent);

	pm_runtime_put_suppliers(dev);
	return ret;
}

bool driver_allows_async_probing(struct device_driver *drv)
{
	switch (drv->probe_type) {
	case PROBE_PREFER_ASYNCHRONOUS:
		return true;

	case PROBE_FORCE_SYNCHRONOUS:
		return false;

	default:
		if (module_requested_async_probing(drv->owner))
			return true;

		return false;
	}
}

struct device_attach_data {
	struct device *dev;

	/*
	 * Indicates whether we are are considering asynchronous probing or
	 * not. Only initial binding after device or driver registration
	 * (including deferral processing) may be done asynchronously, the
	 * rest is always synchronous, as we expect it is being done by
	 * request from userspace.
	 */
	bool check_async;

	/*
	 * Indicates if we are binding synchronous or asynchronous drivers.
	 * When asynchronous probing is enabled we'll execute 2 passes
	 * over drivers: first pass doing synchronous probing and second
	 * doing asynchronous probing (if synchronous did not succeed -
	 * most likely because there was no driver requiring synchronous
	 * probing - and we found asynchronous driver during first pass).
	 * The 2 passes are done because we can't shoot asynchronous
	 * probe for given device and driver from bus_for_each_drv() since
	 * driver pointer is not guaranteed to stay valid once
	 * bus_for_each_drv() iterates to the next driver on the bus.
	 */
	bool want_async;

	/*
	 * We'll set have_async to 'true' if, while scanning for matching
	 * driver, we'll encounter one that requests asynchronous probing.
	 */
	bool have_async;
};

static int __device_attach_driver(struct device_driver *drv, void *_data)
{
	struct device_attach_data *data = _data;
	struct device *dev = data->dev;
	bool async_allowed;
	int ret;

	/*
	 * Check if device has already been claimed. This may
	 * happen with driver loading, device discovery/registration,
	 * and deferred probe processing happens all at once with
	 * multiple threads.
	 */
	if (dev->driver)
		return -EBUSY;

	ret = driver_match_device(drv, dev);
	if (ret == 0) {
		/* no match */
		return 0;
	} else if (ret == -EPROBE_DEFER) {
		dev_dbg(dev, "Device match requests probe deferral\n");
		driver_deferred_probe_add(dev);
	} else if (ret < 0) {
		dev_dbg(dev, "Bus failed to match device: %d", ret);
		return ret;
	} /* ret > 0 means positive match */

	async_allowed = driver_allows_async_probing(drv);

	if (async_allowed)
		data->have_async = true;

	if (data->check_async && async_allowed != data->want_async)
		return 0;

	return driver_probe_device(drv, dev);
}

static void __device_attach_async_helper(void *_dev, async_cookie_t cookie)
{
	struct device *dev = _dev;
	struct device_attach_data data = {
		.dev		= dev,
		.check_async	= true,
		.want_async	= true,
	};

	device_lock(dev);

	if (dev->parent)
		pm_runtime_get_sync(dev->parent);

	bus_for_each_drv(dev->bus, NULL, &data, __device_attach_driver);
	dev_dbg(dev, "async probe completed\n");

	pm_request_idle(dev);

	if (dev->parent)
		pm_runtime_put(dev->parent);

	device_unlock(dev);

	put_device(dev);
}

static int __device_attach(struct device *dev, bool allow_async)
{
	int ret = 0;

	device_lock(dev);
	if (dev->driver) {
		if (device_is_bound(dev)) {
			ret = 1;
			goto out_unlock;
		}
		ret = device_bind_driver(dev);
		if (ret == 0)
			ret = 1;
		else {
			dev->driver = NULL;
			ret = 0;
		}
	} else {
		struct device_attach_data data = {
			.dev = dev,
			.check_async = allow_async,
			.want_async = false,
		};

		if (dev->parent)
			pm_runtime_get_sync(dev->parent);

		ret = bus_for_each_drv(dev->bus, NULL, &data,
					__device_attach_driver);
		if (!ret && allow_async && data.have_async) {
			/*
			 * If we could not find appropriate driver
			 * synchronously and we are allowed to do
			 * async probes and there are drivers that
			 * want to probe asynchronously, we'll
			 * try them.
			 */
			dev_dbg(dev, "scheduling asynchronous probe\n");
			get_device(dev);
			async_schedule(__device_attach_async_helper, dev);
		} else {
			pm_request_idle(dev);
		}

		if (dev->parent)
			pm_runtime_put(dev->parent);
	}
out_unlock:
	device_unlock(dev);
	return ret;
}

/**
 * device_attach - try to attach device to a driver.
 * @dev: device.
 *
 * Walk the list of drivers that the bus has and call
 * driver_probe_device() for each pair. If a compatible
 * pair is found, break out and return.
 *
 * Returns 1 if the device was bound to a driver;
 * 0 if no matching driver was found;
 * -ENODEV if the device is not registered.
 *
 * When called for a USB interface, @dev->parent lock must be held.
 */
int device_attach(struct device *dev)
{
	return __device_attach(dev, false);
}
EXPORT_SYMBOL_GPL(device_attach);

void device_initial_probe(struct device *dev)
{
	__device_attach(dev, true);
}

static int __driver_attach(struct device *dev, void *data)
{
	struct device_driver *drv = data;
	int ret;

	/*
	 * Lock device and try to bind to it. We drop the error
	 * here and always return 0, because we need to keep trying
	 * to bind to devices and some drivers will return an error
	 * simply if it didn't support the device.
	 *
	 * driver_probe_device() will spit a warning if there
	 * is an error.
	 */

	ret = driver_match_device(drv, dev);
	if (ret == 0) {
		/* no match */
		return 0;
	} else if (ret == -EPROBE_DEFER) {
		dev_dbg(dev, "Device match requests probe deferral\n");
		driver_deferred_probe_add(dev);
	} else if (ret < 0) {
		dev_dbg(dev, "Bus failed to match device: %d", ret);
		return ret;
	} /* ret > 0 means positive match */

	if (dev->parent && dev->bus->need_parent_lock)
		device_lock(dev->parent);
	device_lock(dev);
	if (!dev->driver)
		driver_probe_device(drv, dev);
	device_unlock(dev);
	if (dev->parent && dev->bus->need_parent_lock)
		device_unlock(dev->parent);

	return 0;
}

/**
 * driver_attach - try to bind driver to devices.
 * @drv: driver.
 *
 * Walk the list of devices that the bus has on it and try to
 * match the driver with each one.  If driver_probe_device()
 * returns 0 and the @dev->driver is set, we've found a
 * compatible pair.
 */
int driver_attach(struct device_driver *drv)
{
	return bus_for_each_dev(drv->bus, NULL, drv, __driver_attach);
}
EXPORT_SYMBOL_GPL(driver_attach);

/*
 * __device_release_driver() must be called with @dev lock held.
 * When called for a USB interface, @dev->parent lock must be held as well.
 */
static void __device_release_driver(struct device *dev, struct device *parent)
{
	struct device_driver *drv;

	drv = dev->driver;
	if (drv) {
		if (driver_allows_async_probing(drv))
			async_synchronize_full();

		while (device_links_busy(dev)) {
			device_unlock(dev);
			if (parent)
				device_unlock(parent);

			device_links_unbind_consumers(dev);
			if (parent)
				device_lock(parent);

			device_lock(dev);
			/*
			 * A concurrent invocation of the same function might
			 * have released the driver successfully while this one
			 * was waiting, so check for that.
			 */
			if (dev->driver != drv)
				return;
		}

		pm_runtime_get_sync(dev);
		pm_runtime_clean_up_links(dev);

		driver_sysfs_remove(dev);

		if (dev->bus)
			blocking_notifier_call_chain(&dev->bus->p->bus_notifier,
						     BUS_NOTIFY_UNBIND_DRIVER,
						     dev);

		pm_runtime_put_sync(dev);

		if (dev->bus && dev->bus->remove)
			dev->bus->remove(dev);
		else if (drv->remove)
			drv->remove(dev);

		device_links_driver_cleanup(dev);
		dma_deconfigure(dev);

		devres_release_all(dev);
		dev->driver = NULL;
		dev_set_drvdata(dev, NULL);
		if (dev->pm_domain && dev->pm_domain->dismiss)
			dev->pm_domain->dismiss(dev);
		pm_runtime_reinit(dev);
		dev_pm_set_driver_flags(dev, 0);

		klist_remove(&dev->p->knode_driver);
		device_pm_check_callbacks(dev);
		if (dev->bus)
			blocking_notifier_call_chain(&dev->bus->p->bus_notifier,
						     BUS_NOTIFY_UNBOUND_DRIVER,
						     dev);

		kobject_uevent(&dev->kobj, KOBJ_UNBIND);
	}
}

void device_release_driver_internal(struct device *dev,
				    struct device_driver *drv,
				    struct device *parent)
{
	if (parent && dev->bus->need_parent_lock)
		device_lock(parent);

	device_lock(dev);
	if (!drv || drv == dev->driver)
		__device_release_driver(dev, parent);

	device_unlock(dev);
	if (parent && dev->bus->need_parent_lock)
		device_unlock(parent);
}

/**
 * device_release_driver - manually detach device from driver.
 * @dev: device.
 *
 * Manually detach device from driver.
 * When called for a USB interface, @dev->parent lock must be held.
 *
 * If this function is to be called with @dev->parent lock held, ensure that
 * the device's consumers are unbound in advance or that their locks can be
 * acquired under the @dev->parent lock.
 */
void device_release_driver(struct device *dev)
{
	/*
	 * If anyone calls device_release_driver() recursively from
	 * within their ->remove callback for the same device, they
	 * will deadlock right here.
	 */
	device_release_driver_internal(dev, NULL, NULL);
}
EXPORT_SYMBOL_GPL(device_release_driver);

/**
 * driver_detach - detach driver from all devices it controls.
 * @drv: driver.
 */
void driver_detach(struct device_driver *drv)
{
	struct device_private *dev_prv;
	struct device *dev;

	for (;;) {
		spin_lock(&drv->p->klist_devices.k_lock);
		if (list_empty(&drv->p->klist_devices.k_list)) {
			spin_unlock(&drv->p->klist_devices.k_lock);
			break;
		}
		dev_prv = list_entry(drv->p->klist_devices.k_list.prev,
				     struct device_private,
				     knode_driver.n_node);
		dev = dev_prv->device;
		get_device(dev);
		spin_unlock(&drv->p->klist_devices.k_lock);
		device_release_driver_internal(dev, drv, dev->parent);
		put_device(dev);
	}
}
