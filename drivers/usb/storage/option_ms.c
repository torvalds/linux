/*
 * Driver for Option High Speed Mobile Devices.
 *
 *   (c) 2008 Dan Williams <dcbw@redhat.com>
 *
 * Inspiration taken from sierra_ms.c by Kevin Lloyd <klloyd@sierrawireless.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/usb.h>

#include "usb.h"
#include "transport.h"
#include "option_ms.h"
#include "debug.h"

#define ZCD_FORCE_MODEM			0x01
#define ZCD_ALLOW_MS 			0x02

static unsigned int option_zero_cd = ZCD_FORCE_MODEM;
module_param(option_zero_cd, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(option_zero_cd, "ZeroCD mode (1=Force Modem (default),"
		 " 2=Allow CD-Rom");

#define RESPONSE_LEN 1024

static int option_rezero(struct us_data *us, int ep_in, int ep_out)
{
	const unsigned char rezero_msg[] = {
	  0x55, 0x53, 0x42, 0x43, 0x78, 0x56, 0x34, 0x12,
	  0x01, 0x00, 0x00, 0x00, 0x80, 0x00, 0x06, 0x01,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	char *buffer;
	int result;

	US_DEBUGP("Option MS: %s", "DEVICE MODE SWITCH\n");

	buffer = kzalloc(RESPONSE_LEN, GFP_KERNEL);
	if (buffer == NULL)
		return USB_STOR_TRANSPORT_ERROR;

	memcpy(buffer, rezero_msg, sizeof (rezero_msg));
	result = usb_stor_bulk_transfer_buf(us,
			usb_sndbulkpipe(us->pusb_dev, ep_out),
			buffer, sizeof (rezero_msg), NULL);
	if (result != USB_STOR_XFER_GOOD) {
		result = USB_STOR_XFER_ERROR;
		goto out;
	}

	/* Some of the devices need to be asked for a response, but we don't
	 * care what that response is.
	 */
	result = usb_stor_bulk_transfer_buf(us,
			usb_sndbulkpipe(us->pusb_dev, ep_out),
			buffer, RESPONSE_LEN, NULL);
	result = USB_STOR_XFER_GOOD;

out:
	kfree(buffer);
	return result;
}

int option_ms_init(struct us_data *us)
{
	struct usb_device *udev;
	struct usb_interface *intf;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint = NULL;
	u8 ep_in = 0, ep_out = 0;
	int ep_in_size = 0, ep_out_size = 0;
	int i, result;

	udev = us->pusb_dev;
	intf = us->pusb_intf;

	/* Ensure it's really a ZeroCD device; devices that are already
	 * in modem mode return 0xFF for class, subclass, and protocol.
	 */
	if (udev->descriptor.bDeviceClass != 0 ||
	    udev->descriptor.bDeviceSubClass != 0 ||
	    udev->descriptor.bDeviceProtocol != 0)
		return USB_STOR_TRANSPORT_GOOD;

	US_DEBUGP("Option MS: option_ms_init called\n");

	/* Find the right mass storage interface */
	iface_desc = intf->cur_altsetting;
	if (iface_desc->desc.bInterfaceClass != 0x8 ||
	    iface_desc->desc.bInterfaceSubClass != 0x6 ||
	    iface_desc->desc.bInterfaceProtocol != 0x50) {
		US_DEBUGP("Option MS: mass storage interface not found, no action "
		          "required\n");
		return USB_STOR_TRANSPORT_GOOD;
	}

	/* Find the mass storage bulk endpoints */
	for (i = 0; i < iface_desc->desc.bNumEndpoints && (!ep_in_size || !ep_out_size); ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(endpoint)) {
			ep_in = usb_endpoint_num(endpoint);
			ep_in_size = le16_to_cpu(endpoint->wMaxPacketSize);
		} else if (usb_endpoint_is_bulk_out(endpoint)) {
			ep_out = usb_endpoint_num(endpoint);
			ep_out_size = le16_to_cpu(endpoint->wMaxPacketSize);
		}
	}

	/* Can't find the mass storage endpoints */
	if (!ep_in_size || !ep_out_size) {
		US_DEBUGP("Option MS: mass storage endpoints not found, no action "
		          "required\n");
		return USB_STOR_TRANSPORT_GOOD;
	}

	/* Force Modem mode */
	if (option_zero_cd == ZCD_FORCE_MODEM) {
		US_DEBUGP("Option MS: %s", "Forcing Modem Mode\n");
		result = option_rezero(us, ep_in, ep_out);
		if (result != USB_STOR_XFER_GOOD)
			US_DEBUGP("Option MS: Failed to switch to modem mode.\n");
		return -EIO;
	} else if (option_zero_cd == ZCD_ALLOW_MS) {
		/* Allow Mass Storage mode (keep CD-Rom) */
		US_DEBUGP("Option MS: %s", "Allowing Mass Storage Mode if device"
		          " requests it\n");
	}

	return USB_STOR_TRANSPORT_GOOD;
}

