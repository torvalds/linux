/**
 * Copyright (c) 2014 Redpine Signals Inc.
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
 *
 */

#include <linux/firmware.h>
#include "rsi_usb.h"

/**
 * rsi_usb_rx_thread() - This is a kernel thread to receive the packets from
 *			 the USB device.
 * @common: Pointer to the driver private structure.
 *
 * Return: None.
 */
void rsi_usb_rx_thread(struct rsi_common *common)
{
	struct rsi_hw *adapter = common->priv;
	struct rsi_91x_usbdev *dev = (struct rsi_91x_usbdev *)adapter->rsi_dev;
	struct rx_usb_ctrl_block *rx_cb;
	int status, idx, num_rx_cb;

	num_rx_cb = (adapter->priv->coex_mode > 1 ? 2 : 1);
	do {
		rsi_wait_event(&dev->rx_thread.event, EVENT_WAIT_FOREVER);

		if (atomic_read(&dev->rx_thread.thread_done))
			goto out;

		for (idx = 0; idx < num_rx_cb; idx++) {
			rx_cb = &dev->rx_cb[idx];
			if (!rx_cb->pend)
				continue;

			mutex_lock(&common->rx_lock);
			status = rsi_read_pkt(common, rx_cb->rx_buffer, 0);
			if (status) {
				rsi_dbg(ERR_ZONE, "%s: Failed To read data",
					__func__);
				mutex_unlock(&common->rx_lock);
				break;
			}
			rx_cb->pend = 0;
			mutex_unlock(&common->rx_lock);

			if (adapter->rx_urb_submit(adapter, rx_cb->ep_num)) {
				rsi_dbg(ERR_ZONE,
					"%s: Failed in urb submission",
					__func__);
				return;
			}
		}
		rsi_reset_event(&dev->rx_thread.event);
	} while (1);

out:
	rsi_dbg(INFO_ZONE, "%s: Terminated thread\n", __func__);
	complete_and_exit(&dev->rx_thread.completion, 0);
}

