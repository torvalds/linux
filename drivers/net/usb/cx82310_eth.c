// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for USB ethernet port of Conexant CX82310-based ADSL routers
 * Copyright (C) 2010 by Ondrej Zary
 * some parts inspired by the cxacru driver
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/usb/usbnet.h>

enum cx82310_cmd {
	CMD_START		= 0x84,	/* no effect? */
	CMD_STOP		= 0x85,	/* no effect? */
	CMD_GET_STATUS		= 0x90,	/* returns nothing? */
	CMD_GET_MAC_ADDR	= 0x91,	/* read MAC address */
	CMD_GET_LINK_STATUS	= 0x92,	/* not useful, link is always up */
	CMD_ETHERNET_MODE	= 0x99,	/* unknown, needed during init */
};

enum cx82310_status {
	STATUS_UNDEFINED,
	STATUS_SUCCESS,
	STATUS_ERROR,
	STATUS_UNSUPPORTED,
	STATUS_UNIMPLEMENTED,
	STATUS_PARAMETER_ERROR,
	STATUS_DBG_LOOPBACK,
};

#define CMD_PACKET_SIZE	64
#define CMD_TIMEOUT	100
#define CMD_REPLY_RETRY 5

#define CX82310_MTU	1514
#define CMD_EP		0x01

struct cx82310_priv {
	struct work_struct reenable_work;
	struct usbnet *dev;
};

/*
 * execute control command
 *  - optionally send some data (command parameters)
 *  - optionally wait for the reply
 *  - optionally read some data from the reply
 */
static int cx82310_cmd(struct usbnet *dev, enum cx82310_cmd cmd, bool reply,
		       u8 *wdata, int wlen, u8 *rdata, int rlen)
{
	int actual_len, retries, ret;
	struct usb_device *udev = dev->udev;
	u8 *buf = kzalloc(CMD_PACKET_SIZE, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	/* create command packet */
	buf[0] = cmd;
	if (wdata)
		memcpy(buf + 4, wdata, min_t(int, wlen, CMD_PACKET_SIZE - 4));

	/* send command packet */
	ret = usb_bulk_msg(udev, usb_sndbulkpipe(udev, CMD_EP), buf,
			   CMD_PACKET_SIZE, &actual_len, CMD_TIMEOUT);
	if (ret < 0) {
		if (cmd != CMD_GET_LINK_STATUS)
			dev_err(&dev->udev->dev, "send command %#x: error %d\n",
				cmd, ret);
		goto end;
	}

	if (reply) {
		/* wait for reply, retry if it's empty */
		for (retries = 0; retries < CMD_REPLY_RETRY; retries++) {
			ret = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, CMD_EP),
					   buf, CMD_PACKET_SIZE, &actual_len,
					   CMD_TIMEOUT);
			if (ret < 0) {
				if (cmd != CMD_GET_LINK_STATUS)
					dev_err(&dev->udev->dev,
						"reply receive error %d\n",
						ret);
				goto end;
			}
			if (actual_len > 0)
				break;
		}
		if (actual_len == 0) {
			dev_err(&dev->udev->dev, "no reply to command %#x\n",
				cmd);
			ret = -EIO;
			goto end;
		}
		if (buf[0] != cmd) {
			dev_err(&dev->udev->dev,
				"got reply to command %#x, expected: %#x\n",
				buf[0], cmd);
			ret = -EIO;
			goto end;
		}
		if (buf[1] != STATUS_SUCCESS) {
			dev_err(&dev->udev->dev, "command %#x failed: %#x\n",
				cmd, buf[1]);
			ret = -EIO;
			goto end;
		}
		if (rdata)
			memcpy(rdata, buf + 4,
			       min_t(int, rlen, CMD_PACKET_SIZE - 4));
	}
end:
	kfree(buf);
	return ret;
}

static int cx82310_enable_ethernet(struct usbnet *dev)
{
	int ret = cx82310_cmd(dev, CMD_ETHERNET_MODE, true, "\x01", 1, NULL, 0);

	if (ret)
		netdev_err(dev->net, "unable to enable ethernet mode: %d\n",
			   ret);
	return ret;
}

static void cx82310_reenable_work(struct work_struct *work)
{
	struct cx82310_priv *priv = container_of(work, struct cx82310_priv,
						 reenable_work);
	cx82310_enable_ethernet(priv->dev);
}

#define partial_len	data[0]		/* length of partial packet data */
#define partial_rem	data[1]		/* remaining (missing) data length */
#define partial_data	data[2]		/* partial packet data */

static int cx82310_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int ret;
	char buf[15];
	struct usb_device *udev = dev->udev;
	u8 link[3];
	int timeout = 50;
	struct cx82310_priv *priv;

	/* avoid ADSL modems - continue only if iProduct is "USB NET CARD" */
	if (usb_string(udev, udev->descriptor.iProduct, buf, sizeof(buf)) > 0
	    && strcmp(buf, "USB NET CARD")) {
		dev_info(&udev->dev, "ignoring: probably an ADSL modem\n");
		return -ENODEV;
	}

	ret = usbnet_get_endpoints(dev, intf);
	if (ret)
		return ret;

	/*
	 * this must not include ethernet header as the device can send partial
	 * packets with no header (and sometimes even empty URBs)
	 */
	dev->net->hard_header_len = 0;
	/* we can send at most 1514 bytes of data (+ 2-byte header) per URB */
	dev->hard_mtu = CX82310_MTU + 2;
	/* we can receive URBs up to 4KB from the device */
	dev->rx_urb_size = 4096;

	dev->partial_data = (unsigned long) kmalloc(dev->hard_mtu, GFP_KERNEL);
	if (!dev->partial_data)
		return -ENOMEM;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto err_partial;
	}
	dev->driver_priv = priv;
	INIT_WORK(&priv->reenable_work, cx82310_reenable_work);
	priv->dev = dev;

	/* wait for firmware to become ready (indicated by the link being up) */
	while (--timeout) {
		ret = cx82310_cmd(dev, CMD_GET_LINK_STATUS, true, NULL, 0,
				  link, sizeof(link));
		/* the command can time out during boot - it's not an error */
		if (!ret && link[0] == 1 && link[2] == 1)
			break;
		msleep(500);
	}
	if (!timeout) {
		dev_err(&udev->dev, "firmware not ready in time\n");
		ret = -ETIMEDOUT;
		goto err;
	}

	/* enable ethernet mode (?) */
	if (cx82310_enable_ethernet(dev))
		goto err;

	/* get the MAC address */
	ret = cx82310_cmd(dev, CMD_GET_MAC_ADDR, true, NULL, 0,
			  dev->net->dev_addr, ETH_ALEN);
	if (ret) {
		dev_err(&udev->dev, "unable to read MAC address: %d\n", ret);
		goto err;
	}

	/* start (does not seem to have any effect?) */
	ret = cx82310_cmd(dev, CMD_START, false, NULL, 0, NULL, 0);
	if (ret)
		goto err;

	return 0;
err:
	kfree(dev->driver_priv);
err_partial:
	kfree((void *)dev->partial_data);
	return ret;
}

static void cx82310_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	struct cx82310_priv *priv = dev->driver_priv;

	kfree((void *)dev->partial_data);
	cancel_work_sync(&priv->reenable_work);
	kfree(dev->driver_priv);
}

/*
 * RX is NOT easy - we can receive multiple packets per skb, each having 2-byte
 * packet length at the beginning.
 * The last packet might be incomplete (when it crosses the 4KB URB size),
 * continuing in the next skb (without any headers).
 * If a packet has odd length, there is one extra byte at the end (before next
 * packet or at the end of the URB).
 */
static int cx82310_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	int len;
	struct sk_buff *skb2;
	struct cx82310_priv *priv = dev->driver_priv;

	/*
	 * If the last skb ended with an incomplete packet, this skb contains
	 * end of that packet at the beginning.
	 */
	if (dev->partial_rem) {
		len = dev->partial_len + dev->partial_rem;
		skb2 = alloc_skb(len, GFP_ATOMIC);
		if (!skb2)
			return 0;
		skb_put(skb2, len);
		memcpy(skb2->data, (void *)dev->partial_data,
		       dev->partial_len);
		memcpy(skb2->data + dev->partial_len, skb->data,
		       dev->partial_rem);
		usbnet_skb_return(dev, skb2);
		skb_pull(skb, (dev->partial_rem + 1) & ~1);
		dev->partial_rem = 0;
		if (skb->len < 2)
			return 1;
	}

	/* a skb can contain multiple packets */
	while (skb->len > 1) {
		/* first two bytes are packet length */
		len = skb->data[0] | (skb->data[1] << 8);
		skb_pull(skb, 2);

		/* if last packet in the skb, let usbnet to process it */
		if (len == skb->len || len + 1 == skb->len) {
			skb_trim(skb, len);
			break;
		}

		if (len == 0xffff) {
			netdev_info(dev->net, "router was rebooted, re-enabling ethernet mode");
			schedule_work(&priv->reenable_work);
		} else if (len > CX82310_MTU) {
			dev_err(&dev->udev->dev, "RX packet too long: %d B\n",
				len);
			return 0;
		}

		/* incomplete packet, save it for the next skb */
		if (len > skb->len) {
			dev->partial_len = skb->len;
			dev->partial_rem = len - skb->len;
			memcpy((void *)dev->partial_data, skb->data,
			       dev->partial_len);
			skb_pull(skb, skb->len);
			break;
		}

		skb2 = alloc_skb(len, GFP_ATOMIC);
		if (!skb2)
			return 0;
		skb_put(skb2, len);
		memcpy(skb2->data, skb->data, len);
		/* process the packet */
		usbnet_skb_return(dev, skb2);

		skb_pull(skb, (len + 1) & ~1);
	}

	/* let usbnet process the last packet */
	return 1;
}

/* TX is easy, just add 2 bytes of length at the beginning */
static struct sk_buff *cx82310_tx_fixup(struct usbnet *dev, struct sk_buff *skb,
				       gfp_t flags)
{
	int len = skb->len;

	if (skb_cow_head(skb, 2)) {
		dev_kfree_skb_any(skb);
		return NULL;
	}
	skb_push(skb, 2);

	skb->data[0] = len;
	skb->data[1] = len >> 8;

	return skb;
}


static const struct driver_info	cx82310_info = {
	.description	= "Conexant CX82310 USB ethernet",
	.flags		= FLAG_ETHER,
	.bind		= cx82310_bind,
	.unbind		= cx82310_unbind,
	.rx_fixup	= cx82310_rx_fixup,
	.tx_fixup	= cx82310_tx_fixup,
};

#define USB_DEVICE_CLASS(vend, prod, cl, sc, pr) \
	.match_flags = USB_DEVICE_ID_MATCH_DEVICE | \
		       USB_DEVICE_ID_MATCH_DEV_INFO, \
	.idVendor = (vend), \
	.idProduct = (prod), \
	.bDeviceClass = (cl), \
	.bDeviceSubClass = (sc), \
	.bDeviceProtocol = (pr)

static const struct usb_device_id products[] = {
	{
		USB_DEVICE_CLASS(0x0572, 0xcb01, 0xff, 0, 0),
		.driver_info = (unsigned long) &cx82310_info
	},
	{ },
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver cx82310_driver = {
	.name		= "cx82310_eth",
	.id_table	= products,
	.probe		= usbnet_probe,
	.disconnect	= usbnet_disconnect,
	.suspend	= usbnet_suspend,
	.resume		= usbnet_resume,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(cx82310_driver);

MODULE_AUTHOR("Ondrej Zary");
MODULE_DESCRIPTION("Conexant CX82310-based ADSL router USB ethernet driver");
MODULE_LICENSE("GPL");
