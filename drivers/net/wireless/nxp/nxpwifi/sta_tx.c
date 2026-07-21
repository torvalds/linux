// SPDX-License-Identifier: GPL-2.0-only
/*
 * NXP Wireless LAN device driver: station TX data handling
 *
 * Copyright 2011-2024 NXP
 */

#include "cfg.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "cmdevt.h"
#include "wmm.h"

/*
 * Fill TxPD for TX packets by inserting it before payload and setting required fields.
 */
void nxpwifi_process_sta_txpd(struct nxpwifi_private *priv,
			      struct sk_buff *skb)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct txpd *local_tx_pd;
	struct nxpwifi_txinfo *tx_info = NXPWIFI_SKB_TXCB(skb);
	unsigned int pad;
	u16 pkt_type, pkt_length, pkt_offset;
	int hroom = adapter->intf_hdr_len;
	u32 tx_control;

	pkt_type = nxpwifi_is_skb_mgmt_frame(skb) ? PKT_TYPE_MGMT : 0;

	pad = ((uintptr_t)skb->data - (sizeof(*local_tx_pd) + hroom)) &
	       (NXPWIFI_DMA_ALIGN_SZ - 1);
	skb_push(skb, sizeof(*local_tx_pd) + pad);

	local_tx_pd = (struct txpd *)skb->data;
	memset(local_tx_pd, 0, sizeof(struct txpd));
	local_tx_pd->bss_num = priv->bss_num;
	local_tx_pd->bss_type = priv->bss_type;

	pkt_length = (u16)(skb->len - (sizeof(struct txpd) + pad));
	if (pkt_type == PKT_TYPE_MGMT)
		pkt_length -= NXPWIFI_MGMT_FRAME_HEADER_SIZE;
	local_tx_pd->tx_pkt_length = cpu_to_le16(pkt_length);

	local_tx_pd->priority = (u8)skb->priority;
	local_tx_pd->pkt_delay_2ms =
				nxpwifi_wmm_compute_drv_pkt_delay(priv, skb);

	if (tx_info->flags & NXPWIFI_BUF_FLAG_EAPOL_TX_STATUS ||
	    tx_info->flags & NXPWIFI_BUF_FLAG_ACTION_TX_STATUS) {
		local_tx_pd->tx_token_id = tx_info->ack_frame_id;
		local_tx_pd->flags |= NXPWIFI_TXPD_FLAGS_REQ_TX_STATUS;
	}

	if (local_tx_pd->priority <
	    ARRAY_SIZE(priv->wmm.user_pri_pkt_tx_ctrl)) {
		/*
		 * Set the priority specific tx_control field, setting of 0 will
		 *   cause the default value to be used later in this function
		 */
		tx_control =
			priv->wmm.user_pri_pkt_tx_ctrl[local_tx_pd->priority];
		local_tx_pd->tx_control = cpu_to_le32(tx_control);
	}

	if (adapter->pps_uapsd_mode) {
		if (nxpwifi_check_last_packet_indication(priv)) {
			adapter->tx_lock_flag = true;
			local_tx_pd->flags =
				NXPWIFI_TxPD_POWER_MGMT_LAST_PACKET;
		}
	}

	/* Offset of actual data */
	pkt_offset = sizeof(struct txpd) + pad;
	if (pkt_type == PKT_TYPE_MGMT) {
		/* Set the packet type and add header for management frame */
		local_tx_pd->tx_pkt_type = cpu_to_le16(pkt_type);
		pkt_offset += NXPWIFI_MGMT_FRAME_HEADER_SIZE;
	}

	local_tx_pd->tx_pkt_offset = cpu_to_le16(pkt_offset);

	/* make space for adapter->intf_hdr_len */
	skb_push(skb, hroom);

	if (!local_tx_pd->tx_control)
		/* TxCtrl set by user or default */
		local_tx_pd->tx_control = cpu_to_le32(priv->pkt_tx_ctrl);
}

/* Send a NULL-data frame with TxPD at highest priority. */
int nxpwifi_send_null_packet(struct nxpwifi_private *priv, u8 flags)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct txpd *local_tx_pd;
	struct nxpwifi_tx_param tx_param;
/* sizeof(struct txpd) + Interface specific header */
#define NULL_PACKET_HDR 64
	u32 data_len = NULL_PACKET_HDR;
	struct sk_buff *skb;
	int ret;
	struct nxpwifi_txinfo *tx_info = NULL;

	if (test_bit(NXPWIFI_SURPRISE_REMOVED, &adapter->work_flags))
		return -EPERM;

	if (!priv->media_connected)
		return -EPERM;

	if (adapter->data_sent)
		return -EBUSY;

	skb = dev_alloc_skb(data_len);
	if (!skb)
		return -ENOMEM;

	tx_info = NXPWIFI_SKB_TXCB(skb);
	memset(tx_info, 0, sizeof(*tx_info));
	tx_info->bss_num = priv->bss_num;
	tx_info->bss_type = priv->bss_type;
	tx_info->pkt_len = data_len -
			(sizeof(struct txpd) + adapter->intf_hdr_len);
	skb_reserve(skb, sizeof(struct txpd) + adapter->intf_hdr_len);
	skb_push(skb, sizeof(struct txpd));

	local_tx_pd = (struct txpd *)skb->data;
	local_tx_pd->tx_control = cpu_to_le32(priv->pkt_tx_ctrl);
	local_tx_pd->flags = flags;
	local_tx_pd->priority = WMM_HIGHEST_PRIORITY;
	local_tx_pd->tx_pkt_offset = cpu_to_le16(sizeof(struct txpd));
	local_tx_pd->bss_num = priv->bss_num;
	local_tx_pd->bss_type = priv->bss_type;

	skb_push(skb, adapter->intf_hdr_len);
	tx_param.next_pkt_len = 0;
	ret = adapter->if_ops.host_to_card(adapter, NXPWIFI_TYPE_DATA,
					   skb, &tx_param);

	switch (ret) {
	case -EBUSY:
		dev_kfree_skb_any(skb);
		nxpwifi_dbg(adapter, ERROR,
			    "%s: host_to_card failed: ret=%d\n",
			    __func__, ret);
		adapter->dbg.num_tx_host_to_card_failure++;
		break;
	case 0:
		dev_kfree_skb_any(skb);
		nxpwifi_dbg(adapter, DATA,
			    "data: %s: host_to_card succeeded\n",
			    __func__);
		adapter->tx_lock_flag = true;
		break;
	case -EINPROGRESS:
		adapter->tx_lock_flag = true;
		break;
	default:
		dev_kfree_skb_any(skb);
		nxpwifi_dbg(adapter, ERROR,
			    "%s: host_to_card failed: ret=%d\n",
			    __func__, ret);
		adapter->dbg.num_tx_host_to_card_failure++;
		break;
	}

	return ret;
}

/* Check whether a last‑packet indication needs to be sent. */
u8 nxpwifi_check_last_packet_indication(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	u8 ret = false;

	if (!adapter->sleep_period.period)
		return ret;
	if (nxpwifi_wmm_lists_empty(adapter))
		ret = true;

	if (ret && !adapter->cmd_sent && !adapter->curr_cmd &&
	    !nxpwifi_is_command_pending(adapter)) {
		adapter->delay_null_pkt = false;
		ret = true;
	} else {
		ret = false;
		adapter->delay_null_pkt = true;
	}
	return ret;
}
