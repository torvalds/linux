/*
 *	drivers/base/dd.c - The core device/driver interactions.
 *
 * 	This file contains the (sometimes tricky) code that controls the
 *	interactions between devices and drivers, which primarily includes
 *	driver binding and unbinding.
 *
 *	All of this code used to exist in drivers/base/bus.c, but was
 *	relocated to here in the name of compartmentalization (since it wasn't
 *	strictly code just for the 'struct bus_type'.
 *
 *	Copyright (c) 2002-5 Patrick Mochel
 *	Copyright (c) 2002-3 Open Source Development Labs
 *
 *	This file is released under the GPLv2
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/wait.h>

#include "base.h"
#include "power/power.h"

#define to_drv(node) container_of(node, struct device_driver, kobj.entry)


/**
 *	device_bind_driver - bind a driver to one device.
 *	@dev:	device.
 *
 *	Allow manual attachment of a driver to a device.
 *	Caller must have already set @dev->driver.
 *
 *	Note that this does not modify the bus reference count
 *	nor take the bus's rwsem. Please verify those are accounted
 *	for before calling this. (It is ok to call with no other effort
 *	from a driver's probe() method.)
 *
 *	This function must be called with @dev->sem held.
 */
int device_bind_driver(struct device *dev)
{
	int ret;

	if (klist_node_attached(&dev->knode_driver)) {
		printk(KERN_WARNING "%s: device %s already bound\n",
			__FUNCTION__, kobject_name(&dev->kobj));
		return 0;
	}

	pr_debug("bound device '%s' to driver '%s'\n",
		 dev->bus_id, dev->driver->name);

	if (dev->bus)
		blocking_notifier_call_chain(&dev->bus->bus_notifier,
					     BUS_NOTIFY_BOUND_DRIVER, dev);

	klist_add_tail(&dev->knode_driver, &dev->driver->klist_devices);
	ret = sysfs_create_link(&dev->driver->kobj, &dev->kobj,
			  kobject_name(&dev->kobj));
	if (ret == 0) {
		ret = sysfs_create_link(&dev->kobj, &dev->driver->kobj,
					"driver");
		if (ret)
			sysfs_remove_link(&dev->driver->kobj,
					kobject_name(&dev->kobj));
	}
	return ret;
}

struct stupid_thread_structure {
	struct device_driver *drv;
	struct device *dev;
};

static atomic_t probe_count = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(probe_waitqueue);

static int really_probe(void *void_data)
{
	struct stupid_thread_structure *data = void_data;
	struct device_driver *drv = data->drv;
	struct device *dev = data->dev;
	int ret = 0;

	atomic_inc(&probe_count);
	pr_debug("%s: Probing driver %s with device %s\n",
		 drv->bus->name, drv->name, dev->bus_id);

	dev->driver = drv;
	if (dev->bus->probe) {
		ret = dev->bus->probe(dev);
		if (ret) {
			dev->driver = NULL;
			goto probe_failed;
		}
	} else if (drv->probe) {
		ret = drv->probe(dev);
		if (ret) {
			dev->driver = NULL;
			goto probe_failed;
		}
	}
	if (device_bind_driver(dev)) {
		printk(KERN_ERR "%s: device_bind_driver(%s) failed\n",
			__FUNCTION__, dev->bus_id);
		/* How does undo a ->probe?  We're screwed. */
	}
	ret = 1;
	pr_debug("%s: Bound Device %s to Driver %s\n",
		 drv->bus->name, dev->bus_id, drv->name);
	goto done;

probe_failed:
	if (ret == -ENODEV || ret == -ENXIO) {
		/* Driver matched, but didn't support device
		 * or device not found.
		 * Not an error; keep going.
		 */
		ret = 0;
	} else {
		/* driver matched but the probe failed */
		printk(KERN_WARNING
		       "%s: probe of %s failed with error %d\n",
		       drv->name, dev->bus_id, ret);
	}
done:
	kfree(data);
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
	pr_debug("%s: probe_count = %d\n", __FUNCTION__,
		 atomic_read(&probe_count));
	if (atomic_read(&probe_count))
		return -EBUSY;
	return 0;
}

/**
 * driver_probe_device - attempt to bind device & driver together
 * @drv: driver to bind a device to
 * @dev: device to try to bind to the driver
 *
 * First, we call the bus's match function, if one present, which should
 * compare the device IDs the driver supports with the device IDs of the
 * device. Note we don't do this ourselves because we don't know the
 * format of the ID structures, nor what is to be considered a match and
 * what is not.
 *
 * This function returns 1 if a match is found, an error if one occurs
 * (that is not -ENODEV or -ENXIO), and 0 otherwise.
 *
 * This function must be called with @dev->sem held.  When called for a
 * USB interface, @dev->parent->sem must be held as well.
 */
int driver_probe_device(struct device_driver * drv, struct device * dev)
{
	struct stupid_thread_structure *data;
	struct task_struct *probe_task;
	int ret = 0;

	if (!device_is_registered(dev))
		return -ENODEV;
	if (drv->bus->match && !drv->bus->match(dev, drv))
		goto done;

	pr_debug("%s: Matched Device %s with Driver %s\n",
		 drv->bus->name, dev->bus_id, drv->name);

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->drv = drv;
	data->dev = dev;

	if (drv->multithread_probe) {
		probe_task = kthread_run(really_probe, data,
					 "probe-%s", dev->bus_id);
		if (IS_ERR(probe_task))
			ret = really_probe(data);
	} else
		ret = really_probe(data);

done:
	return ret;
}

static int __device_attach(struct device_driver * drv, void * data)
{
	struct device * dev = data;
	return driver_probe_device(drv, dev);
}

/**
 *	device_attach - try to attach device to a driver.
 *	@dev:	device.
 *
 *	Walk the list of drivers that the bus has and call
 *	driver_probe_device() for each pair. If a compatible
 *	pair is found, break out and return.
 *
 *	Returns 1 if the device was bound to a driver;
 *	0 if no matching device was found; error code otherwise.
 *
 *	When called for a USB interface, @dev->parent->sem must be held.
 */
int device_attach(struct device * dev)
{
	int ret = 0;

	down(&dev->sem);
	if (dev->driver) {
		ret = device_bind_driver(dev);
		if (ret == 0)
			ret = 1;
	} else
		ret = bus_for_each_drv(dev->bus, NULL, dev, __device_attach);
	up(&dev->sem);
	return ret;
}

static int __driver_attach(struct device * dev, void * data)
{
	struct device_driver * drv = data;

	/*
	 * Lock device and try to bind to it. We drop the error
	 * here and always return 0, because we need to keep trying
	 * to bind to devices and some drivers will return an error
	 * simply if it didn't support the device.
	 *
	 * driver_probe_device() will spit a warning if there
	 * is an error.
	 */

	if (dev->parent)	/* Needed for USB */
		down(&dev->parent->sem);
	down(&dev->sem);
	if (!dev->driver)
		driver_probe_device(drv, dev);
	up(&dev->sem);
	if (dev->parent)
		up(&dev->parent->sem);

	return 0;
}

/**
 *	driver_attach - try to bind driver to devices.
 *	@drv:	driver.
 *
 *	Walk the list of devices that the bus has on it and try to
 *	match the driver with each one.  If driver_probe_device()
 *	returns 0 and the @dev->driver is set, we've found a
 *	compatible pair.
 */
int driver_attach(struct device_driver * drv)
{
	return bus_for_each_dev(drv->bus, NULL, drv, __driver_attach);
}

/**
 *	device_release_driver - manually detach device from driver.
 *	@dev:	device.
 *
 *	Manually detach device from driver.
 *
 *	__device_release_driver() must be called with @dev->sem held.
 *	When called for a USB interface, @dev->parent->sem must be held
 *	as well.
 */

static void __device_release_driver(struct device * dev)
{
	struct device_driver * drv;

	drv = dev->driver;
	if (drv) {
		get_driver(drv);
		sysfs_remove_link(&drv->kobj, kobject_name(&dev->kobj));
		sysfs_remove_link(&dev->kobj, "driver");
		klist_remove(&dev->knode_driver);

		if (dev->bus)
			blocking_notifier_call_chain(&dev->bus->bus_notifier,
						     BUS_NOTIFY_UNBIND_DRIVER,
						     dev);

		if (dev->bus && dev->bus->remove)
			dev->bus->remove(dev);
		else if (drv->remove)
			drv->remove(dev);
		dev->driver = NULL;
		put_driver(drv);
	}
}

void device_release_driver(struct device * dev)
{
	/*
	 * If anyone calls device_release_driver() recursively from
	 * within their ->remove callback for the same device, they
	 * will deadlock right here.
	 */
	down(&dev->sem);
	__device_release_driver(dev);
	up(&dev->sem);
}


/**
 * driver_detach - detach driver from all devices it controls.
 * @drv: driver.
 */
void driver_detach(struct device_driver * drv)
{
	struct device * dev;

	for (;;) {
		spin_lock(&drv->klist_devices.k_lock);
		if (list_empty(&drv->klist_devices.k_list)) {
			spin_unlock(&drv->klist_devices.k_lock);
			break;
		}
		dev = list_entry(drv->klist_devices.k_list.prev,
				struct device, knode_driver.n_node);
		get_device(dev);
		spin_unlock(&drv->klist_devices.k_lock);

		if (dev->parent)	/* Needed for USB */
			down(&dev->parent->sem);
		down(&dev->sem);
		if (dev->driver == drv)
			__device_release_driver(dev);
		up(&dev->sem);
		if (dev->parent)
			up(&dev->parent->sem);
		put_device(dev);
	}
}

#ifdef CONFIG_PCI_MULTITHREAD_PROBE
static int __init wait_for_probes(void)
{
	DEFINE_WAIT(wait);

	printk(KERN_INFO "%s: waiting for %d threads\n", __FUNCTION__,
			atomic_read(&probe_count));
	if (!atomic_read(&probe_count))
		return 0;
	while (atomic_read(&probe_count)) {
		prepare_to_wait(&probe_waitqueue, &wait, TASK_UNINTERRUPTIBLE);
		if (atomic_read(&probe_count))
			schedule();
	}
	finish_wait(&probe_waitqueue, &wait);
	return 0;
}

core_initcall_sync(wait_for_probes);
postcore_initcall_sync(wait_for_probes);
arch_initcall_sync(wait_for_probes);
subsys_initcall_sync(wait_for_probes);
fs_initcall_sync(wait_for_probes);
device_initcall_sync(wait_for_probes);
late_initcall_sync(wait_for_probes);
#endif

EXPORT_SYMBOL_GPL(device_bind_driver);
EXPORT_SYMBOL_GPL(device_release_driver);
EXPORT_SYMBOL_GPL(device_attach);
EXPORT_SYMBOL_GPL(driver_attach);

