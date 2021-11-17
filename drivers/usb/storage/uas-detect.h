/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include "usb.h"

static int uas_is_interface(struct usb_host_interface *intf)
{
	return (intf->desc.bInterfaceClass == USB_CLASS_MASS_STORAGE &&
		intf->desc.bInterfaceSubClass == USB_SC_SCSI &&
		intf->desc.bInterfaceProtocol == USB_PR_UAS);
}

static struct usb_host_interface *uas_find_uas_alt_setting(
		struct usb_interface *intf)
{
	int i;

	for (i = 0; i < intf->num_altsetting; i++) {
		struct usb_host_interface *alt = &intf->altsetting[i];

		if (uas_is_interface(alt))
			return alt;
	}

	return NULL;
}

static int uas_find_endpoints(struct usb_host_interface *alt,
			      struct usb_host_endpoint *eps[])
{
	struct usb_host_endpoint *endpoint = alt->endpoint;
	unsigned i, n_endpoints = alt->desc.bNumEndpoints;

	for (i = 0; i < n_endpoints; i++) {
		unsigned char *extra = endpoint[i].extra;
		int len = endpoint[i].extralen;
		while (len >= 3) {
			if (extra[1] == USB_DT_PIPE_USAGE) {
				unsigned pipe_id = extra[2];
				if (pipe_id > 0 && pipe_id < 5)
					eps[pipe_id - 1] = &endpoint[i];
				break;
			}
			len -= extra[0];
			extra += extra[0];
		}
	}

	if (!eps[0] || !eps[1] || !eps[2] || !eps[3])
		return -ENODEV;

	return 0;
}

static int uas_use_uas_driver(struct usb_interface *intf,
			      const struct usb_device_id *id,
			      unsigned long *flags_ret)
{
	struct usb_host_endpoint *eps[4] = { };
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_hcd *hcd = bus_to_hcd(udev->bus);
	unsigned long flags = id->driver_info;
	struct usb_host_interface *alt;
	int r;

	alt = uas_find_uas_alt_setting(intf);
	if (!alt)
		return 0;

	r = uas_find_endpoints(alt, eps);
	if (r < 0)
		return 0;

	/*
	 * ASMedia has a number of usb3 to sata bridge chips, at the time of
	 * this writing the following versions exist:
	 * ASM1051 - no uas support version
	 * ASM1051 - with broken (*) uas support
	 * ASM1053 - with working uas support, but problems with large xfers
	 * ASM1153 - with working uas support
	 *
	 * Devices with these chips re-use a number of device-ids over the
	 * entire line, so the device-id is useless to determine if we're
	 * dealing with an ASM1051 (which we want to avoid).
	 *
	 * The ASM1153 can be identified by config.MaxPower == 0,
	 * where as the ASM105x models have config.MaxPower == 36.
	 *
	 * Differentiating between the ASM1053 and ASM1051 is trickier, when
	 * connected over USB-3 we can look at the number of streams supported,
	 * ASM1051 supports 32 streams, where as early ASM1053 versions support
	 * 16 streams, newer ASM1053-s also support 32 streams, but have a
	 * different prod-id.
	 *
	 * (*) ASM1051 chips do work with UAS with some disks (with the
	 *     US_FL_NO_REPORT_OPCODES quirk), but are broken with other disks
	 */
	if (le16_to_cpu(udev->descriptor.idVendor) == 0x174c &&
			(le16_to_cpu(udev->descriptor.idProduct) == 0x5106 ||
			 le16_to_cpu(udev->descriptor.idProduct) == 0x55aa)) {
		if (udev->actconfig->desc.bMaxPower == 0) {
			/* ASM1153, do nothing */
		} else if (udev->speed < USB_SPEED_SUPER) {
			/* No streams info, assume ASM1051 */
			flags |= US_FL_IGNORE_UAS;
		} else if (usb_ss_max_streams(&eps[1]->ss_ep_comp) == 32) {
			/* Possibly an ASM1051, disable uas */
			flags |= US_FL_IGNORE_UAS;
		} else {
			/* ASM1053, these have issues with large transfers */
			flags |= US_FL_MAX_SECTORS_240;
		}
	}

	/* All Seagate disk enclosures have broken ATA pass-through support */
	if (le16_to_cpu(udev->descriptor.idVendor) == 0x0bc2)
		flags |= US_FL_NO_ATA_1X;

	usb_stor_adjust_quirks(udev, &flags);

	if (flags & US_FL_IGNORE_UAS) {
		dev_warn(&udev->dev,
			"UAS is ignored for this device, using usb-storage instead\n");
		return 0;
	}

	if (udev->bus->sg_tablesize == 0) {
		dev_warn(&udev->dev,
			"The driver for the USB controller %s does not support scatter-gather which is\n",
			hcd->driver->description);
		dev_warn(&udev->dev,
			"required by the UAS driver. Please try an other USB controller if you wish to use UAS.\n");
		return 0;
	}

	if (udev->speed >= USB_SPEED_SUPER && !hcd->can_do_streams) {
		dev_warn(&udev->dev,
			"USB controller %s does not support streams, which are required by the UAS driver.\n",
			hcd_to_bus(hcd)->bus_name);
		dev_warn(&udev->dev,
			"Please try an other USB controller if you wish to use UAS.\n");
		return 0;
	}

	if (flags_ret)
		*flags_ret = flags;

	return 1;
}
