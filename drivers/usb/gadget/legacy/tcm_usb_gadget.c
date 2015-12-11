/* Target based USB-Gadget
 *
 * UAS protocol handling, target callbacks, configfs handling,
 * BBB (USB Mass Storage Class Bulk-Only (BBB) and Transport protocol handling.
 *
 * Author: Sebastian Andrzej Siewior <bigeasy at linutronix dot de>
 * License: GPLv2 as published by FSF.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/configfs.h>
#include <linux/ctype.h>
#include <linux/usb/ch9.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>
#include <linux/usb/storage.h>
#include <scsi/scsi_tcq.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <asm/unaligned.h>

USB_GADGET_COMPOSITE_OPTIONS();

/* #include to be removed when new function registration interface is used  */
#include "../function/f_tcm.c"

#define UAS_VENDOR_ID	0x0525	/* NetChip */
#define UAS_PRODUCT_ID	0xa4a5	/* Linux-USB File-backed Storage Gadget */

static struct usb_device_descriptor usbg_device_desc = {
	.bLength =		sizeof(usbg_device_desc),
	.bDescriptorType =	USB_DT_DEVICE,
	.bcdUSB =		cpu_to_le16(0x0200),
	.bDeviceClass =		USB_CLASS_PER_INTERFACE,
	.idVendor =		cpu_to_le16(UAS_VENDOR_ID),
	.idProduct =		cpu_to_le16(UAS_PRODUCT_ID),
	.bNumConfigurations =   1,
};

#define USB_G_STR_CONFIG USB_GADGET_FIRST_AVAIL_IDX

static struct usb_string	usbg_us_strings[] = {
	[USB_GADGET_MANUFACTURER_IDX].s	= "Target Manufactor",
	[USB_GADGET_PRODUCT_IDX].s	= "Target Product",
	[USB_GADGET_SERIAL_IDX].s	= "000000000001",
	[USB_G_STR_CONFIG].s		= "default config",
	{ },
};

static struct usb_gadget_strings usbg_stringtab = {
	.language = 0x0409,
	.strings = usbg_us_strings,
};

static struct usb_gadget_strings *usbg_strings[] = {
	&usbg_stringtab,
	NULL,
};

static int guas_unbind(struct usb_composite_dev *cdev)
{
	return 0;
}

static struct usb_configuration usbg_config_driver = {
	.label                  = "Linux Target",
	.bConfigurationValue    = 1,
	.bmAttributes           = USB_CONFIG_ATT_SELFPOWER,
};

static int usb_target_bind(struct usb_composite_dev *cdev)
{
	int ret;

	ret = usb_string_ids_tab(cdev, usbg_us_strings);
	if (ret)
		return ret;

	usbg_device_desc.iManufacturer =
		usbg_us_strings[USB_GADGET_MANUFACTURER_IDX].id;
	usbg_device_desc.iProduct = usbg_us_strings[USB_GADGET_PRODUCT_IDX].id;
	usbg_device_desc.iSerialNumber =
		usbg_us_strings[USB_GADGET_SERIAL_IDX].id;
	usbg_config_driver.iConfiguration =
		usbg_us_strings[USB_G_STR_CONFIG].id;

	ret = usb_add_config(cdev, &usbg_config_driver,
			tcm_bind_config);
	if (ret)
		return ret;
	usb_composite_overwrite_options(cdev, &coverwrite);
	return 0;
}

static struct usb_composite_driver usbg_driver = {
	.name           = "g_target",
	.dev            = &usbg_device_desc,
	.strings        = usbg_strings,
	.max_speed      = USB_SPEED_SUPER,
	.bind		= usb_target_bind,
	.unbind         = guas_unbind,
};

static int usbg_attach(struct usbg_tpg *tpg)
{
	return usb_composite_probe(&usbg_driver);
}

static void usbg_detach(struct usbg_tpg *tpg)
{
	usb_composite_unregister(&usbg_driver);
}

static int __init usb_target_gadget_init(void)
{
	return target_register_template(&usbg_ops);
}
module_init(usb_target_gadget_init);

static void __exit usb_target_gadget_exit(void)
{
	target_unregister_template(&usbg_ops);
}
module_exit(usb_target_gadget_exit);

MODULE_AUTHOR("Sebastian Andrzej Siewior <bigeasy@linutronix.de>");
MODULE_DESCRIPTION("usb-gadget fabric");
MODULE_LICENSE("GPL v2");
