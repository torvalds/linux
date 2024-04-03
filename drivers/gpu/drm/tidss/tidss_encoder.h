/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#ifndef __TIDSS_ENCODER_H__
#define __TIDSS_ENCODER_H__

#include <drm/drm_encoder.h>

struct tidss_device;

int tidss_encoder_create(struct tidss_device *tidss,
			 struct drm_bridge *next_bridge,
			 u32 encoder_type, u32 possible_crtcs);

#endif
