/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef DRM_SYSFB_HELPER_H
#define DRM_SYSFB_HELPER_H

#include <linux/container_of.h>
#include <linux/iosys-map.h>

#include <video/pixel_format.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_modes.h>

struct drm_format_info;
struct drm_scanout_buffer;
struct screen_info;

/*
 * Input parsing
 */

struct drm_sysfb_format {
	struct pixel_format pixel;
	u32 fourcc;
};

int drm_sysfb_get_validated_int(struct drm_device *dev, const char *name,
				u64 value, u32 max);
int drm_sysfb_get_validated_int0(struct drm_device *dev, const char *name,
				 u64 value, u32 max);

#if defined(CONFIG_SCREEN_INFO)
int drm_sysfb_get_width_si(struct drm_device *dev, const struct screen_info *si);
int drm_sysfb_get_height_si(struct drm_device *dev, const struct screen_info *si);
struct resource *drm_sysfb_get_memory_si(struct drm_device *dev,
					 const struct screen_info *si,
					 struct resource *res);
int drm_sysfb_get_stride_si(struct drm_device *dev, const struct screen_info *si,
			    const struct drm_format_info *format,
			    unsigned int width, unsigned int height, u64 size);
u64 drm_sysfb_get_visible_size_si(struct drm_device *dev, const struct screen_info *si,
				  unsigned int height, unsigned int stride, u64 size);
const struct drm_format_info *drm_sysfb_get_format_si(struct drm_device *dev,
						      const struct drm_sysfb_format *formats,
						      size_t nformats,
						      const struct screen_info *si);
#endif

/*
 * Input parsing
 */

int drm_sysfb_get_validated_int(struct drm_device *dev, const char *name,
				u64 value, u32 max);
int drm_sysfb_get_validated_int0(struct drm_device *dev, const char *name,
				 u64 value, u32 max);

/*
 * Display modes
 */

struct drm_display_mode drm_sysfb_mode(unsigned int width,
				       unsigned int height,
				       unsigned int width_mm,
				       unsigned int height_mm);

/*
 * Device
 */

struct drm_sysfb_device {
	struct drm_device dev;

	const u8 *edid; /* can be NULL */

	/* hardware settings */
	struct drm_display_mode fb_mode;
	const struct drm_format_info *fb_format;
	unsigned int fb_pitch;
	unsigned int fb_gamma_lut_size;

	/* hardware-framebuffer kernel address */
	struct iosys_map fb_addr;
};

static inline struct drm_sysfb_device *to_drm_sysfb_device(struct drm_device *dev)
{
	return container_of(dev, struct drm_sysfb_device, dev);
}

/*
 * Plane
 */

size_t drm_sysfb_build_fourcc_list(struct drm_device *dev,
				   const u32 *native_fourccs, size_t native_nfourccs,
				   u32 *fourccs_out, size_t nfourccs_out);

int drm_sysfb_plane_helper_atomic_check(struct drm_plane *plane,
					struct drm_atomic_state *new_state);
void drm_sysfb_plane_helper_atomic_update(struct drm_plane *plane,
					  struct drm_atomic_state *state);
void drm_sysfb_plane_helper_atomic_disable(struct drm_plane *plane,
					   struct drm_atomic_state *state);
int drm_sysfb_plane_helper_get_scanout_buffer(struct drm_plane *plane,
					      struct drm_scanout_buffer *sb);

#define DRM_SYSFB_PLANE_NFORMATS(_num_native) \
	((_num_native) + 1)

#define DRM_SYSFB_PLANE_FORMAT_MODIFIERS \
	DRM_FORMAT_MOD_LINEAR, \
	DRM_FORMAT_MOD_INVALID

#define DRM_SYSFB_PLANE_HELPER_FUNCS \
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS, \
	.atomic_check = drm_sysfb_plane_helper_atomic_check, \
	.atomic_update = drm_sysfb_plane_helper_atomic_update, \
	.atomic_disable = drm_sysfb_plane_helper_atomic_disable, \
	.get_scanout_buffer = drm_sysfb_plane_helper_get_scanout_buffer

#define DRM_SYSFB_PLANE_FUNCS \
	.update_plane = drm_atomic_helper_update_plane, \
	.disable_plane = drm_atomic_helper_disable_plane, \
	DRM_GEM_SHADOW_PLANE_FUNCS

/*
 * CRTC
 */

struct drm_sysfb_crtc_state {
	struct drm_crtc_state base;

	/* CRTC input color format; required for color mgmt. */
	const struct drm_format_info *format;
};

static inline struct drm_sysfb_crtc_state *
to_drm_sysfb_crtc_state(struct drm_crtc_state *base)
{
	return container_of(base, struct drm_sysfb_crtc_state, base);
}

enum drm_mode_status drm_sysfb_crtc_helper_mode_valid(struct drm_crtc *crtc,
						      const struct drm_display_mode *mode);
int drm_sysfb_crtc_helper_atomic_check(struct drm_crtc *crtc, struct drm_atomic_state *new_state);

#define DRM_SYSFB_CRTC_HELPER_FUNCS \
	.mode_valid = drm_sysfb_crtc_helper_mode_valid, \
	.atomic_check = drm_sysfb_crtc_helper_atomic_check

void drm_sysfb_crtc_reset(struct drm_crtc *crtc);
struct drm_crtc_state *drm_sysfb_crtc_atomic_duplicate_state(struct drm_crtc *crtc);
void drm_sysfb_crtc_atomic_destroy_state(struct drm_crtc *crtc, struct drm_crtc_state *crtc_state);

#define DRM_SYSFB_CRTC_FUNCS \
	.reset = drm_sysfb_crtc_reset, \
	.set_config = drm_atomic_helper_set_config, \
	.page_flip = drm_atomic_helper_page_flip, \
	.atomic_duplicate_state = drm_sysfb_crtc_atomic_duplicate_state, \
	.atomic_destroy_state = drm_sysfb_crtc_atomic_destroy_state

/*
 * Connector
 */

int drm_sysfb_connector_helper_get_modes(struct drm_connector *connector);

#define DRM_SYSFB_CONNECTOR_HELPER_FUNCS \
	.get_modes = drm_sysfb_connector_helper_get_modes

#define DRM_SYSFB_CONNECTOR_FUNCS \
	.reset = drm_atomic_helper_connector_reset, \
	.fill_modes = drm_helper_probe_single_connector_modes, \
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state, \
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state

/*
 * Mode config
 */

#define DRM_SYSFB_MODE_CONFIG_FUNCS \
	.fb_create = drm_gem_fb_create_with_dirty, \
	.atomic_check = drm_atomic_helper_check, \
	.atomic_commit = drm_atomic_helper_commit

#endif
