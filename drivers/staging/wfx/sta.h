/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Implementation of mac80211 API.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#ifndef WFX_STA_H
#define WFX_STA_H

#include <net/mac80211.h>

struct wfx_dev;
struct wfx_vif;

struct wfx_sta_priv {
	int link_id;
	int vif_id;
};

/* mac80211 interface */
int wfx_start(struct ieee80211_hw *hw);
void wfx_stop(struct ieee80211_hw *hw);
int wfx_config(struct ieee80211_hw *hw, u32 changed);
int wfx_set_rts_threshold(struct ieee80211_hw *hw, u32 value);
void wfx_set_default_unicast_key(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif, int idx);
void wfx_configure_filter(struct ieee80211_hw *hw, unsigned int changed_flags,
			  unsigned int *total_flags, u64 unused);

int wfx_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
void wfx_remove_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
int wfx_start_ap(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
void wfx_stop_ap(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
int wfx_join_ibss(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
void wfx_leave_ibss(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
int wfx_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		u16 queue, const struct ieee80211_tx_queue_params *params);
void wfx_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *info, u32 changed);
int wfx_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		struct ieee80211_sta *sta);
int wfx_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta);
void wfx_sta_notify(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    enum sta_notify_cmd cmd, struct ieee80211_sta *sta);
int wfx_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta, bool set);
int wfx_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		     struct ieee80211_ampdu_params *params);
int wfx_add_chanctx(struct ieee80211_hw *hw,
		    struct ieee80211_chanctx_conf *conf);
void wfx_remove_chanctx(struct ieee80211_hw *hw,
			struct ieee80211_chanctx_conf *conf);
void wfx_change_chanctx(struct ieee80211_hw *hw,
			struct ieee80211_chanctx_conf *conf, u32 changed);
int wfx_assign_vif_chanctx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_chanctx_conf *conf);
void wfx_unassign_vif_chanctx(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_chanctx_conf *conf);

/* Hardware API Callbacks */
void wfx_cooling_timeout_work(struct work_struct *work);
void wfx_suspend_hot_dev(struct wfx_dev *wdev, enum sta_notify_cmd cmd);
void wfx_suspend_resume_mc(struct wfx_vif *wvif, enum sta_notify_cmd cmd);
void wfx_event_report_rssi(struct wfx_vif *wvif, u8 raw_rcpi_rssi);
int wfx_update_pm(struct wfx_vif *wvif);

/* Other Helpers */
void wfx_reset(struct wfx_vif *wvif);
u32 wfx_rate_mask_to_hw(struct wfx_dev *wdev, u32 rates);

#endif /* WFX_STA_H */
