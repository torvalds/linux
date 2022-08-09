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

#endif
