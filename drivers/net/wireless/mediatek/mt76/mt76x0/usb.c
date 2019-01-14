/*
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "mt76x0.h"
#include "usb.h"
#include "trace.h"

static struct usb_device_id mt76x0_device_table[] = {
	{ USB_DEVICE(0x148F, 0x7610) },	/* MT7610U */
	{ USB_DEVICE(0x13B1, 0x003E) },	/* Linksys AE6000 */
	{ USB_DEVICE(0x0E8D, 0x7610) },	/* Sabrent NTWLAC */
	{ USB_DEVICE(0x7392, 0xa711) },	/* Edimax 7711mac */
	{ USB_DEVICE(0x7392, 0xb711) },	/* Edimax / Elecom  */
	{ USB_DEVICE(0x148f, 0x761a) },	/* TP-Link TL-WDN5200 */
	{ USB_DEVICE(0x148f, 0x760a) },	/* TP-Link unknown */
	{ USB_DEVICE(0x0b05, 0x17d1) },	/* Asus USB-AC51 */
	{ USB_DEVICE(0x0b05, 0x17db) },	/* Asus USB-AC50 */
	{ USB_DEVICE(0x0df6, 0x0075) },	/* Sitecom WLA-3100 */
	{ USB_DEVICE(0x2019, 0xab31) },	/* Planex GW-450D */
	{ USB_DEVICE(0x2001, 0x3d02) },	/* D-LINK DWA-171 rev B1 */
	{ USB_DEVICE(0x0586, 0x3425) },	/* Zyxel NWD6505 */
	{ USB_DEVICE(0x07b8, 0x7610) },	/* AboCom AU7212 */
	{ USB_DEVICE(0x04bb, 0x0951) },	/* I-O DATA WN-AC433UK */
	{ USB_DEVICE(0x057c, 0x8502) },	/* AVM FRITZ!WLAN USB Stick AC 430 */
	{ USB_DEVICE(0x293c, 0x5702) },	/* Comcast Xfinity KXW02AAA  */
	{ USB_DEVICE(0x20f4, 0x806b) },	/* TRENDnet TEW-806UBH  */
	{ USB_DEVICE(0x7392, 0xc711) }, /* Devolo Wifi ac Stick */
	{ USB_DEVICE(0x0df6, 0x0079) }, /* Sitecom Europe B.V. ac  Stick */
	{ USB_DEVICE(0x2357, 0x0105) }, /* TP-LINK Archer T1U */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0E8D, 0x7630, 0xff, 0x2, 0xff)}, /* MT7630U */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0E8D, 0x7650, 0xff, 0x2, 0xff)}, /* MT7650U */
	{ 0, }
};

bool mt76x0_usb_alloc_buf(struct mt76x0_dev *dev, size_t len,
			   struct mt76x0_dma_buf *buf)
{
	struct usb_device *usb_dev = mt76x0_to_usb_dev(dev);

	buf->len = len;
	buf->urb = usb_alloc_urb(0, GFP_KERNEL);
	buf->buf = usb_alloc_coherent(usb_dev, buf->len, GFP_KERNEL, &buf->dma);

	return !buf->urb || !buf->buf;
}

void mt76x0_usb_free_buf(struct mt76x0_dev *dev, struct mt76x0_dma_buf *buf)
{
	struct usb_device *usb_dev = mt76x0_to_usb_dev(dev);

	usb_free_coherent(usb_dev, buf->len, buf->buf, buf->dma);
	usb_free_urb(buf->urb);
}

int mt76x0_usb_submit_buf(struct mt76x0_dev *dev, int dir, int ep_idx,
			   struct mt76x0_dma_buf *buf, gfp_t gfp,
			   usb_complete_t complete_fn, void *context)
{
	struct usb_device *usb_dev = mt76x0_to_usb_dev(dev);
	unsigned pipe;
	int ret;

	if (dir == USB_DIR_IN)
		pipe = usb_rcvbulkpipe(usb_dev, dev->in_ep[ep_idx]);
	else
		pipe = usb_sndbulkpipe(usb_dev, dev->out_ep[ep_idx]);

	usb_fill_bulk_urb(buf->urb, usb_dev, pipe, buf->buf, buf->len,
			  complete_fn, context);
	buf->urb->transfer_dma = buf->dma;
	buf->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	trace_mt76x0_submit_urb(&dev->mt76, buf->urb);
	ret = usb_submit_urb(buf->urb, gfp);
	if (ret)
		dev_err(dev->mt76.dev, "Error: submit URB dir:%d ep:%d failed:%d\n",
			dir, ep_idx, ret);
	return ret;
}

void mt76x0_complete_urb(struct urb *urb)
{
	struct completion *cmpl = urb->context;

	complete(cmpl);
}

int mt76x0_vendor_request(struct mt76x0_dev *dev, const u8 req,
			   const u8 direction, const u16 val, const u16 offset,
			   void *buf, const size_t buflen)
{
	int i, ret;
	struct usb_device *usb_dev = mt76x0_to_usb_dev(dev);
	const u8 req_type = direction | USB_TYPE_VENDOR | USB_RECIP_DEVICE;
	const unsigned int pipe = (direction == USB_DIR_IN) ?
		usb_rcvctrlpipe(usb_dev, 0) : usb_sndctrlpipe(usb_dev, 0);

	for (i = 0; i < MT_VEND_REQ_MAX_RETRY; i++) {
		ret = usb_control_msg(usb_dev, pipe, req, req_type,
				      val, offset, buf, buflen,
				      MT_VEND_REQ_TOUT_MS);
		trace_mt76x0_vend_req(&dev->mt76, pipe, req, req_type, val, offset,
				  buf, buflen, ret);

		if (ret == -ENODEV)
			set_bit(MT76_REMOVED, &dev->mt76.state);
		if (ret >= 0 || ret == -ENODEV)
			return ret;

		msleep(5);
	}

	dev_err(dev->mt76.dev, "Vendor request req:%02x off:%04x failed:%d\n",
		req, offset, ret);

	return ret;
}

void mt76x0_vendor_reset(struct mt76x0_dev *dev)
{
	mt76x0_vendor_request(dev, MT_VEND_DEV_MODE, USB_DIR_OUT,
			      MT_VEND_DEV_MODE_RESET, 0, NULL, 0);
}

static u32 mt76x0_rr(struct mt76_dev *dev, u32 offset)
{
	struct mt76x0_dev *mdev = (struct mt76x0_dev *) dev;
	int ret;
	u32 val = ~0;

	WARN_ONCE(offset > USHRT_MAX, "read high off:%08x", offset);

	mutex_lock(&mdev->usb_ctrl_mtx);

	ret = mt76x0_vendor_request((struct mt76x0_dev *)dev, MT_VEND_MULTI_READ, USB_DIR_IN,
				    0, offset, mdev->data, MT_VEND_BUF);
	if (ret == MT_VEND_BUF)
		val = get_unaligned_le32(mdev->data);
	else if (ret > 0)
		dev_err(dev->dev, "Error: wrong size read:%d off:%08x\n",
			ret, offset);

	mutex_unlock(&mdev->usb_ctrl_mtx);

	trace_mt76x0_reg_read(dev, offset, val);
	return val;
}

int mt76x0_vendor_single_wr(struct mt76x0_dev *dev, const u8 req,
			     const u16 offset, const u32 val)
{
	struct mt76x0_dev *mdev = dev;
	int ret;

	mutex_lock(&mdev->usb_ctrl_mtx);

	ret = mt76x0_vendor_request(dev, req, USB_DIR_OUT,
				    val & 0xffff, offset, NULL, 0);
	if (!ret)
		ret = mt76x0_vendor_request(dev, req, USB_DIR_OUT,
					    val >> 16, offset + 2, NULL, 0);

	mutex_unlock(&mdev->usb_ctrl_mtx);

	return ret;
}

static void mt76x0_wr(struct mt76_dev *dev, u32 offset, u32 val)
{
	struct mt76x0_dev *mdev = (struct mt76x0_dev *) dev;
	int ret;

	WARN_ONCE(offset > USHRT_MAX, "write high off:%08x", offset);

	mutex_lock(&mdev->usb_ctrl_mtx);

	put_unaligned_le32(val, mdev->data);
	ret = mt76x0_vendor_request(mdev, MT_VEND_MULTI_WRITE, USB_DIR_OUT,
				    0, offset, mdev->data, MT_VEND_BUF);
	trace_mt76x0_reg_write(dev, offset, val);

	mutex_unlock(&mdev->usb_ctrl_mtx);
}

static u32 mt76x0_rmw(struct mt76_dev *dev, u32 offset, u32 mask, u32 val)
{
	val |= mt76x0_rr(dev, offset) & ~mask;
	mt76x0_wr(dev, offset, val);
	return val;
}

static void mt76x0_wr_copy(struct mt76_dev *dev, u32 offset,
			   const void *data, int len)
{
	WARN_ONCE(offset & 3, "unaligned write copy off:%08x", offset);
	WARN_ONCE(len & 3, "short write copy off:%08x", offset);

	mt76x0_burst_write_regs((struct mt76x0_dev *) dev, offset, data, len / 4);
}

void mt76x0_addr_wr(struct mt76x0_dev *dev, const u32 offset, const u8 *addr)
{
	mt76_wr(dev, offset, get_unaligned_le32(addr));
	mt76_wr(dev, offset + 4, addr[4] | addr[5] << 8);
}

static int mt76x0_assign_pipes(struct usb_interface *usb_intf,
				struct mt76x0_dev *dev)
{
	struct usb_endpoint_descriptor *ep_desc;
	struct usb_host_interface *intf_desc = usb_intf->cur_altsetting;
	unsigned i, ep_i = 0, ep_o = 0;

	BUILD_BUG_ON(sizeof(dev->in_ep) < __MT_EP_IN_MAX);
	BUILD_BUG_ON(sizeof(dev->out_ep) < __MT_EP_OUT_MAX);

	for (i = 0; i < intf_desc->desc.bNumEndpoints; i++) {
		ep_desc = &intf_desc->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(ep_desc) &&
		    ep_i++ < __MT_EP_IN_MAX) {
			dev->in_ep[ep_i - 1] = usb_endpoint_num(ep_desc);
			dev->in_max_packet = usb_endpoint_maxp(ep_desc);
			/* Note: this is ignored by usb sub-system but vendor
			 *	 code does it. We can drop this at some point.
			 */
			dev->in_ep[ep_i - 1] |= USB_DIR_IN;
		} else if (usb_endpoint_is_bulk_out(ep_desc) &&
			   ep_o++ < __MT_EP_OUT_MAX) {
			dev->out_ep[ep_o - 1] = usb_endpoint_num(ep_desc);
			dev->out_max_packet = usb_endpoint_maxp(ep_desc);
		}
	}

	if (ep_i != __MT_EP_IN_MAX || ep_o != __MT_EP_OUT_MAX) {
		dev_err(dev->mt76.dev, "Error: wrong pipe number in:%d out:%d\n",
			ep_i, ep_o);
		return -EINVAL;
	}

	return 0;
}

static int mt76x0_probe(struct usb_interface *usb_intf,
			 const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(usb_intf);
	struct mt76x0_dev *dev;
	u32 asic_rev, mac_rev;
	int ret;
	static const struct mt76_bus_ops usb_ops = {
		.rr = mt76x0_rr,
		.wr = mt76x0_wr,
		.rmw = mt76x0_rmw,
		.copy = mt76x0_wr_copy,
	};

	dev = mt76x0_alloc_device(&usb_intf->dev);
	if (!dev)
		return -ENOMEM;

	usb_dev = usb_get_dev(usb_dev);
	usb_reset_device(usb_dev);

	usb_set_intfdata(usb_intf, dev);

	dev->mt76.bus = &usb_ops;

	ret = mt76x0_assign_pipes(usb_intf, dev);
	if (ret)
		goto err;

	/* Disable the HW, otherwise MCU fail to initalize on hot reboot */
	mt76x0_chip_onoff(dev, false, false);

	ret = mt76x0_wait_asic_ready(dev);
	if (ret)
		goto err;

	asic_rev = mt76_rr(dev, MT_ASIC_VERSION);
	mac_rev = mt76_rr(dev, MT_MAC_CSR0);
	dev_info(dev->mt76.dev, "ASIC revision: %08x MAC revision: %08x\n",
		 asic_rev, mac_rev);

	/* Note: vendor driver skips this check for MT76X0U */
	if (!(mt76_rr(dev, MT_EFUSE_CTRL) & MT_EFUSE_CTRL_SEL))
		dev_warn(dev->mt76.dev, "Warning: eFUSE not present\n");

	ret = mt76x0_init_hardware(dev);
	if (ret)
		goto err;

	ret = mt76x0_register_device(dev);
	if (ret)
		goto err_hw;

	set_bit(MT76_STATE_INITIALIZED, &dev->mt76.state);

	return 0;
err_hw:
	mt76x0_cleanup(dev);
err:
	usb_set_intfdata(usb_intf, NULL);
	usb_put_dev(interface_to_usbdev(usb_intf));

	destroy_workqueue(dev->stat_wq);
	ieee80211_free_hw(dev->mt76.hw);
	return ret;
}

static void mt76x0_disconnect(struct usb_interface *usb_intf)
{
	struct mt76x0_dev *dev = usb_get_intfdata(usb_intf);
	bool initalized = test_bit(MT76_STATE_INITIALIZED, &dev->mt76.state);

	if (!initalized)
		return;

	ieee80211_unregister_hw(dev->mt76.hw);
	mt76x0_cleanup(dev);

	usb_set_intfdata(usb_intf, NULL);
	usb_put_dev(interface_to_usbdev(usb_intf));

	destroy_workqueue(dev->stat_wq);
	ieee80211_free_hw(dev->mt76.hw);
}

static int mt76x0_suspend(struct usb_interface *usb_intf, pm_message_t state)
{
	struct mt76x0_dev *dev = usb_get_intfdata(usb_intf);

	mt76x0_cleanup(dev);

	return 0;
}

static int mt76x0_resume(struct usb_interface *usb_intf)
{
	struct mt76x0_dev *dev = usb_get_intfdata(usb_intf);
	int ret;

	ret = mt76x0_init_hardware(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_INITIALIZED, &dev->mt76.state);

	return 0;
}

MODULE_DEVICE_TABLE(usb, mt76x0_device_table);
MODULE_FIRMWARE(MT7610_FIRMWARE);
MODULE_LICENSE("GPL");

static struct usb_driver mt76x0_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt76x0_device_table,
	.probe		= mt76x0_probe,
	.disconnect	= mt76x0_disconnect,
	.suspend	= mt76x0_suspend,
	.resume		= mt76x0_resume,
	.reset_resume	= mt76x0_resume,
	.soft_unbind	= 1,
	.disable_hub_initiated_lpm = 1,
};
module_usb_driver(mt76x0_driver);
