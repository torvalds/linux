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

static const char *wmi_cmd_to_name(enum wmi_cmd_id wmi_cmd)
{
	switch (wmi_cmd) {
	case WMI_ECHO_CMDID:
		return "WMI_ECHO_CMDID";
	case WMI_ACCESS_MEMORY_CMDID:
		return "WMI_ACCESS_MEMORY_CMDID";
	case WMI_DISABLE_INTR_CMDID:
		return "WMI_DISABLE_INTR_CMDID";
	case WMI_ENABLE_INTR_CMDID:
		return "WMI_ENABLE_INTR_CMDID";
	case WMI_RX_LINK_CMDID:
		return "WMI_RX_LINK_CMDID";
	case WMI_ATH_INIT_CMDID:
		return "WMI_ATH_INIT_CMDID";
	case WMI_ABORT_TXQ_CMDID:
		return "WMI_ABORT_TXQ_CMDID";
	case WMI_STOP_TX_DMA_CMDID:
		return "WMI_STOP_TX_DMA_CMDID";
	case WMI_STOP_DMA_RECV_CMDID:
		return "WMI_STOP_DMA_RECV_CMDID";
	case WMI_ABORT_TX_DMA_CMDID:
		return "WMI_ABORT_TX_DMA_CMDID";
	case WMI_DRAIN_TXQ_CMDID:
		return "WMI_DRAIN_TXQ_CMDID";
	case WMI_DRAIN_TXQ_ALL_CMDID:
		return "WMI_DRAIN_TXQ_ALL_CMDID";
	case WMI_START_RECV_CMDID:
		return "WMI_START_RECV_CMDID";
	case WMI_STOP_RECV_CMDID:
		return "WMI_STOP_RECV_CMDID";
	case WMI_FLUSH_RECV_CMDID:
		return "WMI_FLUSH_RECV_CMDID";
	case WMI_SET_MODE_CMDID:
		return "WMI_SET_MODE_CMDID";
	case WMI_RESET_CMDID:
		return "WMI_RESET_CMDID";
	case WMI_NODE_CREATE_CMDID:
		return "WMI_NODE_CREATE_CMDID";
	case WMI_NODE_REMOVE_CMDID:
		return "WMI_NODE_REMOVE_CMDID";
	case WMI_VAP_REMOVE_CMDID:
		return "WMI_VAP_REMOVE_CMDID";
	case WMI_VAP_CREATE_CMDID:
		return "WMI_VAP_CREATE_CMDID";
	case WMI_BEACON_UPDATE_CMDID:
		return "WMI_BEACON_UPDATE_CMDID";
	case WMI_REG_READ_CMDID:
		return "WMI_REG_READ_CMDID";
	case WMI_REG_WRITE_CMDID:
		return "WMI_REG_WRITE_CMDID";
	case WMI_RC_STATE_CHANGE_CMDID:
		return "WMI_RC_STATE_CHANGE_CMDID";
	case WMI_RC_RATE_UPDATE_CMDID:
		return "WMI_RC_RATE_UPDATE_CMDID";
	case WMI_DEBUG_INFO_CMDID:
		return "WMI_DEBUG_INFO_CMDID";
	case WMI_HOST_ATTACH:
		return "WMI_HOST_ATTACH";
	case WMI_TARGET_IC_UPDATE_CMDID:
		return "WMI_TARGET_IC_UPDATE_CMDID";
	case WMI_TGT_STATS_CMDID:
		return "WMI_TGT_STATS_CMDID";
	case WMI_TX_AGGR_ENABLE_CMDID:
		return "WMI_TX_AGGR_ENABLE_CMDID";
	case WMI_TGT_DETACH_CMDID:
		return "WMI_TGT_DETACH_CMDID";
	case WMI_TGT_TXQ_ENABLE_CMDID:
		return "WMI_TGT_TXQ_ENABLE_CMDID";
	case WMI_AGGR_LIMIT_CMD:
		return "WMI_AGGR_LIMIT_CMD";
	}

	return "Bogus";
}

struct wmi *ath9k_init_wmi(struct ath9k_htc_priv *priv)
{
	struct wmi *wmi;

	wmi = kzalloc(sizeof(struct wmi), GFP_KERNEL);
	if (!wmi)
		return NULL;

	wmi->drv_priv = priv;
	wmi->stopped = false;
	mutex_init(&wmi->op_mutex);
	mutex_init(&wmi->multi_write_mutex);
	init_completion(&wmi->cmd_wait);

	return wmi;
}

void ath9k_deinit_wmi(struct ath9k_htc_priv *priv)
{
	struct wmi *wmi = priv->wmi;

	mutex_lock(&wmi->op_mutex);
	wmi->stopped = true;
	mutex_unlock(&wmi->op_mutex);

	kfree(priv->wmi);
}

void ath9k_swba_tasklet(unsigned long data)
{
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *)data;
	struct ath_common *common = ath9k_hw_common(priv->ah);

	ath_dbg(common, ATH_DBG_WMI, "SWBA Event received\n");

	ath9k_htc_swba(priv, priv->wmi->beacon_pending);

}

void ath9k_fatal_work(struct work_struct *work)
{
	struct ath9k_htc_priv *priv = container_of(work, struct ath9k_htc_priv,
						   fatal_work);
	struct ath_common *common = ath9k_hw_common(priv->ah);

	ath_dbg(common, ATH_DBG_FATAL, "FATAL Event received, resetting device\n");
	ath9k_htc_reset(priv);
}

static void ath9k_wmi_rsp_callback(struct wmi *wmi, struct sk_buff *skb)
{
	skb_pull(skb, sizeof(struct wmi_cmd_hdr));

	if (wmi->cmd_rsp_buf != NULL && wmi->cmd_rsp_len != 0)
		memcpy(wmi->cmd_rsp_buf, skb->data, wmi->cmd_rsp_len);

	complete(&wmi->cmd_wait);
}

static void ath9k_wmi_ctrl_rx(void *priv, struct sk_buff *skb,
			      enum htc_endpoint_id epid)
{
	struct wmi *wmi = (struct wmi *) priv;
	struct wmi_cmd_hdr *hdr;
	u16 cmd_id;
	void *wmi_event;
#ifdef CONFIG_ATH9K_HTC_DEBUGFS
	__be32 txrate;
#endif

	if (unlikely(wmi->stopped))
		goto free_skb;

	hdr = (struct wmi_cmd_hdr *) skb->data;
	cmd_id = be16_to_cpu(hdr->command_id);

	if (cmd_id & 0x1000) {
		wmi_event = skb_pull(skb, sizeof(struct wmi_cmd_hdr));
		switch (cmd_id) {
		case WMI_SWBA_EVENTID:
			wmi->beacon_pending = *(u8 *)wmi_event;
			tasklet_schedule(&wmi->drv_priv->swba_tasklet);
			break;
		case WMI_FATAL_EVENTID:
			ieee80211_queue_work(wmi->drv_priv->hw,
					     &wmi->drv_priv->fatal_work);
			break;
		case WMI_TXRATE_EVENTID:
#ifdef CONFIG_ATH9K_HTC_DEBUGFS
			txrate = ((struct wmi_event_txrate *)wmi_event)->txrate;
			wmi->drv_priv->debug.txrate = be32_to_cpu(txrate);
#endif
			break;
		default:
			break;
		}
		kfree_skb(skb);
		return;
	}

	/* Check if there has been a timeout. */
	spin_lock(&wmi->wmi_lock);
	if (cmd_id != wmi->last_cmd_id) {
		spin_unlock(&wmi->wmi_lock);
		goto free_skb;
	}
	spin_unlock(&wmi->wmi_lock);

	/* WMI command response */
	ath9k_wmi_rsp_callback(wmi, skb);

free_skb:
	kfree_skb(skb);
}

static void ath9k_wmi_ctrl_tx(void *priv, struct sk_buff *skb,
			      enum htc_endpoint_id epid, bool txok)
{
	kfree_skb(skb);
}

int ath9k_wmi_connect(struct htc_target *htc, struct wmi *wmi,
		      enum htc_endpoint_id *wmi_ctrl_epid)
{
	struct htc_service_connreq connect;
	int ret;

	wmi->htc = htc;

	memset(&connect, 0, sizeof(connect));

	connect.ep_callbacks.priv = wmi;
	connect.ep_callbacks.tx = ath9k_wmi_ctrl_tx;
	connect.ep_callbacks.rx = ath9k_wmi_ctrl_rx;
	connect.service_id = WMI_CONTROL_SVC;

	ret = htc_connect_service(htc, &connect, &wmi->ctrl_epid);
	if (ret)
		return ret;

	*wmi_ctrl_epid = wmi->ctrl_epid;

	return 0;
}

static int ath9k_wmi_cmd_issue(struct wmi *wmi,
			       struct sk_buff *skb,
			       enum wmi_cmd_id cmd, u16 len)
{
	struct wmi_cmd_hdr *hdr;

	hdr = (struct wmi_cmd_hdr *) skb_push(skb, sizeof(struct wmi_cmd_hdr));
	hdr->command_id = cpu_to_be16(cmd);
	hdr->seq_no = cpu_to_be16(++wmi->tx_seq_id);

	return htc_send(wmi->htc, skb, wmi->ctrl_epid, NULL);
}

int ath9k_wmi_cmd(struct wmi *wmi, enum wmi_cmd_id cmd_id,
		  u8 *cmd_buf, u32 cmd_len,
		  u8 *rsp_buf, u32 rsp_len,
		  u32 timeout)
{
	struct ath_hw *ah = wmi->drv_priv->ah;
	struct ath_common *common = ath9k_hw_common(ah);
	u16 headroom = sizeof(struct htc_frame_hdr) +
		       sizeof(struct wmi_cmd_hdr);
	struct sk_buff *skb;
	u8 *data;
	int time_left, ret = 0;
	unsigned long flags;

	if (ah->ah_flags & AH_UNPLUGGED)
		return 0;

	skb = alloc_skb(headroom + cmd_len, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, headroom);

	if (cmd_len != 0 && cmd_buf != NULL) {
		data = (u8 *) skb_put(skb, cmd_len);
		memcpy(data, cmd_buf, cmd_len);
	}

	mutex_lock(&wmi->op_mutex);

	/* check if wmi stopped flag is set */
	if (unlikely(wmi->stopped)) {
		ret = -EPROTO;
		goto out;
	}

	/* record the rsp buffer and length */
	wmi->cmd_rsp_buf = rsp_buf;
	wmi->cmd_rsp_len = rsp_len;

	spin_lock_irqsave(&wmi->wmi_lock, flags);
	wmi->last_cmd_id = cmd_id;
	spin_unlock_irqrestore(&wmi->wmi_lock, flags);

	ret = ath9k_wmi_cmd_issue(wmi, skb, cmd_id, cmd_len);
	if (ret)
		goto out;

	time_left = wait_for_completion_timeout(&wmi->cmd_wait, timeout);
	if (!time_left) {
		ath_dbg(common, ATH_DBG_WMI,
			"Timeout waiting for WMI command: %s\n",
			wmi_cmd_to_name(cmd_id));
		mutex_unlock(&wmi->op_mutex);
		return -ETIMEDOUT;
	}

	mutex_unlock(&wmi->op_mutex);

	return 0;

out:
	ath_dbg(common, ATH_DBG_WMI,
		"WMI failure for: %s\n", wmi_cmd_to_name(cmd_id));
	mutex_unlock(&wmi->op_mutex);
	kfree_skb(skb);

	return ret;
}
