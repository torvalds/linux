/*
 * Copyright (c) 2008, Intel Corporation
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

#ifndef _PSB_FB_H_
#define _PSB_FB_H_

#include <linux/version.h>
#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>

#include "psb_drv.h"

/*IMG Headers*/
/*#include "servicesint.h"*/

struct psb_framebuffer {
	struct drm_framebuffer base;
	struct address_space *addr_space;
	struct ttm_buffer_object *bo;
	struct fb_info * fbdev;
	/* struct ttm_bo_kmap_obj kmap; */
	void *pvrBO;	/* FIXME: sort this out */
	void * hKernelMemInfo;
	uint32_t size;
	uint32_t offset;
};

struct psb_fbdev {
	struct drm_fb_helper psb_fb_helper;
	struct psb_framebuffer * pfb;
};


#define to_psb_fb(x) container_of(x, struct psb_framebuffer, base)


extern int psb_intel_connector_clones(struct drm_device *dev, int type_mask);


#endif

