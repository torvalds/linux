/*
 * Copyright (C) 2008 Maarten Maathuis.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __NOUVEAU_FBCON_H__
#define __NOUVEAU_FBCON_H__

#include "drm_fb_helper.h"

struct nouveau_fbcon_par {
	struct drm_fb_helper helper;
	struct drm_device *dev;
	struct nouveau_framebuffer *nouveau_fb;
};

int nouveau_fbcon_probe(struct drm_device *dev);
int nouveau_fbcon_remove(struct drm_device *dev, struct drm_framebuffer *fb);
void nouveau_fbcon_restore(void);
void nouveau_fbcon_zfill(struct drm_device *dev);

int nv04_fbcon_accel_init(struct fb_info *info);
int nv50_fbcon_accel_init(struct fb_info *info);

void nouveau_fbcon_gpu_lockup(struct fb_info *info);
#endif /* __NV50_FBCON_H__ */

