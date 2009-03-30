/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2009 Intel Corporation. All rights reserved.
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
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/etherdevice.h>
#include <net/mac80211.h>

#include "iwl-eeprom.h"
#include "iwl-dev.h" /* FIXME: remove */
#include "iwl-debug.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-rfkill.h"
#include "iwl-power.h"
#include "iwl-sta.h"


MODULE_DESCRIPTION("iwl core");
MODULE_VERSION(IWLWIFI_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT " " DRV_AUTHOR);
MODULE_LICENSE("GPL");

#define IWL_DECLARE_RATE_INFO(r, s, ip, in, rp, rn, pp, np)    \
	[IWL_RATE_##r##M_INDEX] = { IWL_RATE_##r##M_PLCP,      \
				    IWL_RATE_SISO_##s##M_PLCP, \
				    IWL_RATE_MIMO2_##s##M_PLCP,\
				    IWL_RATE_MIMO3_##s##M_PLCP,\
				    IWL_RATE_##r##M_IEEE,      \
				    IWL_RATE_##ip##M_INDEX,    \
				    IWL_RATE_##in##M_INDEX,    \
				    IWL_RATE_##rp##M_INDEX,    \
				    IWL_RATE_##rn##M_INDEX,    \
				    IWL_RATE_##pp##M_INDEX,    \
				    IWL_RATE_##np##M_INDEX }

/*
 * Parameter order:
 *   rate, ht rate, prev rate, next rate, prev tgg rate, next tgg rate
 *
 * If there isn't a valid next or previous rate then INV is used which
 * maps to IWL_RATE_INVALID
 *
 */
const struct iwl_rate_info iwl_rates[IWL_RATE_COUNT] = {
	IWL_DECLARE_RATE_INFO(1, INV, INV, 2, INV, 2, INV, 2),    /*  1mbps */
	IWL_DECLARE_RATE_INFO(2, INV, 1, 5, 1, 5, 1, 5),          /*  2mbps */
	IWL_DECLARE_RATE_INFO(5, INV, 2, 6, 2, 11, 2, 11),        /*5.5mbps */
	IWL_DECLARE_RATE_INFO(11, INV, 9, 12, 9, 12, 5, 18),      /* 11mbps */
	IWL_DECLARE_RATE_INFO(6, 6, 5, 9, 5, 11, 5, 11),        /*  6mbps */
	IWL_DECLARE_RATE_INFO(9, 6, 6, 11, 6, 11, 5, 11),       /*  9mbps */
	IWL_DECLARE_RATE_INFO(12, 12, 11, 18, 11, 18, 11, 18),   /* 12mbps */
	IWL_DECLARE_RATE_INFO(18, 18, 12, 24, 12, 24, 11, 24),   /* 18mbps */
	IWL_DECLARE_RATE_INFO(24, 24, 18, 36, 18, 36, 18, 36),   /* 24mbps */
	IWL_DECLARE_RATE_INFO(36, 36, 24, 48, 24, 48, 24, 48),   /* 36mbps */
	IWL_DECLARE_RATE_INFO(48, 48, 36, 54, 36, 54, 36, 54),   /* 48mbps */
	IWL_DECLARE_RATE_INFO(54, 54, 48, INV, 48, INV, 48, INV),/* 54mbps */
	IWL_DECLARE_RATE_INFO(60, 60, 48, INV, 48, INV, 48, INV),/* 60mbps */
	/* FIXME:RS:          ^^    should be INV (legacy) */
};
EXPORT_SYMBOL(iwl_rates);

/**
 * translate ucode response to mac80211 tx status control values
 */
void iwl_hwrate_to_tx_control(struct iwl_priv *priv, u32 rate_n_flags,
				  struct ieee80211_tx_info *info)
{
	int rate_index;
	struct ieee80211_tx_rate *r = &info->control.rates[0];

	info->antenna_sel_tx =
		((rate_n_flags & RATE_MCS_ANT_ABC_MSK) >> RATE_MCS_ANT_POS);
	if (rate_n_flags & RATE_MCS_HT_MSK)
		r->flags |= IEEE80211_TX_RC_MCS;
	if (rate_n_flags & RATE_MCS_GF_MSK)
		r->flags |= IEEE80211_TX_RC_GREEN_FIELD;
	if (rate_n_flags & RATE_MCS_FAT_MSK)
		r->flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;
	if (rate_n_flags & RATE_MCS_DUP_MSK)
		r->flags |= IEEE80211_TX_RC_DUP_DATA;
	if (rate_n_flags & RATE_MCS_SGI_MSK)
		r->flags |= IEEE80211_TX_RC_SHORT_GI;
	rate_index = iwl_hwrate_to_plcp_idx(rate_n_flags);
	if (info->band == IEEE80211_BAND_5GHZ)
		rate_index -= IWL_FIRST_OFDM_RATE;
	r->idx = rate_index;
}
EXPORT_SYMBOL(iwl_hwrate_to_tx_control);

int iwl_hwrate_to_plcp_idx(u32 rate_n_flags)
{
	int idx = 0;

	/* HT rate format */
	if (rate_n_flags & RATE_MCS_HT_MSK) {
		idx = (rate_n_flags & 0xff);

		if (idx >= IWL_RATE_MIMO3_6M_PLCP)
			idx = idx - IWL_RATE_MIMO3_6M_PLCP;
		else if (idx >= IWL_RATE_MIMO2_6M_PLCP)
			idx = idx - IWL_RATE_MIMO2_6M_PLCP;

		idx += IWL_FIRST_OFDM_RATE;
		/* skip 9M not supported in ht*/
		if (idx >= IWL_RATE_9M_INDEX)
			idx += 1;
		if ((idx >= IWL_FIRST_OFDM_RATE) && (idx <= IWL_LAST_OFDM_RATE))
			return idx;

	/* legacy rate format, search for match in table */
	} else {
		for (idx = 0; idx < ARRAY_SIZE(iwl_rates); idx++)
			if (iwl_rates[idx].plcp == (rate_n_flags & 0xFF))
				return idx;
	}

	return -1;
}
EXPORT_SYMBOL(iwl_hwrate_to_plcp_idx);

u8 iwl_toggle_tx_ant(struct iwl_priv *priv, u8 ant)
{
	int i;
	u8 ind = ant;
	for (i = 0; i < RATE_ANT_NUM - 1; i++) {
		ind = (ind + 1) < RATE_ANT_NUM ?  ind + 1 : 0;
		if (priv->hw_params.valid_tx_ant & BIT(ind))
			return ind;
	}
	return ant;
}

const u8 iwl_bcast_addr[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
EXPORT_SYMBOL(iwl_bcast_addr);


/* This function both allocates and initializes hw and priv. */
struct ieee80211_hw *iwl_alloc_all(struct iwl_cfg *cfg,
		struct ieee80211_ops *hw_ops)
{
	struct iwl_priv *priv;

	/* mac80211 allocates memory for this device instance, including
	 *   space for this driver's private structure */
	struct ieee80211_hw *hw =
		ieee80211_alloc_hw(sizeof(struct iwl_priv), hw_ops);
	if (hw == NULL) {
		printk(KERN_ERR "%s: Can not allocate network device\n",
		       cfg->name);
		goto out;
	}

	priv = hw->priv;
	priv->hw = hw;

out:
	return hw;
}
EXPORT_SYMBOL(iwl_alloc_all);

void iwl_hw_detect(struct iwl_priv *priv)
{
	priv->hw_rev = _iwl_read32(priv, CSR_HW_REV);
	priv->hw_wa_rev = _iwl_read32(priv, CSR_HW_REV_WA_REG);
	pci_read_config_byte(priv->pci_dev, PCI_REVISION_ID, &priv->rev_id);
}
EXPORT_SYMBOL(iwl_hw_detect);

int iwl_hw_nic_init(struct iwl_priv *priv)
{
	unsigned long flags;
	struct iwl_rx_queue *rxq = &priv->rxq;
	int ret;

	/* nic_init */
	spin_lock_irqsave(&priv->lock, flags);
	priv->cfg->ops->lib->apm_ops.init(priv);
	iwl_write32(priv, CSR_INT_COALESCING, 512 / 32);
	spin_unlock_irqrestore(&priv->lock, flags);

	ret = priv->cfg->ops->lib->apm_ops.set_pwr_src(priv, IWL_PWR_SRC_VMAIN);

	priv->cfg->ops->lib->apm_ops.config(priv);

	/* Allocate the RX queue, or reset if it is already allocated */
	if (!rxq->bd) {
		ret = iwl_rx_queue_alloc(priv);
		if (ret) {
			IWL_ERR(priv, "Unable to initialize Rx queue\n");
			return -ENOMEM;
		}
	} else
		iwl_rx_queue_reset(priv, rxq);

	iwl_rx_replenish(priv);

	iwl_rx_init(priv, rxq);

	spin_lock_irqsave(&priv->lock, flags);

	rxq->need_update = 1;
	iwl_rx_queue_update_write_ptr(priv, rxq);

	spin_unlock_irqrestore(&priv->lock, flags);

	/* Allocate and init all Tx and Command queues */
	ret = iwl_txq_ctx_reset(priv);
	if (ret)
		return ret;

	set_bit(STATUS_INIT, &priv->status);

	return 0;
}
EXPORT_SYMBOL(iwl_hw_nic_init);

/*
 * QoS  support
*/
void iwl_activate_qos(struct iwl_priv *priv, u8 force)
{
	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	priv->qos_data.def_qos_parm.qos_flags = 0;

	if (priv->qos_data.qos_cap.q_AP.queue_request &&
	    !priv->qos_data.qos_cap.q_AP.txop_request)
		priv->qos_data.def_qos_parm.qos_flags |=
			QOS_PARAM_FLG_TXOP_TYPE_MSK;
	if (priv->qos_data.qos_active)
		priv->qos_data.def_qos_parm.qos_flags |=
			QOS_PARAM_FLG_UPDATE_EDCA_MSK;

	if (priv->current_ht_config.is_ht)
		priv->qos_data.def_qos_parm.qos_flags |= QOS_PARAM_FLG_TGN_MSK;

	if (force || iwl_is_associated(priv)) {
		IWL_DEBUG_QOS(priv, "send QoS cmd with Qos active=%d FLAGS=0x%X\n",
				priv->qos_data.qos_active,
				priv->qos_data.def_qos_parm.qos_flags);

		iwl_send_cmd_pdu_async(priv, REPLY_QOS_PARAM,
				       sizeof(struct iwl_qosparam_cmd),
				       &priv->qos_data.def_qos_parm, NULL);
	}
}
EXPORT_SYMBOL(iwl_activate_qos);

void iwl_reset_qos(struct iwl_priv *priv)
{
	u16 cw_min = 15;
	u16 cw_max = 1023;
	u8 aifs = 2;
	bool is_legacy = false;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&priv->lock, flags);
	/* QoS always active in AP and ADHOC mode
	 * In STA mode wait for association
	 */
	if (priv->iw_mode == NL80211_IFTYPE_ADHOC ||
	    priv->iw_mode == NL80211_IFTYPE_AP)
		priv->qos_data.qos_active = 1;
	else
		priv->qos_data.qos_active = 0;

	/* check for legacy mode */
	if ((priv->iw_mode == NL80211_IFTYPE_ADHOC &&
	    (priv->active_rate & IWL_OFDM_RATES_MASK) == 0) ||
	    (priv->iw_mode == NL80211_IFTYPE_STATION &&
	    (priv->staging_rxon.flags & RXON_FLG_SHORT_SLOT_MSK) == 0)) {
		cw_min = 31;
		is_legacy = 1;
	}

	if (priv->qos_data.qos_active)
		aifs = 3;

	priv->qos_data.def_qos_parm.ac[0].cw_min = cpu_to_le16(cw_min);
	priv->qos_data.def_qos_parm.ac[0].cw_max = cpu_to_le16(cw_max);
	priv->qos_data.def_qos_parm.ac[0].aifsn = aifs;
	priv->qos_data.def_qos_parm.ac[0].edca_txop = 0;
	priv->qos_data.def_qos_parm.ac[0].reserved1 = 0;

	if (priv->qos_data.qos_active) {
		i = 1;
		priv->qos_data.def_qos_parm.ac[i].cw_min = cpu_to_le16(cw_min);
		priv->qos_data.def_qos_parm.ac[i].cw_max = cpu_to_le16(cw_max);
		priv->qos_data.def_qos_parm.ac[i].aifsn = 7;
		priv->qos_data.def_qos_parm.ac[i].edca_txop = 0;
		priv->qos_data.def_qos_parm.ac[i].reserved1 = 0;

		i = 2;
		priv->qos_data.def_qos_parm.ac[i].cw_min =
			cpu_to_le16((cw_min + 1) / 2 - 1);
		priv->qos_data.def_qos_parm.ac[i].cw_max =
			cpu_to_le16(cw_max);
		priv->qos_data.def_qos_parm.ac[i].aifsn = 2;
		if (is_legacy)
			priv->qos_data.def_qos_parm.ac[i].edca_txop =
				cpu_to_le16(6016);
		else
			priv->qos_data.def_qos_parm.ac[i].edca_txop =
				cpu_to_le16(3008);
		priv->qos_data.def_qos_parm.ac[i].reserved1 = 0;

		i = 3;
		priv->qos_data.def_qos_parm.ac[i].cw_min =
			cpu_to_le16((cw_min + 1) / 4 - 1);
		priv->qos_data.def_qos_parm.ac[i].cw_max =
			cpu_to_le16((cw_max + 1) / 2 - 1);
		priv->qos_data.def_qos_parm.ac[i].aifsn = 2;
		priv->qos_data.def_qos_parm.ac[i].reserved1 = 0;
		if (is_legacy)
			priv->qos_data.def_qos_parm.ac[i].edca_txop =
				cpu_to_le16(3264);
		else
			priv->qos_data.def_qos_parm.ac[i].edca_txop =
				cpu_to_le16(1504);
	} else {
		for (i = 1; i < 4; i++) {
			priv->qos_data.def_qos_parm.ac[i].cw_min =
				cpu_to_le16(cw_min);
			priv->qos_data.def_qos_parm.ac[i].cw_max =
				cpu_to_le16(cw_max);
			priv->qos_data.def_qos_parm.ac[i].aifsn = aifs;
			priv->qos_data.def_qos_parm.ac[i].edca_txop = 0;
			priv->qos_data.def_qos_parm.ac[i].reserved1 = 0;
		}
	}
	IWL_DEBUG_QOS(priv, "set QoS to default \n");

	spin_unlock_irqrestore(&priv->lock, flags);
}
EXPORT_SYMBOL(iwl_reset_qos);

#define MAX_BIT_RATE_40_MHZ 150 /* Mbps */
#define MAX_BIT_RATE_20_MHZ 72 /* Mbps */
static void iwlcore_init_ht_hw_capab(const struct iwl_priv *priv,
			      struct ieee80211_sta_ht_cap *ht_info,
			      enum ieee80211_band band)
{
	u16 max_bit_rate = 0;
	u8 rx_chains_num = priv->hw_params.rx_chains_num;
	u8 tx_chains_num = priv->hw_params.tx_chains_num;

	ht_info->cap = 0;
	memset(&ht_info->mcs, 0, sizeof(ht_info->mcs));

	ht_info->ht_supported = true;

	ht_info->cap |= IEEE80211_HT_CAP_GRN_FLD;
	ht_info->cap |= IEEE80211_HT_CAP_SGI_20;
	ht_info->cap |= (IEEE80211_HT_CAP_SM_PS &
			     (WLAN_HT_CAP_SM_PS_DISABLED << 2));

	max_bit_rate = MAX_BIT_RATE_20_MHZ;
	if (priv->hw_params.fat_channel & BIT(band)) {
		ht_info->cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
		ht_info->cap |= IEEE80211_HT_CAP_SGI_40;
		ht_info->mcs.rx_mask[4] = 0x01;
		max_bit_rate = MAX_BIT_RATE_40_MHZ;
	}

	if (priv->cfg->mod_params->amsdu_size_8K)
		ht_info->cap |= IEEE80211_HT_CAP_MAX_AMSDU;

	ht_info->ampdu_factor = CFG_HT_RX_AMPDU_FACTOR_DEF;
	ht_info->ampdu_density = CFG_HT_MPDU_DENSITY_DEF;

	ht_info->mcs.rx_mask[0] = 0xFF;
	if (rx_chains_num >= 2)
		ht_info->mcs.rx_mask[1] = 0xFF;
	if (rx_chains_num >= 3)
		ht_info->mcs.rx_mask[2] = 0xFF;

	/* Highest supported Rx data rate */
	max_bit_rate *= rx_chains_num;
	WARN_ON(max_bit_rate & ~IEEE80211_HT_MCS_RX_HIGHEST_MASK);
	ht_info->mcs.rx_highest = cpu_to_le16(max_bit_rate);

	/* Tx MCS capabilities */
	ht_info->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
	if (tx_chains_num != rx_chains_num) {
		ht_info->mcs.tx_params |= IEEE80211_HT_MCS_TX_RX_DIFF;
		ht_info->mcs.tx_params |= ((tx_chains_num - 1) <<
				IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT);
	}
}

static void iwlcore_init_hw_rates(struct iwl_priv *priv,
			      struct ieee80211_rate *rates)
{
	int i;

	for (i = 0; i < IWL_RATE_COUNT; i++) {
		rates[i].bitrate = iwl_rates[i].ieee * 5;
		rates[i].hw_value = i; /* Rate scaling will work on indexes */
		rates[i].hw_value_short = i;
		rates[i].flags = 0;
		if ((i > IWL_LAST_OFDM_RATE) || (i < IWL_FIRST_OFDM_RATE)) {
			/*
			 * If CCK != 1M then set short preamble rate flag.
			 */
			rates[i].flags |=
				(iwl_rates[i].plcp == IWL_RATE_1M_PLCP) ?
					0 : IEEE80211_RATE_SHORT_PREAMBLE;
		}
	}
}


/**
 * iwlcore_init_geos - Initialize mac80211's geo/channel info based from eeprom
 */
int iwlcore_init_geos(struct iwl_priv *priv)
{
	struct iwl_channel_info *ch;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *channels;
	struct ieee80211_channel *geo_ch;
	struct ieee80211_rate *rates;
	int i = 0;

	if (priv->bands[IEEE80211_BAND_2GHZ].n_bitrates ||
	    priv->bands[IEEE80211_BAND_5GHZ].n_bitrates) {
		IWL_DEBUG_INFO(priv, "Geography modes already initialized.\n");
		set_bit(STATUS_GEO_CONFIGURED, &priv->status);
		return 0;
	}

	channels = kzalloc(sizeof(struct ieee80211_channel) *
			   priv->channel_count, GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	rates = kzalloc((sizeof(struct ieee80211_rate) * (IWL_RATE_COUNT + 1)),
			GFP_KERNEL);
	if (!rates) {
		kfree(channels);
		return -ENOMEM;
	}

	/* 5.2GHz channels start after the 2.4GHz channels */
	sband = &priv->bands[IEEE80211_BAND_5GHZ];
	sband->channels = &channels[ARRAY_SIZE(iwl_eeprom_band_1)];
	/* just OFDM */
	sband->bitrates = &rates[IWL_FIRST_OFDM_RATE];
	sband->n_bitrates = IWL_RATE_COUNT - IWL_FIRST_OFDM_RATE;

	if (priv->cfg->sku & IWL_SKU_N)
		iwlcore_init_ht_hw_capab(priv, &sband->ht_cap,
					 IEEE80211_BAND_5GHZ);

	sband = &priv->bands[IEEE80211_BAND_2GHZ];
	sband->channels = channels;
	/* OFDM & CCK */
	sband->bitrates = rates;
	sband->n_bitrates = IWL_RATE_COUNT;

	if (priv->cfg->sku & IWL_SKU_N)
		iwlcore_init_ht_hw_capab(priv, &sband->ht_cap,
					 IEEE80211_BAND_2GHZ);

	priv->ieee_channels = channels;
	priv->ieee_rates = rates;

	for (i = 0;  i < priv->channel_count; i++) {
		ch = &priv->channel_info[i];

		/* FIXME: might be removed if scan is OK */
		if (!is_channel_valid(ch))
			continue;

		if (is_channel_a_band(ch))
			sband =  &priv->bands[IEEE80211_BAND_5GHZ];
		else
			sband =  &priv->bands[IEEE80211_BAND_2GHZ];

		geo_ch = &sband->channels[sband->n_channels++];

		geo_ch->center_freq =
				ieee80211_channel_to_frequency(ch->channel);
		geo_ch->max_power = ch->max_power_avg;
		geo_ch->max_antenna_gain = 0xff;
		geo_ch->hw_value = ch->channel;

		if (is_channel_valid(ch)) {
			if (!(ch->flags & EEPROM_CHANNEL_IBSS))
				geo_ch->flags |= IEEE80211_CHAN_NO_IBSS;

			if (!(ch->flags & EEPROM_CHANNEL_ACTIVE))
				geo_ch->flags |= IEEE80211_CHAN_PASSIVE_SCAN;

			if (ch->flags & EEPROM_CHANNEL_RADAR)
				geo_ch->flags |= IEEE80211_CHAN_RADAR;

			geo_ch->flags |= ch->fat_extension_channel;

			if (ch->max_power_avg > priv->tx_power_channel_lmt)
				priv->tx_power_channel_lmt = ch->max_power_avg;
		} else {
			geo_ch->flags |= IEEE80211_CHAN_DISABLED;
		}

		/* Save flags for reg domain usage */
		geo_ch->orig_flags = geo_ch->flags;

		IWL_DEBUG_INFO(priv, "Channel %d Freq=%d[%sGHz] %s flag=0x%X\n",
				ch->channel, geo_ch->center_freq,
				is_channel_a_band(ch) ?  "5.2" : "2.4",
				geo_ch->flags & IEEE80211_CHAN_DISABLED ?
				"restricted" : "valid",
				 geo_ch->flags);
	}

	if ((priv->bands[IEEE80211_BAND_5GHZ].n_channels == 0) &&
	     priv->cfg->sku & IWL_SKU_A) {
		IWL_INFO(priv, "Incorrectly detected BG card as ABG. "
			"Please send your PCI ID 0x%04X:0x%04X to maintainer.\n",
			   priv->pci_dev->device,
			   priv->pci_dev->subsystem_device);
		priv->cfg->sku &= ~IWL_SKU_A;
	}

	IWL_INFO(priv, "Tunable channels: %d 802.11bg, %d 802.11a channels\n",
		   priv->bands[IEEE80211_BAND_2GHZ].n_channels,
		   priv->bands[IEEE80211_BAND_5GHZ].n_channels);

	set_bit(STATUS_GEO_CONFIGURED, &priv->status);

	return 0;
}
EXPORT_SYMBOL(iwlcore_init_geos);

/*
 * iwlcore_free_geos - undo allocations in iwlcore_init_geos
 */
void iwlcore_free_geos(struct iwl_priv *priv)
{
	kfree(priv->ieee_channels);
	kfree(priv->ieee_rates);
	clear_bit(STATUS_GEO_CONFIGURED, &priv->status);
}
EXPORT_SYMBOL(iwlcore_free_geos);

static bool is_single_rx_stream(struct iwl_priv *priv)
{
	return !priv->current_ht_config.is_ht ||
	       ((priv->current_ht_config.mcs.rx_mask[1] == 0) &&
		(priv->current_ht_config.mcs.rx_mask[2] == 0));
}

static u8 iwl_is_channel_extension(struct iwl_priv *priv,
				   enum ieee80211_band band,
				   u16 channel, u8 extension_chan_offset)
{
	const struct iwl_channel_info *ch_info;

	ch_info = iwl_get_channel_info(priv, band, channel);
	if (!is_channel_valid(ch_info))
		return 0;

	if (extension_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_ABOVE)
		return !(ch_info->fat_extension_channel &
					IEEE80211_CHAN_NO_FAT_ABOVE);
	else if (extension_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_BELOW)
		return !(ch_info->fat_extension_channel &
					IEEE80211_CHAN_NO_FAT_BELOW);

	return 0;
}

u8 iwl_is_fat_tx_allowed(struct iwl_priv *priv,
			 struct ieee80211_sta_ht_cap *sta_ht_inf)
{
	struct iwl_ht_info *iwl_ht_conf = &priv->current_ht_config;

	if ((!iwl_ht_conf->is_ht) ||
	   (iwl_ht_conf->supported_chan_width != IWL_CHANNEL_WIDTH_40MHZ) ||
	   (iwl_ht_conf->extension_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_NONE))
		return 0;

	if (sta_ht_inf) {
		if ((!sta_ht_inf->ht_supported) ||
		   (!(sta_ht_inf->cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40)))
			return 0;
	}

	return iwl_is_channel_extension(priv, priv->band,
					le16_to_cpu(priv->staging_rxon.channel),
					iwl_ht_conf->extension_chan_offset);
}
EXPORT_SYMBOL(iwl_is_fat_tx_allowed);

void iwl_set_rxon_hwcrypto(struct iwl_priv *priv, int hw_decrypt)
{
	struct iwl_rxon_cmd *rxon = &priv->staging_rxon;

	if (hw_decrypt)
		rxon->filter_flags &= ~RXON_FILTER_DIS_DECRYPT_MSK;
	else
		rxon->filter_flags |= RXON_FILTER_DIS_DECRYPT_MSK;

}
EXPORT_SYMBOL(iwl_set_rxon_hwcrypto);

/**
 * iwl_check_rxon_cmd - validate RXON structure is valid
 *
 * NOTE:  This is really only useful during development and can eventually
 * be #ifdef'd out once the driver is stable and folks aren't actively
 * making changes
 */
int iwl_check_rxon_cmd(struct iwl_priv *priv)
{
	int error = 0;
	int counter = 1;
	struct iwl_rxon_cmd *rxon = &priv->staging_rxon;

	if (rxon->flags & RXON_FLG_BAND_24G_MSK) {
		error |= le32_to_cpu(rxon->flags &
				(RXON_FLG_TGJ_NARROW_BAND_MSK |
				 RXON_FLG_RADAR_DETECT_MSK));
		if (error)
			IWL_WARN(priv, "check 24G fields %d | %d\n",
				    counter++, error);
	} else {
		error |= (rxon->flags & RXON_FLG_SHORT_SLOT_MSK) ?
				0 : le32_to_cpu(RXON_FLG_SHORT_SLOT_MSK);
		if (error)
			IWL_WARN(priv, "check 52 fields %d | %d\n",
				    counter++, error);
		error |= le32_to_cpu(rxon->flags & RXON_FLG_CCK_MSK);
		if (error)
			IWL_WARN(priv, "check 52 CCK %d | %d\n",
				    counter++, error);
	}
	error |= (rxon->node_addr[0] | rxon->bssid_addr[0]) & 0x1;
	if (error)
		IWL_WARN(priv, "check mac addr %d | %d\n", counter++, error);

	/* make sure basic rates 6Mbps and 1Mbps are supported */
	error |= (((rxon->ofdm_basic_rates & IWL_RATE_6M_MASK) == 0) &&
		  ((rxon->cck_basic_rates & IWL_RATE_1M_MASK) == 0));
	if (error)
		IWL_WARN(priv, "check basic rate %d | %d\n", counter++, error);

	error |= (le16_to_cpu(rxon->assoc_id) > 2007);
	if (error)
		IWL_WARN(priv, "check assoc id %d | %d\n", counter++, error);

	error |= ((rxon->flags & (RXON_FLG_CCK_MSK | RXON_FLG_SHORT_SLOT_MSK))
			== (RXON_FLG_CCK_MSK | RXON_FLG_SHORT_SLOT_MSK));
	if (error)
		IWL_WARN(priv, "check CCK and short slot %d | %d\n",
			    counter++, error);

	error |= ((rxon->flags & (RXON_FLG_CCK_MSK | RXON_FLG_AUTO_DETECT_MSK))
			== (RXON_FLG_CCK_MSK | RXON_FLG_AUTO_DETECT_MSK));
	if (error)
		IWL_WARN(priv, "check CCK & auto detect %d | %d\n",
			    counter++, error);

	error |= ((rxon->flags & (RXON_FLG_AUTO_DETECT_MSK |
			RXON_FLG_TGG_PROTECT_MSK)) == RXON_FLG_TGG_PROTECT_MSK);
	if (error)
		IWL_WARN(priv, "check TGG and auto detect %d | %d\n",
			    counter++, error);

	if (error)
		IWL_WARN(priv, "Tuning to channel %d\n",
			    le16_to_cpu(rxon->channel));

	if (error) {
		IWL_ERR(priv, "Not a valid iwl_rxon_assoc_cmd field values\n");
		return -1;
	}
	return 0;
}
EXPORT_SYMBOL(iwl_check_rxon_cmd);

/**
 * iwl_full_rxon_required - check if full RXON (vs RXON_ASSOC) cmd is needed
 * @priv: staging_rxon is compared to active_rxon
 *
 * If the RXON structure is changing enough to require a new tune,
 * or is clearing the RXON_FILTER_ASSOC_MSK, then return 1 to indicate that
 * a new tune (full RXON command, rather than RXON_ASSOC cmd) is required.
 */
int iwl_full_rxon_required(struct iwl_priv *priv)
{

	/* These items are only settable from the full RXON command */
	if (!(iwl_is_associated(priv)) ||
	    compare_ether_addr(priv->staging_rxon.bssid_addr,
			       priv->active_rxon.bssid_addr) ||
	    compare_ether_addr(priv->staging_rxon.node_addr,
			       priv->active_rxon.node_addr) ||
	    compare_ether_addr(priv->staging_rxon.wlap_bssid_addr,
			       priv->active_rxon.wlap_bssid_addr) ||
	    (priv->staging_rxon.dev_type != priv->active_rxon.dev_type) ||
	    (priv->staging_rxon.channel != priv->active_rxon.channel) ||
	    (priv->staging_rxon.air_propagation !=
	     priv->active_rxon.air_propagation) ||
	    (priv->staging_rxon.ofdm_ht_single_stream_basic_rates !=
	     priv->active_rxon.ofdm_ht_single_stream_basic_rates) ||
	    (priv->staging_rxon.ofdm_ht_dual_stream_basic_rates !=
	     priv->active_rxon.ofdm_ht_dual_stream_basic_rates) ||
	    (priv->staging_rxon.assoc_id != priv->active_rxon.assoc_id))
		return 1;

	/* flags, filter_flags, ofdm_basic_rates, and cck_basic_rates can
	 * be updated with the RXON_ASSOC command -- however only some
	 * flag transitions are allowed using RXON_ASSOC */

	/* Check if we are not switching bands */
	if ((priv->staging_rxon.flags & RXON_FLG_BAND_24G_MSK) !=
	    (priv->active_rxon.flags & RXON_FLG_BAND_24G_MSK))
		return 1;

	/* Check if we are switching association toggle */
	if ((priv->staging_rxon.filter_flags & RXON_FILTER_ASSOC_MSK) !=
		(priv->active_rxon.filter_flags & RXON_FILTER_ASSOC_MSK))
		return 1;

	return 0;
}
EXPORT_SYMBOL(iwl_full_rxon_required);

u8 iwl_rate_get_lowest_plcp(struct iwl_priv *priv)
{
	int i;
	int rate_mask;

	/* Set rate mask*/
	if (priv->staging_rxon.flags & RXON_FLG_BAND_24G_MSK)
		rate_mask = priv->active_rate_basic & IWL_CCK_RATES_MASK;
	else
		rate_mask = priv->active_rate_basic & IWL_OFDM_RATES_MASK;

	/* Find lowest valid rate */
	for (i = IWL_RATE_1M_INDEX; i != IWL_RATE_INVALID;
					i = iwl_rates[i].next_ieee) {
		if (rate_mask & (1 << i))
			return iwl_rates[i].plcp;
	}

	/* No valid rate was found. Assign the lowest one */
	if (priv->staging_rxon.flags & RXON_FLG_BAND_24G_MSK)
		return IWL_RATE_1M_PLCP;
	else
		return IWL_RATE_6M_PLCP;
}
EXPORT_SYMBOL(iwl_rate_get_lowest_plcp);

void iwl_set_rxon_ht(struct iwl_priv *priv, struct iwl_ht_info *ht_info)
{
	struct iwl_rxon_cmd *rxon = &priv->staging_rxon;
	u32 val;

	if (!ht_info->is_ht) {
		rxon->flags &= ~(RXON_FLG_CHANNEL_MODE_MIXED_MSK |
			RXON_FLG_CHANNEL_MODE_PURE_40_MSK |
			RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK |
			RXON_FLG_FAT_PROT_MSK |
			RXON_FLG_HT_PROT_MSK);
		return;
	}

	/* Set up channel bandwidth:  20 MHz only, or 20/40 mixed if fat ok */
	if (iwl_is_fat_tx_allowed(priv, NULL))
		rxon->flags |= RXON_FLG_CHANNEL_MODE_MIXED_MSK;
	else
		rxon->flags &= ~(RXON_FLG_CHANNEL_MODE_MIXED_MSK |
				 RXON_FLG_CHANNEL_MODE_PURE_40_MSK);

	/* Note: control channel is opposite of extension channel */
	switch (ht_info->extension_chan_offset) {
	case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
		rxon->flags &= ~(RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK);
		break;
	case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
		rxon->flags |= RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK;
		break;
	case IEEE80211_HT_PARAM_CHA_SEC_NONE:
	default:
		rxon->flags &= ~RXON_FLG_CHANNEL_MODE_MIXED_MSK;
		break;
	}

	val = ht_info->ht_protection;

	rxon->flags |= cpu_to_le32(val << RXON_FLG_HT_OPERATING_MODE_POS);

	iwl_set_rxon_chain(priv);

	IWL_DEBUG_ASSOC(priv, "supported HT rate 0x%X 0x%X 0x%X "
			"rxon flags 0x%X operation mode :0x%X "
			"extension channel offset 0x%x\n",
			ht_info->mcs.rx_mask[0],
			ht_info->mcs.rx_mask[1],
			ht_info->mcs.rx_mask[2],
			le32_to_cpu(rxon->flags), ht_info->ht_protection,
			ht_info->extension_chan_offset);
	return;
}
EXPORT_SYMBOL(iwl_set_rxon_ht);

#define IWL_NUM_RX_CHAINS_MULTIPLE	3
#define IWL_NUM_RX_CHAINS_SINGLE	2
#define IWL_NUM_IDLE_CHAINS_DUAL	2
#define IWL_NUM_IDLE_CHAINS_SINGLE	1

/* Determine how many receiver/antenna chains to use.
 * More provides better reception via diversity.  Fewer saves power.
 * MIMO (dual stream) requires at least 2, but works better with 3.
 * This does not determine *which* chains to use, just how many.
 */
static int iwl_get_active_rx_chain_count(struct iwl_priv *priv)
{
	bool is_single = is_single_rx_stream(priv);
	bool is_cam = !test_bit(STATUS_POWER_PMI, &priv->status);

	/* # of Rx chains to use when expecting MIMO. */
	if (is_single || (!is_cam && (priv->current_ht_config.sm_ps ==
						 WLAN_HT_CAP_SM_PS_STATIC)))
		return IWL_NUM_RX_CHAINS_SINGLE;
	else
		return IWL_NUM_RX_CHAINS_MULTIPLE;
}

static int iwl_get_idle_rx_chain_count(struct iwl_priv *priv, int active_cnt)
{
	int idle_cnt;
	bool is_cam = !test_bit(STATUS_POWER_PMI, &priv->status);
	/* # Rx chains when idling and maybe trying to save power */
	switch (priv->current_ht_config.sm_ps) {
	case WLAN_HT_CAP_SM_PS_STATIC:
	case WLAN_HT_CAP_SM_PS_DYNAMIC:
		idle_cnt = (is_cam) ? IWL_NUM_IDLE_CHAINS_DUAL :
					IWL_NUM_IDLE_CHAINS_SINGLE;
		break;
	case WLAN_HT_CAP_SM_PS_DISABLED:
		idle_cnt = (is_cam) ? active_cnt : IWL_NUM_IDLE_CHAINS_SINGLE;
		break;
	case WLAN_HT_CAP_SM_PS_INVALID:
	default:
		IWL_ERR(priv, "invalid mimo ps mode %d\n",
			   priv->current_ht_config.sm_ps);
		WARN_ON(1);
		idle_cnt = -1;
		break;
	}
	return idle_cnt;
}

/* up to 4 chains */
static u8 iwl_count_chain_bitmap(u32 chain_bitmap)
{
	u8 res;
	res = (chain_bitmap & BIT(0)) >> 0;
	res += (chain_bitmap & BIT(1)) >> 1;
	res += (chain_bitmap & BIT(2)) >> 2;
	res += (chain_bitmap & BIT(4)) >> 4;
	return res;
}

/**
 * iwl_is_monitor_mode - Determine if interface in monitor mode
 *
 * priv->iw_mode is set in add_interface, but add_interface is
 * never called for monitor mode. The only way mac80211 informs us about
 * monitor mode is through configuring filters (call to configure_filter).
 */
static bool iwl_is_monitor_mode(struct iwl_priv *priv)
{
	return !!(priv->staging_rxon.filter_flags & RXON_FILTER_PROMISC_MSK);
}

/**
 * iwl_set_rxon_chain - Set up Rx chain usage in "staging" RXON image
 *
 * Selects how many and which Rx receivers/antennas/chains to use.
 * This should not be used for scan command ... it puts data in wrong place.
 */
void iwl_set_rxon_chain(struct iwl_priv *priv)
{
	bool is_single = is_single_rx_stream(priv);
	bool is_cam = !test_bit(STATUS_POWER_PMI, &priv->status);
	u8 idle_rx_cnt, active_rx_cnt, valid_rx_cnt;
	u32 active_chains;
	u16 rx_chain;

	/* Tell uCode which antennas are actually connected.
	 * Before first association, we assume all antennas are connected.
	 * Just after first association, iwl_chain_noise_calibration()
	 *    checks which antennas actually *are* connected. */
	 if (priv->chain_noise_data.active_chains)
		active_chains = priv->chain_noise_data.active_chains;
	else
		active_chains = priv->hw_params.valid_rx_ant;

	rx_chain = active_chains << RXON_RX_CHAIN_VALID_POS;

	/* How many receivers should we use? */
	active_rx_cnt = iwl_get_active_rx_chain_count(priv);
	idle_rx_cnt = iwl_get_idle_rx_chain_count(priv, active_rx_cnt);


	/* correct rx chain count according hw settings
	 * and chain noise calibration
	 */
	valid_rx_cnt = iwl_count_chain_bitmap(active_chains);
	if (valid_rx_cnt < active_rx_cnt)
		active_rx_cnt = valid_rx_cnt;

	if (valid_rx_cnt < idle_rx_cnt)
		idle_rx_cnt = valid_rx_cnt;

	rx_chain |= active_rx_cnt << RXON_RX_CHAIN_MIMO_CNT_POS;
	rx_chain |= idle_rx_cnt  << RXON_RX_CHAIN_CNT_POS;

	/* copied from 'iwl_bg_request_scan()' */
	/* Force use of chains B and C (0x6) for Rx for 4965
	 * Avoid A (0x1) because of its off-channel reception on A-band.
	 * MIMO is not used here, but value is required */
	if (iwl_is_monitor_mode(priv) &&
	    !(priv->staging_rxon.flags & RXON_FLG_BAND_24G_MSK) &&
	    ((priv->hw_rev & CSR_HW_REV_TYPE_MSK) == CSR_HW_REV_TYPE_4965)) {
		rx_chain = 0x07 << RXON_RX_CHAIN_VALID_POS;
		rx_chain |= 0x06 << RXON_RX_CHAIN_FORCE_SEL_POS;
		rx_chain |= 0x07 << RXON_RX_CHAIN_FORCE_MIMO_SEL_POS;
		rx_chain |= 0x01 << RXON_RX_CHAIN_DRIVER_FORCE_POS;
	}

	priv->staging_rxon.rx_chain = cpu_to_le16(rx_chain);

	if (!is_single && (active_rx_cnt >= IWL_NUM_RX_CHAINS_SINGLE) && is_cam)
		priv->staging_rxon.rx_chain |= RXON_RX_CHAIN_MIMO_FORCE_MSK;
	else
		priv->staging_rxon.rx_chain &= ~RXON_RX_CHAIN_MIMO_FORCE_MSK;

	IWL_DEBUG_ASSOC(priv, "rx_chain=0x%X active=%d idle=%d\n",
			priv->staging_rxon.rx_chain,
			active_rx_cnt, idle_rx_cnt);

	WARN_ON(active_rx_cnt == 0 || idle_rx_cnt == 0 ||
		active_rx_cnt < idle_rx_cnt);
}
EXPORT_SYMBOL(iwl_set_rxon_chain);

/**
 * iwl_set_rxon_channel - Set the phymode and channel values in staging RXON
 * @phymode: MODE_IEEE80211A sets to 5.2GHz; all else set to 2.4GHz
 * @channel: Any channel valid for the requested phymode

 * In addition to setting the staging RXON, priv->phymode is also set.
 *
 * NOTE:  Does not commit to the hardware; it sets appropriate bit fields
 * in the staging RXON flag structure based on the phymode
 */
int iwl_set_rxon_channel(struct iwl_priv *priv, struct ieee80211_channel *ch)
{
	enum ieee80211_band band = ch->band;
	u16 channel = ieee80211_frequency_to_channel(ch->center_freq);

	if (!iwl_get_channel_info(priv, band, channel)) {
		IWL_DEBUG_INFO(priv, "Could not set channel to %d [%d]\n",
			       channel, band);
		return -EINVAL;
	}

	if ((le16_to_cpu(priv->staging_rxon.channel) == channel) &&
	    (priv->band == band))
		return 0;

	priv->staging_rxon.channel = cpu_to_le16(channel);
	if (band == IEEE80211_BAND_5GHZ)
		priv->staging_rxon.flags &= ~RXON_FLG_BAND_24G_MSK;
	else
		priv->staging_rxon.flags |= RXON_FLG_BAND_24G_MSK;

	priv->band = band;

	IWL_DEBUG_INFO(priv, "Staging channel set to %d [%d]\n", channel, band);

	return 0;
}
EXPORT_SYMBOL(iwl_set_rxon_channel);

void iwl_set_flags_for_band(struct iwl_priv *priv,
			    enum ieee80211_band band)
{
	if (band == IEEE80211_BAND_5GHZ) {
		priv->staging_rxon.flags &=
		    ~(RXON_FLG_BAND_24G_MSK | RXON_FLG_AUTO_DETECT_MSK
		      | RXON_FLG_CCK_MSK);
		priv->staging_rxon.flags |= RXON_FLG_SHORT_SLOT_MSK;
	} else {
		/* Copied from iwl_post_associate() */
		if (priv->assoc_capability & WLAN_CAPABILITY_SHORT_SLOT_TIME)
			priv->staging_rxon.flags |= RXON_FLG_SHORT_SLOT_MSK;
		else
			priv->staging_rxon.flags &= ~RXON_FLG_SHORT_SLOT_MSK;

		if (priv->iw_mode == NL80211_IFTYPE_ADHOC)
			priv->staging_rxon.flags &= ~RXON_FLG_SHORT_SLOT_MSK;

		priv->staging_rxon.flags |= RXON_FLG_BAND_24G_MSK;
		priv->staging_rxon.flags |= RXON_FLG_AUTO_DETECT_MSK;
		priv->staging_rxon.flags &= ~RXON_FLG_CCK_MSK;
	}
}
EXPORT_SYMBOL(iwl_set_flags_for_band);

/*
 * initialize rxon structure with default values from eeprom
 */
void iwl_connection_init_rx_config(struct iwl_priv *priv, int mode)
{
	const struct iwl_channel_info *ch_info;

	memset(&priv->staging_rxon, 0, sizeof(priv->staging_rxon));

	switch (mode) {
	case NL80211_IFTYPE_AP:
		priv->staging_rxon.dev_type = RXON_DEV_TYPE_AP;
		break;

	case NL80211_IFTYPE_STATION:
		priv->staging_rxon.dev_type = RXON_DEV_TYPE_ESS;
		priv->staging_rxon.filter_flags = RXON_FILTER_ACCEPT_GRP_MSK;
		break;

	case NL80211_IFTYPE_ADHOC:
		priv->staging_rxon.dev_type = RXON_DEV_TYPE_IBSS;
		priv->staging_rxon.flags = RXON_FLG_SHORT_PREAMBLE_MSK;
		priv->staging_rxon.filter_flags = RXON_FILTER_BCON_AWARE_MSK |
						  RXON_FILTER_ACCEPT_GRP_MSK;
		break;

	case NL80211_IFTYPE_MONITOR:
		priv->staging_rxon.dev_type = RXON_DEV_TYPE_SNIFFER;
		priv->staging_rxon.filter_flags = RXON_FILTER_PROMISC_MSK |
		    RXON_FILTER_CTL2HOST_MSK | RXON_FILTER_ACCEPT_GRP_MSK;
		break;
	default:
		IWL_ERR(priv, "Unsupported interface type %d\n", mode);
		break;
	}

#if 0
	/* TODO:  Figure out when short_preamble would be set and cache from
	 * that */
	if (!hw_to_local(priv->hw)->short_preamble)
		priv->staging_rxon.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;
	else
		priv->staging_rxon.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
#endif

	ch_info = iwl_get_channel_info(priv, priv->band,
				       le16_to_cpu(priv->active_rxon.channel));

	if (!ch_info)
		ch_info = &priv->channel_info[0];

	/*
	 * in some case A channels are all non IBSS
	 * in this case force B/G channel
	 */
	if ((priv->iw_mode == NL80211_IFTYPE_ADHOC) &&
	    !(is_channel_ibss(ch_info)))
		ch_info = &priv->channel_info[0];

	priv->staging_rxon.channel = cpu_to_le16(ch_info->channel);
	priv->band = ch_info->band;

	iwl_set_flags_for_band(priv, priv->band);

	priv->staging_rxon.ofdm_basic_rates =
	    (IWL_OFDM_RATES_MASK >> IWL_FIRST_OFDM_RATE) & 0xFF;
	priv->staging_rxon.cck_basic_rates =
	    (IWL_CCK_RATES_MASK >> IWL_FIRST_CCK_RATE) & 0xF;

	priv->staging_rxon.flags &= ~(RXON_FLG_CHANNEL_MODE_MIXED_MSK |
					RXON_FLG_CHANNEL_MODE_PURE_40_MSK);
	memcpy(priv->staging_rxon.node_addr, priv->mac_addr, ETH_ALEN);
	memcpy(priv->staging_rxon.wlap_bssid_addr, priv->mac_addr, ETH_ALEN);
	priv->staging_rxon.ofdm_ht_single_stream_basic_rates = 0xff;
	priv->staging_rxon.ofdm_ht_dual_stream_basic_rates = 0xff;
}
EXPORT_SYMBOL(iwl_connection_init_rx_config);

void iwl_set_rate(struct iwl_priv *priv)
{
	const struct ieee80211_supported_band *hw = NULL;
	struct ieee80211_rate *rate;
	int i;

	hw = iwl_get_hw_mode(priv, priv->band);
	if (!hw) {
		IWL_ERR(priv, "Failed to set rate: unable to get hw mode\n");
		return;
	}

	priv->active_rate = 0;
	priv->active_rate_basic = 0;

	for (i = 0; i < hw->n_bitrates; i++) {
		rate = &(hw->bitrates[i]);
		if (rate->hw_value < IWL_RATE_COUNT)
			priv->active_rate |= (1 << rate->hw_value);
	}

	IWL_DEBUG_RATE(priv, "Set active_rate = %0x, active_rate_basic = %0x\n",
		       priv->active_rate, priv->active_rate_basic);

	/*
	 * If a basic rate is configured, then use it (adding IWL_RATE_1M_MASK)
	 * otherwise set it to the default of all CCK rates and 6, 12, 24 for
	 * OFDM
	 */
	if (priv->active_rate_basic & IWL_CCK_BASIC_RATES_MASK)
		priv->staging_rxon.cck_basic_rates =
		    ((priv->active_rate_basic &
		      IWL_CCK_RATES_MASK) >> IWL_FIRST_CCK_RATE) & 0xF;
	else
		priv->staging_rxon.cck_basic_rates =
		    (IWL_CCK_BASIC_RATES_MASK >> IWL_FIRST_CCK_RATE) & 0xF;

	if (priv->active_rate_basic & IWL_OFDM_BASIC_RATES_MASK)
		priv->staging_rxon.ofdm_basic_rates =
		    ((priv->active_rate_basic &
		      (IWL_OFDM_BASIC_RATES_MASK | IWL_RATE_6M_MASK)) >>
		      IWL_FIRST_OFDM_RATE) & 0xFF;
	else
		priv->staging_rxon.ofdm_basic_rates =
		   (IWL_OFDM_BASIC_RATES_MASK >> IWL_FIRST_OFDM_RATE) & 0xFF;
}
EXPORT_SYMBOL(iwl_set_rate);

void iwl_rx_csa(struct iwl_priv *priv, struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	struct iwl_rxon_cmd *rxon = (void *)&priv->active_rxon;
	struct iwl_csa_notification *csa = &(pkt->u.csa_notif);
	IWL_DEBUG_11H(priv, "CSA notif: channel %d, status %d\n",
		      le16_to_cpu(csa->channel), le32_to_cpu(csa->status));
	rxon->channel = csa->channel;
	priv->staging_rxon.channel = csa->channel;
}
EXPORT_SYMBOL(iwl_rx_csa);

#ifdef CONFIG_IWLWIFI_DEBUG
static void iwl_print_rx_config_cmd(struct iwl_priv *priv)
{
	struct iwl_rxon_cmd *rxon = &priv->staging_rxon;

	IWL_DEBUG_RADIO(priv, "RX CONFIG:\n");
	iwl_print_hex_dump(priv, IWL_DL_RADIO, (u8 *) rxon, sizeof(*rxon));
	IWL_DEBUG_RADIO(priv, "u16 channel: 0x%x\n", le16_to_cpu(rxon->channel));
	IWL_DEBUG_RADIO(priv, "u32 flags: 0x%08X\n", le32_to_cpu(rxon->flags));
	IWL_DEBUG_RADIO(priv, "u32 filter_flags: 0x%08x\n",
			le32_to_cpu(rxon->filter_flags));
	IWL_DEBUG_RADIO(priv, "u8 dev_type: 0x%x\n", rxon->dev_type);
	IWL_DEBUG_RADIO(priv, "u8 ofdm_basic_rates: 0x%02x\n",
			rxon->ofdm_basic_rates);
	IWL_DEBUG_RADIO(priv, "u8 cck_basic_rates: 0x%02x\n", rxon->cck_basic_rates);
	IWL_DEBUG_RADIO(priv, "u8[6] node_addr: %pM\n", rxon->node_addr);
	IWL_DEBUG_RADIO(priv, "u8[6] bssid_addr: %pM\n", rxon->bssid_addr);
	IWL_DEBUG_RADIO(priv, "u16 assoc_id: 0x%x\n", le16_to_cpu(rxon->assoc_id));
}
#endif

/**
 * iwl_irq_handle_error - called for HW or SW error interrupt from card
 */
void iwl_irq_handle_error(struct iwl_priv *priv)
{
	/* Set the FW error flag -- cleared on iwl_down */
	set_bit(STATUS_FW_ERROR, &priv->status);

	/* Cancel currently queued command. */
	clear_bit(STATUS_HCMD_ACTIVE, &priv->status);

#ifdef CONFIG_IWLWIFI_DEBUG
	if (priv->debug_level & IWL_DL_FW_ERRORS) {
		iwl_dump_nic_error_log(priv);
		iwl_dump_nic_event_log(priv);
		iwl_print_rx_config_cmd(priv);
	}
#endif

	wake_up_interruptible(&priv->wait_command_queue);

	/* Keep the restart process from trying to send host
	 * commands by clearing the INIT status bit */
	clear_bit(STATUS_READY, &priv->status);

	if (!test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		IWL_DEBUG(priv, IWL_DL_FW_ERRORS,
			  "Restarting adapter due to uCode error.\n");

		if (iwl_is_associated(priv)) {
			memcpy(&priv->recovery_rxon, &priv->active_rxon,
			       sizeof(priv->recovery_rxon));
			priv->error_recovering = 1;
		}
		if (priv->cfg->mod_params->restart_fw)
			queue_work(priv->workqueue, &priv->restart);
	}
}
EXPORT_SYMBOL(iwl_irq_handle_error);

void iwl_configure_filter(struct ieee80211_hw *hw,
			  unsigned int changed_flags,
			  unsigned int *total_flags,
			  int mc_count, struct dev_addr_list *mc_list)
{
	struct iwl_priv *priv = hw->priv;
	__le32 *filter_flags = &priv->staging_rxon.filter_flags;

	IWL_DEBUG_MAC80211(priv, "Enter: changed: 0x%x, total: 0x%x\n",
			changed_flags, *total_flags);

	if (changed_flags & (FIF_OTHER_BSS | FIF_PROMISC_IN_BSS)) {
		if (*total_flags & (FIF_OTHER_BSS | FIF_PROMISC_IN_BSS))
			*filter_flags |= RXON_FILTER_PROMISC_MSK;
		else
			*filter_flags &= ~RXON_FILTER_PROMISC_MSK;
	}
	if (changed_flags & FIF_ALLMULTI) {
		if (*total_flags & FIF_ALLMULTI)
			*filter_flags |= RXON_FILTER_ACCEPT_GRP_MSK;
		else
			*filter_flags &= ~RXON_FILTER_ACCEPT_GRP_MSK;
	}
	if (changed_flags & FIF_CONTROL) {
		if (*total_flags & FIF_CONTROL)
			*filter_flags |= RXON_FILTER_CTL2HOST_MSK;
		else
			*filter_flags &= ~RXON_FILTER_CTL2HOST_MSK;
	}
	if (changed_flags & FIF_BCN_PRBRESP_PROMISC) {
		if (*total_flags & FIF_BCN_PRBRESP_PROMISC)
			*filter_flags |= RXON_FILTER_BCON_AWARE_MSK;
		else
			*filter_flags &= ~RXON_FILTER_BCON_AWARE_MSK;
	}

	/* We avoid iwl_commit_rxon here to commit the new filter flags
	 * since mac80211 will call ieee80211_hw_config immediately.
	 * (mc_list is not supported at this time). Otherwise, we need to
	 * queue a background iwl_commit_rxon work.
	 */

	*total_flags &= FIF_OTHER_BSS | FIF_ALLMULTI | FIF_PROMISC_IN_BSS |
			FIF_BCN_PRBRESP_PROMISC | FIF_CONTROL;
}
EXPORT_SYMBOL(iwl_configure_filter);

int iwl_setup_mac(struct iwl_priv *priv)
{
	int ret;
	struct ieee80211_hw *hw = priv->hw;
	hw->rate_control_algorithm = "iwl-agn-rs";

	/* Tell mac80211 our characteristics */
	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_NOISE_DBM |
		    IEEE80211_HW_AMPDU_AGGREGATION |
		    IEEE80211_HW_SPECTRUM_MGMT |
		    IEEE80211_HW_SUPPORTS_PS;
	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC);

	hw->wiphy->custom_regulatory = true;
	hw->wiphy->max_scan_ssids = 1;

	/* Default value; 4 EDCA QOS priorities */
	hw->queues = 4;

	hw->conf.beacon_int = 100;
	hw->max_listen_interval = IWL_CONN_MAX_LISTEN_INTERVAL;

	if (priv->bands[IEEE80211_BAND_2GHZ].n_channels)
		priv->hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
			&priv->bands[IEEE80211_BAND_2GHZ];
	if (priv->bands[IEEE80211_BAND_5GHZ].n_channels)
		priv->hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&priv->bands[IEEE80211_BAND_5GHZ];

	ret = ieee80211_register_hw(priv->hw);
	if (ret) {
		IWL_ERR(priv, "Failed to register hw (error %d)\n", ret);
		return ret;
	}
	priv->mac80211_registered = 1;

	return 0;
}
EXPORT_SYMBOL(iwl_setup_mac);

int iwl_set_hw_params(struct iwl_priv *priv)
{
	priv->hw_params.sw_crypto = priv->cfg->mod_params->sw_crypto;
	priv->hw_params.max_rxq_size = RX_QUEUE_SIZE;
	priv->hw_params.max_rxq_log = RX_QUEUE_SIZE_LOG;
	if (priv->cfg->mod_params->amsdu_size_8K)
		priv->hw_params.rx_buf_size = IWL_RX_BUF_SIZE_8K;
	else
		priv->hw_params.rx_buf_size = IWL_RX_BUF_SIZE_4K;
	priv->hw_params.max_pkt_size = priv->hw_params.rx_buf_size - 256;

	if (priv->cfg->mod_params->disable_11n)
		priv->cfg->sku &= ~IWL_SKU_N;

	/* Device-specific setup */
	return priv->cfg->ops->lib->set_hw_params(priv);
}
EXPORT_SYMBOL(iwl_set_hw_params);

int iwl_init_drv(struct iwl_priv *priv)
{
	int ret;

	priv->ibss_beacon = NULL;

	spin_lock_init(&priv->lock);
	spin_lock_init(&priv->power_data.lock);
	spin_lock_init(&priv->sta_lock);
	spin_lock_init(&priv->hcmd_lock);

	INIT_LIST_HEAD(&priv->free_frames);

	mutex_init(&priv->mutex);

	/* Clear the driver's (not device's) station table */
	iwl_clear_stations_table(priv);

	priv->data_retry_limit = -1;
	priv->ieee_channels = NULL;
	priv->ieee_rates = NULL;
	priv->band = IEEE80211_BAND_2GHZ;

	priv->iw_mode = NL80211_IFTYPE_STATION;

	priv->current_ht_config.sm_ps = WLAN_HT_CAP_SM_PS_DISABLED;

	/* Choose which receivers/antennas to use */
	iwl_set_rxon_chain(priv);
	iwl_init_scan_params(priv);

	iwl_reset_qos(priv);

	priv->qos_data.qos_active = 0;
	priv->qos_data.qos_cap.val = 0;

	priv->rates_mask = IWL_RATES_MASK;
	/* If power management is turned on, default to CAM mode */
	priv->power_mode = IWL_POWER_MODE_CAM;
	priv->tx_power_user_lmt = IWL_TX_POWER_TARGET_POWER_MAX;

	ret = iwl_init_channel_map(priv);
	if (ret) {
		IWL_ERR(priv, "initializing regulatory failed: %d\n", ret);
		goto err;
	}

	ret = iwlcore_init_geos(priv);
	if (ret) {
		IWL_ERR(priv, "initializing geos failed: %d\n", ret);
		goto err_free_channel_map;
	}
	iwlcore_init_hw_rates(priv, priv->ieee_rates);

	return 0;

err_free_channel_map:
	iwl_free_channel_map(priv);
err:
	return ret;
}
EXPORT_SYMBOL(iwl_init_drv);

int iwl_set_tx_power(struct iwl_priv *priv, s8 tx_power, bool force)
{
	int ret = 0;
	if (tx_power < IWL_TX_POWER_TARGET_POWER_MIN) {
		IWL_WARN(priv, "Requested user TXPOWER %d below lower limit %d.\n",
			 tx_power,
			 IWL_TX_POWER_TARGET_POWER_MIN);
		return -EINVAL;
	}

	if (tx_power > IWL_TX_POWER_TARGET_POWER_MAX) {
		IWL_WARN(priv, "Requested user TXPOWER %d above upper limit %d.\n",
			 tx_power,
			 IWL_TX_POWER_TARGET_POWER_MAX);
		return -EINVAL;
	}

	if (priv->tx_power_user_lmt != tx_power)
		force = true;

	priv->tx_power_user_lmt = tx_power;

	/* if nic is not up don't send command */
	if (!iwl_is_ready_rf(priv))
		return ret;

	if (force && priv->cfg->ops->lib->send_tx_power)
		ret = priv->cfg->ops->lib->send_tx_power(priv);

	return ret;
}
EXPORT_SYMBOL(iwl_set_tx_power);

void iwl_uninit_drv(struct iwl_priv *priv)
{
	iwl_calib_free_results(priv);
	iwlcore_free_geos(priv);
	iwl_free_channel_map(priv);
	kfree(priv->scan);
}
EXPORT_SYMBOL(iwl_uninit_drv);


void iwl_disable_interrupts(struct iwl_priv *priv)
{
	clear_bit(STATUS_INT_ENABLED, &priv->status);

	/* disable interrupts from uCode/NIC to host */
	iwl_write32(priv, CSR_INT_MASK, 0x00000000);

	/* acknowledge/clear/reset any interrupts still pending
	 * from uCode or flow handler (Rx/Tx DMA) */
	iwl_write32(priv, CSR_INT, 0xffffffff);
	iwl_write32(priv, CSR_FH_INT_STATUS, 0xffffffff);
	IWL_DEBUG_ISR(priv, "Disabled interrupts\n");
}
EXPORT_SYMBOL(iwl_disable_interrupts);

void iwl_enable_interrupts(struct iwl_priv *priv)
{
	IWL_DEBUG_ISR(priv, "Enabling interrupts\n");
	set_bit(STATUS_INT_ENABLED, &priv->status);
	iwl_write32(priv, CSR_INT_MASK, CSR_INI_SET_MASK);
}
EXPORT_SYMBOL(iwl_enable_interrupts);

irqreturn_t iwl_isr(int irq, void *data)
{
	struct iwl_priv *priv = data;
	u32 inta, inta_mask;
	u32 inta_fh;
	if (!priv)
		return IRQ_NONE;

	spin_lock(&priv->lock);

	/* Disable (but don't clear!) interrupts here to avoid
	 *    back-to-back ISRs and sporadic interrupts from our NIC.
	 * If we have something to service, the tasklet will re-enable ints.
	 * If we *don't* have something, we'll re-enable before leaving here. */
	inta_mask = iwl_read32(priv, CSR_INT_MASK);  /* just for debug */
	iwl_write32(priv, CSR_INT_MASK, 0x00000000);

	/* Discover which interrupts are active/pending */
	inta = iwl_read32(priv, CSR_INT);
	inta_fh = iwl_read32(priv, CSR_FH_INT_STATUS);

	/* Ignore interrupt if there's nothing in NIC to service.
	 * This may be due to IRQ shared with another device,
	 * or due to sporadic interrupts thrown from our NIC. */
	if (!inta && !inta_fh) {
		IWL_DEBUG_ISR(priv, "Ignore interrupt, inta == 0, inta_fh == 0\n");
		goto none;
	}

	if ((inta == 0xFFFFFFFF) || ((inta & 0xFFFFFFF0) == 0xa5a5a5a0)) {
		/* Hardware disappeared. It might have already raised
		 * an interrupt */
		IWL_WARN(priv, "HARDWARE GONE?? INTA == 0x%08x\n", inta);
		goto unplugged;
	}

	IWL_DEBUG_ISR(priv, "ISR inta 0x%08x, enabled 0x%08x, fh 0x%08x\n",
		      inta, inta_mask, inta_fh);

	inta &= ~CSR_INT_BIT_SCD;

	/* iwl_irq_tasklet() will service interrupts and re-enable them */
	if (likely(inta || inta_fh))
		tasklet_schedule(&priv->irq_tasklet);

 unplugged:
	spin_unlock(&priv->lock);
	return IRQ_HANDLED;

 none:
	/* re-enable interrupts here since we don't have anything to service. */
	/* only Re-enable if diabled by irq */
	if (test_bit(STATUS_INT_ENABLED, &priv->status))
		iwl_enable_interrupts(priv);
	spin_unlock(&priv->lock);
	return IRQ_NONE;
}
EXPORT_SYMBOL(iwl_isr);

int iwl_send_bt_config(struct iwl_priv *priv)
{
	struct iwl_bt_cmd bt_cmd = {
		.flags = 3,
		.lead_time = 0xAA,
		.max_kill = 1,
		.kill_ack_mask = 0,
		.kill_cts_mask = 0,
	};

	return iwl_send_cmd_pdu(priv, REPLY_BT_CONFIG,
				sizeof(struct iwl_bt_cmd), &bt_cmd);
}
EXPORT_SYMBOL(iwl_send_bt_config);

int iwl_send_statistics_request(struct iwl_priv *priv, u8 flags)
{
	u32 stat_flags = 0;
	struct iwl_host_cmd cmd = {
		.id = REPLY_STATISTICS_CMD,
		.meta.flags = flags,
		.len = sizeof(stat_flags),
		.data = (u8 *) &stat_flags,
	};
	return iwl_send_cmd(priv, &cmd);
}
EXPORT_SYMBOL(iwl_send_statistics_request);

/**
 * iwl_verify_inst_sparse - verify runtime uCode image in card vs. host,
 *   using sample data 100 bytes apart.  If these sample points are good,
 *   it's a pretty good bet that everything between them is good, too.
 */
static int iwlcore_verify_inst_sparse(struct iwl_priv *priv, __le32 *image, u32 len)
{
	u32 val;
	int ret = 0;
	u32 errcnt = 0;
	u32 i;

	IWL_DEBUG_INFO(priv, "ucode inst image size is %u\n", len);

	ret = iwl_grab_nic_access(priv);
	if (ret)
		return ret;

	for (i = 0; i < len; i += 100, image += 100/sizeof(u32)) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IWL_DL_IO is set */
		iwl_write_direct32(priv, HBUS_TARG_MEM_RADDR,
			i + IWL49_RTC_INST_LOWER_BOUND);
		val = _iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image)) {
			ret = -EIO;
			errcnt++;
			if (errcnt >= 3)
				break;
		}
	}

	iwl_release_nic_access(priv);

	return ret;
}

/**
 * iwlcore_verify_inst_full - verify runtime uCode image in card vs. host,
 *     looking at all data.
 */
static int iwl_verify_inst_full(struct iwl_priv *priv, __le32 *image,
				 u32 len)
{
	u32 val;
	u32 save_len = len;
	int ret = 0;
	u32 errcnt;

	IWL_DEBUG_INFO(priv, "ucode inst image size is %u\n", len);

	ret = iwl_grab_nic_access(priv);
	if (ret)
		return ret;

	iwl_write_direct32(priv, HBUS_TARG_MEM_RADDR,
			   IWL49_RTC_INST_LOWER_BOUND);

	errcnt = 0;
	for (; len > 0; len -= sizeof(u32), image++) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IWL_DL_IO is set */
		val = _iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image)) {
			IWL_ERR(priv, "uCode INST section is invalid at "
				  "offset 0x%x, is 0x%x, s/b 0x%x\n",
				  save_len - len, val, le32_to_cpu(*image));
			ret = -EIO;
			errcnt++;
			if (errcnt >= 20)
				break;
		}
	}

	iwl_release_nic_access(priv);

	if (!errcnt)
		IWL_DEBUG_INFO(priv,
		    "ucode image in INSTRUCTION memory is good\n");

	return ret;
}

/**
 * iwl_verify_ucode - determine which instruction image is in SRAM,
 *    and verify its contents
 */
int iwl_verify_ucode(struct iwl_priv *priv)
{
	__le32 *image;
	u32 len;
	int ret;

	/* Try bootstrap */
	image = (__le32 *)priv->ucode_boot.v_addr;
	len = priv->ucode_boot.len;
	ret = iwlcore_verify_inst_sparse(priv, image, len);
	if (!ret) {
		IWL_DEBUG_INFO(priv, "Bootstrap uCode is good in inst SRAM\n");
		return 0;
	}

	/* Try initialize */
	image = (__le32 *)priv->ucode_init.v_addr;
	len = priv->ucode_init.len;
	ret = iwlcore_verify_inst_sparse(priv, image, len);
	if (!ret) {
		IWL_DEBUG_INFO(priv, "Initialize uCode is good in inst SRAM\n");
		return 0;
	}

	/* Try runtime/protocol */
	image = (__le32 *)priv->ucode_code.v_addr;
	len = priv->ucode_code.len;
	ret = iwlcore_verify_inst_sparse(priv, image, len);
	if (!ret) {
		IWL_DEBUG_INFO(priv, "Runtime uCode is good in inst SRAM\n");
		return 0;
	}

	IWL_ERR(priv, "NO VALID UCODE IMAGE IN INSTRUCTION SRAM!!\n");

	/* Since nothing seems to match, show first several data entries in
	 * instruction SRAM, so maybe visual inspection will give a clue.
	 * Selection of bootstrap image (vs. other images) is arbitrary. */
	image = (__le32 *)priv->ucode_boot.v_addr;
	len = priv->ucode_boot.len;
	ret = iwl_verify_inst_full(priv, image, len);

	return ret;
}
EXPORT_SYMBOL(iwl_verify_ucode);


static const char *desc_lookup_text[] = {
	"OK",
	"FAIL",
	"BAD_PARAM",
	"BAD_CHECKSUM",
	"NMI_INTERRUPT_WDG",
	"SYSASSERT",
	"FATAL_ERROR",
	"BAD_COMMAND",
	"HW_ERROR_TUNE_LOCK",
	"HW_ERROR_TEMPERATURE",
	"ILLEGAL_CHAN_FREQ",
	"VCC_NOT_STABLE",
	"FH_ERROR",
	"NMI_INTERRUPT_HOST",
	"NMI_INTERRUPT_ACTION_PT",
	"NMI_INTERRUPT_UNKNOWN",
	"UCODE_VERSION_MISMATCH",
	"HW_ERROR_ABS_LOCK",
	"HW_ERROR_CAL_LOCK_FAIL",
	"NMI_INTERRUPT_INST_ACTION_PT",
	"NMI_INTERRUPT_DATA_ACTION_PT",
	"NMI_TRM_HW_ER",
	"NMI_INTERRUPT_TRM",
	"NMI_INTERRUPT_BREAK_POINT"
	"DEBUG_0",
	"DEBUG_1",
	"DEBUG_2",
	"DEBUG_3",
	"UNKNOWN"
};

static const char *desc_lookup(int i)
{
	int max = ARRAY_SIZE(desc_lookup_text) - 1;

	if (i < 0 || i > max)
		i = max;

	return desc_lookup_text[i];
}

#define ERROR_START_OFFSET  (1 * sizeof(u32))
#define ERROR_ELEM_SIZE     (7 * sizeof(u32))

void iwl_dump_nic_error_log(struct iwl_priv *priv)
{
	u32 data2, line;
	u32 desc, time, count, base, data1;
	u32 blink1, blink2, ilink1, ilink2;
	int ret;

	if (priv->ucode_type == UCODE_INIT)
		base = le32_to_cpu(priv->card_alive_init.error_event_table_ptr);
	else
		base = le32_to_cpu(priv->card_alive.error_event_table_ptr);

	if (!priv->cfg->ops->lib->is_valid_rtc_data_addr(base)) {
		IWL_ERR(priv, "Not valid error log pointer 0x%08X\n", base);
		return;
	}

	ret = iwl_grab_nic_access(priv);
	if (ret) {
		IWL_WARN(priv, "Can not read from adapter at this time.\n");
		return;
	}

	count = iwl_read_targ_mem(priv, base);

	if (ERROR_START_OFFSET <= count * ERROR_ELEM_SIZE) {
		IWL_ERR(priv, "Start IWL Error Log Dump:\n");
		IWL_ERR(priv, "Status: 0x%08lX, count: %d\n",
			priv->status, count);
	}

	desc = iwl_read_targ_mem(priv, base + 1 * sizeof(u32));
	blink1 = iwl_read_targ_mem(priv, base + 3 * sizeof(u32));
	blink2 = iwl_read_targ_mem(priv, base + 4 * sizeof(u32));
	ilink1 = iwl_read_targ_mem(priv, base + 5 * sizeof(u32));
	ilink2 = iwl_read_targ_mem(priv, base + 6 * sizeof(u32));
	data1 = iwl_read_targ_mem(priv, base + 7 * sizeof(u32));
	data2 = iwl_read_targ_mem(priv, base + 8 * sizeof(u32));
	line = iwl_read_targ_mem(priv, base + 9 * sizeof(u32));
	time = iwl_read_targ_mem(priv, base + 11 * sizeof(u32));

	IWL_ERR(priv, "Desc                               Time       "
		"data1      data2      line\n");
	IWL_ERR(priv, "%-28s (#%02d) %010u 0x%08X 0x%08X %u\n",
		desc_lookup(desc), desc, time, data1, data2, line);
	IWL_ERR(priv, "blink1  blink2  ilink1  ilink2\n");
	IWL_ERR(priv, "0x%05X 0x%05X 0x%05X 0x%05X\n", blink1, blink2,
		ilink1, ilink2);

	iwl_release_nic_access(priv);
}
EXPORT_SYMBOL(iwl_dump_nic_error_log);

#define EVENT_START_OFFSET  (4 * sizeof(u32))

/**
 * iwl_print_event_log - Dump error event log to syslog
 *
 * NOTE: Must be called with iwl_grab_nic_access() already obtained!
 */
static void iwl_print_event_log(struct iwl_priv *priv, u32 start_idx,
				u32 num_events, u32 mode)
{
	u32 i;
	u32 base;       /* SRAM byte address of event log header */
	u32 event_size; /* 2 u32s, or 3 u32s if timestamp recorded */
	u32 ptr;        /* SRAM byte address of log data */
	u32 ev, time, data; /* event log data */

	if (num_events == 0)
		return;
	if (priv->ucode_type == UCODE_INIT)
		base = le32_to_cpu(priv->card_alive_init.log_event_table_ptr);
	else
		base = le32_to_cpu(priv->card_alive.log_event_table_ptr);

	if (mode == 0)
		event_size = 2 * sizeof(u32);
	else
		event_size = 3 * sizeof(u32);

	ptr = base + EVENT_START_OFFSET + (start_idx * event_size);

	/* "time" is actually "data" for mode 0 (no timestamp).
	* place event id # at far right for easier visual parsing. */
	for (i = 0; i < num_events; i++) {
		ev = iwl_read_targ_mem(priv, ptr);
		ptr += sizeof(u32);
		time = iwl_read_targ_mem(priv, ptr);
		ptr += sizeof(u32);
		if (mode == 0) {
			/* data, ev */
			IWL_ERR(priv, "EVT_LOG:0x%08x:%04u\n", time, ev);
		} else {
			data = iwl_read_targ_mem(priv, ptr);
			ptr += sizeof(u32);
			IWL_ERR(priv, "EVT_LOGT:%010u:0x%08x:%04u\n",
					time, data, ev);
		}
	}
}

void iwl_dump_nic_event_log(struct iwl_priv *priv)
{
	int ret;
	u32 base;       /* SRAM byte address of event log header */
	u32 capacity;   /* event log capacity in # entries */
	u32 mode;       /* 0 - no timestamp, 1 - timestamp recorded */
	u32 num_wraps;  /* # times uCode wrapped to top of log */
	u32 next_entry; /* index of next entry to be written by uCode */
	u32 size;       /* # entries that we'll print */

	if (priv->ucode_type == UCODE_INIT)
		base = le32_to_cpu(priv->card_alive_init.log_event_table_ptr);
	else
		base = le32_to_cpu(priv->card_alive.log_event_table_ptr);

	if (!priv->cfg->ops->lib->is_valid_rtc_data_addr(base)) {
		IWL_ERR(priv, "Invalid event log pointer 0x%08X\n", base);
		return;
	}

	ret = iwl_grab_nic_access(priv);
	if (ret) {
		IWL_WARN(priv, "Can not read from adapter at this time.\n");
		return;
	}

	/* event log header */
	capacity = iwl_read_targ_mem(priv, base);
	mode = iwl_read_targ_mem(priv, base + (1 * sizeof(u32)));
	num_wraps = iwl_read_targ_mem(priv, base + (2 * sizeof(u32)));
	next_entry = iwl_read_targ_mem(priv, base + (3 * sizeof(u32)));

	size = num_wraps ? capacity : next_entry;

	/* bail out if nothing in log */
	if (size == 0) {
		IWL_ERR(priv, "Start IWL Event Log Dump: nothing in log\n");
		iwl_release_nic_access(priv);
		return;
	}

	IWL_ERR(priv, "Start IWL Event Log Dump: display count %d, wraps %d\n",
			size, num_wraps);

	/* if uCode has wrapped back to top of log, start at the oldest entry,
	 * i.e the next one that uCode would fill. */
	if (num_wraps)
		iwl_print_event_log(priv, next_entry,
					capacity - next_entry, mode);
	/* (then/else) start at top of log */
	iwl_print_event_log(priv, 0, next_entry, mode);

	iwl_release_nic_access(priv);
}
EXPORT_SYMBOL(iwl_dump_nic_event_log);

void iwl_rf_kill_ct_config(struct iwl_priv *priv)
{
	struct iwl_ct_kill_config cmd;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&priv->lock, flags);
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT);
	spin_unlock_irqrestore(&priv->lock, flags);

	cmd.critical_temperature_R =
		cpu_to_le32(priv->hw_params.ct_kill_threshold);

	ret = iwl_send_cmd_pdu(priv, REPLY_CT_KILL_CONFIG_CMD,
			       sizeof(cmd), &cmd);
	if (ret)
		IWL_ERR(priv, "REPLY_CT_KILL_CONFIG_CMD failed\n");
	else
		IWL_DEBUG_INFO(priv, "REPLY_CT_KILL_CONFIG_CMD succeeded, "
			"critical temperature is %d\n",
			cmd.critical_temperature_R);
}
EXPORT_SYMBOL(iwl_rf_kill_ct_config);


/*
 * CARD_STATE_CMD
 *
 * Use: Sets the device's internal card state to enable, disable, or halt
 *
 * When in the 'enable' state the card operates as normal.
 * When in the 'disable' state, the card enters into a low power mode.
 * When in the 'halt' state, the card is shut down and must be fully
 * restarted to come back on.
 */
int iwl_send_card_state(struct iwl_priv *priv, u32 flags, u8 meta_flag)
{
	struct iwl_host_cmd cmd = {
		.id = REPLY_CARD_STATE_CMD,
		.len = sizeof(u32),
		.data = &flags,
		.meta.flags = meta_flag,
	};

	return iwl_send_cmd(priv, &cmd);
}
EXPORT_SYMBOL(iwl_send_card_state);

void iwl_radio_kill_sw_disable_radio(struct iwl_priv *priv)
{
	unsigned long flags;

	if (test_bit(STATUS_RF_KILL_SW, &priv->status))
		return;

	IWL_DEBUG_RF_KILL(priv, "Manual SW RF KILL set to: RADIO OFF\n");

	iwl_scan_cancel(priv);
	/* FIXME: This is a workaround for AP */
	if (priv->iw_mode != NL80211_IFTYPE_AP) {
		spin_lock_irqsave(&priv->lock, flags);
		iwl_write32(priv, CSR_UCODE_DRV_GP1_SET,
			    CSR_UCODE_SW_BIT_RFKILL);
		spin_unlock_irqrestore(&priv->lock, flags);
		/* call the host command only if no hw rf-kill set */
		if (!test_bit(STATUS_RF_KILL_HW, &priv->status) &&
		    iwl_is_ready(priv))
			iwl_send_card_state(priv,
				CARD_STATE_CMD_DISABLE, 0);
		set_bit(STATUS_RF_KILL_SW, &priv->status);
			/* make sure mac80211 stop sending Tx frame */
		if (priv->mac80211_registered)
			ieee80211_stop_queues(priv->hw);
	}
}
EXPORT_SYMBOL(iwl_radio_kill_sw_disable_radio);

int iwl_radio_kill_sw_enable_radio(struct iwl_priv *priv)
{
	unsigned long flags;

	if (!test_bit(STATUS_RF_KILL_SW, &priv->status))
		return 0;

	IWL_DEBUG_RF_KILL(priv, "Manual SW RF KILL set to: RADIO ON\n");

	spin_lock_irqsave(&priv->lock, flags);
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);

	/* If the driver is up it will receive CARD_STATE_NOTIFICATION
	 * notification where it will clear SW rfkill status.
	 * Setting it here would break the handler. Only if the
	 * interface is down we can set here since we don't
	 * receive any further notification.
	 */
	if (!priv->is_open)
		clear_bit(STATUS_RF_KILL_SW, &priv->status);
	spin_unlock_irqrestore(&priv->lock, flags);

	/* wake up ucode */
	msleep(10);

	spin_lock_irqsave(&priv->lock, flags);
	iwl_read32(priv, CSR_UCODE_DRV_GP1);
	if (!iwl_grab_nic_access(priv))
		iwl_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	if (test_bit(STATUS_RF_KILL_HW, &priv->status)) {
		IWL_DEBUG_RF_KILL(priv, "Can not turn radio back on - "
				  "disabled by HW switch\n");
		return 0;
	}

	/* when driver is up while rfkill is on, it wont receive
	 * any CARD_STATE_NOTIFICATION notifications so we have to
	 * restart it in here
	 */
	if (priv->is_open && !test_bit(STATUS_ALIVE, &priv->status)) {
		clear_bit(STATUS_RF_KILL_SW, &priv->status);
		if (!iwl_is_rfkill(priv))
			queue_work(priv->workqueue, &priv->up);
	}

	/* If the driver is already loaded, it will receive
	 * CARD_STATE_NOTIFICATION notifications and the handler will
	 * call restart to reload the driver.
	 */
	return 1;
}
EXPORT_SYMBOL(iwl_radio_kill_sw_enable_radio);

void iwl_bg_rf_kill(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv, rf_kill);

	wake_up_interruptible(&priv->wait_command_queue);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);

	if (!iwl_is_rfkill(priv)) {
		IWL_DEBUG_RF_KILL(priv,
			  "HW and/or SW RF Kill no longer active, restarting "
			  "device\n");
		if (!test_bit(STATUS_EXIT_PENDING, &priv->status) &&
		    test_bit(STATUS_ALIVE, &priv->status))
			queue_work(priv->workqueue, &priv->restart);
	} else {
		/* make sure mac80211 stop sending Tx frame */
		if (priv->mac80211_registered)
			ieee80211_stop_queues(priv->hw);

		if (!test_bit(STATUS_RF_KILL_HW, &priv->status))
			IWL_DEBUG_RF_KILL(priv, "Can not turn radio back on - "
					  "disabled by SW switch\n");
		else
			IWL_WARN(priv, "Radio Frequency Kill Switch is On:\n"
				    "Kill switch must be turned off for "
				    "wireless networking to work.\n");
	}
	mutex_unlock(&priv->mutex);
	iwl_rfkill_set_hw_state(priv);
}
EXPORT_SYMBOL(iwl_bg_rf_kill);

void iwl_rx_pm_sleep_notif(struct iwl_priv *priv,
			   struct iwl_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	struct iwl_sleep_notification *sleep = &(pkt->u.sleep_notif);
	IWL_DEBUG_RX(priv, "sleep mode: %d, src: %d\n",
		     sleep->pm_sleep_mode, sleep->pm_wakeup_src);
#endif
}
EXPORT_SYMBOL(iwl_rx_pm_sleep_notif);

void iwl_rx_pm_debug_statistics_notif(struct iwl_priv *priv,
				      struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	IWL_DEBUG_RADIO(priv, "Dumping %d bytes of unhandled "
			"notification for %s:\n",
			le32_to_cpu(pkt->len), get_cmd_string(pkt->hdr.cmd));
	iwl_print_hex_dump(priv, IWL_DL_RADIO, pkt->u.raw, le32_to_cpu(pkt->len));
}
EXPORT_SYMBOL(iwl_rx_pm_debug_statistics_notif);

void iwl_rx_reply_error(struct iwl_priv *priv,
			struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;

	IWL_ERR(priv, "Error Reply type 0x%08X cmd %s (0x%02X) "
		"seq 0x%04X ser 0x%08X\n",
		le32_to_cpu(pkt->u.err_resp.error_type),
		get_cmd_string(pkt->u.err_resp.cmd_id),
		pkt->u.err_resp.cmd_id,
		le16_to_cpu(pkt->u.err_resp.bad_cmd_seq_num),
		le32_to_cpu(pkt->u.err_resp.error_info));
}
EXPORT_SYMBOL(iwl_rx_reply_error);

