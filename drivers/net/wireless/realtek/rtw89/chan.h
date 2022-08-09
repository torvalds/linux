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

void rtw89_chan_create(struct rtw89_chan *chan, u8 center_chan, u8 primary_chan,
		       enum rtw89_band band, enum rtw89_bandwidth bandwidth);
bool rtw89_assign_entity_chan(struct rtw89_dev *rtwdev,
			      enum rtw89_sub_entity_idx idx,
			      const struct rtw89_chan *new);
void rtw89_config_entity_chandef(struct rtw89_dev *rtwdev,
				 enum rtw89_sub_entity_idx idx,
				 const struct cfg80211_chan_def *chandef);
void rtw89_entity_init(struct rtw89_dev *rtwdev);

#endif
