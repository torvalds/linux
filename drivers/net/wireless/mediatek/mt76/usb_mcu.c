/*
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "mt76.h"

void mt76u_mcu_complete_urb(struct urb *urb)
{
	struct completion *cmpl = urb->context;

	complete(cmpl);
}
EXPORT_SYMBOL_GPL(mt76u_mcu_complete_urb);

int mt76u_mcu_init_rx(struct mt76_dev *dev)
{
	struct mt76_usb *usb = &dev->usb;
	int err;

	err = mt76u_buf_alloc(dev, &usb->mcu.res, MCU_RESP_URB_SIZE,
			      MCU_RESP_URB_SIZE, GFP_KERNEL);
	if (err < 0)
		return err;

	err = mt76u_submit_buf(dev, USB_DIR_IN, MT_EP_IN_CMD_RESP,
			       &usb->mcu.res, GFP_KERNEL,
			       mt76u_mcu_complete_urb,
			       &usb->mcu.cmpl);
	if (err < 0)
		mt76u_buf_free(&usb->mcu.res);

	return err;
}
EXPORT_SYMBOL_GPL(mt76u_mcu_init_rx);

void mt76u_mcu_deinit(struct mt76_dev *dev)
{
	struct mt76u_buf *buf = &dev->usb.mcu.res;

	if (buf->urb) {
		usb_kill_urb(buf->urb);
		mt76u_buf_free(buf);
	}
}
EXPORT_SYMBOL_GPL(mt76u_mcu_deinit);
