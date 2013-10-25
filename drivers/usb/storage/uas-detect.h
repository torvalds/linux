#include <linux/usb.h>
#include <linux/usb/hcd.h>

static int uas_is_interface(struct usb_host_interface *intf)
{
	return (intf->desc.bInterfaceClass == USB_CLASS_MASS_STORAGE &&
		intf->desc.bInterfaceSubClass == USB_SC_SCSI &&
		intf->desc.bInterfaceProtocol == USB_PR_UAS);
}

static int uas_isnt_supported(struct usb_device *udev)
{
	struct usb_hcd *hcd = bus_to_hcd(udev->bus);

	dev_warn(&udev->dev, "The driver for the USB controller %s does not "
			"support scatter-gather which is\n",
			hcd->driver->description);
	dev_warn(&udev->dev, "required by the UAS driver. Please try an"
			"alternative USB controller if you wish to use UAS.\n");
	return -ENODEV;
}

static int uas_find_uas_alt_setting(struct usb_interface *intf)
{
	int i;
	struct usb_device *udev = interface_to_usbdev(intf);
	int sg_supported = udev->bus->sg_tablesize != 0;

	for (i = 0; i < intf->num_altsetting; i++) {
		struct usb_host_interface *alt = &intf->altsetting[i];

		if (uas_is_interface(alt)) {
			if (!sg_supported)
				return uas_isnt_supported(udev);
			return alt->desc.bAlternateSetting;
		}
	}

	return -ENODEV;
}

static int uas_use_uas_driver(struct usb_interface *intf,
			      const struct usb_device_id *id)
{
	unsigned long flags = id->driver_info;

	if (flags & US_FL_IGNORE_UAS)
		return 0;

	return uas_find_uas_alt_setting(intf) >= 0;
}
