/*
 * USB CDC EEM network interface driver
 * Copyright (C) 2009 Oberthur Technologies
 * by Omar Laazimani, Olivier Condemine
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ctype.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/crc32.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>
#include <linux/gfp.h>


/*
 * This driver is an implementation of the CDC "Ethernet Emulation
 * Model" (EEM) specification, which encapsulates Ethernet frames
 * for transport over USB using a simpler USB device model than the
 * previous CDC "Ethernet Control Model" (ECM, or "CDC Ethernet").
 *
 * For details, see www.usb.org/developers/devclass_docs/CDC_EEM10.pdf
 *
 * This version has been tested with GIGAntIC WuaoW SIM Smart Card on 2.6.24,
 * 2.6.27 and 2.6.30rc2 kernel.
 * It has also been validated on Openmoko Om 2008.12 (based on 2.6.24 kernel).
 * build on 23-April-2009
 */

#define EEM_HEAD	2		/* 2 byte header */

/*-------------------------------------------------------------------------*/

static void eem_linkcmd_complete(struct urb *urb)
{
	dev_kfree_skb(urb->context);
	usb_free_urb(urb);
}

static void eem_linkcmd(struct usbnet *dev, struct sk_buff *skb)
{
	struct urb		*urb;
	int			status;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		goto fail;

	usb_fill_bulk_urb(urb, dev->udev, dev->out,
			skb->data, skb->len, eem_linkcmd_complete, skb);

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		usb_free_urb(urb);
fail:
		dev_kfree_skb(skb);
		netdev_warn(dev->net, "link cmd failure\n");
		return;
	}
}

static int eem_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int status = 0;

	status = usbnet_get_endpoints(dev, intf);
	if (status < 0) {
		usb_set_intfdata(intf, NULL);
		usb_driver_release_interface(driver_of(intf), intf);
		return status;
	}

	/* no jumbogram (16K) support for now */

	dev->net->hard_header_len += EEM_HEAD + ETH_FCS_LEN;

	return 0;
}

/*
 * EEM permits packing multiple Ethernet frames into USB transfers
 * (a "bundle"), but for TX we don't try to do that.
 */
static struct sk_buff *eem_tx_fixup(struct usbnet *dev, struct sk_buff *skb,
				       gfp_t flags)
{
	struct sk_buff	*skb2 = NULL;
	u16		len = skb->len;
	u32		crc = 0;
	int		padlen = 0;

	/* When ((len + EEM_HEAD + ETH_FCS_LEN) % dev->maxpacket) is
	 * zero, stick two bytes of zero length EEM packet on the end.
	 * Else the framework would add invalid single byte padding,
	 * since it can't know whether ZLPs will be handled right by
	 * all the relevant hardware and software.
	 */
	if (!((len + EEM_HEAD + ETH_FCS_LEN) % dev->maxpacket))
		padlen += 2;

	if (!skb_cloned(skb)) {
		int	headroom = skb_headroom(skb);
		int	tailroom = skb_tailroom(skb);

		if ((tailroom >= ETH_FCS_LEN + padlen) &&
		    (headroom >= EEM_HEAD))
			goto done;

		if ((headroom + tailroom)
				> (EEM_HEAD + ETH_FCS_LEN + padlen)) {
			skb->data = memmove(skb->head +
					EEM_HEAD,
					skb->data,
					skb->len);
			skb_set_tail_pointer(skb, len);
			goto done;
		}
	}

	skb2 = skb_copy_expand(skb, EEM_HEAD, ETH_FCS_LEN + padlen, flags);
	if (!skb2)
		return NULL;

	dev_kfree_skb_any(skb);
	skb = skb2;

done:
	/* we don't use the "no Ethernet CRC" option */
	crc = crc32_le(~0, skb->data, skb->len);
	crc = ~crc;

	put_unaligned_le32(crc, skb_put(skb, 4));

	/* EEM packet header format:
	 * b0..13:	length of ethernet frame
	 * b14:		bmCRC (1 == valid Ethernet CRC)
	 * b15:		bmType (0 == data)
	 */
	len = skb->len;
	put_unaligned_le16(BIT(14) | len, skb_push(skb, 2));

	/* Bundle a zero length EEM packet if needed */
	if (padlen)
		put_unaligned_le16(0, skb_put(skb, 2));

	return skb;
}

static int eem_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	/*
	 * Our task here is to strip off framing, leaving skb with one
	 * data frame for the usbnet framework code to process.  But we
	 * may have received multiple EEM payloads, or command payloads.
	 * So we must process _everything_ as if it's a header, except
	 * maybe the last data payload
	 *
	 * REVISIT the framework needs updating so that when we consume
	 * all payloads (the last or only message was a command, or a
	 * zero length EEM packet) that is not accounted as an rx_error.
	 */
	do {
		struct sk_buff	*skb2 = NULL;
		u16		header;
		u16		len = 0;

		/* incomplete EEM header? */
		if (skb->len < EEM_HEAD)
			return 0;

		/*
		 * EEM packet header format:
		 * b0..14:	EEM type dependent (Data or Command)
		 * b15:		bmType
		 */
		header = get_unaligned_le16(skb->data);
		skb_pull(skb, EEM_HEAD);

		/*
		 * The bmType bit helps to denote when EEM
		 * packet is data or command :
		 *	bmType = 0	: EEM data payload
		 *	bmType = 1	: EEM (link) command
		 */
		if (header & BIT(15)) {
			u16	bmEEMCmd;

			/*
			 * EEM (link) command packet:
			 * b0..10:	bmEEMCmdParam
			 * b11..13:	bmEEMCmd
			 * b14:		bmReserved (must be 0)
			 * b15:		1 (EEM command)
			 */
			if (header & BIT(14)) {
				netdev_dbg(dev->net, "reserved command %04x\n",
					   header);
				continue;
			}

			bmEEMCmd = (header >> 11) & 0x7;
			switch (bmEEMCmd) {

			/* Responding to echo requests is mandatory. */
			case 0:		/* Echo command */
				len = header & 0x7FF;

				/* bogus command? */
				if (skb->len < len)
					return 0;

				skb2 = skb_clone(skb, GFP_ATOMIC);
				if (unlikely(!skb2))
					goto next;
				skb_trim(skb2, len);
				put_unaligned_le16(BIT(15) | (1 << 11) | len,
						skb_push(skb2, 2));
				eem_linkcmd(dev, skb2);
				break;

			/*
			 * Host may choose to ignore hints.
			 *  - suspend: peripheral ready to suspend
			 *  - response: suggest N millisec polling
			 *  - response complete: suggest N sec polling
			 */
			case 2:		/* Suspend hint */
			case 3:		/* Response hint */
			case 4:		/* Response complete hint */
				continue;

			/*
			 * Hosts should never receive host-to-peripheral
			 * or reserved command codes; or responses to an
			 * echo command we didn't send.
			 */
			case 1:		/* Echo response */
			case 5:		/* Tickle */
			default:	/* reserved */
				netdev_warn(dev->net,
					    "unexpected link command %d\n",
					    bmEEMCmd);
				continue;
			}

		} else {
			u32	crc, crc2;
			int	is_last;

			/* zero length EEM packet? */
			if (header == 0)
				continue;

			/*
			 * EEM data packet header :
			 * b0..13:	length of ethernet frame
			 * b14:		bmCRC
			 * b15:		0 (EEM data)
			 */
			len = header & 0x3FFF;

			/* bogus EEM payload? */
			if (skb->len < len)
				return 0;

			/* bogus ethernet frame? */
			if (len < (ETH_HLEN + ETH_FCS_LEN))
				goto next;

			/*
			 * Treat the last payload differently: framework
			 * code expects our "fixup" to have stripped off
			 * headers, so "skb" is a data packet (or error).
			 * Else if it's not the last payload, keep "skb"
			 * for further processing.
			 */
			is_last = (len == skb->len);
			if (is_last)
				skb2 = skb;
			else {
				skb2 = skb_clone(skb, GFP_ATOMIC);
				if (unlikely(!skb2))
					return 0;
			}

			/*
			 * The bmCRC helps to denote when the CRC field in
			 * the Ethernet frame contains a calculated CRC:
			 *	bmCRC = 1	: CRC is calculated
			 *	bmCRC = 0	: CRC = 0xDEADBEEF
			 */
			if (header & BIT(14)) {
				crc = get_unaligned_le32(skb2->data
						+ len - ETH_FCS_LEN);
				crc2 = ~crc32_le(~0, skb2->data, skb2->len
						- ETH_FCS_LEN);
			} else {
				crc = get_unaligned_be32(skb2->data
						+ len - ETH_FCS_LEN);
				crc2 = 0xdeadbeef;
			}
			skb_trim(skb2, len - ETH_FCS_LEN);

			if (is_last)
				return crc == crc2;

			if (unlikely(crc != crc2)) {
				dev->net->stats.rx_errors++;
				dev_kfree_skb_any(skb2);
			} else
				usbnet_skb_return(dev, skb2);
		}

next:
		skb_pull(skb, len);
	} while (skb->len);

	return 1;
}

static const struct driver_info eem_info = {
	.description =	"CDC EEM Device",
	.flags =	FLAG_ETHER | FLAG_POINTTOPOINT,
	.bind =		eem_bind,
	.rx_fixup =	eem_rx_fixup,
	.tx_fixup =	eem_tx_fixup,
};

/*-------------------------------------------------------------------------*/

static const struct usb_device_id products[] = {
{
	USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_EEM,
			USB_CDC_PROTO_EEM),
	.driver_info = (unsigned long) &eem_info,
},
{
	/* EMPTY == end of list */
},
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver eem_driver = {
	.name =		"cdc_eem",
	.id_table =	products,
	.probe =	usbnet_probe,
	.disconnect =	usbnet_disconnect,
	.suspend =	usbnet_suspend,
	.resume =	usbnet_resume,
};


static int __init eem_init(void)
{
	return usb_register(&eem_driver);
}
module_init(eem_init);

static void __exit eem_exit(void)
{
	usb_deregister(&eem_driver);
}
module_exit(eem_exit);

MODULE_AUTHOR("Omar Laazimani <omar.oberthur@gmail.com>");
MODULE_DESCRIPTION("USB CDC EEM");
MODULE_LICENSE("GPL");
