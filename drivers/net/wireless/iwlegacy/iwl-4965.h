/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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
 *****************************************************************************/

#ifndef __iwl_4965_h__
#define __iwl_4965_h__

#include "iwl-dev.h"

/* configuration for the _4965 devices */
extern struct iwl_cfg iwl4965_cfg;

extern struct iwl_mod_params iwl4965_mod_params;

extern struct ieee80211_ops iwl4965_hw_ops;

/* tx queue */
void iwl4965_free_tfds_in_queue(struct iwl_priv *priv,
			    int sta_id, int tid, int freed);

/* RXON */
void iwl4965_set_rxon_chain(struct iwl_priv *priv,
				struct iwl_rxon_context *ctx);

/* uCode */
int iwl4965_verify_ucode(struct iwl_priv *priv);

/* lib */
void iwl4965_check_abort_status(struct iwl_priv *priv,
			    u8 frame_count, u32 status);

void iwl4965_rx_queue_reset(struct iwl_priv *priv, struct iwl_rx_queue *rxq);
int iwl4965_rx_init(struct iwl_priv *priv, struct iwl_rx_queue *rxq);
int iwl4965_hw_nic_init(struct iwl_priv *priv);
int iwl4965_dump_fh(struct iwl_priv *priv, char **buf, bool display);

/* rx */
void iwl4965_rx_queue_restock(struct iwl_priv *priv);
void iwl4965_rx_replenish(struct iwl_priv *priv);
void iwl4965_rx_replenish_now(struct iwl_priv *priv);
void iwl4965_rx_queue_free(struct iwl_priv *priv, struct iwl_rx_queue *rxq);
int iwl4965_rxq_stop(struct iwl_priv *priv);
int iwl4965_hwrate_to_mac80211_idx(u32 rate_n_flags, enum ieee80211_band band);
void iwl4965_rx_reply_rx(struct iwl_priv *priv,
		     struct iwl_rx_mem_buffer *rxb);
void iwl4965_rx_reply_rx_phy(struct iwl_priv *priv,
			 struct iwl_rx_mem_buffer *rxb);
void iwl4965_rx_handle(struct iwl_priv *priv);

/* tx */
void iwl4965_hw_txq_free_tfd(struct iwl_priv *priv, struct iwl_tx_queue *txq);
int iwl4965_hw_txq_attach_buf_to_tfd(struct iwl_priv *priv,
				 struct iwl_tx_queue *txq,
				 dma_addr_t addr, u16 len, u8 reset, u8 pad);
int iwl4965_hw_tx_queue_init(struct iwl_priv *priv,
			 struct iwl_tx_queue *txq);
void iwl4965_hwrate_to_tx_control(struct iwl_priv *priv, u32 rate_n_flags,
			      struct ieee80211_tx_info *info);
int iwl4965_tx_skb(struct iwl_priv *priv, struct sk_buff *skb);
int iwl4965_tx_agg_start(struct iwl_priv *priv, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, u16 tid, u16 *ssn);
int iwl4965_tx_agg_stop(struct iwl_priv *priv, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, u16 tid);
int iwl4965_txq_check_empty(struct iwl_priv *priv,
			   int sta_id, u8 tid, int txq_id);
void iwl4965_rx_reply_compressed_ba(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb);
int iwl4965_tx_queue_reclaim(struct iwl_priv *priv, int txq_id, int index);
void iwl4965_hw_txq_ctx_free(struct iwl_priv *priv);
int iwl4965_txq_ctx_alloc(struct iwl_priv *priv);
void iwl4965_txq_ctx_reset(struct iwl_priv *priv);
void iwl4965_txq_ctx_stop(struct iwl_priv *priv);
void iwl4965_txq_set_sched(struct iwl_priv *priv, u32 mask);

/*
 * Acquire priv->lock before calling this function !
 */
void iwl4965_set_wr_ptrs(struct iwl_priv *priv, int txq_id, u32 index);
/**
 * iwl4965_tx_queue_set_status - (optionally) start Tx/Cmd queue
 * @tx_fifo_id: Tx DMA/FIFO channel (range 0-7) that the queue will feed
 * @scd_retry: (1) Indicates queue will be used in aggregation mode
 *
 * NOTE:  Acquire priv->lock before calling this function !
 */
void iwl4965_tx_queue_set_status(struct iwl_priv *priv,
					struct iwl_tx_queue *txq,
					int tx_fifo_id, int scd_retry);

static inline u32 iwl4965_tx_status_to_mac80211(u32 status)
{
	status &= TX_STATUS_MSK;

	switch (status) {
	case TX_STATUS_SUCCESS:
	case TX_STATUS_DIRECT_DONE:
		return IEEE80211_TX_STAT_ACK;
	case TX_STATUS_FAIL_DEST_PS:
		return IEEE80211_TX_STAT_TX_FILTERED;
	default:
		return 0;
	}
}

static inline bool iwl4965_is_tx_success(u32 status)
{
	status &= TX_STATUS_MSK;
	return (status == TX_STATUS_SUCCESS) ||
	       (status == TX_STATUS_DIRECT_DONE);
}

u8 iwl4965_toggle_tx_ant(struct iwl_priv *priv, u8 ant_idx, u8 valid);

/* rx */
void iwl4965_rx_missed_beacon_notif(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb);
bool iwl4965_good_plcp_health(struct iwl_priv *priv,
			  struct iwl_rx_packet *pkt);
void iwl4965_rx_statistics(struct iwl_priv *priv,
		       struct iwl_rx_mem_buffer *rxb);
void iwl4965_reply_statistics(struct iwl_priv *priv,
			  struct iwl_rx_mem_buffer *rxb);

/* scan */
int iwl4965_request_scan(struct iwl_priv *priv, struct ieee80211_vif *vif);

/* station mgmt */
int iwl4965_manage_ibss_station(struct iwl_priv *priv,
			       struct ieee80211_vif *vif, bool add);

/* hcmd */
int iwl4965_send_beacon_cmd(struct iwl_priv *priv);

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
const char *iwl4965_get_tx_fail_reason(u32 status);
#else
static inline const char *
iwl4965_get_tx_fail_reason(u32 status) { return ""; }
#endif

/* station management */
int iwl4965_alloc_bcast_station(struct iwl_priv *priv,
			       struct iwl_rxon_context *ctx);
int iwl4965_add_bssid_station(struct iwl_priv *priv,
				struct iwl_rxon_context *ctx,
			     const u8 *addr, u8 *sta_id_r);
int iwl4965_remove_default_wep_key(struct iwl_priv *priv,
			       struct iwl_rxon_context *ctx,
			       struct ieee80211_key_conf *key);
int iwl4965_set_default_wep_key(struct iwl_priv *priv,
			    struct iwl_rxon_context *ctx,
			    struct ieee80211_key_conf *key);
int iwl4965_restore_default_wep_keys(struct iwl_priv *priv,
				 struct iwl_rxon_context *ctx);
int iwl4965_set_dynamic_key(struct iwl_priv *priv,
			struct iwl_rxon_context *ctx,
			struct ieee80211_key_conf *key, u8 sta_id);
int iwl4965_remove_dynamic_key(struct iwl_priv *priv,
			struct iwl_rxon_context *ctx,
			struct ieee80211_key_conf *key, u8 sta_id);
void iwl4965_update_tkip_key(struct iwl_priv *priv,
			 struct iwl_rxon_context *ctx,
			 struct ieee80211_key_conf *keyconf,
			 struct ieee80211_sta *sta, u32 iv32, u16 *phase1key);
int iwl4965_sta_tx_modify_enable_tid(struct iwl_priv *priv,
			int sta_id, int tid);
int iwl4965_sta_rx_agg_start(struct iwl_priv *priv, struct ieee80211_sta *sta,
			 int tid, u16 ssn);
int iwl4965_sta_rx_agg_stop(struct iwl_priv *priv, struct ieee80211_sta *sta,
			int tid);
void iwl4965_sta_modify_sleep_tx_count(struct iwl_priv *priv,
			int sta_id, int cnt);
int iwl4965_update_bcast_stations(struct iwl_priv *priv);

/* rate */
static inline u32 iwl4965_ant_idx_to_flags(u8 ant_idx)
{
	return BIT(ant_idx) << RATE_MCS_ANT_POS;
}

static inline u8 iwl4965_hw_get_rate(__le32 rate_n_flags)
{
	return le32_to_cpu(rate_n_flags) & 0xFF;
}

static inline __le32 iwl4965_hw_set_rate_n_flags(u8 rate, u32 flags)
{
	return cpu_to_le32(flags|(u32)rate);
}

/* eeprom */
void iwl4965_eeprom_get_mac(const struct iwl_priv *priv, u8 *mac);
int iwl4965_eeprom_acquire_semaphore(struct iwl_priv *priv);
void iwl4965_eeprom_release_semaphore(struct iwl_priv *priv);
int  iwl4965_eeprom_check_version(struct iwl_priv *priv);

/* mac80211 handlers (for 4965) */
void iwl4965_mac_tx(struct ieee80211_hw *hw, struct sk_buff *skb);
int iwl4965_mac_start(struct ieee80211_hw *hw);
void iwl4965_mac_stop(struct ieee80211_hw *hw);
void iwl4965_configure_filter(struct ieee80211_hw *hw,
			     unsigned int changed_flags,
			     unsigned int *total_flags,
			     u64 multicast);
int iwl4965_mac_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		       struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		       struct ieee80211_key_conf *key);
void iwl4965_mac_update_tkip_key(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_key_conf *keyconf,
				struct ieee80211_sta *sta,
				u32 iv32, u16 *phase1key);
int iwl4965_mac_ampdu_action(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    enum ieee80211_ampdu_mlme_action action,
			    struct ieee80211_sta *sta, u16 tid, u16 *ssn,
			    u8 buf_size);
int iwl4965_mac_sta_add(struct ieee80211_hw *hw,
		       struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);
void iwl4965_mac_channel_switch(struct ieee80211_hw *hw,
			       struct ieee80211_channel_switch *ch_switch);

#endif /* __iwl_4965_h__ */
