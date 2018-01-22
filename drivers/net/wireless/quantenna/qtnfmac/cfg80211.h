/*
 * Copyright (c) 2015-2016 Quantenna Communications, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _QTN_FMAC_CFG80211_H_
#define _QTN_FMAC_CFG80211_H_

#include <net/cfg80211.h>

#include "core.h"

int qtnf_wiphy_register(struct qtnf_hw_info *hw_info, struct qtnf_wmac *mac);
int qtnf_del_virtual_intf(struct wiphy *wiphy, struct wireless_dev *wdev);
void qtnf_cfg80211_vif_reset(struct qtnf_vif *vif);
void qtnf_band_init_rates(struct ieee80211_supported_band *band);
void qtnf_band_setup_htvht_caps(struct qtnf_mac_info *macinfo,
				struct ieee80211_supported_band *band);

#endif /* _QTN_FMAC_CFG80211_H_ */
