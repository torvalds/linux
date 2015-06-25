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

#ifndef __MT7601U_USB_H
#define __MT7601U_USB_H

#include "mt7601u.h"

#define MT7601U_FIRMWARE	"mt7601u.bin"

#define MT_VEND_REQ_MAX_RETRY	10
#define MT_VEND_REQ_TOUT_MS	300

#define MT_VEND_DEV_MODE_RESET	1

enum mt_vendor_req {
	MT_VEND_DEV_MODE = 1,
	MT_VEND_WRITE = 2,
	MT_VEND_MULTI_READ = 7,
	MT_VEND_WRITE_FCE = 0x42,
};

enum mt_usb_ep_in {
	MT_EP_IN_PKT_RX,
	MT_EP_IN_CMD_RESP,
	__MT_EP_IN_MAX,
};

enum mt_usb_ep_out {
	MT_EP_OUT_INBAND_CMD,
	MT_EP_OUT_AC_BK,
	MT_EP_OUT_AC_BE,
	MT_EP_OUT_AC_VI,
	MT_EP_OUT_AC_VO,
	MT_EP_OUT_HCCA,
	__MT_EP_OUT_MAX,
};

static inline struct usb_device *mt7601u_to_usb_dev(struct mt7601u_dev *mt7601u)
{
	return interface_to_usbdev(to_usb_interface(mt7601u->dev));
}

static inline bool mt7601u_urb_has_error(struct urb *urb)
{
	return urb->status &&
		urb->status != -ENOENT &&
		urb->status != -ECONNRESET &&
		urb->status != -ESHUTDOWN;
}

bool mt7601u_usb_alloc_buf(struct mt7601u_dev *dev, size_t len,
			   struct mt7601u_dma_buf *buf);
void mt7601u_usb_free_buf(struct mt7601u_dev *dev, struct mt7601u_dma_buf *buf);
int mt7601u_usb_submit_buf(struct mt7601u_dev *dev, int dir, int ep_idx,
			   struct mt7601u_dma_buf *buf, gfp_t gfp,
			   usb_complete_t complete_fn, void *context);
void mt7601u_complete_urb(struct urb *urb);

int mt7601u_vendor_request(struct mt7601u_dev *dev, const u8 req,
			   const u8 direction, const u16 val, const u16 offset,
			   void *buf, const size_t buflen);
void mt7601u_vendor_reset(struct mt7601u_dev *dev);
int mt7601u_vendor_single_wr(struct mt7601u_dev *dev, const u8 req,
			     const u16 offset, const u32 val);

#endif
