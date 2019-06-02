/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
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
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
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
 *
 *****************************************************************************/

#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/fs.h>
#include <net/cfg80211.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <net/addrconf.h>
#include "iwl-modparams.h"
#include "fw-api.h"
#include "mvm.h"

void iwl_mvm_set_rekey_data(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct cfg80211_gtk_rekey_data *data)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (iwlwifi_mod_params.swcrypto)
		return;

	mutex_lock(&mvm->mutex);

	memcpy(mvmvif->rekey_data.kek, data->kek, NL80211_KEK_LEN);
	memcpy(mvmvif->rekey_data.kck, data->kck, NL80211_KCK_LEN);
	mvmvif->rekey_data.replay_ctr =
		cpu_to_le64(be64_to_cpup((__be64 *)data->replay_ctr));
	mvmvif->rekey_data.valid = true;

	mutex_unlock(&mvm->mutex);
}

#if IS_ENABLED(CONFIG_IPV6)
void iwl_mvm_ipv6_addr_change(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct inet6_dev *idev)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct inet6_ifaddr *ifa;
	int idx = 0;

	memset(mvmvif->tentative_addrs, 0, sizeof(mvmvif->tentative_addrs));

	read_lock_bh(&idev->lock);
	list_for_each_entry(ifa, &idev->addr_list, if_list) {
		mvmvif->target_ipv6_addrs[idx] = ifa->addr;
		if (ifa->flags & IFA_F_TENTATIVE)
			__set_bit(idx, mvmvif->tentative_addrs);
		idx++;
		if (idx >= IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_MAX)
			break;
	}
	read_unlock_bh(&idev->lock);

	mvmvif->num_target_ipv6_addrs = idx;
}
#endif

void iwl_mvm_set_default_unicast_key(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif, int idx)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	mvmvif->tx_key_idx = idx;
}

static void iwl_mvm_convert_p1k(u16 *p1k, __le16 *out)
{
	int i;

	for (i = 0; i < IWL_P1K_SIZE; i++)
		out[i] = cpu_to_le16(p1k[i]);
}

static const u8 *iwl_mvm_find_max_pn(struct ieee80211_key_conf *key,
				     struct iwl_mvm_key_pn *ptk_pn,
				     struct ieee80211_key_seq *seq,
				     int tid, int queues)
{
	const u8 *ret = seq->ccmp.pn;
	int i;

	/* get the PN from mac80211, used on the default queue */
	ieee80211_get_key_rx_seq(key, tid, seq);

	/* and use the internal data for the other queues */
	for (i = 1; i < queues; i++) {
		const u8 *tmp = ptk_pn->q[i].pn[tid];

		if (memcmp(ret, tmp, IEEE80211_CCMP_PN_LEN) <= 0)
			ret = tmp;
	}

	return ret;
}

struct wowlan_key_data {
	struct iwl_wowlan_rsc_tsc_params_cmd *rsc_tsc;
	struct iwl_wowlan_tkip_params_cmd *tkip;
	bool error, use_rsc_tsc, use_tkip, configure_keys;
	int wep_key_idx;
};

static void iwl_mvm_wowlan_program_keys(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_sta *sta,
					struct ieee80211_key_conf *key,
					void *_data)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct wowlan_key_data *data = _data;
	struct aes_sc *aes_sc, *aes_tx_sc = NULL;
	struct tkip_sc *tkip_sc, *tkip_tx_sc = NULL;
	struct iwl_p1k_cache *rx_p1ks;
	u8 *rx_mic_key;
	struct ieee80211_key_seq seq;
	u32 cur_rx_iv32 = 0;
	u16 p1k[IWL_P1K_SIZE];
	int ret, i;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104: { /* hack it for now */
		struct {
			struct iwl_mvm_wep_key_cmd wep_key_cmd;
			struct iwl_mvm_wep_key wep_key;
		} __packed wkc = {
			.wep_key_cmd.mac_id_n_color =
				cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id,
								mvmvif->color)),
			.wep_key_cmd.num_keys = 1,
			/* firmware sets STA_KEY_FLG_WEP_13BYTES */
			.wep_key_cmd.decryption_type = STA_KEY_FLG_WEP,
			.wep_key.key_index = key->keyidx,
			.wep_key.key_size = key->keylen,
		};

		/*
		 * This will fail -- the key functions don't set support
		 * pairwise WEP keys. However, that's better than silently
		 * failing WoWLAN. Or maybe not?
		 */
		if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE)
			break;

		memcpy(&wkc.wep_key.key[3], key->key, key->keylen);
		if (key->keyidx == mvmvif->tx_key_idx) {
			/* TX key must be at offset 0 */
			wkc.wep_key.key_offset = 0;
		} else {
			/* others start at 1 */
			data->wep_key_idx++;
			wkc.wep_key.key_offset = data->wep_key_idx;
		}

		if (data->configure_keys) {
			mutex_lock(&mvm->mutex);
			ret = iwl_mvm_send_cmd_pdu(mvm, WEP_KEY, 0,
						   sizeof(wkc), &wkc);
			data->error = ret != 0;

			mvm->ptk_ivlen = key->iv_len;
			mvm->ptk_icvlen = key->icv_len;
			mvm->gtk_ivlen = key->iv_len;
			mvm->gtk_icvlen = key->icv_len;
			mutex_unlock(&mvm->mutex);
		}

		/* don't upload key again */
		return;
	}
	default:
		data->error = true;
		return;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		/*
		 * Ignore CMAC keys -- the WoWLAN firmware doesn't support them
		 * but we also shouldn't abort suspend due to that. It does have
		 * support for the IGTK key renewal, but doesn't really use the
		 * IGTK for anything. This means we could spuriously wake up or
		 * be deauthenticated, but that was considered acceptable.
		 */
		return;
	case WLAN_CIPHER_SUITE_TKIP:
		if (sta) {
			u64 pn64;

			tkip_sc = data->rsc_tsc->all_tsc_rsc.tkip.unicast_rsc;
			tkip_tx_sc = &data->rsc_tsc->all_tsc_rsc.tkip.tsc;

			rx_p1ks = data->tkip->rx_uni;

			pn64 = atomic64_read(&key->tx_pn);
			tkip_tx_sc->iv16 = cpu_to_le16(TKIP_PN_TO_IV16(pn64));
			tkip_tx_sc->iv32 = cpu_to_le32(TKIP_PN_TO_IV32(pn64));

			ieee80211_get_tkip_p1k_iv(key, TKIP_PN_TO_IV32(pn64),
						  p1k);
			iwl_mvm_convert_p1k(p1k, data->tkip->tx.p1k);

			memcpy(data->tkip->mic_keys.tx,
			       &key->key[NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY],
			       IWL_MIC_KEY_SIZE);

			rx_mic_key = data->tkip->mic_keys.rx_unicast;
		} else {
			tkip_sc =
				data->rsc_tsc->all_tsc_rsc.tkip.multicast_rsc;
			rx_p1ks = data->tkip->rx_multi;
			rx_mic_key = data->tkip->mic_keys.rx_mcast;
		}

		/*
		 * For non-QoS this relies on the fact that both the uCode and
		 * mac80211 use TID 0 (as they need to to avoid replay attacks)
		 * for checking the IV in the frames.
		 */
		for (i = 0; i < IWL_NUM_RSC; i++) {
			ieee80211_get_key_rx_seq(key, i, &seq);
			tkip_sc[i].iv16 = cpu_to_le16(seq.tkip.iv16);
			tkip_sc[i].iv32 = cpu_to_le32(seq.tkip.iv32);
			/* wrapping isn't allowed, AP must rekey */
			if (seq.tkip.iv32 > cur_rx_iv32)
				cur_rx_iv32 = seq.tkip.iv32;
		}

		ieee80211_get_tkip_rx_p1k(key, vif->bss_conf.bssid,
					  cur_rx_iv32, p1k);
		iwl_mvm_convert_p1k(p1k, rx_p1ks[0].p1k);
		ieee80211_get_tkip_rx_p1k(key, vif->bss_conf.bssid,
					  cur_rx_iv32 + 1, p1k);
		iwl_mvm_convert_p1k(p1k, rx_p1ks[1].p1k);

		memcpy(rx_mic_key,
		       &key->key[NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY],
		       IWL_MIC_KEY_SIZE);

		data->use_tkip = true;
		data->use_rsc_tsc = true;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		if (sta) {
			u64 pn64;

			aes_sc = data->rsc_tsc->all_tsc_rsc.aes.unicast_rsc;
			aes_tx_sc = &data->rsc_tsc->all_tsc_rsc.aes.tsc;

			pn64 = atomic64_read(&key->tx_pn);
			aes_tx_sc->pn = cpu_to_le64(pn64);
		} else {
			aes_sc = data->rsc_tsc->all_tsc_rsc.aes.multicast_rsc;
		}

		/*
		 * For non-QoS this relies on the fact that both the uCode and
		 * mac80211/our RX code use TID 0 for checking the PN.
		 */
		if (sta && iwl_mvm_has_new_rx_api(mvm)) {
			struct iwl_mvm_sta *mvmsta;
			struct iwl_mvm_key_pn *ptk_pn;
			const u8 *pn;

			mvmsta = iwl_mvm_sta_from_mac80211(sta);
			ptk_pn = rcu_dereference_protected(
						mvmsta->ptk_pn[key->keyidx],
						lockdep_is_held(&mvm->mutex));
			if (WARN_ON(!ptk_pn))
				break;

			for (i = 0; i < IWL_MAX_TID_COUNT; i++) {
				pn = iwl_mvm_find_max_pn(key, ptk_pn, &seq, i,
						mvm->trans->num_rx_queues);
				aes_sc[i].pn = cpu_to_le64((u64)pn[5] |
							   ((u64)pn[4] << 8) |
							   ((u64)pn[3] << 16) |
							   ((u64)pn[2] << 24) |
							   ((u64)pn[1] << 32) |
							   ((u64)pn[0] << 40));
			}
		} else {
			for (i = 0; i < IWL_NUM_RSC; i++) {
				u8 *pn = seq.ccmp.pn;

				ieee80211_get_key_rx_seq(key, i, &seq);
				aes_sc[i].pn = cpu_to_le64((u64)pn[5] |
							   ((u64)pn[4] << 8) |
							   ((u64)pn[3] << 16) |
							   ((u64)pn[2] << 24) |
							   ((u64)pn[1] << 32) |
							   ((u64)pn[0] << 40));
			}
		}
		data->use_rsc_tsc = true;
		break;
	}

	if (data->configure_keys) {
		mutex_lock(&mvm->mutex);
		/*
		 * The D3 firmware hardcodes the key offset 0 as the key it
		 * uses to transmit packets to the AP, i.e. the PTK.
		 */
		if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) {
			mvm->ptk_ivlen = key->iv_len;
			mvm->ptk_icvlen = key->icv_len;
			ret = iwl_mvm_set_sta_key(mvm, vif, sta, key, 0);
		} else {
			/*
			 * firmware only supports TSC/RSC for a single key,
			 * so if there are multiple keep overwriting them
			 * with new ones -- this relies on mac80211 doing
			 * list_add_tail().
			 */
			mvm->gtk_ivlen = key->iv_len;
			mvm->gtk_icvlen = key->icv_len;
			ret = iwl_mvm_set_sta_key(mvm, vif, sta, key, 1);
		}
		mutex_unlock(&mvm->mutex);
		data->error = ret != 0;
	}
}

static int iwl_mvm_send_patterns_v1(struct iwl_mvm *mvm,
				    struct cfg80211_wowlan *wowlan)
{
	struct iwl_wowlan_patterns_cmd_v1 *pattern_cmd;
	struct iwl_host_cmd cmd = {
		.id = WOWLAN_PATTERNS,
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
	};
	int i, err;

	if (!wowlan->n_patterns)
		return 0;

	cmd.len[0] = sizeof(*pattern_cmd) +
		wowlan->n_patterns * sizeof(struct iwl_wowlan_pattern_v1);

	pattern_cmd = kmalloc(cmd.len[0], GFP_KERNEL);
	if (!pattern_cmd)
		return -ENOMEM;

	pattern_cmd->n_patterns = cpu_to_le32(wowlan->n_patterns);

	for (i = 0; i < wowlan->n_patterns; i++) {
		int mask_len = DIV_ROUND_UP(wowlan->patterns[i].pattern_len, 8);

		memcpy(&pattern_cmd->patterns[i].mask,
		       wowlan->patterns[i].mask, mask_len);
		memcpy(&pattern_cmd->patterns[i].pattern,
		       wowlan->patterns[i].pattern,
		       wowlan->patterns[i].pattern_len);
		pattern_cmd->patterns[i].mask_size = mask_len;
		pattern_cmd->patterns[i].pattern_size =
			wowlan->patterns[i].pattern_len;
	}

	cmd.data[0] = pattern_cmd;
	err = iwl_mvm_send_cmd(mvm, &cmd);
	kfree(pattern_cmd);
	return err;
}

static int iwl_mvm_send_patterns(struct iwl_mvm *mvm,
				 struct cfg80211_wowlan *wowlan)
{
	struct iwl_wowlan_patterns_cmd *pattern_cmd;
	struct iwl_host_cmd cmd = {
		.id = WOWLAN_PATTERNS,
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
	};
	int i, err;

	if (!wowlan->n_patterns)
		return 0;

	cmd.len[0] = sizeof(*pattern_cmd) +
		wowlan->n_patterns * sizeof(struct iwl_wowlan_pattern_v2);

	pattern_cmd = kmalloc(cmd.len[0], GFP_KERNEL);
	if (!pattern_cmd)
		return -ENOMEM;

	pattern_cmd->n_patterns = cpu_to_le32(wowlan->n_patterns);

	for (i = 0; i < wowlan->n_patterns; i++) {
		int mask_len = DIV_ROUND_UP(wowlan->patterns[i].pattern_len, 8);

		pattern_cmd->patterns[i].pattern_type =
			WOWLAN_PATTERN_TYPE_BITMASK;

		memcpy(&pattern_cmd->patterns[i].u.bitmask.mask,
		       wowlan->patterns[i].mask, mask_len);
		memcpy(&pattern_cmd->patterns[i].u.bitmask.pattern,
		       wowlan->patterns[i].pattern,
		       wowlan->patterns[i].pattern_len);
		pattern_cmd->patterns[i].u.bitmask.mask_size = mask_len;
		pattern_cmd->patterns[i].u.bitmask.pattern_size =
			wowlan->patterns[i].pattern_len;
	}

	cmd.data[0] = pattern_cmd;
	err = iwl_mvm_send_cmd(mvm, &cmd);
	kfree(pattern_cmd);
	return err;
}

static int iwl_mvm_d3_reprogram(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				struct ieee80211_sta *ap_sta)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct ieee80211_chanctx_conf *ctx;
	u8 chains_static, chains_dynamic;
	struct cfg80211_chan_def chandef;
	int ret, i;
	struct iwl_binding_cmd_v1 binding_cmd = {};
	struct iwl_time_quota_cmd quota_cmd = {};
	struct iwl_time_quota_data *quota;
	u32 status;

	if (WARN_ON_ONCE(iwl_mvm_is_cdb_supported(mvm)))
		return -EINVAL;

	/* add back the PHY */
	if (WARN_ON(!mvmvif->phy_ctxt))
		return -EINVAL;

	rcu_read_lock();
	ctx = rcu_dereference(vif->chanctx_conf);
	if (WARN_ON(!ctx)) {
		rcu_read_unlock();
		return -EINVAL;
	}
	chandef = ctx->def;
	chains_static = ctx->rx_chains_static;
	chains_dynamic = ctx->rx_chains_dynamic;
	rcu_read_unlock();

	ret = iwl_mvm_phy_ctxt_add(mvm, mvmvif->phy_ctxt, &chandef,
				   chains_static, chains_dynamic);
	if (ret)
		return ret;

	/* add back the MAC */
	mvmvif->uploaded = false;

	if (WARN_ON(!vif->bss_conf.assoc))
		return -EINVAL;

	ret = iwl_mvm_mac_ctxt_add(mvm, vif);
	if (ret)
		return ret;

	/* add back binding - XXX refactor? */
	binding_cmd.id_and_color =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->phy_ctxt->id,
						mvmvif->phy_ctxt->color));
	binding_cmd.action = cpu_to_le32(FW_CTXT_ACTION_ADD);
	binding_cmd.phy =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->phy_ctxt->id,
						mvmvif->phy_ctxt->color));
	binding_cmd.macs[0] = cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id,
							      mvmvif->color));
	for (i = 1; i < MAX_MACS_IN_BINDING; i++)
		binding_cmd.macs[i] = cpu_to_le32(FW_CTXT_INVALID);

	status = 0;
	ret = iwl_mvm_send_cmd_pdu_status(mvm, BINDING_CONTEXT_CMD,
					  IWL_BINDING_CMD_SIZE_V1, &binding_cmd,
					  &status);
	if (ret) {
		IWL_ERR(mvm, "Failed to add binding: %d\n", ret);
		return ret;
	}

	if (status) {
		IWL_ERR(mvm, "Binding command failed: %u\n", status);
		return -EIO;
	}

	ret = iwl_mvm_sta_send_to_fw(mvm, ap_sta, false, 0);
	if (ret)
		return ret;
	rcu_assign_pointer(mvm->fw_id_to_mac_id[mvmvif->ap_sta_id], ap_sta);

	ret = iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
	if (ret)
		return ret;

	/* and some quota */
	quota = iwl_mvm_quota_cmd_get_quota(mvm, &quota_cmd, 0);
	quota->id_and_color =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->phy_ctxt->id,
						mvmvif->phy_ctxt->color));
	quota->quota = cpu_to_le32(IWL_MVM_MAX_QUOTA);
	quota->max_duration = cpu_to_le32(IWL_MVM_MAX_QUOTA);

	for (i = 1; i < MAX_BINDINGS; i++) {
		quota = iwl_mvm_quota_cmd_get_quota(mvm, &quota_cmd, i);
		quota->id_and_color = cpu_to_le32(FW_CTXT_INVALID);
	}

	ret = iwl_mvm_send_cmd_pdu(mvm, TIME_QUOTA_CMD, 0,
				   iwl_mvm_quota_cmd_size(mvm), &quota_cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send quota: %d\n", ret);

	if (iwl_mvm_is_lar_supported(mvm) && iwl_mvm_init_fw_regd(mvm))
		IWL_ERR(mvm, "Failed to initialize D3 LAR information\n");

	return 0;
}

static int iwl_mvm_get_last_nonqos_seq(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_nonqos_seq_query_cmd query_cmd = {
		.get_set_flag = cpu_to_le32(IWL_NONQOS_SEQ_GET),
		.mac_id_n_color =
			cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id,
							mvmvif->color)),
	};
	struct iwl_host_cmd cmd = {
		.id = NON_QOS_TX_COUNTER_CMD,
		.flags = CMD_WANT_SKB,
	};
	int err;
	u32 size;

	cmd.data[0] = &query_cmd;
	cmd.len[0] = sizeof(query_cmd);

	err = iwl_mvm_send_cmd(mvm, &cmd);
	if (err)
		return err;

	size = iwl_rx_packet_payload_len(cmd.resp_pkt);
	if (size < sizeof(__le16)) {
		err = -EINVAL;
	} else {
		err = le16_to_cpup((__le16 *)cmd.resp_pkt->data);
		/* firmware returns next, not last-used seqno */
		err = (u16) (err - 0x10);
	}

	iwl_free_resp(&cmd);
	return err;
}

void iwl_mvm_set_last_nonqos_seq(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_nonqos_seq_query_cmd query_cmd = {
		.get_set_flag = cpu_to_le32(IWL_NONQOS_SEQ_SET),
		.mac_id_n_color =
			cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id,
							mvmvif->color)),
		.value = cpu_to_le16(mvmvif->seqno),
	};

	/* return if called during restart, not resume from D3 */
	if (!mvmvif->seqno_valid)
		return;

	mvmvif->seqno_valid = false;

	if (iwl_mvm_send_cmd_pdu(mvm, NON_QOS_TX_COUNTER_CMD, 0,
				 sizeof(query_cmd), &query_cmd))
		IWL_ERR(mvm, "failed to set non-QoS seqno\n");
}

static int iwl_mvm_switch_to_d3(struct iwl_mvm *mvm)
{
	iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_REGULAR, true);

	iwl_mvm_stop_device(mvm);
	/*
	 * Set the HW restart bit -- this is mostly true as we're
	 * going to load new firmware and reprogram that, though
	 * the reprogramming is going to be manual to avoid adding
	 * all the MACs that aren't support.
	 * We don't have to clear up everything though because the
	 * reprogramming is manual. When we resume, we'll actually
	 * go through a proper restart sequence again to switch
	 * back to the runtime firmware image.
	 */
	set_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status);

	/* the fw is reset, so all the keys are cleared */
	memset(mvm->fw_key_table, 0, sizeof(mvm->fw_key_table));

	mvm->ptk_ivlen = 0;
	mvm->ptk_icvlen = 0;
	mvm->ptk_ivlen = 0;
	mvm->ptk_icvlen = 0;

	return iwl_mvm_load_d3_fw(mvm);
}

static int
iwl_mvm_get_wowlan_config(struct iwl_mvm *mvm,
			  struct cfg80211_wowlan *wowlan,
			  struct iwl_wowlan_config_cmd *wowlan_config_cmd,
			  struct ieee80211_vif *vif, struct iwl_mvm_vif *mvmvif,
			  struct ieee80211_sta *ap_sta)
{
	int ret;
	struct iwl_mvm_sta *mvm_ap_sta = iwl_mvm_sta_from_mac80211(ap_sta);

	/* TODO: wowlan_config_cmd->wowlan_ba_teardown_tids */

	wowlan_config_cmd->is_11n_connection =
					ap_sta->ht_cap.ht_supported;
	wowlan_config_cmd->flags = ENABLE_L3_FILTERING |
		ENABLE_NBNS_FILTERING | ENABLE_DHCP_FILTERING;

	/* Query the last used seqno and set it */
	ret = iwl_mvm_get_last_nonqos_seq(mvm, vif);
	if (ret < 0)
		return ret;

	wowlan_config_cmd->non_qos_seq = cpu_to_le16(ret);

	iwl_mvm_set_wowlan_qos_seq(mvm_ap_sta, wowlan_config_cmd);

	if (wowlan->disconnect)
		wowlan_config_cmd->wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_BEACON_MISS |
				    IWL_WOWLAN_WAKEUP_LINK_CHANGE);
	if (wowlan->magic_pkt)
		wowlan_config_cmd->wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_MAGIC_PACKET);
	if (wowlan->gtk_rekey_failure)
		wowlan_config_cmd->wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_GTK_REKEY_FAIL);
	if (wowlan->eap_identity_req)
		wowlan_config_cmd->wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_EAP_IDENT_REQ);
	if (wowlan->four_way_handshake)
		wowlan_config_cmd->wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_4WAY_HANDSHAKE);
	if (wowlan->n_patterns)
		wowlan_config_cmd->wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_PATTERN_MATCH);

	if (wowlan->rfkill_release)
		wowlan_config_cmd->wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_RF_KILL_DEASSERT);

	if (wowlan->tcp) {
		/*
		 * Set the "link change" (really "link lost") flag as well
		 * since that implies losing the TCP connection.
		 */
		wowlan_config_cmd->wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_REMOTE_LINK_LOSS |
				    IWL_WOWLAN_WAKEUP_REMOTE_SIGNATURE_TABLE |
				    IWL_WOWLAN_WAKEUP_REMOTE_WAKEUP_PACKET |
				    IWL_WOWLAN_WAKEUP_LINK_CHANGE);
	}

	if (wowlan->any) {
		wowlan_config_cmd->wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_BEACON_MISS |
				    IWL_WOWLAN_WAKEUP_LINK_CHANGE |
				    IWL_WOWLAN_WAKEUP_RX_FRAME |
				    IWL_WOWLAN_WAKEUP_BCN_FILTERING);
	}

	return 0;
}

static void
iwl_mvm_iter_d0i3_ap_keys(struct iwl_mvm *mvm,
			  struct ieee80211_vif *vif,
			  void (*iter)(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta,
				       struct ieee80211_key_conf *key,
				       void *data),
			  void *data)
{
	struct ieee80211_sta *ap_sta;

	rcu_read_lock();

	ap_sta = rcu_dereference(mvm->fw_id_to_mac_id[mvm->d0i3_ap_sta_id]);
	if (IS_ERR_OR_NULL(ap_sta))
		goto out;

	ieee80211_iter_keys_rcu(mvm->hw, vif, iter, data);
out:
	rcu_read_unlock();
}

int iwl_mvm_wowlan_config_key_params(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif,
				     bool d0i3,
				     u32 cmd_flags)
{
	struct iwl_wowlan_kek_kck_material_cmd kek_kck_cmd = {};
	struct iwl_wowlan_tkip_params_cmd tkip_cmd = {};
	bool unified = fw_has_capa(&mvm->fw->ucode_capa,
				   IWL_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG);
	struct wowlan_key_data key_data = {
		.configure_keys = !d0i3 && !unified,
		.use_rsc_tsc = false,
		.tkip = &tkip_cmd,
		.use_tkip = false,
	};
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	key_data.rsc_tsc = kzalloc(sizeof(*key_data.rsc_tsc), GFP_KERNEL);
	if (!key_data.rsc_tsc)
		return -ENOMEM;

	/*
	 * if we have to configure keys, call ieee80211_iter_keys(),
	 * as we need non-atomic context in order to take the
	 * required locks.
	 * for the d0i3 we can't use ieee80211_iter_keys(), as
	 * taking (almost) any mutex might result in deadlock.
	 */
	if (!d0i3) {
		/*
		 * Note that currently we don't propagate cmd_flags
		 * to the iterator. In case of key_data.configure_keys,
		 * all the configured commands are SYNC, and
		 * iwl_mvm_wowlan_program_keys() will take care of
		 * locking/unlocking mvm->mutex.
		 */
		ieee80211_iter_keys(mvm->hw, vif,
				    iwl_mvm_wowlan_program_keys,
				    &key_data);
	} else {
		iwl_mvm_iter_d0i3_ap_keys(mvm, vif,
					  iwl_mvm_wowlan_program_keys,
					  &key_data);
	}

	if (key_data.error) {
		ret = -EIO;
		goto out;
	}

	if (key_data.use_rsc_tsc) {
		ret = iwl_mvm_send_cmd_pdu(mvm,
					   WOWLAN_TSC_RSC_PARAM, cmd_flags,
					   sizeof(*key_data.rsc_tsc),
					   key_data.rsc_tsc);
		if (ret)
			goto out;
	}

	if (key_data.use_tkip &&
	    !fw_has_api(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_API_TKIP_MIC_KEYS)) {
		ret = iwl_mvm_send_cmd_pdu(mvm,
					   WOWLAN_TKIP_PARAM,
					   cmd_flags, sizeof(tkip_cmd),
					   &tkip_cmd);
		if (ret)
			goto out;
	}

	/* configure rekey data only if offloaded rekey is supported (d3) */
	if (mvmvif->rekey_data.valid && !d0i3) {
		memset(&kek_kck_cmd, 0, sizeof(kek_kck_cmd));
		memcpy(kek_kck_cmd.kck, mvmvif->rekey_data.kck,
		       NL80211_KCK_LEN);
		kek_kck_cmd.kck_len = cpu_to_le16(NL80211_KCK_LEN);
		memcpy(kek_kck_cmd.kek, mvmvif->rekey_data.kek,
		       NL80211_KEK_LEN);
		kek_kck_cmd.kek_len = cpu_to_le16(NL80211_KEK_LEN);
		kek_kck_cmd.replay_ctr = mvmvif->rekey_data.replay_ctr;

		ret = iwl_mvm_send_cmd_pdu(mvm,
					   WOWLAN_KEK_KCK_MATERIAL, cmd_flags,
					   sizeof(kek_kck_cmd),
					   &kek_kck_cmd);
		if (ret)
			goto out;
	}
	ret = 0;
out:
	kfree(key_data.rsc_tsc);
	return ret;
}

static int
iwl_mvm_wowlan_config(struct iwl_mvm *mvm,
		      struct cfg80211_wowlan *wowlan,
		      struct iwl_wowlan_config_cmd *wowlan_config_cmd,
		      struct ieee80211_vif *vif, struct iwl_mvm_vif *mvmvif,
		      struct ieee80211_sta *ap_sta)
{
	int ret;
	bool unified_image = fw_has_capa(&mvm->fw->ucode_capa,
					 IWL_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG);

	if (!unified_image) {
		ret = iwl_mvm_switch_to_d3(mvm);
		if (ret)
			return ret;

		ret = iwl_mvm_d3_reprogram(mvm, vif, ap_sta);
		if (ret)
			return ret;
	}

	if (!iwlwifi_mod_params.swcrypto) {
		/*
		 * This needs to be unlocked due to lock ordering
		 * constraints. Since we're in the suspend path
		 * that isn't really a problem though.
		 */
		mutex_unlock(&mvm->mutex);
		ret = iwl_mvm_wowlan_config_key_params(mvm, vif, false,
						       CMD_ASYNC);
		mutex_lock(&mvm->mutex);
		if (ret)
			return ret;
	}

	ret = iwl_mvm_send_cmd_pdu(mvm, WOWLAN_CONFIGURATION, 0,
				   sizeof(*wowlan_config_cmd),
				   wowlan_config_cmd);
	if (ret)
		return ret;

	if (fw_has_api(&mvm->fw->ucode_capa,
		       IWL_UCODE_TLV_API_WOWLAN_TCP_SYN_WAKE))
		ret = iwl_mvm_send_patterns(mvm, wowlan);
	else
		ret = iwl_mvm_send_patterns_v1(mvm, wowlan);
	if (ret)
		return ret;

	return iwl_mvm_send_proto_offload(mvm, vif, false, true, 0);
}

static int
iwl_mvm_netdetect_config(struct iwl_mvm *mvm,
			 struct cfg80211_wowlan *wowlan,
			 struct cfg80211_sched_scan_request *nd_config,
			 struct ieee80211_vif *vif)
{
	struct iwl_wowlan_config_cmd wowlan_config_cmd = {};
	int ret;
	bool unified_image = fw_has_capa(&mvm->fw->ucode_capa,
					 IWL_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG);

	if (!unified_image) {
		ret = iwl_mvm_switch_to_d3(mvm);
		if (ret)
			return ret;
	} else {
		/* In theory, we wouldn't have to stop a running sched
		 * scan in order to start another one (for
		 * net-detect).  But in practice this doesn't seem to
		 * work properly, so stop any running sched_scan now.
		 */
		ret = iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_SCHED, true);
		if (ret)
			return ret;
	}

	/* rfkill release can be either for wowlan or netdetect */
	if (wowlan->rfkill_release)
		wowlan_config_cmd.wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_RF_KILL_DEASSERT);

	ret = iwl_mvm_send_cmd_pdu(mvm, WOWLAN_CONFIGURATION, 0,
				   sizeof(wowlan_config_cmd),
				   &wowlan_config_cmd);
	if (ret)
		return ret;

	ret = iwl_mvm_sched_scan_start(mvm, vif, nd_config, &mvm->nd_ies,
				       IWL_MVM_SCAN_NETDETECT);
	if (ret)
		return ret;

	if (WARN_ON(mvm->nd_match_sets || mvm->nd_channels))
		return -EBUSY;

	/* save the sched scan matchsets... */
	if (nd_config->n_match_sets) {
		mvm->nd_match_sets = kmemdup(nd_config->match_sets,
					     sizeof(*nd_config->match_sets) *
					     nd_config->n_match_sets,
					     GFP_KERNEL);
		if (mvm->nd_match_sets)
			mvm->n_nd_match_sets = nd_config->n_match_sets;
	}

	/* ...and the sched scan channels for later reporting */
	mvm->nd_channels = kmemdup(nd_config->channels,
				   sizeof(*nd_config->channels) *
				   nd_config->n_channels,
				   GFP_KERNEL);
	if (mvm->nd_channels)
		mvm->n_nd_channels = nd_config->n_channels;

	return 0;
}

static void iwl_mvm_free_nd(struct iwl_mvm *mvm)
{
	kfree(mvm->nd_match_sets);
	mvm->nd_match_sets = NULL;
	mvm->n_nd_match_sets = 0;
	kfree(mvm->nd_channels);
	mvm->nd_channels = NULL;
	mvm->n_nd_channels = 0;
}

static int __iwl_mvm_suspend(struct ieee80211_hw *hw,
			     struct cfg80211_wowlan *wowlan,
			     bool test)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct ieee80211_vif *vif = NULL;
	struct iwl_mvm_vif *mvmvif = NULL;
	struct ieee80211_sta *ap_sta = NULL;
	struct iwl_d3_manager_config d3_cfg_cmd_data = {
		/*
		 * Program the minimum sleep time to 10 seconds, as many
		 * platforms have issues processing a wakeup signal while
		 * still being in the process of suspending.
		 */
		.min_sleep_time = cpu_to_le32(10 * 1000 * 1000),
	};
	struct iwl_host_cmd d3_cfg_cmd = {
		.id = D3_CONFIG_CMD,
		.flags = CMD_WANT_SKB,
		.data[0] = &d3_cfg_cmd_data,
		.len[0] = sizeof(d3_cfg_cmd_data),
	};
	int ret;
	int len __maybe_unused;
	bool unified_image = fw_has_capa(&mvm->fw->ucode_capa,
					 IWL_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG);

	if (!wowlan) {
		/*
		 * mac80211 shouldn't get here, but for D3 test
		 * it doesn't warrant a warning
		 */
		WARN_ON(!test);
		return -EINVAL;
	}

	mutex_lock(&mvm->mutex);

	vif = iwl_mvm_get_bss_vif(mvm);
	if (IS_ERR_OR_NULL(vif)) {
		ret = 1;
		goto out_noreset;
	}

	mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (mvmvif->ap_sta_id == IWL_MVM_INVALID_STA) {
		/* if we're not associated, this must be netdetect */
		if (!wowlan->nd_config) {
			ret = 1;
			goto out_noreset;
		}

		ret = iwl_mvm_netdetect_config(
			mvm, wowlan, wowlan->nd_config, vif);
		if (ret)
			goto out;

		mvm->net_detect = true;
	} else {
		struct iwl_wowlan_config_cmd wowlan_config_cmd = {};

		ap_sta = rcu_dereference_protected(
			mvm->fw_id_to_mac_id[mvmvif->ap_sta_id],
			lockdep_is_held(&mvm->mutex));
		if (IS_ERR_OR_NULL(ap_sta)) {
			ret = -EINVAL;
			goto out_noreset;
		}

		ret = iwl_mvm_get_wowlan_config(mvm, wowlan, &wowlan_config_cmd,
						vif, mvmvif, ap_sta);
		if (ret)
			goto out_noreset;
		ret = iwl_mvm_wowlan_config(mvm, wowlan, &wowlan_config_cmd,
					    vif, mvmvif, ap_sta);
		if (ret)
			goto out;

		mvm->net_detect = false;
	}

	ret = iwl_mvm_power_update_device(mvm);
	if (ret)
		goto out;

	ret = iwl_mvm_power_update_mac(mvm);
	if (ret)
		goto out;

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (mvm->d3_wake_sysassert)
		d3_cfg_cmd_data.wakeup_flags |=
			cpu_to_le32(IWL_WAKEUP_D3_CONFIG_FW_ERROR);
#endif

	/*
	 * TODO: this is needed because the firmware is not stopping
	 * the recording automatically before entering D3.  This can
	 * be removed once the FW starts doing that.
	 */
	_iwl_fw_dbg_stop_recording(mvm->fwrt.trans, NULL);

	/* must be last -- this switches firmware state */
	ret = iwl_mvm_send_cmd(mvm, &d3_cfg_cmd);
	if (ret)
		goto out;
#ifdef CONFIG_IWLWIFI_DEBUGFS
	len = iwl_rx_packet_payload_len(d3_cfg_cmd.resp_pkt);
	if (len >= sizeof(u32)) {
		mvm->d3_test_pme_ptr =
			le32_to_cpup((__le32 *)d3_cfg_cmd.resp_pkt->data);
	}
#endif
	iwl_free_resp(&d3_cfg_cmd);

	clear_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status);

	iwl_trans_d3_suspend(mvm->trans, test, !unified_image);
 out:
	if (ret < 0) {
		iwl_mvm_free_nd(mvm);

		if (!unified_image) {
			iwl_mvm_ref(mvm, IWL_MVM_REF_UCODE_DOWN);
			if (mvm->fw_restart > 0) {
				mvm->fw_restart--;
				ieee80211_restart_hw(mvm->hw);
			}
		}
	}
 out_noreset:
	mutex_unlock(&mvm->mutex);

	return ret;
}

static int iwl_mvm_enter_d0i3_sync(struct iwl_mvm *mvm)
{
	struct iwl_notification_wait wait_d3;
	static const u16 d3_notif[] = { D3_CONFIG_CMD };
	int ret;

	iwl_init_notification_wait(&mvm->notif_wait, &wait_d3,
				   d3_notif, ARRAY_SIZE(d3_notif),
				   NULL, NULL);

	ret = iwl_mvm_enter_d0i3(mvm->hw->priv);
	if (ret)
		goto remove_notif;

	ret = iwl_wait_notification(&mvm->notif_wait, &wait_d3, HZ);
	WARN_ON_ONCE(ret);
	return ret;

remove_notif:
	iwl_remove_notification(&mvm->notif_wait, &wait_d3);
	return ret;
}

int iwl_mvm_suspend(struct ieee80211_hw *hw, struct cfg80211_wowlan *wowlan)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_trans *trans = mvm->trans;
	int ret;

	/* make sure the d0i3 exit work is not pending */
	flush_work(&mvm->d0i3_exit_work);
	iwl_mvm_pause_tcm(mvm, true);

	iwl_fw_runtime_suspend(&mvm->fwrt);

	ret = iwl_trans_suspend(trans);
	if (ret)
		return ret;

	if (wowlan->any) {
		trans->system_pm_mode = IWL_PLAT_PM_MODE_D0I3;

		if (iwl_mvm_enter_d0i3_on_suspend(mvm)) {
			ret = iwl_mvm_enter_d0i3_sync(mvm);

			if (ret)
				return ret;
		}

		mutex_lock(&mvm->d0i3_suspend_mutex);
		__set_bit(D0I3_DEFER_WAKEUP, &mvm->d0i3_suspend_flags);
		mutex_unlock(&mvm->d0i3_suspend_mutex);

		iwl_trans_d3_suspend(trans, false, false);

		return 0;
	}

	trans->system_pm_mode = IWL_PLAT_PM_MODE_D3;

	return __iwl_mvm_suspend(hw, wowlan, false);
}

/* converted data from the different status responses */
struct iwl_wowlan_status_data {
	u16 pattern_number;
	u16 qos_seq_ctr[8];
	u32 wakeup_reasons;
	u32 wake_packet_length;
	u32 wake_packet_bufsize;
	const u8 *wake_packet;
};

static void iwl_mvm_report_wakeup_reasons(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  struct iwl_wowlan_status_data *status)
{
	struct sk_buff *pkt = NULL;
	struct cfg80211_wowlan_wakeup wakeup = {
		.pattern_idx = -1,
	};
	struct cfg80211_wowlan_wakeup *wakeup_report = &wakeup;
	u32 reasons = status->wakeup_reasons;

	if (reasons == IWL_WOWLAN_WAKEUP_BY_NON_WIRELESS) {
		wakeup_report = NULL;
		goto report;
	}

	pm_wakeup_event(mvm->dev, 0);

	if (reasons & IWL_WOWLAN_WAKEUP_BY_MAGIC_PACKET)
		wakeup.magic_pkt = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_PATTERN)
		wakeup.pattern_idx =
			status->pattern_number;

	if (reasons & (IWL_WOWLAN_WAKEUP_BY_DISCONNECTION_ON_MISSED_BEACON |
		       IWL_WOWLAN_WAKEUP_BY_DISCONNECTION_ON_DEAUTH))
		wakeup.disconnect = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_GTK_REKEY_FAILURE)
		wakeup.gtk_rekey_failure = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_RFKILL_DEASSERTED)
		wakeup.rfkill_release = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_EAPOL_REQUEST)
		wakeup.eap_identity_req = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_FOUR_WAY_HANDSHAKE)
		wakeup.four_way_handshake = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_REM_WAKE_LINK_LOSS)
		wakeup.tcp_connlost = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_REM_WAKE_SIGNATURE_TABLE)
		wakeup.tcp_nomoretokens = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_REM_WAKE_WAKEUP_PACKET)
		wakeup.tcp_match = true;

	if (status->wake_packet_bufsize) {
		int pktsize = status->wake_packet_bufsize;
		int pktlen = status->wake_packet_length;
		const u8 *pktdata = status->wake_packet;
		struct ieee80211_hdr *hdr = (void *)pktdata;
		int truncated = pktlen - pktsize;

		/* this would be a firmware bug */
		if (WARN_ON_ONCE(truncated < 0))
			truncated = 0;

		if (ieee80211_is_data(hdr->frame_control)) {
			int hdrlen = ieee80211_hdrlen(hdr->frame_control);
			int ivlen = 0, icvlen = 4; /* also FCS */

			pkt = alloc_skb(pktsize, GFP_KERNEL);
			if (!pkt)
				goto report;

			skb_put_data(pkt, pktdata, hdrlen);
			pktdata += hdrlen;
			pktsize -= hdrlen;

			if (ieee80211_has_protected(hdr->frame_control)) {
				/*
				 * This is unlocked and using gtk_i(c)vlen,
				 * but since everything is under RTNL still
				 * that's not really a problem - changing
				 * it would be difficult.
				 */
				if (is_multicast_ether_addr(hdr->addr1)) {
					ivlen = mvm->gtk_ivlen;
					icvlen += mvm->gtk_icvlen;
				} else {
					ivlen = mvm->ptk_ivlen;
					icvlen += mvm->ptk_icvlen;
				}
			}

			/* if truncated, FCS/ICV is (partially) gone */
			if (truncated >= icvlen) {
				icvlen = 0;
				truncated -= icvlen;
			} else {
				icvlen -= truncated;
				truncated = 0;
			}

			pktsize -= ivlen + icvlen;
			pktdata += ivlen;

			skb_put_data(pkt, pktdata, pktsize);

			if (ieee80211_data_to_8023(pkt, vif->addr, vif->type))
				goto report;
			wakeup.packet = pkt->data;
			wakeup.packet_present_len = pkt->len;
			wakeup.packet_len = pkt->len - truncated;
			wakeup.packet_80211 = false;
		} else {
			int fcslen = 4;

			if (truncated >= 4) {
				truncated -= 4;
				fcslen = 0;
			} else {
				fcslen -= truncated;
				truncated = 0;
			}
			pktsize -= fcslen;
			wakeup.packet = status->wake_packet;
			wakeup.packet_present_len = pktsize;
			wakeup.packet_len = pktlen - truncated;
			wakeup.packet_80211 = true;
		}
	}

 report:
	ieee80211_report_wowlan_wakeup(vif, wakeup_report, GFP_KERNEL);
	kfree_skb(pkt);
}

static void iwl_mvm_aes_sc_to_seq(struct aes_sc *sc,
				  struct ieee80211_key_seq *seq)
{
	u64 pn;

	pn = le64_to_cpu(sc->pn);
	seq->ccmp.pn[0] = pn >> 40;
	seq->ccmp.pn[1] = pn >> 32;
	seq->ccmp.pn[2] = pn >> 24;
	seq->ccmp.pn[3] = pn >> 16;
	seq->ccmp.pn[4] = pn >> 8;
	seq->ccmp.pn[5] = pn;
}

static void iwl_mvm_tkip_sc_to_seq(struct tkip_sc *sc,
				   struct ieee80211_key_seq *seq)
{
	seq->tkip.iv32 = le32_to_cpu(sc->iv32);
	seq->tkip.iv16 = le16_to_cpu(sc->iv16);
}

static void iwl_mvm_set_aes_rx_seq(struct iwl_mvm *mvm, struct aes_sc *scs,
				   struct ieee80211_sta *sta,
				   struct ieee80211_key_conf *key)
{
	int tid;

	BUILD_BUG_ON(IWL_NUM_RSC != IEEE80211_NUM_TIDS);

	if (sta && iwl_mvm_has_new_rx_api(mvm)) {
		struct iwl_mvm_sta *mvmsta;
		struct iwl_mvm_key_pn *ptk_pn;

		mvmsta = iwl_mvm_sta_from_mac80211(sta);

		ptk_pn = rcu_dereference_protected(mvmsta->ptk_pn[key->keyidx],
						   lockdep_is_held(&mvm->mutex));
		if (WARN_ON(!ptk_pn))
			return;

		for (tid = 0; tid < IWL_MAX_TID_COUNT; tid++) {
			struct ieee80211_key_seq seq = {};
			int i;

			iwl_mvm_aes_sc_to_seq(&scs[tid], &seq);
			ieee80211_set_key_rx_seq(key, tid, &seq);
			for (i = 1; i < mvm->trans->num_rx_queues; i++)
				memcpy(ptk_pn->q[i].pn[tid],
				       seq.ccmp.pn, IEEE80211_CCMP_PN_LEN);
		}
	} else {
		for (tid = 0; tid < IWL_NUM_RSC; tid++) {
			struct ieee80211_key_seq seq = {};

			iwl_mvm_aes_sc_to_seq(&scs[tid], &seq);
			ieee80211_set_key_rx_seq(key, tid, &seq);
		}
	}
}

static void iwl_mvm_set_tkip_rx_seq(struct tkip_sc *scs,
				    struct ieee80211_key_conf *key)
{
	int tid;

	BUILD_BUG_ON(IWL_NUM_RSC != IEEE80211_NUM_TIDS);

	for (tid = 0; tid < IWL_NUM_RSC; tid++) {
		struct ieee80211_key_seq seq = {};

		iwl_mvm_tkip_sc_to_seq(&scs[tid], &seq);
		ieee80211_set_key_rx_seq(key, tid, &seq);
	}
}

static void iwl_mvm_set_key_rx_seq(struct iwl_mvm *mvm,
				   struct ieee80211_key_conf *key,
				   struct iwl_wowlan_status *status)
{
	union iwl_all_tsc_rsc *rsc = &status->gtk[0].rsc.all_tsc_rsc;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		iwl_mvm_set_aes_rx_seq(mvm, rsc->aes.multicast_rsc, NULL, key);
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		iwl_mvm_set_tkip_rx_seq(rsc->tkip.multicast_rsc, key);
		break;
	default:
		WARN_ON(1);
	}
}

struct iwl_mvm_d3_gtk_iter_data {
	struct iwl_mvm *mvm;
	struct iwl_wowlan_status *status;
	void *last_gtk;
	u32 cipher;
	bool find_phase, unhandled_cipher;
	int num_keys;
};

static void iwl_mvm_d3_update_keys(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta,
				   struct ieee80211_key_conf *key,
				   void *_data)
{
	struct iwl_mvm_d3_gtk_iter_data *data = _data;

	if (data->unhandled_cipher)
		return;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		/* ignore WEP completely, nothing to do */
		return;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_TKIP:
		/* we support these */
		break;
	default:
		/* everything else (even CMAC for MFP) - disconnect from AP */
		data->unhandled_cipher = true;
		return;
	}

	data->num_keys++;

	/*
	 * pairwise key - update sequence counters only;
	 * note that this assumes no TDLS sessions are active
	 */
	if (sta) {
		struct ieee80211_key_seq seq = {};
		union iwl_all_tsc_rsc *sc =
			&data->status->gtk[0].rsc.all_tsc_rsc;

		if (data->find_phase)
			return;

		switch (key->cipher) {
		case WLAN_CIPHER_SUITE_CCMP:
			iwl_mvm_set_aes_rx_seq(data->mvm, sc->aes.unicast_rsc,
					       sta, key);
			atomic64_set(&key->tx_pn, le64_to_cpu(sc->aes.tsc.pn));
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			iwl_mvm_tkip_sc_to_seq(&sc->tkip.tsc, &seq);
			iwl_mvm_set_tkip_rx_seq(sc->tkip.unicast_rsc, key);
			atomic64_set(&key->tx_pn,
				     (u64)seq.tkip.iv16 |
				     ((u64)seq.tkip.iv32 << 16));
			break;
		}

		/* that's it for this key */
		return;
	}

	if (data->find_phase) {
		data->last_gtk = key;
		data->cipher = key->cipher;
		return;
	}

	if (data->status->num_of_gtk_rekeys)
		ieee80211_remove_key(key);
	else if (data->last_gtk == key)
		iwl_mvm_set_key_rx_seq(data->mvm, key, data->status);
}

static bool iwl_mvm_setup_connection_keep(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  struct iwl_wowlan_status *status)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_d3_gtk_iter_data gtkdata = {
		.mvm = mvm,
		.status = status,
	};
	u32 disconnection_reasons =
		IWL_WOWLAN_WAKEUP_BY_DISCONNECTION_ON_MISSED_BEACON |
		IWL_WOWLAN_WAKEUP_BY_DISCONNECTION_ON_DEAUTH;

	if (!status || !vif->bss_conf.bssid)
		return false;

	if (le32_to_cpu(status->wakeup_reasons) & disconnection_reasons)
		return false;

	/* find last GTK that we used initially, if any */
	gtkdata.find_phase = true;
	ieee80211_iter_keys(mvm->hw, vif,
			    iwl_mvm_d3_update_keys, &gtkdata);
	/* not trying to keep connections with MFP/unhandled ciphers */
	if (gtkdata.unhandled_cipher)
		return false;
	if (!gtkdata.num_keys)
		goto out;
	if (!gtkdata.last_gtk)
		return false;

	/*
	 * invalidate all other GTKs that might still exist and update
	 * the one that we used
	 */
	gtkdata.find_phase = false;
	ieee80211_iter_keys(mvm->hw, vif,
			    iwl_mvm_d3_update_keys, &gtkdata);

	if (status->num_of_gtk_rekeys) {
		struct ieee80211_key_conf *key;
		struct {
			struct ieee80211_key_conf conf;
			u8 key[32];
		} conf = {
			.conf.cipher = gtkdata.cipher,
			.conf.keyidx =
				iwlmvm_wowlan_gtk_idx(&status->gtk[0]),
		};
		__be64 replay_ctr;

		switch (gtkdata.cipher) {
		case WLAN_CIPHER_SUITE_CCMP:
			conf.conf.keylen = WLAN_KEY_LEN_CCMP;
			memcpy(conf.conf.key, status->gtk[0].key,
			       WLAN_KEY_LEN_CCMP);
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			conf.conf.keylen = WLAN_KEY_LEN_TKIP;
			memcpy(conf.conf.key, status->gtk[0].key, 16);
			/* leave TX MIC key zeroed, we don't use it anyway */
			memcpy(conf.conf.key +
			       NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY,
			       status->gtk[0].tkip_mic_key, 8);
			break;
		}

		key = ieee80211_gtk_rekey_add(vif, &conf.conf);
		if (IS_ERR(key))
			return false;
		iwl_mvm_set_key_rx_seq(mvm, key, status);

		replay_ctr =
			cpu_to_be64(le64_to_cpu(status->replay_ctr));

		ieee80211_gtk_rekey_notify(vif, vif->bss_conf.bssid,
					   (void *)&replay_ctr, GFP_KERNEL);
	}

out:
	mvmvif->seqno_valid = true;
	/* +0x10 because the set API expects next-to-use, not last-used */
	mvmvif->seqno = le16_to_cpu(status->non_qos_seq_ctr) + 0x10;

	return true;
}

struct iwl_wowlan_status *iwl_mvm_send_wowlan_get_status(struct iwl_mvm *mvm)
{
	struct iwl_wowlan_status *v7, *status;
	struct iwl_host_cmd cmd = {
		.id = WOWLAN_GET_STATUSES,
		.flags = CMD_WANT_SKB,
	};
	int ret, len, status_size;

	lockdep_assert_held(&mvm->mutex);

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (ret) {
		IWL_ERR(mvm, "failed to query wakeup status (%d)\n", ret);
		return ERR_PTR(ret);
	}

	if (!fw_has_api(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_API_WOWLAN_KEY_MATERIAL)) {
		struct iwl_wowlan_status_v6 *v6 = (void *)cmd.resp_pkt->data;
		int data_size;

		status_size = sizeof(*v6);
		len = iwl_rx_packet_payload_len(cmd.resp_pkt);

		if (len < status_size) {
			IWL_ERR(mvm, "Invalid WoWLAN status response!\n");
			status = ERR_PTR(-EIO);
			goto out_free_resp;
		}

		data_size = ALIGN(le32_to_cpu(v6->wake_packet_bufsize), 4);

		if (len != (status_size + data_size)) {
			IWL_ERR(mvm, "Invalid WoWLAN status response!\n");
			status = ERR_PTR(-EIO);
			goto out_free_resp;
		}

		status = kzalloc(sizeof(*status) + data_size, GFP_KERNEL);
		if (!status)
			goto out_free_resp;

		BUILD_BUG_ON(sizeof(v6->gtk.decrypt_key) >
			     sizeof(status->gtk[0].key));
		BUILD_BUG_ON(sizeof(v6->gtk.tkip_mic_key) >
			     sizeof(status->gtk[0].tkip_mic_key));

		/* copy GTK info to the right place */
		memcpy(status->gtk[0].key, v6->gtk.decrypt_key,
		       sizeof(v6->gtk.decrypt_key));
		memcpy(status->gtk[0].tkip_mic_key, v6->gtk.tkip_mic_key,
		       sizeof(v6->gtk.tkip_mic_key));
		memcpy(&status->gtk[0].rsc, &v6->gtk.rsc,
		       sizeof(status->gtk[0].rsc));

		/* hardcode the key length to 16 since v6 only supports 16 */
		status->gtk[0].key_len = 16;

		/*
		 * The key index only uses 2 bits (values 0 to 3) and
		 * we always set bit 7 which means this is the
		 * currently used key.
		 */
		status->gtk[0].key_flags = v6->gtk.key_index | BIT(7);

		status->replay_ctr = v6->replay_ctr;

		/* everything starting from pattern_number is identical */
		memcpy(&status->pattern_number, &v6->pattern_number,
		       offsetof(struct iwl_wowlan_status, wake_packet) -
		       offsetof(struct iwl_wowlan_status, pattern_number) +
		       data_size);

		goto out_free_resp;
	}

	v7 = (void *)cmd.resp_pkt->data;
	status_size = sizeof(*v7);
	len = iwl_rx_packet_payload_len(cmd.resp_pkt);

	if (len < status_size) {
		IWL_ERR(mvm, "Invalid WoWLAN status response!\n");
		status = ERR_PTR(-EIO);
		goto out_free_resp;
	}

	if (len != (status_size +
		    ALIGN(le32_to_cpu(v7->wake_packet_bufsize), 4))) {
		IWL_ERR(mvm, "Invalid WoWLAN status response!\n");
		status = ERR_PTR(-EIO);
		goto out_free_resp;
	}

	status = kmemdup(v7, len, GFP_KERNEL);

out_free_resp:
	iwl_free_resp(&cmd);
	return status;
}

static struct iwl_wowlan_status *
iwl_mvm_get_wakeup_status(struct iwl_mvm *mvm)
{
	int ret;

	/* only for tracing for now */
	ret = iwl_mvm_send_cmd_pdu(mvm, OFFLOADS_QUERY_CMD, 0, 0, NULL);
	if (ret)
		IWL_ERR(mvm, "failed to query offload statistics (%d)\n", ret);

	return iwl_mvm_send_wowlan_get_status(mvm);
}

/* releases the MVM mutex */
static bool iwl_mvm_query_wakeup_reasons(struct iwl_mvm *mvm,
					 struct ieee80211_vif *vif)
{
	struct iwl_wowlan_status_data status;
	struct iwl_wowlan_status *fw_status;
	int i;
	bool keep;
	struct iwl_mvm_sta *mvm_ap_sta;

	fw_status = iwl_mvm_get_wakeup_status(mvm);
	if (IS_ERR_OR_NULL(fw_status))
		goto out_unlock;

	status.pattern_number = le16_to_cpu(fw_status->pattern_number);
	for (i = 0; i < 8; i++)
		status.qos_seq_ctr[i] =
			le16_to_cpu(fw_status->qos_seq_ctr[i]);
	status.wakeup_reasons = le32_to_cpu(fw_status->wakeup_reasons);
	status.wake_packet_length =
		le32_to_cpu(fw_status->wake_packet_length);
	status.wake_packet_bufsize =
		le32_to_cpu(fw_status->wake_packet_bufsize);
	status.wake_packet = fw_status->wake_packet;

	/* still at hard-coded place 0 for D3 image */
	mvm_ap_sta = iwl_mvm_sta_from_staid_protected(mvm, 0);
	if (!mvm_ap_sta)
		goto out_free;

	for (i = 0; i < IWL_MAX_TID_COUNT; i++) {
		u16 seq = status.qos_seq_ctr[i];
		/* firmware stores last-used value, we store next value */
		seq += 0x10;
		mvm_ap_sta->tid_data[i].seq_number = seq;
	}

	/* now we have all the data we need, unlock to avoid mac80211 issues */
	mutex_unlock(&mvm->mutex);

	iwl_mvm_report_wakeup_reasons(mvm, vif, &status);

	keep = iwl_mvm_setup_connection_keep(mvm, vif, fw_status);

	kfree(fw_status);
	return keep;

out_free:
	kfree(fw_status);
out_unlock:
	mutex_unlock(&mvm->mutex);
	return false;
}

void iwl_mvm_d0i3_update_keys(struct iwl_mvm *mvm,
			      struct ieee80211_vif *vif,
			      struct iwl_wowlan_status *status)
{
	struct iwl_mvm_d3_gtk_iter_data gtkdata = {
		.mvm = mvm,
		.status = status,
	};

	/*
	 * rekey handling requires taking locks that can't be taken now.
	 * however, d0i3 doesn't offload rekey, so we're fine.
	 */
	if (WARN_ON_ONCE(status->num_of_gtk_rekeys))
		return;

	/* find last GTK that we used initially, if any */
	gtkdata.find_phase = true;
	iwl_mvm_iter_d0i3_ap_keys(mvm, vif, iwl_mvm_d3_update_keys, &gtkdata);

	gtkdata.find_phase = false;
	iwl_mvm_iter_d0i3_ap_keys(mvm, vif, iwl_mvm_d3_update_keys, &gtkdata);
}

#define ND_QUERY_BUF_LEN (sizeof(struct iwl_scan_offload_profile_match) * \
			  IWL_SCAN_MAX_PROFILES)

struct iwl_mvm_nd_query_results {
	u32 matched_profiles;
	u8 matches[ND_QUERY_BUF_LEN];
};

static int
iwl_mvm_netdetect_query_results(struct iwl_mvm *mvm,
				struct iwl_mvm_nd_query_results *results)
{
	struct iwl_scan_offload_profiles_query *query;
	struct iwl_host_cmd cmd = {
		.id = SCAN_OFFLOAD_PROFILES_QUERY_CMD,
		.flags = CMD_WANT_SKB,
	};
	int ret, len;
	size_t query_len, matches_len;

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (ret) {
		IWL_ERR(mvm, "failed to query matched profiles (%d)\n", ret);
		return ret;
	}

	if (fw_has_api(&mvm->fw->ucode_capa,
		       IWL_UCODE_TLV_API_SCAN_OFFLOAD_CHANS)) {
		query_len = sizeof(struct iwl_scan_offload_profiles_query);
		matches_len = sizeof(struct iwl_scan_offload_profile_match) *
			IWL_SCAN_MAX_PROFILES;
	} else {
		query_len = sizeof(struct iwl_scan_offload_profiles_query_v1);
		matches_len = sizeof(struct iwl_scan_offload_profile_match_v1) *
			IWL_SCAN_MAX_PROFILES;
	}

	len = iwl_rx_packet_payload_len(cmd.resp_pkt);
	if (len < query_len) {
		IWL_ERR(mvm, "Invalid scan offload profiles query response!\n");
		ret = -EIO;
		goto out_free_resp;
	}

	query = (void *)cmd.resp_pkt->data;

	results->matched_profiles = le32_to_cpu(query->matched_profiles);
	memcpy(results->matches, query->matches, matches_len);

#ifdef CONFIG_IWLWIFI_DEBUGFS
	mvm->last_netdetect_scans = le32_to_cpu(query->n_scans_done);
#endif

out_free_resp:
	iwl_free_resp(&cmd);
	return ret;
}

static int iwl_mvm_query_num_match_chans(struct iwl_mvm *mvm,
					 struct iwl_mvm_nd_query_results *query,
					 int idx)
{
	int n_chans = 0, i;

	if (fw_has_api(&mvm->fw->ucode_capa,
		       IWL_UCODE_TLV_API_SCAN_OFFLOAD_CHANS)) {
		struct iwl_scan_offload_profile_match *matches =
			(struct iwl_scan_offload_profile_match *)query->matches;

		for (i = 0; i < SCAN_OFFLOAD_MATCHING_CHANNELS_LEN; i++)
			n_chans += hweight8(matches[idx].matching_channels[i]);
	} else {
		struct iwl_scan_offload_profile_match_v1 *matches =
			(struct iwl_scan_offload_profile_match_v1 *)query->matches;

		for (i = 0; i < SCAN_OFFLOAD_MATCHING_CHANNELS_LEN_V1; i++)
			n_chans += hweight8(matches[idx].matching_channels[i]);
	}

	return n_chans;
}

static void iwl_mvm_query_set_freqs(struct iwl_mvm *mvm,
				    struct iwl_mvm_nd_query_results *query,
				    struct cfg80211_wowlan_nd_match *match,
				    int idx)
{
	int i;

	if (fw_has_api(&mvm->fw->ucode_capa,
		       IWL_UCODE_TLV_API_SCAN_OFFLOAD_CHANS)) {
		struct iwl_scan_offload_profile_match *matches =
			(struct iwl_scan_offload_profile_match *)query->matches;

		for (i = 0; i < SCAN_OFFLOAD_MATCHING_CHANNELS_LEN * 8; i++)
			if (matches[idx].matching_channels[i / 8] & (BIT(i % 8)))
				match->channels[match->n_channels++] =
					mvm->nd_channels[i]->center_freq;
	} else {
		struct iwl_scan_offload_profile_match_v1 *matches =
			(struct iwl_scan_offload_profile_match_v1 *)query->matches;

		for (i = 0; i < SCAN_OFFLOAD_MATCHING_CHANNELS_LEN_V1 * 8; i++)
			if (matches[idx].matching_channels[i / 8] & (BIT(i % 8)))
				match->channels[match->n_channels++] =
					mvm->nd_channels[i]->center_freq;
	}
}

static void iwl_mvm_query_netdetect_reasons(struct iwl_mvm *mvm,
					    struct ieee80211_vif *vif)
{
	struct cfg80211_wowlan_nd_info *net_detect = NULL;
	struct cfg80211_wowlan_wakeup wakeup = {
		.pattern_idx = -1,
	};
	struct cfg80211_wowlan_wakeup *wakeup_report = &wakeup;
	struct iwl_mvm_nd_query_results query;
	struct iwl_wowlan_status *fw_status;
	unsigned long matched_profiles;
	u32 reasons = 0;
	int i, n_matches, ret;

	fw_status = iwl_mvm_get_wakeup_status(mvm);
	if (!IS_ERR_OR_NULL(fw_status)) {
		reasons = le32_to_cpu(fw_status->wakeup_reasons);
		kfree(fw_status);
	}

	if (reasons & IWL_WOWLAN_WAKEUP_BY_RFKILL_DEASSERTED)
		wakeup.rfkill_release = true;

	if (reasons != IWL_WOWLAN_WAKEUP_BY_NON_WIRELESS)
		goto out;

	ret = iwl_mvm_netdetect_query_results(mvm, &query);
	if (ret || !query.matched_profiles) {
		wakeup_report = NULL;
		goto out;
	}

	matched_profiles = query.matched_profiles;
	if (mvm->n_nd_match_sets) {
		n_matches = hweight_long(matched_profiles);
	} else {
		IWL_ERR(mvm, "no net detect match information available\n");
		n_matches = 0;
	}

	net_detect = kzalloc(struct_size(net_detect, matches, n_matches),
			     GFP_KERNEL);
	if (!net_detect || !n_matches)
		goto out_report_nd;

	for_each_set_bit(i, &matched_profiles, mvm->n_nd_match_sets) {
		struct cfg80211_wowlan_nd_match *match;
		int idx, n_channels = 0;

		n_channels = iwl_mvm_query_num_match_chans(mvm, &query, i);

		match = kzalloc(struct_size(match, channels, n_channels),
				GFP_KERNEL);
		if (!match)
			goto out_report_nd;

		net_detect->matches[net_detect->n_matches++] = match;

		/* We inverted the order of the SSIDs in the scan
		 * request, so invert the index here.
		 */
		idx = mvm->n_nd_match_sets - i - 1;
		match->ssid.ssid_len = mvm->nd_match_sets[idx].ssid.ssid_len;
		memcpy(match->ssid.ssid, mvm->nd_match_sets[idx].ssid.ssid,
		       match->ssid.ssid_len);

		if (mvm->n_nd_channels < n_channels)
			continue;

		iwl_mvm_query_set_freqs(mvm, &query, match, i);
	}

out_report_nd:
	wakeup.net_detect = net_detect;
out:
	iwl_mvm_free_nd(mvm);

	mutex_unlock(&mvm->mutex);
	ieee80211_report_wowlan_wakeup(vif, wakeup_report, GFP_KERNEL);

	if (net_detect) {
		for (i = 0; i < net_detect->n_matches; i++)
			kfree(net_detect->matches[i]);
		kfree(net_detect);
	}
}

static void iwl_mvm_read_d3_sram(struct iwl_mvm *mvm)
{
#ifdef CONFIG_IWLWIFI_DEBUGFS
	const struct fw_img *img = &mvm->fw->img[IWL_UCODE_WOWLAN];
	u32 len = img->sec[IWL_UCODE_SECTION_DATA].len;
	u32 offs = img->sec[IWL_UCODE_SECTION_DATA].offset;

	if (!mvm->store_d3_resume_sram)
		return;

	if (!mvm->d3_resume_sram) {
		mvm->d3_resume_sram = kzalloc(len, GFP_KERNEL);
		if (!mvm->d3_resume_sram)
			return;
	}

	iwl_trans_read_mem_bytes(mvm->trans, offs, mvm->d3_resume_sram, len);
#endif
}

static void iwl_mvm_d3_disconnect_iter(void *data, u8 *mac,
				       struct ieee80211_vif *vif)
{
	/* skip the one we keep connection on */
	if (data == vif)
		return;

	if (vif->type == NL80211_IFTYPE_STATION)
		ieee80211_resume_disconnect(vif);
}

static int iwl_mvm_check_rt_status(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif)
{
	u32 base = mvm->trans->lmac_error_event_table[0];
	struct error_table_start {
		/* cf. struct iwl_error_event_table */
		u32 valid;
		u32 error_id;
	} err_info;

	iwl_trans_read_mem_bytes(mvm->trans, base,
				 &err_info, sizeof(err_info));

	if (err_info.valid &&
	    err_info.error_id == RF_KILL_INDICATOR_FOR_WOWLAN) {
		struct cfg80211_wowlan_wakeup wakeup = {
			.rfkill_release = true,
		};
		ieee80211_report_wowlan_wakeup(vif, &wakeup, GFP_KERNEL);
	}
	return err_info.valid;
}

static int __iwl_mvm_resume(struct iwl_mvm *mvm, bool test)
{
	struct ieee80211_vif *vif = NULL;
	int ret = 1;
	enum iwl_d3_status d3_status;
	bool keep = false;
	bool unified_image = fw_has_capa(&mvm->fw->ucode_capa,
					 IWL_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG);
	bool d0i3_first = fw_has_capa(&mvm->fw->ucode_capa,
				      IWL_UCODE_TLV_CAPA_D0I3_END_FIRST);

	mutex_lock(&mvm->mutex);

	/* get the BSS vif pointer again */
	vif = iwl_mvm_get_bss_vif(mvm);
	if (IS_ERR_OR_NULL(vif))
		goto err;

	ret = iwl_trans_d3_resume(mvm->trans, &d3_status, test, !unified_image);
	if (ret)
		goto err;

	if (d3_status != IWL_D3_STATUS_ALIVE) {
		IWL_INFO(mvm, "Device was reset during suspend\n");
		goto err;
	}

	iwl_fw_dbg_read_d3_debug_data(&mvm->fwrt);
	/* query SRAM first in case we want event logging */
	iwl_mvm_read_d3_sram(mvm);

	if (iwl_mvm_check_rt_status(mvm, vif)) {
		set_bit(STATUS_FW_ERROR, &mvm->trans->status);
		iwl_mvm_dump_nic_error_log(mvm);
		iwl_fw_dbg_collect_desc(&mvm->fwrt, &iwl_dump_desc_assert,
					false, 0);
		ret = 1;
		goto err;
	}

	if (d0i3_first) {
		ret = iwl_mvm_send_cmd_pdu(mvm, D0I3_END_CMD, 0, 0, NULL);
		if (ret < 0) {
			IWL_ERR(mvm, "Failed to send D0I3_END_CMD first (%d)\n",
				ret);
			goto err;
		}
	}

	/*
	 * Query the current location and source from the D3 firmware so we
	 * can play it back when we re-intiailize the D0 firmware
	 */
	iwl_mvm_update_changed_regdom(mvm);

	if (!unified_image)
		/*  Re-configure default SAR profile */
		iwl_mvm_sar_select_profile(mvm, 1, 1);

	if (mvm->net_detect) {
		/* If this is a non-unified image, we restart the FW,
		 * so no need to stop the netdetect scan.  If that
		 * fails, continue and try to get the wake-up reasons,
		 * but trigger a HW restart by keeping a failure code
		 * in ret.
		 */
		if (unified_image)
			ret = iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_NETDETECT,
						false);

		iwl_mvm_query_netdetect_reasons(mvm, vif);
		/* has unlocked the mutex, so skip that */
		goto out;
	} else {
		keep = iwl_mvm_query_wakeup_reasons(mvm, vif);
#ifdef CONFIG_IWLWIFI_DEBUGFS
		if (keep)
			mvm->keep_vif = vif;
#endif
		/* has unlocked the mutex, so skip that */
		goto out_iterate;
	}

err:
	iwl_mvm_free_nd(mvm);
	mutex_unlock(&mvm->mutex);

out_iterate:
	if (!test)
		ieee80211_iterate_active_interfaces_rtnl(mvm->hw,
			IEEE80211_IFACE_ITER_NORMAL,
			iwl_mvm_d3_disconnect_iter, keep ? vif : NULL);

out:
	/* no need to reset the device in unified images, if successful */
	if (unified_image && !ret) {
		/* nothing else to do if we already sent D0I3_END_CMD */
		if (d0i3_first)
			return 0;

		ret = iwl_mvm_send_cmd_pdu(mvm, D0I3_END_CMD, 0, 0, NULL);
		if (!ret)
			return 0;
	}

	/*
	 * Reconfigure the device in one of the following cases:
	 * 1. We are not using a unified image
	 * 2. We are using a unified image but had an error while exiting D3
	 */
	set_bit(IWL_MVM_STATUS_HW_RESTART_REQUESTED, &mvm->status);
	/*
	 * When switching images we return 1, which causes mac80211
	 * to do a reconfig with IEEE80211_RECONFIG_TYPE_RESTART.
	 * This type of reconfig calls iwl_mvm_restart_complete(),
	 * where we unref the IWL_MVM_REF_UCODE_DOWN, so we need
	 * to take the reference here.
	 */
	iwl_mvm_ref(mvm, IWL_MVM_REF_UCODE_DOWN);

	return 1;
}

static int iwl_mvm_resume_d3(struct iwl_mvm *mvm)
{
	iwl_trans_resume(mvm->trans);

	return __iwl_mvm_resume(mvm, false);
}

static int iwl_mvm_resume_d0i3(struct iwl_mvm *mvm)
{
	bool exit_now;
	enum iwl_d3_status d3_status;
	struct iwl_trans *trans = mvm->trans;

	iwl_trans_d3_resume(trans, &d3_status, false, false);

	/*
	 * make sure to clear D0I3_DEFER_WAKEUP before
	 * calling iwl_trans_resume(), which might wait
	 * for d0i3 exit completion.
	 */
	mutex_lock(&mvm->d0i3_suspend_mutex);
	__clear_bit(D0I3_DEFER_WAKEUP, &mvm->d0i3_suspend_flags);
	exit_now = __test_and_clear_bit(D0I3_PENDING_WAKEUP,
					&mvm->d0i3_suspend_flags);
	mutex_unlock(&mvm->d0i3_suspend_mutex);
	if (exit_now) {
		IWL_DEBUG_RPM(mvm, "Run deferred d0i3 exit\n");
		_iwl_mvm_exit_d0i3(mvm);
	}

	iwl_trans_resume(trans);

	if (iwl_mvm_enter_d0i3_on_suspend(mvm)) {
		int ret = iwl_mvm_exit_d0i3(mvm->hw->priv);

		if (ret)
			return ret;
		/*
		 * d0i3 exit will be deferred until reconfig_complete.
		 * make sure there we are out of d0i3.
		 */
	}
	return 0;
}

int iwl_mvm_resume(struct ieee80211_hw *hw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	if (mvm->trans->system_pm_mode == IWL_PLAT_PM_MODE_D0I3)
		ret = iwl_mvm_resume_d0i3(mvm);
	else
		ret = iwl_mvm_resume_d3(mvm);

	mvm->trans->system_pm_mode = IWL_PLAT_PM_MODE_DISABLED;

	iwl_mvm_resume_tcm(mvm);

	iwl_fw_runtime_resume(&mvm->fwrt);

	return ret;
}

void iwl_mvm_set_wakeup(struct ieee80211_hw *hw, bool enabled)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	device_set_wakeup_enable(mvm->trans->dev, enabled);
}

#ifdef CONFIG_IWLWIFI_DEBUGFS
static int iwl_mvm_d3_test_open(struct inode *inode, struct file *file)
{
	struct iwl_mvm *mvm = inode->i_private;
	int err;

	if (mvm->d3_test_active)
		return -EBUSY;

	file->private_data = inode->i_private;

	synchronize_net();

	mvm->trans->system_pm_mode = IWL_PLAT_PM_MODE_D3;

	iwl_mvm_pause_tcm(mvm, true);

	iwl_fw_runtime_suspend(&mvm->fwrt);

	/* start pseudo D3 */
	rtnl_lock();
	err = __iwl_mvm_suspend(mvm->hw, mvm->hw->wiphy->wowlan_config, true);
	rtnl_unlock();
	if (err > 0)
		err = -EINVAL;
	if (err)
		return err;

	mvm->d3_test_active = true;
	mvm->keep_vif = NULL;
	return 0;
}

static ssize_t iwl_mvm_d3_test_read(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	u32 pme_asserted;

	while (true) {
		/* read pme_ptr if available */
		if (mvm->d3_test_pme_ptr) {
			pme_asserted = iwl_trans_read_mem32(mvm->trans,
						mvm->d3_test_pme_ptr);
			if (pme_asserted)
				break;
		}

		if (msleep_interruptible(100))
			break;
	}

	return 0;
}

static void iwl_mvm_d3_test_disconn_work_iter(void *_data, u8 *mac,
					      struct ieee80211_vif *vif)
{
	/* skip the one we keep connection on */
	if (_data == vif)
		return;

	if (vif->type == NL80211_IFTYPE_STATION)
		ieee80211_connection_loss(vif);
}

static int iwl_mvm_d3_test_release(struct inode *inode, struct file *file)
{
	struct iwl_mvm *mvm = inode->i_private;
	bool unified_image = fw_has_capa(&mvm->fw->ucode_capa,
					 IWL_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG);

	mvm->d3_test_active = false;

	iwl_fw_dbg_read_d3_debug_data(&mvm->fwrt);

	rtnl_lock();
	__iwl_mvm_resume(mvm, true);
	rtnl_unlock();

	iwl_mvm_resume_tcm(mvm);

	iwl_fw_runtime_resume(&mvm->fwrt);

	mvm->trans->system_pm_mode = IWL_PLAT_PM_MODE_DISABLED;

	iwl_abort_notification_waits(&mvm->notif_wait);
	if (!unified_image) {
		int remaining_time = 10;

		ieee80211_restart_hw(mvm->hw);

		/* wait for restart and disconnect all interfaces */
		while (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) &&
		       remaining_time > 0) {
			remaining_time--;
			msleep(1000);
		}

		if (remaining_time == 0)
			IWL_ERR(mvm, "Timed out waiting for HW restart!\n");
	}

	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_d3_test_disconn_work_iter, mvm->keep_vif);

	return 0;
}

const struct file_operations iwl_dbgfs_d3_test_ops = {
	.llseek = no_llseek,
	.open = iwl_mvm_d3_test_open,
	.read = iwl_mvm_d3_test_read,
	.release = iwl_mvm_d3_test_release,
};
#endif
