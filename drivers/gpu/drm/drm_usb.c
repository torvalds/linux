#include <drm/drmP.h>
#include <drm/drm_usb.h>
#include <linux/usb.h>
#include <linux/module.h>

int drm_get_usb_dev(struct usb_interface *interface,
		    const struct usb_device_id *id,
		    struct drm_driver *driver)
{
	struct drm_device *dev;
	int ret;

	DRM_DEBUG("\n");

	dev = drm_dev_alloc(driver, &interface->dev);
	if (!dev)
		return -ENOMEM;

	dev->usbdev = interface_to_usbdev(interface);
	usb_set_intfdata(interface, dev);

	ret = drm_dev_register(dev, 0);
	if (ret)
		goto err_free;

	DRM_INFO("Initialized %s %d.%d.%d %s on minor %d\n",
		 driver->name, driver->major, driver->minor, driver->patchlevel,
		 driver->date, dev->primary->index);

	return 0;

err_free:
	drm_dev_unref(dev);
	return ret;

}
EXPORT_SYMBOL(drm_get_usb_dev);

static int drm_usb_set_busid(struct drm_device *dev,
			       struct drm_master *master)
{
	return 0;
}

static struct drm_bus drm_usb_bus = {
	.set_busid = drm_usb_set_busid,
};

/**
 * drm_usb_init - Register matching USB devices with the DRM subsystem
 * @driver: DRM device driver
 * @udriver: USB device driver
 *
 * Registers one or more devices matched by a USB driver with the DRM
 * subsystem.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_usb_init(struct drm_driver *driver, struct usb_driver *udriver)
{
	int res;
	DRM_DEBUG("\n");

	driver->bus = &drm_usb_bus;

	res = usb_register(udriver);
	return res;
}
EXPORT_SYMBOL(drm_usb_init);

/**
 * drm_usb_exit - Unregister matching USB devices from the DRM subsystem
 * @driver: DRM device driver
 * @udriver: USB device driver
 *
 * Unregisters one or more devices matched by a USB driver from the DRM
 * subsystem.
 */
void drm_usb_exit(struct drm_driver *driver,
		  struct usb_driver *udriver)
{
	usb_deregister(udriver);
}
EXPORT_SYMBOL(drm_usb_exit);

MODULE_AUTHOR("David Airlie");
MODULE_DESCRIPTION("USB DRM support");
MODULE_LICENSE("GPL and additional rights");
