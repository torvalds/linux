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
	drm_dev_free(dev);
	return ret;

}
EXPORT_SYMBOL(drm_get_usb_dev);

static int drm_usb_get_irq(struct drm_device *dev)
{
	return 0;
}

static const char *drm_usb_get_name(struct drm_device *dev)
{
	return "USB";
}

static int drm_usb_set_busid(struct drm_device *dev,
			       struct drm_master *master)
{
	return 0;
}

static struct drm_bus drm_usb_bus = {
	.bus_type = DRIVER_BUS_USB,
	.get_irq = drm_usb_get_irq,
	.get_name = drm_usb_get_name,
	.set_busid = drm_usb_set_busid,
};
    
int drm_usb_init(struct drm_driver *driver, struct usb_driver *udriver)
{
	int res;
	DRM_DEBUG("\n");

	driver->kdriver.usb = udriver;
	driver->bus = &drm_usb_bus;

	res = usb_register(udriver);
	return res;
}
EXPORT_SYMBOL(drm_usb_init);

void drm_usb_exit(struct drm_driver *driver,
		  struct usb_driver *udriver)
{
	usb_deregister(udriver);
}
EXPORT_SYMBOL(drm_usb_exit);

MODULE_AUTHOR("David Airlie");
MODULE_DESCRIPTION("USB DRM support");
MODULE_LICENSE("GPL and additional rights");
