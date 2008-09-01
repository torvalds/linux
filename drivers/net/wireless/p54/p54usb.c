
/*
 * Linux device driver for USB based Prism54
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 *
 * Based on the islsm (softmac prism54) driver, which is:
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/usb.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <net/mac80211.h>

#include "p54.h"
#include "p54usb.h"

MODULE_AUTHOR("Michael Wu <flamingice@sourmilk.net>");
MODULE_DESCRIPTION("Prism54 USB wireless driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("prism54usb");

static struct usb_device_id p54u_table[] __devinitdata = {
	/* Version 1 devices (pci chip + net2280) */
	{USB_DEVICE(0x0506, 0x0a11)},	/* 3COM 3CRWE254G72 */
	{USB_DEVICE(0x0707, 0xee06)},	/* SMC 2862W-G */
	{USB_DEVICE(0x083a, 0x4501)},	/* Accton 802.11g WN4501 USB */
	{USB_DEVICE(0x083a, 0x4502)},	/* Siemens Gigaset USB Adapter */
	{USB_DEVICE(0x083a, 0x5501)},	/* Phillips CPWUA054 */
	{USB_DEVICE(0x0846, 0x4200)},	/* Netgear WG121 */
	{USB_DEVICE(0x0846, 0x4210)},	/* Netgear WG121 the second ? */
	{USB_DEVICE(0x0846, 0x4220)},	/* Netgear WG111 */
	{USB_DEVICE(0x0cde, 0x0006)},	/* Medion 40900, Roper Europe */
	{USB_DEVICE(0x124a, 0x4023)},	/* Shuttle PN15, Airvast WM168g, IOGear GWU513 */
	{USB_DEVICE(0x1915, 0x2234)},	/* Linksys WUSB54G OEM */
	{USB_DEVICE(0x1915, 0x2235)},	/* Linksys WUSB54G Portable OEM */
	{USB_DEVICE(0x2001, 0x3701)},	/* DLink DWL-G120 Spinnaker */
	{USB_DEVICE(0x2001, 0x3703)},	/* DLink DWL-G122 */
	{USB_DEVICE(0x5041, 0x2234)},	/* Linksys WUSB54G */
	{USB_DEVICE(0x5041, 0x2235)},	/* Linksys WUSB54G Portable */

	/* Version 2 devices (3887) */
	{USB_DEVICE(0x0471, 0x1230)},   /* Philips CPWUA054/00 */
	{USB_DEVICE(0x050d, 0x7050)},	/* Belkin F5D7050 ver 1000 */
	{USB_DEVICE(0x0572, 0x2000)},	/* Cohiba Proto board */
	{USB_DEVICE(0x0572, 0x2002)},	/* Cohiba Proto board */
	{USB_DEVICE(0x0707, 0xee13)},   /* SMC 2862W-G version 2 */
	{USB_DEVICE(0x083a, 0x4521)},   /* Siemens Gigaset USB Adapter 54 version 2 */
	{USB_DEVICE(0x0846, 0x4240)},	/* Netgear WG111 (v2) */
	{USB_DEVICE(0x0915, 0x2000)},	/* Cohiba Proto board */
	{USB_DEVICE(0x0915, 0x2002)},	/* Cohiba Proto board */
	{USB_DEVICE(0x0baf, 0x0118)},   /* U.S. Robotics U5 802.11g Adapter*/
	{USB_DEVICE(0x0bf8, 0x1009)},   /* FUJITSU E-5400 USB D1700*/
	{USB_DEVICE(0x0cde, 0x0006)},   /* Medion MD40900 */
	{USB_DEVICE(0x0cde, 0x0008)},	/* Sagem XG703A */
	{USB_DEVICE(0x0d8e, 0x3762)},	/* DLink DWL-G120 Cohiba */
	{USB_DEVICE(0x09aa, 0x1000)},	/* Spinnaker Proto board */
	{USB_DEVICE(0x124a, 0x4025)},	/* IOGear GWU513 (GW3887IK chip) */
	{USB_DEVICE(0x13b1, 0x000a)},	/* Linksys WUSB54G ver 2 */
	{USB_DEVICE(0x13B1, 0x000C)},	/* Linksys WUSB54AG */
	{USB_DEVICE(0x1435, 0x0427)},	/* Inventel UR054G */
	{USB_DEVICE(0x2001, 0x3704)},	/* DLink DWL-G122 rev A2 */
	{USB_DEVICE(0x413c, 0x8102)},	/* Spinnaker DUT */
	{USB_DEVICE(0x413c, 0x8104)},	/* Cohiba Proto board */
	{}
};

MODULE_DEVICE_TABLE(usb, p54u_table);

static void p54u_rx_cb(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *) urb->context;
	struct p54u_rx_info *info = (struct p54u_rx_info *)skb->cb;
	struct ieee80211_hw *dev = info->dev;
	struct p54u_priv *priv = dev->priv;

	if (unlikely(urb->status)) {
		info->urb = NULL;
		usb_free_urb(urb);
		return;
	}

	skb_unlink(skb, &priv->rx_queue);
	skb_put(skb, urb->actual_length);
	if (!priv->hw_type)
		skb_pull(skb, sizeof(struct net2280_tx_hdr));

	if (p54_rx(dev, skb)) {
		skb = dev_alloc_skb(priv->common.rx_mtu + 32);
		if (unlikely(!skb)) {
			usb_free_urb(urb);
			/* TODO check rx queue length and refill *somewhere* */
			return;
		}

		info = (struct p54u_rx_info *) skb->cb;
		info->urb = urb;
		info->dev = dev;
		urb->transfer_buffer = skb_tail_pointer(skb);
		urb->context = skb;
		skb_queue_tail(&priv->rx_queue, skb);
	} else {
		if (!priv->hw_type)
			skb_push(skb, sizeof(struct net2280_tx_hdr));

		skb_reset_tail_pointer(skb);
		skb_trim(skb, 0);
		if (urb->transfer_buffer != skb_tail_pointer(skb)) {
			/* this should not happen */
			WARN_ON(1);
			urb->transfer_buffer = skb_tail_pointer(skb);
		}

		skb_queue_tail(&priv->rx_queue, skb);
	}

	usb_submit_urb(urb, GFP_ATOMIC);
}

static void p54u_tx_cb(struct urb *urb)
{
	usb_free_urb(urb);
}

static void p54u_tx_free_cb(struct urb *urb)
{
	kfree(urb->transfer_buffer);
	usb_free_urb(urb);
}

static int p54u_init_urbs(struct ieee80211_hw *dev)
{
	struct p54u_priv *priv = dev->priv;
	struct urb *entry;
	struct sk_buff *skb;
	struct p54u_rx_info *info;

	while (skb_queue_len(&priv->rx_queue) < 32) {
		skb = __dev_alloc_skb(priv->common.rx_mtu + 32, GFP_KERNEL);
		if (!skb)
			break;
		entry = usb_alloc_urb(0, GFP_KERNEL);
		if (!entry) {
			kfree_skb(skb);
			break;
		}
		usb_fill_bulk_urb(entry, priv->udev,
				  usb_rcvbulkpipe(priv->udev, P54U_PIPE_DATA),
				  skb_tail_pointer(skb),
				  priv->common.rx_mtu + 32, p54u_rx_cb, skb);
		info = (struct p54u_rx_info *) skb->cb;
		info->urb = entry;
		info->dev = dev;
		skb_queue_tail(&priv->rx_queue, skb);
		usb_submit_urb(entry, GFP_KERNEL);
	}

	return 0;
}

static void p54u_free_urbs(struct ieee80211_hw *dev)
{
	struct p54u_priv *priv = dev->priv;
	struct p54u_rx_info *info;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&priv->rx_queue))) {
		info = (struct p54u_rx_info *) skb->cb;
		if (!info->urb)
			continue;

		usb_kill_urb(info->urb);
		kfree_skb(skb);
	}
}

static void p54u_tx_3887(struct ieee80211_hw *dev, struct p54_control_hdr *data,
			 size_t len, int free_on_tx)
{
	struct p54u_priv *priv = dev->priv;
	struct urb *addr_urb, *data_urb;

	addr_urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!addr_urb)
		return;

	data_urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!data_urb) {
		usb_free_urb(addr_urb);
		return;
	}

	usb_fill_bulk_urb(addr_urb, priv->udev,
		usb_sndbulkpipe(priv->udev, P54U_PIPE_DATA), &data->req_id,
		sizeof(data->req_id), p54u_tx_cb, dev);
	usb_fill_bulk_urb(data_urb, priv->udev,
		usb_sndbulkpipe(priv->udev, P54U_PIPE_DATA), data, len,
		free_on_tx ? p54u_tx_free_cb : p54u_tx_cb, dev);

	usb_submit_urb(addr_urb, GFP_ATOMIC);
	usb_submit_urb(data_urb, GFP_ATOMIC);
}

static void p54u_tx_net2280(struct ieee80211_hw *dev, struct p54_control_hdr *data,
			    size_t len, int free_on_tx)
{
	struct p54u_priv *priv = dev->priv;
	struct urb *int_urb, *data_urb;
	struct net2280_tx_hdr *hdr;
	struct net2280_reg_write *reg;

	reg = kmalloc(sizeof(*reg), GFP_ATOMIC);
	if (!reg)
		return;

	int_urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!int_urb) {
		kfree(reg);
		return;
	}

	data_urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!data_urb) {
		kfree(reg);
		usb_free_urb(int_urb);
		return;
	}

	reg->port = cpu_to_le16(NET2280_DEV_U32);
	reg->addr = cpu_to_le32(P54U_DEV_BASE);
	reg->val = cpu_to_le32(ISL38XX_DEV_INT_DATA);

	len += sizeof(*data);
	hdr = (void *)data - sizeof(*hdr);
	memset(hdr, 0, sizeof(*hdr));
	hdr->device_addr = data->req_id;
	hdr->len = cpu_to_le16(len);

	usb_fill_bulk_urb(int_urb, priv->udev,
		usb_sndbulkpipe(priv->udev, P54U_PIPE_DEV), reg, sizeof(*reg),
		p54u_tx_free_cb, dev);
	usb_submit_urb(int_urb, GFP_ATOMIC);

	usb_fill_bulk_urb(data_urb, priv->udev,
		usb_sndbulkpipe(priv->udev, P54U_PIPE_DATA), hdr, len + sizeof(*hdr),
		free_on_tx ? p54u_tx_free_cb : p54u_tx_cb, dev);
	usb_submit_urb(data_urb, GFP_ATOMIC);
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

static int p54u_upload_firmware_3887(struct ieee80211_hw *dev)
{
	static char start_string[] = "~~~~<\r";
	struct p54u_priv *priv = dev->priv;
	const struct firmware *fw_entry = NULL;
	int err, alen;
	u8 carry = 0;
	u8 *buf, *tmp;
	const u8 *data;
	unsigned int left, remains, block_size;
	struct x2_header *hdr;
	unsigned long timeout;

	tmp = buf = kmalloc(P54U_FW_BLOCK, GFP_KERNEL);
	if (!buf) {
		printk(KERN_ERR "p54usb: cannot allocate firmware upload buffer!\n");
		err = -ENOMEM;
		goto err_bufalloc;
	}

	memcpy(buf, start_string, 4);
	err = p54u_bulk_msg(priv, P54U_PIPE_DATA, buf, 4);
	if (err) {
		printk(KERN_ERR "p54usb: reset failed! (%d)\n", err);
		goto err_reset;
	}

	err = request_firmware(&fw_entry, "isl3887usb_bare", &priv->udev->dev);
	if (err) {
		printk(KERN_ERR "p54usb: cannot find firmware (isl3887usb_bare)!\n");
		goto err_req_fw_failed;
	}

	err = p54_parse_firmware(dev, fw_entry);
	if (err)
		goto err_upload_failed;

	left = block_size = min((size_t)P54U_FW_BLOCK, fw_entry->size);
	strcpy(buf, start_string);
	left -= strlen(start_string);
	tmp += strlen(start_string);

	data = fw_entry->data;
	remains = fw_entry->size;

	hdr = (struct x2_header *)(buf + strlen(start_string));
	memcpy(hdr->signature, X2_SIGNATURE, X2_SIGNATURE_SIZE);
	hdr->fw_load_addr = cpu_to_le32(ISL38XX_DEV_FIRMWARE_ADDR);
	hdr->fw_length = cpu_to_le32(fw_entry->size);
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
			printk(KERN_ERR "p54usb: firmware upload failed!\n");
			goto err_upload_failed;
		}

		tmp = buf;
		left = block_size = min((unsigned int)P54U_FW_BLOCK, remains);
	}

	*((__le32 *)buf) = cpu_to_le32(~crc32_le(~0, fw_entry->data, fw_entry->size));
	err = p54u_bulk_msg(priv, P54U_PIPE_DATA, buf, sizeof(u32));
	if (err) {
		printk(KERN_ERR "p54usb: firmware upload failed!\n");
		goto err_upload_failed;
	}

	timeout = jiffies + msecs_to_jiffies(1000);
	while (!(err = usb_bulk_msg(priv->udev,
		usb_rcvbulkpipe(priv->udev, P54U_PIPE_DATA), buf, 128, &alen, 1000))) {
		if (alen > 2 && !memcmp(buf, "OK", 2))
			break;

		if (alen > 5 && !memcmp(buf, "ERROR", 5)) {
			printk(KERN_INFO "p54usb: firmware upload failed!\n");
			err = -EINVAL;
			break;
		}

		if (time_after(jiffies, timeout)) {
			printk(KERN_ERR "p54usb: firmware boot timed out!\n");
			err = -ETIMEDOUT;
			break;
		}
	}
	if (err)
		goto err_upload_failed;

	buf[0] = 'g';
	buf[1] = '\r';
	err = p54u_bulk_msg(priv, P54U_PIPE_DATA, buf, 2);
	if (err) {
		printk(KERN_ERR "p54usb: firmware boot failed!\n");
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
	release_firmware(fw_entry);
  err_req_fw_failed:
  err_reset:
	kfree(buf);
  err_bufalloc:
	return err;
}

static int p54u_upload_firmware_net2280(struct ieee80211_hw *dev)
{
	struct p54u_priv *priv = dev->priv;
	const struct firmware *fw_entry = NULL;
	const struct p54p_csr *devreg = (const struct p54p_csr *) P54U_DEV_BASE;
	int err, alen;
	void *buf;
	__le32 reg;
	unsigned int remains, offset;
	const u8 *data;

	buf = kmalloc(512, GFP_KERNEL);
	if (!buf) {
		printk(KERN_ERR "p54usb: firmware buffer alloc failed!\n");
		return -ENOMEM;
	}

	err = request_firmware(&fw_entry, "isl3890usb", &priv->udev->dev);
	if (err) {
		printk(KERN_ERR "p54usb: cannot find firmware (isl3890usb)!\n");
		kfree(buf);
		return err;
	}

	err = p54_parse_firmware(dev, fw_entry);
	if (err) {
		kfree(buf);
		release_firmware(fw_entry);
		return err;
	}

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
	remains = fw_entry->size;
	data = fw_entry->data;
	offset = ISL38XX_DEV_FIRMWARE_ADDR;

	while (remains) {
		unsigned int block_len = min(remains, (unsigned int)512);
		memcpy(buf, data, block_len);

		err = p54u_bulk_msg(priv, P54U_PIPE_DATA, buf, block_len);
		if (err) {
			printk(KERN_ERR "p54usb: firmware block upload "
			       "failed\n");
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
			printk(KERN_ERR "p54usb: firmware DMA transfer "
			       "failed\n");
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
	release_firmware(fw_entry);
	kfree(buf);
	return err;
}

static int p54u_open(struct ieee80211_hw *dev)
{
	struct p54u_priv *priv = dev->priv;
	int err;

	err = p54u_init_urbs(dev);
	if (err) {
		return err;
	}

	priv->common.open = p54u_init_urbs;

	return 0;
}

static void p54u_stop(struct ieee80211_hw *dev)
{
	/* TODO: figure out how to reliably stop the 3887 and net2280 so
	   the hardware is still usable next time we want to start it.
	   until then, we just stop listening to the hardware.. */
	p54u_free_urbs(dev);
	return;
}

static int __devinit p54u_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct ieee80211_hw *dev;
	struct p54u_priv *priv;
	int err;
	unsigned int i, recognized_pipes;
	DECLARE_MAC_BUF(mac);

	dev = p54_init_common(sizeof(*priv));
	if (!dev) {
		printk(KERN_ERR "p54usb: ieee80211 alloc failed\n");
		return -ENOMEM;
	}

	priv = dev->priv;

	SET_IEEE80211_DEV(dev, &intf->dev);
	usb_set_intfdata(intf, dev);
	priv->udev = udev;

	usb_get_dev(udev);

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

	if (recognized_pipes < P54U_PIPE_NUMBER) {
		priv->hw_type = P54U_3887;
		priv->common.tx = p54u_tx_3887;
	} else {
		dev->extra_tx_headroom += sizeof(struct net2280_tx_hdr);
		priv->common.tx_hdr_len = sizeof(struct net2280_tx_hdr);
		priv->common.tx = p54u_tx_net2280;
	}
	priv->common.stop = p54u_stop;

	if (priv->hw_type)
		err = p54u_upload_firmware_3887(dev);
	else
		err = p54u_upload_firmware_net2280(dev);
	if (err)
		goto err_free_dev;

	skb_queue_head_init(&priv->rx_queue);

	p54u_open(dev);
	err = p54_read_eeprom(dev);
	p54u_stop(dev);
	if (err)
		goto err_free_dev;

	err = ieee80211_register_hw(dev);
	if (err) {
		printk(KERN_ERR "p54usb: Cannot register netdevice\n");
		goto err_free_dev;
	}

	return 0;

 err_free_dev:
	ieee80211_free_hw(dev);
	usb_set_intfdata(intf, NULL);
	usb_put_dev(udev);
	return err;
}

static void __devexit p54u_disconnect(struct usb_interface *intf)
{
	struct ieee80211_hw *dev = usb_get_intfdata(intf);
	struct p54u_priv *priv;

	if (!dev)
		return;

	ieee80211_unregister_hw(dev);

	priv = dev->priv;
	usb_put_dev(interface_to_usbdev(intf));
	p54_free_common(dev);
	ieee80211_free_hw(dev);
}

static struct usb_driver p54u_driver = {
	.name	= "p54usb",
	.id_table = p54u_table,
	.probe = p54u_probe,
	.disconnect = p54u_disconnect,
};

static int __init p54u_init(void)
{
	return usb_register(&p54u_driver);
}

static void __exit p54u_exit(void)
{
	usb_deregister(&p54u_driver);
}

module_init(p54u_init);
module_exit(p54u_exit);
