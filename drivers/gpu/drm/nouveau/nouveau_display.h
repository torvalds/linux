/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NOUVEAU_DISPLAY_H__
#define __NOUVEAU_DISPLAY_H__
#include "nouveau_drv.h"

struct nouveau_framebuffer {
	struct drm_framebuffer base;
	struct nouveau_bo *nvbo;
	struct nouveau_vma *vma;
	u32 r_handle;
	u32 r_format;
	u32 r_pitch;
	struct nvif_object h_base[4];
	struct nvif_object h_core;
};

static inline struct nouveau_framebuffer *
nouveau_framebuffer(struct drm_framebuffer *fb)
{
	return container_of(fb, struct nouveau_framebuffer, base);
}

int nouveau_framebuffer_new(struct drm_device *,
			    const struct drm_mode_fb_cmd2 *,
			    struct nouveau_bo *, struct nouveau_framebuffer **);

struct nouveau_page_flip_state {
	struct list_head head;
	struct drm_pending_vblank_event *event;
	struct drm_crtc *crtc;
	int bpp, pitch;
	u64 offset;
};

struct nouveau_display {
	void *priv;
	void (*dtor)(struct drm_device *);
	int  (*init)(struct drm_device *);
	void (*fini)(struct drm_device *);

	struct nvif_object disp;

	struct drm_property *dithering_mode;
	struct drm_property *dithering_depth;
	struct drm_property *underscan_property;
	struct drm_property *underscan_hborder_property;
	struct drm_property *underscan_vborder_property;
	/* not really hue and saturation: */
	struct drm_property *vibrant_hue_property;
	struct drm_property *color_vibrance_property;

	struct drm_atomic_state *suspend;
};

static inline struct nouveau_display *
nouveau_display(struct drm_device *dev)
{
	return nouveau_drm(dev)->display;
}

int  nouveau_display_create(struct drm_device *dev);
void nouveau_display_destroy(struct drm_device *dev);
int  nouveau_display_init(struct drm_device *dev);
void nouveau_display_fini(struct drm_device *dev, bool suspend);
int  nouveau_display_suspend(struct drm_device *dev, bool runtime);
void nouveau_display_resume(struct drm_device *dev, bool runtime);
int  nouveau_display_vblank_enable(struct drm_device *, unsigned int);
void nouveau_display_vblank_disable(struct drm_device *, unsigned int);
bool  nouveau_display_scanoutpos(struct drm_device *, unsigned int,
				 bool, int *, int *, ktime_t *,
				 ktime_t *, const struct drm_display_mode *);

int  nouveau_crtc_page_flip(struct drm_crtc *crtc, struct drm_framebuffer *fb,
			    struct drm_pending_vblank_event *event,
			    uint32_t page_flip_flags,
			    struct drm_modeset_acquire_ctx *ctx);
int  nouveau_finish_page_flip(struct nouveau_channel *,
			      struct nouveau_page_flip_state *);

int  nouveau_display_dumb_create(struct drm_file *, struct drm_device *,
				 struct drm_mode_create_dumb *args);
int  nouveau_display_dumb_map_offset(struct drm_file *, struct drm_device *,
				     u32 handle, u64 *offset);

void nouveau_hdmi_mode_set(struct drm_encoder *, struct drm_display_mode *);

#ifdef CONFIG_DRM_NOUVEAU_BACKLIGHT
extern int nouveau_backlight_init(struct drm_device *);
extern void nouveau_backlight_exit(struct drm_device *);
extern void nouveau_backlight_ctor(void);
extern void nouveau_backlight_dtor(void);
#else
static inline int
nouveau_backlight_init(struct drm_device *dev)
{
	return 0;
}

static inline void
nouveau_backlight_exit(struct drm_device *dev) {
}

static inline void
nouveau_backlight_ctor(void) {
}

static inline void
nouveau_backlight_dtor(void) {
}
#endif

struct drm_framebuffer *
nouveau_user_framebuffer_create(struct drm_device *, struct drm_file *,
				const struct drm_mode_fb_cmd2 *);
#endif
