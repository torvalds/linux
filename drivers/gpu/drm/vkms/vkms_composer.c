// SPDX-License-Identifier: GPL-2.0+

#include <linux/crc32.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_vblank.h>
#include <linux/minmax.h>

#include "vkms_drv.h"

static u16 pre_mul_blend_channel(u16 src, u16 dst, u16 alpha)
{
	u32 new_color;

	new_color = (src * 0xffff + dst * (0xffff - alpha));

	return DIV_ROUND_CLOSEST(new_color, 0xffff);
}

/**
 * pre_mul_alpha_blend - alpha blending equation
 * @src_frame_info: source framebuffer's metadata
 * @stage_buffer: The line with the pixels from src_plane
 * @output_buffer: A line buffer that receives all the blends output
 *
 * Using the information from the `frame_info`, this blends only the
 * necessary pixels from the `stage_buffer` to the `output_buffer`
 * using premultiplied blend formula.
 *
 * The current DRM assumption is that pixel color values have been already
 * pre-multiplied with the alpha channel values. See more
 * drm_plane_create_blend_mode_property(). Also, this formula assumes a
 * completely opaque background.
 */
static void pre_mul_alpha_blend(struct vkms_frame_info *frame_info,
				struct line_buffer *stage_buffer,
				struct line_buffer *output_buffer)
{
	int x_dst = frame_info->dst.x1;
	struct pixel_argb_u16 *out = output_buffer->pixels + x_dst;
	struct pixel_argb_u16 *in = stage_buffer->pixels;
	int x_limit = min_t(size_t, drm_rect_width(&frame_info->dst),
			    stage_buffer->n_pixels);

	for (int x = 0; x < x_limit; x++) {
		out[x].a = (u16)0xffff;
		out[x].r = pre_mul_blend_channel(in[x].r, out[x].r, in[x].a);
		out[x].g = pre_mul_blend_channel(in[x].g, out[x].g, in[x].a);
		out[x].b = pre_mul_blend_channel(in[x].b, out[x].b, in[x].a);
	}
}

static bool check_y_limit(struct vkms_frame_info *frame_info, int y)
{
	if (y >= frame_info->dst.y1 && y < frame_info->dst.y2)
		return true;

	return false;
}

static void fill_background(const struct pixel_argb_u16 *background_color,
			    struct line_buffer *output_buffer)
{
	for (size_t i = 0; i < output_buffer->n_pixels; i++)
		output_buffer->pixels[i] = *background_color;
}

/**
 * @wb_frame_info: The writeback frame buffer metadata
 * @crtc_state: The crtc state
 * @crc32: The crc output of the final frame
 * @output_buffer: A buffer of a row that will receive the result of the blend(s)
 * @stage_buffer: The line with the pixels from plane being blend to the output
 *
 * This function blends the pixels (Using the `pre_mul_alpha_blend`)
 * from all planes, calculates the crc32 of the output from the former step,
 * and, if necessary, convert and store the output to the writeback buffer.
 */
static void blend(struct vkms_writeback_job *wb,
		  struct vkms_crtc_state *crtc_state,
		  u32 *crc32, struct line_buffer *stage_buffer,
		  struct line_buffer *output_buffer, size_t row_size)
{
	struct vkms_plane_state **plane = crtc_state->active_planes;
	u32 n_active_planes = crtc_state->num_active_planes;

	const struct pixel_argb_u16 background_color = { .a = 0xffff };

	size_t crtc_y_limit = crtc_state->base.crtc->mode.vdisplay;

	for (size_t y = 0; y < crtc_y_limit; y++) {
		fill_background(&background_color, output_buffer);

		/* The active planes are composed associatively in z-order. */
		for (size_t i = 0; i < n_active_planes; i++) {
			if (!check_y_limit(plane[i]->frame_info, y))
				continue;

			vkms_compose_row(stage_buffer, plane[i], y);
			pre_mul_alpha_blend(plane[i]->frame_info, stage_buffer,
					    output_buffer);
		}

		*crc32 = crc32_le(*crc32, (void *)output_buffer->pixels, row_size);

		if (wb)
			wb->wb_write(&wb->wb_frame_info, output_buffer, y);
	}
}

static int check_format_funcs(struct vkms_crtc_state *crtc_state,
			      struct vkms_writeback_job *active_wb)
{
	struct vkms_plane_state **planes = crtc_state->active_planes;
	u32 n_active_planes = crtc_state->num_active_planes;

	for (size_t i = 0; i < n_active_planes; i++)
		if (!planes[i]->pixel_read)
			return -1;

	if (active_wb && !active_wb->wb_write)
		return -1;

	return 0;
}

static int check_iosys_map(struct vkms_crtc_state *crtc_state)
{
	struct vkms_plane_state **plane_state = crtc_state->active_planes;
	u32 n_active_planes = crtc_state->num_active_planes;

	for (size_t i = 0; i < n_active_planes; i++)
		if (iosys_map_is_null(&plane_state[i]->frame_info->map[0]))
			return -1;

	return 0;
}

static int compose_active_planes(struct vkms_writeback_job *active_wb,
				 struct vkms_crtc_state *crtc_state,
				 u32 *crc32)
{
	size_t line_width, pixel_size = sizeof(struct pixel_argb_u16);
	struct line_buffer output_buffer, stage_buffer;
	int ret = 0;

	/*
	 * This check exists so we can call `crc32_le` for the entire line
	 * instead doing it for each channel of each pixel in case
	 * `struct `pixel_argb_u16` had any gap added by the compiler
	 * between the struct fields.
	 */
	static_assert(sizeof(struct pixel_argb_u16) == 8);

	if (WARN_ON(check_iosys_map(crtc_state)))
		return -EINVAL;

	if (WARN_ON(check_format_funcs(crtc_state, active_wb)))
		return -EINVAL;

	line_width = crtc_state->base.crtc->mode.hdisplay;
	stage_buffer.n_pixels = line_width;
	output_buffer.n_pixels = line_width;

	stage_buffer.pixels = kvmalloc(line_width * pixel_size, GFP_KERNEL);
	if (!stage_buffer.pixels) {
		DRM_ERROR("Cannot allocate memory for the output line buffer");
		return -ENOMEM;
	}

	output_buffer.pixels = kvmalloc(line_width * pixel_size, GFP_KERNEL);
	if (!output_buffer.pixels) {
		DRM_ERROR("Cannot allocate memory for intermediate line buffer");
		ret = -ENOMEM;
		goto free_stage_buffer;
	}

	blend(active_wb, crtc_state, crc32, &stage_buffer,
	      &output_buffer, line_width * pixel_size);

	kvfree(output_buffer.pixels);
free_stage_buffer:
	kvfree(stage_buffer.pixels);

	return ret;
}

/**
 * vkms_composer_worker - ordered work_struct to compute CRC
 *
 * @work: work_struct
 *
 * Work handler for composing and computing CRCs. work_struct scheduled in
 * an ordered workqueue that's periodically scheduled to run by
 * vkms_vblank_simulate() and flushed at vkms_atomic_commit_tail().
 */
void vkms_composer_worker(struct work_struct *work)
{
	struct vkms_crtc_state *crtc_state = container_of(work,
						struct vkms_crtc_state,
						composer_work);
	struct drm_crtc *crtc = crtc_state->base.crtc;
	struct vkms_writeback_job *active_wb = crtc_state->active_writeback;
	struct vkms_output *out = drm_crtc_to_vkms_output(crtc);
	bool crc_pending, wb_pending;
	u64 frame_start, frame_end;
	u32 crc32 = 0;
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

	if (wb_pending)
		ret = compose_active_planes(active_wb, crtc_state, &crc32);
	else
		ret = compose_active_planes(NULL, crtc_state, &crc32);

	if (ret)
		return;

	if (wb_pending) {
		drm_writeback_signal_completion(&out->wb_connector, 0);
		spin_lock_irq(&out->composer_lock);
		crtc_state->wb_pending = false;
		spin_unlock_irq(&out->composer_lock);
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
