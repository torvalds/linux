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

#ifndef __MT76X0U_USB_H
#define __MT76X0U_USB_H

#include "mt76x0.h"

#define MT7610_FIRMWARE	"mediatek/mt7610u.bin"

#define MT_VEND_REQ_MAX_RETRY	10
#define MT_VEND_REQ_TOUT_MS	300

#define MT_VEND_DEV_MODE_RESET	1

#define MT_VEND_BUF		sizeof(__le32)

static inline struct usb_device *mt76x0_to_usb_dev(struct mt76x0_dev *mt76x0)
{
	return interface_to_usbdev(to_usb_interface(mt76x0->mt76.dev));
}

static inline struct usb_device *mt76_to_usb_dev(struct mt76_dev *mt76)
{
	return interface_to_usbdev(to_usb_interface(mt76->dev));
}

static inline bool mt76x0_urb_has_error(struct urb *urb)
{
	return urb->status &&
		urb->status != -ENOENT &&
		urb->status != -ECONNRESET &&
		urb->status != -ESHUTDOWN;
}

bool mt76x0_usb_alloc_buf(struct mt76x0_dev *dev, size_t len,
			   struct mt76x0_dma_buf *buf);
void mt76x0_usb_free_buf(struct mt76x0_dev *dev, struct mt76x0_dma_buf *buf);
int mt76x0_usb_submit_buf(struct mt76x0_dev *dev, int dir, int ep_idx,
			   struct mt76x0_dma_buf *buf, gfp_t gfp,
			   usb_complete_t complete_fn, void *context);
void mt76x0_complete_urb(struct urb *urb);

int mt76x0_vendor_request(struct mt76x0_dev *dev, const u8 req,
			   const u8 direction, const u16 val, const u16 offset,
			   void *buf, const size_t buflen);
void mt76x0_vendor_reset(struct mt76x0_dev *dev);
int mt76x0_vendor_single_wr(struct mt76x0_dev *dev, const u8 req,
			     const u16 offset, const u32 val);

#endif
