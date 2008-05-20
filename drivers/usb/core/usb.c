/*
 * drivers/usb/core/usb.c
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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/interrupt.h>  /* for in_interrupt() */
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include <asm/io.h>
#include <linux/scatterlist.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>

#include "hcd.h"
#include "usb.h"


const char *usbcore_name = "usbcore";

static int nousb;	/* Disable USB when built into kernel image */

/* Workqueue for autosuspend and for remote wakeup of root hubs */
struct workqueue_struct *ksuspend_usb_wq;

#ifdef	CONFIG_USB_SUSPEND
static int usb_autosuspend_delay = 2;		/* Default delay value,
						 * in seconds */
module_param_named(autosuspend, usb_autosuspend_delay, int, 0644);
MODULE_PARM_DESC(autosuspend, "default autosuspend delay");

#else
#define usb_autosuspend_delay		0
#endif


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
struct usb_interface *usb_ifnum_to_if(const struct usb_device *dev,
				      unsigned ifnum)
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
EXPORT_SYMBOL_GPL(usb_ifnum_to_if);

/**
 * usb_altnum_to_altsetting - get the altsetting structure with a given alternate setting number.
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
struct usb_host_interface *usb_altnum_to_altsetting(
					const struct usb_interface *intf,
					unsigned int altnum)
{
	int i;

	for (i = 0; i < intf->num_altsetting; i++) {
		if (intf->altsetting[i].desc.bAlternateSetting == altnum)
			return &intf->altsetting[i];
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(usb_altnum_to_altsetting);

struct find_interface_arg {
	int minor;
	struct usb_interface *interface;
};

static int __find_interface(struct device *dev, void *data)
{
	struct find_interface_arg *arg = data;
	struct usb_interface *intf;

	/* can't look at usb devices, only interfaces */
	if (is_usb_device(dev))
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
	int retval;

	argb.minor = minor;
	argb.interface = NULL;
	/* eat the error, it will be in argb.interface */
	retval = driver_for_each_device(&drv->drvwrap.driver, NULL, &argb,
					__find_interface);
	return argb.interface;
}
EXPORT_SYMBOL_GPL(usb_find_interface);

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
	usb_put_hcd(bus_to_hcd(udev->bus));
	kfree(udev->product);
	kfree(udev->manufacturer);
	kfree(udev->serial);
	kfree(udev);
}

#ifdef	CONFIG_HOTPLUG
static int usb_dev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct usb_device *usb_dev;

	usb_dev = to_usb_device(dev);

	if (add_uevent_var(env, "BUSNUM=%03d", usb_dev->bus->busnum))
		return -ENOMEM;

	if (add_uevent_var(env, "DEVNUM=%03d", usb_dev->devnum))
		return -ENOMEM;

	return 0;
}

#else

static int usb_dev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	return -ENODEV;
}
#endif	/* CONFIG_HOTPLUG */

struct device_type usb_device_type = {
	.name =		"usb_device",
	.release =	usb_release_dev,
	.uevent =	usb_dev_uevent,
};

#ifdef	CONFIG_PM

static int ksuspend_usb_init(void)
{
	/* This workqueue is supposed to be both freezable and
	 * singlethreaded.  Its job doesn't justify running on more
	 * than one CPU.
	 */
	ksuspend_usb_wq = create_freezeable_workqueue("ksuspend_usbd");
	if (!ksuspend_usb_wq)
		return -ENOMEM;
	return 0;
}

static void ksuspend_usb_cleanup(void)
{
	destroy_workqueue(ksuspend_usb_wq);
}

#else

#define ksuspend_usb_init()	0
#define ksuspend_usb_cleanup()	do {} while (0)

#endif	/* CONFIG_PM */


/* Returns 1 if @usb_bus is WUSB, 0 otherwise */
static unsigned usb_bus_is_wusb(struct usb_bus *bus)
{
	struct usb_hcd *hcd = container_of(bus, struct usb_hcd, self);
	return hcd->wireless;
}


/**
 * usb_alloc_dev - usb device constructor (usbcore-internal)
 * @parent: hub to which device is connected; null to allocate a root hub
 * @bus: bus used to access the device
 * @port1: one-based index of port; ignored for root hubs
 * Context: !in_interrupt()
 *
 * Only hub drivers (including virtual root hub drivers for host
 * controllers) should ever call this.
 *
 * This call may not be used in a non-sleeping context.
 */
struct usb_device *usb_alloc_dev(struct usb_device *parent,
				 struct usb_bus *bus, unsigned port1)
{
	struct usb_device *dev;
	struct usb_hcd *usb_hcd = container_of(bus, struct usb_hcd, self);
	unsigned root_hub = 0;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	if (!usb_get_hcd(bus_to_hcd(bus))) {
		kfree(dev);
		return NULL;
	}

	device_initialize(&dev->dev);
	dev->dev.bus = &usb_bus_type;
	dev->dev.type = &usb_device_type;
	dev->dev.groups = usb_device_groups;
	dev->dev.dma_mask = bus->controller->dma_mask;
	set_dev_node(&dev->dev, dev_to_node(bus->controller));
	dev->state = USB_STATE_ATTACHED;
	atomic_set(&dev->urbnum, 0);

	INIT_LIST_HEAD(&dev->ep0.urb_list);
	dev->ep0.desc.bLength = USB_DT_ENDPOINT_SIZE;
	dev->ep0.desc.bDescriptorType = USB_DT_ENDPOINT;
	/* ep0 maxpacket comes later, from device descriptor */
	usb_enable_endpoint(dev, &dev->ep0);
	dev->can_submit = 1;

	/* Save readable and stable topology id, distinguishing devices
	 * by location for diagnostics, tools, driver model, etc.  The
	 * string is a path along hub ports, from the root.  Each device's
	 * dev->devpath will be stable until USB is re-cabled, and hubs
	 * are often labeled with these port numbers.  The bus_id isn't
	 * as stable:  bus->busnum changes easily from modprobe order,
	 * cardbus or pci hotplugging, and so on.
	 */
	if (unlikely(!parent)) {
		dev->devpath[0] = '0';

		dev->dev.parent = bus->controller;
		sprintf(&dev->dev.bus_id[0], "usb%d", bus->busnum);
		root_hub = 1;
	} else {
		/* match any labeling on the hubs; it's one-based */
		if (parent->devpath[0] == '0')
			snprintf(dev->devpath, sizeof dev->devpath,
				"%d", port1);
		else
			snprintf(dev->devpath, sizeof dev->devpath,
				"%s.%d", parent->devpath, port1);

		dev->dev.parent = &parent->dev;
		sprintf(&dev->dev.bus_id[0], "%d-%s",
			bus->busnum, dev->devpath);

		/* hub driver sets up TT records */
	}

	dev->portnum = port1;
	dev->bus = bus;
	dev->parent = parent;
	INIT_LIST_HEAD(&dev->filelist);

#ifdef	CONFIG_PM
	mutex_init(&dev->pm_mutex);
	INIT_DELAYED_WORK(&dev->autosuspend, usb_autosuspend_work);
	dev->autosuspend_delay = usb_autosuspend_delay * HZ;
	dev->connect_time = jiffies;
	dev->active_duration = -jiffies;
#endif
	if (root_hub)	/* Root hub always ok [and always wired] */
		dev->authorized = 1;
	else {
		dev->authorized = usb_hcd->authorized_default;
		dev->wusb = usb_bus_is_wusb(bus)? 1 : 0;
	}
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
EXPORT_SYMBOL_GPL(usb_get_dev);

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
EXPORT_SYMBOL_GPL(usb_put_dev);

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
EXPORT_SYMBOL_GPL(usb_get_intf);

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
EXPORT_SYMBOL_GPL(usb_put_intf);

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
 * usb_lock_device_for_reset - cautiously acquire the lock for a usb device structure
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
			      const struct usb_interface *iface)
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
EXPORT_SYMBOL_GPL(usb_lock_device_for_reset);

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
		dev_dbg(&dev->dev, "matched this device!\n");
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

	mutex_lock(&usb_bus_list_lock);
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
	mutex_unlock(&usb_bus_list_lock);
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
	return usb_hcd_get_frame_number(dev);
}
EXPORT_SYMBOL_GPL(usb_get_current_frame_number);

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
EXPORT_SYMBOL_GPL(__usb_get_extra_descriptor);

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
 * to avoid behaviors like using "DMA bounce buffers", or thrashing IOMMU
 * hardware during URB completion/resubmit.  The implementation varies between
 * platforms, depending on details of how DMA will work to this device.
 * Using these buffers also eliminates cacheline sharing problems on
 * architectures where CPU caches are not DMA-coherent.  On systems without
 * bus-snooping caches, these buffers are uncached.
 *
 * When the buffer is no longer used, free it with usb_buffer_free().
 */
void *usb_buffer_alloc(struct usb_device *dev, size_t size, gfp_t mem_flags,
		       dma_addr_t *dma)
{
	if (!dev || !dev->bus)
		return NULL;
	return hcd_buffer_alloc(dev->bus, size, mem_flags, dma);
}
EXPORT_SYMBOL_GPL(usb_buffer_alloc);

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
void usb_buffer_free(struct usb_device *dev, size_t size, void *addr,
		     dma_addr_t dma)
{
	if (!dev || !dev->bus)
		return;
	if (!addr)
		return;
	hcd_buffer_free(dev->bus, size, addr, dma);
}
EXPORT_SYMBOL_GPL(usb_buffer_free);

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
struct urb *usb_buffer_map(struct urb *urb)
{
	struct usb_bus		*bus;
	struct device		*controller;

	if (!urb
			|| !urb->dev
			|| !(bus = urb->dev->bus)
			|| !(controller = bus->controller))
		return NULL;

	if (controller->dma_mask) {
		urb->transfer_dma = dma_map_single(controller,
			urb->transfer_buffer, urb->transfer_buffer_length,
			usb_pipein(urb->pipe)
				? DMA_FROM_DEVICE : DMA_TO_DEVICE);
		if (usb_pipecontrol(urb->pipe))
			urb->setup_dma = dma_map_single(controller,
					urb->setup_packet,
					sizeof(struct usb_ctrlrequest),
					DMA_TO_DEVICE);
	/* FIXME generic api broken like pci, can't report errors */
	/* if (urb->transfer_dma == DMA_ADDR_INVALID) return 0; */
	} else
		urb->transfer_dma = ~0;
	urb->transfer_flags |= (URB_NO_TRANSFER_DMA_MAP
				| URB_NO_SETUP_DMA_MAP);
	return urb;
}
EXPORT_SYMBOL_GPL(usb_buffer_map);
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
void usb_buffer_dmasync(struct urb *urb)
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
		dma_sync_single(controller,
			urb->transfer_dma, urb->transfer_buffer_length,
			usb_pipein(urb->pipe)
				? DMA_FROM_DEVICE : DMA_TO_DEVICE);
		if (usb_pipecontrol(urb->pipe))
			dma_sync_single(controller,
					urb->setup_dma,
					sizeof(struct usb_ctrlrequest),
					DMA_TO_DEVICE);
	}
}
EXPORT_SYMBOL_GPL(usb_buffer_dmasync);
#endif

/**
 * usb_buffer_unmap - free DMA mapping(s) for an urb
 * @urb: urb whose transfer_buffer will be unmapped
 *
 * Reverses the effect of usb_buffer_map().
 */
#if 0
void usb_buffer_unmap(struct urb *urb)
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
		dma_unmap_single(controller,
			urb->transfer_dma, urb->transfer_buffer_length,
			usb_pipein(urb->pipe)
				? DMA_FROM_DEVICE : DMA_TO_DEVICE);
		if (usb_pipecontrol(urb->pipe))
			dma_unmap_single(controller,
					urb->setup_dma,
					sizeof(struct usb_ctrlrequest),
					DMA_TO_DEVICE);
	}
	urb->transfer_flags &= ~(URB_NO_TRANSFER_DMA_MAP
				| URB_NO_SETUP_DMA_MAP);
}
EXPORT_SYMBOL_GPL(usb_buffer_unmap);
#endif  /*  0  */

/**
 * usb_buffer_map_sg - create scatterlist DMA mapping(s) for an endpoint
 * @dev: device to which the scatterlist will be mapped
 * @is_in: mapping transfer direction
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
int usb_buffer_map_sg(const struct usb_device *dev, int is_in,
		      struct scatterlist *sg, int nents)
{
	struct usb_bus		*bus;
	struct device		*controller;

	if (!dev
			|| !(bus = dev->bus)
			|| !(controller = bus->controller)
			|| !controller->dma_mask)
		return -1;

	/* FIXME generic api broken like pci, can't report errors */
	return dma_map_sg(controller, sg, nents,
			is_in ? DMA_FROM_DEVICE : DMA_TO_DEVICE);
}
EXPORT_SYMBOL_GPL(usb_buffer_map_sg);

/* XXX DISABLED, no users currently.  If you wish to re-enable this
 * XXX please determine whether the sync is to transfer ownership of
 * XXX the buffer from device to cpu or vice verse, and thusly use the
 * XXX appropriate _for_{cpu,device}() method.  -DaveM
 */
#if 0

/**
 * usb_buffer_dmasync_sg - synchronize DMA and CPU view of scatterlist buffer(s)
 * @dev: device to which the scatterlist will be mapped
 * @is_in: mapping transfer direction
 * @sg: the scatterlist to synchronize
 * @n_hw_ents: the positive return value from usb_buffer_map_sg
 *
 * Use this when you are re-using a scatterlist's data buffers for
 * another USB request.
 */
void usb_buffer_dmasync_sg(const struct usb_device *dev, int is_in,
			   struct scatterlist *sg, int n_hw_ents)
{
	struct usb_bus		*bus;
	struct device		*controller;

	if (!dev
			|| !(bus = dev->bus)
			|| !(controller = bus->controller)
			|| !controller->dma_mask)
		return;

	dma_sync_sg(controller, sg, n_hw_ents,
			is_in ? DMA_FROM_DEVICE : DMA_TO_DEVICE);
}
EXPORT_SYMBOL_GPL(usb_buffer_dmasync_sg);
#endif

/**
 * usb_buffer_unmap_sg - free DMA mapping(s) for a scatterlist
 * @dev: device to which the scatterlist will be mapped
 * @is_in: mapping transfer direction
 * @sg: the scatterlist to unmap
 * @n_hw_ents: the positive return value from usb_buffer_map_sg
 *
 * Reverses the effect of usb_buffer_map_sg().
 */
void usb_buffer_unmap_sg(const struct usb_device *dev, int is_in,
			 struct scatterlist *sg, int n_hw_ents)
{
	struct usb_bus		*bus;
	struct device		*controller;

	if (!dev
			|| !(bus = dev->bus)
			|| !(controller = bus->controller)
			|| !controller->dma_mask)
		return;

	dma_unmap_sg(controller, sg, n_hw_ents,
			is_in ? DMA_FROM_DEVICE : DMA_TO_DEVICE);
}
EXPORT_SYMBOL_GPL(usb_buffer_unmap_sg);

/* format to disable USB on kernel command line is: nousb */
__module_param_call("", nousb, param_set_bool, param_get_bool, &nousb, 0444);

/*
 * for external read access to <nousb>
 */
int usb_disabled(void)
{
	return nousb;
}
EXPORT_SYMBOL_GPL(usb_disabled);

/*
 * Init
 */
static int __init usb_init(void)
{
	int retval;
	if (nousb) {
		pr_info("%s: USB support disabled\n", usbcore_name);
		return 0;
	}

	retval = ksuspend_usb_init();
	if (retval)
		goto out;
	retval = bus_register(&usb_bus_type);
	if (retval)
		goto bus_register_failed;
	retval = usb_host_init();
	if (retval)
		goto host_init_failed;
	retval = usb_major_init();
	if (retval)
		goto major_init_failed;
	retval = usb_register(&usbfs_driver);
	if (retval)
		goto driver_register_failed;
	retval = usb_devio_init();
	if (retval)
		goto usb_devio_init_failed;
	retval = usbfs_init();
	if (retval)
		goto fs_init_failed;
	retval = usb_hub_init();
	if (retval)
		goto hub_init_failed;
	retval = usb_register_device_driver(&usb_generic_driver, THIS_MODULE);
	if (!retval)
		goto out;

	usb_hub_cleanup();
hub_init_failed:
	usbfs_cleanup();
fs_init_failed:
	usb_devio_cleanup();
usb_devio_init_failed:
	usb_deregister(&usbfs_driver);
driver_register_failed:
	usb_major_cleanup();
major_init_failed:
	usb_host_cleanup();
host_init_failed:
	bus_unregister(&usb_bus_type);
bus_register_failed:
	ksuspend_usb_cleanup();
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

	usb_deregister_device_driver(&usb_generic_driver);
	usb_major_cleanup();
	usbfs_cleanup();
	usb_deregister(&usbfs_driver);
	usb_devio_cleanup();
	usb_hub_cleanup();
	usb_host_cleanup();
	bus_unregister(&usb_bus_type);
	ksuspend_usb_cleanup();
}

subsys_initcall(usb_init);
module_exit(usb_exit);
MODULE_LICENSE("GPL");
