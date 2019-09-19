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
#include "secure_link.h"
#include "hif_api_cmd.h"

static int hif_generic_confirm(struct wfx_dev *wdev, struct hif_msg *hif, void *buf)
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
		dev_warn(wdev->dev, "chip response mismatch request: 0x%.2x vs 0x%.2x\n",
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

static int hif_startup_indication(struct wfx_dev *wdev, struct hif_msg *hif, void *buf)
{
	struct hif_ind_startup *body = buf;

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

static int hif_wakeup_indication(struct wfx_dev *wdev, struct hif_msg *hif, void *buf)
{
	if (!wdev->pdata.gpio_wakeup
	    || !gpiod_get_value(wdev->pdata.gpio_wakeup)) {
		dev_warn(wdev->dev, "unexpected wake-up indication\n");
		return -EIO;
	}
	return 0;
}

static int hif_keys_indication(struct wfx_dev *wdev, struct hif_msg *hif, void *buf)
{
	struct hif_ind_sl_exchange_pub_keys *body = buf;

	// Compatibility with legacy secure link
	if (body->status == SL_PUB_KEY_EXCHANGE_STATUS_SUCCESS)
		body->status = 0;
	if (body->status)
		dev_warn(wdev->dev, "secure link negociation error\n");
	wfx_sl_check_pubkey(wdev, body->ncp_pub_key, body->ncp_pub_key_mac);
	return 0;
}

static const struct {
	int msg_id;
	int (*handler)(struct wfx_dev *wdev, struct hif_msg *hif, void *buf);
} hif_handlers[] = {
	{ HIF_IND_ID_STARTUP,              hif_startup_indication },
	{ HIF_IND_ID_WAKEUP,               hif_wakeup_indication },
	{ HIF_IND_ID_SL_EXCHANGE_PUB_KEYS, hif_keys_indication },
};

void wfx_handle_rx(struct wfx_dev *wdev, struct sk_buff *skb)
{
	int i;
	struct hif_msg *hif = (struct hif_msg *) skb->data;
	int hif_id = hif->id;

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
	dev_err(wdev->dev, "unsupported HIF ID %02x\n", hif_id);
free:
	dev_kfree_skb(skb);
}
