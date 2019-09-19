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
#include "hif_api_cmd.h"

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

static const struct {
	int msg_id;
	int (*handler)(struct wfx_dev *wdev, struct hif_msg *hif, void *buf);
} hif_handlers[] = {
	{ HIF_IND_ID_STARTUP,              hif_startup_indication },
};

void wfx_handle_rx(struct wfx_dev *wdev, struct sk_buff *skb)
{
	int i;
	struct hif_msg *hif = (struct hif_msg *) skb->data;
	int hif_id = hif->id;

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
