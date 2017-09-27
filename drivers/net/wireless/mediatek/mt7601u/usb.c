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

#include "mt7601u.h"
#include "usb.h"
#include "trace.h"

static const struct usb_device_id mt7601u_device_table[] = {
	{ USB_DEVICE(0x0b05, 0x17d3) },
	{ USB_DEVICE(0x0e8d, 0x760a) },
	{ USB_DEVICE(0x0e8d, 0x760b) },
	{ USB_DEVICE(0x13d3, 0x3431) },
	{ USB_DEVICE(0x13d3, 0x3434) },
	{ USB_DEVICE(0x148f, 0x7601) },
	{ USB_DEVICE(0x148f, 0x760a) },
	{ USB_DEVICE(0x148f, 0x760b) },
	{ USB_DEVICE(0x148f, 0x760c) },
	{ USB_DEVICE(0x148f, 0x760d) },
	{ USB_DEVICE(0x2001, 0x3d04) },
	{ USB_DEVICE(0x2717, 0x4106) },
	{ USB_DEVICE(0x2955, 0x0001) },
	{ USB_DEVICE(0x2955, 0x1001) },
	{ USB_DEVICE(0x2a5f, 0x1000) },
	{ USB_DEVICE(0x7392, 0x7710) },
	{ 0, }
};

bool mt7601u_usb_alloc_buf(struct mt7601u_dev *dev, size_t len,
			   struct mt7601u_dma_buf *buf)
{
	struct usb_device *usb_dev = mt7601u_to_usb_dev(dev);

	buf->len = len;
	buf->urb = usb_alloc_urb(0, GFP_KERNEL);
	buf->buf = usb_alloc_coherent(usb_dev, buf->len, GFP_KERNEL, &buf->dma);

	return !buf->urb || !buf->buf;
}

void mt7601u_usb_free_buf(struct mt7601u_dev *dev, struct mt7601u_dma_buf *buf)
{
	struct usb_device *usb_dev = mt7601u_to_usb_dev(dev);

	usb_free_coherent(usb_dev, buf->len, buf->buf, buf->dma);
	usb_free_urb(buf->urb);
}

int mt7601u_usb_submit_buf(struct mt7601u_dev *dev, int dir, int ep_idx,
			   struct mt7601u_dma_buf *buf, gfp_t gfp,
			   usb_complete_t complete_fn, void *context)
{
	struct usb_device *usb_dev = mt7601u_to_usb_dev(dev);
	unsigned pipe;
	int ret;

	if (dir == USB_DIR_IN)
		pipe = usb_rcvbulkpipe(usb_dev, dev->in_eps[ep_idx]);
	else
		pipe = usb_sndbulkpipe(usb_dev, dev->out_eps[ep_idx]);

	usb_fill_bulk_urb(buf->urb, usb_dev, pipe, buf->buf, buf->len,
			  complete_fn, context);
	buf->urb->transfer_dma = buf->dma;
	buf->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	trace_mt_submit_urb(dev, buf->urb);
	ret = usb_submit_urb(buf->urb, gfp);
	if (ret)
		dev_err(dev->dev, "Error: submit URB dir:%d ep:%d failed:%d\n",
			dir, ep_idx, ret);
	return ret;
}

void mt7601u_complete_urb(struct urb *urb)
{
	struct completion *cmpl = urb->context;

	complete(cmpl);
}

int mt7601u_vendor_request(struct mt7601u_dev *dev, const u8 req,
			   const u8 direction, const u16 val, const u16 offset,
			   void *buf, const size_t buflen)
{
	int i, ret;
	struct usb_device *usb_dev = mt7601u_to_usb_dev(dev);
	const u8 req_type = direction | USB_TYPE_VENDOR | USB_RECIP_DEVICE;
	const unsigned int pipe = (direction == USB_DIR_IN) ?
		usb_rcvctrlpipe(usb_dev, 0) : usb_sndctrlpipe(usb_dev, 0);

	for (i = 0; i < MT_VEND_REQ_MAX_RETRY; i++) {
		ret = usb_control_msg(usb_dev, pipe, req, req_type,
				      val, offset, buf, buflen,
				      MT_VEND_REQ_TOUT_MS);
		trace_mt_vend_req(dev, pipe, req, req_type, val, offset,
				  buf, buflen, ret);

		if (ret == -ENODEV)
			set_bit(MT7601U_STATE_REMOVED, &dev->state);
		if (ret >= 0 || ret == -ENODEV)
			return ret;

		msleep(5);
	}

	dev_err(dev->dev, "Vendor request req:%02x off:%04x failed:%d\n",
		req, offset, ret);

	return ret;
}

void mt7601u_vendor_reset(struct mt7601u_dev *dev)
{
	mt7601u_vendor_request(dev, MT_VEND_DEV_MODE, USB_DIR_OUT,
			       MT_VEND_DEV_MODE_RESET, 0, NULL, 0);
}

u32 mt7601u_rr(struct mt7601u_dev *dev, u32 offset)
{
	int ret;
	u32 val = ~0;

	WARN_ONCE(offset > USHRT_MAX, "read high off:%08x", offset);

	mutex_lock(&dev->vendor_req_mutex);

	ret = mt7601u_vendor_request(dev, MT_VEND_MULTI_READ, USB_DIR_IN,
				     0, offset, dev->vend_buf, MT_VEND_BUF);
	if (ret == MT_VEND_BUF)
		val = get_unaligned_le32(dev->vend_buf);
	else if (ret > 0)
		dev_err(dev->dev, "Error: wrong size read:%d off:%08x\n",
			ret, offset);

	mutex_unlock(&dev->vendor_req_mutex);

	trace_reg_read(dev, offset, val);
	return val;
}

int mt7601u_vendor_single_wr(struct mt7601u_dev *dev, const u8 req,
			     const u16 offset, const u32 val)
{
	int ret;

	mutex_lock(&dev->vendor_req_mutex);

	ret = mt7601u_vendor_request(dev, req, USB_DIR_OUT,
				     val & 0xffff, offset, NULL, 0);
	if (!ret)
		ret = mt7601u_vendor_request(dev, req, USB_DIR_OUT,
					     val >> 16, offset + 2, NULL, 0);

	mutex_unlock(&dev->vendor_req_mutex);

	return ret;
}

void mt7601u_wr(struct mt7601u_dev *dev, u32 offset, u32 val)
{
	WARN_ONCE(offset > USHRT_MAX, "write high off:%08x", offset);

	mt7601u_vendor_single_wr(dev, MT_VEND_WRITE, offset, val);
	trace_reg_write(dev, offset, val);
}

u32 mt7601u_rmw(struct mt7601u_dev *dev, u32 offset, u32 mask, u32 val)
{
	val |= mt7601u_rr(dev, offset) & ~mask;
	mt7601u_wr(dev, offset, val);
	return val;
}

u32 mt7601u_rmc(struct mt7601u_dev *dev, u32 offset, u32 mask, u32 val)
{
	u32 reg = mt7601u_rr(dev, offset);

	val |= reg & ~mask;
	if (reg != val)
		mt7601u_wr(dev, offset, val);
	return val;
}

void mt7601u_wr_copy(struct mt7601u_dev *dev, u32 offset,
		     const void *data, int len)
{
	WARN_ONCE(offset & 3, "unaligned write copy off:%08x", offset);
	WARN_ONCE(len & 3, "short write copy off:%08x", offset);

	mt7601u_burst_write_regs(dev, offset, data, len / 4);
}

void mt7601u_addr_wr(struct mt7601u_dev *dev, const u32 offset, const u8 *addr)
{
	mt7601u_wr(dev, offset, get_unaligned_le32(addr));
	mt7601u_wr(dev, offset + 4, addr[4] | addr[5] << 8);
}

static int mt7601u_assign_pipes(struct usb_interface *usb_intf,
				struct mt7601u_dev *dev)
{
	struct usb_endpoint_descriptor *ep_desc;
	struct usb_host_interface *intf_desc = usb_intf->cur_altsetting;
	unsigned i, ep_i = 0, ep_o = 0;

	BUILD_BUG_ON(sizeof(dev->in_eps) < __MT_EP_IN_MAX);
	BUILD_BUG_ON(sizeof(dev->out_eps) < __MT_EP_OUT_MAX);

	for (i = 0; i < intf_desc->desc.bNumEndpoints; i++) {
		ep_desc = &intf_desc->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(ep_desc) &&
		    ep_i++ < __MT_EP_IN_MAX) {
			dev->in_eps[ep_i - 1] = usb_endpoint_num(ep_desc);
			dev->in_max_packet = usb_endpoint_maxp(ep_desc);
			/* Note: this is ignored by usb sub-system but vendor
			 *	 code does it. We can drop this at some point.
			 */
			dev->in_eps[ep_i - 1] |= USB_DIR_IN;
		} else if (usb_endpoint_is_bulk_out(ep_desc) &&
			   ep_o++ < __MT_EP_OUT_MAX) {
			dev->out_eps[ep_o - 1] = usb_endpoint_num(ep_desc);
			dev->out_max_packet = usb_endpoint_maxp(ep_desc);
		}
	}

	if (ep_i != __MT_EP_IN_MAX || ep_o != __MT_EP_OUT_MAX) {
		dev_err(dev->dev, "Error: wrong pipe number in:%d out:%d\n",
			ep_i, ep_o);
		return -EINVAL;
	}

	return 0;
}

static int mt7601u_probe(struct usb_interface *usb_intf,
			 const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(usb_intf);
	struct mt7601u_dev *dev;
	u32 asic_rev, mac_rev;
	int ret;

	dev = mt7601u_alloc_device(&usb_intf->dev);
	if (!dev)
		return -ENOMEM;

	usb_dev = usb_get_dev(usb_dev);
	usb_reset_device(usb_dev);

	usb_set_intfdata(usb_intf, dev);

	dev->vend_buf = devm_kmalloc(dev->dev, MT_VEND_BUF, GFP_KERNEL);
	if (!dev->vend_buf) {
		ret = -ENOMEM;
		goto err;
	}

	ret = mt7601u_assign_pipes(usb_intf, dev);
	if (ret)
		goto err;
	ret = mt7601u_wait_asic_ready(dev);
	if (ret)
		goto err;

	asic_rev = mt7601u_rr(dev, MT_ASIC_VERSION);
	mac_rev = mt7601u_rr(dev, MT_MAC_CSR0);
	dev_info(dev->dev, "ASIC revision: %08x MAC revision: %08x\n",
		 asic_rev, mac_rev);

	/* Note: vendor driver skips this check for MT7601U */
	if (!(mt7601u_rr(dev, MT_EFUSE_CTRL) & MT_EFUSE_CTRL_SEL))
		dev_warn(dev->dev, "Warning: eFUSE not present\n");

	ret = mt7601u_init_hardware(dev);
	if (ret)
		goto err;
	ret = mt7601u_register_device(dev);
	if (ret)
		goto err_hw;

	set_bit(MT7601U_STATE_INITIALIZED, &dev->state);

	return 0;
err_hw:
	mt7601u_cleanup(dev);
err:
	usb_set_intfdata(usb_intf, NULL);
	usb_put_dev(interface_to_usbdev(usb_intf));

	destroy_workqueue(dev->stat_wq);
	ieee80211_free_hw(dev->hw);
	return ret;
}

static void mt7601u_disconnect(struct usb_interface *usb_intf)
{
	struct mt7601u_dev *dev = usb_get_intfdata(usb_intf);

	ieee80211_unregister_hw(dev->hw);
	mt7601u_cleanup(dev);

	usb_set_intfdata(usb_intf, NULL);
	usb_put_dev(interface_to_usbdev(usb_intf));

	destroy_workqueue(dev->stat_wq);
	ieee80211_free_hw(dev->hw);
}

static int mt7601u_suspend(struct usb_interface *usb_intf, pm_message_t state)
{
	struct mt7601u_dev *dev = usb_get_intfdata(usb_intf);

	mt7601u_cleanup(dev);

	return 0;
}

static int mt7601u_resume(struct usb_interface *usb_intf)
{
	struct mt7601u_dev *dev = usb_get_intfdata(usb_intf);
	int ret;

	ret = mt7601u_init_hardware(dev);
	if (ret)
		return ret;

	set_bit(MT7601U_STATE_INITIALIZED, &dev->state);

	return 0;
}

MODULE_DEVICE_TABLE(usb, mt7601u_device_table);
MODULE_FIRMWARE(MT7601U_FIRMWARE);
MODULE_LICENSE("GPL");

static struct usb_driver mt7601u_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt7601u_device_table,
	.probe		= mt7601u_probe,
	.disconnect	= mt7601u_disconnect,
	.suspend	= mt7601u_suspend,
	.resume		= mt7601u_resume,
	.reset_resume	= mt7601u_resume,
	.soft_unbind	= 1,
	.disable_hub_initiated_lpm = 1,
};
module_usb_driver(mt7601u_driver);
