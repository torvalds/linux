/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl1271
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 */

#ifndef __TESTMODE_H__
#define __TESTMODE_H__

#include <net/mac80211.h>

int wl1271_tm_cmd(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  void *data, int len);

#endif /* __WL1271_TESTMODE_H__ */
