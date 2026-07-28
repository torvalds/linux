/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 * Copyright (C) 2026 Intel Corporation
 */
#ifndef __iwl_mld_mac80211_h__
#define __iwl_mld_mac80211_h__

#include "mld.h"

int iwl_mld_register_hw(struct iwl_mld *mld);
void iwl_mld_recalc_multicast_filter(struct iwl_mld *mld);
void iwl_mld_restart_cleanup(struct iwl_mld *mld);

#endif /* __iwl_mld_mac80211_h__ */
