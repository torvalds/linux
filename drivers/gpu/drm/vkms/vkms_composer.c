// SPDX-License-Identifier: GPL-2.0+

#include <linux/crc32.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_fixed.h>
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
 * @stage_buffer: The line with the pixels from src_plane
 * @output_buffer: A line buffer that receives all the blends output
 * @x_start: The start offset
 * @pixel_count: The number of pixels to blend
 *
 * The pixels [0;@pixel_count) in stage_buffer are blended at [@x_start;@x_start+@pixel_count) in
 * output_buffer.
 *
 * The current DRM assumption is that pixel color values have been already
 * pre-multiplied with the alpha channel values. See more
 * drm_plane_create_blend_mode_property(). Also, this formula assumes a
 * completely opaque background.
 */
static void pre_mul_alpha_blend(const struct line_buffer *stage_buffer,
				struct line_buffer *output_buffer, int x_start, int pixel_count)
{
	struct pixel_argb_u16 *out = &output_buffer->pixels[x_start];
	const struct pixel_argb_u16 *in = stage_buffer->pixels;

	for (int i = 0; i < pixel_count; i++) {
		out[i].a = (u16)0xffff;
		out[i].r = pre_mul_blend_channel(in[i].r, out[i].r, in[i].a);
		out[i].g = pre_mul_blend_channel(in[i].g, out[i].g, in[i].a);
		out[i].b = pre_mul_blend_channel(in[i].b, out[i].b, in[i].a);
	}
}

static int get_y_pos(struct vkms_frame_info *frame_info, int y)
{
	if (frame_info->rotation & DRM_MODE_REFLECT_Y)
		return drm_rect_height(&frame_info->rotated) - y - 1;

	switch (frame_info->rotation & DRM_MODE_ROTATE_MASK) {
	case DRM_MODE_ROTATE_90:
		return frame_info->rotated.x2 - y - 1;
	case DRM_MODE_ROTATE_270:
		return y + frame_info->rotated.x1;
	default:
		return y;
	}
}

static bool check_limit(struct vkms_frame_info *frame_info, int pos)
{
	if (drm_rotation_90_or_270(frame_info->rotation)) {
		if (pos >= 0 && pos < drm_rect_width(&frame_info->rotated))
			return true;
	} else {
		if (pos >= frame_info->rotated.y1 && pos < frame_info->rotated.y2)
			return true;
	}

	return false;
}

static void fill_background(const struct pixel_argb_u16 *background_color,
			    struct line_buffer *output_buffer)
{
	for (size_t i = 0; i < output_buffer->n_pixels; i++)
		output_buffer->pixels[i] = *background_color;
}

// lerp(a, b, t) = a + (b - a) * t
static u16 lerp_u16(u16 a, u16 b, s64 t)
{
	s64 a_fp = drm_int2fixp(a);
	s64 b_fp = drm_int2fixp(b);

	s64 delta = drm_fixp_mul(b_fp - a_fp, t);

	return drm_fixp2int(a_fp + delta);
}

static s64 get_lut_index(const struct vkms_color_lut *lut, u16 channel_value)
{
	s64 color_channel_fp = drm_int2fixp(channel_value);

	return drm_fixp_mul(color_channel_fp, lut->channel_value2index_ratio);
}

/*
 * This enum is related to the positions of the variables inside
 * `struct drm_color_lut`, so the order of both needs to be the same.
 */
enum lut_channel {
	LUT_RED = 0,
	LUT_GREEN,
	LUT_BLUE,
	LUT_RESERVED
};

static u16 apply_lut_to_channel_value(const struct vkms_color_lut *lut, u16 channel_value,
				      enum lut_channel channel)
{
	s64 lut_index = get_lut_index(lut, channel_value);
	u16 *floor_lut_value, *ceil_lut_value;
	u16 floor_channel_value, ceil_channel_value;

	/*
	 * This checks if `struct drm_color_lut` has any gap added by the compiler
	 * between the struct fields.
	 */
	static_assert(sizeof(struct drm_color_lut) == sizeof(__u16) * 4);

	floor_lut_value = (__u16 *)&lut->base[drm_fixp2int(lut_index)];
	if (drm_fixp2int(lut_index) == (lut->lut_length - 1))
		/* We're at the end of the LUT array, use same value for ceil and floor */
		ceil_lut_value = floor_lut_value;
	else
		ceil_lut_value = (__u16 *)&lut->base[drm_fixp2int_ceil(lut_index)];

	floor_channel_value = floor_lut_value[channel];
	ceil_channel_value = ceil_lut_value[channel];

	return lerp_u16(floor_channel_value, ceil_channel_value,
			lut_index & DRM_FIXED_DECIMAL_MASK);
}

static void apply_lut(const struct vkms_crtc_state *crtc_state, struct line_buffer *output_buffer)
{
	if (!crtc_state->gamma_lut.base)
		return;

	if (!crtc_state->gamma_lut.lut_length)
		return;

	for (size_t x = 0; x < output_buffer->n_pixels; x++) {
		struct pixel_argb_u16 *pixel = &output_buffer->pixels[x];

		pixel->r = apply_lut_to_channel_value(&crtc_state->gamma_lut, pixel->r, LUT_RED);
		pixel->g = apply_lut_to_channel_value(&crtc_state->gamma_lut, pixel->g, LUT_GREEN);
		pixel->b = apply_lut_to_channel_value(&crtc_state->gamma_lut, pixel->b, LUT_BLUE);
	}
}

/**
 * blend - blend the pixels from all planes and compute crc
 * @wb: The writeback frame buffer metadata
 * @crtc_state: The crtc state
 * @crc32: The crc output of the final frame
 * @output_buffer: A buffer of a row that will receive the result of the blend(s)
 * @stage_buffer: The line with the pixels from plane being blend to the output
 * @row_size: The size, in bytes, of a single row
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
	int y_pos, x_dst, pixel_count;

	const struct pixel_argb_u16 background_color = { .a = 0xffff };

	size_t crtc_y_limit = crtc_state->base.mode.vdisplay;

	/*
	 * The planes are composed line-by-line to avoid heavy memory usage. It is a necessary
	 * complexity to avoid poor blending performance.
	 *
	 * The function vkms_compose_row() is used to read a line, pixel-by-pixel, into the staging
	 * buffer.
	 */
	for (size_t y = 0; y < crtc_y_limit; y++) {
		fill_background(&background_color, output_buffer);

		/* The active planes are composed associatively in z-order. */
		for (size_t i = 0; i < n_active_planes; i++) {
			x_dst = plane[i]->frame_info->dst.x1;
			pixel_count = min_t(int, drm_rect_width(&plane[i]->frame_info->dst),
					    (int)stage_buffer->n_pixels);
			y_pos = get_y_pos(plane[i]->frame_info, y);

			if (!check_limit(plane[i]->frame_info, y_pos))
				continue;

			vkms_compose_row(stage_buffer, plane[i], y_pos);
			pre_mul_alpha_blend(stage_buffer, output_buffer, x_dst, pixel_count);
		}

		apply_lut(crtc_state, output_buffer);

		*crc32 = crc32_le(*crc32, (void *)output_buffer->pixels, row_size);

		if (wb)
			vkms_writeback_row(wb, output_buffer, y_pos);
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

	if (active_wb && !active_wb->pixel_write)
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

	line_width = crtc_state->base.mode.hdisplay;
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

	if (crtc->state->gamma_lut) {
		s64 max_lut_index_fp;
		s64 u16_max_fp = drm_int2fixp(0xffff);

		crtc_state->gamma_lut.base = (struct drm_color_lut *)crtc->state->gamma_lut->data;
		crtc_state->gamma_lut.lut_length =
			crtc->state->gamma_lut->length / sizeof(struct drm_color_lut);
		max_lut_index_fp = drm_int2fixp(crtc_state->gamma_lut.lut_length - 1);
		crtc_state->gamma_lut.channel_value2index_ratio = drm_fixp_div(max_lut_index_fp,
									       u16_max_fp);

	} else {
		crtc_state->gamma_lut.base = NULL;
	}

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

static const char *const pipe_crc_sources[] = { "auto" };

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
