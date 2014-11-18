/*
 * vivid-tpg.h - Test Pattern Generator
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _VIVID_TPG_H_
#define _VIVID_TPG_H_

#include <linux/version.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/videodev2.h>

#include "vivid-tpg-colors.h"

enum tpg_pattern {
	TPG_PAT_75_COLORBAR,
	TPG_PAT_100_COLORBAR,
	TPG_PAT_CSC_COLORBAR,
	TPG_PAT_100_HCOLORBAR,
	TPG_PAT_100_COLORSQUARES,
	TPG_PAT_BLACK,
	TPG_PAT_WHITE,
	TPG_PAT_RED,
	TPG_PAT_GREEN,
	TPG_PAT_BLUE,
	TPG_PAT_CHECKERS_16X16,
	TPG_PAT_CHECKERS_1X1,
	TPG_PAT_ALTERNATING_HLINES,
	TPG_PAT_ALTERNATING_VLINES,
	TPG_PAT_CROSS_1_PIXEL,
	TPG_PAT_CROSS_2_PIXELS,
	TPG_PAT_CROSS_10_PIXELS,
	TPG_PAT_GRAY_RAMP,

	/* Must be the last pattern */
	TPG_PAT_NOISE,
};

extern const char * const tpg_pattern_strings[];

enum tpg_quality {
	TPG_QUAL_COLOR,
	TPG_QUAL_GRAY,
	TPG_QUAL_NOISE
};

enum tpg_video_aspect {
	TPG_VIDEO_ASPECT_IMAGE,
	TPG_VIDEO_ASPECT_4X3,
	TPG_VIDEO_ASPECT_14X9_CENTRE,
	TPG_VIDEO_ASPECT_16X9_CENTRE,
	TPG_VIDEO_ASPECT_16X9_ANAMORPHIC,
};

enum tpg_pixel_aspect {
	TPG_PIXEL_ASPECT_SQUARE,
	TPG_PIXEL_ASPECT_NTSC,
	TPG_PIXEL_ASPECT_PAL,
};

enum tpg_move_mode {
	TPG_MOVE_NEG_FAST,
	TPG_MOVE_NEG,
	TPG_MOVE_NEG_SLOW,
	TPG_MOVE_NONE,
	TPG_MOVE_POS_SLOW,
	TPG_MOVE_POS,
	TPG_MOVE_POS_FAST,
};

extern const char * const tpg_aspect_strings[];

#define TPG_MAX_PLANES 2
#define TPG_MAX_PAT_LINES 8

struct tpg_data {
	/* Source frame size */
	unsigned			src_width, src_height;
	/* Buffer height */
	unsigned			buf_height;
	/* Scaled output frame size */
	unsigned			scaled_width;
	u32				field;
	/* crop coordinates are frame-based */
	struct v4l2_rect		crop;
	/* compose coordinates are format-based */
	struct v4l2_rect		compose;
	/* border and square coordinates are frame-based */
	struct v4l2_rect		border;
	struct v4l2_rect		square;

	/* Color-related fields */
	enum tpg_quality		qual;
	unsigned			qual_offset;
	u8				alpha_component;
	bool				alpha_red_only;
	u8				brightness;
	u8				contrast;
	u8				saturation;
	s16				hue;
	u32				fourcc;
	bool				is_yuv;
	u32				colorspace;
	enum tpg_video_aspect		vid_aspect;
	enum tpg_pixel_aspect		pix_aspect;
	unsigned			rgb_range;
	unsigned			real_rgb_range;
	unsigned			planes;
	/* Used to store the colors in native format, either RGB or YUV */
	u8				colors[TPG_COLOR_MAX][3];
	u8				textfg[TPG_MAX_PLANES][8], textbg[TPG_MAX_PLANES][8];
	/* size in bytes for two pixels in each plane */
	unsigned			twopixelsize[TPG_MAX_PLANES];
	unsigned			bytesperline[TPG_MAX_PLANES];

	/* Configuration */
	enum tpg_pattern		pattern;
	bool				hflip;
	bool				vflip;
	unsigned			perc_fill;
	bool				perc_fill_blank;
	bool				show_border;
	bool				show_square;
	bool				insert_sav;
	bool				insert_eav;

	/* Test pattern movement */
	enum tpg_move_mode		mv_hor_mode;
	int				mv_hor_count;
	int				mv_hor_step;
	enum tpg_move_mode		mv_vert_mode;
	int				mv_vert_count;
	int				mv_vert_step;

	bool				recalc_colors;
	bool				recalc_lines;
	bool				recalc_square_border;

	/* Used to store TPG_MAX_PAT_LINES lines, each with up to two planes */
	unsigned			max_line_width;
	u8				*lines[TPG_MAX_PAT_LINES][TPG_MAX_PLANES];
	u8				*random_line[TPG_MAX_PLANES];
	u8				*contrast_line[TPG_MAX_PLANES];
	u8				*black_line[TPG_MAX_PLANES];
};

void tpg_init(struct tpg_data *tpg, unsigned w, unsigned h);
int tpg_alloc(struct tpg_data *tpg, unsigned max_w);
void tpg_free(struct tpg_data *tpg);
void tpg_reset_source(struct tpg_data *tpg, unsigned width, unsigned height,
		       u32 field);

void tpg_set_font(const u8 *f);
void tpg_gen_text(struct tpg_data *tpg,
		u8 *basep[TPG_MAX_PLANES][2], int y, int x, char *text);
void tpg_calc_text_basep(struct tpg_data *tpg,
		u8 *basep[TPG_MAX_PLANES][2], unsigned p, u8 *vbuf);
void tpg_fillbuffer(struct tpg_data *tpg, v4l2_std_id std, unsigned p, u8 *vbuf);
bool tpg_s_fourcc(struct tpg_data *tpg, u32 fourcc);
void tpg_s_crop_compose(struct tpg_data *tpg, const struct v4l2_rect *crop,
		const struct v4l2_rect *compose);

static inline void tpg_s_pattern(struct tpg_data *tpg, enum tpg_pattern pattern)
{
	if (tpg->pattern == pattern)
		return;
	tpg->pattern = pattern;
	tpg->recalc_colors = true;
}

static inline void tpg_s_quality(struct tpg_data *tpg,
				    enum tpg_quality qual, unsigned qual_offset)
{
	if (tpg->qual == qual && tpg->qual_offset == qual_offset)
		return;
	tpg->qual = qual;
	tpg->qual_offset = qual_offset;
	tpg->recalc_colors = true;
}

static inline enum tpg_quality tpg_g_quality(const struct tpg_data *tpg)
{
	return tpg->qual;
}

static inline void tpg_s_alpha_component(struct tpg_data *tpg,
					    u8 alpha_component)
{
	if (tpg->alpha_component == alpha_component)
		return;
	tpg->alpha_component = alpha_component;
	tpg->recalc_colors = true;
}

static inline void tpg_s_alpha_mode(struct tpg_data *tpg,
					    bool red_only)
{
	if (tpg->alpha_red_only == red_only)
		return;
	tpg->alpha_red_only = red_only;
	tpg->recalc_colors = true;
}

static inline void tpg_s_brightness(struct tpg_data *tpg,
					u8 brightness)
{
	if (tpg->brightness == brightness)
		return;
	tpg->brightness = brightness;
	tpg->recalc_colors = true;
}

static inline void tpg_s_contrast(struct tpg_data *tpg,
					u8 contrast)
{
	if (tpg->contrast == contrast)
		return;
	tpg->contrast = contrast;
	tpg->recalc_colors = true;
}

static inline void tpg_s_saturation(struct tpg_data *tpg,
					u8 saturation)
{
	if (tpg->saturation == saturation)
		return;
	tpg->saturation = saturation;
	tpg->recalc_colors = true;
}

static inline void tpg_s_hue(struct tpg_data *tpg,
					s16 hue)
{
	if (tpg->hue == hue)
		return;
	tpg->hue = hue;
	tpg->recalc_colors = true;
}

static inline void tpg_s_rgb_range(struct tpg_data *tpg,
					unsigned rgb_range)
{
	if (tpg->rgb_range == rgb_range)
		return;
	tpg->rgb_range = rgb_range;
	tpg->recalc_colors = true;
}

static inline void tpg_s_real_rgb_range(struct tpg_data *tpg,
					unsigned rgb_range)
{
	if (tpg->real_rgb_range == rgb_range)
		return;
	tpg->real_rgb_range = rgb_range;
	tpg->recalc_colors = true;
}

static inline void tpg_s_colorspace(struct tpg_data *tpg, u32 colorspace)
{
	if (tpg->colorspace == colorspace)
		return;
	tpg->colorspace = colorspace;
	tpg->recalc_colors = true;
}

static inline u32 tpg_g_colorspace(const struct tpg_data *tpg)
{
	return tpg->colorspace;
}

static inline unsigned tpg_g_planes(const struct tpg_data *tpg)
{
	return tpg->planes;
}

static inline unsigned tpg_g_twopixelsize(const struct tpg_data *tpg, unsigned plane)
{
	return tpg->twopixelsize[plane];
}

static inline unsigned tpg_g_bytesperline(const struct tpg_data *tpg, unsigned plane)
{
	return tpg->bytesperline[plane];
}

static inline void tpg_s_bytesperline(struct tpg_data *tpg, unsigned plane, unsigned bpl)
{
	tpg->bytesperline[plane] = bpl;
}

static inline void tpg_s_buf_height(struct tpg_data *tpg, unsigned h)
{
	tpg->buf_height = h;
}

static inline void tpg_s_field(struct tpg_data *tpg, unsigned field)
{
	tpg->field = field;
}

static inline void tpg_s_perc_fill(struct tpg_data *tpg,
				      unsigned perc_fill)
{
	tpg->perc_fill = perc_fill;
}

static inline unsigned tpg_g_perc_fill(const struct tpg_data *tpg)
{
	return tpg->perc_fill;
}

static inline void tpg_s_perc_fill_blank(struct tpg_data *tpg,
					 bool perc_fill_blank)
{
	tpg->perc_fill_blank = perc_fill_blank;
}

static inline void tpg_s_video_aspect(struct tpg_data *tpg,
					enum tpg_video_aspect vid_aspect)
{
	if (tpg->vid_aspect == vid_aspect)
		return;
	tpg->vid_aspect = vid_aspect;
	tpg->recalc_square_border = true;
}

static inline enum tpg_video_aspect tpg_g_video_aspect(const struct tpg_data *tpg)
{
	return tpg->vid_aspect;
}

static inline void tpg_s_pixel_aspect(struct tpg_data *tpg,
					enum tpg_pixel_aspect pix_aspect)
{
	if (tpg->pix_aspect == pix_aspect)
		return;
	tpg->pix_aspect = pix_aspect;
	tpg->recalc_square_border = true;
}

static inline void tpg_s_show_border(struct tpg_data *tpg,
					bool show_border)
{
	tpg->show_border = show_border;
}

static inline void tpg_s_show_square(struct tpg_data *tpg,
					bool show_square)
{
	tpg->show_square = show_square;
}

static inline void tpg_s_insert_sav(struct tpg_data *tpg, bool insert_sav)
{
	tpg->insert_sav = insert_sav;
}

static inline void tpg_s_insert_eav(struct tpg_data *tpg, bool insert_eav)
{
	tpg->insert_eav = insert_eav;
}

void tpg_update_mv_step(struct tpg_data *tpg);

static inline void tpg_s_mv_hor_mode(struct tpg_data *tpg,
				enum tpg_move_mode mv_hor_mode)
{
	tpg->mv_hor_mode = mv_hor_mode;
	tpg_update_mv_step(tpg);
}

static inline void tpg_s_mv_vert_mode(struct tpg_data *tpg,
				enum tpg_move_mode mv_vert_mode)
{
	tpg->mv_vert_mode = mv_vert_mode;
	tpg_update_mv_step(tpg);
}

static inline void tpg_init_mv_count(struct tpg_data *tpg)
{
	tpg->mv_hor_count = tpg->mv_vert_count = 0;
}

static inline void tpg_update_mv_count(struct tpg_data *tpg, bool frame_is_field)
{
	tpg->mv_hor_count += tpg->mv_hor_step * (frame_is_field ? 1 : 2);
	tpg->mv_vert_count += tpg->mv_vert_step * (frame_is_field ? 1 : 2);
}

static inline void tpg_s_hflip(struct tpg_data *tpg, bool hflip)
{
	if (tpg->hflip == hflip)
		return;
	tpg->hflip = hflip;
	tpg_update_mv_step(tpg);
	tpg->recalc_lines = true;
}

static inline bool tpg_g_hflip(const struct tpg_data *tpg)
{
	return tpg->hflip;
}

static inline void tpg_s_vflip(struct tpg_data *tpg, bool vflip)
{
	tpg->vflip = vflip;
}

static inline bool tpg_g_vflip(const struct tpg_data *tpg)
{
	return tpg->vflip;
}

static inline bool tpg_pattern_is_static(const struct tpg_data *tpg)
{
	return tpg->pattern != TPG_PAT_NOISE &&
	       tpg->mv_hor_mode == TPG_MOVE_NONE &&
	       tpg->mv_vert_mode == TPG_MOVE_NONE;
}

#endif
