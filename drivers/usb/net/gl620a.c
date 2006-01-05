/*
 * GeneSys GL620USB-A based links
 * Copyright (C) 2001 by Jiun-Jie Huang <huangjj@genesyslogic.com.tw>
 * Copyright (C) 2001 by Stanislav Brabec <utx@penguin.cz>
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
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mii.h>
#include <linux/usb.h>

#include "usbnet.h"


/*
 * GeneSys GL620USB-A (www.genesyslogic.com.tw)
 *
 * ... should partially interop with the Win32 driver for this hardware.
 * The GeneSys docs imply there's some NDIS issue motivating this framing.
 *
 * Some info from GeneSys:
 *  - GL620USB-A is full duplex; GL620USB is only half duplex for bulk.
 *    (Some cables, like the BAFO-100c, use the half duplex version.)
 *  - For the full duplex model, the low bit of the version code says
 *    which side is which ("left/right").
 *  - For the half duplex type, a control/interrupt handshake settles
 *    the transfer direction.  (That's disabled here, partially coded.)
 *    A control URB would block until other side writes an interrupt.
 *
 * Original code from Jiun-Jie Huang <huangjj@genesyslogic.com.tw>
 * and merged into "usbnet" by Stanislav Brabec <utx@penguin.cz>.
 */

// control msg write command
#define GENELINK_CONNECT_WRITE			0xF0
// interrupt pipe index
#define GENELINK_INTERRUPT_PIPE			0x03
// interrupt read buffer size
#define INTERRUPT_BUFSIZE			0x08
// interrupt pipe interval value
#define GENELINK_INTERRUPT_INTERVAL		0x10
// max transmit packet number per transmit
#define GL_MAX_TRANSMIT_PACKETS			32
// max packet length
#define GL_MAX_PACKET_LEN			1514
// max receive buffer size
#define GL_RCV_BUF_SIZE		\
	(((GL_MAX_PACKET_LEN + 4) * GL_MAX_TRANSMIT_PACKETS) + 4)

struct gl_packet {
	u32		packet_length;
	char		packet_data [1];
};

struct gl_header {
	u32			packet_count;
	struct gl_packet	packets;
};

#ifdef	GENELINK_ACK

// FIXME:  this code is incomplete, not debugged; it doesn't
// handle interrupts correctly; it should use the generic
// status IRQ code (which didn't exist back in 2001).

struct gl_priv {
	struct urb	*irq_urb;
	char		irq_buf [INTERRUPT_BUFSIZE];
};

static inline int gl_control_write(struct usbnet *dev, u8 request, u16 value)
{
	int retval;

	retval = usb_control_msg(dev->udev,
		      usb_sndctrlpipe(dev->udev, 0),
		      request,
		      USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		      value,
		      0,			// index
		      0,			// data buffer
		      0,			// size
		      USB_CTRL_SET_TIMEOUT);
	return retval;
}

static void gl_interrupt_complete(struct urb *urb, struct pt_regs *regs)
{
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
				__FUNCTION__, status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d",
				__FUNCTION__, urb->status);
	}

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status)
		err("%s - usb_submit_urb failed with result %d",
		     __FUNCTION__, status);
}

static int gl_interrupt_read(struct usbnet *dev)
{
	struct gl_priv	*priv = dev->priv_data;
	int		retval;

	// issue usb interrupt read
	if (priv && priv->irq_urb) {
		// submit urb
		if ((retval = usb_submit_urb(priv->irq_urb, GFP_KERNEL)) != 0)
			dbg("gl_interrupt_read: submit fail - %X...", retval);
		else
			dbg("gl_interrupt_read: submit success...");
	}

	return 0;
}

// check whether another side is connected
static int genelink_check_connect(struct usbnet *dev)
{
	int			retval;

	dbg("genelink_check_connect...");

	// detect whether another side is connected
	if ((retval = gl_control_write(dev, GENELINK_CONNECT_WRITE, 0)) != 0) {
		dbg("%s: genelink_check_connect write fail - %X",
			dev->net->name, retval);
		return retval;
	}

	// usb interrupt read to ack another side
	if ((retval = gl_interrupt_read(dev)) != 0) {
		dbg("%s: genelink_check_connect read fail - %X",
			dev->net->name, retval);
		return retval;
	}

	dbg("%s: genelink_check_connect read success", dev->net->name);
	return 0;
}

// allocate and initialize the private data for genelink
static int genelink_init(struct usbnet *dev)
{
	struct gl_priv *priv;

	// allocate the private data structure
	if ((priv = kmalloc(sizeof *priv, GFP_KERNEL)) == 0) {
		dbg("%s: cannot allocate private data per device",
			dev->net->name);
		return -ENOMEM;
	}

	// allocate irq urb
	if ((priv->irq_urb = usb_alloc_urb(0, GFP_KERNEL)) == 0) {
		dbg("%s: cannot allocate private irq urb per device",
			dev->net->name);
		kfree(priv);
		return -ENOMEM;
	}

	// fill irq urb
	usb_fill_int_urb(priv->irq_urb, dev->udev,
		usb_rcvintpipe(dev->udev, GENELINK_INTERRUPT_PIPE),
		priv->irq_buf, INTERRUPT_BUFSIZE,
		gl_interrupt_complete, 0,
		GENELINK_INTERRUPT_INTERVAL);

	// set private data pointer
	dev->priv_data = priv;

	return 0;
}

// release the private data
static int genelink_free(struct usbnet *dev)
{
	struct gl_priv	*priv = dev->priv_data;

	if (!priv)
		return 0;

// FIXME:  can't cancel here; it's synchronous, and
// should have happened earlier in any case (interrupt
// handling needs to be generic)

	// cancel irq urb first
	usb_kill_urb(priv->irq_urb);

	// free irq urb
	usb_free_urb(priv->irq_urb);

	// free the private data structure
	kfree(priv);

	return 0;
}

#endif

static int genelink_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	struct gl_header	*header;
	struct gl_packet	*packet;
	struct sk_buff		*gl_skb;
	u32			size;

	header = (struct gl_header *) skb->data;

	// get the packet count of the received skb
	le32_to_cpus(&header->packet_count);
	if ((header->packet_count > GL_MAX_TRANSMIT_PACKETS)
			|| (header->packet_count < 0)) {
		dbg("genelink: invalid received packet count %d",
			header->packet_count);
		return 0;
	}

	// set the current packet pointer to the first packet
	packet = &header->packets;

	// decrement the length for the packet count size 4 bytes
	skb_pull(skb, 4);

	while (header->packet_count > 1) {
		// get the packet length
		size = le32_to_cpu(packet->packet_length);

		// this may be a broken packet
		if (size > GL_MAX_PACKET_LEN) {
			dbg("genelink: invalid rx length %d", size);
			return 0;
		}

		// allocate the skb for the individual packet
		gl_skb = alloc_skb(size, GFP_ATOMIC);
		if (gl_skb) {

			// copy the packet data to the new skb
			memcpy(skb_put(gl_skb, size),
					packet->packet_data, size);
			usbnet_skb_return(dev, gl_skb);
		}

		// advance to the next packet
		packet = (struct gl_packet *)
			&packet->packet_data [size];
		header->packet_count--;

		// shift the data pointer to the next gl_packet
		skb_pull(skb, size + 4);
	}

	// skip the packet length field 4 bytes
	skb_pull(skb, 4);

	if (skb->len > GL_MAX_PACKET_LEN) {
		dbg("genelink: invalid rx length %d", skb->len);
		return 0;
	}
	return 1;
}

static struct sk_buff *
genelink_tx_fixup(struct usbnet *dev, struct sk_buff *skb, gfp_t flags)
{
	int 	padlen;
	int	length = skb->len;
	int	headroom = skb_headroom(skb);
	int	tailroom = skb_tailroom(skb);
	u32	*packet_count;
	u32	*packet_len;

	// FIXME:  magic numbers, bleech
	padlen = ((skb->len + (4 + 4*1)) % 64) ? 0 : 1;

	if ((!skb_cloned(skb))
			&& ((headroom + tailroom) >= (padlen + (4 + 4*1)))) {
		if ((headroom < (4 + 4*1)) || (tailroom < padlen)) {
			skb->data = memmove(skb->head + (4 + 4*1),
					     skb->data, skb->len);
			skb->tail = skb->data + skb->len;
		}
	} else {
		struct sk_buff	*skb2;
		skb2 = skb_copy_expand(skb, (4 + 4*1) , padlen, flags);
		dev_kfree_skb_any(skb);
		skb = skb2;
		if (!skb)
			return NULL;
	}

	// attach the packet count to the header
	packet_count = (u32 *) skb_push(skb, (4 + 4*1));
	packet_len = packet_count + 1;

	*packet_count = cpu_to_le32(1);
	*packet_len = cpu_to_le32(length);

	// add padding byte
	if ((skb->len % dev->maxpacket) == 0)
		skb_put(skb, 1);

	return skb;
}

static int genelink_bind(struct usbnet *dev, struct usb_interface *intf)
{
	dev->hard_mtu = GL_RCV_BUF_SIZE;
	dev->net->hard_header_len += 4;
	dev->in = usb_rcvbulkpipe(dev->udev, dev->driver_info->in);
	dev->out = usb_sndbulkpipe(dev->udev, dev->driver_info->out);
	return 0;
}

static const struct driver_info	genelink_info = {
	.description =	"Genesys GeneLink",
	.flags =	FLAG_FRAMING_GL | FLAG_NO_SETINT,
	.bind =		genelink_bind,
	.rx_fixup =	genelink_rx_fixup,
	.tx_fixup =	genelink_tx_fixup,

	.in = 1, .out = 2,

#ifdef	GENELINK_ACK
	.check_connect =genelink_check_connect,
#endif
};

static const struct usb_device_id	products [] = {

{
	USB_DEVICE(0x05e3, 0x0502),	// GL620USB-A
	.driver_info =	(unsigned long) &genelink_info,
},
	/* NOT: USB_DEVICE(0x05e3, 0x0501),	// GL620USB
	 * that's half duplex, not currently supported
	 */
	{ },		// END
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver gl620a_driver = {
	.name =		"gl620a",
	.id_table =	products,
	.probe =	usbnet_probe,
	.disconnect =	usbnet_disconnect,
	.suspend =	usbnet_suspend,
	.resume =	usbnet_resume,
};

static int __init usbnet_init(void)
{
 	return usb_register(&gl620a_driver);
}
module_init(usbnet_init);

static void __exit usbnet_exit(void)
{
 	usb_deregister(&gl620a_driver);
}
module_exit(usbnet_exit);

MODULE_AUTHOR("Jiun-Jie Huang");
MODULE_DESCRIPTION("GL620-USB-A Host-to-Host Link cables");
MODULE_LICENSE("GPL");

