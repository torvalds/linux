/*
 * Garmin GPS driver
 *
 * Copyright (C) 2006-2011 Hermann Kneissel herkne@gmx.de
 *
 * The latest version of the driver can be found at
 * http://sourceforge.net/projects/garmin-gps/
 *
 * This driver has been derived from v2.1 of the visor driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111 USA
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

/* the mode to be set when the port ist opened */
static int initial_mode = 1;

/* debug flag */
static bool debug;

#define GARMIN_VENDOR_ID             0x091E

/*
 * Version Information
 */

#define VERSION_MAJOR	0
#define VERSION_MINOR	36

#define _STR(s) #s
#define _DRIVER_VERSION(a, b) "v" _STR(a) "." _STR(b)
#define DRIVER_VERSION _DRIVER_VERSION(VERSION_MAJOR, VERSION_MINOR)
#define DRIVER_AUTHOR "hermann kneissel"
#define DRIVER_DESC "garmin gps driver"

/* error codes returned by the driver */
#define EINVPKT	1000	/* invalid packet structure */


/* size of the header of a packet using the usb protocol */
#define GARMIN_PKTHDR_LENGTH	12

/* max. possible size of a packet using the serial protocol */
#define MAX_SERIAL_PKT_SIZ (3 + 255 + 3)

/*  max. possible size of a packet with worst case stuffing */
#define MAX_SERIAL_PKT_SIZ_STUFFED (MAX_SERIAL_PKT_SIZ + 256)

/* size of a buffer able to hold a complete (no stuffing) packet
 * (the document protocol does not contain packets with a larger
 *  size, but in theory a packet may be 64k+12 bytes - if in
 *  later protocol versions larger packet sizes occur, this value
 *  should be increased accordingly, so the input buffer is always
 *  large enough the store a complete packet inclusive header) */
#define GPS_IN_BUFSIZ  (GARMIN_PKTHDR_LENGTH+MAX_SERIAL_PKT_SIZ)

/* size of a buffer able to hold a complete (incl. stuffing) packet */
#define GPS_OUT_BUFSIZ (GARMIN_PKTHDR_LENGTH+MAX_SERIAL_PKT_SIZ_STUFFED)

/* where to place the packet id of a serial packet, so we can
 * prepend the usb-packet header without the need to move the
 * packets data */
#define GSP_INITIAL_OFFSET (GARMIN_PKTHDR_LENGTH-2)

/* max. size of incoming private packets (header+1 param) */
#define PRIVPKTSIZ (GARMIN_PKTHDR_LENGTH+4)

#define GARMIN_LAYERID_TRANSPORT  0
#define GARMIN_LAYERID_APPL      20
/* our own layer-id to use for some control mechanisms */
#define GARMIN_LAYERID_PRIVATE	0x01106E4B

#define GARMIN_PKTID_PVT_DATA	51
#define GARMIN_PKTID_L001_COMMAND_DATA 10

#define CMND_ABORT_TRANSFER 0

/* packet ids used in private layer */
#define PRIV_PKTID_SET_DEBUG	1
#define PRIV_PKTID_SET_MODE	2
#define PRIV_PKTID_INFO_REQ	3
#define PRIV_PKTID_INFO_RESP	4
#define PRIV_PKTID_RESET_REQ	5
#define PRIV_PKTID_SET_DEF_MODE	6


#define ETX	0x03
#define DLE	0x10
#define ACK	0x06
#define NAK	0x15

/* structure used to queue incoming packets */
struct garmin_packet {
	struct list_head  list;
	int               seq;
	/* the real size of the data array, always > 0 */
	int               size;
	__u8              data[1];
};

/* structure used to keep the current state of the driver */
struct garmin_data {
	__u8   state;
	__u16  flags;
	__u8   mode;
	__u8   count;
	__u8   pkt_id;
	__u32  serial_num;
	struct timer_list timer;
	struct usb_serial_port *port;
	int    seq_counter;
	int    insize;
	int    outsize;
	__u8   inbuffer [GPS_IN_BUFSIZ];  /* tty -> usb */
	__u8   outbuffer[GPS_OUT_BUFSIZ]; /* usb -> tty */
	__u8   privpkt[4*6];
	spinlock_t lock;
	struct list_head pktlist;
};


#define STATE_NEW            0
#define STATE_INITIAL_DELAY  1
#define STATE_TIMEOUT        2
#define STATE_SESSION_REQ1   3
#define STATE_SESSION_REQ2   4
#define STATE_ACTIVE         5

#define STATE_RESET	     8
#define STATE_DISCONNECTED   9
#define STATE_WAIT_TTY_ACK  10
#define STATE_GSP_WAIT_DATA 11

#define MODE_NATIVE          0
#define MODE_GARMIN_SERIAL   1

/* Flags used in garmin_data.flags: */
#define FLAGS_SESSION_REPLY_MASK  0x00C0
#define FLAGS_SESSION_REPLY1_SEEN 0x0080
#define FLAGS_SESSION_REPLY2_SEEN 0x0040
#define FLAGS_BULK_IN_ACTIVE      0x0020
#define FLAGS_BULK_IN_RESTART     0x0010
#define FLAGS_THROTTLED           0x0008
#define APP_REQ_SEEN              0x0004
#define APP_RESP_SEEN             0x0002
#define CLEAR_HALT_REQUIRED       0x0001

#define FLAGS_QUEUING             0x0100
#define FLAGS_DROP_DATA           0x0800

#define FLAGS_GSP_SKIP            0x1000
#define FLAGS_GSP_DLESEEN         0x2000






/* function prototypes */
static int gsp_next_packet(struct garmin_data *garmin_data_p);
static int garmin_write_bulk(struct usb_serial_port *port,
			     const unsigned char *buf, int count,
			     int dismiss_ack);

/* some special packets to be send or received */
static unsigned char const GARMIN_START_SESSION_REQ[]
	= { 0, 0, 0, 0,  5, 0, 0, 0, 0, 0, 0, 0 };
static unsigned char const GARMIN_START_SESSION_REPLY[]
	= { 0, 0, 0, 0,  6, 0, 0, 0, 4, 0, 0, 0 };
static unsigned char const GARMIN_BULK_IN_AVAIL_REPLY[]
	= { 0, 0, 0, 0,  2, 0, 0, 0, 0, 0, 0, 0 };
static unsigned char const GARMIN_APP_LAYER_REPLY[]
	= { 0x14, 0, 0, 0 };
static unsigned char const GARMIN_START_PVT_REQ[]
	= { 20, 0, 0, 0,  10, 0, 0, 0, 2, 0, 0, 0, 49, 0 };
static unsigned char const GARMIN_STOP_PVT_REQ[]
	= { 20, 0, 0, 0,  10, 0, 0, 0, 2, 0, 0, 0, 50, 0 };
static unsigned char const GARMIN_STOP_TRANSFER_REQ[]
	= { 20, 0, 0, 0,  10, 0, 0, 0, 2, 0, 0, 0, 0, 0 };
static unsigned char const GARMIN_STOP_TRANSFER_REQ_V2[]
	= { 20, 0, 0, 0,  10, 0, 0, 0, 1, 0, 0, 0, 0 };
static unsigned char const PRIVATE_REQ[]
	=    { 0x4B, 0x6E, 0x10, 0x01,  0xFF, 0, 0, 0, 0xFF, 0, 0, 0 };



static const struct usb_device_id id_table[] = {
	/* the same device id seems to be used by all
	   usb enabled GPS devices */
	{ USB_DEVICE(GARMIN_VENDOR_ID, 3) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver garmin_driver = {
	.name =		"garmin_gps",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
	.no_dynamic_id = 1,
};


static inline int getLayerId(const __u8 *usbPacket)
{
	return __le32_to_cpup((__le32 *)(usbPacket));
}

static inline int getPacketId(const __u8 *usbPacket)
{
	return __le32_to_cpup((__le32 *)(usbPacket+4));
}

static inline int getDataLength(const __u8 *usbPacket)
{
	return __le32_to_cpup((__le32 *)(usbPacket+8));
}


/*
 * check if the usb-packet in buf contains an abort-transfer command.
 * (if yes, all queued data will be dropped)
 */
static inline int isAbortTrfCmnd(const unsigned char *buf)
{
	if (0 == memcmp(buf, GARMIN_STOP_TRANSFER_REQ,
					sizeof(GARMIN_STOP_TRANSFER_REQ)) ||
	    0 == memcmp(buf, GARMIN_STOP_TRANSFER_REQ_V2,
					sizeof(GARMIN_STOP_TRANSFER_REQ_V2)))
		return 1;
	else
		return 0;
}



static void send_to_tty(struct usb_serial_port *port,
			char *data, unsigned int actual_length)
{
	struct tty_struct *tty = tty_port_tty_get(&port->port);

	if (tty && actual_length) {

		usb_serial_debug_data(debug, &port->dev,
					__func__, actual_length, data);

		tty_insert_flip_string(tty, data, actual_length);
		tty_flip_buffer_push(tty);
	}
	tty_kref_put(tty);
}


/******************************************************************************
 * packet queue handling
 ******************************************************************************/

/*
 * queue a received (usb-)packet for later processing
 */
static int pkt_add(struct garmin_data *garmin_data_p,
		   unsigned char *data, unsigned int data_length)
{
	int state = 0;
	int result = 0;
	unsigned long flags;
	struct garmin_packet *pkt;

	/* process only packets containg data ... */
	if (data_length) {
		pkt = kmalloc(sizeof(struct garmin_packet)+data_length,
								GFP_ATOMIC);
		if (pkt == NULL) {
			dev_err(&garmin_data_p->port->dev, "out of memory\n");
			return 0;
		}
		pkt->size = data_length;
		memcpy(pkt->data, data, data_length);

		spin_lock_irqsave(&garmin_data_p->lock, flags);
		garmin_data_p->flags |= FLAGS_QUEUING;
		result = list_empty(&garmin_data_p->pktlist);
		pkt->seq = garmin_data_p->seq_counter++;
		list_add_tail(&pkt->list, &garmin_data_p->pktlist);
		state = garmin_data_p->state;
		spin_unlock_irqrestore(&garmin_data_p->lock, flags);

		dbg("%s - added: pkt: %d - %d bytes",
			__func__, pkt->seq, data_length);

		/* in serial mode, if someone is waiting for data from
		   the device, convert and send the next packet to tty. */
		if (result && (state == STATE_GSP_WAIT_DATA))
			gsp_next_packet(garmin_data_p);
	}
	return result;
}


/* get the next pending packet */
static struct garmin_packet *pkt_pop(struct garmin_data *garmin_data_p)
{
	unsigned long flags;
	struct garmin_packet *result = NULL;

	spin_lock_irqsave(&garmin_data_p->lock, flags);
	if (!list_empty(&garmin_data_p->pktlist)) {
		result = (struct garmin_packet *)garmin_data_p->pktlist.next;
		list_del(&result->list);
	}
	spin_unlock_irqrestore(&garmin_data_p->lock, flags);
	return result;
}


/* free up all queued data */
static void pkt_clear(struct garmin_data *garmin_data_p)
{
	unsigned long flags;
	struct garmin_packet *result = NULL;

	dbg("%s", __func__);

	spin_lock_irqsave(&garmin_data_p->lock, flags);
	while (!list_empty(&garmin_data_p->pktlist)) {
		result = (struct garmin_packet *)garmin_data_p->pktlist.next;
		list_del(&result->list);
		kfree(result);
	}
	spin_unlock_irqrestore(&garmin_data_p->lock, flags);
}


/******************************************************************************
 * garmin serial protocol handling handling
 ******************************************************************************/

/* send an ack packet back to the tty */
static int gsp_send_ack(struct garmin_data *garmin_data_p, __u8 pkt_id)
{
	__u8 pkt[10];
	__u8 cksum = 0;
	__u8 *ptr = pkt;
	unsigned  l = 0;

	dbg("%s - pkt-id: 0x%X.", __func__, 0xFF & pkt_id);

	*ptr++ = DLE;
	*ptr++ = ACK;
	cksum += ACK;

	*ptr++ = 2;
	cksum += 2;

	*ptr++ = pkt_id;
	cksum += pkt_id;

	if (pkt_id == DLE)
		*ptr++ = DLE;

	*ptr++ = 0;
	*ptr++ = 0xFF & (-cksum);
	*ptr++ = DLE;
	*ptr++ = ETX;

	l = ptr-pkt;

	send_to_tty(garmin_data_p->port, pkt, l);
	return 0;
}



/*
 * called for a complete packet received from tty layer
 *
 * the complete packet (pktid ... cksum) is in garmin_data_p->inbuf starting
 * at GSP_INITIAL_OFFSET.
 *
 * count - number of bytes in the input buffer including space reserved for
 *         the usb header: GSP_INITIAL_OFFSET + number of bytes in packet
 *         (including pkt-id, data-length a. cksum)
 */
static int gsp_rec_packet(struct garmin_data *garmin_data_p, int count)
{
	unsigned long flags;
	const __u8 *recpkt = garmin_data_p->inbuffer+GSP_INITIAL_OFFSET;
	__le32 *usbdata = (__le32 *) garmin_data_p->inbuffer;

	int cksum = 0;
	int n = 0;
	int pktid = recpkt[0];
	int size = recpkt[1];

	usb_serial_debug_data(debug, &garmin_data_p->port->dev,
			       __func__, count-GSP_INITIAL_OFFSET, recpkt);

	if (size != (count-GSP_INITIAL_OFFSET-3)) {
		dbg("%s - invalid size, expected %d bytes, got %d",
			__func__, size, (count-GSP_INITIAL_OFFSET-3));
		return -EINVPKT;
	}

	cksum += *recpkt++;
	cksum += *recpkt++;

	/* sanity check, remove after test ... */
	if ((__u8 *)&(usbdata[3]) != recpkt) {
		dbg("%s - ptr mismatch %p - %p",
			__func__, &(usbdata[4]), recpkt);
		return -EINVPKT;
	}

	while (n < size) {
		cksum += *recpkt++;
		n++;
	}

	if ((0xff & (cksum + *recpkt)) != 0) {
		dbg("%s - invalid checksum, expected %02x, got %02x",
			__func__, 0xff & -cksum, 0xff & *recpkt);
		return -EINVPKT;
	}

	usbdata[0] = __cpu_to_le32(GARMIN_LAYERID_APPL);
	usbdata[1] = __cpu_to_le32(pktid);
	usbdata[2] = __cpu_to_le32(size);

	garmin_write_bulk(garmin_data_p->port, garmin_data_p->inbuffer,
			   GARMIN_PKTHDR_LENGTH+size, 0);

	/* if this was an abort-transfer command, flush all
	   queued data. */
	if (isAbortTrfCmnd(garmin_data_p->inbuffer)) {
		spin_lock_irqsave(&garmin_data_p->lock, flags);
		garmin_data_p->flags |= FLAGS_DROP_DATA;
		spin_unlock_irqrestore(&garmin_data_p->lock, flags);
		pkt_clear(garmin_data_p);
	}

	return count;
}



/*
 * Called for data received from tty
 *
 * buf contains the data read, it may span more than one packet or even
 * incomplete packets
 *
 * input record should be a serial-record, but it may not be complete.
 * Copy it into our local buffer, until an etx is seen (or an error
 * occurs).
 * Once the record is complete, convert into a usb packet and send it
 * to the bulk pipe, send an ack back to the tty.
 *
 * If the input is an ack, just send the last queued packet to the
 * tty layer.
 *
 * if the input is an abort command, drop all queued data.
 */

static int gsp_receive(struct garmin_data *garmin_data_p,
		       const unsigned char *buf, int count)
{
	unsigned long flags;
	int offs = 0;
	int ack_or_nak_seen = 0;
	__u8 *dest;
	int size;
	/* dleSeen: set if last byte read was a DLE */
	int dleSeen;
	/* skip: if set, skip incoming data until possible start of
	 *       new packet
	 */
	int skip;
	__u8 data;

	spin_lock_irqsave(&garmin_data_p->lock, flags);
	dest = garmin_data_p->inbuffer;
	size = garmin_data_p->insize;
	dleSeen = garmin_data_p->flags & FLAGS_GSP_DLESEEN;
	skip = garmin_data_p->flags & FLAGS_GSP_SKIP;
	spin_unlock_irqrestore(&garmin_data_p->lock, flags);

	/* dbg("%s - dle=%d skip=%d size=%d count=%d",
		__func__, dleSeen, skip, size, count); */

	if (size == 0)
		size = GSP_INITIAL_OFFSET;

	while (offs < count) {

		data = *(buf+offs);
		offs++;

		if (data == DLE) {
			if (skip) { /* start of a new pkt */
				skip = 0;
				size = GSP_INITIAL_OFFSET;
				dleSeen = 1;
			} else if (dleSeen) {
				dest[size++] = data;
				dleSeen = 0;
			} else {
				dleSeen = 1;
			}
		} else if (data == ETX) {
			if (dleSeen) {
				/* packet complete */

				data = dest[GSP_INITIAL_OFFSET];

				if (data == ACK) {
					ack_or_nak_seen = ACK;
					dbg("ACK packet complete.");
				} else if (data == NAK) {
					ack_or_nak_seen = NAK;
					dbg("NAK packet complete.");
				} else {
					dbg("packet complete - id=0x%X.",
						0xFF & data);
					gsp_rec_packet(garmin_data_p, size);
				}

				skip = 1;
				size = GSP_INITIAL_OFFSET;
				dleSeen = 0;
			} else {
				dest[size++] = data;
			}
		} else if (!skip) {

			if (dleSeen) {
				size = GSP_INITIAL_OFFSET;
				dleSeen = 0;
			}

			dest[size++] = data;
		}

		if (size >= GPS_IN_BUFSIZ) {
			dbg("%s - packet too large.", __func__);
			skip = 1;
			size = GSP_INITIAL_OFFSET;
			dleSeen = 0;
		}
	}

	spin_lock_irqsave(&garmin_data_p->lock, flags);

	garmin_data_p->insize = size;

	/* copy flags back to structure */
	if (skip)
		garmin_data_p->flags |= FLAGS_GSP_SKIP;
	else
		garmin_data_p->flags &= ~FLAGS_GSP_SKIP;

	if (dleSeen)
		garmin_data_p->flags |= FLAGS_GSP_DLESEEN;
	else
		garmin_data_p->flags &= ~FLAGS_GSP_DLESEEN;

	spin_unlock_irqrestore(&garmin_data_p->lock, flags);

	if (ack_or_nak_seen) {
		if (gsp_next_packet(garmin_data_p) > 0)
			garmin_data_p->state = STATE_ACTIVE;
		else
			garmin_data_p->state = STATE_GSP_WAIT_DATA;
	}
	return count;
}



/*
 * Sends a usb packet to the tty
 *
 * Assumes, that all packages and at an usb-packet boundary.
 *
 * return <0 on error, 0 if packet is incomplete or > 0 if packet was sent
 */
static int gsp_send(struct garmin_data *garmin_data_p,
		    const unsigned char *buf, int count)
{
	const unsigned char *src;
	unsigned char *dst;
	int pktid = 0;
	int datalen = 0;
	int cksum = 0;
	int i = 0;
	int k;

	dbg("%s - state %d - %d bytes.", __func__,
					garmin_data_p->state, count);

	k = garmin_data_p->outsize;
	if ((k+count) > GPS_OUT_BUFSIZ) {
		dbg("packet too large");
		garmin_data_p->outsize = 0;
		return -4;
	}

	memcpy(garmin_data_p->outbuffer+k, buf, count);
	k += count;
	garmin_data_p->outsize = k;

	if (k >= GARMIN_PKTHDR_LENGTH) {
		pktid  = getPacketId(garmin_data_p->outbuffer);
		datalen = getDataLength(garmin_data_p->outbuffer);
		i = GARMIN_PKTHDR_LENGTH + datalen;
		if (k < i)
			return 0;
	} else {
		return 0;
	}

	dbg("%s - %d bytes in buffer, %d bytes in pkt.", __func__, k, i);

	/* garmin_data_p->outbuffer now contains a complete packet */

	usb_serial_debug_data(debug, &garmin_data_p->port->dev,
				__func__, k, garmin_data_p->outbuffer);

	garmin_data_p->outsize = 0;

	if (GARMIN_LAYERID_APPL != getLayerId(garmin_data_p->outbuffer)) {
		dbg("not an application packet (%d)",
				getLayerId(garmin_data_p->outbuffer));
		return -1;
	}

	if (pktid > 255) {
		dbg("packet-id %d too large", pktid);
		return -2;
	}

	if (datalen > 255) {
		dbg("packet-size %d too large", datalen);
		return -3;
	}

	/* the serial protocol should be able to handle this packet */

	k = 0;
	src = garmin_data_p->outbuffer+GARMIN_PKTHDR_LENGTH;
	for (i = 0; i < datalen; i++) {
		if (*src++ == DLE)
			k++;
	}

	src = garmin_data_p->outbuffer+GARMIN_PKTHDR_LENGTH;
	if (k > (GARMIN_PKTHDR_LENGTH-2)) {
		/* can't add stuffing DLEs in place, move data to end
		   of buffer ... */
		dst = garmin_data_p->outbuffer+GPS_OUT_BUFSIZ-datalen;
		memcpy(dst, src, datalen);
		src = dst;
	}

	dst = garmin_data_p->outbuffer;

	*dst++ = DLE;
	*dst++ = pktid;
	cksum += pktid;
	*dst++ = datalen;
	cksum += datalen;
	if (datalen == DLE)
		*dst++ = DLE;

	for (i = 0; i < datalen; i++) {
		__u8 c = *src++;
		*dst++ = c;
		cksum += c;
		if (c == DLE)
			*dst++ = DLE;
	}

	cksum = 0xFF & -cksum;
	*dst++ = cksum;
	if (cksum == DLE)
		*dst++ = DLE;
	*dst++ = DLE;
	*dst++ = ETX;

	i = dst-garmin_data_p->outbuffer;

	send_to_tty(garmin_data_p->port, garmin_data_p->outbuffer, i);

	garmin_data_p->pkt_id = pktid;
	garmin_data_p->state  = STATE_WAIT_TTY_ACK;

	return i;
}


/*
 * Process the next pending data packet - if there is one
 */
static int gsp_next_packet(struct garmin_data *garmin_data_p)
{
	int result = 0;
	struct garmin_packet *pkt = NULL;

	while ((pkt = pkt_pop(garmin_data_p)) != NULL) {
		dbg("%s - next pkt: %d", __func__, pkt->seq);
		result = gsp_send(garmin_data_p, pkt->data, pkt->size);
		if (result > 0) {
			kfree(pkt);
			return result;
		}
		kfree(pkt);
	}
	return result;
}



/******************************************************************************
 * garmin native mode
 ******************************************************************************/


/*
 * Called for data received from tty
 *
 * The input data is expected to be in garmin usb-packet format.
 *
 * buf contains the data read, it may span more than one packet
 * or even incomplete packets
 */
static int nat_receive(struct garmin_data *garmin_data_p,
		       const unsigned char *buf, int count)
{
	unsigned long flags;
	__u8 *dest;
	int offs = 0;
	int result = count;
	int len;

	while (offs < count) {
		/* if buffer contains header, copy rest of data */
		if (garmin_data_p->insize >= GARMIN_PKTHDR_LENGTH)
			len = GARMIN_PKTHDR_LENGTH
			      +getDataLength(garmin_data_p->inbuffer);
		else
			len = GARMIN_PKTHDR_LENGTH;

		if (len >= GPS_IN_BUFSIZ) {
			/* seems to be an invalid packet, ignore rest
			   of input */
			dbg("%s - packet size too large: %d", __func__, len);
			garmin_data_p->insize = 0;
			count = 0;
			result = -EINVPKT;
		} else {
			len -= garmin_data_p->insize;
			if (len > (count-offs))
				len = (count-offs);
			if (len > 0) {
				dest = garmin_data_p->inbuffer
						+ garmin_data_p->insize;
				memcpy(dest, buf+offs, len);
				garmin_data_p->insize += len;
				offs += len;
			}
		}

		/* do we have a complete packet ? */
		if (garmin_data_p->insize >= GARMIN_PKTHDR_LENGTH) {
			len = GARMIN_PKTHDR_LENGTH+
			   getDataLength(garmin_data_p->inbuffer);
			if (garmin_data_p->insize >= len) {
				garmin_write_bulk(garmin_data_p->port,
						   garmin_data_p->inbuffer,
						   len, 0);
				garmin_data_p->insize = 0;

				/* if this was an abort-transfer command,
				   flush all queued data. */
				if (isAbortTrfCmnd(garmin_data_p->inbuffer)) {
					spin_lock_irqsave(&garmin_data_p->lock,
									flags);
					garmin_data_p->flags |= FLAGS_DROP_DATA;
					spin_unlock_irqrestore(
						&garmin_data_p->lock, flags);
					pkt_clear(garmin_data_p);
				}
			}
		}
	}
	return result;
}


/******************************************************************************
 * private packets
 ******************************************************************************/

static void priv_status_resp(struct usb_serial_port *port)
{
	struct garmin_data *garmin_data_p = usb_get_serial_port_data(port);
	__le32 *pkt = (__le32 *)garmin_data_p->privpkt;

	pkt[0] = __cpu_to_le32(GARMIN_LAYERID_PRIVATE);
	pkt[1] = __cpu_to_le32(PRIV_PKTID_INFO_RESP);
	pkt[2] = __cpu_to_le32(12);
	pkt[3] = __cpu_to_le32(VERSION_MAJOR << 16 | VERSION_MINOR);
	pkt[4] = __cpu_to_le32(garmin_data_p->mode);
	pkt[5] = __cpu_to_le32(garmin_data_p->serial_num);

	send_to_tty(port, (__u8 *)pkt, 6 * 4);
}


/******************************************************************************
 * Garmin specific driver functions
 ******************************************************************************/

static int process_resetdev_request(struct usb_serial_port *port)
{
	unsigned long flags;
	int status;
	struct garmin_data *garmin_data_p = usb_get_serial_port_data(port);

	spin_lock_irqsave(&garmin_data_p->lock, flags);
	garmin_data_p->flags &= ~(CLEAR_HALT_REQUIRED);
	garmin_data_p->state = STATE_RESET;
	garmin_data_p->serial_num = 0;
	spin_unlock_irqrestore(&garmin_data_p->lock, flags);

	usb_kill_urb(port->interrupt_in_urb);
	dbg("%s - usb_reset_device", __func__);
	status = usb_reset_device(port->serial->dev);
	if (status)
		dbg("%s - usb_reset_device failed: %d",
			__func__, status);
	return status;
}



/*
 * clear all cached data
 */
static int garmin_clear(struct garmin_data *garmin_data_p)
{
	unsigned long flags;
	int status = 0;

	/* flush all queued data */
	pkt_clear(garmin_data_p);

	spin_lock_irqsave(&garmin_data_p->lock, flags);
	garmin_data_p->insize = 0;
	garmin_data_p->outsize = 0;
	spin_unlock_irqrestore(&garmin_data_p->lock, flags);

	return status;
}


static int garmin_init_session(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct garmin_data *garmin_data_p = usb_get_serial_port_data(port);
	int status = 0;
	int i = 0;

	if (status == 0) {
		usb_kill_urb(port->interrupt_in_urb);

		dbg("%s - adding interrupt input", __func__);
		status = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
		if (status)
			dev_err(&serial->dev->dev,
			  "%s - failed submitting interrupt urb, error %d\n",
							__func__, status);
	}

	/*
	 * using the initialization method from gpsbabel. See comments in
	 * gpsbabel/jeeps/gpslibusb.c gusb_reset_toggles()
	 */
	if (status == 0) {
		dbg("%s - starting session ...", __func__);
		garmin_data_p->state = STATE_ACTIVE;

		for (i = 0; i < 3; i++) {
			status = garmin_write_bulk(port,
					GARMIN_START_SESSION_REQ,
					sizeof(GARMIN_START_SESSION_REQ), 0);

			if (status < 0)
				break;
		}

		if (status > 0)
			status = 0;
	}

	return status;
}



static int garmin_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	unsigned long flags;
	int status = 0;
	struct garmin_data *garmin_data_p = usb_get_serial_port_data(port);

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&garmin_data_p->lock, flags);
	garmin_data_p->mode  = initial_mode;
	garmin_data_p->count = 0;
	garmin_data_p->flags &= FLAGS_SESSION_REPLY1_SEEN;
	spin_unlock_irqrestore(&garmin_data_p->lock, flags);

	/* shutdown any bulk reads that might be going on */
	usb_kill_urb(port->write_urb);
	usb_kill_urb(port->read_urb);

	if (garmin_data_p->state == STATE_RESET)
		status = garmin_init_session(port);

	garmin_data_p->state = STATE_ACTIVE;
	return status;
}


static void garmin_close(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct garmin_data *garmin_data_p = usb_get_serial_port_data(port);

	dbg("%s - port %d - mode=%d state=%d flags=0x%X", __func__,
		port->number, garmin_data_p->mode,
		garmin_data_p->state, garmin_data_p->flags);

	if (!serial)
		return;

	mutex_lock(&port->serial->disc_mutex);

	if (!port->serial->disconnected)
		garmin_clear(garmin_data_p);

	/* shutdown our urbs */
	usb_kill_urb(port->read_urb);
	usb_kill_urb(port->write_urb);

	/* keep reset state so we know that we must start a new session */
	if (garmin_data_p->state != STATE_RESET)
		garmin_data_p->state = STATE_DISCONNECTED;

	mutex_unlock(&port->serial->disc_mutex);
}


static void garmin_write_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;

	if (port) {
		struct garmin_data *garmin_data_p =
					usb_get_serial_port_data(port);

		dbg("%s - port %d", __func__, port->number);

		if (GARMIN_LAYERID_APPL == getLayerId(urb->transfer_buffer)) {

			if (garmin_data_p->mode == MODE_GARMIN_SERIAL) {
				gsp_send_ack(garmin_data_p,
					((__u8 *)urb->transfer_buffer)[4]);
			}
		}
		usb_serial_port_softint(port);
	}

	/* Ignore errors that resulted from garmin_write_bulk with
	   dismiss_ack = 1 */

	/* free up the transfer buffer, as usb_free_urb() does not do this */
	kfree(urb->transfer_buffer);
}


static int garmin_write_bulk(struct usb_serial_port *port,
			      const unsigned char *buf, int count,
			      int dismiss_ack)
{
	unsigned long flags;
	struct usb_serial *serial = port->serial;
	struct garmin_data *garmin_data_p = usb_get_serial_port_data(port);
	struct urb *urb;
	unsigned char *buffer;
	int status;

	dbg("%s - port %d, state %d", __func__, port->number,
		garmin_data_p->state);

	spin_lock_irqsave(&garmin_data_p->lock, flags);
	garmin_data_p->flags &= ~FLAGS_DROP_DATA;
	spin_unlock_irqrestore(&garmin_data_p->lock, flags);

	buffer = kmalloc(count, GFP_ATOMIC);
	if (!buffer) {
		dev_err(&port->dev, "out of memory\n");
		return -ENOMEM;
	}

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		dev_err(&port->dev, "no more free urbs\n");
		kfree(buffer);
		return -ENOMEM;
	}

	memcpy(buffer, buf, count);

	usb_serial_debug_data(debug, &port->dev, __func__, count, buffer);

	usb_fill_bulk_urb(urb, serial->dev,
				usb_sndbulkpipe(serial->dev,
					port->bulk_out_endpointAddress),
				buffer, count,
				garmin_write_bulk_callback,
				dismiss_ack ? NULL : port);
	urb->transfer_flags |= URB_ZERO_PACKET;

	if (GARMIN_LAYERID_APPL == getLayerId(buffer)) {

		spin_lock_irqsave(&garmin_data_p->lock, flags);
		garmin_data_p->flags |= APP_REQ_SEEN;
		spin_unlock_irqrestore(&garmin_data_p->lock, flags);

		if (garmin_data_p->mode == MODE_GARMIN_SERIAL)  {
			pkt_clear(garmin_data_p);
			garmin_data_p->state = STATE_GSP_WAIT_DATA;
		}
	}

	/* send it down the pipe */
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		dev_err(&port->dev,
		   "%s - usb_submit_urb(write bulk) failed with status = %d\n",
				__func__, status);
		count = status;
	}

	/* we are done with this urb, so let the host driver
	 * really free it when it is finished with it */
	usb_free_urb(urb);

	return count;
}

static int garmin_write(struct tty_struct *tty, struct usb_serial_port *port,
					 const unsigned char *buf, int count)
{
	int pktid, pktsiz, len;
	struct garmin_data *garmin_data_p = usb_get_serial_port_data(port);
	__le32 *privpkt = (__le32 *)garmin_data_p->privpkt;

	usb_serial_debug_data(debug, &port->dev, __func__, count, buf);

	if (garmin_data_p->state == STATE_RESET)
		return -EIO;

	/* check for our private packets */
	if (count >= GARMIN_PKTHDR_LENGTH) {
		len = PRIVPKTSIZ;
		if (count < len)
			len = count;

		memcpy(garmin_data_p->privpkt, buf, len);

		pktsiz = getDataLength(garmin_data_p->privpkt);
		pktid  = getPacketId(garmin_data_p->privpkt);

		if (count == (GARMIN_PKTHDR_LENGTH+pktsiz)
		    && GARMIN_LAYERID_PRIVATE ==
				getLayerId(garmin_data_p->privpkt)) {

			dbg("%s - processing private request %d",
				__func__, pktid);

			/* drop all unfinished transfers */
			garmin_clear(garmin_data_p);

			switch (pktid) {

			case PRIV_PKTID_SET_DEBUG:
				if (pktsiz != 4)
					return -EINVPKT;
				debug = __le32_to_cpu(privpkt[3]);
				dbg("%s - debug level set to 0x%X",
					__func__, debug);
				break;

			case PRIV_PKTID_SET_MODE:
				if (pktsiz != 4)
					return -EINVPKT;
				garmin_data_p->mode = __le32_to_cpu(privpkt[3]);
				dbg("%s - mode set to %d",
					__func__, garmin_data_p->mode);
				break;

			case PRIV_PKTID_INFO_REQ:
				priv_status_resp(port);
				break;

			case PRIV_PKTID_RESET_REQ:
				process_resetdev_request(port);
				break;

			case PRIV_PKTID_SET_DEF_MODE:
				if (pktsiz != 4)
					return -EINVPKT;
				initial_mode = __le32_to_cpu(privpkt[3]);
				dbg("%s - initial_mode set to %d",
					__func__,
					garmin_data_p->mode);
				break;
			}
			return count;
		}
	}

	if (garmin_data_p->mode == MODE_GARMIN_SERIAL) {
		return gsp_receive(garmin_data_p, buf, count);
	} else {	/* MODE_NATIVE */
		return nat_receive(garmin_data_p, buf, count);
	}
}


static int garmin_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	/*
	 * Report back the bytes currently available in the output buffer.
	 */
	struct garmin_data *garmin_data_p = usb_get_serial_port_data(port);
	return GPS_OUT_BUFSIZ-garmin_data_p->outsize;
}


static void garmin_read_process(struct garmin_data *garmin_data_p,
				 unsigned char *data, unsigned data_length,
				 int bulk_data)
{
	unsigned long flags;

	if (garmin_data_p->flags & FLAGS_DROP_DATA) {
		/* abort-transfer cmd is actice */
		dbg("%s - pkt dropped", __func__);
	} else if (garmin_data_p->state != STATE_DISCONNECTED &&
		garmin_data_p->state != STATE_RESET) {

		/* if throttling is active or postprecessing is required
		   put the received data in the input queue, otherwise
		   send it directly to the tty port */
		if (garmin_data_p->flags & FLAGS_QUEUING) {
			pkt_add(garmin_data_p, data, data_length);
		} else if (bulk_data || 
			   getLayerId(data) == GARMIN_LAYERID_APPL) {

			spin_lock_irqsave(&garmin_data_p->lock, flags);
			garmin_data_p->flags |= APP_RESP_SEEN;
			spin_unlock_irqrestore(&garmin_data_p->lock, flags);

			if (garmin_data_p->mode == MODE_GARMIN_SERIAL) {
				pkt_add(garmin_data_p, data, data_length);
			} else {
				send_to_tty(garmin_data_p->port, data,
						data_length);
			}
		}
		/* ignore system layer packets ... */
	}
}


static void garmin_read_bulk_callback(struct urb *urb)
{
	unsigned long flags;
	struct usb_serial_port *port = urb->context;
	struct usb_serial *serial =  port->serial;
	struct garmin_data *garmin_data_p = usb_get_serial_port_data(port);
	unsigned char *data = urb->transfer_buffer;
	int status = urb->status;
	int retval;

	dbg("%s - port %d", __func__, port->number);

	if (!serial) {
		dbg("%s - bad serial pointer, exiting", __func__);
		return;
	}

	if (status) {
		dbg("%s - nonzero read bulk status received: %d",
			__func__, status);
		return;
	}

	usb_serial_debug_data(debug, &port->dev,
				__func__, urb->actual_length, data);

	garmin_read_process(garmin_data_p, data, urb->actual_length, 1);

	if (urb->actual_length == 0 &&
			0 != (garmin_data_p->flags & FLAGS_BULK_IN_RESTART)) {
		spin_lock_irqsave(&garmin_data_p->lock, flags);
		garmin_data_p->flags &= ~FLAGS_BULK_IN_RESTART;
		spin_unlock_irqrestore(&garmin_data_p->lock, flags);
		retval = usb_submit_urb(port->read_urb, GFP_ATOMIC);
		if (retval)
			dev_err(&port->dev,
				"%s - failed resubmitting read urb, error %d\n",
				__func__, retval);
	} else if (urb->actual_length > 0) {
		/* Continue trying to read until nothing more is received  */
		if (0 == (garmin_data_p->flags & FLAGS_THROTTLED)) {
			retval = usb_submit_urb(port->read_urb, GFP_ATOMIC);
			if (retval)
				dev_err(&port->dev,
					"%s - failed resubmitting read urb, "
					"error %d\n", __func__, retval);
		}
	} else {
		dbg("%s - end of bulk data", __func__);
		spin_lock_irqsave(&garmin_data_p->lock, flags);
		garmin_data_p->flags &= ~FLAGS_BULK_IN_ACTIVE;
		spin_unlock_irqrestore(&garmin_data_p->lock, flags);
	}
}


static void garmin_read_int_callback(struct urb *urb)
{
	unsigned long flags;
	int retval;
	struct usb_serial_port *port = urb->context;
	struct garmin_data *garmin_data_p = usb_get_serial_port_data(port);
	unsigned char *data = urb->transfer_buffer;
	int status = urb->status;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d",
			__func__, status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d",
			__func__, status);
		return;
	}

	usb_serial_debug_data(debug, &port->dev, __func__,
				urb->actual_length, urb->transfer_buffer);

	if (urb->actual_length == sizeof(GARMIN_BULK_IN_AVAIL_REPLY) &&
	    0 == memcmp(data, GARMIN_BULK_IN_AVAIL_REPLY,
				sizeof(GARMIN_BULK_IN_AVAIL_REPLY))) {

		dbg("%s - bulk data available.", __func__);

		if (0 == (garmin_data_p->flags & FLAGS_BULK_IN_ACTIVE)) {

			/* bulk data available */
			retval = usb_submit_urb(port->read_urb, GFP_ATOMIC);
			if (retval) {
				dev_err(&port->dev,
				 "%s - failed submitting read urb, error %d\n",
							__func__, retval);
			} else {
				spin_lock_irqsave(&garmin_data_p->lock, flags);
				garmin_data_p->flags |= FLAGS_BULK_IN_ACTIVE;
				spin_unlock_irqrestore(&garmin_data_p->lock,
									flags);
			}
		} else {
			/* bulk-in transfer still active */
			spin_lock_irqsave(&garmin_data_p->lock, flags);
			garmin_data_p->flags |= FLAGS_BULK_IN_RESTART;
			spin_unlock_irqrestore(&garmin_data_p->lock, flags);
		}

	} else if (urb->actual_length == (4+sizeof(GARMIN_START_SESSION_REPLY))
			 && 0 == memcmp(data, GARMIN_START_SESSION_REPLY,
					sizeof(GARMIN_START_SESSION_REPLY))) {

		spin_lock_irqsave(&garmin_data_p->lock, flags);
		garmin_data_p->flags |= FLAGS_SESSION_REPLY1_SEEN;
		spin_unlock_irqrestore(&garmin_data_p->lock, flags);

		/* save the serial number */
		garmin_data_p->serial_num = __le32_to_cpup(
					(__le32 *)(data+GARMIN_PKTHDR_LENGTH));

		dbg("%s - start-of-session reply seen - serial %u.",
			__func__, garmin_data_p->serial_num);
	}

	garmin_read_process(garmin_data_p, data, urb->actual_length, 0);

	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&urb->dev->dev,
			"%s - Error %d submitting interrupt urb\n",
			__func__, retval);
}


/*
 * Sends the next queued packt to the tty port (garmin native mode only)
 * and then sets a timer to call itself again until all queued data
 * is sent.
 */
static int garmin_flush_queue(struct garmin_data *garmin_data_p)
{
	unsigned long flags;
	struct garmin_packet *pkt;

	if ((garmin_data_p->flags & FLAGS_THROTTLED) == 0) {
		pkt = pkt_pop(garmin_data_p);
		if (pkt != NULL) {
			send_to_tty(garmin_data_p->port, pkt->data, pkt->size);
			kfree(pkt);
			mod_timer(&garmin_data_p->timer, (1)+jiffies);

		} else {
			spin_lock_irqsave(&garmin_data_p->lock, flags);
			garmin_data_p->flags &= ~FLAGS_QUEUING;
			spin_unlock_irqrestore(&garmin_data_p->lock, flags);
		}
	}
	return 0;
}


static void garmin_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct garmin_data *garmin_data_p = usb_get_serial_port_data(port);

	dbg("%s - port %d", __func__, port->number);
	/* set flag, data received will be put into a queue
	   for later processing */
	spin_lock_irq(&garmin_data_p->lock);
	garmin_data_p->flags |= FLAGS_QUEUING|FLAGS_THROTTLED;
	spin_unlock_irq(&garmin_data_p->lock);
}


static void garmin_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct garmin_data *garmin_data_p = usb_get_serial_port_data(port);
	int status;

	dbg("%s - port %d", __func__, port->number);
	spin_lock_irq(&garmin_data_p->lock);
	garmin_data_p->flags &= ~FLAGS_THROTTLED;
	spin_unlock_irq(&garmin_data_p->lock);

	/* in native mode send queued data to tty, in
	   serial mode nothing needs to be done here */
	if (garmin_data_p->mode == MODE_NATIVE)
		garmin_flush_queue(garmin_data_p);

	if (0 != (garmin_data_p->flags & FLAGS_BULK_IN_ACTIVE)) {
		status = usb_submit_urb(port->read_urb, GFP_KERNEL);
		if (status)
			dev_err(&port->dev,
				"%s - failed resubmitting read urb, error %d\n",
				__func__, status);
	}
}

/*
 * The timer is currently only used to send queued packets to
 * the tty in cases where the protocol provides no own handshaking
 * to initiate the transfer.
 */
static void timeout_handler(unsigned long data)
{
	struct garmin_data *garmin_data_p = (struct garmin_data *) data;

	/* send the next queued packet to the tty port */
	if (garmin_data_p->mode == MODE_NATIVE)
		if (garmin_data_p->flags & FLAGS_QUEUING)
			garmin_flush_queue(garmin_data_p);
}



static int garmin_attach(struct usb_serial *serial)
{
	int status = 0;
	struct usb_serial_port *port = serial->port[0];
	struct garmin_data *garmin_data_p = NULL;

	dbg("%s", __func__);

	garmin_data_p = kzalloc(sizeof(struct garmin_data), GFP_KERNEL);
	if (garmin_data_p == NULL) {
		dev_err(&port->dev, "%s - Out of memory\n", __func__);
		return -ENOMEM;
	}
	init_timer(&garmin_data_p->timer);
	spin_lock_init(&garmin_data_p->lock);
	INIT_LIST_HEAD(&garmin_data_p->pktlist);
	/* garmin_data_p->timer.expires = jiffies + session_timeout; */
	garmin_data_p->timer.data = (unsigned long)garmin_data_p;
	garmin_data_p->timer.function = timeout_handler;
	garmin_data_p->port = port;
	garmin_data_p->state = 0;
	garmin_data_p->flags = 0;
	garmin_data_p->count = 0;
	usb_set_serial_port_data(port, garmin_data_p);

	status = garmin_init_session(port);

	return status;
}


static void garmin_disconnect(struct usb_serial *serial)
{
	struct usb_serial_port *port = serial->port[0];
	struct garmin_data *garmin_data_p = usb_get_serial_port_data(port);

	dbg("%s", __func__);

	usb_kill_urb(port->interrupt_in_urb);
	del_timer_sync(&garmin_data_p->timer);
}


static void garmin_release(struct usb_serial *serial)
{
	struct usb_serial_port *port = serial->port[0];
	struct garmin_data *garmin_data_p = usb_get_serial_port_data(port);

	dbg("%s", __func__);

	kfree(garmin_data_p);
}


/* All of the device info needed */
static struct usb_serial_driver garmin_device = {
	.driver = {
		.owner       = THIS_MODULE,
		.name        = "garmin_gps",
	},
	.description         = "Garmin GPS usb/tty",
	.usb_driver          = &garmin_driver,
	.id_table            = id_table,
	.num_ports           = 1,
	.open                = garmin_open,
	.close               = garmin_close,
	.throttle            = garmin_throttle,
	.unthrottle          = garmin_unthrottle,
	.attach              = garmin_attach,
	.disconnect          = garmin_disconnect,
	.release             = garmin_release,
	.write               = garmin_write,
	.write_room          = garmin_write_room,
	.write_bulk_callback = garmin_write_bulk_callback,
	.read_bulk_callback  = garmin_read_bulk_callback,
	.read_int_callback   = garmin_read_int_callback,
};



static int __init garmin_init(void)
{
	int retval;

	retval = usb_serial_register(&garmin_device);
	if (retval)
		goto failed_garmin_register;
	retval = usb_register(&garmin_driver);
	if (retval)
		goto failed_usb_register;
	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":"
	       DRIVER_DESC "\n");

	return 0;
failed_usb_register:
	usb_serial_deregister(&garmin_device);
failed_garmin_register:
	return retval;
}


static void __exit garmin_exit(void)
{
	usb_deregister(&garmin_driver);
	usb_serial_deregister(&garmin_device);
}




module_init(garmin_init);
module_exit(garmin_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(debug, "Debug enabled or not");
module_param(initial_mode, int, S_IRUGO);
MODULE_PARM_DESC(initial_mode, "Initial mode");

