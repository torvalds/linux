/*
 * USB 10M/100M ethernet adapter
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/crc32.h>
#include <linux/usb/usbnet.h>
#include <linux/slab.h>

#define CH9200_VID		0x1A86
#define CH9200_PID_E092		0xE092

#define CTRL_TIMEOUT_MS		1000

#define CONTROL_TIMEOUT_MS 1000

#define REQUEST_READ	0x0E
#define REQUEST_WRITE	0x0F

/* Address space:
 * 00-63 : MII
 * 64-128: MAC
 *
 * Note: all accesses must be 16-bit
 */

#define MAC_REG_CTRL 64
#define MAC_REG_STATUS 66
#define MAC_REG_INTERRUPT_MASK 68
#define MAC_REG_PHY_COMMAND 70
#define MAC_REG_PHY_DATA 72
#define MAC_REG_STATION_L 74
#define MAC_REG_STATION_M 76
#define MAC_REG_STATION_H 78
#define MAC_REG_HASH_L 80
#define MAC_REG_HASH_M1 82
#define MAC_REG_HASH_M2 84
#define MAC_REG_HASH_H 86
#define MAC_REG_THRESHOLD 88
#define MAC_REG_FIFO_DEPTH 90
#define MAC_REG_PAUSE 92
#define MAC_REG_FLOW_CONTROL 94

/* Control register bits
 *
 * Note: bits 13 and 15 are reserved
 */
#define LOOPBACK		(0x01 << 14)
#define BASE100X		(0x01 << 12)
#define MBPS_10			(0x01 << 11)
#define DUPLEX_MODE		(0x01 << 10)
#define PAUSE_FRAME		(0x01 << 9)
#define PROMISCUOUS		(0x01 << 8)
#define MULTICAST		(0x01 << 7)
#define BROADCAST		(0x01 << 6)
#define HASH			(0x01 << 5)
#define APPEND_PAD		(0x01 << 4)
#define APPEND_CRC		(0x01 << 3)
#define TRANSMITTER_ACTION	(0x01 << 2)
#define RECEIVER_ACTION		(0x01 << 1)
#define DMA_ACTION		(0x01 << 0)

/* Status register bits
 *
 * Note: bits 7-15 are reserved
 */
#define ALIGNMENT		(0x01 << 6)
#define FIFO_OVER_RUN		(0x01 << 5)
#define FIFO_UNDER_RUN		(0x01 << 4)
#define RX_ERROR		(0x01 << 3)
#define RX_COMPLETE		(0x01 << 2)
#define TX_ERROR		(0x01 << 1)
#define TX_COMPLETE		(0x01 << 0)

/* FIFO depth register bits
 *
 * Note: bits 6 and 14 are reserved
 */

#define ETH_TXBD		(0x01 << 15)
#define ETN_TX_FIFO_DEPTH	(0x01 << 8)
#define ETH_RXBD		(0x01 << 7)
#define ETH_RX_FIFO_DEPTH	(0x01 << 0)

static int control_read(struct usbnet *dev,
			unsigned char request, unsigned short value,
			unsigned short index, void *data, unsigned short size,
			int timeout)
{
	unsigned char *buf = NULL;
	unsigned char request_type;
	int err = 0;

	if (request == REQUEST_READ)
		request_type = (USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_OTHER);
	else
		request_type = (USB_DIR_IN | USB_TYPE_VENDOR |
				USB_RECIP_DEVICE);

	netdev_dbg(dev->net, "%s() index=0x%02x size=%d\n",
		   __func__, index, size);

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto err_out;
	}

	err = usb_control_msg(dev->udev,
			      usb_rcvctrlpipe(dev->udev, 0),
			      request, request_type, value, index, buf, size,
			      timeout);
	if (err == size)
		memcpy(data, buf, size);
	else if (err >= 0)
		err = -EINVAL;
	kfree(buf);

err_out:
	return err;
}

static int control_write(struct usbnet *dev, unsigned char request,
			 unsigned short value, unsigned short index,
			 void *data, unsigned short size, int timeout)
{
	unsigned char *buf = NULL;
	unsigned char request_type;
	int err = 0;

	if (request == REQUEST_WRITE)
		request_type = (USB_DIR_OUT | USB_TYPE_VENDOR |
				USB_RECIP_OTHER);
	else
		request_type = (USB_DIR_OUT | USB_TYPE_VENDOR |
				USB_RECIP_DEVICE);

	netdev_dbg(dev->net, "%s() index=0x%02x size=%d\n",
		   __func__, index, size);

	if (data) {
		buf = kmemdup(data, size, GFP_KERNEL);
		if (!buf) {
			err = -ENOMEM;
			goto err_out;
		}
	}

	err = usb_control_msg(dev->udev,
			      usb_sndctrlpipe(dev->udev, 0),
			      request, request_type, value, index, buf, size,
			      timeout);
	if (err >= 0 && err < size)
		err = -EINVAL;
	kfree(buf);

	return 0;

err_out:
	return err;
}

static int ch9200_mdio_read(struct net_device *netdev, int phy_id, int loc)
{
	struct usbnet *dev = netdev_priv(netdev);
	unsigned char buff[2];
	int ret;

	netdev_dbg(netdev, "%s phy_id:%02x loc:%02x\n",
		   __func__, phy_id, loc);

	if (phy_id != 0)
		return -ENODEV;

	ret = control_read(dev, REQUEST_READ, 0, loc * 2, buff, 0x02,
			   CONTROL_TIMEOUT_MS);
	if (ret < 0)
		return ret;

	return (buff[0] | buff[1] << 8);
}

static void ch9200_mdio_write(struct net_device *netdev,
			      int phy_id, int loc, int val)
{
	struct usbnet *dev = netdev_priv(netdev);
	unsigned char buff[2];

	netdev_dbg(netdev, "%s() phy_id=%02x loc:%02x\n",
		   __func__, phy_id, loc);

	if (phy_id != 0)
		return;

	buff[0] = (unsigned char)val;
	buff[1] = (unsigned char)(val >> 8);

	control_write(dev, REQUEST_WRITE, 0, loc * 2, buff, 0x02,
		      CONTROL_TIMEOUT_MS);
}

static int ch9200_link_reset(struct usbnet *dev)
{
	struct ethtool_cmd ecmd;

	mii_check_media(&dev->mii, 1, 1);
	mii_ethtool_gset(&dev->mii, &ecmd);

	netdev_dbg(dev->net, "%s() speed:%d duplex:%d\n",
		   __func__, ecmd.speed, ecmd.duplex);

	return 0;
}

static void ch9200_status(struct usbnet *dev, struct urb *urb)
{
	int link;
	unsigned char *buf;

	if (urb->actual_length < 16)
		return;

	buf = urb->transfer_buffer;
	link = !!(buf[0] & 0x01);

	if (link) {
		netif_carrier_on(dev->net);
		usbnet_defer_kevent(dev, EVENT_LINK_RESET);
	} else {
		netif_carrier_off(dev->net);
	}
}

static struct sk_buff *ch9200_tx_fixup(struct usbnet *dev, struct sk_buff *skb,
				       gfp_t flags)
{
	int i = 0;
	int len = 0;
	int tx_overhead = 0;

	tx_overhead = 0x40;

	len = skb->len;
	if (skb_cow_head(skb, tx_overhead)) {
		dev_kfree_skb_any(skb);
		return NULL;
	}

	__skb_push(skb, tx_overhead);
	/* usbnet adds padding if length is a multiple of packet size
	 * if so, adjust length value in header
	 */
	if ((skb->len % dev->maxpacket) == 0)
		len++;

	skb->data[0] = len;
	skb->data[1] = len >> 8;
	skb->data[2] = 0x00;
	skb->data[3] = 0x80;

	for (i = 4; i < 48; i++)
		skb->data[i] = 0x00;

	skb->data[48] = len;
	skb->data[49] = len >> 8;
	skb->data[50] = 0x00;
	skb->data[51] = 0x80;

	for (i = 52; i < 64; i++)
		skb->data[i] = 0x00;

	return skb;
}

static int ch9200_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	int len = 0;
	int rx_overhead = 0;

	rx_overhead = 64;

	if (unlikely(skb->len < rx_overhead)) {
		dev_err(&dev->udev->dev, "unexpected tiny rx frame\n");
		return 0;
	}

	len = (skb->data[skb->len - 16] | skb->data[skb->len - 15] << 8);
	skb_trim(skb, len);

	return 1;
}

static int get_mac_address(struct usbnet *dev, unsigned char *data)
{
	int err = 0;
	unsigned char mac_addr[0x06];
	int rd_mac_len = 0;

	netdev_dbg(dev->net, "%s:\n\tusbnet VID:%0x PID:%0x\n", __func__,
		   le16_to_cpu(dev->udev->descriptor.idVendor),
		   le16_to_cpu(dev->udev->descriptor.idProduct));

	memset(mac_addr, 0, sizeof(mac_addr));
	rd_mac_len = control_read(dev, REQUEST_READ, 0,
				  MAC_REG_STATION_L, mac_addr, 0x02,
				  CONTROL_TIMEOUT_MS);
	rd_mac_len += control_read(dev, REQUEST_READ, 0, MAC_REG_STATION_M,
				   mac_addr + 2, 0x02, CONTROL_TIMEOUT_MS);
	rd_mac_len += control_read(dev, REQUEST_READ, 0, MAC_REG_STATION_H,
				   mac_addr + 4, 0x02, CONTROL_TIMEOUT_MS);
	if (rd_mac_len != ETH_ALEN)
		err = -EINVAL;

	data[0] = mac_addr[5];
	data[1] = mac_addr[4];
	data[2] = mac_addr[3];
	data[3] = mac_addr[2];
	data[4] = mac_addr[1];
	data[5] = mac_addr[0];

	return err;
}

static int ch9200_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int retval = 0;
	unsigned char data[2];
	u8 addr[ETH_ALEN];

	retval = usbnet_get_endpoints(dev, intf);
	if (retval)
		return retval;

	dev->mii.dev = dev->net;
	dev->mii.mdio_read = ch9200_mdio_read;
	dev->mii.mdio_write = ch9200_mdio_write;
	dev->mii.reg_num_mask = 0x1f;

	dev->mii.phy_id_mask = 0x1f;

	dev->hard_mtu = dev->net->mtu + dev->net->hard_header_len;
	dev->rx_urb_size = 24 * 64 + 16;
	mii_nway_restart(&dev->mii);

	data[0] = 0x01;
	data[1] = 0x0F;
	retval = control_write(dev, REQUEST_WRITE, 0, MAC_REG_THRESHOLD, data,
			       0x02, CONTROL_TIMEOUT_MS);

	data[0] = 0xA0;
	data[1] = 0x90;
	retval = control_write(dev, REQUEST_WRITE, 0, MAC_REG_FIFO_DEPTH, data,
			       0x02, CONTROL_TIMEOUT_MS);

	data[0] = 0x30;
	data[1] = 0x00;
	retval = control_write(dev, REQUEST_WRITE, 0, MAC_REG_PAUSE, data,
			       0x02, CONTROL_TIMEOUT_MS);

	data[0] = 0x17;
	data[1] = 0xD8;
	retval = control_write(dev, REQUEST_WRITE, 0, MAC_REG_FLOW_CONTROL,
			       data, 0x02, CONTROL_TIMEOUT_MS);

	/* Undocumented register */
	data[0] = 0x01;
	data[1] = 0x00;
	retval = control_write(dev, REQUEST_WRITE, 0, 254, data, 0x02,
			       CONTROL_TIMEOUT_MS);

	data[0] = 0x5F;
	data[1] = 0x0D;
	retval = control_write(dev, REQUEST_WRITE, 0, MAC_REG_CTRL, data, 0x02,
			       CONTROL_TIMEOUT_MS);

	retval = get_mac_address(dev, addr);
	eth_hw_addr_set(dev->net, addr);

	return retval;
}

static const struct driver_info ch9200_info = {
	.description = "CH9200 USB to Network Adaptor",
	.flags = FLAG_ETHER,
	.bind = ch9200_bind,
	.rx_fixup = ch9200_rx_fixup,
	.tx_fixup = ch9200_tx_fixup,
	.status = ch9200_status,
	.link_reset = ch9200_link_reset,
	.reset = ch9200_link_reset,
};

static const struct usb_device_id ch9200_products[] = {
	{
	 USB_DEVICE(0x1A86, 0xE092),
	 .driver_info = (unsigned long)&ch9200_info,
	 },
	{},
};

MODULE_DEVICE_TABLE(usb, ch9200_products);

static struct usb_driver ch9200_driver = {
	.name = "ch9200",
	.id_table = ch9200_products,
	.probe = usbnet_probe,
	.disconnect = usbnet_disconnect,
	.suspend = usbnet_suspend,
	.resume = usbnet_resume,
};

module_usb_driver(ch9200_driver);

MODULE_DESCRIPTION("QinHeng CH9200 USB Network device");
MODULE_LICENSE("GPL");
