/* Special Initializers for certain USB Mass Storage devices
 *
 * $Id: initializers.c,v 1.2 2000/09/06 22:35:57 mdharm Exp $
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

#include <linux/sched.h>
#include <linux/errno.h>

#include "usb.h"
#include "initializers.h"
#include "debug.h"
#include "transport.h"

#define RIO_MSC 0x08
#define RIOP_INIT "RIOP\x00\x01\x08"
#define RIOP_INIT_LEN 7
#define RIO_SEND_LEN 40
#define RIO_RECV_LEN 0x200

/* This places the Shuttle/SCM USB<->SCSI bridge devices in multi-target
 * mode */
int usb_stor_euscsi_init(struct us_data *us)
{
	int result;

	US_DEBUGP("Attempting to init eUSCSI bridge...\n");
	us->iobuf[0] = 0x1;
	result = usb_stor_control_msg(us, us->send_ctrl_pipe,
			0x0C, USB_RECIP_INTERFACE | USB_TYPE_VENDOR,
			0x01, 0x0, us->iobuf, 0x1, 5*HZ);
	US_DEBUGP("-- result is %d\n", result);

	return 0;
}

/* This function is required to activate all four slots on the UCR-61S2B
 * flash reader */
int usb_stor_ucr61s2b_init(struct us_data *us)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap*) us->iobuf;
	struct bulk_cs_wrap *bcs = (struct bulk_cs_wrap*) us->iobuf;
	int res, partial;
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
	if(res)
		return res;

	US_DEBUGP("Getting status packet...\n");
	res = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe, bcs,
			US_BULK_CS_WRAP_LEN, &partial);

	return (res ? -1 : 0);
}

/* Place the Rio Karma into mass storage mode.
 *
 * The initialization begins by sending 40 bytes starting
 * RIOP\x00\x01\x08\x00, which the device will ack with a 512-byte
 * packet with the high four bits set and everything else null.
 *
 * Next, we send RIOP\x80\x00\x08\x00.  Each time, a 512 byte response
 * must be read, but we must loop until byte 5 in the response is 0x08,
 * indicating success.  */
int rio_karma_init(struct us_data *us)
{
	int result, partial;
	char *recv;
	unsigned long timeout;

	// us->iobuf is big enough to hold cmd but not receive
	if (!(recv = kmalloc(RIO_RECV_LEN, GFP_KERNEL)))
		goto die_nomem;

	US_DEBUGP("Initializing Karma...\n");

	memset(us->iobuf, 0, RIO_SEND_LEN);
	memcpy(us->iobuf, RIOP_INIT, RIOP_INIT_LEN);

	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
		us->iobuf, RIO_SEND_LEN, &partial);
	if (result != USB_STOR_XFER_GOOD)
		goto die;

	result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
		recv, RIO_RECV_LEN, &partial);
	if (result != USB_STOR_XFER_GOOD)
		goto die;

	us->iobuf[4] = 0x80;
	us->iobuf[5] = 0;
	timeout = jiffies + msecs_to_jiffies(3000);
	for (;;) {
		US_DEBUGP("Sending init command\n");
		result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
			us->iobuf, RIO_SEND_LEN, &partial);
		if (result != USB_STOR_XFER_GOOD)
			goto die;

		result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
			recv, RIO_RECV_LEN, &partial);
		if (result != USB_STOR_XFER_GOOD)
			goto die;

		if (recv[5] == RIO_MSC)
			break;
		if (time_after(jiffies, timeout))
			goto die;
		msleep(10);
	}
	US_DEBUGP("Karma initialized.\n");
	kfree(recv);
	return 0;

die:
	kfree(recv);
die_nomem:
	US_DEBUGP("Could not initialize karma.\n");
	return USB_STOR_TRANSPORT_FAILED;
}

