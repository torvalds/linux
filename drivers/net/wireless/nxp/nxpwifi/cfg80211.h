/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * nxpwifi: cfg80211 support
 *
 * Copyright 2011-2024 NXP
 */

#ifndef __NXPWIFI_CFG80211__
#define __NXPWIFI_CFG80211__

#include "main.h"

int nxpwifi_register_cfg80211(struct nxpwifi_adapter *adapter);

int nxpwifi_cfg80211_change_beacon(struct wiphy *wiphy,
				   struct net_device *dev,
				   struct cfg80211_ap_update *params);
#endif
