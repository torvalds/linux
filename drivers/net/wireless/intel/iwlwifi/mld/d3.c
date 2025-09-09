// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#include "mld.h"

#include "d3.h"
#include "power.h"
#include "hcmd.h"
#include "iface.h"
#include "mcc.h"
#include "sta.h"
#include "mlo.h"
#include "key.h"

#include "fw/api/d3.h"
#include "fw/api/offload.h"
#include "fw/api/sta.h"
#include "fw/dbg.h"

#include <net/ipv6.h>
#include <net/addrconf.h>
#include <linux/bitops.h>

/**
 * enum iwl_mld_d3_notif - d3 notifications
 * @IWL_D3_NOTIF_WOWLAN_INFO: WOWLAN_INFO_NOTIF is expected/was received
 * @IWL_D3_NOTIF_WOWLAN_WAKE_PKT: WOWLAN_WAKE_PKT_NOTIF is expected/was received
 * @IWL_D3_NOTIF_PROT_OFFLOAD: PROT_OFFLOAD_NOTIF is expected/was received
 * @IWL_D3_ND_MATCH_INFO: OFFLOAD_MATCH_INFO_NOTIF is expected/was received
 * @IWL_D3_NOTIF_D3_END_NOTIF: D3_END_NOTIF is expected/was received
 */
enum iwl_mld_d3_notif {
	IWL_D3_NOTIF_WOWLAN_INFO =	BIT(0),
	IWL_D3_NOTIF_WOWLAN_WAKE_PKT =	BIT(1),
	IWL_D3_NOTIF_PROT_OFFLOAD =	BIT(2),
	IWL_D3_ND_MATCH_INFO      =     BIT(3),
	IWL_D3_NOTIF_D3_END_NOTIF =	BIT(4)
};

struct iwl_mld_resume_key_iter_data {
	struct iwl_mld *mld;
	struct iwl_mld_wowlan_status *wowlan_status;
};

struct iwl_mld_suspend_key_iter_data {
	struct iwl_wowlan_rsc_tsc_params_cmd *rsc;
	bool have_rsc;
	int gtks;
	int found_gtk_idx[4];
	__le32 gtk_cipher;
	__le32 igtk_cipher;
	__le32 bigtk_cipher;
};

struct iwl_mld_mcast_key_data {
	u8 key[WOWLAN_KEY_MAX_SIZE];
	u8 len;
	u8 flags;
	u8 id;
	union {
		struct {
			struct ieee80211_key_seq aes_seq[IWL_MAX_TID_COUNT];
			struct ieee80211_key_seq tkip_seq[IWL_MAX_TID_COUNT];
		} gtk;
		struct {
			struct ieee80211_key_seq cmac_gmac_seq;
		} igtk_bigtk;
	};

};

struct iwl_mld_wowlan_mlo_key {
	u8 key[WOWLAN_KEY_MAX_SIZE];
	u8 idx, type, link_id;
	u8 pn[6];
};

/**
 * struct iwl_mld_wowlan_status - contains wowlan status data from
 * all wowlan notifications
 * @wakeup_reasons: wakeup reasons, see &enum iwl_wowlan_wakeup_reason
 * @replay_ctr: GTK rekey replay counter
 * @pattern_number: number of the matched patterns on packets
 * @last_qos_seq: QoS sequence counter of offloaded tid
 * @num_of_gtk_rekeys: number of GTK rekeys during D3
 * @tid_offloaded_tx: tid used by the firmware to transmit data packets
 *	while in wowlan
 * @wake_packet: wakeup packet received
 * @wake_packet_length: wake packet length
 * @wake_packet_bufsize: wake packet bufsize
 * @gtk: data of the last two used gtk's by the FW upon resume
 * @igtk: data of the last used igtk by the FW upon resume
 * @bigtk: data of the last two used gtk's by the FW upon resume
 * @ptk: last seq numbers per tid passed by the FW,
 *	holds both in tkip and aes formats
 * @num_mlo_keys: number of &struct iwl_mld_wowlan_mlo_key structs
 * @mlo_keys: array of MLO keys
 */
struct iwl_mld_wowlan_status {
	u32 wakeup_reasons;
	u64 replay_ctr;
	u16 pattern_number;
	u16 last_qos_seq;
	u32 num_of_gtk_rekeys;
	u8 tid_offloaded_tx;
	u8 *wake_packet;
	u32 wake_packet_length;
	u32 wake_packet_bufsize;
	struct iwl_mld_mcast_key_data gtk[WOWLAN_GTK_KEYS_NUM];
	struct iwl_mld_mcast_key_data igtk;
	struct iwl_mld_mcast_key_data bigtk[WOWLAN_BIGTK_KEYS_NUM];
	struct {
		struct ieee80211_key_seq aes_seq[IWL_MAX_TID_COUNT];
		struct ieee80211_key_seq tkip_seq[IWL_MAX_TID_COUNT];

	} ptk;

	int num_mlo_keys;
	struct iwl_mld_wowlan_mlo_key mlo_keys[WOWLAN_MAX_MLO_KEYS];
};

#define NETDETECT_QUERY_BUF_LEN \
	(sizeof(struct iwl_scan_offload_profile_match) * \
	 IWL_SCAN_MAX_PROFILES_V2)

/**
 * struct iwl_mld_netdetect_res - contains netdetect results from
 * match_info_notif
 * @matched_profiles: bitmap of matched profiles, referencing the
 *	matches passed in the scan offload request
 * @matches: array of match information, one for each match
 */
struct iwl_mld_netdetect_res {
	u32 matched_profiles;
	u8 matches[NETDETECT_QUERY_BUF_LEN];
};

/**
 * struct iwl_mld_resume_data - d3 resume flow data
 * @notifs_expected: bitmap of expected notifications from fw,
 *	see &enum iwl_mld_d3_notif
 * @notifs_received: bitmap of received notifications from fw,
 *	see &enum iwl_mld_d3_notif
 * @d3_end_flags: bitmap of flags from d3_end_notif
 * @notif_handling_err: error handling one of the resume notifications
 * @wowlan_status: wowlan status data from all wowlan notifications
 * @netdetect_res: contains netdetect results from match_info_notif
 */
struct iwl_mld_resume_data {
	u32 notifs_expected;
	u32 notifs_received;
	u32 d3_end_flags;
	bool notif_handling_err;
	struct iwl_mld_wowlan_status *wowlan_status;
	struct iwl_mld_netdetect_res *netdetect_res;
};

#define IWL_WOWLAN_WAKEUP_REASON_HAS_WAKEUP_PKT \
	(IWL_WOWLAN_WAKEUP_BY_MAGIC_PACKET | \
	IWL_WOWLAN_WAKEUP_BY_PATTERN | \
	IWL_WAKEUP_BY_PATTERN_IPV4_TCP_SYN |\
	IWL_WAKEUP_BY_PATTERN_IPV4_TCP_SYN_WILDCARD |\
	IWL_WAKEUP_BY_PATTERN_IPV6_TCP_SYN |\
	IWL_WAKEUP_BY_PATTERN_IPV6_TCP_SYN_WILDCARD)

#define IWL_WOWLAN_OFFLOAD_TID 0

void iwl_mld_set_rekey_data(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct cfg80211_gtk_rekey_data *data)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_wowlan_data *wowlan_data = &mld_vif->wowlan_data;

	lockdep_assert_wiphy(mld->wiphy);

	wowlan_data->rekey_data.kek_len = data->kek_len;
	wowlan_data->rekey_data.kck_len = data->kck_len;
	memcpy(wowlan_data->rekey_data.kek, data->kek, data->kek_len);
	memcpy(wowlan_data->rekey_data.kck, data->kck, data->kck_len);
	wowlan_data->rekey_data.akm = data->akm & 0xFF;
	wowlan_data->rekey_data.replay_ctr =
		cpu_to_le64(be64_to_cpup((const __be64 *)data->replay_ctr));
	wowlan_data->rekey_data.valid = true;
}

#if IS_ENABLED(CONFIG_IPV6)
void iwl_mld_ipv6_addr_change(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct inet6_dev *idev)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_wowlan_data *wowlan_data = &mld_vif->wowlan_data;
	struct inet6_ifaddr *ifa;
	int idx = 0;

	memset(wowlan_data->tentative_addrs, 0,
	       sizeof(wowlan_data->tentative_addrs));

	read_lock_bh(&idev->lock);
	list_for_each_entry(ifa, &idev->addr_list, if_list) {
		wowlan_data->target_ipv6_addrs[idx] = ifa->addr;
		if (ifa->flags & IFA_F_TENTATIVE)
			__set_bit(idx, wowlan_data->tentative_addrs);
		idx++;
		if (idx >= IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_MAX)
			break;
	}
	read_unlock_bh(&idev->lock);

	wowlan_data->num_target_ipv6_addrs = idx;
}
#endif

static int
iwl_mld_netdetect_config(struct iwl_mld *mld,
			 struct ieee80211_vif *vif,
			 const struct cfg80211_wowlan *wowlan)
{
	int ret;
	struct cfg80211_sched_scan_request *netdetect_cfg =
		wowlan->nd_config;
	struct ieee80211_scan_ies ies = {};

	ret = iwl_mld_scan_stop(mld, IWL_MLD_SCAN_SCHED, true);
	if (ret)
		return ret;

	ret = iwl_mld_sched_scan_start(mld, vif, netdetect_cfg, &ies,
				       IWL_MLD_SCAN_NETDETECT);
	return ret;
}

static void
iwl_mld_le64_to_tkip_seq(__le64 le_pn, struct ieee80211_key_seq *seq)
{
	u64 pn = le64_to_cpu(le_pn);

	seq->tkip.iv16 = (u16)pn;
	seq->tkip.iv32 = (u32)(pn >> 16);
}

static void
iwl_mld_le64_to_aes_seq(__le64 le_pn, struct ieee80211_key_seq *seq)
{
	u64 pn = le64_to_cpu(le_pn);

	seq->ccmp.pn[0] = pn >> 40;
	seq->ccmp.pn[1] = pn >> 32;
	seq->ccmp.pn[2] = pn >> 24;
	seq->ccmp.pn[3] = pn >> 16;
	seq->ccmp.pn[4] = pn >> 8;
	seq->ccmp.pn[5] = pn;
}

static void
iwl_mld_convert_gtk_resume_seq(struct iwl_mld_mcast_key_data *gtk_data,
			       const struct iwl_wowlan_all_rsc_tsc_v5 *sc,
			       int rsc_idx)
{
	struct ieee80211_key_seq *aes_seq = gtk_data->gtk.aes_seq;
	struct ieee80211_key_seq *tkip_seq = gtk_data->gtk.tkip_seq;

	if (rsc_idx >= ARRAY_SIZE(sc->mcast_rsc))
		return;

	/* We store both the TKIP and AES representations coming from the
	 * FW because we decode the data from there before we iterate
	 * the keys and know which type is used.
	 */
	for (int tid = 0; tid < IWL_MAX_TID_COUNT; tid++) {
		iwl_mld_le64_to_tkip_seq(sc->mcast_rsc[rsc_idx][tid],
					 &tkip_seq[tid]);
		iwl_mld_le64_to_aes_seq(sc->mcast_rsc[rsc_idx][tid],
					&aes_seq[tid]);
	}
}

static void
iwl_mld_convert_gtk_resume_data(struct iwl_mld *mld,
				struct iwl_mld_wowlan_status *wowlan_status,
				const struct iwl_wowlan_gtk_status *gtk_data,
				const struct iwl_wowlan_all_rsc_tsc_v5 *sc)
{
	int status_idx = 0;

	BUILD_BUG_ON(sizeof(wowlan_status->gtk[0].key) <
		     sizeof(gtk_data[0].key));
	BUILD_BUG_ON(ARRAY_SIZE(wowlan_status->gtk) < WOWLAN_GTK_KEYS_NUM);

	for (int notif_idx = 0; notif_idx < ARRAY_SIZE(wowlan_status->gtk);
	     notif_idx++) {
		int rsc_idx;
		u8 key_status = gtk_data[notif_idx].key_status;

		if (!key_status)
			continue;

		wowlan_status->gtk[status_idx].len =
			gtk_data[notif_idx].key_len;
		wowlan_status->gtk[status_idx].flags =
			gtk_data[notif_idx].key_flags;
		wowlan_status->gtk[status_idx].id =
			wowlan_status->gtk[status_idx].flags &
			IWL_WOWLAN_GTK_IDX_MASK;
		/* The rsc for both gtk keys are stored in gtk[0]->sc->mcast_rsc
		 * The gtk ids can be any two numbers between 0 and 3,
		 * the id_map maps between the key id and the index in sc->mcast
		 */
		rsc_idx =
			sc->mcast_key_id_map[wowlan_status->gtk[status_idx].id];
		iwl_mld_convert_gtk_resume_seq(&wowlan_status->gtk[status_idx],
					       sc, rsc_idx);

		if (key_status == IWL_WOWLAN_STATUS_NEW_KEY) {
			memcpy(wowlan_status->gtk[status_idx].key,
			       gtk_data[notif_idx].key,
			       sizeof(gtk_data[notif_idx].key));

			/* if it's as long as the TKIP encryption key,
			 * copy MIC key
			 */
			if (wowlan_status->gtk[status_idx].len ==
			    NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY)
				memcpy(wowlan_status->gtk[status_idx].key +
				       NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY,
				       gtk_data[notif_idx].tkip_mic_key,
				       sizeof(gtk_data[notif_idx].tkip_mic_key));
		} else {
			/* If the key status is WOWLAN_STATUS_OLD_KEY, it
			 * indicates that no key material is present, Set the
			 * key length to 0 as an indication
			 */
			wowlan_status->gtk[status_idx].len = 0;
		}
		status_idx++;
	}
}

static void
iwl_mld_convert_ptk_resume_seq(struct iwl_mld *mld,
			       struct iwl_mld_wowlan_status *wowlan_status,
			       const struct iwl_wowlan_all_rsc_tsc_v5 *sc)
{
	struct ieee80211_key_seq *aes_seq = wowlan_status->ptk.aes_seq;
	struct ieee80211_key_seq *tkip_seq = wowlan_status->ptk.tkip_seq;

	BUILD_BUG_ON(ARRAY_SIZE(sc->ucast_rsc) != IWL_MAX_TID_COUNT);

	for (int tid = 0; tid < IWL_MAX_TID_COUNT; tid++) {
		iwl_mld_le64_to_aes_seq(sc->ucast_rsc[tid], &aes_seq[tid]);
		iwl_mld_le64_to_tkip_seq(sc->ucast_rsc[tid], &tkip_seq[tid]);
	}
}

static void
iwl_mld_convert_mcast_ipn(struct iwl_mld_mcast_key_data *key_status,
			  const struct iwl_wowlan_igtk_status *key)
{
	struct ieee80211_key_seq *seq =
		&key_status->igtk_bigtk.cmac_gmac_seq;
	u8 ipn_len = ARRAY_SIZE(key->ipn);

	BUILD_BUG_ON(ipn_len != ARRAY_SIZE(seq->aes_gmac.pn));
	BUILD_BUG_ON(ipn_len != ARRAY_SIZE(seq->aes_cmac.pn));
	BUILD_BUG_ON(offsetof(struct ieee80211_key_seq, aes_gmac) !=
		     offsetof(struct ieee80211_key_seq, aes_cmac));

	/* mac80211 expects big endian for memcmp() to work, convert.
	 * We don't have the key cipher yet so copy to both to cmac and gmac
	 */
	for (int i = 0; i < ipn_len; i++) {
		seq->aes_gmac.pn[i] = key->ipn[ipn_len - i - 1];
		seq->aes_cmac.pn[i] = key->ipn[ipn_len - i - 1];
	}
}

static void
iwl_mld_convert_igtk_resume_data(struct iwl_mld_wowlan_status *wowlan_status,
				 const struct iwl_wowlan_igtk_status *igtk)
{
	if (!igtk->key_status)
		return;

	BUILD_BUG_ON(sizeof(wowlan_status->igtk.key) < sizeof(igtk->key));

	wowlan_status->igtk.len = igtk->key_len;
	wowlan_status->igtk.flags = igtk->key_flags;
	wowlan_status->igtk.id =
		u32_get_bits(igtk->key_flags,
			     IWL_WOWLAN_IGTK_BIGTK_IDX_MASK) +
		WOWLAN_IGTK_MIN_INDEX;

	if (igtk->key_status == IWL_WOWLAN_STATUS_NEW_KEY)
		memcpy(wowlan_status->igtk.key, igtk->key, sizeof(igtk->key));
	else
		/* If the key status is WOWLAN_STATUS_OLD_KEY, it indicates
		 * that no key material is present. Set the key length to 0
		 * as an indication.
		 */
		wowlan_status->igtk.len = 0;

	iwl_mld_convert_mcast_ipn(&wowlan_status->igtk, igtk);
}

static void
iwl_mld_convert_bigtk_resume_data(struct iwl_mld_wowlan_status *wowlan_status,
				  const struct iwl_wowlan_igtk_status *bigtk)
{
	int status_idx = 0;

	BUILD_BUG_ON(ARRAY_SIZE(wowlan_status->bigtk) < WOWLAN_BIGTK_KEYS_NUM);

	for (int notif_idx = 0; notif_idx < WOWLAN_BIGTK_KEYS_NUM;
	     notif_idx++) {
		if (!bigtk[notif_idx].key_status)
			continue;

		wowlan_status->bigtk[status_idx].len = bigtk[notif_idx].key_len;
		wowlan_status->bigtk[status_idx].flags =
			bigtk[notif_idx].key_flags;
		wowlan_status->bigtk[status_idx].id =
			u32_get_bits(bigtk[notif_idx].key_flags,
				     IWL_WOWLAN_IGTK_BIGTK_IDX_MASK)
			+ WOWLAN_BIGTK_MIN_INDEX;

		BUILD_BUG_ON(sizeof(wowlan_status->bigtk[status_idx].key) <
			     sizeof(bigtk[notif_idx].key));
		if (bigtk[notif_idx].key_status == IWL_WOWLAN_STATUS_NEW_KEY)
			memcpy(wowlan_status->bigtk[status_idx].key,
			       bigtk[notif_idx].key,
			       sizeof(bigtk[notif_idx].key));
		else
			/* If the key status is WOWLAN_STATUS_OLD_KEY, it
			 * indicates that no key material is present. Set the
			 * key length to 0 as an indication.
			 */
			wowlan_status->bigtk[status_idx].len = 0;

		iwl_mld_convert_mcast_ipn(&wowlan_status->bigtk[status_idx],
					  &bigtk[notif_idx]);
		status_idx++;
	}
}

static void
iwl_mld_convert_mlo_keys(struct iwl_mld *mld,
			 const struct iwl_wowlan_info_notif *notif,
			 struct iwl_mld_wowlan_status *wowlan_status)
{
	if (!notif->num_mlo_link_keys)
		return;

	wowlan_status->num_mlo_keys = notif->num_mlo_link_keys;

	if (IWL_FW_CHECK(mld, wowlan_status->num_mlo_keys > WOWLAN_MAX_MLO_KEYS,
			 "Too many MLO keys: %d, max %d\n",
			 wowlan_status->num_mlo_keys, WOWLAN_MAX_MLO_KEYS))
		wowlan_status->num_mlo_keys = WOWLAN_MAX_MLO_KEYS;

	for (int i = 0; i < wowlan_status->num_mlo_keys; i++) {
		const struct iwl_wowlan_mlo_gtk *fw_mlo_key = &notif->mlo_gtks[i];
		struct iwl_mld_wowlan_mlo_key *driver_mlo_key =
			&wowlan_status->mlo_keys[i];
		u16 flags = le16_to_cpu(fw_mlo_key->flags);

		driver_mlo_key->link_id =
			u16_get_bits(flags, WOWLAN_MLO_GTK_FLAG_LINK_ID_MSK);
		driver_mlo_key->type =
			u16_get_bits(flags, WOWLAN_MLO_GTK_FLAG_KEY_TYPE_MSK);
		driver_mlo_key->idx =
			u16_get_bits(flags, WOWLAN_MLO_GTK_FLAG_KEY_ID_MSK);

		BUILD_BUG_ON(sizeof(driver_mlo_key->key) != sizeof(fw_mlo_key->key));
		BUILD_BUG_ON(sizeof(driver_mlo_key->pn) != sizeof(fw_mlo_key->pn));

		memcpy(driver_mlo_key->key, fw_mlo_key->key, sizeof(fw_mlo_key->key));
		memcpy(driver_mlo_key->pn, fw_mlo_key->pn, sizeof(fw_mlo_key->pn));
	}
}

static void
iwl_mld_convert_wowlan_notif_v5(const struct iwl_wowlan_info_notif_v5 *notif_v5,
				struct iwl_wowlan_info_notif *notif)
{
	/* Convert GTK from v3 to the new format */
	BUILD_BUG_ON(ARRAY_SIZE(notif->gtk) != ARRAY_SIZE(notif_v5->gtk));

	for (int i = 0; i < ARRAY_SIZE(notif_v5->gtk); i++) {
		const struct iwl_wowlan_gtk_status_v3 *gtk_v3 = &notif_v5->gtk[i];
		struct iwl_wowlan_gtk_status *gtk = &notif->gtk[i];

		/* Copy key material and metadata */
		BUILD_BUG_ON(sizeof(gtk->key) != sizeof(gtk_v3->key));
		BUILD_BUG_ON(sizeof(gtk->tkip_mic_key) != sizeof(gtk_v3->tkip_mic_key));

		memcpy(gtk->key, gtk_v3->key, sizeof(gtk_v3->key));

		gtk->key_len = gtk_v3->key_len;
		gtk->key_flags = gtk_v3->key_flags;

		memcpy(gtk->tkip_mic_key, gtk_v3->tkip_mic_key,
		       sizeof(gtk_v3->tkip_mic_key));
		gtk->sc = gtk_v3->sc;

		/* Set key_status based on whether key material is present.
		 * in v5, a key is either invalid (should be skipped) or has
		 * both meta data and the key itself.
		 */
		if (gtk_v3->key_len)
			gtk->key_status = IWL_WOWLAN_STATUS_NEW_KEY;
	}

	/* Convert IGTK from v1 to the new format, only one IGTK is passed by FW */
	BUILD_BUG_ON(offsetof(struct iwl_wowlan_igtk_status, key_status) !=
		     sizeof(struct iwl_wowlan_igtk_status_v1));

	memcpy(&notif->igtk[0], &notif_v5->igtk[0],
	       offsetof(struct iwl_wowlan_igtk_status, key_status));

	/* Set key_status based on whether key material is present.
	 * in v5, a key is either invalid (should be skipped) or has
	 * both meta data and the key itself.
	 */
	if (notif_v5->igtk[0].key_len)
		notif->igtk[0].key_status = IWL_WOWLAN_STATUS_NEW_KEY;

	/* Convert BIGTK from v1 to the new format */
	BUILD_BUG_ON(ARRAY_SIZE(notif->bigtk) != ARRAY_SIZE(notif_v5->bigtk));

	for (int i = 0; i < ARRAY_SIZE(notif_v5->bigtk); i++) {
		/* Copy everything until key_status */
		memcpy(&notif->bigtk[i], &notif_v5->bigtk[i],
		       offsetof(struct iwl_wowlan_igtk_status, key_status));

		/* Set key_status based on whether key material is present.
		 * in v5, a key is either invalid (should be skipped) or has
		 * both meta data and the key itself.
		 */
		if (notif_v5->bigtk[i].key_len)
			notif->bigtk[i].key_status = IWL_WOWLAN_STATUS_NEW_KEY;
	}

	notif->replay_ctr = notif_v5->replay_ctr;
	notif->pattern_number = notif_v5->pattern_number;
	notif->qos_seq_ctr = notif_v5->qos_seq_ctr;
	notif->wakeup_reasons = notif_v5->wakeup_reasons;
	notif->num_of_gtk_rekeys = notif_v5->num_of_gtk_rekeys;
	notif->transmitted_ndps = notif_v5->transmitted_ndps;
	notif->received_beacons = notif_v5->received_beacons;
	notif->tid_tear_down = notif_v5->tid_tear_down;
	notif->station_id = notif_v5->station_id;
	notif->num_mlo_link_keys = notif_v5->num_mlo_link_keys;
	notif->tid_offloaded_tx = notif_v5->tid_offloaded_tx;

	/* Copy MLO GTK keys */
	if (notif_v5->num_mlo_link_keys) {
		memcpy(notif->mlo_gtks, notif_v5->mlo_gtks,
		       notif_v5->num_mlo_link_keys * sizeof(struct iwl_wowlan_mlo_gtk));
	}
}

static bool iwl_mld_validate_wowlan_notif_size(struct iwl_mld *mld,
					       u32 len,
					       u32 expected_len,
					       u8 num_mlo_keys,
					       int version)
{
	u32 len_with_mlo_keys;

	if (IWL_FW_CHECK(mld, len < expected_len,
			 "Invalid wowlan_info_notif v%d (expected=%u got=%u)\n",
			 version, expected_len, len))
		return false;

	len_with_mlo_keys = expected_len +
		(num_mlo_keys * sizeof(struct iwl_wowlan_mlo_gtk));

	if (IWL_FW_CHECK(mld, len < len_with_mlo_keys,
			 "Invalid wowlan_info_notif v%d with MLO keys (expected=%u got=%u)\n",
			 version, len_with_mlo_keys, len))
		return false;

	return true;
}

static bool
iwl_mld_handle_wowlan_info_notif(struct iwl_mld *mld,
				 struct iwl_mld_wowlan_status *wowlan_status,
				 struct iwl_rx_packet *pkt)
{
	const struct iwl_wowlan_info_notif *notif;
	struct iwl_wowlan_info_notif *converted_notif __free(kfree) = NULL;
	u32 len = iwl_rx_packet_payload_len(pkt);
	int wowlan_info_ver = iwl_fw_lookup_notif_ver(mld->fw,
						      PROT_OFFLOAD_GROUP,
						      WOWLAN_INFO_NOTIFICATION,
						      IWL_FW_CMD_VER_UNKNOWN);

	if (wowlan_info_ver == 5) {
		/* v5 format - validate before conversion */
		const struct iwl_wowlan_info_notif_v5 *notif_v5 = (void *)pkt->data;

		if (!iwl_mld_validate_wowlan_notif_size(mld, len,
							sizeof(*notif_v5),
							notif_v5->num_mlo_link_keys,
							5))
			return true;

		converted_notif = kzalloc(struct_size(converted_notif,
						      mlo_gtks,
						      notif_v5->num_mlo_link_keys),
					  GFP_ATOMIC);
		if (!converted_notif) {
			IWL_ERR(mld,
				"Failed to allocate memory for converted wowlan_info_notif\n");
			return true;
		}

		iwl_mld_convert_wowlan_notif_v5(notif_v5,
						converted_notif);
		notif = converted_notif;
	} else if (wowlan_info_ver == 6) {
		notif = (void *)pkt->data;
		if (!iwl_mld_validate_wowlan_notif_size(mld, len,
							sizeof(*notif),
							notif->num_mlo_link_keys,
							6))
			return true;
	} else {
		/* smaller versions are not supported */
		IWL_WARN(mld,
			 "Unsupported wowlan_info_notif version %d\n",
			 wowlan_info_ver);
		return true;
	}

	if (IWL_FW_CHECK(mld, notif->tid_offloaded_tx != IWL_WOWLAN_OFFLOAD_TID,
			 "Invalid tid_offloaded_tx %d\n",
			 notif->tid_offloaded_tx))
		return true;

	iwl_mld_convert_gtk_resume_data(mld, wowlan_status, notif->gtk,
					&notif->gtk[0].sc);
	iwl_mld_convert_ptk_resume_seq(mld, wowlan_status, &notif->gtk[0].sc);
	/* only one igtk is passed by FW */
	iwl_mld_convert_igtk_resume_data(wowlan_status, &notif->igtk[0]);
	iwl_mld_convert_bigtk_resume_data(wowlan_status, notif->bigtk);

	wowlan_status->replay_ctr = le64_to_cpu(notif->replay_ctr);
	wowlan_status->pattern_number = le16_to_cpu(notif->pattern_number);

	wowlan_status->tid_offloaded_tx = notif->tid_offloaded_tx;
	wowlan_status->last_qos_seq = le16_to_cpu(notif->qos_seq_ctr);
	wowlan_status->num_of_gtk_rekeys =
		le32_to_cpu(notif->num_of_gtk_rekeys);
	wowlan_status->wakeup_reasons = le32_to_cpu(notif->wakeup_reasons);

	iwl_mld_convert_mlo_keys(mld, notif, wowlan_status);

	return false;
}

static bool
iwl_mld_handle_wake_pkt_notif(struct iwl_mld *mld,
			      struct iwl_mld_wowlan_status *wowlan_status,
			      struct iwl_rx_packet *pkt)
{
	const struct iwl_wowlan_wake_pkt_notif *notif = (void *)pkt->data;
	u32 actual_size, len = iwl_rx_packet_payload_len(pkt);
	u32 expected_size = le32_to_cpu(notif->wake_packet_length);

	if (IWL_FW_CHECK(mld, len < sizeof(*notif),
			 "Invalid WoWLAN wake packet notification (expected size=%zu got=%u)\n",
			 sizeof(*notif), len))
		return true;

	if (IWL_FW_CHECK(mld, !(wowlan_status->wakeup_reasons &
				IWL_WOWLAN_WAKEUP_REASON_HAS_WAKEUP_PKT),
			 "Got wake packet but wakeup reason is %x\n",
			 wowlan_status->wakeup_reasons))
		return true;

	actual_size = len - offsetof(struct iwl_wowlan_wake_pkt_notif,
				     wake_packet);

	/* actual_size got the padding from the notification, remove it. */
	if (expected_size < actual_size)
		actual_size = expected_size;
	wowlan_status->wake_packet = kmemdup(notif->wake_packet, actual_size,
					     GFP_ATOMIC);
	if (!wowlan_status->wake_packet)
		return true;

	wowlan_status->wake_packet_length = expected_size;
	wowlan_status->wake_packet_bufsize = actual_size;

	return false;
}

static void
iwl_mld_set_wake_packet(struct iwl_mld *mld,
			struct ieee80211_vif *vif,
			const struct iwl_mld_wowlan_status *wowlan_status,
			struct cfg80211_wowlan_wakeup *wakeup,
			struct sk_buff **_pkt)
{
	int pkt_bufsize = wowlan_status->wake_packet_bufsize;
	int expected_pktlen = wowlan_status->wake_packet_length;
	const u8 *pktdata = wowlan_status->wake_packet;
	const struct ieee80211_hdr *hdr = (const void *)pktdata;
	int truncated = expected_pktlen - pkt_bufsize;

	if (ieee80211_is_data(hdr->frame_control)) {
		int hdrlen = ieee80211_hdrlen(hdr->frame_control);
		int ivlen = 0, icvlen = 4; /* also FCS */

		struct sk_buff *pkt = alloc_skb(pkt_bufsize, GFP_KERNEL);
		*_pkt = pkt;
		if (!pkt)
			return;

		skb_put_data(pkt, pktdata, hdrlen);
		pktdata += hdrlen;
		pkt_bufsize -= hdrlen;

		/* if truncated, FCS/ICV is (partially) gone */
		if (truncated >= icvlen) {
			truncated -= icvlen;
			icvlen = 0;
		} else {
			icvlen -= truncated;
			truncated = 0;
		}

		pkt_bufsize -= ivlen + icvlen;
		pktdata += ivlen;

		skb_put_data(pkt, pktdata, pkt_bufsize);

		if (ieee80211_data_to_8023(pkt, vif->addr, vif->type))
			return;
		wakeup->packet = pkt->data;
		wakeup->packet_present_len = pkt->len;
		wakeup->packet_len = pkt->len - truncated;
		wakeup->packet_80211 = false;
	} else {
		int fcslen = 4;

		if (truncated >= 4) {
			truncated -= 4;
			fcslen = 0;
		} else {
			fcslen -= truncated;
			truncated = 0;
		}
		pkt_bufsize -= fcslen;
		wakeup->packet = wowlan_status->wake_packet;
		wakeup->packet_present_len = pkt_bufsize;
		wakeup->packet_len = expected_pktlen - truncated;
		wakeup->packet_80211 = true;
	}
}

static void
iwl_mld_report_wowlan_wakeup(struct iwl_mld *mld,
			     struct ieee80211_vif *vif,
			     struct iwl_mld_wowlan_status *wowlan_status)
{
	struct sk_buff *pkt = NULL;
	struct cfg80211_wowlan_wakeup wakeup = {
		.pattern_idx = -1,
	};
	u32 reasons = wowlan_status->wakeup_reasons;

	if (reasons == IWL_WOWLAN_WAKEUP_BY_NON_WIRELESS) {
		ieee80211_report_wowlan_wakeup(vif, NULL, GFP_KERNEL);
		return;
	}

	pm_wakeup_event(mld->dev, 0);

	if (reasons & IWL_WOWLAN_WAKEUP_BY_MAGIC_PACKET)
		wakeup.magic_pkt = true;

	if (reasons & IWL_WOWLAN_WAKEUP_BY_PATTERN)
		wakeup.pattern_idx =
			wowlan_status->pattern_number;

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

	if (wowlan_status->wake_packet)
		iwl_mld_set_wake_packet(mld, vif, wowlan_status, &wakeup, &pkt);

	ieee80211_report_wowlan_wakeup(vif, &wakeup, GFP_KERNEL);
	kfree_skb(pkt);
}

static void
iwl_mld_set_key_rx_seq_tids(struct ieee80211_key_conf *key,
			    struct ieee80211_key_seq *seq)
{
	int tid;

	for (tid = 0; tid < IWL_MAX_TID_COUNT; tid++)
		ieee80211_set_key_rx_seq(key, tid, &seq[tid]);
}

static void
iwl_mld_update_mcast_rx_seq(struct ieee80211_key_conf *key,
			    struct iwl_mld_mcast_key_data *key_data)
{
	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		iwl_mld_set_key_rx_seq_tids(key,
					    key_data->gtk.aes_seq);
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		iwl_mld_set_key_rx_seq_tids(key,
					    key_data->gtk.tkip_seq);
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	case WLAN_CIPHER_SUITE_AES_CMAC:
		/* igtk/bigtk ciphers*/
		ieee80211_set_key_rx_seq(key, 0,
					 &key_data->igtk_bigtk.cmac_gmac_seq);
		break;
	default:
		WARN_ON(1);
	}
}

static void
iwl_mld_update_ptk_rx_seq(struct iwl_mld *mld,
			  struct iwl_mld_wowlan_status *wowlan_status,
			  struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key,
			  bool is_tkip)
{
	struct iwl_mld_sta *mld_sta =
		iwl_mld_sta_from_mac80211(sta);
	struct iwl_mld_ptk_pn *mld_ptk_pn =
		wiphy_dereference(mld->wiphy,
				  mld_sta->ptk_pn[key->keyidx]);

	iwl_mld_set_key_rx_seq_tids(key, is_tkip ?
				    wowlan_status->ptk.tkip_seq :
				    wowlan_status->ptk.aes_seq);
	if (is_tkip)
		return;

	if (WARN_ON(!mld_ptk_pn))
		return;

	for (int tid = 0; tid < IWL_MAX_TID_COUNT; tid++) {
		for (int i = 1; i < mld->trans->info.num_rxqs; i++)
			memcpy(mld_ptk_pn->q[i].pn[tid],
			       wowlan_status->ptk.aes_seq[tid].ccmp.pn,
			       IEEE80211_CCMP_PN_LEN);
	}
}

static void
iwl_mld_resume_keys_iter(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta,
			 struct ieee80211_key_conf *key,
			 void *_data)
{
	struct iwl_mld_resume_key_iter_data *data = _data;
	struct iwl_mld_wowlan_status *wowlan_status = data->wowlan_status;
	u8 status_idx;

	if (key->keyidx >= 0 && key->keyidx <= 3) {
		/* PTK */
		if (sta) {
			iwl_mld_update_ptk_rx_seq(data->mld, wowlan_status,
						  sta, key,
						  key->cipher ==
						  WLAN_CIPHER_SUITE_TKIP);
		/* GTK */
		} else {
			status_idx = key->keyidx == wowlan_status->gtk[1].id;
			iwl_mld_update_mcast_rx_seq(key,
						    &wowlan_status->gtk[status_idx]);
		}
	}

	/* IGTK */
	if (key->keyidx == 4 || key->keyidx == 5) {
		if (key->keyidx == wowlan_status->igtk.id)
			iwl_mld_update_mcast_rx_seq(key, &wowlan_status->igtk);
	}

	/* BIGTK */
	if (key->keyidx == 6 || key->keyidx == 7) {
		status_idx = key->keyidx == wowlan_status->bigtk[1].id;
		iwl_mld_update_mcast_rx_seq(key,
					    &wowlan_status->bigtk[status_idx]);
	}
}

static void
iwl_mld_add_mcast_rekey(struct ieee80211_vif *vif,
			struct iwl_mld *mld,
			struct iwl_mld_mcast_key_data *key_data,
			struct ieee80211_bss_conf *link_conf)
{
	struct ieee80211_key_conf *key_config;
	int link_id = vif->active_links ? __ffs(vif->active_links) : -1;

	if (!key_data->len)
		return;

	key_config = ieee80211_gtk_rekey_add(vif, key_data->id, key_data->key,
					     sizeof(key_data->key), link_id);
	if (IS_ERR(key_config))
		return;

	iwl_mld_update_mcast_rx_seq(key_config, key_data);

	/* The FW holds only one igtk so we keep track of the valid one */
	if (key_config->keyidx == 4 || key_config->keyidx == 5) {
		struct iwl_mld_link *mld_link =
			iwl_mld_link_from_mac80211(link_conf);

		/* If we had more than one rekey, mac80211 will tell us to
		 * remove the old and add the new so we will update the IGTK in
		 * drv_set_key
		 */
		if (mld_link->igtk && mld_link->igtk != key_config) {
			/* mark the old IGTK as not in FW */
			mld_link->igtk->hw_key_idx = STA_KEY_IDX_INVALID;
			mld_link->igtk = key_config;
		}
	}

	/* Also keep track of the new BIGTK */
	if (key_config->keyidx == 6 || key_config->keyidx == 7)
		iwl_mld_track_bigtk(mld, vif, key_config, true);
}

static void
iwl_mld_add_all_rekeys(struct iwl_mld *mld,
		       struct ieee80211_vif *vif,
		       struct iwl_mld_wowlan_status *wowlan_status,
		       struct ieee80211_bss_conf *link_conf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wowlan_status->gtk); i++)
		iwl_mld_add_mcast_rekey(vif, mld, &wowlan_status->gtk[i],
					link_conf);

	iwl_mld_add_mcast_rekey(vif, mld, &wowlan_status->igtk, link_conf);

	for (i = 0; i < ARRAY_SIZE(wowlan_status->bigtk); i++)
		iwl_mld_add_mcast_rekey(vif, mld, &wowlan_status->bigtk[i],
					link_conf);
}

static void iwl_mld_mlo_rekey(struct iwl_mld *mld,
			      struct iwl_mld_wowlan_status *wowlan_status,
			      struct ieee80211_vif *vif)
{
	struct iwl_mld_old_mlo_keys *old_keys __free(kfree) = NULL;

	IWL_DEBUG_WOWLAN(mld, "Num of MLO Keys: %d\n", wowlan_status->num_mlo_keys);

	if (!wowlan_status->num_mlo_keys)
		return;

	for (int i = 0; i < wowlan_status->num_mlo_keys; i++) {
		struct iwl_mld_wowlan_mlo_key *mlo_key = &wowlan_status->mlo_keys[i];
		struct ieee80211_key_conf *key;
		struct ieee80211_key_seq seq;
		u8 link_id = mlo_key->link_id;

		if (IWL_FW_CHECK(mld, mlo_key->link_id >= IEEE80211_MLD_MAX_NUM_LINKS ||
				      mlo_key->idx >= 8 ||
				      mlo_key->type >= WOWLAN_MLO_GTK_KEY_NUM_TYPES,
				      "Invalid MLO key link_id %d, idx %d, type %d\n",
				      mlo_key->link_id, mlo_key->idx, mlo_key->type))
			continue;

		if (!(vif->valid_links & BIT(link_id)) ||
		    (vif->active_links & BIT(link_id)))
			continue;

		IWL_DEBUG_WOWLAN(mld, "Add MLO key id %d, link id %d\n",
				 mlo_key->idx, link_id);

		key = ieee80211_gtk_rekey_add(vif, mlo_key->idx, mlo_key->key,
					      sizeof(mlo_key->key), link_id);

		if (IS_ERR(key))
			continue;

		/*
		 * mac80211 expects the PN in big-endian
		 * also note that seq is a union of all cipher types
		 * (ccmp, gcmp, cmac, gmac), and they all have the same
		 * pn field (of length 6) so just copy it to ccmp.pn.
		 */
		for (int j = 5; j >= 0; j--)
			seq.ccmp.pn[5 - j] = mlo_key->pn[j];

		/* group keys are non-QoS and use TID 0 */
		ieee80211_set_key_rx_seq(key, 0, &seq);
	}
}

static bool
iwl_mld_update_sec_keys(struct iwl_mld *mld,
			struct ieee80211_vif *vif,
			struct iwl_mld_wowlan_status *wowlan_status)
{
	int link_id = vif->active_links ? __ffs(vif->active_links) : 0;
	struct ieee80211_bss_conf *link_conf =
		link_conf_dereference_protected(vif, link_id);
	__be64 replay_ctr = cpu_to_be64(wowlan_status->replay_ctr);
	struct iwl_mld_resume_key_iter_data key_iter_data = {
		.mld = mld,
		.wowlan_status = wowlan_status,
	};

	if (WARN_ON(!link_conf))
		return false;

	ieee80211_iter_keys(mld->hw, vif, iwl_mld_resume_keys_iter,
			    &key_iter_data);

	IWL_DEBUG_WOWLAN(mld, "Number of rekeys: %d\n",
			 wowlan_status->num_of_gtk_rekeys);

	if (!wowlan_status->num_of_gtk_rekeys)
		return true;

	iwl_mld_add_all_rekeys(mld, vif, wowlan_status,
			       link_conf);

	iwl_mld_mlo_rekey(mld, wowlan_status, vif);

	ieee80211_gtk_rekey_notify(vif, link_conf->bssid,
				   (void *)&replay_ctr, GFP_KERNEL);
	return true;
}

static bool
iwl_mld_process_wowlan_status(struct iwl_mld *mld,
			      struct ieee80211_vif *vif,
			      struct iwl_mld_wowlan_status *wowlan_status)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct ieee80211_sta *ap_sta = mld_vif->ap_sta;
	struct iwl_mld_txq *mld_txq;

	iwl_mld_report_wowlan_wakeup(mld, vif, wowlan_status);

	if (WARN_ON(!ap_sta))
		return false;

	mld_txq =
		iwl_mld_txq_from_mac80211(ap_sta->txq[wowlan_status->tid_offloaded_tx]);

	/* Update the pointers of the Tx queue that may have moved during
	 * suspend if the firmware sent frames.
	 * The firmware stores last-used value, we store next value.
	 */
	WARN_ON(!mld_txq->status.allocated);
	iwl_trans_set_q_ptrs(mld->trans, mld_txq->fw_id,
			     (wowlan_status->last_qos_seq +
			     0x10) >> 4);

	if (!iwl_mld_update_sec_keys(mld, vif, wowlan_status))
		return false;

	if (wowlan_status->wakeup_reasons &
	    (IWL_WOWLAN_WAKEUP_BY_DISCONNECTION_ON_MISSED_BEACON |
	     IWL_WOWLAN_WAKEUP_BY_DISCONNECTION_ON_DEAUTH |
	     IWL_WOWLAN_WAKEUP_BY_GTK_REKEY_FAILURE))
		return false;

	return true;
}

static bool
iwl_mld_netdetect_match_info_handler(struct iwl_mld *mld,
				     struct iwl_mld_resume_data *resume_data,
				     struct iwl_rx_packet *pkt)
{
	struct iwl_mld_netdetect_res *results = resume_data->netdetect_res;
	const struct iwl_scan_offload_match_info *notif = (void *)pkt->data;
	u32 len = iwl_rx_packet_payload_len(pkt);

	if (IWL_FW_CHECK(mld, !mld->netdetect,
			 "Got scan match info notif when mld->netdetect==%d\n",
			 mld->netdetect))
		return true;

	if (IWL_FW_CHECK(mld, len < sizeof(*notif),
			 "Invalid scan offload match notif of length: %d\n",
			 len))
		return true;

	if (IWL_FW_CHECK(mld, resume_data->wowlan_status->wakeup_reasons !=
			 IWL_WOWLAN_WAKEUP_BY_NON_WIRELESS,
			 "Ignore scan match info: unexpected wakeup reason (expected=0x%x got=0x%x)\n",
			 IWL_WOWLAN_WAKEUP_BY_NON_WIRELESS,
			 resume_data->wowlan_status->wakeup_reasons))
		return true;

	results->matched_profiles = le32_to_cpu(notif->matched_profiles);
	IWL_DEBUG_WOWLAN(mld, "number of matched profiles=%u\n",
			 results->matched_profiles);

	if (results->matched_profiles)
		memcpy(results->matches, notif->matches,
		       NETDETECT_QUERY_BUF_LEN);

	/* No scan should be active at this point */
	mld->scan.status = 0;
	memset(mld->scan.uid_status, 0, sizeof(mld->scan.uid_status));
	return false;
}

static void
iwl_mld_set_netdetect_info(struct iwl_mld *mld,
			   const struct cfg80211_sched_scan_request *netdetect_cfg,
			   struct cfg80211_wowlan_nd_info *netdetect_info,
			   struct iwl_mld_netdetect_res *netdetect_res,
			   unsigned long matched_profiles)
{
	int i;

	for_each_set_bit(i, &matched_profiles, netdetect_cfg->n_match_sets) {
		struct cfg80211_wowlan_nd_match *match;
		int idx, j, n_channels = 0;
		struct iwl_scan_offload_profile_match *matches =
			(void *)netdetect_res->matches;

		for (int k = 0; k < SCAN_OFFLOAD_MATCHING_CHANNELS_LEN; k++)
			n_channels +=
				hweight8(matches[i].matching_channels[k]);
		match = kzalloc(struct_size(match, channels, n_channels),
				GFP_KERNEL);
		if (!match)
			return;

		netdetect_info->matches[netdetect_info->n_matches] = match;
		netdetect_info->n_matches++;

		/* We inverted the order of the SSIDs in the scan
		 * request, so invert the index here.
		 */
		idx = netdetect_cfg->n_match_sets - i - 1;
		match->ssid.ssid_len =
			netdetect_cfg->match_sets[idx].ssid.ssid_len;
		memcpy(match->ssid.ssid,
		       netdetect_cfg->match_sets[idx].ssid.ssid,
		       match->ssid.ssid_len);

		if (netdetect_cfg->n_channels < n_channels)
			continue;

		for_each_set_bit(j,
				 (unsigned long *)&matches[i].matching_channels[0],
				 sizeof(matches[i].matching_channels)) {
			match->channels[match->n_channels] =
				netdetect_cfg->channels[j]->center_freq;
			match->n_channels++;
		}
	}
}

static void
iwl_mld_process_netdetect_res(struct iwl_mld *mld,
			      struct ieee80211_vif *vif,
			      struct iwl_mld_resume_data *resume_data)
{
	struct cfg80211_wowlan_nd_info *netdetect_info = NULL;
	const struct cfg80211_sched_scan_request *netdetect_cfg;
	struct cfg80211_wowlan_wakeup wakeup = {
		.pattern_idx = -1,
	};
	struct cfg80211_wowlan_wakeup *wakeup_report = &wakeup;
	unsigned long matched_profiles;
	u32 wakeup_reasons;
	int n_matches;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(!mld->wiphy->wowlan_config ||
		    !mld->wiphy->wowlan_config->nd_config)) {
		IWL_DEBUG_WOWLAN(mld,
				 "Netdetect isn't configured on resume flow\n");
		goto out;
	}

	netdetect_cfg = mld->wiphy->wowlan_config->nd_config;
	wakeup_reasons = resume_data->wowlan_status->wakeup_reasons;

	if (wakeup_reasons & IWL_WOWLAN_WAKEUP_BY_RFKILL_DEASSERTED)
		wakeup.rfkill_release = true;

	if (wakeup_reasons != IWL_WOWLAN_WAKEUP_BY_NON_WIRELESS)
		goto out;

	if (!resume_data->netdetect_res->matched_profiles) {
		IWL_DEBUG_WOWLAN(mld,
				 "Netdetect results aren't valid\n");
		wakeup_report = NULL;
		goto out;
	}

	matched_profiles = resume_data->netdetect_res->matched_profiles;
	if (!netdetect_cfg->n_match_sets) {
		IWL_DEBUG_WOWLAN(mld,
				 "No netdetect match sets are configured\n");
		goto out;
	}
	n_matches = hweight_long(matched_profiles);
	netdetect_info = kzalloc(struct_size(netdetect_info, matches,
					     n_matches), GFP_KERNEL);
	if (netdetect_info)
		iwl_mld_set_netdetect_info(mld, netdetect_cfg, netdetect_info,
					   resume_data->netdetect_res,
					   matched_profiles);

	wakeup.net_detect = netdetect_info;
 out:
	ieee80211_report_wowlan_wakeup(vif, wakeup_report, GFP_KERNEL);
	if (netdetect_info) {
		for (int i = 0; i < netdetect_info->n_matches; i++)
			kfree(netdetect_info->matches[i]);
		kfree(netdetect_info);
	}
}

static bool iwl_mld_handle_d3_notif(struct iwl_notif_wait_data *notif_wait,
				    struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_mld_resume_data *resume_data = data;
	struct iwl_mld *mld =
		container_of(notif_wait, struct iwl_mld, notif_wait);

	switch (WIDE_ID(pkt->hdr.group_id, pkt->hdr.cmd)) {
	case WIDE_ID(PROT_OFFLOAD_GROUP, WOWLAN_INFO_NOTIFICATION): {
		if (resume_data->notifs_received & IWL_D3_NOTIF_WOWLAN_INFO) {
			IWL_DEBUG_WOWLAN(mld,
					 "got additional wowlan_info notif\n");
			break;
		}
		resume_data->notif_handling_err =
			iwl_mld_handle_wowlan_info_notif(mld,
							 resume_data->wowlan_status,
							 pkt);
		resume_data->notifs_received |= IWL_D3_NOTIF_WOWLAN_INFO;

		if (resume_data->wowlan_status->wakeup_reasons &
		    IWL_WOWLAN_WAKEUP_REASON_HAS_WAKEUP_PKT)
			resume_data->notifs_expected |=
				IWL_D3_NOTIF_WOWLAN_WAKE_PKT;
		break;
	}
	case WIDE_ID(PROT_OFFLOAD_GROUP, WOWLAN_WAKE_PKT_NOTIFICATION): {
		if (resume_data->notifs_received &
		    IWL_D3_NOTIF_WOWLAN_WAKE_PKT) {
			/* We shouldn't get two wake packet notifications */
			IWL_DEBUG_WOWLAN(mld,
					 "Got additional wowlan wake packet notification\n");
			break;
		}
		resume_data->notif_handling_err =
			iwl_mld_handle_wake_pkt_notif(mld,
						      resume_data->wowlan_status,
						      pkt);
		resume_data->notifs_received |= IWL_D3_NOTIF_WOWLAN_WAKE_PKT;
		break;
	}
	case WIDE_ID(SCAN_GROUP, OFFLOAD_MATCH_INFO_NOTIF): {
		if (resume_data->notifs_received & IWL_D3_ND_MATCH_INFO) {
			IWL_ERR(mld,
				"Got additional netdetect match info\n");
			break;
		}

		resume_data->notif_handling_err =
			iwl_mld_netdetect_match_info_handler(mld, resume_data,
							     pkt);
		resume_data->notifs_received |= IWL_D3_ND_MATCH_INFO;
		break;
	}
	case WIDE_ID(PROT_OFFLOAD_GROUP, D3_END_NOTIFICATION): {
		struct iwl_d3_end_notif *notif = (void *)pkt->data;

		resume_data->d3_end_flags = le32_to_cpu(notif->flags);
		resume_data->notifs_received |= IWL_D3_NOTIF_D3_END_NOTIF;
		break;
	}
	default:
		WARN_ON(1);
	}

	return resume_data->notifs_received == resume_data->notifs_expected;
}

#define IWL_MLD_D3_NOTIF_TIMEOUT (HZ / 3)

static int iwl_mld_wait_d3_notif(struct iwl_mld *mld,
				 struct iwl_mld_resume_data *resume_data,
				 bool with_wowlan)
{
	static const u16 wowlan_resume_notif[] = {
		WIDE_ID(PROT_OFFLOAD_GROUP, WOWLAN_INFO_NOTIFICATION),
		WIDE_ID(PROT_OFFLOAD_GROUP, WOWLAN_WAKE_PKT_NOTIFICATION),
		WIDE_ID(SCAN_GROUP, OFFLOAD_MATCH_INFO_NOTIF),
		WIDE_ID(PROT_OFFLOAD_GROUP, D3_END_NOTIFICATION)
	};
	static const u16 d3_resume_notif[] = {
		WIDE_ID(PROT_OFFLOAD_GROUP, D3_END_NOTIFICATION)
	};
	struct iwl_notification_wait wait_d3_notif;
	int ret;

	if (with_wowlan)
		iwl_init_notification_wait(&mld->notif_wait, &wait_d3_notif,
					   wowlan_resume_notif,
					   ARRAY_SIZE(wowlan_resume_notif),
					   iwl_mld_handle_d3_notif,
					   resume_data);
	else
		iwl_init_notification_wait(&mld->notif_wait, &wait_d3_notif,
					   d3_resume_notif,
					   ARRAY_SIZE(d3_resume_notif),
					   iwl_mld_handle_d3_notif,
					   resume_data);

	ret = iwl_trans_d3_resume(mld->trans, false);
	if (ret) {
		/* Avoid sending commands if the FW is dead */
		iwl_trans_notify_fw_error(mld->trans);
		iwl_remove_notification(&mld->notif_wait, &wait_d3_notif);
		return ret;
	}

	ret = iwl_wait_notification(&mld->notif_wait, &wait_d3_notif,
				    IWL_MLD_D3_NOTIF_TIMEOUT);
	if (ret)
		IWL_ERR(mld, "Couldn't get the d3 notif %d\n", ret);

	if (resume_data->notif_handling_err)
		ret = -EIO;

	return ret;
}

int iwl_mld_no_wowlan_suspend(struct iwl_mld *mld)
{
	struct iwl_d3_manager_config d3_cfg_cmd_data = {};
	int ret;

	if (mld->debug_max_sleep) {
		d3_cfg_cmd_data.wakeup_host_timer =
			cpu_to_le32(mld->debug_max_sleep);
		d3_cfg_cmd_data.wakeup_flags =
			cpu_to_le32(IWL_WAKEUP_D3_HOST_TIMER);
	}

	lockdep_assert_wiphy(mld->wiphy);

	IWL_DEBUG_WOWLAN(mld, "Starting the no wowlan suspend flow\n");

	iwl_mld_low_latency_stop(mld);

	ret = iwl_mld_update_device_power(mld, true);
	if (ret) {
		IWL_ERR(mld,
			"d3 suspend: couldn't send power_device %d\n", ret);
		return ret;
	}

	ret = iwl_mld_send_cmd_pdu(mld, D3_CONFIG_CMD,
				   &d3_cfg_cmd_data);
	if (ret) {
		IWL_ERR(mld,
			"d3 suspend: couldn't send D3_CONFIG_CMD %d\n", ret);
		return ret;
	}

	ret = iwl_trans_d3_suspend(mld->trans, false);
	if (ret) {
		IWL_ERR(mld, "d3 suspend: trans_d3_suspend failed %d\n", ret);
		/* We are going to stop the FW. Avoid sending commands in that flow */
		iwl_trans_notify_fw_error(mld->trans);
	} else {
		/* Async notification might send hcmds, which is not allowed in suspend */
		iwl_mld_cancel_async_notifications(mld);
		mld->fw_status.in_d3 = true;
	}

	return ret;
}

int iwl_mld_no_wowlan_resume(struct iwl_mld *mld)
{
	struct iwl_mld_resume_data resume_data = {
		.notifs_expected =
			IWL_D3_NOTIF_D3_END_NOTIF,
	};
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	IWL_DEBUG_WOWLAN(mld, "Starting the no wowlan resume flow\n");

	mld->fw_status.in_d3 = false;
	iwl_fw_dbg_read_d3_debug_data(&mld->fwrt);

	ret = iwl_mld_wait_d3_notif(mld, &resume_data, false);
	if (ret)
		return ret;

	if (!ret && (resume_data.d3_end_flags & IWL_D0I3_RESET_REQUIRE))
		return -ENODEV;

	iwl_mld_low_latency_restart(mld);

	return iwl_mld_update_device_power(mld, false);
}

static void
iwl_mld_aes_seq_to_le64_pn(struct ieee80211_key_conf *key,
			   __le64 *key_rsc)
{
	for (int i = 0; i < IWL_MAX_TID_COUNT; i++) {
		struct ieee80211_key_seq seq;
		u8 *pn = key->cipher == WLAN_CIPHER_SUITE_CCMP ? seq.ccmp.pn :
			seq.gcmp.pn;

		ieee80211_get_key_rx_seq(key, i, &seq);
		key_rsc[i] = cpu_to_le64((u64)pn[5] |
					 ((u64)pn[4] << 8) |
					 ((u64)pn[3] << 16) |
					 ((u64)pn[2] << 24) |
					 ((u64)pn[1] << 32) |
					 ((u64)pn[0] << 40));
	}
}

static void
iwl_mld_suspend_set_ucast_pn(struct iwl_mld *mld, struct ieee80211_sta *sta,
			     struct ieee80211_key_conf *key, __le64 *key_rsc)
{
	struct iwl_mld_sta *mld_sta =
		iwl_mld_sta_from_mac80211(sta);
	struct iwl_mld_ptk_pn *mld_ptk_pn;

	if (WARN_ON(key->keyidx >= ARRAY_SIZE(mld_sta->ptk_pn)))
		return;

	mld_ptk_pn = wiphy_dereference(mld->wiphy,
				       mld_sta->ptk_pn[key->keyidx]);
	if (WARN_ON(!mld_ptk_pn))
		return;

	for (int tid = 0; tid < IWL_MAX_TID_COUNT; tid++) {
		struct ieee80211_key_seq seq;
		u8 *max_pn = seq.ccmp.pn;

		/* get the PN from mac80211, used on the default queue */
		ieee80211_get_key_rx_seq(key, tid, &seq);

		/* and use the internal data for all queues */
		for (int que = 1; que < mld->trans->info.num_rxqs; que++) {
			u8 *cur_pn = mld_ptk_pn->q[que].pn[tid];

			if (memcmp(max_pn, cur_pn, IEEE80211_CCMP_PN_LEN) < 0)
				max_pn = cur_pn;
		}
		key_rsc[tid] = cpu_to_le64((u64)max_pn[5] |
					   ((u64)max_pn[4] << 8) |
					   ((u64)max_pn[3] << 16) |
					   ((u64)max_pn[2] << 24) |
					   ((u64)max_pn[1] << 32) |
					   ((u64)max_pn[0] << 40));
	}
}

static void
iwl_mld_suspend_convert_tkip_ipn(struct ieee80211_key_conf *key,
				 __le64 *rsc)
{
	struct ieee80211_key_seq seq;

	for (int i = 0; i < IWL_MAX_TID_COUNT; i++) {
		ieee80211_get_key_rx_seq(key, i, &seq);
		rsc[i] =
			cpu_to_le64(((u64)seq.tkip.iv32 << 16) |
				    seq.tkip.iv16);
	}
}

static void
iwl_mld_suspend_key_data_iter(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      struct ieee80211_key_conf *key,
			      void *_data)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_suspend_key_iter_data *data = _data;
	__le64 *key_rsc;
	__le32 cipher = 0;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		cipher = cpu_to_le32(STA_KEY_FLG_CCM);
		fallthrough;
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		if (!cipher)
			cipher = cpu_to_le32(STA_KEY_FLG_GCMP);
		fallthrough;
	case WLAN_CIPHER_SUITE_TKIP:
		if (!cipher)
			cipher = cpu_to_le32(STA_KEY_FLG_TKIP);
		if (sta) {
			key_rsc = data->rsc->ucast_rsc;
			if (key->cipher == WLAN_CIPHER_SUITE_TKIP)
				iwl_mld_suspend_convert_tkip_ipn(key, key_rsc);
			else
				iwl_mld_suspend_set_ucast_pn(mld, sta, key,
							     key_rsc);

			data->have_rsc = true;
			return;
		}
		/* We're iterating from old to new, there're 4 possible
		 * gtk ids, and only the last two keys matter
		 */
		if (WARN_ON(data->gtks >=
				ARRAY_SIZE(data->found_gtk_idx)))
			return;

		if (WARN_ON(key->keyidx >=
				ARRAY_SIZE(data->rsc->mcast_key_id_map)))
			return;
		data->gtk_cipher = cipher;
		data->found_gtk_idx[data->gtks] = key->keyidx;
		key_rsc = data->rsc->mcast_rsc[data->gtks % 2];
		data->rsc->mcast_key_id_map[key->keyidx] =
			data->gtks % 2;

		if (data->gtks >= 2) {
			int prev = data->gtks % 2;
			int prev_idx = data->found_gtk_idx[prev];

			data->rsc->mcast_key_id_map[prev_idx] =
				IWL_MCAST_KEY_MAP_INVALID;
		}

		if (key->cipher == WLAN_CIPHER_SUITE_TKIP)
			iwl_mld_suspend_convert_tkip_ipn(key, key_rsc);
		else
			iwl_mld_aes_seq_to_le64_pn(key, key_rsc);

		data->gtks++;
		data->have_rsc = true;
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		cipher = cpu_to_le32(STA_KEY_FLG_GCMP);
		fallthrough;
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	case WLAN_CIPHER_SUITE_AES_CMAC:
		if (!cipher)
			cipher = cpu_to_le32(STA_KEY_FLG_CCM);
		if (key->keyidx == 4 || key->keyidx == 5)
			data->igtk_cipher = cipher;

		if (key->keyidx == 6 || key->keyidx == 7)
			data->bigtk_cipher = cipher;

		break;
	}
}

static int
iwl_mld_send_kek_kck_cmd(struct iwl_mld *mld,
			 struct iwl_mld_vif *mld_vif,
			 struct iwl_mld_suspend_key_iter_data data,
			 int ap_sta_id)
{
	struct iwl_wowlan_kek_kck_material_cmd_v4 kek_kck_cmd = {};
	struct iwl_mld_rekey_data *rekey_data =
		&mld_vif->wowlan_data.rekey_data;

	memcpy(kek_kck_cmd.kck, rekey_data->kck,
	       rekey_data->kck_len);
	kek_kck_cmd.kck_len = cpu_to_le16(rekey_data->kck_len);
	memcpy(kek_kck_cmd.kek, rekey_data->kek,
	       rekey_data->kek_len);
	kek_kck_cmd.kek_len = cpu_to_le16(rekey_data->kek_len);
	kek_kck_cmd.replay_ctr = rekey_data->replay_ctr;
	kek_kck_cmd.akm = cpu_to_le32(rekey_data->akm);
	kek_kck_cmd.sta_id = cpu_to_le32(ap_sta_id);
	kek_kck_cmd.gtk_cipher = data.gtk_cipher;
	kek_kck_cmd.igtk_cipher = data.igtk_cipher;
	kek_kck_cmd.bigtk_cipher = data.bigtk_cipher;

	IWL_DEBUG_WOWLAN(mld, "setting akm %d\n",
			 rekey_data->akm);

	return iwl_mld_send_cmd_pdu(mld, WOWLAN_KEK_KCK_MATERIAL,
				    &kek_kck_cmd);
}

static int
iwl_mld_suspend_send_security_cmds(struct iwl_mld *mld,
				   struct ieee80211_vif *vif,
				   struct iwl_mld_vif *mld_vif,
				   int ap_sta_id)
{
	struct iwl_mld_suspend_key_iter_data data = {};
	int ret;

	data.rsc = kzalloc(sizeof(*data.rsc), GFP_KERNEL);
	if (!data.rsc)
		return -ENOMEM;

	memset(data.rsc->mcast_key_id_map, IWL_MCAST_KEY_MAP_INVALID,
	       ARRAY_SIZE(data.rsc->mcast_key_id_map));

	data.rsc->sta_id = cpu_to_le32(ap_sta_id);
	ieee80211_iter_keys(mld->hw, vif,
			    iwl_mld_suspend_key_data_iter,
			    &data);

	if (data.have_rsc)
		ret = iwl_mld_send_cmd_pdu(mld, WOWLAN_TSC_RSC_PARAM,
					   data.rsc);
	else
		ret = 0;

	if (!ret && mld_vif->wowlan_data.rekey_data.valid)
		ret = iwl_mld_send_kek_kck_cmd(mld, mld_vif, data, ap_sta_id);

	kfree(data.rsc);

	return ret;
}

static void
iwl_mld_set_wowlan_config_cmd(struct iwl_mld *mld,
			      struct cfg80211_wowlan *wowlan,
			      struct iwl_wowlan_config_cmd *wowlan_config_cmd,
			      struct ieee80211_sta *ap_sta,
			      struct ieee80211_bss_conf *link)
{
	wowlan_config_cmd->is_11n_connection =
					ap_sta->deflink.ht_cap.ht_supported;
	wowlan_config_cmd->flags = ENABLE_L3_FILTERING |
		ENABLE_NBNS_FILTERING | ENABLE_DHCP_FILTERING;

	if (ap_sta->mfp)
		wowlan_config_cmd->flags |= IS_11W_ASSOC;

	if (iwl_mld_beacon_protection_enabled(mld, link))
		wowlan_config_cmd->flags |= HAS_BEACON_PROTECTION;

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

	if (wowlan->any) {
		wowlan_config_cmd->wakeup_filter |=
			cpu_to_le32(IWL_WOWLAN_WAKEUP_BEACON_MISS |
				    IWL_WOWLAN_WAKEUP_LINK_CHANGE |
				    IWL_WOWLAN_WAKEUP_RX_FRAME |
				    IWL_WOWLAN_WAKEUP_BCN_FILTERING);
	}
}

static int iwl_mld_send_patterns(struct iwl_mld *mld,
				 struct cfg80211_wowlan *wowlan,
				 int ap_sta_id)
{
	struct iwl_wowlan_patterns_cmd *pattern_cmd;
	struct iwl_host_cmd cmd = {
		.id = WOWLAN_PATTERNS,
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
	};
	int ret;

	if (!wowlan->n_patterns)
		return 0;

	cmd.len[0] = struct_size(pattern_cmd, patterns, wowlan->n_patterns);

	pattern_cmd = kzalloc(cmd.len[0], GFP_KERNEL);
	if (!pattern_cmd)
		return -ENOMEM;

	pattern_cmd->n_patterns = wowlan->n_patterns;
	pattern_cmd->sta_id = ap_sta_id;

	for (int i = 0; i < wowlan->n_patterns; i++) {
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
	ret = iwl_mld_send_cmd(mld, &cmd);
	kfree(pattern_cmd);
	return ret;
}

static int
iwl_mld_send_proto_offload(struct iwl_mld *mld,
			   struct ieee80211_vif *vif,
			   u8 ap_sta_id)
{
	struct iwl_proto_offload_cmd_v4 *cmd __free(kfree);
	struct iwl_host_cmd hcmd = {
		.id = PROT_OFFLOAD_CONFIG_CMD,
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
		.len[0] = sizeof(*cmd),
	};
	u32 enabled = 0;

	cmd = kzalloc(hcmd.len[0], GFP_KERNEL);

#if IS_ENABLED(CONFIG_IPV6)
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_wowlan_data *wowlan_data = &mld_vif->wowlan_data;
	struct iwl_ns_config *nsc;
	struct iwl_targ_addr *addrs;
	int n_nsc, n_addrs;
	int i, c;
	int num_skipped = 0;

	nsc = cmd->ns_config;
	n_nsc = IWL_PROTO_OFFLOAD_NUM_NS_CONFIG_V3L;
	addrs = cmd->targ_addrs;
	n_addrs = IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V3L;

	/* For each address we have (and that will fit) fill a target
	 * address struct and combine for NS offload structs with the
	 * solicited node addresses.
	 */
	for (i = 0, c = 0;
		i < wowlan_data->num_target_ipv6_addrs &&
		i < n_addrs && c < n_nsc; i++) {
		int j;
		struct in6_addr solicited_addr;

		/* Because ns is offloaded skip tentative address to avoid
		 * violating RFC4862.
		 */
		if (test_bit(i, wowlan_data->tentative_addrs)) {
			num_skipped++;
			continue;
		}

		addrconf_addr_solict_mult(&wowlan_data->target_ipv6_addrs[i],
					  &solicited_addr);
		for (j = 0; j < n_nsc && j < c; j++)
			if (ipv6_addr_cmp(&nsc[j].dest_ipv6_addr,
					  &solicited_addr) == 0)
				break;
		if (j == c)
			c++;
		addrs[i].addr = wowlan_data->target_ipv6_addrs[i];
		addrs[i].config_num = cpu_to_le32(j);
		nsc[j].dest_ipv6_addr = solicited_addr;
		memcpy(nsc[j].target_mac_addr, vif->addr, ETH_ALEN);
	}

	if (wowlan_data->num_target_ipv6_addrs - num_skipped)
		enabled |= IWL_D3_PROTO_IPV6_VALID;

	cmd->num_valid_ipv6_addrs = cpu_to_le32(i - num_skipped);
	if (enabled & IWL_D3_PROTO_IPV6_VALID)
		enabled |= IWL_D3_PROTO_OFFLOAD_NS;
#endif

	if (vif->cfg.arp_addr_cnt) {
		enabled |= IWL_D3_PROTO_OFFLOAD_ARP | IWL_D3_PROTO_IPV4_VALID;
		cmd->common.host_ipv4_addr = vif->cfg.arp_addr_list[0];
		ether_addr_copy(cmd->common.arp_mac_addr, vif->addr);
	}

	enabled |= IWL_D3_PROTO_OFFLOAD_BTM;
	cmd->common.enabled = cpu_to_le32(enabled);
	cmd->sta_id = cpu_to_le32(ap_sta_id);
	hcmd.data[0] = cmd;
	return iwl_mld_send_cmd(mld, &hcmd);
}

static int
iwl_mld_wowlan_config(struct iwl_mld *mld, struct ieee80211_vif *bss_vif,
		      struct cfg80211_wowlan *wowlan)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(bss_vif);
	struct ieee80211_sta *ap_sta = mld_vif->ap_sta;
	struct iwl_wowlan_config_cmd wowlan_config_cmd = {
			.offloading_tid = IWL_WOWLAN_OFFLOAD_TID,
	};
	u32 sta_id_mask;
	int ap_sta_id, ret;
	int link_id = iwl_mld_get_primary_link(bss_vif);
	struct ieee80211_bss_conf *link_conf;

	ret = iwl_mld_block_emlsr_sync(mld, bss_vif,
				       IWL_MLD_EMLSR_BLOCKED_WOWLAN, link_id);
	if (ret)
		return ret;

	link_conf = link_conf_dereference_protected(bss_vif, link_id);

	if (WARN_ON(!ap_sta || !link_conf))
		return -EINVAL;

	sta_id_mask = iwl_mld_fw_sta_id_mask(mld, ap_sta);
	if (WARN_ON(hweight32(sta_id_mask) != 1))
		return -EINVAL;

	ap_sta_id = __ffs(sta_id_mask);
	wowlan_config_cmd.sta_id = ap_sta_id;

	ret = iwl_mld_ensure_queue(mld,
				   ap_sta->txq[wowlan_config_cmd.offloading_tid]);
	if (ret)
		return ret;

	iwl_mld_set_wowlan_config_cmd(mld, wowlan,
				      &wowlan_config_cmd, ap_sta, link_conf);
	ret = iwl_mld_send_cmd_pdu(mld, WOWLAN_CONFIGURATION,
				   &wowlan_config_cmd);
	if (ret)
		return ret;

	ret = iwl_mld_suspend_send_security_cmds(mld, bss_vif, mld_vif,
						 ap_sta_id);
	if (ret)
		return ret;

	ret = iwl_mld_send_patterns(mld, wowlan, ap_sta_id);
	if (ret)
		return ret;

	ret = iwl_mld_send_proto_offload(mld, bss_vif, ap_sta_id);
	if (ret)
		return ret;

	iwl_mld_enable_beacon_filter(mld, link_conf, true);
	return iwl_mld_update_mac_power(mld, bss_vif, true);
}

int iwl_mld_wowlan_suspend(struct iwl_mld *mld, struct cfg80211_wowlan *wowlan)
{
	struct ieee80211_vif *bss_vif;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(!wowlan))
		return 1;

	IWL_DEBUG_WOWLAN(mld, "Starting the wowlan suspend flow\n");

	bss_vif = iwl_mld_get_bss_vif(mld);
	if (WARN_ON(!bss_vif))
		return 1;

	if (!bss_vif->cfg.assoc) {
		int ret;
		/* If we're not associated, this must be netdetect */
		if (WARN_ON(!wowlan->nd_config))
			return 1;

		ret = iwl_mld_netdetect_config(mld, bss_vif, wowlan);
		if (!ret)
			mld->netdetect = true;

		return ret;
	}

	return iwl_mld_wowlan_config(mld, bss_vif, wowlan);
}

/* Returns 0 on success, 1 if an error occurred in firmware during d3,
 * A negative value is expected only in unrecovreable cases.
 */
int iwl_mld_wowlan_resume(struct iwl_mld *mld)
{
	struct ieee80211_vif *bss_vif;
	struct ieee80211_bss_conf *link_conf;
	struct iwl_mld_netdetect_res netdetect_res;
	struct iwl_mld_resume_data resume_data = {
		.notifs_expected =
			IWL_D3_NOTIF_WOWLAN_INFO |
			IWL_D3_NOTIF_D3_END_NOTIF,
		.netdetect_res = &netdetect_res,
	};
	int link_id;
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	IWL_DEBUG_WOWLAN(mld, "Starting the wowlan resume flow\n");

	if (!mld->fw_status.in_d3) {
		IWL_DEBUG_WOWLAN(mld,
				 "Device_powered_off() was called during wowlan\n");
		goto err;
	}

	mld->fw_status.resuming = true;
	mld->fw_status.in_d3 = false;
	mld->scan.last_start_time_jiffies = jiffies;

	bss_vif = iwl_mld_get_bss_vif(mld);
	if (WARN_ON(!bss_vif))
		goto err;

	/* We can't have several links upon wowlan entry,
	 * this is enforced in the suspend flow.
	 */
	WARN_ON(hweight16(bss_vif->active_links) > 1);
	link_id = bss_vif->active_links ? __ffs(bss_vif->active_links) : 0;
	link_conf = link_conf_dereference_protected(bss_vif, link_id);

	if (WARN_ON(!link_conf))
		goto err;

	iwl_fw_dbg_read_d3_debug_data(&mld->fwrt);

	resume_data.wowlan_status = kzalloc(sizeof(*resume_data.wowlan_status),
					    GFP_KERNEL);
	if (!resume_data.wowlan_status)
		return -ENOMEM;

	if (mld->netdetect)
		resume_data.notifs_expected |= IWL_D3_ND_MATCH_INFO;

	ret = iwl_mld_wait_d3_notif(mld, &resume_data, true);
	if (ret) {
		IWL_ERR(mld, "Couldn't get the d3 notifs %d\n", ret);
		goto err;
	}

	if (resume_data.d3_end_flags & IWL_D0I3_RESET_REQUIRE) {
		mld->fw_status.in_hw_restart = true;
		goto process_wakeup_results;
	}

	iwl_mld_update_changed_regdomain(mld);
	iwl_mld_update_mac_power(mld, bss_vif, false);
	iwl_mld_enable_beacon_filter(mld, link_conf, false);
	iwl_mld_update_device_power(mld, false);

	if (mld->netdetect)
		ret = iwl_mld_scan_stop(mld, IWL_MLD_SCAN_NETDETECT, false);

 process_wakeup_results:
	if (mld->netdetect) {
		iwl_mld_process_netdetect_res(mld, bss_vif, &resume_data);
		mld->netdetect = false;
	} else {
		bool keep_connection =
			iwl_mld_process_wowlan_status(mld, bss_vif,
						      resume_data.wowlan_status);

		/* EMLSR state will be cleared if the connection is not kept */
		if (keep_connection)
			iwl_mld_unblock_emlsr(mld, bss_vif,
					      IWL_MLD_EMLSR_BLOCKED_WOWLAN);
		else
			ieee80211_resume_disconnect(bss_vif);
	}

	goto out;

 err:
	mld->fw_status.in_hw_restart = true;
	ret = 1;
 out:
	mld->fw_status.resuming = false;

	if (resume_data.wowlan_status) {
		kfree(resume_data.wowlan_status->wake_packet);
		kfree(resume_data.wowlan_status);
	}

	return ret;
}
