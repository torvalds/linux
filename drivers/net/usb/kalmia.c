// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * USB network interface driver for Samsung Kalmia based LTE USB modem like the
 * Samsung GT-B3730 and GT-B3710.
 *
 * Copyright (C) 2011 Marius Bjoernstad Kotsbak <marius@kotsbak.com>
 *
 * Sponsored by Quicklink Video Distribution Services Ltd.
 *
 * Based on the cdc_eem module.
 */

#include <linux/module.h>
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
 * The Samsung Kalmia based LTE USB modems have a CDC ACM port for modem control
 * handled by the "option" module and an ethernet data port handled by this
 * module.
 *
 * The stick must first be switched into modem mode by usb_modeswitch
 * or similar tool. Then the modem gets sent two initialization packets by
 * this module, which gives the MAC address of the device. User space can then
 * connect the modem using AT commands through the ACM port and then use
 * DHCP on the network interface exposed by this module. Network packets are
 * sent to and from the modem in a proprietary format discovered after watching
 * the behavior of the windows driver for the modem.
 *
 * More information about the use of the modem is available in usb_modeswitch
 * forum and the project page:
 *
 * http://www.draisberghof.de/usb_modeswitch/bb/viewtopic.php?t=465
 * https://github.com/mkotsbak/Samsung-GT-B3730-linux-driver
 */

/* #define	DEBUG */
/* #define	VERBOSE */

#define KALMIA_HEADER_LENGTH 6
#define KALMIA_ALIGN_SIZE 4
#define KALMIA_USB_TIMEOUT 10000

/*-------------------------------------------------------------------------*/

static int
kalmia_send_init_packet(struct usbnet *dev, u8 *init_msg, u8 init_msg_len,
	u8 *buffer, u8 expected_len)
{
	int act_len;
	int status;

	netdev_dbg(dev->net, "Sending init packet");

	status = usb_bulk_msg(dev->udev, usb_sndbulkpipe(dev->udev, 0x02),
		init_msg, init_msg_len, &act_len, KALMIA_USB_TIMEOUT);
	if (status != 0) {
		netdev_err(dev->net,
			"Error sending init packet. Status %i\n",
			status);
		return status;
	}
	else if (act_len != init_msg_len) {
		netdev_err(dev->net,
			"Did not send all of init packet. Bytes sent: %i",
			act_len);
	}
	else {
		netdev_dbg(dev->net, "Successfully sent init packet.");
	}

	status = usb_bulk_msg(dev->udev, usb_rcvbulkpipe(dev->udev, 0x81),
		buffer, expected_len, &act_len, KALMIA_USB_TIMEOUT);

	if (status != 0)
		netdev_err(dev->net,
			"Error receiving init result. Status %i\n",
			status);
	else if (act_len != expected_len)
		netdev_err(dev->net, "Unexpected init result length: %i\n",
			act_len);

	return status;
}

static int
kalmia_init_and_get_ethernet_addr(struct usbnet *dev, u8 *ethernet_addr)
{
	static const char init_msg_1[] =
		{ 0x57, 0x50, 0x04, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
		0x00, 0x00 };
	static const char init_msg_2[] =
		{ 0x57, 0x50, 0x04, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xf4,
		0x00, 0x00 };
	static const int buflen = 28;
	char *usb_buf;
	int status;

	usb_buf = kmalloc(buflen, GFP_DMA | GFP_KERNEL);
	if (!usb_buf)
		return -ENOMEM;

	memcpy(usb_buf, init_msg_1, 12);
	status = kalmia_send_init_packet(dev, usb_buf, ARRAY_SIZE(init_msg_1),
					 usb_buf, 24);
	if (status != 0)
		goto out;

	memcpy(usb_buf, init_msg_2, 12);
	status = kalmia_send_init_packet(dev, usb_buf, ARRAY_SIZE(init_msg_2),
					 usb_buf, 28);
	if (status != 0)
		goto out;

	memcpy(ethernet_addr, usb_buf + 10, ETH_ALEN);
out:
	kfree(usb_buf);
	return status;
}

static int
kalmia_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int status;
	u8 ethernet_addr[ETH_ALEN];

	/* Don't bind to AT command interface */
	if (intf->cur_altsetting->desc.bInterfaceClass != USB_CLASS_VENDOR_SPEC)
		return -EINVAL;

	dev->in = usb_rcvbulkpipe(dev->udev, 0x81 & USB_ENDPOINT_NUMBER_MASK);
	dev->out = usb_sndbulkpipe(dev->udev, 0x02 & USB_ENDPOINT_NUMBER_MASK);
	dev->status = NULL;

	dev->net->hard_header_len += KALMIA_HEADER_LENGTH;
	dev->hard_mtu = 1400;
	dev->rx_urb_size = dev->hard_mtu * 10; // Found as optimal after testing

	status = kalmia_init_and_get_ethernet_addr(dev, ethernet_addr);
	if (status)
		return status;

	eth_hw_addr_set(dev->net, ethernet_addr);

	return status;
}

static struct sk_buff *
kalmia_tx_fixup(struct usbnet *dev, struct sk_buff *skb, gfp_t flags)
{
	struct sk_buff *skb2 = NULL;
	u16 content_len;
	unsigned char *header_start;
	unsigned char ether_type_1, ether_type_2;
	u8 remainder, padlen = 0;

	if (!skb_cloned(skb)) {
		int headroom = skb_headroom(skb);
		int tailroom = skb_tailroom(skb);

		if ((tailroom >= KALMIA_ALIGN_SIZE) && (headroom
			>= KALMIA_HEADER_LENGTH))
			goto done;

		if ((headroom + tailroom) > (KALMIA_HEADER_LENGTH
			+ KALMIA_ALIGN_SIZE)) {
			skb->data = memmove(skb->head + KALMIA_HEADER_LENGTH,
				skb->data, skb->len);
			skb_set_tail_pointer(skb, skb->len);
			goto done;
		}
	}

	skb2 = skb_copy_expand(skb, KALMIA_HEADER_LENGTH,
		KALMIA_ALIGN_SIZE, flags);
	if (!skb2)
		return NULL;

	dev_kfree_skb_any(skb);
	skb = skb2;

done:
	header_start = skb_push(skb, KALMIA_HEADER_LENGTH);
	ether_type_1 = header_start[KALMIA_HEADER_LENGTH + 12];
	ether_type_2 = header_start[KALMIA_HEADER_LENGTH + 13];

	netdev_dbg(dev->net, "Sending etherType: %02x%02x", ether_type_1,
		ether_type_2);

	/* According to empiric data for data packages */
	header_start[0] = 0x57;
	header_start[1] = 0x44;
	content_len = skb->len - KALMIA_HEADER_LENGTH;

	put_unaligned_le16(content_len, &header_start[2]);
	header_start[4] = ether_type_1;
	header_start[5] = ether_type_2;

	/* Align to 4 bytes by padding with zeros */
	remainder = skb->len % KALMIA_ALIGN_SIZE;
	if (remainder > 0) {
		padlen = KALMIA_ALIGN_SIZE - remainder;
		skb_put_zero(skb, padlen);
	}

	netdev_dbg(dev->net,
		"Sending package with length %i and padding %i. Header: %6phC.",
		content_len, padlen, header_start);

	return skb;
}

static int
kalmia_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	/*
	 * Our task here is to strip off framing, leaving skb with one
	 * data frame for the usbnet framework code to process.
	 */
	static const u8 HEADER_END_OF_USB_PACKET[] =
		{ 0x57, 0x5a, 0x00, 0x00, 0x08, 0x00 };
	static const u8 EXPECTED_UNKNOWN_HEADER_1[] =
		{ 0x57, 0x43, 0x1e, 0x00, 0x15, 0x02 };
	static const u8 EXPECTED_UNKNOWN_HEADER_2[] =
		{ 0x57, 0x50, 0x0e, 0x00, 0x00, 0x00 };
	int i = 0;

	/* incomplete header? */
	if (skb->len < KALMIA_HEADER_LENGTH)
		return 0;

	do {
		struct sk_buff *skb2 = NULL;
		u8 *header_start;
		u16 usb_packet_length, ether_packet_length;
		int is_last;

		header_start = skb->data;

		if (unlikely(header_start[0] != 0x57 || header_start[1] != 0x44)) {
			if (!memcmp(header_start, EXPECTED_UNKNOWN_HEADER_1,
				sizeof(EXPECTED_UNKNOWN_HEADER_1)) || !memcmp(
				header_start, EXPECTED_UNKNOWN_HEADER_2,
				sizeof(EXPECTED_UNKNOWN_HEADER_2))) {
				netdev_dbg(dev->net,
					"Received expected unknown frame header: %6phC. Package length: %i\n",
					header_start,
					skb->len - KALMIA_HEADER_LENGTH);
			}
			else {
				netdev_err(dev->net,
					"Received unknown frame header: %6phC. Package length: %i\n",
					header_start,
					skb->len - KALMIA_HEADER_LENGTH);
				return 0;
			}
		}
		else
			netdev_dbg(dev->net,
				"Received header: %6phC. Package length: %i\n",
				header_start, skb->len - KALMIA_HEADER_LENGTH);

		/* subtract start header and end header */
		usb_packet_length = skb->len - (2 * KALMIA_HEADER_LENGTH);
		ether_packet_length = get_unaligned_le16(&header_start[2]);
		skb_pull(skb, KALMIA_HEADER_LENGTH);

		/* Some small packets misses end marker */
		if (usb_packet_length < ether_packet_length) {
			ether_packet_length = usb_packet_length
				+ KALMIA_HEADER_LENGTH;
			is_last = true;
		}
		else {
			netdev_dbg(dev->net, "Correct package length #%i", i
				+ 1);

			is_last = (memcmp(skb->data + ether_packet_length,
				HEADER_END_OF_USB_PACKET,
				sizeof(HEADER_END_OF_USB_PACKET)) == 0);
			if (!is_last) {
				header_start = skb->data + ether_packet_length;
				netdev_dbg(dev->net,
					"End header: %6phC. Package length: %i\n",
					header_start,
					skb->len - KALMIA_HEADER_LENGTH);
			}
		}

		if (is_last) {
			skb2 = skb;
		}
		else {
			skb2 = skb_clone(skb, GFP_ATOMIC);
			if (unlikely(!skb2))
				return 0;
		}

		skb_trim(skb2, ether_packet_length);

		if (is_last) {
			return 1;
		}
		else {
			usbnet_skb_return(dev, skb2);
			skb_pull(skb, ether_packet_length);
		}

		i++;
	}
	while (skb->len);

	return 1;
}

static const struct driver_info kalmia_info = {
	.description = "Samsung Kalmia LTE USB dongle",
	.flags = FLAG_WWAN,
	.bind = kalmia_bind,
	.rx_fixup = kalmia_rx_fixup,
	.tx_fixup = kalmia_tx_fixup
};

/*-------------------------------------------------------------------------*/

static const struct usb_device_id products[] = {
	/* The unswitched USB ID, to get the module auto loaded: */
	{ USB_DEVICE(0x04e8, 0x689a) },
	/* The stick switched into modem (by e.g. usb_modeswitch): */
	{ USB_DEVICE(0x04e8, 0x6889),
		.driver_info = (unsigned long) &kalmia_info, },
	{ /* EMPTY == end of list */} };
MODULE_DEVICE_TABLE( usb, products);

static struct usb_driver kalmia_driver = {
	.name = "kalmia",
	.id_table = products,
	.probe = usbnet_probe,
	.disconnect = usbnet_disconnect,
	.suspend = usbnet_suspend,
	.resume = usbnet_resume,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(kalmia_driver);

MODULE_AUTHOR("Marius Bjoernstad Kotsbak <marius@kotsbak.com>");
MODULE_DESCRIPTION("Samsung Kalmia USB network driver");
MODULE_LICENSE("GPL");
