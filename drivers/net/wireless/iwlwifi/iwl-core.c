/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 Intel Corporation. All rights reserved.
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
 * Tomas Winkler <tomas.winkler@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <net/mac80211.h>

struct iwl_priv; /* FIXME: remove */
#include "iwl-debug.h"
#include "iwl-eeprom.h"
#include "iwl-dev.h" /* FIXME: remove */
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-rfkill.h"
#include "iwl-power.h"


MODULE_DESCRIPTION("iwl core");
MODULE_VERSION(IWLWIFI_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT);
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
				  struct ieee80211_tx_info *control)
{
	int rate_index;

	control->antenna_sel_tx =
		((rate_n_flags & RATE_MCS_ANT_ABC_MSK) >> RATE_MCS_ANT_POS);
	if (rate_n_flags & RATE_MCS_HT_MSK)
		control->flags |= IEEE80211_TX_CTL_OFDM_HT;
	if (rate_n_flags & RATE_MCS_GF_MSK)
		control->flags |= IEEE80211_TX_CTL_GREEN_FIELD;
	if (rate_n_flags & RATE_MCS_FAT_MSK)
		control->flags |= IEEE80211_TX_CTL_40_MHZ_WIDTH;
	if (rate_n_flags & RATE_MCS_DUP_MSK)
		control->flags |= IEEE80211_TX_CTL_DUP_DATA;
	if (rate_n_flags & RATE_MCS_SGI_MSK)
		control->flags |= IEEE80211_TX_CTL_SHORT_GI;
	rate_index = iwl_hwrate_to_plcp_idx(rate_n_flags);
	if (control->band == IEEE80211_BAND_5GHZ)
		rate_index -= IWL_FIRST_OFDM_RATE;
	control->tx_rate_idx = rate_index;
}
EXPORT_SYMBOL(iwl_hwrate_to_tx_control);

int iwl_hwrate_to_plcp_idx(u32 rate_n_flags)
{
	int idx = 0;

	/* HT rate format */
	if (rate_n_flags & RATE_MCS_HT_MSK) {
		idx = (rate_n_flags & 0xff);

		if (idx >= IWL_RATE_MIMO2_6M_PLCP)
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
		IWL_ERROR("Can not allocate network device\n");
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

/* Tell nic where to find the "keep warm" buffer */
int iwl_kw_init(struct iwl_priv *priv)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&priv->lock, flags);
	ret = iwl_grab_nic_access(priv);
	if (ret)
		goto out;

	iwl_write_direct32(priv, FH_KW_MEM_ADDR_REG,
			     priv->kw.dma_addr >> 4);
	iwl_release_nic_access(priv);
out:
	spin_unlock_irqrestore(&priv->lock, flags);
	return ret;
}

int iwl_kw_alloc(struct iwl_priv *priv)
{
	struct pci_dev *dev = priv->pci_dev;
	struct iwl_kw *kw = &priv->kw;

	kw->size = IWL_KW_SIZE;
	kw->v_addr = pci_alloc_consistent(dev, kw->size, &kw->dma_addr);
	if (!kw->v_addr)
		return -ENOMEM;

	return 0;
}

/**
 * iwl_kw_free - Free the "keep warm" buffer
 */
void iwl_kw_free(struct iwl_priv *priv)
{
	struct pci_dev *dev = priv->pci_dev;
	struct iwl_kw *kw = &priv->kw;

	if (kw->v_addr) {
		pci_free_consistent(dev, kw->size, kw->v_addr, kw->dma_addr);
		memset(kw, 0, sizeof(*kw));
	}
}

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
			IWL_ERROR("Unable to initialize Rx queue\n");
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

/**
 * iwl_clear_stations_table - Clear the driver's station table
 *
 * NOTE:  This does not clear or otherwise alter the device's station table.
 */
void iwl_clear_stations_table(struct iwl_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_lock, flags);

	if (iwl_is_alive(priv) &&
	   !test_bit(STATUS_EXIT_PENDING, &priv->status) &&
	   iwl_send_cmd_pdu_async(priv, REPLY_REMOVE_ALL_STA, 0, NULL, NULL))
		IWL_ERROR("Couldn't clear the station table\n");

	priv->num_stations = 0;
	memset(priv->stations, 0, sizeof(priv->stations));

	spin_unlock_irqrestore(&priv->sta_lock, flags);
}
EXPORT_SYMBOL(iwl_clear_stations_table);

void iwl_reset_qos(struct iwl_priv *priv)
{
	u16 cw_min = 15;
	u16 cw_max = 1023;
	u8 aifs = 2;
	u8 is_legacy = 0;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&priv->lock, flags);
	priv->qos_data.qos_active = 0;

	if (priv->iw_mode == IEEE80211_IF_TYPE_IBSS) {
		if (priv->qos_data.qos_enable)
			priv->qos_data.qos_active = 1;
		if (!(priv->active_rate & 0xfff0)) {
			cw_min = 31;
			is_legacy = 1;
		}
	} else if (priv->iw_mode == IEEE80211_IF_TYPE_AP) {
		if (priv->qos_data.qos_enable)
			priv->qos_data.qos_active = 1;
	} else if (!(priv->staging_rxon.flags & RXON_FLG_SHORT_SLOT_MSK)) {
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
	IWL_DEBUG_QOS("set QoS to default \n");

	spin_unlock_irqrestore(&priv->lock, flags);
}
EXPORT_SYMBOL(iwl_reset_qos);

#define MAX_BIT_RATE_40_MHZ 0x96 /* 150 Mbps */
#define MAX_BIT_RATE_20_MHZ 0x48 /* 72 Mbps */
static void iwlcore_init_ht_hw_capab(const struct iwl_priv *priv,
			      struct ieee80211_ht_info *ht_info,
			      enum ieee80211_band band)
{
	u16 max_bit_rate = 0;
	u8 rx_chains_num = priv->hw_params.rx_chains_num;
	u8 tx_chains_num = priv->hw_params.tx_chains_num;

	ht_info->cap = 0;
	memset(ht_info->supp_mcs_set, 0, 16);

	ht_info->ht_supported = 1;

	ht_info->cap |= (u16)IEEE80211_HT_CAP_GRN_FLD;
	ht_info->cap |= (u16)IEEE80211_HT_CAP_SGI_20;
	ht_info->cap |= (u16)(IEEE80211_HT_CAP_SM_PS &
			     (WLAN_HT_CAP_SM_PS_DISABLED << 2));

	max_bit_rate = MAX_BIT_RATE_20_MHZ;
	if (priv->hw_params.fat_channel & BIT(band)) {
		ht_info->cap |= (u16)IEEE80211_HT_CAP_SUP_WIDTH;
		ht_info->cap |= (u16)IEEE80211_HT_CAP_SGI_40;
		ht_info->supp_mcs_set[4] = 0x01;
		max_bit_rate = MAX_BIT_RATE_40_MHZ;
	}

	if (priv->cfg->mod_params->amsdu_size_8K)
		ht_info->cap |= (u16)IEEE80211_HT_CAP_MAX_AMSDU;

	ht_info->ampdu_factor = CFG_HT_RX_AMPDU_FACTOR_DEF;
	ht_info->ampdu_density = CFG_HT_MPDU_DENSITY_DEF;

	ht_info->supp_mcs_set[0] = 0xFF;
	if (rx_chains_num >= 2)
		ht_info->supp_mcs_set[1] = 0xFF;
	if (rx_chains_num >= 3)
		ht_info->supp_mcs_set[2] = 0xFF;

	/* Highest supported Rx data rate */
	max_bit_rate *= rx_chains_num;
	ht_info->supp_mcs_set[10] = (u8)(max_bit_rate & 0x00FF);
	ht_info->supp_mcs_set[11] = (u8)((max_bit_rate & 0xFF00) >> 8);

	/* Tx MCS capabilities */
	ht_info->supp_mcs_set[12] = IEEE80211_HT_CAP_MCS_TX_DEFINED;
	if (tx_chains_num != rx_chains_num) {
		ht_info->supp_mcs_set[12] |= IEEE80211_HT_CAP_MCS_TX_RX_DIFF;
		ht_info->supp_mcs_set[12] |= ((tx_chains_num - 1) << 2);
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
static int iwlcore_init_geos(struct iwl_priv *priv)
{
	struct iwl_channel_info *ch;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *channels;
	struct ieee80211_channel *geo_ch;
	struct ieee80211_rate *rates;
	int i = 0;

	if (priv->bands[IEEE80211_BAND_2GHZ].n_bitrates ||
	    priv->bands[IEEE80211_BAND_5GHZ].n_bitrates) {
		IWL_DEBUG_INFO("Geography modes already initialized.\n");
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
		iwlcore_init_ht_hw_capab(priv, &sband->ht_info,
					 IEEE80211_BAND_5GHZ);

	sband = &priv->bands[IEEE80211_BAND_2GHZ];
	sband->channels = channels;
	/* OFDM & CCK */
	sband->bitrates = rates;
	sband->n_bitrates = IWL_RATE_COUNT;

	if (priv->cfg->sku & IWL_SKU_N)
		iwlcore_init_ht_hw_capab(priv, &sband->ht_info,
					 IEEE80211_BAND_2GHZ);

	priv->ieee_channels = channels;
	priv->ieee_rates = rates;

	iwlcore_init_hw_rates(priv, rates);

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

		IWL_DEBUG_INFO("Channel %d Freq=%d[%sGHz] %s flag=0x%X\n",
				ch->channel, geo_ch->center_freq,
				is_channel_a_band(ch) ?  "5.2" : "2.4",
				geo_ch->flags & IEEE80211_CHAN_DISABLED ?
				"restricted" : "valid",
				 geo_ch->flags);
	}

	if ((priv->bands[IEEE80211_BAND_5GHZ].n_channels == 0) &&
	     priv->cfg->sku & IWL_SKU_A) {
		printk(KERN_INFO DRV_NAME
		       ": Incorrectly detected BG card as ABG.  Please send "
		       "your PCI ID 0x%04X:0x%04X to maintainer.\n",
		       priv->pci_dev->device, priv->pci_dev->subsystem_device);
		priv->cfg->sku &= ~IWL_SKU_A;
	}

	printk(KERN_INFO DRV_NAME
	       ": Tunable channels: %d 802.11bg, %d 802.11a channels\n",
	       priv->bands[IEEE80211_BAND_2GHZ].n_channels,
	       priv->bands[IEEE80211_BAND_5GHZ].n_channels);


	set_bit(STATUS_GEO_CONFIGURED, &priv->status);

	return 0;
}

/*
 * iwlcore_free_geos - undo allocations in iwlcore_init_geos
 */
static void iwlcore_free_geos(struct iwl_priv *priv)
{
	kfree(priv->ieee_channels);
	kfree(priv->ieee_rates);
	clear_bit(STATUS_GEO_CONFIGURED, &priv->status);
}

static bool is_single_rx_stream(struct iwl_priv *priv)
{
	return !priv->current_ht_config.is_ht ||
	       ((priv->current_ht_config.supp_mcs_set[1] == 0) &&
		(priv->current_ht_config.supp_mcs_set[2] == 0));
}

static u8 iwl_is_channel_extension(struct iwl_priv *priv,
				   enum ieee80211_band band,
				   u16 channel, u8 extension_chan_offset)
{
	const struct iwl_channel_info *ch_info;

	ch_info = iwl_get_channel_info(priv, band, channel);
	if (!is_channel_valid(ch_info))
		return 0;

	if (extension_chan_offset == IEEE80211_HT_IE_CHA_SEC_ABOVE)
		return !(ch_info->fat_extension_channel &
					IEEE80211_CHAN_NO_FAT_ABOVE);
	else if (extension_chan_offset == IEEE80211_HT_IE_CHA_SEC_BELOW)
		return !(ch_info->fat_extension_channel &
					IEEE80211_CHAN_NO_FAT_BELOW);

	return 0;
}

u8 iwl_is_fat_tx_allowed(struct iwl_priv *priv,
			     struct ieee80211_ht_info *sta_ht_inf)
{
	struct iwl_ht_info *iwl_ht_conf = &priv->current_ht_config;

	if ((!iwl_ht_conf->is_ht) ||
	   (iwl_ht_conf->supported_chan_width != IWL_CHANNEL_WIDTH_40MHZ) ||
	   (iwl_ht_conf->extension_chan_offset == IEEE80211_HT_IE_CHA_SEC_NONE))
		return 0;

	if (sta_ht_inf) {
		if ((!sta_ht_inf->ht_supported) ||
		   (!(sta_ht_inf->cap & IEEE80211_HT_CAP_SUP_WIDTH)))
			return 0;
	}

	return iwl_is_channel_extension(priv, priv->band,
					 iwl_ht_conf->control_channel,
					 iwl_ht_conf->extension_chan_offset);
}
EXPORT_SYMBOL(iwl_is_fat_tx_allowed);

void iwl_set_rxon_ht(struct iwl_priv *priv, struct iwl_ht_info *ht_info)
{
	struct iwl_rxon_cmd *rxon = &priv->staging_rxon;
	u32 val;

	if (!ht_info->is_ht)
		return;

	/* Set up channel bandwidth:  20 MHz only, or 20/40 mixed if fat ok */
	if (iwl_is_fat_tx_allowed(priv, NULL))
		rxon->flags |= RXON_FLG_CHANNEL_MODE_MIXED_MSK;
	else
		rxon->flags &= ~(RXON_FLG_CHANNEL_MODE_MIXED_MSK |
				 RXON_FLG_CHANNEL_MODE_PURE_40_MSK);

	if (le16_to_cpu(rxon->channel) != ht_info->control_channel) {
		IWL_DEBUG_ASSOC("control diff than current %d %d\n",
				le16_to_cpu(rxon->channel),
				ht_info->control_channel);
		return;
	}

	/* Note: control channel is opposite of extension channel */
	switch (ht_info->extension_chan_offset) {
	case IEEE80211_HT_IE_CHA_SEC_ABOVE:
		rxon->flags &= ~(RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK);
		break;
	case IEEE80211_HT_IE_CHA_SEC_BELOW:
		rxon->flags |= RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK;
		break;
	case IEEE80211_HT_IE_CHA_SEC_NONE:
	default:
		rxon->flags &= ~RXON_FLG_CHANNEL_MODE_MIXED_MSK;
		break;
	}

	val = ht_info->ht_protection;

	rxon->flags |= cpu_to_le32(val << RXON_FLG_HT_OPERATING_MODE_POS);

	iwl_set_rxon_chain(priv);

	IWL_DEBUG_ASSOC("supported HT rate 0x%X 0x%X 0x%X "
			"rxon flags 0x%X operation mode :0x%X "
			"extension channel offset 0x%x "
			"control chan %d\n",
			ht_info->supp_mcs_set[0],
			ht_info->supp_mcs_set[1],
			ht_info->supp_mcs_set[2],
			le32_to_cpu(rxon->flags), ht_info->ht_protection,
			ht_info->extension_chan_offset,
			ht_info->control_channel);
	return;
}
EXPORT_SYMBOL(iwl_set_rxon_ht);

/*
 * Determine how many receiver/antenna chains to use.
 * More provides better reception via diversity.  Fewer saves power.
 * MIMO (dual stream) requires at least 2, but works better with 3.
 * This does not determine *which* chains to use, just how many.
 */
static int iwl_get_active_rx_chain_count(struct iwl_priv *priv)
{
	bool is_single = is_single_rx_stream(priv);
	bool is_cam = !test_bit(STATUS_POWER_PMI, &priv->status);

	/* # of Rx chains to use when expecting MIMO. */
	if (is_single || (!is_cam && (priv->ps_mode == WLAN_HT_CAP_SM_PS_STATIC)))
		return 2;
	else
		return 3;
}

static int iwl_get_idle_rx_chain_count(struct iwl_priv *priv, int active_cnt)
{
	int idle_cnt;
	bool is_cam = !test_bit(STATUS_POWER_PMI, &priv->status);
	/* # Rx chains when idling and maybe trying to save power */
	switch (priv->ps_mode) {
	case WLAN_HT_CAP_SM_PS_STATIC:
	case WLAN_HT_CAP_SM_PS_DYNAMIC:
		idle_cnt = (is_cam) ? 2 : 1;
		break;
	case WLAN_HT_CAP_SM_PS_DISABLED:
		idle_cnt = (is_cam) ? active_cnt : 1;
		break;
	case WLAN_HT_CAP_SM_PS_INVALID:
	default:
		IWL_ERROR("invalide mimo ps mode %d\n", priv->ps_mode);
		WARN_ON(1);
		idle_cnt = -1;
		break;
	}
	return idle_cnt;
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
	u8 idle_rx_cnt, active_rx_cnt;
	u16 rx_chain;

	/* Tell uCode which antennas are actually connected.
	 * Before first association, we assume all antennas are connected.
	 * Just after first association, iwl_chain_noise_calibration()
	 *    checks which antennas actually *are* connected. */
	rx_chain = priv->hw_params.valid_rx_ant << RXON_RX_CHAIN_VALID_POS;

	/* How many receivers should we use? */
	active_rx_cnt = iwl_get_active_rx_chain_count(priv);
	idle_rx_cnt = iwl_get_idle_rx_chain_count(priv, active_rx_cnt);

	/* correct rx chain count accoridng hw settings */
	if (priv->hw_params.rx_chains_num < active_rx_cnt)
		active_rx_cnt = priv->hw_params.rx_chains_num;

	if (priv->hw_params.rx_chains_num < idle_rx_cnt)
		idle_rx_cnt = priv->hw_params.rx_chains_num;

	rx_chain |= active_rx_cnt << RXON_RX_CHAIN_MIMO_CNT_POS;
	rx_chain |= idle_rx_cnt  << RXON_RX_CHAIN_CNT_POS;

	priv->staging_rxon.rx_chain = cpu_to_le16(rx_chain);

	if (!is_single && (active_rx_cnt >= 2) && is_cam)
		priv->staging_rxon.rx_chain |= RXON_RX_CHAIN_MIMO_FORCE_MSK;
	else
		priv->staging_rxon.rx_chain &= ~RXON_RX_CHAIN_MIMO_FORCE_MSK;

	IWL_DEBUG_ASSOC("rx_chain=0x%Xi active=%d idle=%d\n",
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
		IWL_DEBUG_INFO("Could not set channel to %d [%d]\n",
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

	IWL_DEBUG_INFO("Staging channel set to %d [%d]\n", channel, band);

	return 0;
}
EXPORT_SYMBOL(iwl_set_rxon_channel);

int iwl_setup_mac(struct iwl_priv *priv)
{
	int ret;
	struct ieee80211_hw *hw = priv->hw;
	hw->rate_control_algorithm = "iwl-agn-rs";

	/* Tell mac80211 our characteristics */
	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_NOISE_DBM;
	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_AP) |
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC);
	/* Default value; 4 EDCA QOS priorities */
	hw->queues = 4;
	/* queues to support 11n aggregation */
	if (priv->cfg->sku & IWL_SKU_N)
		hw->ampdu_queues = priv->cfg->mod_params->num_of_ampdu_queues;

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
		IWL_ERROR("Failed to register hw (error %d)\n", ret);
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

	priv->retry_rate = 1;
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

	priv->iw_mode = IEEE80211_IF_TYPE_STA;

	priv->use_ant_b_for_management_frame = 1; /* start with ant B */
	priv->ps_mode = WLAN_HT_CAP_SM_PS_DISABLED;

	/* Choose which receivers/antennas to use */
	iwl_set_rxon_chain(priv);
	iwl_init_scan_params(priv);

	if (priv->cfg->mod_params->enable_qos)
		priv->qos_data.qos_enable = 1;

	iwl_reset_qos(priv);

	priv->qos_data.qos_active = 0;
	priv->qos_data.qos_cap.val = 0;

	priv->rates_mask = IWL_RATES_MASK;
	/* If power management is turned on, default to AC mode */
	priv->power_mode = IWL_POWER_AC;
	priv->tx_power_user_lmt = IWL_TX_POWER_TARGET_POWER_MAX;

	ret = iwl_init_channel_map(priv);
	if (ret) {
		IWL_ERROR("initializing regulatory failed: %d\n", ret);
		goto err;
	}

	ret = iwlcore_init_geos(priv);
	if (ret) {
		IWL_ERROR("initializing geos failed: %d\n", ret);
		goto err_free_channel_map;
	}

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
		IWL_WARNING("Requested user TXPOWER %d below limit.\n",
			    priv->tx_power_user_lmt);
		return -EINVAL;
	}

	if (tx_power > IWL_TX_POWER_TARGET_POWER_MAX) {
		IWL_WARNING("Requested user TXPOWER %d above limit.\n",
			    priv->tx_power_user_lmt);
		return -EINVAL;
	}

	if (priv->tx_power_user_lmt != tx_power)
		force = true;

	priv->tx_power_user_lmt = tx_power;

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

	IWL_DEBUG_INFO("ucode inst image size is %u\n", len);

	ret = iwl_grab_nic_access(priv);
	if (ret)
		return ret;

	for (i = 0; i < len; i += 100, image += 100/sizeof(u32)) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IWL_DL_IO is set */
		iwl_write_direct32(priv, HBUS_TARG_MEM_RADDR,
			i + RTC_INST_LOWER_BOUND);
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

	IWL_DEBUG_INFO("ucode inst image size is %u\n", len);

	ret = iwl_grab_nic_access(priv);
	if (ret)
		return ret;

	iwl_write_direct32(priv, HBUS_TARG_MEM_RADDR, RTC_INST_LOWER_BOUND);

	errcnt = 0;
	for (; len > 0; len -= sizeof(u32), image++) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IWL_DL_IO is set */
		val = _iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image)) {
			IWL_ERROR("uCode INST section is invalid at "
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
		IWL_DEBUG_INFO
		    ("ucode image in INSTRUCTION memory is good\n");

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
		IWL_DEBUG_INFO("Bootstrap uCode is good in inst SRAM\n");
		return 0;
	}

	/* Try initialize */
	image = (__le32 *)priv->ucode_init.v_addr;
	len = priv->ucode_init.len;
	ret = iwlcore_verify_inst_sparse(priv, image, len);
	if (!ret) {
		IWL_DEBUG_INFO("Initialize uCode is good in inst SRAM\n");
		return 0;
	}

	/* Try runtime/protocol */
	image = (__le32 *)priv->ucode_code.v_addr;
	len = priv->ucode_code.len;
	ret = iwlcore_verify_inst_sparse(priv, image, len);
	if (!ret) {
		IWL_DEBUG_INFO("Runtime uCode is good in inst SRAM\n");
		return 0;
	}

	IWL_ERROR("NO VALID UCODE IMAGE IN INSTRUCTION SRAM!!\n");

	/* Since nothing seems to match, show first several data entries in
	 * instruction SRAM, so maybe visual inspection will give a clue.
	 * Selection of bootstrap image (vs. other images) is arbitrary. */
	image = (__le32 *)priv->ucode_boot.v_addr;
	len = priv->ucode_boot.len;
	ret = iwl_verify_inst_full(priv, image, len);

	return ret;
}
EXPORT_SYMBOL(iwl_verify_ucode);


static const char *desc_lookup(int i)
{
	switch (i) {
	case 1:
		return "FAIL";
	case 2:
		return "BAD_PARAM";
	case 3:
		return "BAD_CHECKSUM";
	case 4:
		return "NMI_INTERRUPT";
	case 5:
		return "SYSASSERT";
	case 6:
		return "FATAL_ERROR";
	}

	return "UNKNOWN";
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
		IWL_ERROR("Not valid error log pointer 0x%08X\n", base);
		return;
	}

	ret = iwl_grab_nic_access(priv);
	if (ret) {
		IWL_WARNING("Can not read from adapter at this time.\n");
		return;
	}

	count = iwl_read_targ_mem(priv, base);

	if (ERROR_START_OFFSET <= count * ERROR_ELEM_SIZE) {
		IWL_ERROR("Start IWL Error Log Dump:\n");
		IWL_ERROR("Status: 0x%08lX, count: %d\n", priv->status, count);
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

	IWL_ERROR("Desc        Time       "
		"data1      data2      line\n");
	IWL_ERROR("%-13s (#%d) %010u 0x%08X 0x%08X %u\n",
		desc_lookup(desc), desc, time, data1, data2, line);
	IWL_ERROR("blink1  blink2  ilink1  ilink2\n");
	IWL_ERROR("0x%05X 0x%05X 0x%05X 0x%05X\n", blink1, blink2,
		ilink1, ilink2);

	iwl_release_nic_access(priv);
}
EXPORT_SYMBOL(iwl_dump_nic_error_log);

#define EVENT_START_OFFSET  (4 * sizeof(u32))

/**
 * iwl_print_event_log - Dump error event log to syslog
 *
 * NOTE: Must be called with iwl4965_grab_nic_access() already obtained!
 */
void iwl_print_event_log(struct iwl_priv *priv, u32 start_idx,
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
			IWL_ERROR("EVT_LOG:0x%08x:%04u\n", time, ev);
		} else {
			data = iwl_read_targ_mem(priv, ptr);
			ptr += sizeof(u32);
			IWL_ERROR("EVT_LOGT:%010u:0x%08x:%04u\n",
					time, data, ev);
		}
	}
}
EXPORT_SYMBOL(iwl_print_event_log);


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
		IWL_ERROR("Invalid event log pointer 0x%08X\n", base);
		return;
	}

	ret = iwl_grab_nic_access(priv);
	if (ret) {
		IWL_WARNING("Can not read from adapter at this time.\n");
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
		IWL_ERROR("Start IWL Event Log Dump: nothing in log\n");
		iwl_release_nic_access(priv);
		return;
	}

	IWL_ERROR("Start IWL Event Log Dump: display count %d, wraps %d\n",
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
		IWL_ERROR("REPLY_CT_KILL_CONFIG_CMD failed\n");
	else
		IWL_DEBUG_INFO("REPLY_CT_KILL_CONFIG_CMD succeeded, "
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
static int iwl_send_card_state(struct iwl_priv *priv, u32 flags, u8 meta_flag)
{
	struct iwl_host_cmd cmd = {
		.id = REPLY_CARD_STATE_CMD,
		.len = sizeof(u32),
		.data = &flags,
		.meta.flags = meta_flag,
	};

	return iwl_send_cmd(priv, &cmd);
}

void iwl_radio_kill_sw_disable_radio(struct iwl_priv *priv)
{
	unsigned long flags;

	if (test_bit(STATUS_RF_KILL_SW, &priv->status))
		return;

	IWL_DEBUG_RF_KILL("Manual SW RF KILL set to: RADIO OFF\n");

	iwl_scan_cancel(priv);
	/* FIXME: This is a workaround for AP */
	if (priv->iw_mode != IEEE80211_IF_TYPE_AP) {
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

	IWL_DEBUG_RF_KILL("Manual SW RF KILL set to: RADIO ON\n");

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
		IWL_DEBUG_RF_KILL("Can not turn radio back on - "
				  "disabled by HW switch\n");
		return 0;
	}

	/* If the driver is already loaded, it will receive
	 * CARD_STATE_NOTIFICATION notifications and the handler will
	 * call restart to reload the driver.
	 */
	return 1;
}
EXPORT_SYMBOL(iwl_radio_kill_sw_enable_radio);
