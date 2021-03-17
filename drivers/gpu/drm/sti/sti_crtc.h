/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com> for STMicroelectronics.
 */

#ifndef _STI_CRTC_H_
#define _STI_CRTC_H_

struct drm_crtc;
struct drm_device;
struct drm_plane;
struct notifier_block;
struct sti_mixer;

int sti_crtc_init(struct drm_device *drm_dev, struct sti_mixer *mixer,
		  struct drm_plane *primary, struct drm_plane *cursor);
int sti_crtc_vblank_cb(struct notifier_block *nb,
		       unsigned long event, void *data);
bool sti_crtc_is_main(struct drm_crtc *drm_crtc);

#endif
