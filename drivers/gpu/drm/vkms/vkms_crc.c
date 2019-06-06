// SPDX-License-Identifier: GPL-2.0+

#include "vkms_drv.h"
#include <linux/crc32.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>

/**
 * compute_crc - Compute CRC value on output frame
 *
 * @vaddr_out: address to final framebuffer
 * @crc_out: framebuffer's metadata
 *
 * returns CRC value computed using crc32 on the visible portion of
 * the final framebuffer at vaddr_out
 */
static uint32_t compute_crc(void *vaddr_out, struct vkms_crc_data *crc_out)
{
	int i, j, src_offset;
	int x_src = crc_out->src.x1 >> 16;
	int y_src = crc_out->src.y1 >> 16;
	int h_src = drm_rect_height(&crc_out->src) >> 16;
	int w_src = drm_rect_width(&crc_out->src) >> 16;
	u32 crc = 0;

	for (i = y_src; i < y_src + h_src; ++i) {
		for (j = x_src; j < x_src + w_src; ++j) {
			src_offset = crc_out->offset
				     + (i * crc_out->pitch)
				     + (j * crc_out->cpp);
			/* XRGB format ignores Alpha channel */
			memset(vaddr_out + src_offset + 24, 0,  8);
			crc = crc32_le(crc, vaddr_out + src_offset,
				       sizeof(u32));
		}
	}

	return crc;
}

/**
 * blend - belnd value at vaddr_src with value at vaddr_dst
 * @vaddr_dst: destination address
 * @vaddr_src: source address
 * @crc_dst: destination framebuffer's metadata
 * @crc_src: source framebuffer's metadata
 *
 * Blend value at vaddr_src with value at vaddr_dst.
 * Currently, this function write value at vaddr_src on value
 * at vaddr_dst using buffer's metadata to locate the new values
 * from vaddr_src and their distenation at vaddr_dst.
 *
 * Todo: Use the alpha value to blend vaddr_src with vaddr_dst
 *	 instead of overwriting it.
 */
static void blend(void *vaddr_dst, void *vaddr_src,
		  struct vkms_crc_data *crc_dst,
		  struct vkms_crc_data *crc_src)
{
	int i, j, j_dst, i_dst;
	int offset_src, offset_dst;

	int x_src = crc_src->src.x1 >> 16;
	int y_src = crc_src->src.y1 >> 16;

	int x_dst = crc_src->dst.x1;
	int y_dst = crc_src->dst.y1;
	int h_dst = drm_rect_height(&crc_src->dst);
	int w_dst = drm_rect_width(&crc_src->dst);

	int y_limit = y_src + h_dst;
	int x_limit = x_src + w_dst;

	for (i = y_src, i_dst = y_dst; i < y_limit; ++i) {
		for (j = x_src, j_dst = x_dst; j < x_limit; ++j) {
			offset_dst = crc_dst->offset
				     + (i_dst * crc_dst->pitch)
				     + (j_dst++ * crc_dst->cpp);
			offset_src = crc_src->offset
				     + (i * crc_src->pitch)
				     + (j * crc_src->cpp);

			memcpy(vaddr_dst + offset_dst,
			       vaddr_src + offset_src, sizeof(u32));
		}
		i_dst++;
	}
}

static void compose_cursor(struct vkms_crc_data *cursor_crc,
			   struct vkms_crc_data *primary_crc, void *vaddr_out)
{
	struct drm_gem_object *cursor_obj;
	struct vkms_gem_object *cursor_vkms_obj;

	cursor_obj = drm_gem_fb_get_obj(&cursor_crc->fb, 0);
	cursor_vkms_obj = drm_gem_to_vkms_gem(cursor_obj);

	if (WARN_ON(!cursor_vkms_obj->vaddr))
		return;

	blend(vaddr_out, cursor_vkms_obj->vaddr, primary_crc, cursor_crc);
}

static uint32_t _vkms_get_crc(struct vkms_crc_data *primary_crc,
			      struct vkms_crc_data *cursor_crc)
{
	struct drm_framebuffer *fb = &primary_crc->fb;
	struct drm_gem_object *gem_obj = drm_gem_fb_get_obj(fb, 0);
	struct vkms_gem_object *vkms_obj = drm_gem_to_vkms_gem(gem_obj);
	void *vaddr_out = kzalloc(vkms_obj->gem.size, GFP_KERNEL);
	u32 crc = 0;

	if (!vaddr_out) {
		DRM_ERROR("Failed to allocate memory for output frame.");
		return 0;
	}

	if (WARN_ON(!vkms_obj->vaddr)) {
		kfree(vaddr_out);
		return crc;
	}

	memcpy(vaddr_out, vkms_obj->vaddr, vkms_obj->gem.size);

	if (cursor_crc)
		compose_cursor(cursor_crc, primary_crc, vaddr_out);

	crc = compute_crc(vaddr_out, primary_crc);

	kfree(vaddr_out);

	return crc;
}

/**
 * vkms_crc_work_handle - ordered work_struct to compute CRC
 *
 * @work: work_struct
 *
 * Work handler for computing CRCs. work_struct scheduled in
 * an ordered workqueue that's periodically scheduled to run by
 * _vblank_handle() and flushed at vkms_atomic_crtc_destroy_state().
 */
void vkms_crc_work_handle(struct work_struct *work)
{
	struct vkms_crtc_state *crtc_state = container_of(work,
						struct vkms_crtc_state,
						crc_work);
	struct drm_crtc *crtc = crtc_state->base.crtc;
	struct vkms_output *out = drm_crtc_to_vkms_output(crtc);
	struct vkms_crc_data *primary_crc = NULL;
	struct vkms_crc_data *cursor_crc = NULL;
	u32 crc32 = 0;
	u64 frame_start, frame_end;
	bool crc_pending;

	spin_lock_irq(&out->crc_lock);
	frame_start = crtc_state->frame_start;
	frame_end = crtc_state->frame_end;
	crc_pending = crtc_state->crc_pending;
	crtc_state->frame_start = 0;
	crtc_state->frame_end = 0;
	crtc_state->crc_pending = false;
	spin_unlock_irq(&out->crc_lock);

	/*
	 * We raced with the vblank hrtimer and previous work already computed
	 * the crc, nothing to do.
	 */
	if (!crc_pending)
		return;

	if (crtc_state->num_active_planes >= 1)
		primary_crc = crtc_state->active_planes[0]->crc_data;

	if (crtc_state->num_active_planes == 2)
		cursor_crc = crtc_state->active_planes[1]->crc_data;

	if (primary_crc)
		crc32 = _vkms_get_crc(primary_crc, cursor_crc);

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

int vkms_set_crc_source(struct drm_crtc *crtc, const char *src_name)
{
	struct vkms_output *out = drm_crtc_to_vkms_output(crtc);
	bool enabled = false;
	int ret = 0;

	ret = vkms_crc_parse_source(src_name, &enabled);

	spin_lock_irq(&out->lock);
	out->crc_enabled = enabled;
	spin_unlock_irq(&out->lock);

	return ret;
}
