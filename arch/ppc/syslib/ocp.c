/*
 * ocp.c
 *
 *      (c) Benjamin Herrenschmidt (benh@kernel.crashing.org)
 *          Mipsys - France
 *
 *          Derived from work (c) Armin Kuster akuster@pacbell.net
 *
 *          Additional support and port to 2.6 LDM/sysfs by
 *          Matt Porter <mporter@kernel.crashing.org>
 *          Copyright 2004 MontaVista Software, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  OCP (On Chip Peripheral) is a software emulated "bus" with a
 *  pseudo discovery method for dumb peripherals. Usually these type
 *  of peripherals are found on embedded SoC (System On a Chip)
 *  processors or highly integrated system controllers that have
 *  a host bridge and many peripherals.  Common examples where
 *  this is already used include the PPC4xx, PPC85xx, MPC52xx,
 *  and MV64xxx parts.
 *
 *  This subsystem creates a standard OCP bus type within the
 *  device model.  The devices on the OCP bus are seeded by an
 *  an initial OCP device array created by the arch-specific
 *  Device entries can be added/removed/modified through OCP
 *  helper functions to accomodate system and  board-specific
 *  parameters commonly found in embedded systems. OCP also
 *  provides a standard method for devices to describe extended
 *  attributes about themselves to the system.  A standard access
 *  method allows OCP drivers to obtain the information, both
 *  SoC-specific and system/board-specific, needed for operation.
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/bootmem.h>
#include <linux/device.h>

#include <asm/io.h>
#include <asm/ocp.h>
#include <asm/errno.h>
#include <asm/rwsem.h>
#include <asm/semaphore.h>

//#define DBG(x)	printk x
#define DBG(x)

extern int mem_init_done;

extern struct ocp_def core_ocp[];	/* Static list of devices, provided by
					   CPU core */

LIST_HEAD(ocp_devices);			/* List of all OCP devices */
DECLARE_RWSEM(ocp_devices_sem);		/* Global semaphores for those lists */

static int ocp_inited;

/* Sysfs support */
#define OCP_DEF_ATTR(field, format_string)				\
static ssize_t								\
show_##field(struct device *dev, char *buf)				\
{									\
	struct ocp_device *odev = to_ocp_dev(dev);			\
									\
	return sprintf(buf, format_string, odev->def->field);		\
}									\
static DEVICE_ATTR(field, S_IRUGO, show_##field, NULL);

OCP_DEF_ATTR(vendor, "0x%04x\n");
OCP_DEF_ATTR(function, "0x%04x\n");
OCP_DEF_ATTR(index, "0x%04x\n");
#ifdef CONFIG_PTE_64BIT
OCP_DEF_ATTR(paddr, "0x%016Lx\n");
#else
OCP_DEF_ATTR(paddr, "0x%08lx\n");
#endif
OCP_DEF_ATTR(irq, "%d\n");
OCP_DEF_ATTR(pm, "%lu\n");

void ocp_create_sysfs_dev_files(struct ocp_device *odev)
{
	struct device *dev = &odev->dev;

	/* Current OCP device def attributes */
	device_create_file(dev, &dev_attr_vendor);
	device_create_file(dev, &dev_attr_function);
	device_create_file(dev, &dev_attr_index);
	device_create_file(dev, &dev_attr_paddr);
	device_create_file(dev, &dev_attr_irq);
	device_create_file(dev, &dev_attr_pm);
	/* Current OCP device additions attributes */
	if (odev->def->additions && odev->def->show)
		odev->def->show(dev);
}

/**
 *	ocp_device_match	-	Match one driver to one device
 *	@drv: driver to match
 *	@dev: device to match
 *
 *	This function returns 0 if the driver and device don't match
 */
static int
ocp_device_match(struct device *dev, struct device_driver *drv)
{
	struct ocp_device *ocp_dev = to_ocp_dev(dev);
	struct ocp_driver *ocp_drv = to_ocp_drv(drv);
	const struct ocp_device_id *ids = ocp_drv->id_table;

	if (!ids)
		return 0;

	while (ids->vendor || ids->function) {
		if ((ids->vendor == OCP_ANY_ID
		     || ids->vendor == ocp_dev->def->vendor)
		    && (ids->function == OCP_ANY_ID
			|| ids->function == ocp_dev->def->function))
		        return 1;
		ids++;
	}
	return 0;
}

static int
ocp_device_probe(struct device *dev)
{
	int error = 0;
	struct ocp_driver *drv;
	struct ocp_device *ocp_dev;

	drv = to_ocp_drv(dev->driver);
	ocp_dev = to_ocp_dev(dev);

	if (drv->probe) {
		error = drv->probe(ocp_dev);
		if (error >= 0) {
			ocp_dev->driver = drv;
			error = 0;
		}
	}
	return error;
}

static int
ocp_device_remove(struct device *dev)
{
	struct ocp_device *ocp_dev = to_ocp_dev(dev);

	if (ocp_dev->driver) {
		if (ocp_dev->driver->remove)
			ocp_dev->driver->remove(ocp_dev);
		ocp_dev->driver = NULL;
	}
	return 0;
}

static int
ocp_device_suspend(struct device *dev, u32 state)
{
	struct ocp_device *ocp_dev = to_ocp_dev(dev);
	struct ocp_driver *ocp_drv = to_ocp_drv(dev->driver);

	if (dev->driver && ocp_drv->suspend)
		return ocp_drv->suspend(ocp_dev, state);
	return 0;
}

static int
ocp_device_resume(struct device *dev)
{
	struct ocp_device *ocp_dev = to_ocp_dev(dev);
	struct ocp_driver *ocp_drv = to_ocp_drv(dev->driver);

	if (dev->driver && ocp_drv->resume)
		return ocp_drv->resume(ocp_dev);
	return 0;
}

struct bus_type ocp_bus_type = {
	.name = "ocp",
	.match = ocp_device_match,
	.suspend = ocp_device_suspend,
	.resume = ocp_device_resume,
};

/**
 *	ocp_register_driver	-	Register an OCP driver
 *	@drv: pointer to statically defined ocp_driver structure
 *
 *	The driver's probe() callback is called either recursively
 *	by this function or upon later call of ocp_driver_init
 *
 *	NOTE: Detection of devices is a 2 pass step on this implementation,
 *	hotswap isn't supported. First, all OCP devices are put in the device
 *	list, _then_ all drivers are probed on each match.
 */
int
ocp_register_driver(struct ocp_driver *drv)
{
	/* initialize common driver fields */
	drv->driver.name = drv->name;
	drv->driver.bus = &ocp_bus_type;
	drv->driver.probe = ocp_device_probe;
	drv->driver.remove = ocp_device_remove;

	/* register with core */
	return driver_register(&drv->driver);
}

/**
 *	ocp_unregister_driver	-	Unregister an OCP driver
 *	@drv: pointer to statically defined ocp_driver structure
 *
 *	The driver's remove() callback is called recursively
 *	by this function for any device already registered
 */
void
ocp_unregister_driver(struct ocp_driver *drv)
{
	DBG(("ocp: ocp_unregister_driver(%s)...\n", drv->name));

	driver_unregister(&drv->driver);

	DBG(("ocp: ocp_unregister_driver(%s)... done.\n", drv->name));
}

/* Core of ocp_find_device(). Caller must hold ocp_devices_sem */
static struct ocp_device *
__ocp_find_device(unsigned int vendor, unsigned int function, int index)
{
	struct list_head	*entry;
	struct ocp_device	*dev, *found = NULL;

	DBG(("ocp: __ocp_find_device(vendor: %x, function: %x, index: %d)...\n", vendor, function, index));

	list_for_each(entry, &ocp_devices) {
		dev = list_entry(entry, struct ocp_device, link);
		if (vendor != OCP_ANY_ID && vendor != dev->def->vendor)
			continue;
		if (function != OCP_ANY_ID && function != dev->def->function)
			continue;
		if (index != OCP_ANY_INDEX && index != dev->def->index)
			continue;
		found = dev;
		break;
	}

	DBG(("ocp: __ocp_find_device(vendor: %x, function: %x, index: %d)... done\n", vendor, function, index));

	return found;
}

/**
 *	ocp_find_device	-	Find a device by function & index
 *      @vendor: vendor ID of the device (or OCP_ANY_ID)
 *	@function: function code of the device (or OCP_ANY_ID)
 *	@idx: index of the device (or OCP_ANY_INDEX)
 *
 *	This function allows a lookup of a given function by it's
 *	index, it's typically used to find the MAL or ZMII associated
 *	with an EMAC or similar horrors.
 *      You can pass vendor, though you usually want OCP_ANY_ID there...
 */
struct ocp_device *
ocp_find_device(unsigned int vendor, unsigned int function, int index)
{
	struct ocp_device	*dev;

	down_read(&ocp_devices_sem);
	dev = __ocp_find_device(vendor, function, index);
	up_read(&ocp_devices_sem);

	return dev;
}

/**
 *	ocp_get_one_device -	Find a def by function & index
 *      @vendor: vendor ID of the device (or OCP_ANY_ID)
 *	@function: function code of the device (or OCP_ANY_ID)
 *	@idx: index of the device (or OCP_ANY_INDEX)
 *
 *	This function allows a lookup of a given ocp_def by it's
 *	vendor, function, and index.  The main purpose for is to
 *	allow modification of the def before binding to the driver
 */
struct ocp_def *
ocp_get_one_device(unsigned int vendor, unsigned int function, int index)
{
	struct ocp_device	*dev;
	struct ocp_def		*found = NULL;

	DBG(("ocp: ocp_get_one_device(vendor: %x, function: %x, index: %d)...\n",
		vendor, function, index));

	dev = ocp_find_device(vendor, function, index);

	if (dev)
		found = dev->def;

	DBG(("ocp: ocp_get_one_device(vendor: %x, function: %x, index: %d)... done.\n",
		vendor, function, index));

	return found;
}

/**
 *	ocp_add_one_device	-	Add a device
 *	@def: static device definition structure
 *
 *	This function adds a device definition to the
 *	device list. It may only be called before
 *	ocp_driver_init() and will return an error
 *	otherwise.
 */
int
ocp_add_one_device(struct ocp_def *def)
{
	struct	ocp_device	*dev;

	DBG(("ocp: ocp_add_one_device()...\n"));

	/* Can't be called after ocp driver init */
	if (ocp_inited)
		return 1;

	if (mem_init_done)
		dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	else
		dev = alloc_bootmem(sizeof(*dev));

	if (dev == NULL)
		return 1;
	memset(dev, 0, sizeof(*dev));
	dev->def = def;
	dev->current_state = 4;
	sprintf(dev->name, "OCP device %04x:%04x:%04x",
		dev->def->vendor, dev->def->function, dev->def->index);
	down_write(&ocp_devices_sem);
	list_add_tail(&dev->link, &ocp_devices);
	up_write(&ocp_devices_sem);

	DBG(("ocp: ocp_add_one_device()...done\n"));

	return 0;
}

/**
 *	ocp_remove_one_device -	Remove a device by function & index
 *      @vendor: vendor ID of the device (or OCP_ANY_ID)
 *	@function: function code of the device (or OCP_ANY_ID)
 *	@idx: index of the device (or OCP_ANY_INDEX)
 *
 *	This function allows removal of a given function by its
 *	index. It may only be called before ocp_driver_init()
 *	and will return an error otherwise.
 */
int
ocp_remove_one_device(unsigned int vendor, unsigned int function, int index)
{
	struct ocp_device *dev;

	DBG(("ocp: ocp_remove_one_device(vendor: %x, function: %x, index: %d)...\n", vendor, function, index));

	/* Can't be called after ocp driver init */
	if (ocp_inited)
		return 1;

	down_write(&ocp_devices_sem);
	dev = __ocp_find_device(vendor, function, index);
	list_del((struct list_head *)dev);
	up_write(&ocp_devices_sem);

	DBG(("ocp: ocp_remove_one_device(vendor: %x, function: %x, index: %d)... done.\n", vendor, function, index));

	return 0;
}

/**
 *	ocp_for_each_device	-	Iterate over OCP devices
 *	@callback: routine to execute for each ocp device.
 *	@arg: user data to be passed to callback routine.
 *
 *	This routine holds the ocp_device semaphore, so the
 *	callback routine cannot modify the ocp_device list.
 */
void
ocp_for_each_device(void(*callback)(struct ocp_device *, void *arg), void *arg)
{
	struct list_head *entry;

	if (callback) {
		down_read(&ocp_devices_sem);
		list_for_each(entry, &ocp_devices)
			callback(list_entry(entry, struct ocp_device, link),
				arg);
		up_read(&ocp_devices_sem);
	}
}

/**
 *	ocp_early_init	-	Init OCP device management
 *
 *	This function builds the list of devices before setup_arch.
 *	This allows platform code to modify the device lists before
 *	they are bound to drivers (changes to paddr, removing devices
 *	etc)
 */
int __init
ocp_early_init(void)
{
	struct ocp_def	*def;

	DBG(("ocp: ocp_early_init()...\n"));

	/* Fill the devices list */
	for (def = core_ocp; def->vendor != OCP_VENDOR_INVALID; def++)
		ocp_add_one_device(def);

	DBG(("ocp: ocp_early_init()... done.\n"));

	return 0;
}

/**
 *	ocp_driver_init	-	Init OCP device management
 *
 *	This function is meant to be called via OCP bus registration.
 */
static int __init
ocp_driver_init(void)
{
	int ret = 0, index = 0;
	struct device *ocp_bus;
	struct list_head *entry;
	struct ocp_device *dev;

	if (ocp_inited)
		return ret;
	ocp_inited = 1;

	DBG(("ocp: ocp_driver_init()...\n"));

	/* Allocate/register primary OCP bus */
	ocp_bus = kmalloc(sizeof(struct device), GFP_KERNEL);
	if (ocp_bus == NULL)
		return 1;
	memset(ocp_bus, 0, sizeof(struct device));
	strcpy(ocp_bus->bus_id, "ocp");

	bus_register(&ocp_bus_type);

	device_register(ocp_bus);

	/* Put each OCP device into global device list */
	list_for_each(entry, &ocp_devices) {
		dev = list_entry(entry, struct ocp_device, link);
		sprintf(dev->dev.bus_id, "%2.2x", index);
		dev->dev.parent = ocp_bus;
		dev->dev.bus = &ocp_bus_type;
		device_register(&dev->dev);
		ocp_create_sysfs_dev_files(dev);
		index++;
	}

	DBG(("ocp: ocp_driver_init()... done.\n"));

	return 0;
}

postcore_initcall(ocp_driver_init);

EXPORT_SYMBOL(ocp_bus_type);
EXPORT_SYMBOL(ocp_find_device);
EXPORT_SYMBOL(ocp_register_driver);
EXPORT_SYMBOL(ocp_unregister_driver);
