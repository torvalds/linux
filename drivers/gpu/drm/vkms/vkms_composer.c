// SPDX-License-Identifier: GPL-2.0+

#include <linux/crc32.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_vblank.h>

#include "vkms_drv.h"

static u32 get_pixel_from_buffer(int x, int y, const u8 *buffer,
				 const struct vkms_composer *composer)
{
	u32 pixel;
	int src_offset = composer->offset + (y * composer->pitch)
				      + (x * composer->cpp);

	pixel = *(u32 *)&buffer[src_offset];

	return pixel;
}

/**
 * compute_crc - Compute CRC value on output frame
 *
 * @vaddr: address to final framebuffer
 * @composer: framebuffer's metadata
 *
 * returns CRC value computed using crc32 on the visible portion of
 * the final framebuffer at vaddr_out
 */
static uint32_t compute_crc(const u8 *vaddr,
			    const struct vkms_composer *composer)
{
	int x, y;
	u32 crc = 0, pixel = 0;
	int x_src = composer->src.x1 >> 16;
	int y_src = composer->src.y1 >> 16;
	int h_src = drm_rect_height(&composer->src) >> 16;
	int w_src = drm_rect_width(&composer->src) >> 16;

	for (y = y_src; y < y_src + h_src; ++y) {
		for (x = x_src; x < x_src + w_src; ++x) {
			pixel = get_pixel_from_buffer(x, y, vaddr, composer);
			crc = crc32_le(crc, (void *)&pixel, sizeof(u32));
		}
	}

	return crc;
}

static u8 blend_channel(u8 src, u8 dst, u8 alpha)
{
	u32 pre_blend;
	u8 new_color;

	pre_blend = (src * 255 + dst * (255 - alpha));

	/* Faster div by 255 */
	new_color = ((pre_blend + ((pre_blend + 257) >> 8)) >> 8);

	return new_color;
}

static void alpha_blending(const u8 *argb_src, u8 *argb_dst)
{
	u8 alpha;

	alpha = argb_src[3];
	argb_dst[0] = blend_channel(argb_src[0], argb_dst[0], alpha);
	argb_dst[1] = blend_channel(argb_src[1], argb_dst[1], alpha);
	argb_dst[2] = blend_channel(argb_src[2], argb_dst[2], alpha);
	/* Opaque primary */
	argb_dst[3] = 0xFF;
}

/**
 * blend - blend value at vaddr_src with value at vaddr_dst
 * @vaddr_dst: destination address
 * @vaddr_src: source address
 * @dst_composer: destination framebuffer's metadata
 * @src_composer: source framebuffer's metadata
 *
 * Blend the vaddr_src value with the vaddr_dst value using the pre-multiplied
 * alpha blending equation, since DRM currently assumes that the pixel color
 * values have already been pre-multiplied with the alpha channel values. See
 * more drm_plane_create_blend_mode_property(). This function uses buffer's
 * metadata to locate the new composite values at vaddr_dst.
 */
static void blend(void *vaddr_dst, void *vaddr_src,
		  struct vkms_composer *dst_composer,
		  struct vkms_composer *src_composer)
{
	int i, j, j_dst, i_dst;
	int offset_src, offset_dst;
	u8 *pixel_dst, *pixel_src;

	int x_src = src_composer->src.x1 >> 16;
	int y_src = src_composer->src.y1 >> 16;

	int x_dst = src_composer->dst.x1;
	int y_dst = src_composer->dst.y1;
	int h_dst = drm_rect_height(&src_composer->dst);
	int w_dst = drm_rect_width(&src_composer->dst);

	int y_limit = y_src + h_dst;
	int x_limit = x_src + w_dst;

	for (i = y_src, i_dst = y_dst; i < y_limit; ++i) {
		for (j = x_src, j_dst = x_dst; j < x_limit; ++j) {
			offset_dst = dst_composer->offset
				     + (i_dst * dst_composer->pitch)
				     + (j_dst++ * dst_composer->cpp);
			offset_src = src_composer->offset
				     + (i * src_composer->pitch)
				     + (j * src_composer->cpp);

			pixel_src = (u8 *)(vaddr_src + offset_src);
			pixel_dst = (u8 *)(vaddr_dst + offset_dst);
			alpha_blending(pixel_src, pixel_dst);
		}
		i_dst++;
	}
}

static void compose_cursor(struct vkms_composer *cursor_composer,
			   struct vkms_composer *primary_composer,
			   void *vaddr_out)
{
	struct drm_gem_object *cursor_obj;
	struct drm_gem_shmem_object *cursor_shmem_obj;

	cursor_obj = drm_gem_fb_get_obj(&cursor_composer->fb, 0);
	cursor_shmem_obj = to_drm_gem_shmem_obj(cursor_obj);

	if (WARN_ON(!cursor_shmem_obj->vaddr))
		return;

	blend(vaddr_out, cursor_shmem_obj->vaddr,
	      primary_composer, cursor_composer);
}

static int compose_planes(void **vaddr_out,
			  struct vkms_composer *primary_composer,
			  struct vkms_composer *cursor_composer)
{
	struct drm_framebuffer *fb = &primary_composer->fb;
	struct drm_gem_object *gem_obj = drm_gem_fb_get_obj(fb, 0);
	struct drm_gem_shmem_object *shmem_obj = to_drm_gem_shmem_obj(gem_obj);

	if (!*vaddr_out) {
		*vaddr_out = kzalloc(shmem_obj->base.size, GFP_KERNEL);
		if (!*vaddr_out) {
			DRM_ERROR("Cannot allocate memory for output frame.");
			return -ENOMEM;
		}
	}

	if (WARN_ON(!shmem_obj->vaddr))
		return -EINVAL;

	memcpy(*vaddr_out, shmem_obj->vaddr, shmem_obj->base.size);

	if (cursor_composer)
		compose_cursor(cursor_composer, primary_composer, *vaddr_out);

	return 0;
}

/**
 * vkms_composer_worker - ordered work_struct to compute CRC
 *
 * @work: work_struct
 *
 * Work handler for composing and computing CRCs. work_struct scheduled in
 * an ordered workqueue that's periodically scheduled to run by
 * _vblank_handle() and flushed at vkms_atomic_crtc_destroy_state().
 */
void vkms_composer_worker(struct work_struct *work)
{
	struct vkms_crtc_state *crtc_state = container_of(work,
						struct vkms_crtc_state,
						composer_work);
	struct drm_crtc *crtc = crtc_state->base.crtc;
	struct vkms_output *out = drm_crtc_to_vkms_output(crtc);
	struct vkms_composer *primary_composer = NULL;
	struct vkms_composer *cursor_composer = NULL;
	bool crc_pending, wb_pending;
	void *vaddr_out = NULL;
	u32 crc32 = 0;
	u64 frame_start, frame_end;
	int ret;

	spin_lock_irq(&out->composer_lock);
	frame_start = crtc_state->frame_start;
	frame_end = crtc_state->frame_end;
	crc_pending = crtc_state->crc_pending;
	wb_pending = crtc_state->wb_pending;
	crtc_state->frame_start = 0;
	crtc_state->frame_end = 0;
	crtc_state->crc_pending = false;
	spin_unlock_irq(&out->composer_lock);

	/*
	 * We raced with the vblank hrtimer and previous work already computed
	 * the crc, nothing to do.
	 */
	if (!crc_pending)
		return;

	if (crtc_state->num_active_planes >= 1)
		primary_composer = crtc_state->active_planes[0]->composer;

	if (crtc_state->num_active_planes == 2)
		cursor_composer = crtc_state->active_planes[1]->composer;

	if (!primary_composer)
		return;

	if (wb_pending)
		vaddr_out = crtc_state->active_writeback;

	ret = compose_planes(&vaddr_out, primary_composer, cursor_composer);
	if (ret) {
		if (ret == -EINVAL && !wb_pending)
			kfree(vaddr_out);
		return;
	}

	crc32 = compute_crc(vaddr_out, primary_composer);

	if (wb_pending) {
		drm_writeback_signal_completion(&out->wb_connector, 0);
		spin_lock_irq(&out->composer_lock);
		crtc_state->wb_pending = false;
		spin_unlock_irq(&out->composer_lock);
	} else {
		kfree(vaddr_out);
	}

	/*
	 * The worker can fall behind the vblank hrtimer, make sure we catch up.
	 */
	while (frame_start <= frame_end)
		drm_crtc_add_crc_entry(crtc, true, frame_start++, &crc32);
}

static const char * const pipe_crc_sources[] = {"auto"};

const char *const *vkms_get_crc_sources(struct drm_crtc *crtc,
					size_t *count)
{
	*count = ARRAY_SIZE(pipe_crc_sources);
	return pipe_crc_sources;
}

static int vkms_crc_parse_source(const char *src_name, bool *enabled)
{
	int ret = 0;

	if (!src_name) {
		*enabled = false;
	} else if (strcmp(src_name, "auto") == 0) {
		*enabled = true;
	} else {
		*enabled = false;
		ret = -EINVAL;
	}

	return ret;
}

int vkms_verify_crc_source(struct drm_crtc *crtc, const char *src_name,
			   size_t *values_cnt)
{
	bool enabled;

	if (vkms_crc_parse_source(src_name, &enabled) < 0) {
		DRM_DEBUG_DRIVER("unknown source %s\n", src_name);
		return -EINVAL;
	}

	*values_cnt = 1;

	return 0;
}

void vkms_set_composer(struct vkms_output *out, bool enabled)
{
	bool old_enabled;

	if (enabled)
		drm_crtc_vblank_get(&out->crtc);

	spin_lock_irq(&out->lock);
	old_enabled = out->composer_enabled;
	out->composer_enabled = enabled;
	spin_unlock_irq(&out->lock);

	if (old_enabled)
		drm_crtc_vblank_put(&out->crtc);
}

int vkms_set_crc_source(struct drm_crtc *crtc, const char *src_name)
{
	struct vkms_output *out = drm_crtc_to_vkms_output(crtc);
	bool enabled = false;
	int ret = 0;

	ret = vkms_crc_parse_source(src_name, &enabled);

	vkms_set_composer(out, enabled);

	return ret;
}
