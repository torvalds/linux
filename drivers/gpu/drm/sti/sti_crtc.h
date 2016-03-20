/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _STI_CRTC_H_
#define _STI_CRTC_H_

#include <drm/drmP.h>

struct sti_mixer;

int sti_crtc_init(struct drm_device *drm_dev, struct sti_mixer *mixer,
		  struct drm_plane *primary, struct drm_plane *cursor);
int sti_crtc_enable_vblank(struct drm_device *dev, unsigned int pipe);
void sti_crtc_disable_vblank(struct drm_device *dev, unsigned int pipe);
int sti_crtc_vblank_cb(struct notifier_block *nb,
		       unsigned long event, void *data);
bool sti_crtc_is_main(struct drm_crtc *drm_crtc);

#endif
