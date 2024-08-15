/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2015-2016 Quantenna Communications. All rights reserved. */

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
