/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_CMN_DEFS_H
#define ATH12K_CMN_DEFS_H

#include <net/mac80211.h>

#define MAX_RADIOS 2
#define ATH12K_MAX_DEVICES 3
#define ATH12K_GROUP_MAX_RADIO (ATH12K_MAX_DEVICES * MAX_RADIOS)

#define ATH12K_SCAN_MAX_LINKS	ATH12K_GROUP_MAX_RADIO
/* Define 1 scan link for each radio for parallel scan purposes */
#define ATH12K_NUM_MAX_LINKS (IEEE80211_MLD_MAX_NUM_LINKS + ATH12K_SCAN_MAX_LINKS)

#define MAX_MU_GROUP_ID 64
#endif
