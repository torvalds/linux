/*
 * Copyright (c) 2017 Redpine Signals Inc.
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
#include <linux/module.h>
#include <linux/kernel.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <linux/unaligned.h>
#include <net/rsi_91x.h>

#define RSI_DMA_ALIGN	8
#define RSI_FRAME_DESC_SIZE	16
#define RSI_HEADROOM_FOR_BT_HAL	(RSI_FRAME_DESC_SIZE + RSI_DMA_ALIGN)

struct rsi_hci_adapter {
	void *priv;
	struct rsi_proto_ops *proto_ops;
	struct hci_dev *hdev;
};

static int rsi_hci_open(struct hci_dev *hdev)
{
	return 0;
}

static int rsi_hci_close(struct hci_dev *hdev)
{
	return 0;
}

static int rsi_hci_flush(struct hci_dev *hdev)
{
	return 0;
}

static int rsi_hci_send_pkt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct rsi_hci_adapter *h_adapter = hci_get_drvdata(hdev);
	struct sk_buff *new_skb = NULL;

	switch (hci_skb_pkt_type(skb)) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;
	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;
	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		break;
	}

	if (skb_headroom(skb) < RSI_HEADROOM_FOR_BT_HAL) {
		/* Insufficient skb headroom - allocate a new skb */
		new_skb = skb_realloc_headroom(skb, RSI_HEADROOM_FOR_BT_HAL);
		if (unlikely(!new_skb))
			return -ENOMEM;
		bt_cb(new_skb)->pkt_type = hci_skb_pkt_type(skb);
		kfree_skb(skb);
		skb = new_skb;
		if (!IS_ALIGNED((unsigned long)skb->data, RSI_DMA_ALIGN)) {
			u8 *skb_data = skb->data;
			int skb_len = skb->len;

			skb_push(skb, RSI_DMA_ALIGN);
			skb_pull(skb, PTR_ALIGN(skb->data,
						RSI_DMA_ALIGN) - skb->data);
			memmove(skb->data, skb_data, skb_len);
			skb_trim(skb, skb_len);
		}
	}

	return h_adapter->proto_ops->coex_send_pkt(h_adapter->priv, skb,
						   RSI_BT_Q);
}

static int rsi_hci_recv_pkt(void *priv, const u8 *pkt)
{
	struct rsi_hci_adapter *h_adapter = priv;
	struct hci_dev *hdev = h_adapter->hdev;
	struct sk_buff *skb;
	int pkt_len = get_unaligned_le16(pkt) & 0x0fff;

	skb = dev_alloc_skb(pkt_len);
	if (!skb)
		return -ENOMEM;

	memcpy(skb->data, pkt + RSI_FRAME_DESC_SIZE, pkt_len);
	skb_put(skb, pkt_len);
	h_adapter->hdev->stat.byte_rx += skb->len;

	hci_skb_pkt_type(skb) = pkt[14];

	return hci_recv_frame(hdev, skb);
}

static int rsi_hci_attach(void *priv, struct rsi_proto_ops *ops)
{
	struct rsi_hci_adapter *h_adapter = NULL;
	struct hci_dev *hdev;
	int err = 0;

	h_adapter = kzalloc(sizeof(*h_adapter), GFP_KERNEL);
	if (!h_adapter)
		return -ENOMEM;

	h_adapter->priv = priv;
	ops->set_bt_context(priv, h_adapter);
	h_adapter->proto_ops = ops;

	hdev = hci_alloc_dev();
	if (!hdev) {
		BT_ERR("Failed to alloc HCI device");
		goto err;
	}

	h_adapter->hdev = hdev;

	if (ops->get_host_intf(priv) == RSI_HOST_INTF_SDIO)
		hdev->bus = HCI_SDIO;
	else
		hdev->bus = HCI_USB;

	hci_set_drvdata(hdev, h_adapter);
	hdev->open = rsi_hci_open;
	hdev->close = rsi_hci_close;
	hdev->flush = rsi_hci_flush;
	hdev->send = rsi_hci_send_pkt;

	err = hci_register_dev(hdev);
	if (err < 0) {
		BT_ERR("HCI registration failed with errcode %d", err);
		hci_free_dev(hdev);
		goto err;
	}

	return 0;
err:
	h_adapter->hdev = NULL;
	kfree(h_adapter);
	return -EINVAL;
}

static void rsi_hci_detach(void *priv)
{
	struct rsi_hci_adapter *h_adapter = priv;
	struct hci_dev *hdev;

	if (!h_adapter)
		return;

	hdev = h_adapter->hdev;
	if (hdev) {
		hci_unregister_dev(hdev);
		hci_free_dev(hdev);
		h_adapter->hdev = NULL;
	}

	kfree(h_adapter);
}

const struct rsi_mod_ops rsi_bt_ops = {
	.attach	= rsi_hci_attach,
	.detach	= rsi_hci_detach,
	.recv_pkt = rsi_hci_recv_pkt,
};
EXPORT_SYMBOL(rsi_bt_ops);

static int rsi_91x_bt_module_init(void)
{
	return 0;
}

static void rsi_91x_bt_module_exit(void)
{
	return;
}

module_init(rsi_91x_bt_module_init);
module_exit(rsi_91x_bt_module_exit);
MODULE_AUTHOR("Redpine Signals Inc");
MODULE_DESCRIPTION("RSI BT driver");
MODULE_LICENSE("Dual BSD/GPL");
