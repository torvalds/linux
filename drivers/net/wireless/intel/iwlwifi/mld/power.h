/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 */
#ifndef __iwl_mld_power_h__
#define __iwl_mld_power_h__

#include <net/mac80211.h>

#include "mld.h"

int iwl_mld_update_device_power(struct iwl_mld *mld, bool d3);

int iwl_mld_enable_beacon_filter(struct iwl_mld *mld,
				 const struct ieee80211_bss_conf *link_conf,
				 bool d3);

int iwl_mld_disable_beacon_filter(struct iwl_mld *mld,
				  struct ieee80211_vif *vif);

int iwl_mld_update_mac_power(struct iwl_mld *mld, struct ieee80211_vif *vif,
			     bool d3);

void
iwl_mld_send_ap_tx_power_constraint_cmd(struct iwl_mld *mld,
					struct ieee80211_vif *vif,
					struct ieee80211_bss_conf *link);

int iwl_mld_set_tx_power(struct iwl_mld *mld,
			 struct ieee80211_bss_conf *link_conf,
			 s16 tx_power);

#endif /* __iwl_mld_power_h__ */
