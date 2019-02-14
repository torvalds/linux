// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 MediaTek Inc.

/*
 * Bluetooth support for MediaTek serial devices
 *
 * Author: Sean Wang <sean.wang@mediatek.com>
 *
 */

#include <asm/unaligned.h>
#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/serdev.h>
#include <linux/skbuff.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "h4_recv.h"

#define VERSION "0.1"

#define FIRMWARE_MT7622		"mediatek/mt7622pr2h.bin"

#define MTK_STP_TLR_SIZE	2

#define BTMTKUART_TX_STATE_ACTIVE	1
#define BTMTKUART_TX_STATE_WAKEUP	2
#define BTMTKUART_TX_WAIT_VND_EVT	3

enum {
	MTK_WMT_PATCH_DWNLD = 0x1,
	MTK_WMT_FUNC_CTRL = 0x6,
	MTK_WMT_RST = 0x7
};

struct mtk_stp_hdr {
	u8	prefix;
	__be16	dlen;
	u8	cs;
} __packed;

struct mtk_wmt_hdr {
	u8	dir;
	u8	op;
	__le16	dlen;
	u8	flag;
} __packed;

struct mtk_hci_wmt_cmd {
	struct mtk_wmt_hdr hdr;
	u8 data[256];
} __packed;

struct btmtkuart_dev {
	struct hci_dev *hdev;
	struct serdev_device *serdev;
	struct clk *clk;

	struct work_struct tx_work;
	unsigned long tx_state;
	struct sk_buff_head txq;

	struct sk_buff *rx_skb;

	u8	stp_pad[6];
	u8	stp_cursor;
	u16	stp_dlen;
};

static int mtk_hci_wmt_sync(struct hci_dev *hdev, u8 op, u8 flag, u16 plen,
			    const void *param)
{
	struct btmtkuart_dev *bdev = hci_get_drvdata(hdev);
	struct mtk_hci_wmt_cmd wc;
	struct mtk_wmt_hdr *hdr;
	u32 hlen;
	int err;

	hlen = sizeof(*hdr) + plen;
	if (hlen > 255)
		return -EINVAL;

	hdr = (struct mtk_wmt_hdr *)&wc;
	hdr->dir = 1;
	hdr->op = op;
	hdr->dlen = cpu_to_le16(plen + 1);
	hdr->flag = flag;
	memcpy(wc.data, param, plen);

	set_bit(BTMTKUART_TX_WAIT_VND_EVT, &bdev->tx_state);

	err = __hci_cmd_send(hdev, 0xfc6f, hlen, &wc);
	if (err < 0) {
		clear_bit(BTMTKUART_TX_WAIT_VND_EVT, &bdev->tx_state);
		return err;
	}

	/* The vendor specific WMT commands are all answered by a vendor
	 * specific event and will not have the Command Status or Command
	 * Complete as with usual HCI command flow control.
	 *
	 * After sending the command, wait for BTMTKUART_TX_WAIT_VND_EVT
	 * state to be cleared. The driver speicfic event receive routine
	 * will clear that state and with that indicate completion of the
	 * WMT command.
	 */
	err = wait_on_bit_timeout(&bdev->tx_state, BTMTKUART_TX_WAIT_VND_EVT,
				  TASK_INTERRUPTIBLE, HCI_INIT_TIMEOUT);
	if (err == -EINTR) {
		bt_dev_err(hdev, "Execution of wmt command interrupted");
		clear_bit(BTMTKUART_TX_WAIT_VND_EVT, &bdev->tx_state);
		return err;
	}

	if (err) {
		bt_dev_err(hdev, "Execution of wmt command timed out");
		clear_bit(BTMTKUART_TX_WAIT_VND_EVT, &bdev->tx_state);
		return -ETIMEDOUT;
	}

	return 0;
}

static int mtk_setup_fw(struct hci_dev *hdev)
{
	const struct firmware *fw;
	const u8 *fw_ptr;
	size_t fw_size;
	int err, dlen;
	u8 flag;

	err = request_firmware(&fw, FIRMWARE_MT7622, &hdev->dev);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to load firmware file (%d)", err);
		return err;
	}

	fw_ptr = fw->data;
	fw_size = fw->size;

	/* The size of patch header is 30 bytes, should be skip */
	if (fw_size < 30) {
		err = -EINVAL;
		goto free_fw;
	}

	fw_size -= 30;
	fw_ptr += 30;
	flag = 1;

	while (fw_size > 0) {
		dlen = min_t(int, 250, fw_size);

		/* Tell device the position in sequence */
		if (fw_size - dlen <= 0)
			flag = 3;
		else if (fw_size < fw->size - 30)
			flag = 2;

		err = mtk_hci_wmt_sync(hdev, MTK_WMT_PATCH_DWNLD, flag, dlen,
				       fw_ptr);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to send wmt patch dwnld (%d)",
				   err);
			break;
		}

		fw_size -= dlen;
		fw_ptr += dlen;
	}

free_fw:
	release_firmware(fw);
	return err;
}

static int btmtkuart_recv_event(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtkuart_dev *bdev = hci_get_drvdata(hdev);
	struct hci_event_hdr *hdr = (void *)skb->data;
	int err;

	/* Fix up the vendor event id with 0xff for vendor specific instead
	 * of 0xe4 so that event send via monitoring socket can be parsed
	 * properly.
	 */
	if (hdr->evt == 0xe4)
		hdr->evt = HCI_EV_VENDOR;

	err = hci_recv_frame(hdev, skb);

	if (hdr->evt == HCI_EV_VENDOR) {
		if (test_and_clear_bit(BTMTKUART_TX_WAIT_VND_EVT,
				       &bdev->tx_state)) {
			/* Barrier to sync with other CPUs */
			smp_mb__after_atomic();
			wake_up_bit(&bdev->tx_state, BTMTKUART_TX_WAIT_VND_EVT);
		}
	}

	return err;
}

static const struct h4_recv_pkt mtk_recv_pkts[] = {
	{ H4_RECV_ACL,      .recv = hci_recv_frame },
	{ H4_RECV_SCO,      .recv = hci_recv_frame },
	{ H4_RECV_EVENT,    .recv = btmtkuart_recv_event },
};

static void btmtkuart_tx_work(struct work_struct *work)
{
	struct btmtkuart_dev *bdev = container_of(work, struct btmtkuart_dev,
						   tx_work);
	struct serdev_device *serdev = bdev->serdev;
	struct hci_dev *hdev = bdev->hdev;

	while (1) {
		clear_bit(BTMTKUART_TX_STATE_WAKEUP, &bdev->tx_state);

		while (1) {
			struct sk_buff *skb = skb_dequeue(&bdev->txq);
			int len;

			if (!skb)
				break;

			len = serdev_device_write_buf(serdev, skb->data,
						      skb->len);
			hdev->stat.byte_tx += len;

			skb_pull(skb, len);
			if (skb->len > 0) {
				skb_queue_head(&bdev->txq, skb);
				break;
			}

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

			kfree_skb(skb);
		}

		if (!test_bit(BTMTKUART_TX_STATE_WAKEUP, &bdev->tx_state))
			break;
	}

	clear_bit(BTMTKUART_TX_STATE_ACTIVE, &bdev->tx_state);
}

static void btmtkuart_tx_wakeup(struct btmtkuart_dev *bdev)
{
	if (test_and_set_bit(BTMTKUART_TX_STATE_ACTIVE, &bdev->tx_state))
		set_bit(BTMTKUART_TX_STATE_WAKEUP, &bdev->tx_state);

	schedule_work(&bdev->tx_work);
}

static const unsigned char *
mtk_stp_split(struct btmtkuart_dev *bdev, const unsigned char *data, int count,
	      int *sz_h4)
{
	struct mtk_stp_hdr *shdr;

	/* The cursor is reset when all the data of STP is consumed out */
	if (!bdev->stp_dlen && bdev->stp_cursor >= 6)
		bdev->stp_cursor = 0;

	/* Filling pad until all STP info is obtained */
	while (bdev->stp_cursor < 6 && count > 0) {
		bdev->stp_pad[bdev->stp_cursor] = *data;
		bdev->stp_cursor++;
		data++;
		count--;
	}

	/* Retrieve STP info and have a sanity check */
	if (!bdev->stp_dlen && bdev->stp_cursor >= 6) {
		shdr = (struct mtk_stp_hdr *)&bdev->stp_pad[2];
		bdev->stp_dlen = be16_to_cpu(shdr->dlen) & 0x0fff;

		/* Resync STP when unexpected data is being read */
		if (shdr->prefix != 0x80 || bdev->stp_dlen > 2048) {
			bt_dev_err(bdev->hdev, "stp format unexpect (%d, %d)",
				   shdr->prefix, bdev->stp_dlen);
			bdev->stp_cursor = 2;
			bdev->stp_dlen = 0;
		}
	}

	/* Directly quit when there's no data found for H4 can process */
	if (count <= 0)
		return NULL;

	/* Tranlate to how much the size of data H4 can handle so far */
	*sz_h4 = min_t(int, count, bdev->stp_dlen);

	/* Update the remaining size of STP packet */
	bdev->stp_dlen -= *sz_h4;

	/* Data points to STP payload which can be handled by H4 */
	return data;
}

static int btmtkuart_recv(struct hci_dev *hdev, const u8 *data, size_t count)
{
	struct btmtkuart_dev *bdev = hci_get_drvdata(hdev);
	const unsigned char *p_left = data, *p_h4;
	int sz_left = count, sz_h4, adv;
	int err;

	while (sz_left > 0) {
		/*  The serial data received from MT7622 BT controller is
		 *  at all time padded around with the STP header and tailer.
		 *
		 *  A full STP packet is looking like
		 *   -----------------------------------
		 *  | STP header  |  H:4   | STP tailer |
		 *   -----------------------------------
		 *  but it doesn't guarantee to contain a full H:4 packet which
		 *  means that it's possible for multiple STP packets forms a
		 *  full H:4 packet that means extra STP header + length doesn't
		 *  indicate a full H:4 frame, things can fragment. Whose length
		 *  recorded in STP header just shows up the most length the
		 *  H:4 engine can handle currently.
		 */

		p_h4 = mtk_stp_split(bdev, p_left, sz_left, &sz_h4);
		if (!p_h4)
			break;

		adv = p_h4 - p_left;
		sz_left -= adv;
		p_left += adv;

		bdev->rx_skb = h4_recv_buf(bdev->hdev, bdev->rx_skb, p_h4,
					   sz_h4, mtk_recv_pkts,
					   ARRAY_SIZE(mtk_recv_pkts));
		if (IS_ERR(bdev->rx_skb)) {
			err = PTR_ERR(bdev->rx_skb);
			bt_dev_err(bdev->hdev,
				   "Frame reassembly failed (%d)", err);
			bdev->rx_skb = NULL;
			return err;
		}

		sz_left -= sz_h4;
		p_left += sz_h4;
	}

	return 0;
}

static int btmtkuart_receive_buf(struct serdev_device *serdev, const u8 *data,
				 size_t count)
{
	struct btmtkuart_dev *bdev = serdev_device_get_drvdata(serdev);
	int err;

	err = btmtkuart_recv(bdev->hdev, data, count);
	if (err < 0)
		return err;

	bdev->hdev->stat.byte_rx += count;

	return count;
}

static void btmtkuart_write_wakeup(struct serdev_device *serdev)
{
	struct btmtkuart_dev *bdev = serdev_device_get_drvdata(serdev);

	btmtkuart_tx_wakeup(bdev);
}

static const struct serdev_device_ops btmtkuart_client_ops = {
	.receive_buf = btmtkuart_receive_buf,
	.write_wakeup = btmtkuart_write_wakeup,
};

static int btmtkuart_open(struct hci_dev *hdev)
{
	struct btmtkuart_dev *bdev = hci_get_drvdata(hdev);
	struct device *dev;
	int err;

	err = serdev_device_open(bdev->serdev);
	if (err) {
		bt_dev_err(hdev, "Unable to open UART device %s",
			   dev_name(&bdev->serdev->dev));
		goto err_open;
	}

	bdev->stp_cursor = 2;
	bdev->stp_dlen = 0;

	dev = &bdev->serdev->dev;

	/* Enable the power domain and clock the device requires */
	pm_runtime_enable(dev);
	err = pm_runtime_get_sync(dev);
	if (err < 0) {
		pm_runtime_put_noidle(dev);
		goto err_disable_rpm;
	}

	err = clk_prepare_enable(bdev->clk);
	if (err < 0)
		goto err_put_rpm;

	return 0;

err_put_rpm:
	pm_runtime_put_sync(dev);
err_disable_rpm:
	pm_runtime_disable(dev);
err_open:
	return err;
}

static int btmtkuart_close(struct hci_dev *hdev)
{
	struct btmtkuart_dev *bdev = hci_get_drvdata(hdev);
	struct device *dev = &bdev->serdev->dev;

	/* Shutdown the clock and power domain the device requires */
	clk_disable_unprepare(bdev->clk);
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	serdev_device_close(bdev->serdev);

	return 0;
}

static int btmtkuart_flush(struct hci_dev *hdev)
{
	struct btmtkuart_dev *bdev = hci_get_drvdata(hdev);

	/* Flush any pending characters */
	serdev_device_write_flush(bdev->serdev);
	skb_queue_purge(&bdev->txq);

	cancel_work_sync(&bdev->tx_work);

	kfree_skb(bdev->rx_skb);
	bdev->rx_skb = NULL;

	bdev->stp_cursor = 2;
	bdev->stp_dlen = 0;

	return 0;
}

static int btmtkuart_setup(struct hci_dev *hdev)
{
	u8 param = 0x1;
	int err = 0;

	/* Setup a firmware which the device definitely requires */
	err = mtk_setup_fw(hdev);
	if (err < 0)
		return err;

	/* Activate function the firmware providing to */
	err = mtk_hci_wmt_sync(hdev, MTK_WMT_RST, 0x4, 0, 0);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt rst (%d)", err);
		return err;
	}

	/* Enable Bluetooth protocol */
	err = mtk_hci_wmt_sync(hdev, MTK_WMT_FUNC_CTRL, 0x0, sizeof(param),
			       &param);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
		return err;
	}

	return 0;
}

static int btmtkuart_shutdown(struct hci_dev *hdev)
{
	u8 param = 0x0;
	int err;

	/* Disable the device */
	err = mtk_hci_wmt_sync(hdev, MTK_WMT_FUNC_CTRL, 0x0, sizeof(param),
			       &param);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
		return err;
	}

	return 0;
}

static int btmtkuart_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtkuart_dev *bdev = hci_get_drvdata(hdev);
	struct mtk_stp_hdr *shdr;
	int err, dlen, type = 0;

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);

	/* Make sure that there is enough rooms for STP header and trailer */
	if (unlikely(skb_headroom(skb) < sizeof(*shdr)) ||
	    (skb_tailroom(skb) < MTK_STP_TLR_SIZE)) {
		err = pskb_expand_head(skb, sizeof(*shdr), MTK_STP_TLR_SIZE,
				       GFP_ATOMIC);
		if (err < 0)
			return err;
	}

	/* Add the STP header */
	dlen = skb->len;
	shdr = skb_push(skb, sizeof(*shdr));
	shdr->prefix = 0x80;
	shdr->dlen = cpu_to_be16((dlen & 0x0fff) | (type << 12));
	shdr->cs = 0;		/* MT7622 doesn't care about checksum value */

	/* Add the STP trailer */
	skb_put_zero(skb, MTK_STP_TLR_SIZE);

	skb_queue_tail(&bdev->txq, skb);

	btmtkuart_tx_wakeup(bdev);
	return 0;
}

static int btmtkuart_probe(struct serdev_device *serdev)
{
	struct btmtkuart_dev *bdev;
	struct hci_dev *hdev;

	bdev = devm_kzalloc(&serdev->dev, sizeof(*bdev), GFP_KERNEL);
	if (!bdev)
		return -ENOMEM;

	bdev->clk = devm_clk_get(&serdev->dev, "ref");
	if (IS_ERR(bdev->clk))
		return PTR_ERR(bdev->clk);

	bdev->serdev = serdev;
	serdev_device_set_drvdata(serdev, bdev);

	serdev_device_set_client_ops(serdev, &btmtkuart_client_ops);

	INIT_WORK(&bdev->tx_work, btmtkuart_tx_work);
	skb_queue_head_init(&bdev->txq);

	/* Initialize and register HCI device */
	hdev = hci_alloc_dev();
	if (!hdev) {
		dev_err(&serdev->dev, "Can't allocate HCI device\n");
		return -ENOMEM;
	}

	bdev->hdev = hdev;

	hdev->bus = HCI_UART;
	hci_set_drvdata(hdev, bdev);

	hdev->open     = btmtkuart_open;
	hdev->close    = btmtkuart_close;
	hdev->flush    = btmtkuart_flush;
	hdev->setup    = btmtkuart_setup;
	hdev->shutdown = btmtkuart_shutdown;
	hdev->send     = btmtkuart_send_frame;
	SET_HCIDEV_DEV(hdev, &serdev->dev);

	hdev->manufacturer = 70;
	set_bit(HCI_QUIRK_NON_PERSISTENT_SETUP, &hdev->quirks);

	if (hci_register_dev(hdev) < 0) {
		dev_err(&serdev->dev, "Can't register HCI device\n");
		hci_free_dev(hdev);
		return -ENODEV;
	}

	return 0;
}

static void btmtkuart_remove(struct serdev_device *serdev)
{
	struct btmtkuart_dev *bdev = serdev_device_get_drvdata(serdev);
	struct hci_dev *hdev = bdev->hdev;

	hci_unregister_dev(hdev);
	hci_free_dev(hdev);
}

#ifdef CONFIG_OF
static const struct of_device_id mtk_of_match_table[] = {
	{ .compatible = "mediatek,mt7622-bluetooth"},
	{ }
};
MODULE_DEVICE_TABLE(of, mtk_of_match_table);
#endif

static struct serdev_device_driver btmtkuart_driver = {
	.probe = btmtkuart_probe,
	.remove = btmtkuart_remove,
	.driver = {
		.name = "btmtkuart",
		.of_match_table = of_match_ptr(mtk_of_match_table),
	},
};

module_serdev_device_driver(btmtkuart_driver);

MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_DESCRIPTION("MediaTek Bluetooth Serial driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(FIRMWARE_MT7622);
