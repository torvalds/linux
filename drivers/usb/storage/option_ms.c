// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Option High Speed Mobile Devices.
 *
 *   (c) 2008 Dan Williams <dcbw@redhat.com>
 *
 * Inspiration taken from sierra_ms.c by Kevin Lloyd <klloyd@sierrawireless.com>
 */

#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/module.h>

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

static int option_rezero(struct us_data *us)
{
	static const unsigned char rezero_msg[] = {
	  0x55, 0x53, 0x42, 0x43, 0x78, 0x56, 0x34, 0x12,
	  0x01, 0x00, 0x00, 0x00, 0x80, 0x00, 0x06, 0x01,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	char *buffer;
	int result;

	usb_stor_dbg(us, "Option MS: %s\n", "DEVICE MODE SWITCH");

	buffer = kzalloc(RESPONSE_LEN, GFP_KERNEL);
	if (buffer == NULL)
		return USB_STOR_TRANSPORT_ERROR;

	memcpy(buffer, rezero_msg, sizeof(rezero_msg));
	result = usb_stor_bulk_transfer_buf(us,
			us->send_bulk_pipe,
			buffer, sizeof(rezero_msg), NULL);
	if (result != USB_STOR_XFER_GOOD) {
		result = USB_STOR_XFER_ERROR;
		goto out;
	}

	/*
	 * Some of the devices need to be asked for a response, but we don't
	 * care what that response is.
	 */
	usb_stor_bulk_transfer_buf(us,
			us->recv_bulk_pipe,
			buffer, RESPONSE_LEN, NULL);

	/* Read the CSW */
	usb_stor_bulk_transfer_buf(us,
			us->recv_bulk_pipe,
			buffer, 13, NULL);

	result = USB_STOR_XFER_GOOD;

out:
	kfree(buffer);
	return result;
}

static int option_inquiry(struct us_data *us)
{
	static const unsigned char inquiry_msg[] = {
	  0x55, 0x53, 0x42, 0x43, 0x12, 0x34, 0x56, 0x78,
	  0x24, 0x00, 0x00, 0x00, 0x80, 0x00, 0x06, 0x12,
	  0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	char *buffer;
	int result;

	usb_stor_dbg(us, "Option MS: %s\n", "device inquiry for vendor name");

	buffer = kzalloc(0x24, GFP_KERNEL);
	if (buffer == NULL)
		return USB_STOR_TRANSPORT_ERROR;

	memcpy(buffer, inquiry_msg, sizeof(inquiry_msg));
	result = usb_stor_bulk_transfer_buf(us,
			us->send_bulk_pipe,
			buffer, sizeof(inquiry_msg), NULL);
	if (result != USB_STOR_XFER_GOOD) {
		result = USB_STOR_XFER_ERROR;
		goto out;
	}

	result = usb_stor_bulk_transfer_buf(us,
			us->recv_bulk_pipe,
			buffer, 0x24, NULL);
	if (result != USB_STOR_XFER_GOOD) {
		result = USB_STOR_XFER_ERROR;
		goto out;
	}

	result = memcmp(buffer+8, "Option", 6);

	if (result != 0)
		result = memcmp(buffer+8, "ZCOPTION", 8);

	/* Read the CSW */
	usb_stor_bulk_transfer_buf(us,
			us->recv_bulk_pipe,
			buffer, 13, NULL);

out:
	kfree(buffer);
	return result;
}


int option_ms_init(struct us_data *us)
{
	int result;

	usb_stor_dbg(us, "Option MS: %s\n", "option_ms_init called");

	/*
	 * Additional test for vendor information via INQUIRY,
	 * because some vendor/product IDs are ambiguous
	 */
	result = option_inquiry(us);
	if (result != 0) {
		usb_stor_dbg(us, "Option MS: %s\n",
			     "vendor is not Option or not determinable, no action taken");
		return 0;
	} else
		usb_stor_dbg(us, "Option MS: %s\n",
			     "this is a genuine Option device, proceeding");

	/* Force Modem mode */
	if (option_zero_cd == ZCD_FORCE_MODEM) {
		usb_stor_dbg(us, "Option MS: %s\n", "Forcing Modem Mode");
		result = option_rezero(us);
		if (result != USB_STOR_XFER_GOOD)
			usb_stor_dbg(us, "Option MS: %s\n",
				     "Failed to switch to modem mode");
		return -EIO;
	} else if (option_zero_cd == ZCD_ALLOW_MS) {
		/* Allow Mass Storage mode (keep CD-Rom) */
		usb_stor_dbg(us, "Option MS: %s\n",
			     "Allowing Mass Storage Mode if device requests it");
	}

	return 0;
}

