// SPDX-License-Identifier: GPL-2.0-only
/*
 * Handling of the chip-to-host events (aka indications) of the hardware API.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/skbuff.h>
#include <linux/etherdevice.h>

#include "hif_rx.h"
#include "wfx.h"
#include "scan.h"
#include "bh.h"
#include "sta.h"
#include "data_rx.h"
#include "hif_api_cmd.h"

static int wfx_hif_generic_confirm(struct wfx_dev *wdev,
				   const struct wfx_hif_msg *hif, const void *buf)
{
	/* All confirm messages start with status */
	int status = le32_to_cpup((__le32 *)buf);
	int cmd = hif->id;
	int len = le16_to_cpu(hif->len) - 4; /* drop header */

	WARN(!mutex_is_locked(&wdev->hif_cmd.lock), "data locking error");

	if (!wdev->hif_cmd.buf_send) {
		dev_warn(wdev->dev, "unexpected confirmation: 0x%.2x\n", cmd);
		return -EINVAL;
	}

	if (cmd != wdev->hif_cmd.buf_send->id) {
		dev_warn(wdev->dev, "chip response mismatch request: 0x%.2x vs 0x%.2x\n",
			 cmd, wdev->hif_cmd.buf_send->id);
		return -EINVAL;
	}

	if (wdev->hif_cmd.buf_recv) {
		if (wdev->hif_cmd.len_recv >= len && len > 0)
			memcpy(wdev->hif_cmd.buf_recv, buf, len);
		else
			status = -EIO;
	}
	wdev->hif_cmd.ret = status;

	complete(&wdev->hif_cmd.done);
	return status;
}

static int wfx_hif_tx_confirm(struct wfx_dev *wdev,
			      const struct wfx_hif_msg *hif, const void *buf)
{
	const struct wfx_hif_cnf_tx *body = buf;

	wfx_tx_confirm_cb(wdev, body);
	return 0;
}

static int wfx_hif_multi_tx_confirm(struct wfx_dev *wdev,
				    const struct wfx_hif_msg *hif, const void *buf)
{
	const struct wfx_hif_cnf_multi_transmit *body = buf;
	int i;

	WARN(body->num_tx_confs <= 0, "corrupted message");
	for (i = 0; i < body->num_tx_confs; i++)
		wfx_tx_confirm_cb(wdev, &body->tx_conf_payload[i]);
	return 0;
}

static int wfx_hif_startup_indication(struct wfx_dev *wdev,
				      const struct wfx_hif_msg *hif, const void *buf)
{
	const struct wfx_hif_ind_startup *body = buf;

	if (body->status || body->firmware_type > 4) {
		dev_err(wdev->dev, "received invalid startup indication");
		return -EINVAL;
	}
	memcpy(&wdev->hw_caps, body, sizeof(struct wfx_hif_ind_startup));
	complete(&wdev->firmware_ready);
	return 0;
}

static int wfx_hif_wakeup_indication(struct wfx_dev *wdev,
				     const struct wfx_hif_msg *hif, const void *buf)
{
	if (!wdev->pdata.gpio_wakeup || gpiod_get_value(wdev->pdata.gpio_wakeup) == 0) {
		dev_warn(wdev->dev, "unexpected wake-up indication\n");
		return -EIO;
	}
	return 0;
}

static int wfx_hif_receive_indication(struct wfx_dev *wdev, const struct wfx_hif_msg *hif,
				      const void *buf, struct sk_buff *skb)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hif->interface);
	const struct wfx_hif_ind_rx *body = buf;

	if (!wvif) {
		dev_warn(wdev->dev, "%s: received event for non-existent vif\n", __func__);
		return -EIO;
	}
	skb_pull(skb, sizeof(struct wfx_hif_msg) + sizeof(struct wfx_hif_ind_rx));
	wfx_rx_cb(wvif, body, skb);

	return 0;
}

static int wfx_hif_event_indication(struct wfx_dev *wdev,
				    const struct wfx_hif_msg *hif, const void *buf)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hif->interface);
	const struct wfx_hif_ind_event *body = buf;
	int type = le32_to_cpu(body->event_id);

	if (!wvif) {
		dev_warn(wdev->dev, "%s: received event for non-existent vif\n", __func__);
		return -EIO;
	}

	switch (type) {
	case HIF_EVENT_IND_RCPI_RSSI:
		wfx_event_report_rssi(wvif, body->event_data.rcpi_rssi);
		break;
	case HIF_EVENT_IND_BSSLOST:
		schedule_delayed_work(&wvif->beacon_loss_work, 0);
		break;
	case HIF_EVENT_IND_BSSREGAINED:
		cancel_delayed_work(&wvif->beacon_loss_work);
		dev_dbg(wdev->dev, "ignore BSSREGAINED indication\n");
		break;
	case HIF_EVENT_IND_PS_MODE_ERROR:
		dev_warn(wdev->dev, "error while processing power save request: %d\n",
			 le32_to_cpu(body->event_data.ps_mode_error));
		break;
	default:
		dev_warn(wdev->dev, "unhandled event indication: %.2x\n", type);
		break;
	}
	return 0;
}

static int wfx_hif_pm_mode_complete_indication(struct wfx_dev *wdev,
					       const struct wfx_hif_msg *hif, const void *buf)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hif->interface);

	if (!wvif) {
		dev_warn(wdev->dev, "%s: received event for non-existent vif\n", __func__);
		return -EIO;
	}
	complete(&wvif->set_pm_mode_complete);

	return 0;
}

static int wfx_hif_scan_complete_indication(struct wfx_dev *wdev,
					    const struct wfx_hif_msg *hif, const void *buf)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hif->interface);
	const struct wfx_hif_ind_scan_cmpl *body = buf;

	if (!wvif) {
		dev_warn(wdev->dev, "%s: received event for non-existent vif\n", __func__);
		return -EIO;
	}

	wfx_scan_complete(wvif, body->num_channels_completed);

	return 0;
}

static int wfx_hif_join_complete_indication(struct wfx_dev *wdev,
					    const struct wfx_hif_msg *hif, const void *buf)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hif->interface);

	if (!wvif) {
		dev_warn(wdev->dev, "%s: received event for non-existent vif\n", __func__);
		return -EIO;
	}
	dev_warn(wdev->dev, "unattended JoinCompleteInd\n");

	return 0;
}

static int wfx_hif_suspend_resume_indication(struct wfx_dev *wdev,
					     const struct wfx_hif_msg *hif, const void *buf)
{
	const struct wfx_hif_ind_suspend_resume_tx *body = buf;
	struct wfx_vif *wvif;

	if (body->bc_mc_only) {
		wvif = wdev_to_wvif(wdev, hif->interface);
		if (!wvif) {
			dev_warn(wdev->dev, "%s: received event for non-existent vif\n", __func__);
			return -EIO;
		}
		if (body->resume)
			wfx_suspend_resume_mc(wvif, STA_NOTIFY_AWAKE);
		else
			wfx_suspend_resume_mc(wvif, STA_NOTIFY_SLEEP);
	} else {
		WARN(body->peer_sta_set, "misunderstood indication");
		WARN(hif->interface != 2, "misunderstood indication");
		if (body->resume)
			wfx_suspend_hot_dev(wdev, STA_NOTIFY_AWAKE);
		else
			wfx_suspend_hot_dev(wdev, STA_NOTIFY_SLEEP);
	}

	return 0;
}

static int wfx_hif_generic_indication(struct wfx_dev *wdev,
				      const struct wfx_hif_msg *hif, const void *buf)
{
	const struct wfx_hif_ind_generic *body = buf;
	int type = le32_to_cpu(body->type);

	switch (type) {
	case HIF_GENERIC_INDICATION_TYPE_RAW:
		return 0;
	case HIF_GENERIC_INDICATION_TYPE_STRING:
		dev_info(wdev->dev, "firmware says: %s\n", (char *)&body->data);
		return 0;
	case HIF_GENERIC_INDICATION_TYPE_RX_STATS:
		mutex_lock(&wdev->rx_stats_lock);
		/* Older firmware send a generic indication beside RxStats */
		if (!wfx_api_older_than(wdev, 1, 4))
			dev_info(wdev->dev, "Rx test ongoing. Temperature: %d degrees C\n",
				 body->data.rx_stats.current_temp);
		memcpy(&wdev->rx_stats, &body->data.rx_stats, sizeof(wdev->rx_stats));
		mutex_unlock(&wdev->rx_stats_lock);
		return 0;
	case HIF_GENERIC_INDICATION_TYPE_TX_POWER_LOOP_INFO:
		mutex_lock(&wdev->tx_power_loop_info_lock);
		memcpy(&wdev->tx_power_loop_info, &body->data.tx_power_loop_info,
		       sizeof(wdev->tx_power_loop_info));
		mutex_unlock(&wdev->tx_power_loop_info_lock);
		return 0;
	default:
		dev_err(wdev->dev, "generic_indication: unknown indication type: %#.8x\n", type);
		return -EIO;
	}
}

static const struct {
	int val;
	const char *str;
	bool has_param;
} hif_errors[] = {
	{ HIF_ERROR_FIRMWARE_ROLLBACK,
		"rollback status" },
	{ HIF_ERROR_FIRMWARE_DEBUG_ENABLED,
		"debug feature enabled" },
	{ HIF_ERROR_PDS_PAYLOAD,
		"PDS version is not supported" },
	{ HIF_ERROR_PDS_TESTFEATURE,
		"PDS ask for an unknown test mode" },
	{ HIF_ERROR_OOR_VOLTAGE,
		"out-of-range power supply voltage", true },
	{ HIF_ERROR_OOR_TEMPERATURE,
		"out-of-range temperature", true },
	{ HIF_ERROR_SLK_REQ_DURING_KEY_EXCHANGE,
		"secure link does not expect request during key exchange" },
	{ HIF_ERROR_SLK_SESSION_KEY,
		"secure link session key is invalid" },
	{ HIF_ERROR_SLK_OVERFLOW,
		"secure link overflow" },
	{ HIF_ERROR_SLK_WRONG_ENCRYPTION_STATE,
		"secure link messages list does not match message encryption" },
	{ HIF_ERROR_SLK_UNCONFIGURED,
		"secure link not yet configured" },
	{ HIF_ERROR_HIF_BUS_FREQUENCY_TOO_LOW,
		"bus clock is too slow (<1kHz)" },
	{ HIF_ERROR_HIF_RX_DATA_TOO_LARGE,
		"HIF message too large" },
	/* Following errors only exists in old firmware versions: */
	{ HIF_ERROR_HIF_TX_QUEUE_FULL,
		"HIF messages queue is full" },
	{ HIF_ERROR_HIF_BUS,
		"HIF bus" },
	{ HIF_ERROR_SLK_MULTI_TX_UNSUPPORTED,
		"secure link does not support multi-tx confirmations" },
	{ HIF_ERROR_SLK_OUTDATED_SESSION_KEY,
		"secure link session key is outdated" },
	{ HIF_ERROR_SLK_DECRYPTION,
		"secure link params (nonce or tag) mismatch" },
};

static int wfx_hif_error_indication(struct wfx_dev *wdev,
				    const struct wfx_hif_msg *hif, const void *buf)
{
	const struct wfx_hif_ind_error *body = buf;
	int type = le32_to_cpu(body->type);
	int param = (s8)body->data[0];
	int i;

	for (i = 0; i < ARRAY_SIZE(hif_errors); i++)
		if (type == hif_errors[i].val)
			break;
	if (i < ARRAY_SIZE(hif_errors))
		if (hif_errors[i].has_param)
			dev_err(wdev->dev, "asynchronous error: %s: %d\n",
				hif_errors[i].str, param);
		else
			dev_err(wdev->dev, "asynchronous error: %s\n", hif_errors[i].str);
	else
		dev_err(wdev->dev, "asynchronous error: unknown: %08x\n", type);
	print_hex_dump(KERN_INFO, "hif: ", DUMP_PREFIX_OFFSET,
		       16, 1, hif, le16_to_cpu(hif->len), false);
	wdev->chip_frozen = true;

	return 0;
};

static int wfx_hif_exception_indication(struct wfx_dev *wdev,
					const struct wfx_hif_msg *hif, const void *buf)
{
	const struct wfx_hif_ind_exception *body = buf;
	int type = le32_to_cpu(body->type);

	if (type == 4)
		dev_err(wdev->dev, "firmware assert %d\n", le32_to_cpup((__le32 *)body->data));
	else
		dev_err(wdev->dev, "firmware exception\n");
	print_hex_dump(KERN_INFO, "hif: ", DUMP_PREFIX_OFFSET,
		       16, 1, hif, le16_to_cpu(hif->len), false);
	wdev->chip_frozen = true;

	return -1;
}

static const struct {
	int msg_id;
	int (*handler)(struct wfx_dev *wdev, const struct wfx_hif_msg *hif, const void *buf);
} hif_handlers[] = {
	/* Confirmations */
	{ HIF_CNF_ID_TX,                wfx_hif_tx_confirm },
	{ HIF_CNF_ID_MULTI_TRANSMIT,    wfx_hif_multi_tx_confirm },
	/* Indications */
	{ HIF_IND_ID_STARTUP,           wfx_hif_startup_indication },
	{ HIF_IND_ID_WAKEUP,            wfx_hif_wakeup_indication },
	{ HIF_IND_ID_JOIN_COMPLETE,     wfx_hif_join_complete_indication },
	{ HIF_IND_ID_SET_PM_MODE_CMPL,  wfx_hif_pm_mode_complete_indication },
	{ HIF_IND_ID_SCAN_CMPL,         wfx_hif_scan_complete_indication },
	{ HIF_IND_ID_SUSPEND_RESUME_TX, wfx_hif_suspend_resume_indication },
	{ HIF_IND_ID_EVENT,             wfx_hif_event_indication },
	{ HIF_IND_ID_GENERIC,           wfx_hif_generic_indication },
	{ HIF_IND_ID_ERROR,             wfx_hif_error_indication },
	{ HIF_IND_ID_EXCEPTION,         wfx_hif_exception_indication },
	/* FIXME: allocate skb_p from wfx_hif_receive_indication and make it generic */
	//{ HIF_IND_ID_RX,              wfx_hif_receive_indication },
};

void wfx_handle_rx(struct wfx_dev *wdev, struct sk_buff *skb)
{
	int i;
	const struct wfx_hif_msg *hif = (const struct wfx_hif_msg *)skb->data;
	int hif_id = hif->id;

	if (hif_id == HIF_IND_ID_RX) {
		/* wfx_hif_receive_indication take care of skb lifetime */
		wfx_hif_receive_indication(wdev, hif, hif->body, skb);
		return;
	}
	/* Note: mutex_is_lock cause an implicit memory barrier that protect buf_send */
	if (mutex_is_locked(&wdev->hif_cmd.lock) &&
	    wdev->hif_cmd.buf_send && wdev->hif_cmd.buf_send->id == hif_id) {
		wfx_hif_generic_confirm(wdev, hif, hif->body);
		goto free;
	}
	for (i = 0; i < ARRAY_SIZE(hif_handlers); i++) {
		if (hif_handlers[i].msg_id == hif_id) {
			if (hif_handlers[i].handler)
				hif_handlers[i].handler(wdev, hif, hif->body);
			goto free;
		}
	}
	if (hif_id & HIF_ID_IS_INDICATION)
		dev_err(wdev->dev, "unsupported HIF indication: ID %02x\n", hif_id);
	else
		dev_err(wdev->dev, "unexpected HIF confirmation: ID %02x\n", hif_id);
free:
	dev_kfree_skb(skb);
}
