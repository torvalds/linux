#include "drmP.h"
#include <linux/usb.h>
#include <linux/module.h>

int drm_get_usb_dev(struct usb_interface *interface,
		    const struct usb_device_id *id,
		    struct drm_driver *driver)
{
	struct drm_device *dev;
	struct usb_device *usbdev;
	int ret;

	DRM_DEBUG("\n");

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	usbdev = interface_to_usbdev(interface);
	dev->usbdev = usbdev;
	dev->dev = &usbdev->dev;

	mutex_lock(&drm_global_mutex);

	ret = drm_fill_in_dev(dev, NULL, driver);
	if (ret) {
		printk(KERN_ERR "DRM: Fill_in_dev failed.\n");
		goto err_g1;
	}

	usb_set_intfdata(interface, dev);
	ret = drm_get_minor(dev, &dev->control, DRM_MINOR_CONTROL);
	if (ret)
		goto err_g1;

	ret = drm_get_minor(dev, &dev->primary, DRM_MINOR_LEGACY);
	if (ret)
		goto err_g2;

	if (dev->driver->load) {
		ret = dev->driver->load(dev, 0);
		if (ret)
			goto err_g3;
	}

	/* setup the grouping for the legacy output */
	ret = drm_mode_group_init_legacy_group(dev,
					       &dev->primary->mode_group);
	if (ret)
		goto err_g3;

	list_add_tail(&dev->driver_item, &driver->device_list);

	mutex_unlock(&drm_global_mutex);

	DRM_INFO("Initialized %s %d.%d.%d %s on minor %d\n",
		 driver->name, driver->major, driver->minor, driver->patchlevel,
		 driver->date, dev->primary->index);

	return 0;

err_g3:
	drm_put_minor(&dev->primary);
err_g2:
	drm_put_minor(&dev->control);
err_g1:
	kfree(dev);
	mutex_unlock(&drm_global_mutex);
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

	INIT_LIST_HEAD(&driver->device_list);
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
