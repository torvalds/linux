/*
 * Host Side support for RNDIS Networking Links
 * Copyright (C) 2005 by David Brownell
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// #define	DEBUG			// error path messages, extra info
// #define	VERBOSE			// more; success messages

#include <linux/config.h>
#ifdef	CONFIG_USB_DEBUG
#   define DEBUG
#endif
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/usb_cdc.h>

#include "usbnet.h"


/*
 * RNDIS is NDIS remoted over USB.  It's a MSFT variant of CDC ACM ... of
 * course ACM was intended for modems, not Ethernet links!  USB's standard
 * for Ethernet links is "CDC Ethernet", which is significantly simpler.
 */

/*
 * CONTROL uses CDC "encapsulated commands" with funky notifications.
 *  - control-out:  SEND_ENCAPSULATED
 *  - interrupt-in:  RESPONSE_AVAILABLE
 *  - control-in:  GET_ENCAPSULATED
 *
 * We'll try to ignore the RESPONSE_AVAILABLE notifications.
 */
struct rndis_msg_hdr {
	__le32	msg_type;			/* RNDIS_MSG_* */
	__le32	msg_len;
	// followed by data that varies between messages
	__le32	request_id;
	__le32	status;
	// ... and more
} __attribute__ ((packed));

/* RNDIS defines this (absurdly huge) control timeout */
#define	RNDIS_CONTROL_TIMEOUT_MS	(10 * 1000)


#define ccpu2 __constant_cpu_to_le32

#define RNDIS_MSG_COMPLETION	ccpu2(0x80000000)

/* codes for "msg_type" field of rndis messages;
 * only the data channel uses packet messages (maybe batched);
 * everything else goes on the control channel.
 */
#define RNDIS_MSG_PACKET	ccpu2(0x00000001)	/* 1-N packets */
#define RNDIS_MSG_INIT		ccpu2(0x00000002)
#define RNDIS_MSG_INIT_C 	(RNDIS_MSG_INIT|RNDIS_MSG_COMPLETION)
#define RNDIS_MSG_HALT		ccpu2(0x00000003)
#define RNDIS_MSG_QUERY		ccpu2(0x00000004)
#define RNDIS_MSG_QUERY_C 	(RNDIS_MSG_QUERY|RNDIS_MSG_COMPLETION)
#define RNDIS_MSG_SET		ccpu2(0x00000005)
#define RNDIS_MSG_SET_C 	(RNDIS_MSG_SET|RNDIS_MSG_COMPLETION)
#define RNDIS_MSG_RESET		ccpu2(0x00000006)
#define RNDIS_MSG_RESET_C 	(RNDIS_MSG_RESET|RNDIS_MSG_COMPLETION)
#define RNDIS_MSG_INDICATE	ccpu2(0x00000007)
#define RNDIS_MSG_KEEPALIVE	ccpu2(0x00000008)
#define RNDIS_MSG_KEEPALIVE_C 	(RNDIS_MSG_KEEPALIVE|RNDIS_MSG_COMPLETION)

/* codes for "status" field of completion messages */
#define	RNDIS_STATUS_SUCCESS		ccpu2(0x00000000)
#define	RNDIS_STATUS_FAILURE		ccpu2(0xc0000001)
#define	RNDIS_STATUS_INVALID_DATA	ccpu2(0xc0010015)
#define	RNDIS_STATUS_NOT_SUPPORTED	ccpu2(0xc00000bb)
#define	RNDIS_STATUS_MEDIA_CONNECT	ccpu2(0x4001000b)
#define	RNDIS_STATUS_MEDIA_DISCONNECT	ccpu2(0x4001000c)


struct rndis_data_hdr {
	__le32	msg_type;		/* RNDIS_MSG_PACKET */
	__le32	msg_len;		// rndis_data_hdr + data_len + pad
	__le32	data_offset;		// 36 -- right after header
	__le32	data_len;		// ... real packet size

	__le32	oob_data_offset;	// zero
	__le32	oob_data_len;		// zero
	__le32	num_oob;		// zero
	__le32	packet_data_offset;	// zero

	__le32	packet_data_len;	// zero
	__le32	vc_handle;		// zero
	__le32	reserved;		// zero
} __attribute__ ((packed));

struct rndis_init {		/* OUT */
	// header and:
	__le32	msg_type;			/* RNDIS_MSG_INIT */
	__le32	msg_len;			// 24
	__le32	request_id;
	__le32	major_version;			// of rndis (1.0)
	__le32	minor_version;
	__le32	max_transfer_size;
} __attribute__ ((packed));

struct rndis_init_c {		/* IN */
	// header and:
	__le32	msg_type;			/* RNDIS_MSG_INIT_C */
	__le32	msg_len;
	__le32	request_id;
	__le32	status;
	__le32	major_version;			// of rndis (1.0)
	__le32	minor_version;
	__le32	device_flags;
	__le32	medium;				// zero == 802.3
	__le32	max_packets_per_message;
	__le32	max_transfer_size;
	__le32	packet_alignment;		// max 7; (1<<n) bytes
	__le32	af_list_offset;			// zero
	__le32	af_list_size;			// zero
} __attribute__ ((packed));

struct rndis_halt {		/* OUT (no reply) */
	// header and:
	__le32	msg_type;			/* RNDIS_MSG_HALT */
	__le32	msg_len;
	__le32	request_id;
} __attribute__ ((packed));

struct rndis_query {		/* OUT */
	// header and:
	__le32	msg_type;			/* RNDIS_MSG_QUERY */
	__le32	msg_len;
	__le32	request_id;
	__le32	oid;
	__le32	len;
	__le32	offset;
/*?*/	__le32	handle;				// zero
} __attribute__ ((packed));

struct rndis_query_c {		/* IN */
	// header and:
	__le32	msg_type;			/* RNDIS_MSG_QUERY_C */
	__le32	msg_len;
	__le32	request_id;
	__le32	status;
	__le32	len;
	__le32	offset;
} __attribute__ ((packed));

struct rndis_set {		/* OUT */
	// header and:
	__le32	msg_type;			/* RNDIS_MSG_SET */
	__le32	msg_len;
	__le32	request_id;
	__le32	oid;
	__le32	len;
	__le32	offset;
/*?*/	__le32	handle;				// zero
} __attribute__ ((packed));

struct rndis_set_c {		/* IN */
	// header and:
	__le32	msg_type;			/* RNDIS_MSG_SET_C */
	__le32	msg_len;
	__le32	request_id;
	__le32	status;
} __attribute__ ((packed));

struct rndis_reset {		/* IN */
	// header and:
	__le32	msg_type;			/* RNDIS_MSG_RESET */
	__le32	msg_len;
	__le32	reserved;
} __attribute__ ((packed));

struct rndis_reset_c {		/* OUT */
	// header and:
	__le32	msg_type;			/* RNDIS_MSG_RESET_C */
	__le32	msg_len;
	__le32	status;
	__le32	addressing_lost;
} __attribute__ ((packed));

struct rndis_indicate {		/* IN (unrequested) */
	// header and:
	__le32	msg_type;			/* RNDIS_MSG_INDICATE */
	__le32	msg_len;
	__le32	status;
	__le32	length;
	__le32	offset;
/**/	__le32	diag_status;
	__le32	error_offset;
/**/	__le32	message;
} __attribute__ ((packed));

struct rndis_keepalive {	/* OUT (optionally IN) */
	// header and:
	__le32	msg_type;			/* RNDIS_MSG_KEEPALIVE */
	__le32	msg_len;
	__le32	request_id;
} __attribute__ ((packed));

struct rndis_keepalive_c {	/* IN (optionally OUT) */
	// header and:
	__le32	msg_type;			/* RNDIS_MSG_KEEPALIVE_C */
	__le32	msg_len;
	__le32	request_id;
	__le32	status;
} __attribute__ ((packed));

/* NOTE:  about 30 OIDs are "mandatory" for peripherals to support ... and
 * there are gobs more that may optionally be supported.  We'll avoid as much
 * of that mess as possible.
 */
#define OID_802_3_PERMANENT_ADDRESS	ccpu2(0x01010101)
#define OID_GEN_CURRENT_PACKET_FILTER	ccpu2(0x0001010e)

/*
 * RNDIS notifications from device: command completion; "reverse"
 * keepalives; etc
 */
static void rndis_status(struct usbnet *dev, struct urb *urb)
{
	devdbg(dev, "rndis status urb, len %d stat %d",
		urb->actual_length, urb->status);
	// FIXME for keepalives, respond immediately (asynchronously)
	// if not an RNDIS status, do like cdc_status(dev,urb) does
}

/*
 * RPC done RNDIS-style.  Caller guarantees:
 * - message is properly byteswapped
 * - there's no other request pending
 * - buf can hold up to 1KB response (required by RNDIS spec)
 * On return, the first few entries are already byteswapped.
 *
 * Call context is likely probe(), before interface name is known,
 * which is why we won't try to use it in the diagnostics.
 */
static int rndis_command(struct usbnet *dev, struct rndis_msg_hdr *buf)
{
	struct cdc_state	*info = (void *) &dev->data;
	int			retval;
	unsigned		count;
	__le32			rsp;
	u32			xid = 0, msg_len, request_id;

	/* REVISIT when this gets called from contexts other than probe() or
	 * disconnect(): either serialize, or dispatch responses on xid
	 */

	/* Issue the request; don't bother byteswapping our xid */
	if (likely(buf->msg_type != RNDIS_MSG_HALT
			&& buf->msg_type != RNDIS_MSG_RESET)) {
		xid = dev->xid++;
		if (!xid)
			xid = dev->xid++;
		buf->request_id = (__force __le32) xid;
	}
	retval = usb_control_msg(dev->udev,
		usb_sndctrlpipe(dev->udev, 0),
		USB_CDC_SEND_ENCAPSULATED_COMMAND,
		USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		0, info->u->bMasterInterface0,
		buf, le32_to_cpu(buf->msg_len),
		RNDIS_CONTROL_TIMEOUT_MS);
	if (unlikely(retval < 0 || xid == 0))
		return retval;

	// FIXME Seems like some devices discard responses when
	// we time out and cancel our "get response" requests...
	// so, this is fragile.  Probably need to poll for status.

	/* ignore status endpoint, just poll the control channel;
	 * the request probably completed immediately
	 */
	rsp = buf->msg_type | RNDIS_MSG_COMPLETION;
	for (count = 0; count < 10; count++) {
		memset(buf, 0, 1024);
		retval = usb_control_msg(dev->udev,
			usb_rcvctrlpipe(dev->udev, 0),
			USB_CDC_GET_ENCAPSULATED_RESPONSE,
			USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			0, info->u->bMasterInterface0,
			buf, 1024,
			RNDIS_CONTROL_TIMEOUT_MS);
		if (likely(retval >= 8)) {
			msg_len = le32_to_cpu(buf->msg_len);
			request_id = (__force u32) buf->request_id;
			if (likely(buf->msg_type == rsp)) {
				if (likely(request_id == xid)) {
					if (unlikely(rsp == RNDIS_MSG_RESET_C))
						return 0;
					if (likely(RNDIS_STATUS_SUCCESS
							== buf->status))
						return 0;
					dev_dbg(&info->control->dev,
						"rndis reply status %08x\n",
						le32_to_cpu(buf->status));
					return -EL3RST;
				}
				dev_dbg(&info->control->dev,
					"rndis reply id %d expected %d\n",
					request_id, xid);
				/* then likely retry */
			} else switch (buf->msg_type) {
			case RNDIS_MSG_INDICATE: {	/* fault */
				// struct rndis_indicate *msg = (void *)buf;
				dev_info(&info->control->dev,
					 "rndis fault indication\n");
				}
				break;
			case RNDIS_MSG_KEEPALIVE: {	/* ping */
				struct rndis_keepalive_c *msg = (void *)buf;

				msg->msg_type = RNDIS_MSG_KEEPALIVE_C;
				msg->msg_len = ccpu2(sizeof *msg);
				msg->status = RNDIS_STATUS_SUCCESS;
				retval = usb_control_msg(dev->udev,
					usb_sndctrlpipe(dev->udev, 0),
					USB_CDC_SEND_ENCAPSULATED_COMMAND,
					USB_TYPE_CLASS | USB_RECIP_INTERFACE,
					0, info->u->bMasterInterface0,
					msg, sizeof *msg,
					RNDIS_CONTROL_TIMEOUT_MS);
				if (unlikely(retval < 0))
					dev_dbg(&info->control->dev,
						"rndis keepalive err %d\n",
						retval);
				}
				break;
			default:
				dev_dbg(&info->control->dev,
					"unexpected rndis msg %08x len %d\n",
					le32_to_cpu(buf->msg_type), msg_len);
			}
		} else {
			/* device probably issued a protocol stall; ignore */
			dev_dbg(&info->control->dev,
				"rndis response error, code %d\n", retval);
		}
		msleep(2);
	}
	dev_dbg(&info->control->dev, "rndis response timeout\n");
	return -ETIMEDOUT;
}

static int rndis_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int			retval;
	struct net_device	*net = dev->net;
	union {
		void			*buf;
		struct rndis_msg_hdr	*header;
		struct rndis_init	*init;
		struct rndis_init_c	*init_c;
		struct rndis_query	*get;
		struct rndis_query_c	*get_c;
		struct rndis_set	*set;
		struct rndis_set_c	*set_c;
	} u;
	u32			tmp;

	/* we can't rely on i/o from stack working, or stack allocation */
	u.buf = kmalloc(1024, GFP_KERNEL);
	if (!u.buf)
		return -ENOMEM;
	retval = usbnet_generic_cdc_bind(dev, intf);
	if (retval < 0)
		goto done;

	net->hard_header_len += sizeof (struct rndis_data_hdr);

	/* initialize; max transfer is 16KB at full speed */
	u.init->msg_type = RNDIS_MSG_INIT;
	u.init->msg_len = ccpu2(sizeof *u.init);
	u.init->major_version = ccpu2(1);
	u.init->minor_version = ccpu2(0);
	u.init->max_transfer_size = ccpu2(net->mtu + net->hard_header_len);

	retval = rndis_command(dev, u.header);
	if (unlikely(retval < 0)) {
		/* it might not even be an RNDIS device!! */
		dev_err(&intf->dev, "RNDIS init failed, %d\n", retval);
fail:
		usb_driver_release_interface(driver_of(intf),
			((struct cdc_state *)&(dev->data))->data);
		goto done;
	}
	dev->hard_mtu = le32_to_cpu(u.init_c->max_transfer_size);
	/* REVISIT:  peripheral "alignment" request is ignored ... */
	dev_dbg(&intf->dev, "hard mtu %u, align %d\n", dev->hard_mtu,
		1 << le32_to_cpu(u.init_c->packet_alignment));

	/* get designated host ethernet address */
	memset(u.get, 0, sizeof *u.get);
	u.get->msg_type = RNDIS_MSG_QUERY;
	u.get->msg_len = ccpu2(sizeof *u.get);
	u.get->oid = OID_802_3_PERMANENT_ADDRESS;

	retval = rndis_command(dev, u.header);
	if (unlikely(retval < 0)) {
		dev_err(&intf->dev, "rndis get ethaddr, %d\n", retval);
		goto fail;
	}
	tmp = le32_to_cpu(u.get_c->offset);
	if (unlikely((tmp + 8) > (1024 - ETH_ALEN)
			|| u.get_c->len != ccpu2(ETH_ALEN))) {
		dev_err(&intf->dev, "rndis ethaddr off %d len %d ?\n",
			tmp, le32_to_cpu(u.get_c->len));
		retval = -EDOM;
		goto fail;
	}
	memcpy(net->dev_addr, tmp + (char *)&u.get_c->request_id, ETH_ALEN);

	/* set a nonzero filter to enable data transfers */
	memset(u.set, 0, sizeof *u.set);
	u.set->msg_type = RNDIS_MSG_SET;
	u.set->msg_len = ccpu2(4 + sizeof *u.set);
	u.set->oid = OID_GEN_CURRENT_PACKET_FILTER;
	u.set->len = ccpu2(4);
	u.set->offset = ccpu2((sizeof *u.set) - 8);
	*(__le32 *)(u.buf + sizeof *u.set) = ccpu2(DEFAULT_FILTER);

	retval = rndis_command(dev, u.header);
	if (unlikely(retval < 0)) {
		dev_err(&intf->dev, "rndis set packet filter, %d\n", retval);
		goto fail;
	}

	retval = 0;
done:
	kfree(u.buf);
	return retval;
}

static void rndis_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	struct rndis_halt	*halt;

	/* try to clear any rndis state/activity (no i/o from stack!) */
	halt = kcalloc(1, sizeof *halt, SLAB_KERNEL);
	if (halt) {
		halt->msg_type = RNDIS_MSG_HALT;
		halt->msg_len = ccpu2(sizeof *halt);
		(void) rndis_command(dev, (void *)halt);
		kfree(halt);
	}

	return usbnet_cdc_unbind(dev, intf);
}

/*
 * DATA -- host must not write zlps
 */
static int rndis_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	/* peripheral may have batched packets to us... */
	while (likely(skb->len)) {
		struct rndis_data_hdr	*hdr = (void *)skb->data;
		struct sk_buff		*skb2;
		u32			msg_len, data_offset, data_len;

		msg_len = le32_to_cpu(hdr->msg_len);
		data_offset = le32_to_cpu(hdr->data_offset);
		data_len = le32_to_cpu(hdr->data_len);

		/* don't choke if we see oob, per-packet data, etc */
		if (unlikely(hdr->msg_type != RNDIS_MSG_PACKET
				|| skb->len < msg_len
				|| (data_offset + data_len + 8) > msg_len)) {
			dev->stats.rx_frame_errors++;
			devdbg(dev, "bad rndis message %d/%d/%d/%d, len %d",
				le32_to_cpu(hdr->msg_type),
				msg_len, data_offset, data_len, skb->len);
			return 0;
		}
		skb_pull(skb, 8 + data_offset);

		/* at most one packet left? */
		if (likely((data_len - skb->len) <= sizeof *hdr)) {
			skb_trim(skb, data_len);
			break;
		}

		/* try to return all the packets in the batch */
		skb2 = skb_clone(skb, GFP_ATOMIC);
		if (unlikely(!skb2))
			break;
		skb_pull(skb, msg_len - sizeof *hdr);
		skb_trim(skb2, data_len);
		usbnet_skb_return(dev, skb2);
	}

	/* caller will usbnet_skb_return the remaining packet */
	return 1;
}

static struct sk_buff *
rndis_tx_fixup(struct usbnet *dev, struct sk_buff *skb, unsigned flags)
{
	struct rndis_data_hdr	*hdr;
	struct sk_buff		*skb2;
	unsigned		len = skb->len;

	if (likely(!skb_cloned(skb))) {
		int	room = skb_headroom(skb);

		/* enough head room as-is? */
		if (unlikely((sizeof *hdr) <= room))
			goto fill;

		/* enough room, but needs to be readjusted? */
		room += skb_tailroom(skb);
		if (likely((sizeof *hdr) <= room)) {
			skb->data = memmove(skb->head + sizeof *hdr,
					    skb->data, len);
			skb->tail = skb->data + len;
			goto fill;
		}
	}

	/* create a new skb, with the correct size (and tailpad) */
	skb2 = skb_copy_expand(skb, sizeof *hdr, 1, flags);
	dev_kfree_skb_any(skb);
	if (unlikely(!skb2))
		return skb2;
	skb = skb2;

	/* fill out the RNDIS header.  we won't bother trying to batch
	 * packets; Linux minimizes wasted bandwidth through tx queues.
	 */
fill:
	hdr = (void *) __skb_push(skb, sizeof *hdr);
	memset(hdr, 0, sizeof *hdr);
	hdr->msg_type = RNDIS_MSG_PACKET;
	hdr->msg_len = cpu_to_le32(skb->len);
	hdr->data_offset = ccpu2(sizeof(*hdr) - 8);
	hdr->data_len = cpu_to_le32(len);

	/* FIXME make the last packet always be short ... */
	return skb;
}


static const struct driver_info	rndis_info = {
	.description =	"RNDIS device",
	.flags =	FLAG_ETHER | FLAG_FRAMING_RN,
	.bind =		rndis_bind,
	.unbind =	rndis_unbind,
	.status =	rndis_status,
	.rx_fixup =	rndis_rx_fixup,
	.tx_fixup =	rndis_tx_fixup,
};

#undef ccpu2


/*-------------------------------------------------------------------------*/

static const struct usb_device_id	products [] = {
{
	/* RNDIS is MSFT's un-official variant of CDC ACM */
	USB_INTERFACE_INFO(USB_CLASS_COMM, 2 /* ACM */, 0x0ff),
	.driver_info = (unsigned long) &rndis_info,
},
	{ },		// END
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver rndis_driver = {
	.owner =	THIS_MODULE,
	.name =		"rndis_host",
	.id_table =	products,
	.probe =	usbnet_probe,
	.disconnect =	usbnet_disconnect,
	.suspend =	usbnet_suspend,
	.resume =	usbnet_resume,
};

static int __init rndis_init(void)
{
 	return usb_register(&rndis_driver);
}
module_init(rndis_init);

static void __exit rndis_exit(void)
{
 	usb_deregister(&rndis_driver);
}
module_exit(rndis_exit);

MODULE_AUTHOR("David Brownell");
MODULE_DESCRIPTION("USB Host side RNDIS driver");
MODULE_LICENSE("GPL");
