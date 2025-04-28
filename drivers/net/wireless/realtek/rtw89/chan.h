/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Copyright(c) 2020-2022  Realtek Corporation
 */

#ifndef __RTW89_CHAN_H__
#define __RTW89_CHAN_H__

#include "core.h"

/* The dwell time in TU before doing rtw89_chanctx_work(). */
#define RTW89_CHANCTX_TIME_MCC_PREPARE 100
#define RTW89_CHANCTX_TIME_MCC 100

/* various MCC setting time in TU */
#define RTW89_MCC_LONG_TRIGGER_TIME 300
#define RTW89_MCC_SHORT_TRIGGER_TIME 100
#define RTW89_MCC_EARLY_TX_BCN_TIME 10
#define RTW89_MCC_EARLY_RX_BCN_TIME 5
#define RTW89_MCC_MIN_RX_BCN_TIME 10
#define RTW89_MCC_DFLT_BCN_OFST_TIME 40

#define RTW89_MCC_MIN_GO_DURATION \
	(RTW89_MCC_EARLY_TX_BCN_TIME + RTW89_MCC_MIN_RX_BCN_TIME)

#define RTW89_MCC_MIN_STA_DURATION \
	(RTW89_MCC_EARLY_RX_BCN_TIME + RTW89_MCC_MIN_RX_BCN_TIME)

#define RTW89_MCC_DFLT_GROUP 0
#define RTW89_MCC_NEXT_GROUP(cur) (((cur) + 1) % 4)

#define RTW89_MCC_DFLT_TX_NULL_EARLY 3
#define RTW89_MCC_DFLT_COURTESY_SLOT 3

#define RTW89_MCC_REQ_COURTESY_TIME 5
#define RTW89_MCC_REQ_COURTESY(pattern, role)			\
({								\
	const struct rtw89_mcc_pattern *p = pattern;		\
	p->tob_ ## role <= RTW89_MCC_REQ_COURTESY_TIME ||	\
	p->toa_ ## role <= RTW89_MCC_REQ_COURTESY_TIME;		\
})

#define NUM_OF_RTW89_MCC_ROLES 2

enum rtw89_chanctx_pause_reasons {
	RTW89_CHANCTX_PAUSE_REASON_HW_SCAN,
	RTW89_CHANCTX_PAUSE_REASON_ROC,
};

struct rtw89_chanctx_cb_parm {
	int (*cb)(struct rtw89_dev *rtwdev, void *data);
	void *data;
	const char *caller;
};

struct rtw89_entity_weight {
	unsigned int active_chanctxs;
	unsigned int active_roles;
};

static inline bool rtw89_get_entity_state(struct rtw89_dev *rtwdev,
					  enum rtw89_phy_idx phy_idx)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	return READ_ONCE(hal->entity_active[phy_idx]);
}

static inline void rtw89_set_entity_state(struct rtw89_dev *rtwdev,
					  enum rtw89_phy_idx phy_idx,
					  bool active)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	WRITE_ONCE(hal->entity_active[phy_idx], active);
}

static inline
enum rtw89_entity_mode rtw89_get_entity_mode(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	return READ_ONCE(hal->entity_mode);
}

static inline void rtw89_set_entity_mode(struct rtw89_dev *rtwdev,
					 enum rtw89_entity_mode mode)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	WRITE_ONCE(hal->entity_mode, mode);
}

void rtw89_chan_create(struct rtw89_chan *chan, u8 center_chan, u8 primary_chan,
		       enum rtw89_band band, enum rtw89_bandwidth bandwidth);
bool rtw89_assign_entity_chan(struct rtw89_dev *rtwdev,
			      enum rtw89_chanctx_idx idx,
			      const struct rtw89_chan *new);
int rtw89_iterate_entity_chan(struct rtw89_dev *rtwdev,
			      int (*iterator)(const struct rtw89_chan *chan,
					      void *data),
			      void *data);
void rtw89_config_entity_chandef(struct rtw89_dev *rtwdev,
				 enum rtw89_chanctx_idx idx,
				 const struct cfg80211_chan_def *chandef);
void rtw89_config_roc_chandef(struct rtw89_dev *rtwdev,
			      struct rtw89_vif_link *rtwvif_link,
			      const struct cfg80211_chan_def *chandef);
void rtw89_entity_init(struct rtw89_dev *rtwdev);
enum rtw89_entity_mode rtw89_entity_recalc(struct rtw89_dev *rtwdev);
void rtw89_chanctx_work(struct wiphy *wiphy, struct wiphy_work *work);
void rtw89_queue_chanctx_work(struct rtw89_dev *rtwdev);
void rtw89_queue_chanctx_change(struct rtw89_dev *rtwdev,
				enum rtw89_chanctx_changes change);
void rtw89_chanctx_track(struct rtw89_dev *rtwdev);
void rtw89_chanctx_pause(struct rtw89_dev *rtwdev,
			 enum rtw89_chanctx_pause_reasons rsn);
void rtw89_chanctx_proceed(struct rtw89_dev *rtwdev,
			   const struct rtw89_chanctx_cb_parm *cb_parm);

const struct rtw89_chan *__rtw89_mgnt_chan_get(struct rtw89_dev *rtwdev,
					       const char *caller_message,
					       u8 link_index);

#define rtw89_mgnt_chan_get(rtwdev, link_index) \
	__rtw89_mgnt_chan_get(rtwdev, __func__, link_index)

int rtw89_chanctx_ops_add(struct rtw89_dev *rtwdev,
			  struct ieee80211_chanctx_conf *ctx);
void rtw89_chanctx_ops_remove(struct rtw89_dev *rtwdev,
			      struct ieee80211_chanctx_conf *ctx);
void rtw89_chanctx_ops_change(struct rtw89_dev *rtwdev,
			      struct ieee80211_chanctx_conf *ctx,
			      u32 changed);
int rtw89_chanctx_ops_assign_vif(struct rtw89_dev *rtwdev,
				 struct rtw89_vif_link *rtwvif_link,
				 struct ieee80211_chanctx_conf *ctx);
void rtw89_chanctx_ops_unassign_vif(struct rtw89_dev *rtwdev,
				    struct rtw89_vif_link *rtwvif_link,
				    struct ieee80211_chanctx_conf *ctx);

#endif
