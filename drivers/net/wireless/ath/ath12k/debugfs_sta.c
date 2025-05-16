// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/vmalloc.h>

#include "debugfs_sta.h"
#include "core.h"
#include "peer.h"
#include "debug.h"
#include "debugfs_htt_stats.h"
#include "debugfs.h"

static
u32 ath12k_dbg_sta_dump_rate_stats(u8 *buf, u32 offset, const int size,
				   bool he_rates_avail,
				   const struct ath12k_rx_peer_rate_stats *stats)
{
	static const char *legacy_rate_str[HAL_RX_MAX_NUM_LEGACY_RATES] = {
					"1 Mbps", "2 Mbps", "5.5 Mbps", "6 Mbps",
					"9 Mbps", "11 Mbps", "12 Mbps", "18 Mbps",
					"24 Mbps", "36 Mbps", "48 Mbps", "54 Mbps"};
	u8 max_bw = HAL_RX_BW_MAX, max_gi = HAL_RX_GI_MAX, max_mcs = HAL_RX_MAX_NSS;
	int mcs = 0, bw = 0, nss = 0, gi = 0, bw_num = 0;
	u32 i, len = offset, max = max_bw * max_gi * max_mcs;
	bool found;

	len += scnprintf(buf + len, size - len, "\nEHT stats:\n");
	for (i = 0; i <= HAL_RX_MAX_MCS_BE; i++)
		len += scnprintf(buf + len, size - len,
				   "MCS %d: %llu%s", i, stats->be_mcs_count[i],
				   (i + 1) % 8 ? "\t" : "\n");

	len += scnprintf(buf + len, size - len, "\nHE stats:\n");
	for (i = 0; i <= HAL_RX_MAX_MCS_HE; i++)
		len += scnprintf(buf + len, size - len,
				   "MCS %d: %llu%s", i, stats->he_mcs_count[i],
				   (i + 1) % 6 ? "\t" : "\n");

	len += scnprintf(buf + len, size - len, "\nVHT stats:\n");
	for (i = 0; i <= HAL_RX_MAX_MCS_VHT; i++)
		len += scnprintf(buf + len, size - len,
				   "MCS %d: %llu%s", i, stats->vht_mcs_count[i],
				   (i + 1) % 5 ? "\t" : "\n");

	len += scnprintf(buf + len, size - len, "\nHT stats:\n");
	for (i = 0; i <= HAL_RX_MAX_MCS_HT; i++)
		len += scnprintf(buf + len, size - len,
				   "MCS %d: %llu%s", i, stats->ht_mcs_count[i],
				   (i + 1) % 8 ? "\t" : "\n");

	len += scnprintf(buf + len, size - len, "\nLegacy stats:\n");
	for (i = 0; i < HAL_RX_MAX_NUM_LEGACY_RATES; i++)
		len += scnprintf(buf + len, size - len,
				   "%s: %llu%s", legacy_rate_str[i],
				   stats->legacy_count[i],
				   (i + 1) % 4 ? "\t" : "\n");

	len += scnprintf(buf + len, size - len, "\nNSS stats:\n");
	for (i = 0; i < HAL_RX_MAX_NSS; i++)
		len += scnprintf(buf + len, size - len,
				   "%dx%d: %llu ", i + 1, i + 1,
				   stats->nss_count[i]);

	len += scnprintf(buf + len, size - len,
			  "\n\nGI: 0.8 us %llu 0.4 us %llu 1.6 us %llu 3.2 us %llu\n",
			  stats->gi_count[0],
			  stats->gi_count[1],
			  stats->gi_count[2],
			  stats->gi_count[3]);

	len += scnprintf(buf + len, size - len,
			   "BW: 20 MHz %llu 40 MHz %llu 80 MHz %llu 160 MHz %llu 320 MHz %llu\n",
			   stats->bw_count[0],
			   stats->bw_count[1],
			   stats->bw_count[2],
			   stats->bw_count[3],
			   stats->bw_count[4]);

	for (i = 0; i < max; i++) {
		found = false;

		for (mcs = 0; mcs <= HAL_RX_MAX_MCS_HT; mcs++) {
			if (stats->rx_rate[bw][gi][nss][mcs]) {
				found = true;
				break;
			}
		}

		if (!found)
			goto skip_report;

		switch (bw) {
		case HAL_RX_BW_20MHZ:
			bw_num = 20;
			break;
		case HAL_RX_BW_40MHZ:
			bw_num = 40;
			break;
		case HAL_RX_BW_80MHZ:
			bw_num = 80;
			break;
		case HAL_RX_BW_160MHZ:
			bw_num = 160;
			break;
		case HAL_RX_BW_320MHZ:
			bw_num = 320;
			break;
		}

		len += scnprintf(buf + len, size - len, "\n%d Mhz gi %d us %dx%d : ",
				 bw_num, gi, nss + 1, nss + 1);

		for (mcs = 0; mcs <= HAL_RX_MAX_MCS_HT; mcs++) {
			if (stats->rx_rate[bw][gi][nss][mcs])
				len += scnprintf(buf + len, size - len,
						 " %d:%llu", mcs,
						 stats->rx_rate[bw][gi][nss][mcs]);
		}

skip_report:
		if (nss++ >= max_mcs - 1) {
			nss = 0;
			if (gi++ >= max_gi - 1) {
				gi = 0;
				if (bw < max_bw - 1)
					bw++;
			}
		}
	}

	len += scnprintf(buf + len, size - len, "\n");

	return len - offset;
}

static ssize_t ath12k_dbg_sta_dump_rx_stats(struct file *file,
					    char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	struct ieee80211_link_sta *link_sta = file->private_data;
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(link_sta->sta);
	const int size = ATH12K_STA_RX_STATS_BUF_SIZE;
	struct ath12k_hw *ah = ahsta->ahvif->ah;
	struct ath12k_rx_peer_stats *rx_stats;
	struct ath12k_link_sta *arsta;
	u8 link_id = link_sta->link_id;
	int len = 0, i, ret = 0;
	bool he_rates_avail;
	struct ath12k *ar;

	wiphy_lock(ah->hw->wiphy);

	if (!(BIT(link_id) & ahsta->links_map)) {
		wiphy_unlock(ah->hw->wiphy);
		return -ENOENT;
	}

	arsta = wiphy_dereference(ah->hw->wiphy, ahsta->link[link_id]);
	if (!arsta || !arsta->arvif->ar) {
		wiphy_unlock(ah->hw->wiphy);
		return -ENOENT;
	}

	ar = arsta->arvif->ar;

	u8 *buf __free(kfree) = kzalloc(size, GFP_KERNEL);
	if (!buf) {
		ret = -ENOENT;
		goto out;
	}

	spin_lock_bh(&ar->ab->base_lock);

	rx_stats = arsta->rx_stats;
	if (!rx_stats) {
		ret = -ENOENT;
		goto unlock;
	}

	len += scnprintf(buf + len, size - len, "RX peer stats:\n\n");
	len += scnprintf(buf + len, size - len, "Num of MSDUs: %llu\n",
			 rx_stats->num_msdu);
	len += scnprintf(buf + len, size - len, "Num of MSDUs with TCP L4: %llu\n",
			 rx_stats->tcp_msdu_count);
	len += scnprintf(buf + len, size - len, "Num of MSDUs with UDP L4: %llu\n",
			 rx_stats->udp_msdu_count);
	len += scnprintf(buf + len, size - len, "Num of other MSDUs: %llu\n",
			 rx_stats->other_msdu_count);
	len += scnprintf(buf + len, size - len, "Num of MSDUs part of AMPDU: %llu\n",
			 rx_stats->ampdu_msdu_count);
	len += scnprintf(buf + len, size - len, "Num of MSDUs not part of AMPDU: %llu\n",
			 rx_stats->non_ampdu_msdu_count);
	len += scnprintf(buf + len, size - len, "Num of MSDUs using STBC: %llu\n",
			 rx_stats->stbc_count);
	len += scnprintf(buf + len, size - len, "Num of MSDUs beamformed: %llu\n",
			 rx_stats->beamformed_count);
	len += scnprintf(buf + len, size - len, "Num of MPDUs with FCS ok: %llu\n",
			 rx_stats->num_mpdu_fcs_ok);
	len += scnprintf(buf + len, size - len, "Num of MPDUs with FCS error: %llu\n",
			 rx_stats->num_mpdu_fcs_err);

	he_rates_avail = (rx_stats->pream_cnt[HAL_RX_PREAMBLE_11AX] > 1) ? true : false;

	len += scnprintf(buf + len, size - len,
			 "preamble: 11A %llu 11B %llu 11N %llu 11AC %llu 11AX %llu 11BE %llu\n",
			 rx_stats->pream_cnt[0], rx_stats->pream_cnt[1],
			 rx_stats->pream_cnt[2], rx_stats->pream_cnt[3],
			 rx_stats->pream_cnt[4], rx_stats->pream_cnt[6]);
	len += scnprintf(buf + len, size - len,
			 "reception type: SU %llu MU_MIMO %llu MU_OFDMA %llu MU_OFDMA_MIMO %llu\n",
			 rx_stats->reception_type[0], rx_stats->reception_type[1],
			 rx_stats->reception_type[2], rx_stats->reception_type[3]);

	len += scnprintf(buf + len, size - len, "TID(0-15) Legacy TID(16):");
	for (i = 0; i <= IEEE80211_NUM_TIDS; i++)
		len += scnprintf(buf + len, size - len, "%llu ", rx_stats->tid_count[i]);

	len += scnprintf(buf + len, size - len, "\nRX Duration:%llu\n",
			 rx_stats->rx_duration);

	len += scnprintf(buf + len, size - len,
			 "\nDCM: %llu\nRU26:  %llu\nRU52:  %llu\nRU106: %llu\nRU242: %llu\nRU484: %llu\nRU996: %llu\nRU996x2: %llu\n",
			 rx_stats->dcm_count, rx_stats->ru_alloc_cnt[0],
			 rx_stats->ru_alloc_cnt[1], rx_stats->ru_alloc_cnt[2],
			 rx_stats->ru_alloc_cnt[3], rx_stats->ru_alloc_cnt[4],
			 rx_stats->ru_alloc_cnt[5], rx_stats->ru_alloc_cnt[6]);

	len += scnprintf(buf + len, size - len, "\nRX success packet stats:\n");
	len += ath12k_dbg_sta_dump_rate_stats(buf, len, size, he_rates_avail,
					      &rx_stats->pkt_stats);

	len += scnprintf(buf + len, size - len, "\n");

	len += scnprintf(buf + len, size - len, "\nRX success byte stats:\n");
	len += ath12k_dbg_sta_dump_rate_stats(buf, len, size, he_rates_avail,
					      &rx_stats->byte_stats);

unlock:
	spin_unlock_bh(&ar->ab->base_lock);

	if (len)
		ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);
out:
	wiphy_unlock(ah->hw->wiphy);
	return ret;
}

static const struct file_operations fops_rx_stats = {
	.read = ath12k_dbg_sta_dump_rx_stats,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath12k_dbg_sta_reset_rx_stats(struct file *file,
					     const char __user *buf,
					     size_t count, loff_t *ppos)
{
	struct ieee80211_link_sta *link_sta = file->private_data;
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(link_sta->sta);
	struct ath12k_hw *ah = ahsta->ahvif->ah;
	struct ath12k_rx_peer_stats *rx_stats;
	struct ath12k_link_sta *arsta;
	u8 link_id = link_sta->link_id;
	struct ath12k *ar;
	bool reset;
	int ret;

	ret = kstrtobool_from_user(buf, count, &reset);
	if (ret)
		return ret;

	if (!reset)
		return -EINVAL;

	wiphy_lock(ah->hw->wiphy);

	if (!(BIT(link_id) & ahsta->links_map)) {
		ret = -ENOENT;
		goto out;
	}

	arsta = wiphy_dereference(ah->hw->wiphy, ahsta->link[link_id]);
	if (!arsta || !arsta->arvif->ar) {
		ret = -ENOENT;
		goto out;
	}

	ar = arsta->arvif->ar;

	spin_lock_bh(&ar->ab->base_lock);

	rx_stats = arsta->rx_stats;
	if (!rx_stats) {
		spin_unlock_bh(&ar->ab->base_lock);
		ret = -ENOENT;
		goto out;
	}

	memset(rx_stats, 0, sizeof(*rx_stats));
	spin_unlock_bh(&ar->ab->base_lock);

	ret = count;
out:
	wiphy_unlock(ah->hw->wiphy);
	return ret;
}

static const struct file_operations fops_reset_rx_stats = {
	.write = ath12k_dbg_sta_reset_rx_stats,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath12k_debugfs_link_sta_op_add(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_link_sta *link_sta,
				    struct dentry *dir)
{
	struct ath12k *ar;

	lockdep_assert_wiphy(hw->wiphy);

	ar = ath12k_get_ar_by_vif(hw, vif, link_sta->link_id);
	if (!ar)
		return;

	if (ath12k_debugfs_is_extd_rx_stats_enabled(ar)) {
		debugfs_create_file("rx_stats", 0400, dir, link_sta,
				    &fops_rx_stats);
		debugfs_create_file("reset_rx_stats", 0200, dir, link_sta,
				    &fops_reset_rx_stats);
	}
}
