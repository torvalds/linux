/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Copyright(c) 2020-2022  Realtek Corporation
 */

#ifndef __RTW89_CHAN_H__
#define __RTW89_CHAN_H__

#include "core.h"

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
			      enum rtw89_sub_entity_idx idx,
			      const struct rtw89_chan *new);
void rtw89_config_entity_chandef(struct rtw89_dev *rtwdev,
				 enum rtw89_sub_entity_idx idx,
				 const struct cfg80211_chan_def *chandef);
void rtw89_config_roc_chandef(struct rtw89_dev *rtwdev,
			      enum rtw89_sub_entity_idx idx,
			      const struct cfg80211_chan_def *chandef);
void rtw89_entity_init(struct rtw89_dev *rtwdev);
enum rtw89_entity_mode rtw89_entity_recalc(struct rtw89_dev *rtwdev);
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
