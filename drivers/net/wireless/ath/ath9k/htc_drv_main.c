/*
 * Copyright (c) 2010 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "htc.h"

#ifdef CONFIG_ATH9K_HTC_DEBUGFS
static struct dentry *ath9k_debugfs_root;
#endif

/*************/
/* Utilities */
/*************/

/* HACK Alert: Use 11NG for 2.4, use 11NA for 5 */
static enum htc_phymode ath9k_htc_get_curmode(struct ath9k_htc_priv *priv,
					      struct ath9k_channel *ichan)
{
	enum htc_phymode mode;

	mode = HTC_MODE_AUTO;

	switch (ichan->chanmode) {
	case CHANNEL_G:
	case CHANNEL_G_HT20:
	case CHANNEL_G_HT40PLUS:
	case CHANNEL_G_HT40MINUS:
		mode = HTC_MODE_11NG;
		break;
	case CHANNEL_A:
	case CHANNEL_A_HT20:
	case CHANNEL_A_HT40PLUS:
	case CHANNEL_A_HT40MINUS:
		mode = HTC_MODE_11NA;
		break;
	default:
		break;
	}

	return mode;
}

bool ath9k_htc_setpower(struct ath9k_htc_priv *priv,
			enum ath9k_power_mode mode)
{
	bool ret;

	mutex_lock(&priv->htc_pm_lock);
	ret = ath9k_hw_setpower(priv->ah, mode);
	mutex_unlock(&priv->htc_pm_lock);

	return ret;
}

void ath9k_htc_ps_wakeup(struct ath9k_htc_priv *priv)
{
	mutex_lock(&priv->htc_pm_lock);
	if (++priv->ps_usecount != 1)
		goto unlock;
	ath9k_hw_setpower(priv->ah, ATH9K_PM_AWAKE);

unlock:
	mutex_unlock(&priv->htc_pm_lock);
}

void ath9k_htc_ps_restore(struct ath9k_htc_priv *priv)
{
	mutex_lock(&priv->htc_pm_lock);
	if (--priv->ps_usecount != 0)
		goto unlock;

	if (priv->ps_idle)
		ath9k_hw_setpower(priv->ah, ATH9K_PM_FULL_SLEEP);
	else if (priv->ps_enabled)
		ath9k_hw_setpower(priv->ah, ATH9K_PM_NETWORK_SLEEP);

unlock:
	mutex_unlock(&priv->htc_pm_lock);
}

void ath9k_ps_work(struct work_struct *work)
{
	struct ath9k_htc_priv *priv =
		container_of(work, struct ath9k_htc_priv,
			     ps_work);
	ath9k_htc_setpower(priv, ATH9K_PM_AWAKE);

	/* The chip wakes up after receiving the first beacon
	   while network sleep is enabled. For the driver to
	   be in sync with the hw, set the chip to awake and
	   only then set it to sleep.
	 */
	ath9k_htc_setpower(priv, ATH9K_PM_NETWORK_SLEEP);
}

static void ath9k_htc_vif_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct ath9k_htc_priv *priv = data;
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;

	if ((vif->type == NL80211_IFTYPE_AP) && bss_conf->enable_beacon)
		priv->reconfig_beacon = true;

	if (bss_conf->assoc) {
		priv->rearm_ani = true;
		priv->reconfig_beacon = true;
	}
}

static void ath9k_htc_vif_reconfig(struct ath9k_htc_priv *priv)
{
	priv->rearm_ani = false;
	priv->reconfig_beacon = false;

	ieee80211_iterate_active_interfaces_atomic(priv->hw,
						   ath9k_htc_vif_iter, priv);
	if (priv->rearm_ani)
		ath9k_htc_start_ani(priv);

	if (priv->reconfig_beacon) {
		ath9k_htc_ps_wakeup(priv);
		ath9k_htc_beacon_reconfig(priv);
		ath9k_htc_ps_restore(priv);
	}
}

static void ath9k_htc_bssid_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct ath9k_vif_iter_data *iter_data = data;
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		iter_data->mask[i] &= ~(iter_data->hw_macaddr[i] ^ mac[i]);
}

static void ath9k_htc_set_bssid_mask(struct ath9k_htc_priv *priv,
				     struct ieee80211_vif *vif)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath9k_vif_iter_data iter_data;

	/*
	 * Use the hardware MAC address as reference, the hardware uses it
	 * together with the BSSID mask when matching addresses.
	 */
	iter_data.hw_macaddr = common->macaddr;
	memset(&iter_data.mask, 0xff, ETH_ALEN);

	if (vif)
		ath9k_htc_bssid_iter(&iter_data, vif->addr, vif);

	/* Get list of all active MAC addresses */
	ieee80211_iterate_active_interfaces_atomic(priv->hw, ath9k_htc_bssid_iter,
						   &iter_data);

	memcpy(common->bssidmask, iter_data.mask, ETH_ALEN);
	ath_hw_setbssidmask(common);
}

static void ath9k_htc_set_opmode(struct ath9k_htc_priv *priv)
{
	if (priv->num_ibss_vif)
		priv->ah->opmode = NL80211_IFTYPE_ADHOC;
	else if (priv->num_ap_vif)
		priv->ah->opmode = NL80211_IFTYPE_AP;
	else
		priv->ah->opmode = NL80211_IFTYPE_STATION;

	ath9k_hw_setopmode(priv->ah);
}

void ath9k_htc_reset(struct ath9k_htc_priv *priv)
{
	struct ath_hw *ah = priv->ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_channel *channel = priv->hw->conf.channel;
	struct ath9k_hw_cal_data *caldata = NULL;
	enum htc_phymode mode;
	__be16 htc_mode;
	u8 cmd_rsp;
	int ret;

	mutex_lock(&priv->mutex);
	ath9k_htc_ps_wakeup(priv);

	ath9k_htc_stop_ani(priv);
	ieee80211_stop_queues(priv->hw);
	htc_stop(priv->htc);
	WMI_CMD(WMI_DISABLE_INTR_CMDID);
	WMI_CMD(WMI_DRAIN_TXQ_ALL_CMDID);
	WMI_CMD(WMI_STOP_RECV_CMDID);

	caldata = &priv->caldata;
	ret = ath9k_hw_reset(ah, ah->curchan, caldata, false);
	if (ret) {
		ath_err(common,
			"Unable to reset device (%u Mhz) reset status %d\n",
			channel->center_freq, ret);
	}

	ath9k_cmn_update_txpow(ah, priv->curtxpow, priv->txpowlimit,
			       &priv->curtxpow);

	WMI_CMD(WMI_START_RECV_CMDID);
	ath9k_host_rx_init(priv);

	mode = ath9k_htc_get_curmode(priv, ah->curchan);
	htc_mode = cpu_to_be16(mode);
	WMI_CMD_BUF(WMI_SET_MODE_CMDID, &htc_mode);

	WMI_CMD(WMI_ENABLE_INTR_CMDID);
	htc_start(priv->htc);
	ath9k_htc_vif_reconfig(priv);
	ieee80211_wake_queues(priv->hw);

	ath9k_htc_ps_restore(priv);
	mutex_unlock(&priv->mutex);
}

static int ath9k_htc_set_channel(struct ath9k_htc_priv *priv,
				 struct ieee80211_hw *hw,
				 struct ath9k_channel *hchan)
{
	struct ath_hw *ah = priv->ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_conf *conf = &common->hw->conf;
	bool fastcc;
	struct ieee80211_channel *channel = hw->conf.channel;
	struct ath9k_hw_cal_data *caldata = NULL;
	enum htc_phymode mode;
	__be16 htc_mode;
	u8 cmd_rsp;
	int ret;

	if (priv->op_flags & OP_INVALID)
		return -EIO;

	fastcc = !!(hw->conf.flags & IEEE80211_CONF_OFFCHANNEL);

	ath9k_htc_ps_wakeup(priv);
	htc_stop(priv->htc);
	WMI_CMD(WMI_DISABLE_INTR_CMDID);
	WMI_CMD(WMI_DRAIN_TXQ_ALL_CMDID);
	WMI_CMD(WMI_STOP_RECV_CMDID);

	ath_dbg(common, ATH_DBG_CONFIG,
		"(%u MHz) -> (%u MHz), HT: %d, HT40: %d fastcc: %d\n",
		priv->ah->curchan->channel,
		channel->center_freq, conf_is_ht(conf), conf_is_ht40(conf),
		fastcc);

	if (!fastcc)
		caldata = &priv->caldata;
	ret = ath9k_hw_reset(ah, hchan, caldata, fastcc);
	if (ret) {
		ath_err(common,
			"Unable to reset channel (%u Mhz) reset status %d\n",
			channel->center_freq, ret);
		goto err;
	}

	ath9k_cmn_update_txpow(ah, priv->curtxpow, priv->txpowlimit,
			       &priv->curtxpow);

	WMI_CMD(WMI_START_RECV_CMDID);
	if (ret)
		goto err;

	ath9k_host_rx_init(priv);

	mode = ath9k_htc_get_curmode(priv, hchan);
	htc_mode = cpu_to_be16(mode);
	WMI_CMD_BUF(WMI_SET_MODE_CMDID, &htc_mode);
	if (ret)
		goto err;

	WMI_CMD(WMI_ENABLE_INTR_CMDID);
	if (ret)
		goto err;

	htc_start(priv->htc);

	if (!(priv->op_flags & OP_SCANNING) &&
	    !(hw->conf.flags & IEEE80211_CONF_OFFCHANNEL))
		ath9k_htc_vif_reconfig(priv);

err:
	ath9k_htc_ps_restore(priv);
	return ret;
}

/*
 * Monitor mode handling is a tad complicated because the firmware requires
 * an interface to be created exclusively, while mac80211 doesn't associate
 * an interface with the mode.
 *
 * So, for now, only one monitor interface can be configured.
 */
static void __ath9k_htc_remove_monitor_interface(struct ath9k_htc_priv *priv)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath9k_htc_target_vif hvif;
	int ret = 0;
	u8 cmd_rsp;

	memset(&hvif, 0, sizeof(struct ath9k_htc_target_vif));
	memcpy(&hvif.myaddr, common->macaddr, ETH_ALEN);
	hvif.index = priv->mon_vif_idx;
	WMI_CMD_BUF(WMI_VAP_REMOVE_CMDID, &hvif);
	priv->nvifs--;
	priv->vif_slot &= ~(1 << priv->mon_vif_idx);
}

static int ath9k_htc_add_monitor_interface(struct ath9k_htc_priv *priv)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath9k_htc_target_vif hvif;
	struct ath9k_htc_target_sta tsta;
	int ret = 0, sta_idx;
	u8 cmd_rsp;

	if ((priv->nvifs >= ATH9K_HTC_MAX_VIF) ||
	    (priv->nstations >= ATH9K_HTC_MAX_STA)) {
		ret = -ENOBUFS;
		goto err_vif;
	}

	sta_idx = ffz(priv->sta_slot);
	if ((sta_idx < 0) || (sta_idx > ATH9K_HTC_MAX_STA)) {
		ret = -ENOBUFS;
		goto err_vif;
	}

	/*
	 * Add an interface.
	 */
	memset(&hvif, 0, sizeof(struct ath9k_htc_target_vif));
	memcpy(&hvif.myaddr, common->macaddr, ETH_ALEN);

	hvif.opmode = cpu_to_be32(HTC_M_MONITOR);
	hvif.index = ffz(priv->vif_slot);

	WMI_CMD_BUF(WMI_VAP_CREATE_CMDID, &hvif);
	if (ret)
		goto err_vif;

	/*
	 * Assign the monitor interface index as a special case here.
	 * This is needed when the interface is brought down.
	 */
	priv->mon_vif_idx = hvif.index;
	priv->vif_slot |= (1 << hvif.index);

	/*
	 * Set the hardware mode to monitor only if there are no
	 * other interfaces.
	 */
	if (!priv->nvifs)
		priv->ah->opmode = NL80211_IFTYPE_MONITOR;

	priv->nvifs++;

	/*
	 * Associate a station with the interface for packet injection.
	 */
	memset(&tsta, 0, sizeof(struct ath9k_htc_target_sta));

	memcpy(&tsta.macaddr, common->macaddr, ETH_ALEN);

	tsta.is_vif_sta = 1;
	tsta.sta_index = sta_idx;
	tsta.vif_index = hvif.index;
	tsta.maxampdu = 0xffff;

	WMI_CMD_BUF(WMI_NODE_CREATE_CMDID, &tsta);
	if (ret) {
		ath_err(common, "Unable to add station entry for monitor mode\n");
		goto err_sta;
	}

	priv->sta_slot |= (1 << sta_idx);
	priv->nstations++;
	priv->vif_sta_pos[priv->mon_vif_idx] = sta_idx;
	priv->ah->is_monitoring = true;

	ath_dbg(common, ATH_DBG_CONFIG,
		"Attached a monitor interface at idx: %d, sta idx: %d\n",
		priv->mon_vif_idx, sta_idx);

	return 0;

err_sta:
	/*
	 * Remove the interface from the target.
	 */
	__ath9k_htc_remove_monitor_interface(priv);
err_vif:
	ath_dbg(common, ATH_DBG_FATAL, "Unable to attach a monitor interface\n");

	return ret;
}

static int ath9k_htc_remove_monitor_interface(struct ath9k_htc_priv *priv)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	int ret = 0;
	u8 cmd_rsp, sta_idx;

	__ath9k_htc_remove_monitor_interface(priv);

	sta_idx = priv->vif_sta_pos[priv->mon_vif_idx];

	WMI_CMD_BUF(WMI_NODE_REMOVE_CMDID, &sta_idx);
	if (ret) {
		ath_err(common, "Unable to remove station entry for monitor mode\n");
		return ret;
	}

	priv->sta_slot &= ~(1 << sta_idx);
	priv->nstations--;
	priv->ah->is_monitoring = false;

	ath_dbg(common, ATH_DBG_CONFIG,
		"Removed a monitor interface at idx: %d, sta idx: %d\n",
		priv->mon_vif_idx, sta_idx);

	return 0;
}

static int ath9k_htc_add_station(struct ath9k_htc_priv *priv,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath9k_htc_target_sta tsta;
	struct ath9k_htc_vif *avp = (struct ath9k_htc_vif *) vif->drv_priv;
	struct ath9k_htc_sta *ista;
	int ret, sta_idx;
	u8 cmd_rsp;

	if (priv->nstations >= ATH9K_HTC_MAX_STA)
		return -ENOBUFS;

	sta_idx = ffz(priv->sta_slot);
	if ((sta_idx < 0) || (sta_idx > ATH9K_HTC_MAX_STA))
		return -ENOBUFS;

	memset(&tsta, 0, sizeof(struct ath9k_htc_target_sta));

	if (sta) {
		ista = (struct ath9k_htc_sta *) sta->drv_priv;
		memcpy(&tsta.macaddr, sta->addr, ETH_ALEN);
		memcpy(&tsta.bssid, common->curbssid, ETH_ALEN);
		tsta.associd = common->curaid;
		tsta.is_vif_sta = 0;
		tsta.valid = true;
		ista->index = sta_idx;
	} else {
		memcpy(&tsta.macaddr, vif->addr, ETH_ALEN);
		tsta.is_vif_sta = 1;
	}

	tsta.sta_index = sta_idx;
	tsta.vif_index = avp->index;
	tsta.maxampdu = 0xffff;
	if (sta && sta->ht_cap.ht_supported)
		tsta.flags = cpu_to_be16(ATH_HTC_STA_HT);

	WMI_CMD_BUF(WMI_NODE_CREATE_CMDID, &tsta);
	if (ret) {
		if (sta)
			ath_err(common,
				"Unable to add station entry for: %pM\n",
				sta->addr);
		return ret;
	}

	if (sta) {
		ath_dbg(common, ATH_DBG_CONFIG,
			"Added a station entry for: %pM (idx: %d)\n",
			sta->addr, tsta.sta_index);
	} else {
		ath_dbg(common, ATH_DBG_CONFIG,
			"Added a station entry for VIF %d (idx: %d)\n",
			avp->index, tsta.sta_index);
	}

	priv->sta_slot |= (1 << sta_idx);
	priv->nstations++;
	if (!sta)
		priv->vif_sta_pos[avp->index] = sta_idx;

	return 0;
}

static int ath9k_htc_remove_station(struct ath9k_htc_priv *priv,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath9k_htc_vif *avp = (struct ath9k_htc_vif *) vif->drv_priv;
	struct ath9k_htc_sta *ista;
	int ret;
	u8 cmd_rsp, sta_idx;

	if (sta) {
		ista = (struct ath9k_htc_sta *) sta->drv_priv;
		sta_idx = ista->index;
	} else {
		sta_idx = priv->vif_sta_pos[avp->index];
	}

	WMI_CMD_BUF(WMI_NODE_REMOVE_CMDID, &sta_idx);
	if (ret) {
		if (sta)
			ath_err(common,
				"Unable to remove station entry for: %pM\n",
				sta->addr);
		return ret;
	}

	if (sta) {
		ath_dbg(common, ATH_DBG_CONFIG,
			"Removed a station entry for: %pM (idx: %d)\n",
			sta->addr, sta_idx);
	} else {
		ath_dbg(common, ATH_DBG_CONFIG,
			"Removed a station entry for VIF %d (idx: %d)\n",
			avp->index, sta_idx);
	}

	priv->sta_slot &= ~(1 << sta_idx);
	priv->nstations--;

	return 0;
}

int ath9k_htc_update_cap_target(struct ath9k_htc_priv *priv)
{
	struct ath9k_htc_cap_target tcap;
	int ret;
	u8 cmd_rsp;

	memset(&tcap, 0, sizeof(struct ath9k_htc_cap_target));

	/* FIXME: Values are hardcoded */
	tcap.flags = 0x240c40;
	tcap.flags_ext = 0x80601000;
	tcap.ampdu_limit = 0xffff0000;
	tcap.ampdu_subframes = 20;
	tcap.tx_chainmask_legacy = priv->ah->caps.tx_chainmask;
	tcap.protmode = 1;
	tcap.tx_chainmask = priv->ah->caps.tx_chainmask;

	WMI_CMD_BUF(WMI_TARGET_IC_UPDATE_CMDID, &tcap);

	return ret;
}

static void ath9k_htc_setup_rate(struct ath9k_htc_priv *priv,
				 struct ieee80211_sta *sta,
				 struct ath9k_htc_target_rate *trate)
{
	struct ath9k_htc_sta *ista = (struct ath9k_htc_sta *) sta->drv_priv;
	struct ieee80211_supported_band *sband;
	u32 caps = 0;
	int i, j;

	sband = priv->hw->wiphy->bands[priv->hw->conf.channel->band];

	for (i = 0, j = 0; i < sband->n_bitrates; i++) {
		if (sta->supp_rates[sband->band] & BIT(i)) {
			trate->rates.legacy_rates.rs_rates[j]
				= (sband->bitrates[i].bitrate * 2) / 10;
			j++;
		}
	}
	trate->rates.legacy_rates.rs_nrates = j;

	if (sta->ht_cap.ht_supported) {
		for (i = 0, j = 0; i < 77; i++) {
			if (sta->ht_cap.mcs.rx_mask[i/8] & (1<<(i%8)))
				trate->rates.ht_rates.rs_rates[j++] = i;
			if (j == ATH_HTC_RATE_MAX)
				break;
		}
		trate->rates.ht_rates.rs_nrates = j;

		caps = WLAN_RC_HT_FLAG;
		if (sta->ht_cap.mcs.rx_mask[1])
			caps |= WLAN_RC_DS_FLAG;
		if ((sta->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40) &&
		     (conf_is_ht40(&priv->hw->conf)))
			caps |= WLAN_RC_40_FLAG;
		if (conf_is_ht40(&priv->hw->conf) &&
		    (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40))
			caps |= WLAN_RC_SGI_FLAG;
		else if (conf_is_ht20(&priv->hw->conf) &&
			 (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20))
			caps |= WLAN_RC_SGI_FLAG;
	}

	trate->sta_index = ista->index;
	trate->isnew = 1;
	trate->capflags = cpu_to_be32(caps);
}

static int ath9k_htc_send_rate_cmd(struct ath9k_htc_priv *priv,
				    struct ath9k_htc_target_rate *trate)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	int ret;
	u8 cmd_rsp;

	WMI_CMD_BUF(WMI_RC_RATE_UPDATE_CMDID, trate);
	if (ret) {
		ath_err(common,
			"Unable to initialize Rate information on target\n");
	}

	return ret;
}

static void ath9k_htc_init_rate(struct ath9k_htc_priv *priv,
				struct ieee80211_sta *sta)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath9k_htc_target_rate trate;
	int ret;

	memset(&trate, 0, sizeof(struct ath9k_htc_target_rate));
	ath9k_htc_setup_rate(priv, sta, &trate);
	ret = ath9k_htc_send_rate_cmd(priv, &trate);
	if (!ret)
		ath_dbg(common, ATH_DBG_CONFIG,
			"Updated target sta: %pM, rate caps: 0x%X\n",
			sta->addr, be32_to_cpu(trate.capflags));
}

static void ath9k_htc_update_rate(struct ath9k_htc_priv *priv,
				  struct ieee80211_vif *vif,
				  struct ieee80211_bss_conf *bss_conf)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath9k_htc_target_rate trate;
	struct ieee80211_sta *sta;
	int ret;

	memset(&trate, 0, sizeof(struct ath9k_htc_target_rate));

	rcu_read_lock();
	sta = ieee80211_find_sta(vif, bss_conf->bssid);
	if (!sta) {
		rcu_read_unlock();
		return;
	}
	ath9k_htc_setup_rate(priv, sta, &trate);
	rcu_read_unlock();

	ret = ath9k_htc_send_rate_cmd(priv, &trate);
	if (!ret)
		ath_dbg(common, ATH_DBG_CONFIG,
			"Updated target sta: %pM, rate caps: 0x%X\n",
			bss_conf->bssid, be32_to_cpu(trate.capflags));
}

static int ath9k_htc_tx_aggr_oper(struct ath9k_htc_priv *priv,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta,
				  enum ieee80211_ampdu_mlme_action action,
				  u16 tid)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath9k_htc_target_aggr aggr;
	struct ath9k_htc_sta *ista;
	int ret = 0;
	u8 cmd_rsp;

	if (tid >= ATH9K_HTC_MAX_TID)
		return -EINVAL;

	memset(&aggr, 0, sizeof(struct ath9k_htc_target_aggr));
	ista = (struct ath9k_htc_sta *) sta->drv_priv;

	aggr.sta_index = ista->index;
	aggr.tidno = tid & 0xf;
	aggr.aggr_enable = (action == IEEE80211_AMPDU_TX_START) ? true : false;

	WMI_CMD_BUF(WMI_TX_AGGR_ENABLE_CMDID, &aggr);
	if (ret)
		ath_dbg(common, ATH_DBG_CONFIG,
			"Unable to %s TX aggregation for (%pM, %d)\n",
			(aggr.aggr_enable) ? "start" : "stop", sta->addr, tid);
	else
		ath_dbg(common, ATH_DBG_CONFIG,
			"%s TX aggregation for (%pM, %d)\n",
			(aggr.aggr_enable) ? "Starting" : "Stopping",
			sta->addr, tid);

	spin_lock_bh(&priv->tx_lock);
	ista->tid_state[tid] = (aggr.aggr_enable && !ret) ? AGGR_START : AGGR_STOP;
	spin_unlock_bh(&priv->tx_lock);

	return ret;
}

/*********/
/* DEBUG */
/*********/

#ifdef CONFIG_ATH9K_HTC_DEBUGFS

static int ath9k_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t read_file_tgt_stats(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath9k_htc_priv *priv = file->private_data;
	struct ath9k_htc_target_stats cmd_rsp;
	char buf[512];
	unsigned int len = 0;
	int ret = 0;

	memset(&cmd_rsp, 0, sizeof(cmd_rsp));

	WMI_CMD(WMI_TGT_STATS_CMDID);
	if (ret)
		return -EINVAL;


	len += snprintf(buf + len, sizeof(buf) - len,
			"%19s : %10u\n", "TX Short Retries",
			be32_to_cpu(cmd_rsp.tx_shortretry));
	len += snprintf(buf + len, sizeof(buf) - len,
			"%19s : %10u\n", "TX Long Retries",
			be32_to_cpu(cmd_rsp.tx_longretry));
	len += snprintf(buf + len, sizeof(buf) - len,
			"%19s : %10u\n", "TX Xretries",
			be32_to_cpu(cmd_rsp.tx_xretries));
	len += snprintf(buf + len, sizeof(buf) - len,
			"%19s : %10u\n", "TX Unaggr. Xretries",
			be32_to_cpu(cmd_rsp.ht_txunaggr_xretry));
	len += snprintf(buf + len, sizeof(buf) - len,
			"%19s : %10u\n", "TX Xretries (HT)",
			be32_to_cpu(cmd_rsp.ht_tx_xretries));
	len += snprintf(buf + len, sizeof(buf) - len,
			"%19s : %10u\n", "TX Rate", priv->debug.txrate);

	if (len > sizeof(buf))
		len = sizeof(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_tgt_stats = {
	.read = read_file_tgt_stats,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_xmit(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct ath9k_htc_priv *priv = file->private_data;
	char buf[512];
	unsigned int len = 0;

	len += snprintf(buf + len, sizeof(buf) - len,
			"%20s : %10u\n", "Buffers queued",
			priv->debug.tx_stats.buf_queued);
	len += snprintf(buf + len, sizeof(buf) - len,
			"%20s : %10u\n", "Buffers completed",
			priv->debug.tx_stats.buf_completed);
	len += snprintf(buf + len, sizeof(buf) - len,
			"%20s : %10u\n", "SKBs queued",
			priv->debug.tx_stats.skb_queued);
	len += snprintf(buf + len, sizeof(buf) - len,
			"%20s : %10u\n", "SKBs completed",
			priv->debug.tx_stats.skb_completed);
	len += snprintf(buf + len, sizeof(buf) - len,
			"%20s : %10u\n", "SKBs dropped",
			priv->debug.tx_stats.skb_dropped);

	len += snprintf(buf + len, sizeof(buf) - len,
			"%20s : %10u\n", "BE queued",
			priv->debug.tx_stats.queue_stats[WME_AC_BE]);
	len += snprintf(buf + len, sizeof(buf) - len,
			"%20s : %10u\n", "BK queued",
			priv->debug.tx_stats.queue_stats[WME_AC_BK]);
	len += snprintf(buf + len, sizeof(buf) - len,
			"%20s : %10u\n", "VI queued",
			priv->debug.tx_stats.queue_stats[WME_AC_VI]);
	len += snprintf(buf + len, sizeof(buf) - len,
			"%20s : %10u\n", "VO queued",
			priv->debug.tx_stats.queue_stats[WME_AC_VO]);

	if (len > sizeof(buf))
		len = sizeof(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_xmit = {
	.read = read_file_xmit,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_recv(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct ath9k_htc_priv *priv = file->private_data;
	char buf[512];
	unsigned int len = 0;

	len += snprintf(buf + len, sizeof(buf) - len,
			"%20s : %10u\n", "SKBs allocated",
			priv->debug.rx_stats.skb_allocated);
	len += snprintf(buf + len, sizeof(buf) - len,
			"%20s : %10u\n", "SKBs completed",
			priv->debug.rx_stats.skb_completed);
	len += snprintf(buf + len, sizeof(buf) - len,
			"%20s : %10u\n", "SKBs Dropped",
			priv->debug.rx_stats.skb_dropped);

	if (len > sizeof(buf))
		len = sizeof(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_recv = {
	.read = read_file_recv,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

int ath9k_htc_init_debug(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;

	if (!ath9k_debugfs_root)
		return -ENOENT;

	priv->debug.debugfs_phy = debugfs_create_dir(wiphy_name(priv->hw->wiphy),
						     ath9k_debugfs_root);
	if (!priv->debug.debugfs_phy)
		goto err;

	priv->debug.debugfs_tgt_stats = debugfs_create_file("tgt_stats", S_IRUSR,
						    priv->debug.debugfs_phy,
						    priv, &fops_tgt_stats);
	if (!priv->debug.debugfs_tgt_stats)
		goto err;


	priv->debug.debugfs_xmit = debugfs_create_file("xmit", S_IRUSR,
						       priv->debug.debugfs_phy,
						       priv, &fops_xmit);
	if (!priv->debug.debugfs_xmit)
		goto err;

	priv->debug.debugfs_recv = debugfs_create_file("recv", S_IRUSR,
						       priv->debug.debugfs_phy,
						       priv, &fops_recv);
	if (!priv->debug.debugfs_recv)
		goto err;

	return 0;

err:
	ath9k_htc_exit_debug(ah);
	return -ENOMEM;
}

void ath9k_htc_exit_debug(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;

	debugfs_remove(priv->debug.debugfs_recv);
	debugfs_remove(priv->debug.debugfs_xmit);
	debugfs_remove(priv->debug.debugfs_tgt_stats);
	debugfs_remove(priv->debug.debugfs_phy);
}

int ath9k_htc_debug_create_root(void)
{
	ath9k_debugfs_root = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!ath9k_debugfs_root)
		return -ENOENT;

	return 0;
}

void ath9k_htc_debug_remove_root(void)
{
	debugfs_remove(ath9k_debugfs_root);
	ath9k_debugfs_root = NULL;
}

#endif /* CONFIG_ATH9K_HTC_DEBUGFS */

/*******/
/* ANI */
/*******/

void ath9k_htc_start_ani(struct ath9k_htc_priv *priv)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	unsigned long timestamp = jiffies_to_msecs(jiffies);

	common->ani.longcal_timer = timestamp;
	common->ani.shortcal_timer = timestamp;
	common->ani.checkani_timer = timestamp;

	priv->op_flags |= OP_ANI_RUNNING;

	ieee80211_queue_delayed_work(common->hw, &priv->ani_work,
				     msecs_to_jiffies(ATH_ANI_POLLINTERVAL));
}

void ath9k_htc_stop_ani(struct ath9k_htc_priv *priv)
{
	cancel_delayed_work_sync(&priv->ani_work);
	priv->op_flags &= ~OP_ANI_RUNNING;
}

void ath9k_htc_ani_work(struct work_struct *work)
{
	struct ath9k_htc_priv *priv =
		container_of(work, struct ath9k_htc_priv, ani_work.work);
	struct ath_hw *ah = priv->ah;
	struct ath_common *common = ath9k_hw_common(ah);
	bool longcal = false;
	bool shortcal = false;
	bool aniflag = false;
	unsigned int timestamp = jiffies_to_msecs(jiffies);
	u32 cal_interval, short_cal_interval;

	short_cal_interval = (ah->opmode == NL80211_IFTYPE_AP) ?
		ATH_AP_SHORT_CALINTERVAL : ATH_STA_SHORT_CALINTERVAL;

	/* Only calibrate if awake */
	if (ah->power_mode != ATH9K_PM_AWAKE)
		goto set_timer;

	/* Long calibration runs independently of short calibration. */
	if ((timestamp - common->ani.longcal_timer) >= ATH_LONG_CALINTERVAL) {
		longcal = true;
		ath_dbg(common, ATH_DBG_ANI, "longcal @%lu\n", jiffies);
		common->ani.longcal_timer = timestamp;
	}

	/* Short calibration applies only while caldone is false */
	if (!common->ani.caldone) {
		if ((timestamp - common->ani.shortcal_timer) >=
		    short_cal_interval) {
			shortcal = true;
			ath_dbg(common, ATH_DBG_ANI,
				"shortcal @%lu\n", jiffies);
			common->ani.shortcal_timer = timestamp;
			common->ani.resetcal_timer = timestamp;
		}
	} else {
		if ((timestamp - common->ani.resetcal_timer) >=
		    ATH_RESTART_CALINTERVAL) {
			common->ani.caldone = ath9k_hw_reset_calvalid(ah);
			if (common->ani.caldone)
				common->ani.resetcal_timer = timestamp;
		}
	}

	/* Verify whether we must check ANI */
	if ((timestamp - common->ani.checkani_timer) >= ATH_ANI_POLLINTERVAL) {
		aniflag = true;
		common->ani.checkani_timer = timestamp;
	}

	/* Skip all processing if there's nothing to do. */
	if (longcal || shortcal || aniflag) {

		ath9k_htc_ps_wakeup(priv);

		/* Call ANI routine if necessary */
		if (aniflag)
			ath9k_hw_ani_monitor(ah, ah->curchan);

		/* Perform calibration if necessary */
		if (longcal || shortcal)
			common->ani.caldone =
				ath9k_hw_calibrate(ah, ah->curchan,
						   common->rx_chainmask,
						   longcal);

		ath9k_htc_ps_restore(priv);
	}

set_timer:
	/*
	* Set timer interval based on previous results.
	* The interval must be the shortest necessary to satisfy ANI,
	* short calibration and long calibration.
	*/
	cal_interval = ATH_LONG_CALINTERVAL;
	if (priv->ah->config.enable_ani)
		cal_interval = min(cal_interval, (u32)ATH_ANI_POLLINTERVAL);
	if (!common->ani.caldone)
		cal_interval = min(cal_interval, (u32)short_cal_interval);

	ieee80211_queue_delayed_work(common->hw, &priv->ani_work,
				     msecs_to_jiffies(cal_interval));
}

/**********************/
/* mac80211 Callbacks */
/**********************/

static void ath9k_htc_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;
	struct ath9k_htc_priv *priv = hw->priv;
	int padpos, padsize, ret;

	hdr = (struct ieee80211_hdr *) skb->data;

	/* Add the padding after the header if this is not already done */
	padpos = ath9k_cmn_padpos(hdr->frame_control);
	padsize = padpos & 3;
	if (padsize && skb->len > padpos) {
		if (skb_headroom(skb) < padsize)
			goto fail_tx;
		skb_push(skb, padsize);
		memmove(skb->data, skb->data + padsize, padpos);
	}

	ret = ath9k_htc_tx_start(priv, skb);
	if (ret != 0) {
		if (ret == -ENOMEM) {
			ath_dbg(ath9k_hw_common(priv->ah), ATH_DBG_XMIT,
				"Stopping TX queues\n");
			ieee80211_stop_queues(hw);
			spin_lock_bh(&priv->tx_lock);
			priv->tx_queues_stop = true;
			spin_unlock_bh(&priv->tx_lock);
		} else {
			ath_dbg(ath9k_hw_common(priv->ah), ATH_DBG_XMIT,
				"Tx failed\n");
		}
		goto fail_tx;
	}

	return;

fail_tx:
	dev_kfree_skb_any(skb);
}

static int ath9k_htc_start(struct ieee80211_hw *hw)
{
	struct ath9k_htc_priv *priv = hw->priv;
	struct ath_hw *ah = priv->ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_channel *curchan = hw->conf.channel;
	struct ath9k_channel *init_channel;
	int ret = 0;
	enum htc_phymode mode;
	__be16 htc_mode;
	u8 cmd_rsp;

	mutex_lock(&priv->mutex);

	ath_dbg(common, ATH_DBG_CONFIG,
		"Starting driver with initial channel: %d MHz\n",
		curchan->center_freq);

	/* Ensure that HW is awake before flushing RX */
	ath9k_htc_setpower(priv, ATH9K_PM_AWAKE);
	WMI_CMD(WMI_FLUSH_RECV_CMDID);

	/* setup initial channel */
	init_channel = ath9k_cmn_get_curchannel(hw, ah);

	ath9k_hw_htc_resetinit(ah);
	ret = ath9k_hw_reset(ah, init_channel, ah->caldata, false);
	if (ret) {
		ath_err(common,
			"Unable to reset hardware; reset status %d (freq %u MHz)\n",
			ret, curchan->center_freq);
		mutex_unlock(&priv->mutex);
		return ret;
	}

	ath9k_cmn_update_txpow(ah, priv->curtxpow, priv->txpowlimit,
			       &priv->curtxpow);

	mode = ath9k_htc_get_curmode(priv, init_channel);
	htc_mode = cpu_to_be16(mode);
	WMI_CMD_BUF(WMI_SET_MODE_CMDID, &htc_mode);
	WMI_CMD(WMI_ATH_INIT_CMDID);
	WMI_CMD(WMI_START_RECV_CMDID);

	ath9k_host_rx_init(priv);

	ret = ath9k_htc_update_cap_target(priv);
	if (ret)
		ath_dbg(common, ATH_DBG_CONFIG,
			"Failed to update capability in target\n");

	priv->op_flags &= ~OP_INVALID;
	htc_start(priv->htc);

	spin_lock_bh(&priv->tx_lock);
	priv->tx_queues_stop = false;
	spin_unlock_bh(&priv->tx_lock);

	ieee80211_wake_queues(hw);

	if (ah->btcoex_hw.scheme == ATH_BTCOEX_CFG_3WIRE) {
		ath9k_hw_btcoex_set_weight(ah, AR_BT_COEX_WGHT,
					   AR_STOMP_LOW_WLAN_WGHT);
		ath9k_hw_btcoex_enable(ah);
		ath_htc_resume_btcoex_work(priv);
	}
	mutex_unlock(&priv->mutex);

	return ret;
}

static void ath9k_htc_stop(struct ieee80211_hw *hw)
{
	struct ath9k_htc_priv *priv = hw->priv;
	struct ath_hw *ah = priv->ah;
	struct ath_common *common = ath9k_hw_common(ah);
	int ret = 0;
	u8 cmd_rsp;

	mutex_lock(&priv->mutex);

	if (priv->op_flags & OP_INVALID) {
		ath_dbg(common, ATH_DBG_ANY, "Device not present\n");
		mutex_unlock(&priv->mutex);
		return;
	}

	ath9k_htc_ps_wakeup(priv);
	htc_stop(priv->htc);
	WMI_CMD(WMI_DISABLE_INTR_CMDID);
	WMI_CMD(WMI_DRAIN_TXQ_ALL_CMDID);
	WMI_CMD(WMI_STOP_RECV_CMDID);

	tasklet_kill(&priv->swba_tasklet);
	tasklet_kill(&priv->rx_tasklet);
	tasklet_kill(&priv->tx_tasklet);

	skb_queue_purge(&priv->tx_queue);

	mutex_unlock(&priv->mutex);

	/* Cancel all the running timers/work .. */
	cancel_work_sync(&priv->fatal_work);
	cancel_work_sync(&priv->ps_work);
	cancel_delayed_work_sync(&priv->ath9k_led_blink_work);
	ath9k_htc_stop_ani(priv);
	ath9k_led_stop_brightness(priv);

	mutex_lock(&priv->mutex);

	if (ah->btcoex_hw.enabled) {
		ath9k_hw_btcoex_disable(ah);
		if (ah->btcoex_hw.scheme == ATH_BTCOEX_CFG_3WIRE)
			ath_htc_cancel_btcoex_work(priv);
	}

	/* Remove a monitor interface if it's present. */
	if (priv->ah->is_monitoring)
		ath9k_htc_remove_monitor_interface(priv);

	ath9k_hw_phy_disable(ah);
	ath9k_hw_disable(ah);
	ath9k_htc_ps_restore(priv);
	ath9k_htc_setpower(priv, ATH9K_PM_FULL_SLEEP);

	priv->op_flags |= OP_INVALID;

	ath_dbg(common, ATH_DBG_CONFIG, "Driver halt\n");
	mutex_unlock(&priv->mutex);
}

static int ath9k_htc_add_interface(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	struct ath9k_htc_priv *priv = hw->priv;
	struct ath9k_htc_vif *avp = (void *)vif->drv_priv;
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath9k_htc_target_vif hvif;
	int ret = 0;
	u8 cmd_rsp;

	mutex_lock(&priv->mutex);

	if (priv->nvifs >= ATH9K_HTC_MAX_VIF) {
		mutex_unlock(&priv->mutex);
		return -ENOBUFS;
	}

	if (priv->num_ibss_vif ||
	    (priv->nvifs && vif->type == NL80211_IFTYPE_ADHOC)) {
		ath_err(common, "IBSS coexistence with other modes is not allowed\n");
		mutex_unlock(&priv->mutex);
		return -ENOBUFS;
	}

	if (((vif->type == NL80211_IFTYPE_AP) ||
	     (vif->type == NL80211_IFTYPE_ADHOC)) &&
	    ((priv->num_ap_vif + priv->num_ibss_vif) >= ATH9K_HTC_MAX_BCN_VIF)) {
		ath_err(common, "Max. number of beaconing interfaces reached\n");
		mutex_unlock(&priv->mutex);
		return -ENOBUFS;
	}

	ath9k_htc_ps_wakeup(priv);
	memset(&hvif, 0, sizeof(struct ath9k_htc_target_vif));
	memcpy(&hvif.myaddr, vif->addr, ETH_ALEN);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		hvif.opmode = cpu_to_be32(HTC_M_STA);
		break;
	case NL80211_IFTYPE_ADHOC:
		hvif.opmode = cpu_to_be32(HTC_M_IBSS);
		break;
	case NL80211_IFTYPE_AP:
		hvif.opmode = cpu_to_be32(HTC_M_HOSTAP);
		break;
	default:
		ath_err(common,
			"Interface type %d not yet supported\n", vif->type);
		ret = -EOPNOTSUPP;
		goto out;
	}

	/* Index starts from zero on the target */
	avp->index = hvif.index = ffz(priv->vif_slot);
	hvif.rtsthreshold = cpu_to_be16(2304);
	WMI_CMD_BUF(WMI_VAP_CREATE_CMDID, &hvif);
	if (ret)
		goto out;

	/*
	 * We need a node in target to tx mgmt frames
	 * before association.
	 */
	ret = ath9k_htc_add_station(priv, vif, NULL);
	if (ret) {
		WMI_CMD_BUF(WMI_VAP_REMOVE_CMDID, &hvif);
		goto out;
	}

	ath9k_htc_set_bssid_mask(priv, vif);

	priv->vif_slot |= (1 << avp->index);
	priv->nvifs++;
	priv->vif = vif;

	INC_VIF(priv, vif->type);
	ath9k_htc_set_opmode(priv);

	if ((priv->ah->opmode == NL80211_IFTYPE_AP) &&
	    !(priv->op_flags & OP_ANI_RUNNING))
		ath9k_htc_start_ani(priv);

	ath_dbg(common, ATH_DBG_CONFIG,
		"Attach a VIF of type: %d at idx: %d\n", vif->type, avp->index);

out:
	ath9k_htc_ps_restore(priv);
	mutex_unlock(&priv->mutex);

	return ret;
}

static void ath9k_htc_remove_interface(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct ath9k_htc_priv *priv = hw->priv;
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath9k_htc_vif *avp = (void *)vif->drv_priv;
	struct ath9k_htc_target_vif hvif;
	int ret = 0;
	u8 cmd_rsp;

	mutex_lock(&priv->mutex);
	ath9k_htc_ps_wakeup(priv);

	memset(&hvif, 0, sizeof(struct ath9k_htc_target_vif));
	memcpy(&hvif.myaddr, vif->addr, ETH_ALEN);
	hvif.index = avp->index;
	WMI_CMD_BUF(WMI_VAP_REMOVE_CMDID, &hvif);
	priv->nvifs--;
	priv->vif_slot &= ~(1 << avp->index);

	ath9k_htc_remove_station(priv, vif, NULL);
	priv->vif = NULL;

	DEC_VIF(priv, vif->type);
	ath9k_htc_set_opmode(priv);

	/*
	 * Stop ANI only if there are no associated station interfaces.
	 */
	if ((vif->type == NL80211_IFTYPE_AP) && (priv->num_ap_vif == 0)) {
		priv->rearm_ani = false;
		ieee80211_iterate_active_interfaces_atomic(priv->hw,
						   ath9k_htc_vif_iter, priv);
		if (!priv->rearm_ani)
			ath9k_htc_stop_ani(priv);
	}

	ath_dbg(common, ATH_DBG_CONFIG, "Detach Interface at idx: %d\n", avp->index);

	ath9k_htc_ps_restore(priv);
	mutex_unlock(&priv->mutex);
}

static int ath9k_htc_config(struct ieee80211_hw *hw, u32 changed)
{
	struct ath9k_htc_priv *priv = hw->priv;
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ieee80211_conf *conf = &hw->conf;

	mutex_lock(&priv->mutex);

	if (changed & IEEE80211_CONF_CHANGE_IDLE) {
		bool enable_radio = false;
		bool idle = !!(conf->flags & IEEE80211_CONF_IDLE);

		mutex_lock(&priv->htc_pm_lock);
		if (!idle && priv->ps_idle)
			enable_radio = true;
		priv->ps_idle = idle;
		mutex_unlock(&priv->htc_pm_lock);

		if (enable_radio) {
			ath_dbg(common, ATH_DBG_CONFIG,
				"not-idle: enabling radio\n");
			ath9k_htc_setpower(priv, ATH9K_PM_AWAKE);
			ath9k_htc_radio_enable(hw);
		}
	}

	/*
	 * Monitor interface should be added before
	 * IEEE80211_CONF_CHANGE_CHANNEL is handled.
	 */
	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		if ((conf->flags & IEEE80211_CONF_MONITOR) &&
		    !priv->ah->is_monitoring)
			ath9k_htc_add_monitor_interface(priv);
		else if (priv->ah->is_monitoring)
			ath9k_htc_remove_monitor_interface(priv);
	}

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		struct ieee80211_channel *curchan = hw->conf.channel;
		int pos = curchan->hw_value;

		ath_dbg(common, ATH_DBG_CONFIG, "Set channel: %d MHz\n",
			curchan->center_freq);

		ath9k_cmn_update_ichannel(&priv->ah->channels[pos],
					  hw->conf.channel,
					  hw->conf.channel_type);

		if (ath9k_htc_set_channel(priv, hw, &priv->ah->channels[pos]) < 0) {
			ath_err(common, "Unable to set channel\n");
			mutex_unlock(&priv->mutex);
			return -EINVAL;
		}

	}

	if (changed & IEEE80211_CONF_CHANGE_PS) {
		if (conf->flags & IEEE80211_CONF_PS) {
			ath9k_htc_setpower(priv, ATH9K_PM_NETWORK_SLEEP);
			priv->ps_enabled = true;
		} else {
			priv->ps_enabled = false;
			cancel_work_sync(&priv->ps_work);
			ath9k_htc_setpower(priv, ATH9K_PM_AWAKE);
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		priv->txpowlimit = 2 * conf->power_level;
		ath9k_cmn_update_txpow(priv->ah, priv->curtxpow,
				       priv->txpowlimit, &priv->curtxpow);
	}

	if (changed & IEEE80211_CONF_CHANGE_IDLE) {
		mutex_lock(&priv->htc_pm_lock);
		if (!priv->ps_idle) {
			mutex_unlock(&priv->htc_pm_lock);
			goto out;
		}
		mutex_unlock(&priv->htc_pm_lock);

		ath_dbg(common, ATH_DBG_CONFIG,
			"idle: disabling radio\n");
		ath9k_htc_radio_disable(hw);
	}

out:
	mutex_unlock(&priv->mutex);
	return 0;
}

#define SUPPORTED_FILTERS			\
	(FIF_PROMISC_IN_BSS |			\
	FIF_ALLMULTI |				\
	FIF_CONTROL |				\
	FIF_PSPOLL |				\
	FIF_OTHER_BSS |				\
	FIF_BCN_PRBRESP_PROMISC |		\
	FIF_PROBE_REQ |				\
	FIF_FCSFAIL)

static void ath9k_htc_configure_filter(struct ieee80211_hw *hw,
				       unsigned int changed_flags,
				       unsigned int *total_flags,
				       u64 multicast)
{
	struct ath9k_htc_priv *priv = hw->priv;
	u32 rfilt;

	mutex_lock(&priv->mutex);
	ath9k_htc_ps_wakeup(priv);

	changed_flags &= SUPPORTED_FILTERS;
	*total_flags &= SUPPORTED_FILTERS;

	priv->rxfilter = *total_flags;
	rfilt = ath9k_htc_calcrxfilter(priv);
	ath9k_hw_setrxfilter(priv->ah, rfilt);

	ath_dbg(ath9k_hw_common(priv->ah), ATH_DBG_CONFIG,
		"Set HW RX filter: 0x%x\n", rfilt);

	ath9k_htc_ps_restore(priv);
	mutex_unlock(&priv->mutex);
}

static int ath9k_htc_sta_add(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta)
{
	struct ath9k_htc_priv *priv = hw->priv;
	int ret;

	mutex_lock(&priv->mutex);
	ath9k_htc_ps_wakeup(priv);
	ret = ath9k_htc_add_station(priv, vif, sta);
	if (!ret)
		ath9k_htc_init_rate(priv, sta);
	ath9k_htc_ps_restore(priv);
	mutex_unlock(&priv->mutex);

	return ret;
}

static int ath9k_htc_sta_remove(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta)
{
	struct ath9k_htc_priv *priv = hw->priv;
	int ret;

	mutex_lock(&priv->mutex);
	ath9k_htc_ps_wakeup(priv);
	ret = ath9k_htc_remove_station(priv, vif, sta);
	ath9k_htc_ps_restore(priv);
	mutex_unlock(&priv->mutex);

	return ret;
}

static int ath9k_htc_conf_tx(struct ieee80211_hw *hw, u16 queue,
			     const struct ieee80211_tx_queue_params *params)
{
	struct ath9k_htc_priv *priv = hw->priv;
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath9k_tx_queue_info qi;
	int ret = 0, qnum;

	if (queue >= WME_NUM_AC)
		return 0;

	mutex_lock(&priv->mutex);
	ath9k_htc_ps_wakeup(priv);

	memset(&qi, 0, sizeof(struct ath9k_tx_queue_info));

	qi.tqi_aifs = params->aifs;
	qi.tqi_cwmin = params->cw_min;
	qi.tqi_cwmax = params->cw_max;
	qi.tqi_burstTime = params->txop;

	qnum = get_hw_qnum(queue, priv->hwq_map);

	ath_dbg(common, ATH_DBG_CONFIG,
		"Configure tx [queue/hwq] [%d/%d],  aifs: %d, cw_min: %d, cw_max: %d, txop: %d\n",
		queue, qnum, params->aifs, params->cw_min,
		params->cw_max, params->txop);

	ret = ath_htc_txq_update(priv, qnum, &qi);
	if (ret) {
		ath_err(common, "TXQ Update failed\n");
		goto out;
	}

	if ((priv->ah->opmode == NL80211_IFTYPE_ADHOC) &&
	    (qnum == priv->hwq_map[WME_AC_BE]))
		    ath9k_htc_beaconq_config(priv);
out:
	ath9k_htc_ps_restore(priv);
	mutex_unlock(&priv->mutex);

	return ret;
}

static int ath9k_htc_set_key(struct ieee80211_hw *hw,
			     enum set_key_cmd cmd,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct ieee80211_key_conf *key)
{
	struct ath9k_htc_priv *priv = hw->priv;
	struct ath_common *common = ath9k_hw_common(priv->ah);
	int ret = 0;

	if (htc_modparam_nohwcrypt)
		return -ENOSPC;

	mutex_lock(&priv->mutex);
	ath_dbg(common, ATH_DBG_CONFIG, "Set HW Key\n");
	ath9k_htc_ps_wakeup(priv);

	switch (cmd) {
	case SET_KEY:
		ret = ath_key_config(common, vif, sta, key);
		if (ret >= 0) {
			key->hw_key_idx = ret;
			/* push IV and Michael MIC generation to stack */
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
			if (key->cipher == WLAN_CIPHER_SUITE_TKIP)
				key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
			if (priv->ah->sw_mgmt_crypto &&
			    key->cipher == WLAN_CIPHER_SUITE_CCMP)
				key->flags |= IEEE80211_KEY_FLAG_SW_MGMT;
			ret = 0;
		}
		break;
	case DISABLE_KEY:
		ath_key_delete(common, key);
		break;
	default:
		ret = -EINVAL;
	}

	ath9k_htc_ps_restore(priv);
	mutex_unlock(&priv->mutex);

	return ret;
}

static void ath9k_htc_bss_info_changed(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *bss_conf,
				       u32 changed)
{
	struct ath9k_htc_priv *priv = hw->priv;
	struct ath_hw *ah = priv->ah;
	struct ath_common *common = ath9k_hw_common(ah);
	bool set_assoc;

	mutex_lock(&priv->mutex);
	ath9k_htc_ps_wakeup(priv);

	/*
	 * Set the HW AID/BSSID only for the first station interface
	 * or in IBSS mode.
	 */
	set_assoc = !!((priv->ah->opmode == NL80211_IFTYPE_ADHOC) ||
		       ((priv->ah->opmode == NL80211_IFTYPE_STATION) &&
			(priv->num_sta_vif == 1)));


	if (changed & BSS_CHANGED_ASSOC) {
		if (set_assoc) {
			ath_dbg(common, ATH_DBG_CONFIG, "BSS Changed ASSOC %d\n",
				bss_conf->assoc);

			common->curaid = bss_conf->assoc ?
				bss_conf->aid : 0;

			if (bss_conf->assoc)
				ath9k_htc_start_ani(priv);
			else
				ath9k_htc_stop_ani(priv);
		}
	}

	if (changed & BSS_CHANGED_BSSID) {
		if (set_assoc) {
			memcpy(common->curbssid, bss_conf->bssid, ETH_ALEN);
			ath9k_hw_write_associd(ah);

			ath_dbg(common, ATH_DBG_CONFIG,
				"BSSID: %pM aid: 0x%x\n",
				common->curbssid, common->curaid);
		}
	}

	if ((changed & BSS_CHANGED_BEACON_ENABLED) && bss_conf->enable_beacon) {
		ath_dbg(common, ATH_DBG_CONFIG,
			"Beacon enabled for BSS: %pM\n", bss_conf->bssid);
		priv->op_flags |= OP_ENABLE_BEACON;
		ath9k_htc_beacon_config(priv, vif);
	}

	if ((changed & BSS_CHANGED_BEACON_ENABLED) && !bss_conf->enable_beacon) {
		/*
		 * Disable SWBA interrupt only if there are no
		 * AP/IBSS interfaces.
		 */
		if ((priv->num_ap_vif <= 1) || priv->num_ibss_vif) {
			ath_dbg(common, ATH_DBG_CONFIG,
				"Beacon disabled for BSS: %pM\n",
				bss_conf->bssid);
			priv->op_flags &= ~OP_ENABLE_BEACON;
			ath9k_htc_beacon_config(priv, vif);
		}
	}

	if (changed & BSS_CHANGED_BEACON_INT) {
		/*
		 * Reset the HW TSF for the first AP interface.
		 */
		if ((priv->ah->opmode == NL80211_IFTYPE_AP) &&
		    (priv->nvifs == 1) &&
		    (priv->num_ap_vif == 1) &&
		    (vif->type == NL80211_IFTYPE_AP)) {
			priv->op_flags |= OP_TSF_RESET;
		}
		ath_dbg(common, ATH_DBG_CONFIG,
			"Beacon interval changed for BSS: %pM\n",
			bss_conf->bssid);
		ath9k_htc_beacon_config(priv, vif);
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		if (bss_conf->use_short_slot)
			ah->slottime = 9;
		else
			ah->slottime = 20;

		ath9k_hw_init_global_settings(ah);
	}

	if (changed & BSS_CHANGED_HT)
		ath9k_htc_update_rate(priv, vif, bss_conf);

	ath9k_htc_ps_restore(priv);
	mutex_unlock(&priv->mutex);
}

static u64 ath9k_htc_get_tsf(struct ieee80211_hw *hw)
{
	struct ath9k_htc_priv *priv = hw->priv;
	u64 tsf;

	mutex_lock(&priv->mutex);
	ath9k_htc_ps_wakeup(priv);
	tsf = ath9k_hw_gettsf64(priv->ah);
	ath9k_htc_ps_restore(priv);
	mutex_unlock(&priv->mutex);

	return tsf;
}

static void ath9k_htc_set_tsf(struct ieee80211_hw *hw, u64 tsf)
{
	struct ath9k_htc_priv *priv = hw->priv;

	mutex_lock(&priv->mutex);
	ath9k_htc_ps_wakeup(priv);
	ath9k_hw_settsf64(priv->ah, tsf);
	ath9k_htc_ps_restore(priv);
	mutex_unlock(&priv->mutex);
}

static void ath9k_htc_reset_tsf(struct ieee80211_hw *hw)
{
	struct ath9k_htc_priv *priv = hw->priv;

	mutex_lock(&priv->mutex);
	ath9k_htc_ps_wakeup(priv);
	ath9k_hw_reset_tsf(priv->ah);
	ath9k_htc_ps_restore(priv);
	mutex_unlock(&priv->mutex);
}

static int ath9k_htc_ampdu_action(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  enum ieee80211_ampdu_mlme_action action,
				  struct ieee80211_sta *sta,
				  u16 tid, u16 *ssn, u8 buf_size)
{
	struct ath9k_htc_priv *priv = hw->priv;
	struct ath9k_htc_sta *ista;
	int ret = 0;

	mutex_lock(&priv->mutex);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		break;
	case IEEE80211_AMPDU_RX_STOP:
		break;
	case IEEE80211_AMPDU_TX_START:
		ret = ath9k_htc_tx_aggr_oper(priv, vif, sta, action, tid);
		if (!ret)
			ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_STOP:
		ath9k_htc_tx_aggr_oper(priv, vif, sta, action, tid);
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		ista = (struct ath9k_htc_sta *) sta->drv_priv;
		spin_lock_bh(&priv->tx_lock);
		ista->tid_state[tid] = AGGR_OPERATIONAL;
		spin_unlock_bh(&priv->tx_lock);
		break;
	default:
		ath_err(ath9k_hw_common(priv->ah), "Unknown AMPDU action\n");
	}

	mutex_unlock(&priv->mutex);

	return ret;
}

static void ath9k_htc_sw_scan_start(struct ieee80211_hw *hw)
{
	struct ath9k_htc_priv *priv = hw->priv;

	mutex_lock(&priv->mutex);
	spin_lock_bh(&priv->beacon_lock);
	priv->op_flags |= OP_SCANNING;
	spin_unlock_bh(&priv->beacon_lock);
	cancel_work_sync(&priv->ps_work);
	ath9k_htc_stop_ani(priv);
	mutex_unlock(&priv->mutex);
}

static void ath9k_htc_sw_scan_complete(struct ieee80211_hw *hw)
{
	struct ath9k_htc_priv *priv = hw->priv;

	mutex_lock(&priv->mutex);
	spin_lock_bh(&priv->beacon_lock);
	priv->op_flags &= ~OP_SCANNING;
	spin_unlock_bh(&priv->beacon_lock);
	ath9k_htc_ps_wakeup(priv);
	ath9k_htc_vif_reconfig(priv);
	ath9k_htc_ps_restore(priv);
	mutex_unlock(&priv->mutex);
}

static int ath9k_htc_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	return 0;
}

static void ath9k_htc_set_coverage_class(struct ieee80211_hw *hw,
					 u8 coverage_class)
{
	struct ath9k_htc_priv *priv = hw->priv;

	mutex_lock(&priv->mutex);
	ath9k_htc_ps_wakeup(priv);
	priv->ah->coverage_class = coverage_class;
	ath9k_hw_init_global_settings(priv->ah);
	ath9k_htc_ps_restore(priv);
	mutex_unlock(&priv->mutex);
}

struct ieee80211_ops ath9k_htc_ops = {
	.tx                 = ath9k_htc_tx,
	.start              = ath9k_htc_start,
	.stop               = ath9k_htc_stop,
	.add_interface      = ath9k_htc_add_interface,
	.remove_interface   = ath9k_htc_remove_interface,
	.config             = ath9k_htc_config,
	.configure_filter   = ath9k_htc_configure_filter,
	.sta_add            = ath9k_htc_sta_add,
	.sta_remove         = ath9k_htc_sta_remove,
	.conf_tx            = ath9k_htc_conf_tx,
	.bss_info_changed   = ath9k_htc_bss_info_changed,
	.set_key            = ath9k_htc_set_key,
	.get_tsf            = ath9k_htc_get_tsf,
	.set_tsf            = ath9k_htc_set_tsf,
	.reset_tsf          = ath9k_htc_reset_tsf,
	.ampdu_action       = ath9k_htc_ampdu_action,
	.sw_scan_start      = ath9k_htc_sw_scan_start,
	.sw_scan_complete   = ath9k_htc_sw_scan_complete,
	.set_rts_threshold  = ath9k_htc_set_rts_threshold,
	.rfkill_poll        = ath9k_htc_rfkill_poll_state,
	.set_coverage_class = ath9k_htc_set_coverage_class,
};
