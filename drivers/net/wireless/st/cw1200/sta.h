/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Mac80211 STA interface for ST-Ericsson CW1200 mac80211 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 */

#ifndef STA_H_INCLUDED
#define STA_H_INCLUDED

/* ******************************************************************** */
/* mac80211 API								*/

int cw1200_start(struct ieee80211_hw *dev);
void cw1200_stop(struct ieee80211_hw *dev);
int cw1200_add_interface(struct ieee80211_hw *dev,
			 struct ieee80211_vif *vif);
void cw1200_remove_interface(struct ieee80211_hw *dev,
			     struct ieee80211_vif *vif);
int cw1200_change_interface(struct ieee80211_hw *dev,
			    struct ieee80211_vif *vif,
			    enum nl80211_iftype new_type,
			    bool p2p);
int cw1200_config(struct ieee80211_hw *dev, u32 changed);
void cw1200_configure_filter(struct ieee80211_hw *dev,
			     unsigned int changed_flags,
			     unsigned int *total_flags,
			     u64 multicast);
int cw1200_conf_tx(struct ieee80211_hw *dev, struct ieee80211_vif *vif,
		   unsigned int link_id, u16 queue,
		   const struct ieee80211_tx_queue_params *params);
int cw1200_get_stats(struct ieee80211_hw *dev,
		     struct ieee80211_low_level_stats *stats);
int cw1200_set_key(struct ieee80211_hw *dev, enum set_key_cmd cmd,
		   struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		   struct ieee80211_key_conf *key);

int cw1200_set_rts_threshold(struct ieee80211_hw *hw, u32 value);

void cw1200_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  u32 queues, bool drop);

u64 cw1200_prepare_multicast(struct ieee80211_hw *hw,
			     struct netdev_hw_addr_list *mc_list);

int cw1200_set_pm(struct cw1200_common *priv, const struct wsm_set_pm *arg);

/* ******************************************************************** */
/* WSM callbacks							*/

void cw1200_join_complete_cb(struct cw1200_common *priv,
				struct wsm_join_complete *arg);

/* ******************************************************************** */
/* WSM events								*/

void cw1200_free_event_queue(struct cw1200_common *priv);
void cw1200_event_handler(struct work_struct *work);
void cw1200_bss_loss_work(struct work_struct *work);
void cw1200_bss_params_work(struct work_struct *work);
void cw1200_keep_alive_work(struct work_struct *work);
void cw1200_tx_failure_work(struct work_struct *work);

void __cw1200_cqm_bssloss_sm(struct cw1200_common *priv, int init, int good,
			     int bad);
static inline void cw1200_cqm_bssloss_sm(struct cw1200_common *priv,
					 int init, int good, int bad)
{
	spin_lock(&priv->bss_loss_lock);
	__cw1200_cqm_bssloss_sm(priv, init, good, bad);
	spin_unlock(&priv->bss_loss_lock);
}

/* ******************************************************************** */
/* Internal API								*/

int cw1200_setup_mac(struct cw1200_common *priv);
void cw1200_join_timeout(struct work_struct *work);
void cw1200_unjoin_work(struct work_struct *work);
void cw1200_join_complete_work(struct work_struct *work);
void cw1200_wep_key_work(struct work_struct *work);
void cw1200_update_listening(struct cw1200_common *priv, bool enabled);
void cw1200_update_filtering(struct cw1200_common *priv);
void cw1200_update_filtering_work(struct work_struct *work);
void cw1200_set_beacon_wakeup_period_work(struct work_struct *work);
int cw1200_enable_listening(struct cw1200_common *priv);
int cw1200_disable_listening(struct cw1200_common *priv);
int cw1200_set_uapsd_param(struct cw1200_common *priv,
				const struct wsm_edca_params *arg);
void cw1200_ba_work(struct work_struct *work);
void cw1200_ba_timer(unsigned long arg);

/* AP stuffs */
int cw1200_set_tim(struct ieee80211_hw *dev, struct ieee80211_sta *sta,
		   bool set);
int cw1200_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta);
int cw1200_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta);
void cw1200_sta_notify(struct ieee80211_hw *dev, struct ieee80211_vif *vif,
		       enum sta_notify_cmd notify_cmd,
		       struct ieee80211_sta *sta);
void cw1200_bss_info_changed(struct ieee80211_hw *dev,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *info,
			     u64 changed);
int cw1200_ampdu_action(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			struct ieee80211_ampdu_params *params);

void cw1200_suspend_resume(struct cw1200_common *priv,
			  struct wsm_suspend_resume *arg);
void cw1200_set_tim_work(struct work_struct *work);
void cw1200_set_cts_work(struct work_struct *work);
void cw1200_multicast_start_work(struct work_struct *work);
void cw1200_multicast_stop_work(struct work_struct *work);
void cw1200_mcast_timeout(struct timer_list *t);

#endif
