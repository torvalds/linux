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

#define NUM_OF_RTW89_MCC_ROLES 2

enum rtw89_chanctx_pause_reasons {
	RTW89_CHANCTX_PAUSE_REASON_HW_SCAN,
	RTW89_CHANCTX_PAUSE_REASON_ROC,
};

struct rtw89_entity_weight {
	unsigned int active_chanctxs;
	unsigned int active_roles;
};

static inline bool rtw89_get_entity_state(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	return READ_ONCE(hal->entity_active);
}

static inline void rtw89_set_entity_state(struct rtw89_dev *rtwdev, bool active)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	WRITE_ONCE(hal->entity_active, active);
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
			      enum rtw89_chanctx_idx idx,
			      const struct cfg80211_chan_def *chandef);
void rtw89_entity_init(struct rtw89_dev *rtwdev);
enum rtw89_entity_mode rtw89_entity_recalc(struct rtw89_dev *rtwdev);
void rtw89_chanctx_work(struct work_struct *work);
void rtw89_queue_chanctx_work(struct rtw89_dev *rtwdev);
void rtw89_queue_chanctx_change(struct rtw89_dev *rtwdev,
				enum rtw89_chanctx_changes change);
void rtw89_chanctx_track(struct rtw89_dev *rtwdev);
void rtw89_chanctx_pause(struct rtw89_dev *rtwdev,
			 enum rtw89_chanctx_pause_reasons rsn);
void rtw89_chanctx_proceed(struct rtw89_dev *rtwdev);
int rtw89_chanctx_ops_add(struct rtw89_dev *rtwdev,
			  struct ieee80211_chanctx_conf *ctx);
void rtw89_chanctx_ops_remove(struct rtw89_dev *rtwdev,
			      struct ieee80211_chanctx_conf *ctx);
void rtw89_chanctx_ops_change(struct rtw89_dev *rtwdev,
			      struct ieee80211_chanctx_conf *ctx,
			      u32 changed);
int rtw89_chanctx_ops_assign_vif(struct rtw89_dev *rtwdev,
				 struct rtw89_vif *rtwvif,
				 struct ieee80211_chanctx_conf *ctx);
void rtw89_chanctx_ops_unassign_vif(struct rtw89_dev *rtwdev,
				    struct rtw89_vif *rtwvif,
				    struct ieee80211_chanctx_conf *ctx);

#endif
