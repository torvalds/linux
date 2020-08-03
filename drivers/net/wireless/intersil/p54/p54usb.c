// SPDX-License-Identifier: GPL-2.0-only

/*
 * Linux device driver for USB based Prism54
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 *
 * Based on the islsm (softmac prism54) driver, which is:
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 */

#include <linux/usb.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <linux/module.h>
#include <net/mac80211.h>

#include "p54.h"
#include "lmac.h"
#include "p54usb.h"

MODULE_AUTHOR("Michael Wu <flamingice@sourmilk.net>");
MODULE_DESCRIPTION("Prism54 USB wireless driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("prism54usb");
MODULE_FIRMWARE("isl3886usb");
MODULE_FIRMWARE("isl3887usb");

static struct usb_driver p54u_driver;

/*
 * Note:
 *
 * Always update our wiki's device list (located at:
 * http://wireless.kernel.org/en/users/Drivers/p54/devices ),
 * whenever you add a new device.
 */

static const struct usb_device_id p54u_table[] = {
	/* Version 1 devices (pci chip + net2280) */
	{USB_DEVICE(0x0411, 0x0050)},	/* Buffalo WLI2-USB2-G54 */
	{USB_DEVICE(0x045e, 0x00c2)},	/* Microsoft MN-710 */
	{USB_DEVICE(0x0506, 0x0a11)},	/* 3COM 3CRWE254G72 */
	{USB_DEVICE(0x0675, 0x0530)},	/* DrayTek Vigor 530 */
	{USB_DEVICE(0x06b9, 0x0120)},	/* Thomson SpeedTouch 120g */
	{USB_DEVICE(0x0707, 0xee06)},	/* SMC 2862W-G */
	{USB_DEVICE(0x07aa, 0x001c)},	/* Corega CG-WLUSB2GT */
	{USB_DEVICE(0x083a, 0x4501)},	/* Accton 802.11g WN4501 USB */
	{USB_DEVICE(0x083a, 0x4502)},	/* Siemens Gigaset USB Adapter */
	{USB_DEVICE(0x083a, 0x5501)},	/* Phillips CPWUA054 */
	{USB_DEVICE(0x0846, 0x4200)},	/* Netgear WG121 */
	{USB_DEVICE(0x0846, 0x4210)},	/* Netgear WG121 the second ? */
	{USB_DEVICE(0x0846, 0x4220)},	/* Netgear WG111 */
	{USB_DEVICE(0x09aa, 0x1000)},	/* Spinnaker Proto board */
	{USB_DEVICE(0x0bf8, 0x1007)},	/* Fujitsu E-5400 USB */
	{USB_DEVICE(0x0cde, 0x0006)},	/* Medion 40900, Roper Europe */
	{USB_DEVICE(0x0db0, 0x6826)},	/* MSI UB54G (MS-6826) */
	{USB_DEVICE(0x107b, 0x55f2)},	/* Gateway WGU-210 (Gemtek) */
	{USB_DEVICE(0x124a, 0x4023)},	/* Shuttle PN15, Airvast WM168g, IOGear GWU513 */
	{USB_DEVICE(0x124a, 0x4026)},	/* AirVasT USB wireless device */
	{USB_DEVICE(0x1435, 0x0210)},	/* Inventel UR054G */
	{USB_DEVICE(0x15a9, 0x0002)},	/* Gemtek WUBI-100GW 802.11g */
	{USB_DEVICE(0x1630, 0x0005)},	/* 2Wire 802.11g USB (v1) / Z-Com */
	{USB_DEVICE(0x182d, 0x096b)},	/* Sitecom WL-107 */
	{USB_DEVICE(0x1915, 0x2234)},	/* Linksys WUSB54G OEM */
	{USB_DEVICE(0x1915, 0x2235)},	/* Linksys WUSB54G Portable OEM */
	{USB_DEVICE(0x2001, 0x3701)},	/* DLink DWL-G120 Spinnaker */
	{USB_DEVICE(0x2001, 0x3703)},	/* DLink DWL-G122 */
	{USB_DEVICE(0x2001, 0x3762)},	/* Conceptronic C54U */
	{USB_DEVICE(0x5041, 0x2234)},	/* Linksys WUSB54G */
	{USB_DEVICE(0x5041, 0x2235)},	/* Linksys WUSB54G Portable */

	/* Version 2 devices (3887) */
	{USB_DEVICE(0x0471, 0x1230)},   /* Philips CPWUA054/00 */
	{USB_DEVICE(0x050d, 0x7050)},	/* Belkin F5D7050 ver 1000 */
	{USB_DEVICE(0x0572, 0x2000)},	/* Cohiba Proto board */
	{USB_DEVICE(0x0572, 0x2002)},	/* Cohiba Proto board */
	{USB_DEVICE(0x06a9, 0x000e)},	/* Westell 802.11g USB (A90-211WG-01) */
	{USB_DEVICE(0x06b9, 0x0121)},	/* Thomson SpeedTouch 121g */
	{USB_DEVICE(0x0707, 0xee13)},   /* SMC 2862W-G version 2 */
	{USB_DEVICE(0x07aa, 0x0020)},	/* Corega WLUSB2GTST USB */
	{USB_DEVICE(0x0803, 0x4310)},	/* Zoom 4410a */
	{USB_DEVICE(0x083a, 0x4521)},   /* Siemens Gigaset USB Adapter 54 version 2 */
	{USB_DEVICE(0x083a, 0x4531)},	/* T-Com Sinus 154 data II */
	{USB_DEVICE(0x083a, 0xc501)},	/* Zoom Wireless-G 4410 */
	{USB_DEVICE(0x083a, 0xf503)},	/* Accton FD7050E ver 1010ec  */
	{USB_DEVICE(0x0846, 0x4240)},	/* Netgear WG111 (v2) */
	{USB_DEVICE(0x0915, 0x2000)},	/* Cohiba Proto board */
	{USB_DEVICE(0x0915, 0x2002)},	/* Cohiba Proto board */
	{USB_DEVICE(0x0baf, 0x0118)},   /* U.S. Robotics U5 802.11g Adapter*/
	{USB_DEVICE(0x0bf8, 0x1009)},   /* FUJITSU E-5400 USB D1700*/
	/* {USB_DEVICE(0x0cde, 0x0006)}, * Medion MD40900 already listed above,
					 * just noting it here for clarity */
	{USB_DEVICE(0x0cde, 0x0008)},	/* Sagem XG703A */
	{USB_DEVICE(0x0cde, 0x0015)},	/* Zcomax XG-705A */
	{USB_DEVICE(0x0d8e, 0x3762)},	/* DLink DWL-G120 Cohiba */
	{USB_DEVICE(0x124a, 0x4025)},	/* IOGear GWU513 (GW3887IK chip) */
	{USB_DEVICE(0x1260, 0xee22)},	/* SMC 2862W-G version 2 */
	{USB_DEVICE(0x13b1, 0x000a)},	/* Linksys WUSB54G ver 2 */
	{USB_DEVICE(0x13B1, 0x000C)},	/* Linksys WUSB54AG */
	{USB_DEVICE(0x1413, 0x5400)},   /* Telsey 802.11g USB2.0 Adapter */
	{USB_DEVICE(0x1435, 0x0427)},	/* Inventel UR054G */
	/* {USB_DEVICE(0x15a9, 0x0002)}, * Also SparkLAN WL-682 with 3887 */
	{USB_DEVICE(0x1668, 0x1050)},	/* Actiontec 802UIG-1 */
	{USB_DEVICE(0x1740, 0x1000)},	/* Senao NUB-350 */
	{USB_DEVICE(0x2001, 0x3704)},	/* DLink DWL-G122 rev A2 */
	{USB_DEVICE(0x2001, 0x3705)},	/* D-Link DWL-G120 rev C1 */
	{USB_DEVICE(0x413c, 0x5513)},	/* Dell WLA3310 USB Wireless Adapter */
	{USB_DEVICE(0x413c, 0x8102)},	/* Spinnaker DUT */
	{USB_DEVICE(0x413c, 0x8104)},	/* Cohiba Proto board */
	{}
};

MODULE_DEVICE_TABLE(usb, p54u_table);

static const struct {
	u32 intf;
	enum p54u_hw_type type;
	const char *fw;
	char hw[20];
} p54u_fwlist[__NUM_P54U_HWTYPES] = {
	{
		.type = P54U_NET2280,
		.intf = FW_LM86,
		.fw = "isl3886usb",
		.hw = "ISL3886 + net2280",
	},
	{
		.type = P54U_3887,
		.intf = FW_LM87,
		.fw = "isl3887usb",
		.hw = "ISL3887",
	},
};

static void p54u_rx_cb(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *) urb->context;
	struct p54u_rx_info *info = (struct p54u_rx_info *)skb->cb;
	struct ieee80211_hw *dev = info->dev;
	struct p54u_priv *priv = dev->priv;

	skb_unlink(skb, &priv->rx_queue);

	if (unlikely(urb->status)) {
		dev_kfree_skb_irq(skb);
		return;
	}

	skb_put(skb, urb->actual_length);

	if (priv->hw_type == P54U_NET2280)
		skb_pull(skb, priv->common.tx_hdr_len);
	if (priv->common.fw_interface == FW_LM87) {
		skb_pull(skb, 4);
		skb_put(skb, 4);
	}

	if (p54_rx(dev, skb)) {
		skb = dev_alloc_skb(priv->common.rx_mtu + 32);
		if (unlikely(!skb)) {
			/* TODO check rx queue length and refill *somewhere* */
			return;
		}

		info = (struct p54u_rx_info *) skb->cb;
		info->urb = urb;
		info->dev = dev;
		urb->transfer_buffer = skb_tail_pointer(skb);
		urb->context = skb;
	} else {
		if (priv->hw_type == P54U_NET2280)
			skb_push(skb, priv->common.tx_hdr_len);
		if (priv->common.fw_interface == FW_LM87) {
			skb_push(skb, 4);
			skb_put(skb, 4);
		}
		skb_reset_tail_pointer(skb);
		skb_trim(skb, 0);
		urb->transfer_buffer = skb_tail_pointer(skb);
	}
	skb_queue_tail(&priv->rx_queue, skb);
	usb_anchor_urb(urb, &priv->submitted);
	if (usb_submit_urb(urb, GFP_ATOMIC)) {
		skb_unlink(skb, &priv->rx_queue);
		usb_unanchor_urb(urb);
		dev_kfree_skb_irq(skb);
	}
}

static void p54u_tx_cb(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct ieee80211_hw *dev =
		usb_get_intfdata(usb_ifnum_to_if(urb->dev, 0));

	p54_free_skb(dev, skb);
}

static void p54u_tx_dummy_cb(struct urb *urb) { }

static void p54u_free_urbs(struct ieee80211_hw *dev)
{
	struct p54u_priv *priv = dev->priv;
	usb_kill_anchored_urbs(&priv->submitted);
}

static void p54u_stop(struct ieee80211_hw *dev)
{
	/*
	 * TODO: figure out how to reliably stop the 3887 and net2280 so
	 * the hardware is still usable next time we want to start it.
	 * until then, we just stop listening to the hardware..
	 */
	p54u_free_urbs(dev);
}

static int p54u_init_urbs(struct ieee80211_hw *dev)
{
	struct p54u_priv *priv = dev->priv;
	struct urb *entry = NULL;
	struct sk_buff *skb;
	struct p54u_rx_info *info;
	int ret = 0;

	while (skb_queue_len(&priv->rx_queue) < 32) {
		skb = __dev_alloc_skb(priv->common.rx_mtu + 32, GFP_KERNEL);
		if (!skb) {
			ret = -ENOMEM;
			goto err;
		}
		entry = usb_alloc_urb(0, GFP_KERNEL);
		if (!entry) {
			ret = -ENOMEM;
			goto err;
		}

		usb_fill_bulk_urb(entry, priv->udev,
				  usb_rcvbulkpipe(priv->udev, P54U_PIPE_DATA),
				  skb_tail_pointer(skb),
				  priv->common.rx_mtu + 32, p54u_rx_cb, skb);
		info = (struct p54u_rx_info *) skb->cb;
		info->urb = entry;
		info->dev = dev;
		skb_queue_tail(&priv->rx_queue, skb);

		usb_anchor_urb(entry, &priv->submitted);
		ret = usb_submit_urb(entry, GFP_KERNEL);
		if (ret) {
			skb_unlink(skb, &priv->rx_queue);
			usb_unanchor_urb(entry);
			goto err;
		}
		usb_free_urb(entry);
		entry = NULL;
	}

	return 0;

 err:
	usb_free_urb(entry);
	kfree_skb(skb);
	p54u_free_urbs(dev);
	return ret;
}

static int p54u_open(struct ieee80211_hw *dev)
{
	/*
	 * TODO: Because we don't know how to reliably stop the 3887 and
	 * the isl3886+net2280, other than brutally cut off all
	 * communications. We have to reinitialize the urbs on every start.
	 */
	return p54u_init_urbs(dev);
}

static __le32 p54u_lm87_chksum(const __le32 *data, size_t length)
{
	u32 chk = 0;

	length >>= 2;
	while (length--) {
		chk ^= le32_to_cpu(*data++);
		chk = (chk >> 5) ^ (chk << 3);
	}

	return cpu_to_le32(chk);
}

static void p54u_tx_lm87(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54u_priv *priv = dev->priv;
	struct urb *data_urb;
	struct lm87_tx_hdr *hdr = (void *)skb->data - sizeof(*hdr);

	data_urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!data_urb) {
		p54_free_skb(dev, skb);
		return;
	}

	hdr->chksum = p54u_lm87_chksum((__le32 *)skb->data, skb->len);
	hdr->device_addr = ((struct p54_hdr *)skb->data)->req_id;

	usb_fill_bulk_urb(data_urb, priv->udev,
			  usb_sndbulkpipe(priv->udev, P54U_PIPE_DATA),
			  hdr, skb->len + sizeof(*hdr),  FREE_AFTER_TX(skb) ?
			  p54u_tx_cb : p54u_tx_dummy_cb, skb);
	data_urb->transfer_flags |= URB_ZERO_PACKET;

	usb_anchor_urb(data_urb, &priv->submitted);
	if (usb_submit_urb(data_urb, GFP_ATOMIC)) {
		usb_unanchor_urb(data_urb);
		p54_free_skb(dev, skb);
	}
	usb_free_urb(data_urb);
}

static void p54u_tx_net2280(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54u_priv *priv = dev->priv;
	struct urb *int_urb = NULL, *data_urb = NULL;
	struct net2280_tx_hdr *hdr = (void *)skb->data - sizeof(*hdr);
	struct net2280_reg_write *reg = NULL;
	int err = -ENOMEM;

	reg = kmalloc(sizeof(*reg), GFP_ATOMIC);
	if (!reg)
		goto out;

	int_urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!int_urb)
		goto out;

	data_urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!data_urb)
		goto out;

	reg->port = cpu_to_le16(NET2280_DEV_U32);
	reg->addr = cpu_to_le32(P54U_DEV_BASE);
	reg->val = cpu_to_le32(ISL38XX_DEV_INT_DATA);

	memset(hdr, 0, sizeof(*hdr));
	hdr->len = cpu_to_le16(skb->len);
	hdr->device_addr = ((struct p54_hdr *) skb->data)->req_id;

	usb_fill_bulk_urb(int_urb, priv->udev,
		usb_sndbulkpipe(priv->udev, P54U_PIPE_DEV), reg, sizeof(*reg),
		p54u_tx_dummy_cb, dev);

	/*
	 * URB_FREE_BUFFER triggers a code path in the USB subsystem that will
	 * free what is inside the transfer_buffer after the last reference to
	 * the int_urb is dropped.
	 */
	int_urb->transfer_flags |= URB_FREE_BUFFER | URB_ZERO_PACKET;
	reg = NULL;

	usb_fill_bulk_urb(data_urb, priv->udev,
			  usb_sndbulkpipe(priv->udev, P54U_PIPE_DATA),
			  hdr, skb->len + sizeof(*hdr), FREE_AFTER_TX(skb) ?
			  p54u_tx_cb : p54u_tx_dummy_cb, skb);
	data_urb->transfer_flags |= URB_ZERO_PACKET;

	usb_anchor_urb(int_urb, &priv->submitted);
	err = usb_submit_urb(int_urb, GFP_ATOMIC);
	if (err) {
		usb_unanchor_urb(int_urb);
		goto out;
	}

	usb_anchor_urb(data_urb, &priv->submitted);
	err = usb_submit_urb(data_urb, GFP_ATOMIC);
	if (err) {
		usb_unanchor_urb(data_urb);
		goto out;
	}
out:
	usb_free_urb(int_urb);
	usb_free_urb(data_urb);

	if (err) {
		kfree(reg);
		p54_free_skb(dev, skb);
	}
}

static int p54u_write(struct p54u_priv *priv,
		      struct net2280_reg_write *buf,
		      enum net2280_op_type type,
		      __le32 addr, __le32 val)
{
	unsigned int ep;
	int alen;

	if (type & 0x0800)
		ep = usb_sndbulkpipe(priv->udev, P54U_PIPE_DEV);
	else
		ep = usb_sndbulkpipe(priv->udev, P54U_PIPE_BRG);

	buf->port = cpu_to_le16(type);
	buf->addr = addr;
	buf->val = val;

	return usb_bulk_msg(priv->udev, ep, buf, sizeof(*buf), &alen, 1000);
}

static int p54u_read(struct p54u_priv *priv, void *buf,
		     enum net2280_op_type type,
		     __le32 addr, __le32 *val)
{
	struct net2280_reg_read *read = buf;
	__le32 *reg = buf;
	unsigned int ep;
	int alen, err;

	if (type & 0x0800)
		ep = P54U_PIPE_DEV;
	else
		ep = P54U_PIPE_BRG;

	read->port = cpu_to_le16(type);
	read->addr = addr;

	err = usb_bulk_msg(priv->udev, usb_sndbulkpipe(priv->udev, ep),
			   read, sizeof(*read), &alen, 1000);
	if (err)
		return err;

	err = usb_bulk_msg(priv->udev, usb_rcvbulkpipe(priv->udev, ep),
			   reg, sizeof(*reg), &alen, 1000);
	if (err)
		return err;

	*val = *reg;
	return 0;
}

static int p54u_bulk_msg(struct p54u_priv *priv, unsigned int ep,
			 void *data, size_t len)
{
	int alen;
	return usb_bulk_msg(priv->udev, usb_sndbulkpipe(priv->udev, ep),
			    data, len, &alen, 2000);
}

static int p54u_device_reset(struct ieee80211_hw *dev)
{
	struct p54u_priv *priv = dev->priv;
	int ret, lock = (priv->intf->condition != USB_INTERFACE_BINDING);

	if (lock) {
		ret = usb_lock_device_for_reset(priv->udev, priv->intf);
		if (ret < 0) {
			dev_err(&priv->udev->dev, "(p54usb) unable to lock "
				"device for reset (%d)!\n", ret);
			return ret;
		}
	}

	ret = usb_reset_device(priv->udev);
	if (lock)
		usb_unlock_device(priv->udev);

	if (ret)
		dev_err(&priv->udev->dev, "(p54usb) unable to reset "
			"device (%d)!\n", ret);

	return ret;
}

static const char p54u_romboot_3887[] = "~~~~";
static int p54u_firmware_reset_3887(struct ieee80211_hw *dev)
{
	struct p54u_priv *priv = dev->priv;
	u8 *buf;
	int ret;

	buf = kmemdup(p54u_romboot_3887, 4, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	ret = p54u_bulk_msg(priv, P54U_PIPE_DATA,
			    buf, 4);
	kfree(buf);
	if (ret)
		dev_err(&priv->udev->dev, "(p54usb) unable to jump to "
			"boot ROM (%d)!\n", ret);

	return ret;
}

static const char p54u_firmware_upload_3887[] = "<\r";
static int p54u_upload_firmware_3887(struct ieee80211_hw *dev)
{
	struct p54u_priv *priv = dev->priv;
	int err, alen;
	u8 carry = 0;
	u8 *buf, *tmp;
	const u8 *data;
	unsigned int left, remains, block_size;
	struct x2_header *hdr;
	unsigned long timeout;

	err = p54u_firmware_reset_3887(dev);
	if (err)
		return err;

	tmp = buf = kmalloc(P54U_FW_BLOCK, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	left = block_size = min_t(size_t, P54U_FW_BLOCK, priv->fw->size);
	strcpy(buf, p54u_firmware_upload_3887);
	left -= strlen(p54u_firmware_upload_3887);
	tmp += strlen(p54u_firmware_upload_3887);

	data = priv->fw->data;
	remains = priv->fw->size;

	hdr = (struct x2_header *)(buf + strlen(p54u_firmware_upload_3887));
	memcpy(hdr->signature, X2_SIGNATURE, X2_SIGNATURE_SIZE);
	hdr->fw_load_addr = cpu_to_le32(ISL38XX_DEV_FIRMWARE_ADDR);
	hdr->fw_length = cpu_to_le32(priv->fw->size);
	hdr->crc = cpu_to_le32(~crc32_le(~0, (void *)&hdr->fw_load_addr,
					 sizeof(u32)*2));
	left -= sizeof(*hdr);
	tmp += sizeof(*hdr);

	while (remains) {
		while (left--) {
			if (carry) {
				*tmp++ = carry;
				carry = 0;
				remains--;
				continue;
			}
			switch (*data) {
			case '~':
				*tmp++ = '}';
				carry = '^';
				break;
			case '}':
				*tmp++ = '}';
				carry = ']';
				break;
			default:
				*tmp++ = *data;
				remains--;
				break;
			}
			data++;
		}

		err = p54u_bulk_msg(priv, P54U_PIPE_DATA, buf, block_size);
		if (err) {
			dev_err(&priv->udev->dev, "(p54usb) firmware "
						  "upload failed!\n");
			goto err_upload_failed;
		}

		tmp = buf;
		left = block_size = min((unsigned int)P54U_FW_BLOCK, remains);
	}

	*((__le32 *)buf) = cpu_to_le32(~crc32_le(~0, priv->fw->data,
						 priv->fw->size));
	err = p54u_bulk_msg(priv, P54U_PIPE_DATA, buf, sizeof(u32));
	if (err) {
		dev_err(&priv->udev->dev, "(p54usb) firmware upload failed!\n");
		goto err_upload_failed;
	}
	timeout = jiffies + msecs_to_jiffies(1000);
	while (!(err = usb_bulk_msg(priv->udev,
		usb_rcvbulkpipe(priv->udev, P54U_PIPE_DATA), buf, 128, &alen, 1000))) {
		if (alen > 2 && !memcmp(buf, "OK", 2))
			break;

		if (alen > 5 && !memcmp(buf, "ERROR", 5)) {
			err = -EINVAL;
			break;
		}

		if (time_after(jiffies, timeout)) {
			dev_err(&priv->udev->dev, "(p54usb) firmware boot "
						  "timed out!\n");
			err = -ETIMEDOUT;
			break;
		}
	}
	if (err) {
		dev_err(&priv->udev->dev, "(p54usb) firmware upload failed!\n");
		goto err_upload_failed;
	}

	buf[0] = 'g';
	buf[1] = '\r';
	err = p54u_bulk_msg(priv, P54U_PIPE_DATA, buf, 2);
	if (err) {
		dev_err(&priv->udev->dev, "(p54usb) firmware boot failed!\n");
		goto err_upload_failed;
	}

	timeout = jiffies + msecs_to_jiffies(1000);
	while (!(err = usb_bulk_msg(priv->udev,
		usb_rcvbulkpipe(priv->udev, P54U_PIPE_DATA), buf, 128, &alen, 1000))) {
		if (alen > 0 && buf[0] == 'g')
			break;

		if (time_after(jiffies, timeout)) {
			err = -ETIMEDOUT;
			break;
		}
	}
	if (err)
		goto err_upload_failed;

err_upload_failed:
	kfree(buf);
	return err;
}

static int p54u_upload_firmware_net2280(struct ieee80211_hw *dev)
{
	struct p54u_priv *priv = dev->priv;
	const struct p54p_csr *devreg = (const struct p54p_csr *) P54U_DEV_BASE;
	int err, alen;
	void *buf;
	__le32 reg;
	unsigned int remains, offset;
	const u8 *data;

	buf = kmalloc(512, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

#define P54U_WRITE(type, addr, data) \
	do {\
		err = p54u_write(priv, buf, type,\
				 cpu_to_le32((u32)(unsigned long)addr), data);\
		if (err) \
			goto fail;\
	} while (0)

#define P54U_READ(type, addr) \
	do {\
		err = p54u_read(priv, buf, type,\
				cpu_to_le32((u32)(unsigned long)addr), &reg);\
		if (err)\
			goto fail;\
	} while (0)

	/* power down net2280 bridge */
	P54U_READ(NET2280_BRG_U32, NET2280_GPIOCTL);
	reg |= cpu_to_le32(P54U_BRG_POWER_DOWN);
	reg &= cpu_to_le32(~P54U_BRG_POWER_UP);
	P54U_WRITE(NET2280_BRG_U32, NET2280_GPIOCTL, reg);

	mdelay(100);

	/* power up bridge */
	reg |= cpu_to_le32(P54U_BRG_POWER_UP);
	reg &= cpu_to_le32(~P54U_BRG_POWER_DOWN);
	P54U_WRITE(NET2280_BRG_U32, NET2280_GPIOCTL, reg);

	mdelay(100);

	P54U_WRITE(NET2280_BRG_U32, NET2280_DEVINIT,
		   cpu_to_le32(NET2280_CLK_30Mhz |
			       NET2280_PCI_ENABLE |
			       NET2280_PCI_SOFT_RESET));

	mdelay(20);

	P54U_WRITE(NET2280_BRG_CFG_U16, PCI_COMMAND,
		   cpu_to_le32(PCI_COMMAND_MEMORY |
			       PCI_COMMAND_MASTER));

	P54U_WRITE(NET2280_BRG_CFG_U32, PCI_BASE_ADDRESS_0,
		   cpu_to_le32(NET2280_BASE));

	P54U_READ(NET2280_BRG_CFG_U16, PCI_STATUS);
	reg |= cpu_to_le32(PCI_STATUS_REC_MASTER_ABORT);
	P54U_WRITE(NET2280_BRG_CFG_U16, PCI_STATUS, reg);

	// TODO: we really need this?
	P54U_READ(NET2280_BRG_U32, NET2280_RELNUM);

	P54U_WRITE(NET2280_BRG_U32, NET2280_EPA_RSP,
		   cpu_to_le32(NET2280_CLEAR_NAK_OUT_PACKETS_MODE));
	P54U_WRITE(NET2280_BRG_U32, NET2280_EPC_RSP,
		   cpu_to_le32(NET2280_CLEAR_NAK_OUT_PACKETS_MODE));

	P54U_WRITE(NET2280_BRG_CFG_U32, PCI_BASE_ADDRESS_2,
		   cpu_to_le32(NET2280_BASE2));

	/* finally done setting up the bridge */

	P54U_WRITE(NET2280_DEV_CFG_U16, 0x10000 | PCI_COMMAND,
		   cpu_to_le32(PCI_COMMAND_MEMORY |
			       PCI_COMMAND_MASTER));

	P54U_WRITE(NET2280_DEV_CFG_U16, 0x10000 | 0x40 /* TRDY timeout */, 0);
	P54U_WRITE(NET2280_DEV_CFG_U32, 0x10000 | PCI_BASE_ADDRESS_0,
		   cpu_to_le32(P54U_DEV_BASE));

	P54U_WRITE(NET2280_BRG_U32, NET2280_USBIRQENB1, 0);
	P54U_WRITE(NET2280_BRG_U32, NET2280_IRQSTAT1,
		   cpu_to_le32(NET2280_PCI_INTA_INTERRUPT));

	/* do romboot */
	P54U_WRITE(NET2280_DEV_U32, &devreg->int_enable, 0);

	P54U_READ(NET2280_DEV_U32, &devreg->ctrl_stat);
	reg &= cpu_to_le32(~ISL38XX_CTRL_STAT_RESET);
	reg &= cpu_to_le32(~ISL38XX_CTRL_STAT_RAMBOOT);
	reg &= cpu_to_le32(~ISL38XX_CTRL_STAT_CLKRUN);
	P54U_WRITE(NET2280_DEV_U32, &devreg->ctrl_stat, reg);

	mdelay(20);

	reg |= cpu_to_le32(ISL38XX_CTRL_STAT_RESET);
	P54U_WRITE(NET2280_DEV_U32, &devreg->ctrl_stat, reg);

	mdelay(20);

	reg &= cpu_to_le32(~ISL38XX_CTRL_STAT_RESET);
	P54U_WRITE(NET2280_DEV_U32, &devreg->ctrl_stat, reg);

	mdelay(100);

	P54U_READ(NET2280_DEV_U32, &devreg->int_ident);
	P54U_WRITE(NET2280_DEV_U32, &devreg->int_ack, reg);

	/* finally, we can upload firmware now! */
	remains = priv->fw->size;
	data = priv->fw->data;
	offset = ISL38XX_DEV_FIRMWARE_ADDR;

	while (remains) {
		unsigned int block_len = min(remains, (unsigned int)512);
		memcpy(buf, data, block_len);

		err = p54u_bulk_msg(priv, P54U_PIPE_DATA, buf, block_len);
		if (err) {
			dev_err(&priv->udev->dev, "(p54usb) firmware block "
						  "upload failed\n");
			goto fail;
		}

		P54U_WRITE(NET2280_DEV_U32, &devreg->direct_mem_base,
			   cpu_to_le32(0xc0000f00));

		P54U_WRITE(NET2280_DEV_U32,
			   0x0020 | (unsigned long)&devreg->direct_mem_win, 0);
		P54U_WRITE(NET2280_DEV_U32,
			   0x0020 | (unsigned long)&devreg->direct_mem_win,
			   cpu_to_le32(1));

		P54U_WRITE(NET2280_DEV_U32,
			   0x0024 | (unsigned long)&devreg->direct_mem_win,
			   cpu_to_le32(block_len));
		P54U_WRITE(NET2280_DEV_U32,
			   0x0028 | (unsigned long)&devreg->direct_mem_win,
			   cpu_to_le32(offset));

		P54U_WRITE(NET2280_DEV_U32, &devreg->dma_addr,
			   cpu_to_le32(NET2280_EPA_FIFO_PCI_ADDR));
		P54U_WRITE(NET2280_DEV_U32, &devreg->dma_len,
			   cpu_to_le32(block_len >> 2));
		P54U_WRITE(NET2280_DEV_U32, &devreg->dma_ctrl,
			   cpu_to_le32(ISL38XX_DMA_MASTER_CONTROL_TRIGGER));

		mdelay(10);

		P54U_READ(NET2280_DEV_U32,
			  0x002C | (unsigned long)&devreg->direct_mem_win);
		if (!(reg & cpu_to_le32(ISL38XX_DMA_STATUS_DONE)) ||
		    !(reg & cpu_to_le32(ISL38XX_DMA_STATUS_READY))) {
			dev_err(&priv->udev->dev, "(p54usb) firmware DMA "
						  "transfer failed\n");
			goto fail;
		}

		P54U_WRITE(NET2280_BRG_U32, NET2280_EPA_STAT,
			   cpu_to_le32(NET2280_FIFO_FLUSH));

		remains -= block_len;
		data += block_len;
		offset += block_len;
	}

	/* do ramboot */
	P54U_READ(NET2280_DEV_U32, &devreg->ctrl_stat);
	reg &= cpu_to_le32(~ISL38XX_CTRL_STAT_RESET);
	reg &= cpu_to_le32(~ISL38XX_CTRL_STAT_CLKRUN);
	reg |= cpu_to_le32(ISL38XX_CTRL_STAT_RAMBOOT);
	P54U_WRITE(NET2280_DEV_U32, &devreg->ctrl_stat, reg);

	mdelay(20);

	reg |= cpu_to_le32(ISL38XX_CTRL_STAT_RESET);
	P54U_WRITE(NET2280_DEV_U32, &devreg->ctrl_stat, reg);

	reg &= cpu_to_le32(~ISL38XX_CTRL_STAT_RESET);
	P54U_WRITE(NET2280_DEV_U32, &devreg->ctrl_stat, reg);

	mdelay(100);

	P54U_READ(NET2280_DEV_U32, &devreg->int_ident);
	P54U_WRITE(NET2280_DEV_U32, &devreg->int_ack, reg);

	/* start up the firmware */
	P54U_WRITE(NET2280_DEV_U32, &devreg->int_enable,
		   cpu_to_le32(ISL38XX_INT_IDENT_INIT));

	P54U_WRITE(NET2280_BRG_U32, NET2280_IRQSTAT1,
		   cpu_to_le32(NET2280_PCI_INTA_INTERRUPT));

	P54U_WRITE(NET2280_BRG_U32, NET2280_USBIRQENB1,
		   cpu_to_le32(NET2280_PCI_INTA_INTERRUPT_ENABLE |
			       NET2280_USB_INTERRUPT_ENABLE));

	P54U_WRITE(NET2280_DEV_U32, &devreg->dev_int,
		   cpu_to_le32(ISL38XX_DEV_INT_RESET));

	err = usb_interrupt_msg(priv->udev,
				usb_rcvbulkpipe(priv->udev, P54U_PIPE_INT),
				buf, sizeof(__le32), &alen, 1000);
	if (err || alen != sizeof(__le32))
		goto fail;

	P54U_READ(NET2280_DEV_U32, &devreg->int_ident);
	P54U_WRITE(NET2280_DEV_U32, &devreg->int_ack, reg);

	if (!(reg & cpu_to_le32(ISL38XX_INT_IDENT_INIT)))
		err = -EINVAL;

	P54U_WRITE(NET2280_BRG_U32, NET2280_USBIRQENB1, 0);
	P54U_WRITE(NET2280_BRG_U32, NET2280_IRQSTAT1,
		   cpu_to_le32(NET2280_PCI_INTA_INTERRUPT));

#undef P54U_WRITE
#undef P54U_READ

fail:
	kfree(buf);
	return err;
}

static int p54_find_type(struct p54u_priv *priv)
{
	int i;

	for (i = 0; i < __NUM_P54U_HWTYPES; i++)
		if (p54u_fwlist[i].type == priv->hw_type)
			break;
	if (i == __NUM_P54U_HWTYPES)
		return -EOPNOTSUPP;

	return i;
}

static int p54u_start_ops(struct p54u_priv *priv)
{
	struct ieee80211_hw *dev = priv->common.hw;
	int ret;

	ret = p54_parse_firmware(dev, priv->fw);
	if (ret)
		goto err_out;

	ret = p54_find_type(priv);
	if (ret < 0)
		goto err_out;

	if (priv->common.fw_interface != p54u_fwlist[ret].intf) {
		dev_err(&priv->udev->dev, "wrong firmware, please get "
			"a firmware for \"%s\" and try again.\n",
			p54u_fwlist[ret].hw);
		ret = -ENODEV;
		goto err_out;
	}

	ret = priv->upload_fw(dev);
	if (ret)
		goto err_out;

	ret = p54u_open(dev);
	if (ret)
		goto err_out;

	ret = p54_read_eeprom(dev);
	if (ret)
		goto err_stop;

	p54u_stop(dev);

	ret = p54_register_common(dev, &priv->udev->dev);
	if (ret)
		goto err_stop;

	return 0;

err_stop:
	p54u_stop(dev);

err_out:
	/*
	 * p54u_disconnect will do the rest of the
	 * cleanup
	 */
	return ret;
}

static void p54u_load_firmware_cb(const struct firmware *firmware,
				  void *context)
{
	struct p54u_priv *priv = context;
	struct usb_device *udev = priv->udev;
	struct usb_interface *intf = priv->intf;
	int err;

	if (firmware) {
		priv->fw = firmware;
		err = p54u_start_ops(priv);
	} else {
		err = -ENOENT;
		dev_err(&udev->dev, "Firmware not found.\n");
	}

	complete(&priv->fw_wait_load);
	/*
	 * At this point p54u_disconnect may have already freed
	 * the "priv" context. Do not use it anymore!
	 */
	priv = NULL;

	if (err) {
		dev_err(&intf->dev, "failed to initialize device (%d)\n", err);

		usb_lock_device(udev);
		usb_driver_release_interface(&p54u_driver, intf);
		usb_unlock_device(udev);
	}

	usb_put_intf(intf);
}

static int p54u_load_firmware(struct ieee80211_hw *dev,
			      struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct p54u_priv *priv = dev->priv;
	struct device *device = &udev->dev;
	int err, i;

	BUILD_BUG_ON(ARRAY_SIZE(p54u_fwlist) != __NUM_P54U_HWTYPES);

	init_completion(&priv->fw_wait_load);
	i = p54_find_type(priv);
	if (i < 0)
		return i;

	dev_info(&priv->udev->dev, "Loading firmware file %s\n",
	       p54u_fwlist[i].fw);

	usb_get_intf(intf);
	err = request_firmware_nowait(THIS_MODULE, 1, p54u_fwlist[i].fw,
				      device, GFP_KERNEL, priv,
				      p54u_load_firmware_cb);
	if (err) {
		dev_err(&priv->udev->dev, "(p54usb) cannot load firmware %s "
					  "(%d)!\n", p54u_fwlist[i].fw, err);
		usb_put_intf(intf);
	}

	return err;
}

static int p54u_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct ieee80211_hw *dev;
	struct p54u_priv *priv;
	int err;
	unsigned int i, recognized_pipes;

	dev = p54_init_common(sizeof(*priv));

	if (!dev) {
		dev_err(&udev->dev, "(p54usb) ieee80211 alloc failed\n");
		return -ENOMEM;
	}

	priv = dev->priv;
	priv->hw_type = P54U_INVALID_HW;

	SET_IEEE80211_DEV(dev, &intf->dev);
	usb_set_intfdata(intf, dev);
	priv->udev = udev;
	priv->intf = intf;
	skb_queue_head_init(&priv->rx_queue);
	init_usb_anchor(&priv->submitted);

	/* really lazy and simple way of figuring out if we're a 3887 */
	/* TODO: should just stick the identification in the device table */
	i = intf->altsetting->desc.bNumEndpoints;
	recognized_pipes = 0;
	while (i--) {
		switch (intf->altsetting->endpoint[i].desc.bEndpointAddress) {
		case P54U_PIPE_DATA:
		case P54U_PIPE_MGMT:
		case P54U_PIPE_BRG:
		case P54U_PIPE_DEV:
		case P54U_PIPE_DATA | USB_DIR_IN:
		case P54U_PIPE_MGMT | USB_DIR_IN:
		case P54U_PIPE_BRG | USB_DIR_IN:
		case P54U_PIPE_DEV | USB_DIR_IN:
		case P54U_PIPE_INT | USB_DIR_IN:
			recognized_pipes++;
		}
	}
	priv->common.open = p54u_open;
	priv->common.stop = p54u_stop;
	if (recognized_pipes < P54U_PIPE_NUMBER) {
#ifdef CONFIG_PM
		/* ISL3887 needs a full reset on resume */
		udev->reset_resume = 1;
#endif /* CONFIG_PM */
		err = p54u_device_reset(dev);

		priv->hw_type = P54U_3887;
		dev->extra_tx_headroom += sizeof(struct lm87_tx_hdr);
		priv->common.tx_hdr_len = sizeof(struct lm87_tx_hdr);
		priv->common.tx = p54u_tx_lm87;
		priv->upload_fw = p54u_upload_firmware_3887;
	} else {
		priv->hw_type = P54U_NET2280;
		dev->extra_tx_headroom += sizeof(struct net2280_tx_hdr);
		priv->common.tx_hdr_len = sizeof(struct net2280_tx_hdr);
		priv->common.tx = p54u_tx_net2280;
		priv->upload_fw = p54u_upload_firmware_net2280;
	}
	err = p54u_load_firmware(dev, intf);
	if (err)
		p54_free_common(dev);
	return err;
}

static void p54u_disconnect(struct usb_interface *intf)
{
	struct ieee80211_hw *dev = usb_get_intfdata(intf);
	struct p54u_priv *priv;

	if (!dev)
		return;

	priv = dev->priv;
	wait_for_completion(&priv->fw_wait_load);
	p54_unregister_common(dev);

	release_firmware(priv->fw);
	p54_free_common(dev);
}

static int p54u_pre_reset(struct usb_interface *intf)
{
	struct ieee80211_hw *dev = usb_get_intfdata(intf);

	if (!dev)
		return -ENODEV;

	p54u_stop(dev);
	return 0;
}

static int p54u_resume(struct usb_interface *intf)
{
	struct ieee80211_hw *dev = usb_get_intfdata(intf);
	struct p54u_priv *priv;

	if (!dev)
		return -ENODEV;

	priv = dev->priv;
	if (unlikely(!(priv->upload_fw && priv->fw)))
		return 0;

	return priv->upload_fw(dev);
}

static int p54u_post_reset(struct usb_interface *intf)
{
	struct ieee80211_hw *dev = usb_get_intfdata(intf);
	struct p54u_priv *priv;
	int err;

	err = p54u_resume(intf);
	if (err)
		return err;

	/* reinitialize old device state */
	priv = dev->priv;
	if (priv->common.mode != NL80211_IFTYPE_UNSPECIFIED)
		ieee80211_restart_hw(dev);

	return 0;
}

#ifdef CONFIG_PM

static int p54u_suspend(struct usb_interface *intf, pm_message_t message)
{
	return p54u_pre_reset(intf);
}

#endif /* CONFIG_PM */

static struct usb_driver p54u_driver = {
	.name	= "p54usb",
	.id_table = p54u_table,
	.probe = p54u_probe,
	.disconnect = p54u_disconnect,
	.pre_reset = p54u_pre_reset,
	.post_reset = p54u_post_reset,
#ifdef CONFIG_PM
	.suspend = p54u_suspend,
	.resume = p54u_resume,
	.reset_resume = p54u_resume,
#endif /* CONFIG_PM */
	.soft_unbind = 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(p54u_driver);
