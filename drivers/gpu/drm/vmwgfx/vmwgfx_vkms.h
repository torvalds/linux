/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright (c) 2024 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef VMWGFX_VKMS_H_
#define VMWGFX_VKMS_H_

#include <linux/hrtimer_types.h>
#include <linux/types.h>

struct drm_atomic_state;
struct drm_crtc;
struct vmw_private;
struct vmw_surface;

void vmw_vkms_init(struct vmw_private *vmw);
void vmw_vkms_cleanup(struct vmw_private *vmw);

void vmw_vkms_modeset_lock(struct drm_crtc *crtc);
bool vmw_vkms_modeset_lock_relaxed(struct drm_crtc *crtc);
bool vmw_vkms_vblank_trylock(struct drm_crtc *crtc);
void vmw_vkms_unlock(struct drm_crtc *crtc);

bool vmw_vkms_get_vblank_timestamp(struct drm_crtc *crtc,
				   int *max_error,
				   ktime_t *vblank_time,
				   bool in_vblank_irq);
int vmw_vkms_enable_vblank(struct drm_crtc *crtc);
void vmw_vkms_disable_vblank(struct drm_crtc *crtc);

void vmw_vkms_crtc_init(struct drm_crtc *crtc);
void vmw_vkms_crtc_cleanup(struct drm_crtc *crtc);
void  vmw_vkms_crtc_atomic_begin(struct drm_crtc *crtc,
				 struct drm_atomic_state *state);
void vmw_vkms_crtc_atomic_flush(struct drm_crtc *crtc, struct drm_atomic_state *state);
void vmw_vkms_crtc_atomic_enable(struct drm_crtc *crtc,
				 struct drm_atomic_state *state);
void vmw_vkms_crtc_atomic_disable(struct drm_crtc *crtc,
				  struct drm_atomic_state *state);

const char *const *vmw_vkms_get_crc_sources(struct drm_crtc *crtc,
					    size_t *count);
int vmw_vkms_verify_crc_source(struct drm_crtc *crtc,
			       const char *src_name,
			       size_t *values_cnt);
int vmw_vkms_set_crc_source(struct drm_crtc *crtc,
			    const char *src_name);
void vmw_vkms_set_crc_surface(struct drm_crtc *crtc,
			      struct vmw_surface *surf);

#endif
