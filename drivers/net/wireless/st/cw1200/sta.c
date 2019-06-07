/*
 * Mac80211 STA API for ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/etherdevice.h>

#include "cw1200.h"
#include "sta.h"
#include "fwio.h"
#include "bh.h"
#include "debug.h"

#ifndef ERP_INFO_BYTE_OFFSET
#define ERP_INFO_BYTE_OFFSET 2
#endif

static void cw1200_do_join(struct cw1200_common *priv);
static void cw1200_do_unjoin(struct cw1200_common *priv);

static int cw1200_upload_beacon(struct cw1200_common *priv);
static int cw1200_upload_pspoll(struct cw1200_common *priv);
static int cw1200_upload_null(struct cw1200_common *priv);
static int cw1200_upload_qosnull(struct cw1200_common *priv);
static int cw1200_start_ap(struct cw1200_common *priv);
static int cw1200_update_beaconing(struct cw1200_common *priv);
static int cw1200_enable_beaconing(struct cw1200_common *priv,
				   bool enable);
static void __cw1200_sta_notify(struct ieee80211_hw *dev,
				struct ieee80211_vif *vif,
				enum sta_notify_cmd notify_cmd,
				int link_id);
static int __cw1200_flush(struct cw1200_common *priv, bool drop);

static inline void __cw1200_free_event_queue(struct list_head *list)
{
	struct cw1200_wsm_event *event, *tmp;
	list_for_each_entry_safe(event, tmp, list, link) {
		list_del(&event->link);
		kfree(event);
	}
}

/* ******************************************************************** */
/* STA API								*/

int cw1200_start(struct ieee80211_hw *dev)
{
	struct cw1200_common *priv = dev->priv;
	int ret = 0;

	cw1200_pm_stay_awake(&priv->pm_state, HZ);

	mutex_lock(&priv->conf_mutex);

	/* default EDCA */
	WSM_EDCA_SET(&priv->edca, 0, 0x0002, 0x0003, 0x0007, 47, 0xc8, false);
	WSM_EDCA_SET(&priv->edca, 1, 0x0002, 0x0007, 0x000f, 94, 0xc8, false);
	WSM_EDCA_SET(&priv->edca, 2, 0x0003, 0x000f, 0x03ff, 0, 0xc8, false);
	WSM_EDCA_SET(&priv->edca, 3, 0x0007, 0x000f, 0x03ff, 0, 0xc8, false);
	ret = wsm_set_edca_params(priv, &priv->edca);
	if (ret)
		goto out;

	ret = cw1200_set_uapsd_param(priv, &priv->edca);
	if (ret)
		goto out;

	priv->setbssparams_done = false;

	memcpy(priv->mac_addr, dev->wiphy->perm_addr, ETH_ALEN);
	priv->mode = NL80211_IFTYPE_MONITOR;
	priv->wep_default_key_id = -1;

	priv->cqm_beacon_loss_count = 10;

	ret = cw1200_setup_mac(priv);
	if (ret)
		goto out;

out:
	mutex_unlock(&priv->conf_mutex);
	return ret;
}

void cw1200_stop(struct ieee80211_hw *dev)
{
	struct cw1200_common *priv = dev->priv;
	LIST_HEAD(list);
	int i;

	wsm_lock_tx(priv);

	while (down_trylock(&priv->scan.lock)) {
		/* Scan is in progress. Force it to stop. */
		priv->scan.req = NULL;
		schedule();
	}
	up(&priv->scan.lock);

	cancel_delayed_work_sync(&priv->scan.probe_work);
	cancel_delayed_work_sync(&priv->scan.timeout);
	cancel_delayed_work_sync(&priv->clear_recent_scan_work);
	cancel_delayed_work_sync(&priv->join_timeout);
	cw1200_cqm_bssloss_sm(priv, 0, 0, 0);
	cancel_work_sync(&priv->unjoin_work);
	cancel_delayed_work_sync(&priv->link_id_gc_work);
	flush_workqueue(priv->workqueue);
	del_timer_sync(&priv->mcast_timeout);
	mutex_lock(&priv->conf_mutex);
	priv->mode = NL80211_IFTYPE_UNSPECIFIED;
	priv->listening = false;

	spin_lock(&priv->event_queue_lock);
	list_splice_init(&priv->event_queue, &list);
	spin_unlock(&priv->event_queue_lock);
	__cw1200_free_event_queue(&list);


	priv->join_status = CW1200_JOIN_STATUS_PASSIVE;
	priv->join_pending = false;

	for (i = 0; i < 4; i++)
		cw1200_queue_clear(&priv->tx_queue[i]);
	mutex_unlock(&priv->conf_mutex);
	tx_policy_clean(priv);

	/* HACK! */
	if (atomic_xchg(&priv->tx_lock, 1) != 1)
		pr_debug("[STA] TX is force-unlocked due to stop request.\n");

	wsm_unlock_tx(priv);
	atomic_xchg(&priv->tx_lock, 0); /* for recovery to work */
}

static int cw1200_bssloss_mitigation = 1;
module_param(cw1200_bssloss_mitigation, int, 0644);
MODULE_PARM_DESC(cw1200_bssloss_mitigation, "BSS Loss mitigation. 0 == disabled, 1 == enabled (default)");


void __cw1200_cqm_bssloss_sm(struct cw1200_common *priv,
			     int init, int good, int bad)
{
	int tx = 0;

	priv->delayed_link_loss = 0;
	cancel_work_sync(&priv->bss_params_work);

	pr_debug("[STA] CQM BSSLOSS_SM: state: %d init %d good %d bad: %d txlock: %d uj: %d\n",
		 priv->bss_loss_state,
		 init, good, bad,
		 atomic_read(&priv->tx_lock),
		 priv->delayed_unjoin);

	/* If we have a pending unjoin */
	if (priv->delayed_unjoin)
		return;

	if (init) {
		queue_delayed_work(priv->workqueue,
				   &priv->bss_loss_work,
				   HZ);
		priv->bss_loss_state = 0;

		/* Skip the confimration procedure in P2P case */
		if (!priv->vif->p2p && !atomic_read(&priv->tx_lock))
			tx = 1;
	} else if (good) {
		cancel_delayed_work_sync(&priv->bss_loss_work);
		priv->bss_loss_state = 0;
		queue_work(priv->workqueue, &priv->bss_params_work);
	} else if (bad) {
		/* XXX Should we just keep going until we time out? */
		if (priv->bss_loss_state < 3)
			tx = 1;
	} else {
		cancel_delayed_work_sync(&priv->bss_loss_work);
		priv->bss_loss_state = 0;
	}

	/* Bypass mitigation if it's disabled */
	if (!cw1200_bssloss_mitigation)
		tx = 0;

	/* Spit out a NULL packet to our AP if necessary */
	if (tx) {
		struct sk_buff *skb;

		priv->bss_loss_state++;

		skb = ieee80211_nullfunc_get(priv->hw, priv->vif, false);
		WARN_ON(!skb);
		if (skb)
			cw1200_tx(priv->hw, NULL, skb);
	}
}

int cw1200_add_interface(struct ieee80211_hw *dev,
			 struct ieee80211_vif *vif)
{
	int ret;
	struct cw1200_common *priv = dev->priv;
	/* __le32 auto_calibration_mode = __cpu_to_le32(1); */

	vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER |
			     IEEE80211_VIF_SUPPORTS_UAPSD |
			     IEEE80211_VIF_SUPPORTS_CQM_RSSI;

	mutex_lock(&priv->conf_mutex);

	if (priv->mode != NL80211_IFTYPE_MONITOR) {
		mutex_unlock(&priv->conf_mutex);
		return -EOPNOTSUPP;
	}

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_AP:
		priv->mode = vif->type;
		break;
	default:
		mutex_unlock(&priv->conf_mutex);
		return -EOPNOTSUPP;
	}

	priv->vif = vif;
	memcpy(priv->mac_addr, vif->addr, ETH_ALEN);
	ret = cw1200_setup_mac(priv);
	/* Enable auto-calibration */
	/* Exception in subsequent channel switch; disabled.
	 *  wsm_write_mib(priv, WSM_MIB_ID_SET_AUTO_CALIBRATION_MODE,
	 *      &auto_calibration_mode, sizeof(auto_calibration_mode));
	*/

	mutex_unlock(&priv->conf_mutex);
	return ret;
}

void cw1200_remove_interface(struct ieee80211_hw *dev,
			     struct ieee80211_vif *vif)
{
	struct cw1200_common *priv = dev->priv;
	struct wsm_reset reset = {
		.reset_statistics = true,
	};
	int i;

	mutex_lock(&priv->conf_mutex);
	switch (priv->join_status) {
	case CW1200_JOIN_STATUS_JOINING:
	case CW1200_JOIN_STATUS_PRE_STA:
	case CW1200_JOIN_STATUS_STA:
	case CW1200_JOIN_STATUS_IBSS:
		wsm_lock_tx(priv);
		if (queue_work(priv->workqueue, &priv->unjoin_work) <= 0)
			wsm_unlock_tx(priv);
		break;
	case CW1200_JOIN_STATUS_AP:
		for (i = 0; priv->link_id_map; ++i) {
			if (priv->link_id_map & BIT(i)) {
				reset.link_id = i;
				wsm_reset(priv, &reset);
				priv->link_id_map &= ~BIT(i);
			}
		}
		memset(priv->link_id_db, 0, sizeof(priv->link_id_db));
		priv->sta_asleep_mask = 0;
		priv->enable_beacon = false;
		priv->tx_multicast = false;
		priv->aid0_bit_set = false;
		priv->buffered_multicasts = false;
		priv->pspoll_mask = 0;
		reset.link_id = 0;
		wsm_reset(priv, &reset);
		break;
	case CW1200_JOIN_STATUS_MONITOR:
		cw1200_update_listening(priv, false);
		break;
	default:
		break;
	}
	priv->vif = NULL;
	priv->mode = NL80211_IFTYPE_MONITOR;
	eth_zero_addr(priv->mac_addr);
	memset(&priv->p2p_ps_modeinfo, 0, sizeof(priv->p2p_ps_modeinfo));
	cw1200_free_keys(priv);
	cw1200_setup_mac(priv);
	priv->listening = false;
	priv->join_status = CW1200_JOIN_STATUS_PASSIVE;
	if (!__cw1200_flush(priv, true))
		wsm_unlock_tx(priv);

	mutex_unlock(&priv->conf_mutex);
}

int cw1200_change_interface(struct ieee80211_hw *dev,
			    struct ieee80211_vif *vif,
			    enum nl80211_iftype new_type,
			    bool p2p)
{
	int ret = 0;
	pr_debug("change_interface new: %d (%d), old: %d (%d)\n", new_type,
		 p2p, vif->type, vif->p2p);

	if (new_type != vif->type || vif->p2p != p2p) {
		cw1200_remove_interface(dev, vif);
		vif->type = new_type;
		vif->p2p = p2p;
		ret = cw1200_add_interface(dev, vif);
	}

	return ret;
}

int cw1200_config(struct ieee80211_hw *dev, u32 changed)
{
	int ret = 0;
	struct cw1200_common *priv = dev->priv;
	struct ieee80211_conf *conf = &dev->conf;

	pr_debug("CONFIG CHANGED:  %08x\n", changed);

	down(&priv->scan.lock);
	mutex_lock(&priv->conf_mutex);
	/* TODO: IEEE80211_CONF_CHANGE_QOS */
	/* TODO: IEEE80211_CONF_CHANGE_LISTEN_INTERVAL */

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		priv->output_power = conf->power_level;
		pr_debug("[STA] TX power: %d\n", priv->output_power);
		wsm_set_output_power(priv, priv->output_power * 10);
	}

	if ((changed & IEEE80211_CONF_CHANGE_CHANNEL) &&
	    (priv->channel != conf->chandef.chan)) {
		struct ieee80211_channel *ch = conf->chandef.chan;
		struct wsm_switch_channel channel = {
			.channel_number = ch->hw_value,
		};
		pr_debug("[STA] Freq %d (wsm ch: %d).\n",
			 ch->center_freq, ch->hw_value);

		/* __cw1200_flush() implicitly locks tx, if successful */
		if (!__cw1200_flush(priv, false)) {
			if (!wsm_switch_channel(priv, &channel)) {
				ret = wait_event_timeout(priv->channel_switch_done,
							 !priv->channel_switch_in_progress,
							 3 * HZ);
				if (ret) {
					/* Already unlocks if successful */
					priv->channel = ch;
					ret = 0;
				} else {
					ret = -ETIMEDOUT;
				}
			} else {
				/* Unlock if switch channel fails */
				wsm_unlock_tx(priv);
			}
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_PS) {
		if (!(conf->flags & IEEE80211_CONF_PS))
			priv->powersave_mode.mode = WSM_PSM_ACTIVE;
		else if (conf->dynamic_ps_timeout <= 0)
			priv->powersave_mode.mode = WSM_PSM_PS;
		else
			priv->powersave_mode.mode = WSM_PSM_FAST_PS;

		/* Firmware requires that value for this 1-byte field must
		 * be specified in units of 500us. Values above the 128ms
		 * threshold are not supported.
		 */
		if (conf->dynamic_ps_timeout >= 0x80)
			priv->powersave_mode.fast_psm_idle_period = 0xFF;
		else
			priv->powersave_mode.fast_psm_idle_period =
					conf->dynamic_ps_timeout << 1;

		if (priv->join_status == CW1200_JOIN_STATUS_STA &&
		    priv->bss_params.aid)
			cw1200_set_pm(priv, &priv->powersave_mode);
	}

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		/* TBD: It looks like it's transparent
		 * there's a monitor interface present -- use this
		 * to determine for example whether to calculate
		 * timestamps for packets or not, do not use instead
		 * of filter flags!
		 */
	}

	if (changed & IEEE80211_CONF_CHANGE_IDLE) {
		struct wsm_operational_mode mode = {
			.power_mode = cw1200_power_mode,
			.disable_more_flag_usage = true,
		};

		wsm_lock_tx(priv);
		/* Disable p2p-dev mode forced by TX request */
		if ((priv->join_status == CW1200_JOIN_STATUS_MONITOR) &&
		    (conf->flags & IEEE80211_CONF_IDLE) &&
		    !priv->listening) {
			cw1200_disable_listening(priv);
			priv->join_status = CW1200_JOIN_STATUS_PASSIVE;
		}
		wsm_set_operational_mode(priv, &mode);
		wsm_unlock_tx(priv);
	}

	if (changed & IEEE80211_CONF_CHANGE_RETRY_LIMITS) {
		pr_debug("[STA] Retry limits: %d (long), %d (short).\n",
			 conf->long_frame_max_tx_count,
			 conf->short_frame_max_tx_count);
		spin_lock_bh(&priv->tx_policy_cache.lock);
		priv->long_frame_max_tx_count = conf->long_frame_max_tx_count;
		priv->short_frame_max_tx_count =
			(conf->short_frame_max_tx_count < 0x0F) ?
			conf->short_frame_max_tx_count : 0x0F;
		priv->hw->max_rate_tries = priv->short_frame_max_tx_count;
		spin_unlock_bh(&priv->tx_policy_cache.lock);
	}
	mutex_unlock(&priv->conf_mutex);
	up(&priv->scan.lock);
	return ret;
}

void cw1200_update_filtering(struct cw1200_common *priv)
{
	int ret;
	bool bssid_filtering = !priv->rx_filter.bssid;
	bool is_p2p = priv->vif && priv->vif->p2p;
	bool is_sta = priv->vif && NL80211_IFTYPE_STATION == priv->vif->type;

	static struct wsm_beacon_filter_control bf_ctrl;
	static struct wsm_mib_beacon_filter_table bf_tbl = {
		.entry[0].ie_id = WLAN_EID_VENDOR_SPECIFIC,
		.entry[0].flags = WSM_BEACON_FILTER_IE_HAS_CHANGED |
					WSM_BEACON_FILTER_IE_NO_LONGER_PRESENT |
					WSM_BEACON_FILTER_IE_HAS_APPEARED,
		.entry[0].oui[0] = 0x50,
		.entry[0].oui[1] = 0x6F,
		.entry[0].oui[2] = 0x9A,
		.entry[1].ie_id = WLAN_EID_HT_OPERATION,
		.entry[1].flags = WSM_BEACON_FILTER_IE_HAS_CHANGED |
					WSM_BEACON_FILTER_IE_NO_LONGER_PRESENT |
					WSM_BEACON_FILTER_IE_HAS_APPEARED,
		.entry[2].ie_id = WLAN_EID_ERP_INFO,
		.entry[2].flags = WSM_BEACON_FILTER_IE_HAS_CHANGED |
					WSM_BEACON_FILTER_IE_NO_LONGER_PRESENT |
					WSM_BEACON_FILTER_IE_HAS_APPEARED,
	};

	if (priv->join_status == CW1200_JOIN_STATUS_PASSIVE)
		return;
	else if (priv->join_status == CW1200_JOIN_STATUS_MONITOR)
		bssid_filtering = false;

	if (priv->disable_beacon_filter) {
		bf_ctrl.enabled = 0;
		bf_ctrl.bcn_count = 1;
		bf_tbl.num = __cpu_to_le32(0);
	} else if (is_p2p || !is_sta) {
		bf_ctrl.enabled = WSM_BEACON_FILTER_ENABLE |
			WSM_BEACON_FILTER_AUTO_ERP;
		bf_ctrl.bcn_count = 0;
		bf_tbl.num = __cpu_to_le32(2);
	} else {
		bf_ctrl.enabled = WSM_BEACON_FILTER_ENABLE;
		bf_ctrl.bcn_count = 0;
		bf_tbl.num = __cpu_to_le32(3);
	}

	/* When acting as p2p client being connected to p2p GO, in order to
	 * receive frames from a different p2p device, turn off bssid filter.
	 *
	 * WARNING: FW dependency!
	 * This can only be used with FW WSM371 and its successors.
	 * In that FW version even with bssid filter turned off,
	 * device will block most of the unwanted frames.
	 */
	if (is_p2p)
		bssid_filtering = false;

	ret = wsm_set_rx_filter(priv, &priv->rx_filter);
	if (!ret)
		ret = wsm_set_beacon_filter_table(priv, &bf_tbl);
	if (!ret)
		ret = wsm_beacon_filter_control(priv, &bf_ctrl);
	if (!ret)
		ret = wsm_set_bssid_filtering(priv, bssid_filtering);
	if (!ret)
		ret = wsm_set_multicast_filter(priv, &priv->multicast_filter);
	if (ret)
		wiphy_err(priv->hw->wiphy,
			  "Update filtering failed: %d.\n", ret);
	return;
}

void cw1200_update_filtering_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common,
			     update_filtering_work);

	cw1200_update_filtering(priv);
}

void cw1200_set_beacon_wakeup_period_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common,
			     set_beacon_wakeup_period_work);

	wsm_set_beacon_wakeup_period(priv,
				     priv->beacon_int * priv->join_dtim_period >
				     MAX_BEACON_SKIP_TIME_MS ? 1 :
				     priv->join_dtim_period, 0);
}

u64 cw1200_prepare_multicast(struct ieee80211_hw *hw,
			     struct netdev_hw_addr_list *mc_list)
{
	static u8 broadcast_ipv6[ETH_ALEN] = {
		0x33, 0x33, 0x00, 0x00, 0x00, 0x01
	};
	static u8 broadcast_ipv4[ETH_ALEN] = {
		0x01, 0x00, 0x5e, 0x00, 0x00, 0x01
	};
	struct cw1200_common *priv = hw->priv;
	struct netdev_hw_addr *ha;
	int count = 0;

	/* Disable multicast filtering */
	priv->has_multicast_subscription = false;
	memset(&priv->multicast_filter, 0x00, sizeof(priv->multicast_filter));

	if (netdev_hw_addr_list_count(mc_list) > WSM_MAX_GRP_ADDRTABLE_ENTRIES)
		return 0;

	/* Enable if requested */
	netdev_hw_addr_list_for_each(ha, mc_list) {
		pr_debug("[STA] multicast: %pM\n", ha->addr);
		memcpy(&priv->multicast_filter.macaddrs[count],
		       ha->addr, ETH_ALEN);
		if (!ether_addr_equal(ha->addr, broadcast_ipv4) &&
		    !ether_addr_equal(ha->addr, broadcast_ipv6))
			priv->has_multicast_subscription = true;
		count++;
	}

	if (count) {
		priv->multicast_filter.enable = __cpu_to_le32(1);
		priv->multicast_filter.num_addrs = __cpu_to_le32(count);
	}

	return netdev_hw_addr_list_count(mc_list);
}

void cw1200_configure_filter(struct ieee80211_hw *dev,
			     unsigned int changed_flags,
			     unsigned int *total_flags,
			     u64 multicast)
{
	struct cw1200_common *priv = dev->priv;
	bool listening = !!(*total_flags &
			    (FIF_OTHER_BSS |
			     FIF_BCN_PRBRESP_PROMISC |
			     FIF_PROBE_REQ));

	*total_flags &= FIF_OTHER_BSS |
			FIF_FCSFAIL |
			FIF_BCN_PRBRESP_PROMISC |
			FIF_PROBE_REQ;

	down(&priv->scan.lock);
	mutex_lock(&priv->conf_mutex);

	priv->rx_filter.promiscuous = 0;
	priv->rx_filter.bssid = (*total_flags & (FIF_OTHER_BSS |
			FIF_PROBE_REQ)) ? 1 : 0;
	priv->rx_filter.fcs = (*total_flags & FIF_FCSFAIL) ? 1 : 0;
	priv->disable_beacon_filter = !(*total_flags &
					(FIF_BCN_PRBRESP_PROMISC |
					 FIF_PROBE_REQ));
	if (priv->listening != listening) {
		priv->listening = listening;
		wsm_lock_tx(priv);
		cw1200_update_listening(priv, listening);
		wsm_unlock_tx(priv);
	}
	cw1200_update_filtering(priv);
	mutex_unlock(&priv->conf_mutex);
	up(&priv->scan.lock);
}

int cw1200_conf_tx(struct ieee80211_hw *dev, struct ieee80211_vif *vif,
		   u16 queue, const struct ieee80211_tx_queue_params *params)
{
	struct cw1200_common *priv = dev->priv;
	int ret = 0;
	/* To prevent re-applying PM request OID again and again*/
	bool old_uapsd_flags;

	mutex_lock(&priv->conf_mutex);

	if (queue < dev->queues) {
		old_uapsd_flags = le16_to_cpu(priv->uapsd_info.uapsd_flags);

		WSM_TX_QUEUE_SET(&priv->tx_queue_params, queue, 0, 0, 0);
		ret = wsm_set_tx_queue_params(priv,
					      &priv->tx_queue_params.params[queue], queue);
		if (ret) {
			ret = -EINVAL;
			goto out;
		}

		WSM_EDCA_SET(&priv->edca, queue, params->aifs,
			     params->cw_min, params->cw_max,
			     params->txop, 0xc8,
			     params->uapsd);
		ret = wsm_set_edca_params(priv, &priv->edca);
		if (ret) {
			ret = -EINVAL;
			goto out;
		}

		if (priv->mode == NL80211_IFTYPE_STATION) {
			ret = cw1200_set_uapsd_param(priv, &priv->edca);
			if (!ret && priv->setbssparams_done &&
			    (priv->join_status == CW1200_JOIN_STATUS_STA) &&
			    (old_uapsd_flags != le16_to_cpu(priv->uapsd_info.uapsd_flags)))
				ret = cw1200_set_pm(priv, &priv->powersave_mode);
		}
	} else {
		ret = -EINVAL;
	}

out:
	mutex_unlock(&priv->conf_mutex);
	return ret;
}

int cw1200_get_stats(struct ieee80211_hw *dev,
		     struct ieee80211_low_level_stats *stats)
{
	struct cw1200_common *priv = dev->priv;

	memcpy(stats, &priv->stats, sizeof(*stats));
	return 0;
}

int cw1200_set_pm(struct cw1200_common *priv, const struct wsm_set_pm *arg)
{
	struct wsm_set_pm pm = *arg;

	if (priv->uapsd_info.uapsd_flags != 0)
		pm.mode &= ~WSM_PSM_FAST_PS_FLAG;

	if (memcmp(&pm, &priv->firmware_ps_mode,
		   sizeof(struct wsm_set_pm))) {
		priv->firmware_ps_mode = pm;
		return wsm_set_pm(priv, &pm);
	} else {
		return 0;
	}
}

int cw1200_set_key(struct ieee80211_hw *dev, enum set_key_cmd cmd,
		   struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		   struct ieee80211_key_conf *key)
{
	int ret = -EOPNOTSUPP;
	struct cw1200_common *priv = dev->priv;
	struct ieee80211_key_seq seq;

	mutex_lock(&priv->conf_mutex);

	if (cmd == SET_KEY) {
		u8 *peer_addr = NULL;
		int pairwise = (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) ?
			1 : 0;
		int idx = cw1200_alloc_key(priv);
		struct wsm_add_key *wsm_key = &priv->keys[idx];

		if (idx < 0) {
			ret = -EINVAL;
			goto finally;
		}

		if (sta)
			peer_addr = sta->addr;

		key->flags |= IEEE80211_KEY_FLAG_PUT_IV_SPACE |
			      IEEE80211_KEY_FLAG_RESERVE_TAILROOM;

		switch (key->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			if (key->keylen > 16) {
				cw1200_free_key(priv, idx);
				ret = -EINVAL;
				goto finally;
			}

			if (pairwise) {
				wsm_key->type = WSM_KEY_TYPE_WEP_PAIRWISE;
				memcpy(wsm_key->wep_pairwise.peer,
				       peer_addr, ETH_ALEN);
				memcpy(wsm_key->wep_pairwise.keydata,
				       &key->key[0], key->keylen);
				wsm_key->wep_pairwise.keylen = key->keylen;
			} else {
				wsm_key->type = WSM_KEY_TYPE_WEP_DEFAULT;
				memcpy(wsm_key->wep_group.keydata,
				       &key->key[0], key->keylen);
				wsm_key->wep_group.keylen = key->keylen;
				wsm_key->wep_group.keyid = key->keyidx;
			}
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			ieee80211_get_key_rx_seq(key, 0, &seq);
			if (pairwise) {
				wsm_key->type = WSM_KEY_TYPE_TKIP_PAIRWISE;
				memcpy(wsm_key->tkip_pairwise.peer,
				       peer_addr, ETH_ALEN);
				memcpy(wsm_key->tkip_pairwise.keydata,
				       &key->key[0], 16);
				memcpy(wsm_key->tkip_pairwise.tx_mic_key,
				       &key->key[16], 8);
				memcpy(wsm_key->tkip_pairwise.rx_mic_key,
				       &key->key[24], 8);
			} else {
				size_t mic_offset =
					(priv->mode == NL80211_IFTYPE_AP) ?
					16 : 24;
				wsm_key->type = WSM_KEY_TYPE_TKIP_GROUP;
				memcpy(wsm_key->tkip_group.keydata,
				       &key->key[0], 16);
				memcpy(wsm_key->tkip_group.rx_mic_key,
				       &key->key[mic_offset], 8);

				wsm_key->tkip_group.rx_seqnum[0] = seq.tkip.iv16 & 0xff;
				wsm_key->tkip_group.rx_seqnum[1] = (seq.tkip.iv16 >> 8) & 0xff;
				wsm_key->tkip_group.rx_seqnum[2] = seq.tkip.iv32 & 0xff;
				wsm_key->tkip_group.rx_seqnum[3] = (seq.tkip.iv32 >> 8) & 0xff;
				wsm_key->tkip_group.rx_seqnum[4] = (seq.tkip.iv32 >> 16) & 0xff;
				wsm_key->tkip_group.rx_seqnum[5] = (seq.tkip.iv32 >> 24) & 0xff;
				wsm_key->tkip_group.rx_seqnum[6] = 0;
				wsm_key->tkip_group.rx_seqnum[7] = 0;

				wsm_key->tkip_group.keyid = key->keyidx;
			}
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			ieee80211_get_key_rx_seq(key, 0, &seq);
			if (pairwise) {
				wsm_key->type = WSM_KEY_TYPE_AES_PAIRWISE;
				memcpy(wsm_key->aes_pairwise.peer,
				       peer_addr, ETH_ALEN);
				memcpy(wsm_key->aes_pairwise.keydata,
				       &key->key[0], 16);
			} else {
				wsm_key->type = WSM_KEY_TYPE_AES_GROUP;
				memcpy(wsm_key->aes_group.keydata,
				       &key->key[0], 16);

				wsm_key->aes_group.rx_seqnum[0] = seq.ccmp.pn[5];
				wsm_key->aes_group.rx_seqnum[1] = seq.ccmp.pn[4];
				wsm_key->aes_group.rx_seqnum[2] = seq.ccmp.pn[3];
				wsm_key->aes_group.rx_seqnum[3] = seq.ccmp.pn[2];
				wsm_key->aes_group.rx_seqnum[4] = seq.ccmp.pn[1];
				wsm_key->aes_group.rx_seqnum[5] = seq.ccmp.pn[0];
				wsm_key->aes_group.rx_seqnum[6] = 0;
				wsm_key->aes_group.rx_seqnum[7] = 0;
				wsm_key->aes_group.keyid = key->keyidx;
			}
			break;
		case WLAN_CIPHER_SUITE_SMS4:
			if (pairwise) {
				wsm_key->type = WSM_KEY_TYPE_WAPI_PAIRWISE;
				memcpy(wsm_key->wapi_pairwise.peer,
				       peer_addr, ETH_ALEN);
				memcpy(wsm_key->wapi_pairwise.keydata,
				       &key->key[0], 16);
				memcpy(wsm_key->wapi_pairwise.mic_key,
				       &key->key[16], 16);
				wsm_key->wapi_pairwise.keyid = key->keyidx;
			} else {
				wsm_key->type = WSM_KEY_TYPE_WAPI_GROUP;
				memcpy(wsm_key->wapi_group.keydata,
				       &key->key[0],  16);
				memcpy(wsm_key->wapi_group.mic_key,
				       &key->key[16], 16);
				wsm_key->wapi_group.keyid = key->keyidx;
			}
			break;
		default:
			pr_warn("Unhandled key type %d\n", key->cipher);
			cw1200_free_key(priv, idx);
			ret = -EOPNOTSUPP;
			goto finally;
		}
		ret = wsm_add_key(priv, wsm_key);
		if (!ret)
			key->hw_key_idx = idx;
		else
			cw1200_free_key(priv, idx);
	} else if (cmd == DISABLE_KEY) {
		struct wsm_remove_key wsm_key = {
			.index = key->hw_key_idx,
		};

		if (wsm_key.index > WSM_KEY_MAX_INDEX) {
			ret = -EINVAL;
			goto finally;
		}

		cw1200_free_key(priv, wsm_key.index);
		ret = wsm_remove_key(priv, &wsm_key);
	} else {
		pr_warn("Unhandled key command %d\n", cmd);
	}

finally:
	mutex_unlock(&priv->conf_mutex);
	return ret;
}

void cw1200_wep_key_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, wep_key_work);
	u8 queue_id = cw1200_queue_get_queue_id(priv->pending_frame_id);
	struct cw1200_queue *queue = &priv->tx_queue[queue_id];
	__le32 wep_default_key_id = __cpu_to_le32(
		priv->wep_default_key_id);

	pr_debug("[STA] Setting default WEP key: %d\n",
		 priv->wep_default_key_id);
	wsm_flush_tx(priv);
	wsm_write_mib(priv, WSM_MIB_ID_DOT11_WEP_DEFAULT_KEY_ID,
		      &wep_default_key_id, sizeof(wep_default_key_id));
	cw1200_queue_requeue(queue, priv->pending_frame_id);
	wsm_unlock_tx(priv);
}

int cw1200_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	int ret = 0;
	__le32 val32;
	struct cw1200_common *priv = hw->priv;

	if (priv->mode == NL80211_IFTYPE_UNSPECIFIED)
		return 0;

	if (value != (u32) -1)
		val32 = __cpu_to_le32(value);
	else
		val32 = 0; /* disabled */

	if (priv->rts_threshold == value)
		goto out;

	pr_debug("[STA] Setting RTS threshold: %d\n",
		 priv->rts_threshold);

	/* mutex_lock(&priv->conf_mutex); */
	ret = wsm_write_mib(priv, WSM_MIB_ID_DOT11_RTS_THRESHOLD,
			    &val32, sizeof(val32));
	if (!ret)
		priv->rts_threshold = value;
	/* mutex_unlock(&priv->conf_mutex); */

out:
	return ret;
}

/* If successful, LOCKS the TX queue! */
static int __cw1200_flush(struct cw1200_common *priv, bool drop)
{
	int i, ret;

	for (;;) {
		/* TODO: correct flush handling is required when dev_stop.
		 * Temporary workaround: 2s
		 */
		if (drop) {
			for (i = 0; i < 4; ++i)
				cw1200_queue_clear(&priv->tx_queue[i]);
		} else {
			ret = wait_event_timeout(
				priv->tx_queue_stats.wait_link_id_empty,
				cw1200_queue_stats_is_empty(
					&priv->tx_queue_stats, -1),
				2 * HZ);
		}

		if (!drop && ret <= 0) {
			ret = -ETIMEDOUT;
			break;
		} else {
			ret = 0;
		}

		wsm_lock_tx(priv);
		if (!cw1200_queue_stats_is_empty(&priv->tx_queue_stats, -1)) {
			/* Highly unlikely: WSM requeued frames. */
			wsm_unlock_tx(priv);
			continue;
		}
		break;
	}
	return ret;
}

void cw1200_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  u32 queues, bool drop)
{
	struct cw1200_common *priv = hw->priv;

	switch (priv->mode) {
	case NL80211_IFTYPE_MONITOR:
		drop = true;
		break;
	case NL80211_IFTYPE_AP:
		if (!priv->enable_beacon)
			drop = true;
		break;
	}

	if (!__cw1200_flush(priv, drop))
		wsm_unlock_tx(priv);

	return;
}

/* ******************************************************************** */
/* WSM callbacks							*/

void cw1200_free_event_queue(struct cw1200_common *priv)
{
	LIST_HEAD(list);

	spin_lock(&priv->event_queue_lock);
	list_splice_init(&priv->event_queue, &list);
	spin_unlock(&priv->event_queue_lock);

	__cw1200_free_event_queue(&list);
}

void cw1200_event_handler(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, event_handler);
	struct cw1200_wsm_event *event;
	LIST_HEAD(list);

	spin_lock(&priv->event_queue_lock);
	list_splice_init(&priv->event_queue, &list);
	spin_unlock(&priv->event_queue_lock);

	list_for_each_entry(event, &list, link) {
		switch (event->evt.id) {
		case WSM_EVENT_ERROR:
			pr_err("Unhandled WSM Error from LMAC\n");
			break;
		case WSM_EVENT_BSS_LOST:
			pr_debug("[CQM] BSS lost.\n");
			cancel_work_sync(&priv->unjoin_work);
			if (!down_trylock(&priv->scan.lock)) {
				cw1200_cqm_bssloss_sm(priv, 1, 0, 0);
				up(&priv->scan.lock);
			} else {
				/* Scan is in progress. Delay reporting.
				 * Scan complete will trigger bss_loss_work
				 */
				priv->delayed_link_loss = 1;
				/* Also start a watchdog. */
				queue_delayed_work(priv->workqueue,
						   &priv->bss_loss_work, 5*HZ);
			}
			break;
		case WSM_EVENT_BSS_REGAINED:
			pr_debug("[CQM] BSS regained.\n");
			cw1200_cqm_bssloss_sm(priv, 0, 0, 0);
			cancel_work_sync(&priv->unjoin_work);
			break;
		case WSM_EVENT_RADAR_DETECTED:
			wiphy_info(priv->hw->wiphy, "radar pulse detected\n");
			break;
		case WSM_EVENT_RCPI_RSSI:
		{
			/* RSSI: signed Q8.0, RCPI: unsigned Q7.1
			 * RSSI = RCPI / 2 - 110
			 */
			int rcpi_rssi = (int)(event->evt.data & 0xFF);
			int cqm_evt;
			if (priv->cqm_use_rssi)
				rcpi_rssi = (s8)rcpi_rssi;
			else
				rcpi_rssi =  rcpi_rssi / 2 - 110;

			cqm_evt = (rcpi_rssi <= priv->cqm_rssi_thold) ?
				NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW :
				NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH;
			pr_debug("[CQM] RSSI event: %d.\n", rcpi_rssi);
			ieee80211_cqm_rssi_notify(priv->vif, cqm_evt, rcpi_rssi,
						  GFP_KERNEL);
			break;
		}
		case WSM_EVENT_BT_INACTIVE:
			pr_warn("Unhandled BT INACTIVE from LMAC\n");
			break;
		case WSM_EVENT_BT_ACTIVE:
			pr_warn("Unhandled BT ACTIVE from LMAC\n");
			break;
		}
	}
	__cw1200_free_event_queue(&list);
}

void cw1200_bss_loss_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, bss_loss_work.work);

	pr_debug("[CQM] Reporting connection loss.\n");
	wsm_lock_tx(priv);
	if (queue_work(priv->workqueue, &priv->unjoin_work) <= 0)
		wsm_unlock_tx(priv);
}

void cw1200_bss_params_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, bss_params_work);
	mutex_lock(&priv->conf_mutex);

	priv->bss_params.reset_beacon_loss = 1;
	wsm_set_bss_params(priv, &priv->bss_params);
	priv->bss_params.reset_beacon_loss = 0;

	mutex_unlock(&priv->conf_mutex);
}

/* ******************************************************************** */
/* Internal API								*/

/* This function is called to Parse the SDD file
 * to extract listen_interval and PTA related information
 * sdd is a TLV: u8 id, u8 len, u8 data[]
 */
static int cw1200_parse_sdd_file(struct cw1200_common *priv)
{
	const u8 *p = priv->sdd->data;
	int ret = 0;

	while (p + 2 <= priv->sdd->data + priv->sdd->size) {
		if (p + p[1] + 2 > priv->sdd->data + priv->sdd->size) {
			pr_warn("Malformed sdd structure\n");
			return -1;
		}
		switch (p[0]) {
		case SDD_PTA_CFG_ELT_ID: {
			u16 v;
			if (p[1] < 4) {
				pr_warn("SDD_PTA_CFG_ELT_ID malformed\n");
				ret = -1;
				break;
			}
			v = le16_to_cpu(*((__le16 *)(p + 2)));
			if (!v)  /* non-zero means this is enabled */
				break;

			v = le16_to_cpu(*((__le16 *)(p + 4)));
			priv->conf_listen_interval = (v >> 7) & 0x1F;
			pr_debug("PTA found; Listen Interval %d\n",
				 priv->conf_listen_interval);
			break;
		}
		case SDD_REFERENCE_FREQUENCY_ELT_ID: {
			u16 clk = le16_to_cpu(*((__le16 *)(p + 2)));
			if (clk != priv->hw_refclk)
				pr_warn("SDD file doesn't match configured refclk (%d vs %d)\n",
					clk, priv->hw_refclk);
			break;
		}
		default:
			break;
		}
		p += p[1] + 2;
	}

	if (!priv->bt_present) {
		pr_debug("PTA element NOT found.\n");
		priv->conf_listen_interval = 0;
	}
	return ret;
}

int cw1200_setup_mac(struct cw1200_common *priv)
{
	int ret = 0;

	/* NOTE: There is a bug in FW: it reports signal
	 * as RSSI if RSSI subscription is enabled.
	 * It's not enough to set WSM_RCPI_RSSI_USE_RSSI.
	 *
	 * NOTE2: RSSI based reports have been switched to RCPI, since
	 * FW has a bug and RSSI reported values are not stable,
	 * what can lead to signal level oscilations in user-end applications
	 */
	struct wsm_rcpi_rssi_threshold threshold = {
		.rssiRcpiMode = WSM_RCPI_RSSI_THRESHOLD_ENABLE |
		WSM_RCPI_RSSI_DONT_USE_UPPER |
		WSM_RCPI_RSSI_DONT_USE_LOWER,
		.rollingAverageCount = 16,
	};

	struct wsm_configuration cfg = {
		.dot11StationId = &priv->mac_addr[0],
	};

	/* Remember the decission here to make sure, we will handle
	 * the RCPI/RSSI value correctly on WSM_EVENT_RCPI_RSS
	 */
	if (threshold.rssiRcpiMode & WSM_RCPI_RSSI_USE_RSSI)
		priv->cqm_use_rssi = true;

	if (!priv->sdd) {
		ret = request_firmware(&priv->sdd, priv->sdd_path, priv->pdev);
		if (ret) {
			pr_err("Can't load sdd file %s.\n", priv->sdd_path);
			return ret;
		}
		cw1200_parse_sdd_file(priv);
	}

	cfg.dpdData = priv->sdd->data;
	cfg.dpdData_size = priv->sdd->size;
	ret = wsm_configuration(priv, &cfg);
	if (ret)
		return ret;

	/* Configure RSSI/SCPI reporting as RSSI. */
	wsm_set_rcpi_rssi_threshold(priv, &threshold);

	return 0;
}

static void cw1200_join_complete(struct cw1200_common *priv)
{
	pr_debug("[STA] Join complete (%d)\n", priv->join_complete_status);

	priv->join_pending = false;
	if (priv->join_complete_status) {
		priv->join_status = CW1200_JOIN_STATUS_PASSIVE;
		cw1200_update_listening(priv, priv->listening);
		cw1200_do_unjoin(priv);
		ieee80211_connection_loss(priv->vif);
	} else {
		if (priv->mode == NL80211_IFTYPE_ADHOC)
			priv->join_status = CW1200_JOIN_STATUS_IBSS;
		else
			priv->join_status = CW1200_JOIN_STATUS_PRE_STA;
	}
	wsm_unlock_tx(priv); /* Clearing the lock held before do_join() */
}

void cw1200_join_complete_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, join_complete_work);
	mutex_lock(&priv->conf_mutex);
	cw1200_join_complete(priv);
	mutex_unlock(&priv->conf_mutex);
}

void cw1200_join_complete_cb(struct cw1200_common *priv,
			     struct wsm_join_complete *arg)
{
	pr_debug("[STA] cw1200_join_complete_cb called, status=%d.\n",
		 arg->status);

	if (cancel_delayed_work(&priv->join_timeout)) {
		priv->join_complete_status = arg->status;
		queue_work(priv->workqueue, &priv->join_complete_work);
	}
}

/* MUST be called with tx_lock held!  It will be unlocked for us. */
static void cw1200_do_join(struct cw1200_common *priv)
{
	const u8 *bssid;
	struct ieee80211_bss_conf *conf = &priv->vif->bss_conf;
	struct cfg80211_bss *bss = NULL;
	struct wsm_protected_mgmt_policy mgmt_policy;
	struct wsm_join join = {
		.mode = conf->ibss_joined ?
				WSM_JOIN_MODE_IBSS : WSM_JOIN_MODE_BSS,
		.preamble_type = WSM_JOIN_PREAMBLE_LONG,
		.probe_for_join = 1,
		.atim_window = 0,
		.basic_rate_set = cw1200_rate_mask_to_wsm(priv,
							  conf->basic_rates),
	};
	if (delayed_work_pending(&priv->join_timeout)) {
		pr_warn("[STA] - Join request already pending, skipping..\n");
		wsm_unlock_tx(priv);
		return;
	}

	if (priv->join_status)
		cw1200_do_unjoin(priv);

	bssid = priv->vif->bss_conf.bssid;

	bss = cfg80211_get_bss(priv->hw->wiphy, priv->channel, bssid, NULL, 0,
			       IEEE80211_BSS_TYPE_ANY, IEEE80211_PRIVACY_ANY);

	if (!bss && !conf->ibss_joined) {
		wsm_unlock_tx(priv);
		return;
	}

	mutex_lock(&priv->conf_mutex);

	/* Under the conf lock: check scan status and
	 * bail out if it is in progress.
	 */
	if (atomic_read(&priv->scan.in_progress)) {
		wsm_unlock_tx(priv);
		goto done_put;
	}

	priv->join_pending = true;

	/* Sanity check basic rates */
	if (!join.basic_rate_set)
		join.basic_rate_set = 7;

	/* Sanity check beacon interval */
	if (!priv->beacon_int)
		priv->beacon_int = 1;

	join.beacon_interval = priv->beacon_int;

	/* BT Coex related changes */
	if (priv->bt_present) {
		if (((priv->conf_listen_interval * 100) %
		     priv->beacon_int) == 0)
			priv->listen_interval =
				((priv->conf_listen_interval * 100) /
				 priv->beacon_int);
		else
			priv->listen_interval =
				((priv->conf_listen_interval * 100) /
				 priv->beacon_int + 1);
	}

	if (priv->hw->conf.ps_dtim_period)
		priv->join_dtim_period = priv->hw->conf.ps_dtim_period;
	join.dtim_period = priv->join_dtim_period;

	join.channel_number = priv->channel->hw_value;
	join.band = (priv->channel->band == NL80211_BAND_5GHZ) ?
		WSM_PHY_BAND_5G : WSM_PHY_BAND_2_4G;

	memcpy(join.bssid, bssid, sizeof(join.bssid));

	pr_debug("[STA] Join BSSID: %pM DTIM: %d, interval: %d\n",
		 join.bssid,
		 join.dtim_period, priv->beacon_int);

	if (!conf->ibss_joined) {
		const u8 *ssidie;
		rcu_read_lock();
		ssidie = ieee80211_bss_get_ie(bss, WLAN_EID_SSID);
		if (ssidie) {
			join.ssid_len = ssidie[1];
			memcpy(join.ssid, &ssidie[2], join.ssid_len);
		}
		rcu_read_unlock();
	}

	if (priv->vif->p2p) {
		join.flags |= WSM_JOIN_FLAGS_P2P_GO;
		join.basic_rate_set =
			cw1200_rate_mask_to_wsm(priv, 0xFF0);
	}

	/* Enable asynchronous join calls */
	if (!conf->ibss_joined) {
		join.flags |= WSM_JOIN_FLAGS_FORCE;
		join.flags |= WSM_JOIN_FLAGS_FORCE_WITH_COMPLETE_IND;
	}

	wsm_flush_tx(priv);

	/* Stay Awake for Join and Auth Timeouts and a bit more */
	cw1200_pm_stay_awake(&priv->pm_state,
			     CW1200_JOIN_TIMEOUT + CW1200_AUTH_TIMEOUT);

	cw1200_update_listening(priv, false);

	/* Turn on Block ACKs */
	wsm_set_block_ack_policy(priv, priv->ba_tx_tid_mask,
				 priv->ba_rx_tid_mask);

	/* Set up timeout */
	if (join.flags & WSM_JOIN_FLAGS_FORCE_WITH_COMPLETE_IND) {
		priv->join_status = CW1200_JOIN_STATUS_JOINING;
		queue_delayed_work(priv->workqueue,
				   &priv->join_timeout,
				   CW1200_JOIN_TIMEOUT);
	}

	/* 802.11w protected mgmt frames */
	mgmt_policy.protectedMgmtEnable = 0;
	mgmt_policy.unprotectedMgmtFramesAllowed = 1;
	mgmt_policy.encryptionForAuthFrame = 1;
	wsm_set_protected_mgmt_policy(priv, &mgmt_policy);

	/* Perform actual join */
	if (wsm_join(priv, &join)) {
		pr_err("[STA] cw1200_join_work: wsm_join failed!\n");
		cancel_delayed_work_sync(&priv->join_timeout);
		cw1200_update_listening(priv, priv->listening);
		/* Tx lock still held, unjoin will clear it. */
		if (queue_work(priv->workqueue, &priv->unjoin_work) <= 0)
			wsm_unlock_tx(priv);
	} else {
		if (!(join.flags & WSM_JOIN_FLAGS_FORCE_WITH_COMPLETE_IND))
			cw1200_join_complete(priv); /* Will clear tx_lock */

		/* Upload keys */
		cw1200_upload_keys(priv);

		/* Due to beacon filtering it is possible that the
		 * AP's beacon is not known for the mac80211 stack.
		 * Disable filtering temporary to make sure the stack
		 * receives at least one
		 */
		priv->disable_beacon_filter = true;
	}
	cw1200_update_filtering(priv);

done_put:
	mutex_unlock(&priv->conf_mutex);
	if (bss)
		cfg80211_put_bss(priv->hw->wiphy, bss);
}

void cw1200_join_timeout(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, join_timeout.work);
	pr_debug("[WSM] Join timed out.\n");
	wsm_lock_tx(priv);
	if (queue_work(priv->workqueue, &priv->unjoin_work) <= 0)
		wsm_unlock_tx(priv);
}

static void cw1200_do_unjoin(struct cw1200_common *priv)
{
	struct wsm_reset reset = {
		.reset_statistics = true,
	};

	cancel_delayed_work_sync(&priv->join_timeout);

	mutex_lock(&priv->conf_mutex);
	priv->join_pending = false;

	if (atomic_read(&priv->scan.in_progress)) {
		if (priv->delayed_unjoin)
			wiphy_dbg(priv->hw->wiphy, "Delayed unjoin is already scheduled.\n");
		else
			priv->delayed_unjoin = true;
		goto done;
	}

	priv->delayed_link_loss = false;

	if (!priv->join_status)
		goto done;

	if (priv->join_status == CW1200_JOIN_STATUS_AP)
		goto done;

	cancel_work_sync(&priv->update_filtering_work);
	cancel_work_sync(&priv->set_beacon_wakeup_period_work);
	priv->join_status = CW1200_JOIN_STATUS_PASSIVE;

	/* Unjoin is a reset. */
	wsm_flush_tx(priv);
	wsm_keep_alive_period(priv, 0);
	wsm_reset(priv, &reset);
	wsm_set_output_power(priv, priv->output_power * 10);
	priv->join_dtim_period = 0;
	cw1200_setup_mac(priv);
	cw1200_free_event_queue(priv);
	cancel_work_sync(&priv->event_handler);
	cw1200_update_listening(priv, priv->listening);
	cw1200_cqm_bssloss_sm(priv, 0, 0, 0);

	/* Disable Block ACKs */
	wsm_set_block_ack_policy(priv, 0, 0);

	priv->disable_beacon_filter = false;
	cw1200_update_filtering(priv);
	memset(&priv->association_mode, 0,
	       sizeof(priv->association_mode));
	memset(&priv->bss_params, 0, sizeof(priv->bss_params));
	priv->setbssparams_done = false;
	memset(&priv->firmware_ps_mode, 0,
	       sizeof(priv->firmware_ps_mode));

	pr_debug("[STA] Unjoin completed.\n");

done:
	mutex_unlock(&priv->conf_mutex);
}

void cw1200_unjoin_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, unjoin_work);

	cw1200_do_unjoin(priv);

	/* Tell the stack we're dead */
	ieee80211_connection_loss(priv->vif);

	wsm_unlock_tx(priv);
}

int cw1200_enable_listening(struct cw1200_common *priv)
{
	struct wsm_start start = {
		.mode = WSM_START_MODE_P2P_DEV,
		.band = WSM_PHY_BAND_2_4G,
		.beacon_interval = 100,
		.dtim_period = 1,
		.probe_delay = 0,
		.basic_rate_set = 0x0F,
	};

	if (priv->channel) {
		start.band = priv->channel->band == NL80211_BAND_5GHZ ?
			     WSM_PHY_BAND_5G : WSM_PHY_BAND_2_4G;
		start.channel_number = priv->channel->hw_value;
	} else {
		start.band = WSM_PHY_BAND_2_4G;
		start.channel_number = 1;
	}

	return wsm_start(priv, &start);
}

int cw1200_disable_listening(struct cw1200_common *priv)
{
	int ret;
	struct wsm_reset reset = {
		.reset_statistics = true,
	};
	ret = wsm_reset(priv, &reset);
	return ret;
}

void cw1200_update_listening(struct cw1200_common *priv, bool enabled)
{
	if (enabled) {
		if (priv->join_status == CW1200_JOIN_STATUS_PASSIVE) {
			if (!cw1200_enable_listening(priv))
				priv->join_status = CW1200_JOIN_STATUS_MONITOR;
			wsm_set_probe_responder(priv, true);
		}
	} else {
		if (priv->join_status == CW1200_JOIN_STATUS_MONITOR) {
			if (!cw1200_disable_listening(priv))
				priv->join_status = CW1200_JOIN_STATUS_PASSIVE;
			wsm_set_probe_responder(priv, false);
		}
	}
}

int cw1200_set_uapsd_param(struct cw1200_common *priv,
			   const struct wsm_edca_params *arg)
{
	int ret;
	u16 uapsd_flags = 0;

	/* Here's the mapping AC [queue, bit]
	 *  VO [0,3], VI [1, 2], BE [2, 1], BK [3, 0]
	 */

	if (arg->uapsd_enable[0])
		uapsd_flags |= 1 << 3;

	if (arg->uapsd_enable[1])
		uapsd_flags |= 1 << 2;

	if (arg->uapsd_enable[2])
		uapsd_flags |= 1 << 1;

	if (arg->uapsd_enable[3])
		uapsd_flags |= 1;

	/* Currently pseudo U-APSD operation is not supported, so setting
	 * MinAutoTriggerInterval, MaxAutoTriggerInterval and
	 * AutoTriggerStep to 0
	 */

	priv->uapsd_info.uapsd_flags = cpu_to_le16(uapsd_flags);
	priv->uapsd_info.min_auto_trigger_interval = 0;
	priv->uapsd_info.max_auto_trigger_interval = 0;
	priv->uapsd_info.auto_trigger_step = 0;

	ret = wsm_set_uapsd_info(priv, &priv->uapsd_info);
	return ret;
}

/* ******************************************************************** */
/* AP API								*/

int cw1200_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta)
{
	struct cw1200_common *priv = hw->priv;
	struct cw1200_sta_priv *sta_priv =
			(struct cw1200_sta_priv *)&sta->drv_priv;
	struct cw1200_link_entry *entry;
	struct sk_buff *skb;

	if (priv->mode != NL80211_IFTYPE_AP)
		return 0;

	sta_priv->link_id = cw1200_find_link_id(priv, sta->addr);
	if (WARN_ON(!sta_priv->link_id)) {
		wiphy_info(priv->hw->wiphy,
			   "[AP] No more link IDs available.\n");
		return -ENOENT;
	}

	entry = &priv->link_id_db[sta_priv->link_id - 1];
	spin_lock_bh(&priv->ps_state_lock);
	if ((sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_MASK) ==
					IEEE80211_WMM_IE_STA_QOSINFO_AC_MASK)
		priv->sta_asleep_mask |= BIT(sta_priv->link_id);
	entry->status = CW1200_LINK_HARD;
	while ((skb = skb_dequeue(&entry->rx_queue)))
		ieee80211_rx_irqsafe(priv->hw, skb);
	spin_unlock_bh(&priv->ps_state_lock);
	return 0;
}

int cw1200_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta)
{
	struct cw1200_common *priv = hw->priv;
	struct cw1200_sta_priv *sta_priv =
			(struct cw1200_sta_priv *)&sta->drv_priv;
	struct cw1200_link_entry *entry;

	if (priv->mode != NL80211_IFTYPE_AP || !sta_priv->link_id)
		return 0;

	entry = &priv->link_id_db[sta_priv->link_id - 1];
	spin_lock_bh(&priv->ps_state_lock);
	entry->status = CW1200_LINK_RESERVE;
	entry->timestamp = jiffies;
	wsm_lock_tx_async(priv);
	if (queue_work(priv->workqueue, &priv->link_id_work) <= 0)
		wsm_unlock_tx(priv);
	spin_unlock_bh(&priv->ps_state_lock);
	flush_workqueue(priv->workqueue);
	return 0;
}

static void __cw1200_sta_notify(struct ieee80211_hw *dev,
				struct ieee80211_vif *vif,
				enum sta_notify_cmd notify_cmd,
				int link_id)
{
	struct cw1200_common *priv = dev->priv;
	u32 bit, prev;

	/* Zero link id means "for all link IDs" */
	if (link_id)
		bit = BIT(link_id);
	else if (WARN_ON_ONCE(notify_cmd != STA_NOTIFY_AWAKE))
		bit = 0;
	else
		bit = priv->link_id_map;
	prev = priv->sta_asleep_mask & bit;

	switch (notify_cmd) {
	case STA_NOTIFY_SLEEP:
		if (!prev) {
			if (priv->buffered_multicasts &&
			    !priv->sta_asleep_mask)
				queue_work(priv->workqueue,
					   &priv->multicast_start_work);
			priv->sta_asleep_mask |= bit;
		}
		break;
	case STA_NOTIFY_AWAKE:
		if (prev) {
			priv->sta_asleep_mask &= ~bit;
			priv->pspoll_mask &= ~bit;
			if (priv->tx_multicast && link_id &&
			    !priv->sta_asleep_mask)
				queue_work(priv->workqueue,
					   &priv->multicast_stop_work);
			cw1200_bh_wakeup(priv);
		}
		break;
	}
}

void cw1200_sta_notify(struct ieee80211_hw *dev,
		       struct ieee80211_vif *vif,
		       enum sta_notify_cmd notify_cmd,
		       struct ieee80211_sta *sta)
{
	struct cw1200_common *priv = dev->priv;
	struct cw1200_sta_priv *sta_priv =
		(struct cw1200_sta_priv *)&sta->drv_priv;

	spin_lock_bh(&priv->ps_state_lock);
	__cw1200_sta_notify(dev, vif, notify_cmd, sta_priv->link_id);
	spin_unlock_bh(&priv->ps_state_lock);
}

static void cw1200_ps_notify(struct cw1200_common *priv,
		      int link_id, bool ps)
{
	if (link_id > CW1200_MAX_STA_IN_AP_MODE)
		return;

	pr_debug("%s for LinkId: %d. STAs asleep: %.8X\n",
		 ps ? "Stop" : "Start",
		 link_id, priv->sta_asleep_mask);

	__cw1200_sta_notify(priv->hw, priv->vif,
			    ps ? STA_NOTIFY_SLEEP : STA_NOTIFY_AWAKE, link_id);
}

static int cw1200_set_tim_impl(struct cw1200_common *priv, bool aid0_bit_set)
{
	struct sk_buff *skb;
	struct wsm_update_ie update_ie = {
		.what = WSM_UPDATE_IE_BEACON,
		.count = 1,
	};
	u16 tim_offset, tim_length;

	pr_debug("[AP] mcast: %s.\n", aid0_bit_set ? "ena" : "dis");

	skb = ieee80211_beacon_get_tim(priv->hw, priv->vif,
			&tim_offset, &tim_length);
	if (!skb) {
		if (!__cw1200_flush(priv, true))
			wsm_unlock_tx(priv);
		return -ENOENT;
	}

	if (tim_offset && tim_length >= 6) {
		/* Ignore DTIM count from mac80211:
		 * firmware handles DTIM internally.
		 */
		skb->data[tim_offset + 2] = 0;

		/* Set/reset aid0 bit */
		if (aid0_bit_set)
			skb->data[tim_offset + 4] |= 1;
		else
			skb->data[tim_offset + 4] &= ~1;
	}

	update_ie.ies = &skb->data[tim_offset];
	update_ie.length = tim_length;
	wsm_update_ie(priv, &update_ie);

	dev_kfree_skb(skb);

	return 0;
}

void cw1200_set_tim_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, set_tim_work);
	(void)cw1200_set_tim_impl(priv, priv->aid0_bit_set);
}

int cw1200_set_tim(struct ieee80211_hw *dev, struct ieee80211_sta *sta,
		   bool set)
{
	struct cw1200_common *priv = dev->priv;
	queue_work(priv->workqueue, &priv->set_tim_work);
	return 0;
}

void cw1200_set_cts_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, set_cts_work);

	u8 erp_ie[3] = {WLAN_EID_ERP_INFO, 0x1, 0};
	struct wsm_update_ie update_ie = {
		.what = WSM_UPDATE_IE_BEACON,
		.count = 1,
		.ies = erp_ie,
		.length = 3,
	};
	u32 erp_info;
	__le32 use_cts_prot;
	mutex_lock(&priv->conf_mutex);
	erp_info = priv->erp_info;
	mutex_unlock(&priv->conf_mutex);
	use_cts_prot =
		erp_info & WLAN_ERP_USE_PROTECTION ?
		__cpu_to_le32(1) : 0;

	erp_ie[ERP_INFO_BYTE_OFFSET] = erp_info;

	pr_debug("[STA] ERP information 0x%x\n", erp_info);

	wsm_write_mib(priv, WSM_MIB_ID_NON_ERP_PROTECTION,
		      &use_cts_prot, sizeof(use_cts_prot));
	wsm_update_ie(priv, &update_ie);

	return;
}

static int cw1200_set_btcoexinfo(struct cw1200_common *priv)
{
	struct wsm_override_internal_txrate arg;
	int ret = 0;

	if (priv->mode == NL80211_IFTYPE_STATION) {
		/* Plumb PSPOLL and NULL template */
		cw1200_upload_pspoll(priv);
		cw1200_upload_null(priv);
		cw1200_upload_qosnull(priv);
	} else {
		return 0;
	}

	memset(&arg, 0, sizeof(struct wsm_override_internal_txrate));

	if (!priv->vif->p2p) {
		/* STATION mode */
		if (priv->bss_params.operational_rate_set & ~0xF) {
			pr_debug("[STA] STA has ERP rates\n");
			/* G or BG mode */
			arg.internalTxRate = (__ffs(
			priv->bss_params.operational_rate_set & ~0xF));
		} else {
			pr_debug("[STA] STA has non ERP rates\n");
			/* B only mode */
			arg.internalTxRate = (__ffs(le32_to_cpu(priv->association_mode.basic_rate_set)));
		}
		arg.nonErpInternalTxRate = (__ffs(le32_to_cpu(priv->association_mode.basic_rate_set)));
	} else {
		/* P2P mode */
		arg.internalTxRate = (__ffs(priv->bss_params.operational_rate_set & ~0xF));
		arg.nonErpInternalTxRate = (__ffs(priv->bss_params.operational_rate_set & ~0xF));
	}

	pr_debug("[STA] BTCOEX_INFO MODE %d, internalTxRate : %x, nonErpInternalTxRate: %x\n",
		 priv->mode,
		 arg.internalTxRate,
		 arg.nonErpInternalTxRate);

	ret = wsm_write_mib(priv, WSM_MIB_ID_OVERRIDE_INTERNAL_TX_RATE,
			    &arg, sizeof(arg));

	return ret;
}

void cw1200_bss_info_changed(struct ieee80211_hw *dev,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *info,
			     u32 changed)
{
	struct cw1200_common *priv = dev->priv;
	bool do_join = false;

	mutex_lock(&priv->conf_mutex);

	pr_debug("BSS CHANGED:  %08x\n", changed);

	/* TODO: BSS_CHANGED_QOS */
	/* TODO: BSS_CHANGED_TXPOWER */

	if (changed & BSS_CHANGED_ARP_FILTER) {
		struct wsm_mib_arp_ipv4_filter filter = {0};
		int i;

		pr_debug("[STA] BSS_CHANGED_ARP_FILTER cnt: %d\n",
			 info->arp_addr_cnt);

		/* Currently only one IP address is supported by firmware.
		 * In case of more IPs arp filtering will be disabled.
		 */
		if (info->arp_addr_cnt > 0 &&
		    info->arp_addr_cnt <= WSM_MAX_ARP_IP_ADDRTABLE_ENTRIES) {
			for (i = 0; i < info->arp_addr_cnt; i++) {
				filter.ipv4addrs[i] = info->arp_addr_list[i];
				pr_debug("[STA] addr[%d]: 0x%X\n",
					 i, filter.ipv4addrs[i]);
			}
			filter.enable = __cpu_to_le32(1);
		}

		pr_debug("[STA] arp ip filter enable: %d\n",
			 __le32_to_cpu(filter.enable));

		wsm_set_arp_ipv4_filter(priv, &filter);
	}

	if (changed &
	    (BSS_CHANGED_BEACON |
	     BSS_CHANGED_AP_PROBE_RESP |
	     BSS_CHANGED_BSSID |
	     BSS_CHANGED_SSID |
	     BSS_CHANGED_IBSS)) {
		pr_debug("BSS_CHANGED_BEACON\n");
		priv->beacon_int = info->beacon_int;
		cw1200_update_beaconing(priv);
		cw1200_upload_beacon(priv);
	}

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		pr_debug("BSS_CHANGED_BEACON_ENABLED (%d)\n", info->enable_beacon);

		if (priv->enable_beacon != info->enable_beacon) {
			cw1200_enable_beaconing(priv, info->enable_beacon);
			priv->enable_beacon = info->enable_beacon;
		}
	}

	if (changed & BSS_CHANGED_BEACON_INT) {
		pr_debug("CHANGED_BEACON_INT\n");
		if (info->ibss_joined)
			do_join = true;
		else if (priv->join_status == CW1200_JOIN_STATUS_AP)
			cw1200_update_beaconing(priv);
	}

	/* assoc/disassoc, or maybe AID changed */
	if (changed & BSS_CHANGED_ASSOC) {
		wsm_lock_tx(priv);
		priv->wep_default_key_id = -1;
		wsm_unlock_tx(priv);
	}

	if (changed & BSS_CHANGED_BSSID) {
		pr_debug("BSS_CHANGED_BSSID\n");
		do_join = true;
	}

	if (changed &
	    (BSS_CHANGED_ASSOC |
	     BSS_CHANGED_BSSID |
	     BSS_CHANGED_IBSS |
	     BSS_CHANGED_BASIC_RATES |
	     BSS_CHANGED_HT)) {
		pr_debug("BSS_CHANGED_ASSOC\n");
		if (info->assoc) {
			if (priv->join_status < CW1200_JOIN_STATUS_PRE_STA) {
				ieee80211_connection_loss(vif);
				mutex_unlock(&priv->conf_mutex);
				return;
			} else if (priv->join_status == CW1200_JOIN_STATUS_PRE_STA) {
				priv->join_status = CW1200_JOIN_STATUS_STA;
			}
		} else {
			do_join = true;
		}

		if (info->assoc || info->ibss_joined) {
			struct ieee80211_sta *sta = NULL;
			__le32 htprot = 0;

			if (info->dtim_period)
				priv->join_dtim_period = info->dtim_period;
			priv->beacon_int = info->beacon_int;

			rcu_read_lock();

			if (info->bssid && !info->ibss_joined)
				sta = ieee80211_find_sta(vif, info->bssid);
			if (sta) {
				priv->ht_info.ht_cap = sta->ht_cap;
				priv->bss_params.operational_rate_set =
					cw1200_rate_mask_to_wsm(priv,
								sta->supp_rates[priv->channel->band]);
				priv->ht_info.channel_type = cfg80211_get_chandef_type(&dev->conf.chandef);
				priv->ht_info.operation_mode = info->ht_operation_mode;
			} else {
				memset(&priv->ht_info, 0,
				       sizeof(priv->ht_info));
				priv->bss_params.operational_rate_set = -1;
			}
			rcu_read_unlock();

			/* Non Greenfield stations present */
			if (priv->ht_info.operation_mode &
			    IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT)
				htprot |= cpu_to_le32(WSM_NON_GREENFIELD_STA_PRESENT);

			/* Set HT protection method */
			htprot |= cpu_to_le32((priv->ht_info.operation_mode & IEEE80211_HT_OP_MODE_PROTECTION) << 2);

			/* TODO:
			 * STBC_param.dual_cts
			 *  STBC_param.LSIG_TXOP_FILL
			 */

			wsm_write_mib(priv, WSM_MIB_ID_SET_HT_PROTECTION,
				      &htprot, sizeof(htprot));

			priv->association_mode.greenfield =
				cw1200_ht_greenfield(&priv->ht_info);
			priv->association_mode.flags =
				WSM_ASSOCIATION_MODE_SNOOP_ASSOC_FRAMES |
				WSM_ASSOCIATION_MODE_USE_PREAMBLE_TYPE |
				WSM_ASSOCIATION_MODE_USE_HT_MODE |
				WSM_ASSOCIATION_MODE_USE_BASIC_RATE_SET |
				WSM_ASSOCIATION_MODE_USE_MPDU_START_SPACING;
			priv->association_mode.preamble =
				info->use_short_preamble ?
				WSM_JOIN_PREAMBLE_SHORT :
				WSM_JOIN_PREAMBLE_LONG;
			priv->association_mode.basic_rate_set = __cpu_to_le32(
				cw1200_rate_mask_to_wsm(priv,
							info->basic_rates));
			priv->association_mode.mpdu_start_spacing =
				cw1200_ht_ampdu_density(&priv->ht_info);

			cw1200_cqm_bssloss_sm(priv, 0, 0, 0);
			cancel_work_sync(&priv->unjoin_work);

			priv->bss_params.beacon_lost_count = priv->cqm_beacon_loss_count;
			priv->bss_params.aid = info->aid;

			if (priv->join_dtim_period < 1)
				priv->join_dtim_period = 1;

			pr_debug("[STA] DTIM %d, interval: %d\n",
				 priv->join_dtim_period, priv->beacon_int);
			pr_debug("[STA] Preamble: %d, Greenfield: %d, Aid: %d, Rates: 0x%.8X, Basic: 0x%.8X\n",
				 priv->association_mode.preamble,
				 priv->association_mode.greenfield,
				 priv->bss_params.aid,
				 priv->bss_params.operational_rate_set,
				 priv->association_mode.basic_rate_set);
			wsm_set_association_mode(priv, &priv->association_mode);

			if (!info->ibss_joined) {
				wsm_keep_alive_period(priv, 30 /* sec */);
				wsm_set_bss_params(priv, &priv->bss_params);
				priv->setbssparams_done = true;
				cw1200_set_beacon_wakeup_period_work(&priv->set_beacon_wakeup_period_work);
				cw1200_set_pm(priv, &priv->powersave_mode);
			}
			if (priv->vif->p2p) {
				pr_debug("[STA] Setting p2p powersave configuration.\n");
				wsm_set_p2p_ps_modeinfo(priv,
							&priv->p2p_ps_modeinfo);
			}
			if (priv->bt_present)
				cw1200_set_btcoexinfo(priv);
		} else {
			memset(&priv->association_mode, 0,
			       sizeof(priv->association_mode));
			memset(&priv->bss_params, 0, sizeof(priv->bss_params));
		}
	}

	/* ERP Protection */
	if (changed & (BSS_CHANGED_ASSOC |
		       BSS_CHANGED_ERP_CTS_PROT |
		       BSS_CHANGED_ERP_PREAMBLE)) {
		u32 prev_erp_info = priv->erp_info;
		if (info->use_cts_prot)
			priv->erp_info |= WLAN_ERP_USE_PROTECTION;
		else if (!(prev_erp_info & WLAN_ERP_NON_ERP_PRESENT))
			priv->erp_info &= ~WLAN_ERP_USE_PROTECTION;

		if (info->use_short_preamble)
			priv->erp_info |= WLAN_ERP_BARKER_PREAMBLE;
		else
			priv->erp_info &= ~WLAN_ERP_BARKER_PREAMBLE;

		pr_debug("[STA] ERP Protection: %x\n", priv->erp_info);

		if (prev_erp_info != priv->erp_info)
			queue_work(priv->workqueue, &priv->set_cts_work);
	}

	/* ERP Slottime */
	if (changed & (BSS_CHANGED_ASSOC | BSS_CHANGED_ERP_SLOT)) {
		__le32 slot_time = info->use_short_slot ?
			__cpu_to_le32(9) : __cpu_to_le32(20);
		pr_debug("[STA] Slot time: %d us.\n",
			 __le32_to_cpu(slot_time));
		wsm_write_mib(priv, WSM_MIB_ID_DOT11_SLOT_TIME,
			      &slot_time, sizeof(slot_time));
	}

	if (changed & (BSS_CHANGED_ASSOC | BSS_CHANGED_CQM)) {
		struct wsm_rcpi_rssi_threshold threshold = {
			.rollingAverageCount = 8,
		};
		pr_debug("[CQM] RSSI threshold subscribe: %d +- %d\n",
			 info->cqm_rssi_thold, info->cqm_rssi_hyst);
		priv->cqm_rssi_thold = info->cqm_rssi_thold;
		priv->cqm_rssi_hyst = info->cqm_rssi_hyst;

		if (info->cqm_rssi_thold || info->cqm_rssi_hyst) {
			/* RSSI subscription enabled */
			/* TODO: It's not a correct way of setting threshold.
			 * Upper and lower must be set equal here and adjusted
			 * in callback. However current implementation is much
			 * more relaible and stable.
			 */

			/* RSSI: signed Q8.0, RCPI: unsigned Q7.1
			 * RSSI = RCPI / 2 - 110
			 */
			if (priv->cqm_use_rssi) {
				threshold.upperThreshold =
					info->cqm_rssi_thold + info->cqm_rssi_hyst;
				threshold.lowerThreshold =
					info->cqm_rssi_thold;
				threshold.rssiRcpiMode |= WSM_RCPI_RSSI_USE_RSSI;
			} else {
				threshold.upperThreshold = (info->cqm_rssi_thold + info->cqm_rssi_hyst + 110) * 2;
				threshold.lowerThreshold = (info->cqm_rssi_thold + 110) * 2;
			}
			threshold.rssiRcpiMode |= WSM_RCPI_RSSI_THRESHOLD_ENABLE;
		} else {
			/* There is a bug in FW, see sta.c. We have to enable
			 * dummy subscription to get correct RSSI values.
			 */
			threshold.rssiRcpiMode |=
				WSM_RCPI_RSSI_THRESHOLD_ENABLE |
				WSM_RCPI_RSSI_DONT_USE_UPPER |
				WSM_RCPI_RSSI_DONT_USE_LOWER;
			if (priv->cqm_use_rssi)
				threshold.rssiRcpiMode |= WSM_RCPI_RSSI_USE_RSSI;
		}
		wsm_set_rcpi_rssi_threshold(priv, &threshold);
	}
	mutex_unlock(&priv->conf_mutex);

	if (do_join) {
		wsm_lock_tx(priv);
		cw1200_do_join(priv); /* Will unlock it for us */
	}
}

void cw1200_multicast_start_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, multicast_start_work);
	long tmo = priv->join_dtim_period *
			(priv->beacon_int + 20) * HZ / 1024;

	cancel_work_sync(&priv->multicast_stop_work);

	if (!priv->aid0_bit_set) {
		wsm_lock_tx(priv);
		cw1200_set_tim_impl(priv, true);
		priv->aid0_bit_set = true;
		mod_timer(&priv->mcast_timeout, jiffies + tmo);
		wsm_unlock_tx(priv);
	}
}

void cw1200_multicast_stop_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, multicast_stop_work);

	if (priv->aid0_bit_set) {
		del_timer_sync(&priv->mcast_timeout);
		wsm_lock_tx(priv);
		priv->aid0_bit_set = false;
		cw1200_set_tim_impl(priv, false);
		wsm_unlock_tx(priv);
	}
}

void cw1200_mcast_timeout(struct timer_list *t)
{
	struct cw1200_common *priv = from_timer(priv, t, mcast_timeout);

	wiphy_warn(priv->hw->wiphy,
		   "Multicast delivery timeout.\n");
	spin_lock_bh(&priv->ps_state_lock);
	priv->tx_multicast = priv->aid0_bit_set &&
			priv->buffered_multicasts;
	if (priv->tx_multicast)
		cw1200_bh_wakeup(priv);
	spin_unlock_bh(&priv->ps_state_lock);
}

int cw1200_ampdu_action(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			struct ieee80211_ampdu_params *params)
{
	/* Aggregation is implemented fully in firmware,
	 * including block ack negotiation. Do not allow
	 * mac80211 stack to do anything: it interferes with
	 * the firmware.
	 */

	/* Note that we still need this function stubbed. */
	return -ENOTSUPP;
}

/* ******************************************************************** */
/* WSM callback								*/
void cw1200_suspend_resume(struct cw1200_common *priv,
			  struct wsm_suspend_resume *arg)
{
	pr_debug("[AP] %s: %s\n",
		 arg->stop ? "stop" : "start",
		 arg->multicast ? "broadcast" : "unicast");

	if (arg->multicast) {
		bool cancel_tmo = false;
		spin_lock_bh(&priv->ps_state_lock);
		if (arg->stop) {
			priv->tx_multicast = false;
		} else {
			/* Firmware sends this indication every DTIM if there
			 * is a STA in powersave connected. There is no reason
			 * to suspend, following wakeup will consume much more
			 * power than it could be saved.
			 */
			cw1200_pm_stay_awake(&priv->pm_state,
					     priv->join_dtim_period *
					     (priv->beacon_int + 20) * HZ / 1024);
			priv->tx_multicast = (priv->aid0_bit_set &&
					      priv->buffered_multicasts);
			if (priv->tx_multicast) {
				cancel_tmo = true;
				cw1200_bh_wakeup(priv);
			}
		}
		spin_unlock_bh(&priv->ps_state_lock);
		if (cancel_tmo)
			del_timer_sync(&priv->mcast_timeout);
	} else {
		spin_lock_bh(&priv->ps_state_lock);
		cw1200_ps_notify(priv, arg->link_id, arg->stop);
		spin_unlock_bh(&priv->ps_state_lock);
		if (!arg->stop)
			cw1200_bh_wakeup(priv);
	}
	return;
}

/* ******************************************************************** */
/* AP privates								*/

static int cw1200_upload_beacon(struct cw1200_common *priv)
{
	int ret = 0;
	struct ieee80211_mgmt *mgmt;
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_BEACON,
	};

	u16 tim_offset;
	u16 tim_len;

	if (priv->mode == NL80211_IFTYPE_STATION ||
	    priv->mode == NL80211_IFTYPE_MONITOR ||
	    priv->mode == NL80211_IFTYPE_UNSPECIFIED)
		goto done;

	if (priv->vif->p2p)
		frame.rate = WSM_TRANSMIT_RATE_6;

	frame.skb = ieee80211_beacon_get_tim(priv->hw, priv->vif,
					     &tim_offset, &tim_len);
	if (!frame.skb)
		return -ENOMEM;

	ret = wsm_set_template_frame(priv, &frame);

	if (ret)
		goto done;

	/* TODO: Distill probe resp; remove TIM
	 * and any other beacon-specific IEs
	 */
	mgmt = (void *)frame.skb->data;
	mgmt->frame_control =
		__cpu_to_le16(IEEE80211_FTYPE_MGMT |
			      IEEE80211_STYPE_PROBE_RESP);

	frame.frame_type = WSM_FRAME_TYPE_PROBE_RESPONSE;
	if (priv->vif->p2p) {
		ret = wsm_set_probe_responder(priv, true);
	} else {
		ret = wsm_set_template_frame(priv, &frame);
		wsm_set_probe_responder(priv, false);
	}

done:
	dev_kfree_skb(frame.skb);

	return ret;
}

static int cw1200_upload_pspoll(struct cw1200_common *priv)
{
	int ret = 0;
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_PS_POLL,
		.rate = 0xFF,
	};


	frame.skb = ieee80211_pspoll_get(priv->hw, priv->vif);
	if (!frame.skb)
		return -ENOMEM;

	ret = wsm_set_template_frame(priv, &frame);

	dev_kfree_skb(frame.skb);

	return ret;
}

static int cw1200_upload_null(struct cw1200_common *priv)
{
	int ret = 0;
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_NULL,
		.rate = 0xFF,
	};

	frame.skb = ieee80211_nullfunc_get(priv->hw, priv->vif, false);
	if (!frame.skb)
		return -ENOMEM;

	ret = wsm_set_template_frame(priv, &frame);

	dev_kfree_skb(frame.skb);

	return ret;
}

static int cw1200_upload_qosnull(struct cw1200_common *priv)
{
	/* TODO:  This needs to be implemented

	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_QOS_NULL,
		.rate = 0xFF,
	};

	frame.skb = ieee80211_qosnullfunc_get(priv->hw, priv->vif);
	if (!frame.skb)
		return -ENOMEM;

	ret = wsm_set_template_frame(priv, &frame);

	dev_kfree_skb(frame.skb);

	*/
	return 0;
}

static int cw1200_enable_beaconing(struct cw1200_common *priv,
				   bool enable)
{
	struct wsm_beacon_transmit transmit = {
		.enable_beaconing = enable,
	};

	return wsm_beacon_transmit(priv, &transmit);
}

static int cw1200_start_ap(struct cw1200_common *priv)
{
	int ret;
	struct ieee80211_bss_conf *conf = &priv->vif->bss_conf;
	struct wsm_start start = {
		.mode = priv->vif->p2p ?
				WSM_START_MODE_P2P_GO : WSM_START_MODE_AP,
		.band = (priv->channel->band == NL80211_BAND_5GHZ) ?
				WSM_PHY_BAND_5G : WSM_PHY_BAND_2_4G,
		.channel_number = priv->channel->hw_value,
		.beacon_interval = conf->beacon_int,
		.dtim_period = conf->dtim_period,
		.preamble = conf->use_short_preamble ?
				WSM_JOIN_PREAMBLE_SHORT :
				WSM_JOIN_PREAMBLE_LONG,
		.probe_delay = 100,
		.basic_rate_set = cw1200_rate_mask_to_wsm(priv,
				conf->basic_rates),
	};
	struct wsm_operational_mode mode = {
		.power_mode = cw1200_power_mode,
		.disable_more_flag_usage = true,
	};

	memset(start.ssid, 0, sizeof(start.ssid));
	if (!conf->hidden_ssid) {
		start.ssid_len = conf->ssid_len;
		memcpy(start.ssid, conf->ssid, start.ssid_len);
	}

	priv->beacon_int = conf->beacon_int;
	priv->join_dtim_period = conf->dtim_period;

	memset(&priv->link_id_db, 0, sizeof(priv->link_id_db));

	pr_debug("[AP] ch: %d(%d), bcn: %d(%d), brt: 0x%.8X, ssid: %.*s.\n",
		 start.channel_number, start.band,
		 start.beacon_interval, start.dtim_period,
		 start.basic_rate_set,
		 start.ssid_len, start.ssid);
	ret = wsm_start(priv, &start);
	if (!ret)
		ret = cw1200_upload_keys(priv);
	if (!ret && priv->vif->p2p) {
		pr_debug("[AP] Setting p2p powersave configuration.\n");
		wsm_set_p2p_ps_modeinfo(priv, &priv->p2p_ps_modeinfo);
	}
	if (!ret) {
		wsm_set_block_ack_policy(priv, 0, 0);
		priv->join_status = CW1200_JOIN_STATUS_AP;
		cw1200_update_filtering(priv);
	}
	wsm_set_operational_mode(priv, &mode);
	return ret;
}

static int cw1200_update_beaconing(struct cw1200_common *priv)
{
	struct ieee80211_bss_conf *conf = &priv->vif->bss_conf;
	struct wsm_reset reset = {
		.link_id = 0,
		.reset_statistics = true,
	};

	if (priv->mode == NL80211_IFTYPE_AP) {
		/* TODO: check if changed channel, band */
		if (priv->join_status != CW1200_JOIN_STATUS_AP ||
		    priv->beacon_int != conf->beacon_int) {
			pr_debug("ap restarting\n");
			wsm_lock_tx(priv);
			if (priv->join_status != CW1200_JOIN_STATUS_PASSIVE)
				wsm_reset(priv, &reset);
			priv->join_status = CW1200_JOIN_STATUS_PASSIVE;
			cw1200_start_ap(priv);
			wsm_unlock_tx(priv);
		} else
			pr_debug("ap started join_status: %d\n",
				 priv->join_status);
	}
	return 0;
}
