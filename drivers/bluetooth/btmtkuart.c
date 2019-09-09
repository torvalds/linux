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
#include <linux/gpio/consumer.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/serdev.h>
#include <linux/skbuff.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "h4_recv.h"

#define VERSION "0.2"

#define FIRMWARE_MT7622		"mediatek/mt7622pr2h.bin"
#define FIRMWARE_MT7663		"mediatek/mt7663pr2h.bin"
#define FIRMWARE_MT7668		"mediatek/mt7668pr2h.bin"

#define MTK_STP_TLR_SIZE	2

#define BTMTKUART_TX_STATE_ACTIVE	1
#define BTMTKUART_TX_STATE_WAKEUP	2
#define BTMTKUART_TX_WAIT_VND_EVT	3
#define BTMTKUART_REQUIRED_WAKEUP	4

#define BTMTKUART_FLAG_STANDALONE_HW	 BIT(0)

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

struct mtk_stp_hdr {
	u8	prefix;
	__be16	dlen;
	u8	cs;
} __packed;

struct btmtkuart_data {
	unsigned int flags;
	const char *fwname;
};

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

struct btmtkuart_dev {
	struct hci_dev *hdev;
	struct serdev_device *serdev;

	struct clk *clk;
	struct clk *osc;
	struct regulator *vcc;
	struct gpio_desc *reset;
	struct gpio_desc *boot;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_runtime;
	struct pinctrl_state *pins_boot;
	speed_t	desired_speed;
	speed_t	curr_speed;

	struct work_struct tx_work;
	unsigned long tx_state;
	struct sk_buff_head txq;

	struct sk_buff *rx_skb;
	struct sk_buff *evt_skb;

	u8	stp_pad[6];
	u8	stp_cursor;
	u16	stp_dlen;

	const struct btmtkuart_data *data;
};

#define btmtkuart_is_standalone(bdev)	\
	((bdev)->data->flags & BTMTKUART_FLAG_STANDALONE_HW)
#define btmtkuart_is_builtin_soc(bdev)	\
	!((bdev)->data->flags & BTMTKUART_FLAG_STANDALONE_HW)

static int mtk_hci_wmt_sync(struct hci_dev *hdev,
			    struct btmtk_hci_wmt_params *wmt_params)
{
	struct btmtkuart_dev *bdev = hci_get_drvdata(hdev);
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
	 * state to be cleared. The driver specific event receive routine
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

static int mtk_setup_firmware(struct hci_dev *hdev, const char *fwname)
{
	struct btmtk_hci_wmt_params wmt_params;
	const struct firmware *fw;
	const u8 *fw_ptr;
	size_t fw_size;
	int err, dlen;
	u8 flag;

	err = request_firmware(&fw, fwname, &hdev->dev);
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

	/* When someone waits for the WMT event, the skb is being cloned
	 * and being processed the events from there then.
	 */
	if (test_bit(BTMTKUART_TX_WAIT_VND_EVT, &bdev->tx_state)) {
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
		if (test_and_clear_bit(BTMTKUART_TX_WAIT_VND_EVT,
				       &bdev->tx_state)) {
			/* Barrier to sync with other CPUs */
			smp_mb__after_atomic();
			wake_up_bit(&bdev->tx_state, BTMTKUART_TX_WAIT_VND_EVT);
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

	if (btmtkuart_is_standalone(bdev)) {
		if (bdev->curr_speed != bdev->desired_speed)
			err = serdev_device_set_baudrate(bdev->serdev,
							 115200);
		else
			err = serdev_device_set_baudrate(bdev->serdev,
							 bdev->desired_speed);

		if (err < 0) {
			bt_dev_err(hdev, "Unable to set baudrate UART device %s",
				   dev_name(&bdev->serdev->dev));
			goto  err_serdev_close;
		}

		serdev_device_set_flow_control(bdev->serdev, false);
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
err_serdev_close:
	serdev_device_close(bdev->serdev);
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

static int btmtkuart_func_query(struct hci_dev *hdev)
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

static int btmtkuart_change_baudrate(struct hci_dev *hdev)
{
	struct btmtkuart_dev *bdev = hci_get_drvdata(hdev);
	struct btmtk_hci_wmt_params wmt_params;
	__le32 baudrate;
	u8 param;
	int err;

	/* Indicate the device to enter the probe state the host is
	 * ready to change a new baudrate.
	 */
	baudrate = cpu_to_le32(bdev->desired_speed);
	wmt_params.op = MTK_WMT_HIF;
	wmt_params.flag = 1;
	wmt_params.dlen = 4;
	wmt_params.data = &baudrate;
	wmt_params.status = NULL;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to device baudrate (%d)", err);
		return err;
	}

	err = serdev_device_set_baudrate(bdev->serdev,
					 bdev->desired_speed);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to set up host baudrate (%d)",
			   err);
		return err;
	}

	serdev_device_set_flow_control(bdev->serdev, false);

	/* Send a dummy byte 0xff to activate the new baudrate */
	param = 0xff;
	err = serdev_device_write(bdev->serdev, &param, sizeof(param),
				  MAX_SCHEDULE_TIMEOUT);
	if (err < 0 || err < sizeof(param))
		return err;

	serdev_device_wait_until_sent(bdev->serdev, 0);

	/* Wait some time for the device changing baudrate done */
	usleep_range(20000, 22000);

	/* Test the new baudrate */
	wmt_params.op = MTK_WMT_TEST;
	wmt_params.flag = 7;
	wmt_params.dlen = 0;
	wmt_params.data = NULL;
	wmt_params.status = NULL;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to test new baudrate (%d)",
			   err);
		return err;
	}

	bdev->curr_speed = bdev->desired_speed;

	return 0;
}

static int btmtkuart_setup(struct hci_dev *hdev)
{
	struct btmtkuart_dev *bdev = hci_get_drvdata(hdev);
	struct btmtk_hci_wmt_params wmt_params;
	ktime_t calltime, delta, rettime;
	struct btmtk_tci_sleep tci_sleep;
	unsigned long long duration;
	struct sk_buff *skb;
	int err, status;
	u8 param = 0x1;

	calltime = ktime_get();

	/* Wakeup MCUSYS is required for certain devices before we start to
	 * do any setups.
	 */
	if (test_bit(BTMTKUART_REQUIRED_WAKEUP, &bdev->tx_state)) {
		wmt_params.op = MTK_WMT_WAKEUP;
		wmt_params.flag = 3;
		wmt_params.dlen = 0;
		wmt_params.data = NULL;
		wmt_params.status = NULL;

		err = mtk_hci_wmt_sync(hdev, &wmt_params);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to wakeup the chip (%d)", err);
			return err;
		}

		clear_bit(BTMTKUART_REQUIRED_WAKEUP, &bdev->tx_state);
	}

	if (btmtkuart_is_standalone(bdev))
		btmtkuart_change_baudrate(hdev);

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
	err = readx_poll_timeout(btmtkuart_func_query, hdev, status,
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

	bt_dev_info(hdev, "Device setup in %llu usecs", duration);

	return 0;
}

static int btmtkuart_shutdown(struct hci_dev *hdev)
{
	struct btmtk_hci_wmt_params wmt_params;
	u8 param = 0x0;
	int err;

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

static int btmtkuart_parse_dt(struct serdev_device *serdev)
{
	struct btmtkuart_dev *bdev = serdev_device_get_drvdata(serdev);
	struct device_node *node = serdev->dev.of_node;
	u32 speed = 921600;
	int err;

	if (btmtkuart_is_standalone(bdev)) {
		of_property_read_u32(node, "current-speed", &speed);

		bdev->desired_speed = speed;

		bdev->vcc = devm_regulator_get(&serdev->dev, "vcc");
		if (IS_ERR(bdev->vcc)) {
			err = PTR_ERR(bdev->vcc);
			return err;
		}

		bdev->osc = devm_clk_get_optional(&serdev->dev, "osc");
		if (IS_ERR(bdev->osc)) {
			err = PTR_ERR(bdev->osc);
			return err;
		}

		bdev->boot = devm_gpiod_get_optional(&serdev->dev, "boot",
						     GPIOD_OUT_LOW);
		if (IS_ERR(bdev->boot)) {
			err = PTR_ERR(bdev->boot);
			return err;
		}

		bdev->pinctrl = devm_pinctrl_get(&serdev->dev);
		if (IS_ERR(bdev->pinctrl)) {
			err = PTR_ERR(bdev->pinctrl);
			return err;
		}

		bdev->pins_boot = pinctrl_lookup_state(bdev->pinctrl,
						       "default");
		if (IS_ERR(bdev->pins_boot) && !bdev->boot) {
			err = PTR_ERR(bdev->pins_boot);
			dev_err(&serdev->dev,
				"Should assign RXD to LOW at boot stage\n");
			return err;
		}

		bdev->pins_runtime = pinctrl_lookup_state(bdev->pinctrl,
							  "runtime");
		if (IS_ERR(bdev->pins_runtime)) {
			err = PTR_ERR(bdev->pins_runtime);
			return err;
		}

		bdev->reset = devm_gpiod_get_optional(&serdev->dev, "reset",
						      GPIOD_OUT_LOW);
		if (IS_ERR(bdev->reset)) {
			err = PTR_ERR(bdev->reset);
			return err;
		}
	} else if (btmtkuart_is_builtin_soc(bdev)) {
		bdev->clk = devm_clk_get(&serdev->dev, "ref");
		if (IS_ERR(bdev->clk))
			return PTR_ERR(bdev->clk);
	}

	return 0;
}

static int btmtkuart_probe(struct serdev_device *serdev)
{
	struct btmtkuart_dev *bdev;
	struct hci_dev *hdev;
	int err;

	bdev = devm_kzalloc(&serdev->dev, sizeof(*bdev), GFP_KERNEL);
	if (!bdev)
		return -ENOMEM;

	bdev->data = of_device_get_match_data(&serdev->dev);
	if (!bdev->data)
		return -ENODEV;

	bdev->serdev = serdev;
	serdev_device_set_drvdata(serdev, bdev);

	serdev_device_set_client_ops(serdev, &btmtkuart_client_ops);

	err = btmtkuart_parse_dt(serdev);
	if (err < 0)
		return err;

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

	if (btmtkuart_is_standalone(bdev)) {
		err = clk_prepare_enable(bdev->osc);
		if (err < 0)
			return err;

		if (bdev->boot) {
			gpiod_set_value_cansleep(bdev->boot, 1);
		} else {
			/* Switch to the specific pin state for the booting
			 * requires.
			 */
			pinctrl_select_state(bdev->pinctrl, bdev->pins_boot);
		}

		/* Power on */
		err = regulator_enable(bdev->vcc);
		if (err < 0) {
			clk_disable_unprepare(bdev->osc);
			return err;
		}

		/* Reset if the reset-gpios is available otherwise the board
		 * -level design should be guaranteed.
		 */
		if (bdev->reset) {
			gpiod_set_value_cansleep(bdev->reset, 1);
			usleep_range(1000, 2000);
			gpiod_set_value_cansleep(bdev->reset, 0);
		}

		/* Wait some time until device got ready and switch to the pin
		 * mode the device requires for UART transfers.
		 */
		msleep(50);

		if (bdev->boot)
			devm_gpiod_put(&serdev->dev, bdev->boot);

		pinctrl_select_state(bdev->pinctrl, bdev->pins_runtime);

		/* A standalone device doesn't depends on power domain on SoC,
		 * so mark it as no callbacks.
		 */
		pm_runtime_no_callbacks(&serdev->dev);

		set_bit(BTMTKUART_REQUIRED_WAKEUP, &bdev->tx_state);
	}

	err = hci_register_dev(hdev);
	if (err < 0) {
		dev_err(&serdev->dev, "Can't register HCI device\n");
		hci_free_dev(hdev);
		goto err_regulator_disable;
	}

	return 0;

err_regulator_disable:
	if (btmtkuart_is_standalone(bdev))
		regulator_disable(bdev->vcc);

	return err;
}

static void btmtkuart_remove(struct serdev_device *serdev)
{
	struct btmtkuart_dev *bdev = serdev_device_get_drvdata(serdev);
	struct hci_dev *hdev = bdev->hdev;

	if (btmtkuart_is_standalone(bdev)) {
		regulator_disable(bdev->vcc);
		clk_disable_unprepare(bdev->osc);
	}

	hci_unregister_dev(hdev);
	hci_free_dev(hdev);
}

static const struct btmtkuart_data mt7622_data = {
	.fwname = FIRMWARE_MT7622,
};

static const struct btmtkuart_data mt7663_data = {
	.flags = BTMTKUART_FLAG_STANDALONE_HW,
	.fwname = FIRMWARE_MT7663,
};

static const struct btmtkuart_data mt7668_data = {
	.flags = BTMTKUART_FLAG_STANDALONE_HW,
	.fwname = FIRMWARE_MT7668,
};

#ifdef CONFIG_OF
static const struct of_device_id mtk_of_match_table[] = {
	{ .compatible = "mediatek,mt7622-bluetooth", .data = &mt7622_data},
	{ .compatible = "mediatek,mt7663u-bluetooth", .data = &mt7663_data},
	{ .compatible = "mediatek,mt7668u-bluetooth", .data = &mt7668_data},
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
MODULE_FIRMWARE(FIRMWARE_MT7663);
MODULE_FIRMWARE(FIRMWARE_MT7668);
