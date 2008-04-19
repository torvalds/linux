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
#include <linux/version.h>
#include <net/mac80211.h>

struct iwl_priv; /* FIXME: remove */
#include "iwl-debug.h"
#include "iwl-eeprom.h"
#include "iwl-4965.h" /* FIXME: remove */
#include "iwl-core.h"
#include "iwl-rfkill.h"


MODULE_DESCRIPTION("iwl core");
MODULE_VERSION(IWLWIFI_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT);
MODULE_LICENSE("GPL");

#ifdef CONFIG_IWLWIFI_DEBUG
u32 iwl_debug_level;
EXPORT_SYMBOL(iwl_debug_level);
#endif

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

/**
 * iwlcore_clear_stations_table - Clear the driver's station table
 *
 * NOTE:  This does not clear or otherwise alter the device's station table.
 */
void iwlcore_clear_stations_table(struct iwl_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_lock, flags);

	priv->num_stations = 0;
	memset(priv->stations, 0, sizeof(priv->stations));

	spin_unlock_irqrestore(&priv->sta_lock, flags);
}
EXPORT_SYMBOL(iwlcore_clear_stations_table);

void iwlcore_reset_qos(struct iwl_priv *priv)
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
EXPORT_SYMBOL(iwlcore_reset_qos);

/**
 * iwlcore_set_rxon_channel - Set the phymode and channel values in staging RXON
 * @phymode: MODE_IEEE80211A sets to 5.2GHz; all else set to 2.4GHz
 * @channel: Any channel valid for the requested phymode

 * In addition to setting the staging RXON, priv->phymode is also set.
 *
 * NOTE:  Does not commit to the hardware; it sets appropriate bit fields
 * in the staging RXON flag structure based on the phymode
 */
int iwlcore_set_rxon_channel(struct iwl_priv *priv,
				enum ieee80211_band band,
				u16 channel)
{
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
EXPORT_SYMBOL(iwlcore_set_rxon_channel);

static void iwlcore_init_hw(struct iwl_priv *priv)
{
	struct ieee80211_hw *hw = priv->hw;
	hw->rate_control_algorithm = "iwl-4965-rs";

	/* Tell mac80211 and its clients (e.g. Wireless Extensions)
	 *	 the range of signal quality values that we'll provide.
	 * Negative values for level/noise indicate that we'll provide dBm.
	 * For WE, at least, non-0 values here *enable* display of values
	 *	 in app (iwconfig). */
	hw->max_rssi = -20; /* signal level, negative indicates dBm */
	hw->max_noise = -20;	/* noise level, negative indicates dBm */
	hw->max_signal = 100;	/* link quality indication (%) */

	/* Tell mac80211 our Tx characteristics */
	hw->flags = IEEE80211_HW_HOST_GEN_BEACON_TEMPLATE;

	/* Default value; 4 EDCA QOS priorities */
	hw->queues = 4;
#ifdef CONFIG_IWL4965_HT
	/* Enhanced value; more queues, to support 11n aggregation */
	hw->queues = 16;
#endif /* CONFIG_IWL4965_HT */
}

int iwl_setup(struct iwl_priv *priv)
{
	int ret = 0;
	iwlcore_init_hw(priv);
	ret = priv->cfg->ops->lib->init_drv(priv);
	return ret;
}
EXPORT_SYMBOL(iwl_setup);

/* Low level driver call this function to update iwlcore with
 * driver status.
 */
int iwlcore_low_level_notify(struct iwl_priv *priv,
			      enum iwlcore_card_notify notify)
{
	int ret;
	switch (notify) {
	case IWLCORE_INIT_EVT:
		ret = iwl_rfkill_init(priv);
		if (ret)
			IWL_ERROR("Unable to initialize RFKILL system. "
				  "Ignoring error: %d\n", ret);
		break;
	case IWLCORE_START_EVT:
		break;
	case IWLCORE_STOP_EVT:
		break;
	case IWLCORE_REMOVE_EVT:
		iwl_rfkill_unregister(priv);
		break;
	}

	return 0;
}
EXPORT_SYMBOL(iwlcore_low_level_notify);

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

