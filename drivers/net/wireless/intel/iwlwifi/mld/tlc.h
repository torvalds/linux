/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 */
#ifndef __iwl_mld_tlc_h__
#define __iwl_mld_tlc_h__

#include "mld.h"

void iwl_mld_config_tlc_link(struct iwl_mld *mld,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *link_conf,
			     struct ieee80211_link_sta *link_sta);

void iwl_mld_config_tlc(struct iwl_mld *mld, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta);

void iwl_mld_handle_tlc_notif(struct iwl_mld *mld,
			      struct iwl_rx_packet *pkt);

int iwl_mld_send_tlc_dhc(struct iwl_mld *mld, u8 sta_id, u32 type, u32 data);

#endif /* __iwl_mld_tlc_h__ */
