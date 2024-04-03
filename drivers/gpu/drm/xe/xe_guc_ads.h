/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GUC_ADS_H_
#define _XE_GUC_ADS_H_

#include "xe_guc_ads_types.h"

int xe_guc_ads_init(struct xe_guc_ads *ads);
int xe_guc_ads_init_post_hwconfig(struct xe_guc_ads *ads);
void xe_guc_ads_populate(struct xe_guc_ads *ads);
void xe_guc_ads_populate_minimal(struct xe_guc_ads *ads);
void xe_guc_ads_populate_post_load(struct xe_guc_ads *ads);

#endif
