/*
 * Ethernet interface part of the LG VL600 LTE modem (4G dongle)
 *
 * Copyright (C) 2011 Intel Corporation
 * Author: Andrzej Zaborowski <balrogg@gmail.com>
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
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/inetdevice.h>
#include <linux/module.h>

/*
 * The device has a CDC ACM port for modem control (it claims to be
 * CDC ACM anyway) and a CDC Ethernet port for actual network data.
 * It will however ignore data on both ports that is not encapsulated
 * in a specific way, any data returned is also encapsulated the same
 * way.  The headers don't seem to follow any popular standard.
 *
 * This driver adds and strips these headers from the ethernet frames
 * sent/received from the CDC Ethernet port.  The proprietary header
 * replaces the standard ethernet header in a packet so only actual
 * ethernet frames are allowed.  The headers allow some form of
 * multiplexing by using non standard values of the .h_proto field.
 * Windows/Mac drivers do send a couple of such frames to the device
 * during initialisation, with protocol set to 0x0906 or 0x0b06 and (what
 * seems to be) a flag in the .dummy_flags.  This doesn't seem necessary
 * for modem operation but can possibly be used for GPS or other funcitons.
 */

struct vl600_frame_hdr {
	__le32 len;
	__le32 serial;
	__le32 pkt_cnt;
	__le32 dummy_flags;
	__le32 dummy;
	__le32 magic;
} __attribute__((packed));

struct vl600_pkt_hdr {
	__le32 dummy[2];
	__le32 len;
	__be16 h_proto;
} __attribute__((packed));

struct vl600_state {
	struct sk_buff *current_rx_buf;
};

static int vl600_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int ret;
	struct vl600_state *s = kzalloc(sizeof(struct vl600_state), GFP_KERNEL);

	if (!s)
		return -ENOMEM;

	ret = usbnet_cdc_bind(dev, intf);
	if (ret) {
		kfree(s);
		return ret;
	}

	dev->driver_priv = s;

	/* ARP packets don't go through, but they're also of no use.  The
	 * subnet has only two hosts anyway: us and the gateway / DHCP
	 * server (probably simulated by modem firmware or network operator)
	 * whose address changes everytime we connect to the intarwebz and
	 * who doesn't bother answering ARP requests either.  So hardware
	 * addresses have no meaning, the destination and the source of every
	 * packet depend only on whether it is on the IN or OUT endpoint.  */
	dev->net->flags |= IFF_NOARP;
	/* IPv6 NDP relies on multicast.  Enable it by default. */
	dev->net->flags |= IFF_MULTICAST;

	return ret;
}

static void vl600_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	struct vl600_state *s = dev->driver_priv;

	if (s->current_rx_buf)
		dev_kfree_skb(s->current_rx_buf);

	kfree(s);

	return usbnet_cdc_unbind(dev, intf);
}

static int vl600_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	struct vl600_frame_hdr *frame;
	struct vl600_pkt_hdr *packet;
	struct ethhdr *ethhdr;
	int packet_len, count;
	struct sk_buff *buf = skb;
	struct sk_buff *clone;
	struct vl600_state *s = dev->driver_priv;

	/* Frame lengths are generally 4B multiplies but every couple of
	 * hours there's an odd number of bytes sized yet correct frame,
	 * so don't require this.  */

	/* Allow a packet (or multiple packets batched together) to be
	 * split across many frames.  We don't allow a new batch to
	 * begin in the same frame another one is ending however, and no
	 * leading or trailing pad bytes.  */
	if (s->current_rx_buf) {
		frame = (struct vl600_frame_hdr *) s->current_rx_buf->data;
		if (skb->len + s->current_rx_buf->len >
				le32_to_cpup(&frame->len)) {
			netif_err(dev, ifup, dev->net, "Fragment too long\n");
			dev->net->stats.rx_length_errors++;
			goto error;
		}

		buf = s->current_rx_buf;
		memcpy(skb_put(buf, skb->len), skb->data, skb->len);
	} else if (skb->len < 4) {
		netif_err(dev, ifup, dev->net, "Frame too short\n");
		dev->net->stats.rx_length_errors++;
		goto error;
	}

	frame = (struct vl600_frame_hdr *) buf->data;
	/* NOTE: Should check that frame->magic == 0x53544448?
	 * Otherwise if we receive garbage at the beginning of the frame
	 * we may end up allocating a huge buffer and saving all the
	 * future incoming data into it.  */

	if (buf->len < sizeof(*frame) ||
			buf->len != le32_to_cpup(&frame->len)) {
		/* Save this fragment for later assembly */
		if (s->current_rx_buf)
			return 0;

		s->current_rx_buf = skb_copy_expand(skb, 0,
				le32_to_cpup(&frame->len), GFP_ATOMIC);
		if (!s->current_rx_buf) {
			netif_err(dev, ifup, dev->net, "Reserving %i bytes "
					"for packet assembly failed.\n",
					le32_to_cpup(&frame->len));
			dev->net->stats.rx_errors++;
		}

		return 0;
	}

	count = le32_to_cpup(&frame->pkt_cnt);

	skb_pull(buf, sizeof(*frame));

	while (count--) {
		if (buf->len < sizeof(*packet)) {
			netif_err(dev, ifup, dev->net, "Packet too short\n");
			goto error;
		}

		packet = (struct vl600_pkt_hdr *) buf->data;
		packet_len = sizeof(*packet) + le32_to_cpup(&packet->len);
		if (packet_len > buf->len) {
			netif_err(dev, ifup, dev->net,
					"Bad packet length stored in header\n");
			goto error;
		}

		/* Packet header is same size as the ethernet header
		 * (sizeof(*packet) == sizeof(*ethhdr)), additionally
		 * the h_proto field is in the same place so we just leave it
		 * alone and fill in the remaining fields.
		 */
		ethhdr = (struct ethhdr *) skb->data;
		if (be16_to_cpup(&ethhdr->h_proto) == ETH_P_ARP &&
				buf->len > 0x26) {
			/* Copy the addresses from packet contents */
			memcpy(ethhdr->h_source,
					&buf->data[sizeof(*ethhdr) + 0x8],
					ETH_ALEN);
			memcpy(ethhdr->h_dest,
					&buf->data[sizeof(*ethhdr) + 0x12],
					ETH_ALEN);
		} else {
			memset(ethhdr->h_source, 0, ETH_ALEN);
			memcpy(ethhdr->h_dest, dev->net->dev_addr, ETH_ALEN);

			/* Inbound IPv6 packets have an IPv4 ethertype (0x800)
			 * for some reason.  Peek at the L3 header to check
			 * for IPv6 packets, and set the ethertype to IPv6
			 * (0x86dd) so Linux can understand it.
			 */
			if ((buf->data[sizeof(*ethhdr)] & 0xf0) == 0x60)
				ethhdr->h_proto = __constant_htons(ETH_P_IPV6);
		}

		if (count) {
			/* Not the last packet in this batch */
			clone = skb_clone(buf, GFP_ATOMIC);
			if (!clone)
				goto error;

			skb_trim(clone, packet_len);
			usbnet_skb_return(dev, clone);

			skb_pull(buf, (packet_len + 3) & ~3);
		} else {
			skb_trim(buf, packet_len);

			if (s->current_rx_buf) {
				usbnet_skb_return(dev, buf);
				s->current_rx_buf = NULL;
				return 0;
			}

			return 1;
		}
	}

error:
	if (s->current_rx_buf) {
		dev_kfree_skb_any(s->current_rx_buf);
		s->current_rx_buf = NULL;
	}
	dev->net->stats.rx_errors++;
	return 0;
}

static struct sk_buff *vl600_tx_fixup(struct usbnet *dev,
		struct sk_buff *skb, gfp_t flags)
{
	struct sk_buff *ret;
	struct vl600_frame_hdr *frame;
	struct vl600_pkt_hdr *packet;
	static uint32_t serial = 1;
	int orig_len = skb->len - sizeof(struct ethhdr);
	int full_len = (skb->len + sizeof(struct vl600_frame_hdr) + 3) & ~3;

	frame = (struct vl600_frame_hdr *) skb->data;
	if (skb->len > sizeof(*frame) && skb->len == le32_to_cpup(&frame->len))
		return skb; /* Already encapsulated? */

	if (skb->len < sizeof(struct ethhdr))
		/* Drop, device can only deal with ethernet packets */
		return NULL;

	if (!skb_cloned(skb)) {
		int headroom = skb_headroom(skb);
		int tailroom = skb_tailroom(skb);

		if (tailroom >= full_len - skb->len - sizeof(*frame) &&
				headroom >= sizeof(*frame))
			/* There's enough head and tail room */
			goto encapsulate;

		if (headroom + tailroom + skb->len >= full_len) {
			/* There's enough total room, just readjust */
			skb->data = memmove(skb->head + sizeof(*frame),
					skb->data, skb->len);
			skb_set_tail_pointer(skb, skb->len);
			goto encapsulate;
		}
	}

	/* Alloc a new skb with the required size */
	ret = skb_copy_expand(skb, sizeof(struct vl600_frame_hdr), full_len -
			skb->len - sizeof(struct vl600_frame_hdr), flags);
	dev_kfree_skb_any(skb);
	if (!ret)
		return ret;
	skb = ret;

encapsulate:
	/* Packet header is same size as ethernet packet header
	 * (sizeof(*packet) == sizeof(struct ethhdr)), additionally the
	 * h_proto field is in the same place so we just leave it alone and
	 * overwrite the remaining fields.
	 */
	packet = (struct vl600_pkt_hdr *) skb->data;
	memset(&packet->dummy, 0, sizeof(packet->dummy));
	packet->len = cpu_to_le32(orig_len);

	frame = (struct vl600_frame_hdr *) skb_push(skb, sizeof(*frame));
	memset(frame, 0, sizeof(*frame));
	frame->len = cpu_to_le32(full_len);
	frame->serial = cpu_to_le32(serial++);
	frame->pkt_cnt = cpu_to_le32(1);

	if (skb->len < full_len) /* Pad */
		skb_put(skb, full_len - skb->len);

	/* The VL600 wants IPv6 packets to have an IPv4 ethertype
	 * Check if this is an IPv6 packet, and set the ethertype
	 * to 0x800
	 */
	if ((skb->data[sizeof(struct vl600_pkt_hdr *) + 0x22] & 0xf0) == 0x60) {
		skb->data[sizeof(struct vl600_pkt_hdr *) + 0x20] = 0x08;
		skb->data[sizeof(struct vl600_pkt_hdr *) + 0x21] = 0;
	}

	return skb;
}

static const struct driver_info	vl600_info = {
	.description	= "LG VL600 modem",
	.flags		= FLAG_ETHER | FLAG_RX_ASSEMBLE,
	.bind		= vl600_bind,
	.unbind		= vl600_unbind,
	.status		= usbnet_cdc_status,
	.rx_fixup	= vl600_rx_fixup,
	.tx_fixup	= vl600_tx_fixup,
};

static const struct usb_device_id products[] = {
	{
		USB_DEVICE_AND_INTERFACE_INFO(0x1004, 0x61aa, USB_CLASS_COMM,
				USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
		.driver_info	= (unsigned long) &vl600_info,
	},
	{},	/* End */
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver lg_vl600_driver = {
	.name		= "lg-vl600",
	.id_table	= products,
	.probe		= usbnet_probe,
	.disconnect	= usbnet_disconnect,
	.suspend	= usbnet_suspend,
	.resume		= usbnet_resume,
};

static int __init vl600_init(void)
{
	return usb_register(&lg_vl600_driver);
}
module_init(vl600_init);

static void __exit vl600_exit(void)
{
	usb_deregister(&lg_vl600_driver);
}
module_exit(vl600_exit);

MODULE_AUTHOR("Anrzej Zaborowski");
MODULE_DESCRIPTION("LG-VL600 modem's ethernet link");
MODULE_LICENSE("GPL");
