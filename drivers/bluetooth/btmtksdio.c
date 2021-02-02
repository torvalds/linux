// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 MediaTek Inc.

/*
 * Bluetooth support for MediaTek SDIO devices
 *
 * This file is written based on btsdio.c and btmtkuart.c.
 *
 * Author: Sean Wang <sean.wang@mediatek.com>
 *
 */

#include <asm/unaligned.h>
#include <linux/atomic.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/skbuff.h>

#include <linux/mmc/host.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "h4_recv.h"

#define VERSION "0.1"

#define FIRMWARE_MT7663		"mediatek/mt7663pr2h.bin"
#define FIRMWARE_MT7668		"mediatek/mt7668pr2h.bin"

#define MTKBTSDIO_AUTOSUSPEND_DELAY	8000

static bool enable_autosuspend;

struct btmtksdio_data {
	const char *fwname;
};

static const struct btmtksdio_data mt7663_data = {
	.fwname = FIRMWARE_MT7663,
};

static const struct btmtksdio_data mt7668_data = {
	.fwname = FIRMWARE_MT7668,
};

static const struct sdio_device_id btmtksdio_table[] = {
	{SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, SDIO_DEVICE_ID_MEDIATEK_MT7663),
	 .driver_data = (kernel_ulong_t)&mt7663_data },
	{SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, SDIO_DEVICE_ID_MEDIATEK_MT7668),
	 .driver_data = (kernel_ulong_t)&mt7668_data },
	{ }	/* Terminating entry */
};
MODULE_DEVICE_TABLE(sdio, btmtksdio_table);

#define MTK_REG_CHLPCR		0x4	/* W1S */
#define C_INT_EN_SET		BIT(0)
#define C_INT_EN_CLR		BIT(1)
#define C_FW_OWN_REQ_SET	BIT(8)  /* For write */
#define C_COM_DRV_OWN		BIT(8)  /* For read */
#define C_FW_OWN_REQ_CLR	BIT(9)

#define MTK_REG_CSDIOCSR	0x8
#define SDIO_RE_INIT_EN		BIT(0)
#define SDIO_INT_CTL		BIT(2)

#define MTK_REG_CHCR		0xc
#define C_INT_CLR_CTRL		BIT(1)

/* CHISR have the same bits field definition with CHIER */
#define MTK_REG_CHISR		0x10
#define MTK_REG_CHIER		0x14
#define FW_OWN_BACK_INT		BIT(0)
#define RX_DONE_INT		BIT(1)
#define TX_EMPTY		BIT(2)
#define TX_FIFO_OVERFLOW	BIT(8)
#define RX_PKT_LEN		GENMASK(31, 16)

#define MTK_REG_CTDR		0x18

#define MTK_REG_CRDR		0x1c

#define MTK_SDIO_BLOCK_SIZE	256

#define BTMTKSDIO_TX_WAIT_VND_EVT	1

enum {
	MTK_WMT_PATCH_DWNLD = 0x1,
	MTK_WMT_TEST = 0x2,
	MTK_WMT_WAKEUP = 0x3,
	MTK_WMT_HIF = 0x4,
	MTK_WMT_FUNC_CTRL = 0x6,
	MTK_WMT_RST = 0x7,
	MTK_WMT_SEMAPHORE = 0x17,
};

enum {
	BTMTK_WMT_INVALID,
	BTMTK_WMT_PATCH_UNDONE,
	BTMTK_WMT_PATCH_DONE,
	BTMTK_WMT_ON_UNDONE,
	BTMTK_WMT_ON_DONE,
	BTMTK_WMT_ON_PROGRESS,
};

struct mtkbtsdio_hdr {
	__le16	len;
	__le16	reserved;
	u8	bt_type;
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

struct btmtk_hci_wmt_evt {
	struct hci_event_hdr hhdr;
	struct mtk_wmt_hdr whdr;
} __packed;

struct btmtk_hci_wmt_evt_funcc {
	struct btmtk_hci_wmt_evt hwhdr;
	__be16 status;
} __packed;

struct btmtk_tci_sleep {
	u8 mode;
	__le16 duration;
	__le16 host_duration;
	u8 host_wakeup_pin;
	u8 time_compensation;
} __packed;

struct btmtk_hci_wmt_params {
	u8 op;
	u8 flag;
	u16 dlen;
	const void *data;
	u32 *status;
};

struct btmtksdio_dev {
	struct hci_dev *hdev;
	struct sdio_func *func;
	struct device *dev;

	struct work_struct tx_work;
	unsigned long tx_state;
	struct sk_buff_head txq;

	struct sk_buff *evt_skb;

	const struct btmtksdio_data *data;
};

static int mtk_hci_wmt_sync(struct hci_dev *hdev,
			    struct btmtk_hci_wmt_params *wmt_params)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	struct btmtk_hci_wmt_evt_funcc *wmt_evt_funcc;
	u32 hlen, status = BTMTK_WMT_INVALID;
	struct btmtk_hci_wmt_evt *wmt_evt;
	struct mtk_hci_wmt_cmd wc;
	struct mtk_wmt_hdr *hdr;
	int err;

	hlen = sizeof(*hdr) + wmt_params->dlen;
	if (hlen > 255)
		return -EINVAL;

	hdr = (struct mtk_wmt_hdr *)&wc;
	hdr->dir = 1;
	hdr->op = wmt_params->op;
	hdr->dlen = cpu_to_le16(wmt_params->dlen + 1);
	hdr->flag = wmt_params->flag;
	memcpy(wc.data, wmt_params->data, wmt_params->dlen);

	set_bit(BTMTKSDIO_TX_WAIT_VND_EVT, &bdev->tx_state);

	err = __hci_cmd_send(hdev, 0xfc6f, hlen, &wc);
	if (err < 0) {
		clear_bit(BTMTKSDIO_TX_WAIT_VND_EVT, &bdev->tx_state);
		return err;
	}

	/* The vendor specific WMT commands are all answered by a vendor
	 * specific event and will not have the Command Status or Command
	 * Complete as with usual HCI command flow control.
	 *
	 * After sending the command, wait for BTMTKSDIO_TX_WAIT_VND_EVT
	 * state to be cleared. The driver specific event receive routine
	 * will clear that state and with that indicate completion of the
	 * WMT command.
	 */
	err = wait_on_bit_timeout(&bdev->tx_state, BTMTKSDIO_TX_WAIT_VND_EVT,
				  TASK_INTERRUPTIBLE, HCI_INIT_TIMEOUT);
	if (err == -EINTR) {
		bt_dev_err(hdev, "Execution of wmt command interrupted");
		clear_bit(BTMTKSDIO_TX_WAIT_VND_EVT, &bdev->tx_state);
		return err;
	}

	if (err) {
		bt_dev_err(hdev, "Execution of wmt command timed out");
		clear_bit(BTMTKSDIO_TX_WAIT_VND_EVT, &bdev->tx_state);
		return -ETIMEDOUT;
	}

	/* Parse and handle the return WMT event */
	wmt_evt = (struct btmtk_hci_wmt_evt *)bdev->evt_skb->data;
	if (wmt_evt->whdr.op != hdr->op) {
		bt_dev_err(hdev, "Wrong op received %d expected %d",
			   wmt_evt->whdr.op, hdr->op);
		err = -EIO;
		goto err_free_skb;
	}

	switch (wmt_evt->whdr.op) {
	case MTK_WMT_SEMAPHORE:
		if (wmt_evt->whdr.flag == 2)
			status = BTMTK_WMT_PATCH_UNDONE;
		else
			status = BTMTK_WMT_PATCH_DONE;
		break;
	case MTK_WMT_FUNC_CTRL:
		wmt_evt_funcc = (struct btmtk_hci_wmt_evt_funcc *)wmt_evt;
		if (be16_to_cpu(wmt_evt_funcc->status) == 0x404)
			status = BTMTK_WMT_ON_DONE;
		else if (be16_to_cpu(wmt_evt_funcc->status) == 0x420)
			status = BTMTK_WMT_ON_PROGRESS;
		else
			status = BTMTK_WMT_ON_UNDONE;
		break;
	}

	if (wmt_params->status)
		*wmt_params->status = status;

err_free_skb:
	kfree_skb(bdev->evt_skb);
	bdev->evt_skb = NULL;

	return err;
}

static int btmtksdio_tx_packet(struct btmtksdio_dev *bdev,
			       struct sk_buff *skb)
{
	struct mtkbtsdio_hdr *sdio_hdr;
	int err;

	/* Make sure that there are enough rooms for SDIO header */
	if (unlikely(skb_headroom(skb) < sizeof(*sdio_hdr))) {
		err = pskb_expand_head(skb, sizeof(*sdio_hdr), 0,
				       GFP_ATOMIC);
		if (err < 0)
			return err;
	}

	/* Prepend MediaTek SDIO Specific Header */
	skb_push(skb, sizeof(*sdio_hdr));

	sdio_hdr = (void *)skb->data;
	sdio_hdr->len = cpu_to_le16(skb->len);
	sdio_hdr->reserved = cpu_to_le16(0);
	sdio_hdr->bt_type = hci_skb_pkt_type(skb);

	err = sdio_writesb(bdev->func, MTK_REG_CTDR, skb->data,
			   round_up(skb->len, MTK_SDIO_BLOCK_SIZE));
	if (err < 0)
		goto err_skb_pull;

	bdev->hdev->stat.byte_tx += skb->len;

	kfree_skb(skb);

	return 0;

err_skb_pull:
	skb_pull(skb, sizeof(*sdio_hdr));

	return err;
}

static u32 btmtksdio_drv_own_query(struct btmtksdio_dev *bdev)
{
	return sdio_readl(bdev->func, MTK_REG_CHLPCR, NULL);
}

static void btmtksdio_tx_work(struct work_struct *work)
{
	struct btmtksdio_dev *bdev = container_of(work, struct btmtksdio_dev,
						  tx_work);
	struct sk_buff *skb;
	int err;

	pm_runtime_get_sync(bdev->dev);

	sdio_claim_host(bdev->func);

	while ((skb = skb_dequeue(&bdev->txq))) {
		err = btmtksdio_tx_packet(bdev, skb);
		if (err < 0) {
			bdev->hdev->stat.err_tx++;
			skb_queue_head(&bdev->txq, skb);
			break;
		}
	}

	sdio_release_host(bdev->func);

	pm_runtime_mark_last_busy(bdev->dev);
	pm_runtime_put_autosuspend(bdev->dev);
}

static int btmtksdio_recv_event(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	struct hci_event_hdr *hdr = (void *)skb->data;
	int err;

	/* Fix up the vendor event id with 0xff for vendor specific instead
	 * of 0xe4 so that event send via monitoring socket can be parsed
	 * properly.
	 */
	if (hdr->evt == 0xe4)
		hdr->evt = HCI_EV_VENDOR;

	/* When someone waits for the WMT event, the skb is being cloned
	 * and being processed the events from there then.
	 */
	if (test_bit(BTMTKSDIO_TX_WAIT_VND_EVT, &bdev->tx_state)) {
		bdev->evt_skb = skb_clone(skb, GFP_KERNEL);
		if (!bdev->evt_skb) {
			err = -ENOMEM;
			goto err_out;
		}
	}

	err = hci_recv_frame(hdev, skb);
	if (err < 0)
		goto err_free_skb;

	if (hdr->evt == HCI_EV_VENDOR) {
		if (test_and_clear_bit(BTMTKSDIO_TX_WAIT_VND_EVT,
				       &bdev->tx_state)) {
			/* Barrier to sync with other CPUs */
			smp_mb__after_atomic();
			wake_up_bit(&bdev->tx_state, BTMTKSDIO_TX_WAIT_VND_EVT);
		}
	}

	return 0;

err_free_skb:
	kfree_skb(bdev->evt_skb);
	bdev->evt_skb = NULL;

err_out:
	return err;
}

static const struct h4_recv_pkt mtk_recv_pkts[] = {
	{ H4_RECV_ACL,      .recv = hci_recv_frame },
	{ H4_RECV_SCO,      .recv = hci_recv_frame },
	{ H4_RECV_EVENT,    .recv = btmtksdio_recv_event },
};

static int btmtksdio_rx_packet(struct btmtksdio_dev *bdev, u16 rx_size)
{
	const struct h4_recv_pkt *pkts = mtk_recv_pkts;
	int pkts_count = ARRAY_SIZE(mtk_recv_pkts);
	struct mtkbtsdio_hdr *sdio_hdr;
	int err, i, pad_size;
	struct sk_buff *skb;
	u16 dlen;

	if (rx_size < sizeof(*sdio_hdr))
		return -EILSEQ;

	/* A SDIO packet is exactly containing a Bluetooth packet */
	skb = bt_skb_alloc(rx_size, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, rx_size);

	err = sdio_readsb(bdev->func, skb->data, MTK_REG_CRDR, rx_size);
	if (err < 0)
		goto err_kfree_skb;

	sdio_hdr = (void *)skb->data;

	/* We assume the default error as -EILSEQ simply to make the error path
	 * be cleaner.
	 */
	err = -EILSEQ;

	if (rx_size != le16_to_cpu(sdio_hdr->len)) {
		bt_dev_err(bdev->hdev, "Rx size in sdio header is mismatched ");
		goto err_kfree_skb;
	}

	hci_skb_pkt_type(skb) = sdio_hdr->bt_type;

	/* Remove MediaTek SDIO header */
	skb_pull(skb, sizeof(*sdio_hdr));

	/* We have to dig into the packet to get payload size and then know how
	 * many padding bytes at the tail, these padding bytes should be removed
	 * before the packet is indicated to the core layer.
	 */
	for (i = 0; i < pkts_count; i++) {
		if (sdio_hdr->bt_type == (&pkts[i])->type)
			break;
	}

	if (i >= pkts_count) {
		bt_dev_err(bdev->hdev, "Invalid bt type 0x%02x",
			   sdio_hdr->bt_type);
		goto err_kfree_skb;
	}

	/* Remaining bytes cannot hold a header*/
	if (skb->len < (&pkts[i])->hlen) {
		bt_dev_err(bdev->hdev, "The size of bt header is mismatched");
		goto err_kfree_skb;
	}

	switch ((&pkts[i])->lsize) {
		case 1:
			dlen = skb->data[(&pkts[i])->loff];
			break;
		case 2:
			dlen = get_unaligned_le16(skb->data +
						  (&pkts[i])->loff);
			break;
		default:
			goto err_kfree_skb;
	}

	pad_size = skb->len - (&pkts[i])->hlen -  dlen;

	/* Remaining bytes cannot hold a payload */
	if (pad_size < 0) {
		bt_dev_err(bdev->hdev, "The size of bt payload is mismatched");
		goto err_kfree_skb;
	}

	/* Remove padding bytes */
	skb_trim(skb, skb->len - pad_size);

	/* Complete frame */
	(&pkts[i])->recv(bdev->hdev, skb);

	bdev->hdev->stat.byte_rx += rx_size;

	return 0;

err_kfree_skb:
	kfree_skb(skb);

	return err;
}

static void btmtksdio_interrupt(struct sdio_func *func)
{
	struct btmtksdio_dev *bdev = sdio_get_drvdata(func);
	u32 int_status;
	u16 rx_size;

	/* It is required that the host gets ownership from the device before
	 * accessing any register, however, if SDIO host is not being released,
	 * a potential deadlock probably happens in a circular wait between SDIO
	 * IRQ work and PM runtime work. So, we have to explicitly release SDIO
	 * host here and claim again after the PM runtime work is all done.
	 */
	sdio_release_host(bdev->func);

	pm_runtime_get_sync(bdev->dev);

	sdio_claim_host(bdev->func);

	/* Disable interrupt */
	sdio_writel(func, C_INT_EN_CLR, MTK_REG_CHLPCR, NULL);

	int_status = sdio_readl(func, MTK_REG_CHISR, NULL);

	/* Ack an interrupt as soon as possible before any operation on
	 * hardware.
	 *
	 * Note that we don't ack any status during operations to avoid race
	 * condition between the host and the device such as it's possible to
	 * mistakenly ack RX_DONE for the next packet and then cause interrupts
	 * not be raised again but there is still pending data in the hardware
	 * FIFO.
	 */
	sdio_writel(func, int_status, MTK_REG_CHISR, NULL);

	if (unlikely(!int_status))
		bt_dev_err(bdev->hdev, "CHISR is 0");

	if (int_status & FW_OWN_BACK_INT)
		bt_dev_dbg(bdev->hdev, "Get fw own back");

	if (int_status & TX_EMPTY)
		schedule_work(&bdev->tx_work);
	else if (unlikely(int_status & TX_FIFO_OVERFLOW))
		bt_dev_warn(bdev->hdev, "Tx fifo overflow");

	if (int_status & RX_DONE_INT) {
		rx_size = (int_status & RX_PKT_LEN) >> 16;

		if (btmtksdio_rx_packet(bdev, rx_size) < 0)
			bdev->hdev->stat.err_rx++;
	}

	/* Enable interrupt */
	sdio_writel(func, C_INT_EN_SET, MTK_REG_CHLPCR, NULL);

	pm_runtime_mark_last_busy(bdev->dev);
	pm_runtime_put_autosuspend(bdev->dev);
}

static int btmtksdio_open(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	int err;
	u32 status;

	sdio_claim_host(bdev->func);

	err = sdio_enable_func(bdev->func);
	if (err < 0)
		goto err_release_host;

	/* Get ownership from the device */
	sdio_writel(bdev->func, C_FW_OWN_REQ_CLR, MTK_REG_CHLPCR, &err);
	if (err < 0)
		goto err_disable_func;

	err = readx_poll_timeout(btmtksdio_drv_own_query, bdev, status,
				 status & C_COM_DRV_OWN, 2000, 1000000);
	if (err < 0) {
		bt_dev_err(bdev->hdev, "Cannot get ownership from device");
		goto err_disable_func;
	}

	/* Disable interrupt & mask out all interrupt sources */
	sdio_writel(bdev->func, C_INT_EN_CLR, MTK_REG_CHLPCR, &err);
	if (err < 0)
		goto err_disable_func;

	sdio_writel(bdev->func, 0, MTK_REG_CHIER, &err);
	if (err < 0)
		goto err_disable_func;

	err = sdio_claim_irq(bdev->func, btmtksdio_interrupt);
	if (err < 0)
		goto err_disable_func;

	err = sdio_set_block_size(bdev->func, MTK_SDIO_BLOCK_SIZE);
	if (err < 0)
		goto err_release_irq;

	/* SDIO CMD 5 allows the SDIO device back to idle state an
	 * synchronous interrupt is supported in SDIO 4-bit mode
	 */
	sdio_writel(bdev->func, SDIO_INT_CTL | SDIO_RE_INIT_EN,
		    MTK_REG_CSDIOCSR, &err);
	if (err < 0)
		goto err_release_irq;

	/* Setup write-1-clear for CHISR register */
	sdio_writel(bdev->func, C_INT_CLR_CTRL, MTK_REG_CHCR, &err);
	if (err < 0)
		goto err_release_irq;

	/* Setup interrupt sources */
	sdio_writel(bdev->func, RX_DONE_INT | TX_EMPTY | TX_FIFO_OVERFLOW,
		    MTK_REG_CHIER, &err);
	if (err < 0)
		goto err_release_irq;

	/* Enable interrupt */
	sdio_writel(bdev->func, C_INT_EN_SET, MTK_REG_CHLPCR, &err);
	if (err < 0)
		goto err_release_irq;

	sdio_release_host(bdev->func);

	return 0;

err_release_irq:
	sdio_release_irq(bdev->func);

err_disable_func:
	sdio_disable_func(bdev->func);

err_release_host:
	sdio_release_host(bdev->func);

	return err;
}

static int btmtksdio_close(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	u32 status;
	int err;

	sdio_claim_host(bdev->func);

	/* Disable interrupt */
	sdio_writel(bdev->func, C_INT_EN_CLR, MTK_REG_CHLPCR, NULL);

	sdio_release_irq(bdev->func);

	/* Return ownership to the device */
	sdio_writel(bdev->func, C_FW_OWN_REQ_SET, MTK_REG_CHLPCR, NULL);

	err = readx_poll_timeout(btmtksdio_drv_own_query, bdev, status,
				 !(status & C_COM_DRV_OWN), 2000, 1000000);
	if (err < 0)
		bt_dev_err(bdev->hdev, "Cannot return ownership to device");

	sdio_disable_func(bdev->func);

	sdio_release_host(bdev->func);

	return 0;
}

static int btmtksdio_flush(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);

	skb_queue_purge(&bdev->txq);

	cancel_work_sync(&bdev->tx_work);

	return 0;
}

static int btmtksdio_func_query(struct hci_dev *hdev)
{
	struct btmtk_hci_wmt_params wmt_params;
	int status, err;
	u8 param = 0;

	/* Query whether the function is enabled */
	wmt_params.op = MTK_WMT_FUNC_CTRL;
	wmt_params.flag = 4;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = &status;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to query function status (%d)", err);
		return err;
	}

	return status;
}

static int mtk_setup_firmware(struct hci_dev *hdev, const char *fwname)
{
	struct btmtk_hci_wmt_params wmt_params;
	const struct firmware *fw;
	const u8 *fw_ptr;
	size_t fw_size;
	int err, dlen;
	u8 flag, param;

	err = request_firmware(&fw, fwname, &hdev->dev);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to load firmware file (%d)", err);
		return err;
	}

	/* Power on data RAM the firmware relies on. */
	param = 1;
	wmt_params.op = MTK_WMT_FUNC_CTRL;
	wmt_params.flag = 3;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = NULL;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to power on data RAM (%d)", err);
		goto free_fw;
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

	wmt_params.op = MTK_WMT_PATCH_DWNLD;
	wmt_params.status = NULL;

	while (fw_size > 0) {
		dlen = min_t(int, 250, fw_size);

		/* Tell device the position in sequence */
		if (fw_size - dlen <= 0)
			flag = 3;
		else if (fw_size < fw->size - 30)
			flag = 2;

		wmt_params.flag = flag;
		wmt_params.dlen = dlen;
		wmt_params.data = fw_ptr;

		err = mtk_hci_wmt_sync(hdev, &wmt_params);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to send wmt patch dwnld (%d)",
				   err);
			goto free_fw;
		}

		fw_size -= dlen;
		fw_ptr += dlen;
	}

	wmt_params.op = MTK_WMT_RST;
	wmt_params.flag = 4;
	wmt_params.dlen = 0;
	wmt_params.data = NULL;
	wmt_params.status = NULL;

	/* Activate funciton the firmware providing to */
	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt rst (%d)", err);
		goto free_fw;
	}

	/* Wait a few moments for firmware activation done */
	usleep_range(10000, 12000);

free_fw:
	release_firmware(fw);
	return err;
}

static int btmtksdio_setup(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	struct btmtk_hci_wmt_params wmt_params;
	ktime_t calltime, delta, rettime;
	struct btmtk_tci_sleep tci_sleep;
	unsigned long long duration;
	struct sk_buff *skb;
	int err, status;
	u8 param = 0x1;

	calltime = ktime_get();

	/* Query whether the firmware is already download */
	wmt_params.op = MTK_WMT_SEMAPHORE;
	wmt_params.flag = 1;
	wmt_params.dlen = 0;
	wmt_params.data = NULL;
	wmt_params.status = &status;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to query firmware status (%d)", err);
		return err;
	}

	if (status == BTMTK_WMT_PATCH_DONE) {
		bt_dev_info(hdev, "Firmware already downloaded");
		goto ignore_setup_fw;
	}

	/* Setup a firmware which the device definitely requires */
	err = mtk_setup_firmware(hdev, bdev->data->fwname);
	if (err < 0)
		return err;

ignore_setup_fw:
	/* Query whether the device is already enabled */
	err = readx_poll_timeout(btmtksdio_func_query, hdev, status,
				 status < 0 || status != BTMTK_WMT_ON_PROGRESS,
				 2000, 5000000);
	/* -ETIMEDOUT happens */
	if (err < 0)
		return err;

	/* The other errors happen in btusb_mtk_func_query */
	if (status < 0)
		return status;

	if (status == BTMTK_WMT_ON_DONE) {
		bt_dev_info(hdev, "function already on");
		goto ignore_func_on;
	}

	/* Enable Bluetooth protocol */
	wmt_params.op = MTK_WMT_FUNC_CTRL;
	wmt_params.flag = 0;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = NULL;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
		return err;
	}

ignore_func_on:
	/* Apply the low power environment setup */
	tci_sleep.mode = 0x5;
	tci_sleep.duration = cpu_to_le16(0x640);
	tci_sleep.host_duration = cpu_to_le16(0x640);
	tci_sleep.host_wakeup_pin = 0;
	tci_sleep.time_compensation = 0;

	skb = __hci_cmd_sync(hdev, 0xfc7a, sizeof(tci_sleep), &tci_sleep,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "Failed to apply low power setting (%d)", err);
		return err;
	}
	kfree_skb(skb);

	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long)ktime_to_ns(delta) >> 10;

	pm_runtime_set_autosuspend_delay(bdev->dev,
					 MTKBTSDIO_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(bdev->dev);

	err = pm_runtime_set_active(bdev->dev);
	if (err < 0)
		return err;

	/* Default forbid runtime auto suspend, that can be allowed by
	 * enable_autosuspend flag or the PM runtime entry under sysfs.
	 */
	pm_runtime_forbid(bdev->dev);
	pm_runtime_enable(bdev->dev);

	if (enable_autosuspend)
		pm_runtime_allow(bdev->dev);

	bt_dev_info(hdev, "Device setup in %llu usecs", duration);

	return 0;
}

static int btmtksdio_shutdown(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	struct btmtk_hci_wmt_params wmt_params;
	u8 param = 0x0;
	int err;

	/* Get back the state to be consistent with the state
	 * in btmtksdio_setup.
	 */
	pm_runtime_get_sync(bdev->dev);

	/* Disable the device */
	wmt_params.op = MTK_WMT_FUNC_CTRL;
	wmt_params.flag = 0;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = NULL;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
		return err;
	}

	pm_runtime_put_noidle(bdev->dev);
	pm_runtime_disable(bdev->dev);

	return 0;
}

static int btmtksdio_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);

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

	default:
		return -EILSEQ;
	}

	skb_queue_tail(&bdev->txq, skb);

	schedule_work(&bdev->tx_work);

	return 0;
}

static int btmtksdio_probe(struct sdio_func *func,
			   const struct sdio_device_id *id)
{
	struct btmtksdio_dev *bdev;
	struct hci_dev *hdev;
	int err;

	bdev = devm_kzalloc(&func->dev, sizeof(*bdev), GFP_KERNEL);
	if (!bdev)
		return -ENOMEM;

	bdev->data = (void *)id->driver_data;
	if (!bdev->data)
		return -ENODEV;

	bdev->dev = &func->dev;
	bdev->func = func;

	INIT_WORK(&bdev->tx_work, btmtksdio_tx_work);
	skb_queue_head_init(&bdev->txq);

	/* Initialize and register HCI device */
	hdev = hci_alloc_dev();
	if (!hdev) {
		dev_err(&func->dev, "Can't allocate HCI device\n");
		return -ENOMEM;
	}

	bdev->hdev = hdev;

	hdev->bus = HCI_SDIO;
	hci_set_drvdata(hdev, bdev);

	hdev->open     = btmtksdio_open;
	hdev->close    = btmtksdio_close;
	hdev->flush    = btmtksdio_flush;
	hdev->setup    = btmtksdio_setup;
	hdev->shutdown = btmtksdio_shutdown;
	hdev->send     = btmtksdio_send_frame;
	SET_HCIDEV_DEV(hdev, &func->dev);

	hdev->manufacturer = 70;
	set_bit(HCI_QUIRK_NON_PERSISTENT_SETUP, &hdev->quirks);

	err = hci_register_dev(hdev);
	if (err < 0) {
		dev_err(&func->dev, "Can't register HCI device\n");
		hci_free_dev(hdev);
		return err;
	}

	sdio_set_drvdata(func, bdev);

	/* pm_runtime_enable would be done after the firmware is being
	 * downloaded because the core layer probably already enables
	 * runtime PM for this func such as the case host->caps &
	 * MMC_CAP_POWER_OFF_CARD.
	 */
	if (pm_runtime_enabled(bdev->dev))
		pm_runtime_disable(bdev->dev);

	/* As explaination in drivers/mmc/core/sdio_bus.c tells us:
	 * Unbound SDIO functions are always suspended.
	 * During probe, the function is set active and the usage count
	 * is incremented.  If the driver supports runtime PM,
	 * it should call pm_runtime_put_noidle() in its probe routine and
	 * pm_runtime_get_noresume() in its remove routine.
	 *
	 * So, put a pm_runtime_put_noidle here !
	 */
	pm_runtime_put_noidle(bdev->dev);

	return 0;
}

static void btmtksdio_remove(struct sdio_func *func)
{
	struct btmtksdio_dev *bdev = sdio_get_drvdata(func);
	struct hci_dev *hdev;

	if (!bdev)
		return;

	/* Be consistent the state in btmtksdio_probe */
	pm_runtime_get_noresume(bdev->dev);

	hdev = bdev->hdev;

	sdio_set_drvdata(func, NULL);
	hci_unregister_dev(hdev);
	hci_free_dev(hdev);
}

#ifdef CONFIG_PM
static int btmtksdio_runtime_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct btmtksdio_dev *bdev;
	u32 status;
	int err;

	bdev = sdio_get_drvdata(func);
	if (!bdev)
		return 0;

	sdio_claim_host(bdev->func);

	sdio_writel(bdev->func, C_FW_OWN_REQ_SET, MTK_REG_CHLPCR, &err);
	if (err < 0)
		goto out;

	err = readx_poll_timeout(btmtksdio_drv_own_query, bdev, status,
				 !(status & C_COM_DRV_OWN), 2000, 1000000);
out:
	bt_dev_info(bdev->hdev, "status (%d) return ownership to device", err);

	sdio_release_host(bdev->func);

	return err;
}

static int btmtksdio_runtime_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct btmtksdio_dev *bdev;
	u32 status;
	int err;

	bdev = sdio_get_drvdata(func);
	if (!bdev)
		return 0;

	sdio_claim_host(bdev->func);

	sdio_writel(bdev->func, C_FW_OWN_REQ_CLR, MTK_REG_CHLPCR, &err);
	if (err < 0)
		goto out;

	err = readx_poll_timeout(btmtksdio_drv_own_query, bdev, status,
				 status & C_COM_DRV_OWN, 2000, 1000000);
out:
	bt_dev_info(bdev->hdev, "status (%d) get ownership from device", err);

	sdio_release_host(bdev->func);

	return err;
}

static UNIVERSAL_DEV_PM_OPS(btmtksdio_pm_ops, btmtksdio_runtime_suspend,
			    btmtksdio_runtime_resume, NULL);
#define BTMTKSDIO_PM_OPS (&btmtksdio_pm_ops)
#else	/* CONFIG_PM */
#define BTMTKSDIO_PM_OPS NULL
#endif	/* CONFIG_PM */

static struct sdio_driver btmtksdio_driver = {
	.name		= "btmtksdio",
	.probe		= btmtksdio_probe,
	.remove		= btmtksdio_remove,
	.id_table	= btmtksdio_table,
	.drv = {
		.owner = THIS_MODULE,
		.pm = BTMTKSDIO_PM_OPS,
	}
};

module_sdio_driver(btmtksdio_driver);

module_param(enable_autosuspend, bool, 0644);
MODULE_PARM_DESC(enable_autosuspend, "Enable autosuspend by default");

MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_DESCRIPTION("MediaTek Bluetooth SDIO driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(FIRMWARE_MT7663);
MODULE_FIRMWARE(FIRMWARE_MT7668);
