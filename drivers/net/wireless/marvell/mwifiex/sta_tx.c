// SPDX-License-Identifier: GPL-2.0-only
/*
 * NXP Wireless LAN device driver: station TX data handling
 *
 * Copyright 2011-2020 NXP
 */

#include "decl.h"
#include "ioctl.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "wmm.h"

/*
 * This function fills the TxPD for tx packets.
 *
 * The Tx buffer received by this function should already have the
 * header space allocated for TxPD.
 *
 * This function inserts the TxPD in between interface header and actual
 * data and adjusts the buffer pointers accordingly.
 *
 * The following TxPD fields are set by this function, as required -
 *      - BSS number
 *      - Tx packet length and offset
 *      - Priority
 *      - Packet delay
 *      - Priority specific Tx control
 *      - Flags
 */
void *mwifiex_process_sta_txpd(struct mwifiex_private *priv,
				struct sk_buff *skb)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct txpd *local_tx_pd;
	struct mwifiex_txinfo *tx_info = MWIFIEX_SKB_TXCB(skb);
	unsigned int pad;
	u16 pkt_type, pkt_offset;
	int hroom = adapter->intf_hdr_len;

	if (!skb->len) {
		mwifiex_dbg(adapter, ERROR,
			    "Tx: bad packet length: %d\n", skb->len);
		tx_info->status_code = -1;
		return skb->data;
	}

	BUG_ON(skb_headroom(skb) < MWIFIEX_MIN_DATA_HEADER_LEN);

	pkt_type = mwifiex_is_skb_mgmt_frame(skb) ? PKT_TYPE_MGMT : 0;

	pad = ((uintptr_t)skb->data - (sizeof(*local_tx_pd) + hroom)) &
	       (MWIFIEX_DMA_ALIGN_SZ - 1);
	skb_push(skb, sizeof(*local_tx_pd) + pad);

	local_tx_pd = (struct txpd *) skb->data;
	memset(local_tx_pd, 0, sizeof(struct txpd));
	local_tx_pd->bss_num = priv->bss_num;
	local_tx_pd->bss_type = priv->bss_type;
	local_tx_pd->tx_pkt_length = cpu_to_le16((u16)(skb->len -
						       (sizeof(struct txpd) +
							pad)));

	local_tx_pd->priority = (u8) skb->priority;
	local_tx_pd->pkt_delay_2ms =
				mwifiex_wmm_compute_drv_pkt_delay(priv, skb);

	if (tx_info->flags & MWIFIEX_BUF_FLAG_EAPOL_TX_STATUS ||
	    tx_info->flags & MWIFIEX_BUF_FLAG_ACTION_TX_STATUS) {
		local_tx_pd->tx_token_id = tx_info->ack_frame_id;
		local_tx_pd->flags |= MWIFIEX_TXPD_FLAGS_REQ_TX_STATUS;
	}

	if (local_tx_pd->priority <
	    ARRAY_SIZE(priv->wmm.user_pri_pkt_tx_ctrl))
		/*
		 * Set the priority specific tx_control field, setting of 0 will
		 *   cause the default value to be used later in this function
		 */
		local_tx_pd->tx_control =
			cpu_to_le32(priv->wmm.user_pri_pkt_tx_ctrl[local_tx_pd->
								   priority]);

	if (adapter->pps_uapsd_mode) {
		if (mwifiex_check_last_packet_indication(priv)) {
			adapter->tx_lock_flag = true;
			local_tx_pd->flags =
				MWIFIEX_TxPD_POWER_MGMT_LAST_PACKET;
		}
	}

	if (tx_info->flags & MWIFIEX_BUF_FLAG_TDLS_PKT)
		local_tx_pd->flags |= MWIFIEX_TXPD_FLAGS_TDLS_PACKET;

	/* Offset of actual data */
	pkt_offset = sizeof(struct txpd) + pad;
	if (pkt_type == PKT_TYPE_MGMT) {
		/* Set the packet type and add header for management frame */
		local_tx_pd->tx_pkt_type = cpu_to_le16(pkt_type);
		pkt_offset += MWIFIEX_MGMT_FRAME_HEADER_SIZE;
	}

	local_tx_pd->tx_pkt_offset = cpu_to_le16(pkt_offset);

	/* make space for adapter->intf_hdr_len */
	skb_push(skb, hroom);

	if (!local_tx_pd->tx_control)
		/* TxCtrl set by user or default */
		local_tx_pd->tx_control = cpu_to_le32(priv->pkt_tx_ctrl);

	return skb->data;
}

/*
 * This function tells firmware to send a NULL data packet.
 *
 * The function creates a NULL data packet with TxPD and sends to the
 * firmware for transmission, with highest priority setting.
 */
int mwifiex_send_null_packet(struct mwifiex_private *priv, u8 flags)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct txpd *local_tx_pd;
	struct mwifiex_tx_param tx_param;
/* sizeof(struct txpd) + Interface specific header */
#define NULL_PACKET_HDR 64
	u32 data_len = NULL_PACKET_HDR;
	struct sk_buff *skb;
	int ret;
	struct mwifiex_txinfo *tx_info = NULL;

	if (test_bit(MWIFIEX_SURPRISE_REMOVED, &adapter->work_flags))
		return -1;

	if (!priv->media_connected)
		return -1;

	if (adapter->data_sent)
		return -1;

	if (adapter->if_ops.is_port_ready &&
	    !adapter->if_ops.is_port_ready(priv))
		return -1;

	skb = dev_alloc_skb(data_len);
	if (!skb)
		return -1;

	tx_info = MWIFIEX_SKB_TXCB(skb);
	memset(tx_info, 0, sizeof(*tx_info));
	tx_info->bss_num = priv->bss_num;
	tx_info->bss_type = priv->bss_type;
	tx_info->pkt_len = data_len -
			(sizeof(struct txpd) + adapter->intf_hdr_len);
	skb_reserve(skb, sizeof(struct txpd) + adapter->intf_hdr_len);
	skb_push(skb, sizeof(struct txpd));

	local_tx_pd = (struct txpd *) skb->data;
	local_tx_pd->tx_control = cpu_to_le32(priv->pkt_tx_ctrl);
	local_tx_pd->flags = flags;
	local_tx_pd->priority = WMM_HIGHEST_PRIORITY;
	local_tx_pd->tx_pkt_offset = cpu_to_le16(sizeof(struct txpd));
	local_tx_pd->bss_num = priv->bss_num;
	local_tx_pd->bss_type = priv->bss_type;

	skb_push(skb, adapter->intf_hdr_len);
	if (adapter->iface_type == MWIFIEX_USB) {
		ret = adapter->if_ops.host_to_card(adapter, priv->usb_port,
						   skb, NULL);
	} else {
		tx_param.next_pkt_len = 0;
		ret = adapter->if_ops.host_to_card(adapter, MWIFIEX_TYPE_DATA,
						   skb, &tx_param);
	}
	switch (ret) {
	case -EBUSY:
		dev_kfree_skb_any(skb);
		mwifiex_dbg(adapter, ERROR,
			    "%s: host_to_card failed: ret=%d\n",
			    __func__, ret);
		adapter->dbg.num_tx_host_to_card_failure++;
		break;
	case -1:
		dev_kfree_skb_any(skb);
		mwifiex_dbg(adapter, ERROR,
			    "%s: host_to_card failed: ret=%d\n",
			    __func__, ret);
		adapter->dbg.num_tx_host_to_card_failure++;
		break;
	case 0:
		dev_kfree_skb_any(skb);
		mwifiex_dbg(adapter, DATA,
			    "data: %s: host_to_card succeeded\n",
			    __func__);
		adapter->tx_lock_flag = true;
		break;
	case -EINPROGRESS:
		adapter->tx_lock_flag = true;
		break;
	default:
		break;
	}

	return ret;
}

/*
 * This function checks if we need to send last packet indication.
 */
u8
mwifiex_check_last_packet_indication(struct mwifiex_private *priv)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	u8 ret = false;

	if (!adapter->sleep_period.period)
		return ret;
	if (mwifiex_wmm_lists_empty(adapter))
			ret = true;

	if (ret && !adapter->cmd_sent && !adapter->curr_cmd &&
	    !is_command_pending(adapter)) {
		adapter->delay_null_pkt = false;
		ret = true;
	} else {
		ret = false;
		adapter->delay_null_pkt = true;
	}
	return ret;
}
