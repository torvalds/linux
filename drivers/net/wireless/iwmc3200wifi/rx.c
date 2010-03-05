/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <linux/if_arp.h>
#include <linux/list.h>
#include <net/iw_handler.h>

#include "iwm.h"
#include "debug.h"
#include "hal.h"
#include "umac.h"
#include "lmac.h"
#include "commands.h"
#include "rx.h"
#include "cfg80211.h"
#include "eeprom.h"

static int iwm_rx_check_udma_hdr(struct iwm_udma_in_hdr *hdr)
{
	if ((le32_to_cpu(hdr->cmd) == UMAC_PAD_TERMINAL) ||
	    (le32_to_cpu(hdr->size) == UMAC_PAD_TERMINAL))
		return -EINVAL;

	return 0;
}

static inline int iwm_rx_resp_size(struct iwm_udma_in_hdr *hdr)
{
	return ALIGN(le32_to_cpu(hdr->size) + sizeof(struct iwm_udma_in_hdr),
		     16);
}

/*
 * Notification handlers:
 *
 * For every possible notification we can receive from the
 * target, we have a handler.
 * When we get a target notification, and there is no one
 * waiting for it, it's just processed through the rx code
 * path:
 *
 * iwm_rx_handle()
 *  -> iwm_rx_handle_umac()
 *      -> iwm_rx_handle_wifi()
 *          -> iwm_rx_handle_resp()
 *              -> iwm_ntf_*()
 *
 *      OR
 *
 *      -> iwm_rx_handle_non_wifi()
 *
 * If there are processes waiting for this notification, then
 * iwm_rx_handle_wifi() just wakes those processes up and they
 * grab the pending notification.
 */
static int iwm_ntf_error(struct iwm_priv *iwm, u8 *buf,
			 unsigned long buf_size, struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_notif_error *error;
	struct iwm_fw_error_hdr *fw_err;

	error = (struct iwm_umac_notif_error *)buf;
	fw_err = &error->err;

	memcpy(iwm->last_fw_err, fw_err, sizeof(struct iwm_fw_error_hdr));

	IWM_ERR(iwm, "%cMAC FW ERROR:\n",
	 (le32_to_cpu(fw_err->category) == UMAC_SYS_ERR_CAT_LMAC) ? 'L' : 'U');
	IWM_ERR(iwm, "\tCategory:    %d\n", le32_to_cpu(fw_err->category));
	IWM_ERR(iwm, "\tStatus:      0x%x\n", le32_to_cpu(fw_err->status));
	IWM_ERR(iwm, "\tPC:          0x%x\n", le32_to_cpu(fw_err->pc));
	IWM_ERR(iwm, "\tblink1:      %d\n", le32_to_cpu(fw_err->blink1));
	IWM_ERR(iwm, "\tblink2:      %d\n", le32_to_cpu(fw_err->blink2));
	IWM_ERR(iwm, "\tilink1:      %d\n", le32_to_cpu(fw_err->ilink1));
	IWM_ERR(iwm, "\tilink2:      %d\n", le32_to_cpu(fw_err->ilink2));
	IWM_ERR(iwm, "\tData1:       0x%x\n", le32_to_cpu(fw_err->data1));
	IWM_ERR(iwm, "\tData2:       0x%x\n", le32_to_cpu(fw_err->data2));
	IWM_ERR(iwm, "\tLine number: %d\n", le32_to_cpu(fw_err->line_num));
	IWM_ERR(iwm, "\tUMAC status: 0x%x\n", le32_to_cpu(fw_err->umac_status));
	IWM_ERR(iwm, "\tLMAC status: 0x%x\n", le32_to_cpu(fw_err->lmac_status));
	IWM_ERR(iwm, "\tSDIO status: 0x%x\n", le32_to_cpu(fw_err->sdio_status));

	iwm_resetting(iwm);

	return 0;
}

static int iwm_ntf_umac_alive(struct iwm_priv *iwm, u8 *buf,
			      unsigned long buf_size, struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_notif_alive *alive_resp =
			(struct iwm_umac_notif_alive *)(buf);
	u16 status = le16_to_cpu(alive_resp->status);

	if (status == UMAC_NTFY_ALIVE_STATUS_ERR) {
		IWM_ERR(iwm, "Receive error UMAC_ALIVE\n");
		return -EIO;
	}

	iwm_tx_credit_init_pools(iwm, alive_resp);

	return 0;
}

static int iwm_ntf_init_complete(struct iwm_priv *iwm, u8 *buf,
				 unsigned long buf_size,
				 struct iwm_wifi_cmd *cmd)
{
	struct wiphy *wiphy = iwm_to_wiphy(iwm);
	struct iwm_umac_notif_init_complete *init_complete =
			(struct iwm_umac_notif_init_complete *)(buf);
	u16 status = le16_to_cpu(init_complete->status);
	bool blocked = (status == UMAC_NTFY_INIT_COMPLETE_STATUS_ERR);

	if (blocked)
		IWM_DBG_NTF(iwm, DBG, "Hardware rf kill is on (radio off)\n");
	else
		IWM_DBG_NTF(iwm, DBG, "Hardware rf kill is off (radio on)\n");

	wiphy_rfkill_set_hw_state(wiphy, blocked);

	return 0;
}

static int iwm_ntf_tx_credit_update(struct iwm_priv *iwm, u8 *buf,
				    unsigned long buf_size,
				    struct iwm_wifi_cmd *cmd)
{
	int pool_nr, total_freed_pages;
	unsigned long pool_map;
	int i, id;
	struct iwm_umac_notif_page_dealloc *dealloc =
			(struct iwm_umac_notif_page_dealloc *)buf;

	pool_nr = GET_VAL32(dealloc->changes, UMAC_DEALLOC_NTFY_CHANGES_CNT);
	pool_map = GET_VAL32(dealloc->changes, UMAC_DEALLOC_NTFY_CHANGES_MSK);

	IWM_DBG_TX(iwm, DBG, "UMAC dealloc notification: pool nr %d, "
		   "update map 0x%lx\n", pool_nr, pool_map);

	spin_lock(&iwm->tx_credit.lock);

	for (i = 0; i < pool_nr; i++) {
		id = GET_VAL32(dealloc->grp_info[i],
			       UMAC_DEALLOC_NTFY_GROUP_NUM);
		if (test_bit(id, &pool_map)) {
			total_freed_pages = GET_VAL32(dealloc->grp_info[i],
					      UMAC_DEALLOC_NTFY_PAGE_CNT);
			iwm_tx_credit_inc(iwm, id, total_freed_pages);
		}
	}

	spin_unlock(&iwm->tx_credit.lock);

	return 0;
}

static int iwm_ntf_umac_reset(struct iwm_priv *iwm, u8 *buf,
			      unsigned long buf_size, struct iwm_wifi_cmd *cmd)
{
	IWM_DBG_NTF(iwm, DBG, "UMAC RESET done\n");

	return 0;
}

static int iwm_ntf_lmac_version(struct iwm_priv *iwm, u8 *buf,
				unsigned long buf_size,
				struct iwm_wifi_cmd *cmd)
{
	IWM_DBG_NTF(iwm, INFO, "LMAC Version: %x.%x\n", buf[9], buf[8]);

	return 0;
}

static int iwm_ntf_tx(struct iwm_priv *iwm, u8 *buf,
		      unsigned long buf_size, struct iwm_wifi_cmd *cmd)
{
	struct iwm_lmac_tx_resp *tx_resp;
	struct iwm_umac_wifi_in_hdr *hdr;

	tx_resp = (struct iwm_lmac_tx_resp *)
		(buf + sizeof(struct iwm_umac_wifi_in_hdr));
	hdr = (struct iwm_umac_wifi_in_hdr *)buf;

	IWM_DBG_TX(iwm, DBG, "REPLY_TX, buf size: %lu\n", buf_size);

	IWM_DBG_TX(iwm, DBG, "Seqnum: %d\n",
		   le16_to_cpu(hdr->sw_hdr.cmd.seq_num));
	IWM_DBG_TX(iwm, DBG, "\tFrame cnt: %d\n", tx_resp->frame_cnt);
	IWM_DBG_TX(iwm, DBG, "\tRetry cnt: %d\n",
		   le16_to_cpu(tx_resp->retry_cnt));
	IWM_DBG_TX(iwm, DBG, "\tSeq ctl: %d\n", le16_to_cpu(tx_resp->seq_ctl));
	IWM_DBG_TX(iwm, DBG, "\tByte cnt: %d\n",
		   le16_to_cpu(tx_resp->byte_cnt));
	IWM_DBG_TX(iwm, DBG, "\tStatus: 0x%x\n", le32_to_cpu(tx_resp->status));

	return 0;
}


static int iwm_ntf_calib_res(struct iwm_priv *iwm, u8 *buf,
			     unsigned long buf_size, struct iwm_wifi_cmd *cmd)
{
	u8 opcode;
	u8 *calib_buf;
	struct iwm_lmac_calib_hdr *hdr = (struct iwm_lmac_calib_hdr *)
				(buf + sizeof(struct iwm_umac_wifi_in_hdr));

	opcode = hdr->opcode;

	BUG_ON(opcode >= CALIBRATION_CMD_NUM ||
	       opcode < PHY_CALIBRATE_OPCODES_NUM);

	IWM_DBG_NTF(iwm, DBG, "Store calibration result for opcode: %d\n",
		    opcode);

	buf_size -= sizeof(struct iwm_umac_wifi_in_hdr);
	calib_buf = iwm->calib_res[opcode].buf;

	if (!calib_buf || (iwm->calib_res[opcode].size < buf_size)) {
		kfree(calib_buf);
		calib_buf = kzalloc(buf_size, GFP_KERNEL);
		if (!calib_buf) {
			IWM_ERR(iwm, "Memory allocation failed: calib_res\n");
			return -ENOMEM;
		}
		iwm->calib_res[opcode].buf = calib_buf;
		iwm->calib_res[opcode].size = buf_size;
	}

	memcpy(calib_buf, hdr, buf_size);
	set_bit(opcode - PHY_CALIBRATE_OPCODES_NUM, &iwm->calib_done_map);

	return 0;
}

static int iwm_ntf_calib_complete(struct iwm_priv *iwm, u8 *buf,
				  unsigned long buf_size,
				  struct iwm_wifi_cmd *cmd)
{
	IWM_DBG_NTF(iwm, DBG, "Calibration completed\n");

	return 0;
}

static int iwm_ntf_calib_cfg(struct iwm_priv *iwm, u8 *buf,
			     unsigned long buf_size, struct iwm_wifi_cmd *cmd)
{
	struct iwm_lmac_cal_cfg_resp *cal_resp;

	cal_resp = (struct iwm_lmac_cal_cfg_resp *)
			(buf + sizeof(struct iwm_umac_wifi_in_hdr));

	IWM_DBG_NTF(iwm, DBG, "Calibration CFG command status: %d\n",
		    le32_to_cpu(cal_resp->status));

	return 0;
}

static int iwm_ntf_wifi_status(struct iwm_priv *iwm, u8 *buf,
			       unsigned long buf_size, struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_notif_wifi_status *status =
		(struct iwm_umac_notif_wifi_status *)buf;

	iwm->core_enabled |= le16_to_cpu(status->status);

	return 0;
}

static struct iwm_rx_ticket_node *
iwm_rx_ticket_node_alloc(struct iwm_priv *iwm, struct iwm_rx_ticket *ticket)
{
	struct iwm_rx_ticket_node *ticket_node;

	ticket_node = kzalloc(sizeof(struct iwm_rx_ticket_node), GFP_KERNEL);
	if (!ticket_node) {
		IWM_ERR(iwm, "Couldn't allocate ticket node\n");
		return ERR_PTR(-ENOMEM);
	}

	ticket_node->ticket = kzalloc(sizeof(struct iwm_rx_ticket), GFP_KERNEL);
	if (!ticket_node->ticket) {
		IWM_ERR(iwm, "Couldn't allocate RX ticket\n");
		kfree(ticket_node);
		return ERR_PTR(-ENOMEM);
	}

	memcpy(ticket_node->ticket, ticket, sizeof(struct iwm_rx_ticket));
	INIT_LIST_HEAD(&ticket_node->node);

	return ticket_node;
}

static void iwm_rx_ticket_node_free(struct iwm_rx_ticket_node *ticket_node)
{
	kfree(ticket_node->ticket);
	kfree(ticket_node);
}

static struct iwm_rx_packet *iwm_rx_packet_get(struct iwm_priv *iwm, u16 id)
{
	u8 id_hash = IWM_RX_ID_GET_HASH(id);
	struct list_head *packet_list;
	struct iwm_rx_packet *packet, *next;

	packet_list = &iwm->rx_packets[id_hash];

	list_for_each_entry_safe(packet, next, packet_list, node)
		if (packet->id == id)
			return packet;

	return NULL;
}

static struct iwm_rx_packet *iwm_rx_packet_alloc(struct iwm_priv *iwm, u8 *buf,
						 u32 size, u16 id)
{
	struct iwm_rx_packet *packet;

	packet = kzalloc(sizeof(struct iwm_rx_packet), GFP_KERNEL);
	if (!packet) {
		IWM_ERR(iwm, "Couldn't allocate packet\n");
		return ERR_PTR(-ENOMEM);
	}

	packet->skb = dev_alloc_skb(size);
	if (!packet->skb) {
		IWM_ERR(iwm, "Couldn't allocate packet SKB\n");
		kfree(packet);
		return ERR_PTR(-ENOMEM);
	}

	packet->pkt_size = size;

	skb_put(packet->skb, size);
	memcpy(packet->skb->data, buf, size);
	INIT_LIST_HEAD(&packet->node);
	packet->id = id;

	return packet;
}

void iwm_rx_free(struct iwm_priv *iwm)
{
	struct iwm_rx_ticket_node *ticket, *nt;
	struct iwm_rx_packet *packet, *np;
	int i;

	list_for_each_entry_safe(ticket, nt, &iwm->rx_tickets, node) {
		list_del(&ticket->node);
		iwm_rx_ticket_node_free(ticket);
	}

	for (i = 0; i < IWM_RX_ID_HASH; i++) {
		list_for_each_entry_safe(packet, np, &iwm->rx_packets[i],
					 node) {
			list_del(&packet->node);
			kfree_skb(packet->skb);
			kfree(packet);
		}
	}
}

static int iwm_ntf_rx_ticket(struct iwm_priv *iwm, u8 *buf,
			     unsigned long buf_size, struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_notif_rx_ticket *ntf_rx_ticket =
		(struct iwm_umac_notif_rx_ticket *)buf;
	struct iwm_rx_ticket *ticket =
		(struct iwm_rx_ticket *)ntf_rx_ticket->tickets;
	int i, schedule_rx = 0;

	for (i = 0; i < ntf_rx_ticket->num_tickets; i++) {
		struct iwm_rx_ticket_node *ticket_node;

		switch (le16_to_cpu(ticket->action)) {
		case IWM_RX_TICKET_RELEASE:
		case IWM_RX_TICKET_DROP:
			/* We can push the packet to the stack */
			ticket_node = iwm_rx_ticket_node_alloc(iwm, ticket);
			if (IS_ERR(ticket_node))
				return PTR_ERR(ticket_node);

			IWM_DBG_RX(iwm, DBG, "TICKET %s(%d)\n",
				   ticket->action ==  IWM_RX_TICKET_RELEASE ?
				   "RELEASE" : "DROP",
				   ticket->id);
			list_add_tail(&ticket_node->node, &iwm->rx_tickets);

			/*
			 * We received an Rx ticket, most likely there's
			 * a packet pending for it, it's not worth going
			 * through the packet hash list to double check.
			 * Let's just fire the rx worker..
			 */
			schedule_rx = 1;

			break;

		default:
			IWM_ERR(iwm, "Invalid RX ticket action: 0x%x\n",
				ticket->action);
		}

		ticket++;
	}

	if (schedule_rx)
		queue_work(iwm->rx_wq, &iwm->rx_worker);

	return 0;
}

static int iwm_ntf_rx_packet(struct iwm_priv *iwm, u8 *buf,
			     unsigned long buf_size, struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_wifi_in_hdr *wifi_hdr;
	struct iwm_rx_packet *packet;
	u16 id, buf_offset;
	u32 packet_size;

	IWM_DBG_RX(iwm, DBG, "\n");

	wifi_hdr = (struct iwm_umac_wifi_in_hdr *)buf;
	id = le16_to_cpu(wifi_hdr->sw_hdr.cmd.seq_num);
	buf_offset = sizeof(struct iwm_umac_wifi_in_hdr);
	packet_size = buf_size - sizeof(struct iwm_umac_wifi_in_hdr);

	IWM_DBG_RX(iwm, DBG, "CMD:0x%x, seqnum: %d, packet size: %d\n",
		   wifi_hdr->sw_hdr.cmd.cmd, id, packet_size);
	IWM_DBG_RX(iwm, DBG, "Packet id: %d\n", id);
	IWM_HEXDUMP(iwm, DBG, RX, "PACKET: ", buf + buf_offset, packet_size);

	packet = iwm_rx_packet_alloc(iwm, buf + buf_offset, packet_size, id);
	if (IS_ERR(packet))
		return PTR_ERR(packet);

	list_add_tail(&packet->node, &iwm->rx_packets[IWM_RX_ID_GET_HASH(id)]);

	/* We might (unlikely) have received the packet _after_ the ticket */
	queue_work(iwm->rx_wq, &iwm->rx_worker);

	return 0;
}

/* MLME handlers */
static int iwm_mlme_assoc_start(struct iwm_priv *iwm, u8 *buf,
				unsigned long buf_size,
				struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_notif_assoc_start *start;

	start = (struct iwm_umac_notif_assoc_start *)buf;

	IWM_DBG_MLME(iwm, INFO, "Association with %pM Started, reason: %d\n",
		     start->bssid, le32_to_cpu(start->roam_reason));

	wake_up_interruptible(&iwm->mlme_queue);

	return 0;
}

static u8 iwm_is_open_wep_profile(struct iwm_priv *iwm)
{
	if ((iwm->umac_profile->sec.ucast_cipher == UMAC_CIPHER_TYPE_WEP_40 ||
	     iwm->umac_profile->sec.ucast_cipher == UMAC_CIPHER_TYPE_WEP_104) &&
	    (iwm->umac_profile->sec.ucast_cipher ==
	     iwm->umac_profile->sec.mcast_cipher) &&
	    (iwm->umac_profile->sec.auth_type == UMAC_AUTH_TYPE_OPEN))
	       return 1;

       return 0;
}

static int iwm_mlme_assoc_complete(struct iwm_priv *iwm, u8 *buf,
				   unsigned long buf_size,
				   struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_notif_assoc_complete *complete =
		(struct iwm_umac_notif_assoc_complete *)buf;

	IWM_DBG_MLME(iwm, INFO, "Association with %pM completed, status: %d\n",
		     complete->bssid, complete->status);

	switch (le32_to_cpu(complete->status)) {
	case UMAC_ASSOC_COMPLETE_SUCCESS:
		set_bit(IWM_STATUS_ASSOCIATED, &iwm->status);
		memcpy(iwm->bssid, complete->bssid, ETH_ALEN);
		iwm->channel = complete->channel;

		/* Internal roaming state, avoid notifying SME. */
		if (!test_and_clear_bit(IWM_STATUS_SME_CONNECTING, &iwm->status)
		    && iwm->conf.mode == UMAC_MODE_BSS) {
			cancel_delayed_work(&iwm->disconnect);
			cfg80211_roamed(iwm_to_ndev(iwm),
					complete->bssid,
					iwm->req_ie, iwm->req_ie_len,
					iwm->resp_ie, iwm->resp_ie_len,
					GFP_KERNEL);
			break;
		}

		iwm_link_on(iwm);

		if (iwm->conf.mode == UMAC_MODE_IBSS)
			goto ibss;

		if (!test_bit(IWM_STATUS_RESETTING, &iwm->status))
			cfg80211_connect_result(iwm_to_ndev(iwm),
						complete->bssid,
						iwm->req_ie, iwm->req_ie_len,
						iwm->resp_ie, iwm->resp_ie_len,
						WLAN_STATUS_SUCCESS,
						GFP_KERNEL);
		else
			cfg80211_roamed(iwm_to_ndev(iwm),
					complete->bssid,
					iwm->req_ie, iwm->req_ie_len,
					iwm->resp_ie, iwm->resp_ie_len,
					GFP_KERNEL);
		break;
	case UMAC_ASSOC_COMPLETE_FAILURE:
		clear_bit(IWM_STATUS_ASSOCIATED, &iwm->status);
		memset(iwm->bssid, 0, ETH_ALEN);
		iwm->channel = 0;

		/* Internal roaming state, avoid notifying SME. */
		if (!test_and_clear_bit(IWM_STATUS_SME_CONNECTING, &iwm->status)
		    && iwm->conf.mode == UMAC_MODE_BSS) {
			cancel_delayed_work(&iwm->disconnect);
			break;
		}

		iwm_link_off(iwm);

		if (iwm->conf.mode == UMAC_MODE_IBSS)
			goto ibss;

		if (!test_bit(IWM_STATUS_RESETTING, &iwm->status))
			if (!iwm_is_open_wep_profile(iwm)) {
				cfg80211_connect_result(iwm_to_ndev(iwm),
					       complete->bssid,
					       NULL, 0, NULL, 0,
					       WLAN_STATUS_UNSPECIFIED_FAILURE,
					       GFP_KERNEL);
			} else {
				/* Let's try shared WEP auth */
				IWM_ERR(iwm, "Trying WEP shared auth\n");
				schedule_work(&iwm->auth_retry_worker);
			}
		else
			cfg80211_disconnected(iwm_to_ndev(iwm), 0, NULL, 0,
					      GFP_KERNEL);
		break;
	default:
		break;
	}

	clear_bit(IWM_STATUS_RESETTING, &iwm->status);
	return 0;

 ibss:
	cfg80211_ibss_joined(iwm_to_ndev(iwm), iwm->bssid, GFP_KERNEL);
	clear_bit(IWM_STATUS_RESETTING, &iwm->status);
	return 0;
}

static int iwm_mlme_profile_invalidate(struct iwm_priv *iwm, u8 *buf,
				       unsigned long buf_size,
				       struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_notif_profile_invalidate *invalid;
	u32 reason;

	invalid = (struct iwm_umac_notif_profile_invalidate *)buf;
	reason = le32_to_cpu(invalid->reason);

	IWM_DBG_MLME(iwm, INFO, "Profile Invalidated. Reason: %d\n", reason);

	if (reason != UMAC_PROFILE_INVALID_REQUEST &&
	    test_bit(IWM_STATUS_SME_CONNECTING, &iwm->status))
		cfg80211_connect_result(iwm_to_ndev(iwm), NULL, NULL, 0, NULL,
					0, WLAN_STATUS_UNSPECIFIED_FAILURE,
					GFP_KERNEL);

	clear_bit(IWM_STATUS_SME_CONNECTING, &iwm->status);
	clear_bit(IWM_STATUS_ASSOCIATED, &iwm->status);

	iwm->umac_profile_active = 0;
	memset(iwm->bssid, 0, ETH_ALEN);
	iwm->channel = 0;

	iwm_link_off(iwm);

	wake_up_interruptible(&iwm->mlme_queue);

	return 0;
}

#define IWM_DISCONNECT_INTERVAL	(5 * HZ)

static int iwm_mlme_connection_terminated(struct iwm_priv *iwm, u8 *buf,
					  unsigned long buf_size,
					  struct iwm_wifi_cmd *cmd)
{
	IWM_DBG_MLME(iwm, DBG, "Connection terminated\n");

	schedule_delayed_work(&iwm->disconnect, IWM_DISCONNECT_INTERVAL);

	return 0;
}

static int iwm_mlme_scan_complete(struct iwm_priv *iwm, u8 *buf,
				  unsigned long buf_size,
				  struct iwm_wifi_cmd *cmd)
{
	int ret;
	struct iwm_umac_notif_scan_complete *scan_complete =
		(struct iwm_umac_notif_scan_complete *)buf;
	u32 result = le32_to_cpu(scan_complete->result);

	IWM_DBG_MLME(iwm, INFO, "type:0x%x result:0x%x seq:%d\n",
		     le32_to_cpu(scan_complete->type),
		     le32_to_cpu(scan_complete->result),
		     scan_complete->seq_num);

	if (!test_and_clear_bit(IWM_STATUS_SCANNING, &iwm->status)) {
		IWM_ERR(iwm, "Scan complete while device not scanning\n");
		return -EIO;
	}
	if (!iwm->scan_request)
		return 0;

	ret = iwm_cfg80211_inform_bss(iwm);

	cfg80211_scan_done(iwm->scan_request,
			   (result & UMAC_SCAN_RESULT_ABORTED) ? 1 : !!ret);
	iwm->scan_request = NULL;

	return ret;
}

static int iwm_mlme_update_sta_table(struct iwm_priv *iwm, u8 *buf,
				     unsigned long buf_size,
				     struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_notif_sta_info *umac_sta =
			(struct iwm_umac_notif_sta_info *)buf;
	struct iwm_sta_info *sta;
	int i;

	switch (le32_to_cpu(umac_sta->opcode)) {
	case UMAC_OPCODE_ADD_MODIFY:
		sta = &iwm->sta_table[GET_VAL8(umac_sta->sta_id, LMAC_STA_ID)];

		IWM_DBG_MLME(iwm, INFO, "%s STA: ID = %d, Color = %d, "
			     "addr = %pM, qos = %d\n",
			     sta->valid ? "Modify" : "Add",
			     GET_VAL8(umac_sta->sta_id, LMAC_STA_ID),
			     GET_VAL8(umac_sta->sta_id, LMAC_STA_COLOR),
			     umac_sta->mac_addr,
			     umac_sta->flags & UMAC_STA_FLAG_QOS);

		sta->valid = 1;
		sta->qos = umac_sta->flags & UMAC_STA_FLAG_QOS;
		sta->color = GET_VAL8(umac_sta->sta_id, LMAC_STA_COLOR);
		memcpy(sta->addr, umac_sta->mac_addr, ETH_ALEN);
		break;
	case UMAC_OPCODE_REMOVE:
		IWM_DBG_MLME(iwm, INFO, "Remove STA: ID = %d, Color = %d, "
			     "addr = %pM\n",
			     GET_VAL8(umac_sta->sta_id, LMAC_STA_ID),
			     GET_VAL8(umac_sta->sta_id, LMAC_STA_COLOR),
			     umac_sta->mac_addr);

		sta = &iwm->sta_table[GET_VAL8(umac_sta->sta_id, LMAC_STA_ID)];

		if (!memcmp(sta->addr, umac_sta->mac_addr, ETH_ALEN))
			sta->valid = 0;

		break;
	case UMAC_OPCODE_CLEAR_ALL:
		for (i = 0; i < IWM_STA_TABLE_NUM; i++)
			iwm->sta_table[i].valid = 0;

		break;
	default:
		break;
	}

	return 0;
}

static int iwm_mlme_medium_lost(struct iwm_priv *iwm, u8 *buf,
				unsigned long buf_size,
				struct iwm_wifi_cmd *cmd)
{
	struct wiphy *wiphy = iwm_to_wiphy(iwm);

	IWM_DBG_NTF(iwm, DBG, "WiFi/WiMax coexistence radio is OFF\n");

	wiphy_rfkill_set_hw_state(wiphy, true);

	return 0;
}

static int iwm_mlme_update_bss_table(struct iwm_priv *iwm, u8 *buf,
				     unsigned long buf_size,
				     struct iwm_wifi_cmd *cmd)
{
	struct wiphy *wiphy = iwm_to_wiphy(iwm);
	struct ieee80211_mgmt *mgmt;
	struct iwm_umac_notif_bss_info *umac_bss =
			(struct iwm_umac_notif_bss_info *)buf;
	struct ieee80211_channel *channel;
	struct ieee80211_supported_band *band;
	struct iwm_bss_info *bss, *next;
	s32 signal;
	int freq;
	u16 frame_len = le16_to_cpu(umac_bss->frame_len);
	size_t bss_len = sizeof(struct iwm_umac_notif_bss_info) + frame_len;

	mgmt = (struct ieee80211_mgmt *)(umac_bss->frame_buf);

	IWM_DBG_MLME(iwm, DBG, "New BSS info entry: %pM\n", mgmt->bssid);
	IWM_DBG_MLME(iwm, DBG, "\tType: 0x%x\n", le32_to_cpu(umac_bss->type));
	IWM_DBG_MLME(iwm, DBG, "\tTimestamp: %d\n",
		     le32_to_cpu(umac_bss->timestamp));
	IWM_DBG_MLME(iwm, DBG, "\tTable Index: %d\n",
		     le16_to_cpu(umac_bss->table_idx));
	IWM_DBG_MLME(iwm, DBG, "\tBand: %d\n", umac_bss->band);
	IWM_DBG_MLME(iwm, DBG, "\tChannel: %d\n", umac_bss->channel);
	IWM_DBG_MLME(iwm, DBG, "\tRSSI: %d\n", umac_bss->rssi);
	IWM_DBG_MLME(iwm, DBG, "\tFrame Length: %d\n", frame_len);

	list_for_each_entry_safe(bss, next, &iwm->bss_list, node)
		if (bss->bss->table_idx == umac_bss->table_idx)
			break;

	if (&bss->node != &iwm->bss_list) {
		/* Remove the old BSS entry, we will add it back later. */
		list_del(&bss->node);
		kfree(bss->bss);
	} else {
		/* New BSS entry */

		bss = kzalloc(sizeof(struct iwm_bss_info), GFP_KERNEL);
		if (!bss) {
			IWM_ERR(iwm, "Couldn't allocate bss_info\n");
			return -ENOMEM;
		}
	}

	bss->bss = kzalloc(bss_len, GFP_KERNEL);
	if (!bss->bss) {
		kfree(bss);
		IWM_ERR(iwm, "Couldn't allocate bss\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&bss->node);
	memcpy(bss->bss, umac_bss, bss_len);

	if (umac_bss->band == UMAC_BAND_2GHZ)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else if (umac_bss->band == UMAC_BAND_5GHZ)
		band = wiphy->bands[IEEE80211_BAND_5GHZ];
	else {
		IWM_ERR(iwm, "Invalid band: %d\n", umac_bss->band);
		goto err;
	}

	freq = ieee80211_channel_to_frequency(umac_bss->channel);
	channel = ieee80211_get_channel(wiphy, freq);
	signal = umac_bss->rssi * 100;

	bss->cfg_bss = cfg80211_inform_bss_frame(wiphy, channel,
						 mgmt, frame_len,
						 signal, GFP_KERNEL);
	if (!bss->cfg_bss)
		goto err;

	list_add_tail(&bss->node, &iwm->bss_list);

	return 0;
 err:
	kfree(bss->bss);
	kfree(bss);

	return -EINVAL;
}

static int iwm_mlme_remove_bss(struct iwm_priv *iwm, u8 *buf,
			       unsigned long buf_size, struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_notif_bss_removed *bss_rm =
		(struct iwm_umac_notif_bss_removed *)buf;
	struct iwm_bss_info *bss, *next;
	u16 table_idx;
	int i;

	for (i = 0; i < le32_to_cpu(bss_rm->count); i++) {
		table_idx = (le16_to_cpu(bss_rm->entries[i])
			     & IWM_BSS_REMOVE_INDEX_MSK);
		list_for_each_entry_safe(bss, next, &iwm->bss_list, node)
			if (bss->bss->table_idx == cpu_to_le16(table_idx)) {
				struct ieee80211_mgmt *mgmt;

				mgmt = (struct ieee80211_mgmt *)
					(bss->bss->frame_buf);
				IWM_DBG_MLME(iwm, ERR,
					     "BSS removed: %pM\n",
					     mgmt->bssid);
				list_del(&bss->node);
				kfree(bss->bss);
				kfree(bss);
			}
	}

	return 0;
}

static int iwm_mlme_mgt_frame(struct iwm_priv *iwm, u8 *buf,
			      unsigned long buf_size, struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_notif_mgt_frame *mgt_frame =
			(struct iwm_umac_notif_mgt_frame *)buf;
	struct ieee80211_mgmt *mgt = (struct ieee80211_mgmt *)mgt_frame->frame;

	IWM_HEXDUMP(iwm, DBG, MLME, "MGT: ", mgt_frame->frame,
		    le16_to_cpu(mgt_frame->len));

	if (ieee80211_is_assoc_req(mgt->frame_control)) {
		iwm->req_ie_len = le16_to_cpu(mgt_frame->len)
				  - offsetof(struct ieee80211_mgmt,
					     u.assoc_req.variable);
		kfree(iwm->req_ie);
		iwm->req_ie = kmemdup(mgt->u.assoc_req.variable,
				      iwm->req_ie_len, GFP_KERNEL);
	} else if (ieee80211_is_reassoc_req(mgt->frame_control)) {
		iwm->req_ie_len = le16_to_cpu(mgt_frame->len)
				  - offsetof(struct ieee80211_mgmt,
					     u.reassoc_req.variable);
		kfree(iwm->req_ie);
		iwm->req_ie = kmemdup(mgt->u.reassoc_req.variable,
				      iwm->req_ie_len, GFP_KERNEL);
	} else if (ieee80211_is_assoc_resp(mgt->frame_control)) {
		iwm->resp_ie_len = le16_to_cpu(mgt_frame->len)
				   - offsetof(struct ieee80211_mgmt,
					      u.assoc_resp.variable);
		kfree(iwm->resp_ie);
		iwm->resp_ie = kmemdup(mgt->u.assoc_resp.variable,
				       iwm->resp_ie_len, GFP_KERNEL);
	} else if (ieee80211_is_reassoc_resp(mgt->frame_control)) {
		iwm->resp_ie_len = le16_to_cpu(mgt_frame->len)
				   - offsetof(struct ieee80211_mgmt,
					      u.reassoc_resp.variable);
		kfree(iwm->resp_ie);
		iwm->resp_ie = kmemdup(mgt->u.reassoc_resp.variable,
				       iwm->resp_ie_len, GFP_KERNEL);
	} else {
		IWM_ERR(iwm, "Unsupported management frame: 0x%x",
			le16_to_cpu(mgt->frame_control));
		return 0;
	}

	return 0;
}

static int iwm_ntf_mlme(struct iwm_priv *iwm, u8 *buf,
			unsigned long buf_size, struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_notif_wifi_if *notif =
		(struct iwm_umac_notif_wifi_if *)buf;

	switch (notif->status) {
	case WIFI_IF_NTFY_ASSOC_START:
		return iwm_mlme_assoc_start(iwm, buf, buf_size, cmd);
	case WIFI_IF_NTFY_ASSOC_COMPLETE:
		return iwm_mlme_assoc_complete(iwm, buf, buf_size, cmd);
	case WIFI_IF_NTFY_PROFILE_INVALIDATE_COMPLETE:
		return iwm_mlme_profile_invalidate(iwm, buf, buf_size, cmd);
	case WIFI_IF_NTFY_CONNECTION_TERMINATED:
		return iwm_mlme_connection_terminated(iwm, buf, buf_size, cmd);
	case WIFI_IF_NTFY_SCAN_COMPLETE:
		return iwm_mlme_scan_complete(iwm, buf, buf_size, cmd);
	case WIFI_IF_NTFY_STA_TABLE_CHANGE:
		return iwm_mlme_update_sta_table(iwm, buf, buf_size, cmd);
	case WIFI_IF_NTFY_EXTENDED_IE_REQUIRED:
		IWM_DBG_MLME(iwm, DBG, "Extended IE required\n");
		break;
	case WIFI_IF_NTFY_RADIO_PREEMPTION:
		return iwm_mlme_medium_lost(iwm, buf, buf_size, cmd);
	case WIFI_IF_NTFY_BSS_TRK_TABLE_CHANGED:
		return iwm_mlme_update_bss_table(iwm, buf, buf_size, cmd);
	case WIFI_IF_NTFY_BSS_TRK_ENTRIES_REMOVED:
		return iwm_mlme_remove_bss(iwm, buf, buf_size, cmd);
		break;
	case WIFI_IF_NTFY_MGMT_FRAME:
		return iwm_mlme_mgt_frame(iwm, buf, buf_size, cmd);
	case WIFI_DBG_IF_NTFY_SCAN_SUPER_JOB_START:
	case WIFI_DBG_IF_NTFY_SCAN_SUPER_JOB_COMPLETE:
	case WIFI_DBG_IF_NTFY_SCAN_CHANNEL_START:
	case WIFI_DBG_IF_NTFY_SCAN_CHANNEL_RESULT:
	case WIFI_DBG_IF_NTFY_SCAN_MINI_JOB_START:
	case WIFI_DBG_IF_NTFY_SCAN_MINI_JOB_COMPLETE:
	case WIFI_DBG_IF_NTFY_CNCT_ATC_START:
	case WIFI_DBG_IF_NTFY_COEX_NOTIFICATION:
	case WIFI_DBG_IF_NTFY_COEX_HANDLE_ENVELOP:
	case WIFI_DBG_IF_NTFY_COEX_HANDLE_RELEASE_ENVELOP:
		IWM_DBG_MLME(iwm, DBG, "MLME debug notification: 0x%x\n",
			     notif->status);
		break;
	default:
		IWM_ERR(iwm, "Unhandled notification: 0x%x\n", notif->status);
		break;
	}

	return 0;
}

#define IWM_STATS_UPDATE_INTERVAL		(2 * HZ)

static int iwm_ntf_statistics(struct iwm_priv *iwm, u8 *buf,
			      unsigned long buf_size, struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_notif_stats *stats = (struct iwm_umac_notif_stats *)buf;
	struct iw_statistics *wstats = &iwm->wstats;
	u16 max_rate = 0;
	int i;

	IWM_DBG_MLME(iwm, DBG, "Statistics notification received\n");

	if (test_bit(IWM_STATUS_ASSOCIATED, &iwm->status)) {
		for (i = 0; i < UMAC_NTF_RATE_SAMPLE_NR; i++) {
			max_rate = max_t(u16, max_rate,
					 max(le16_to_cpu(stats->tx_rate[i]),
					     le16_to_cpu(stats->rx_rate[i])));
		}
		/* UMAC passes rate info multiplies by 2 */
		iwm->rate = max_rate >> 1;
	}
	iwm->txpower = le32_to_cpu(stats->tx_power);

	wstats->status = 0;

	wstats->discard.nwid = le32_to_cpu(stats->rx_drop_other_bssid);
	wstats->discard.code = le32_to_cpu(stats->rx_drop_decode);
	wstats->discard.fragment = le32_to_cpu(stats->rx_drop_reassembly);
	wstats->discard.retries = le32_to_cpu(stats->tx_drop_max_retry);

	wstats->miss.beacon = le32_to_cpu(stats->missed_beacons);

	/* according to cfg80211 */
	if (stats->rssi_dbm < -110)
		wstats->qual.qual = 0;
	else if (stats->rssi_dbm > -40)
		wstats->qual.qual = 70;
	else
		wstats->qual.qual = stats->rssi_dbm + 110;

	wstats->qual.level = stats->rssi_dbm;
	wstats->qual.noise = stats->noise_dbm;
	wstats->qual.updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;

	schedule_delayed_work(&iwm->stats_request, IWM_STATS_UPDATE_INTERVAL);

	mod_timer(&iwm->watchdog, round_jiffies(jiffies + IWM_WATCHDOG_PERIOD));

	return 0;
}

static int iwm_ntf_eeprom_proxy(struct iwm_priv *iwm, u8 *buf,
				unsigned long buf_size,
				struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_cmd_eeprom_proxy *eeprom_proxy =
		(struct iwm_umac_cmd_eeprom_proxy *)
		(buf + sizeof(struct iwm_umac_wifi_in_hdr));
	struct iwm_umac_cmd_eeprom_proxy_hdr *hdr = &eeprom_proxy->hdr;
	u32 hdr_offset = le32_to_cpu(hdr->offset);
	u32 hdr_len = le32_to_cpu(hdr->len);
	u32 hdr_type = le32_to_cpu(hdr->type);

	IWM_DBG_NTF(iwm, DBG, "type: 0x%x, len: %d, offset: 0x%x\n",
		    hdr_type, hdr_len, hdr_offset);

	if ((hdr_offset + hdr_len) > IWM_EEPROM_LEN)
		return -EINVAL;

	switch (hdr_type) {
	case IWM_UMAC_CMD_EEPROM_TYPE_READ:
		memcpy(iwm->eeprom + hdr_offset, eeprom_proxy->buf, hdr_len);
		break;
	case IWM_UMAC_CMD_EEPROM_TYPE_WRITE:
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int iwm_ntf_channel_info_list(struct iwm_priv *iwm, u8 *buf,
				     unsigned long buf_size,
				     struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_cmd_get_channel_list *ch_list =
			(struct iwm_umac_cmd_get_channel_list *)
			(buf + sizeof(struct iwm_umac_wifi_in_hdr));
	struct wiphy *wiphy = iwm_to_wiphy(iwm);
	struct ieee80211_supported_band *band;
	int i;

	band = wiphy->bands[IEEE80211_BAND_2GHZ];

	for (i = 0; i < band->n_channels; i++) {
		unsigned long ch_mask_0 =
			le32_to_cpu(ch_list->ch[0].channels_mask);
		unsigned long ch_mask_2 =
			le32_to_cpu(ch_list->ch[2].channels_mask);

		if (!test_bit(i, &ch_mask_0))
			band->channels[i].flags |= IEEE80211_CHAN_DISABLED;

		if (!test_bit(i, &ch_mask_2))
			band->channels[i].flags |= IEEE80211_CHAN_NO_IBSS;
	}

	band = wiphy->bands[IEEE80211_BAND_5GHZ];

	for (i = 0; i < min(band->n_channels, 32); i++) {
		unsigned long ch_mask_1 =
			le32_to_cpu(ch_list->ch[1].channels_mask);
		unsigned long ch_mask_3 =
			le32_to_cpu(ch_list->ch[3].channels_mask);

		if (!test_bit(i, &ch_mask_1))
			band->channels[i].flags |= IEEE80211_CHAN_DISABLED;

		if (!test_bit(i, &ch_mask_3))
			band->channels[i].flags |= IEEE80211_CHAN_NO_IBSS;
	}

	return 0;
}

static int iwm_ntf_stop_resume_tx(struct iwm_priv *iwm, u8 *buf,
				  unsigned long buf_size,
				  struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_notif_stop_resume_tx *stp_res_tx =
		(struct iwm_umac_notif_stop_resume_tx *)buf;
	struct iwm_sta_info *sta_info;
	struct iwm_tid_info *tid_info;
	u8 sta_id = STA_ID_N_COLOR_ID(stp_res_tx->sta_id);
	u16 tid_msk = le16_to_cpu(stp_res_tx->stop_resume_tid_msk);
	int bit, ret = 0;
	bool stop = false;

	IWM_DBG_NTF(iwm, DBG, "stop/resume notification:\n"
		    "\tflags:       0x%x\n"
		    "\tSTA id:      %d\n"
		    "\tTID bitmask: 0x%x\n",
		    stp_res_tx->flags, stp_res_tx->sta_id,
		    stp_res_tx->stop_resume_tid_msk);

	if (stp_res_tx->flags & UMAC_STOP_TX_FLAG)
		stop = true;

	sta_info = &iwm->sta_table[sta_id];
	if (!sta_info->valid) {
		IWM_ERR(iwm, "Stoping an invalid STA: %d %d\n",
			sta_id, stp_res_tx->sta_id);
		return -EINVAL;
	}

	for_each_bit(bit, (unsigned long *)&tid_msk, IWM_UMAC_TID_NR) {
		tid_info = &sta_info->tid_info[bit];

		mutex_lock(&tid_info->mutex);
		tid_info->stopped = stop;
		mutex_unlock(&tid_info->mutex);

		if (!stop) {
			struct iwm_tx_queue *txq;
			int queue = iwm_tid_to_queue(bit);

			if (queue < 0)
				continue;

			txq = &iwm->txq[queue];
			/*
			 * If we resume, we have to move our SKBs
			 * back to the tx queue and queue some work.
			 */
			spin_lock_bh(&txq->lock);
			skb_queue_splice_init(&txq->queue, &txq->stopped_queue);
			spin_unlock_bh(&txq->lock);

			queue_work(txq->wq, &txq->worker);
		}

	}

	/* We send an ACK only for the stop case */
	if (stop)
		ret = iwm_send_umac_stop_resume_tx(iwm, stp_res_tx);

	return ret;
}

static int iwm_ntf_wifi_if_wrapper(struct iwm_priv *iwm, u8 *buf,
				   unsigned long buf_size,
				   struct iwm_wifi_cmd *cmd)
{
	struct iwm_umac_wifi_if *hdr;

	if (cmd == NULL) {
		IWM_ERR(iwm, "Couldn't find expected wifi command\n");
		return -EINVAL;
	}

	hdr = (struct iwm_umac_wifi_if *)cmd->buf.payload;

	IWM_DBG_NTF(iwm, DBG, "WIFI_IF_WRAPPER cmd is delivered to UMAC: "
		    "oid is 0x%x\n", hdr->oid);

	if (hdr->oid <= WIFI_IF_NTFY_MAX) {
		set_bit(hdr->oid, &iwm->wifi_ntfy[0]);
		wake_up_interruptible(&iwm->wifi_ntfy_queue);
	} else
		return -EINVAL;

	switch (hdr->oid) {
	case UMAC_WIFI_IF_CMD_SET_PROFILE:
		iwm->umac_profile_active = 1;
		break;
	default:
		break;
	}

	return 0;
}

#define CT_KILL_DELAY (30 * HZ)
static int iwm_ntf_card_state(struct iwm_priv *iwm, u8 *buf,
			      unsigned long buf_size, struct iwm_wifi_cmd *cmd)
{
	struct wiphy *wiphy = iwm_to_wiphy(iwm);
	struct iwm_lmac_card_state *state = (struct iwm_lmac_card_state *)
				(buf + sizeof(struct iwm_umac_wifi_in_hdr));
	u32 flags = le32_to_cpu(state->flags);

	IWM_INFO(iwm, "HW RF Kill %s, CT Kill %s\n",
		 flags & IWM_CARD_STATE_HW_DISABLED ? "ON" : "OFF",
		 flags & IWM_CARD_STATE_CTKILL_DISABLED ? "ON" : "OFF");

	if (flags & IWM_CARD_STATE_CTKILL_DISABLED) {
		/*
		 * We got a CTKILL event: We bring the interface down in
		 * oder to cool the device down, and try to bring it up
		 * 30 seconds later. If it's still too hot, we'll go through
		 * this code path again.
		 */
		cancel_delayed_work_sync(&iwm->ct_kill_delay);
		schedule_delayed_work(&iwm->ct_kill_delay, CT_KILL_DELAY);
	}

	wiphy_rfkill_set_hw_state(wiphy, flags &
				  (IWM_CARD_STATE_HW_DISABLED |
				   IWM_CARD_STATE_CTKILL_DISABLED));

	return 0;
}

static int iwm_rx_handle_wifi(struct iwm_priv *iwm, u8 *buf,
			      unsigned long buf_size)
{
	struct iwm_umac_wifi_in_hdr *wifi_hdr;
	struct iwm_wifi_cmd *cmd;
	u8 source, cmd_id;
	u16 seq_num;
	u32 count;
	u8 resp;

	wifi_hdr = (struct iwm_umac_wifi_in_hdr *)buf;
	cmd_id = wifi_hdr->sw_hdr.cmd.cmd;

	source = GET_VAL32(wifi_hdr->hw_hdr.cmd, UMAC_HDI_IN_CMD_SOURCE);
	if (source >= IWM_SRC_NUM) {
		IWM_CRIT(iwm, "invalid source %d\n", source);
		return -EINVAL;
	}

	count = (GET_VAL32(wifi_hdr->sw_hdr.meta_data, UMAC_FW_CMD_BYTE_COUNT));
	count += sizeof(struct iwm_umac_wifi_in_hdr) -
		 sizeof(struct iwm_dev_cmd_hdr);
	if (count > buf_size) {
		IWM_CRIT(iwm, "count %d, buf size:%ld\n", count, buf_size);
		return -EINVAL;
	}

	resp = GET_VAL32(wifi_hdr->sw_hdr.meta_data, UMAC_FW_CMD_STATUS);

	seq_num = le16_to_cpu(wifi_hdr->sw_hdr.cmd.seq_num);

	IWM_DBG_RX(iwm, DBG, "CMD:0x%x, source: 0x%x, seqnum: %d\n",
		   cmd_id, source, seq_num);

	/*
	 * If this is a response to a previously sent command, there must
	 * be a pending command for this sequence number.
	 */
	cmd = iwm_get_pending_wifi_cmd(iwm, seq_num);

	/* Notify the caller only for sync commands. */
	switch (source) {
	case UMAC_HDI_IN_SOURCE_FHRX:
		if (iwm->lmac_handlers[cmd_id] &&
		    test_bit(cmd_id, &iwm->lmac_handler_map[0]))
			return iwm_notif_send(iwm, cmd, cmd_id, source,
					      buf, count);
		break;
	case UMAC_HDI_IN_SOURCE_FW:
		if (iwm->umac_handlers[cmd_id] &&
		    test_bit(cmd_id, &iwm->umac_handler_map[0]))
			return iwm_notif_send(iwm, cmd, cmd_id, source,
					      buf, count);
		break;
	case UMAC_HDI_IN_SOURCE_UDMA:
		break;
	}

	return iwm_rx_handle_resp(iwm, buf, count, cmd);
}

int iwm_rx_handle_resp(struct iwm_priv *iwm, u8 *buf, unsigned long buf_size,
		       struct iwm_wifi_cmd *cmd)
{
	u8 source, cmd_id;
	struct iwm_umac_wifi_in_hdr *wifi_hdr;
	int ret = 0;

	wifi_hdr = (struct iwm_umac_wifi_in_hdr *)buf;
	cmd_id = wifi_hdr->sw_hdr.cmd.cmd;

	source = GET_VAL32(wifi_hdr->hw_hdr.cmd, UMAC_HDI_IN_CMD_SOURCE);

	IWM_DBG_RX(iwm, DBG, "CMD:0x%x, source: 0x%x\n", cmd_id, source);

	switch (source) {
	case UMAC_HDI_IN_SOURCE_FHRX:
		if (iwm->lmac_handlers[cmd_id])
			ret = iwm->lmac_handlers[cmd_id]
					(iwm, buf, buf_size, cmd);
		break;
	case UMAC_HDI_IN_SOURCE_FW:
		if (iwm->umac_handlers[cmd_id])
			ret = iwm->umac_handlers[cmd_id]
					(iwm, buf, buf_size, cmd);
		break;
	case UMAC_HDI_IN_SOURCE_UDMA:
		ret = -EINVAL;
		break;
	}

	kfree(cmd);

	return ret;
}

static int iwm_rx_handle_nonwifi(struct iwm_priv *iwm, u8 *buf,
				 unsigned long buf_size)
{
	u8 seq_num;
	struct iwm_udma_in_hdr *hdr = (struct iwm_udma_in_hdr *)buf;
	struct iwm_nonwifi_cmd *cmd, *next;

	seq_num = GET_VAL32(hdr->cmd, UDMA_HDI_IN_CMD_NON_WIFI_HW_SEQ_NUM);

	/*
	 * We received a non wifi answer.
	 * Let's check if there's a pending command for it, and if so
	 * replace the command payload with the buffer, and then wake the
	 * callers up.
	 * That means we only support synchronised non wifi command response
	 * schemes.
	 */
	list_for_each_entry_safe(cmd, next, &iwm->nonwifi_pending_cmd, pending)
		if (cmd->seq_num == seq_num) {
			cmd->resp_received = 1;
			cmd->buf.len = buf_size;
			memcpy(cmd->buf.hdr, buf, buf_size);
			wake_up_interruptible(&iwm->nonwifi_queue);
		}

	return 0;
}

static int iwm_rx_handle_umac(struct iwm_priv *iwm, u8 *buf,
			      unsigned long buf_size)
{
	int ret = 0;
	u8 op_code;
	unsigned long buf_offset = 0;
	struct iwm_udma_in_hdr *hdr;

	/*
	 * To allow for a more efficient bus usage, UMAC
	 * messages are encapsulated into UDMA ones. This
	 * way we can have several UMAC messages in one bus
	 * transfer.
	 * A UDMA frame size is always aligned on 16 bytes,
	 * and a UDMA frame must not start with a UMAC_PAD_TERMINAL
	 * word. This is how we parse a bus frame into several
	 * UDMA ones.
	 */
	while (buf_offset < buf_size) {

		hdr = (struct iwm_udma_in_hdr *)(buf + buf_offset);

		if (iwm_rx_check_udma_hdr(hdr) < 0) {
			IWM_DBG_RX(iwm, DBG, "End of frame\n");
			break;
		}

		op_code = GET_VAL32(hdr->cmd, UMAC_HDI_IN_CMD_OPCODE);

		IWM_DBG_RX(iwm, DBG, "Op code: 0x%x\n", op_code);

		if (op_code == UMAC_HDI_IN_OPCODE_WIFI) {
			ret |= iwm_rx_handle_wifi(iwm, buf + buf_offset,
						  buf_size - buf_offset);
		} else if (op_code < UMAC_HDI_IN_OPCODE_NONWIFI_MAX) {
			if (GET_VAL32(hdr->cmd,
				      UDMA_HDI_IN_CMD_NON_WIFI_HW_SIG) !=
			    UDMA_HDI_IN_CMD_NON_WIFI_HW_SIG) {
				IWM_ERR(iwm, "Incorrect hw signature\n");
				return -EINVAL;
			}
			ret |= iwm_rx_handle_nonwifi(iwm, buf + buf_offset,
						     buf_size - buf_offset);
		} else {
			IWM_ERR(iwm, "Invalid RX opcode: 0x%x\n", op_code);
			ret |= -EINVAL;
		}

		buf_offset += iwm_rx_resp_size(hdr);
	}

	return ret;
}

int iwm_rx_handle(struct iwm_priv *iwm, u8 *buf, unsigned long buf_size)
{
	struct iwm_udma_in_hdr *hdr;

	hdr = (struct iwm_udma_in_hdr *)buf;

	switch (le32_to_cpu(hdr->cmd)) {
	case UMAC_REBOOT_BARKER:
		if (test_bit(IWM_STATUS_READY, &iwm->status)) {
			IWM_ERR(iwm, "Unexpected BARKER\n");

			schedule_work(&iwm->reset_worker);

			return 0;
		}

		return iwm_notif_send(iwm, NULL, IWM_BARKER_REBOOT_NOTIFICATION,
				      IWM_SRC_UDMA, buf, buf_size);
	case UMAC_ACK_BARKER:
		return iwm_notif_send(iwm, NULL, IWM_ACK_BARKER_NOTIFICATION,
				      IWM_SRC_UDMA, NULL, 0);
	default:
		IWM_DBG_RX(iwm, DBG, "Received cmd: 0x%x\n", hdr->cmd);
		return iwm_rx_handle_umac(iwm, buf, buf_size);
	}

	return 0;
}

static const iwm_handler iwm_umac_handlers[] =
{
	[UMAC_NOTIFY_OPCODE_ERROR]		= iwm_ntf_error,
	[UMAC_NOTIFY_OPCODE_ALIVE]		= iwm_ntf_umac_alive,
	[UMAC_NOTIFY_OPCODE_INIT_COMPLETE]	= iwm_ntf_init_complete,
	[UMAC_NOTIFY_OPCODE_WIFI_CORE_STATUS]	= iwm_ntf_wifi_status,
	[UMAC_NOTIFY_OPCODE_WIFI_IF_WRAPPER]	= iwm_ntf_mlme,
	[UMAC_NOTIFY_OPCODE_PAGE_DEALLOC]	= iwm_ntf_tx_credit_update,
	[UMAC_NOTIFY_OPCODE_RX_TICKET]		= iwm_ntf_rx_ticket,
	[UMAC_CMD_OPCODE_RESET]			= iwm_ntf_umac_reset,
	[UMAC_NOTIFY_OPCODE_STATS]		= iwm_ntf_statistics,
	[UMAC_CMD_OPCODE_EEPROM_PROXY]		= iwm_ntf_eeprom_proxy,
	[UMAC_CMD_OPCODE_GET_CHAN_INFO_LIST]	= iwm_ntf_channel_info_list,
	[UMAC_CMD_OPCODE_STOP_RESUME_STA_TX]	= iwm_ntf_stop_resume_tx,
	[REPLY_RX_MPDU_CMD]			= iwm_ntf_rx_packet,
	[UMAC_CMD_OPCODE_WIFI_IF_WRAPPER]	= iwm_ntf_wifi_if_wrapper,
};

static const iwm_handler iwm_lmac_handlers[] =
{
	[REPLY_TX]				= iwm_ntf_tx,
	[REPLY_ALIVE]				= iwm_ntf_lmac_version,
	[CALIBRATION_RES_NOTIFICATION]		= iwm_ntf_calib_res,
	[CALIBRATION_COMPLETE_NOTIFICATION]	= iwm_ntf_calib_complete,
	[CALIBRATION_CFG_CMD]			= iwm_ntf_calib_cfg,
	[REPLY_RX_MPDU_CMD]			= iwm_ntf_rx_packet,
	[CARD_STATE_NOTIFICATION]		= iwm_ntf_card_state,
};

void iwm_rx_setup_handlers(struct iwm_priv *iwm)
{
	iwm->umac_handlers = (iwm_handler *) iwm_umac_handlers;
	iwm->lmac_handlers = (iwm_handler *) iwm_lmac_handlers;
}

static void iwm_remove_iv(struct sk_buff *skb, u32 hdr_total_len)
{
	struct ieee80211_hdr *hdr;
	unsigned int hdr_len;

	hdr = (struct ieee80211_hdr *)skb->data;

	if (!ieee80211_has_protected(hdr->frame_control))
		return;

	hdr_len = ieee80211_hdrlen(hdr->frame_control);
	if (hdr_total_len <= hdr_len)
		return;

	memmove(skb->data + (hdr_total_len - hdr_len), skb->data, hdr_len);
	skb_pull(skb, (hdr_total_len - hdr_len));
}

static void iwm_rx_adjust_packet(struct iwm_priv *iwm,
				 struct iwm_rx_packet *packet,
				 struct iwm_rx_ticket_node *ticket_node)
{
	u32 payload_offset = 0, payload_len;
	struct iwm_rx_ticket *ticket = ticket_node->ticket;
	struct iwm_rx_mpdu_hdr *mpdu_hdr;
	struct ieee80211_hdr *hdr;

	mpdu_hdr = (struct iwm_rx_mpdu_hdr *)packet->skb->data;
	payload_offset += sizeof(struct iwm_rx_mpdu_hdr);
	/* Padding is 0 or 2 bytes */
	payload_len = le16_to_cpu(mpdu_hdr->len) +
		(le16_to_cpu(ticket->flags) & IWM_RX_TICKET_PAD_SIZE_MSK);
	payload_len -= ticket->tail_len;

	IWM_DBG_RX(iwm, DBG, "Packet adjusted, len:%d, offset:%d, "
		   "ticket offset:%d ticket tail len:%d\n",
		   payload_len, payload_offset, ticket->payload_offset,
		   ticket->tail_len);

	IWM_HEXDUMP(iwm, DBG, RX, "RAW: ", packet->skb->data, packet->skb->len);

	skb_pull(packet->skb, payload_offset);
	skb_trim(packet->skb, payload_len);

	iwm_remove_iv(packet->skb, ticket->payload_offset);

	hdr = (struct ieee80211_hdr *) packet->skb->data;
	if (ieee80211_is_data_qos(hdr->frame_control)) {
		/* UMAC handed QOS_DATA frame with 2 padding bytes appended
		 * to the qos_ctl field in IEEE 802.11 headers. */
		memmove(packet->skb->data + IEEE80211_QOS_CTL_LEN + 2,
			packet->skb->data,
			ieee80211_hdrlen(hdr->frame_control) -
			IEEE80211_QOS_CTL_LEN);
		hdr = (struct ieee80211_hdr *) skb_pull(packet->skb,
				IEEE80211_QOS_CTL_LEN + 2);
		hdr->frame_control &= ~cpu_to_le16(IEEE80211_STYPE_QOS_DATA);
	}

	IWM_HEXDUMP(iwm, DBG, RX, "ADJUSTED: ",
		    packet->skb->data, packet->skb->len);
}

static void classify8023(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;

	if (ieee80211_is_data_qos(hdr->frame_control)) {
		u8 *qc = ieee80211_get_qos_ctl(hdr);
		/* frame has qos control */
		skb->priority = *qc & IEEE80211_QOS_CTL_TID_MASK;
	} else {
		skb->priority = 0;
	}
}

static void iwm_rx_process_amsdu(struct iwm_priv *iwm, struct sk_buff *skb)
{
	struct wireless_dev *wdev = iwm_to_wdev(iwm);
	struct net_device *ndev = iwm_to_ndev(iwm);
	struct sk_buff_head list;
	struct sk_buff *frame;

	IWM_HEXDUMP(iwm, DBG, RX, "A-MSDU: ", skb->data, skb->len);

	__skb_queue_head_init(&list);
	ieee80211_amsdu_to_8023s(skb, &list, ndev->dev_addr, wdev->iftype, 0);

	while ((frame = __skb_dequeue(&list))) {
		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += frame->len;

		frame->protocol = eth_type_trans(frame, ndev);
		frame->ip_summed = CHECKSUM_NONE;
		memset(frame->cb, 0, sizeof(frame->cb));

		if (netif_rx_ni(frame) == NET_RX_DROP) {
			IWM_ERR(iwm, "Packet dropped\n");
			ndev->stats.rx_dropped++;
		}
	}
}

static void iwm_rx_process_packet(struct iwm_priv *iwm,
				  struct iwm_rx_packet *packet,
				  struct iwm_rx_ticket_node *ticket_node)
{
	int ret;
	struct sk_buff *skb = packet->skb;
	struct wireless_dev *wdev = iwm_to_wdev(iwm);
	struct net_device *ndev = iwm_to_ndev(iwm);

	IWM_DBG_RX(iwm, DBG, "Processing packet ID %d\n", packet->id);

	switch (le16_to_cpu(ticket_node->ticket->action)) {
	case IWM_RX_TICKET_RELEASE:
		IWM_DBG_RX(iwm, DBG, "RELEASE packet\n");

		iwm_rx_adjust_packet(iwm, packet, ticket_node);
		skb->dev = iwm_to_ndev(iwm);
		classify8023(skb);

		if (le16_to_cpu(ticket_node->ticket->flags) &
		    IWM_RX_TICKET_AMSDU_MSK) {
			iwm_rx_process_amsdu(iwm, skb);
			break;
		}

		ret = ieee80211_data_to_8023(skb, ndev->dev_addr, wdev->iftype);
		if (ret < 0) {
			IWM_DBG_RX(iwm, DBG, "Couldn't convert 802.11 header - "
				   "%d\n", ret);
			kfree_skb(packet->skb);
			break;
		}

		IWM_HEXDUMP(iwm, DBG, RX, "802.3: ", skb->data, skb->len);

		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += skb->len;

		skb->protocol = eth_type_trans(skb, ndev);
		skb->ip_summed = CHECKSUM_NONE;
		memset(skb->cb, 0, sizeof(skb->cb));

		if (netif_rx_ni(skb) == NET_RX_DROP) {
			IWM_ERR(iwm, "Packet dropped\n");
			ndev->stats.rx_dropped++;
		}
		break;
	case IWM_RX_TICKET_DROP:
		IWM_DBG_RX(iwm, DBG, "DROP packet: 0x%x\n",
			   le16_to_cpu(ticket_node->ticket->flags));
		kfree_skb(packet->skb);
		break;
	default:
		IWM_ERR(iwm, "Unknown ticket action: %d\n",
			le16_to_cpu(ticket_node->ticket->action));
		kfree_skb(packet->skb);
	}

	kfree(packet);
	iwm_rx_ticket_node_free(ticket_node);
}

/*
 * Rx data processing:
 *
 * We're receiving Rx packet from the LMAC, and Rx ticket from
 * the UMAC.
 * To forward a target data packet upstream (i.e. to the
 * kernel network stack), we must have received an Rx ticket
 * that tells us we're allowed to release this packet (ticket
 * action is IWM_RX_TICKET_RELEASE). The Rx ticket also indicates,
 * among other things, where valid data actually starts in the Rx
 * packet.
 */
void iwm_rx_worker(struct work_struct *work)
{
	struct iwm_priv *iwm;
	struct iwm_rx_ticket_node *ticket, *next;

	iwm = container_of(work, struct iwm_priv, rx_worker);

	/*
	 * We go through the tickets list and if there is a pending
	 * packet for it, we push it upstream.
	 * We stop whenever a ticket is missing its packet, as we're
	 * supposed to send the packets in order.
	 */
	list_for_each_entry_safe(ticket, next, &iwm->rx_tickets, node) {
		struct iwm_rx_packet *packet =
			iwm_rx_packet_get(iwm, le16_to_cpu(ticket->ticket->id));

		if (!packet) {
			IWM_DBG_RX(iwm, DBG, "Skip rx_work: Wait for ticket %d "
				   "to be handled first\n",
				   le16_to_cpu(ticket->ticket->id));
			return;
		}

		list_del(&ticket->node);
		list_del(&packet->node);
		iwm_rx_process_packet(iwm, packet, ticket);
	}
}

