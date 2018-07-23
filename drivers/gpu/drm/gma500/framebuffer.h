/*
 * Copyright (c) 2008-2011, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Authors:
 *      Eric Anholt <eric@anholt.net>
 *
 */

#ifndef _FRAMEBUFFER_H_
#define _FRAMEBUFFER_H_

#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>

#include "psb_drv.h"

struct psb_framebuffer {
	struct drm_framebuffer base;
	struct address_space *addr_space;
	struct fb_info *fbdev;
};

struct psb_fbdev {
	struct drm_fb_helper psb_fb_helper;
	struct psb_framebuffer pfb;
};

#define to_psb_fb(x) container_of(x, struct psb_framebuffer, base)

extern int gma_connector_clones(struct drm_device *dev, int type_mask);

#endif

