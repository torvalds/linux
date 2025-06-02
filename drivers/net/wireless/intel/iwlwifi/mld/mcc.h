/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 */
#ifndef __iwl_mld_mcc_h__
#define __iwl_mld_mcc_h__

int iwl_mld_init_mcc(struct iwl_mld *mld);
void iwl_mld_handle_update_mcc(struct iwl_mld *mld, struct iwl_rx_packet *pkt);
void iwl_mld_update_changed_regdomain(struct iwl_mld *mld);
struct ieee80211_regdomain *
iwl_mld_get_regdomain(struct iwl_mld *mld,
		      const char *alpha2,
		      enum iwl_mcc_source src_id,
		      bool *changed);

#endif /* __iwl_mld_mcc_h__ */
