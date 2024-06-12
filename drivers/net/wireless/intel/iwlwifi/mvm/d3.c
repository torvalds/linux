// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2024 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
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
#include "fw/img.h"

void iwl_mvm_set_rekey_data(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct cfg80211_gtk_rekey_data *data)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	mutex_lock(&mvm->mutex);

	mvmvif->rekey_data.kek_len = data->kek_len;
	mvmvif->rekey_data.kck_len = data->kck_len;
	memcpy(mvmvif->rekey_data.kek, data->kek, data->kek_len);
	memcpy(mvmvif->rekey_data.kck, data->kck, data->kck_len);
	mvmvif->rekey_data.akm = data->akm & 0xFF;
	mvmvif->rekey_data.replay_ctr =
		cpu_to_le64(be64_to_cpup((const __be64 *)data->replay_ctr));
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

struct wowlan_key_reprogram_data {
	bool error;
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
	struct wowlan_key_reprogram_data *data = _data;
	int ret;

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

		mutex_lock(&mvm->mutex);
		ret = iwl_mvm_send_cmd_pdu(mvm, WEP_KEY, 0, sizeof(wkc), &wkc);
		data->error = ret != 0;

		mvm->ptk_ivlen = key->iv_len;
		mvm->ptk_icvlen = key->icv_len;
		mvm->gtk_ivlen = key->iv_len;
		mvm->gtk_icvlen = key->icv_len;
		mutex_unlock(&mvm->mutex);

		/* don't upload key again */
		return;
	}
	default:
		data->error = true;
		return;
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
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
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		break;
	}

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

struct wowlan_key_rsc_tsc_data {
	struct iwl_wowlan_rsc_tsc_params_cmd_v4 *rsc_tsc;
	bool have_rsc_tsc;
};

static void iwl_mvm_wowlan_get_rsc_tsc_data(struct ieee80211_hw *hw,
					    struct ieee80211_vif *vif,
					    struct ieee80211_sta *sta,
					    struct ieee80211_key_conf *key,
					    void *_data)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct wowlan_key_rsc_tsc_data *data = _data;
	struct aes_sc *aes_sc;
	struct tkip_sc *tkip_sc, *tkip_tx_sc = NULL;
	struct ieee80211_key_seq seq;
	int i;

	switch (key->cipher) {
	default:
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		if (sta) {
			u64 pn64;

			tkip_sc =
			   data->rsc_tsc->params.all_tsc_rsc.tkip.unicast_rsc;
			tkip_tx_sc =
				&data->rsc_tsc->params.all_tsc_rsc.tkip.tsc;

			pn64 = atomic64_read(&key->tx_pn);
			tkip_tx_sc->iv16 = cpu_to_le16(TKIP_PN_TO_IV16(pn64));
			tkip_tx_sc->iv32 = cpu_to_le32(TKIP_PN_TO_IV32(pn64));
		} else {
			tkip_sc =
			  data->rsc_tsc->params.all_tsc_rsc.tkip.multicast_rsc;
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
		}

		data->have_rsc_tsc = true;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		if (sta) {
			struct aes_sc *aes_tx_sc;
			u64 pn64;

			aes_sc =
			   data->rsc_tsc->params.all_tsc_rsc.aes.unicast_rsc;
			aes_tx_sc =
				&data->rsc_tsc->params.all_tsc_rsc.aes.tsc;

			pn64 = atomic64_read(&key->tx_pn);
			aes_tx_sc->pn = cpu_to_le64(pn64);
		} else {
			aes_sc =
			   data->rsc_tsc->params.all_tsc_rsc.aes.multicast_rsc;
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
			rcu_read_lock();
			ptk_pn = rcu_dereference(mvmsta->ptk_pn[key->keyidx]);
			if (WARN_ON(!ptk_pn)) {
				rcu_read_unlock();
				break;
			}

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

			rcu_read_unlock();
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
		data->have_rsc_tsc = true;
		break;
	}
}

struct wowlan_key_rsc_v5_data {
	struct iwl_wowlan_rsc_tsc_params_cmd *rsc;
	bool have_rsc;
	int gtks;
	int gtk_ids[4];
};

static void iwl_mvm_wowlan_get_rsc_v5_data(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   struct ieee80211_sta *sta,
					   struct ieee80211_key_conf *key,
					   void *_data)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct wowlan_key_rsc_v5_data *data = _data;
	struct ieee80211_key_seq seq;
	__le64 *rsc;
	int i;

	/* only for ciphers that can be PTK/GTK */
	switch (key->cipher) {
	default:
		return;
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		break;
	}

	if (sta) {
		rsc = data->rsc->ucast_rsc;
	} else {
		if (WARN_ON(data->gtks >= ARRAY_SIZE(data->gtk_ids)))
			return;
		data->gtk_ids[data->gtks] = key->keyidx;
		rsc = data->rsc->mcast_rsc[data->gtks % 2];
		if (WARN_ON(key->keyidx >=
				ARRAY_SIZE(data->rsc->mcast_key_id_map)))
			return;
		data->rsc->mcast_key_id_map[key->keyidx] = data->gtks % 2;
		if (data->gtks >= 2) {
			int prev = data->gtks - 2;
			int prev_idx = data->gtk_ids[prev];

			data->rsc->mcast_key_id_map[prev_idx] =
				IWL_MCAST_KEY_MAP_INVALID;
		}
		data->gtks++;
	}

	switch (key->cipher) {
	default:
		WARN_ON(1);
		break;
	case WLAN_CIPHER_SUITE_TKIP:

		/*
		 * For non-QoS this relies on the fact that both the uCode and
		 * mac80211 use TID 0 (as they need to to avoid replay attacks)
		 * for checking the IV in the frames.
		 */
		for (i = 0; i < IWL_MAX_TID_COUNT; i++) {
			ieee80211_get_key_rx_seq(key, i, &seq);

			rsc[i] = cpu_to_le64(((u64)seq.tkip.iv32 << 16) |
					     seq.tkip.iv16);
		}

		data->have_rsc = true;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		/*
		 * For non-QoS this relies on the fact that both the uCode and
		 * mac80211/our RX code use TID 0 for checking the PN.
		 */
		if (sta) {
			struct iwl_mvm_sta *mvmsta;
			struct iwl_mvm_key_pn *ptk_pn;
			const u8 *pn;

			mvmsta = iwl_mvm_sta_from_mac80211(sta);
			rcu_read_lock();
			ptk_pn = rcu_dereference(mvmsta->ptk_pn[key->keyidx]);
			if (WARN_ON(!ptk_pn)) {
				rcu_read_unlock();
				break;
			}

			for (i = 0; i < IWL_MAX_TID_COUNT; i++) {
				pn = iwl_mvm_find_max_pn(key, ptk_pn, &seq, i,
						mvm->trans->num_rx_queues);
				rsc[i] = cpu_to_le64((u64)pn[5] |
						     ((u64)pn[4] << 8) |
						     ((u64)pn[3] << 16) |
						     ((u64)pn[2] << 24) |
						     ((u64)pn[1] << 32) |
						     ((u64)pn[0] << 40));
			}

			rcu_read_unlock();
		} else {
			for (i = 0; i < IWL_MAX_TID_COUNT; i++) {
				u8 *pn = seq.ccmp.pn;

				ieee80211_get_key_rx_seq(key, i, &seq);
				rsc[i] = cpu_to_le64((u64)pn[5] |
						     ((u64)pn[4] << 8) |
						     ((u64)pn[3] << 16) |
						     ((u64)pn[2] << 24) |
						     ((u64)pn[1] << 32) |
						     ((u64)pn[0] << 40));
			}
		}
		data->have_rsc = true;
		break;
	}
}

static int iwl_mvm_wowlan_config_rsc_tsc(struct iwl_mvm *mvm,
					 struct ieee80211_vif *vif,
					 struct iwl_mvm_vif_link_info *mvm_link)
{
	int ver = iwl_fw_lookup_cmd_ver(mvm->fw, WOWLAN_TSC_RSC_PARAM,
					IWL_FW_CMD_VER_UNKNOWN);
	int ret;

	if (ver == 5) {
		struct wowlan_key_rsc_v5_data data = {};
		int i;

		data.rsc = kzalloc(sizeof(*data.rsc), GFP_KERNEL);
		if (!data.rsc)
			return -ENOMEM;

		for (i = 0; i < ARRAY_SIZE(data.rsc->mcast_key_id_map); i++)
			data.rsc->mcast_key_id_map[i] =
				IWL_MCAST_KEY_MAP_INVALID;
		data.rsc->sta_id = cpu_to_le32(mvm_link->ap_sta_id);

		ieee80211_iter_keys(mvm->hw, vif,
				    iwl_mvm_wowlan_get_rsc_v5_data,
				    &data);

		if (data.have_rsc)
			ret = iwl_mvm_send_cmd_pdu(mvm, WOWLAN_TSC_RSC_PARAM,
						   CMD_ASYNC, sizeof(*data.rsc),
						   data.rsc);
		else
			ret = 0;
		kfree(data.rsc);
	} else if (ver == 4 || ver == 2 || ver == IWL_FW_CMD_VER_UNKNOWN) {
		struct wowlan_key_rsc_tsc_data data = {};
		int size;

		data.rsc_tsc = kzalloc(sizeof(*data.rsc_tsc), GFP_KERNEL);
		if (!data.rsc_tsc)
			return -ENOMEM;

		if (ver == 4) {
			size = sizeof(*data.rsc_tsc);
			data.rsc_tsc->sta_id =
				cpu_to_le32(mvm_link->ap_sta_id);
		} else {
			/* ver == 2 || ver == IWL_FW_CMD_VER_UNKNOWN */
			size = sizeof(data.rsc_tsc->params);
		}

		ieee80211_iter_keys(mvm->hw, vif,
				    iwl_mvm_wowlan_get_rsc_tsc_data,
				    &data);

		if (data.have_rsc_tsc)
			ret = iwl_mvm_send_cmd_pdu(mvm, WOWLAN_TSC_RSC_PARAM,
						   CMD_ASYNC, size,
						   data.rsc_tsc);
		else
			ret = 0;
		kfree(data.rsc_tsc);
	} else {
		ret = 0;
		WARN_ON_ONCE(1);
	}

	return ret;
}

struct wowlan_key_tkip_data {
	struct iwl_wowlan_tkip_params_cmd tkip;
	bool have_tkip_keys;
};

static void iwl_mvm_wowlan_get_tkip_data(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_sta *sta,
					 struct ieee80211_key_conf *key,
					 void *_data)
{
	struct wowlan_key_tkip_data *data = _data;
	struct iwl_p1k_cache *rx_p1ks;
	u8 *rx_mic_key;
	struct ieee80211_key_seq seq;
	u32 cur_rx_iv32 = 0;
	u16 p1k[IWL_P1K_SIZE];
	int i;

	switch (key->cipher) {
	default:
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		if (sta) {
			u64 pn64;

			rx_p1ks = data->tkip.rx_uni;

			pn64 = atomic64_read(&key->tx_pn);

			ieee80211_get_tkip_p1k_iv(key, TKIP_PN_TO_IV32(pn64),
						  p1k);
			iwl_mvm_convert_p1k(p1k, data->tkip.tx.p1k);

			memcpy(data->tkip.mic_keys.tx,
			       &key->key[NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY],
			       IWL_MIC_KEY_SIZE);

			rx_mic_key = data->tkip.mic_keys.rx_unicast;
		} else {
			rx_p1ks = data->tkip.rx_multi;
			rx_mic_key = data->tkip.mic_keys.rx_mcast;
		}

		for (i = 0; i < IWL_NUM_RSC; i++) {
			ieee80211_get_key_rx_seq(key, i, &seq);
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

		data->have_tkip_keys = true;
		break;
	}
}

struct wowlan_key_gtk_type_iter {
	struct iwl_wowlan_kek_kck_material_cmd_v4 *kek_kck_cmd;
};

static void iwl_mvm_wowlan_gtk_type_iter(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_sta *sta,
					 struct ieee80211_key_conf *key,
					 void *_data)
{
	struct wowlan_key_gtk_type_iter *data = _data;

	switch (key->cipher) {
	default:
		return;
	case WLAN_CIPHER_SUITE_TKIP:
		if (!sta)
			data->kek_kck_cmd->gtk_cipher =
				cpu_to_le32(STA_KEY_FLG_TKIP);
		return;
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
		data->kek_kck_cmd->igtk_cipher = cpu_to_le32(STA_KEY_FLG_GCMP);
		return;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		data->kek_kck_cmd->igtk_cipher = cpu_to_le32(STA_KEY_FLG_CCM);
		return;
	case WLAN_CIPHER_SUITE_CCMP:
		if (!sta)
			data->kek_kck_cmd->gtk_cipher =
				cpu_to_le32(STA_KEY_FLG_CCM);
		return;
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		if (!sta)
			data->kek_kck_cmd->gtk_cipher =
				cpu_to_le32(STA_KEY_FLG_GCMP);
		return;
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

	cmd.len[0] = struct_size(pattern_cmd, patterns, wowlan->n_patterns);

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
				 struct iwl_mvm_vif_link_info *mvm_link,
				 struct cfg80211_wowlan *wowlan)
{
	struct iwl_wowlan_patterns_cmd *pattern_cmd;
	struct iwl_host_cmd cmd = {
		.id = WOWLAN_PATTERNS,
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
	};
	int i, err;
	int ver = iwl_fw_lookup_cmd_ver(mvm->fw, cmd.id,
					IWL_FW_CMD_VER_UNKNOWN);

	if (!wowlan->n_patterns)
		return 0;

	cmd.len[0] = sizeof(*pattern_cmd) +
		wowlan->n_patterns * sizeof(struct iwl_wowlan_pattern_v2);

	pattern_cmd = kzalloc(cmd.len[0], GFP_KERNEL);
	if (!pattern_cmd)
		return -ENOMEM;

	pattern_cmd->n_patterns = wowlan->n_patterns;
	if (ver >= 3)
		pattern_cmd->sta_id = mvm_link->ap_sta_id;

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
	struct cfg80211_chan_def chandef, ap_def;
	int ret, i;
	struct iwl_binding_cmd_v1 binding_cmd = {};
	struct iwl_time_quota_cmd quota_cmd = {};
	struct iwl_time_quota_data *quota;
	u32 status;

	if (WARN_ON_ONCE(iwl_mvm_is_cdb_supported(mvm) ||
			 ieee80211_vif_is_mld(vif)))
		return -EINVAL;

	/* add back the PHY */
	if (WARN_ON(!mvmvif->deflink.phy_ctxt))
		return -EINVAL;

	rcu_read_lock();
	ctx = rcu_dereference(vif->bss_conf.chanctx_conf);
	if (WARN_ON(!ctx)) {
		rcu_read_unlock();
		return -EINVAL;
	}
	chandef = ctx->def;
	ap_def = ctx->ap;
	chains_static = ctx->rx_chains_static;
	chains_dynamic = ctx->rx_chains_dynamic;
	rcu_read_unlock();

	ret = iwl_mvm_phy_ctxt_add(mvm, mvmvif->deflink.phy_ctxt, &chandef,
				   &ap_def, chains_static, chains_dynamic);
	if (ret)
		return ret;

	/* add back the MAC */
	mvmvif->uploaded = false;

	if (WARN_ON(!vif->cfg.assoc))
		return -EINVAL;

	ret = iwl_mvm_mac_ctxt_add(mvm, vif);
	if (ret)
		return ret;

	/* add back binding - XXX refactor? */
	binding_cmd.id_and_color =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->deflink.phy_ctxt->id,
						mvmvif->deflink.phy_ctxt->color));
	binding_cmd.action = cpu_to_le32(FW_CTXT_ACTION_ADD);
	binding_cmd.phy =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->deflink.phy_ctxt->id,
						mvmvif->deflink.phy_ctxt->color));
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
	rcu_assign_pointer(mvm->fw_id_to_mac_id[mvmvif->deflink.ap_sta_id],
			   ap_sta);

	ret = iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
	if (ret)
		return ret;

	/* and some quota */
	quota = iwl_mvm_quota_cmd_get_quota(mvm, &quota_cmd, 0);
	quota->id_and_color =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->deflink.phy_ctxt->id,
						mvmvif->deflink.phy_ctxt->color));
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

	if (iwl_mvm_is_lar_supported(mvm) && iwl_mvm_init_fw_regd(mvm, false))
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
	struct iwl_mvm_sta *mvm_ap_sta = iwl_mvm_sta_from_mac80211(ap_sta);

	/* TODO: wowlan_config_cmd->wowlan_ba_teardown_tids */

	wowlan_config_cmd->is_11n_connection =
					ap_sta->deflink.ht_cap.ht_supported;
	wowlan_config_cmd->flags = ENABLE_L3_FILTERING |
		ENABLE_NBNS_FILTERING | ENABLE_DHCP_FILTERING;

	if (ap_sta->mfp)
		wowlan_config_cmd->flags |= IS_11W_ASSOC;

	if (iwl_fw_lookup_cmd_ver(mvm->fw, WOWLAN_CONFIGURATION, 0) < 6) {
		/* Query the last used seqno and set it */
		int ret = iwl_mvm_get_last_nonqos_seq(mvm, vif);

		if (ret < 0)
			return ret;

		wowlan_config_cmd->non_qos_seq = cpu_to_le16(ret);
	}

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

static int iwl_mvm_wowlan_config_key_params(struct iwl_mvm *mvm,
					    struct ieee80211_vif *vif,
					    struct iwl_mvm_vif_link_info *mvm_link)
{
	bool unified = fw_has_capa(&mvm->fw->ucode_capa,
				   IWL_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG);
	struct wowlan_key_reprogram_data key_data = {};
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;
	u8 cmd_ver;
	size_t cmd_size;

	if (!unified) {
		/*
		 * if we have to configure keys, call ieee80211_iter_keys(),
		 * as we need non-atomic context in order to take the
		 * required locks.
		 */
		/*
		 * Note that currently we don't use CMD_ASYNC in the iterator.
		 * In case of key_data.configure_keys, all the configured
		 * commands are SYNC, and iwl_mvm_wowlan_program_keys() will
		 * take care of locking/unlocking mvm->mutex.
		 */
		ieee80211_iter_keys(mvm->hw, vif, iwl_mvm_wowlan_program_keys,
				    &key_data);

		if (key_data.error)
			return -EIO;
	}

	ret = iwl_mvm_wowlan_config_rsc_tsc(mvm, vif, mvm_link);
	if (ret)
		return ret;

	if (!fw_has_api(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_API_TKIP_MIC_KEYS)) {
		int ver = iwl_fw_lookup_cmd_ver(mvm->fw, WOWLAN_TKIP_PARAM,
						IWL_FW_CMD_VER_UNKNOWN);
		struct wowlan_key_tkip_data tkip_data = {};
		int size;

		if (ver == 2) {
			size = sizeof(tkip_data.tkip);
			tkip_data.tkip.sta_id =
				cpu_to_le32(mvm_link->ap_sta_id);
		} else if (ver == 1 || ver == IWL_FW_CMD_VER_UNKNOWN) {
			size = sizeof(struct iwl_wowlan_tkip_params_cmd_ver_1);
		} else {
			WARN_ON_ONCE(1);
			return -EINVAL;
		}

		ieee80211_iter_keys(mvm->hw, vif, iwl_mvm_wowlan_get_tkip_data,
				    &tkip_data);

		if (tkip_data.have_tkip_keys) {
			/* send relevant data according to CMD version */
			ret = iwl_mvm_send_cmd_pdu(mvm,
						   WOWLAN_TKIP_PARAM,
						   CMD_ASYNC, size,
						   &tkip_data.tkip);
			if (ret)
				return ret;
		}
	}

	/* configure rekey data only if offloaded rekey is supported (d3) */
	if (mvmvif->rekey_data.valid) {
		struct iwl_wowlan_kek_kck_material_cmd_v4 kek_kck_cmd = {};
		struct iwl_wowlan_kek_kck_material_cmd_v4 *_kek_kck_cmd =
			&kek_kck_cmd;
		struct wowlan_key_gtk_type_iter gtk_type_data = {
			.kek_kck_cmd = _kek_kck_cmd,
		};

		cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw,
						WOWLAN_KEK_KCK_MATERIAL,
						IWL_FW_CMD_VER_UNKNOWN);
		if (WARN_ON(cmd_ver != 2 && cmd_ver != 3 && cmd_ver != 4 &&
			    cmd_ver != IWL_FW_CMD_VER_UNKNOWN))
			return -EINVAL;

		ieee80211_iter_keys(mvm->hw, vif, iwl_mvm_wowlan_gtk_type_iter,
				    &gtk_type_data);

		memcpy(kek_kck_cmd.kck, mvmvif->rekey_data.kck,
		       mvmvif->rekey_data.kck_len);
		kek_kck_cmd.kck_len = cpu_to_le16(mvmvif->rekey_data.kck_len);
		memcpy(kek_kck_cmd.kek, mvmvif->rekey_data.kek,
		       mvmvif->rekey_data.kek_len);
		kek_kck_cmd.kek_len = cpu_to_le16(mvmvif->rekey_data.kek_len);
		kek_kck_cmd.replay_ctr = mvmvif->rekey_data.replay_ctr;
		kek_kck_cmd.akm = cpu_to_le32(mvmvif->rekey_data.akm);
		kek_kck_cmd.sta_id = cpu_to_le32(mvm_link->ap_sta_id);

		if (cmd_ver == 4) {
			cmd_size = sizeof(struct iwl_wowlan_kek_kck_material_cmd_v4);
		} else {
			if (cmd_ver == 3)
				cmd_size =
					sizeof(struct iwl_wowlan_kek_kck_material_cmd_v3);
			else
				cmd_size =
					sizeof(struct iwl_wowlan_kek_kck_material_cmd_v2);
			/* skip the sta_id at the beginning */
			_kek_kck_cmd = (void *)
				((u8 *)_kek_kck_cmd + sizeof(kek_kck_cmd.sta_id));
		}

		IWL_DEBUG_WOWLAN(mvm, "setting akm %d\n",
				 mvmvif->rekey_data.akm);

		ret = iwl_mvm_send_cmd_pdu(mvm, WOWLAN_KEK_KCK_MATERIAL,
					   CMD_ASYNC, cmd_size, _kek_kck_cmd);
		if (ret)
			return ret;
	}

	return 0;
}

static int
iwl_mvm_wowlan_config(struct iwl_mvm *mvm,
		      struct cfg80211_wowlan *wowlan,
		      struct iwl_wowlan_config_cmd *wowlan_config_cmd,
		      struct ieee80211_vif *vif, struct iwl_mvm_vif *mvmvif,
		      struct iwl_mvm_vif_link_info *mvm_link,
		      struct ieee80211_sta *ap_sta)
{
	int ret;
	bool unified_image = fw_has_capa(&mvm->fw->ucode_capa,
					 IWL_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG);

	mvm->offload_tid = wowlan_config_cmd->offloading_tid;

	if (!unified_image) {
		ret = iwl_mvm_switch_to_d3(mvm);
		if (ret)
			return ret;

		ret = iwl_mvm_d3_reprogram(mvm, vif, ap_sta);
		if (ret)
			return ret;
	}

	ret = iwl_mvm_wowlan_config_key_params(mvm, vif, mvm_link);
	if (ret)
		return ret;

	ret = iwl_mvm_send_cmd_pdu(mvm, WOWLAN_CONFIGURATION, 0,
				   sizeof(*wowlan_config_cmd),
				   wowlan_config_cmd);
	if (ret)
		return ret;

	if (fw_has_api(&mvm->fw->ucode_capa,
		       IWL_UCODE_TLV_API_WOWLAN_TCP_SYN_WAKE))
		ret = iwl_mvm_send_patterns(mvm, mvm_link, wowlan);
	else
		ret = iwl_mvm_send_patterns_v1(mvm, wowlan);
	if (ret)
		return ret;

	return iwl_mvm_send_proto_offload(mvm, vif, false, true, 0,
					  mvm_link->ap_sta_id);
}

static int
iwl_mvm_netdetect_config(struct iwl_mvm *mvm,
			 struct cfg80211_wowlan *wowlan,
			 struct cfg80211_sched_scan_request *nd_config,
			 struct ieee80211_vif *vif)
{
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
	struct iwl_mvm_vif_link_info *mvm_link;
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
		.flags = CMD_WANT_SKB | CMD_SEND_IN_D3,
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

	vif = iwl_mvm_get_bss_vif(mvm);
	if (IS_ERR_OR_NULL(vif))
		return 1;

	ret = iwl_mvm_block_esr_sync(mvm, vif, IWL_MVM_ESR_BLOCKED_WOWLAN);
	if (ret)
		return ret;

	mutex_lock(&mvm->mutex);

	set_bit(IWL_MVM_STATUS_IN_D3, &mvm->status);

	synchronize_net();

	mvmvif = iwl_mvm_vif_from_mac80211(vif);

	mvm_link = mvmvif->link[iwl_mvm_get_primary_link(vif)];
	if (WARN_ON_ONCE(!mvm_link)) {
		ret = -EINVAL;
		goto out_noreset;
	}

	if (mvm_link->ap_sta_id == IWL_MVM_INVALID_STA) {
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
		struct iwl_wowlan_config_cmd wowlan_config_cmd = {
			.offloading_tid = 0,
		};

		wowlan_config_cmd.sta_id = mvm_link->ap_sta_id;

		ap_sta = rcu_dereference_protected(
			mvm->fw_id_to_mac_id[mvm_link->ap_sta_id],
			lockdep_is_held(&mvm->mutex));
		if (IS_ERR_OR_NULL(ap_sta)) {
			ret = -EINVAL;
			goto out_noreset;
		}

		ret = iwl_mvm_sta_ensure_queue(
			mvm, ap_sta->txq[wowlan_config_cmd.offloading_tid]);
		if (ret)
			goto out_noreset;

		ret = iwl_mvm_get_wowlan_config(mvm, wowlan, &wowlan_config_cmd,
						vif, mvmvif, ap_sta);
		if (ret)
			goto out_noreset;
		ret = iwl_mvm_wowlan_config(mvm, wowlan, &wowlan_config_cmd,
					    vif, mvmvif, mvm_link, ap_sta);
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
	 * Prior to 9000 device family the driver needs to stop the dbg
	 * recording before entering D3. In later devices the FW stops the
	 * recording automatically.
	 */
	if (mvm->trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_9000)
		iwl_fw_dbg_stop_restart_recording(&mvm->fwrt, NULL, true);

	mvm->trans->system_pm_mode = IWL_PLAT_PM_MODE_D3;

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

	ret = iwl_trans_d3_suspend(mvm->trans, test, !unified_image);
 out:
	if (ret < 0) {
		iwl_mvm_free_nd(mvm);

		if (!unified_image) {
			if (mvm->fw_restart > 0) {
				mvm->fw_restart--;
				ieee80211_restart_hw(mvm->hw);
			}
		}

		clear_bit(IWL_MVM_STATUS_IN_D3, &mvm->status);
	}
 out_noreset:
	mutex_unlock(&mvm->mutex);

	return ret;
}

int iwl_mvm_suspend(struct ieee80211_hw *hw, struct cfg80211_wowlan *wowlan)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	iwl_mvm_pause_tcm(mvm, true);

	iwl_fw_runtime_suspend(&mvm->fwrt);

	return __iwl_mvm_suspend(hw, wowlan, false);
}

struct iwl_multicast_key_data {
	u8 key[WOWLAN_KEY_MAX_SIZE];
	u8 len;
	u8 flags;
	u8 id;
	u8 ipn[6];
};

/* converted data from the different status responses */
struct iwl_wowlan_status_data {
	u64 replay_ctr;
	u32 num_of_gtk_rekeys;
	u32 received_beacons;
	u32 wakeup_reasons;
	u32 wake_packet_length;
	u32 wake_packet_bufsize;
	u16 pattern_number;
	u16 non_qos_seq_ctr;
	u16 qos_seq_ctr[8];
	u8 tid_tear_down;

	struct {
		/* including RX MIC key for TKIP */
		u8 key[WOWLAN_KEY_MAX_SIZE];
		u8 len;
		u8 flags;
		u8 id;
	} gtk[WOWLAN_GTK_KEYS_NUM];

	struct {
		/*
		 * We store both the TKIP and AES representations
		 * coming from the firmware because we decode the
		 * data from there before we iterate the keys and
		 * know which one we need.
		 */
		struct {
			struct ieee80211_key_seq seq[IWL_MAX_TID_COUNT];
		} tkip, aes;

		/*
		 * We use -1 for when we have valid data but don't know
		 * the key ID from firmware, and thus it needs to be
		 * installed with the last key (depending on rekeying).
		 */
		s8 key_id;
		bool valid;
	} gtk_seq[2];

	struct {
		/* Same as above */
		struct {
			struct ieee80211_key_seq seq[IWL_MAX_TID_COUNT];
			u64 tx_pn;
		} tkip, aes;
	} ptk;

	struct iwl_multicast_key_data igtk;
	struct iwl_multicast_key_data bigtk[WOWLAN_BIGTK_KEYS_NUM];

	int num_mlo_keys;
	struct iwl_wowlan_mlo_gtk mlo_keys[WOWLAN_MAX_MLO_KEYS];

	u8 *wake_packet;
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
		       IWL_WOWLAN_WAKEUP_BY_DISCONNECTION_ON_DEAUTH |
		       IWL_WOWLAN_WAKEUP_BY_GTK_REKEY_FAILURE))
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

	if (reasons & IWL_WAKEUP_BY_11W_UNPROTECTED_DEAUTH_OR_DISASSOC)
		wakeup.unprot_deauth_disassoc = true;

	if (status->wake_packet) {
		int pktsize = status->wake_packet_bufsize;
		int pktlen = status->wake_packet_length;
		const u8 *pktdata = status->wake_packet;
		const struct ieee80211_hdr *hdr = (const void *)pktdata;
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

static void iwl_mvm_le64_to_aes_seq(__le64 le_pn, struct ieee80211_key_seq *seq)
{
	u64 pn = le64_to_cpu(le_pn);

	seq->ccmp.pn[0] = pn >> 40;
	seq->ccmp.pn[1] = pn >> 32;
	seq->ccmp.pn[2] = pn >> 24;
	seq->ccmp.pn[3] = pn >> 16;
	seq->ccmp.pn[4] = pn >> 8;
	seq->ccmp.pn[5] = pn;
}

static void iwl_mvm_aes_sc_to_seq(struct aes_sc *sc,
				  struct ieee80211_key_seq *seq)
{
	iwl_mvm_le64_to_aes_seq(sc->pn, seq);
}

static void iwl_mvm_le64_to_tkip_seq(__le64 le_pn, struct ieee80211_key_seq *seq)
{
	u64 pn = le64_to_cpu(le_pn);

	seq->tkip.iv16 = (u16)pn;
	seq->tkip.iv32 = (u32)(pn >> 16);
}

static void iwl_mvm_tkip_sc_to_seq(struct tkip_sc *sc,
				   struct ieee80211_key_seq *seq)
{
	seq->tkip.iv32 = le32_to_cpu(sc->iv32);
	seq->tkip.iv16 = le16_to_cpu(sc->iv16);
}

static void iwl_mvm_set_key_rx_seq_tids(struct ieee80211_key_conf *key,
					struct ieee80211_key_seq *seq)
{
	int tid;

	for (tid = 0; tid < IWL_MAX_TID_COUNT; tid++)
		ieee80211_set_key_rx_seq(key, tid, &seq[tid]);
}

static void iwl_mvm_set_aes_ptk_rx_seq(struct iwl_mvm *mvm,
				       struct iwl_wowlan_status_data *status,
				       struct ieee80211_sta *sta,
				       struct ieee80211_key_conf *key)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_key_pn *ptk_pn;
	int tid;

	iwl_mvm_set_key_rx_seq_tids(key, status->ptk.aes.seq);

	if (!iwl_mvm_has_new_rx_api(mvm))
		return;


	rcu_read_lock();
	ptk_pn = rcu_dereference(mvmsta->ptk_pn[key->keyidx]);
	if (WARN_ON(!ptk_pn)) {
		rcu_read_unlock();
		return;
	}

	for (tid = 0; tid < IWL_MAX_TID_COUNT; tid++) {
		int i;

		for (i = 1; i < mvm->trans->num_rx_queues; i++)
			memcpy(ptk_pn->q[i].pn[tid],
			       status->ptk.aes.seq[tid].ccmp.pn,
			       IEEE80211_CCMP_PN_LEN);
	}
	rcu_read_unlock();
}

static void iwl_mvm_convert_key_counters(struct iwl_wowlan_status_data *status,
					 union iwl_all_tsc_rsc *sc)
{
	int i;

	BUILD_BUG_ON(IWL_MAX_TID_COUNT > IWL_MAX_TID_COUNT);
	BUILD_BUG_ON(IWL_MAX_TID_COUNT > IWL_NUM_RSC);

	/* GTK RX counters */
	for (i = 0; i < IWL_MAX_TID_COUNT; i++) {
		iwl_mvm_tkip_sc_to_seq(&sc->tkip.multicast_rsc[i],
				       &status->gtk_seq[0].tkip.seq[i]);
		iwl_mvm_aes_sc_to_seq(&sc->aes.multicast_rsc[i],
				      &status->gtk_seq[0].aes.seq[i]);
	}
	status->gtk_seq[0].valid = true;
	status->gtk_seq[0].key_id = -1;

	/* PTK TX counter */
	status->ptk.tkip.tx_pn = (u64)le16_to_cpu(sc->tkip.tsc.iv16) |
				 ((u64)le32_to_cpu(sc->tkip.tsc.iv32) << 16);
	status->ptk.aes.tx_pn = le64_to_cpu(sc->aes.tsc.pn);

	/* PTK RX counters */
	for (i = 0; i < IWL_MAX_TID_COUNT; i++) {
		iwl_mvm_tkip_sc_to_seq(&sc->tkip.unicast_rsc[i],
				       &status->ptk.tkip.seq[i]);
		iwl_mvm_aes_sc_to_seq(&sc->aes.unicast_rsc[i],
				      &status->ptk.aes.seq[i]);
	}
}

static void
iwl_mvm_convert_key_counters_v5_gtk_seq(struct iwl_wowlan_status_data *status,
					struct iwl_wowlan_all_rsc_tsc_v5 *sc,
					unsigned int idx, unsigned int key_id)
{
	int tid;

	for (tid = 0; tid < IWL_MAX_TID_COUNT; tid++) {
		iwl_mvm_le64_to_tkip_seq(sc->mcast_rsc[idx][tid],
					 &status->gtk_seq[idx].tkip.seq[tid]);
		iwl_mvm_le64_to_aes_seq(sc->mcast_rsc[idx][tid],
					&status->gtk_seq[idx].aes.seq[tid]);
	}

	status->gtk_seq[idx].valid = true;
	status->gtk_seq[idx].key_id = key_id;
}

static void
iwl_mvm_convert_key_counters_v5(struct iwl_wowlan_status_data *status,
				struct iwl_wowlan_all_rsc_tsc_v5 *sc)
{
	int i, tid;

	BUILD_BUG_ON(IWL_MAX_TID_COUNT > IWL_MAX_TID_COUNT);
	BUILD_BUG_ON(IWL_MAX_TID_COUNT > IWL_NUM_RSC);
	BUILD_BUG_ON(ARRAY_SIZE(sc->mcast_rsc) != ARRAY_SIZE(status->gtk_seq));

	/* GTK RX counters */
	for (i = 0; i < ARRAY_SIZE(sc->mcast_key_id_map); i++) {
		u8 entry = sc->mcast_key_id_map[i];

		if (entry < ARRAY_SIZE(sc->mcast_rsc))
			iwl_mvm_convert_key_counters_v5_gtk_seq(status, sc,
								entry, i);
	}

	/* PTK TX counters not needed, assigned in device */

	/* PTK RX counters */
	for (tid = 0; tid < IWL_MAX_TID_COUNT; tid++) {
		iwl_mvm_le64_to_tkip_seq(sc->ucast_rsc[tid],
					 &status->ptk.tkip.seq[tid]);
		iwl_mvm_le64_to_aes_seq(sc->ucast_rsc[tid],
					&status->ptk.aes.seq[tid]);
	}
}

static void iwl_mvm_set_key_rx_seq_idx(struct ieee80211_key_conf *key,
				       struct iwl_wowlan_status_data *status,
				       int idx)
{
	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		iwl_mvm_set_key_rx_seq_tids(key, status->gtk_seq[idx].aes.seq);
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		iwl_mvm_set_key_rx_seq_tids(key, status->gtk_seq[idx].tkip.seq);
		break;
	default:
		WARN_ON(1);
	}
}

static void iwl_mvm_set_key_rx_seq(struct ieee80211_key_conf *key,
				   struct iwl_wowlan_status_data *status,
				   bool installed)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(status->gtk_seq); i++) {
		if (!status->gtk_seq[i].valid)
			continue;

		/* Handle the case where we know the key ID */
		if (status->gtk_seq[i].key_id == key->keyidx) {
			s8 new_key_id = -1;

			if (status->num_of_gtk_rekeys)
				new_key_id = status->gtk[0].flags &
						IWL_WOWLAN_GTK_IDX_MASK;

			/* Don't install a new key's value to an old key */
			if (new_key_id != key->keyidx)
				iwl_mvm_set_key_rx_seq_idx(key, status, i);
			continue;
		}

		/* handle the case where we didn't, last key only */
		if (status->gtk_seq[i].key_id == -1 &&
		    (!status->num_of_gtk_rekeys || installed))
			iwl_mvm_set_key_rx_seq_idx(key, status, i);
	}
}

struct iwl_mvm_d3_gtk_iter_data {
	struct iwl_mvm *mvm;
	struct iwl_wowlan_status_data *status;
	u32 gtk_cipher, igtk_cipher, bigtk_cipher;
	bool unhandled_cipher, igtk_support, bigtk_support;
	int num_keys;
};

static void iwl_mvm_d3_find_last_keys(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_sta *sta,
				      struct ieee80211_key_conf *key,
				      void *_data)
{
	struct iwl_mvm_d3_gtk_iter_data *data = _data;
	int link_id = vif->active_links ? __ffs(vif->active_links) : -1;

	if (link_id >= 0 && key->link_id >= 0 && link_id != key->link_id)
		return;

	if (data->unhandled_cipher)
		return;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		/* ignore WEP completely, nothing to do */
		return;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
	case WLAN_CIPHER_SUITE_TKIP:
		/* we support these */
		data->gtk_cipher = key->cipher;
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	case WLAN_CIPHER_SUITE_AES_CMAC:
		/* we support these */
		if (data->igtk_support &&
		    (key->keyidx == 4 || key->keyidx == 5)) {
			data->igtk_cipher = key->cipher;
		} else if (data->bigtk_support &&
			   (key->keyidx == 6 || key->keyidx == 7)) {
			data->bigtk_cipher = key->cipher;
		} else {
			data->unhandled_cipher = true;
			return;
		}
		break;
	default:
		/* everything else - disconnect from AP */
		data->unhandled_cipher = true;
		return;
	}

	data->num_keys++;
}

static void
iwl_mvm_d3_set_igtk_bigtk_ipn(const struct iwl_multicast_key_data *key,
			      struct ieee80211_key_seq *seq, u32 cipher)
{
	switch (cipher) {
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		BUILD_BUG_ON(sizeof(seq->aes_gmac.pn) != sizeof(key->ipn));
		memcpy(seq->aes_gmac.pn, key->ipn, sizeof(seq->aes_gmac.pn));
		break;
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	case WLAN_CIPHER_SUITE_AES_CMAC:
		BUILD_BUG_ON(sizeof(seq->aes_cmac.pn) != sizeof(key->ipn));
		memcpy(seq->aes_cmac.pn, key->ipn, sizeof(seq->aes_cmac.pn));
		break;
	default:
		WARN_ON(1);
	}
}

static void
iwl_mvm_d3_update_igtk_bigtk(struct iwl_wowlan_status_data *status,
			     struct ieee80211_key_conf *key,
			     struct iwl_multicast_key_data *key_data)
{
	if (status->num_of_gtk_rekeys && key_data->len) {
		/* remove rekeyed key */
		ieee80211_remove_key(key);
	} else {
		struct ieee80211_key_seq seq;

		iwl_mvm_d3_set_igtk_bigtk_ipn(key_data,
					      &seq,
					      key->cipher);
		ieee80211_set_key_rx_seq(key, 0, &seq);
	}
}

static void iwl_mvm_d3_update_keys(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta,
				   struct ieee80211_key_conf *key,
				   void *_data)
{
	struct iwl_mvm_d3_gtk_iter_data *data = _data;
	struct iwl_wowlan_status_data *status = data->status;
	s8 keyidx;
	int link_id = vif->active_links ? __ffs(vif->active_links) : -1;

	if (link_id >= 0 && key->link_id >= 0 && link_id != key->link_id)
		return;

	if (data->unhandled_cipher)
		return;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		/* ignore WEP completely, nothing to do */
		return;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		if (sta) {
			atomic64_set(&key->tx_pn, status->ptk.aes.tx_pn);
			iwl_mvm_set_aes_ptk_rx_seq(data->mvm, status, sta, key);
			return;
		}
		fallthrough;
	case WLAN_CIPHER_SUITE_TKIP:
		if (sta) {
			atomic64_set(&key->tx_pn, status->ptk.tkip.tx_pn);
			iwl_mvm_set_key_rx_seq_tids(key, status->ptk.tkip.seq);
			return;
		}
		keyidx = key->keyidx;
		/* The current key is always sent by the FW, even if it wasn't
		 * rekeyed during D3.
		 * We remove an existing key if it has the same index as
		 * a new key
		 */
		if (status->num_of_gtk_rekeys &&
		    ((status->gtk[0].len && keyidx == status->gtk[0].id) ||
		     (status->gtk[1].len && keyidx == status->gtk[1].id))) {
			ieee80211_remove_key(key);
		} else {
			iwl_mvm_set_key_rx_seq(key, data->status, false);
		}
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	case WLAN_CIPHER_SUITE_AES_CMAC:
		if (key->keyidx == 4 || key->keyidx == 5) {
			iwl_mvm_d3_update_igtk_bigtk(status, key,
						     &status->igtk);
		}
		if (key->keyidx == 6 || key->keyidx == 7) {
			u8 idx = key->keyidx == status->bigtk[1].id;

			iwl_mvm_d3_update_igtk_bigtk(status, key,
						     &status->bigtk[idx]);
		}
	}
}

struct iwl_mvm_d3_mlo_old_keys {
	u32 cipher[IEEE80211_MLD_MAX_NUM_LINKS][WOWLAN_MLO_GTK_KEY_NUM_TYPES];
	struct ieee80211_key_conf *key[IEEE80211_MLD_MAX_NUM_LINKS][8];
};

static void iwl_mvm_mlo_key_ciphers(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta,
				    struct ieee80211_key_conf *key,
				    void *data)
{
	struct iwl_mvm_d3_mlo_old_keys *old_keys = data;
	enum iwl_wowlan_mlo_gtk_type key_type;

	if (key->link_id < 0)
		return;

	if (WARN_ON(key->link_id >= IEEE80211_MLD_MAX_NUM_LINKS ||
		    key->keyidx >= 8))
		return;

	if (WARN_ON(old_keys->key[key->link_id][key->keyidx]))
		return;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		key_type = WOWLAN_MLO_GTK_KEY_TYPE_GTK;
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	case WLAN_CIPHER_SUITE_AES_CMAC:
		if (key->keyidx == 4 || key->keyidx == 5) {
			key_type = WOWLAN_MLO_GTK_KEY_TYPE_IGTK;
			break;
		} else if (key->keyidx == 6 || key->keyidx == 7) {
			key_type = WOWLAN_MLO_GTK_KEY_TYPE_BIGTK;
			break;
		}
		return;
	default:
		/* ignore WEP/TKIP or unknown ciphers */
		return;
	}

	old_keys->cipher[key->link_id][key_type] = key->cipher;
	old_keys->key[key->link_id][key->keyidx] = key;
}

static bool iwl_mvm_mlo_gtk_rekey(struct iwl_wowlan_status_data *status,
				  struct ieee80211_vif *vif,
				  struct iwl_mvm *mvm)
{
	int i;
	struct iwl_mvm_d3_mlo_old_keys *old_keys;
	bool ret = true;

	IWL_DEBUG_WOWLAN(mvm, "Num of MLO Keys: %d\n", status->num_mlo_keys);
	if (!status->num_mlo_keys)
		return true;

	old_keys = kzalloc(sizeof(*old_keys), GFP_KERNEL);
	if (!old_keys)
		return false;

	/* find the cipher for each mlo key */
	ieee80211_iter_keys(mvm->hw, vif, iwl_mvm_mlo_key_ciphers, old_keys);

	for (i = 0; i < status->num_mlo_keys; i++) {
		struct iwl_wowlan_mlo_gtk *mlo_key = &status->mlo_keys[i];
		struct ieee80211_key_conf *key, *old_key;
		struct ieee80211_key_seq seq;
		struct {
			struct ieee80211_key_conf conf;
			u8 key[32];
		} conf = {};
		u16 flags = le16_to_cpu(mlo_key->flags);
		int j, link_id, key_id, key_type;

		link_id = u16_get_bits(flags, WOWLAN_MLO_GTK_FLAG_LINK_ID_MSK);
		key_id = u16_get_bits(flags, WOWLAN_MLO_GTK_FLAG_KEY_ID_MSK);
		key_type = u16_get_bits(flags,
					WOWLAN_MLO_GTK_FLAG_KEY_TYPE_MSK);

		if (!(vif->valid_links & BIT(link_id)))
			continue;

		if (WARN_ON(link_id >= IEEE80211_MLD_MAX_NUM_LINKS ||
			    key_id >= 8 ||
			    key_type >= WOWLAN_MLO_GTK_KEY_NUM_TYPES))
			continue;

		conf.conf.cipher = old_keys->cipher[link_id][key_type];
		/* WARN_ON? */
		if (!conf.conf.cipher)
			continue;

		conf.conf.keylen = 0;
		switch (conf.conf.cipher) {
		case WLAN_CIPHER_SUITE_CCMP:
		case WLAN_CIPHER_SUITE_GCMP:
			conf.conf.keylen = WLAN_KEY_LEN_CCMP;
			break;
		case WLAN_CIPHER_SUITE_GCMP_256:
			conf.conf.keylen = WLAN_KEY_LEN_GCMP_256;
			break;
		case WLAN_CIPHER_SUITE_BIP_GMAC_128:
			conf.conf.keylen = WLAN_KEY_LEN_BIP_GMAC_128;
			break;
		case WLAN_CIPHER_SUITE_BIP_GMAC_256:
			conf.conf.keylen = WLAN_KEY_LEN_BIP_GMAC_256;
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			conf.conf.keylen = WLAN_KEY_LEN_AES_CMAC;
			break;
		case WLAN_CIPHER_SUITE_BIP_CMAC_256:
			conf.conf.keylen = WLAN_KEY_LEN_BIP_CMAC_256;
			break;
		}

		if (WARN_ON(!conf.conf.keylen ||
			    conf.conf.keylen > sizeof(conf.key)))
			continue;

		memcpy(conf.conf.key, mlo_key->key, conf.conf.keylen);
		conf.conf.keyidx = key_id;

		old_key = old_keys->key[link_id][key_id];
		if (old_key) {
			IWL_DEBUG_WOWLAN(mvm,
					 "Remove MLO key id %d, link id %d\n",
					 key_id, link_id);
			ieee80211_remove_key(old_key);
		}

		IWL_DEBUG_WOWLAN(mvm, "Add MLO key id %d, link id %d\n",
				 key_id, link_id);
		key = ieee80211_gtk_rekey_add(vif, &conf.conf, link_id);
		if (WARN_ON(IS_ERR(key))) {
			ret = false;
			goto out;
		}

		/*
		 * mac80211 expects the pn in big-endian
		 * also note that seq is a union of all cipher types
		 * (ccmp, gcmp, cmac, gmac), and they all have the same
		 * pn field (of length 6) so just copy it to ccmp.pn.
		 */
		for (j = 5; j >= 0; j--)
			seq.ccmp.pn[5 - j] = mlo_key->pn[j];

		/* group keys are non-QoS and use TID 0 */
		ieee80211_set_key_rx_seq(key, 0, &seq);
	}

out:
	kfree(old_keys);
	return ret;
}

static bool iwl_mvm_gtk_rekey(struct iwl_wowlan_status_data *status,
			      struct ieee80211_vif *vif,
			      struct iwl_mvm *mvm, u32 gtk_cipher)
{
	int i, j;
	struct ieee80211_key_conf *key;
	struct {
		struct ieee80211_key_conf conf;
		u8 key[32];
	} conf = {
		.conf.cipher = gtk_cipher,
	};
	int link_id = vif->active_links ? __ffs(vif->active_links) : -1;

	BUILD_BUG_ON(WLAN_KEY_LEN_CCMP != WLAN_KEY_LEN_GCMP);
	BUILD_BUG_ON(sizeof(conf.key) < WLAN_KEY_LEN_CCMP);
	BUILD_BUG_ON(sizeof(conf.key) < WLAN_KEY_LEN_GCMP_256);
	BUILD_BUG_ON(sizeof(conf.key) < WLAN_KEY_LEN_TKIP);
	BUILD_BUG_ON(sizeof(conf.key) < sizeof(status->gtk[0].key));

	switch (gtk_cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
		conf.conf.keylen = WLAN_KEY_LEN_CCMP;
		break;
	case WLAN_CIPHER_SUITE_GCMP_256:
		conf.conf.keylen = WLAN_KEY_LEN_GCMP_256;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		conf.conf.keylen = WLAN_KEY_LEN_TKIP;
		break;
	default:
		WARN_ON(1);
	}

	for (i = 0; i < ARRAY_SIZE(status->gtk); i++) {
		if (!status->gtk[i].len)
			continue;

		conf.conf.keyidx = status->gtk[i].id;
		IWL_DEBUG_WOWLAN(mvm,
				 "Received from FW GTK cipher %d, key index %d\n",
				 conf.conf.cipher, conf.conf.keyidx);
		memcpy(conf.conf.key, status->gtk[i].key,
		       sizeof(status->gtk[i].key));

		key = ieee80211_gtk_rekey_add(vif, &conf.conf, link_id);
		if (IS_ERR(key))
			return false;

		for (j = 0; j < ARRAY_SIZE(status->gtk_seq); j++) {
			if (!status->gtk_seq[j].valid ||
			    status->gtk_seq[j].key_id != key->keyidx)
				continue;
			iwl_mvm_set_key_rx_seq_idx(key, status, j);
			break;
		}
		WARN_ON(j == ARRAY_SIZE(status->gtk_seq));
	}

	return true;
}

static bool
iwl_mvm_d3_igtk_bigtk_rekey_add(struct iwl_wowlan_status_data *status,
				struct ieee80211_vif *vif, u32 cipher,
				struct iwl_multicast_key_data *key_data)
{
	struct ieee80211_key_conf *key_config;
	struct {
		struct ieee80211_key_conf conf;
		u8 key[WOWLAN_KEY_MAX_SIZE];
	} conf = {
		.conf.cipher = cipher,
		.conf.keyidx = key_data->id,
	};
	struct ieee80211_key_seq seq;
	int link_id = vif->active_links ? __ffs(vif->active_links) : -1;

	if (!key_data->len)
		return true;

	iwl_mvm_d3_set_igtk_bigtk_ipn(key_data, &seq, conf.conf.cipher);

	switch (cipher) {
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
		conf.conf.keylen = WLAN_KEY_LEN_BIP_GMAC_128;
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		conf.conf.keylen = WLAN_KEY_LEN_BIP_GMAC_256;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		conf.conf.keylen = WLAN_KEY_LEN_AES_CMAC;
		break;
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		conf.conf.keylen = WLAN_KEY_LEN_BIP_CMAC_256;
		break;
	default:
		WARN_ON(1);
	}
	BUILD_BUG_ON(sizeof(conf.key) < sizeof(key_data->key));
	memcpy(conf.conf.key, key_data->key, conf.conf.keylen);

	key_config = ieee80211_gtk_rekey_add(vif, &conf.conf, link_id);
	if (IS_ERR(key_config))
		return false;
	ieee80211_set_key_rx_seq(key_config, 0, &seq);

	if (key_config->keyidx == 4 || key_config->keyidx == 5) {
		struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
		struct iwl_mvm_vif_link_info *mvm_link;

		link_id = link_id < 0 ? 0 : link_id;
		mvm_link = mvmvif->link[link_id];
		mvm_link->igtk = key_config;
	}

	return true;
}

static int iwl_mvm_lookup_wowlan_status_ver(struct iwl_mvm *mvm)
{
	u8 notif_ver;

	if (!fw_has_api(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_API_WOWLAN_KEY_MATERIAL))
		return 6;

	/* default to 7 (when we have IWL_UCODE_TLV_API_WOWLAN_KEY_MATERIAL) */
	notif_ver = iwl_fw_lookup_notif_ver(mvm->fw, LONG_GROUP,
					    WOWLAN_GET_STATUSES, 0);
	if (!notif_ver)
		notif_ver = iwl_fw_lookup_notif_ver(mvm->fw, LEGACY_GROUP,
						    WOWLAN_GET_STATUSES, 7);

	return notif_ver;
}

static bool iwl_mvm_setup_connection_keep(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  struct iwl_wowlan_status_data *status)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_d3_gtk_iter_data gtkdata = {
		.mvm = mvm,
		.status = status,
	};
	int i;
	u32 disconnection_reasons =
		IWL_WOWLAN_WAKEUP_BY_DISCONNECTION_ON_MISSED_BEACON |
		IWL_WOWLAN_WAKEUP_BY_DISCONNECTION_ON_DEAUTH;

	if (!status || !vif->bss_conf.bssid)
		return false;

	if (iwl_mvm_lookup_wowlan_status_ver(mvm) > 6 ||
	    iwl_fw_lookup_notif_ver(mvm->fw, PROT_OFFLOAD_GROUP,
				    WOWLAN_INFO_NOTIFICATION,
				    0))
		gtkdata.igtk_support = true;

	if (iwl_fw_lookup_notif_ver(mvm->fw, PROT_OFFLOAD_GROUP,
				    WOWLAN_INFO_NOTIFICATION,
				    0) >= 3)
		gtkdata.bigtk_support = true;

	/* find last GTK that we used initially, if any */
	ieee80211_iter_keys(mvm->hw, vif,
			    iwl_mvm_d3_find_last_keys, &gtkdata);
	/* not trying to keep connections with MFP/unhandled ciphers */
	if (gtkdata.unhandled_cipher)
		return false;
	if (!gtkdata.num_keys)
		goto out;

	/*
	 * invalidate all other GTKs that might still exist and update
	 * the one that we used
	 */
	ieee80211_iter_keys(mvm->hw, vif,
			    iwl_mvm_d3_update_keys, &gtkdata);

	if (status->num_of_gtk_rekeys) {
		__be64 replay_ctr = cpu_to_be64(status->replay_ctr);

		IWL_DEBUG_WOWLAN(mvm, "num of GTK rekeying %d\n",
				 status->num_of_gtk_rekeys);

		if (!iwl_mvm_gtk_rekey(status, vif, mvm, gtkdata.gtk_cipher))
			return false;

		if (!iwl_mvm_d3_igtk_bigtk_rekey_add(status, vif,
						     gtkdata.igtk_cipher,
						     &status->igtk))
			return false;

		for (i = 0; i < ARRAY_SIZE(status->bigtk); i++) {
			if (!iwl_mvm_d3_igtk_bigtk_rekey_add(status, vif,
							     gtkdata.bigtk_cipher,
							     &status->bigtk[i]))
				return false;
		}

		if (!iwl_mvm_mlo_gtk_rekey(status, vif, mvm))
			return false;

		ieee80211_gtk_rekey_notify(vif, vif->bss_conf.bssid,
					   (void *)&replay_ctr, GFP_KERNEL);
	}

out:
	if (iwl_fw_lookup_notif_ver(mvm->fw, LONG_GROUP,
				    WOWLAN_GET_STATUSES, 0) < 10) {
		mvmvif->seqno_valid = true;
		/* +0x10 because the set API expects next-to-use, not last-used */
		mvmvif->seqno = status->non_qos_seq_ctr + 0x10;
	}

	if (status->wakeup_reasons & disconnection_reasons)
		return false;

	return true;
}

static void iwl_mvm_convert_gtk_v2(struct iwl_wowlan_status_data *status,
				   struct iwl_wowlan_gtk_status_v2 *data)
{
	BUILD_BUG_ON(sizeof(status->gtk[0].key) < sizeof(data->key));
	BUILD_BUG_ON(NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY +
		     sizeof(data->tkip_mic_key) >
		     sizeof(status->gtk[0].key));

	status->gtk[0].len = data->key_len;
	status->gtk[0].flags = data->key_flags;

	memcpy(status->gtk[0].key, data->key, sizeof(data->key));

	/* if it's as long as the TKIP encryption key, copy MIC key */
	if (status->gtk[0].len == NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY)
		memcpy(status->gtk[0].key + NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY,
		       data->tkip_mic_key, sizeof(data->tkip_mic_key));
}

static void iwl_mvm_convert_gtk_v3(struct iwl_wowlan_status_data *status,
				   struct iwl_wowlan_gtk_status_v3 *data)
{
	int data_idx, status_idx = 0;

	BUILD_BUG_ON(sizeof(status->gtk[0].key) < sizeof(data[0].key));
	BUILD_BUG_ON(NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY +
		     sizeof(data[0].tkip_mic_key) >
		     sizeof(status->gtk[0].key));
	BUILD_BUG_ON(ARRAY_SIZE(status->gtk) < WOWLAN_GTK_KEYS_NUM);
	for (data_idx = 0; data_idx < ARRAY_SIZE(status->gtk); data_idx++) {
		if (!(data[data_idx].key_len))
			continue;
		status->gtk[status_idx].len = data[data_idx].key_len;
		status->gtk[status_idx].flags = data[data_idx].key_flags;
		status->gtk[status_idx].id = status->gtk[status_idx].flags &
				    IWL_WOWLAN_GTK_IDX_MASK;

		memcpy(status->gtk[status_idx].key, data[data_idx].key,
		       sizeof(data[data_idx].key));

		/* if it's as long as the TKIP encryption key, copy MIC key */
		if (status->gtk[status_idx].len ==
		    NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY)
			memcpy(status->gtk[status_idx].key +
			       NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY,
			       data[data_idx].tkip_mic_key,
			       sizeof(data[data_idx].tkip_mic_key));
		status_idx++;
	}
}

static void iwl_mvm_convert_igtk(struct iwl_wowlan_status_data *status,
				 struct iwl_wowlan_igtk_status *data)
{
	int i;

	BUILD_BUG_ON(sizeof(status->igtk.key) < sizeof(data->key));
	BUILD_BUG_ON(sizeof(status->igtk.ipn) != sizeof(data->ipn));

	if (!data->key_len)
		return;

	status->igtk.len = data->key_len;
	status->igtk.flags = data->key_flags;
	status->igtk.id = u32_get_bits(data->key_flags,
				       IWL_WOWLAN_IGTK_BIGTK_IDX_MASK)
		+ WOWLAN_IGTK_MIN_INDEX;

	memcpy(status->igtk.key, data->key, sizeof(data->key));

	/* mac80211 expects big endian for memcmp() to work, convert */
	for (i = 0; i < sizeof(data->ipn); i++)
		status->igtk.ipn[i] = data->ipn[sizeof(data->ipn) - i - 1];
}

static void iwl_mvm_convert_bigtk(struct iwl_wowlan_status_data *status,
				  const struct iwl_wowlan_igtk_status *data)
{
	int data_idx, status_idx = 0;

	BUILD_BUG_ON(ARRAY_SIZE(status->bigtk) < WOWLAN_BIGTK_KEYS_NUM);

	for (data_idx = 0; data_idx < WOWLAN_BIGTK_KEYS_NUM; data_idx++) {
		if (!data[data_idx].key_len)
			continue;

		status->bigtk[status_idx].len = data[data_idx].key_len;
		status->bigtk[status_idx].flags = data[data_idx].key_flags;
		status->bigtk[status_idx].id =
			u32_get_bits(data[data_idx].key_flags,
				     IWL_WOWLAN_IGTK_BIGTK_IDX_MASK)
			+ WOWLAN_BIGTK_MIN_INDEX;

		BUILD_BUG_ON(sizeof(status->bigtk[status_idx].key) <
			     sizeof(data[data_idx].key));
		BUILD_BUG_ON(sizeof(status->bigtk[status_idx].ipn) <
			     sizeof(data[data_idx].ipn));

		memcpy(status->bigtk[status_idx].key, data[data_idx].key,
		       sizeof(data[data_idx].key));
		memcpy(status->bigtk[status_idx].ipn, data[data_idx].ipn,
		       sizeof(data[data_idx].ipn));
		status_idx++;
	}
}

static void iwl_mvm_parse_wowlan_info_notif(struct iwl_mvm *mvm,
					    struct iwl_wowlan_info_notif *data,
					    struct iwl_wowlan_status_data *status,
					    u32 len, bool has_mlo_keys)
{
	u32 i;
	u32 expected_len = sizeof(*data);

	if (!data) {
		IWL_ERR(mvm, "iwl_wowlan_info_notif data is NULL\n");
		status = NULL;
		return;
	}

	if (has_mlo_keys)
		expected_len += (data->num_mlo_link_keys *
				 sizeof(status->mlo_keys[0]));

	if (len < expected_len) {
		IWL_ERR(mvm, "Invalid WoWLAN info notification!\n");
		status = NULL;
		return;
	}

	iwl_mvm_convert_key_counters_v5(status, &data->gtk[0].sc);
	iwl_mvm_convert_gtk_v3(status, data->gtk);
	iwl_mvm_convert_igtk(status, &data->igtk[0]);
	iwl_mvm_convert_bigtk(status, data->bigtk);
	status->replay_ctr = le64_to_cpu(data->replay_ctr);
	status->pattern_number = le16_to_cpu(data->pattern_number);
	for (i = 0; i < IWL_MAX_TID_COUNT; i++)
		status->qos_seq_ctr[i] =
			le16_to_cpu(data->qos_seq_ctr[i]);
	status->wakeup_reasons = le32_to_cpu(data->wakeup_reasons);
	status->num_of_gtk_rekeys =
		le32_to_cpu(data->num_of_gtk_rekeys);
	status->received_beacons = le32_to_cpu(data->received_beacons);
	status->tid_tear_down = data->tid_tear_down;

	if (has_mlo_keys && data->num_mlo_link_keys) {
		status->num_mlo_keys = data->num_mlo_link_keys;
		if (IWL_FW_CHECK(mvm,
				 status->num_mlo_keys > WOWLAN_MAX_MLO_KEYS,
				 "Too many mlo keys: %d, max %d\n",
				 status->num_mlo_keys, WOWLAN_MAX_MLO_KEYS))
			status->num_mlo_keys = WOWLAN_MAX_MLO_KEYS;
		memcpy(status->mlo_keys, data->mlo_gtks,
		       status->num_mlo_keys * sizeof(status->mlo_keys[0]));
	}
}

static void
iwl_mvm_parse_wowlan_info_notif_v2(struct iwl_mvm *mvm,
				   struct iwl_wowlan_info_notif_v2 *data,
				   struct iwl_wowlan_status_data *status,
				   u32 len)
{
	u32 i;

	if (!data) {
		IWL_ERR(mvm, "iwl_wowlan_info_notif data is NULL\n");
		status = NULL;
		return;
	}

	if (len < sizeof(*data)) {
		IWL_ERR(mvm, "Invalid WoWLAN info notification!\n");
		status = NULL;
		return;
	}

	iwl_mvm_convert_key_counters_v5(status, &data->gtk[0].sc);
	iwl_mvm_convert_gtk_v3(status, data->gtk);
	iwl_mvm_convert_igtk(status, &data->igtk[0]);
	status->replay_ctr = le64_to_cpu(data->replay_ctr);
	status->pattern_number = le16_to_cpu(data->pattern_number);
	for (i = 0; i < IWL_MAX_TID_COUNT; i++)
		status->qos_seq_ctr[i] =
			le16_to_cpu(data->qos_seq_ctr[i]);
	status->wakeup_reasons = le32_to_cpu(data->wakeup_reasons);
	status->num_of_gtk_rekeys =
		le32_to_cpu(data->num_of_gtk_rekeys);
	status->received_beacons = le32_to_cpu(data->received_beacons);
	status->tid_tear_down = data->tid_tear_down;
}

/* Occasionally, templates would be nice. This is one of those times ... */
#define iwl_mvm_parse_wowlan_status_common(_ver)			\
static struct iwl_wowlan_status_data *					\
iwl_mvm_parse_wowlan_status_common_ ## _ver(struct iwl_mvm *mvm,	\
					    struct iwl_wowlan_status_ ##_ver *data,\
					    int len)			\
{									\
	struct iwl_wowlan_status_data *status;				\
	int data_size, i;						\
									\
	if (len < sizeof(*data)) {					\
		IWL_ERR(mvm, "Invalid WoWLAN status response!\n");	\
		return NULL;						\
	}								\
									\
	data_size = ALIGN(le32_to_cpu(data->wake_packet_bufsize), 4);	\
	if (len != sizeof(*data) + data_size) {				\
		IWL_ERR(mvm, "Invalid WoWLAN status response!\n");	\
		return NULL;						\
	}								\
									\
	status = kzalloc(sizeof(*status), GFP_KERNEL);			\
	if (!status)							\
		return NULL;						\
									\
	/* copy all the common fields */				\
	status->replay_ctr = le64_to_cpu(data->replay_ctr);		\
	status->pattern_number = le16_to_cpu(data->pattern_number);	\
	status->non_qos_seq_ctr = le16_to_cpu(data->non_qos_seq_ctr);	\
	for (i = 0; i < 8; i++)						\
		status->qos_seq_ctr[i] =				\
			le16_to_cpu(data->qos_seq_ctr[i]);		\
	status->wakeup_reasons = le32_to_cpu(data->wakeup_reasons);	\
	status->num_of_gtk_rekeys =					\
		le32_to_cpu(data->num_of_gtk_rekeys);			\
	status->received_beacons = le32_to_cpu(data->received_beacons);	\
	status->wake_packet_length =					\
		le32_to_cpu(data->wake_packet_length);			\
	status->wake_packet_bufsize =					\
		le32_to_cpu(data->wake_packet_bufsize);			\
	if (status->wake_packet_bufsize) {				\
		status->wake_packet =					\
			kmemdup(data->wake_packet,			\
				status->wake_packet_bufsize,		\
				GFP_KERNEL);				\
		if (!status->wake_packet) {				\
			kfree(status);					\
			return NULL;					\
		}							\
	} else {							\
		status->wake_packet = NULL;				\
	}								\
									\
	return status;							\
}

iwl_mvm_parse_wowlan_status_common(v6)
iwl_mvm_parse_wowlan_status_common(v7)
iwl_mvm_parse_wowlan_status_common(v9)
iwl_mvm_parse_wowlan_status_common(v12)

static struct iwl_wowlan_status_data *
iwl_mvm_send_wowlan_get_status(struct iwl_mvm *mvm, u8 sta_id)
{
	struct iwl_wowlan_status_data *status;
	struct iwl_wowlan_get_status_cmd get_status_cmd = {
		.sta_id = cpu_to_le32(sta_id),
	};
	struct iwl_host_cmd cmd = {
		.id = WOWLAN_GET_STATUSES,
		.flags = CMD_WANT_SKB,
		.data = { &get_status_cmd, },
		.len = { sizeof(get_status_cmd), },
	};
	int ret, len;
	u8 notif_ver;
	u8 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw, cmd.id,
					   IWL_FW_CMD_VER_UNKNOWN);

	if (cmd_ver == IWL_FW_CMD_VER_UNKNOWN)
		cmd.len[0] = 0;

	lockdep_assert_held(&mvm->mutex);

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (ret) {
		IWL_ERR(mvm, "failed to query wakeup status (%d)\n", ret);
		return ERR_PTR(ret);
	}

	len = iwl_rx_packet_payload_len(cmd.resp_pkt);

	/* default to 7 (when we have IWL_UCODE_TLV_API_WOWLAN_KEY_MATERIAL) */
	notif_ver = iwl_mvm_lookup_wowlan_status_ver(mvm);

	if (notif_ver < 7) {
		struct iwl_wowlan_status_v6 *v6 = (void *)cmd.resp_pkt->data;

		status = iwl_mvm_parse_wowlan_status_common_v6(mvm, v6, len);
		if (!status)
			goto out_free_resp;

		BUILD_BUG_ON(sizeof(v6->gtk.decrypt_key) >
			     sizeof(status->gtk[0].key));
		BUILD_BUG_ON(NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY +
			     sizeof(v6->gtk.tkip_mic_key) >
			     sizeof(status->gtk[0].key));

		/* copy GTK info to the right place */
		memcpy(status->gtk[0].key, v6->gtk.decrypt_key,
		       sizeof(v6->gtk.decrypt_key));
		memcpy(status->gtk[0].key + NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY,
		       v6->gtk.tkip_mic_key,
		       sizeof(v6->gtk.tkip_mic_key));

		iwl_mvm_convert_key_counters(status, &v6->gtk.rsc.all_tsc_rsc);

		/* hardcode the key length to 16 since v6 only supports 16 */
		status->gtk[0].len = 16;

		/*
		 * The key index only uses 2 bits (values 0 to 3) and
		 * we always set bit 7 which means this is the
		 * currently used key.
		 */
		status->gtk[0].flags = v6->gtk.key_index | BIT(7);
	} else if (notif_ver == 7) {
		struct iwl_wowlan_status_v7 *v7 = (void *)cmd.resp_pkt->data;

		status = iwl_mvm_parse_wowlan_status_common_v7(mvm, v7, len);
		if (!status)
			goto out_free_resp;

		iwl_mvm_convert_key_counters(status, &v7->gtk[0].rsc.all_tsc_rsc);
		iwl_mvm_convert_gtk_v2(status, &v7->gtk[0]);
		iwl_mvm_convert_igtk(status, &v7->igtk[0]);
	} else if (notif_ver == 9 || notif_ver == 10 || notif_ver == 11) {
		struct iwl_wowlan_status_v9 *v9 = (void *)cmd.resp_pkt->data;

		/* these three command versions have same layout and size, the
		 * difference is only in a few not used (reserved) fields.
		 */
		status = iwl_mvm_parse_wowlan_status_common_v9(mvm, v9, len);
		if (!status)
			goto out_free_resp;

		iwl_mvm_convert_key_counters(status, &v9->gtk[0].rsc.all_tsc_rsc);
		iwl_mvm_convert_gtk_v2(status, &v9->gtk[0]);
		iwl_mvm_convert_igtk(status, &v9->igtk[0]);

		status->tid_tear_down = v9->tid_tear_down;
	} else if (notif_ver == 12) {
		struct iwl_wowlan_status_v12 *v12 = (void *)cmd.resp_pkt->data;

		status = iwl_mvm_parse_wowlan_status_common_v12(mvm, v12, len);
		if (!status)
			goto out_free_resp;

		iwl_mvm_convert_key_counters_v5(status, &v12->gtk[0].sc);
		iwl_mvm_convert_gtk_v3(status, v12->gtk);
		iwl_mvm_convert_igtk(status, &v12->igtk[0]);

		status->tid_tear_down = v12->tid_tear_down;
	} else {
		IWL_ERR(mvm,
			"Firmware advertises unknown WoWLAN status response %d!\n",
			notif_ver);
		status = NULL;
	}

out_free_resp:
	iwl_free_resp(&cmd);
	return status;
}

/* releases the MVM mutex */
static bool iwl_mvm_query_wakeup_reasons(struct iwl_mvm *mvm,
					 struct ieee80211_vif *vif,
					 struct iwl_wowlan_status_data *status)
{
	int i;
	bool keep = false;
	struct iwl_mvm_sta *mvm_ap_sta;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int link_id = vif->active_links ? __ffs(vif->active_links) : 0;
	struct iwl_mvm_vif_link_info *mvm_link = mvmvif->link[link_id];

	if (WARN_ON(!mvm_link))
		goto out_unlock;

	if (!status)
		goto out_unlock;

	IWL_DEBUG_WOWLAN(mvm, "wakeup reason 0x%x\n",
			 status->wakeup_reasons);

	mvm_ap_sta = iwl_mvm_sta_from_staid_protected(mvm, mvm_link->ap_sta_id);
	if (!mvm_ap_sta)
		goto out_unlock;

	for (i = 0; i < IWL_MAX_TID_COUNT; i++) {
		u16 seq = status->qos_seq_ctr[i];
		/* firmware stores last-used value, we store next value */
		seq += 0x10;
		mvm_ap_sta->tid_data[i].seq_number = seq;
	}

	if (mvm->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_22000) {
		i = mvm->offload_tid;
		iwl_trans_set_q_ptrs(mvm->trans,
				     mvm_ap_sta->tid_data[i].txq_id,
				     mvm_ap_sta->tid_data[i].seq_number >> 4);
	}

	iwl_mvm_report_wakeup_reasons(mvm, vif, status);

	keep = iwl_mvm_setup_connection_keep(mvm, vif, status);
out_unlock:
	mutex_unlock(&mvm->mutex);
	return keep;
}

#define ND_QUERY_BUF_LEN (sizeof(struct iwl_scan_offload_profile_match) * \
			  IWL_SCAN_MAX_PROFILES)

struct iwl_mvm_nd_results {
	u32 matched_profiles;
	u8 matches[ND_QUERY_BUF_LEN];
};

static int
iwl_mvm_netdetect_query_results(struct iwl_mvm *mvm,
				struct iwl_mvm_nd_results *results)
{
	struct iwl_scan_offload_match_info *query;
	struct iwl_host_cmd cmd = {
		.id = SCAN_OFFLOAD_PROFILES_QUERY_CMD,
		.flags = CMD_WANT_SKB,
	};
	int ret, len;
	size_t query_len, matches_len;
	int max_profiles = iwl_umac_scan_get_max_profiles(mvm->fw);

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (ret) {
		IWL_ERR(mvm, "failed to query matched profiles (%d)\n", ret);
		return ret;
	}

	if (fw_has_api(&mvm->fw->ucode_capa,
		       IWL_UCODE_TLV_API_SCAN_OFFLOAD_CHANS)) {
		query_len = sizeof(struct iwl_scan_offload_match_info);
		matches_len = sizeof(struct iwl_scan_offload_profile_match) *
			max_profiles;
	} else {
		query_len = sizeof(struct iwl_scan_offload_profiles_query_v1);
		matches_len = sizeof(struct iwl_scan_offload_profile_match_v1) *
			max_profiles;
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
					 struct iwl_mvm_nd_results *results,
					 int idx)
{
	int n_chans = 0, i;

	if (fw_has_api(&mvm->fw->ucode_capa,
		       IWL_UCODE_TLV_API_SCAN_OFFLOAD_CHANS)) {
		struct iwl_scan_offload_profile_match *matches =
			(void *)results->matches;

		for (i = 0; i < SCAN_OFFLOAD_MATCHING_CHANNELS_LEN; i++)
			n_chans += hweight8(matches[idx].matching_channels[i]);
	} else {
		struct iwl_scan_offload_profile_match_v1 *matches =
			(void *)results->matches;

		for (i = 0; i < SCAN_OFFLOAD_MATCHING_CHANNELS_LEN_V1; i++)
			n_chans += hweight8(matches[idx].matching_channels[i]);
	}

	return n_chans;
}

static void iwl_mvm_query_set_freqs(struct iwl_mvm *mvm,
				    struct iwl_mvm_nd_results *results,
				    struct cfg80211_wowlan_nd_match *match,
				    int idx)
{
	int i;

	if (fw_has_api(&mvm->fw->ucode_capa,
		       IWL_UCODE_TLV_API_SCAN_OFFLOAD_CHANS)) {
		struct iwl_scan_offload_profile_match *matches =
			 (void *)results->matches;

		for (i = 0; i < SCAN_OFFLOAD_MATCHING_CHANNELS_LEN * 8; i++)
			if (matches[idx].matching_channels[i / 8] & (BIT(i % 8)))
				match->channels[match->n_channels++] =
					mvm->nd_channels[i]->center_freq;
	} else {
		struct iwl_scan_offload_profile_match_v1 *matches =
			 (void *)results->matches;

		for (i = 0; i < SCAN_OFFLOAD_MATCHING_CHANNELS_LEN_V1 * 8; i++)
			if (matches[idx].matching_channels[i / 8] & (BIT(i % 8)))
				match->channels[match->n_channels++] =
					mvm->nd_channels[i]->center_freq;
	}
}

/**
 * enum iwl_d3_notif - d3 notifications
 * @IWL_D3_NOTIF_WOWLAN_INFO: WOWLAN_INFO_NOTIF was received
 * @IWL_D3_NOTIF_WOWLAN_WAKE_PKT: WOWLAN_WAKE_PKT_NOTIF was received
 * @IWL_D3_NOTIF_PROT_OFFLOAD: PROT_OFFLOAD_NOTIF was received
 * @IWL_D3_ND_MATCH_INFO: OFFLOAD_MATCH_INFO_NOTIF was received
 * @IWL_D3_NOTIF_D3_END_NOTIF: D3_END_NOTIF was received
 */
enum iwl_d3_notif {
	IWL_D3_NOTIF_WOWLAN_INFO =	BIT(0),
	IWL_D3_NOTIF_WOWLAN_WAKE_PKT =	BIT(1),
	IWL_D3_NOTIF_PROT_OFFLOAD =	BIT(2),
	IWL_D3_ND_MATCH_INFO      =     BIT(3),
	IWL_D3_NOTIF_D3_END_NOTIF =	BIT(4)
};

/* manage d3 resume data */
struct iwl_d3_data {
	struct iwl_wowlan_status_data *status;
	bool test;
	u32 d3_end_flags;
	u32 notif_expected;	/* bitmap - see &enum iwl_d3_notif */
	u32 notif_received;	/* bitmap - see &enum iwl_d3_notif */
	struct iwl_mvm_nd_results *nd_results;
	bool nd_results_valid;
};

static void iwl_mvm_query_netdetect_reasons(struct iwl_mvm *mvm,
					    struct ieee80211_vif *vif,
					    struct iwl_d3_data *d3_data)
{
	struct cfg80211_wowlan_nd_info *net_detect = NULL;
	struct cfg80211_wowlan_wakeup wakeup = {
		.pattern_idx = -1,
	};
	struct cfg80211_wowlan_wakeup *wakeup_report = &wakeup;
	unsigned long matched_profiles;
	u32 reasons = 0;
	int i, n_matches, ret;

	if (WARN_ON(!d3_data || !d3_data->status))
		goto out;

	reasons = d3_data->status->wakeup_reasons;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_RFKILL_DEASSERTED)
		wakeup.rfkill_release = true;

	if (reasons != IWL_WOWLAN_WAKEUP_BY_NON_WIRELESS)
		goto out;

	if (!iwl_fw_lookup_notif_ver(mvm->fw, PROT_OFFLOAD_GROUP,
				     WOWLAN_INFO_NOTIFICATION, 0)) {
		IWL_INFO(mvm, "Query FW for ND results\n");
		ret = iwl_mvm_netdetect_query_results(mvm, d3_data->nd_results);

	} else {
		IWL_INFO(mvm, "Notification based ND results\n");
		ret = d3_data->nd_results_valid ? 0 : -1;
	}

	if (ret || !d3_data->nd_results->matched_profiles) {
		wakeup_report = NULL;
		goto out;
	}

	matched_profiles = d3_data->nd_results->matched_profiles;
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

		n_channels = iwl_mvm_query_num_match_chans(mvm,
							   d3_data->nd_results,
							   i);

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

		iwl_mvm_query_set_freqs(mvm, d3_data->nd_results, match, i);
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

static void iwl_mvm_d3_disconnect_iter(void *data, u8 *mac,
				       struct ieee80211_vif *vif)
{
	/* skip the one we keep connection on */
	if (data == vif)
		return;

	if (vif->type == NL80211_IFTYPE_STATION)
		ieee80211_resume_disconnect(vif);
}

static bool iwl_mvm_rt_status(struct iwl_trans *trans, u32 base, u32 *err_id)
{
	struct error_table_start {
		/* cf. struct iwl_error_event_table */
		u32 valid;
		__le32 err_id;
	} err_info;

	if (!base)
		return false;

	iwl_trans_read_mem_bytes(trans, base,
				 &err_info, sizeof(err_info));
	if (err_info.valid && err_id)
		*err_id = le32_to_cpu(err_info.err_id);

	return !!err_info.valid;
}

static bool iwl_mvm_check_rt_status(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif)
{
	u32 err_id;

	/* check for lmac1 error */
	if (iwl_mvm_rt_status(mvm->trans,
			      mvm->trans->dbg.lmac_error_event_table[0],
			      &err_id)) {
		if (err_id == RF_KILL_INDICATOR_FOR_WOWLAN) {
			struct cfg80211_wowlan_wakeup wakeup = {
				.rfkill_release = true,
			};
			ieee80211_report_wowlan_wakeup(vif, &wakeup,
						       GFP_KERNEL);
		}
		return true;
	}

	/* check if we have lmac2 set and check for error */
	if (iwl_mvm_rt_status(mvm->trans,
			      mvm->trans->dbg.lmac_error_event_table[1], NULL))
		return true;

	/* check for umac error */
	if (iwl_mvm_rt_status(mvm->trans,
			      mvm->trans->dbg.umac_error_event_table, NULL))
		return true;

	return false;
}

/*
 * This function assumes:
 *	1. The mutex is already held.
 *	2. The callee functions unlock the mutex.
 */
static bool
iwl_mvm_choose_query_wakeup_reasons(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif,
				    struct iwl_d3_data *d3_data)
{
	lockdep_assert_held(&mvm->mutex);

	/* if FW uses status notification, status shouldn't be NULL here */
	if (!d3_data->status) {
		struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
		u8 sta_id = mvm->net_detect ? IWL_MVM_INVALID_STA :
					      mvmvif->deflink.ap_sta_id;

		/* bug - FW with MLO has status notification */
		WARN_ON(ieee80211_vif_is_mld(vif));

		d3_data->status = iwl_mvm_send_wowlan_get_status(mvm, sta_id);
	}

	if (mvm->net_detect) {
		iwl_mvm_query_netdetect_reasons(mvm, vif, d3_data);
	} else {
		bool keep = iwl_mvm_query_wakeup_reasons(mvm, vif,
							 d3_data->status);

#ifdef CONFIG_IWLWIFI_DEBUGFS
		if (keep)
			mvm->keep_vif = vif;
#endif

		return keep;
	}
	return false;
}

#define IWL_WOWLAN_WAKEUP_REASON_HAS_WAKEUP_PKT (IWL_WOWLAN_WAKEUP_BY_MAGIC_PACKET | \
						 IWL_WOWLAN_WAKEUP_BY_PATTERN | \
						 IWL_WAKEUP_BY_PATTERN_IPV4_TCP_SYN |\
						 IWL_WAKEUP_BY_PATTERN_IPV4_TCP_SYN_WILDCARD |\
						 IWL_WAKEUP_BY_PATTERN_IPV6_TCP_SYN |\
						 IWL_WAKEUP_BY_PATTERN_IPV6_TCP_SYN_WILDCARD)

static int iwl_mvm_wowlan_store_wake_pkt(struct iwl_mvm *mvm,
					 struct iwl_wowlan_wake_pkt_notif *notif,
					 struct iwl_wowlan_status_data *status,
					 u32 len)
{
	u32 data_size, packet_len = le32_to_cpu(notif->wake_packet_length);

	if (len < sizeof(*notif)) {
		IWL_ERR(mvm, "Invalid WoWLAN wake packet notification!\n");
		return -EIO;
	}

	if (WARN_ON(!status)) {
		IWL_ERR(mvm, "Got wake packet notification but wowlan status data is NULL\n");
		return -EIO;
	}

	if (WARN_ON(!(status->wakeup_reasons &
		      IWL_WOWLAN_WAKEUP_REASON_HAS_WAKEUP_PKT))) {
		IWL_ERR(mvm, "Got wakeup packet but wakeup reason is %x\n",
			status->wakeup_reasons);
		return -EIO;
	}

	data_size = len - offsetof(struct iwl_wowlan_wake_pkt_notif, wake_packet);

	/* data_size got the padding from the notification, remove it. */
	if (packet_len < data_size)
		data_size = packet_len;

	status->wake_packet = kmemdup(notif->wake_packet, data_size,
				      GFP_ATOMIC);

	if (!status->wake_packet)
		return -ENOMEM;

	status->wake_packet_length = packet_len;
	status->wake_packet_bufsize = data_size;

	return 0;
}

static void iwl_mvm_nd_match_info_handler(struct iwl_mvm *mvm,
					  struct iwl_d3_data *d3_data,
					  struct iwl_scan_offload_match_info *notif,
					  u32 len)
{
	struct iwl_wowlan_status_data *status = d3_data->status;
	struct ieee80211_vif *vif = iwl_mvm_get_bss_vif(mvm);
	struct iwl_mvm_nd_results *results = d3_data->nd_results;
	size_t i, matches_len = sizeof(struct iwl_scan_offload_profile_match) *
		iwl_umac_scan_get_max_profiles(mvm->fw);

	if (IS_ERR_OR_NULL(vif))
		return;

	if (len < sizeof(struct iwl_scan_offload_match_info)) {
		IWL_ERR(mvm, "Invalid scan match info notification\n");
		return;
	}

	if (!mvm->net_detect) {
		IWL_ERR(mvm, "Unexpected scan match info notification\n");
		return;
	}

	if (!status || status->wakeup_reasons != IWL_WOWLAN_WAKEUP_BY_NON_WIRELESS) {
		IWL_ERR(mvm,
			"Ignore scan match info notification: no reason\n");
		return;
	}

#ifdef CONFIG_IWLWIFI_DEBUGFS
	mvm->last_netdetect_scans = le32_to_cpu(notif->n_scans_done);
#endif

	results->matched_profiles = le32_to_cpu(notif->matched_profiles);
	IWL_INFO(mvm, "number of matched profiles=%u\n",
		 results->matched_profiles);

	if (results->matched_profiles) {
		memcpy(results->matches, notif->matches, matches_len);
		d3_data->nd_results_valid = true;
	}

	/* no scan should be active at this point */
	mvm->scan_status = 0;
	for (i = 0; i < mvm->max_scans; i++)
		mvm->scan_uid_status[i] = 0;
}

static bool iwl_mvm_wait_d3_notif(struct iwl_notif_wait_data *notif_wait,
				  struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_mvm *mvm =
		container_of(notif_wait, struct iwl_mvm, notif_wait);
	struct iwl_d3_data *d3_data = data;
	u32 len = iwl_rx_packet_payload_len(pkt);
	int ret;
	int wowlan_info_ver = iwl_fw_lookup_notif_ver(mvm->fw,
						      PROT_OFFLOAD_GROUP,
						      WOWLAN_INFO_NOTIFICATION,
						      IWL_FW_CMD_VER_UNKNOWN);


	switch (WIDE_ID(pkt->hdr.group_id, pkt->hdr.cmd)) {
	case WIDE_ID(PROT_OFFLOAD_GROUP, WOWLAN_INFO_NOTIFICATION): {

		if (d3_data->notif_received & IWL_D3_NOTIF_WOWLAN_INFO) {
			/* We might get two notifications due to dual bss */
			IWL_DEBUG_WOWLAN(mvm,
					 "Got additional wowlan info notification\n");
			break;
		}

		if (wowlan_info_ver < 2) {
			struct iwl_wowlan_info_notif_v1 *notif_v1 =
				(void *)pkt->data;
			struct iwl_wowlan_info_notif_v2 *notif_v2;

			notif_v2 = kmemdup(notif_v1, sizeof(*notif_v2), GFP_ATOMIC);

			if (!notif_v2)
				return false;

			notif_v2->tid_tear_down = notif_v1->tid_tear_down;
			notif_v2->station_id = notif_v1->station_id;
			memset_after(notif_v2, 0, station_id);
			iwl_mvm_parse_wowlan_info_notif_v2(mvm, notif_v2,
							   d3_data->status,
							   len);
			kfree(notif_v2);

		} else if (wowlan_info_ver == 2) {
			struct iwl_wowlan_info_notif_v2 *notif_v2 =
				(void *)pkt->data;

			iwl_mvm_parse_wowlan_info_notif_v2(mvm, notif_v2,
							   d3_data->status,
							   len);
		} else {
			struct iwl_wowlan_info_notif *notif =
				(void *)pkt->data;

			iwl_mvm_parse_wowlan_info_notif(mvm, notif,
							d3_data->status, len,
							wowlan_info_ver > 3);
		}

		d3_data->notif_received |= IWL_D3_NOTIF_WOWLAN_INFO;

		if (d3_data->status &&
		    d3_data->status->wakeup_reasons & IWL_WOWLAN_WAKEUP_REASON_HAS_WAKEUP_PKT)
			/* We are supposed to get also wake packet notif */
			d3_data->notif_expected |= IWL_D3_NOTIF_WOWLAN_WAKE_PKT;

		break;
	}
	case WIDE_ID(PROT_OFFLOAD_GROUP, WOWLAN_WAKE_PKT_NOTIFICATION): {
		struct iwl_wowlan_wake_pkt_notif *notif = (void *)pkt->data;

		if (d3_data->notif_received & IWL_D3_NOTIF_WOWLAN_WAKE_PKT) {
			/* We shouldn't get two wake packet notifications */
			IWL_ERR(mvm,
				"Got additional wowlan wake packet notification\n");
		} else {
			d3_data->notif_received |= IWL_D3_NOTIF_WOWLAN_WAKE_PKT;
			len =  iwl_rx_packet_payload_len(pkt);
			ret = iwl_mvm_wowlan_store_wake_pkt(mvm, notif,
							    d3_data->status,
							    len);
			if (ret)
				IWL_ERR(mvm,
					"Can't parse WOWLAN_WAKE_PKT_NOTIFICATION\n");
		}

		break;
	}
	case WIDE_ID(SCAN_GROUP, OFFLOAD_MATCH_INFO_NOTIF): {
		struct iwl_scan_offload_match_info *notif = (void *)pkt->data;

		if (d3_data->notif_received & IWL_D3_ND_MATCH_INFO) {
			IWL_ERR(mvm,
				"Got additional netdetect match info\n");
			break;
		}

		d3_data->notif_received |= IWL_D3_ND_MATCH_INFO;

		/* explicitly set this in the 'expected' as well */
		d3_data->notif_expected |= IWL_D3_ND_MATCH_INFO;

		len = iwl_rx_packet_payload_len(pkt);
		iwl_mvm_nd_match_info_handler(mvm, d3_data, notif, len);
		break;
	}
	case WIDE_ID(PROT_OFFLOAD_GROUP, D3_END_NOTIFICATION): {
		struct iwl_mvm_d3_end_notif *notif = (void *)pkt->data;

		d3_data->d3_end_flags = __le32_to_cpu(notif->flags);
		d3_data->notif_received |= IWL_D3_NOTIF_D3_END_NOTIF;

		break;
	}
	default:
		WARN_ON(1);
	}

	return d3_data->notif_received == d3_data->notif_expected;
}

static int iwl_mvm_resume_firmware(struct iwl_mvm *mvm, bool test)
{
	int ret;
	enum iwl_d3_status d3_status;
	struct iwl_host_cmd cmd = {
			.id = D0I3_END_CMD,
			.flags = CMD_WANT_SKB | CMD_SEND_IN_D3,
		};
	bool reset = fw_has_capa(&mvm->fw->ucode_capa,
				 IWL_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG);

	ret = iwl_trans_d3_resume(mvm->trans, &d3_status, test, !reset);
	if (ret)
		return ret;

	if (d3_status != IWL_D3_STATUS_ALIVE) {
		IWL_INFO(mvm, "Device was reset during suspend\n");
		return -ENOENT;
	}

	/*
	 * We should trigger resume flow using command only for 22000 family
	 * AX210 and above don't need the command since they have
	 * the doorbell interrupt.
	 */
	if (mvm->trans->trans_cfg->device_family <= IWL_DEVICE_FAMILY_22000 &&
	    fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_D0I3_END_FIRST)) {
		ret = iwl_mvm_send_cmd(mvm, &cmd);
		if (ret < 0)
			IWL_ERR(mvm, "Failed to send D0I3_END_CMD first (%d)\n",
				ret);
	}

	return ret;
}

#define IWL_MVM_D3_NOTIF_TIMEOUT (HZ / 5)

static int iwl_mvm_d3_notif_wait(struct iwl_mvm *mvm,
				 struct iwl_d3_data *d3_data)
{
	static const u16 d3_resume_notif[] = {
		WIDE_ID(PROT_OFFLOAD_GROUP, WOWLAN_INFO_NOTIFICATION),
		WIDE_ID(PROT_OFFLOAD_GROUP, WOWLAN_WAKE_PKT_NOTIFICATION),
		WIDE_ID(SCAN_GROUP, OFFLOAD_MATCH_INFO_NOTIF),
		WIDE_ID(PROT_OFFLOAD_GROUP, D3_END_NOTIFICATION)
	};
	struct iwl_notification_wait wait_d3_notif;
	int ret;

	iwl_init_notification_wait(&mvm->notif_wait, &wait_d3_notif,
				   d3_resume_notif, ARRAY_SIZE(d3_resume_notif),
				   iwl_mvm_wait_d3_notif, d3_data);

	ret = iwl_mvm_resume_firmware(mvm, d3_data->test);
	if (ret) {
		iwl_remove_notification(&mvm->notif_wait, &wait_d3_notif);
		return ret;
	}

	return iwl_wait_notification(&mvm->notif_wait, &wait_d3_notif,
				     IWL_MVM_D3_NOTIF_TIMEOUT);
}

static inline bool iwl_mvm_d3_resume_notif_based(struct iwl_mvm *mvm)
{
	return iwl_fw_lookup_notif_ver(mvm->fw, PROT_OFFLOAD_GROUP,
				       WOWLAN_INFO_NOTIFICATION, 0) &&
		iwl_fw_lookup_notif_ver(mvm->fw, PROT_OFFLOAD_GROUP,
					WOWLAN_WAKE_PKT_NOTIFICATION, 0) &&
		iwl_fw_lookup_notif_ver(mvm->fw, PROT_OFFLOAD_GROUP,
					D3_END_NOTIFICATION, 0);
}

static int __iwl_mvm_resume(struct iwl_mvm *mvm, bool test)
{
	struct ieee80211_vif *vif = NULL;
	int ret = 1;
	struct iwl_mvm_nd_results results = {};
	struct iwl_d3_data d3_data = {
		.test = test,
		.notif_expected =
			IWL_D3_NOTIF_WOWLAN_INFO |
			IWL_D3_NOTIF_D3_END_NOTIF,
		.nd_results_valid = false,
		.nd_results = &results,
	};
	bool unified_image = fw_has_capa(&mvm->fw->ucode_capa,
					 IWL_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG);
	bool d0i3_first = fw_has_capa(&mvm->fw->ucode_capa,
				      IWL_UCODE_TLV_CAPA_D0I3_END_FIRST);
	bool resume_notif_based = iwl_mvm_d3_resume_notif_based(mvm);
	bool keep = false;

	mutex_lock(&mvm->mutex);

	mvm->last_reset_or_resume_time_jiffies = jiffies;

	/* get the BSS vif pointer again */
	vif = iwl_mvm_get_bss_vif(mvm);
	if (IS_ERR_OR_NULL(vif))
		goto err;

	iwl_fw_dbg_read_d3_debug_data(&mvm->fwrt);

	if (iwl_mvm_check_rt_status(mvm, vif)) {
		set_bit(STATUS_FW_ERROR, &mvm->trans->status);
		iwl_mvm_dump_nic_error_log(mvm);
		iwl_dbg_tlv_time_point(&mvm->fwrt,
				       IWL_FW_INI_TIME_POINT_FW_ASSERT, NULL);
		iwl_fw_dbg_collect_desc(&mvm->fwrt, &iwl_dump_desc_assert,
					false, 0);
		ret = 1;
		goto err;
	}

	if (resume_notif_based) {
		d3_data.status = kzalloc(sizeof(*d3_data.status), GFP_KERNEL);
		if (!d3_data.status) {
			IWL_ERR(mvm, "Failed to allocate wowlan status\n");
			ret = -ENOMEM;
			goto err;
		}

		ret = iwl_mvm_d3_notif_wait(mvm, &d3_data);
		if (ret)
			goto err;
	} else {
		ret = iwl_mvm_resume_firmware(mvm, test);
		if (ret < 0)
			goto err;
	}

	iwl_mvm_unblock_esr(mvm, vif, IWL_MVM_ESR_BLOCKED_WOWLAN);

	/* after the successful handshake, we're out of D3 */
	mvm->trans->system_pm_mode = IWL_PLAT_PM_MODE_DISABLED;

	/* when reset is required we can't send these following commands */
	if (d3_data.d3_end_flags & IWL_D0I3_RESET_REQUIRE)
		goto query_wakeup_reasons;

	/*
	 * Query the current location and source from the D3 firmware so we
	 * can play it back when we re-intiailize the D0 firmware
	 */
	iwl_mvm_update_changed_regdom(mvm);

	/* Re-configure PPAG settings */
	iwl_mvm_ppag_send_cmd(mvm);

	if (!unified_image)
		/*  Re-configure default SAR profile */
		iwl_mvm_sar_select_profile(mvm, 1, 1);

	if (mvm->net_detect && unified_image) {
		/* If this is a non-unified image, we restart the FW,
		 * so no need to stop the netdetect scan.  If that
		 * fails, continue and try to get the wake-up reasons,
		 * but trigger a HW restart by keeping a failure code
		 * in ret.
		 */
		ret = iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_NETDETECT,
					false);
	}

query_wakeup_reasons:
	keep = iwl_mvm_choose_query_wakeup_reasons(mvm, vif, &d3_data);
	/* has unlocked the mutex, so skip that */
	goto out;

err:
	mutex_unlock(&mvm->mutex);
out:
	if (d3_data.status)
		kfree(d3_data.status->wake_packet);
	kfree(d3_data.status);
	iwl_mvm_free_nd(mvm);

	if (!d3_data.test && !mvm->net_detect)
		ieee80211_iterate_active_interfaces_mtx(mvm->hw,
							IEEE80211_IFACE_ITER_NORMAL,
							iwl_mvm_d3_disconnect_iter,
							keep ? vif : NULL);

	clear_bit(IWL_MVM_STATUS_IN_D3, &mvm->status);

	/* no need to reset the device in unified images, if successful */
	if (unified_image && !ret) {
		/* nothing else to do if we already sent D0I3_END_CMD */
		if (d0i3_first)
			return 0;

		if (!iwl_fw_lookup_notif_ver(mvm->fw, PROT_OFFLOAD_GROUP,
					     D3_END_NOTIFICATION, 0)) {
			ret = iwl_mvm_send_cmd_pdu(mvm, D0I3_END_CMD, 0, 0, NULL);
			if (!ret)
				return 0;
		} else if (!(d3_data.d3_end_flags & IWL_D0I3_RESET_REQUIRE)) {
			return 0;
		}
	}

	/*
	 * Reconfigure the device in one of the following cases:
	 * 1. We are not using a unified image
	 * 2. We are using a unified image but had an error while exiting D3
	 */
	set_bit(IWL_MVM_STATUS_HW_RESTART_REQUESTED, &mvm->status);

	/* regardless of what happened, we're now out of D3 */
	mvm->trans->system_pm_mode = IWL_PLAT_PM_MODE_DISABLED;

	return 1;
}

int iwl_mvm_resume(struct ieee80211_hw *hw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	ret = __iwl_mvm_resume(mvm, false);

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

	iwl_mvm_pause_tcm(mvm, true);

	iwl_fw_runtime_suspend(&mvm->fwrt);

	/* start pseudo D3 */
	rtnl_lock();
	wiphy_lock(mvm->hw->wiphy);
	err = __iwl_mvm_suspend(mvm->hw, mvm->hw->wiphy->wowlan_config, true);
	wiphy_unlock(mvm->hw->wiphy);
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
	unsigned long end = jiffies + 60 * HZ;
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

		if (time_is_before_jiffies(end)) {
			IWL_ERR(mvm,
				"ending pseudo-D3 with timeout after ~60 seconds\n");
			return -ETIMEDOUT;
		}
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
	wiphy_lock(mvm->hw->wiphy);
	__iwl_mvm_resume(mvm, true);
	wiphy_unlock(mvm->hw->wiphy);
	rtnl_unlock();

	iwl_mvm_resume_tcm(mvm);

	iwl_fw_runtime_resume(&mvm->fwrt);

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
