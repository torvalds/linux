/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_DISPLAY_H__
#define __NOUVEAU_DISPLAY_H__

#include "nouveau_drv.h"

#include <nvif/disp.h>

#include <drm/drm_framebuffer.h>

int
nouveau_framebuffer_new(struct drm_device *dev,
			const struct drm_mode_fb_cmd2 *mode_cmd,
			struct drm_gem_object *gem,
			struct drm_framebuffer **pfb);

struct nouveau_display {
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
	/* not really hue and saturation: */
	struct drm_property *vibrant_hue_property;
	struct drm_property *color_vibrance_property;

	struct drm_atomic_state *suspend;

	const u64 *format_modifiers;
};

static inline struct nouveau_display *
nouveau_display(struct drm_device *dev)
{
	return nouveau_drm(dev)->display;
}

int  nouveau_display_create(struct drm_device *dev);
void nouveau_display_destroy(struct drm_device *dev);
int  nouveau_display_init(struct drm_device *dev, bool resume, bool runtime);
void nouveau_display_hpd_resume(struct nouveau_drm *);
void nouveau_display_fini(struct drm_device *dev, bool suspend, bool runtime);
int  nouveau_display_suspend(struct drm_device *dev, bool runtime);
void nouveau_display_resume(struct drm_device *dev, bool runtime);
int  nouveau_display_vblank_enable(struct drm_crtc *crtc);
void nouveau_display_vblank_disable(struct drm_crtc *crtc);
bool nouveau_display_scanoutpos(struct drm_crtc *crtc,
				bool in_vblank_irq, int *vpos, int *hpos,
				ktime_t *stime, ktime_t *etime,
				const struct drm_display_mode *mode);

int  nouveau_display_dumb_create(struct drm_file *, struct drm_device *,
				 struct drm_mode_create_dumb *args);

void nouveau_hdmi_mode_set(struct drm_encoder *, struct drm_display_mode *);

void
nouveau_framebuffer_get_layout(struct drm_framebuffer *fb, uint32_t *tile_mode,
			       uint8_t *kind);

struct drm_framebuffer *
nouveau_user_framebuffer_create(struct drm_device *, struct drm_file *,
				const struct drm_mode_fb_cmd2 *);
#endif
