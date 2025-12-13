// SPDX-License-Identifier: GPL-2.0-only

#include <linux/export.h>
#include <linux/slab.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_panic.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "drm_sysfb_helper.h"

struct drm_display_mode drm_sysfb_mode(unsigned int width,
				       unsigned int height,
				       unsigned int width_mm,
				       unsigned int height_mm)
{
	/*
	 * Assume a monitor resolution of 96 dpi to
	 * get a somewhat reasonable screen size.
	 */
	if (!width_mm)
		width_mm = DRM_MODE_RES_MM(width, 96ul);
	if (!height_mm)
		height_mm = DRM_MODE_RES_MM(height, 96ul);

	{
		const struct drm_display_mode mode = {
			DRM_MODE_INIT(60, width, height, width_mm, height_mm)
		};

		return mode;
	}
}
EXPORT_SYMBOL(drm_sysfb_mode);

/*
 * Plane
 */

static u32 to_nonalpha_fourcc(u32 fourcc)
{
	/* only handle formats with depth != 0 and alpha channel */
	switch (fourcc) {
	case DRM_FORMAT_ARGB1555:
		return DRM_FORMAT_XRGB1555;
	case DRM_FORMAT_ABGR1555:
		return DRM_FORMAT_XBGR1555;
	case DRM_FORMAT_RGBA5551:
		return DRM_FORMAT_RGBX5551;
	case DRM_FORMAT_BGRA5551:
		return DRM_FORMAT_BGRX5551;
	case DRM_FORMAT_ARGB8888:
		return DRM_FORMAT_XRGB8888;
	case DRM_FORMAT_ABGR8888:
		return DRM_FORMAT_XBGR8888;
	case DRM_FORMAT_RGBA8888:
		return DRM_FORMAT_RGBX8888;
	case DRM_FORMAT_BGRA8888:
		return DRM_FORMAT_BGRX8888;
	case DRM_FORMAT_ARGB2101010:
		return DRM_FORMAT_XRGB2101010;
	case DRM_FORMAT_ABGR2101010:
		return DRM_FORMAT_XBGR2101010;
	case DRM_FORMAT_RGBA1010102:
		return DRM_FORMAT_RGBX1010102;
	case DRM_FORMAT_BGRA1010102:
		return DRM_FORMAT_BGRX1010102;
	}

	return fourcc;
}

static bool is_listed_fourcc(const u32 *fourccs, size_t nfourccs, u32 fourcc)
{
	const u32 *fourccs_end = fourccs + nfourccs;

	while (fourccs < fourccs_end) {
		if (*fourccs == fourcc)
			return true;
		++fourccs;
	}
	return false;
}

/**
 * drm_sysfb_build_fourcc_list - Filters a list of supported color formats against
 *                               the device's native formats
 * @dev: DRM device
 * @native_fourccs: 4CC codes of natively supported color formats
 * @native_nfourccs: The number of entries in @native_fourccs
 * @fourccs_out: Returns 4CC codes of supported color formats
 * @nfourccs_out: The number of available entries in @fourccs_out
 *
 * This function create a list of supported color format from natively
 * supported formats and additional emulated formats.
 * At a minimum, most userspace programs expect at least support for
 * XRGB8888 on the primary plane. Sysfb devices that have to emulate
 * the format should use drm_sysfb_build_fourcc_list() to create a list
 * of supported color formats. The returned list can be handed over to
 * drm_universal_plane_init() et al. Native formats will go before
 * emulated formats. Native formats with alpha channel will be replaced
 * by equal formats without alpha channel, as primary planes usually
 * don't support alpha. Other heuristics might be applied to optimize
 * the sorting order. Formats near the beginning of the list are usually
 * preferred over formats near the end of the list.
 *
 * Returns:
 * The number of color-formats 4CC codes returned in @fourccs_out.
 */
size_t drm_sysfb_build_fourcc_list(struct drm_device *dev,
				   const u32 *native_fourccs, size_t native_nfourccs,
				   u32 *fourccs_out, size_t nfourccs_out)
{
	/*
	 * XRGB8888 is the default fallback format for most of userspace
	 * and it's currently the only format that should be emulated for
	 * the primary plane. Only if there's ever another default fallback,
	 * it should be added here.
	 */
	static const u32 extra_fourccs[] = {
		DRM_FORMAT_XRGB8888,
	};
	static const size_t extra_nfourccs = ARRAY_SIZE(extra_fourccs);

	u32 *fourccs = fourccs_out;
	const u32 *fourccs_end = fourccs_out + nfourccs_out;
	size_t i;

	/*
	 * The device's native formats go first.
	 */

	for (i = 0; i < native_nfourccs; ++i) {
		/*
		 * Several DTs, boot loaders and firmware report native
		 * alpha formats that are non-alpha formats instead. So
		 * replace alpha formats by non-alpha formats.
		 */
		u32 fourcc = to_nonalpha_fourcc(native_fourccs[i]);

		if (is_listed_fourcc(fourccs_out, fourccs - fourccs_out, fourcc)) {
			continue; /* skip duplicate entries */
		} else if (fourccs == fourccs_end) {
			drm_warn(dev, "Ignoring native format %p4cc\n", &fourcc);
			continue; /* end of available output buffer */
		}

		drm_dbg_kms(dev, "adding native format %p4cc\n", &fourcc);

		*fourccs = fourcc;
		++fourccs;
	}

	/*
	 * The extra formats, emulated by the driver, go second.
	 */

	for (i = 0; (i < extra_nfourccs) && (fourccs < fourccs_end); ++i) {
		u32 fourcc = extra_fourccs[i];

		if (is_listed_fourcc(fourccs_out, fourccs - fourccs_out, fourcc)) {
			continue; /* skip duplicate and native entries */
		} else if (fourccs == fourccs_end) {
			drm_warn(dev, "Ignoring emulated format %p4cc\n", &fourcc);
			continue; /* end of available output buffer */
		}

		drm_dbg_kms(dev, "adding emulated format %p4cc\n", &fourcc);

		*fourccs = fourcc;
		++fourccs;
	}

	return fourccs - fourccs_out;
}
EXPORT_SYMBOL(drm_sysfb_build_fourcc_list);

int drm_sysfb_plane_helper_atomic_check(struct drm_plane *plane,
					struct drm_atomic_state *new_state)
{
	struct drm_sysfb_device *sysfb = to_drm_sysfb_device(plane->dev);
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(new_state, plane);
	struct drm_shadow_plane_state *new_shadow_plane_state =
		to_drm_shadow_plane_state(new_plane_state);
	struct drm_framebuffer *new_fb = new_plane_state->fb;
	struct drm_crtc *new_crtc = new_plane_state->crtc;
	struct drm_crtc_state *new_crtc_state = NULL;
	struct drm_sysfb_crtc_state *new_sysfb_crtc_state;
	int ret;

	if (new_crtc)
		new_crtc_state = drm_atomic_get_new_crtc_state(new_state, new_plane_state->crtc);

	ret = drm_atomic_helper_check_plane_state(new_plane_state, new_crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  false, false);
	if (ret)
		return ret;
	else if (!new_plane_state->visible)
		return 0;

	new_crtc_state = drm_atomic_get_new_crtc_state(new_state, new_plane_state->crtc);

	new_sysfb_crtc_state = to_drm_sysfb_crtc_state(new_crtc_state);
	new_sysfb_crtc_state->format = sysfb->fb_format;

	if (new_fb->format != new_sysfb_crtc_state->format) {
		void *buf;

		/* format conversion necessary; reserve buffer */
		buf = drm_format_conv_state_reserve(&new_shadow_plane_state->fmtcnv_state,
						    sysfb->fb_pitch, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL(drm_sysfb_plane_helper_atomic_check);

void drm_sysfb_plane_helper_atomic_update(struct drm_plane *plane, struct drm_atomic_state *state)
{
	struct drm_device *dev = plane->dev;
	struct drm_sysfb_device *sysfb = to_drm_sysfb_device(dev);
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	unsigned int dst_pitch = sysfb->fb_pitch;
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, plane_state->crtc);
	struct drm_sysfb_crtc_state *sysfb_crtc_state = to_drm_sysfb_crtc_state(crtc_state);
	const struct drm_format_info *dst_format = sysfb_crtc_state->format;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect damage;
	int ret, idx;

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret)
		return;

	if (!drm_dev_enter(dev, &idx))
		goto out_drm_gem_fb_end_cpu_access;

	drm_atomic_helper_damage_iter_init(&iter, old_plane_state, plane_state);
	drm_atomic_for_each_plane_damage(&iter, &damage) {
		struct iosys_map dst = sysfb->fb_addr;
		struct drm_rect dst_clip = plane_state->dst;

		if (!drm_rect_intersect(&dst_clip, &damage))
			continue;

		iosys_map_incr(&dst, drm_fb_clip_offset(dst_pitch, dst_format, &dst_clip));
		drm_fb_blit(&dst, &dst_pitch, dst_format->format, shadow_plane_state->data, fb,
			    &damage, &shadow_plane_state->fmtcnv_state);
	}

	drm_dev_exit(idx);
out_drm_gem_fb_end_cpu_access:
	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);
}
EXPORT_SYMBOL(drm_sysfb_plane_helper_atomic_update);

void drm_sysfb_plane_helper_atomic_disable(struct drm_plane *plane,
					   struct drm_atomic_state *state)
{
	struct drm_device *dev = plane->dev;
	struct drm_sysfb_device *sysfb = to_drm_sysfb_device(dev);
	struct iosys_map dst = sysfb->fb_addr;
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
	void __iomem *dst_vmap = dst.vaddr_iomem; /* TODO: Use mapping abstraction */
	unsigned int dst_pitch = sysfb->fb_pitch;
	const struct drm_format_info *dst_format = sysfb->fb_format;
	struct drm_rect dst_clip;
	unsigned long lines, linepixels, i;
	int idx;

	drm_rect_init(&dst_clip,
		      plane_state->src_x >> 16, plane_state->src_y >> 16,
		      plane_state->src_w >> 16, plane_state->src_h >> 16);

	lines = drm_rect_height(&dst_clip);
	linepixels = drm_rect_width(&dst_clip);

	if (!drm_dev_enter(dev, &idx))
		return;

	/* Clear buffer to black if disabled */
	dst_vmap += drm_fb_clip_offset(dst_pitch, dst_format, &dst_clip);
	for (i = 0; i < lines; ++i) {
		memset_io(dst_vmap, 0, linepixels * dst_format->cpp[0]);
		dst_vmap += dst_pitch;
	}

	drm_dev_exit(idx);
}
EXPORT_SYMBOL(drm_sysfb_plane_helper_atomic_disable);

int drm_sysfb_plane_helper_get_scanout_buffer(struct drm_plane *plane,
					      struct drm_scanout_buffer *sb)
{
	struct drm_sysfb_device *sysfb = to_drm_sysfb_device(plane->dev);

	sb->width = sysfb->fb_mode.hdisplay;
	sb->height = sysfb->fb_mode.vdisplay;
	sb->format = sysfb->fb_format;
	sb->pitch[0] = sysfb->fb_pitch;
	sb->map[0] = sysfb->fb_addr;

	return 0;
}
EXPORT_SYMBOL(drm_sysfb_plane_helper_get_scanout_buffer);

/*
 * CRTC
 */

static void drm_sysfb_crtc_state_destroy(struct drm_sysfb_crtc_state *sysfb_crtc_state)
{
	__drm_atomic_helper_crtc_destroy_state(&sysfb_crtc_state->base);

	kfree(sysfb_crtc_state);
}

enum drm_mode_status drm_sysfb_crtc_helper_mode_valid(struct drm_crtc *crtc,
						      const struct drm_display_mode *mode)
{
	struct drm_sysfb_device *sysfb = to_drm_sysfb_device(crtc->dev);

	return drm_crtc_helper_mode_valid_fixed(crtc, mode, &sysfb->fb_mode);
}
EXPORT_SYMBOL(drm_sysfb_crtc_helper_mode_valid);

int drm_sysfb_crtc_helper_atomic_check(struct drm_crtc *crtc, struct drm_atomic_state *new_state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_sysfb_device *sysfb = to_drm_sysfb_device(dev);
	struct drm_crtc_state *new_crtc_state = drm_atomic_get_new_crtc_state(new_state, crtc);
	int ret;

	if (!new_crtc_state->enable)
		return 0;

	ret = drm_atomic_helper_check_crtc_primary_plane(new_crtc_state);
	if (ret)
		return ret;

	if (new_crtc_state->color_mgmt_changed) {
		const size_t gamma_lut_length =
			sysfb->fb_gamma_lut_size * sizeof(struct drm_color_lut);
		const struct drm_property_blob *gamma_lut = new_crtc_state->gamma_lut;

		if (gamma_lut && (gamma_lut->length != gamma_lut_length)) {
			drm_dbg(dev, "Incorrect gamma_lut length %zu\n", gamma_lut->length);
			return -EINVAL;
		}
	}

	return 0;
}
EXPORT_SYMBOL(drm_sysfb_crtc_helper_atomic_check);

void drm_sysfb_crtc_reset(struct drm_crtc *crtc)
{
	struct drm_sysfb_device *sysfb = to_drm_sysfb_device(crtc->dev);
	struct drm_sysfb_crtc_state *sysfb_crtc_state;

	if (crtc->state)
		drm_sysfb_crtc_state_destroy(to_drm_sysfb_crtc_state(crtc->state));

	sysfb_crtc_state = kzalloc(sizeof(*sysfb_crtc_state), GFP_KERNEL);
	if (sysfb_crtc_state) {
		sysfb_crtc_state->format = sysfb->fb_format;
		__drm_atomic_helper_crtc_reset(crtc, &sysfb_crtc_state->base);
	} else {
		__drm_atomic_helper_crtc_reset(crtc, NULL);
	}
}
EXPORT_SYMBOL(drm_sysfb_crtc_reset);

struct drm_crtc_state *drm_sysfb_crtc_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_crtc_state *crtc_state = crtc->state;
	struct drm_sysfb_crtc_state *new_sysfb_crtc_state;
	struct drm_sysfb_crtc_state *sysfb_crtc_state;

	if (drm_WARN_ON(dev, !crtc_state))
		return NULL;

	new_sysfb_crtc_state = kzalloc(sizeof(*new_sysfb_crtc_state), GFP_KERNEL);
	if (!new_sysfb_crtc_state)
		return NULL;

	sysfb_crtc_state = to_drm_sysfb_crtc_state(crtc_state);

	__drm_atomic_helper_crtc_duplicate_state(crtc, &new_sysfb_crtc_state->base);
	new_sysfb_crtc_state->format = sysfb_crtc_state->format;

	return &new_sysfb_crtc_state->base;
}
EXPORT_SYMBOL(drm_sysfb_crtc_atomic_duplicate_state);

void drm_sysfb_crtc_atomic_destroy_state(struct drm_crtc *crtc, struct drm_crtc_state *crtc_state)
{
	drm_sysfb_crtc_state_destroy(to_drm_sysfb_crtc_state(crtc_state));
}
EXPORT_SYMBOL(drm_sysfb_crtc_atomic_destroy_state);

/*
 * Connector
 */

static int drm_sysfb_get_edid_block(void *data, u8 *buf, unsigned int block, size_t len)
{
	struct drm_sysfb_device *sysfb = data;
	const u8 *edid = sysfb->edid;
	size_t off = block * EDID_LENGTH;
	size_t end = off + len;

	if (!edid)
		return -EINVAL;
	if (end > EDID_LENGTH)
		return -EINVAL;
	memcpy(buf, &edid[off], len);

	/*
	 * We don't have EDID extensions available and reporting them
	 * will upset DRM helpers. Thus clear the extension field and
	 * update the checksum. Adding the extension flag to the checksum
	 * does this.
	 */
	buf[127] += buf[126];
	buf[126] = 0;

	return 0;
}

int drm_sysfb_connector_helper_get_modes(struct drm_connector *connector)
{
	struct drm_sysfb_device *sysfb = to_drm_sysfb_device(connector->dev);
	const struct drm_edid *drm_edid;

	if (sysfb->edid) {
		drm_edid = drm_edid_read_custom(connector, drm_sysfb_get_edid_block, sysfb);
		drm_edid_connector_update(connector, drm_edid);
		drm_edid_free(drm_edid);
	}

	/* Return the fixed mode even with EDID */
	return drm_connector_helper_get_modes_fixed(connector, &sysfb->fb_mode);
}
EXPORT_SYMBOL(drm_sysfb_connector_helper_get_modes);
