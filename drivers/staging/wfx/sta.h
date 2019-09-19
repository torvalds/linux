/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Implementation of mac80211 API.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#ifndef WFX_STA_H
#define WFX_STA_H

#include <net/mac80211.h>

#include "hif_api_cmd.h"

struct wfx_dev;
struct wfx_vif;

enum wfx_state {
	WFX_STATE_PASSIVE = 0,
	WFX_STATE_PRE_STA,
	WFX_STATE_STA,
	WFX_STATE_IBSS,
	WFX_STATE_AP,
};

struct wfx_ht_info {
	struct ieee80211_sta_ht_cap ht_cap;
	enum nl80211_channel_type channel_type;
	uint16_t operation_mode;
};

struct wfx_hif_event {
	struct list_head link;
	struct hif_ind_event evt;
};

struct wfx_edca_params {
	/* NOTE: index is a linux queue id. */
	struct hif_req_edca_queue_params params[IEEE80211_NUM_ACS];
	bool uapsd_enable[IEEE80211_NUM_ACS];
};

struct wfx_grp_addr_table {
	bool enable;
	int num_addresses;
	u8 address_list[8][ETH_ALEN];
};

struct wfx_sta_priv {
	int link_id;
	int vif_id;
};

// mac80211 interface
int wfx_start(struct ieee80211_hw *hw);
void wfx_stop(struct ieee80211_hw *hw);
int wfx_config(struct ieee80211_hw *hw, u32 changed);
int wfx_set_rts_threshold(struct ieee80211_hw *hw, u32 value);
u64 wfx_prepare_multicast(struct ieee80211_hw *hw,
			  struct netdev_hw_addr_list *mc_list);
void wfx_configure_filter(struct ieee80211_hw *hw, unsigned int changed_flags,
			  unsigned int *total_flags, u64 unused);

int wfx_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
void wfx_remove_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
void wfx_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       u32 queues, bool drop);
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
void wfx_unassign_vif_chanctx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			      struct ieee80211_chanctx_conf *conf);

// WSM Callbacks
void wfx_suspend_resume(struct wfx_vif *wvif, struct hif_ind_suspend_resume_tx *arg);

// Other Helpers
void wfx_cqm_bssloss_sm(struct wfx_vif *wvif, int init, int good, int bad);
void wfx_update_filtering(struct wfx_vif *wvif);
int wfx_set_pm(struct wfx_vif *wvif, const struct hif_req_set_pm_mode *arg);
int wfx_fwd_probe_req(struct wfx_vif *wvif, bool enable);

#endif /* WFX_STA_H */
