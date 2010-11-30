/*
 * Copyright (c) 2010 Atheros Communications Inc.
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

#include "htc.h"

/* identify firmware images */
#define FIRMWARE_AR7010		"ar7010.fw"
#define FIRMWARE_AR7010_1_1	"ar7010_1_1.fw"
#define FIRMWARE_AR9271		"ar9271.fw"

MODULE_FIRMWARE(FIRMWARE_AR7010);
MODULE_FIRMWARE(FIRMWARE_AR7010_1_1);
MODULE_FIRMWARE(FIRMWARE_AR9271);

static struct usb_device_id ath9k_hif_usb_ids[] = {
	{ USB_DEVICE(0x0cf3, 0x9271) }, /* Atheros */
	{ USB_DEVICE(0x0cf3, 0x1006) }, /* Atheros */
	{ USB_DEVICE(0x0cf3, 0x7010),
		.driver_info = AR7010_DEVICE },
					/* Atheros */
	{ USB_DEVICE(0x0cf3, 0x7015),
		.driver_info = AR7010_DEVICE | AR9287_DEVICE },
					/* Atheros */
	{ USB_DEVICE(0x0846, 0x9030) }, /* Netgear N150 */
	{ USB_DEVICE(0x0846, 0x9018),
		.driver_info = AR7010_DEVICE },
					/* Netgear WNDA3200 */
	{ USB_DEVICE(0x07D1, 0x3A10) }, /* Dlink Wireless 150 */
	{ USB_DEVICE(0x13D3, 0x3327) }, /* Azurewave */
	{ USB_DEVICE(0x13D3, 0x3328) }, /* Azurewave */
	{ USB_DEVICE(0x13D3, 0x3346) }, /* IMC Networks */
	{ USB_DEVICE(0x13D3, 0x3348) }, /* Azurewave */
	{ USB_DEVICE(0x13D3, 0x3349) }, /* Azurewave */
	{ USB_DEVICE(0x13D3, 0x3350) }, /* Azurewave */
	{ USB_DEVICE(0x04CA, 0x4605) }, /* Liteon */
	{ USB_DEVICE(0x083A, 0xA704),
		.driver_info = AR7010_DEVICE },
					/* SMC Networks */
	{ USB_DEVICE(0x040D, 0x3801) }, /* VIA */
	{ USB_DEVICE(0x1668, 0x1200),
		.driver_info = AR7010_DEVICE | AR9287_DEVICE },
					/* Verizon */
	{ },
};

MODULE_DEVICE_TABLE(usb, ath9k_hif_usb_ids);

static int __hif_usb_tx(struct hif_device_usb *hif_dev);

static void hif_usb_regout_cb(struct urb *urb)
{
	struct cmd_buf *cmd = (struct cmd_buf *)urb->context;

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ENODEV:
	case -ESHUTDOWN:
		goto free;
	default:
		break;
	}

	if (cmd) {
		ath9k_htc_txcompletion_cb(cmd->hif_dev->htc_handle,
					  cmd->skb, 1);
		kfree(cmd);
	}

	return;
free:
	kfree_skb(cmd->skb);
	kfree(cmd);
}

static int hif_usb_send_regout(struct hif_device_usb *hif_dev,
			       struct sk_buff *skb)
{
	struct urb *urb;
	struct cmd_buf *cmd;
	int ret = 0;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (urb == NULL)
		return -ENOMEM;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	cmd->skb = skb;
	cmd->hif_dev = hif_dev;

	usb_fill_bulk_urb(urb, hif_dev->udev,
			 usb_sndbulkpipe(hif_dev->udev, USB_REG_OUT_PIPE),
			 skb->data, skb->len,
			 hif_usb_regout_cb, cmd);

	usb_anchor_urb(urb, &hif_dev->regout_submitted);
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		usb_unanchor_urb(urb);
		kfree(cmd);
	}
	usb_free_urb(urb);

	return ret;
}

static inline void ath9k_skb_queue_purge(struct hif_device_usb *hif_dev,
					 struct sk_buff_head *list)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(list)) != NULL) {
		dev_kfree_skb_any(skb);
		TX_STAT_INC(skb_dropped);
	}
}

static void hif_usb_tx_cb(struct urb *urb)
{
	struct tx_buf *tx_buf = (struct tx_buf *) urb->context;
	struct hif_device_usb *hif_dev;
	struct sk_buff *skb;

	if (!tx_buf || !tx_buf->hif_dev)
		return;

	hif_dev = tx_buf->hif_dev;

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ENODEV:
	case -ESHUTDOWN:
		/*
		 * The URB has been killed, free the SKBs
		 * and return.
		 */
		ath9k_skb_queue_purge(hif_dev, &tx_buf->skb_queue);
		return;
	default:
		break;
	}

	/* Check if TX has been stopped */
	spin_lock(&hif_dev->tx.tx_lock);
	if (hif_dev->tx.flags & HIF_USB_TX_STOP) {
		spin_unlock(&hif_dev->tx.tx_lock);
		ath9k_skb_queue_purge(hif_dev, &tx_buf->skb_queue);
		goto add_free;
	}
	spin_unlock(&hif_dev->tx.tx_lock);

	/* Complete the queued SKBs. */
	while ((skb = __skb_dequeue(&tx_buf->skb_queue)) != NULL) {
		ath9k_htc_txcompletion_cb(hif_dev->htc_handle,
					  skb, 1);
		TX_STAT_INC(skb_completed);
	}

add_free:
	/* Re-initialize the SKB queue */
	tx_buf->len = tx_buf->offset = 0;
	__skb_queue_head_init(&tx_buf->skb_queue);

	/* Add this TX buffer to the free list */
	spin_lock(&hif_dev->tx.tx_lock);
	list_move_tail(&tx_buf->list, &hif_dev->tx.tx_buf);
	hif_dev->tx.tx_buf_cnt++;
	if (!(hif_dev->tx.flags & HIF_USB_TX_STOP))
		__hif_usb_tx(hif_dev); /* Check for pending SKBs */
	TX_STAT_INC(buf_completed);
	spin_unlock(&hif_dev->tx.tx_lock);
}

/* TX lock has to be taken */
static int __hif_usb_tx(struct hif_device_usb *hif_dev)
{
	struct tx_buf *tx_buf = NULL;
	struct sk_buff *nskb = NULL;
	int ret = 0, i;
	u16 *hdr, tx_skb_cnt = 0;
	u8 *buf;

	if (hif_dev->tx.tx_skb_cnt == 0)
		return 0;

	/* Check if a free TX buffer is available */
	if (list_empty(&hif_dev->tx.tx_buf))
		return 0;

	tx_buf = list_first_entry(&hif_dev->tx.tx_buf, struct tx_buf, list);
	list_move_tail(&tx_buf->list, &hif_dev->tx.tx_pending);
	hif_dev->tx.tx_buf_cnt--;

	tx_skb_cnt = min_t(u16, hif_dev->tx.tx_skb_cnt, MAX_TX_AGGR_NUM);

	for (i = 0; i < tx_skb_cnt; i++) {
		nskb = __skb_dequeue(&hif_dev->tx.tx_skb_queue);

		/* Should never be NULL */
		BUG_ON(!nskb);

		hif_dev->tx.tx_skb_cnt--;

		buf = tx_buf->buf;
		buf += tx_buf->offset;
		hdr = (u16 *)buf;
		*hdr++ = nskb->len;
		*hdr++ = ATH_USB_TX_STREAM_MODE_TAG;
		buf += 4;
		memcpy(buf, nskb->data, nskb->len);
		tx_buf->len = nskb->len + 4;

		if (i < (tx_skb_cnt - 1))
			tx_buf->offset += (((tx_buf->len - 1) / 4) + 1) * 4;

		if (i == (tx_skb_cnt - 1))
			tx_buf->len += tx_buf->offset;

		__skb_queue_tail(&tx_buf->skb_queue, nskb);
		TX_STAT_INC(skb_queued);
	}

	usb_fill_bulk_urb(tx_buf->urb, hif_dev->udev,
			  usb_sndbulkpipe(hif_dev->udev, USB_WLAN_TX_PIPE),
			  tx_buf->buf, tx_buf->len,
			  hif_usb_tx_cb, tx_buf);

	ret = usb_submit_urb(tx_buf->urb, GFP_ATOMIC);
	if (ret) {
		tx_buf->len = tx_buf->offset = 0;
		ath9k_skb_queue_purge(hif_dev, &tx_buf->skb_queue);
		__skb_queue_head_init(&tx_buf->skb_queue);
		list_move_tail(&tx_buf->list, &hif_dev->tx.tx_buf);
		hif_dev->tx.tx_buf_cnt++;
	}

	if (!ret)
		TX_STAT_INC(buf_queued);

	return ret;
}

static int hif_usb_send_tx(struct hif_device_usb *hif_dev, struct sk_buff *skb,
			   struct ath9k_htc_tx_ctl *tx_ctl)
{
	unsigned long flags;

	spin_lock_irqsave(&hif_dev->tx.tx_lock, flags);

	if (hif_dev->tx.flags & HIF_USB_TX_STOP) {
		spin_unlock_irqrestore(&hif_dev->tx.tx_lock, flags);
		return -ENODEV;
	}

	/* Check if the max queue count has been reached */
	if (hif_dev->tx.tx_skb_cnt > MAX_TX_BUF_NUM) {
		spin_unlock_irqrestore(&hif_dev->tx.tx_lock, flags);
		return -ENOMEM;
	}

	__skb_queue_tail(&hif_dev->tx.tx_skb_queue, skb);
	hif_dev->tx.tx_skb_cnt++;

	/* Send normal frames immediately */
	if (!tx_ctl || (tx_ctl && (tx_ctl->type == ATH9K_HTC_NORMAL)))
		__hif_usb_tx(hif_dev);

	/* Check if AMPDUs have to be sent immediately */
	if (tx_ctl && (tx_ctl->type == ATH9K_HTC_AMPDU) &&
	    (hif_dev->tx.tx_buf_cnt == MAX_TX_URB_NUM) &&
	    (hif_dev->tx.tx_skb_cnt < 2)) {
		__hif_usb_tx(hif_dev);
	}

	spin_unlock_irqrestore(&hif_dev->tx.tx_lock, flags);

	return 0;
}

static void hif_usb_start(void *hif_handle, u8 pipe_id)
{
	struct hif_device_usb *hif_dev = (struct hif_device_usb *)hif_handle;
	unsigned long flags;

	hif_dev->flags |= HIF_USB_START;

	spin_lock_irqsave(&hif_dev->tx.tx_lock, flags);
	hif_dev->tx.flags &= ~HIF_USB_TX_STOP;
	spin_unlock_irqrestore(&hif_dev->tx.tx_lock, flags);
}

static void hif_usb_stop(void *hif_handle, u8 pipe_id)
{
	struct hif_device_usb *hif_dev = (struct hif_device_usb *)hif_handle;
	unsigned long flags;

	spin_lock_irqsave(&hif_dev->tx.tx_lock, flags);
	ath9k_skb_queue_purge(hif_dev, &hif_dev->tx.tx_skb_queue);
	hif_dev->tx.tx_skb_cnt = 0;
	hif_dev->tx.flags |= HIF_USB_TX_STOP;
	spin_unlock_irqrestore(&hif_dev->tx.tx_lock, flags);
}

static int hif_usb_send(void *hif_handle, u8 pipe_id, struct sk_buff *skb,
			struct ath9k_htc_tx_ctl *tx_ctl)
{
	struct hif_device_usb *hif_dev = (struct hif_device_usb *)hif_handle;
	int ret = 0;

	switch (pipe_id) {
	case USB_WLAN_TX_PIPE:
		ret = hif_usb_send_tx(hif_dev, skb, tx_ctl);
		break;
	case USB_REG_OUT_PIPE:
		ret = hif_usb_send_regout(hif_dev, skb);
		break;
	default:
		dev_err(&hif_dev->udev->dev,
			"ath9k_htc: Invalid TX pipe: %d\n", pipe_id);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static struct ath9k_htc_hif hif_usb = {
	.transport = ATH9K_HIF_USB,
	.name = "ath9k_hif_usb",

	.control_ul_pipe = USB_REG_OUT_PIPE,
	.control_dl_pipe = USB_REG_IN_PIPE,

	.start = hif_usb_start,
	.stop = hif_usb_stop,
	.send = hif_usb_send,
};

static void ath9k_hif_usb_rx_stream(struct hif_device_usb *hif_dev,
				    struct sk_buff *skb)
{
	struct sk_buff *nskb, *skb_pool[MAX_PKT_NUM_IN_TRANSFER];
	int index = 0, i = 0, len = skb->len;
	int rx_remain_len, rx_pkt_len;
	u16 pool_index = 0;
	u8 *ptr;

	spin_lock(&hif_dev->rx_lock);

	rx_remain_len = hif_dev->rx_remain_len;
	rx_pkt_len = hif_dev->rx_transfer_len;

	if (rx_remain_len != 0) {
		struct sk_buff *remain_skb = hif_dev->remain_skb;

		if (remain_skb) {
			ptr = (u8 *) remain_skb->data;

			index = rx_remain_len;
			rx_remain_len -= hif_dev->rx_pad_len;
			ptr += rx_pkt_len;

			memcpy(ptr, skb->data, rx_remain_len);

			rx_pkt_len += rx_remain_len;
			hif_dev->rx_remain_len = 0;
			skb_put(remain_skb, rx_pkt_len);

			skb_pool[pool_index++] = remain_skb;

		} else {
			index = rx_remain_len;
		}
	}

	spin_unlock(&hif_dev->rx_lock);

	while (index < len) {
		u16 pkt_len;
		u16 pkt_tag;
		u16 pad_len;
		int chk_idx;

		ptr = (u8 *) skb->data;

		pkt_len = ptr[index] + (ptr[index+1] << 8);
		pkt_tag = ptr[index+2] + (ptr[index+3] << 8);

		if (pkt_tag != ATH_USB_RX_STREAM_MODE_TAG) {
			RX_STAT_INC(skb_dropped);
			return;
		}

		pad_len = 4 - (pkt_len & 0x3);
		if (pad_len == 4)
			pad_len = 0;

		chk_idx = index;
		index = index + 4 + pkt_len + pad_len;

		if (index > MAX_RX_BUF_SIZE) {
			spin_lock(&hif_dev->rx_lock);
			hif_dev->rx_remain_len = index - MAX_RX_BUF_SIZE;
			hif_dev->rx_transfer_len =
				MAX_RX_BUF_SIZE - chk_idx - 4;
			hif_dev->rx_pad_len = pad_len;

			nskb = __dev_alloc_skb(pkt_len + 32, GFP_ATOMIC);
			if (!nskb) {
				dev_err(&hif_dev->udev->dev,
					"ath9k_htc: RX memory allocation error\n");
				spin_unlock(&hif_dev->rx_lock);
				goto err;
			}
			skb_reserve(nskb, 32);
			RX_STAT_INC(skb_allocated);

			memcpy(nskb->data, &(skb->data[chk_idx+4]),
			       hif_dev->rx_transfer_len);

			/* Record the buffer pointer */
			hif_dev->remain_skb = nskb;
			spin_unlock(&hif_dev->rx_lock);
		} else {
			nskb = __dev_alloc_skb(pkt_len + 32, GFP_ATOMIC);
			if (!nskb) {
				dev_err(&hif_dev->udev->dev,
					"ath9k_htc: RX memory allocation error\n");
				goto err;
			}
			skb_reserve(nskb, 32);
			RX_STAT_INC(skb_allocated);

			memcpy(nskb->data, &(skb->data[chk_idx+4]), pkt_len);
			skb_put(nskb, pkt_len);
			skb_pool[pool_index++] = nskb;
		}
	}

err:
	for (i = 0; i < pool_index; i++) {
		ath9k_htc_rx_msg(hif_dev->htc_handle, skb_pool[i],
				 skb_pool[i]->len, USB_WLAN_RX_PIPE);
		RX_STAT_INC(skb_completed);
	}
}

static void ath9k_hif_usb_rx_cb(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *) urb->context;
	struct hif_device_usb *hif_dev =
		usb_get_intfdata(usb_ifnum_to_if(urb->dev, 0));
	int ret;

	if (!skb)
		return;

	if (!hif_dev)
		goto free;

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ENODEV:
	case -ESHUTDOWN:
		goto free;
	default:
		goto resubmit;
	}

	if (likely(urb->actual_length != 0)) {
		skb_put(skb, urb->actual_length);
		ath9k_hif_usb_rx_stream(hif_dev, skb);
	}

resubmit:
	skb_reset_tail_pointer(skb);
	skb_trim(skb, 0);

	usb_anchor_urb(urb, &hif_dev->rx_submitted);
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret) {
		usb_unanchor_urb(urb);
		goto free;
	}

	return;
free:
	kfree_skb(skb);
}

static void ath9k_hif_usb_reg_in_cb(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *) urb->context;
	struct sk_buff *nskb;
	struct hif_device_usb *hif_dev =
		usb_get_intfdata(usb_ifnum_to_if(urb->dev, 0));
	int ret;

	if (!skb)
		return;

	if (!hif_dev)
		goto free;

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ENODEV:
	case -ESHUTDOWN:
		goto free;
	default:
		goto resubmit;
	}

	if (likely(urb->actual_length != 0)) {
		skb_put(skb, urb->actual_length);

		/* Process the command first */
		ath9k_htc_rx_msg(hif_dev->htc_handle, skb,
				 skb->len, USB_REG_IN_PIPE);


		nskb = alloc_skb(MAX_REG_IN_BUF_SIZE, GFP_ATOMIC);
		if (!nskb) {
			dev_err(&hif_dev->udev->dev,
				"ath9k_htc: REG_IN memory allocation failure\n");
			urb->context = NULL;
			return;
		}

		usb_fill_bulk_urb(urb, hif_dev->udev,
				 usb_rcvbulkpipe(hif_dev->udev,
						 USB_REG_IN_PIPE),
				 nskb->data, MAX_REG_IN_BUF_SIZE,
				 ath9k_hif_usb_reg_in_cb, nskb);

		ret = usb_submit_urb(urb, GFP_ATOMIC);
		if (ret) {
			kfree_skb(nskb);
			urb->context = NULL;
		}

		return;
	}

resubmit:
	skb_reset_tail_pointer(skb);
	skb_trim(skb, 0);

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret)
		goto free;

	return;
free:
	kfree_skb(skb);
	urb->context = NULL;
}

static void ath9k_hif_usb_dealloc_tx_urbs(struct hif_device_usb *hif_dev)
{
	struct tx_buf *tx_buf = NULL, *tx_buf_tmp = NULL;

	list_for_each_entry_safe(tx_buf, tx_buf_tmp,
				 &hif_dev->tx.tx_buf, list) {
		usb_kill_urb(tx_buf->urb);
		list_del(&tx_buf->list);
		usb_free_urb(tx_buf->urb);
		kfree(tx_buf->buf);
		kfree(tx_buf);
	}

	list_for_each_entry_safe(tx_buf, tx_buf_tmp,
				 &hif_dev->tx.tx_pending, list) {
		usb_kill_urb(tx_buf->urb);
		list_del(&tx_buf->list);
		usb_free_urb(tx_buf->urb);
		kfree(tx_buf->buf);
		kfree(tx_buf);
	}
}

static int ath9k_hif_usb_alloc_tx_urbs(struct hif_device_usb *hif_dev)
{
	struct tx_buf *tx_buf;
	int i;

	INIT_LIST_HEAD(&hif_dev->tx.tx_buf);
	INIT_LIST_HEAD(&hif_dev->tx.tx_pending);
	spin_lock_init(&hif_dev->tx.tx_lock);
	__skb_queue_head_init(&hif_dev->tx.tx_skb_queue);

	for (i = 0; i < MAX_TX_URB_NUM; i++) {
		tx_buf = kzalloc(sizeof(struct tx_buf), GFP_KERNEL);
		if (!tx_buf)
			goto err;

		tx_buf->buf = kzalloc(MAX_TX_BUF_SIZE, GFP_KERNEL);
		if (!tx_buf->buf)
			goto err;

		tx_buf->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!tx_buf->urb)
			goto err;

		tx_buf->hif_dev = hif_dev;
		__skb_queue_head_init(&tx_buf->skb_queue);

		list_add_tail(&tx_buf->list, &hif_dev->tx.tx_buf);
	}

	hif_dev->tx.tx_buf_cnt = MAX_TX_URB_NUM;

	return 0;
err:
	if (tx_buf) {
		kfree(tx_buf->buf);
		kfree(tx_buf);
	}
	ath9k_hif_usb_dealloc_tx_urbs(hif_dev);
	return -ENOMEM;
}

static void ath9k_hif_usb_dealloc_rx_urbs(struct hif_device_usb *hif_dev)
{
	usb_kill_anchored_urbs(&hif_dev->rx_submitted);
}

static int ath9k_hif_usb_alloc_rx_urbs(struct hif_device_usb *hif_dev)
{
	struct urb *urb = NULL;
	struct sk_buff *skb = NULL;
	int i, ret;

	init_usb_anchor(&hif_dev->rx_submitted);
	spin_lock_init(&hif_dev->rx_lock);

	for (i = 0; i < MAX_RX_URB_NUM; i++) {

		/* Allocate URB */
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (urb == NULL) {
			ret = -ENOMEM;
			goto err_urb;
		}

		/* Allocate buffer */
		skb = alloc_skb(MAX_RX_BUF_SIZE, GFP_KERNEL);
		if (!skb) {
			ret = -ENOMEM;
			goto err_skb;
		}

		usb_fill_bulk_urb(urb, hif_dev->udev,
				  usb_rcvbulkpipe(hif_dev->udev,
						  USB_WLAN_RX_PIPE),
				  skb->data, MAX_RX_BUF_SIZE,
				  ath9k_hif_usb_rx_cb, skb);

		/* Anchor URB */
		usb_anchor_urb(urb, &hif_dev->rx_submitted);

		/* Submit URB */
		ret = usb_submit_urb(urb, GFP_KERNEL);
		if (ret) {
			usb_unanchor_urb(urb);
			goto err_submit;
		}

		/*
		 * Drop reference count.
		 * This ensures that the URB is freed when killing them.
		 */
		usb_free_urb(urb);
	}

	return 0;

err_submit:
	kfree_skb(skb);
err_skb:
	usb_free_urb(urb);
err_urb:
	ath9k_hif_usb_dealloc_rx_urbs(hif_dev);
	return ret;
}

static void ath9k_hif_usb_dealloc_reg_in_urb(struct hif_device_usb *hif_dev)
{
	if (hif_dev->reg_in_urb) {
		usb_kill_urb(hif_dev->reg_in_urb);
		if (hif_dev->reg_in_urb->context)
			kfree_skb((void *)hif_dev->reg_in_urb->context);
		usb_free_urb(hif_dev->reg_in_urb);
		hif_dev->reg_in_urb = NULL;
	}
}

static int ath9k_hif_usb_alloc_reg_in_urb(struct hif_device_usb *hif_dev)
{
	struct sk_buff *skb;

	hif_dev->reg_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (hif_dev->reg_in_urb == NULL)
		return -ENOMEM;

	skb = alloc_skb(MAX_REG_IN_BUF_SIZE, GFP_KERNEL);
	if (!skb)
		goto err;

	usb_fill_bulk_urb(hif_dev->reg_in_urb, hif_dev->udev,
			 usb_rcvbulkpipe(hif_dev->udev,
					 USB_REG_IN_PIPE),
			 skb->data, MAX_REG_IN_BUF_SIZE,
			 ath9k_hif_usb_reg_in_cb, skb);

	if (usb_submit_urb(hif_dev->reg_in_urb, GFP_KERNEL) != 0)
		goto err;

	return 0;

err:
	ath9k_hif_usb_dealloc_reg_in_urb(hif_dev);
	return -ENOMEM;
}

static int ath9k_hif_usb_alloc_urbs(struct hif_device_usb *hif_dev)
{
	/* Register Write */
	init_usb_anchor(&hif_dev->regout_submitted);

	/* TX */
	if (ath9k_hif_usb_alloc_tx_urbs(hif_dev) < 0)
		goto err;

	/* RX */
	if (ath9k_hif_usb_alloc_rx_urbs(hif_dev) < 0)
		goto err_rx;

	/* Register Read */
	if (ath9k_hif_usb_alloc_reg_in_urb(hif_dev) < 0)
		goto err_reg;

	return 0;
err_reg:
	ath9k_hif_usb_dealloc_rx_urbs(hif_dev);
err_rx:
	ath9k_hif_usb_dealloc_tx_urbs(hif_dev);
err:
	return -ENOMEM;
}

static void ath9k_hif_usb_dealloc_urbs(struct hif_device_usb *hif_dev)
{
	usb_kill_anchored_urbs(&hif_dev->regout_submitted);
	ath9k_hif_usb_dealloc_reg_in_urb(hif_dev);
	ath9k_hif_usb_dealloc_tx_urbs(hif_dev);
	ath9k_hif_usb_dealloc_rx_urbs(hif_dev);
}

static int ath9k_hif_usb_download_fw(struct hif_device_usb *hif_dev,
				     u32 drv_info)
{
	int transfer, err;
	const void *data = hif_dev->firmware->data;
	size_t len = hif_dev->firmware->size;
	u32 addr = AR9271_FIRMWARE;
	u8 *buf = kzalloc(4096, GFP_KERNEL);
	u32 firm_offset;

	if (!buf)
		return -ENOMEM;

	while (len) {
		transfer = min_t(int, len, 4096);
		memcpy(buf, data, transfer);

		err = usb_control_msg(hif_dev->udev,
				      usb_sndctrlpipe(hif_dev->udev, 0),
				      FIRMWARE_DOWNLOAD, 0x40 | USB_DIR_OUT,
				      addr >> 8, 0, buf, transfer, HZ);
		if (err < 0) {
			kfree(buf);
			return err;
		}

		len -= transfer;
		data += transfer;
		addr += transfer;
	}
	kfree(buf);

	if (drv_info & AR7010_DEVICE)
		firm_offset = AR7010_FIRMWARE_TEXT;
	else
		firm_offset = AR9271_FIRMWARE_TEXT;

	/*
	 * Issue FW download complete command to firmware.
	 */
	err = usb_control_msg(hif_dev->udev, usb_sndctrlpipe(hif_dev->udev, 0),
			      FIRMWARE_DOWNLOAD_COMP,
			      0x40 | USB_DIR_OUT,
			      firm_offset >> 8, 0, NULL, 0, HZ);
	if (err)
		return -EIO;

	dev_info(&hif_dev->udev->dev, "ath9k_htc: Transferred FW: %s, size: %ld\n",
		 hif_dev->fw_name, (unsigned long) hif_dev->firmware->size);

	return 0;
}

static int ath9k_hif_usb_dev_init(struct hif_device_usb *hif_dev, u32 drv_info)
{
	int ret, idx;
	struct usb_host_interface *alt = &hif_dev->interface->altsetting[0];
	struct usb_endpoint_descriptor *endp;

	/* Request firmware */
	ret = request_firmware(&hif_dev->firmware, hif_dev->fw_name,
			       &hif_dev->udev->dev);
	if (ret) {
		dev_err(&hif_dev->udev->dev,
			"ath9k_htc: Firmware - %s not found\n", hif_dev->fw_name);
		goto err_fw_req;
	}

	/* Download firmware */
	ret = ath9k_hif_usb_download_fw(hif_dev, drv_info);
	if (ret) {
		dev_err(&hif_dev->udev->dev,
			"ath9k_htc: Firmware - %s download failed\n",
			hif_dev->fw_name);
		goto err_fw_download;
	}

	/* On downloading the firmware to the target, the USB descriptor of EP4
	 * is 'patched' to change the type of the endpoint to Bulk. This will
	 * bring down CPU usage during the scan period.
	 */
	for (idx = 0; idx < alt->desc.bNumEndpoints; idx++) {
		endp = &alt->endpoint[idx].desc;
		if ((endp->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				== USB_ENDPOINT_XFER_INT) {
			endp->bmAttributes &= ~USB_ENDPOINT_XFERTYPE_MASK;
			endp->bmAttributes |= USB_ENDPOINT_XFER_BULK;
			endp->bInterval = 0;
		}
	}

	/* Alloc URBs */
	ret = ath9k_hif_usb_alloc_urbs(hif_dev);
	if (ret) {
		dev_err(&hif_dev->udev->dev,
			"ath9k_htc: Unable to allocate URBs\n");
		goto err_urb;
	}

	return 0;

err_fw_download:
	ath9k_hif_usb_dealloc_urbs(hif_dev);
err_urb:
	release_firmware(hif_dev->firmware);
err_fw_req:
	hif_dev->firmware = NULL;
	return ret;
}

static void ath9k_hif_usb_dev_deinit(struct hif_device_usb *hif_dev)
{
	ath9k_hif_usb_dealloc_urbs(hif_dev);
	if (hif_dev->firmware)
		release_firmware(hif_dev->firmware);
}

static int ath9k_hif_usb_probe(struct usb_interface *interface,
			       const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct hif_device_usb *hif_dev;
	int ret = 0;

	hif_dev = kzalloc(sizeof(struct hif_device_usb), GFP_KERNEL);
	if (!hif_dev) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	usb_get_dev(udev);
	hif_dev->udev = udev;
	hif_dev->interface = interface;
	hif_dev->device_id = id->idProduct;
#ifdef CONFIG_PM
	udev->reset_resume = 1;
#endif
	usb_set_intfdata(interface, hif_dev);

	hif_dev->htc_handle = ath9k_htc_hw_alloc(hif_dev, &hif_usb,
						 &hif_dev->udev->dev);
	if (hif_dev->htc_handle == NULL) {
		ret = -ENOMEM;
		goto err_htc_hw_alloc;
	}

	/* Find out which firmware to load */

	if (id->driver_info & AR7010_DEVICE)
		if (le16_to_cpu(udev->descriptor.bcdDevice) == 0x0202)
			hif_dev->fw_name = FIRMWARE_AR7010_1_1;
		else
			hif_dev->fw_name = FIRMWARE_AR7010;
	else
		hif_dev->fw_name = FIRMWARE_AR9271;

	ret = ath9k_hif_usb_dev_init(hif_dev, id->driver_info);
	if (ret) {
		ret = -EINVAL;
		goto err_hif_init_usb;
	}

	ret = ath9k_htc_hw_init(hif_dev->htc_handle,
				&hif_dev->udev->dev, hif_dev->device_id,
				hif_dev->udev->product, id->driver_info);
	if (ret) {
		ret = -EINVAL;
		goto err_htc_hw_init;
	}

	dev_info(&hif_dev->udev->dev, "ath9k_htc: USB layer initialized\n");

	return 0;

err_htc_hw_init:
	ath9k_hif_usb_dev_deinit(hif_dev);
err_hif_init_usb:
	ath9k_htc_hw_free(hif_dev->htc_handle);
err_htc_hw_alloc:
	usb_set_intfdata(interface, NULL);
	kfree(hif_dev);
	usb_put_dev(udev);
err_alloc:
	return ret;
}

static void ath9k_hif_usb_reboot(struct usb_device *udev)
{
	u32 reboot_cmd = 0xffffffff;
	void *buf;
	int ret;

	buf = kmemdup(&reboot_cmd, 4, GFP_KERNEL);
	if (!buf)
		return;

	ret = usb_bulk_msg(udev, usb_sndbulkpipe(udev, USB_REG_OUT_PIPE),
			   buf, 4, NULL, HZ);
	if (ret)
		dev_err(&udev->dev, "ath9k_htc: USB reboot failed\n");

	kfree(buf);
}

static void ath9k_hif_usb_disconnect(struct usb_interface *interface)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct hif_device_usb *hif_dev = usb_get_intfdata(interface);

	if (hif_dev) {
		ath9k_htc_hw_deinit(hif_dev->htc_handle,
		    (udev->state == USB_STATE_NOTATTACHED) ? true : false);
		ath9k_htc_hw_free(hif_dev->htc_handle);
		ath9k_hif_usb_dev_deinit(hif_dev);
		usb_set_intfdata(interface, NULL);
	}

	if (hif_dev->flags & HIF_USB_START)
		ath9k_hif_usb_reboot(udev);

	kfree(hif_dev);
	dev_info(&udev->dev, "ath9k_htc: USB layer deinitialized\n");
	usb_put_dev(udev);
}

#ifdef CONFIG_PM
static int ath9k_hif_usb_suspend(struct usb_interface *interface,
				 pm_message_t message)
{
	struct hif_device_usb *hif_dev = usb_get_intfdata(interface);

	ath9k_hif_usb_dealloc_urbs(hif_dev);

	return 0;
}

static int ath9k_hif_usb_resume(struct usb_interface *interface)
{
	struct hif_device_usb *hif_dev = usb_get_intfdata(interface);
	struct htc_target *htc_handle = hif_dev->htc_handle;
	int ret;

	ret = ath9k_hif_usb_alloc_urbs(hif_dev);
	if (ret)
		return ret;

	if (hif_dev->firmware) {
		ret = ath9k_hif_usb_download_fw(hif_dev,
				htc_handle->drv_priv->ah->common.driver_info);
		if (ret)
			goto fail_resume;
	} else {
		ath9k_hif_usb_dealloc_urbs(hif_dev);
		return -EIO;
	}

	mdelay(100);

	ret = ath9k_htc_resume(htc_handle);

	if (ret)
		goto fail_resume;

	return 0;

fail_resume:
	ath9k_hif_usb_dealloc_urbs(hif_dev);

	return ret;
}
#endif

static struct usb_driver ath9k_hif_usb_driver = {
	.name = "ath9k_hif_usb",
	.probe = ath9k_hif_usb_probe,
	.disconnect = ath9k_hif_usb_disconnect,
#ifdef CONFIG_PM
	.suspend = ath9k_hif_usb_suspend,
	.resume = ath9k_hif_usb_resume,
	.reset_resume = ath9k_hif_usb_resume,
#endif
	.id_table = ath9k_hif_usb_ids,
	.soft_unbind = 1,
};

int ath9k_hif_usb_init(void)
{
	return usb_register(&ath9k_hif_usb_driver);
}

void ath9k_hif_usb_exit(void)
{
	usb_deregister(&ath9k_hif_usb_driver);
}
