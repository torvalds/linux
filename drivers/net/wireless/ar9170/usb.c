/*
 * Atheros AR9170 driver
 *
 * USB - frontend
 *
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2009, Christian Lamparter <chunkeey@web.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, see
 * http://www.gnu.org/licenses/.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *    Copyright (c) 2007-2008 Atheros Communications, Inc.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <net/mac80211.h>
#include "ar9170.h"
#include "cmd.h"
#include "hw.h"
#include "usb.h"

MODULE_AUTHOR("Johannes Berg <johannes@sipsolutions.net>");
MODULE_AUTHOR("Christian Lamparter <chunkeey@web.de>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Atheros AR9170 802.11n USB wireless");
MODULE_FIRMWARE("ar9170-1.fw");
MODULE_FIRMWARE("ar9170-2.fw");

static struct usb_device_id ar9170_usb_ids[] = {
	/* Atheros 9170 */
	{ USB_DEVICE(0x0cf3, 0x9170) },
	/* Atheros TG121N */
	{ USB_DEVICE(0x0cf3, 0x1001) },
	/* Cace Airpcap NX */
	{ USB_DEVICE(0xcace, 0x0300) },
	/* D-Link DWA 160A */
	{ USB_DEVICE(0x07d1, 0x3c10) },
	/* Netgear WNDA3100 */
	{ USB_DEVICE(0x0846, 0x9010) },
	/* Netgear WN111 v2 */
	{ USB_DEVICE(0x0846, 0x9001) },
	/* Zydas ZD1221 */
	{ USB_DEVICE(0x0ace, 0x1221) },
	/* Z-Com UB81 BG */
	{ USB_DEVICE(0x0cde, 0x0023) },
	/* Z-Com UB82 ABG */
	{ USB_DEVICE(0x0cde, 0x0026) },
	/* Arcadyan WN7512 */
	{ USB_DEVICE(0x083a, 0xf522) },
	/* Planex GWUS300 */
	{ USB_DEVICE(0x2019, 0x5304) },
	/* IO-Data WNGDNUS2 */
	{ USB_DEVICE(0x04bb, 0x093f) },

	/* terminate */
	{}
};
MODULE_DEVICE_TABLE(usb, ar9170_usb_ids);

static void ar9170_usb_tx_urb_complete_free(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct ar9170_usb *aru = (struct ar9170_usb *)
	      usb_get_intfdata(usb_ifnum_to_if(urb->dev, 0));

	if (!aru) {
		dev_kfree_skb_irq(skb);
		return ;
	}

	ar9170_handle_tx_status(&aru->common, skb, false,
				AR9170_TX_STATUS_COMPLETE);
}

static void ar9170_usb_tx_urb_complete(struct urb *urb)
{
}

static void ar9170_usb_irq_completed(struct urb *urb)
{
	struct ar9170_usb *aru = urb->context;

	switch (urb->status) {
	/* everything is fine */
	case 0:
		break;

	/* disconnect */
	case -ENOENT:
	case -ECONNRESET:
	case -ENODEV:
	case -ESHUTDOWN:
		goto free;

	default:
		goto resubmit;
	}

	print_hex_dump_bytes("ar9170 irq: ", DUMP_PREFIX_OFFSET,
			     urb->transfer_buffer, urb->actual_length);

resubmit:
	usb_anchor_urb(urb, &aru->rx_submitted);
	if (usb_submit_urb(urb, GFP_ATOMIC)) {
		usb_unanchor_urb(urb);
		goto free;
	}

	return;

free:
	usb_buffer_free(aru->udev, 64, urb->transfer_buffer, urb->transfer_dma);
}

static void ar9170_usb_rx_completed(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct ar9170_usb *aru = (struct ar9170_usb *)
		usb_get_intfdata(usb_ifnum_to_if(urb->dev, 0));
	int err;

	if (!aru)
		goto free;

	switch (urb->status) {
	/* everything is fine */
	case 0:
		break;

	/* disconnect */
	case -ENOENT:
	case -ECONNRESET:
	case -ENODEV:
	case -ESHUTDOWN:
		goto free;

	default:
		goto resubmit;
	}

	skb_put(skb, urb->actual_length);
	ar9170_rx(&aru->common, skb);

resubmit:
	skb_reset_tail_pointer(skb);
	skb_trim(skb, 0);

	usb_anchor_urb(urb, &aru->rx_submitted);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		usb_unanchor_urb(urb);
		dev_kfree_skb_irq(skb);
	}

	return ;

free:
	dev_kfree_skb_irq(skb);
	return;
}

static int ar9170_usb_prep_rx_urb(struct ar9170_usb *aru,
				  struct urb *urb, gfp_t gfp)
{
	struct sk_buff *skb;

	skb = __dev_alloc_skb(AR9170_MAX_RX_BUFFER_SIZE + 32, gfp);
	if (!skb)
		return -ENOMEM;

	/* reserve some space for mac80211's radiotap */
	skb_reserve(skb, 32);

	usb_fill_bulk_urb(urb, aru->udev,
			  usb_rcvbulkpipe(aru->udev, AR9170_EP_RX),
			  skb->data, min(skb_tailroom(skb),
			  AR9170_MAX_RX_BUFFER_SIZE),
			  ar9170_usb_rx_completed, skb);

	return 0;
}

static int ar9170_usb_alloc_rx_irq_urb(struct ar9170_usb *aru)
{
	struct urb *urb = NULL;
	void *ibuf;
	int err = -ENOMEM;

	/* initialize interrupt endpoint */
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		goto out;

	ibuf = usb_buffer_alloc(aru->udev, 64, GFP_KERNEL, &urb->transfer_dma);
	if (!ibuf)
		goto out;

	usb_fill_int_urb(urb, aru->udev,
			 usb_rcvintpipe(aru->udev, AR9170_EP_IRQ), ibuf,
			 64, ar9170_usb_irq_completed, aru, 1);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	usb_anchor_urb(urb, &aru->rx_submitted);
	err = usb_submit_urb(urb, GFP_KERNEL);
	if (err) {
		usb_unanchor_urb(urb);
		usb_buffer_free(aru->udev, 64, urb->transfer_buffer,
				urb->transfer_dma);
	}

out:
	usb_free_urb(urb);
	return err;
}

static int ar9170_usb_alloc_rx_bulk_urbs(struct ar9170_usb *aru)
{
	struct urb *urb;
	int i;
	int err = -EINVAL;

	for (i = 0; i < AR9170_NUM_RX_URBS; i++) {
		err = -ENOMEM;
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb)
			goto err_out;

		err = ar9170_usb_prep_rx_urb(aru, urb, GFP_KERNEL);
		if (err) {
			usb_free_urb(urb);
			goto err_out;
		}

		usb_anchor_urb(urb, &aru->rx_submitted);
		err = usb_submit_urb(urb, GFP_KERNEL);
		if (err) {
			usb_unanchor_urb(urb);
			dev_kfree_skb_any((void *) urb->transfer_buffer);
			usb_free_urb(urb);
			goto err_out;
		}
		usb_free_urb(urb);
	}

	/* the device now waiting for a firmware. */
	aru->common.state = AR9170_IDLE;
	return 0;

err_out:

	usb_kill_anchored_urbs(&aru->rx_submitted);
	return err;
}

static void ar9170_usb_cancel_urbs(struct ar9170_usb *aru)
{
	int ret;

	aru->common.state = AR9170_UNKNOWN_STATE;

	usb_unlink_anchored_urbs(&aru->tx_submitted);

	/* give the LED OFF command and the deauth frame a chance to air. */
	ret = usb_wait_anchor_empty_timeout(&aru->tx_submitted,
					    msecs_to_jiffies(100));
	if (ret == 0)
		dev_err(&aru->udev->dev, "kill pending tx urbs.\n");
	usb_poison_anchored_urbs(&aru->tx_submitted);

	usb_poison_anchored_urbs(&aru->rx_submitted);
}

static int ar9170_usb_exec_cmd(struct ar9170 *ar, enum ar9170_cmd cmd,
			       unsigned int plen, void *payload,
			       unsigned int outlen, void *out)
{
	struct ar9170_usb *aru = (void *) ar;
	struct urb *urb = NULL;
	unsigned long flags;
	int err = -ENOMEM;

	if (unlikely(!IS_ACCEPTING_CMD(ar)))
		return -EPERM;

	if (WARN_ON(plen > AR9170_MAX_CMD_LEN - 4))
		return -EINVAL;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (unlikely(!urb))
		goto err_free;

	ar->cmdbuf[0] = cpu_to_le32(plen);
	ar->cmdbuf[0] |= cpu_to_le32(cmd << 8);
	/* writing multiple regs fills this buffer already */
	if (plen && payload != (u8 *)(&ar->cmdbuf[1]))
		memcpy(&ar->cmdbuf[1], payload, plen);

	spin_lock_irqsave(&aru->common.cmdlock, flags);
	aru->readbuf = (u8 *)out;
	aru->readlen = outlen;
	spin_unlock_irqrestore(&aru->common.cmdlock, flags);

	usb_fill_int_urb(urb, aru->udev,
			 usb_sndbulkpipe(aru->udev, AR9170_EP_CMD),
			 aru->common.cmdbuf, plen + 4,
			 ar9170_usb_tx_urb_complete, NULL, 1);

	usb_anchor_urb(urb, &aru->tx_submitted);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		usb_unanchor_urb(urb);
		usb_free_urb(urb);
		goto err_unbuf;
	}
	usb_free_urb(urb);

	err = wait_for_completion_timeout(&aru->cmd_wait, HZ);
	if (err == 0) {
		err = -ETIMEDOUT;
		goto err_unbuf;
	}

	if (outlen >= 0 && aru->readlen != outlen) {
		err = -EMSGSIZE;
		goto err_unbuf;
	}

	return 0;

err_unbuf:
	/* Maybe the device was removed in the second we were waiting? */
	if (IS_STARTED(ar)) {
		dev_err(&aru->udev->dev, "no command feedback "
					 "received (%d).\n", err);

		/* provide some maybe useful debug information */
		print_hex_dump_bytes("ar9170 cmd: ", DUMP_PREFIX_NONE,
				     aru->common.cmdbuf, plen + 4);
		dump_stack();
	}

	/* invalidate to avoid completing the next prematurely */
	spin_lock_irqsave(&aru->common.cmdlock, flags);
	aru->readbuf = NULL;
	aru->readlen = 0;
	spin_unlock_irqrestore(&aru->common.cmdlock, flags);

err_free:

	return err;
}

static int ar9170_usb_tx(struct ar9170 *ar, struct sk_buff *skb,
			 bool txstatus_needed, unsigned int extra_len)
{
	struct ar9170_usb *aru = (struct ar9170_usb *) ar;
	struct urb *urb;
	int err;

	if (unlikely(!IS_STARTED(ar))) {
		/* Seriously, what were you drink... err... thinking!? */
		return -EPERM;
	}

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (unlikely(!urb))
		return -ENOMEM;

	usb_fill_bulk_urb(urb, aru->udev,
			  usb_sndbulkpipe(aru->udev, AR9170_EP_TX),
			  skb->data, skb->len + extra_len, (txstatus_needed ?
			  ar9170_usb_tx_urb_complete :
			  ar9170_usb_tx_urb_complete_free), skb);
	urb->transfer_flags |= URB_ZERO_PACKET;

	usb_anchor_urb(urb, &aru->tx_submitted);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (unlikely(err))
		usb_unanchor_urb(urb);

	usb_free_urb(urb);
	return err;
}

static void ar9170_usb_callback_cmd(struct ar9170 *ar, u32 len , void *buffer)
{
	struct ar9170_usb *aru = (void *) ar;
	unsigned long flags;
	u32 in, out;

	if (!buffer)
		return ;

	in = le32_to_cpup((__le32 *)buffer);
	out = le32_to_cpu(ar->cmdbuf[0]);

	/* mask off length byte */
	out &= ~0xFF;

	if (aru->readlen >= 0) {
		/* add expected length */
		out |= aru->readlen;
	} else {
		/* add obtained length */
		out |= in & 0xFF;
	}

	/*
	 * Some commands (e.g: AR9170_CMD_FREQUENCY) have a variable response
	 * length and we cannot predict the correct length in advance.
	 * So we only check if we provided enough space for the data.
	 */
	if (unlikely(out < in)) {
		dev_warn(&aru->udev->dev, "received invalid command response "
					  "got %d bytes, instead of %d bytes "
					  "and the resp length is %d bytes\n",
			 in, out, len);
		print_hex_dump_bytes("ar9170 invalid resp: ",
				     DUMP_PREFIX_OFFSET, buffer, len);
		/*
		 * Do not complete, then the command times out,
		 * and we get a stack trace from there.
		 */
		return ;
	}

	spin_lock_irqsave(&aru->common.cmdlock, flags);
	if (aru->readbuf && len > 0) {
		memcpy(aru->readbuf, buffer + 4, len - 4);
		aru->readbuf = NULL;
	}
	complete(&aru->cmd_wait);
	spin_unlock_irqrestore(&aru->common.cmdlock, flags);
}

static int ar9170_usb_upload(struct ar9170_usb *aru, const void *data,
			     size_t len, u32 addr, bool complete)
{
	int transfer, err;
	u8 *buf = kmalloc(4096, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	while (len) {
		transfer = min_t(int, len, 4096);
		memcpy(buf, data, transfer);

		err = usb_control_msg(aru->udev, usb_sndctrlpipe(aru->udev, 0),
				      0x30 /* FW DL */, 0x40 | USB_DIR_OUT,
				      addr >> 8, 0, buf, transfer, 1000);

		if (err < 0) {
			kfree(buf);
			return err;
		}

		len -= transfer;
		data += transfer;
		addr += transfer;
	}
	kfree(buf);

	if (complete) {
		err = usb_control_msg(aru->udev, usb_sndctrlpipe(aru->udev, 0),
				      0x31 /* FW DL COMPLETE */,
				      0x40 | USB_DIR_OUT, 0, 0, NULL, 0, 5000);
	}

	return 0;
}

static int ar9170_usb_request_firmware(struct ar9170_usb *aru)
{
	int err = 0;

	err = request_firmware(&aru->init_values, "ar9170-1.fw",
			       &aru->udev->dev);
	if (err) {
		dev_err(&aru->udev->dev, "file with init values not found.\n");
		return err;
	}

	err = request_firmware(&aru->firmware, "ar9170-2.fw", &aru->udev->dev);
	if (err) {
		release_firmware(aru->init_values);
		dev_err(&aru->udev->dev, "firmware file not found.\n");
		return err;
	}

	return err;
}

static int ar9170_usb_reset(struct ar9170_usb *aru)
{
	int ret, lock = (aru->intf->condition != USB_INTERFACE_BINDING);

	if (lock) {
		ret = usb_lock_device_for_reset(aru->udev, aru->intf);
		if (ret < 0) {
			dev_err(&aru->udev->dev, "unable to lock device "
				"for reset (%d).\n", ret);
			return ret;
		}
	}

	ret = usb_reset_device(aru->udev);
	if (lock)
		usb_unlock_device(aru->udev);

	/* let it rest - for a second - */
	msleep(1000);

	return ret;
}

static int ar9170_usb_upload_firmware(struct ar9170_usb *aru)
{
	int err;

	/* First, upload initial values to device RAM */
	err = ar9170_usb_upload(aru, aru->init_values->data,
				aru->init_values->size, 0x102800, false);
	if (err) {
		dev_err(&aru->udev->dev, "firmware part 1 "
			"upload failed (%d).\n", err);
		return err;
	}

	/* Then, upload the firmware itself and start it */
	return ar9170_usb_upload(aru, aru->firmware->data, aru->firmware->size,
				0x200000, true);
}

static int ar9170_usb_init_transport(struct ar9170_usb *aru)
{
	struct ar9170 *ar = (void *) &aru->common;
	int err;

	ar9170_regwrite_begin(ar);

	/* Set USB Rx stream mode MAX packet number to 2 */
	ar9170_regwrite(AR9170_USB_REG_MAX_AGG_UPLOAD, 0x4);

	/* Set USB Rx stream mode timeout to 10us */
	ar9170_regwrite(AR9170_USB_REG_UPLOAD_TIME_CTL, 0x80);

	ar9170_regwrite_finish();

	err = ar9170_regwrite_result();
	if (err)
		dev_err(&aru->udev->dev, "USB setup failed (%d).\n", err);

	return err;
}

static void ar9170_usb_stop(struct ar9170 *ar)
{
	struct ar9170_usb *aru = (void *) ar;
	int ret;

	if (IS_ACCEPTING_CMD(ar))
		aru->common.state = AR9170_STOPPED;

	/* lets wait a while until the tx - queues are dried out */
	ret = usb_wait_anchor_empty_timeout(&aru->tx_submitted,
					    msecs_to_jiffies(1000));
	if (ret == 0)
		dev_err(&aru->udev->dev, "kill pending tx urbs.\n");

	usb_poison_anchored_urbs(&aru->tx_submitted);

	/*
	 * Note:
	 * So far we freed all tx urbs, but we won't dare to touch any rx urbs.
	 * Else we would end up with a unresponsive device...
	 */
}

static int ar9170_usb_open(struct ar9170 *ar)
{
	struct ar9170_usb *aru = (void *) ar;
	int err;

	usb_unpoison_anchored_urbs(&aru->tx_submitted);
	err = ar9170_usb_init_transport(aru);
	if (err) {
		usb_poison_anchored_urbs(&aru->tx_submitted);
		return err;
	}

	aru->common.state = AR9170_IDLE;
	return 0;
}

static int ar9170_usb_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct ar9170_usb *aru;
	struct ar9170 *ar;
	struct usb_device *udev;
	int err;

	aru = ar9170_alloc(sizeof(*aru));
	if (IS_ERR(aru)) {
		err = PTR_ERR(aru);
		goto out;
	}

	udev = interface_to_usbdev(intf);
	usb_get_dev(udev);
	aru->udev = udev;
	aru->intf = intf;
	ar = &aru->common;

	usb_set_intfdata(intf, aru);
	SET_IEEE80211_DEV(ar->hw, &udev->dev);

	init_usb_anchor(&aru->rx_submitted);
	init_usb_anchor(&aru->tx_submitted);
	init_completion(&aru->cmd_wait);

	aru->common.stop = ar9170_usb_stop;
	aru->common.open = ar9170_usb_open;
	aru->common.tx = ar9170_usb_tx;
	aru->common.exec_cmd = ar9170_usb_exec_cmd;
	aru->common.callback_cmd = ar9170_usb_callback_cmd;

	err = ar9170_usb_reset(aru);
	if (err)
		goto err_unlock;

	err = ar9170_usb_request_firmware(aru);
	if (err)
		goto err_unlock;

	err = ar9170_usb_alloc_rx_irq_urb(aru);
	if (err)
		goto err_freefw;

	err = ar9170_usb_alloc_rx_bulk_urbs(aru);
	if (err)
		goto err_unrx;

	err = ar9170_usb_upload_firmware(aru);
	if (err) {
		err = ar9170_echo_test(&aru->common, 0x60d43110);
		if (err) {
			/* force user invention, by disabling the device */
			err = usb_driver_set_configuration(aru->udev, -1);
			dev_err(&aru->udev->dev, "device is in a bad state. "
						 "please reconnect it!\n");
			goto err_unrx;
		}
	}

	err = ar9170_usb_open(ar);
	if (err)
		goto err_unrx;

	err = ar9170_register(ar, &udev->dev);

	ar9170_usb_stop(ar);
	if (err)
		goto err_unrx;

	return 0;

err_unrx:
	ar9170_usb_cancel_urbs(aru);

err_freefw:
	release_firmware(aru->init_values);
	release_firmware(aru->firmware);

err_unlock:
	usb_set_intfdata(intf, NULL);
	usb_put_dev(udev);
	ieee80211_free_hw(ar->hw);
out:
	return err;
}

static void ar9170_usb_disconnect(struct usb_interface *intf)
{
	struct ar9170_usb *aru = usb_get_intfdata(intf);

	if (!aru)
		return;

	aru->common.state = AR9170_IDLE;
	ar9170_unregister(&aru->common);
	ar9170_usb_cancel_urbs(aru);

	release_firmware(aru->init_values);
	release_firmware(aru->firmware);

	usb_put_dev(aru->udev);
	usb_set_intfdata(intf, NULL);
	ieee80211_free_hw(aru->common.hw);
}

static struct usb_driver ar9170_driver = {
	.name = "ar9170usb",
	.probe = ar9170_usb_probe,
	.disconnect = ar9170_usb_disconnect,
	.id_table = ar9170_usb_ids,
	.soft_unbind = 1,
};

static int __init ar9170_init(void)
{
	return usb_register(&ar9170_driver);
}

static void __exit ar9170_exit(void)
{
	usb_deregister(&ar9170_driver);
}

module_init(ar9170_init);
module_exit(ar9170_exit);
