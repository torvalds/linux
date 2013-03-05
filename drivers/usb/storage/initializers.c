/* Special Initializers for certain USB Mass Storage devices
 *
 * Current development and maintenance by:
 *   (c) 1999, 2000 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * This driver is based on the 'USB Mass Storage Class' document. This
 * describes in detail the protocol used to communicate with such
 * devices.  Clearly, the designers had SCSI and ATAPI commands in
 * mind when they created this document.  The commands are all very
 * similar to commands in the SCSI-II and ATAPI specifications.
 *
 * It is important to note that in a number of cases this class
 * exhibits class-specific exemptions from the USB specification.
 * Notably the usage of NAK, STALL and ACK differs from the norm, in
 * that they are used to communicate wait, failed and OK on commands.
 *
 * Also, for certain devices, the interrupt endpoint is used to convey
 * status of a command.
 *
 * Please see http://www.one-eyed-alien.net/~mdharm/linux-usb for more
 * information about this driver.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
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

#include <linux/errno.h>

#include "usb.h"
#include "initializers.h"
#include "debug.h"
#include "transport.h"

/* This places the Shuttle/SCM USB<->SCSI bridge devices in multi-target
 * mode */
int usb_stor_euscsi_init(struct us_data *us)
{
	int result;

	US_DEBUGP("Attempting to init eUSCSI bridge...\n");
	us->iobuf[0] = 0x1;
	result = usb_stor_control_msg(us, us->send_ctrl_pipe,
			0x0C, USB_RECIP_INTERFACE | USB_TYPE_VENDOR,
			0x01, 0x0, us->iobuf, 0x1, 5000);
	US_DEBUGP("-- result is %d\n", result);

	return 0;
}

/* This function is required to activate all four slots on the UCR-61S2B
 * flash reader */
int usb_stor_ucr61s2b_init(struct us_data *us)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap*) us->iobuf;
	struct bulk_cs_wrap *bcs = (struct bulk_cs_wrap*) us->iobuf;
	int res;
	unsigned int partial;
	static char init_string[] = "\xec\x0a\x06\x00$PCCHIPS";

	US_DEBUGP("Sending UCR-61S2B initialization packet...\n");

	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->Tag = 0;
	bcb->DataTransferLength = cpu_to_le32(0);
	bcb->Flags = bcb->Lun = 0;
	bcb->Length = sizeof(init_string) - 1;
	memset(bcb->CDB, 0, sizeof(bcb->CDB));
	memcpy(bcb->CDB, init_string, sizeof(init_string) - 1);

	res = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe, bcb,
			US_BULK_CB_WRAP_LEN, &partial);
	if (res)
		return -EIO;

	US_DEBUGP("Getting status packet...\n");
	res = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe, bcs,
			US_BULK_CS_WRAP_LEN, &partial);
	if (res)
		return -EIO;

	return 0;
}

/* This places the HUAWEI usb dongles in multi-port mode */
static int usb_stor_huawei_feature_init(struct us_data *us)
{
	int result;

	result = usb_stor_control_msg(us, us->send_ctrl_pipe,
				      USB_REQ_SET_FEATURE,
				      USB_TYPE_STANDARD | USB_RECIP_DEVICE,
				      0x01, 0x0, NULL, 0x0, 1000);
	US_DEBUGP("Huawei mode set result is %d\n", result);
	return 0;
}

/*
 * It will send a scsi switch command called rewind' to huawei dongle.
 * When the dongle receives this command at the first time,
 * it will reboot immediately. After rebooted, it will ignore this command.
 * So it is  unnecessary to read its response.
 */
static int usb_stor_huawei_scsi_init(struct us_data *us)
{
	int result = 0;
	int act_len = 0;
	struct bulk_cb_wrap *bcbw = (struct bulk_cb_wrap *) us->iobuf;
	char rewind_cmd[] = {0x11, 0x06, 0x20, 0x00, 0x00, 0x01, 0x01, 0x00,
			0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	bcbw->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcbw->Tag = 0;
	bcbw->DataTransferLength = 0;
	bcbw->Flags = bcbw->Lun = 0;
	bcbw->Length = sizeof(rewind_cmd);
	memset(bcbw->CDB, 0, sizeof(bcbw->CDB));
	memcpy(bcbw->CDB, rewind_cmd, sizeof(rewind_cmd));

	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe, bcbw,
					US_BULK_CB_WRAP_LEN, &act_len);
	US_DEBUGP("transfer actual length=%d, result=%d\n", act_len, result);
	return result;
}

/*
 * It tries to find the supported Huawei USB dongles.
 * In Huawei, they assign the following product IDs
 * for all of their mobile broadband dongles,
 * including the new dongles in the future.
 * So if the product ID is not included in this list,
 * it means it is not Huawei's mobile broadband dongles.
 */
static int usb_stor_huawei_dongles_pid(struct us_data *us)
{
	struct usb_interface_descriptor *idesc;
	int idProduct;

	idesc = &us->pusb_intf->cur_altsetting->desc;
	idProduct = le16_to_cpu(us->pusb_dev->descriptor.idProduct);
	/* The first port is CDROM,
	 * means the dongle in the single port mode,
	 * and a switch command is required to be sent. */
	if (idesc && idesc->bInterfaceNumber == 0) {
		if ((idProduct == 0x1001)
			|| (idProduct == 0x1003)
			|| (idProduct == 0x1004)
			|| (idProduct >= 0x1401 && idProduct <= 0x1500)
			|| (idProduct >= 0x1505 && idProduct <= 0x1600)
			|| (idProduct >= 0x1c02 && idProduct <= 0x2202)) {
			return 1;
		}
	}
	return 0;
}

int usb_stor_huawei_init(struct us_data *us)
{
	int result = 0;

	if (usb_stor_huawei_dongles_pid(us)) {
		if (le16_to_cpu(us->pusb_dev->descriptor.idProduct) >= 0x1446)
			result = usb_stor_huawei_scsi_init(us);
		else
			result = usb_stor_huawei_feature_init(us);
	}
	return result;
}
