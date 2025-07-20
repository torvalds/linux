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

#include <linux/unaligned.h>
#include <linux/atomic.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/skbuff.h>
#include <linux/usb.h>

#include <linux/mmc/host.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "h4_recv.h"
#include "btmtk.h"

#define VERSION "0.1"

#define MTKBTSDIO_AUTOSUSPEND_DELAY	1000

static bool enable_autosuspend = true;

struct btmtksdio_data {
	const char *fwname;
	u16 chipid;
	bool lp_mbox_supported;
};

static const struct btmtksdio_data mt7663_data = {
	.fwname = FIRMWARE_MT7663,
	.chipid = 0x7663,
	.lp_mbox_supported = false,
};

static const struct btmtksdio_data mt7668_data = {
	.fwname = FIRMWARE_MT7668,
	.chipid = 0x7668,
	.lp_mbox_supported = false,
};

static const struct btmtksdio_data mt7921_data = {
	.fwname = FIRMWARE_MT7961,
	.chipid = 0x7921,
	.lp_mbox_supported = true,
};

static const struct sdio_device_id btmtksdio_table[] = {
	{SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, SDIO_DEVICE_ID_MEDIATEK_MT7663),
	 .driver_data = (kernel_ulong_t)&mt7663_data },
	{SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, SDIO_DEVICE_ID_MEDIATEK_MT7668),
	 .driver_data = (kernel_ulong_t)&mt7668_data },
	{SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, SDIO_DEVICE_ID_MEDIATEK_MT7961),
	 .driver_data = (kernel_ulong_t)&mt7921_data },
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
#define BT_RST_DONE		BIT(8)

/* CHISR have the same bits field definition with CHIER */
#define MTK_REG_CHISR		0x10
#define MTK_REG_CHIER		0x14
#define FW_OWN_BACK_INT		BIT(0)
#define RX_DONE_INT		BIT(1)
#define TX_EMPTY		BIT(2)
#define TX_FIFO_OVERFLOW	BIT(8)
#define FW_MAILBOX_INT		BIT(15)
#define INT_MASK		GENMASK(15, 0)
#define RX_PKT_LEN		GENMASK(31, 16)

#define MTK_REG_CSICR		0xc0
#define CSICR_CLR_MBOX_ACK BIT(0)
#define MTK_REG_PH2DSM0R	0xc4
#define PH2DSM0R_DRIVER_OWN	BIT(0)
#define MTK_REG_PD2HRM0R	0xdc
#define PD2HRM0R_DRV_OWN	BIT(0)

#define MTK_REG_CTDR		0x18

#define MTK_REG_CRDR		0x1c

#define MTK_REG_CRPLR		0x24

#define MTK_SDIO_BLOCK_SIZE	256

#define BTMTKSDIO_TX_WAIT_VND_EVT	1
#define BTMTKSDIO_HW_TX_READY		2
#define BTMTKSDIO_FUNC_ENABLED		3
#define BTMTKSDIO_PATCH_ENABLED		4
#define BTMTKSDIO_HW_RESET_ACTIVE	5
#define BTMTKSDIO_BT_WAKE_ENABLED	6

struct mtkbtsdio_hdr {
	__le16	len;
	__le16	reserved;
	u8	bt_type;
} __packed;

struct btmtksdio_dev {
	struct hci_dev *hdev;
	struct sdio_func *func;
	struct device *dev;

	struct work_struct txrx_work;
	unsigned long tx_state;
	struct sk_buff_head txq;

	struct sk_buff *evt_skb;

	const struct btmtksdio_data *data;

	struct gpio_desc *reset;
};

static int mtk_hci_wmt_sync(struct hci_dev *hdev,
			    struct btmtk_hci_wmt_params *wmt_params)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	struct btmtk_hci_wmt_evt_funcc *wmt_evt_funcc;
	struct btmtk_hci_wmt_evt_reg *wmt_evt_reg;
	u32 hlen, status = BTMTK_WMT_INVALID;
	struct btmtk_hci_wmt_evt *wmt_evt;
	struct btmtk_hci_wmt_cmd *wc;
	struct btmtk_wmt_hdr *hdr;
	int err;

	/* Send the WMT command and wait until the WMT event returns */
	hlen = sizeof(*hdr) + wmt_params->dlen;
	if (hlen > 255)
		return -EINVAL;

	wc = kzalloc(hlen, GFP_KERNEL);
	if (!wc)
		return -ENOMEM;

	hdr = &wc->hdr;
	hdr->dir = 1;
	hdr->op = wmt_params->op;
	hdr->dlen = cpu_to_le16(wmt_params->dlen + 1);
	hdr->flag = wmt_params->flag;
	memcpy(wc->data, wmt_params->data, wmt_params->dlen);

	set_bit(BTMTKSDIO_TX_WAIT_VND_EVT, &bdev->tx_state);

	err = __hci_cmd_send(hdev, 0xfc6f, hlen, wc);
	if (err < 0) {
		clear_bit(BTMTKSDIO_TX_WAIT_VND_EVT, &bdev->tx_state);
		goto err_free_wc;
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
		goto err_free_wc;
	}

	if (err) {
		bt_dev_err(hdev, "Execution of wmt command timed out");
		clear_bit(BTMTKSDIO_TX_WAIT_VND_EVT, &bdev->tx_state);
		err = -ETIMEDOUT;
		goto err_free_wc;
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
	case BTMTK_WMT_SEMAPHORE:
		if (wmt_evt->whdr.flag == 2)
			status = BTMTK_WMT_PATCH_UNDONE;
		else
			status = BTMTK_WMT_PATCH_DONE;
		break;
	case BTMTK_WMT_FUNC_CTRL:
		wmt_evt_funcc = (struct btmtk_hci_wmt_evt_funcc *)wmt_evt;
		if (be16_to_cpu(wmt_evt_funcc->status) == 0x404)
			status = BTMTK_WMT_ON_DONE;
		else if (be16_to_cpu(wmt_evt_funcc->status) == 0x420)
			status = BTMTK_WMT_ON_PROGRESS;
		else
			status = BTMTK_WMT_ON_UNDONE;
		break;
	case BTMTK_WMT_PATCH_DWNLD:
		if (wmt_evt->whdr.flag == 2)
			status = BTMTK_WMT_PATCH_DONE;
		else if (wmt_evt->whdr.flag == 1)
			status = BTMTK_WMT_PATCH_PROGRESS;
		else
			status = BTMTK_WMT_PATCH_UNDONE;
		break;
	case BTMTK_WMT_REGISTER:
		wmt_evt_reg = (struct btmtk_hci_wmt_evt_reg *)wmt_evt;
		if (le16_to_cpu(wmt_evt->whdr.dlen) == 12)
			status = le32_to_cpu(wmt_evt_reg->val);
		break;
	}

	if (wmt_params->status)
		*wmt_params->status = status;

err_free_skb:
	kfree_skb(bdev->evt_skb);
	bdev->evt_skb = NULL;
err_free_wc:
	kfree(wc);

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

	clear_bit(BTMTKSDIO_HW_TX_READY, &bdev->tx_state);
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

static u32 btmtksdio_drv_own_query_79xx(struct btmtksdio_dev *bdev)
{
	return sdio_readl(bdev->func, MTK_REG_PD2HRM0R, NULL);
}

static u32 btmtksdio_chcr_query(struct btmtksdio_dev *bdev)
{
	return sdio_readl(bdev->func, MTK_REG_CHCR, NULL);
}

static int btmtksdio_fw_pmctrl(struct btmtksdio_dev *bdev)
{
	u32 status;
	int err;

	sdio_claim_host(bdev->func);

	if (bdev->data->lp_mbox_supported &&
	    test_bit(BTMTKSDIO_PATCH_ENABLED, &bdev->tx_state)) {
		sdio_writel(bdev->func, CSICR_CLR_MBOX_ACK, MTK_REG_CSICR,
			    &err);
		err = readx_poll_timeout(btmtksdio_drv_own_query_79xx, bdev,
					 status, !(status & PD2HRM0R_DRV_OWN),
					 2000, 1000000);
		if (err < 0) {
			bt_dev_err(bdev->hdev, "mailbox ACK not cleared");
			goto out;
		}
	}

	/* Return ownership to the device */
	sdio_writel(bdev->func, C_FW_OWN_REQ_SET, MTK_REG_CHLPCR, &err);
	if (err < 0)
		goto out;

	err = readx_poll_timeout(btmtksdio_drv_own_query, bdev, status,
				 !(status & C_COM_DRV_OWN), 2000, 1000000);

out:
	sdio_release_host(bdev->func);

	if (err < 0)
		bt_dev_err(bdev->hdev, "Cannot return ownership to device");

	return err;
}

static int btmtksdio_drv_pmctrl(struct btmtksdio_dev *bdev)
{
	u32 status;
	int err;

	sdio_claim_host(bdev->func);

	/* Get ownership from the device */
	sdio_writel(bdev->func, C_FW_OWN_REQ_CLR, MTK_REG_CHLPCR, &err);
	if (err < 0)
		goto out;

	err = readx_poll_timeout(btmtksdio_drv_own_query, bdev, status,
				 status & C_COM_DRV_OWN, 2000, 1000000);

	if (!err && bdev->data->lp_mbox_supported &&
	    test_bit(BTMTKSDIO_PATCH_ENABLED, &bdev->tx_state))
		err = readx_poll_timeout(btmtksdio_drv_own_query_79xx, bdev,
					 status, status & PD2HRM0R_DRV_OWN,
					 2000, 1000000);

out:
	sdio_release_host(bdev->func);

	if (err < 0)
		bt_dev_err(bdev->hdev, "Cannot get ownership from device");

	return err;
}

static int btmtksdio_recv_event(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	struct hci_event_hdr *hdr = (void *)skb->data;
	u8 evt = hdr->evt;
	int err;

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

	if (evt == HCI_EV_WMT) {
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

static int btmtksdio_recv_acl(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	u16 handle = le16_to_cpu(hci_acl_hdr(skb)->handle);

	switch (handle) {
	case 0xfc6f:
		/* Firmware dump from device: when the firmware hangs, the
		 * device can no longer suspend and thus disable auto-suspend.
		 */
		pm_runtime_forbid(bdev->dev);
		fallthrough;
	case 0x05ff:
	case 0x05fe:
		/* Firmware debug logging */
		return hci_recv_diag(hdev, skb);
	}

	return hci_recv_frame(hdev, skb);
}

static const struct h4_recv_pkt mtk_recv_pkts[] = {
	{ H4_RECV_ACL,      .recv = btmtksdio_recv_acl },
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

static void btmtksdio_txrx_work(struct work_struct *work)
{
	struct btmtksdio_dev *bdev = container_of(work, struct btmtksdio_dev,
						  txrx_work);
	unsigned long txrx_timeout;
	u32 int_status, rx_size;
	struct sk_buff *skb;
	int err;

	pm_runtime_get_sync(bdev->dev);

	sdio_claim_host(bdev->func);

	/* Disable interrupt */
	sdio_writel(bdev->func, C_INT_EN_CLR, MTK_REG_CHLPCR, NULL);

	txrx_timeout = jiffies + 5 * HZ;

	do {
		int_status = sdio_readl(bdev->func, MTK_REG_CHISR, NULL);

		/* Ack an interrupt as soon as possible before any operation on
		 * hardware.
		 *
		 * Note that we don't ack any status during operations to avoid race
		 * condition between the host and the device such as it's possible to
		 * mistakenly ack RX_DONE for the next packet and then cause interrupts
		 * not be raised again but there is still pending data in the hardware
		 * FIFO.
		 */
		sdio_writel(bdev->func, int_status, MTK_REG_CHISR, NULL);
		int_status &= INT_MASK;

		if ((int_status & FW_MAILBOX_INT) &&
		    bdev->data->chipid == 0x7921) {
			sdio_writel(bdev->func, PH2DSM0R_DRIVER_OWN,
				    MTK_REG_PH2DSM0R, NULL);
		}

		if (int_status & FW_OWN_BACK_INT)
			bt_dev_dbg(bdev->hdev, "Get fw own back");

		if (int_status & TX_EMPTY)
			set_bit(BTMTKSDIO_HW_TX_READY, &bdev->tx_state);

		else if (unlikely(int_status & TX_FIFO_OVERFLOW))
			bt_dev_warn(bdev->hdev, "Tx fifo overflow");

		if (test_bit(BTMTKSDIO_HW_TX_READY, &bdev->tx_state)) {
			skb = skb_dequeue(&bdev->txq);
			if (skb) {
				err = btmtksdio_tx_packet(bdev, skb);
				if (err < 0) {
					bdev->hdev->stat.err_tx++;
					skb_queue_head(&bdev->txq, skb);
				}
			}
		}

		if (int_status & RX_DONE_INT) {
			rx_size = sdio_readl(bdev->func, MTK_REG_CRPLR, NULL);
			rx_size = (rx_size & RX_PKT_LEN) >> 16;
			if (btmtksdio_rx_packet(bdev, rx_size) < 0)
				bdev->hdev->stat.err_rx++;
		}
	} while (int_status || time_is_before_jiffies(txrx_timeout));

	/* Enable interrupt */
	if (bdev->func->irq_handler)
		sdio_writel(bdev->func, C_INT_EN_SET, MTK_REG_CHLPCR, NULL);

	sdio_release_host(bdev->func);

	pm_runtime_mark_last_busy(bdev->dev);
	pm_runtime_put_autosuspend(bdev->dev);
}

static void btmtksdio_interrupt(struct sdio_func *func)
{
	struct btmtksdio_dev *bdev = sdio_get_drvdata(func);

	if (test_bit(BTMTKSDIO_BT_WAKE_ENABLED, &bdev->tx_state)) {
		if (bdev->hdev->suspended)
			pm_wakeup_event(bdev->dev, 0);
		clear_bit(BTMTKSDIO_BT_WAKE_ENABLED, &bdev->tx_state);
	}

	/* Disable interrupt */
	sdio_writel(bdev->func, C_INT_EN_CLR, MTK_REG_CHLPCR, NULL);

	schedule_work(&bdev->txrx_work);
}

static int btmtksdio_open(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	u32 val;
	int err;

	sdio_claim_host(bdev->func);

	err = sdio_enable_func(bdev->func);
	if (err < 0)
		goto err_release_host;

	set_bit(BTMTKSDIO_FUNC_ENABLED, &bdev->tx_state);

	err = btmtksdio_drv_pmctrl(bdev);
	if (err < 0)
		goto err_disable_func;

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
	val = sdio_readl(bdev->func, MTK_REG_CSDIOCSR, &err);
	if (err < 0)
		goto err_release_irq;

	val |= SDIO_INT_CTL;
	sdio_writel(bdev->func, val, MTK_REG_CSDIOCSR, &err);
	if (err < 0)
		goto err_release_irq;

	/* Explicitly set write-1-clear method */
	val = sdio_readl(bdev->func, MTK_REG_CHCR, &err);
	if (err < 0)
		goto err_release_irq;

	val |= C_INT_CLR_CTRL;
	sdio_writel(bdev->func, val, MTK_REG_CHCR, &err);
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

	/* Skip btmtksdio_close if BTMTKSDIO_FUNC_ENABLED isn't set */
	if (!test_bit(BTMTKSDIO_FUNC_ENABLED, &bdev->tx_state))
		return 0;

	sdio_claim_host(bdev->func);

	/* Disable interrupt */
	sdio_writel(bdev->func, C_INT_EN_CLR, MTK_REG_CHLPCR, NULL);

	sdio_release_irq(bdev->func);

	cancel_work_sync(&bdev->txrx_work);

	btmtksdio_fw_pmctrl(bdev);

	clear_bit(BTMTKSDIO_FUNC_ENABLED, &bdev->tx_state);
	sdio_disable_func(bdev->func);

	sdio_release_host(bdev->func);

	return 0;
}

static int btmtksdio_flush(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);

	skb_queue_purge(&bdev->txq);

	cancel_work_sync(&bdev->txrx_work);

	return 0;
}

static int btmtksdio_func_query(struct hci_dev *hdev)
{
	struct btmtk_hci_wmt_params wmt_params;
	int status, err;
	u8 param = 0;

	/* Query whether the function is enabled */
	wmt_params.op = BTMTK_WMT_FUNC_CTRL;
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

static int mt76xx_setup(struct hci_dev *hdev, const char *fwname)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	struct btmtk_hci_wmt_params wmt_params;
	struct btmtk_tci_sleep tci_sleep;
	struct sk_buff *skb;
	int err, status;
	u8 param = 0x1;

	/* Query whether the firmware is already download */
	wmt_params.op = BTMTK_WMT_SEMAPHORE;
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
	err = btmtk_setup_firmware(hdev, fwname, mtk_hci_wmt_sync);
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
	wmt_params.op = BTMTK_WMT_FUNC_CTRL;
	wmt_params.flag = 0;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = NULL;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
		return err;
	}

	set_bit(BTMTKSDIO_PATCH_ENABLED, &bdev->tx_state);

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

	return 0;
}

static int mt79xx_setup(struct hci_dev *hdev, const char *fwname)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	struct btmtk_hci_wmt_params wmt_params;
	u8 param = 0x1;
	int err;

	err = btmtk_setup_firmware_79xx(hdev, fwname, mtk_hci_wmt_sync);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to setup 79xx firmware (%d)", err);
		return err;
	}

	err = btmtksdio_fw_pmctrl(bdev);
	if (err < 0)
		return err;

	err = btmtksdio_drv_pmctrl(bdev);
	if (err < 0)
		return err;

	/* Enable Bluetooth protocol */
	wmt_params.op = BTMTK_WMT_FUNC_CTRL;
	wmt_params.flag = 0;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = NULL;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
		return err;
	}

	hci_set_msft_opcode(hdev, 0xFD30);
	hci_set_aosp_capable(hdev);
	set_bit(BTMTKSDIO_PATCH_ENABLED, &bdev->tx_state);

	return err;
}

static int btmtksdio_mtk_reg_read(struct hci_dev *hdev, u32 reg, u32 *val)
{
	struct btmtk_hci_wmt_params wmt_params;
	struct reg_read_cmd reg_read = {
		.type = 1,
		.num = 1,
	};
	u32 status;
	int err;

	reg_read.addr = cpu_to_le32(reg);
	wmt_params.op = BTMTK_WMT_REGISTER;
	wmt_params.flag = BTMTK_WMT_REG_READ;
	wmt_params.dlen = sizeof(reg_read);
	wmt_params.data = &reg_read;
	wmt_params.status = &status;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to read reg (%d)", err);
		return err;
	}

	*val = status;

	return err;
}

static int btmtksdio_mtk_reg_write(struct hci_dev *hdev, u32 reg, u32 val, u32 mask)
{
	struct btmtk_hci_wmt_params wmt_params;
	const struct reg_write_cmd reg_write = {
		.type = 1,
		.num = 1,
		.addr = cpu_to_le32(reg),
		.data = cpu_to_le32(val),
		.mask = cpu_to_le32(mask),
	};
	int err, status;

	wmt_params.op = BTMTK_WMT_REGISTER;
	wmt_params.flag = BTMTK_WMT_REG_WRITE;
	wmt_params.dlen = sizeof(reg_write);
	wmt_params.data = &reg_write;
	wmt_params.status = &status;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0)
		bt_dev_err(hdev, "Failed to write reg (%d)", err);

	return err;
}

static int btmtksdio_get_data_path_id(struct hci_dev *hdev, __u8 *data_path_id)
{
	/* uses 1 as data path id for all the usecases */
	*data_path_id = 1;
	return 0;
}

static int btmtksdio_get_codec_config_data(struct hci_dev *hdev,
					   __u8 link, struct bt_codec *codec,
					   __u8 *ven_len, __u8 **ven_data)
{
	int err = 0;

	if (!ven_data || !ven_len)
		return -EINVAL;

	*ven_len = 0;
	*ven_data = NULL;

	if (link != ESCO_LINK) {
		bt_dev_err(hdev, "Invalid link type(%u)", link);
		return -EINVAL;
	}

	*ven_data = kmalloc(sizeof(__u8), GFP_KERNEL);
	if (!*ven_data) {
		err = -ENOMEM;
		goto error;
	}

	/* supports only CVSD and mSBC offload codecs */
	switch (codec->id) {
	case 0x02:
		**ven_data = 0x00;
		break;
	case 0x05:
		**ven_data = 0x01;
		break;
	default:
		err = -EINVAL;
		bt_dev_err(hdev, "Invalid codec id(%u)", codec->id);
		goto error;
	}
	/* codec and its capabilities are pre-defined to ids
	 * preset id = 0x00 represents CVSD codec with sampling rate 8K
	 * preset id = 0x01 represents mSBC codec with sampling rate 16K
	 */
	*ven_len = sizeof(__u8);
	return err;

error:
	kfree(*ven_data);
	*ven_data = NULL;
	return err;
}

static int btmtksdio_sco_setting(struct hci_dev *hdev)
{
	const struct btmtk_sco sco_setting = {
		.clock_config = 0x49,
		.channel_format_config = 0x80,
	};
	struct sk_buff *skb;
	u32 val;
	int err;

	/* Enable SCO over I2S/PCM for MediaTek chipset */
	skb =  __hci_cmd_sync(hdev, 0xfc72, sizeof(sco_setting),
			      &sco_setting, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	kfree_skb(skb);

	err = btmtksdio_mtk_reg_read(hdev, MT7921_PINMUX_0, &val);
	if (err < 0)
		return err;

	val |= 0x11000000;
	err = btmtksdio_mtk_reg_write(hdev, MT7921_PINMUX_0, val, ~0);
	if (err < 0)
		return err;

	err = btmtksdio_mtk_reg_read(hdev, MT7921_PINMUX_1, &val);
	if (err < 0)
		return err;

	val |= 0x00000101;
	err =  btmtksdio_mtk_reg_write(hdev, MT7921_PINMUX_1, val, ~0);
	if (err < 0)
		return err;

	hdev->get_data_path_id = btmtksdio_get_data_path_id;
	hdev->get_codec_config_data = btmtksdio_get_codec_config_data;

	return err;
}

static int btmtksdio_reset_setting(struct hci_dev *hdev)
{
	int err;
	u32 val;

	err = btmtksdio_mtk_reg_read(hdev, MT7921_PINMUX_1, &val);
	if (err < 0)
		return err;

	val |= 0x20; /* set the pin (bit field 11:8) work as GPIO mode */
	err = btmtksdio_mtk_reg_write(hdev, MT7921_PINMUX_1, val, ~0);
	if (err < 0)
		return err;

	err = btmtksdio_mtk_reg_read(hdev, MT7921_BTSYS_RST, &val);
	if (err < 0)
		return err;

	val |= MT7921_BTSYS_RST_WITH_GPIO;
	return btmtksdio_mtk_reg_write(hdev, MT7921_BTSYS_RST, val, ~0);
}

static int btmtksdio_setup(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	ktime_t calltime, delta, rettime;
	unsigned long long duration;
	char fwname[64];
	int err, dev_id;
	u32 fw_version = 0, val;

	calltime = ktime_get();
	set_bit(BTMTKSDIO_HW_TX_READY, &bdev->tx_state);

	switch (bdev->data->chipid) {
	case 0x7921:
		if (test_bit(BTMTKSDIO_HW_RESET_ACTIVE, &bdev->tx_state)) {
			err = btmtksdio_mtk_reg_read(hdev, MT7921_DLSTATUS,
						     &val);
			if (err < 0)
				return err;

			val &= ~BT_DL_STATE;
			err = btmtksdio_mtk_reg_write(hdev, MT7921_DLSTATUS,
						      val, ~0);
			if (err < 0)
				return err;

			btmtksdio_fw_pmctrl(bdev);
			msleep(20);
			btmtksdio_drv_pmctrl(bdev);

			clear_bit(BTMTKSDIO_HW_RESET_ACTIVE, &bdev->tx_state);
		}

		err = btmtksdio_mtk_reg_read(hdev, 0x70010200, &dev_id);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to get device id (%d)", err);
			return err;
		}

		err = btmtksdio_mtk_reg_read(hdev, 0x80021004, &fw_version);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to get fw version (%d)", err);
			return err;
		}

		btmtk_fw_get_filename(fwname, sizeof(fwname), dev_id,
				      fw_version, 0);

		snprintf(fwname, sizeof(fwname),
			 "mediatek/BT_RAM_CODE_MT%04x_1_%x_hdr.bin",
			 dev_id & 0xffff, (fw_version & 0xff) + 1);
		err = mt79xx_setup(hdev, fwname);
		if (err < 0)
			return err;

		/* Enable SCO over I2S/PCM */
		err = btmtksdio_sco_setting(hdev);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to enable SCO setting (%d)", err);
			return err;
		}

		/* Enable WBS with mSBC codec */
		hci_set_quirk(hdev, HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED);

		/* Enable GPIO reset mechanism */
		if (bdev->reset) {
			err = btmtksdio_reset_setting(hdev);
			if (err < 0) {
				bt_dev_err(hdev, "Failed to enable Reset setting (%d)", err);
				devm_gpiod_put(bdev->dev, bdev->reset);
				bdev->reset = NULL;
			}
		}

		break;
	case 0x7663:
	case 0x7668:
		err = mt76xx_setup(hdev, bdev->data->fwname);
		if (err < 0)
			return err;
		break;
	default:
		return -ENODEV;
	}

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

	/* wmt command only works until the reset is complete */
	if (test_bit(BTMTKSDIO_HW_RESET_ACTIVE, &bdev->tx_state))
		goto ignore_wmt_cmd;

	/* Disable the device */
	wmt_params.op = BTMTK_WMT_FUNC_CTRL;
	wmt_params.flag = 0;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = NULL;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
		return err;
	}

ignore_wmt_cmd:
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

	schedule_work(&bdev->txrx_work);

	return 0;
}

static void btmtksdio_reset(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	u32 status;
	int err;

	if (!bdev->reset || bdev->data->chipid != 0x7921)
		return;

	pm_runtime_get_sync(bdev->dev);

	if (test_and_set_bit(BTMTKSDIO_HW_RESET_ACTIVE, &bdev->tx_state))
		return;

	sdio_claim_host(bdev->func);

	sdio_writel(bdev->func, C_INT_EN_CLR, MTK_REG_CHLPCR, NULL);
	skb_queue_purge(&bdev->txq);
	cancel_work_sync(&bdev->txrx_work);

	gpiod_set_value_cansleep(bdev->reset, 1);
	msleep(100);
	gpiod_set_value_cansleep(bdev->reset, 0);

	err = readx_poll_timeout(btmtksdio_chcr_query, bdev, status,
				 status & BT_RST_DONE, 100000, 2000000);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to reset (%d)", err);
		goto err;
	}

	clear_bit(BTMTKSDIO_PATCH_ENABLED, &bdev->tx_state);
err:
	sdio_release_host(bdev->func);

	pm_runtime_put_noidle(bdev->dev);
	pm_runtime_disable(bdev->dev);

	hci_reset_dev(hdev);
}

static bool btmtksdio_sdio_inband_wakeup(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);

	return device_may_wakeup(bdev->dev);
}

static bool btmtksdio_sdio_wakeup(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	bool may_wakeup = device_may_wakeup(bdev->dev);
	const struct btmtk_wakeon bt_awake = {
		.mode = 0x1,
		.gpo = 0,
		.active_high = 0x1,
		.enable_delay = cpu_to_le16(0xc80),
		.wakeup_delay = cpu_to_le16(0x20),
	};

	if (may_wakeup && bdev->data->chipid == 0x7921) {
		struct sk_buff *skb;

		skb =  __hci_cmd_sync(hdev, 0xfc27, sizeof(bt_awake),
				      &bt_awake, HCI_CMD_TIMEOUT);
		if (IS_ERR(skb))
			may_wakeup = false;
		else
			kfree_skb(skb);
	}

	return may_wakeup;
}

static int btmtksdio_probe(struct sdio_func *func,
			   const struct sdio_device_id *id)
{
	struct btmtksdio_dev *bdev;
	struct hci_dev *hdev;
	struct device_node *old_node;
	bool restore_node;
	int err;

	bdev = devm_kzalloc(&func->dev, sizeof(*bdev), GFP_KERNEL);
	if (!bdev)
		return -ENOMEM;

	bdev->data = (void *)id->driver_data;
	if (!bdev->data)
		return -ENODEV;

	bdev->dev = &func->dev;
	bdev->func = func;

	INIT_WORK(&bdev->txrx_work, btmtksdio_txrx_work);
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
	hdev->reset    = btmtksdio_reset;
	hdev->flush    = btmtksdio_flush;
	hdev->setup    = btmtksdio_setup;
	hdev->shutdown = btmtksdio_shutdown;
	hdev->send     = btmtksdio_send_frame;
	hdev->wakeup   = btmtksdio_sdio_wakeup;
	/*
	 * If SDIO controller supports wake on Bluetooth, sending a wakeon
	 * command is not necessary.
	 */
	if (device_can_wakeup(func->card->host->parent))
		hdev->wakeup = btmtksdio_sdio_inband_wakeup;
	else
		hdev->wakeup = btmtksdio_sdio_wakeup;
	hdev->set_bdaddr = btmtk_set_bdaddr;

	SET_HCIDEV_DEV(hdev, &func->dev);

	hdev->manufacturer = 70;
	hci_set_quirk(hdev, HCI_QUIRK_NON_PERSISTENT_SETUP);

	sdio_set_drvdata(func, bdev);

	err = hci_register_dev(hdev);
	if (err < 0) {
		dev_err(&func->dev, "Can't register HCI device\n");
		hci_free_dev(hdev);
		return err;
	}

	/* pm_runtime_enable would be done after the firmware is being
	 * downloaded because the core layer probably already enables
	 * runtime PM for this func such as the case host->caps &
	 * MMC_CAP_POWER_OFF_CARD.
	 */
	if (pm_runtime_enabled(bdev->dev))
		pm_runtime_disable(bdev->dev);

	/* As explanation in drivers/mmc/core/sdio_bus.c tells us:
	 * Unbound SDIO functions are always suspended.
	 * During probe, the function is set active and the usage count
	 * is incremented.  If the driver supports runtime PM,
	 * it should call pm_runtime_put_noidle() in its probe routine and
	 * pm_runtime_get_noresume() in its remove routine.
	 *
	 * So, put a pm_runtime_put_noidle here !
	 */
	pm_runtime_put_noidle(bdev->dev);

	err = devm_device_init_wakeup(bdev->dev);
	if (err)
		bt_dev_err(hdev, "failed to initialize device wakeup");

	restore_node = false;
	if (!of_device_is_compatible(bdev->dev->of_node, "mediatek,mt7921s-bluetooth")) {
		restore_node = true;
		old_node = bdev->dev->of_node;
		bdev->dev->of_node = of_find_compatible_node(NULL, NULL,
							     "mediatek,mt7921s-bluetooth");
	}

	bdev->reset = devm_gpiod_get_optional(bdev->dev, "reset",
					      GPIOD_OUT_LOW);
	if (IS_ERR(bdev->reset))
		err = PTR_ERR(bdev->reset);

	if (restore_node) {
		of_node_put(bdev->dev->of_node);
		bdev->dev->of_node = old_node;
	}

	return err;
}

static void btmtksdio_remove(struct sdio_func *func)
{
	struct btmtksdio_dev *bdev = sdio_get_drvdata(func);
	struct hci_dev *hdev;

	if (!bdev)
		return;

	hdev = bdev->hdev;

	/* Make sure to call btmtksdio_close before removing sdio card */
	if (test_bit(BTMTKSDIO_FUNC_ENABLED, &bdev->tx_state))
		btmtksdio_close(hdev);

	/* Be consistent the state in btmtksdio_probe */
	pm_runtime_get_noresume(bdev->dev);

	sdio_set_drvdata(func, NULL);
	hci_unregister_dev(hdev);
	hci_free_dev(hdev);
}

#ifdef CONFIG_PM
static int btmtksdio_runtime_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct btmtksdio_dev *bdev;
	int err;

	bdev = sdio_get_drvdata(func);
	if (!bdev)
		return 0;

	if (!test_bit(BTMTKSDIO_FUNC_ENABLED, &bdev->tx_state))
		return 0;

	sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);

	err = btmtksdio_fw_pmctrl(bdev);

	bt_dev_dbg(bdev->hdev, "status (%d) return ownership to device", err);

	return err;
}

static int btmtksdio_system_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct btmtksdio_dev *bdev;

	bdev = sdio_get_drvdata(func);
	if (!bdev)
		return 0;

	if (!test_bit(BTMTKSDIO_FUNC_ENABLED, &bdev->tx_state))
		return 0;

	set_bit(BTMTKSDIO_BT_WAKE_ENABLED, &bdev->tx_state);

	return btmtksdio_runtime_suspend(dev);
}

static int btmtksdio_runtime_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct btmtksdio_dev *bdev;
	int err;

	bdev = sdio_get_drvdata(func);
	if (!bdev)
		return 0;

	if (!test_bit(BTMTKSDIO_FUNC_ENABLED, &bdev->tx_state))
		return 0;

	err = btmtksdio_drv_pmctrl(bdev);

	bt_dev_dbg(bdev->hdev, "status (%d) get ownership from device", err);

	return err;
}

static int btmtksdio_system_resume(struct device *dev)
{
	return btmtksdio_runtime_resume(dev);
}

static const struct dev_pm_ops btmtksdio_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(btmtksdio_system_suspend, btmtksdio_system_resume)
	RUNTIME_PM_OPS(btmtksdio_runtime_suspend, btmtksdio_runtime_resume, NULL)
};

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
