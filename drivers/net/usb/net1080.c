/*
 * Net1080 based USB host-to-host cables
 * Copyright (C) 2000-2005 by David Brownell
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mii.h>
#include <linux/usb.h>

#include <asm/unaligned.h>

#include "usbnet.h"


/*
 * Netchip 1080 driver ... http://www.netchip.com
 * (Sept 2004:  End-of-life announcement has been sent.)
 * Used in (some) LapLink cables
 */

#define frame_errors	data[1]

/*
 * NetChip framing of ethernet packets, supporting additional error
 * checks for links that may drop bulk packets from inside messages.
 * Odd USB length == always short read for last usb packet.
 *	- nc_header
 *	- Ethernet header (14 bytes)
 *	- payload
 *	- (optional padding byte, if needed so length becomes odd)
 *	- nc_trailer
 *
 * This framing is to be avoided for non-NetChip devices.
 */

struct nc_header {		// packed:
	__le16	hdr_len;		// sizeof nc_header (LE, all)
	__le16	packet_len;		// payload size (including ethhdr)
	__le16	packet_id;		// detects dropped packets
#define MIN_HEADER	6

	// all else is optional, and must start with:
	// __le16	vendorId;	// from usb-if
	// __le16	productId;
} __attribute__((__packed__));

#define	PAD_BYTE	((unsigned char)0xAC)

struct nc_trailer {
	__le16	packet_id;
} __attribute__((__packed__));

// packets may use FLAG_FRAMING_NC and optional pad
#define FRAMED_SIZE(mtu) (sizeof (struct nc_header) \
				+ sizeof (struct ethhdr) \
				+ (mtu) \
				+ 1 \
				+ sizeof (struct nc_trailer))

#define MIN_FRAMED	FRAMED_SIZE(0)

/* packets _could_ be up to 64KB... */
#define NC_MAX_PACKET	32767


/*
 * Zero means no timeout; else, how long a 64 byte bulk packet may be queued
 * before the hardware drops it.  If that's done, the driver will need to
 * frame network packets to guard against the dropped USB packets.  The win32
 * driver sets this for both sides of the link.
 */
#define	NC_READ_TTL_MS	((u8)255)	// ms

/*
 * We ignore most registers and EEPROM contents.
 */
#define	REG_USBCTL	((u8)0x04)
#define REG_TTL		((u8)0x10)
#define REG_STATUS	((u8)0x11)

/*
 * Vendor specific requests to read/write data
 */
#define	REQUEST_REGISTER	((u8)0x10)
#define	REQUEST_EEPROM		((u8)0x11)

static int
nc_vendor_read(struct usbnet *dev, u8 req, u8 regnum, u16 *retval_ptr)
{
	int status = usb_control_msg(dev->udev,
		usb_rcvctrlpipe(dev->udev, 0),
		req,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		0, regnum,
		retval_ptr, sizeof *retval_ptr,
		USB_CTRL_GET_TIMEOUT);
	if (status > 0)
		status = 0;
	if (!status)
		le16_to_cpus(retval_ptr);
	return status;
}

static inline int
nc_register_read(struct usbnet *dev, u8 regnum, u16 *retval_ptr)
{
	return nc_vendor_read(dev, REQUEST_REGISTER, regnum, retval_ptr);
}

// no retval ... can become async, usable in_interrupt()
static void
nc_vendor_write(struct usbnet *dev, u8 req, u8 regnum, u16 value)
{
	usb_control_msg(dev->udev,
		usb_sndctrlpipe(dev->udev, 0),
		req,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		value, regnum,
		NULL, 0,			// data is in setup packet
		USB_CTRL_SET_TIMEOUT);
}

static inline void
nc_register_write(struct usbnet *dev, u8 regnum, u16 value)
{
	nc_vendor_write(dev, REQUEST_REGISTER, regnum, value);
}


#if 0
static void nc_dump_registers(struct usbnet *dev)
{
	u8	reg;
	u16	*vp = kmalloc(sizeof (u16));

	if (!vp) {
		dbg("no memory?");
		return;
	}

	dbg("%s registers:", dev->net->name);
	for (reg = 0; reg < 0x20; reg++) {
		int retval;

		// reading some registers is trouble
		if (reg >= 0x08 && reg <= 0xf)
			continue;
		if (reg >= 0x12 && reg <= 0x1e)
			continue;

		retval = nc_register_read(dev, reg, vp);
		if (retval < 0)
			dbg("%s reg [0x%x] ==> error %d",
				dev->net->name, reg, retval);
		else
			dbg("%s reg [0x%x] = 0x%x",
				dev->net->name, reg, *vp);
	}
	kfree(vp);
}
#endif


/*-------------------------------------------------------------------------*/

/*
 * Control register
 */

#define	USBCTL_WRITABLE_MASK	0x1f0f
// bits 15-13 reserved, r/o
#define	USBCTL_ENABLE_LANG	(1 << 12)
#define	USBCTL_ENABLE_MFGR	(1 << 11)
#define	USBCTL_ENABLE_PROD	(1 << 10)
#define	USBCTL_ENABLE_SERIAL	(1 << 9)
#define	USBCTL_ENABLE_DEFAULTS	(1 << 8)
// bits 7-4 reserved, r/o
#define	USBCTL_FLUSH_OTHER	(1 << 3)
#define	USBCTL_FLUSH_THIS	(1 << 2)
#define	USBCTL_DISCONN_OTHER	(1 << 1)
#define	USBCTL_DISCONN_THIS	(1 << 0)

static inline void nc_dump_usbctl(struct usbnet *dev, u16 usbctl)
{
	if (!netif_msg_link(dev))
		return;
	devdbg(dev, "net1080 %s-%s usbctl 0x%x:%s%s%s%s%s;"
			" this%s%s;"
			" other%s%s; r/o 0x%x",
		dev->udev->bus->bus_name, dev->udev->devpath,
		usbctl,
		(usbctl & USBCTL_ENABLE_LANG) ? " lang" : "",
		(usbctl & USBCTL_ENABLE_MFGR) ? " mfgr" : "",
		(usbctl & USBCTL_ENABLE_PROD) ? " prod" : "",
		(usbctl & USBCTL_ENABLE_SERIAL) ? " serial" : "",
		(usbctl & USBCTL_ENABLE_DEFAULTS) ? " defaults" : "",

		(usbctl & USBCTL_FLUSH_OTHER) ? " FLUSH" : "",
		(usbctl & USBCTL_DISCONN_OTHER) ? " DIS" : "",
		(usbctl & USBCTL_FLUSH_THIS) ? " FLUSH" : "",
		(usbctl & USBCTL_DISCONN_THIS) ? " DIS" : "",
		usbctl & ~USBCTL_WRITABLE_MASK
		);
}

/*-------------------------------------------------------------------------*/

/*
 * Status register
 */

#define	STATUS_PORT_A		(1 << 15)

#define	STATUS_CONN_OTHER	(1 << 14)
#define	STATUS_SUSPEND_OTHER	(1 << 13)
#define	STATUS_MAILBOX_OTHER	(1 << 12)
#define	STATUS_PACKETS_OTHER(n)	(((n) >> 8) & 0x03)

#define	STATUS_CONN_THIS	(1 << 6)
#define	STATUS_SUSPEND_THIS	(1 << 5)
#define	STATUS_MAILBOX_THIS	(1 << 4)
#define	STATUS_PACKETS_THIS(n)	(((n) >> 0) & 0x03)

#define	STATUS_UNSPEC_MASK	0x0c8c
#define	STATUS_NOISE_MASK 	((u16)~(0x0303|STATUS_UNSPEC_MASK))


static inline void nc_dump_status(struct usbnet *dev, u16 status)
{
	if (!netif_msg_link(dev))
		return;
	devdbg(dev, "net1080 %s-%s status 0x%x:"
			" this (%c) PKT=%d%s%s%s;"
			" other PKT=%d%s%s%s; unspec 0x%x",
		dev->udev->bus->bus_name, dev->udev->devpath,
		status,

		// XXX the packet counts don't seem right
		// (1 at reset, not 0); maybe UNSPEC too

		(status & STATUS_PORT_A) ? 'A' : 'B',
		STATUS_PACKETS_THIS(status),
		(status & STATUS_CONN_THIS) ? " CON" : "",
		(status & STATUS_SUSPEND_THIS) ? " SUS" : "",
		(status & STATUS_MAILBOX_THIS) ? " MBOX" : "",

		STATUS_PACKETS_OTHER(status),
		(status & STATUS_CONN_OTHER) ? " CON" : "",
		(status & STATUS_SUSPEND_OTHER) ? " SUS" : "",
		(status & STATUS_MAILBOX_OTHER) ? " MBOX" : "",

		status & STATUS_UNSPEC_MASK
		);
}

/*-------------------------------------------------------------------------*/

/*
 * TTL register
 */

#define	TTL_THIS(ttl)	(0x00ff & ttl)
#define	TTL_OTHER(ttl)	(0x00ff & (ttl >> 8))
#define MK_TTL(this,other)	((u16)(((other)<<8)|(0x00ff&(this))))

static inline void nc_dump_ttl(struct usbnet *dev, u16 ttl)
{
	if (netif_msg_link(dev))
		devdbg(dev, "net1080 %s-%s ttl 0x%x this = %d, other = %d",
			dev->udev->bus->bus_name, dev->udev->devpath,
			ttl, TTL_THIS(ttl), TTL_OTHER(ttl));
}

/*-------------------------------------------------------------------------*/

static int net1080_reset(struct usbnet *dev)
{
	u16		usbctl, status, ttl;
	u16		*vp = kmalloc(sizeof (u16), GFP_KERNEL);
	int		retval;

	if (!vp)
		return -ENOMEM;

	// nc_dump_registers(dev);

	if ((retval = nc_register_read(dev, REG_STATUS, vp)) < 0) {
		dbg("can't read %s-%s status: %d",
			dev->udev->bus->bus_name, dev->udev->devpath, retval);
		goto done;
	}
	status = *vp;
	nc_dump_status(dev, status);

	if ((retval = nc_register_read(dev, REG_USBCTL, vp)) < 0) {
		dbg("can't read USBCTL, %d", retval);
		goto done;
	}
	usbctl = *vp;
	nc_dump_usbctl(dev, usbctl);

	nc_register_write(dev, REG_USBCTL,
			USBCTL_FLUSH_THIS | USBCTL_FLUSH_OTHER);

	if ((retval = nc_register_read(dev, REG_TTL, vp)) < 0) {
		dbg("can't read TTL, %d", retval);
		goto done;
	}
	ttl = *vp;
	// nc_dump_ttl(dev, ttl);

	nc_register_write(dev, REG_TTL,
			MK_TTL(NC_READ_TTL_MS, TTL_OTHER(ttl)) );
	dbg("%s: assigned TTL, %d ms", dev->net->name, NC_READ_TTL_MS);

	if (netif_msg_link(dev))
		devinfo(dev, "port %c, peer %sconnected",
			(status & STATUS_PORT_A) ? 'A' : 'B',
			(status & STATUS_CONN_OTHER) ? "" : "dis"
			);
	retval = 0;

done:
	kfree(vp);
	return retval;
}

static int net1080_check_connect(struct usbnet *dev)
{
	int			retval;
	u16			status;
	u16			*vp = kmalloc(sizeof (u16), GFP_KERNEL);

	if (!vp)
		return -ENOMEM;
	retval = nc_register_read(dev, REG_STATUS, vp);
	status = *vp;
	kfree(vp);
	if (retval != 0) {
		dbg("%s net1080_check_conn read - %d", dev->net->name, retval);
		return retval;
	}
	if ((status & STATUS_CONN_OTHER) != STATUS_CONN_OTHER)
		return -ENOLINK;
	return 0;
}

static void nc_flush_complete(struct urb *urb)
{
	kfree(urb->context);
	usb_free_urb(urb);
}

static void nc_ensure_sync(struct usbnet *dev)
{
	dev->frame_errors++;
	if (dev->frame_errors > 5) {
		struct urb		*urb;
		struct usb_ctrlrequest	*req;
		int			status;

		/* Send a flush */
		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (!urb)
			return;

		req = kmalloc(sizeof *req, GFP_ATOMIC);
		if (!req) {
			usb_free_urb(urb);
			return;
		}

		req->bRequestType = USB_DIR_OUT
			| USB_TYPE_VENDOR
			| USB_RECIP_DEVICE;
		req->bRequest = REQUEST_REGISTER;
		req->wValue = cpu_to_le16(USBCTL_FLUSH_THIS
				| USBCTL_FLUSH_OTHER);
		req->wIndex = cpu_to_le16(REG_USBCTL);
		req->wLength = cpu_to_le16(0);

		/* queue an async control request, we don't need
		 * to do anything when it finishes except clean up.
		 */
		usb_fill_control_urb(urb, dev->udev,
			usb_sndctrlpipe(dev->udev, 0),
			(unsigned char *) req,
			NULL, 0,
			nc_flush_complete, req);
		status = usb_submit_urb(urb, GFP_ATOMIC);
		if (status) {
			kfree(req);
			usb_free_urb(urb);
			return;
		}

		if (netif_msg_rx_err(dev))
			devdbg(dev, "flush net1080; too many framing errors");
		dev->frame_errors = 0;
	}
}

static int net1080_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	struct nc_header	*header;
	struct nc_trailer	*trailer;
	u16			hdr_len, packet_len;

	if (!(skb->len & 0x01)) {
#ifdef DEBUG
		struct net_device	*net = dev->net;
		dbg("rx framesize %d range %d..%d mtu %d", skb->len,
			net->hard_header_len, dev->hard_mtu, net->mtu);
#endif
		dev->stats.rx_frame_errors++;
		nc_ensure_sync(dev);
		return 0;
	}

	header = (struct nc_header *) skb->data;
	hdr_len = le16_to_cpup(&header->hdr_len);
	packet_len = le16_to_cpup(&header->packet_len);
	if (FRAMED_SIZE(packet_len) > NC_MAX_PACKET) {
		dev->stats.rx_frame_errors++;
		dbg("packet too big, %d", packet_len);
		nc_ensure_sync(dev);
		return 0;
	} else if (hdr_len < MIN_HEADER) {
		dev->stats.rx_frame_errors++;
		dbg("header too short, %d", hdr_len);
		nc_ensure_sync(dev);
		return 0;
	} else if (hdr_len > MIN_HEADER) {
		// out of band data for us?
		dbg("header OOB, %d bytes", hdr_len - MIN_HEADER);
		nc_ensure_sync(dev);
		// switch (vendor/product ids) { ... }
	}
	skb_pull(skb, hdr_len);

	trailer = (struct nc_trailer *)
		(skb->data + skb->len - sizeof *trailer);
	skb_trim(skb, skb->len - sizeof *trailer);

	if ((packet_len & 0x01) == 0) {
		if (skb->data [packet_len] != PAD_BYTE) {
			dev->stats.rx_frame_errors++;
			dbg("bad pad");
			return 0;
		}
		skb_trim(skb, skb->len - 1);
	}
	if (skb->len != packet_len) {
		dev->stats.rx_frame_errors++;
		dbg("bad packet len %d (expected %d)",
			skb->len, packet_len);
		nc_ensure_sync(dev);
		return 0;
	}
	if (header->packet_id != get_unaligned(&trailer->packet_id)) {
		dev->stats.rx_fifo_errors++;
		dbg("(2+ dropped) rx packet_id mismatch 0x%x 0x%x",
			le16_to_cpu(header->packet_id),
			le16_to_cpu(trailer->packet_id));
		return 0;
	}
#if 0
	devdbg(dev, "frame <rx h %d p %d id %d", header->hdr_len,
		header->packet_len, header->packet_id);
#endif
	dev->frame_errors = 0;
	return 1;
}

static struct sk_buff *
net1080_tx_fixup(struct usbnet *dev, struct sk_buff *skb, gfp_t flags)
{
	struct sk_buff		*skb2;
	struct nc_header	*header = NULL;
	struct nc_trailer	*trailer = NULL;
	int			padlen = sizeof (struct nc_trailer);
	int			len = skb->len;

	if (!((len + padlen + sizeof (struct nc_header)) & 0x01))
		padlen++;
	if (!skb_cloned(skb)) {
		int	headroom = skb_headroom(skb);
		int	tailroom = skb_tailroom(skb);

		if (padlen <= tailroom &&
		    sizeof(struct nc_header) <= headroom)
			/* There's enough head and tail room */
			goto encapsulate;

		if ((sizeof (struct nc_header) + padlen) <
				(headroom + tailroom)) {
			/* There's enough total room, so just readjust */
			skb->data = memmove(skb->head
						+ sizeof (struct nc_header),
					    skb->data, skb->len);
			skb_set_tail_pointer(skb, len);
			goto encapsulate;
		}
	}

	/* Create a new skb to use with the correct size */
	skb2 = skb_copy_expand(skb,
				sizeof (struct nc_header),
				padlen,
				flags);
	dev_kfree_skb_any(skb);
	if (!skb2)
		return skb2;
	skb = skb2;

encapsulate:
	/* header first */
	header = (struct nc_header *) skb_push(skb, sizeof *header);
	header->hdr_len = cpu_to_le16(sizeof (*header));
	header->packet_len = cpu_to_le16(len);
	header->packet_id = cpu_to_le16((u16)dev->xid++);

	/* maybe pad; then trailer */
	if (!((skb->len + sizeof *trailer) & 0x01))
		*skb_put(skb, 1) = PAD_BYTE;
	trailer = (struct nc_trailer *) skb_put(skb, sizeof *trailer);
	put_unaligned(header->packet_id, &trailer->packet_id);
#if 0
	devdbg(dev, "frame >tx h %d p %d id %d",
		header->hdr_len, header->packet_len,
		header->packet_id);
#endif
	return skb;
}

static int net1080_bind(struct usbnet *dev, struct usb_interface *intf)
{
	unsigned	extra = sizeof (struct nc_header)
				+ 1
				+ sizeof (struct nc_trailer);

	dev->net->hard_header_len += extra;
	dev->rx_urb_size = dev->net->hard_header_len + dev->net->mtu;
	dev->hard_mtu = NC_MAX_PACKET;
	return usbnet_get_endpoints (dev, intf);
}

static const struct driver_info	net1080_info = {
	.description =	"NetChip TurboCONNECT",
	.flags =	FLAG_FRAMING_NC,
	.bind =		net1080_bind,
	.reset =	net1080_reset,
	.check_connect = net1080_check_connect,
	.rx_fixup =	net1080_rx_fixup,
	.tx_fixup =	net1080_tx_fixup,
};

static const struct usb_device_id	products [] = {
{
	USB_DEVICE(0x0525, 0x1080),	// NetChip ref design
	.driver_info =	(unsigned long) &net1080_info,
}, {
	USB_DEVICE(0x06D0, 0x0622),	// Laplink Gold
	.driver_info =	(unsigned long) &net1080_info,
},
	{ },		// END
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver net1080_driver = {
	.name =		"net1080",
	.id_table =	products,
	.probe =	usbnet_probe,
	.disconnect =	usbnet_disconnect,
	.suspend =	usbnet_suspend,
	.resume =	usbnet_resume,
};

static int __init net1080_init(void)
{
 	return usb_register(&net1080_driver);
}
module_init(net1080_init);

static void __exit net1080_exit(void)
{
 	usb_deregister(&net1080_driver);
}
module_exit(net1080_exit);

MODULE_AUTHOR("David Brownell");
MODULE_DESCRIPTION("NetChip 1080 based USB Host-to-Host Links");
MODULE_LICENSE("GPL");
