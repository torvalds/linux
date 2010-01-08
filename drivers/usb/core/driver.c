/*
 * drivers/usb/driver.c - most of the driver model stuff for usb
 *
 * (C) Copyright 2005 Greg Kroah-Hartman <gregkh@suse.de>
 *
 * based on drivers/usb/usb.c which had the following copyrights:
 *	(C) Copyright Linus Torvalds 1999
 *	(C) Copyright Johannes Erdfelt 1999-2001
 *	(C) Copyright Andreas Gal 1999
 *	(C) Copyright Gregory P. Smith 1999
 *	(C) Copyright Deti Fliegl 1999 (new USB architecture)
 *	(C) Copyright Randy Dunlap 2000
 *	(C) Copyright David Brownell 2000-2004
 *	(C) Copyright Yggdrasil Computing, Inc. 2000
 *		(usb_device_id matching changes by Adam J. Richter)
 *	(C) Copyright Greg Kroah-Hartman 2002-2003
 *
 * NOTE! This is not actually a driver at all, rather this is
 * just a collection of helper routines that implement the
 * matching, probing, releasing, suspending and resuming for
 * real drivers.
 *
 */

#include <linux/device.h>
#include <linux/usb.h>
#include <linux/usb/quirks.h>
#include <linux/workqueue.h>
#include "hcd.h"
#include "usb.h"


#ifdef CONFIG_HOTPLUG

/*
 * Adds a new dynamic USBdevice ID to this driver,
 * and cause the driver to probe for all devices again.
 */
ssize_t usb_store_new_id(struct usb_dynids *dynids,
			 struct device_driver *driver,
			 const char *buf, size_t count)
{
	struct usb_dynid *dynid;
	u32 idVendor = 0;
	u32 idProduct = 0;
	int fields = 0;
	int retval = 0;

	fields = sscanf(buf, "%x %x", &idVendor, &idProduct);
	if (fields < 2)
		return -EINVAL;

	dynid = kzalloc(sizeof(*dynid), GFP_KERNEL);
	if (!dynid)
		return -ENOMEM;

	INIT_LIST_HEAD(&dynid->node);
	dynid->id.idVendor = idVendor;
	dynid->id.idProduct = idProduct;
	dynid->id.match_flags = USB_DEVICE_ID_MATCH_DEVICE;

	spin_lock(&dynids->lock);
	list_add_tail(&dynid->node, &dynids->list);
	spin_unlock(&dynids->lock);

	if (get_driver(driver)) {
		retval = driver_attach(driver);
		put_driver(driver);
	}

	if (retval)
		return retval;
	return count;
}
EXPORT_SYMBOL_GPL(usb_store_new_id);

static ssize_t store_new_id(struct device_driver *driver,
			    const char *buf, size_t count)
{
	struct usb_driver *usb_drv = to_usb_driver(driver);

	return usb_store_new_id(&usb_drv->dynids, driver, buf, count);
}
static DRIVER_ATTR(new_id, S_IWUSR, NULL, store_new_id);

/**
 * store_remove_id - remove a USB device ID from this driver
 * @driver: target device driver
 * @buf: buffer for scanning device ID data
 * @count: input size
 *
 * Removes a dynamic usb device ID from this driver.
 */
static ssize_t
store_remove_id(struct device_driver *driver, const char *buf, size_t count)
{
	struct usb_dynid *dynid, *n;
	struct usb_driver *usb_driver = to_usb_driver(driver);
	u32 idVendor = 0;
	u32 idProduct = 0;
	int fields = 0;
	int retval = 0;

	fields = sscanf(buf, "%x %x", &idVendor, &idProduct);
	if (fields < 2)
		return -EINVAL;

	spin_lock(&usb_driver->dynids.lock);
	list_for_each_entry_safe(dynid, n, &usb_driver->dynids.list, node) {
		struct usb_device_id *id = &dynid->id;
		if ((id->idVendor == idVendor) &&
		    (id->idProduct == idProduct)) {
			list_del(&dynid->node);
			kfree(dynid);
			retval = 0;
			break;
		}
	}
	spin_unlock(&usb_driver->dynids.lock);

	if (retval)
		return retval;
	return count;
}
static DRIVER_ATTR(remove_id, S_IWUSR, NULL, store_remove_id);

static int usb_create_newid_file(struct usb_driver *usb_drv)
{
	int error = 0;

	if (usb_drv->no_dynamic_id)
		goto exit;

	if (usb_drv->probe != NULL)
		error = driver_create_file(&usb_drv->drvwrap.driver,
					   &driver_attr_new_id);
exit:
	return error;
}

static void usb_remove_newid_file(struct usb_driver *usb_drv)
{
	if (usb_drv->no_dynamic_id)
		return;

	if (usb_drv->probe != NULL)
		driver_remove_file(&usb_drv->drvwrap.driver,
				   &driver_attr_new_id);
}

static int
usb_create_removeid_file(struct usb_driver *drv)
{
	int error = 0;
	if (drv->probe != NULL)
		error = driver_create_file(&drv->drvwrap.driver,
				&driver_attr_remove_id);
	return error;
}

static void usb_remove_removeid_file(struct usb_driver *drv)
{
	driver_remove_file(&drv->drvwrap.driver, &driver_attr_remove_id);
}

static void usb_free_dynids(struct usb_driver *usb_drv)
{
	struct usb_dynid *dynid, *n;

	spin_lock(&usb_drv->dynids.lock);
	list_for_each_entry_safe(dynid, n, &usb_drv->dynids.list, node) {
		list_del(&dynid->node);
		kfree(dynid);
	}
	spin_unlock(&usb_drv->dynids.lock);
}
#else
static inline int usb_create_newid_file(struct usb_driver *usb_drv)
{
	return 0;
}

static void usb_remove_newid_file(struct usb_driver *usb_drv)
{
}

static int
usb_create_removeid_file(struct usb_driver *drv)
{
	return 0;
}

static void usb_remove_removeid_file(struct usb_driver *drv)
{
}

static inline void usb_free_dynids(struct usb_driver *usb_drv)
{
}
#endif

static const struct usb_device_id *usb_match_dynamic_id(struct usb_interface *intf,
							struct usb_driver *drv)
{
	struct usb_dynid *dynid;

	spin_lock(&drv->dynids.lock);
	list_for_each_entry(dynid, &drv->dynids.list, node) {
		if (usb_match_one_id(intf, &dynid->id)) {
			spin_unlock(&drv->dynids.lock);
			return &dynid->id;
		}
	}
	spin_unlock(&drv->dynids.lock);
	return NULL;
}


/* called from driver core with dev locked */
static int usb_probe_device(struct device *dev)
{
	struct usb_device_driver *udriver = to_usb_device_driver(dev->driver);
	struct usb_device *udev = to_usb_device(dev);
	int error = -ENODEV;

	dev_dbg(dev, "%s\n", __func__);

	/* TODO: Add real matching code */

	/* The device should always appear to be in use
	 * unless the driver suports autosuspend.
	 */
	udev->pm_usage_cnt = !(udriver->supports_autosuspend);

	error = udriver->probe(udev);
	return error;
}

/* called from driver core with dev locked */
static int usb_unbind_device(struct device *dev)
{
	struct usb_device_driver *udriver = to_usb_device_driver(dev->driver);

	udriver->disconnect(to_usb_device(dev));
	return 0;
}

/*
 * Cancel any pending scheduled resets
 *
 * [see usb_queue_reset_device()]
 *
 * Called after unconfiguring / when releasing interfaces. See
 * comments in __usb_queue_reset_device() regarding
 * udev->reset_running.
 */
static void usb_cancel_queued_reset(struct usb_interface *iface)
{
	if (iface->reset_running == 0)
		cancel_work_sync(&iface->reset_ws);
}

/* called from driver core with dev locked */
static int usb_probe_interface(struct device *dev)
{
	struct usb_driver *driver = to_usb_driver(dev->driver);
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_device *udev = interface_to_usbdev(intf);
	const struct usb_device_id *id;
	int error = -ENODEV;

	dev_dbg(dev, "%s\n", __func__);

	intf->needs_binding = 0;

	if (usb_device_is_owned(udev))
		return error;

	if (udev->authorized == 0) {
		dev_err(&intf->dev, "Device is not authorized for usage\n");
		return error;
	}

	id = usb_match_id(intf, driver->id_table);
	if (!id)
		id = usb_match_dynamic_id(intf, driver);
	if (!id)
		return error;

	dev_dbg(dev, "%s - got id\n", __func__);

	error = usb_autoresume_device(udev);
	if (error)
		return error;

	/* Interface "power state" doesn't correspond to any hardware
	 * state whatsoever.  We use it to record when it's bound to
	 * a driver that may start I/0:  it's not frozen/quiesced.
	 */
	mark_active(intf);
	intf->condition = USB_INTERFACE_BINDING;

	/* The interface should always appear to be in use
	 * unless the driver suports autosuspend.
	 */
	atomic_set(&intf->pm_usage_cnt, !driver->supports_autosuspend);

	/* Carry out a deferred switch to altsetting 0 */
	if (intf->needs_altsetting0) {
		error = usb_set_interface(udev, intf->altsetting[0].
				desc.bInterfaceNumber, 0);
		if (error < 0)
			goto err;
		intf->needs_altsetting0 = 0;
	}

	error = driver->probe(intf, id);
	if (error)
		goto err;

	intf->condition = USB_INTERFACE_BOUND;
	usb_autosuspend_device(udev);
	return error;

 err:
	mark_quiesced(intf);
	intf->needs_remote_wakeup = 0;
	intf->condition = USB_INTERFACE_UNBOUND;
	usb_cancel_queued_reset(intf);
	usb_autosuspend_device(udev);
	return error;
}

/* called from driver core with dev locked */
static int usb_unbind_interface(struct device *dev)
{
	struct usb_driver *driver = to_usb_driver(dev->driver);
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_device *udev;
	int error, r;

	intf->condition = USB_INTERFACE_UNBINDING;

	/* Autoresume for set_interface call below */
	udev = interface_to_usbdev(intf);
	error = usb_autoresume_device(udev);

	/* Terminate all URBs for this interface unless the driver
	 * supports "soft" unbinding.
	 */
	if (!driver->soft_unbind)
		usb_disable_interface(udev, intf, false);

	driver->disconnect(intf);
	usb_cancel_queued_reset(intf);

	/* Reset other interface state.
	 * We cannot do a Set-Interface if the device is suspended or
	 * if it is prepared for a system sleep (since installing a new
	 * altsetting means creating new endpoint device entries).
	 * When either of these happens, defer the Set-Interface.
	 */
	if (intf->cur_altsetting->desc.bAlternateSetting == 0) {
		/* Already in altsetting 0 so skip Set-Interface.
		 * Just re-enable it without affecting the endpoint toggles.
		 */
		usb_enable_interface(udev, intf, false);
	} else if (!error && intf->dev.power.status == DPM_ON) {
		r = usb_set_interface(udev, intf->altsetting[0].
				desc.bInterfaceNumber, 0);
		if (r < 0)
			intf->needs_altsetting0 = 1;
	} else {
		intf->needs_altsetting0 = 1;
	}
	usb_set_intfdata(intf, NULL);

	intf->condition = USB_INTERFACE_UNBOUND;
	mark_quiesced(intf);
	intf->needs_remote_wakeup = 0;

	if (!error)
		usb_autosuspend_device(udev);

	return 0;
}

/**
 * usb_driver_claim_interface - bind a driver to an interface
 * @driver: the driver to be bound
 * @iface: the interface to which it will be bound; must be in the
 *	usb device's active configuration
 * @priv: driver data associated with that interface
 *
 * This is used by usb device drivers that need to claim more than one
 * interface on a device when probing (audio and acm are current examples).
 * No device driver should directly modify internal usb_interface or
 * usb_device structure members.
 *
 * Few drivers should need to use this routine, since the most natural
 * way to bind to an interface is to return the private data from
 * the driver's probe() method.
 *
 * Callers must own the device lock, so driver probe() entries don't need
 * extra locking, but other call contexts may need to explicitly claim that
 * lock.
 */
int usb_driver_claim_interface(struct usb_driver *driver,
				struct usb_interface *iface, void *priv)
{
	struct device *dev = &iface->dev;
	struct usb_device *udev = interface_to_usbdev(iface);
	int retval = 0;

	if (dev->driver)
		return -EBUSY;

	dev->driver = &driver->drvwrap.driver;
	usb_set_intfdata(iface, priv);
	iface->needs_binding = 0;

	usb_pm_lock(udev);
	iface->condition = USB_INTERFACE_BOUND;
	mark_active(iface);
	atomic_set(&iface->pm_usage_cnt, !driver->supports_autosuspend);
	usb_pm_unlock(udev);

	/* if interface was already added, bind now; else let
	 * the future device_add() bind it, bypassing probe()
	 */
	if (device_is_registered(dev))
		retval = device_bind_driver(dev);

	return retval;
}
EXPORT_SYMBOL_GPL(usb_driver_claim_interface);

/**
 * usb_driver_release_interface - unbind a driver from an interface
 * @driver: the driver to be unbound
 * @iface: the interface from which it will be unbound
 *
 * This can be used by drivers to release an interface without waiting
 * for their disconnect() methods to be called.  In typical cases this
 * also causes the driver disconnect() method to be called.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 * Callers must own the device lock, so driver disconnect() entries don't
 * need extra locking, but other call contexts may need to explicitly claim
 * that lock.
 */
void usb_driver_release_interface(struct usb_driver *driver,
					struct usb_interface *iface)
{
	struct device *dev = &iface->dev;

	/* this should never happen, don't release something that's not ours */
	if (!dev->driver || dev->driver != &driver->drvwrap.driver)
		return;

	/* don't release from within disconnect() */
	if (iface->condition != USB_INTERFACE_BOUND)
		return;
	iface->condition = USB_INTERFACE_UNBINDING;

	/* Release via the driver core only if the interface
	 * has already been registered
	 */
	if (device_is_registered(dev)) {
		device_release_driver(dev);
	} else {
		down(&dev->sem);
		usb_unbind_interface(dev);
		dev->driver = NULL;
		up(&dev->sem);
	}
}
EXPORT_SYMBOL_GPL(usb_driver_release_interface);

/* returns 0 if no match, 1 if match */
int usb_match_device(struct usb_device *dev, const struct usb_device_id *id)
{
	if ((id->match_flags & USB_DEVICE_ID_MATCH_VENDOR) &&
	    id->idVendor != le16_to_cpu(dev->descriptor.idVendor))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_PRODUCT) &&
	    id->idProduct != le16_to_cpu(dev->descriptor.idProduct))
		return 0;

	/* No need to test id->bcdDevice_lo != 0, since 0 is never
	   greater than any unsigned number. */
	if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_LO) &&
	    (id->bcdDevice_lo > le16_to_cpu(dev->descriptor.bcdDevice)))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_HI) &&
	    (id->bcdDevice_hi < le16_to_cpu(dev->descriptor.bcdDevice)))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_CLASS) &&
	    (id->bDeviceClass != dev->descriptor.bDeviceClass))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_SUBCLASS) &&
	    (id->bDeviceSubClass != dev->descriptor.bDeviceSubClass))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_PROTOCOL) &&
	    (id->bDeviceProtocol != dev->descriptor.bDeviceProtocol))
		return 0;

	return 1;
}

/* returns 0 if no match, 1 if match */
int usb_match_one_id(struct usb_interface *interface,
		     const struct usb_device_id *id)
{
	struct usb_host_interface *intf;
	struct usb_device *dev;

	/* proc_connectinfo in devio.c may call us with id == NULL. */
	if (id == NULL)
		return 0;

	intf = interface->cur_altsetting;
	dev = interface_to_usbdev(interface);

	if (!usb_match_device(dev, id))
		return 0;

	/* The interface class, subclass, and protocol should never be
	 * checked for a match if the device class is Vendor Specific,
	 * unless the match record specifies the Vendor ID. */
	if (dev->descriptor.bDeviceClass == USB_CLASS_VENDOR_SPEC &&
			!(id->match_flags & USB_DEVICE_ID_MATCH_VENDOR) &&
			(id->match_flags & (USB_DEVICE_ID_MATCH_INT_CLASS |
				USB_DEVICE_ID_MATCH_INT_SUBCLASS |
				USB_DEVICE_ID_MATCH_INT_PROTOCOL)))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_CLASS) &&
	    (id->bInterfaceClass != intf->desc.bInterfaceClass))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_SUBCLASS) &&
	    (id->bInterfaceSubClass != intf->desc.bInterfaceSubClass))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_PROTOCOL) &&
	    (id->bInterfaceProtocol != intf->desc.bInterfaceProtocol))
		return 0;

	return 1;
}
EXPORT_SYMBOL_GPL(usb_match_one_id);

/**
 * usb_match_id - find first usb_device_id matching device or interface
 * @interface: the interface of interest
 * @id: array of usb_device_id structures, terminated by zero entry
 *
 * usb_match_id searches an array of usb_device_id's and returns
 * the first one matching the device or interface, or null.
 * This is used when binding (or rebinding) a driver to an interface.
 * Most USB device drivers will use this indirectly, through the usb core,
 * but some layered driver frameworks use it directly.
 * These device tables are exported with MODULE_DEVICE_TABLE, through
 * modutils, to support the driver loading functionality of USB hotplugging.
 *
 * What Matches:
 *
 * The "match_flags" element in a usb_device_id controls which
 * members are used.  If the corresponding bit is set, the
 * value in the device_id must match its corresponding member
 * in the device or interface descriptor, or else the device_id
 * does not match.
 *
 * "driver_info" is normally used only by device drivers,
 * but you can create a wildcard "matches anything" usb_device_id
 * as a driver's "modules.usbmap" entry if you provide an id with
 * only a nonzero "driver_info" field.  If you do this, the USB device
 * driver's probe() routine should use additional intelligence to
 * decide whether to bind to the specified interface.
 *
 * What Makes Good usb_device_id Tables:
 *
 * The match algorithm is very simple, so that intelligence in
 * driver selection must come from smart driver id records.
 * Unless you have good reasons to use another selection policy,
 * provide match elements only in related groups, and order match
 * specifiers from specific to general.  Use the macros provided
 * for that purpose if you can.
 *
 * The most specific match specifiers use device descriptor
 * data.  These are commonly used with product-specific matches;
 * the USB_DEVICE macro lets you provide vendor and product IDs,
 * and you can also match against ranges of product revisions.
 * These are widely used for devices with application or vendor
 * specific bDeviceClass values.
 *
 * Matches based on device class/subclass/protocol specifications
 * are slightly more general; use the USB_DEVICE_INFO macro, or
 * its siblings.  These are used with single-function devices
 * where bDeviceClass doesn't specify that each interface has
 * its own class.
 *
 * Matches based on interface class/subclass/protocol are the
 * most general; they let drivers bind to any interface on a
 * multiple-function device.  Use the USB_INTERFACE_INFO
 * macro, or its siblings, to match class-per-interface style
 * devices (as recorded in bInterfaceClass).
 *
 * Note that an entry created by USB_INTERFACE_INFO won't match
 * any interface if the device class is set to Vendor-Specific.
 * This is deliberate; according to the USB spec the meanings of
 * the interface class/subclass/protocol for these devices are also
 * vendor-specific, and hence matching against a standard product
 * class wouldn't work anyway.  If you really want to use an
 * interface-based match for such a device, create a match record
 * that also specifies the vendor ID.  (Unforunately there isn't a
 * standard macro for creating records like this.)
 *
 * Within those groups, remember that not all combinations are
 * meaningful.  For example, don't give a product version range
 * without vendor and product IDs; or specify a protocol without
 * its associated class and subclass.
 */
const struct usb_device_id *usb_match_id(struct usb_interface *interface,
					 const struct usb_device_id *id)
{
	/* proc_connectinfo in devio.c may call us with id == NULL. */
	if (id == NULL)
		return NULL;

	/* It is important to check that id->driver_info is nonzero,
	   since an entry that is all zeroes except for a nonzero
	   id->driver_info is the way to create an entry that
	   indicates that the driver want to examine every
	   device and interface. */
	for (; id->idVendor || id->idProduct || id->bDeviceClass ||
	       id->bInterfaceClass || id->driver_info; id++) {
		if (usb_match_one_id(interface, id))
			return id;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(usb_match_id);

static int usb_device_match(struct device *dev, struct device_driver *drv)
{
	/* devices and interfaces are handled separately */
	if (is_usb_device(dev)) {

		/* interface drivers never match devices */
		if (!is_usb_device_driver(drv))
			return 0;

		/* TODO: Add real matching code */
		return 1;

	} else if (is_usb_interface(dev)) {
		struct usb_interface *intf;
		struct usb_driver *usb_drv;
		const struct usb_device_id *id;

		/* device drivers never match interfaces */
		if (is_usb_device_driver(drv))
			return 0;

		intf = to_usb_interface(dev);
		usb_drv = to_usb_driver(drv);

		id = usb_match_id(intf, usb_drv->id_table);
		if (id)
			return 1;

		id = usb_match_dynamic_id(intf, usb_drv);
		if (id)
			return 1;
	}

	return 0;
}

#ifdef	CONFIG_HOTPLUG
static int usb_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct usb_device *usb_dev;

	/* driver is often null here; dev_dbg() would oops */
	pr_debug("usb %s: uevent\n", dev_name(dev));

	if (is_usb_device(dev)) {
		usb_dev = to_usb_device(dev);
	} else if (is_usb_interface(dev)) {
		struct usb_interface *intf = to_usb_interface(dev);

		usb_dev = interface_to_usbdev(intf);
	} else {
		return 0;
	}

	if (usb_dev->devnum < 0) {
		pr_debug("usb %s: already deleted?\n", dev_name(dev));
		return -ENODEV;
	}
	if (!usb_dev->bus) {
		pr_debug("usb %s: bus removed?\n", dev_name(dev));
		return -ENODEV;
	}

#ifdef	CONFIG_USB_DEVICEFS
	/* If this is available, userspace programs can directly read
	 * all the device descriptors we don't tell them about.  Or
	 * act as usermode drivers.
	 */
	if (add_uevent_var(env, "DEVICE=/proc/bus/usb/%03d/%03d",
			   usb_dev->bus->busnum, usb_dev->devnum))
		return -ENOMEM;
#endif

	/* per-device configurations are common */
	if (add_uevent_var(env, "PRODUCT=%x/%x/%x",
			   le16_to_cpu(usb_dev->descriptor.idVendor),
			   le16_to_cpu(usb_dev->descriptor.idProduct),
			   le16_to_cpu(usb_dev->descriptor.bcdDevice)))
		return -ENOMEM;

	/* class-based driver binding models */
	if (add_uevent_var(env, "TYPE=%d/%d/%d",
			   usb_dev->descriptor.bDeviceClass,
			   usb_dev->descriptor.bDeviceSubClass,
			   usb_dev->descriptor.bDeviceProtocol))
		return -ENOMEM;

	return 0;
}

#else

static int usb_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	return -ENODEV;
}
#endif	/* CONFIG_HOTPLUG */

/**
 * usb_register_device_driver - register a USB device (not interface) driver
 * @new_udriver: USB operations for the device driver
 * @owner: module owner of this driver.
 *
 * Registers a USB device driver with the USB core.  The list of
 * unattached devices will be rescanned whenever a new driver is
 * added, allowing the new driver to attach to any recognized devices.
 * Returns a negative error code on failure and 0 on success.
 */
int usb_register_device_driver(struct usb_device_driver *new_udriver,
		struct module *owner)
{
	int retval = 0;

	if (usb_disabled())
		return -ENODEV;

	new_udriver->drvwrap.for_devices = 1;
	new_udriver->drvwrap.driver.name = (char *) new_udriver->name;
	new_udriver->drvwrap.driver.bus = &usb_bus_type;
	new_udriver->drvwrap.driver.probe = usb_probe_device;
	new_udriver->drvwrap.driver.remove = usb_unbind_device;
	new_udriver->drvwrap.driver.owner = owner;

	retval = driver_register(&new_udriver->drvwrap.driver);

	if (!retval) {
		pr_info("%s: registered new device driver %s\n",
			usbcore_name, new_udriver->name);
		usbfs_update_special();
	} else {
		printk(KERN_ERR "%s: error %d registering device "
			"	driver %s\n",
			usbcore_name, retval, new_udriver->name);
	}

	return retval;
}
EXPORT_SYMBOL_GPL(usb_register_device_driver);

/**
 * usb_deregister_device_driver - unregister a USB device (not interface) driver
 * @udriver: USB operations of the device driver to unregister
 * Context: must be able to sleep
 *
 * Unlinks the specified driver from the internal USB driver list.
 */
void usb_deregister_device_driver(struct usb_device_driver *udriver)
{
	pr_info("%s: deregistering device driver %s\n",
			usbcore_name, udriver->name);

	driver_unregister(&udriver->drvwrap.driver);
	usbfs_update_special();
}
EXPORT_SYMBOL_GPL(usb_deregister_device_driver);

/**
 * usb_register_driver - register a USB interface driver
 * @new_driver: USB operations for the interface driver
 * @owner: module owner of this driver.
 * @mod_name: module name string
 *
 * Registers a USB interface driver with the USB core.  The list of
 * unattached interfaces will be rescanned whenever a new driver is
 * added, allowing the new driver to attach to any recognized interfaces.
 * Returns a negative error code on failure and 0 on success.
 *
 * NOTE: if you want your driver to use the USB major number, you must call
 * usb_register_dev() to enable that functionality.  This function no longer
 * takes care of that.
 */
int usb_register_driver(struct usb_driver *new_driver, struct module *owner,
			const char *mod_name)
{
	int retval = 0;

	if (usb_disabled())
		return -ENODEV;

	new_driver->drvwrap.for_devices = 0;
	new_driver->drvwrap.driver.name = (char *) new_driver->name;
	new_driver->drvwrap.driver.bus = &usb_bus_type;
	new_driver->drvwrap.driver.probe = usb_probe_interface;
	new_driver->drvwrap.driver.remove = usb_unbind_interface;
	new_driver->drvwrap.driver.owner = owner;
	new_driver->drvwrap.driver.mod_name = mod_name;
	spin_lock_init(&new_driver->dynids.lock);
	INIT_LIST_HEAD(&new_driver->dynids.list);

	retval = driver_register(&new_driver->drvwrap.driver);
	if (retval)
		goto out;

	usbfs_update_special();

	retval = usb_create_newid_file(new_driver);
	if (retval)
		goto out_newid;

	retval = usb_create_removeid_file(new_driver);
	if (retval)
		goto out_removeid;

	pr_info("%s: registered new interface driver %s\n",
			usbcore_name, new_driver->name);

out:
	return retval;

out_removeid:
	usb_remove_newid_file(new_driver);
out_newid:
	driver_unregister(&new_driver->drvwrap.driver);

	printk(KERN_ERR "%s: error %d registering interface "
			"	driver %s\n",
			usbcore_name, retval, new_driver->name);
	goto out;
}
EXPORT_SYMBOL_GPL(usb_register_driver);

/**
 * usb_deregister - unregister a USB interface driver
 * @driver: USB operations of the interface driver to unregister
 * Context: must be able to sleep
 *
 * Unlinks the specified driver from the internal USB driver list.
 *
 * NOTE: If you called usb_register_dev(), you still need to call
 * usb_deregister_dev() to clean up your driver's allocated minor numbers,
 * this * call will no longer do it for you.
 */
void usb_deregister(struct usb_driver *driver)
{
	pr_info("%s: deregistering interface driver %s\n",
			usbcore_name, driver->name);

	usb_remove_removeid_file(driver);
	usb_remove_newid_file(driver);
	usb_free_dynids(driver);
	driver_unregister(&driver->drvwrap.driver);

	usbfs_update_special();
}
EXPORT_SYMBOL_GPL(usb_deregister);

/* Forced unbinding of a USB interface driver, either because
 * it doesn't support pre_reset/post_reset/reset_resume or
 * because it doesn't support suspend/resume.
 *
 * The caller must hold @intf's device's lock, but not its pm_mutex
 * and not @intf->dev.sem.
 */
void usb_forced_unbind_intf(struct usb_interface *intf)
{
	struct usb_driver *driver = to_usb_driver(intf->dev.driver);

	dev_dbg(&intf->dev, "forced unbind\n");
	usb_driver_release_interface(driver, intf);

	/* Mark the interface for later rebinding */
	intf->needs_binding = 1;
}

/* Delayed forced unbinding of a USB interface driver and scan
 * for rebinding.
 *
 * The caller must hold @intf's device's lock, but not its pm_mutex
 * and not @intf->dev.sem.
 *
 * Note: Rebinds will be skipped if a system sleep transition is in
 * progress and the PM "complete" callback hasn't occurred yet.
 */
void usb_rebind_intf(struct usb_interface *intf)
{
	int rc;

	/* Delayed unbind of an existing driver */
	if (intf->dev.driver) {
		struct usb_driver *driver =
				to_usb_driver(intf->dev.driver);

		dev_dbg(&intf->dev, "forced unbind\n");
		usb_driver_release_interface(driver, intf);
	}

	/* Try to rebind the interface */
	if (intf->dev.power.status == DPM_ON) {
		intf->needs_binding = 0;
		rc = device_attach(&intf->dev);
		if (rc < 0)
			dev_warn(&intf->dev, "rebind failed: %d\n", rc);
	}
}

#ifdef CONFIG_PM

#define DO_UNBIND	0
#define DO_REBIND	1

/* Unbind drivers for @udev's interfaces that don't support suspend/resume,
 * or rebind interfaces that have been unbound, according to @action.
 *
 * The caller must hold @udev's device lock.
 */
static void do_unbind_rebind(struct usb_device *udev, int action)
{
	struct usb_host_config	*config;
	int			i;
	struct usb_interface	*intf;
	struct usb_driver	*drv;

	config = udev->actconfig;
	if (config) {
		for (i = 0; i < config->desc.bNumInterfaces; ++i) {
			intf = config->interface[i];
			switch (action) {
			case DO_UNBIND:
				if (intf->dev.driver) {
					drv = to_usb_driver(intf->dev.driver);
					if (!drv->suspend || !drv->resume)
						usb_forced_unbind_intf(intf);
				}
				break;
			case DO_REBIND:
				if (intf->needs_binding)
					usb_rebind_intf(intf);
				break;
			}
		}
	}
}

/* Caller has locked udev's pm_mutex */
static int usb_suspend_device(struct usb_device *udev, pm_message_t msg)
{
	struct usb_device_driver	*udriver;
	int				status = 0;

	if (udev->state == USB_STATE_NOTATTACHED ||
			udev->state == USB_STATE_SUSPENDED)
		goto done;

	/* For devices that don't have a driver, we do a generic suspend. */
	if (udev->dev.driver)
		udriver = to_usb_device_driver(udev->dev.driver);
	else {
		udev->do_remote_wakeup = 0;
		udriver = &usb_generic_driver;
	}
	status = udriver->suspend(udev, msg);

 done:
	dev_vdbg(&udev->dev, "%s: status %d\n", __func__, status);
	return status;
}

/* Caller has locked udev's pm_mutex */
static int usb_resume_device(struct usb_device *udev, pm_message_t msg)
{
	struct usb_device_driver	*udriver;
	int				status = 0;

	if (udev->state == USB_STATE_NOTATTACHED)
		goto done;

	/* Can't resume it if it doesn't have a driver. */
	if (udev->dev.driver == NULL) {
		status = -ENOTCONN;
		goto done;
	}

	/* Non-root devices on a full/low-speed bus must wait for their
	 * companion high-speed root hub, in case a handoff is needed.
	 */
	if (!(msg.event & PM_EVENT_AUTO) && udev->parent &&
			udev->bus->hs_companion)
		device_pm_wait_for_dev(&udev->dev,
				&udev->bus->hs_companion->root_hub->dev);

	if (udev->quirks & USB_QUIRK_RESET_RESUME)
		udev->reset_resume = 1;

	udriver = to_usb_device_driver(udev->dev.driver);
	status = udriver->resume(udev, msg);

 done:
	dev_vdbg(&udev->dev, "%s: status %d\n", __func__, status);
	return status;
}

/* Caller has locked intf's usb_device's pm mutex */
static int usb_suspend_interface(struct usb_device *udev,
		struct usb_interface *intf, pm_message_t msg)
{
	struct usb_driver	*driver;
	int			status = 0;

	/* with no hardware, USB interfaces only use FREEZE and ON states */
	if (udev->state == USB_STATE_NOTATTACHED || !is_active(intf))
		goto done;

	/* This can happen; see usb_driver_release_interface() */
	if (intf->condition == USB_INTERFACE_UNBOUND)
		goto done;
	driver = to_usb_driver(intf->dev.driver);

	if (driver->suspend) {
		status = driver->suspend(intf, msg);
		if (status == 0)
			mark_quiesced(intf);
		else if (!(msg.event & PM_EVENT_AUTO))
			dev_err(&intf->dev, "%s error %d\n",
					"suspend", status);
	} else {
		/* Later we will unbind the driver and reprobe */
		intf->needs_binding = 1;
		dev_warn(&intf->dev, "no %s for driver %s?\n",
				"suspend", driver->name);
		mark_quiesced(intf);
	}

 done:
	dev_vdbg(&intf->dev, "%s: status %d\n", __func__, status);
	return status;
}

/* Caller has locked intf's usb_device's pm_mutex */
static int usb_resume_interface(struct usb_device *udev,
		struct usb_interface *intf, pm_message_t msg, int reset_resume)
{
	struct usb_driver	*driver;
	int			status = 0;

	if (udev->state == USB_STATE_NOTATTACHED || is_active(intf))
		goto done;

	/* Don't let autoresume interfere with unbinding */
	if (intf->condition == USB_INTERFACE_UNBINDING)
		goto done;

	/* Can't resume it if it doesn't have a driver. */
	if (intf->condition == USB_INTERFACE_UNBOUND) {

		/* Carry out a deferred switch to altsetting 0 */
		if (intf->needs_altsetting0 &&
				intf->dev.power.status == DPM_ON) {
			usb_set_interface(udev, intf->altsetting[0].
					desc.bInterfaceNumber, 0);
			intf->needs_altsetting0 = 0;
		}
		goto done;
	}

	/* Don't resume if the interface is marked for rebinding */
	if (intf->needs_binding)
		goto done;
	driver = to_usb_driver(intf->dev.driver);

	if (reset_resume) {
		if (driver->reset_resume) {
			status = driver->reset_resume(intf);
			if (status)
				dev_err(&intf->dev, "%s error %d\n",
						"reset_resume", status);
		} else {
			intf->needs_binding = 1;
			dev_warn(&intf->dev, "no %s for driver %s?\n",
					"reset_resume", driver->name);
		}
	} else {
		if (driver->resume) {
			status = driver->resume(intf);
			if (status)
				dev_err(&intf->dev, "%s error %d\n",
						"resume", status);
		} else {
			intf->needs_binding = 1;
			dev_warn(&intf->dev, "no %s for driver %s?\n",
					"resume", driver->name);
		}
	}

done:
	dev_vdbg(&intf->dev, "%s: status %d\n", __func__, status);
	if (status == 0 && intf->condition == USB_INTERFACE_BOUND)
		mark_active(intf);

	/* Later we will unbind the driver and/or reprobe, if necessary */
	return status;
}

#ifdef	CONFIG_USB_SUSPEND

/* Internal routine to check whether we may autosuspend a device. */
static int autosuspend_check(struct usb_device *udev, int reschedule)
{
	int			i;
	struct usb_interface	*intf;
	unsigned long		suspend_time, j;

	/* For autosuspend, fail fast if anything is in use or autosuspend
	 * is disabled.  Also fail if any interfaces require remote wakeup
	 * but it isn't available.
	 */
	if (udev->pm_usage_cnt > 0)
		return -EBUSY;
	if (udev->autosuspend_delay < 0 || udev->autosuspend_disabled)
		return -EPERM;

	suspend_time = udev->last_busy + udev->autosuspend_delay;
	if (udev->actconfig) {
		for (i = 0; i < udev->actconfig->desc.bNumInterfaces; i++) {
			intf = udev->actconfig->interface[i];
			if (!is_active(intf))
				continue;
			if (atomic_read(&intf->pm_usage_cnt) > 0)
				return -EBUSY;
			if (intf->needs_remote_wakeup &&
					!udev->do_remote_wakeup) {
				dev_dbg(&udev->dev, "remote wakeup needed "
						"for autosuspend\n");
				return -EOPNOTSUPP;
			}

			/* Don't allow autosuspend if the device will need
			 * a reset-resume and any of its interface drivers
			 * doesn't include support.
			 */
			if (udev->quirks & USB_QUIRK_RESET_RESUME) {
				struct usb_driver *driver;

				driver = to_usb_driver(intf->dev.driver);
				if (!driver->reset_resume ||
				    intf->needs_remote_wakeup)
					return -EOPNOTSUPP;
			}
		}
	}

	/* If everything is okay but the device hasn't been idle for long
	 * enough, queue a delayed autosuspend request.  If the device
	 * _has_ been idle for long enough and the reschedule flag is set,
	 * likewise queue a delayed (1 second) autosuspend request.
	 */
	j = jiffies;
	if (time_before(j, suspend_time))
		reschedule = 1;
	else
		suspend_time = j + HZ;
	if (reschedule) {
		if (!timer_pending(&udev->autosuspend.timer)) {
			queue_delayed_work(ksuspend_usb_wq, &udev->autosuspend,
				round_jiffies_up_relative(suspend_time - j));
		}
		return -EAGAIN;
	}
	return 0;
}

#else

static inline int autosuspend_check(struct usb_device *udev, int reschedule)
{
	return 0;
}

#endif	/* CONFIG_USB_SUSPEND */

/**
 * usb_suspend_both - suspend a USB device and its interfaces
 * @udev: the usb_device to suspend
 * @msg: Power Management message describing this state transition
 *
 * This is the central routine for suspending USB devices.  It calls the
 * suspend methods for all the interface drivers in @udev and then calls
 * the suspend method for @udev itself.  If an error occurs at any stage,
 * all the interfaces which were suspended are resumed so that they remain
 * in the same state as the device.
 *
 * If an autosuspend is in progress the routine checks first to make sure
 * that neither the device itself or any of its active interfaces is in use
 * (pm_usage_cnt is greater than 0).  If they are, the autosuspend fails.
 *
 * If the suspend succeeds, the routine recursively queues an autosuspend
 * request for @udev's parent device, thereby propagating the change up
 * the device tree.  If all of the parent's children are now suspended,
 * the parent will autosuspend in turn.
 *
 * The suspend method calls are subject to mutual exclusion under control
 * of @udev's pm_mutex.  Many of these calls are also under the protection
 * of @udev's device lock (including all requests originating outside the
 * USB subsystem), but autosuspend requests generated by a child device or
 * interface driver may not be.  Usbcore will insure that the method calls
 * do not arrive during bind, unbind, or reset operations.  However, drivers
 * must be prepared to handle suspend calls arriving at unpredictable times.
 * The only way to block such calls is to do an autoresume (preventing
 * autosuspends) while holding @udev's device lock (preventing outside
 * suspends).
 *
 * The caller must hold @udev->pm_mutex.
 *
 * This routine can run only in process context.
 */
static int usb_suspend_both(struct usb_device *udev, pm_message_t msg)
{
	int			status = 0;
	int			i = 0;
	struct usb_interface	*intf;
	struct usb_device	*parent = udev->parent;

	if (udev->state == USB_STATE_NOTATTACHED ||
			udev->state == USB_STATE_SUSPENDED)
		goto done;

	udev->do_remote_wakeup = device_may_wakeup(&udev->dev);

	if (msg.event & PM_EVENT_AUTO) {
		status = autosuspend_check(udev, 0);
		if (status < 0)
			goto done;
	}

	/* Suspend all the interfaces and then udev itself */
	if (udev->actconfig) {
		for (; i < udev->actconfig->desc.bNumInterfaces; i++) {
			intf = udev->actconfig->interface[i];
			status = usb_suspend_interface(udev, intf, msg);
			if (status != 0)
				break;
		}
	}
	if (status == 0)
		status = usb_suspend_device(udev, msg);

	/* If the suspend failed, resume interfaces that did get suspended */
	if (status != 0) {
		pm_message_t msg2;

		msg2.event = msg.event ^ (PM_EVENT_SUSPEND | PM_EVENT_RESUME);
		while (--i >= 0) {
			intf = udev->actconfig->interface[i];
			usb_resume_interface(udev, intf, msg2, 0);
		}

		/* Try another autosuspend when the interfaces aren't busy */
		if (msg.event & PM_EVENT_AUTO)
			autosuspend_check(udev, status == -EBUSY);

	/* If the suspend succeeded then prevent any more URB submissions,
	 * flush any outstanding URBs, and propagate the suspend up the tree.
	 */
	} else {
		cancel_delayed_work(&udev->autosuspend);
		udev->can_submit = 0;
		for (i = 0; i < 16; ++i) {
			usb_hcd_flush_endpoint(udev, udev->ep_out[i]);
			usb_hcd_flush_endpoint(udev, udev->ep_in[i]);
		}

		/* If this is just a FREEZE or a PRETHAW, udev might
		 * not really be suspended.  Only true suspends get
		 * propagated up the device tree.
		 */
		if (parent && udev->state == USB_STATE_SUSPENDED)
			usb_autosuspend_device(parent);
	}

 done:
	dev_vdbg(&udev->dev, "%s: status %d\n", __func__, status);
	return status;
}

/**
 * usb_resume_both - resume a USB device and its interfaces
 * @udev: the usb_device to resume
 * @msg: Power Management message describing this state transition
 *
 * This is the central routine for resuming USB devices.  It calls the
 * the resume method for @udev and then calls the resume methods for all
 * the interface drivers in @udev.
 *
 * Before starting the resume, the routine calls itself recursively for
 * the parent device of @udev, thereby propagating the change up the device
 * tree and assuring that @udev will be able to resume.  If the parent is
 * unable to resume successfully, the routine fails.
 *
 * The resume method calls are subject to mutual exclusion under control
 * of @udev's pm_mutex.  Many of these calls are also under the protection
 * of @udev's device lock (including all requests originating outside the
 * USB subsystem), but autoresume requests generated by a child device or
 * interface driver may not be.  Usbcore will insure that the method calls
 * do not arrive during bind, unbind, or reset operations.  However, drivers
 * must be prepared to handle resume calls arriving at unpredictable times.
 * The only way to block such calls is to do an autoresume (preventing
 * other autoresumes) while holding @udev's device lock (preventing outside
 * resumes).
 *
 * The caller must hold @udev->pm_mutex.
 *
 * This routine can run only in process context.
 */
static int usb_resume_both(struct usb_device *udev, pm_message_t msg)
{
	int			status = 0;
	int			i;
	struct usb_interface	*intf;
	struct usb_device	*parent = udev->parent;

	cancel_delayed_work(&udev->autosuspend);
	if (udev->state == USB_STATE_NOTATTACHED) {
		status = -ENODEV;
		goto done;
	}
	udev->can_submit = 1;

	/* Propagate the resume up the tree, if necessary */
	if (udev->state == USB_STATE_SUSPENDED) {
		if (parent) {
			status = usb_autoresume_device(parent);
			if (status == 0) {
				status = usb_resume_device(udev, msg);
				if (status || udev->state ==
						USB_STATE_NOTATTACHED) {
					usb_autosuspend_device(parent);

					/* It's possible usb_resume_device()
					 * failed after the port was
					 * unsuspended, causing udev to be
					 * logically disconnected.  We don't
					 * want usb_disconnect() to autosuspend
					 * the parent again, so tell it that
					 * udev disconnected while still
					 * suspended. */
					if (udev->state ==
							USB_STATE_NOTATTACHED)
						udev->discon_suspended = 1;
				}
			}
		} else {

			/* We can't progagate beyond the USB subsystem,
			 * so if a root hub's controller is suspended
			 * then we're stuck. */
			status = usb_resume_device(udev, msg);
		}
	} else if (udev->reset_resume)
		status = usb_resume_device(udev, msg);

	if (status == 0 && udev->actconfig) {
		for (i = 0; i < udev->actconfig->desc.bNumInterfaces; i++) {
			intf = udev->actconfig->interface[i];
			usb_resume_interface(udev, intf, msg,
					udev->reset_resume);
		}
	}

 done:
	dev_vdbg(&udev->dev, "%s: status %d\n", __func__, status);
	if (!status)
		udev->reset_resume = 0;
	return status;
}

#ifdef CONFIG_USB_SUSPEND

/* Internal routine to adjust a device's usage counter and change
 * its autosuspend state.
 */
static int usb_autopm_do_device(struct usb_device *udev, int inc_usage_cnt)
{
	int	status = 0;

	usb_pm_lock(udev);
	udev->pm_usage_cnt += inc_usage_cnt;
	WARN_ON(udev->pm_usage_cnt < 0);
	if (inc_usage_cnt)
		udev->last_busy = jiffies;
	if (inc_usage_cnt >= 0 && udev->pm_usage_cnt > 0) {
		if (udev->state == USB_STATE_SUSPENDED)
			status = usb_resume_both(udev, PMSG_AUTO_RESUME);
		if (status != 0)
			udev->pm_usage_cnt -= inc_usage_cnt;
		else if (inc_usage_cnt)
			udev->last_busy = jiffies;
	} else if (inc_usage_cnt <= 0 && udev->pm_usage_cnt <= 0) {
		status = usb_suspend_both(udev, PMSG_AUTO_SUSPEND);
	}
	usb_pm_unlock(udev);
	return status;
}

/* usb_autosuspend_work - callback routine to autosuspend a USB device */
void usb_autosuspend_work(struct work_struct *work)
{
	struct usb_device *udev =
		container_of(work, struct usb_device, autosuspend.work);

	usb_autopm_do_device(udev, 0);
}

/* usb_autoresume_work - callback routine to autoresume a USB device */
void usb_autoresume_work(struct work_struct *work)
{
	struct usb_device *udev =
		container_of(work, struct usb_device, autoresume);

	/* Wake it up, let the drivers do their thing, and then put it
	 * back to sleep.
	 */
	if (usb_autopm_do_device(udev, 1) == 0)
		usb_autopm_do_device(udev, -1);
}

/**
 * usb_autosuspend_device - delayed autosuspend of a USB device and its interfaces
 * @udev: the usb_device to autosuspend
 *
 * This routine should be called when a core subsystem is finished using
 * @udev and wants to allow it to autosuspend.  Examples would be when
 * @udev's device file in usbfs is closed or after a configuration change.
 *
 * @udev's usage counter is decremented.  If it or any of the usage counters
 * for an active interface is greater than 0, no autosuspend request will be
 * queued.  (If an interface driver does not support autosuspend then its
 * usage counter is permanently positive.)  Furthermore, if an interface
 * driver requires remote-wakeup capability during autosuspend but remote
 * wakeup is disabled, the autosuspend will fail.
 *
 * The caller must hold @udev's device lock.
 *
 * This routine can run only in process context.
 */
void usb_autosuspend_device(struct usb_device *udev)
{
	int	status;

	status = usb_autopm_do_device(udev, -1);
	dev_vdbg(&udev->dev, "%s: cnt %d\n",
			__func__, udev->pm_usage_cnt);
}

/**
 * usb_try_autosuspend_device - attempt an autosuspend of a USB device and its interfaces
 * @udev: the usb_device to autosuspend
 *
 * This routine should be called when a core subsystem thinks @udev may
 * be ready to autosuspend.
 *
 * @udev's usage counter left unchanged.  If it or any of the usage counters
 * for an active interface is greater than 0, or autosuspend is not allowed
 * for any other reason, no autosuspend request will be queued.
 *
 * The caller must hold @udev's device lock.
 *
 * This routine can run only in process context.
 */
void usb_try_autosuspend_device(struct usb_device *udev)
{
	usb_autopm_do_device(udev, 0);
	dev_vdbg(&udev->dev, "%s: cnt %d\n",
			__func__, udev->pm_usage_cnt);
}

/**
 * usb_autoresume_device - immediately autoresume a USB device and its interfaces
 * @udev: the usb_device to autoresume
 *
 * This routine should be called when a core subsystem wants to use @udev
 * and needs to guarantee that it is not suspended.  No autosuspend will
 * occur until usb_autosuspend_device is called.  (Note that this will not
 * prevent suspend events originating in the PM core.)  Examples would be
 * when @udev's device file in usbfs is opened or when a remote-wakeup
 * request is received.
 *
 * @udev's usage counter is incremented to prevent subsequent autosuspends.
 * However if the autoresume fails then the usage counter is re-decremented.
 *
 * The caller must hold @udev's device lock.
 *
 * This routine can run only in process context.
 */
int usb_autoresume_device(struct usb_device *udev)
{
	int	status;

	status = usb_autopm_do_device(udev, 1);
	dev_vdbg(&udev->dev, "%s: status %d cnt %d\n",
			__func__, status, udev->pm_usage_cnt);
	return status;
}

/* Internal routine to adjust an interface's usage counter and change
 * its device's autosuspend state.
 */
static int usb_autopm_do_interface(struct usb_interface *intf,
		int inc_usage_cnt)
{
	struct usb_device	*udev = interface_to_usbdev(intf);
	int			status = 0;

	usb_pm_lock(udev);
	if (intf->condition == USB_INTERFACE_UNBOUND)
		status = -ENODEV;
	else {
		atomic_add(inc_usage_cnt, &intf->pm_usage_cnt);
		udev->last_busy = jiffies;
		if (inc_usage_cnt >= 0 &&
				atomic_read(&intf->pm_usage_cnt) > 0) {
			if (udev->state == USB_STATE_SUSPENDED)
				status = usb_resume_both(udev,
						PMSG_AUTO_RESUME);
			if (status != 0)
				atomic_sub(inc_usage_cnt, &intf->pm_usage_cnt);
			else
				udev->last_busy = jiffies;
		} else if (inc_usage_cnt <= 0 &&
				atomic_read(&intf->pm_usage_cnt) <= 0) {
			status = usb_suspend_both(udev, PMSG_AUTO_SUSPEND);
		}
	}
	usb_pm_unlock(udev);
	return status;
}

/**
 * usb_autopm_put_interface - decrement a USB interface's PM-usage counter
 * @intf: the usb_interface whose counter should be decremented
 *
 * This routine should be called by an interface driver when it is
 * finished using @intf and wants to allow it to autosuspend.  A typical
 * example would be a character-device driver when its device file is
 * closed.
 *
 * The routine decrements @intf's usage counter.  When the counter reaches
 * 0, a delayed autosuspend request for @intf's device is queued.  When
 * the delay expires, if @intf->pm_usage_cnt is still <= 0 along with all
 * the other usage counters for the sibling interfaces and @intf's
 * usb_device, the device and all its interfaces will be autosuspended.
 *
 * Note that @intf->pm_usage_cnt is owned by the interface driver.  The
 * core will not change its value other than the increment and decrement
 * in usb_autopm_get_interface and usb_autopm_put_interface.  The driver
 * may use this simple counter-oriented discipline or may set the value
 * any way it likes.
 *
 * If the driver has set @intf->needs_remote_wakeup then autosuspend will
 * take place only if the device's remote-wakeup facility is enabled.
 *
 * Suspend method calls queued by this routine can arrive at any time
 * while @intf is resumed and its usage counter is equal to 0.  They are
 * not protected by the usb_device's lock but only by its pm_mutex.
 * Drivers must provide their own synchronization.
 *
 * This routine can run only in process context.
 */
void usb_autopm_put_interface(struct usb_interface *intf)
{
	int	status;

	status = usb_autopm_do_interface(intf, -1);
	dev_vdbg(&intf->dev, "%s: status %d cnt %d\n",
			__func__, status, atomic_read(&intf->pm_usage_cnt));
}
EXPORT_SYMBOL_GPL(usb_autopm_put_interface);

/**
 * usb_autopm_put_interface_async - decrement a USB interface's PM-usage counter
 * @intf: the usb_interface whose counter should be decremented
 *
 * This routine does essentially the same thing as
 * usb_autopm_put_interface(): it decrements @intf's usage counter and
 * queues a delayed autosuspend request if the counter is <= 0.  The
 * difference is that it does not acquire the device's pm_mutex;
 * callers must handle all synchronization issues themselves.
 *
 * Typically a driver would call this routine during an URB's completion
 * handler, if no more URBs were pending.
 *
 * This routine can run in atomic context.
 */
void usb_autopm_put_interface_async(struct usb_interface *intf)
{
	struct usb_device	*udev = interface_to_usbdev(intf);
	int			status = 0;

	if (intf->condition == USB_INTERFACE_UNBOUND) {
		status = -ENODEV;
	} else {
		udev->last_busy = jiffies;
		atomic_dec(&intf->pm_usage_cnt);
		if (udev->autosuspend_disabled || udev->autosuspend_delay < 0)
			status = -EPERM;
		else if (atomic_read(&intf->pm_usage_cnt) <= 0 &&
				!timer_pending(&udev->autosuspend.timer)) {
			queue_delayed_work(ksuspend_usb_wq, &udev->autosuspend,
					round_jiffies_up_relative(
						udev->autosuspend_delay));
		}
	}
	dev_vdbg(&intf->dev, "%s: status %d cnt %d\n",
			__func__, status, atomic_read(&intf->pm_usage_cnt));
}
EXPORT_SYMBOL_GPL(usb_autopm_put_interface_async);

/**
 * usb_autopm_get_interface - increment a USB interface's PM-usage counter
 * @intf: the usb_interface whose counter should be incremented
 *
 * This routine should be called by an interface driver when it wants to
 * use @intf and needs to guarantee that it is not suspended.  In addition,
 * the routine prevents @intf from being autosuspended subsequently.  (Note
 * that this will not prevent suspend events originating in the PM core.)
 * This prevention will persist until usb_autopm_put_interface() is called
 * or @intf is unbound.  A typical example would be a character-device
 * driver when its device file is opened.
 *
 *
 * The routine increments @intf's usage counter.  (However if the
 * autoresume fails then the counter is re-decremented.)  So long as the
 * counter is greater than 0, autosuspend will not be allowed for @intf
 * or its usb_device.  When the driver is finished using @intf it should
 * call usb_autopm_put_interface() to decrement the usage counter and
 * queue a delayed autosuspend request (if the counter is <= 0).
 *
 *
 * Note that @intf->pm_usage_cnt is owned by the interface driver.  The
 * core will not change its value other than the increment and decrement
 * in usb_autopm_get_interface and usb_autopm_put_interface.  The driver
 * may use this simple counter-oriented discipline or may set the value
 * any way it likes.
 *
 * Resume method calls generated by this routine can arrive at any time
 * while @intf is suspended.  They are not protected by the usb_device's
 * lock but only by its pm_mutex.  Drivers must provide their own
 * synchronization.
 *
 * This routine can run only in process context.
 */
int usb_autopm_get_interface(struct usb_interface *intf)
{
	int	status;

	status = usb_autopm_do_interface(intf, 1);
	dev_vdbg(&intf->dev, "%s: status %d cnt %d\n",
			__func__, status, atomic_read(&intf->pm_usage_cnt));
	return status;
}
EXPORT_SYMBOL_GPL(usb_autopm_get_interface);

/**
 * usb_autopm_get_interface_async - increment a USB interface's PM-usage counter
 * @intf: the usb_interface whose counter should be incremented
 *
 * This routine does much the same thing as
 * usb_autopm_get_interface(): it increments @intf's usage counter and
 * queues an autoresume request if the result is > 0.  The differences
 * are that it does not acquire the device's pm_mutex (callers must
 * handle all synchronization issues themselves), and it does not
 * autoresume the device directly (it only queues a request).  After a
 * successful call, the device will generally not yet be resumed.
 *
 * This routine can run in atomic context.
 */
int usb_autopm_get_interface_async(struct usb_interface *intf)
{
	struct usb_device	*udev = interface_to_usbdev(intf);
	int			status = 0;

	if (intf->condition == USB_INTERFACE_UNBOUND)
		status = -ENODEV;
	else {
		atomic_inc(&intf->pm_usage_cnt);
		if (atomic_read(&intf->pm_usage_cnt) > 0 &&
				udev->state == USB_STATE_SUSPENDED)
			queue_work(ksuspend_usb_wq, &udev->autoresume);
	}
	dev_vdbg(&intf->dev, "%s: status %d cnt %d\n",
			__func__, status, atomic_read(&intf->pm_usage_cnt));
	return status;
}
EXPORT_SYMBOL_GPL(usb_autopm_get_interface_async);

#else

void usb_autosuspend_work(struct work_struct *work)
{}

void usb_autoresume_work(struct work_struct *work)
{}

#endif /* CONFIG_USB_SUSPEND */

/**
 * usb_external_suspend_device - external suspend of a USB device and its interfaces
 * @udev: the usb_device to suspend
 * @msg: Power Management message describing this state transition
 *
 * This routine handles external suspend requests: ones not generated
 * internally by a USB driver (autosuspend) but rather coming from the user
 * (via sysfs) or the PM core (system sleep).  The suspend will be carried
 * out regardless of @udev's usage counter or those of its interfaces,
 * and regardless of whether or not remote wakeup is enabled.  Of course,
 * interface drivers still have the option of failing the suspend (if
 * there are unsuspended children, for example).
 *
 * The caller must hold @udev's device lock.
 */
int usb_external_suspend_device(struct usb_device *udev, pm_message_t msg)
{
	int	status;

	do_unbind_rebind(udev, DO_UNBIND);
	usb_pm_lock(udev);
	status = usb_suspend_both(udev, msg);
	usb_pm_unlock(udev);
	return status;
}

/**
 * usb_external_resume_device - external resume of a USB device and its interfaces
 * @udev: the usb_device to resume
 * @msg: Power Management message describing this state transition
 *
 * This routine handles external resume requests: ones not generated
 * internally by a USB driver (autoresume) but rather coming from the user
 * (via sysfs), the PM core (system resume), or the device itself (remote
 * wakeup).  @udev's usage counter is unaffected.
 *
 * The caller must hold @udev's device lock.
 */
int usb_external_resume_device(struct usb_device *udev, pm_message_t msg)
{
	int	status;

	usb_pm_lock(udev);
	status = usb_resume_both(udev, msg);
	udev->last_busy = jiffies;
	usb_pm_unlock(udev);
	if (status == 0)
		do_unbind_rebind(udev, DO_REBIND);

	/* Now that the device is awake, we can start trying to autosuspend
	 * it again. */
	if (status == 0)
		usb_try_autosuspend_device(udev);
	return status;
}

int usb_suspend(struct device *dev, pm_message_t msg)
{
	struct usb_device	*udev;

	udev = to_usb_device(dev);

	/* If udev is already suspended, we can skip this suspend and
	 * we should also skip the upcoming system resume.  High-speed
	 * root hubs are an exception; they need to resume whenever the
	 * system wakes up in order for USB-PERSIST port handover to work
	 * properly.
	 */
	if (udev->state == USB_STATE_SUSPENDED) {
		if (udev->parent || udev->speed != USB_SPEED_HIGH)
			udev->skip_sys_resume = 1;
		return 0;
	}

	udev->skip_sys_resume = 0;
	return usb_external_suspend_device(udev, msg);
}

int usb_resume(struct device *dev, pm_message_t msg)
{
	struct usb_device	*udev;
	int			status;

	udev = to_usb_device(dev);

	/* If udev->skip_sys_resume is set then udev was already suspended
	 * when the system sleep started, so we don't want to resume it
	 * during this system wakeup.
	 */
	if (udev->skip_sys_resume)
		return 0;
	status = usb_external_resume_device(udev, msg);

	/* Avoid PM error messages for devices disconnected while suspended
	 * as we'll display regular disconnect messages just a bit later.
	 */
	if (status == -ENODEV)
		return 0;
	return status;
}

#endif /* CONFIG_PM */

struct bus_type usb_bus_type = {
	.name =		"usb",
	.match =	usb_device_match,
	.uevent =	usb_uevent,
};
