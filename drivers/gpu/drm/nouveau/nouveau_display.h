/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_DISPLAY_H__
#define __NOUVEAU_DISPLAY_H__

#include "yesuveau_drv.h"

#include <nvif/disp.h>

#include <drm/drm_framebuffer.h>

struct yesuveau_framebuffer {
	struct drm_framebuffer base;
	struct yesuveau_bo *nvbo;
	struct yesuveau_vma *vma;
	u32 r_handle;
	u32 r_format;
	u32 r_pitch;
	struct nvif_object h_base[4];
	struct nvif_object h_core;
};

static inline struct yesuveau_framebuffer *
yesuveau_framebuffer(struct drm_framebuffer *fb)
{
	return container_of(fb, struct yesuveau_framebuffer, base);
}

int yesuveau_framebuffer_new(struct drm_device *,
			    const struct drm_mode_fb_cmd2 *,
			    struct yesuveau_bo *, struct yesuveau_framebuffer **);

struct yesuveau_display {
	void *priv;
	void (*dtor)(struct drm_device *);
	int  (*init)(struct drm_device *, bool resume, bool runtime);
	void (*fini)(struct drm_device *, bool suspend);

	struct nvif_disp disp;

	struct drm_property *dithering_mode;
	struct drm_property *dithering_depth;
	struct drm_property *underscan_property;
	struct drm_property *underscan_hborder_property;
	struct drm_property *underscan_vborder_property;
	/* yest really hue and saturation: */
	struct drm_property *vibrant_hue_property;
	struct drm_property *color_vibrance_property;

	struct drm_atomic_state *suspend;
};

static inline struct yesuveau_display *
yesuveau_display(struct drm_device *dev)
{
	return yesuveau_drm(dev)->display;
}

int  yesuveau_display_create(struct drm_device *dev);
void yesuveau_display_destroy(struct drm_device *dev);
int  yesuveau_display_init(struct drm_device *dev, bool resume, bool runtime);
void yesuveau_display_fini(struct drm_device *dev, bool suspend, bool runtime);
int  yesuveau_display_suspend(struct drm_device *dev, bool runtime);
void yesuveau_display_resume(struct drm_device *dev, bool runtime);
int  yesuveau_display_vblank_enable(struct drm_device *, unsigned int);
void yesuveau_display_vblank_disable(struct drm_device *, unsigned int);
bool  yesuveau_display_scayesutpos(struct drm_device *, unsigned int,
				 bool, int *, int *, ktime_t *,
				 ktime_t *, const struct drm_display_mode *);

int  yesuveau_display_dumb_create(struct drm_file *, struct drm_device *,
				 struct drm_mode_create_dumb *args);
int  yesuveau_display_dumb_map_offset(struct drm_file *, struct drm_device *,
				     u32 handle, u64 *offset);

void yesuveau_hdmi_mode_set(struct drm_encoder *, struct drm_display_mode *);

struct drm_framebuffer *
yesuveau_user_framebuffer_create(struct drm_device *, struct drm_file *,
				const struct drm_mode_fb_cmd2 *);
#endif
