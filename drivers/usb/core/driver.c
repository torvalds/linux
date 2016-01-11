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
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/usb.h>
#include <linux/usb/quirks.h>
#include <linux/usb/hcd.h>

#include "usb.h"


/*
 * Adds a new dynamic USBdevice ID to this driver,
 * and cause the driver to probe for all devices again.
 */
ssize_t usb_store_new_id(struct usb_dynids *dynids,
			 const struct usb_device_id *id_table,
			 struct device_driver *driver,
			 const char *buf, size_t count)
{
	struct usb_dynid *dynid;
	u32 idVendor = 0;
	u32 idProduct = 0;
	unsigned int bInterfaceClass = 0;
	u32 refVendor, refProduct;
	int fields = 0;
	int retval = 0;

	fields = sscanf(buf, "%x %x %x %x %x", &idVendor, &idProduct,
			&bInterfaceClass, &refVendor, &refProduct);
	if (fields < 2)
		return -EINVAL;

	dynid = kzalloc(sizeof(*dynid), GFP_KERNEL);
	if (!dynid)
		return -ENOMEM;

	INIT_LIST_HEAD(&dynid->node);
	dynid->id.idVendor = idVendor;
	dynid->id.idProduct = idProduct;
	dynid->id.match_flags = USB_DEVICE_ID_MATCH_DEVICE;
	if (fields > 2 && bInterfaceClass) {
		if (bInterfaceClass > 255) {
			retval = -EINVAL;
			goto fail;
		}

		dynid->id.bInterfaceClass = (u8)bInterfaceClass;
		dynid->id.match_flags |= USB_DEVICE_ID_MATCH_INT_CLASS;
	}

	if (fields > 4) {
		const struct usb_device_id *id = id_table;

		if (!id) {
			retval = -ENODEV;
			goto fail;
		}

		for (; id->match_flags; id++)
			if (id->idVendor == refVendor && id->idProduct == refProduct)
				break;

		if (id->match_flags) {
			dynid->id.driver_info = id->driver_info;
		} else {
			retval = -ENODEV;
			goto fail;
		}
	}

	spin_lock(&dynids->lock);
	list_add_tail(&dynid->node, &dynids->list);
	spin_unlock(&dynids->lock);

	retval = driver_attach(driver);

	if (retval)
		return retval;
	return count;

fail:
	kfree(dynid);
	return retval;
}
EXPORT_SYMBOL_GPL(usb_store_new_id);

ssize_t usb_show_dynids(struct usb_dynids *dynids, char *buf)
{
	struct usb_dynid *dynid;
	size_t count = 0;

	list_for_each_entry(dynid, &dynids->list, node)
		if (dynid->id.bInterfaceClass != 0)
			count += scnprintf(&buf[count], PAGE_SIZE - count, "%04x %04x %02x\n",
					   dynid->id.idVendor, dynid->id.idProduct,
					   dynid->id.bInterfaceClass);
		else
			count += scnprintf(&buf[count], PAGE_SIZE - count, "%04x %04x\n",
					   dynid->id.idVendor, dynid->id.idProduct);
	return count;
}
EXPORT_SYMBOL_GPL(usb_show_dynids);

static ssize_t new_id_show(struct device_driver *driver, char *buf)
{
	struct usb_driver *usb_drv = to_usb_driver(driver);

	return usb_show_dynids(&usb_drv->dynids, buf);
}

static ssize_t new_id_store(struct device_driver *driver,
			    const char *buf, size_t count)
{
	struct usb_driver *usb_drv = to_usb_driver(driver);

	return usb_store_new_id(&usb_drv->dynids, usb_drv->id_table, driver, buf, count);
}
static DRIVER_ATTR_RW(new_id);

/*
 * Remove a USB device ID from this driver
 */
static ssize_t remove_id_store(struct device_driver *driver, const char *buf,
			       size_t count)
{
	struct usb_dynid *dynid, *n;
	struct usb_driver *usb_driver = to_usb_driver(driver);
	u32 idVendor;
	u32 idProduct;
	int fields;

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
			break;
		}
	}
	spin_unlock(&usb_driver->dynids.lock);
	return count;
}

static ssize_t remove_id_show(struct device_driver *driver, char *buf)
{
	return new_id_show(driver, buf);
}
static DRIVER_ATTR_RW(remove_id);

static int usb_create_newid_files(struct usb_driver *usb_drv)
{
	int error = 0;

	if (usb_drv->no_dynamic_id)
		goto exit;

	if (usb_drv->probe != NULL) {
		error = driver_create_file(&usb_drv->drvwrap.driver,
					   &driver_attr_new_id);
		if (error == 0) {
			error = driver_create_file(&usb_drv->drvwrap.driver,
					&driver_attr_remove_id);
			if (error)
				driver_remove_file(&usb_drv->drvwrap.driver,
						&driver_attr_new_id);
		}
	}
exit:
	return error;
}

static void usb_remove_newid_files(struct usb_driver *usb_drv)
{
	if (usb_drv->no_dynamic_id)
		return;

	if (usb_drv->probe != NULL) {
		driver_remove_file(&usb_drv->drvwrap.driver,
				&driver_attr_remove_id);
		driver_remove_file(&usb_drv->drvwrap.driver,
				   &driver_attr_new_id);
	}
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
	int error = 0;

	dev_dbg(dev, "%s\n", __func__);

	/* TODO: Add real matching code */

	/* The device should always appear to be in use
	 * unless the driver supports autosuspend.
	 */
	if (!udriver->supports_autosuspend)
		error = usb_autoresume_device(udev);

	if (!error)
		error = udriver->probe(udev);
	return error;
}

/* called from driver core with dev locked */
static int usb_unbind_device(struct device *dev)
{
	struct usb_device *udev = to_usb_device(dev);
	struct usb_device_driver *udriver = to_usb_device_driver(dev->driver);

	udriver->disconnect(udev);
	if (!udriver->supports_autosuspend)
		usb_autosuspend_device(udev);
	return 0;
}

/* called from driver core with dev locked */
static int usb_probe_interface(struct device *dev)
{
	struct usb_driver *driver = to_usb_driver(dev->driver);
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_device *udev = interface_to_usbdev(intf);
	const struct usb_device_id *id;
	int error = -ENODEV;
	int lpm_disable_error;

	dev_dbg(dev, "%s\n", __func__);

	intf->needs_binding = 0;

	if (usb_device_is_owned(udev))
		return error;

	if (udev->authorized == 0) {
		dev_err(&intf->dev, "Device is not authorized for usage\n");
		return error;
	} else if (intf->authorized == 0) {
		dev_err(&intf->dev, "Interface %d is not authorized for usage\n",
				intf->altsetting->desc.bInterfaceNumber);
		return error;
	}

	id = usb_match_dynamic_id(intf, driver);
	if (!id)
		id = usb_match_id(intf, driver->id_table);
	if (!id)
		return error;

	dev_dbg(dev, "%s - got id\n", __func__);

	error = usb_autoresume_device(udev);
	if (error)
		return error;

	intf->condition = USB_INTERFACE_BINDING;

	/* Probed interfaces are initially active.  They are
	 * runtime-PM-enabled only if the driver has autosuspend support.
	 * They are sensitive to their children's power states.
	 */
	pm_runtime_set_active(dev);
	pm_suspend_ignore_children(dev, false);
	if (driver->supports_autosuspend)
		pm_runtime_enable(dev);

	/* If the new driver doesn't allow hub-initiated LPM, and we can't
	 * disable hub-initiated LPM, then fail the probe.
	 *
	 * Otherwise, leaving LPM enabled should be harmless, because the
	 * endpoint intervals should remain the same, and the U1/U2 timeouts
	 * should remain the same.
	 *
	 * If we need to install alt setting 0 before probe, or another alt
	 * setting during probe, that should also be fine.  usb_set_interface()
	 * will attempt to disable LPM, and fail if it can't disable it.
	 */
	lpm_disable_error = usb_unlocked_disable_lpm(udev);
	if (lpm_disable_error && driver->disable_hub_initiated_lpm) {
		dev_err(&intf->dev, "%s Failed to disable LPM for driver %s\n.",
				__func__, driver->name);
		error = lpm_disable_error;
		goto err;
	}

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

	/* If the LPM disable succeeded, balance the ref counts. */
	if (!lpm_disable_error)
		usb_unlocked_enable_lpm(udev);

	usb_autosuspend_device(udev);
	return error;

 err:
	usb_set_intfdata(intf, NULL);
	intf->needs_remote_wakeup = 0;
	intf->condition = USB_INTERFACE_UNBOUND;

	/* If the LPM disable succeeded, balance the ref counts. */
	if (!lpm_disable_error)
		usb_unlocked_enable_lpm(udev);

	/* Unbound interfaces are always runtime-PM-disabled and -suspended */
	if (driver->supports_autosuspend)
		pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);

	usb_autosuspend_device(udev);
	return error;
}

/* called from driver core with dev locked */
static int usb_unbind_interface(struct device *dev)
{
	struct usb_driver *driver = to_usb_driver(dev->driver);
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_host_endpoint *ep, **eps = NULL;
	struct usb_device *udev;
	int i, j, error, r, lpm_disable_error;

	intf->condition = USB_INTERFACE_UNBINDING;

	/* Autoresume for set_interface call below */
	udev = interface_to_usbdev(intf);
	error = usb_autoresume_device(udev);

	/* Hub-initiated LPM policy may change, so attempt to disable LPM until
	 * the driver is unbound.  If LPM isn't disabled, that's fine because it
	 * wouldn't be enabled unless all the bound interfaces supported
	 * hub-initiated LPM.
	 */
	lpm_disable_error = usb_unlocked_disable_lpm(udev);

	/*
	 * Terminate all URBs for this interface unless the driver
	 * supports "soft" unbinding and the device is still present.
	 */
	if (!driver->soft_unbind || udev->state == USB_STATE_NOTATTACHED)
		usb_disable_interface(udev, intf, false);

	driver->disconnect(intf);

	/* Free streams */
	for (i = 0, j = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep = &intf->cur_altsetting->endpoint[i];
		if (ep->streams == 0)
			continue;
		if (j == 0) {
			eps = kmalloc_array(USB_MAXENDPOINTS, sizeof(void *),
				      GFP_KERNEL);
			if (!eps)
				break;
		}
		eps[j++] = ep;
	}
	if (j) {
		usb_free_streams(intf, eps, j, GFP_KERNEL);
		kfree(eps);
	}

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
	} else if (!error && !intf->dev.power.is_prepared) {
		r = usb_set_interface(udev, intf->altsetting[0].
				desc.bInterfaceNumber, 0);
		if (r < 0)
			intf->needs_altsetting0 = 1;
	} else {
		intf->needs_altsetting0 = 1;
	}
	usb_set_intfdata(intf, NULL);

	intf->condition = USB_INTERFACE_UNBOUND;
	intf->needs_remote_wakeup = 0;

	/* Attempt to re-enable USB3 LPM, if the disable succeeded. */
	if (!lpm_disable_error)
		usb_unlocked_enable_lpm(udev);

	/* Unbound interfaces are always runtime-PM-disabled and -suspended */
	if (driver->supports_autosuspend)
		pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);

	/* Undo any residual pm_autopm_get_interface_* calls */
	for (r = atomic_read(&intf->pm_usage_cnt); r > 0; --r)
		usb_autopm_put_interface_no_suspend(intf);
	atomic_set(&intf->pm_usage_cnt, 0);

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
 *
 * Return: 0 on success.
 */
int usb_driver_claim_interface(struct usb_driver *driver,
				struct usb_interface *iface, void *priv)
{
	struct device *dev = &iface->dev;
	struct usb_device *udev;
	int retval = 0;
	int lpm_disable_error;

	if (dev->driver)
		return -EBUSY;

	/* reject claim if interface is not authorized */
	if (!iface->authorized)
		return -ENODEV;

	udev = interface_to_usbdev(iface);

	dev->driver = &driver->drvwrap.driver;
	usb_set_intfdata(iface, priv);
	iface->needs_binding = 0;

	iface->condition = USB_INTERFACE_BOUND;

	/* Disable LPM until this driver is bound. */
	lpm_disable_error = usb_unlocked_disable_lpm(udev);
	if (lpm_disable_error && driver->disable_hub_initiated_lpm) {
		dev_err(&iface->dev, "%s Failed to disable LPM for driver %s\n.",
				__func__, driver->name);
		return -ENOMEM;
	}

	/* Claimed interfaces are initially inactive (suspended) and
	 * runtime-PM-enabled, but only if the driver has autosuspend
	 * support.  Otherwise they are marked active, to prevent the
	 * device from being autosuspended, but left disabled.  In either
	 * case they are sensitive to their children's power states.
	 */
	pm_suspend_ignore_children(dev, false);
	if (driver->supports_autosuspend)
		pm_runtime_enable(dev);
	else
		pm_runtime_set_active(dev);

	/* if interface was already added, bind now; else let
	 * the future device_add() bind it, bypassing probe()
	 */
	if (device_is_registered(dev))
		retval = device_bind_driver(dev);

	/* Attempt to re-enable USB3 LPM, if the disable was successful. */
	if (!lpm_disable_error)
		usb_unlocked_enable_lpm(udev);

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
		device_lock(dev);
		usb_unbind_interface(dev);
		dev->driver = NULL;
		device_unlock(dev);
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
int usb_match_one_id_intf(struct usb_device *dev,
			  struct usb_host_interface *intf,
			  const struct usb_device_id *id)
{
	/* The interface class, subclass, protocol and number should never be
	 * checked for a match if the device class is Vendor Specific,
	 * unless the match record specifies the Vendor ID. */
	if (dev->descriptor.bDeviceClass == USB_CLASS_VENDOR_SPEC &&
			!(id->match_flags & USB_DEVICE_ID_MATCH_VENDOR) &&
			(id->match_flags & (USB_DEVICE_ID_MATCH_INT_CLASS |
				USB_DEVICE_ID_MATCH_INT_SUBCLASS |
				USB_DEVICE_ID_MATCH_INT_PROTOCOL |
				USB_DEVICE_ID_MATCH_INT_NUMBER)))
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

	if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_NUMBER) &&
	    (id->bInterfaceNumber != intf->desc.bInterfaceNumber))
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

	return usb_match_one_id_intf(dev, intf, id);
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
 * Return: The first matching usb_device_id, or %NULL.
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

static int usb_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct usb_device *usb_dev;

	if (is_usb_device(dev)) {
		usb_dev = to_usb_device(dev);
	} else if (is_usb_interface(dev)) {
		struct usb_interface *intf = to_usb_interface(dev);

		usb_dev = interface_to_usbdev(intf);
	} else {
		return 0;
	}

	if (usb_dev->devnum < 0) {
		/* driver is often null here; dev_dbg() would oops */
		pr_debug("usb %s: already deleted?\n", dev_name(dev));
		return -ENODEV;
	}
	if (!usb_dev->bus) {
		pr_debug("usb %s: bus removed?\n", dev_name(dev));
		return -ENODEV;
	}

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

/**
 * usb_register_device_driver - register a USB device (not interface) driver
 * @new_udriver: USB operations for the device driver
 * @owner: module owner of this driver.
 *
 * Registers a USB device driver with the USB core.  The list of
 * unattached devices will be rescanned whenever a new driver is
 * added, allowing the new driver to attach to any recognized devices.
 *
 * Return: A negative error code on failure and 0 on success.
 */
int usb_register_device_driver(struct usb_device_driver *new_udriver,
		struct module *owner)
{
	int retval = 0;

	if (usb_disabled())
		return -ENODEV;

	new_udriver->drvwrap.for_devices = 1;
	new_udriver->drvwrap.driver.name = new_udriver->name;
	new_udriver->drvwrap.driver.bus = &usb_bus_type;
	new_udriver->drvwrap.driver.probe = usb_probe_device;
	new_udriver->drvwrap.driver.remove = usb_unbind_device;
	new_udriver->drvwrap.driver.owner = owner;

	retval = driver_register(&new_udriver->drvwrap.driver);

	if (!retval)
		pr_info("%s: registered new device driver %s\n",
			usbcore_name, new_udriver->name);
	else
		printk(KERN_ERR "%s: error %d registering device "
			"	driver %s\n",
			usbcore_name, retval, new_udriver->name);

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
 *
 * Return: A negative error code on failure and 0 on success.
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
	new_driver->drvwrap.driver.name = new_driver->name;
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

	retval = usb_create_newid_files(new_driver);
	if (retval)
		goto out_newid;

	pr_info("%s: registered new interface driver %s\n",
			usbcore_name, new_driver->name);

out:
	return retval;

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

	usb_remove_newid_files(driver);
	driver_unregister(&driver->drvwrap.driver);
	usb_free_dynids(driver);
}
EXPORT_SYMBOL_GPL(usb_deregister);

/* Forced unbinding of a USB interface driver, either because
 * it doesn't support pre_reset/post_reset/reset_resume or
 * because it doesn't support suspend/resume.
 *
 * The caller must hold @intf's device's lock, but not @intf's lock.
 */
void usb_forced_unbind_intf(struct usb_interface *intf)
{
	struct usb_driver *driver = to_usb_driver(intf->dev.driver);

	dev_dbg(&intf->dev, "forced unbind\n");
	usb_driver_release_interface(driver, intf);

	/* Mark the interface for later rebinding */
	intf->needs_binding = 1;
}

/*
 * Unbind drivers for @udev's marked interfaces.  These interfaces have
 * the needs_binding flag set, for example by usb_resume_interface().
 *
 * The caller must hold @udev's device lock.
 */
static void unbind_marked_interfaces(struct usb_device *udev)
{
	struct usb_host_config	*config;
	int			i;
	struct usb_interface	*intf;

	config = udev->actconfig;
	if (config) {
		for (i = 0; i < config->desc.bNumInterfaces; ++i) {
			intf = config->interface[i];
			if (intf->dev.driver && intf->needs_binding)
				usb_forced_unbind_intf(intf);
		}
	}
}

/* Delayed forced unbinding of a USB interface driver and scan
 * for rebinding.
 *
 * The caller must hold @intf's device's lock, but not @intf's lock.
 *
 * Note: Rebinds will be skipped if a system sleep transition is in
 * progress and the PM "complete" callback hasn't occurred yet.
 */
static void usb_rebind_intf(struct usb_interface *intf)
{
	int rc;

	/* Delayed unbind of an existing driver */
	if (intf->dev.driver)
		usb_forced_unbind_intf(intf);

	/* Try to rebind the interface */
	if (!intf->dev.power.is_prepared) {
		intf->needs_binding = 0;
		rc = device_attach(&intf->dev);
		if (rc < 0)
			dev_warn(&intf->dev, "rebind failed: %d\n", rc);
	}
}

/*
 * Rebind drivers to @udev's marked interfaces.  These interfaces have
 * the needs_binding flag set.
 *
 * The caller must hold @udev's device lock.
 */
static void rebind_marked_interfaces(struct usb_device *udev)
{
	struct usb_host_config	*config;
	int			i;
	struct usb_interface	*intf;

	config = udev->actconfig;
	if (config) {
		for (i = 0; i < config->desc.bNumInterfaces; ++i) {
			intf = config->interface[i];
			if (intf->needs_binding)
				usb_rebind_intf(intf);
		}
	}
}

/*
 * Unbind all of @udev's marked interfaces and then rebind all of them.
 * This ordering is necessary because some drivers claim several interfaces
 * when they are first probed.
 *
 * The caller must hold @udev's device lock.
 */
void usb_unbind_and_rebind_marked_interfaces(struct usb_device *udev)
{
	unbind_marked_interfaces(udev);
	rebind_marked_interfaces(udev);
}

#ifdef CONFIG_PM

/* Unbind drivers for @udev's interfaces that don't support suspend/resume
 * There is no check for reset_resume here because it can be determined
 * only during resume whether reset_resume is needed.
 *
 * The caller must hold @udev's device lock.
 */
static void unbind_no_pm_drivers_interfaces(struct usb_device *udev)
{
	struct usb_host_config	*config;
	int			i;
	struct usb_interface	*intf;
	struct usb_driver	*drv;

	config = udev->actconfig;
	if (config) {
		for (i = 0; i < config->desc.bNumInterfaces; ++i) {
			intf = config->interface[i];

			if (intf->dev.driver) {
				drv = to_usb_driver(intf->dev.driver);
				if (!drv->suspend || !drv->resume)
					usb_forced_unbind_intf(intf);
			}
		}
	}
}

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
	if (!PMSG_IS_AUTO(msg) && udev->parent && udev->bus->hs_companion)
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

static int usb_suspend_interface(struct usb_device *udev,
		struct usb_interface *intf, pm_message_t msg)
{
	struct usb_driver	*driver;
	int			status = 0;

	if (udev->state == USB_STATE_NOTATTACHED ||
			intf->condition == USB_INTERFACE_UNBOUND)
		goto done;
	driver = to_usb_driver(intf->dev.driver);

	/* at this time we know the driver supports suspend */
	status = driver->suspend(intf, msg);
	if (status && !PMSG_IS_AUTO(msg))
		dev_err(&intf->dev, "suspend error %d\n", status);

 done:
	dev_vdbg(&intf->dev, "%s: status %d\n", __func__, status);
	return status;
}

static int usb_resume_interface(struct usb_device *udev,
		struct usb_interface *intf, pm_message_t msg, int reset_resume)
{
	struct usb_driver	*driver;
	int			status = 0;

	if (udev->state == USB_STATE_NOTATTACHED)
		goto done;

	/* Don't let autoresume interfere with unbinding */
	if (intf->condition == USB_INTERFACE_UNBINDING)
		goto done;

	/* Can't resume it if it doesn't have a driver. */
	if (intf->condition == USB_INTERFACE_UNBOUND) {

		/* Carry out a deferred switch to altsetting 0 */
		if (intf->needs_altsetting0 && !intf->dev.power.is_prepared) {
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
			dev_dbg(&intf->dev, "no reset_resume for driver %s?\n",
					driver->name);
		}
	} else {
		status = driver->resume(intf);
		if (status)
			dev_err(&intf->dev, "resume error %d\n", status);
	}

done:
	dev_vdbg(&intf->dev, "%s: status %d\n", __func__, status);

	/* Later we will unbind the driver and/or reprobe, if necessary */
	return status;
}

/**
 * usb_suspend_both - suspend a USB device and its interfaces
 * @udev: the usb_device to suspend
 * @msg: Power Management message describing this state transition
 *
 * This is the central routine for suspending USB devices.  It calls the
 * suspend methods for all the interface drivers in @udev and then calls
 * the suspend method for @udev itself.  When the routine is called in
 * autosuspend, if an error occurs at any stage, all the interfaces
 * which were suspended are resumed so that they remain in the same
 * state as the device, but when called from system sleep, all error
 * from suspend methods of interfaces and the non-root-hub device itself
 * are simply ignored, so all suspended interfaces are only resumed
 * to the device's state when @udev is root-hub and its suspend method
 * returns failure.
 *
 * Autosuspend requests originating from a child device or an interface
 * driver may be made without the protection of @udev's device lock, but
 * all other suspend calls will hold the lock.  Usbcore will insure that
 * method calls do not arrive during bind, unbind, or reset operations.
 * However drivers must be prepared to handle suspend calls arriving at
 * unpredictable times.
 *
 * This routine can run only in process context.
 *
 * Return: 0 if the suspend succeeded.
 */
static int usb_suspend_both(struct usb_device *udev, pm_message_t msg)
{
	int			status = 0;
	int			i = 0, n = 0;
	struct usb_interface	*intf;

	if (udev->state == USB_STATE_NOTATTACHED ||
			udev->state == USB_STATE_SUSPENDED)
		goto done;

	/* Suspend all the interfaces and then udev itself */
	if (udev->actconfig) {
		n = udev->actconfig->desc.bNumInterfaces;
		for (i = n - 1; i >= 0; --i) {
			intf = udev->actconfig->interface[i];
			status = usb_suspend_interface(udev, intf, msg);

			/* Ignore errors during system sleep transitions */
			if (!PMSG_IS_AUTO(msg))
				status = 0;
			if (status != 0)
				break;
		}
	}
	if (status == 0) {
		status = usb_suspend_device(udev, msg);

		/*
		 * Ignore errors from non-root-hub devices during
		 * system sleep transitions.  For the most part,
		 * these devices should go to low power anyway when
		 * the entire bus is suspended.
		 */
		if (udev->parent && !PMSG_IS_AUTO(msg))
			status = 0;
	}

	/* If the suspend failed, resume interfaces that did get suspended */
	if (status != 0) {
		if (udev->actconfig) {
			msg.event ^= (PM_EVENT_SUSPEND | PM_EVENT_RESUME);
			while (++i < n) {
				intf = udev->actconfig->interface[i];
				usb_resume_interface(udev, intf, msg, 0);
			}
		}

	/* If the suspend succeeded then prevent any more URB submissions
	 * and flush any outstanding URBs.
	 */
	} else {
		udev->can_submit = 0;
		for (i = 0; i < 16; ++i) {
			usb_hcd_flush_endpoint(udev, udev->ep_out[i]);
			usb_hcd_flush_endpoint(udev, udev->ep_in[i]);
		}
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
 * Autoresume requests originating from a child device or an interface
 * driver may be made without the protection of @udev's device lock, but
 * all other resume calls will hold the lock.  Usbcore will insure that
 * method calls do not arrive during bind, unbind, or reset operations.
 * However drivers must be prepared to handle resume calls arriving at
 * unpredictable times.
 *
 * This routine can run only in process context.
 *
 * Return: 0 on success.
 */
static int usb_resume_both(struct usb_device *udev, pm_message_t msg)
{
	int			status = 0;
	int			i;
	struct usb_interface	*intf;

	if (udev->state == USB_STATE_NOTATTACHED) {
		status = -ENODEV;
		goto done;
	}
	udev->can_submit = 1;

	/* Resume the device */
	if (udev->state == USB_STATE_SUSPENDED || udev->reset_resume)
		status = usb_resume_device(udev, msg);

	/* Resume the interfaces */
	if (status == 0 && udev->actconfig) {
		for (i = 0; i < udev->actconfig->desc.bNumInterfaces; i++) {
			intf = udev->actconfig->interface[i];
			usb_resume_interface(udev, intf, msg,
					udev->reset_resume);
		}
	}
	usb_mark_last_busy(udev);

 done:
	dev_vdbg(&udev->dev, "%s: status %d\n", __func__, status);
	if (!status)
		udev->reset_resume = 0;
	return status;
}

static void choose_wakeup(struct usb_device *udev, pm_message_t msg)
{
	int	w;

	/* Remote wakeup is needed only when we actually go to sleep.
	 * For things like FREEZE and QUIESCE, if the device is already
	 * autosuspended then its current wakeup setting is okay.
	 */
	if (msg.event == PM_EVENT_FREEZE || msg.event == PM_EVENT_QUIESCE) {
		if (udev->state != USB_STATE_SUSPENDED)
			udev->do_remote_wakeup = 0;
		return;
	}

	/* Enable remote wakeup if it is allowed, even if no interface drivers
	 * actually want it.
	 */
	w = device_may_wakeup(&udev->dev);

	/* If the device is autosuspended with the wrong wakeup setting,
	 * autoresume now so the setting can be changed.
	 */
	if (udev->state == USB_STATE_SUSPENDED && w != udev->do_remote_wakeup)
		pm_runtime_resume(&udev->dev);
	udev->do_remote_wakeup = w;
}

/* The device lock is held by the PM core */
int usb_suspend(struct device *dev, pm_message_t msg)
{
	struct usb_device	*udev = to_usb_device(dev);

	unbind_no_pm_drivers_interfaces(udev);

	/* From now on we are sure all drivers support suspend/resume
	 * but not necessarily reset_resume()
	 * so we may still need to unbind and rebind upon resume
	 */
	choose_wakeup(udev, msg);
	return usb_suspend_both(udev, msg);
}

/* The device lock is held by the PM core */
int usb_resume_complete(struct device *dev)
{
	struct usb_device *udev = to_usb_device(dev);

	/* For PM complete calls, all we do is rebind interfaces
	 * whose needs_binding flag is set
	 */
	if (udev->state != USB_STATE_NOTATTACHED)
		rebind_marked_interfaces(udev);
	return 0;
}

/* The device lock is held by the PM core */
int usb_resume(struct device *dev, pm_message_t msg)
{
	struct usb_device	*udev = to_usb_device(dev);
	int			status;

	/* For all calls, take the device back to full power and
	 * tell the PM core in case it was autosuspended previously.
	 * Unbind the interfaces that will need rebinding later,
	 * because they fail to support reset_resume.
	 * (This can't be done in usb_resume_interface()
	 * above because it doesn't own the right set of locks.)
	 */
	status = usb_resume_both(udev, msg);
	if (status == 0) {
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
		unbind_marked_interfaces(udev);
	}

	/* Avoid PM error messages for devices disconnected while suspended
	 * as we'll display regular disconnect messages just a bit later.
	 */
	if (status == -ENODEV || status == -ESHUTDOWN)
		status = 0;
	return status;
}

/**
 * usb_enable_autosuspend - allow a USB device to be autosuspended
 * @udev: the USB device which may be autosuspended
 *
 * This routine allows @udev to be autosuspended.  An autosuspend won't
 * take place until the autosuspend_delay has elapsed and all the other
 * necessary conditions are satisfied.
 *
 * The caller must hold @udev's device lock.
 */
void usb_enable_autosuspend(struct usb_device *udev)
{
	pm_runtime_allow(&udev->dev);
}
EXPORT_SYMBOL_GPL(usb_enable_autosuspend);

/**
 * usb_disable_autosuspend - prevent a USB device from being autosuspended
 * @udev: the USB device which may not be autosuspended
 *
 * This routine prevents @udev from being autosuspended and wakes it up
 * if it is already autosuspended.
 *
 * The caller must hold @udev's device lock.
 */
void usb_disable_autosuspend(struct usb_device *udev)
{
	pm_runtime_forbid(&udev->dev);
}
EXPORT_SYMBOL_GPL(usb_disable_autosuspend);

/**
 * usb_autosuspend_device - delayed autosuspend of a USB device and its interfaces
 * @udev: the usb_device to autosuspend
 *
 * This routine should be called when a core subsystem is finished using
 * @udev and wants to allow it to autosuspend.  Examples would be when
 * @udev's device file in usbfs is closed or after a configuration change.
 *
 * @udev's usage counter is decremented; if it drops to 0 and all the
 * interfaces are inactive then a delayed autosuspend will be attempted.
 * The attempt may fail (see autosuspend_check()).
 *
 * The caller must hold @udev's device lock.
 *
 * This routine can run only in process context.
 */
void usb_autosuspend_device(struct usb_device *udev)
{
	int	status;

	usb_mark_last_busy(udev);
	status = pm_runtime_put_sync_autosuspend(&udev->dev);
	dev_vdbg(&udev->dev, "%s: cnt %d -> %d\n",
			__func__, atomic_read(&udev->dev.power.usage_count),
			status);
}

/**
 * usb_autoresume_device - immediately autoresume a USB device and its interfaces
 * @udev: the usb_device to autoresume
 *
 * This routine should be called when a core subsystem wants to use @udev
 * and needs to guarantee that it is not suspended.  No autosuspend will
 * occur until usb_autosuspend_device() is called.  (Note that this will
 * not prevent suspend events originating in the PM core.)  Examples would
 * be when @udev's device file in usbfs is opened or when a remote-wakeup
 * request is received.
 *
 * @udev's usage counter is incremented to prevent subsequent autosuspends.
 * However if the autoresume fails then the usage counter is re-decremented.
 *
 * The caller must hold @udev's device lock.
 *
 * This routine can run only in process context.
 *
 * Return: 0 on success. A negative error code otherwise.
 */
int usb_autoresume_device(struct usb_device *udev)
{
	int	status;

	status = pm_runtime_get_sync(&udev->dev);
	if (status < 0)
		pm_runtime_put_sync(&udev->dev);
	dev_vdbg(&udev->dev, "%s: cnt %d -> %d\n",
			__func__, atomic_read(&udev->dev.power.usage_count),
			status);
	if (status > 0)
		status = 0;
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
 * 0, a delayed autosuspend request for @intf's device is attempted.  The
 * attempt may fail (see autosuspend_check()).
 *
 * This routine can run only in process context.
 */
void usb_autopm_put_interface(struct usb_interface *intf)
{
	struct usb_device	*udev = interface_to_usbdev(intf);
	int			status;

	usb_mark_last_busy(udev);
	atomic_dec(&intf->pm_usage_cnt);
	status = pm_runtime_put_sync(&intf->dev);
	dev_vdbg(&intf->dev, "%s: cnt %d -> %d\n",
			__func__, atomic_read(&intf->dev.power.usage_count),
			status);
}
EXPORT_SYMBOL_GPL(usb_autopm_put_interface);

/**
 * usb_autopm_put_interface_async - decrement a USB interface's PM-usage counter
 * @intf: the usb_interface whose counter should be decremented
 *
 * This routine does much the same thing as usb_autopm_put_interface():
 * It decrements @intf's usage counter and schedules a delayed
 * autosuspend request if the counter is <= 0.  The difference is that it
 * does not perform any synchronization; callers should hold a private
 * lock and handle all synchronization issues themselves.
 *
 * Typically a driver would call this routine during an URB's completion
 * handler, if no more URBs were pending.
 *
 * This routine can run in atomic context.
 */
void usb_autopm_put_interface_async(struct usb_interface *intf)
{
	struct usb_device	*udev = interface_to_usbdev(intf);
	int			status;

	usb_mark_last_busy(udev);
	atomic_dec(&intf->pm_usage_cnt);
	status = pm_runtime_put(&intf->dev);
	dev_vdbg(&intf->dev, "%s: cnt %d -> %d\n",
			__func__, atomic_read(&intf->dev.power.usage_count),
			status);
}
EXPORT_SYMBOL_GPL(usb_autopm_put_interface_async);

/**
 * usb_autopm_put_interface_no_suspend - decrement a USB interface's PM-usage counter
 * @intf: the usb_interface whose counter should be decremented
 *
 * This routine decrements @intf's usage counter but does not carry out an
 * autosuspend.
 *
 * This routine can run in atomic context.
 */
void usb_autopm_put_interface_no_suspend(struct usb_interface *intf)
{
	struct usb_device	*udev = interface_to_usbdev(intf);

	usb_mark_last_busy(udev);
	atomic_dec(&intf->pm_usage_cnt);
	pm_runtime_put_noidle(&intf->dev);
}
EXPORT_SYMBOL_GPL(usb_autopm_put_interface_no_suspend);

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
 * @intf's usage counter is incremented to prevent subsequent autosuspends.
 * However if the autoresume fails then the counter is re-decremented.
 *
 * This routine can run only in process context.
 *
 * Return: 0 on success.
 */
int usb_autopm_get_interface(struct usb_interface *intf)
{
	int	status;

	status = pm_runtime_get_sync(&intf->dev);
	if (status < 0)
		pm_runtime_put_sync(&intf->dev);
	else
		atomic_inc(&intf->pm_usage_cnt);
	dev_vdbg(&intf->dev, "%s: cnt %d -> %d\n",
			__func__, atomic_read(&intf->dev.power.usage_count),
			status);
	if (status > 0)
		status = 0;
	return status;
}
EXPORT_SYMBOL_GPL(usb_autopm_get_interface);

/**
 * usb_autopm_get_interface_async - increment a USB interface's PM-usage counter
 * @intf: the usb_interface whose counter should be incremented
 *
 * This routine does much the same thing as
 * usb_autopm_get_interface(): It increments @intf's usage counter and
 * queues an autoresume request if the device is suspended.  The
 * differences are that it does not perform any synchronization (callers
 * should hold a private lock and handle all synchronization issues
 * themselves), and it does not autoresume the device directly (it only
 * queues a request).  After a successful call, the device may not yet be
 * resumed.
 *
 * This routine can run in atomic context.
 *
 * Return: 0 on success. A negative error code otherwise.
 */
int usb_autopm_get_interface_async(struct usb_interface *intf)
{
	int	status;

	status = pm_runtime_get(&intf->dev);
	if (status < 0 && status != -EINPROGRESS)
		pm_runtime_put_noidle(&intf->dev);
	else
		atomic_inc(&intf->pm_usage_cnt);
	dev_vdbg(&intf->dev, "%s: cnt %d -> %d\n",
			__func__, atomic_read(&intf->dev.power.usage_count),
			status);
	if (status > 0 || status == -EINPROGRESS)
		status = 0;
	return status;
}
EXPORT_SYMBOL_GPL(usb_autopm_get_interface_async);

/**
 * usb_autopm_get_interface_no_resume - increment a USB interface's PM-usage counter
 * @intf: the usb_interface whose counter should be incremented
 *
 * This routine increments @intf's usage counter but does not carry out an
 * autoresume.
 *
 * This routine can run in atomic context.
 */
void usb_autopm_get_interface_no_resume(struct usb_interface *intf)
{
	struct usb_device	*udev = interface_to_usbdev(intf);

	usb_mark_last_busy(udev);
	atomic_inc(&intf->pm_usage_cnt);
	pm_runtime_get_noresume(&intf->dev);
}
EXPORT_SYMBOL_GPL(usb_autopm_get_interface_no_resume);

/* Internal routine to check whether we may autosuspend a device. */
static int autosuspend_check(struct usb_device *udev)
{
	int			w, i;
	struct usb_interface	*intf;

	/* Fail if autosuspend is disabled, or any interfaces are in use, or
	 * any interface drivers require remote wakeup but it isn't available.
	 */
	w = 0;
	if (udev->actconfig) {
		for (i = 0; i < udev->actconfig->desc.bNumInterfaces; i++) {
			intf = udev->actconfig->interface[i];

			/* We don't need to check interfaces that are
			 * disabled for runtime PM.  Either they are unbound
			 * or else their drivers don't support autosuspend
			 * and so they are permanently active.
			 */
			if (intf->dev.power.disable_depth)
				continue;
			if (atomic_read(&intf->dev.power.usage_count) > 0)
				return -EBUSY;
			w |= intf->needs_remote_wakeup;

			/* Don't allow autosuspend if the device will need
			 * a reset-resume and any of its interface drivers
			 * doesn't include support or needs remote wakeup.
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
	if (w && !device_can_wakeup(&udev->dev)) {
		dev_dbg(&udev->dev, "remote wakeup needed for autosuspend\n");
		return -EOPNOTSUPP;
	}

	/*
	 * If the device is a direct child of the root hub and the HCD
	 * doesn't handle wakeup requests, don't allow autosuspend when
	 * wakeup is needed.
	 */
	if (w && udev->parent == udev->bus->root_hub &&
			bus_to_hcd(udev->bus)->cant_recv_wakeups) {
		dev_dbg(&udev->dev, "HCD doesn't handle wakeup requests\n");
		return -EOPNOTSUPP;
	}

	udev->do_remote_wakeup = w;
	return 0;
}

int usb_runtime_suspend(struct device *dev)
{
	struct usb_device	*udev = to_usb_device(dev);
	int			status;

	/* A USB device can be suspended if it passes the various autosuspend
	 * checks.  Runtime suspend for a USB device means suspending all the
	 * interfaces and then the device itself.
	 */
	if (autosuspend_check(udev) != 0)
		return -EAGAIN;

	status = usb_suspend_both(udev, PMSG_AUTO_SUSPEND);

	/* Allow a retry if autosuspend failed temporarily */
	if (status == -EAGAIN || status == -EBUSY)
		usb_mark_last_busy(udev);

	/*
	 * The PM core reacts badly unless the return code is 0,
	 * -EAGAIN, or -EBUSY, so always return -EBUSY on an error
	 * (except for root hubs, because they don't suspend through
	 * an upstream port like other USB devices).
	 */
	if (status != 0 && udev->parent)
		return -EBUSY;
	return status;
}

int usb_runtime_resume(struct device *dev)
{
	struct usb_device	*udev = to_usb_device(dev);
	int			status;

	/* Runtime resume for a USB device means resuming both the device
	 * and all its interfaces.
	 */
	status = usb_resume_both(udev, PMSG_AUTO_RESUME);
	return status;
}

int usb_runtime_idle(struct device *dev)
{
	struct usb_device	*udev = to_usb_device(dev);

	/* An idle USB device can be suspended if it passes the various
	 * autosuspend checks.
	 */
	if (autosuspend_check(udev) == 0)
		pm_runtime_autosuspend(dev);
	/* Tell the core not to suspend it, though. */
	return -EBUSY;
}

int usb_set_usb2_hardware_lpm(struct usb_device *udev, int enable)
{
	struct usb_hcd *hcd = bus_to_hcd(udev->bus);
	int ret = -EPERM;

	if (enable && !udev->usb2_hw_lpm_allowed)
		return 0;

	if (hcd->driver->set_usb2_hw_lpm) {
		ret = hcd->driver->set_usb2_hw_lpm(hcd, udev, enable);
		if (!ret)
			udev->usb2_hw_lpm_enabled = enable;
	}

	return ret;
}

#endif /* CONFIG_PM */

struct bus_type usb_bus_type = {
	.name =		"usb",
	.match =	usb_device_match,
	.uevent =	usb_uevent,
};
