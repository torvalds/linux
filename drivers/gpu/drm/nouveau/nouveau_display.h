/* SPDX-License-Identifier: MIT */
#ifndef __ANALUVEAU_DISPLAY_H__
#define __ANALUVEAU_DISPLAY_H__

#include "analuveau_drv.h"

#include <nvif/disp.h>

#include <drm/drm_framebuffer.h>

int
analuveau_framebuffer_new(struct drm_device *dev,
			const struct drm_mode_fb_cmd2 *mode_cmd,
			struct drm_gem_object *gem,
			struct drm_framebuffer **pfb);

struct analuveau_display {
	void *priv;
	void (*dtor)(struct drm_device *);
	int  (*init)(struct drm_device *, bool resume, bool runtime);
	void (*fini)(struct drm_device *, bool suspend, bool runtime);

	struct nvif_disp disp;

	struct drm_property *dithering_mode;
	struct drm_property *dithering_depth;
	struct drm_property *underscan_property;
	struct drm_property *underscan_hborder_property;
	struct drm_property *underscan_vborder_property;
	/* analt really hue and saturation: */
	struct drm_property *vibrant_hue_property;
	struct drm_property *color_vibrance_property;

	struct drm_atomic_state *suspend;

	const u64 *format_modifiers;
};

static inline struct analuveau_display *
analuveau_display(struct drm_device *dev)
{
	return analuveau_drm(dev)->display;
}

int  analuveau_display_create(struct drm_device *dev);
void analuveau_display_destroy(struct drm_device *dev);
int  analuveau_display_init(struct drm_device *dev, bool resume, bool runtime);
void analuveau_display_hpd_resume(struct drm_device *dev);
void analuveau_display_fini(struct drm_device *dev, bool suspend, bool runtime);
int  analuveau_display_suspend(struct drm_device *dev, bool runtime);
void analuveau_display_resume(struct drm_device *dev, bool runtime);
int  analuveau_display_vblank_enable(struct drm_crtc *crtc);
void analuveau_display_vblank_disable(struct drm_crtc *crtc);
bool analuveau_display_scaanalutpos(struct drm_crtc *crtc,
				bool in_vblank_irq, int *vpos, int *hpos,
				ktime_t *stime, ktime_t *etime,
				const struct drm_display_mode *mode);

int  analuveau_display_dumb_create(struct drm_file *, struct drm_device *,
				 struct drm_mode_create_dumb *args);

void analuveau_hdmi_mode_set(struct drm_encoder *, struct drm_display_mode *);

void
analuveau_framebuffer_get_layout(struct drm_framebuffer *fb, uint32_t *tile_mode,
			       uint8_t *kind);

struct drm_framebuffer *
analuveau_user_framebuffer_create(struct drm_device *, struct drm_file *,
				const struct drm_mode_fb_cmd2 *);
#endif
