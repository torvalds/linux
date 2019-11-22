/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2008-2011, Intel Corporation
 *
 * Authors:
 *      Eric Anholt <eric@anholt.net>
 */

#ifndef _FRAMEBUFFER_H_
#define _FRAMEBUFFER_H_

#include <drm/drm_fb_helper.h>

#include "psb_drv.h"

struct psb_fbdev {
	struct drm_fb_helper psb_fb_helper; /* must be first */
	struct drm_framebuffer fb;
};

extern int gma_connector_clones(struct drm_device *dev, int type_mask);

#endif

