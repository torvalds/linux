/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Copyright(c) 2019-2020  Realtek Corporation
 */
#ifndef __RTW89_UTIL_H__
#define __RTW89_UTIL_H__

#include "core.h"

#define rtw89_iterate_vifs_bh(rtwdev, iterator, data)                          \
	ieee80211_iterate_active_interfaces_atomic((rtwdev)->hw,               \
			IEEE80211_IFACE_ITER_NORMAL, iterator, data)

/* call this function with rtwdev->mutex is held */
#define rtw89_for_each_rtwvif(rtwdev, rtwvif)				       \
	list_for_each_entry(rtwvif, &(rtwdev)->rtwvifs_list, list)

#endif
