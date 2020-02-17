// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of chip-to-host event (aka indications) of WFxxx Split Mac
 * (WSM) API.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
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
#include "secure_link.h"
#include "hif_api_cmd.h"

static int hif_generic_confirm(struct wfx_dev *wdev,
			       const struct hif_msg *hif, const void *buf)
{
	// All confirm messages start with status
	int status = le32_to_cpu(*((__le32 *) buf));
	int cmd = hif->id;
	int len = hif->len - 4; // drop header

	WARN(!mutex_is_locked(&wdev->hif_cmd.lock), "data locking error");

	if (!wdev->hif_cmd.buf_send) {
		dev_warn(wdev->dev, "unexpected confirmation: 0x%.2x\n", cmd);
		return -EINVAL;
	}

	if (cmd != wdev->hif_cmd.buf_send->id) {
		dev_warn(wdev->dev,
			 "chip response mismatch request: 0x%.2x vs 0x%.2x\n",
			 cmd, wdev->hif_cmd.buf_send->id);
		return -EINVAL;
	}

	if (wdev->hif_cmd.buf_recv) {
		if (wdev->hif_cmd.len_recv >= len)
			memcpy(wdev->hif_cmd.buf_recv, buf, len);
		else
			status = -ENOMEM;
	}
	wdev->hif_cmd.ret = status;

	if (!wdev->hif_cmd.async) {
		complete(&wdev->hif_cmd.done);
	} else {
		wdev->hif_cmd.buf_send = NULL;
		mutex_unlock(&wdev->hif_cmd.lock);
		if (cmd != HIF_REQ_ID_SL_EXCHANGE_PUB_KEYS)
			mutex_unlock(&wdev->hif_cmd.key_renew_lock);
	}
	return status;
}

static int hif_tx_confirm(struct wfx_dev *wdev,
			  const struct hif_msg *hif, const void *buf)
{
	const struct hif_cnf_tx *body = buf;
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hif->interface);

	WARN_ON(!wvif);
	if (!wvif)
		return -EFAULT;

	wfx_tx_confirm_cb(wvif, body);
	return 0;
}

static int hif_multi_tx_confirm(struct wfx_dev *wdev,
				const struct hif_msg *hif, const void *buf)
{
	const struct hif_cnf_multi_transmit *body = buf;
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hif->interface);
	int i;

	WARN(body->num_tx_confs <= 0, "corrupted message");
	WARN_ON(!wvif);
	if (!wvif)
		return -EFAULT;

	for (i = 0; i < body->num_tx_confs; i++)
		wfx_tx_confirm_cb(wvif, &body->tx_conf_payload[i]);
	return 0;
}

static int hif_startup_indication(struct wfx_dev *wdev,
				  const struct hif_msg *hif, const void *buf)
{
	const struct hif_ind_startup *body = buf;

	if (body->status || body->firmware_type > 4) {
		dev_err(wdev->dev, "received invalid startup indication");
		return -EINVAL;
	}
	memcpy(&wdev->hw_caps, body, sizeof(struct hif_ind_startup));
	le32_to_cpus(&wdev->hw_caps.status);
	le16_to_cpus(&wdev->hw_caps.hardware_id);
	le16_to_cpus(&wdev->hw_caps.num_inp_ch_bufs);
	le16_to_cpus(&wdev->hw_caps.size_inp_ch_buf);

	complete(&wdev->firmware_ready);
	return 0;
}

static int hif_wakeup_indication(struct wfx_dev *wdev,
				 const struct hif_msg *hif, const void *buf)
{
	if (!wdev->pdata.gpio_wakeup
	    || !gpiod_get_value(wdev->pdata.gpio_wakeup)) {
		dev_warn(wdev->dev, "unexpected wake-up indication\n");
		return -EIO;
	}
	return 0;
}

static int hif_keys_indication(struct wfx_dev *wdev,
			       const struct hif_msg *hif, const void *buf)
{
	const struct hif_ind_sl_exchange_pub_keys *body = buf;
	u8 pubkey[API_NCP_PUB_KEY_SIZE];

	// SL_PUB_KEY_EXCHANGE_STATUS_SUCCESS is used by legacy secure link
	if (body->status && body->status != SL_PUB_KEY_EXCHANGE_STATUS_SUCCESS)
		dev_warn(wdev->dev, "secure link negociation error\n");
	memcpy(pubkey, body->ncp_pub_key, sizeof(pubkey));
	memreverse(pubkey, sizeof(pubkey));
	wfx_sl_check_pubkey(wdev, pubkey, body->ncp_pub_key_mac);
	return 0;
}

static int hif_receive_indication(struct wfx_dev *wdev,
				  const struct hif_msg *hif,
				  const void *buf, struct sk_buff *skb)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hif->interface);
	const struct hif_ind_rx *body = buf;

	if (!wvif) {
		dev_warn(wdev->dev, "ignore rx data for non-existent vif %d\n",
			 hif->interface);
		return 0;
	}
	skb_pull(skb, sizeof(struct hif_msg) + sizeof(struct hif_ind_rx));
	wfx_rx_cb(wvif, body, skb);

	return 0;
}

static int hif_event_indication(struct wfx_dev *wdev,
				const struct hif_msg *hif, const void *buf)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hif->interface);
	const struct hif_ind_event *body = buf;
	struct wfx_hif_event *event;
	int first;

	WARN_ON(!wvif);
	if (!wvif)
		return 0;

	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	memcpy(&event->evt, body, sizeof(struct hif_ind_event));
	spin_lock(&wvif->event_queue_lock);
	first = list_empty(&wvif->event_queue);
	list_add_tail(&event->link, &wvif->event_queue);
	spin_unlock(&wvif->event_queue_lock);

	if (first)
		schedule_work(&wvif->event_handler_work);

	return 0;
}

static int hif_pm_mode_complete_indication(struct wfx_dev *wdev,
					   const struct hif_msg *hif,
					   const void *buf)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hif->interface);

	WARN_ON(!wvif);
	complete(&wvif->set_pm_mode_complete);

	return 0;
}

static int hif_scan_complete_indication(struct wfx_dev *wdev,
					const struct hif_msg *hif,
					const void *buf)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hif->interface);

	WARN_ON(!wvif);
	wfx_scan_complete(wvif);

	return 0;
}

static int hif_join_complete_indication(struct wfx_dev *wdev,
					const struct hif_msg *hif,
					const void *buf)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hif->interface);

	WARN_ON(!wvif);
	dev_warn(wdev->dev, "unattended JoinCompleteInd\n");

	return 0;
}

static int hif_suspend_resume_indication(struct wfx_dev *wdev,
					 const struct hif_msg *hif,
					 const void *buf)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hif->interface);
	const struct hif_ind_suspend_resume_tx *body = buf;

	WARN_ON(!wvif);
	WARN(!body->suspend_resume_flags.bc_mc_only, "unsupported suspend/resume notification");
	if (body->suspend_resume_flags.resume)
		wfx_suspend_resume_mc(wvif, STA_NOTIFY_AWAKE);
	else
		wfx_suspend_resume_mc(wvif, STA_NOTIFY_SLEEP);

	return 0;
}

static int hif_error_indication(struct wfx_dev *wdev,
				const struct hif_msg *hif, const void *buf)
{
	const struct hif_ind_error *body = buf;
	u8 *pRollback = (u8 *) body->data;
	u32 *pStatus = (u32 *) body->data;

	switch (body->type) {
	case HIF_ERROR_FIRMWARE_ROLLBACK:
		dev_err(wdev->dev,
			"asynchronous error: firmware rollback error %d\n",
			*pRollback);
		break;
	case HIF_ERROR_FIRMWARE_DEBUG_ENABLED:
		dev_err(wdev->dev, "asynchronous error: firmware debug feature enabled\n");
		break;
	case HIF_ERROR_OUTDATED_SESSION_KEY:
		dev_err(wdev->dev, "asynchronous error: secure link outdated key: %#.8x\n",
			*pStatus);
		break;
	case HIF_ERROR_INVALID_SESSION_KEY:
		dev_err(wdev->dev, "asynchronous error: invalid session key\n");
		break;
	case HIF_ERROR_OOR_VOLTAGE:
		dev_err(wdev->dev, "asynchronous error: out-of-range overvoltage: %#.8x\n",
			*pStatus);
		break;
	case HIF_ERROR_PDS_VERSION:
		dev_err(wdev->dev,
			"asynchronous error: wrong PDS payload or version: %#.8x\n",
			*pStatus);
		break;
	default:
		dev_err(wdev->dev, "asynchronous error: unknown (%d)\n",
			body->type);
		break;
	}
	return 0;
}

static int hif_generic_indication(struct wfx_dev *wdev,
				  const struct hif_msg *hif, const void *buf)
{
	const struct hif_ind_generic *body = buf;

	switch (body->indication_type) {
	case HIF_GENERIC_INDICATION_TYPE_RAW:
		return 0;
	case HIF_GENERIC_INDICATION_TYPE_STRING:
		dev_info(wdev->dev, "firmware says: %s\n",
			 (char *) body->indication_data.raw_data);
		return 0;
	case HIF_GENERIC_INDICATION_TYPE_RX_STATS:
		mutex_lock(&wdev->rx_stats_lock);
		// Older firmware send a generic indication beside RxStats
		if (!wfx_api_older_than(wdev, 1, 4))
			dev_info(wdev->dev, "Rx test ongoing. Temperature: %d°C\n",
				 body->indication_data.rx_stats.current_temp);
		memcpy(&wdev->rx_stats, &body->indication_data.rx_stats,
		       sizeof(wdev->rx_stats));
		mutex_unlock(&wdev->rx_stats_lock);
		return 0;
	default:
		dev_err(wdev->dev,
			"generic_indication: unknown indication type: %#.8x\n",
			body->indication_type);
		return -EIO;
	}
}

static int hif_exception_indication(struct wfx_dev *wdev,
				    const struct hif_msg *hif, const void *buf)
{
	size_t len = hif->len - 4; // drop header

	dev_err(wdev->dev, "firmware exception\n");
	print_hex_dump_bytes("Dump: ", DUMP_PREFIX_NONE, buf, len);
	wdev->chip_frozen = 1;

	return -1;
}

static const struct {
	int msg_id;
	int (*handler)(struct wfx_dev *wdev,
		       const struct hif_msg *hif, const void *buf);
} hif_handlers[] = {
	/* Confirmations */
	{ HIF_CNF_ID_TX,                   hif_tx_confirm },
	{ HIF_CNF_ID_MULTI_TRANSMIT,       hif_multi_tx_confirm },
	/* Indications */
	{ HIF_IND_ID_STARTUP,              hif_startup_indication },
	{ HIF_IND_ID_WAKEUP,               hif_wakeup_indication },
	{ HIF_IND_ID_JOIN_COMPLETE,        hif_join_complete_indication },
	{ HIF_IND_ID_SET_PM_MODE_CMPL,     hif_pm_mode_complete_indication },
	{ HIF_IND_ID_SCAN_CMPL,            hif_scan_complete_indication },
	{ HIF_IND_ID_SUSPEND_RESUME_TX,    hif_suspend_resume_indication },
	{ HIF_IND_ID_SL_EXCHANGE_PUB_KEYS, hif_keys_indication },
	{ HIF_IND_ID_EVENT,                hif_event_indication },
	{ HIF_IND_ID_GENERIC,              hif_generic_indication },
	{ HIF_IND_ID_ERROR,                hif_error_indication },
	{ HIF_IND_ID_EXCEPTION,            hif_exception_indication },
	// FIXME: allocate skb_p from hif_receive_indication and make it generic
	//{ HIF_IND_ID_RX,                 hif_receive_indication },
};

void wfx_handle_rx(struct wfx_dev *wdev, struct sk_buff *skb)
{
	int i;
	const struct hif_msg *hif = (const struct hif_msg *)skb->data;
	int hif_id = hif->id;

	if (hif_id == HIF_IND_ID_RX) {
		// hif_receive_indication take care of skb lifetime
		hif_receive_indication(wdev, hif, hif->body, skb);
		return;
	}
	// Note: mutex_is_lock cause an implicit memory barrier that protect
	// buf_send
	if (mutex_is_locked(&wdev->hif_cmd.lock)
	    && wdev->hif_cmd.buf_send
	    && wdev->hif_cmd.buf_send->id == hif_id) {
		hif_generic_confirm(wdev, hif, hif->body);
		goto free;
	}
	for (i = 0; i < ARRAY_SIZE(hif_handlers); i++) {
		if (hif_handlers[i].msg_id == hif_id) {
			if (hif_handlers[i].handler)
				hif_handlers[i].handler(wdev, hif, hif->body);
			goto free;
		}
	}
	if (hif_id & 0x80)
		dev_err(wdev->dev, "unsupported HIF indication: ID %02x\n",
			hif_id);
	else
		dev_err(wdev->dev, "unexpected HIF confirmation: ID %02x\n",
			hif_id);
free:
	dev_kfree_skb(skb);
}
