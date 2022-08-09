// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2020-2022  Realtek Corporation
 */

#include "chan.h"

bool rtw89_assign_entity_chan(struct rtw89_dev *rtwdev,
			      enum rtw89_sub_entity_idx idx,
			      const struct rtw89_chan *new)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_chan *chan = &hal->chan[idx];
	struct rtw89_chan_rcd *rcd = &hal->chan_rcd[idx];
	bool band_changed;

	rcd->prev_primary_channel = chan->primary_channel;
	rcd->prev_band_type = chan->band_type;
	band_changed = new->band_type != chan->band_type;

	*chan = *new;
	return band_changed;
}
