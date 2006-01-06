/*
 * drivers/usb/usb.c
 *
 * (C) Copyright Linus Torvalds 1999
 * (C) Copyright Johannes Erdfelt 1999-2001
 * (C) Copyright Andreas Gal 1999
 * (C) Copyright Gregory P. Smith 1999
 * (C) Copyright Deti Fliegl 1999 (new USB architecture)
 * (C) Copyright Randy Dunlap 2000
 * (C) Copyright David Brownell 2000-2004
 * (C) Copyright Yggdrasil Computing, Inc. 2000
 *     (usb_device_id matching changes by Adam J. Richter)
 * (C) Copyright Greg Kroah-Hartman 2002-2003
 *
 * NOTE! This is not actually a driver at all, rather this is
 * just a collection of helper routines that implement the
 * generic USB things that the real drivers can use..
 *
 * Think of this as a "USB library" rather than anything else.
 * It should be considered a slave, with no callbacks. Callbacks
 * are evil.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/interrupt.h>  /* for in_interrupt() */
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/smp_lock.h>
#include <linux/usb.h>

#include <asm/io.h>
#include <asm/scatterlist.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>

#include "hcd.h"
#include "usb.h"


const char *usbcore_name = "usbcore";

static int nousb;	/* Disable USB when built into kernel image */


/**
 * usb_ifnum_to_if - get the interface object with a given interface number
 * @dev: the device whose current configuration is considered
 * @ifnum: the desired interface
 *
 * This walks the device descriptor for the currently active configuration
 * and returns a pointer to the interface with that particular interface
 * number, or null.
 *
 * Note that configuration descriptors are not required to assign interface
 * numbers sequentially, so that it would be incorrect to assume that
 * the first interface in that descriptor corresponds to interface zero.
 * This routine helps device drivers avoid such mistakes.
 * However, you should make sure that you do the right thing with any
 * alternate settings available for this interfaces.
 *
 * Don't call this function unless you are bound to one of the interfaces
 * on this device or you have locked the device!
 */
struct usb_interface *usb_ifnum_to_if(struct usb_device *dev, unsigned ifnum)
{
	struct usb_host_config *config = dev->actconfig;
	int i;

	if (!config)
		return NULL;
	for (i = 0; i < config->desc.bNumInterfaces; i++)
		if (config->interface[i]->altsetting[0]
				.desc.bInterfaceNumber == ifnum)
			return config->interface[i];

	return NULL;
}

/**
 * usb_altnum_to_altsetting - get the altsetting structure with a given
 *	alternate setting number.
 * @intf: the interface containing the altsetting in question
 * @altnum: the desired alternate setting number
 *
 * This searches the altsetting array of the specified interface for
 * an entry with the correct bAlternateSetting value and returns a pointer
 * to that entry, or null.
 *
 * Note that altsettings need not be stored sequentially by number, so
 * it would be incorrect to assume that the first altsetting entry in
 * the array corresponds to altsetting zero.  This routine helps device
 * drivers avoid such mistakes.
 *
 * Don't call this function unless you are bound to the intf interface
 * or you have locked the device!
 */
struct usb_host_interface *usb_altnum_to_altsetting(struct usb_interface *intf,
		unsigned int altnum)
{
	int i;

	for (i = 0; i < intf->num_altsetting; i++) {
		if (intf->altsetting[i].desc.bAlternateSetting == altnum)
			return &intf->altsetting[i];
	}
	return NULL;
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
 * Callers must own the device lock and the driver model's usb_bus_type.subsys
 * writelock.  So driver probe() entries don't need extra locking,
 * but other call contexts may need to explicitly claim those locks.
 */
int usb_driver_claim_interface(struct usb_driver *driver,
				struct usb_interface *iface, void* priv)
{
	struct device *dev = &iface->dev;

	if (dev->driver)
		return -EBUSY;

	dev->driver = &driver->driver;
	usb_set_intfdata(iface, priv);
	iface->condition = USB_INTERFACE_BOUND;
	mark_active(iface);

	/* if interface was already added, bind now; else let
	 * the future device_add() bind it, bypassing probe()
	 */
	if (device_is_registered(dev))
		device_bind_driver(dev);

	return 0;
}

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
 * Callers must own the device lock and the driver model's usb_bus_type.subsys
 * writelock.  So driver disconnect() entries don't need extra locking,
 * but other call contexts may need to explicitly claim those locks.
 */
void usb_driver_release_interface(struct usb_driver *driver,
					struct usb_interface *iface)
{
	struct device *dev = &iface->dev;

	/* this should never happen, don't release something that's not ours */
	if (!dev->driver || dev->driver != &driver->driver)
		return;

	/* don't release from within disconnect() */
	if (iface->condition != USB_INTERFACE_BOUND)
		return;

	/* don't release if the interface hasn't been added yet */
	if (device_is_registered(dev)) {
		iface->condition = USB_INTERFACE_UNBINDING;
		device_release_driver(dev);
	}

	dev->driver = NULL;
	usb_set_intfdata(iface, NULL);
	iface->condition = USB_INTERFACE_UNBOUND;
	mark_quiesced(iface);
}

struct find_interface_arg {
	int minor;
	struct usb_interface *interface;
};

static int __find_interface(struct device * dev, void * data)
{
	struct find_interface_arg *arg = data;
	struct usb_interface *intf;

	/* can't look at usb devices, only interfaces */
	if (dev->driver == &usb_generic_driver)
		return 0;

	intf = to_usb_interface(dev);
	if (intf->minor != -1 && intf->minor == arg->minor) {
		arg->interface = intf;
		return 1;
	}
	return 0;
}

/**
 * usb_find_interface - find usb_interface pointer for driver and device
 * @drv: the driver whose current configuration is considered
 * @minor: the minor number of the desired device
 *
 * This walks the driver device list and returns a pointer to the interface 
 * with the matching minor.  Note, this only works for devices that share the
 * USB major number.
 */
struct usb_interface *usb_find_interface(struct usb_driver *drv, int minor)
{
	struct find_interface_arg argb;

	argb.minor = minor;
	argb.interface = NULL;
	driver_for_each_device(&drv->driver, NULL, &argb, __find_interface);
	return argb.interface;
}

#ifdef	CONFIG_HOTPLUG

/*
 * This sends an uevent to userspace, typically helping to load driver
 * or other modules, configure the device, and more.  Drivers can provide
 * a MODULE_DEVICE_TABLE to help with module loading subtasks.
 *
 * We're called either from khubd (the typical case) or from root hub
 * (init, kapmd, modprobe, rmmod, etc), but the agents need to handle
 * delays in event delivery.  Use sysfs (and DEVPATH) to make sure the
 * device (and this configuration!) are still present.
 */
static int usb_uevent(struct device *dev, char **envp, int num_envp,
		      char *buffer, int buffer_size)
{
	struct usb_interface *intf;
	struct usb_device *usb_dev;
	struct usb_host_interface *alt;
	int i = 0;
	int length = 0;

	if (!dev)
		return -ENODEV;

	/* driver is often null here; dev_dbg() would oops */
	pr_debug ("usb %s: uevent\n", dev->bus_id);

	/* Must check driver_data here, as on remove driver is always NULL */
	if ((dev->driver == &usb_generic_driver) || 
	    (dev->driver_data == &usb_generic_driver_data))
		return 0;

	intf = to_usb_interface(dev);
	usb_dev = interface_to_usbdev (intf);
	alt = intf->cur_altsetting;

	if (usb_dev->devnum < 0) {
		pr_debug ("usb %s: already deleted?\n", dev->bus_id);
		return -ENODEV;
	}
	if (!usb_dev->bus) {
		pr_debug ("usb %s: bus removed?\n", dev->bus_id);
		return -ENODEV;
	}

#ifdef	CONFIG_USB_DEVICEFS
	/* If this is available, userspace programs can directly read
	 * all the device descriptors we don't tell them about.  Or
	 * even act as usermode drivers.
	 *
	 * FIXME reduce hardwired intelligence here
	 */
	if (add_uevent_var(envp, num_envp, &i,
			   buffer, buffer_size, &length,
			   "DEVICE=/proc/bus/usb/%03d/%03d",
			   usb_dev->bus->busnum, usb_dev->devnum))
		return -ENOMEM;
#endif

	/* per-device configurations are common */
	if (add_uevent_var(envp, num_envp, &i,
			   buffer, buffer_size, &length,
			   "PRODUCT=%x/%x/%x",
			   le16_to_cpu(usb_dev->descriptor.idVendor),
			   le16_to_cpu(usb_dev->descriptor.idProduct),
			   le16_to_cpu(usb_dev->descriptor.bcdDevice)))
		return -ENOMEM;

	/* class-based driver binding models */
	if (add_uevent_var(envp, num_envp, &i,
			   buffer, buffer_size, &length,
			   "TYPE=%d/%d/%d",
			   usb_dev->descriptor.bDeviceClass,
			   usb_dev->descriptor.bDeviceSubClass,
			   usb_dev->descriptor.bDeviceProtocol))
		return -ENOMEM;

	if (add_uevent_var(envp, num_envp, &i,
			   buffer, buffer_size, &length,
			   "INTERFACE=%d/%d/%d",
			   alt->desc.bInterfaceClass,
			   alt->desc.bInterfaceSubClass,
			   alt->desc.bInterfaceProtocol))
		return -ENOMEM;

	if (add_uevent_var(envp, num_envp, &i,
			   buffer, buffer_size, &length,
			   "MODALIAS=usb:v%04Xp%04Xd%04Xdc%02Xdsc%02Xdp%02Xic%02Xisc%02Xip%02X",
			   le16_to_cpu(usb_dev->descriptor.idVendor),
			   le16_to_cpu(usb_dev->descriptor.idProduct),
			   le16_to_cpu(usb_dev->descriptor.bcdDevice),
			   usb_dev->descriptor.bDeviceClass,
			   usb_dev->descriptor.bDeviceSubClass,
			   usb_dev->descriptor.bDeviceProtocol,
			   alt->desc.bInterfaceClass,
			   alt->desc.bInterfaceSubClass,
			   alt->desc.bInterfaceProtocol))
		return -ENOMEM;

	envp[i] = NULL;

	return 0;
}

#else

static int usb_uevent(struct device *dev, char **envp,
			int num_envp, char *buffer, int buffer_size)
{
	return -ENODEV;
}

#endif	/* CONFIG_HOTPLUG */

/**
 * usb_release_dev - free a usb device structure when all users of it are finished.
 * @dev: device that's been disconnected
 *
 * Will be called only by the device core when all users of this usb device are
 * done.
 */
static void usb_release_dev(struct device *dev)
{
	struct usb_device *udev;

	udev = to_usb_device(dev);

	usb_destroy_configuration(udev);
	usb_bus_put(udev->bus);
	kfree(udev->product);
	kfree(udev->manufacturer);
	kfree(udev->serial);
	kfree(udev);
}

/**
 * usb_alloc_dev - usb device constructor (usbcore-internal)
 * @parent: hub to which device is connected; null to allocate a root hub
 * @bus: bus used to access the device
 * @port1: one-based index of port; ignored for root hubs
 * Context: !in_interrupt ()
 *
 * Only hub drivers (including virtual root hub drivers for host
 * controllers) should ever call this.
 *
 * This call may not be used in a non-sleeping context.
 */
struct usb_device *
usb_alloc_dev(struct usb_device *parent, struct usb_bus *bus, unsigned port1)
{
	struct usb_device *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	bus = usb_bus_get(bus);
	if (!bus) {
		kfree(dev);
		return NULL;
	}

	device_initialize(&dev->dev);
	dev->dev.bus = &usb_bus_type;
	dev->dev.dma_mask = bus->controller->dma_mask;
	dev->dev.driver_data = &usb_generic_driver_data;
	dev->dev.driver = &usb_generic_driver;
	dev->dev.release = usb_release_dev;
	dev->state = USB_STATE_ATTACHED;

	INIT_LIST_HEAD(&dev->ep0.urb_list);
	dev->ep0.desc.bLength = USB_DT_ENDPOINT_SIZE;
	dev->ep0.desc.bDescriptorType = USB_DT_ENDPOINT;
	/* ep0 maxpacket comes later, from device descriptor */
	dev->ep_in[0] = dev->ep_out[0] = &dev->ep0;

	/* Save readable and stable topology id, distinguishing devices
	 * by location for diagnostics, tools, driver model, etc.  The
	 * string is a path along hub ports, from the root.  Each device's
	 * dev->devpath will be stable until USB is re-cabled, and hubs
	 * are often labeled with these port numbers.  The bus_id isn't
	 * as stable:  bus->busnum changes easily from modprobe order,
	 * cardbus or pci hotplugging, and so on.
	 */
	if (unlikely (!parent)) {
		dev->devpath [0] = '0';

		dev->dev.parent = bus->controller;
		sprintf (&dev->dev.bus_id[0], "usb%d", bus->busnum);
	} else {
		/* match any labeling on the hubs; it's one-based */
		if (parent->devpath [0] == '0')
			snprintf (dev->devpath, sizeof dev->devpath,
				"%d", port1);
		else
			snprintf (dev->devpath, sizeof dev->devpath,
				"%s.%d", parent->devpath, port1);

		dev->dev.parent = &parent->dev;
		sprintf (&dev->dev.bus_id[0], "%d-%s",
			bus->busnum, dev->devpath);

		/* hub driver sets up TT records */
	}

	dev->portnum = port1;
	dev->bus = bus;
	dev->parent = parent;
	INIT_LIST_HEAD(&dev->filelist);

	return dev;
}

/**
 * usb_get_dev - increments the reference count of the usb device structure
 * @dev: the device being referenced
 *
 * Each live reference to a device should be refcounted.
 *
 * Drivers for USB interfaces should normally record such references in
 * their probe() methods, when they bind to an interface, and release
 * them by calling usb_put_dev(), in their disconnect() methods.
 *
 * A pointer to the device with the incremented reference counter is returned.
 */
struct usb_device *usb_get_dev(struct usb_device *dev)
{
	if (dev)
		get_device(&dev->dev);
	return dev;
}

/**
 * usb_put_dev - release a use of the usb device structure
 * @dev: device that's been disconnected
 *
 * Must be called when a user of a device is finished with it.  When the last
 * user of the device calls this function, the memory of the device is freed.
 */
void usb_put_dev(struct usb_device *dev)
{
	if (dev)
		put_device(&dev->dev);
}

/**
 * usb_get_intf - increments the reference count of the usb interface structure
 * @intf: the interface being referenced
 *
 * Each live reference to a interface must be refcounted.
 *
 * Drivers for USB interfaces should normally record such references in
 * their probe() methods, when they bind to an interface, and release
 * them by calling usb_put_intf(), in their disconnect() methods.
 *
 * A pointer to the interface with the incremented reference counter is
 * returned.
 */
struct usb_interface *usb_get_intf(struct usb_interface *intf)
{
	if (intf)
		get_device(&intf->dev);
	return intf;
}

/**
 * usb_put_intf - release a use of the usb interface structure
 * @intf: interface that's been decremented
 *
 * Must be called when a user of an interface is finished with it.  When the
 * last user of the interface calls this function, the memory of the interface
 * is freed.
 */
void usb_put_intf(struct usb_interface *intf)
{
	if (intf)
		put_device(&intf->dev);
}


/*			USB device locking
 *
 * USB devices and interfaces are locked using the semaphore in their
 * embedded struct device.  The hub driver guarantees that whenever a
 * device is connected or disconnected, drivers are called with the
 * USB device locked as well as their particular interface.
 *
 * Complications arise when several devices are to be locked at the same
 * time.  Only hub-aware drivers that are part of usbcore ever have to
 * do this; nobody else needs to worry about it.  The rule for locking
 * is simple:
 *
 *	When locking both a device and its parent, always lock the
 *	the parent first.
 */

/**
 * usb_lock_device_for_reset - cautiously acquire the lock for a
 *	usb device structure
 * @udev: device that's being locked
 * @iface: interface bound to the driver making the request (optional)
 *
 * Attempts to acquire the device lock, but fails if the device is
 * NOTATTACHED or SUSPENDED, or if iface is specified and the interface
 * is neither BINDING nor BOUND.  Rather than sleeping to wait for the
 * lock, the routine polls repeatedly.  This is to prevent deadlock with
 * disconnect; in some drivers (such as usb-storage) the disconnect()
 * or suspend() method will block waiting for a device reset to complete.
 *
 * Returns a negative error code for failure, otherwise 1 or 0 to indicate
 * that the device will or will not have to be unlocked.  (0 can be
 * returned when an interface is given and is BINDING, because in that
 * case the driver already owns the device lock.)
 */
int usb_lock_device_for_reset(struct usb_device *udev,
		struct usb_interface *iface)
{
	unsigned long jiffies_expire = jiffies + HZ;

	if (udev->state == USB_STATE_NOTATTACHED)
		return -ENODEV;
	if (udev->state == USB_STATE_SUSPENDED)
		return -EHOSTUNREACH;
	if (iface) {
		switch (iface->condition) {
		  case USB_INTERFACE_BINDING:
			return 0;
		  case USB_INTERFACE_BOUND:
			break;
		  default:
			return -EINTR;
		}
	}

	while (usb_trylock_device(udev) != 0) {

		/* If we can't acquire the lock after waiting one second,
		 * we're probably deadlocked */
		if (time_after(jiffies, jiffies_expire))
			return -EBUSY;

		msleep(15);
		if (udev->state == USB_STATE_NOTATTACHED)
			return -ENODEV;
		if (udev->state == USB_STATE_SUSPENDED)
			return -EHOSTUNREACH;
		if (iface && iface->condition != USB_INTERFACE_BOUND)
			return -EINTR;
	}
	return 1;
}


static struct usb_device *match_device(struct usb_device *dev,
				       u16 vendor_id, u16 product_id)
{
	struct usb_device *ret_dev = NULL;
	int child;

	dev_dbg(&dev->dev, "check for vendor %04x, product %04x ...\n",
	    le16_to_cpu(dev->descriptor.idVendor),
	    le16_to_cpu(dev->descriptor.idProduct));

	/* see if this device matches */
	if ((vendor_id == le16_to_cpu(dev->descriptor.idVendor)) &&
	    (product_id == le16_to_cpu(dev->descriptor.idProduct))) {
		dev_dbg (&dev->dev, "matched this device!\n");
		ret_dev = usb_get_dev(dev);
		goto exit;
	}

	/* look through all of the children of this device */
	for (child = 0; child < dev->maxchild; ++child) {
		if (dev->children[child]) {
			usb_lock_device(dev->children[child]);
			ret_dev = match_device(dev->children[child],
					       vendor_id, product_id);
			usb_unlock_device(dev->children[child]);
			if (ret_dev)
				goto exit;
		}
	}
exit:
	return ret_dev;
}

/**
 * usb_find_device - find a specific usb device in the system
 * @vendor_id: the vendor id of the device to find
 * @product_id: the product id of the device to find
 *
 * Returns a pointer to a struct usb_device if such a specified usb
 * device is present in the system currently.  The usage count of the
 * device will be incremented if a device is found.  Make sure to call
 * usb_put_dev() when the caller is finished with the device.
 *
 * If a device with the specified vendor and product id is not found,
 * NULL is returned.
 */
struct usb_device *usb_find_device(u16 vendor_id, u16 product_id)
{
	struct list_head *buslist;
	struct usb_bus *bus;
	struct usb_device *dev = NULL;
	
	down(&usb_bus_list_lock);
	for (buslist = usb_bus_list.next;
	     buslist != &usb_bus_list; 
	     buslist = buslist->next) {
		bus = container_of(buslist, struct usb_bus, bus_list);
		if (!bus->root_hub)
			continue;
		usb_lock_device(bus->root_hub);
		dev = match_device(bus->root_hub, vendor_id, product_id);
		usb_unlock_device(bus->root_hub);
		if (dev)
			goto exit;
	}
exit:
	up(&usb_bus_list_lock);
	return dev;
}

/**
 * usb_get_current_frame_number - return current bus frame number
 * @dev: the device whose bus is being queried
 *
 * Returns the current frame number for the USB host controller
 * used with the given USB device.  This can be used when scheduling
 * isochronous requests.
 *
 * Note that different kinds of host controller have different
 * "scheduling horizons".  While one type might support scheduling only
 * 32 frames into the future, others could support scheduling up to
 * 1024 frames into the future.
 */
int usb_get_current_frame_number(struct usb_device *dev)
{
	return dev->bus->op->get_frame_number (dev);
}

/*-------------------------------------------------------------------*/
/*
 * __usb_get_extra_descriptor() finds a descriptor of specific type in the
 * extra field of the interface and endpoint descriptor structs.
 */

int __usb_get_extra_descriptor(char *buffer, unsigned size,
	unsigned char type, void **ptr)
{
	struct usb_descriptor_header *header;

	while (size >= sizeof(struct usb_descriptor_header)) {
		header = (struct usb_descriptor_header *)buffer;

		if (header->bLength < 2) {
			printk(KERN_ERR
				"%s: bogus descriptor, type %d length %d\n",
				usbcore_name,
				header->bDescriptorType, 
				header->bLength);
			return -1;
		}

		if (header->bDescriptorType == type) {
			*ptr = header;
			return 0;
		}

		buffer += header->bLength;
		size -= header->bLength;
	}
	return -1;
}

/**
 * usb_buffer_alloc - allocate dma-consistent buffer for URB_NO_xxx_DMA_MAP
 * @dev: device the buffer will be used with
 * @size: requested buffer size
 * @mem_flags: affect whether allocation may block
 * @dma: used to return DMA address of buffer
 *
 * Return value is either null (indicating no buffer could be allocated), or
 * the cpu-space pointer to a buffer that may be used to perform DMA to the
 * specified device.  Such cpu-space buffers are returned along with the DMA
 * address (through the pointer provided).
 *
 * These buffers are used with URB_NO_xxx_DMA_MAP set in urb->transfer_flags
 * to avoid behaviors like using "DMA bounce buffers", or tying down I/O
 * mapping hardware for long idle periods.  The implementation varies between
 * platforms, depending on details of how DMA will work to this device.
 * Using these buffers also helps prevent cacheline sharing problems on
 * architectures where CPU caches are not DMA-coherent.
 *
 * When the buffer is no longer used, free it with usb_buffer_free().
 */
void *usb_buffer_alloc (
	struct usb_device *dev,
	size_t size,
	gfp_t mem_flags,
	dma_addr_t *dma
)
{
	if (!dev || !dev->bus || !dev->bus->op || !dev->bus->op->buffer_alloc)
		return NULL;
	return dev->bus->op->buffer_alloc (dev->bus, size, mem_flags, dma);
}

/**
 * usb_buffer_free - free memory allocated with usb_buffer_alloc()
 * @dev: device the buffer was used with
 * @size: requested buffer size
 * @addr: CPU address of buffer
 * @dma: DMA address of buffer
 *
 * This reclaims an I/O buffer, letting it be reused.  The memory must have
 * been allocated using usb_buffer_alloc(), and the parameters must match
 * those provided in that allocation request. 
 */
void usb_buffer_free (
	struct usb_device *dev,
	size_t size,
	void *addr,
	dma_addr_t dma
)
{
	if (!dev || !dev->bus || !dev->bus->op || !dev->bus->op->buffer_free)
	    	return;
	dev->bus->op->buffer_free (dev->bus, size, addr, dma);
}

/**
 * usb_buffer_map - create DMA mapping(s) for an urb
 * @urb: urb whose transfer_buffer/setup_packet will be mapped
 *
 * Return value is either null (indicating no buffer could be mapped), or
 * the parameter.  URB_NO_TRANSFER_DMA_MAP and URB_NO_SETUP_DMA_MAP are
 * added to urb->transfer_flags if the operation succeeds.  If the device
 * is connected to this system through a non-DMA controller, this operation
 * always succeeds.
 *
 * This call would normally be used for an urb which is reused, perhaps
 * as the target of a large periodic transfer, with usb_buffer_dmasync()
 * calls to synchronize memory and dma state.
 *
 * Reverse the effect of this call with usb_buffer_unmap().
 */
#if 0
struct urb *usb_buffer_map (struct urb *urb)
{
	struct usb_bus		*bus;
	struct device		*controller;

	if (!urb
			|| !urb->dev
			|| !(bus = urb->dev->bus)
			|| !(controller = bus->controller))
		return NULL;

	if (controller->dma_mask) {
		urb->transfer_dma = dma_map_single (controller,
			urb->transfer_buffer, urb->transfer_buffer_length,
			usb_pipein (urb->pipe)
				? DMA_FROM_DEVICE : DMA_TO_DEVICE);
		if (usb_pipecontrol (urb->pipe))
			urb->setup_dma = dma_map_single (controller,
					urb->setup_packet,
					sizeof (struct usb_ctrlrequest),
					DMA_TO_DEVICE);
	// FIXME generic api broken like pci, can't report errors
	// if (urb->transfer_dma == DMA_ADDR_INVALID) return 0;
	} else
		urb->transfer_dma = ~0;
	urb->transfer_flags |= (URB_NO_TRANSFER_DMA_MAP
				| URB_NO_SETUP_DMA_MAP);
	return urb;
}
#endif  /*  0  */

/* XXX DISABLED, no users currently.  If you wish to re-enable this
 * XXX please determine whether the sync is to transfer ownership of
 * XXX the buffer from device to cpu or vice verse, and thusly use the
 * XXX appropriate _for_{cpu,device}() method.  -DaveM
 */
#if 0

/**
 * usb_buffer_dmasync - synchronize DMA and CPU view of buffer(s)
 * @urb: urb whose transfer_buffer/setup_packet will be synchronized
 */
void usb_buffer_dmasync (struct urb *urb)
{
	struct usb_bus		*bus;
	struct device		*controller;

	if (!urb
			|| !(urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP)
			|| !urb->dev
			|| !(bus = urb->dev->bus)
			|| !(controller = bus->controller))
		return;

	if (controller->dma_mask) {
		dma_sync_single (controller,
			urb->transfer_dma, urb->transfer_buffer_length,
			usb_pipein (urb->pipe)
				? DMA_FROM_DEVICE : DMA_TO_DEVICE);
		if (usb_pipecontrol (urb->pipe))
			dma_sync_single (controller,
					urb->setup_dma,
					sizeof (struct usb_ctrlrequest),
					DMA_TO_DEVICE);
	}
}
#endif

/**
 * usb_buffer_unmap - free DMA mapping(s) for an urb
 * @urb: urb whose transfer_buffer will be unmapped
 *
 * Reverses the effect of usb_buffer_map().
 */
#if 0
void usb_buffer_unmap (struct urb *urb)
{
	struct usb_bus		*bus;
	struct device		*controller;

	if (!urb
			|| !(urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP)
			|| !urb->dev
			|| !(bus = urb->dev->bus)
			|| !(controller = bus->controller))
		return;

	if (controller->dma_mask) {
		dma_unmap_single (controller,
			urb->transfer_dma, urb->transfer_buffer_length,
			usb_pipein (urb->pipe)
				? DMA_FROM_DEVICE : DMA_TO_DEVICE);
		if (usb_pipecontrol (urb->pipe))
			dma_unmap_single (controller,
					urb->setup_dma,
					sizeof (struct usb_ctrlrequest),
					DMA_TO_DEVICE);
	}
	urb->transfer_flags &= ~(URB_NO_TRANSFER_DMA_MAP
				| URB_NO_SETUP_DMA_MAP);
}
#endif  /*  0  */

/**
 * usb_buffer_map_sg - create scatterlist DMA mapping(s) for an endpoint
 * @dev: device to which the scatterlist will be mapped
 * @pipe: endpoint defining the mapping direction
 * @sg: the scatterlist to map
 * @nents: the number of entries in the scatterlist
 *
 * Return value is either < 0 (indicating no buffers could be mapped), or
 * the number of DMA mapping array entries in the scatterlist.
 *
 * The caller is responsible for placing the resulting DMA addresses from
 * the scatterlist into URB transfer buffer pointers, and for setting the
 * URB_NO_TRANSFER_DMA_MAP transfer flag in each of those URBs.
 *
 * Top I/O rates come from queuing URBs, instead of waiting for each one
 * to complete before starting the next I/O.   This is particularly easy
 * to do with scatterlists.  Just allocate and submit one URB for each DMA
 * mapping entry returned, stopping on the first error or when all succeed.
 * Better yet, use the usb_sg_*() calls, which do that (and more) for you.
 *
 * This call would normally be used when translating scatterlist requests,
 * rather than usb_buffer_map(), since on some hardware (with IOMMUs) it
 * may be able to coalesce mappings for improved I/O efficiency.
 *
 * Reverse the effect of this call with usb_buffer_unmap_sg().
 */
int usb_buffer_map_sg (struct usb_device *dev, unsigned pipe,
		struct scatterlist *sg, int nents)
{
	struct usb_bus		*bus;
	struct device		*controller;

	if (!dev
			|| usb_pipecontrol (pipe)
			|| !(bus = dev->bus)
			|| !(controller = bus->controller)
			|| !controller->dma_mask)
		return -1;

	// FIXME generic api broken like pci, can't report errors
	return dma_map_sg (controller, sg, nents,
			usb_pipein (pipe) ? DMA_FROM_DEVICE : DMA_TO_DEVICE);
}

/* XXX DISABLED, no users currently.  If you wish to re-enable this
 * XXX please determine whether the sync is to transfer ownership of
 * XXX the buffer from device to cpu or vice verse, and thusly use the
 * XXX appropriate _for_{cpu,device}() method.  -DaveM
 */
#if 0

/**
 * usb_buffer_dmasync_sg - synchronize DMA and CPU view of scatterlist buffer(s)
 * @dev: device to which the scatterlist will be mapped
 * @pipe: endpoint defining the mapping direction
 * @sg: the scatterlist to synchronize
 * @n_hw_ents: the positive return value from usb_buffer_map_sg
 *
 * Use this when you are re-using a scatterlist's data buffers for
 * another USB request.
 */
void usb_buffer_dmasync_sg (struct usb_device *dev, unsigned pipe,
		struct scatterlist *sg, int n_hw_ents)
{
	struct usb_bus		*bus;
	struct device		*controller;

	if (!dev
			|| !(bus = dev->bus)
			|| !(controller = bus->controller)
			|| !controller->dma_mask)
		return;

	dma_sync_sg (controller, sg, n_hw_ents,
			usb_pipein (pipe) ? DMA_FROM_DEVICE : DMA_TO_DEVICE);
}
#endif

/**
 * usb_buffer_unmap_sg - free DMA mapping(s) for a scatterlist
 * @dev: device to which the scatterlist will be mapped
 * @pipe: endpoint defining the mapping direction
 * @sg: the scatterlist to unmap
 * @n_hw_ents: the positive return value from usb_buffer_map_sg
 *
 * Reverses the effect of usb_buffer_map_sg().
 */
void usb_buffer_unmap_sg (struct usb_device *dev, unsigned pipe,
		struct scatterlist *sg, int n_hw_ents)
{
	struct usb_bus		*bus;
	struct device		*controller;

	if (!dev
			|| !(bus = dev->bus)
			|| !(controller = bus->controller)
			|| !controller->dma_mask)
		return;

	dma_unmap_sg (controller, sg, n_hw_ents,
			usb_pipein (pipe) ? DMA_FROM_DEVICE : DMA_TO_DEVICE);
}

static int verify_suspended(struct device *dev, void *unused)
{
	return (dev->power.power_state.event == PM_EVENT_ON) ? -EBUSY : 0;
}

static int usb_generic_suspend(struct device *dev, pm_message_t message)
{
	struct usb_interface	*intf;
	struct usb_driver	*driver;
	int			status;

	/* USB devices enter SUSPEND state through their hubs, but can be
	 * marked for FREEZE as soon as their children are already idled.
	 * But those semantics are useless, so we equate the two (sigh).
	 */
	if (dev->driver == &usb_generic_driver) {
		if (dev->power.power_state.event == message.event)
			return 0;
		/* we need to rule out bogus requests through sysfs */
		status = device_for_each_child(dev, NULL, verify_suspended);
		if (status)
			return status;
 		return usb_suspend_device (to_usb_device(dev));
	}

	if ((dev->driver == NULL) ||
	    (dev->driver_data == &usb_generic_driver_data))
		return 0;

	intf = to_usb_interface(dev);
	driver = to_usb_driver(dev->driver);

	/* with no hardware, USB interfaces only use FREEZE and ON states */
	if (!is_active(intf))
		return 0;

	if (driver->suspend && driver->resume) {
		status = driver->suspend(intf, message);
		if (status)
			dev_err(dev, "%s error %d\n", "suspend", status);
		else
			mark_quiesced(intf);
	} else {
		// FIXME else if there's no suspend method, disconnect...
		dev_warn(dev, "no suspend for driver %s?\n", driver->name);
		mark_quiesced(intf);
		status = 0;
	}
	return status;
}

static int usb_generic_resume(struct device *dev)
{
	struct usb_interface	*intf;
	struct usb_driver	*driver;
	struct usb_device	*udev;
	int			status;

	if (dev->power.power_state.event == PM_EVENT_ON)
		return 0;

	/* mark things as "on" immediately, no matter what errors crop up */
	dev->power.power_state.event = PM_EVENT_ON;

	/* devices resume through their hubs */
	if (dev->driver == &usb_generic_driver) {
		udev = to_usb_device(dev);
		if (udev->state == USB_STATE_NOTATTACHED)
			return 0;
		return usb_resume_device (to_usb_device(dev));
	}

	if ((dev->driver == NULL) ||
	    (dev->driver_data == &usb_generic_driver_data)) {
		dev->power.power_state.event = PM_EVENT_FREEZE;
		return 0;
	}

	intf = to_usb_interface(dev);
	driver = to_usb_driver(dev->driver);

	udev = interface_to_usbdev(intf);
	if (udev->state == USB_STATE_NOTATTACHED)
		return 0;

	/* if driver was suspended, it has a resume method;
	 * however, sysfs can wrongly mark things as suspended
	 * (on the "no suspend method" FIXME path above)
	 */
	if (driver->resume) {
		status = driver->resume(intf);
		if (status) {
			dev_err(dev, "%s error %d\n", "resume", status);
			mark_quiesced(intf);
		}
	} else
		dev_warn(dev, "no resume for driver %s?\n", driver->name);
	return 0;
}

struct bus_type usb_bus_type = {
	.name =		"usb",
	.match =	usb_device_match,
	.uevent =	usb_uevent,
	.suspend =	usb_generic_suspend,
	.resume =	usb_generic_resume,
};

/* format to disable USB on kernel command line is: nousb */
__module_param_call("", nousb, param_set_bool, param_get_bool, &nousb, 0444);

/*
 * for external read access to <nousb>
 */
int usb_disabled(void)
{
	return nousb;
}

/*
 * Init
 */
static int __init usb_init(void)
{
	int retval;
	if (nousb) {
		pr_info ("%s: USB support disabled\n", usbcore_name);
		return 0;
	}

	retval = bus_register(&usb_bus_type);
	if (retval) 
		goto out;
	retval = usb_host_init();
	if (retval)
		goto host_init_failed;
	retval = usb_major_init();
	if (retval)
		goto major_init_failed;
	retval = usb_register(&usbfs_driver);
	if (retval)
		goto driver_register_failed;
	retval = usbdev_init();
	if (retval)
		goto usbdevice_init_failed;
	retval = usbfs_init();
	if (retval)
		goto fs_init_failed;
	retval = usb_hub_init();
	if (retval)
		goto hub_init_failed;
	retval = driver_register(&usb_generic_driver);
	if (!retval)
		goto out;

	usb_hub_cleanup();
hub_init_failed:
	usbfs_cleanup();
fs_init_failed:
	usbdev_cleanup();
usbdevice_init_failed:
	usb_deregister(&usbfs_driver);
driver_register_failed:
	usb_major_cleanup();
major_init_failed:
	usb_host_cleanup();
host_init_failed:
	bus_unregister(&usb_bus_type);
out:
	return retval;
}

/*
 * Cleanup
 */
static void __exit usb_exit(void)
{
	/* This will matter if shutdown/reboot does exitcalls. */
	if (nousb)
		return;

	driver_unregister(&usb_generic_driver);
	usb_major_cleanup();
	usbfs_cleanup();
	usb_deregister(&usbfs_driver);
	usbdev_cleanup();
	usb_hub_cleanup();
	usb_host_cleanup();
	bus_unregister(&usb_bus_type);
}

subsys_initcall(usb_init);
module_exit(usb_exit);

/*
 * USB may be built into the kernel or be built as modules.
 * These symbols are exported for device (or host controller)
 * driver modules to use.
 */

EXPORT_SYMBOL(usb_disabled);

EXPORT_SYMBOL_GPL(usb_get_intf);
EXPORT_SYMBOL_GPL(usb_put_intf);

EXPORT_SYMBOL(usb_alloc_dev);
EXPORT_SYMBOL(usb_put_dev);
EXPORT_SYMBOL(usb_get_dev);
EXPORT_SYMBOL(usb_hub_tt_clear_buffer);

EXPORT_SYMBOL(usb_lock_device_for_reset);

EXPORT_SYMBOL(usb_driver_claim_interface);
EXPORT_SYMBOL(usb_driver_release_interface);
EXPORT_SYMBOL(usb_find_interface);
EXPORT_SYMBOL(usb_ifnum_to_if);
EXPORT_SYMBOL(usb_altnum_to_altsetting);

EXPORT_SYMBOL(usb_reset_device);
EXPORT_SYMBOL(usb_disconnect);

EXPORT_SYMBOL(__usb_get_extra_descriptor);

EXPORT_SYMBOL(usb_find_device);
EXPORT_SYMBOL(usb_get_current_frame_number);

EXPORT_SYMBOL (usb_buffer_alloc);
EXPORT_SYMBOL (usb_buffer_free);

#if 0
EXPORT_SYMBOL (usb_buffer_map);
EXPORT_SYMBOL (usb_buffer_dmasync);
EXPORT_SYMBOL (usb_buffer_unmap);
#endif

EXPORT_SYMBOL (usb_buffer_map_sg);
#if 0
EXPORT_SYMBOL (usb_buffer_dmasync_sg);
#endif
EXPORT_SYMBOL (usb_buffer_unmap_sg);

MODULE_LICENSE("GPL");
