/*P:050 Lguest guests use a very simple bus for devices.  It's a simple array
 * of device descriptors contained just above the top of normal memory.  The
 * lguest bus is 80% tedious boilerplate code. :*/
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/lguest_bus.h>
#include <asm/io.h>
#include <asm/paravirt.h>

static ssize_t type_show(struct device *_dev,
                         struct device_attribute *attr, char *buf)
{
	struct lguest_device *dev = container_of(_dev,struct lguest_device,dev);
	return sprintf(buf, "%hu", lguest_devices[dev->index].type);
}
static ssize_t features_show(struct device *_dev,
                             struct device_attribute *attr, char *buf)
{
	struct lguest_device *dev = container_of(_dev,struct lguest_device,dev);
	return sprintf(buf, "%hx", lguest_devices[dev->index].features);
}
static ssize_t pfn_show(struct device *_dev,
			 struct device_attribute *attr, char *buf)
{
	struct lguest_device *dev = container_of(_dev,struct lguest_device,dev);
	return sprintf(buf, "%u", lguest_devices[dev->index].pfn);
}
static ssize_t status_show(struct device *_dev,
                           struct device_attribute *attr, char *buf)
{
	struct lguest_device *dev = container_of(_dev,struct lguest_device,dev);
	return sprintf(buf, "%hx", lguest_devices[dev->index].status);
}
static ssize_t status_store(struct device *_dev, struct device_attribute *attr,
                            const char *buf, size_t count)
{
	struct lguest_device *dev = container_of(_dev,struct lguest_device,dev);
	if (sscanf(buf, "%hi", &lguest_devices[dev->index].status) != 1)
		return -EINVAL;
	return count;
}
static struct device_attribute lguest_dev_attrs[] = {
	__ATTR_RO(type),
	__ATTR_RO(features),
	__ATTR_RO(pfn),
	__ATTR(status, 0644, status_show, status_store),
	__ATTR_NULL
};

/*D:130 The generic bus infrastructure requires a function which says whether a
 * device matches a driver.  For us, it is simple: "struct lguest_driver"
 * contains a "device_type" field which indicates what type of device it can
 * handle, so we just cast the args and compare: */
static int lguest_dev_match(struct device *_dev, struct device_driver *_drv)
{
	struct lguest_device *dev = container_of(_dev,struct lguest_device,dev);
	struct lguest_driver *drv = container_of(_drv,struct lguest_driver,drv);

	return (drv->device_type == lguest_devices[dev->index].type);
}
/*:*/

struct lguest_bus {
	struct bus_type bus;
	struct device dev;
};

static struct lguest_bus lguest_bus = {
	.bus = {
		.name  = "lguest",
		.match = lguest_dev_match,
		.dev_attrs = lguest_dev_attrs,
	},
	.dev = {
		.parent = NULL,
		.bus_id = "lguest",
	}
};

/*D:140 This is the callback which occurs once the bus infrastructure matches
 * up a device and driver, ie. in response to add_lguest_device() calling
 * device_register(), or register_lguest_driver() calling driver_register().
 *
 * At the moment it's always the latter: the devices are added first, since
 * scan_devices() is called from a "core_initcall", and the drivers themselves
 * called later as a normal "initcall".  But it would work the other way too.
 *
 * So now we have the happy couple, we add the status bit to indicate that we
 * found a driver.  If the driver truly loves the device, it will return
 * happiness from its probe function (ok, perhaps this wasn't my greatest
 * analogy), and we set the final "driver ok" bit so the Host sees it's all
 * green. */
static int lguest_dev_probe(struct device *_dev)
{
	int ret;
	struct lguest_device*dev = container_of(_dev,struct lguest_device,dev);
	struct lguest_driver*drv = container_of(dev->dev.driver,
						struct lguest_driver, drv);

	lguest_devices[dev->index].status |= LGUEST_DEVICE_S_DRIVER;
	ret = drv->probe(dev);
	if (ret == 0)
		lguest_devices[dev->index].status |= LGUEST_DEVICE_S_DRIVER_OK;
	return ret;
}

/* The last part of the bus infrastructure is the function lguest drivers use
 * to register themselves.  Firstly, we do nothing if there's no lguest bus
 * (ie. this is not a Guest), otherwise we fill in the embedded generic "struct
 * driver" fields and call the generic driver_register(). */
int register_lguest_driver(struct lguest_driver *drv)
{
	if (!lguest_devices)
		return 0;

	drv->drv.bus = &lguest_bus.bus;
	drv->drv.name = drv->name;
	drv->drv.owner = drv->owner;
	drv->drv.probe = lguest_dev_probe;

	return driver_register(&drv->drv);
}

/* At the moment we build all the drivers into the kernel because they're so
 * simple: 8144 bytes for all three of them as I type this.  And as the console
 * really needs to be built in, it's actually only 3527 bytes for the network
 * and block drivers.
 *
 * If they get complex it will make sense for them to be modularized, so we
 * need to explicitly export the symbol.
 *
 * I don't think non-GPL modules make sense, so it's a GPL-only export.
 */
EXPORT_SYMBOL_GPL(register_lguest_driver);

/*D:120 This is the core of the lguest bus: actually adding a new device.
 * It's a separate function because it's neater that way, and because an
 * earlier version of the code supported hotplug and unplug.  They were removed
 * early on because they were never used.
 *
 * As Andrew Tridgell says, "Untested code is buggy code".
 *
 * It's worth reading this carefully: we start with an index into the array of
 * "struct lguest_device_desc"s indicating the device which is new: */
static void add_lguest_device(unsigned int index)
{
	struct lguest_device *new;

	/* Each "struct lguest_device_desc" has a "status" field, which the
	 * Guest updates as the device is probed.  In the worst case, the Host
	 * can look at these bits to tell what part of device setup failed,
	 * even if the console isn't available. */
	lguest_devices[index].status |= LGUEST_DEVICE_S_ACKNOWLEDGE;
	new = kmalloc(sizeof(struct lguest_device), GFP_KERNEL);
	if (!new) {
		printk(KERN_EMERG "Cannot allocate lguest device %u\n", index);
		lguest_devices[index].status |= LGUEST_DEVICE_S_FAILED;
		return;
	}

	/* The "struct lguest_device" setup is pretty straight-forward example
	 * code. */
	new->index = index;
	new->private = NULL;
	memset(&new->dev, 0, sizeof(new->dev));
	new->dev.parent = &lguest_bus.dev;
	new->dev.bus = &lguest_bus.bus;
	sprintf(new->dev.bus_id, "%u", index);

	/* device_register() causes the bus infrastructure to look for a
	 * matching driver. */
	if (device_register(&new->dev) != 0) {
		printk(KERN_EMERG "Cannot register lguest device %u\n", index);
		lguest_devices[index].status |= LGUEST_DEVICE_S_FAILED;
		kfree(new);
	}
}

/*D:110 scan_devices() simply iterates through the device array.  The type 0
 * is reserved to mean "no device", and anything else means we have found a
 * device: add it. */
static void scan_devices(void)
{
	unsigned int i;

	for (i = 0; i < LGUEST_MAX_DEVICES; i++)
		if (lguest_devices[i].type)
			add_lguest_device(i);
}

/*D:100 Fairly early in boot, lguest_bus_init() is called to set up the lguest
 * bus.  We check that we are a Guest by checking paravirt_ops.name: there are
 * other ways of checking, but this seems most obvious to me.
 *
 * So we can access the array of "struct lguest_device_desc"s easily, we map
 * that memory and store the pointer in the global "lguest_devices".  Then we
 * register the bus with the core.  Doing two registrations seems clunky to me,
 * but it seems to be the correct sysfs incantation.
 *
 * Finally we call scan_devices() which adds all the devices found in the
 * "struct lguest_device_desc" array. */
static int __init lguest_bus_init(void)
{
	if (strcmp(pv_info.name, "lguest") != 0)
		return 0;

	/* Devices are in a single page above top of "normal" mem */
	lguest_devices = lguest_map(max_pfn<<PAGE_SHIFT, 1);

	if (bus_register(&lguest_bus.bus) != 0
	    || device_register(&lguest_bus.dev) != 0)
		panic("lguest bus registration failed");

	scan_devices();
	return 0;
}
/* Do this after core stuff, before devices. */
postcore_initcall(lguest_bus_init);
