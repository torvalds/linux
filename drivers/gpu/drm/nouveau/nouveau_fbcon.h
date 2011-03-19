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

#include "nouveau_fb.h"
struct nouveau_fbdev {
	struct drm_fb_helper helper;
	struct nouveau_framebuffer nouveau_fb;
	struct list_head fbdev_list;
	struct drm_device *dev;
	unsigned int saved_flags;
};

void nouveau_fbcon_restore(void);

int nv04_fbcon_copyarea(struct fb_info *info, const struct fb_copyarea *region);
int nv04_fbcon_fillrect(struct fb_info *info, const struct fb_fillrect *rect);
int nv04_fbcon_imageblit(struct fb_info *info, const struct fb_image *image);
int nv04_fbcon_accel_init(struct fb_info *info);

int nv50_fbcon_fillrect(struct fb_info *info, const struct fb_fillrect *rect);
int nv50_fbcon_copyarea(struct fb_info *info, const struct fb_copyarea *region);
int nv50_fbcon_imageblit(struct fb_info *info, const struct fb_image *image);
int nv50_fbcon_accel_init(struct fb_info *info);

int nvc0_fbcon_fillrect(struct fb_info *info, const struct fb_fillrect *rect);
int nvc0_fbcon_copyarea(struct fb_info *info, const struct fb_copyarea *region);
int nvc0_fbcon_imageblit(struct fb_info *info, const struct fb_image *image);
int nvc0_fbcon_accel_init(struct fb_info *info);

void nouveau_fbcon_gpu_lockup(struct fb_info *info);

int nouveau_fbcon_init(struct drm_device *dev);
void nouveau_fbcon_fini(struct drm_device *dev);
void nouveau_fbcon_set_suspend(struct drm_device *dev, int state);
void nouveau_fbcon_zfill_all(struct drm_device *dev);
void nouveau_fbcon_save_disable_accel(struct drm_device *dev);
void nouveau_fbcon_restore_accel(struct drm_device *dev);

void nouveau_fbcon_output_poll_changed(struct drm_device *dev);
#endif /* __NV50_FBCON_H__ */

