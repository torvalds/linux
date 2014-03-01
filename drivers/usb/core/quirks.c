/*
 * USB device quirk handling logic and table
 *
 * Copyright (c) 2007 Oliver Neukum
 * Copyright (c) 2007 Greg Kroah-Hartman <gregkh@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, version 2.
 *
 *
 */

#include <linux/usb.h>
#include <linux/usb/quirks.h>
#include <linux/usb/hcd.h>
#include "usb.h"

/* Lists of quirky USB devices, split in device quirks and interface quirks.
 * Device quirks are applied at the very beginning of the enumeration process,
 * right after reading the device descriptor. They can thus only match on device
 * information.
 *
 * Interface quirks are applied after reading all the configuration descriptors.
 * They can match on both device and interface information.
 *
 * Note that the DELAY_INIT and HONOR_BNUMINTERFACES quirks do not make sense as
 * interface quirks, as they only influence the enumeration process which is run
 * before processing the interface quirks.
 *
 * Please keep the lists ordered by:
 * 	1) Vendor ID
 * 	2) Product ID
 * 	3) Class ID
 */
static const struct usb_device_id usb_quirk_list[] = {
	/* CBM - Flash disk */
	{ USB_DEVICE(0x0204, 0x6025), .driver_info = USB_QUIRK_RESET_RESUME },

	/* HP 5300/5370C scanner */
	{ USB_DEVICE(0x03f0, 0x0701), .driver_info =
			USB_QUIRK_STRING_FETCH_255 },

	/* Creative SB Audigy 2 NX */
	{ USB_DEVICE(0x041e, 0x3020), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Microsoft LifeCam-VX700 v2.0 */
	{ USB_DEVICE(0x045e, 0x0770), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech Quickcam Fusion */
	{ USB_DEVICE(0x046d, 0x08c1), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech Quickcam Orbit MP */
	{ USB_DEVICE(0x046d, 0x08c2), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech Quickcam Pro for Notebook */
	{ USB_DEVICE(0x046d, 0x08c3), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech Quickcam Pro 5000 */
	{ USB_DEVICE(0x046d, 0x08c5), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech Quickcam OEM Dell Notebook */
	{ USB_DEVICE(0x046d, 0x08c6), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech Quickcam OEM Cisco VT Camera II */
	{ USB_DEVICE(0x046d, 0x08c7), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech Harmony 700-series */
	{ USB_DEVICE(0x046d, 0xc122), .driver_info = USB_QUIRK_DELAY_INIT },

	/* Philips PSC805 audio device */
	{ USB_DEVICE(0x0471, 0x0155), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Artisman Watchdog Dongle */
	{ USB_DEVICE(0x04b4, 0x0526), .driver_info =
			USB_QUIRK_CONFIG_INTF_STRINGS },

	/* Microchip Joss Optical infrared touchboard device */
	{ USB_DEVICE(0x04d8, 0x000c), .driver_info =
			USB_QUIRK_CONFIG_INTF_STRINGS },

	/* CarrolTouch 4000U */
	{ USB_DEVICE(0x04e7, 0x0009), .driver_info = USB_QUIRK_RESET_RESUME },

	/* CarrolTouch 4500U */
	{ USB_DEVICE(0x04e7, 0x0030), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Samsung Android phone modem - ID conflict with SPH-I500 */
	{ USB_DEVICE(0x04e8, 0x6601), .driver_info =
			USB_QUIRK_CONFIG_INTF_STRINGS },

	/* Roland SC-8820 */
	{ USB_DEVICE(0x0582, 0x0007), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Edirol SD-20 */
	{ USB_DEVICE(0x0582, 0x0027), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Alcor Micro Corp. Hub */
	{ USB_DEVICE(0x058f, 0x9254), .driver_info = USB_QUIRK_RESET_RESUME },

	/* appletouch */
	{ USB_DEVICE(0x05ac, 0x021a), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Avision AV600U */
	{ USB_DEVICE(0x0638, 0x0a13), .driver_info =
	  USB_QUIRK_STRING_FETCH_255 },

	/* Saitek Cyborg Gold Joystick */
	{ USB_DEVICE(0x06a3, 0x0006), .driver_info =
			USB_QUIRK_CONFIG_INTF_STRINGS },

	/* Guillemot Webcam Hercules Dualpix Exchange (2nd ID) */
	{ USB_DEVICE(0x06f8, 0x0804), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Guillemot Webcam Hercules Dualpix Exchange*/
	{ USB_DEVICE(0x06f8, 0x3005), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Midiman M-Audio Keystation 88es */
	{ USB_DEVICE(0x0763, 0x0192), .driver_info = USB_QUIRK_RESET_RESUME },

	/* M-Systems Flash Disk Pioneers */
	{ USB_DEVICE(0x08ec, 0x1000), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Keytouch QWERTY Panel keyboard */
	{ USB_DEVICE(0x0926, 0x3333), .driver_info =
			USB_QUIRK_CONFIG_INTF_STRINGS },

	/* X-Rite/Gretag-Macbeth Eye-One Pro display colorimeter */
	{ USB_DEVICE(0x0971, 0x2000), .driver_info = USB_QUIRK_NO_SET_INTF },

	/* Broadcom BCM92035DGROM BT dongle */
	{ USB_DEVICE(0x0a5c, 0x2021), .driver_info = USB_QUIRK_RESET_RESUME },

	/* MAYA44USB sound device */
	{ USB_DEVICE(0x0a92, 0x0091), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Action Semiconductor flash disk */
	{ USB_DEVICE(0x10d6, 0x2200), .driver_info =
			USB_QUIRK_STRING_FETCH_255 },

	/* SKYMEDI USB_DRIVE */
	{ USB_DEVICE(0x1516, 0x8628), .driver_info = USB_QUIRK_RESET_RESUME },

	/* BUILDWIN Photo Frame */
	{ USB_DEVICE(0x1908, 0x1315), .driver_info =
			USB_QUIRK_HONOR_BNUMINTERFACES },

	/* INTEL VALUE SSD */
	{ USB_DEVICE(0x8086, 0xf1a5), .driver_info = USB_QUIRK_RESET_RESUME },

	{ }  /* terminating entry must be last */
};

static const struct usb_device_id usb_interface_quirk_list[] = {
	/* Logitech UVC Cameras */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x046d, USB_CLASS_VIDEO, 1, 0),
	  .driver_info = USB_QUIRK_RESET_RESUME },

	{ }  /* terminating entry must be last */
};

static const struct usb_device_id usb_amd_resume_quirk_list[] = {
	/* Lenovo Mouse with Pixart controller */
	{ USB_DEVICE(0x17ef, 0x602e), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Pixart Mouse */
	{ USB_DEVICE(0x093a, 0x2500), .driver_info = USB_QUIRK_RESET_RESUME },
	{ USB_DEVICE(0x093a, 0x2510), .driver_info = USB_QUIRK_RESET_RESUME },
	{ USB_DEVICE(0x093a, 0x2521), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech Optical Mouse M90/M100 */
	{ USB_DEVICE(0x046d, 0xc05a), .driver_info = USB_QUIRK_RESET_RESUME },

	{ }  /* terminating entry must be last */
};

static bool usb_match_any_interface(struct usb_device *udev,
				    const struct usb_device_id *id)
{
	unsigned int i;

	for (i = 0; i < udev->descriptor.bNumConfigurations; ++i) {
		struct usb_host_config *cfg = &udev->config[i];
		unsigned int j;

		for (j = 0; j < cfg->desc.bNumInterfaces; ++j) {
			struct usb_interface_cache *cache;
			struct usb_host_interface *intf;

			cache = cfg->intf_cache[j];
			if (cache->num_altsetting == 0)
				continue;

			intf = &cache->altsetting[0];
			if (usb_match_one_id_intf(udev, intf, id))
				return true;
		}
	}

	return false;
}

static int usb_amd_resume_quirk(struct usb_device *udev)
{
	struct usb_hcd *hcd;

	hcd = bus_to_hcd(udev->bus);
	/* The device should be attached directly to root hub */
	if (udev->level == 1 && hcd->amd_resume_bug == 1)
		return 1;

	return 0;
}

static u32 __usb_detect_quirks(struct usb_device *udev,
			       const struct usb_device_id *id)
{
	u32 quirks = 0;

	for (; id->match_flags; id++) {
		if (!usb_match_device(udev, id))
			continue;

		if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_INFO) &&
		    !usb_match_any_interface(udev, id))
			continue;

		quirks |= (u32)(id->driver_info);
	}

	return quirks;
}

/*
 * Detect any quirks the device has, and do any housekeeping for it if needed.
 */
void usb_detect_quirks(struct usb_device *udev)
{
	udev->quirks = __usb_detect_quirks(udev, usb_quirk_list);

	/*
	 * Pixart-based mice would trigger remote wakeup issue on AMD
	 * Yangtze chipset, so set them as RESET_RESUME flag.
	 */
	if (usb_amd_resume_quirk(udev))
		udev->quirks |= __usb_detect_quirks(udev,
				usb_amd_resume_quirk_list);

	if (udev->quirks)
		dev_dbg(&udev->dev, "USB quirks for this device: %x\n",
			udev->quirks);

#ifdef CONFIG_USB_DEFAULT_PERSIST
	if (!(udev->quirks & USB_QUIRK_RESET))
		udev->persist_enabled = 1;
#else
	/* Hubs are automatically enabled for USB-PERSIST */
	if (udev->descriptor.bDeviceClass == USB_CLASS_HUB)
		udev->persist_enabled = 1;
#endif	/* CONFIG_USB_DEFAULT_PERSIST */
}

void usb_detect_interface_quirks(struct usb_device *udev)
{
	u32 quirks;

	quirks = __usb_detect_quirks(udev, usb_interface_quirk_list);
	if (quirks == 0)
		return;

	dev_dbg(&udev->dev, "USB interface quirks for this device: %x\n",
		quirks);
	udev->quirks |= quirks;
}
