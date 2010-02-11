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
#include <linux/sched.h>
#include <net/mac80211.h>

#include "iwl-eeprom.h"
#include "iwl-dev.h" /* FIXME: remove */
#include "iwl-debug.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-power.h"
#include "iwl-sta.h"
#include "iwl-helpers.h"


MODULE_DESCRIPTION("iwl core");
MODULE_VERSION(IWLWIFI_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT " " DRV_AUTHOR);
MODULE_LICENSE("GPL");

static struct iwl_wimax_coex_event_entry cu_priorities[COEX_NUM_OF_EVENTS] = {
	{COEX_CU_UNASSOC_IDLE_RP, COEX_CU_UNASSOC_IDLE_WP,
	 0, COEX_UNASSOC_IDLE_FLAGS},
	{COEX_CU_UNASSOC_MANUAL_SCAN_RP, COEX_CU_UNASSOC_MANUAL_SCAN_WP,
	 0, COEX_UNASSOC_MANUAL_SCAN_FLAGS},
	{COEX_CU_UNASSOC_AUTO_SCAN_RP, COEX_CU_UNASSOC_AUTO_SCAN_WP,
	 0, COEX_UNASSOC_AUTO_SCAN_FLAGS},
	{COEX_CU_CALIBRATION_RP, COEX_CU_CALIBRATION_WP,
	 0, COEX_CALIBRATION_FLAGS},
	{COEX_CU_PERIODIC_CALIBRATION_RP, COEX_CU_PERIODIC_CALIBRATION_WP,
	 0, COEX_PERIODIC_CALIBRATION_FLAGS},
	{COEX_CU_CONNECTION_ESTAB_RP, COEX_CU_CONNECTION_ESTAB_WP,
	 0, COEX_CONNECTION_ESTAB_FLAGS},
	{COEX_CU_ASSOCIATED_IDLE_RP, COEX_CU_ASSOCIATED_IDLE_WP,
	 0, COEX_ASSOCIATED_IDLE_FLAGS},
	{COEX_CU_ASSOC_MANUAL_SCAN_RP, COEX_CU_ASSOC_MANUAL_SCAN_WP,
	 0, COEX_ASSOC_MANUAL_SCAN_FLAGS},
	{COEX_CU_ASSOC_AUTO_SCAN_RP, COEX_CU_ASSOC_AUTO_SCAN_WP,
	 0, COEX_ASSOC_AUTO_SCAN_FLAGS},
	{COEX_CU_ASSOC_ACTIVE_LEVEL_RP, COEX_CU_ASSOC_ACTIVE_LEVEL_WP,
	 0, COEX_ASSOC_ACTIVE_LEVEL_FLAGS},
	{COEX_CU_RF_ON_RP, COEX_CU_RF_ON_WP, 0, COEX_CU_RF_ON_FLAGS},
	{COEX_CU_RF_OFF_RP, COEX_CU_RF_OFF_WP, 0, COEX_RF_OFF_FLAGS},
	{COEX_CU_STAND_ALONE_DEBUG_RP, COEX_CU_STAND_ALONE_DEBUG_WP,
	 0, COEX_STAND_ALONE_DEBUG_FLAGS},
	{COEX_CU_IPAN_ASSOC_LEVEL_RP, COEX_CU_IPAN_ASSOC_LEVEL_WP,
	 0, COEX_IPAN_ASSOC_LEVEL_FLAGS},
	{COEX_CU_RSRVD1_RP, COEX_CU_RSRVD1_WP, 0, COEX_RSRVD1_FLAGS},
	{COEX_CU_RSRVD2_RP, COEX_CU_RSRVD2_WP, 0, COEX_RSRVD2_FLAGS}
};

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

u32 iwl_debug_level;
EXPORT_SYMBOL(iwl_debug_level);

static irqreturn_t iwl_isr(int irq, void *data);

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
	struct ieee80211_tx_rate *r = &info->control.rates[0];

	info->antenna_sel_tx =
		((rate_n_flags & RATE_MCS_ANT_ABC_MSK) >> RATE_MCS_ANT_POS);
	if (rate_n_flags & RATE_MCS_HT_MSK)
		r->flags |= IEEE80211_TX_RC_MCS;
	if (rate_n_flags & RATE_MCS_GF_MSK)
		r->flags |= IEEE80211_TX_RC_GREEN_FIELD;
	if (rate_n_flags & RATE_MCS_HT40_MSK)
		r->flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;
	if (rate_n_flags & RATE_MCS_DUP_MSK)
		r->flags |= IEEE80211_TX_RC_DUP_DATA;
	if (rate_n_flags & RATE_MCS_SGI_MSK)
		r->flags |= IEEE80211_TX_RC_SHORT_GI;
	r->idx = iwl_hwrate_to_mac80211_idx(rate_n_flags, info->band);
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

int iwl_hwrate_to_mac80211_idx(u32 rate_n_flags, enum ieee80211_band band)
{
	int idx = 0;
	int band_offset = 0;

	/* HT rate format: mac80211 wants an MCS number, which is just LSB */
	if (rate_n_flags & RATE_MCS_HT_MSK) {
		idx = (rate_n_flags & 0xff);
		return idx;
	/* Legacy rate format, search for match in table */
	} else {
		if (band == IEEE80211_BAND_5GHZ)
			band_offset = IWL_FIRST_OFDM_RATE;
		for (idx = band_offset; idx < IWL_RATE_COUNT_LEGACY; idx++)
			if (iwl_rates[idx].plcp == (rate_n_flags & 0xFF))
				return idx - band_offset;
	}

	return -1;
}

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
EXPORT_SYMBOL(iwl_toggle_tx_ant);

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

	/* Set interrupt coalescing timer to 512 usecs */
	iwl_write8(priv, CSR_INT_COALESCING, 512 / 32);

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

/*
 * AC        CWmin         CW max      AIFSN      TXOP Limit    TXOP Limit
 *                                              (802.11b)      (802.11a/g)
 * AC_BK      15            1023        7           0               0
 * AC_BE      15            1023        3           0               0
 * AC_VI       7              15        2          6.016ms       3.008ms
 * AC_VO       3               7        2          3.264ms       1.504ms
 */
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

	/* AC_BE */
	priv->qos_data.def_qos_parm.ac[0].cw_min = cpu_to_le16(cw_min);
	priv->qos_data.def_qos_parm.ac[0].cw_max = cpu_to_le16(cw_max);
	priv->qos_data.def_qos_parm.ac[0].aifsn = aifs;
	priv->qos_data.def_qos_parm.ac[0].edca_txop = 0;
	priv->qos_data.def_qos_parm.ac[0].reserved1 = 0;

	if (priv->qos_data.qos_active) {
		/* AC_BK */
		i = 1;
		priv->qos_data.def_qos_parm.ac[i].cw_min = cpu_to_le16(cw_min);
		priv->qos_data.def_qos_parm.ac[i].cw_max = cpu_to_le16(cw_max);
		priv->qos_data.def_qos_parm.ac[i].aifsn = 7;
		priv->qos_data.def_qos_parm.ac[i].edca_txop = 0;
		priv->qos_data.def_qos_parm.ac[i].reserved1 = 0;

		/* AC_VI */
		i = 2;
		priv->qos_data.def_qos_parm.ac[i].cw_min =
			cpu_to_le16((cw_min + 1) / 2 - 1);
		priv->qos_data.def_qos_parm.ac[i].cw_max =
			cpu_to_le16(cw_min);
		priv->qos_data.def_qos_parm.ac[i].aifsn = 2;
		if (is_legacy)
			priv->qos_data.def_qos_parm.ac[i].edca_txop =
				cpu_to_le16(6016);
		else
			priv->qos_data.def_qos_parm.ac[i].edca_txop =
				cpu_to_le16(3008);
		priv->qos_data.def_qos_parm.ac[i].reserved1 = 0;

		/* AC_VO */
		i = 3;
		priv->qos_data.def_qos_parm.ac[i].cw_min =
			cpu_to_le16((cw_min + 1) / 4 - 1);
		priv->qos_data.def_qos_parm.ac[i].cw_max =
			cpu_to_le16((cw_min + 1) / 2 - 1);
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

	if (priv->cfg->ht_greenfield_support)
		ht_info->cap |= IEEE80211_HT_CAP_GRN_FLD;
	ht_info->cap |= IEEE80211_HT_CAP_SGI_20;
	ht_info->cap |= (IEEE80211_HT_CAP_SM_PS &
			     (priv->cfg->sm_ps_mode << 2));
	max_bit_rate = MAX_BIT_RATE_20_MHZ;
	if (priv->hw_params.ht40_channel & BIT(band)) {
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

	rates = kzalloc((sizeof(struct ieee80211_rate) * IWL_RATE_COUNT_LEGACY),
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
	sband->n_bitrates = IWL_RATE_COUNT_LEGACY - IWL_FIRST_OFDM_RATE;

	if (priv->cfg->sku & IWL_SKU_N)
		iwlcore_init_ht_hw_capab(priv, &sband->ht_cap,
					 IEEE80211_BAND_5GHZ);

	sband = &priv->bands[IEEE80211_BAND_2GHZ];
	sband->channels = channels;
	/* OFDM & CCK */
	sband->bitrates = rates;
	sband->n_bitrates = IWL_RATE_COUNT_LEGACY;

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

			geo_ch->flags |= ch->ht40_extension_channel;

			if (ch->max_power_avg > priv->tx_power_device_lmt)
				priv->tx_power_device_lmt = ch->max_power_avg;
		} else {
			geo_ch->flags |= IEEE80211_CHAN_DISABLED;
		}

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

/*
 *  iwlcore_rts_tx_cmd_flag: Set rts/cts. 3945 and 4965 only share this
 *  function.
 */
void iwlcore_rts_tx_cmd_flag(struct ieee80211_tx_info *info,
				__le32 *tx_flags)
{
	if (info->control.rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS) {
		*tx_flags |= TX_CMD_FLG_RTS_MSK;
		*tx_flags &= ~TX_CMD_FLG_CTS_MSK;
	} else if (info->control.rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT) {
		*tx_flags &= ~TX_CMD_FLG_RTS_MSK;
		*tx_flags |= TX_CMD_FLG_CTS_MSK;
	}
}
EXPORT_SYMBOL(iwlcore_rts_tx_cmd_flag);

static bool is_single_rx_stream(struct iwl_priv *priv)
{
	return !priv->current_ht_config.is_ht ||
	       priv->current_ht_config.single_chain_sufficient;
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
		return !(ch_info->ht40_extension_channel &
					IEEE80211_CHAN_NO_HT40PLUS);
	else if (extension_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_BELOW)
		return !(ch_info->ht40_extension_channel &
					IEEE80211_CHAN_NO_HT40MINUS);

	return 0;
}

u8 iwl_is_ht40_tx_allowed(struct iwl_priv *priv,
			 struct ieee80211_sta_ht_cap *sta_ht_inf)
{
	struct iwl_ht_config *ht_conf = &priv->current_ht_config;

	if (!ht_conf->is_ht || !ht_conf->is_40mhz)
		return 0;

	/* We do not check for IEEE80211_HT_CAP_SUP_WIDTH_20_40
	 * the bit will not set if it is pure 40MHz case
	 */
	if (sta_ht_inf) {
		if (!sta_ht_inf->ht_supported)
			return 0;
	}
#ifdef CONFIG_IWLWIFI_DEBUG
	if (priv->disable_ht40)
		return 0;
#endif
	return iwl_is_channel_extension(priv, priv->band,
			le16_to_cpu(priv->staging_rxon.channel),
			ht_conf->extension_chan_offset);
}
EXPORT_SYMBOL(iwl_is_ht40_tx_allowed);

static u16 iwl_adjust_beacon_interval(u16 beacon_val, u16 max_beacon_val)
{
	u16 new_val = 0;
	u16 beacon_factor = 0;

	beacon_factor = (beacon_val + max_beacon_val) / max_beacon_val;
	new_val = beacon_val / beacon_factor;

	if (!new_val)
		new_val = max_beacon_val;

	return new_val;
}

void iwl_setup_rxon_timing(struct iwl_priv *priv)
{
	u64 tsf;
	s32 interval_tm, rem;
	unsigned long flags;
	struct ieee80211_conf *conf = NULL;
	u16 beacon_int;

	conf = ieee80211_get_hw_conf(priv->hw);

	spin_lock_irqsave(&priv->lock, flags);
	priv->rxon_timing.timestamp = cpu_to_le64(priv->timestamp);
	priv->rxon_timing.listen_interval = cpu_to_le16(conf->listen_interval);

	if (priv->iw_mode == NL80211_IFTYPE_STATION) {
		beacon_int = priv->beacon_int;
		priv->rxon_timing.atim_window = 0;
	} else {
		beacon_int = priv->vif->bss_conf.beacon_int;

		/* TODO: we need to get atim_window from upper stack
		 * for now we set to 0 */
		priv->rxon_timing.atim_window = 0;
	}

	beacon_int = iwl_adjust_beacon_interval(beacon_int,
				priv->hw_params.max_beacon_itrvl * 1024);
	priv->rxon_timing.beacon_interval = cpu_to_le16(beacon_int);

	tsf = priv->timestamp; /* tsf is modifed by do_div: copy it */
	interval_tm = beacon_int * 1024;
	rem = do_div(tsf, interval_tm);
	priv->rxon_timing.beacon_init_val = cpu_to_le32(interval_tm - rem);

	spin_unlock_irqrestore(&priv->lock, flags);
	IWL_DEBUG_ASSOC(priv,
			"beacon interval %d beacon timer %d beacon tim %d\n",
			le16_to_cpu(priv->rxon_timing.beacon_interval),
			le32_to_cpu(priv->rxon_timing.beacon_init_val),
			le16_to_cpu(priv->rxon_timing.atim_window));
}
EXPORT_SYMBOL(iwl_setup_rxon_timing);

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
	    (priv->staging_rxon.ofdm_ht_triple_stream_basic_rates !=
	     priv->active_rxon.ofdm_ht_triple_stream_basic_rates) ||
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

void iwl_set_rxon_ht(struct iwl_priv *priv, struct iwl_ht_config *ht_conf)
{
	struct iwl_rxon_cmd *rxon = &priv->staging_rxon;

	if (!ht_conf->is_ht) {
		rxon->flags &= ~(RXON_FLG_CHANNEL_MODE_MSK |
			RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK |
			RXON_FLG_HT40_PROT_MSK |
			RXON_FLG_HT_PROT_MSK);
		return;
	}

	/* FIXME: if the definition of ht_protection changed, the "translation"
	 * will be needed for rxon->flags
	 */
	rxon->flags |= cpu_to_le32(ht_conf->ht_protection << RXON_FLG_HT_OPERATING_MODE_POS);

	/* Set up channel bandwidth:
	 * 20 MHz only, 20/40 mixed or pure 40 if ht40 ok */
	/* clear the HT channel mode before set the mode */
	rxon->flags &= ~(RXON_FLG_CHANNEL_MODE_MSK |
			 RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK);
	if (iwl_is_ht40_tx_allowed(priv, NULL)) {
		/* pure ht40 */
		if (ht_conf->ht_protection == IEEE80211_HT_OP_MODE_PROTECTION_20MHZ) {
			rxon->flags |= RXON_FLG_CHANNEL_MODE_PURE_40;
			/* Note: control channel is opposite of extension channel */
			switch (ht_conf->extension_chan_offset) {
			case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
				rxon->flags &= ~RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK;
				break;
			case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
				rxon->flags |= RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK;
				break;
			}
		} else {
			/* Note: control channel is opposite of extension channel */
			switch (ht_conf->extension_chan_offset) {
			case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
				rxon->flags &= ~(RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK);
				rxon->flags |= RXON_FLG_CHANNEL_MODE_MIXED;
				break;
			case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
				rxon->flags |= RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK;
				rxon->flags |= RXON_FLG_CHANNEL_MODE_MIXED;
				break;
			case IEEE80211_HT_PARAM_CHA_SEC_NONE:
			default:
				/* channel location only valid if in Mixed mode */
				IWL_ERR(priv, "invalid extension channel offset\n");
				break;
			}
		}
	} else {
		rxon->flags |= RXON_FLG_CHANNEL_MODE_LEGACY;
	}

	if (priv->cfg->ops->hcmd->set_rxon_chain)
		priv->cfg->ops->hcmd->set_rxon_chain(priv);

	IWL_DEBUG_ASSOC(priv, "rxon flags 0x%X operation mode :0x%X "
			"extension channel offset 0x%x\n",
			le32_to_cpu(rxon->flags), ht_conf->ht_protection,
			ht_conf->extension_chan_offset);
	return;
}
EXPORT_SYMBOL(iwl_set_rxon_ht);

#define IWL_NUM_RX_CHAINS_MULTIPLE	3
#define IWL_NUM_RX_CHAINS_SINGLE	2
#define IWL_NUM_IDLE_CHAINS_DUAL	2
#define IWL_NUM_IDLE_CHAINS_SINGLE	1

/*
 * Determine how many receiver/antenna chains to use.
 *
 * More provides better reception via diversity.  Fewer saves power
 * at the expense of throughput, but only when not in powersave to
 * start with.
 *
 * MIMO (dual stream) requires at least 2, but works better with 3.
 * This does not determine *which* chains to use, just how many.
 */
static int iwl_get_active_rx_chain_count(struct iwl_priv *priv)
{
	/* # of Rx chains to use when expecting MIMO. */
	if (is_single_rx_stream(priv))
		return IWL_NUM_RX_CHAINS_SINGLE;
	else
		return IWL_NUM_RX_CHAINS_MULTIPLE;
}

/*
 * When we are in power saving mode, unless device support spatial
 * multiplexing power save, use the active count for rx chain count.
 */
static int iwl_get_idle_rx_chain_count(struct iwl_priv *priv, int active_cnt)
{
	int idle_cnt = active_cnt;
	bool is_cam = !test_bit(STATUS_POWER_PMI, &priv->status);

	/* # Rx chains when idling and maybe trying to save power */
	switch (priv->cfg->sm_ps_mode) {
	case WLAN_HT_CAP_SM_PS_STATIC:
		idle_cnt = (is_cam) ? active_cnt : IWL_NUM_IDLE_CHAINS_SINGLE;
		break;
	case WLAN_HT_CAP_SM_PS_DYNAMIC:
		idle_cnt = (is_cam) ? IWL_NUM_IDLE_CHAINS_DUAL :
			IWL_NUM_IDLE_CHAINS_SINGLE;
		break;
	case WLAN_HT_CAP_SM_PS_DISABLED:
		break;
	case WLAN_HT_CAP_SM_PS_INVALID:
	default:
		IWL_ERR(priv, "invalid sm_ps mode %u\n",
			priv->cfg->sm_ps_mode);
		WARN_ON(1);
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
	res += (chain_bitmap & BIT(3)) >> 3;
	return res;
}

/**
 * iwl_is_monitor_mode - Determine if interface in monitor mode
 *
 * priv->iw_mode is set in add_interface, but add_interface is
 * never called for monitor mode. The only way mac80211 informs us about
 * monitor mode is through configuring filters (call to configure_filter).
 */
bool iwl_is_monitor_mode(struct iwl_priv *priv)
{
	return !!(priv->staging_rxon.filter_flags & RXON_FILTER_PROMISC_MSK);
}
EXPORT_SYMBOL(iwl_is_monitor_mode);

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
		rx_chain = ANT_ABC << RXON_RX_CHAIN_VALID_POS;
		rx_chain |= ANT_BC << RXON_RX_CHAIN_FORCE_SEL_POS;
		rx_chain |= ANT_ABC << RXON_RX_CHAIN_FORCE_MIMO_SEL_POS;
		rx_chain |= 0x1 << RXON_RX_CHAIN_DRIVER_FORCE_POS;
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

	/* clear both MIX and PURE40 mode flag */
	priv->staging_rxon.flags &= ~(RXON_FLG_CHANNEL_MODE_MIXED |
					RXON_FLG_CHANNEL_MODE_PURE_40);
	memcpy(priv->staging_rxon.node_addr, priv->mac_addr, ETH_ALEN);
	memcpy(priv->staging_rxon.wlap_bssid_addr, priv->mac_addr, ETH_ALEN);
	priv->staging_rxon.ofdm_ht_single_stream_basic_rates = 0xff;
	priv->staging_rxon.ofdm_ht_dual_stream_basic_rates = 0xff;
	priv->staging_rxon.ofdm_ht_triple_stream_basic_rates = 0xff;
}
EXPORT_SYMBOL(iwl_connection_init_rx_config);

static void iwl_set_rate(struct iwl_priv *priv)
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
		if (rate->hw_value < IWL_RATE_COUNT_LEGACY)
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

void iwl_rx_csa(struct iwl_priv *priv, struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_rxon_cmd *rxon = (void *)&priv->active_rxon;
	struct iwl_csa_notification *csa = &(pkt->u.csa_notif);

	if (priv->switch_rxon.switch_in_progress) {
		if (!le32_to_cpu(csa->status) &&
		    (csa->channel == priv->switch_rxon.channel)) {
			rxon->channel = csa->channel;
			priv->staging_rxon.channel = csa->channel;
			IWL_DEBUG_11H(priv, "CSA notif: channel %d\n",
			      le16_to_cpu(csa->channel));
		} else
			IWL_ERR(priv, "CSA notif (fail) : channel %d\n",
			      le16_to_cpu(csa->channel));

		priv->switch_rxon.switch_in_progress = false;
	}
}
EXPORT_SYMBOL(iwl_rx_csa);

#ifdef CONFIG_IWLWIFI_DEBUG
void iwl_print_rx_config_cmd(struct iwl_priv *priv)
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
EXPORT_SYMBOL(iwl_print_rx_config_cmd);
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

	priv->cfg->ops->lib->dump_nic_error_log(priv);
	priv->cfg->ops->lib->dump_nic_event_log(priv, false);
#ifdef CONFIG_IWLWIFI_DEBUG
	if (iwl_get_debug_level(priv) & IWL_DL_FW_ERRORS)
		iwl_print_rx_config_cmd(priv);
#endif

	wake_up_interruptible(&priv->wait_command_queue);

	/* Keep the restart process from trying to send host
	 * commands by clearing the INIT status bit */
	clear_bit(STATUS_READY, &priv->status);

	if (!test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		IWL_DEBUG(priv, IWL_DL_FW_ERRORS,
			  "Restarting adapter due to uCode error.\n");

		if (priv->cfg->mod_params->restart_fw)
			queue_work(priv->workqueue, &priv->restart);
	}
}
EXPORT_SYMBOL(iwl_irq_handle_error);

int iwl_apm_stop_master(struct iwl_priv *priv)
{
	int ret = 0;

	/* stop device's busmaster DMA activity */
	iwl_set_bit(priv, CSR_RESET, CSR_RESET_REG_FLAG_STOP_MASTER);

	ret = iwl_poll_bit(priv, CSR_RESET, CSR_RESET_REG_FLAG_MASTER_DISABLED,
			CSR_RESET_REG_FLAG_MASTER_DISABLED, 100);
	if (ret)
		IWL_WARN(priv, "Master Disable Timed Out, 100 usec\n");

	IWL_DEBUG_INFO(priv, "stop master\n");

	return ret;
}
EXPORT_SYMBOL(iwl_apm_stop_master);

void iwl_apm_stop(struct iwl_priv *priv)
{
	IWL_DEBUG_INFO(priv, "Stop card, put in low power state\n");

	/* Stop device's DMA activity */
	iwl_apm_stop_master(priv);

	/* Reset the entire device */
	iwl_set_bit(priv, CSR_RESET, CSR_RESET_REG_FLAG_SW_RESET);

	udelay(10);

	/*
	 * Clear "initialization complete" bit to move adapter from
	 * D0A* (powered-up Active) --> D0U* (Uninitialized) state.
	 */
	iwl_clear_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
}
EXPORT_SYMBOL(iwl_apm_stop);


/*
 * Start up NIC's basic functionality after it has been reset
 * (e.g. after platform boot, or shutdown via iwl_apm_stop())
 * NOTE:  This does not load uCode nor start the embedded processor
 */
int iwl_apm_init(struct iwl_priv *priv)
{
	int ret = 0;
	u16 lctl;

	IWL_DEBUG_INFO(priv, "Init card's basic functions\n");

	/*
	 * Use "set_bit" below rather than "write", to preserve any hardware
	 * bits already set by default after reset.
	 */

	/* Disable L0S exit timer (platform NMI Work/Around) */
	iwl_set_bit(priv, CSR_GIO_CHICKEN_BITS,
			  CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER);

	/*
	 * Disable L0s without affecting L1;
	 *  don't wait for ICH L0s (ICH bug W/A)
	 */
	iwl_set_bit(priv, CSR_GIO_CHICKEN_BITS,
			  CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX);

	/* Set FH wait threshold to maximum (HW error during stress W/A) */
	iwl_set_bit(priv, CSR_DBG_HPET_MEM_REG, CSR_DBG_HPET_MEM_REG_VAL);

	/*
	 * Enable HAP INTA (interrupt from management bus) to
	 * wake device's PCI Express link L1a -> L0s
	 * NOTE:  This is no-op for 3945 (non-existant bit)
	 */
	iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
				    CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A);

	/*
	 * HW bug W/A for instability in PCIe bus L0->L0S->L1 transition.
	 * Check if BIOS (or OS) enabled L1-ASPM on this device.
	 * If so (likely), disable L0S, so device moves directly L0->L1;
	 *    costs negligible amount of power savings.
	 * If not (unlikely), enable L0S, so there is at least some
	 *    power savings, even without L1.
	 */
	if (priv->cfg->set_l0s) {
		lctl = iwl_pcie_link_ctl(priv);
		if ((lctl & PCI_CFG_LINK_CTRL_VAL_L1_EN) ==
					PCI_CFG_LINK_CTRL_VAL_L1_EN) {
			/* L1-ASPM enabled; disable(!) L0S  */
			iwl_set_bit(priv, CSR_GIO_REG,
					CSR_GIO_REG_VAL_L0S_ENABLED);
			IWL_DEBUG_POWER(priv, "L1 Enabled; Disabling L0S\n");
		} else {
			/* L1-ASPM disabled; enable(!) L0S */
			iwl_clear_bit(priv, CSR_GIO_REG,
					CSR_GIO_REG_VAL_L0S_ENABLED);
			IWL_DEBUG_POWER(priv, "L1 Disabled; Enabling L0S\n");
		}
	}

	/* Configure analog phase-lock-loop before activating to D0A */
	if (priv->cfg->pll_cfg_val)
		iwl_set_bit(priv, CSR_ANA_PLL_CFG, priv->cfg->pll_cfg_val);

	/*
	 * Set "initialization complete" bit to move adapter from
	 * D0U* --> D0A* (powered-up active) state.
	 */
	iwl_set_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/*
	 * Wait for clock stabilization; once stabilized, access to
	 * device-internal resources is supported, e.g. iwl_write_prph()
	 * and accesses to uCode SRAM.
	 */
	ret = iwl_poll_bit(priv, CSR_GP_CNTRL,
			CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000);
	if (ret < 0) {
		IWL_DEBUG_INFO(priv, "Failed to init the card\n");
		goto out;
	}

	/*
	 * Enable DMA and BSM (if used) clocks, wait for them to stabilize.
	 * BSM (Boostrap State Machine) is only in 3945 and 4965;
	 * later devices (i.e. 5000 and later) have non-volatile SRAM,
	 * and don't need BSM to restore data after power-saving sleep.
	 *
	 * Write to "CLK_EN_REG"; "1" bits enable clocks, while "0" bits
	 * do not disable clocks.  This preserves any hardware bits already
	 * set by default in "CLK_CTRL_REG" after reset.
	 */
	if (priv->cfg->use_bsm)
		iwl_write_prph(priv, APMG_CLK_EN_REG,
			APMG_CLK_VAL_DMA_CLK_RQT | APMG_CLK_VAL_BSM_CLK_RQT);
	else
		iwl_write_prph(priv, APMG_CLK_EN_REG,
			APMG_CLK_VAL_DMA_CLK_RQT);
	udelay(20);

	/* Disable L1-Active */
	iwl_set_bits_prph(priv, APMG_PCIDEV_STT_REG,
			  APMG_PCIDEV_STT_VAL_L1_ACT_DIS);

out:
	return ret;
}
EXPORT_SYMBOL(iwl_apm_init);



void iwl_configure_filter(struct ieee80211_hw *hw,
			  unsigned int changed_flags,
			  unsigned int *total_flags,
			  u64 multicast)
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

int iwl_set_hw_params(struct iwl_priv *priv)
{
	priv->hw_params.max_rxq_size = RX_QUEUE_SIZE;
	priv->hw_params.max_rxq_log = RX_QUEUE_SIZE_LOG;
	if (priv->cfg->mod_params->amsdu_size_8K)
		priv->hw_params.rx_page_order = get_order(IWL_RX_BUF_SIZE_8K);
	else
		priv->hw_params.rx_page_order = get_order(IWL_RX_BUF_SIZE_4K);

	priv->hw_params.max_beacon_itrvl = IWL_MAX_UCODE_BEACON_INTERVAL;

	if (priv->cfg->mod_params->disable_11n)
		priv->cfg->sku &= ~IWL_SKU_N;

	/* Device-specific setup */
	return priv->cfg->ops->lib->set_hw_params(priv);
}
EXPORT_SYMBOL(iwl_set_hw_params);

int iwl_set_tx_power(struct iwl_priv *priv, s8 tx_power, bool force)
{
	int ret = 0;
	s8 prev_tx_power = priv->tx_power_user_lmt;

	if (tx_power < IWL_TX_POWER_TARGET_POWER_MIN) {
		IWL_WARN(priv, "Requested user TXPOWER %d below lower limit %d.\n",
			 tx_power,
			 IWL_TX_POWER_TARGET_POWER_MIN);
		return -EINVAL;
	}

	if (tx_power > priv->tx_power_device_lmt) {
		IWL_WARN(priv,
			"Requested user TXPOWER %d above upper limit %d.\n",
			 tx_power, priv->tx_power_device_lmt);
		return -EINVAL;
	}

	if (priv->tx_power_user_lmt != tx_power)
		force = true;

	/* if nic is not up don't send command */
	if (iwl_is_ready_rf(priv)) {
		priv->tx_power_user_lmt = tx_power;
		if (force && priv->cfg->ops->lib->send_tx_power)
			ret = priv->cfg->ops->lib->send_tx_power(priv);
		else if (!priv->cfg->ops->lib->send_tx_power)
			ret = -EOPNOTSUPP;
		/*
		 * if fail to set tx_power, restore the orig. tx power
		 */
		if (ret)
			priv->tx_power_user_lmt = prev_tx_power;
	}

	/*
	 * Even this is an async host command, the command
	 * will always report success from uCode
	 * So once driver can placing the command into the queue
	 * successfully, driver can use priv->tx_power_user_lmt
	 * to reflect the current tx power
	 */
	return ret;
}
EXPORT_SYMBOL(iwl_set_tx_power);

#define ICT_COUNT (PAGE_SIZE/sizeof(u32))

/* Free dram table */
void iwl_free_isr_ict(struct iwl_priv *priv)
{
	if (priv->ict_tbl_vir) {
		pci_free_consistent(priv->pci_dev, (sizeof(u32) * ICT_COUNT) +
					PAGE_SIZE, priv->ict_tbl_vir,
					priv->ict_tbl_dma);
		priv->ict_tbl_vir = NULL;
	}
}
EXPORT_SYMBOL(iwl_free_isr_ict);


/* allocate dram shared table it is a PAGE_SIZE aligned
 * also reset all data related to ICT table interrupt.
 */
int iwl_alloc_isr_ict(struct iwl_priv *priv)
{

	if (priv->cfg->use_isr_legacy)
		return 0;
	/* allocate shrared data table */
	priv->ict_tbl_vir = pci_alloc_consistent(priv->pci_dev, (sizeof(u32) *
						  ICT_COUNT) + PAGE_SIZE,
						  &priv->ict_tbl_dma);
	if (!priv->ict_tbl_vir)
		return -ENOMEM;

	/* align table to PAGE_SIZE boundry */
	priv->aligned_ict_tbl_dma = ALIGN(priv->ict_tbl_dma, PAGE_SIZE);

	IWL_DEBUG_ISR(priv, "ict dma addr %Lx dma aligned %Lx diff %d\n",
			     (unsigned long long)priv->ict_tbl_dma,
			     (unsigned long long)priv->aligned_ict_tbl_dma,
			(int)(priv->aligned_ict_tbl_dma - priv->ict_tbl_dma));

	priv->ict_tbl =  priv->ict_tbl_vir +
			  (priv->aligned_ict_tbl_dma - priv->ict_tbl_dma);

	IWL_DEBUG_ISR(priv, "ict vir addr %p vir aligned %p diff %d\n",
			     priv->ict_tbl, priv->ict_tbl_vir,
			(int)(priv->aligned_ict_tbl_dma - priv->ict_tbl_dma));

	/* reset table and index to all 0 */
	memset(priv->ict_tbl_vir,0, (sizeof(u32) * ICT_COUNT) + PAGE_SIZE);
	priv->ict_index = 0;

	/* add periodic RX interrupt */
	priv->inta_mask |= CSR_INT_BIT_RX_PERIODIC;
	return 0;
}
EXPORT_SYMBOL(iwl_alloc_isr_ict);

/* Device is going up inform it about using ICT interrupt table,
 * also we need to tell the driver to start using ICT interrupt.
 */
int iwl_reset_ict(struct iwl_priv *priv)
{
	u32 val;
	unsigned long flags;

	if (!priv->ict_tbl_vir)
		return 0;

	spin_lock_irqsave(&priv->lock, flags);
	iwl_disable_interrupts(priv);

	memset(&priv->ict_tbl[0], 0, sizeof(u32) * ICT_COUNT);

	val = priv->aligned_ict_tbl_dma >> PAGE_SHIFT;

	val |= CSR_DRAM_INT_TBL_ENABLE;
	val |= CSR_DRAM_INIT_TBL_WRAP_CHECK;

	IWL_DEBUG_ISR(priv, "CSR_DRAM_INT_TBL_REG =0x%X "
			"aligned dma address %Lx\n",
			val, (unsigned long long)priv->aligned_ict_tbl_dma);

	iwl_write32(priv, CSR_DRAM_INT_TBL_REG, val);
	priv->use_ict = true;
	priv->ict_index = 0;
	iwl_write32(priv, CSR_INT, priv->inta_mask);
	iwl_enable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}
EXPORT_SYMBOL(iwl_reset_ict);

/* Device is going down disable ict interrupt usage */
void iwl_disable_ict(struct iwl_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	priv->use_ict = false;
	spin_unlock_irqrestore(&priv->lock, flags);
}
EXPORT_SYMBOL(iwl_disable_ict);

/* interrupt handler using ict table, with this interrupt driver will
 * stop using INTA register to get device's interrupt, reading this register
 * is expensive, device will write interrupts in ICT dram table, increment
 * index then will fire interrupt to driver, driver will OR all ICT table
 * entries from current index up to table entry with 0 value. the result is
 * the interrupt we need to service, driver will set the entries back to 0 and
 * set index.
 */
irqreturn_t iwl_isr_ict(int irq, void *data)
{
	struct iwl_priv *priv = data;
	u32 inta, inta_mask;
	u32 val = 0;

	if (!priv)
		return IRQ_NONE;

	/* dram interrupt table not set yet,
	 * use legacy interrupt.
	 */
	if (!priv->use_ict)
		return iwl_isr(irq, data);

	spin_lock(&priv->lock);

	/* Disable (but don't clear!) interrupts here to avoid
	 * back-to-back ISRs and sporadic interrupts from our NIC.
	 * If we have something to service, the tasklet will re-enable ints.
	 * If we *don't* have something, we'll re-enable before leaving here.
	 */
	inta_mask = iwl_read32(priv, CSR_INT_MASK);  /* just for debug */
	iwl_write32(priv, CSR_INT_MASK, 0x00000000);


	/* Ignore interrupt if there's nothing in NIC to service.
	 * This may be due to IRQ shared with another device,
	 * or due to sporadic interrupts thrown from our NIC. */
	if (!priv->ict_tbl[priv->ict_index]) {
		IWL_DEBUG_ISR(priv, "Ignore interrupt, inta == 0\n");
		goto none;
	}

	/* read all entries that not 0 start with ict_index */
	while (priv->ict_tbl[priv->ict_index]) {

		val |= le32_to_cpu(priv->ict_tbl[priv->ict_index]);
		IWL_DEBUG_ISR(priv, "ICT index %d value 0x%08X\n",
				priv->ict_index,
				le32_to_cpu(priv->ict_tbl[priv->ict_index]));
		priv->ict_tbl[priv->ict_index] = 0;
		priv->ict_index = iwl_queue_inc_wrap(priv->ict_index,
						     ICT_COUNT);

	}

	/* We should not get this value, just ignore it. */
	if (val == 0xffffffff)
		val = 0;

	inta = (0xff & val) | ((0xff00 & val) << 16);
	IWL_DEBUG_ISR(priv, "ISR inta 0x%08x, enabled 0x%08x ict 0x%08x\n",
			inta, inta_mask, val);

	inta &= priv->inta_mask;
	priv->inta |= inta;

	/* iwl_irq_tasklet() will service interrupts and re-enable them */
	if (likely(inta))
		tasklet_schedule(&priv->irq_tasklet);
	else if (test_bit(STATUS_INT_ENABLED, &priv->status) && !priv->inta) {
		/* Allow interrupt if was disabled by this handler and
		 * no tasklet was schedules, We should not enable interrupt,
		 * tasklet will enable it.
		 */
		iwl_enable_interrupts(priv);
	}

	spin_unlock(&priv->lock);
	return IRQ_HANDLED;

 none:
	/* re-enable interrupts here since we don't have anything to service.
	 * only Re-enable if disabled by irq.
	 */
	if (test_bit(STATUS_INT_ENABLED, &priv->status) && !priv->inta)
		iwl_enable_interrupts(priv);

	spin_unlock(&priv->lock);
	return IRQ_NONE;
}
EXPORT_SYMBOL(iwl_isr_ict);


static irqreturn_t iwl_isr(int irq, void *data)
{
	struct iwl_priv *priv = data;
	u32 inta, inta_mask;
#ifdef CONFIG_IWLWIFI_DEBUG
	u32 inta_fh;
#endif
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

	/* Ignore interrupt if there's nothing in NIC to service.
	 * This may be due to IRQ shared with another device,
	 * or due to sporadic interrupts thrown from our NIC. */
	if (!inta) {
		IWL_DEBUG_ISR(priv, "Ignore interrupt, inta == 0\n");
		goto none;
	}

	if ((inta == 0xFFFFFFFF) || ((inta & 0xFFFFFFF0) == 0xa5a5a5a0)) {
		/* Hardware disappeared. It might have already raised
		 * an interrupt */
		IWL_WARN(priv, "HARDWARE GONE?? INTA == 0x%08x\n", inta);
		goto unplugged;
	}

#ifdef CONFIG_IWLWIFI_DEBUG
	if (iwl_get_debug_level(priv) & (IWL_DL_ISR)) {
		inta_fh = iwl_read32(priv, CSR_FH_INT_STATUS);
		IWL_DEBUG_ISR(priv, "ISR inta 0x%08x, enabled 0x%08x, "
			      "fh 0x%08x\n", inta, inta_mask, inta_fh);
	}
#endif

	priv->inta |= inta;
	/* iwl_irq_tasklet() will service interrupts and re-enable them */
	if (likely(inta))
		tasklet_schedule(&priv->irq_tasklet);
	else if (test_bit(STATUS_INT_ENABLED, &priv->status) && !priv->inta)
		iwl_enable_interrupts(priv);

 unplugged:
	spin_unlock(&priv->lock);
	return IRQ_HANDLED;

 none:
	/* re-enable interrupts here since we don't have anything to service. */
	/* only Re-enable if diabled by irq  and no schedules tasklet. */
	if (test_bit(STATUS_INT_ENABLED, &priv->status) && !priv->inta)
		iwl_enable_interrupts(priv);

	spin_unlock(&priv->lock);
	return IRQ_NONE;
}

irqreturn_t iwl_isr_legacy(int irq, void *data)
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
EXPORT_SYMBOL(iwl_isr_legacy);

int iwl_send_bt_config(struct iwl_priv *priv)
{
	struct iwl_bt_cmd bt_cmd = {
		.flags = BT_COEX_MODE_4W,
		.lead_time = BT_LEAD_TIME_DEF,
		.max_kill = BT_MAX_KILL_DEF,
		.kill_ack_mask = 0,
		.kill_cts_mask = 0,
	};

	return iwl_send_cmd_pdu(priv, REPLY_BT_CONFIG,
				sizeof(struct iwl_bt_cmd), &bt_cmd);
}
EXPORT_SYMBOL(iwl_send_bt_config);

int iwl_send_statistics_request(struct iwl_priv *priv, u8 flags, bool clear)
{
	struct iwl_statistics_cmd statistics_cmd = {
		.configuration_flags =
			clear ? IWL_STATS_CONF_CLEAR_STATS : 0,
	};

	if (flags & CMD_ASYNC)
		return iwl_send_cmd_pdu_async(priv, REPLY_STATISTICS_CMD,
					       sizeof(struct iwl_statistics_cmd),
					       &statistics_cmd, NULL);
	else
		return iwl_send_cmd_pdu(priv, REPLY_STATISTICS_CMD,
					sizeof(struct iwl_statistics_cmd),
					&statistics_cmd);
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


void iwl_rf_kill_ct_config(struct iwl_priv *priv)
{
	struct iwl_ct_kill_config cmd;
	struct iwl_ct_kill_throttling_config adv_cmd;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&priv->lock, flags);
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT);
	spin_unlock_irqrestore(&priv->lock, flags);
	priv->thermal_throttle.ct_kill_toggle = false;

	if (priv->cfg->support_ct_kill_exit) {
		adv_cmd.critical_temperature_enter =
			cpu_to_le32(priv->hw_params.ct_kill_threshold);
		adv_cmd.critical_temperature_exit =
			cpu_to_le32(priv->hw_params.ct_kill_exit_threshold);

		ret = iwl_send_cmd_pdu(priv, REPLY_CT_KILL_CONFIG_CMD,
				       sizeof(adv_cmd), &adv_cmd);
		if (ret)
			IWL_ERR(priv, "REPLY_CT_KILL_CONFIG_CMD failed\n");
		else
			IWL_DEBUG_INFO(priv, "REPLY_CT_KILL_CONFIG_CMD "
					"succeeded, "
					"critical temperature enter is %d,"
					"exit is %d\n",
				       priv->hw_params.ct_kill_threshold,
				       priv->hw_params.ct_kill_exit_threshold);
	} else {
		cmd.critical_temperature_R =
			cpu_to_le32(priv->hw_params.ct_kill_threshold);

		ret = iwl_send_cmd_pdu(priv, REPLY_CT_KILL_CONFIG_CMD,
				       sizeof(cmd), &cmd);
		if (ret)
			IWL_ERR(priv, "REPLY_CT_KILL_CONFIG_CMD failed\n");
		else
			IWL_DEBUG_INFO(priv, "REPLY_CT_KILL_CONFIG_CMD "
					"succeeded, "
					"critical temperature is %d\n",
					priv->hw_params.ct_kill_threshold);
	}
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
		.flags = meta_flag,
	};

	return iwl_send_cmd(priv, &cmd);
}

void iwl_rx_pm_sleep_notif(struct iwl_priv *priv,
			   struct iwl_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_sleep_notification *sleep = &(pkt->u.sleep_notif);
	IWL_DEBUG_RX(priv, "sleep mode: %d, src: %d\n",
		     sleep->pm_sleep_mode, sleep->pm_wakeup_src);
#endif
}
EXPORT_SYMBOL(iwl_rx_pm_sleep_notif);

void iwl_rx_pm_debug_statistics_notif(struct iwl_priv *priv,
				      struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	u32 len = le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK;
	IWL_DEBUG_RADIO(priv, "Dumping %d bytes of unhandled "
			"notification for %s:\n", len,
			get_cmd_string(pkt->hdr.cmd));
	iwl_print_hex_dump(priv, IWL_DL_RADIO, pkt->u.raw, len);
}
EXPORT_SYMBOL(iwl_rx_pm_debug_statistics_notif);

void iwl_rx_reply_error(struct iwl_priv *priv,
			struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);

	IWL_ERR(priv, "Error Reply type 0x%08X cmd %s (0x%02X) "
		"seq 0x%04X ser 0x%08X\n",
		le32_to_cpu(pkt->u.err_resp.error_type),
		get_cmd_string(pkt->u.err_resp.cmd_id),
		pkt->u.err_resp.cmd_id,
		le16_to_cpu(pkt->u.err_resp.bad_cmd_seq_num),
		le32_to_cpu(pkt->u.err_resp.error_info));
}
EXPORT_SYMBOL(iwl_rx_reply_error);

void iwl_clear_isr_stats(struct iwl_priv *priv)
{
	memset(&priv->isr_stats, 0, sizeof(priv->isr_stats));
}

int iwl_mac_conf_tx(struct ieee80211_hw *hw, u16 queue,
			   const struct ieee80211_tx_queue_params *params)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;
	int q;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - RF not ready\n");
		return -EIO;
	}

	if (queue >= AC_NUM) {
		IWL_DEBUG_MAC80211(priv, "leave - queue >= AC_NUM %d\n", queue);
		return 0;
	}

	q = AC_NUM - 1 - queue;

	spin_lock_irqsave(&priv->lock, flags);

	priv->qos_data.def_qos_parm.ac[q].cw_min = cpu_to_le16(params->cw_min);
	priv->qos_data.def_qos_parm.ac[q].cw_max = cpu_to_le16(params->cw_max);
	priv->qos_data.def_qos_parm.ac[q].aifsn = params->aifs;
	priv->qos_data.def_qos_parm.ac[q].edca_txop =
			cpu_to_le16((params->txop * 32));

	priv->qos_data.def_qos_parm.ac[q].reserved1 = 0;
	priv->qos_data.qos_active = 1;

	if (priv->iw_mode == NL80211_IFTYPE_AP)
		iwl_activate_qos(priv, 1);
	else if (priv->assoc_id && iwl_is_associated(priv))
		iwl_activate_qos(priv, 0);

	spin_unlock_irqrestore(&priv->lock, flags);

	IWL_DEBUG_MAC80211(priv, "leave\n");
	return 0;
}
EXPORT_SYMBOL(iwl_mac_conf_tx);

static void iwl_ht_conf(struct iwl_priv *priv,
			struct ieee80211_bss_conf *bss_conf)
{
	struct iwl_ht_config *ht_conf = &priv->current_ht_config;
	struct ieee80211_sta *sta;

	IWL_DEBUG_MAC80211(priv, "enter: \n");

	if (!ht_conf->is_ht)
		return;

	ht_conf->ht_protection =
		bss_conf->ht_operation_mode & IEEE80211_HT_OP_MODE_PROTECTION;
	ht_conf->non_GF_STA_present =
		!!(bss_conf->ht_operation_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);

	ht_conf->single_chain_sufficient = false;

	switch (priv->iw_mode) {
	case NL80211_IFTYPE_STATION:
		rcu_read_lock();
		sta = ieee80211_find_sta(priv->vif, priv->bssid);
		if (sta) {
			struct ieee80211_sta_ht_cap *ht_cap = &sta->ht_cap;
			int maxstreams;

			maxstreams = (ht_cap->mcs.tx_params &
				      IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK)
					>> IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT;
			maxstreams += 1;

			if ((ht_cap->mcs.rx_mask[1] == 0) &&
			    (ht_cap->mcs.rx_mask[2] == 0))
				ht_conf->single_chain_sufficient = true;
			if (maxstreams <= 1)
				ht_conf->single_chain_sufficient = true;
		} else {
			/*
			 * If at all, this can only happen through a race
			 * when the AP disconnects us while we're still
			 * setting up the connection, in that case mac80211
			 * will soon tell us about that.
			 */
			ht_conf->single_chain_sufficient = true;
		}
		rcu_read_unlock();
		break;
	case NL80211_IFTYPE_ADHOC:
		ht_conf->single_chain_sufficient = true;
		break;
	default:
		break;
	}

	IWL_DEBUG_MAC80211(priv, "leave\n");
}

static inline void iwl_set_no_assoc(struct iwl_priv *priv)
{
	priv->assoc_id = 0;
	iwl_led_disassociate(priv);
	/*
	 * inform the ucode that there is no longer an
	 * association and that no more packets should be
	 * sent
	 */
	priv->staging_rxon.filter_flags &=
		~RXON_FILTER_ASSOC_MSK;
	priv->staging_rxon.assoc_id = 0;
	iwlcore_commit_rxon(priv);
}

#define IWL_DELAY_NEXT_SCAN_AFTER_ASSOC (HZ*6)
void iwl_bss_info_changed(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *bss_conf,
			  u32 changes)
{
	struct iwl_priv *priv = hw->priv;
	int ret;

	IWL_DEBUG_MAC80211(priv, "changes = 0x%X\n", changes);

	if (!iwl_is_alive(priv))
		return;

	mutex_lock(&priv->mutex);

	if (changes & BSS_CHANGED_BEACON &&
	    priv->iw_mode == NL80211_IFTYPE_AP) {
		dev_kfree_skb(priv->ibss_beacon);
		priv->ibss_beacon = ieee80211_beacon_get(hw, vif);
	}

	if (changes & BSS_CHANGED_BEACON_INT) {
		priv->beacon_int = bss_conf->beacon_int;
		/* TODO: in AP mode, do something to make this take effect */
	}

	if (changes & BSS_CHANGED_BSSID) {
		IWL_DEBUG_MAC80211(priv, "BSSID %pM\n", bss_conf->bssid);

		/*
		 * If there is currently a HW scan going on in the
		 * background then we need to cancel it else the RXON
		 * below/in post_associate will fail.
		 */
		if (iwl_scan_cancel_timeout(priv, 100)) {
			IWL_WARN(priv, "Aborted scan still in progress after 100ms\n");
			IWL_DEBUG_MAC80211(priv, "leaving - scan abort failed.\n");
			mutex_unlock(&priv->mutex);
			return;
		}

		/* mac80211 only sets assoc when in STATION mode */
		if (priv->iw_mode == NL80211_IFTYPE_ADHOC ||
		    bss_conf->assoc) {
			memcpy(priv->staging_rxon.bssid_addr,
			       bss_conf->bssid, ETH_ALEN);

			/* currently needed in a few places */
			memcpy(priv->bssid, bss_conf->bssid, ETH_ALEN);
		} else {
			priv->staging_rxon.filter_flags &=
				~RXON_FILTER_ASSOC_MSK;
		}

	}

	/*
	 * This needs to be after setting the BSSID in case
	 * mac80211 decides to do both changes at once because
	 * it will invoke post_associate.
	 */
	if (priv->iw_mode == NL80211_IFTYPE_ADHOC &&
	    changes & BSS_CHANGED_BEACON) {
		struct sk_buff *beacon = ieee80211_beacon_get(hw, vif);

		if (beacon)
			iwl_mac_beacon_update(hw, beacon);
	}

	if (changes & BSS_CHANGED_ERP_PREAMBLE) {
		IWL_DEBUG_MAC80211(priv, "ERP_PREAMBLE %d\n",
				   bss_conf->use_short_preamble);
		if (bss_conf->use_short_preamble)
			priv->staging_rxon.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
		else
			priv->staging_rxon.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;
	}

	if (changes & BSS_CHANGED_ERP_CTS_PROT) {
		IWL_DEBUG_MAC80211(priv, "ERP_CTS %d\n", bss_conf->use_cts_prot);
		if (bss_conf->use_cts_prot && (priv->band != IEEE80211_BAND_5GHZ))
			priv->staging_rxon.flags |= RXON_FLG_TGG_PROTECT_MSK;
		else
			priv->staging_rxon.flags &= ~RXON_FLG_TGG_PROTECT_MSK;
	}

	if (changes & BSS_CHANGED_BASIC_RATES) {
		/* XXX use this information
		 *
		 * To do that, remove code from iwl_set_rate() and put something
		 * like this here:
		 *
		if (A-band)
			priv->staging_rxon.ofdm_basic_rates =
				bss_conf->basic_rates;
		else
			priv->staging_rxon.ofdm_basic_rates =
				bss_conf->basic_rates >> 4;
			priv->staging_rxon.cck_basic_rates =
				bss_conf->basic_rates & 0xF;
		 */
	}

	if (changes & BSS_CHANGED_HT) {
		iwl_ht_conf(priv, bss_conf);

		if (priv->cfg->ops->hcmd->set_rxon_chain)
			priv->cfg->ops->hcmd->set_rxon_chain(priv);
	}

	if (changes & BSS_CHANGED_ASSOC) {
		IWL_DEBUG_MAC80211(priv, "ASSOC %d\n", bss_conf->assoc);
		if (bss_conf->assoc) {
			priv->assoc_id = bss_conf->aid;
			priv->beacon_int = bss_conf->beacon_int;
			priv->timestamp = bss_conf->timestamp;
			priv->assoc_capability = bss_conf->assoc_capability;

			iwl_led_associate(priv);

			/*
			 * We have just associated, don't start scan too early
			 * leave time for EAPOL exchange to complete.
			 *
			 * XXX: do this in mac80211
			 */
			priv->next_scan_jiffies = jiffies +
					IWL_DELAY_NEXT_SCAN_AFTER_ASSOC;
			if (!iwl_is_rfkill(priv))
				priv->cfg->ops->lib->post_associate(priv);
		} else
			iwl_set_no_assoc(priv);
	}

	if (changes && iwl_is_associated(priv) && priv->assoc_id) {
		IWL_DEBUG_MAC80211(priv, "Changes (%#x) while associated\n",
				   changes);
		ret = iwl_send_rxon_assoc(priv);
		if (!ret) {
			/* Sync active_rxon with latest change. */
			memcpy((void *)&priv->active_rxon,
				&priv->staging_rxon,
				sizeof(struct iwl_rxon_cmd));
		}
	}

	if (changes & BSS_CHANGED_BEACON_ENABLED) {
		if (vif->bss_conf.enable_beacon) {
			memcpy(priv->staging_rxon.bssid_addr,
			       bss_conf->bssid, ETH_ALEN);
			memcpy(priv->bssid, bss_conf->bssid, ETH_ALEN);
			iwlcore_config_ap(priv);
		} else
			iwl_set_no_assoc(priv);
	}

	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211(priv, "leave\n");
}
EXPORT_SYMBOL(iwl_bss_info_changed);

int iwl_mac_beacon_update(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;
	__le64 timestamp;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - RF not ready\n");
		return -EIO;
	}

	if (priv->iw_mode != NL80211_IFTYPE_ADHOC) {
		IWL_DEBUG_MAC80211(priv, "leave - not IBSS\n");
		return -EIO;
	}

	spin_lock_irqsave(&priv->lock, flags);

	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);

	priv->ibss_beacon = skb;

	priv->assoc_id = 0;
	timestamp = ((struct ieee80211_mgmt *)skb->data)->u.beacon.timestamp;
	priv->timestamp = le64_to_cpu(timestamp);

	IWL_DEBUG_MAC80211(priv, "leave\n");
	spin_unlock_irqrestore(&priv->lock, flags);

	iwl_reset_qos(priv);

	priv->cfg->ops->lib->post_associate(priv);


	return 0;
}
EXPORT_SYMBOL(iwl_mac_beacon_update);

int iwl_set_mode(struct iwl_priv *priv, int mode)
{
	if (mode == NL80211_IFTYPE_ADHOC) {
		const struct iwl_channel_info *ch_info;

		ch_info = iwl_get_channel_info(priv,
			priv->band,
			le16_to_cpu(priv->staging_rxon.channel));

		if (!ch_info || !is_channel_ibss(ch_info)) {
			IWL_ERR(priv, "channel %d not IBSS channel\n",
				  le16_to_cpu(priv->staging_rxon.channel));
			return -EINVAL;
		}
	}

	iwl_connection_init_rx_config(priv, mode);

	if (priv->cfg->ops->hcmd->set_rxon_chain)
		priv->cfg->ops->hcmd->set_rxon_chain(priv);

	memcpy(priv->staging_rxon.node_addr, priv->mac_addr, ETH_ALEN);

	iwl_clear_stations_table(priv);

	/* dont commit rxon if rf-kill is on*/
	if (!iwl_is_ready_rf(priv))
		return -EAGAIN;

	iwlcore_commit_rxon(priv);

	return 0;
}
EXPORT_SYMBOL(iwl_set_mode);

int iwl_mac_add_interface(struct ieee80211_hw *hw,
				 struct ieee80211_if_init_conf *conf)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;

	IWL_DEBUG_MAC80211(priv, "enter: type %d\n", conf->type);

	if (priv->vif) {
		IWL_DEBUG_MAC80211(priv, "leave - vif != NULL\n");
		return -EOPNOTSUPP;
	}

	spin_lock_irqsave(&priv->lock, flags);
	priv->vif = conf->vif;
	priv->iw_mode = conf->type;

	spin_unlock_irqrestore(&priv->lock, flags);

	mutex_lock(&priv->mutex);

	if (conf->mac_addr) {
		IWL_DEBUG_MAC80211(priv, "Set %pM\n", conf->mac_addr);
		memcpy(priv->mac_addr, conf->mac_addr, ETH_ALEN);
	}

	if (iwl_set_mode(priv, conf->type) == -EAGAIN)
		/* we are not ready, will run again when ready */
		set_bit(STATUS_MODE_PENDING, &priv->status);

	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211(priv, "leave\n");
	return 0;
}
EXPORT_SYMBOL(iwl_mac_add_interface);

void iwl_mac_remove_interface(struct ieee80211_hw *hw,
				     struct ieee80211_if_init_conf *conf)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	mutex_lock(&priv->mutex);

	if (iwl_is_ready_rf(priv)) {
		iwl_scan_cancel_timeout(priv, 100);
		priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwlcore_commit_rxon(priv);
	}
	if (priv->vif == conf->vif) {
		priv->vif = NULL;
		memset(priv->bssid, 0, ETH_ALEN);
	}
	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211(priv, "leave\n");

}
EXPORT_SYMBOL(iwl_mac_remove_interface);

/**
 * iwl_mac_config - mac80211 config callback
 *
 * We ignore conf->flags & IEEE80211_CONF_SHORT_SLOT_TIME since it seems to
 * be set inappropriately and the driver currently sets the hardware up to
 * use it whenever needed.
 */
int iwl_mac_config(struct ieee80211_hw *hw, u32 changed)
{
	struct iwl_priv *priv = hw->priv;
	const struct iwl_channel_info *ch_info;
	struct ieee80211_conf *conf = &hw->conf;
	struct iwl_ht_config *ht_conf = &priv->current_ht_config;
	unsigned long flags = 0;
	int ret = 0;
	u16 ch;
	int scan_active = 0;

	mutex_lock(&priv->mutex);

	IWL_DEBUG_MAC80211(priv, "enter to channel %d changed 0x%X\n",
					conf->channel->hw_value, changed);

	if (unlikely(!priv->cfg->mod_params->disable_hw_scan &&
			test_bit(STATUS_SCANNING, &priv->status))) {
		scan_active = 1;
		IWL_DEBUG_MAC80211(priv, "leave - scanning\n");
	}


	/* during scanning mac80211 will delay channel setting until
	 * scan finish with changed = 0
	 */
	if (!changed || (changed & IEEE80211_CONF_CHANGE_CHANNEL)) {
		if (scan_active)
			goto set_ch_out;

		ch = ieee80211_frequency_to_channel(conf->channel->center_freq);
		ch_info = iwl_get_channel_info(priv, conf->channel->band, ch);
		if (!is_channel_valid(ch_info)) {
			IWL_DEBUG_MAC80211(priv, "leave - invalid channel\n");
			ret = -EINVAL;
			goto set_ch_out;
		}

		if (priv->iw_mode == NL80211_IFTYPE_ADHOC &&
			!is_channel_ibss(ch_info)) {
			IWL_ERR(priv, "channel %d in band %d not "
				"IBSS channel\n",
				conf->channel->hw_value, conf->channel->band);
			ret = -EINVAL;
			goto set_ch_out;
		}

		spin_lock_irqsave(&priv->lock, flags);

		/* Configure HT40 channels */
		ht_conf->is_ht = conf_is_ht(conf);
		if (ht_conf->is_ht) {
			if (conf_is_ht40_minus(conf)) {
				ht_conf->extension_chan_offset =
					IEEE80211_HT_PARAM_CHA_SEC_BELOW;
				ht_conf->is_40mhz = true;
			} else if (conf_is_ht40_plus(conf)) {
				ht_conf->extension_chan_offset =
					IEEE80211_HT_PARAM_CHA_SEC_ABOVE;
				ht_conf->is_40mhz = true;
			} else {
				ht_conf->extension_chan_offset =
					IEEE80211_HT_PARAM_CHA_SEC_NONE;
				ht_conf->is_40mhz = false;
			}
		} else
			ht_conf->is_40mhz = false;
		/* Default to no protection. Protection mode will later be set
		 * from BSS config in iwl_ht_conf */
		ht_conf->ht_protection = IEEE80211_HT_OP_MODE_PROTECTION_NONE;

		/* if we are switching from ht to 2.4 clear flags
		 * from any ht related info since 2.4 does not
		 * support ht */
		if ((le16_to_cpu(priv->staging_rxon.channel) != ch))
			priv->staging_rxon.flags = 0;

		iwl_set_rxon_channel(priv, conf->channel);

		iwl_set_flags_for_band(priv, conf->channel->band);
		spin_unlock_irqrestore(&priv->lock, flags);
		if (iwl_is_associated(priv) &&
		    (le16_to_cpu(priv->active_rxon.channel) != ch) &&
		    priv->cfg->ops->lib->set_channel_switch) {
			iwl_set_rate(priv);
			/*
			 * at this point, staging_rxon has the
			 * configuration for channel switch
			 */
			ret = priv->cfg->ops->lib->set_channel_switch(priv,
				ch);
			if (!ret) {
				iwl_print_rx_config_cmd(priv);
				goto out;
			}
			priv->switch_rxon.switch_in_progress = false;
		}
 set_ch_out:
		/* The list of supported rates and rate mask can be different
		 * for each band; since the band may have changed, reset
		 * the rate mask to what mac80211 lists */
		iwl_set_rate(priv);
	}

	if (changed & (IEEE80211_CONF_CHANGE_PS |
			IEEE80211_CONF_CHANGE_IDLE)) {
		ret = iwl_power_update_mode(priv, false);
		if (ret)
			IWL_DEBUG_MAC80211(priv, "Error setting sleep level\n");
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		IWL_DEBUG_MAC80211(priv, "TX Power old=%d new=%d\n",
			priv->tx_power_user_lmt, conf->power_level);

		iwl_set_tx_power(priv, conf->power_level, false);
	}

	/* call to ensure that 4965 rx_chain is set properly in monitor mode */
	if (priv->cfg->ops->hcmd->set_rxon_chain)
		priv->cfg->ops->hcmd->set_rxon_chain(priv);

	if (!iwl_is_ready(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - not ready\n");
		goto out;
	}

	if (scan_active)
		goto out;

	if (memcmp(&priv->active_rxon,
		   &priv->staging_rxon, sizeof(priv->staging_rxon)))
		iwlcore_commit_rxon(priv);
	else
		IWL_DEBUG_INFO(priv, "Not re-sending same RXON configuration.\n");


out:
	IWL_DEBUG_MAC80211(priv, "leave\n");
	mutex_unlock(&priv->mutex);
	return ret;
}
EXPORT_SYMBOL(iwl_mac_config);

int iwl_mac_get_tx_stats(struct ieee80211_hw *hw,
			 struct ieee80211_tx_queue_stats *stats)
{
	struct iwl_priv *priv = hw->priv;
	int i, avail;
	struct iwl_tx_queue *txq;
	struct iwl_queue *q;
	unsigned long flags;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - RF not ready\n");
		return -EIO;
	}

	spin_lock_irqsave(&priv->lock, flags);

	for (i = 0; i < AC_NUM; i++) {
		txq = &priv->txq[i];
		q = &txq->q;
		avail = iwl_queue_space(q);

		stats[i].len = q->n_window - avail;
		stats[i].limit = q->n_window - q->high_mark;
		stats[i].count = q->n_window;

	}
	spin_unlock_irqrestore(&priv->lock, flags);

	IWL_DEBUG_MAC80211(priv, "leave\n");

	return 0;
}
EXPORT_SYMBOL(iwl_mac_get_tx_stats);

void iwl_mac_reset_tsf(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;

	mutex_lock(&priv->mutex);
	IWL_DEBUG_MAC80211(priv, "enter\n");

	spin_lock_irqsave(&priv->lock, flags);
	memset(&priv->current_ht_config, 0, sizeof(struct iwl_ht_config));
	spin_unlock_irqrestore(&priv->lock, flags);

	iwl_reset_qos(priv);

	spin_lock_irqsave(&priv->lock, flags);
	priv->assoc_id = 0;
	priv->assoc_capability = 0;
	priv->assoc_station_added = 0;

	/* new association get rid of ibss beacon skb */
	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);

	priv->ibss_beacon = NULL;

	priv->beacon_int = priv->vif->bss_conf.beacon_int;
	priv->timestamp = 0;
	if ((priv->iw_mode == NL80211_IFTYPE_STATION))
		priv->beacon_int = 0;

	spin_unlock_irqrestore(&priv->lock, flags);

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - not ready\n");
		mutex_unlock(&priv->mutex);
		return;
	}

	/* we are restarting association process
	 * clear RXON_FILTER_ASSOC_MSK bit
	 */
	if (priv->iw_mode != NL80211_IFTYPE_AP) {
		iwl_scan_cancel_timeout(priv, 100);
		priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwlcore_commit_rxon(priv);
	}

	if (priv->iw_mode != NL80211_IFTYPE_ADHOC) {
		IWL_DEBUG_MAC80211(priv, "leave - not in IBSS\n");
		mutex_unlock(&priv->mutex);
		return;
	}

	iwl_set_rate(priv);

	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211(priv, "leave\n");
}
EXPORT_SYMBOL(iwl_mac_reset_tsf);

int iwl_alloc_txq_mem(struct iwl_priv *priv)
{
	if (!priv->txq)
		priv->txq = kzalloc(
			sizeof(struct iwl_tx_queue) * priv->cfg->num_of_queues,
			GFP_KERNEL);
	if (!priv->txq) {
		IWL_ERR(priv, "Not enough memory for txq \n");
		return -ENOMEM;
	}
	return 0;
}
EXPORT_SYMBOL(iwl_alloc_txq_mem);

void iwl_free_txq_mem(struct iwl_priv *priv)
{
	kfree(priv->txq);
	priv->txq = NULL;
}
EXPORT_SYMBOL(iwl_free_txq_mem);

int iwl_send_wimax_coex(struct iwl_priv *priv)
{
	struct iwl_wimax_coex_cmd uninitialized_var(coex_cmd);

	if (priv->cfg->support_wimax_coexist) {
		/* UnMask wake up src at associated sleep */
		coex_cmd.flags |= COEX_FLAGS_ASSOC_WA_UNMASK_MSK;

		/* UnMask wake up src at unassociated sleep */
		coex_cmd.flags |= COEX_FLAGS_UNASSOC_WA_UNMASK_MSK;
		memcpy(coex_cmd.sta_prio, cu_priorities,
			sizeof(struct iwl_wimax_coex_event_entry) *
			 COEX_NUM_OF_EVENTS);

		/* enabling the coexistence feature */
		coex_cmd.flags |= COEX_FLAGS_COEX_ENABLE_MSK;

		/* enabling the priorities tables */
		coex_cmd.flags |= COEX_FLAGS_STA_TABLE_VALID_MSK;
	} else {
		/* coexistence is disabled */
		memset(&coex_cmd, 0, sizeof(coex_cmd));
	}
	return iwl_send_cmd_pdu(priv, COEX_PRIORITY_TABLE_CMD,
				sizeof(coex_cmd), &coex_cmd);
}
EXPORT_SYMBOL(iwl_send_wimax_coex);

#ifdef CONFIG_IWLWIFI_DEBUGFS

#define IWL_TRAFFIC_DUMP_SIZE	(IWL_TRAFFIC_ENTRY_SIZE * IWL_TRAFFIC_ENTRIES)

void iwl_reset_traffic_log(struct iwl_priv *priv)
{
	priv->tx_traffic_idx = 0;
	priv->rx_traffic_idx = 0;
	if (priv->tx_traffic)
		memset(priv->tx_traffic, 0, IWL_TRAFFIC_DUMP_SIZE);
	if (priv->rx_traffic)
		memset(priv->rx_traffic, 0, IWL_TRAFFIC_DUMP_SIZE);
}

int iwl_alloc_traffic_mem(struct iwl_priv *priv)
{
	u32 traffic_size = IWL_TRAFFIC_DUMP_SIZE;

	if (iwl_debug_level & IWL_DL_TX) {
		if (!priv->tx_traffic) {
			priv->tx_traffic =
				kzalloc(traffic_size, GFP_KERNEL);
			if (!priv->tx_traffic)
				return -ENOMEM;
		}
	}
	if (iwl_debug_level & IWL_DL_RX) {
		if (!priv->rx_traffic) {
			priv->rx_traffic =
				kzalloc(traffic_size, GFP_KERNEL);
			if (!priv->rx_traffic)
				return -ENOMEM;
		}
	}
	iwl_reset_traffic_log(priv);
	return 0;
}
EXPORT_SYMBOL(iwl_alloc_traffic_mem);

void iwl_free_traffic_mem(struct iwl_priv *priv)
{
	kfree(priv->tx_traffic);
	priv->tx_traffic = NULL;

	kfree(priv->rx_traffic);
	priv->rx_traffic = NULL;
}
EXPORT_SYMBOL(iwl_free_traffic_mem);

void iwl_dbg_log_tx_data_frame(struct iwl_priv *priv,
		      u16 length, struct ieee80211_hdr *header)
{
	__le16 fc;
	u16 len;

	if (likely(!(iwl_debug_level & IWL_DL_TX)))
		return;

	if (!priv->tx_traffic)
		return;

	fc = header->frame_control;
	if (ieee80211_is_data(fc)) {
		len = (length > IWL_TRAFFIC_ENTRY_SIZE)
		       ? IWL_TRAFFIC_ENTRY_SIZE : length;
		memcpy((priv->tx_traffic +
		       (priv->tx_traffic_idx * IWL_TRAFFIC_ENTRY_SIZE)),
		       header, len);
		priv->tx_traffic_idx =
			(priv->tx_traffic_idx + 1) % IWL_TRAFFIC_ENTRIES;
	}
}
EXPORT_SYMBOL(iwl_dbg_log_tx_data_frame);

void iwl_dbg_log_rx_data_frame(struct iwl_priv *priv,
		      u16 length, struct ieee80211_hdr *header)
{
	__le16 fc;
	u16 len;

	if (likely(!(iwl_debug_level & IWL_DL_RX)))
		return;

	if (!priv->rx_traffic)
		return;

	fc = header->frame_control;
	if (ieee80211_is_data(fc)) {
		len = (length > IWL_TRAFFIC_ENTRY_SIZE)
		       ? IWL_TRAFFIC_ENTRY_SIZE : length;
		memcpy((priv->rx_traffic +
		       (priv->rx_traffic_idx * IWL_TRAFFIC_ENTRY_SIZE)),
		       header, len);
		priv->rx_traffic_idx =
			(priv->rx_traffic_idx + 1) % IWL_TRAFFIC_ENTRIES;
	}
}
EXPORT_SYMBOL(iwl_dbg_log_rx_data_frame);

const char *get_mgmt_string(int cmd)
{
	switch (cmd) {
		IWL_CMD(MANAGEMENT_ASSOC_REQ);
		IWL_CMD(MANAGEMENT_ASSOC_RESP);
		IWL_CMD(MANAGEMENT_REASSOC_REQ);
		IWL_CMD(MANAGEMENT_REASSOC_RESP);
		IWL_CMD(MANAGEMENT_PROBE_REQ);
		IWL_CMD(MANAGEMENT_PROBE_RESP);
		IWL_CMD(MANAGEMENT_BEACON);
		IWL_CMD(MANAGEMENT_ATIM);
		IWL_CMD(MANAGEMENT_DISASSOC);
		IWL_CMD(MANAGEMENT_AUTH);
		IWL_CMD(MANAGEMENT_DEAUTH);
		IWL_CMD(MANAGEMENT_ACTION);
	default:
		return "UNKNOWN";

	}
}

const char *get_ctrl_string(int cmd)
{
	switch (cmd) {
		IWL_CMD(CONTROL_BACK_REQ);
		IWL_CMD(CONTROL_BACK);
		IWL_CMD(CONTROL_PSPOLL);
		IWL_CMD(CONTROL_RTS);
		IWL_CMD(CONTROL_CTS);
		IWL_CMD(CONTROL_ACK);
		IWL_CMD(CONTROL_CFEND);
		IWL_CMD(CONTROL_CFENDACK);
	default:
		return "UNKNOWN";

	}
}

void iwl_clear_traffic_stats(struct iwl_priv *priv)
{
	memset(&priv->tx_stats, 0, sizeof(struct traffic_stats));
	memset(&priv->rx_stats, 0, sizeof(struct traffic_stats));
	priv->led_tpt = 0;
}

/*
 * if CONFIG_IWLWIFI_DEBUGFS defined, iwl_update_stats function will
 * record all the MGMT, CTRL and DATA pkt for both TX and Rx pass.
 * Use debugFs to display the rx/rx_statistics
 * if CONFIG_IWLWIFI_DEBUGFS not being defined, then no MGMT and CTRL
 * information will be recorded, but DATA pkt still will be recorded
 * for the reason of iwl_led.c need to control the led blinking based on
 * number of tx and rx data.
 *
 */
void iwl_update_stats(struct iwl_priv *priv, bool is_tx, __le16 fc, u16 len)
{
	struct traffic_stats	*stats;

	if (is_tx)
		stats = &priv->tx_stats;
	else
		stats = &priv->rx_stats;

	if (ieee80211_is_mgmt(fc)) {
		switch (fc & cpu_to_le16(IEEE80211_FCTL_STYPE)) {
		case cpu_to_le16(IEEE80211_STYPE_ASSOC_REQ):
			stats->mgmt[MANAGEMENT_ASSOC_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ASSOC_RESP):
			stats->mgmt[MANAGEMENT_ASSOC_RESP]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_REASSOC_REQ):
			stats->mgmt[MANAGEMENT_REASSOC_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_REASSOC_RESP):
			stats->mgmt[MANAGEMENT_REASSOC_RESP]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_PROBE_REQ):
			stats->mgmt[MANAGEMENT_PROBE_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_PROBE_RESP):
			stats->mgmt[MANAGEMENT_PROBE_RESP]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_BEACON):
			stats->mgmt[MANAGEMENT_BEACON]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ATIM):
			stats->mgmt[MANAGEMENT_ATIM]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_DISASSOC):
			stats->mgmt[MANAGEMENT_DISASSOC]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_AUTH):
			stats->mgmt[MANAGEMENT_AUTH]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_DEAUTH):
			stats->mgmt[MANAGEMENT_DEAUTH]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ACTION):
			stats->mgmt[MANAGEMENT_ACTION]++;
			break;
		}
	} else if (ieee80211_is_ctl(fc)) {
		switch (fc & cpu_to_le16(IEEE80211_FCTL_STYPE)) {
		case cpu_to_le16(IEEE80211_STYPE_BACK_REQ):
			stats->ctrl[CONTROL_BACK_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_BACK):
			stats->ctrl[CONTROL_BACK]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_PSPOLL):
			stats->ctrl[CONTROL_PSPOLL]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_RTS):
			stats->ctrl[CONTROL_RTS]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_CTS):
			stats->ctrl[CONTROL_CTS]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ACK):
			stats->ctrl[CONTROL_ACK]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_CFEND):
			stats->ctrl[CONTROL_CFEND]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_CFENDACK):
			stats->ctrl[CONTROL_CFENDACK]++;
			break;
		}
	} else {
		/* data */
		stats->data_cnt++;
		stats->data_bytes += len;
	}
	iwl_leds_background(priv);
}
EXPORT_SYMBOL(iwl_update_stats);
#endif

#ifdef CONFIG_PM

int iwl_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct iwl_priv *priv = pci_get_drvdata(pdev);

	/*
	 * This function is called when system goes into suspend state
	 * mac80211 will call iwl_mac_stop() from the mac80211 suspend function
	 * first but since iwl_mac_stop() has no knowledge of who the caller is,
	 * it will not call apm_ops.stop() to stop the DMA operation.
	 * Calling apm_ops.stop here to make sure we stop the DMA.
	 */
	priv->cfg->ops->lib->apm_ops.stop(priv);

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}
EXPORT_SYMBOL(iwl_pci_suspend);

int iwl_pci_resume(struct pci_dev *pdev)
{
	struct iwl_priv *priv = pci_get_drvdata(pdev);
	int ret;

	pci_set_power_state(pdev, PCI_D0);
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	pci_restore_state(pdev);
	iwl_enable_interrupts(priv);

	return 0;
}
EXPORT_SYMBOL(iwl_pci_resume);

#endif /* CONFIG_PM */
