/*
 * Copyright (C) 2012 Red Hat
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/module.h>
#include <drm/drm_usb.h>
#include <drm/drm_crtc_helper.h>
#include "udl_drv.h"

static struct drm_driver driver;

/*
 * There are many DisplayLink-based graphics products, all with unique PIDs.
 * So we match on DisplayLink's VID + Vendor-Defined Interface Class (0xff)
 * We also require a match on SubClass (0x00) and Protocol (0x00),
 * which is compatible with all known USB 2.0 era graphics chips and firmware,
 * but allows DisplayLink to increment those for any future incompatible chips
 */
static struct usb_device_id id_table[] = {
	{.idVendor = 0x17e9, .bInterfaceClass = 0xff,
	 .bInterfaceSubClass = 0x00,
	 .bInterfaceProtocol = 0x00,
	 .match_flags = USB_DEVICE_ID_MATCH_VENDOR |
			USB_DEVICE_ID_MATCH_INT_CLASS |
			USB_DEVICE_ID_MATCH_INT_SUBCLASS |
			USB_DEVICE_ID_MATCH_INT_PROTOCOL,},
	{},
};
MODULE_DEVICE_TABLE(usb, id_table);

MODULE_LICENSE("GPL");

static int udl_usb_probe(struct usb_interface *interface,
			 const struct usb_device_id *id)
{
	return drm_get_usb_dev(interface, id, &driver);
}

static void udl_usb_disconnect(struct usb_interface *interface)
{
	struct drm_device *dev = usb_get_intfdata(interface);

	drm_kms_helper_poll_disable(dev);
	drm_connector_unplug_all(dev);
	udl_fbdev_unplug(dev);
	udl_drop_usb(dev);
	drm_unplug_dev(dev);
}

static const struct vm_operations_struct udl_gem_vm_ops = {
	.fault = udl_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct file_operations udl_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.mmap = udl_drm_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.unlocked_ioctl	= drm_ioctl,
	.release = drm_release,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static struct drm_driver driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME,
	.load = udl_driver_load,
	.unload = udl_driver_unload,

	/* gem hooks */
	.gem_free_object = udl_gem_free_object,
	.gem_vm_ops = &udl_gem_vm_ops,

	.dumb_create = udl_dumb_create,
	.dumb_map_offset = udl_gem_mmap,
	.dumb_destroy = drm_gem_dumb_destroy,
	.fops = &udl_driver_fops,

	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_import = udl_gem_prime_import,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static struct usb_driver udl_driver = {
	.name = "udl",
	.probe = udl_usb_probe,
	.disconnect = udl_usb_disconnect,
	.id_table = id_table,
};

static int __init udl_init(void)
{
	return drm_usb_init(&driver, &udl_driver);
}

static void __exit udl_exit(void)
{
	drm_usb_exit(&driver, &udl_driver);
}

module_init(udl_init);
module_exit(udl_exit);
