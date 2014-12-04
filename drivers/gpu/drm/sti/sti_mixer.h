/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _STI_MIXER_H_
#define _STI_MIXER_H_

#include <drm/drmP.h>

#include "sti_layer.h"

#define to_sti_mixer(x) container_of(x, struct sti_mixer, drm_crtc)

/**
 * STI Mixer subdevice structure
 *
 * @dev: driver device
 * @regs: mixer registers
 * @id: id of the mixer
 * @drm_crtc: crtc object link to the mixer
 * @pending_event: set if a flip event is pending on crtc
 */
struct sti_mixer {
	struct device *dev;
	void __iomem *regs;
	int id;
	struct drm_crtc	drm_crtc;
	struct drm_pending_vblank_event *pending_event;
};

const char *sti_mixer_to_str(struct sti_mixer *mixer);

struct sti_mixer *sti_mixer_create(struct device *dev, int id,
		void __iomem *baseaddr);

int sti_mixer_set_layer_status(struct sti_mixer *mixer,
		struct sti_layer *layer, bool status);
void sti_mixer_clear_all_layers(struct sti_mixer *mixer);
int sti_mixer_set_layer_depth(struct sti_mixer *mixer, struct sti_layer *layer);
int sti_mixer_active_video_area(struct sti_mixer *mixer,
		struct drm_display_mode *mode);

void sti_mixer_set_background_status(struct sti_mixer *mixer, bool enable);

/* depth in Cross-bar control = z order */
#define GAM_MIXER_NB_DEPTH_LEVEL 7

#define STI_MIXER_MAIN 0
#define STI_MIXER_AUX  1

#endif
