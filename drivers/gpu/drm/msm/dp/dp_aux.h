/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_AUX_H_
#define _DP_AUX_H_

#include "dp_catalog.h"
#include <drm/display/drm_dp_helper.h>

int dp_aux_register(struct drm_dp_aux *dp_aux);
void dp_aux_unregister(struct drm_dp_aux *dp_aux);
irqreturn_t dp_aux_isr(struct drm_dp_aux *dp_aux);
void dp_aux_enable_xfers(struct drm_dp_aux *dp_aux, bool enabled);
void dp_aux_init(struct drm_dp_aux *dp_aux);
void dp_aux_deinit(struct drm_dp_aux *dp_aux);
void dp_aux_reconfig(struct drm_dp_aux *dp_aux);

struct phy;
struct drm_dp_aux *dp_aux_get(struct device *dev, struct dp_catalog *catalog,
			      struct phy *phy,
			      bool is_edp);
void dp_aux_put(struct drm_dp_aux *aux);

#endif /*__DP_AUX_H_*/
