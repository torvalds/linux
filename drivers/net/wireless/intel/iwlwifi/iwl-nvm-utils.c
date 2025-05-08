// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2005-2014, 2018-2021, 2023, 2025 Intel Corporation
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/export.h>
#include "iwl-drv.h"
#include "iwl-modparams.h"
#include "iwl-nvm-utils.h"

int iwl_init_sband_channels(struct iwl_nvm_data *data,
			    struct ieee80211_supported_band *sband,
			    int n_channels, enum nl80211_band band)
{
	struct ieee80211_channel *chan = &data->channels[0];
	int n = 0, idx = 0;

	while (idx < n_channels && chan->band != band)
		chan = &data->channels[++idx];

	sband->channels = &data->channels[idx];

	while (idx < n_channels && chan->band == band) {
		chan = &data->channels[++idx];
		n++;
	}

	sband->n_channels = n;

	return n;
}
IWL_EXPORT_SYMBOL(iwl_init_sband_channels);

#define MAX_BIT_RATE_40_MHZ	150 /* Mbps */
#define MAX_BIT_RATE_20_MHZ	72 /* Mbps */

void iwl_init_ht_hw_capab(struct iwl_trans *trans,
			  struct iwl_nvm_data *data,
			  struct ieee80211_sta_ht_cap *ht_info,
			  enum nl80211_band band,
			  u8 tx_chains, u8 rx_chains)
{
	const struct iwl_cfg *cfg = trans->cfg;
	int max_bit_rate = 0;

	tx_chains = hweight8(tx_chains);
	if (cfg->rx_with_siso_diversity)
		rx_chains = 1;
	else
		rx_chains = hweight8(rx_chains);

	if (!(data->sku_cap_11n_enable) ||
	    (iwlwifi_mod_params.disable_11n & IWL_DISABLE_HT_ALL) ||
	    /* there are no devices with HT but without HT40 entirely */
	    !cfg->ht_params.ht40_bands) {
		ht_info->ht_supported = false;
		return;
	}

	if (data->sku_cap_mimo_disabled)
		rx_chains = 1;

	ht_info->ht_supported = true;
	ht_info->cap = IEEE80211_HT_CAP_DSSSCCK40;

	if (cfg->ht_params.stbc) {
		ht_info->cap |= (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);

		if (tx_chains > 1)
			ht_info->cap |= IEEE80211_HT_CAP_TX_STBC;
	}

	if (cfg->ht_params.ldpc)
		ht_info->cap |= IEEE80211_HT_CAP_LDPC_CODING;

	if (trans->mac_cfg->mq_rx_supported ||
	    iwlwifi_mod_params.amsdu_size >= IWL_AMSDU_8K)
		ht_info->cap |= IEEE80211_HT_CAP_MAX_AMSDU;

	ht_info->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	ht_info->ampdu_density = IEEE80211_HT_MPDU_DENSITY_4;

	ht_info->mcs.rx_mask[0] = 0xFF;
	ht_info->mcs.rx_mask[1] = 0x00;
	ht_info->mcs.rx_mask[2] = 0x00;

	if (rx_chains >= 2)
		ht_info->mcs.rx_mask[1] = 0xFF;
	if (rx_chains >= 3)
		ht_info->mcs.rx_mask[2] = 0xFF;

	if (cfg->ht_params.ht_greenfield_support)
		ht_info->cap |= IEEE80211_HT_CAP_GRN_FLD;
	ht_info->cap |= IEEE80211_HT_CAP_SGI_20;

	max_bit_rate = MAX_BIT_RATE_20_MHZ;

	if (cfg->ht_params.ht40_bands & BIT(band)) {
		ht_info->cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
		ht_info->cap |= IEEE80211_HT_CAP_SGI_40;
		max_bit_rate = MAX_BIT_RATE_40_MHZ;
	}

	/* Highest supported Rx data rate */
	max_bit_rate *= rx_chains;
	WARN_ON(max_bit_rate & ~IEEE80211_HT_MCS_RX_HIGHEST_MASK);
	ht_info->mcs.rx_highest = cpu_to_le16(max_bit_rate);

	/* Tx MCS capabilities */
	ht_info->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
	if (tx_chains != rx_chains) {
		ht_info->mcs.tx_params |= IEEE80211_HT_MCS_TX_RX_DIFF;
		ht_info->mcs.tx_params |= ((tx_chains - 1) <<
				IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT);
	}
}
IWL_EXPORT_SYMBOL(iwl_init_ht_hw_capab);
