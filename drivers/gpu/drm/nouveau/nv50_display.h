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

#ifndef __NV50_DISPLAY_H__
#define __NV50_DISPLAY_H__

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_reg.h"
#include "nouveau_crtc.h"
#include "nv50_evo.h"

void nv50_display_irq_handler(struct drm_device *dev);
void nv50_display_irq_handler_bh(struct work_struct *work);
void nv50_display_irq_hotplug_bh(struct work_struct *work);
int nv50_display_early_init(struct drm_device *dev);
void nv50_display_late_takedown(struct drm_device *dev);
int nv50_display_create(struct drm_device *dev);
int nv50_display_init(struct drm_device *dev);
void nv50_display_destroy(struct drm_device *dev);
int nv50_crtc_blank(struct nouveau_crtc *, bool blank);
int nv50_crtc_set_clock(struct drm_device *, int head, int pclk);

#endif /* __NV50_DISPLAY_H__ */
