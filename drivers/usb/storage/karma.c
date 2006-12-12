/* Driver for Rio Karma
 *
 *   (c) 2006 Bob Copeland <me@bobcopeland.com>
 *   (c) 2006 Keith Bennett <keith@mcs.st-and.ac.uk>
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

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>

#include "usb.h"
#include "transport.h"
#include "debug.h"
#include "karma.h"

#define RIO_PREFIX "RIOP\x00"
#define RIO_PREFIX_LEN 5
#define RIO_SEND_LEN 40
#define RIO_RECV_LEN 0x200

#define RIO_ENTER_STORAGE 0x1
#define RIO_LEAVE_STORAGE 0x2
#define RIO_RESET 0xC

extern int usb_stor_Bulk_transport(struct scsi_cmnd *, struct us_data *);

struct karma_data {
	int in_storage;
	char *recv;
};

/*
 * Send commands to Rio Karma.
 *
 * For each command we send 40 bytes starting 'RIOP\0' followed by
 * the command number and a sequence number, which the device will ack
 * with a 512-byte packet with the high four bits set and everything
 * else null.  Then we send 'RIOP\x80' followed by a zero and the
 * sequence number, until byte 5 in the response repeats the sequence
 * number.
 */
static int rio_karma_send_command(char cmd, struct us_data *us)
{
	int result, partial;
	unsigned long timeout;
	static unsigned char seq = 1;
	struct karma_data *data = (struct karma_data *) us->extra;

	US_DEBUGP("karma: sending command %04x\n", cmd);
	memset(us->iobuf, 0, RIO_SEND_LEN);
	memcpy(us->iobuf, RIO_PREFIX, RIO_PREFIX_LEN);
	us->iobuf[5] = cmd;
	us->iobuf[6] = seq;

	timeout = jiffies + msecs_to_jiffies(6000);
	for (;;) {
		result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
			us->iobuf, RIO_SEND_LEN, &partial);
		if (result != USB_STOR_XFER_GOOD)
			goto err;

		result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
			data->recv, RIO_RECV_LEN, &partial);
		if (result != USB_STOR_XFER_GOOD)
			goto err;

		if (data->recv[5] == seq)
			break;

		if (time_after(jiffies, timeout))
			goto err;

		us->iobuf[4] = 0x80;
		us->iobuf[5] = 0;
		msleep(50);
	}

	seq++;
	if (seq == 0)
		seq = 1;

	US_DEBUGP("karma: sent command %04x\n", cmd);
	return 0;
err:
	US_DEBUGP("karma: command %04x failed\n", cmd);
	return USB_STOR_TRANSPORT_FAILED;
}

/*
 * Trap START_STOP and READ_10 to leave/re-enter storage mode.
 * Everything else is propagated to the normal bulk layer.
 */
int rio_karma_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	int ret;
	struct karma_data *data = (struct karma_data *) us->extra;

	if (srb->cmnd[0] == READ_10 && !data->in_storage) {
		ret = rio_karma_send_command(RIO_ENTER_STORAGE, us);
		if (ret)
			return ret;

		data->in_storage = 1;
		return usb_stor_Bulk_transport(srb, us);
	} else if (srb->cmnd[0] == START_STOP) {
		ret = rio_karma_send_command(RIO_LEAVE_STORAGE, us);
		if (ret)
			return ret;

		data->in_storage = 0;
		return rio_karma_send_command(RIO_RESET, us);
	}
	return usb_stor_Bulk_transport(srb, us);
}

static void rio_karma_destructor(void *extra)
{
	struct karma_data *data = (struct karma_data *) extra;
	kfree(data->recv);
}

int rio_karma_init(struct us_data *us)
{
	int ret = 0;
	struct karma_data *data = kzalloc(sizeof(struct karma_data), GFP_NOIO);
	if (!data)
		goto out;

	data->recv = kmalloc(RIO_RECV_LEN, GFP_NOIO);
	if (!data->recv) {
		kfree(data);
		goto out;
	}

	us->extra = data;
	us->extra_destructor = rio_karma_destructor;
	ret = rio_karma_send_command(RIO_ENTER_STORAGE, us);
	data->in_storage = (ret == 0);
out:
	return ret;
}
