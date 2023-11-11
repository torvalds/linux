/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DSI_DCS_BACKLIGHT_H__
#define __INTEL_DSI_DCS_BACKLIGHT_H__

struct intel_connector;

int intel_dsi_dcs_init_backlight_funcs(struct intel_connector *intel_connector);

#endif /* __INTEL_DSI_DCS_BACKLIGHT_H__ */
