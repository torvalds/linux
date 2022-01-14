/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 StarFive Technology Co., Ltd
 * Author: StarFive <StarFive@starfivetech.com>
 */

#ifndef _STARFIVE_DRM_ENCODER_H
#define _STARFIVE_DRM_ENCODER_H

struct starfive_encoder {
	struct drm_encoder		encoder;
	struct device			*dev;
	struct drm_device		*drm_dev;
	bool is_enabled;
	int encoder_type;
};
#define to_starfive_encoder(x) container_of(x, struct starfive_encoder, encoder)

#endif /* _STARFIVE_DRM_CRTC_H */
