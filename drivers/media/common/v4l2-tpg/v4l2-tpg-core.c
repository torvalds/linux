// SPDX-License-Identifier: GPL-2.0-only
/*
 * v4l2-tpg-core.c - Test Pattern Generator
 *
 * Note: gen_twopix and tpg_gen_text are based on code from vivi.c. See the
 * vivi.c source for the copyright information of those functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/module.h>
#include <media/tpg/v4l2-tpg.h>

/* Must remain in sync with enum tpg_pattern */
const char * const tpg_pattern_strings[] = {
	"75% Colorbar",
	"100% Colorbar",
	"CSC Colorbar",
	"Horizontal 100% Colorbar",
	"100% Color Squares",
	"100% Black",
	"100% White",
	"100% Red",
	"100% Green",
	"100% Blue",
	"16x16 Checkers",
	"2x2 Checkers",
	"1x1 Checkers",
	"2x2 Red/Green Checkers",
	"1x1 Red/Green Checkers",
	"Alternating Hor Lines",
	"Alternating Vert Lines",
	"One Pixel Wide Cross",
	"Two Pixels Wide Cross",
	"Ten Pixels Wide Cross",
	"Gray Ramp",
	"Noise",
	NULL
};
EXPORT_SYMBOL_GPL(tpg_pattern_strings);

/* Must remain in sync with enum tpg_aspect */
const char * const tpg_aspect_strings[] = {
	"Source Width x Height",
	"4x3",
	"14x9",
	"16x9",
	"16x9 Anamorphic",
	NULL
};
EXPORT_SYMBOL_GPL(tpg_aspect_strings);

/*
 * Sine table: sin[0] = 127 * sin(-180 degrees)
 *             sin[128] = 127 * sin(0 degrees)
 *             sin[256] = 127 * sin(180 degrees)
 */
static const s8 sin[257] = {
	   0,   -4,   -7,  -11,  -13,  -18,  -20,  -22,  -26,  -29,  -33,  -35,  -37,  -41,  -43,  -48,
	 -50,  -52,  -56,  -58,  -62,  -63,  -65,  -69,  -71,  -75,  -76,  -78,  -82,  -83,  -87,  -88,
	 -90,  -93,  -94,  -97,  -99, -101, -103, -104, -107, -108, -110, -111, -112, -114, -115, -117,
	-118, -119, -120, -121, -122, -123, -123, -124, -125, -125, -126, -126, -127, -127, -127, -127,
	-127, -127, -127, -127, -126, -126, -125, -125, -124, -124, -123, -122, -121, -120, -119, -118,
	-117, -116, -114, -113, -111, -110, -109, -107, -105, -103, -101, -100,  -97,  -96,  -93,  -91,
	 -90,  -87,  -85,  -82,  -80,  -76,  -75,  -73,  -69,  -67,  -63,  -62,  -60,  -56,  -54,  -50,
	 -48,  -46,  -41,  -39,  -35,  -33,  -31,  -26,  -24,  -20,  -18,  -15,  -11,   -9,   -4,   -2,
	   0,    2,    4,    9,   11,   15,   18,   20,   24,   26,   31,   33,   35,   39,   41,   46,
	  48,   50,   54,   56,   60,   62,   64,   67,   69,   73,   75,   76,   80,   82,   85,   87,
	  90,   91,   93,   96,   97,  100,  101,  103,  105,  107,  109,  110,  111,  113,  114,  116,
	 117,  118,  119,  120,  121,  122,  123,  124,  124,  125,  125,  126,  126,  127,  127,  127,
	 127,  127,  127,  127,  127,  126,  126,  125,  125,  124,  123,  123,  122,  121,  120,  119,
	 118,  117,  115,  114,  112,  111,  110,  108,  107,  104,  103,  101,   99,   97,   94,   93,
	  90,   88,   87,   83,   82,   78,   76,   75,   71,   69,   65,   64,   62,   58,   56,   52,
	  50,   48,   43,   41,   37,   35,   33,   29,   26,   22,   20,   18,   13,   11,    7,    4,
	   0,
};

#define cos(idx) sin[((idx) + 64) % sizeof(sin)]

/* Global font descriptor */
static const u8 *font8x16;

void tpg_set_font(const u8 *f)
{
	font8x16 = f;
}
EXPORT_SYMBOL_GPL(tpg_set_font);

void tpg_init(struct tpg_data *tpg, unsigned w, unsigned h)
{
	memset(tpg, 0, sizeof(*tpg));
	tpg->scaled_width = tpg->src_width = w;
	tpg->src_height = tpg->buf_height = h;
	tpg->crop.width = tpg->compose.width = w;
	tpg->crop.height = tpg->compose.height = h;
	tpg->recalc_colors = true;
	tpg->recalc_square_border = true;
	tpg->brightness = 128;
	tpg->contrast = 128;
	tpg->saturation = 128;
	tpg->hue = 0;
	tpg->mv_hor_mode = TPG_MOVE_NONE;
	tpg->mv_vert_mode = TPG_MOVE_NONE;
	tpg->field = V4L2_FIELD_NONE;
	tpg_s_fourcc(tpg, V4L2_PIX_FMT_RGB24);
	tpg->colorspace = V4L2_COLORSPACE_SRGB;
	tpg->perc_fill = 100;
	tpg->hsv_enc = V4L2_HSV_ENC_180;
}
EXPORT_SYMBOL_GPL(tpg_init);

int tpg_alloc(struct tpg_data *tpg, unsigned max_w)
{
	unsigned pat;
	unsigned plane;
	int ret = 0;

	tpg->max_line_width = max_w;
	for (pat = 0; pat < TPG_MAX_PAT_LINES; pat++) {
		for (plane = 0; plane < TPG_MAX_PLANES; plane++) {
			unsigned pixelsz = plane ? 2 : 4;

			tpg->lines[pat][plane] =
				vzalloc(array3_size(max_w, 2, pixelsz));
			if (!tpg->lines[pat][plane]) {
				ret = -ENOMEM;
				goto free_lines;
			}
			if (plane == 0)
				continue;
			tpg->downsampled_lines[pat][plane] =
				vzalloc(array3_size(max_w, 2, pixelsz));
			if (!tpg->downsampled_lines[pat][plane]) {
				ret = -ENOMEM;
				goto free_lines;
			}
		}
	}
	for (plane = 0; plane < TPG_MAX_PLANES; plane++) {
		unsigned pixelsz = plane ? 2 : 4;

		tpg->contrast_line[plane] =
			vzalloc(array_size(pixelsz, max_w));
		if (!tpg->contrast_line[plane]) {
			ret = -ENOMEM;
			goto free_contrast_line;
		}
		tpg->black_line[plane] =
			vzalloc(array_size(pixelsz, max_w));
		if (!tpg->black_line[plane]) {
			ret = -ENOMEM;
			goto free_contrast_line;
		}
		tpg->random_line[plane] =
			vzalloc(array3_size(max_w, 2, pixelsz));
		if (!tpg->random_line[plane]) {
			ret = -ENOMEM;
			goto free_contrast_line;
		}
	}
	return 0;

free_contrast_line:
	for (plane = 0; plane < TPG_MAX_PLANES; plane++) {
		vfree(tpg->contrast_line[plane]);
		vfree(tpg->black_line[plane]);
		vfree(tpg->random_line[plane]);
		tpg->contrast_line[plane] = NULL;
		tpg->black_line[plane] = NULL;
		tpg->random_line[plane] = NULL;
	}
free_lines:
	for (pat = 0; pat < TPG_MAX_PAT_LINES; pat++)
		for (plane = 0; plane < TPG_MAX_PLANES; plane++) {
			vfree(tpg->lines[pat][plane]);
			tpg->lines[pat][plane] = NULL;
			if (plane == 0)
				continue;
			vfree(tpg->downsampled_lines[pat][plane]);
			tpg->downsampled_lines[pat][plane] = NULL;
		}
	return ret;
}
EXPORT_SYMBOL_GPL(tpg_alloc);

void tpg_free(struct tpg_data *tpg)
{
	unsigned pat;
	unsigned plane;

	for (pat = 0; pat < TPG_MAX_PAT_LINES; pat++)
		for (plane = 0; plane < TPG_MAX_PLANES; plane++) {
			vfree(tpg->lines[pat][plane]);
			tpg->lines[pat][plane] = NULL;
			if (plane == 0)
				continue;
			vfree(tpg->downsampled_lines[pat][plane]);
			tpg->downsampled_lines[pat][plane] = NULL;
		}
	for (plane = 0; plane < TPG_MAX_PLANES; plane++) {
		vfree(tpg->contrast_line[plane]);
		vfree(tpg->black_line[plane]);
		vfree(tpg->random_line[plane]);
		tpg->contrast_line[plane] = NULL;
		tpg->black_line[plane] = NULL;
		tpg->random_line[plane] = NULL;
	}
}
EXPORT_SYMBOL_GPL(tpg_free);

bool tpg_s_fourcc(struct tpg_data *tpg, u32 fourcc)
{
	tpg->fourcc = fourcc;
	tpg->planes = 1;
	tpg->buffers = 1;
	tpg->recalc_colors = true;
	tpg->interleaved = false;
	tpg->vdownsampling[0] = 1;
	tpg->hdownsampling[0] = 1;
	tpg->hmask[0] = ~0;
	tpg->hmask[1] = ~0;
	tpg->hmask[2] = ~0;

	switch (fourcc) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
	case V4L2_PIX_FMT_SBGGR16:
	case V4L2_PIX_FMT_SGBRG16:
	case V4L2_PIX_FMT_SGRBG16:
	case V4L2_PIX_FMT_SRGGB16:
		tpg->interleaved = true;
		tpg->vdownsampling[1] = 1;
		tpg->hdownsampling[1] = 1;
		tpg->planes = 2;
		fallthrough;
	case V4L2_PIX_FMT_RGB332:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB565X:
	case V4L2_PIX_FMT_RGB444:
	case V4L2_PIX_FMT_XRGB444:
	case V4L2_PIX_FMT_ARGB444:
	case V4L2_PIX_FMT_RGBX444:
	case V4L2_PIX_FMT_RGBA444:
	case V4L2_PIX_FMT_XBGR444:
	case V4L2_PIX_FMT_ABGR444:
	case V4L2_PIX_FMT_BGRX444:
	case V4L2_PIX_FMT_BGRA444:
	case V4L2_PIX_FMT_RGB555:
	case V4L2_PIX_FMT_XRGB555:
	case V4L2_PIX_FMT_ARGB555:
	case V4L2_PIX_FMT_RGBX555:
	case V4L2_PIX_FMT_RGBA555:
	case V4L2_PIX_FMT_XBGR555:
	case V4L2_PIX_FMT_ABGR555:
	case V4L2_PIX_FMT_BGRX555:
	case V4L2_PIX_FMT_BGRA555:
	case V4L2_PIX_FMT_RGB555X:
	case V4L2_PIX_FMT_XRGB555X:
	case V4L2_PIX_FMT_ARGB555X:
	case V4L2_PIX_FMT_BGR666:
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_BGR32:
	case V4L2_PIX_FMT_XRGB32:
	case V4L2_PIX_FMT_XBGR32:
	case V4L2_PIX_FMT_ARGB32:
	case V4L2_PIX_FMT_ABGR32:
	case V4L2_PIX_FMT_RGBX32:
	case V4L2_PIX_FMT_BGRX32:
	case V4L2_PIX_FMT_RGBA32:
	case V4L2_PIX_FMT_BGRA32:
		tpg->color_enc = TGP_COLOR_ENC_RGB;
		break;
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_Y10:
	case V4L2_PIX_FMT_Y12:
	case V4L2_PIX_FMT_Y16:
	case V4L2_PIX_FMT_Y16_BE:
	case V4L2_PIX_FMT_Z16:
		tpg->color_enc = TGP_COLOR_ENC_LUMA;
		break;
	case V4L2_PIX_FMT_YUV444:
	case V4L2_PIX_FMT_YUV555:
	case V4L2_PIX_FMT_YUV565:
	case V4L2_PIX_FMT_YUV32:
	case V4L2_PIX_FMT_AYUV32:
	case V4L2_PIX_FMT_XYUV32:
	case V4L2_PIX_FMT_VUYA32:
	case V4L2_PIX_FMT_VUYX32:
	case V4L2_PIX_FMT_YUVA32:
	case V4L2_PIX_FMT_YUVX32:
		tpg->color_enc = TGP_COLOR_ENC_YCBCR;
		break;
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YVU420M:
		tpg->buffers = 3;
		fallthrough;
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		tpg->vdownsampling[1] = 2;
		tpg->vdownsampling[2] = 2;
		tpg->hdownsampling[1] = 2;
		tpg->hdownsampling[2] = 2;
		tpg->planes = 3;
		tpg->color_enc = TGP_COLOR_ENC_YCBCR;
		break;
	case V4L2_PIX_FMT_YUV422M:
	case V4L2_PIX_FMT_YVU422M:
		tpg->buffers = 3;
		fallthrough;
	case V4L2_PIX_FMT_YUV422P:
		tpg->vdownsampling[1] = 1;
		tpg->vdownsampling[2] = 1;
		tpg->hdownsampling[1] = 2;
		tpg->hdownsampling[2] = 2;
		tpg->planes = 3;
		tpg->color_enc = TGP_COLOR_ENC_YCBCR;
		break;
	case V4L2_PIX_FMT_NV16M:
	case V4L2_PIX_FMT_NV61M:
		tpg->buffers = 2;
		fallthrough;
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		tpg->vdownsampling[1] = 1;
		tpg->hdownsampling[1] = 1;
		tpg->hmask[1] = ~1;
		tpg->planes = 2;
		tpg->color_enc = TGP_COLOR_ENC_YCBCR;
		break;
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV21M:
		tpg->buffers = 2;
		fallthrough;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		tpg->vdownsampling[1] = 2;
		tpg->hdownsampling[1] = 1;
		tpg->hmask[1] = ~1;
		tpg->planes = 2;
		tpg->color_enc = TGP_COLOR_ENC_YCBCR;
		break;
	case V4L2_PIX_FMT_YUV444M:
	case V4L2_PIX_FMT_YVU444M:
		tpg->buffers = 3;
		tpg->planes = 3;
		tpg->vdownsampling[1] = 1;
		tpg->vdownsampling[2] = 1;
		tpg->hdownsampling[1] = 1;
		tpg->hdownsampling[2] = 1;
		tpg->color_enc = TGP_COLOR_ENC_YCBCR;
		break;
	case V4L2_PIX_FMT_NV24:
	case V4L2_PIX_FMT_NV42:
		tpg->vdownsampling[1] = 1;
		tpg->hdownsampling[1] = 1;
		tpg->planes = 2;
		tpg->color_enc = TGP_COLOR_ENC_YCBCR;
		break;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_VYUY:
		tpg->hmask[0] = ~1;
		tpg->color_enc = TGP_COLOR_ENC_YCBCR;
		break;
	case V4L2_PIX_FMT_HSV24:
	case V4L2_PIX_FMT_HSV32:
		tpg->color_enc = TGP_COLOR_ENC_HSV;
		break;
	default:
		return false;
	}

	switch (fourcc) {
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_RGB332:
		tpg->twopixelsize[0] = 2;
		break;
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB565X:
	case V4L2_PIX_FMT_RGB444:
	case V4L2_PIX_FMT_XRGB444:
	case V4L2_PIX_FMT_ARGB444:
	case V4L2_PIX_FMT_RGBX444:
	case V4L2_PIX_FMT_RGBA444:
	case V4L2_PIX_FMT_XBGR444:
	case V4L2_PIX_FMT_ABGR444:
	case V4L2_PIX_FMT_BGRX444:
	case V4L2_PIX_FMT_BGRA444:
	case V4L2_PIX_FMT_RGB555:
	case V4L2_PIX_FMT_XRGB555:
	case V4L2_PIX_FMT_ARGB555:
	case V4L2_PIX_FMT_RGBX555:
	case V4L2_PIX_FMT_RGBA555:
	case V4L2_PIX_FMT_XBGR555:
	case V4L2_PIX_FMT_ABGR555:
	case V4L2_PIX_FMT_BGRX555:
	case V4L2_PIX_FMT_BGRA555:
	case V4L2_PIX_FMT_RGB555X:
	case V4L2_PIX_FMT_XRGB555X:
	case V4L2_PIX_FMT_ARGB555X:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUV444:
	case V4L2_PIX_FMT_YUV555:
	case V4L2_PIX_FMT_YUV565:
	case V4L2_PIX_FMT_Y10:
	case V4L2_PIX_FMT_Y12:
	case V4L2_PIX_FMT_Y16:
	case V4L2_PIX_FMT_Y16_BE:
	case V4L2_PIX_FMT_Z16:
		tpg->twopixelsize[0] = 2 * 2;
		break;
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_HSV24:
		tpg->twopixelsize[0] = 2 * 3;
		break;
	case V4L2_PIX_FMT_BGR666:
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_BGR32:
	case V4L2_PIX_FMT_XRGB32:
	case V4L2_PIX_FMT_XBGR32:
	case V4L2_PIX_FMT_ARGB32:
	case V4L2_PIX_FMT_ABGR32:
	case V4L2_PIX_FMT_RGBX32:
	case V4L2_PIX_FMT_BGRX32:
	case V4L2_PIX_FMT_RGBA32:
	case V4L2_PIX_FMT_BGRA32:
	case V4L2_PIX_FMT_YUV32:
	case V4L2_PIX_FMT_AYUV32:
	case V4L2_PIX_FMT_XYUV32:
	case V4L2_PIX_FMT_VUYA32:
	case V4L2_PIX_FMT_VUYX32:
	case V4L2_PIX_FMT_YUVA32:
	case V4L2_PIX_FMT_YUVX32:
	case V4L2_PIX_FMT_HSV32:
		tpg->twopixelsize[0] = 2 * 4;
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV21M:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_NV16M:
	case V4L2_PIX_FMT_NV61M:
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
		tpg->twopixelsize[0] = 2;
		tpg->twopixelsize[1] = 2;
		break;
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SRGGB12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SRGGB16:
	case V4L2_PIX_FMT_SGRBG16:
	case V4L2_PIX_FMT_SGBRG16:
	case V4L2_PIX_FMT_SBGGR16:
		tpg->twopixelsize[0] = 4;
		tpg->twopixelsize[1] = 4;
		break;
	case V4L2_PIX_FMT_YUV444M:
	case V4L2_PIX_FMT_YVU444M:
	case V4L2_PIX_FMT_YUV422M:
	case V4L2_PIX_FMT_YVU422M:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YVU420M:
		tpg->twopixelsize[0] = 2;
		tpg->twopixelsize[1] = 2;
		tpg->twopixelsize[2] = 2;
		break;
	case V4L2_PIX_FMT_NV24:
	case V4L2_PIX_FMT_NV42:
		tpg->twopixelsize[0] = 2;
		tpg->twopixelsize[1] = 4;
		break;
	}
	return true;
}
EXPORT_SYMBOL_GPL(tpg_s_fourcc);

void tpg_s_crop_compose(struct tpg_data *tpg, const struct v4l2_rect *crop,
		const struct v4l2_rect *compose)
{
	tpg->crop = *crop;
	tpg->compose = *compose;
	tpg->scaled_width = (tpg->src_width * tpg->compose.width +
				 tpg->crop.width - 1) / tpg->crop.width;
	tpg->scaled_width &= ~1;
	if (tpg->scaled_width > tpg->max_line_width)
		tpg->scaled_width = tpg->max_line_width;
	if (tpg->scaled_width < 2)
		tpg->scaled_width = 2;
	tpg->recalc_lines = true;
}
EXPORT_SYMBOL_GPL(tpg_s_crop_compose);

void tpg_reset_source(struct tpg_data *tpg, unsigned width, unsigned height,
		       u32 field)
{
	unsigned p;

	tpg->src_width = width;
	tpg->src_height = height;
	tpg->field = field;
	tpg->buf_height = height;
	if (V4L2_FIELD_HAS_T_OR_B(field))
		tpg->buf_height /= 2;
	tpg->scaled_width = width;
	tpg->crop.top = tpg->crop.left = 0;
	tpg->crop.width = width;
	tpg->crop.height = height;
	tpg->compose.top = tpg->compose.left = 0;
	tpg->compose.width = width;
	tpg->compose.height = tpg->buf_height;
	for (p = 0; p < tpg->planes; p++)
		tpg->bytesperline[p] = (width * tpg->twopixelsize[p]) /
				       (2 * tpg->hdownsampling[p]);
	tpg->recalc_square_border = true;
}
EXPORT_SYMBOL_GPL(tpg_reset_source);

static enum tpg_color tpg_get_textbg_color(struct tpg_data *tpg)
{
	switch (tpg->pattern) {
	case TPG_PAT_BLACK:
		return TPG_COLOR_100_WHITE;
	case TPG_PAT_CSC_COLORBAR:
		return TPG_COLOR_CSC_BLACK;
	default:
		return TPG_COLOR_100_BLACK;
	}
}

static enum tpg_color tpg_get_textfg_color(struct tpg_data *tpg)
{
	switch (tpg->pattern) {
	case TPG_PAT_75_COLORBAR:
	case TPG_PAT_CSC_COLORBAR:
		return TPG_COLOR_CSC_WHITE;
	case TPG_PAT_BLACK:
		return TPG_COLOR_100_BLACK;
	default:
		return TPG_COLOR_100_WHITE;
	}
}

static inline int rec709_to_linear(int v)
{
	v = clamp(v, 0, 0xff0);
	return tpg_rec709_to_linear[v];
}

static inline int linear_to_rec709(int v)
{
	v = clamp(v, 0, 0xff0);
	return tpg_linear_to_rec709[v];
}

static void color_to_hsv(struct tpg_data *tpg, int r, int g, int b,
			   int *h, int *s, int *v)
{
	int max_rgb, min_rgb, diff_rgb;
	int aux;
	int third;
	int third_size;

	r >>= 4;
	g >>= 4;
	b >>= 4;

	/* Value */
	max_rgb = max3(r, g, b);
	*v = max_rgb;
	if (!max_rgb) {
		*h = 0;
		*s = 0;
		return;
	}

	/* Saturation */
	min_rgb = min3(r, g, b);
	diff_rgb = max_rgb - min_rgb;
	aux = 255 * diff_rgb;
	aux += max_rgb / 2;
	aux /= max_rgb;
	*s = aux;
	if (!aux) {
		*h = 0;
		return;
	}

	third_size = (tpg->real_hsv_enc == V4L2_HSV_ENC_180) ? 60 : 85;

	/* Hue */
	if (max_rgb == r) {
		aux =  g - b;
		third = 0;
	} else if (max_rgb == g) {
		aux =  b - r;
		third = third_size;
	} else {
		aux =  r - g;
		third = third_size * 2;
	}

	aux *= third_size / 2;
	aux += diff_rgb / 2;
	aux /= diff_rgb;
	aux += third;

	/* Clamp Hue */
	if (tpg->real_hsv_enc == V4L2_HSV_ENC_180) {
		if (aux < 0)
			aux += 180;
		else if (aux > 180)
			aux -= 180;
	} else {
		aux = aux & 0xff;
	}

	*h = aux;
}

static void rgb2ycbcr(const int m[3][3], int r, int g, int b,
			int y_offset, int *y, int *cb, int *cr)
{
	*y  = ((m[0][0] * r + m[0][1] * g + m[0][2] * b) >> 16) + (y_offset << 4);
	*cb = ((m[1][0] * r + m[1][1] * g + m[1][2] * b) >> 16) + (128 << 4);
	*cr = ((m[2][0] * r + m[2][1] * g + m[2][2] * b) >> 16) + (128 << 4);
}

static void color_to_ycbcr(struct tpg_data *tpg, int r, int g, int b,
			   int *y, int *cb, int *cr)
{
#define COEFF(v, r) ((int)(0.5 + (v) * (r) * 256.0))

	static const int bt601[3][3] = {
		{ COEFF(0.299, 219),   COEFF(0.587, 219),   COEFF(0.114, 219)   },
		{ COEFF(-0.1687, 224), COEFF(-0.3313, 224), COEFF(0.5, 224)     },
		{ COEFF(0.5, 224),     COEFF(-0.4187, 224), COEFF(-0.0813, 224) },
	};
	static const int bt601_full[3][3] = {
		{ COEFF(0.299, 255),   COEFF(0.587, 255),   COEFF(0.114, 255)   },
		{ COEFF(-0.1687, 255), COEFF(-0.3313, 255), COEFF(0.5, 255)     },
		{ COEFF(0.5, 255),     COEFF(-0.4187, 255), COEFF(-0.0813, 255) },
	};
	static const int rec709[3][3] = {
		{ COEFF(0.2126, 219),  COEFF(0.7152, 219),  COEFF(0.0722, 219)  },
		{ COEFF(-0.1146, 224), COEFF(-0.3854, 224), COEFF(0.5, 224)     },
		{ COEFF(0.5, 224),     COEFF(-0.4542, 224), COEFF(-0.0458, 224) },
	};
	static const int rec709_full[3][3] = {
		{ COEFF(0.2126, 255),  COEFF(0.7152, 255),  COEFF(0.0722, 255)  },
		{ COEFF(-0.1146, 255), COEFF(-0.3854, 255), COEFF(0.5, 255)     },
		{ COEFF(0.5, 255),     COEFF(-0.4542, 255), COEFF(-0.0458, 255) },
	};
	static const int smpte240m[3][3] = {
		{ COEFF(0.212, 219),  COEFF(0.701, 219),  COEFF(0.087, 219)  },
		{ COEFF(-0.116, 224), COEFF(-0.384, 224), COEFF(0.5, 224)    },
		{ COEFF(0.5, 224),    COEFF(-0.445, 224), COEFF(-0.055, 224) },
	};
	static const int smpte240m_full[3][3] = {
		{ COEFF(0.212, 255),  COEFF(0.701, 255),  COEFF(0.087, 255)  },
		{ COEFF(-0.116, 255), COEFF(-0.384, 255), COEFF(0.5, 255)    },
		{ COEFF(0.5, 255),    COEFF(-0.445, 255), COEFF(-0.055, 255) },
	};
	static const int bt2020[3][3] = {
		{ COEFF(0.2627, 219),  COEFF(0.6780, 219),  COEFF(0.0593, 219)  },
		{ COEFF(-0.1396, 224), COEFF(-0.3604, 224), COEFF(0.5, 224)     },
		{ COEFF(0.5, 224),     COEFF(-0.4598, 224), COEFF(-0.0402, 224) },
	};
	static const int bt2020_full[3][3] = {
		{ COEFF(0.2627, 255),  COEFF(0.6780, 255),  COEFF(0.0593, 255)  },
		{ COEFF(-0.1396, 255), COEFF(-0.3604, 255), COEFF(0.5, 255)     },
		{ COEFF(0.5, 255),     COEFF(-0.4598, 255), COEFF(-0.0402, 255) },
	};
	static const int bt2020c[4] = {
		COEFF(1.0 / 1.9404, 224), COEFF(1.0 / 1.5816, 224),
		COEFF(1.0 / 1.7184, 224), COEFF(1.0 / 0.9936, 224),
	};
	static const int bt2020c_full[4] = {
		COEFF(1.0 / 1.9404, 255), COEFF(1.0 / 1.5816, 255),
		COEFF(1.0 / 1.7184, 255), COEFF(1.0 / 0.9936, 255),
	};

	bool full = tpg->real_quantization == V4L2_QUANTIZATION_FULL_RANGE;
	unsigned y_offset = full ? 0 : 16;
	int lin_y, yc;

	switch (tpg->real_ycbcr_enc) {
	case V4L2_YCBCR_ENC_601:
		rgb2ycbcr(full ? bt601_full : bt601, r, g, b, y_offset, y, cb, cr);
		break;
	case V4L2_YCBCR_ENC_XV601:
		/* Ignore quantization range, there is only one possible
		 * Y'CbCr encoding. */
		rgb2ycbcr(bt601, r, g, b, 16, y, cb, cr);
		break;
	case V4L2_YCBCR_ENC_XV709:
		/* Ignore quantization range, there is only one possible
		 * Y'CbCr encoding. */
		rgb2ycbcr(rec709, r, g, b, 16, y, cb, cr);
		break;
	case V4L2_YCBCR_ENC_BT2020:
		rgb2ycbcr(full ? bt2020_full : bt2020, r, g, b, y_offset, y, cb, cr);
		break;
	case V4L2_YCBCR_ENC_BT2020_CONST_LUM:
		lin_y = (COEFF(0.2627, 255) * rec709_to_linear(r) +
			 COEFF(0.6780, 255) * rec709_to_linear(g) +
			 COEFF(0.0593, 255) * rec709_to_linear(b)) >> 16;
		yc = linear_to_rec709(lin_y);
		*y = full ? yc : (yc * 219) / 255 + (16 << 4);
		if (b <= yc)
			*cb = (((b - yc) * (full ? bt2020c_full[0] : bt2020c[0])) >> 16) + (128 << 4);
		else
			*cb = (((b - yc) * (full ? bt2020c_full[1] : bt2020c[1])) >> 16) + (128 << 4);
		if (r <= yc)
			*cr = (((r - yc) * (full ? bt2020c_full[2] : bt2020c[2])) >> 16) + (128 << 4);
		else
			*cr = (((r - yc) * (full ? bt2020c_full[3] : bt2020c[3])) >> 16) + (128 << 4);
		break;
	case V4L2_YCBCR_ENC_SMPTE240M:
		rgb2ycbcr(full ? smpte240m_full : smpte240m, r, g, b, y_offset, y, cb, cr);
		break;
	case V4L2_YCBCR_ENC_709:
	default:
		rgb2ycbcr(full ? rec709_full : rec709, r, g, b, y_offset, y, cb, cr);
		break;
	}
}

static void ycbcr2rgb(const int m[3][3], int y, int cb, int cr,
			int y_offset, int *r, int *g, int *b)
{
	y -= y_offset << 4;
	cb -= 128 << 4;
	cr -= 128 << 4;
	*r = m[0][0] * y + m[0][1] * cb + m[0][2] * cr;
	*g = m[1][0] * y + m[1][1] * cb + m[1][2] * cr;
	*b = m[2][0] * y + m[2][1] * cb + m[2][2] * cr;
	*r = clamp(*r >> 12, 0, 0xff0);
	*g = clamp(*g >> 12, 0, 0xff0);
	*b = clamp(*b >> 12, 0, 0xff0);
}

static void ycbcr_to_color(struct tpg_data *tpg, int y, int cb, int cr,
			   int *r, int *g, int *b)
{
#undef COEFF
#define COEFF(v, r) ((int)(0.5 + (v) * ((255.0 * 255.0 * 16.0) / (r))))
	static const int bt601[3][3] = {
		{ COEFF(1, 219), COEFF(0, 224),       COEFF(1.4020, 224)  },
		{ COEFF(1, 219), COEFF(-0.3441, 224), COEFF(-0.7141, 224) },
		{ COEFF(1, 219), COEFF(1.7720, 224),  COEFF(0, 224)       },
	};
	static const int bt601_full[3][3] = {
		{ COEFF(1, 255), COEFF(0, 255),       COEFF(1.4020, 255)  },
		{ COEFF(1, 255), COEFF(-0.3441, 255), COEFF(-0.7141, 255) },
		{ COEFF(1, 255), COEFF(1.7720, 255),  COEFF(0, 255)       },
	};
	static const int rec709[3][3] = {
		{ COEFF(1, 219), COEFF(0, 224),       COEFF(1.5748, 224)  },
		{ COEFF(1, 219), COEFF(-0.1873, 224), COEFF(-0.4681, 224) },
		{ COEFF(1, 219), COEFF(1.8556, 224),  COEFF(0, 224)       },
	};
	static const int rec709_full[3][3] = {
		{ COEFF(1, 255), COEFF(0, 255),       COEFF(1.5748, 255)  },
		{ COEFF(1, 255), COEFF(-0.1873, 255), COEFF(-0.4681, 255) },
		{ COEFF(1, 255), COEFF(1.8556, 255),  COEFF(0, 255)       },
	};
	static const int smpte240m[3][3] = {
		{ COEFF(1, 219), COEFF(0, 224),       COEFF(1.5756, 224)  },
		{ COEFF(1, 219), COEFF(-0.2253, 224), COEFF(-0.4767, 224) },
		{ COEFF(1, 219), COEFF(1.8270, 224),  COEFF(0, 224)       },
	};
	static const int smpte240m_full[3][3] = {
		{ COEFF(1, 255), COEFF(0, 255),       COEFF(1.5756, 255)  },
		{ COEFF(1, 255), COEFF(-0.2253, 255), COEFF(-0.4767, 255) },
		{ COEFF(1, 255), COEFF(1.8270, 255),  COEFF(0, 255)       },
	};
	static const int bt2020[3][3] = {
		{ COEFF(1, 219), COEFF(0, 224),       COEFF(1.4746, 224)  },
		{ COEFF(1, 219), COEFF(-0.1646, 224), COEFF(-0.5714, 224) },
		{ COEFF(1, 219), COEFF(1.8814, 224),  COEFF(0, 224)       },
	};
	static const int bt2020_full[3][3] = {
		{ COEFF(1, 255), COEFF(0, 255),       COEFF(1.4746, 255)  },
		{ COEFF(1, 255), COEFF(-0.1646, 255), COEFF(-0.5714, 255) },
		{ COEFF(1, 255), COEFF(1.8814, 255),  COEFF(0, 255)       },
	};
	static const int bt2020c[4] = {
		COEFF(1.9404, 224), COEFF(1.5816, 224),
		COEFF(1.7184, 224), COEFF(0.9936, 224),
	};
	static const int bt2020c_full[4] = {
		COEFF(1.9404, 255), COEFF(1.5816, 255),
		COEFF(1.7184, 255), COEFF(0.9936, 255),
	};

	bool full = tpg->real_quantization == V4L2_QUANTIZATION_FULL_RANGE;
	unsigned y_offset = full ? 0 : 16;
	int y_fac = full ? COEFF(1.0, 255) : COEFF(1.0, 219);
	int lin_r, lin_g, lin_b, lin_y;

	switch (tpg->real_ycbcr_enc) {
	case V4L2_YCBCR_ENC_601:
		ycbcr2rgb(full ? bt601_full : bt601, y, cb, cr, y_offset, r, g, b);
		break;
	case V4L2_YCBCR_ENC_XV601:
		/* Ignore quantization range, there is only one possible
		 * Y'CbCr encoding. */
		ycbcr2rgb(bt601, y, cb, cr, 16, r, g, b);
		break;
	case V4L2_YCBCR_ENC_XV709:
		/* Ignore quantization range, there is only one possible
		 * Y'CbCr encoding. */
		ycbcr2rgb(rec709, y, cb, cr, 16, r, g, b);
		break;
	case V4L2_YCBCR_ENC_BT2020:
		ycbcr2rgb(full ? bt2020_full : bt2020, y, cb, cr, y_offset, r, g, b);
		break;
	case V4L2_YCBCR_ENC_BT2020_CONST_LUM:
		y -= full ? 0 : 16 << 4;
		cb -= 128 << 4;
		cr -= 128 << 4;

		if (cb <= 0)
			*b = y_fac * y + (full ? bt2020c_full[0] : bt2020c[0]) * cb;
		else
			*b = y_fac * y + (full ? bt2020c_full[1] : bt2020c[1]) * cb;
		*b = *b >> 12;
		if (cr <= 0)
			*r = y_fac * y + (full ? bt2020c_full[2] : bt2020c[2]) * cr;
		else
			*r = y_fac * y + (full ? bt2020c_full[3] : bt2020c[3]) * cr;
		*r = *r >> 12;
		lin_r = rec709_to_linear(*r);
		lin_b = rec709_to_linear(*b);
		lin_y = rec709_to_linear((y * 255) / (full ? 255 : 219));

		lin_g = COEFF(1.0 / 0.6780, 255) * lin_y -
			COEFF(0.2627 / 0.6780, 255) * lin_r -
			COEFF(0.0593 / 0.6780, 255) * lin_b;
		*g = linear_to_rec709(lin_g >> 12);
		break;
	case V4L2_YCBCR_ENC_SMPTE240M:
		ycbcr2rgb(full ? smpte240m_full : smpte240m, y, cb, cr, y_offset, r, g, b);
		break;
	case V4L2_YCBCR_ENC_709:
	default:
		ycbcr2rgb(full ? rec709_full : rec709, y, cb, cr, y_offset, r, g, b);
		break;
	}
}

/* precalculate color bar values to speed up rendering */
static void precalculate_color(struct tpg_data *tpg, int k)
{
	int col = k;
	int r = tpg_colors[col].r;
	int g = tpg_colors[col].g;
	int b = tpg_colors[col].b;
	int y, cb, cr;
	bool ycbcr_valid = false;

	if (k == TPG_COLOR_TEXTBG) {
		col = tpg_get_textbg_color(tpg);

		r = tpg_colors[col].r;
		g = tpg_colors[col].g;
		b = tpg_colors[col].b;
	} else if (k == TPG_COLOR_TEXTFG) {
		col = tpg_get_textfg_color(tpg);

		r = tpg_colors[col].r;
		g = tpg_colors[col].g;
		b = tpg_colors[col].b;
	} else if (tpg->pattern == TPG_PAT_NOISE) {
		r = g = b = get_random_u8();
	} else if (k == TPG_COLOR_RANDOM) {
		r = g = b = tpg->qual_offset + get_random_u32_below(196);
	} else if (k >= TPG_COLOR_RAMP) {
		r = g = b = k - TPG_COLOR_RAMP;
	}

	if (tpg->pattern == TPG_PAT_CSC_COLORBAR && col <= TPG_COLOR_CSC_BLACK) {
		r = tpg_csc_colors[tpg->colorspace][tpg->real_xfer_func][col].r;
		g = tpg_csc_colors[tpg->colorspace][tpg->real_xfer_func][col].g;
		b = tpg_csc_colors[tpg->colorspace][tpg->real_xfer_func][col].b;
	} else {
		r <<= 4;
		g <<= 4;
		b <<= 4;
	}

	if (tpg->qual == TPG_QUAL_GRAY ||
	    tpg->color_enc ==  TGP_COLOR_ENC_LUMA) {
		/* Rec. 709 Luma function */
		/* (0.2126, 0.7152, 0.0722) * (255 * 256) */
		r = g = b = (13879 * r + 46688 * g + 4713 * b) >> 16;
	}

	/*
	 * The assumption is that the RGB output is always full range,
	 * so only if the rgb_range overrides the 'real' rgb range do
	 * we need to convert the RGB values.
	 *
	 * Remember that r, g and b are still in the 0 - 0xff0 range.
	 */
	if (tpg->real_rgb_range == V4L2_DV_RGB_RANGE_LIMITED &&
	    tpg->rgb_range == V4L2_DV_RGB_RANGE_FULL &&
	    tpg->color_enc == TGP_COLOR_ENC_RGB) {
		/*
		 * Convert from full range (which is what r, g and b are)
		 * to limited range (which is the 'real' RGB range), which
		 * is then interpreted as full range.
		 */
		r = (r * 219) / 255 + (16 << 4);
		g = (g * 219) / 255 + (16 << 4);
		b = (b * 219) / 255 + (16 << 4);
	} else if (tpg->real_rgb_range != V4L2_DV_RGB_RANGE_LIMITED &&
		   tpg->rgb_range == V4L2_DV_RGB_RANGE_LIMITED &&
		   tpg->color_enc == TGP_COLOR_ENC_RGB) {

		/*
		 * Clamp r, g and b to the limited range and convert to full
		 * range since that's what we deliver.
		 */
		r = clamp(r, 16 << 4, 235 << 4);
		g = clamp(g, 16 << 4, 235 << 4);
		b = clamp(b, 16 << 4, 235 << 4);
		r = (r - (16 << 4)) * 255 / 219;
		g = (g - (16 << 4)) * 255 / 219;
		b = (b - (16 << 4)) * 255 / 219;
	}

	if ((tpg->brightness != 128 || tpg->contrast != 128 ||
	     tpg->saturation != 128 || tpg->hue) &&
	    tpg->color_enc != TGP_COLOR_ENC_LUMA) {
		/* Implement these operations */
		int tmp_cb, tmp_cr;

		/* First convert to YCbCr */

		color_to_ycbcr(tpg, r, g, b, &y, &cb, &cr);

		y = (16 << 4) + ((y - (16 << 4)) * tpg->contrast) / 128;
		y += (tpg->brightness << 4) - (128 << 4);

		cb -= 128 << 4;
		cr -= 128 << 4;
		tmp_cb = (cb * cos(128 + tpg->hue)) / 127 + (cr * sin[128 + tpg->hue]) / 127;
		tmp_cr = (cr * cos(128 + tpg->hue)) / 127 - (cb * sin[128 + tpg->hue]) / 127;

		cb = (128 << 4) + (tmp_cb * tpg->contrast * tpg->saturation) / (128 * 128);
		cr = (128 << 4) + (tmp_cr * tpg->contrast * tpg->saturation) / (128 * 128);
		if (tpg->color_enc == TGP_COLOR_ENC_YCBCR)
			ycbcr_valid = true;
		else
			ycbcr_to_color(tpg, y, cb, cr, &r, &g, &b);
	} else if ((tpg->brightness != 128 || tpg->contrast != 128) &&
		   tpg->color_enc == TGP_COLOR_ENC_LUMA) {
		r = (16 << 4) + ((r - (16 << 4)) * tpg->contrast) / 128;
		r += (tpg->brightness << 4) - (128 << 4);
	}

	switch (tpg->color_enc) {
	case TGP_COLOR_ENC_HSV:
	{
		int h, s, v;

		color_to_hsv(tpg, r, g, b, &h, &s, &v);
		tpg->colors[k][0] = h;
		tpg->colors[k][1] = s;
		tpg->colors[k][2] = v;
		break;
	}
	case TGP_COLOR_ENC_YCBCR:
	{
		/* Convert to YCbCr */
		if (!ycbcr_valid)
			color_to_ycbcr(tpg, r, g, b, &y, &cb, &cr);

		y >>= 4;
		cb >>= 4;
		cr >>= 4;
		/*
		 * XV601/709 use the header/footer margins to encode R', G'
		 * and B' values outside the range [0-1]. So do not clamp
		 * XV601/709 values.
		 */
		if (tpg->real_quantization == V4L2_QUANTIZATION_LIM_RANGE &&
		    tpg->real_ycbcr_enc != V4L2_YCBCR_ENC_XV601 &&
		    tpg->real_ycbcr_enc != V4L2_YCBCR_ENC_XV709) {
			y = clamp(y, 16, 235);
			cb = clamp(cb, 16, 240);
			cr = clamp(cr, 16, 240);
		} else {
			y = clamp(y, 1, 254);
			cb = clamp(cb, 1, 254);
			cr = clamp(cr, 1, 254);
		}
		switch (tpg->fourcc) {
		case V4L2_PIX_FMT_YUV444:
			y >>= 4;
			cb >>= 4;
			cr >>= 4;
			break;
		case V4L2_PIX_FMT_YUV555:
			y >>= 3;
			cb >>= 3;
			cr >>= 3;
			break;
		case V4L2_PIX_FMT_YUV565:
			y >>= 3;
			cb >>= 2;
			cr >>= 3;
			break;
		}
		tpg->colors[k][0] = y;
		tpg->colors[k][1] = cb;
		tpg->colors[k][2] = cr;
		break;
	}
	case TGP_COLOR_ENC_LUMA:
	{
		tpg->colors[k][0] = r >> 4;
		break;
	}
	case TGP_COLOR_ENC_RGB:
	{
		if (tpg->real_quantization == V4L2_QUANTIZATION_LIM_RANGE) {
			r = (r * 219) / 255 + (16 << 4);
			g = (g * 219) / 255 + (16 << 4);
			b = (b * 219) / 255 + (16 << 4);
		}
		switch (tpg->fourcc) {
		case V4L2_PIX_FMT_RGB332:
			r >>= 9;
			g >>= 9;
			b >>= 10;
			break;
		case V4L2_PIX_FMT_RGB565:
		case V4L2_PIX_FMT_RGB565X:
			r >>= 7;
			g >>= 6;
			b >>= 7;
			break;
		case V4L2_PIX_FMT_RGB444:
		case V4L2_PIX_FMT_XRGB444:
		case V4L2_PIX_FMT_ARGB444:
		case V4L2_PIX_FMT_RGBX444:
		case V4L2_PIX_FMT_RGBA444:
		case V4L2_PIX_FMT_XBGR444:
		case V4L2_PIX_FMT_ABGR444:
		case V4L2_PIX_FMT_BGRX444:
		case V4L2_PIX_FMT_BGRA444:
			r >>= 8;
			g >>= 8;
			b >>= 8;
			break;
		case V4L2_PIX_FMT_RGB555:
		case V4L2_PIX_FMT_XRGB555:
		case V4L2_PIX_FMT_ARGB555:
		case V4L2_PIX_FMT_RGBX555:
		case V4L2_PIX_FMT_RGBA555:
		case V4L2_PIX_FMT_XBGR555:
		case V4L2_PIX_FMT_ABGR555:
		case V4L2_PIX_FMT_BGRX555:
		case V4L2_PIX_FMT_BGRA555:
		case V4L2_PIX_FMT_RGB555X:
		case V4L2_PIX_FMT_XRGB555X:
		case V4L2_PIX_FMT_ARGB555X:
			r >>= 7;
			g >>= 7;
			b >>= 7;
			break;
		case V4L2_PIX_FMT_BGR666:
			r >>= 6;
			g >>= 6;
			b >>= 6;
			break;
		default:
			r >>= 4;
			g >>= 4;
			b >>= 4;
			break;
		}

		tpg->colors[k][0] = r;
		tpg->colors[k][1] = g;
		tpg->colors[k][2] = b;
		break;
	}
	}
}

static void tpg_precalculate_colors(struct tpg_data *tpg)
{
	int k;

	for (k = 0; k < TPG_COLOR_MAX; k++)
		precalculate_color(tpg, k);
}

/* 'odd' is true for pixels 1, 3, 5, etc. and false for pixels 0, 2, 4, etc. */
static void gen_twopix(struct tpg_data *tpg,
		u8 buf[TPG_MAX_PLANES][8], int color, bool odd)
{
	unsigned offset = odd * tpg->twopixelsize[0] / 2;
	u8 alpha = tpg->alpha_component;
	u8 r_y_h, g_u_s, b_v;

	if (tpg->alpha_red_only && color != TPG_COLOR_CSC_RED &&
				   color != TPG_COLOR_100_RED &&
				   color != TPG_COLOR_75_RED)
		alpha = 0;
	if (color == TPG_COLOR_RANDOM)
		precalculate_color(tpg, color);
	r_y_h = tpg->colors[color][0]; /* R or precalculated Y, H */
	g_u_s = tpg->colors[color][1]; /* G or precalculated U, V */
	b_v = tpg->colors[color][2]; /* B or precalculated V */

	switch (tpg->fourcc) {
	case V4L2_PIX_FMT_GREY:
		buf[0][offset] = r_y_h;
		break;
	case V4L2_PIX_FMT_Y10:
		buf[0][offset] = (r_y_h << 2) & 0xff;
		buf[0][offset+1] = r_y_h >> 6;
		break;
	case V4L2_PIX_FMT_Y12:
		buf[0][offset] = (r_y_h << 4) & 0xff;
		buf[0][offset+1] = r_y_h >> 4;
		break;
	case V4L2_PIX_FMT_Y16:
	case V4L2_PIX_FMT_Z16:
		/*
		 * Ideally both bytes should be set to r_y_h, but then you won't
		 * be able to detect endian problems. So keep it 0 except for
		 * the corner case where r_y_h is 0xff so white really will be
		 * white (0xffff).
		 */
		buf[0][offset] = r_y_h == 0xff ? r_y_h : 0;
		buf[0][offset+1] = r_y_h;
		break;
	case V4L2_PIX_FMT_Y16_BE:
		/* See comment for V4L2_PIX_FMT_Y16 above */
		buf[0][offset] = r_y_h;
		buf[0][offset+1] = r_y_h == 0xff ? r_y_h : 0;
		break;
	case V4L2_PIX_FMT_YUV422M:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YUV420M:
		buf[0][offset] = r_y_h;
		if (odd) {
			buf[1][0] = (buf[1][0] + g_u_s) / 2;
			buf[2][0] = (buf[2][0] + b_v) / 2;
			buf[1][1] = buf[1][0];
			buf[2][1] = buf[2][0];
			break;
		}
		buf[1][0] = g_u_s;
		buf[2][0] = b_v;
		break;
	case V4L2_PIX_FMT_YVU422M:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YVU420M:
		buf[0][offset] = r_y_h;
		if (odd) {
			buf[1][0] = (buf[1][0] + b_v) / 2;
			buf[2][0] = (buf[2][0] + g_u_s) / 2;
			buf[1][1] = buf[1][0];
			buf[2][1] = buf[2][0];
			break;
		}
		buf[1][0] = b_v;
		buf[2][0] = g_u_s;
		break;

	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV16M:
		buf[0][offset] = r_y_h;
		if (odd) {
			buf[1][0] = (buf[1][0] + g_u_s) / 2;
			buf[1][1] = (buf[1][1] + b_v) / 2;
			break;
		}
		buf[1][0] = g_u_s;
		buf[1][1] = b_v;
		break;
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV21M:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_NV61M:
		buf[0][offset] = r_y_h;
		if (odd) {
			buf[1][0] = (buf[1][0] + b_v) / 2;
			buf[1][1] = (buf[1][1] + g_u_s) / 2;
			break;
		}
		buf[1][0] = b_v;
		buf[1][1] = g_u_s;
		break;

	case V4L2_PIX_FMT_YUV444M:
		buf[0][offset] = r_y_h;
		buf[1][offset] = g_u_s;
		buf[2][offset] = b_v;
		break;

	case V4L2_PIX_FMT_YVU444M:
		buf[0][offset] = r_y_h;
		buf[1][offset] = b_v;
		buf[2][offset] = g_u_s;
		break;

	case V4L2_PIX_FMT_NV24:
		buf[0][offset] = r_y_h;
		buf[1][2 * offset] = g_u_s;
		buf[1][(2 * offset + 1) % 8] = b_v;
		break;

	case V4L2_PIX_FMT_NV42:
		buf[0][offset] = r_y_h;
		buf[1][2 * offset] = b_v;
		buf[1][(2 * offset + 1) % 8] = g_u_s;
		break;

	case V4L2_PIX_FMT_YUYV:
		buf[0][offset] = r_y_h;
		if (odd) {
			buf[0][1] = (buf[0][1] + g_u_s) / 2;
			buf[0][3] = (buf[0][3] + b_v) / 2;
			break;
		}
		buf[0][1] = g_u_s;
		buf[0][3] = b_v;
		break;
	case V4L2_PIX_FMT_UYVY:
		buf[0][offset + 1] = r_y_h;
		if (odd) {
			buf[0][0] = (buf[0][0] + g_u_s) / 2;
			buf[0][2] = (buf[0][2] + b_v) / 2;
			break;
		}
		buf[0][0] = g_u_s;
		buf[0][2] = b_v;
		break;
	case V4L2_PIX_FMT_YVYU:
		buf[0][offset] = r_y_h;
		if (odd) {
			buf[0][1] = (buf[0][1] + b_v) / 2;
			buf[0][3] = (buf[0][3] + g_u_s) / 2;
			break;
		}
		buf[0][1] = b_v;
		buf[0][3] = g_u_s;
		break;
	case V4L2_PIX_FMT_VYUY:
		buf[0][offset + 1] = r_y_h;
		if (odd) {
			buf[0][0] = (buf[0][0] + b_v) / 2;
			buf[0][2] = (buf[0][2] + g_u_s) / 2;
			break;
		}
		buf[0][0] = b_v;
		buf[0][2] = g_u_s;
		break;
	case V4L2_PIX_FMT_RGB332:
		buf[0][offset] = (r_y_h << 5) | (g_u_s << 2) | b_v;
		break;
	case V4L2_PIX_FMT_YUV565:
	case V4L2_PIX_FMT_RGB565:
		buf[0][offset] = (g_u_s << 5) | b_v;
		buf[0][offset + 1] = (r_y_h << 3) | (g_u_s >> 3);
		break;
	case V4L2_PIX_FMT_RGB565X:
		buf[0][offset] = (r_y_h << 3) | (g_u_s >> 3);
		buf[0][offset + 1] = (g_u_s << 5) | b_v;
		break;
	case V4L2_PIX_FMT_RGB444:
	case V4L2_PIX_FMT_XRGB444:
		alpha = 0;
		fallthrough;
	case V4L2_PIX_FMT_YUV444:
	case V4L2_PIX_FMT_ARGB444:
		buf[0][offset] = (g_u_s << 4) | b_v;
		buf[0][offset + 1] = (alpha & 0xf0) | r_y_h;
		break;
	case V4L2_PIX_FMT_RGBX444:
		alpha = 0;
		fallthrough;
	case V4L2_PIX_FMT_RGBA444:
		buf[0][offset] = (b_v << 4) | (alpha >> 4);
		buf[0][offset + 1] = (r_y_h << 4) | g_u_s;
		break;
	case V4L2_PIX_FMT_XBGR444:
		alpha = 0;
		fallthrough;
	case V4L2_PIX_FMT_ABGR444:
		buf[0][offset] = (g_u_s << 4) | r_y_h;
		buf[0][offset + 1] = (alpha & 0xf0) | b_v;
		break;
	case V4L2_PIX_FMT_BGRX444:
		alpha = 0;
		fallthrough;
	case V4L2_PIX_FMT_BGRA444:
		buf[0][offset] = (r_y_h << 4) | (alpha >> 4);
		buf[0][offset + 1] = (b_v << 4) | g_u_s;
		break;
	case V4L2_PIX_FMT_RGB555:
	case V4L2_PIX_FMT_XRGB555:
		alpha = 0;
		fallthrough;
	case V4L2_PIX_FMT_YUV555:
	case V4L2_PIX_FMT_ARGB555:
		buf[0][offset] = (g_u_s << 5) | b_v;
		buf[0][offset + 1] = (alpha & 0x80) | (r_y_h << 2)
						    | (g_u_s >> 3);
		break;
	case V4L2_PIX_FMT_RGBX555:
		alpha = 0;
		fallthrough;
	case V4L2_PIX_FMT_RGBA555:
		buf[0][offset] = (g_u_s << 6) | (b_v << 1) |
				 ((alpha & 0x80) >> 7);
		buf[0][offset + 1] = (r_y_h << 3) | (g_u_s >> 2);
		break;
	case V4L2_PIX_FMT_XBGR555:
		alpha = 0;
		fallthrough;
	case V4L2_PIX_FMT_ABGR555:
		buf[0][offset] = (g_u_s << 5) | r_y_h;
		buf[0][offset + 1] = (alpha & 0x80) | (b_v << 2)
						    | (g_u_s >> 3);
		break;
	case V4L2_PIX_FMT_BGRX555:
		alpha = 0;
		fallthrough;
	case V4L2_PIX_FMT_BGRA555:
		buf[0][offset] = (g_u_s << 6) | (r_y_h << 1) |
				 ((alpha & 0x80) >> 7);
		buf[0][offset + 1] = (b_v << 3) | (g_u_s >> 2);
		break;
	case V4L2_PIX_FMT_RGB555X:
	case V4L2_PIX_FMT_XRGB555X:
		alpha = 0;
		fallthrough;
	case V4L2_PIX_FMT_ARGB555X:
		buf[0][offset] = (alpha & 0x80) | (r_y_h << 2) | (g_u_s >> 3);
		buf[0][offset + 1] = (g_u_s << 5) | b_v;
		break;
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_HSV24:
		buf[0][offset] = r_y_h;
		buf[0][offset + 1] = g_u_s;
		buf[0][offset + 2] = b_v;
		break;
	case V4L2_PIX_FMT_BGR24:
		buf[0][offset] = b_v;
		buf[0][offset + 1] = g_u_s;
		buf[0][offset + 2] = r_y_h;
		break;
	case V4L2_PIX_FMT_BGR666:
		buf[0][offset] = (b_v << 2) | (g_u_s >> 4);
		buf[0][offset + 1] = (g_u_s << 4) | (r_y_h >> 2);
		buf[0][offset + 2] = r_y_h << 6;
		buf[0][offset + 3] = 0;
		break;
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_XRGB32:
	case V4L2_PIX_FMT_HSV32:
	case V4L2_PIX_FMT_XYUV32:
		alpha = 0;
		fallthrough;
	case V4L2_PIX_FMT_YUV32:
	case V4L2_PIX_FMT_ARGB32:
	case V4L2_PIX_FMT_AYUV32:
		buf[0][offset] = alpha;
		buf[0][offset + 1] = r_y_h;
		buf[0][offset + 2] = g_u_s;
		buf[0][offset + 3] = b_v;
		break;
	case V4L2_PIX_FMT_RGBX32:
	case V4L2_PIX_FMT_YUVX32:
		alpha = 0;
		fallthrough;
	case V4L2_PIX_FMT_RGBA32:
	case V4L2_PIX_FMT_YUVA32:
		buf[0][offset] = r_y_h;
		buf[0][offset + 1] = g_u_s;
		buf[0][offset + 2] = b_v;
		buf[0][offset + 3] = alpha;
		break;
	case V4L2_PIX_FMT_BGR32:
	case V4L2_PIX_FMT_XBGR32:
	case V4L2_PIX_FMT_VUYX32:
		alpha = 0;
		fallthrough;
	case V4L2_PIX_FMT_ABGR32:
	case V4L2_PIX_FMT_VUYA32:
		buf[0][offset] = b_v;
		buf[0][offset + 1] = g_u_s;
		buf[0][offset + 2] = r_y_h;
		buf[0][offset + 3] = alpha;
		break;
	case V4L2_PIX_FMT_BGRX32:
		alpha = 0;
		fallthrough;
	case V4L2_PIX_FMT_BGRA32:
		buf[0][offset] = alpha;
		buf[0][offset + 1] = b_v;
		buf[0][offset + 2] = g_u_s;
		buf[0][offset + 3] = r_y_h;
		break;
	case V4L2_PIX_FMT_SBGGR8:
		buf[0][offset] = odd ? g_u_s : b_v;
		buf[1][offset] = odd ? r_y_h : g_u_s;
		break;
	case V4L2_PIX_FMT_SGBRG8:
		buf[0][offset] = odd ? b_v : g_u_s;
		buf[1][offset] = odd ? g_u_s : r_y_h;
		break;
	case V4L2_PIX_FMT_SGRBG8:
		buf[0][offset] = odd ? r_y_h : g_u_s;
		buf[1][offset] = odd ? g_u_s : b_v;
		break;
	case V4L2_PIX_FMT_SRGGB8:
		buf[0][offset] = odd ? g_u_s : r_y_h;
		buf[1][offset] = odd ? b_v : g_u_s;
		break;
	case V4L2_PIX_FMT_SBGGR10:
		buf[0][offset] = odd ? g_u_s << 2 : b_v << 2;
		buf[0][offset + 1] = odd ? g_u_s >> 6 : b_v >> 6;
		buf[1][offset] = odd ? r_y_h << 2 : g_u_s << 2;
		buf[1][offset + 1] = odd ? r_y_h >> 6 : g_u_s >> 6;
		buf[0][offset] |= (buf[0][offset] >> 2) & 3;
		buf[1][offset] |= (buf[1][offset] >> 2) & 3;
		break;
	case V4L2_PIX_FMT_SGBRG10:
		buf[0][offset] = odd ? b_v << 2 : g_u_s << 2;
		buf[0][offset + 1] = odd ? b_v >> 6 : g_u_s >> 6;
		buf[1][offset] = odd ? g_u_s << 2 : r_y_h << 2;
		buf[1][offset + 1] = odd ? g_u_s >> 6 : r_y_h >> 6;
		buf[0][offset] |= (buf[0][offset] >> 2) & 3;
		buf[1][offset] |= (buf[1][offset] >> 2) & 3;
		break;
	case V4L2_PIX_FMT_SGRBG10:
		buf[0][offset] = odd ? r_y_h << 2 : g_u_s << 2;
		buf[0][offset + 1] = odd ? r_y_h >> 6 : g_u_s >> 6;
		buf[1][offset] = odd ? g_u_s << 2 : b_v << 2;
		buf[1][offset + 1] = odd ? g_u_s >> 6 : b_v >> 6;
		buf[0][offset] |= (buf[0][offset] >> 2) & 3;
		buf[1][offset] |= (buf[1][offset] >> 2) & 3;
		break;
	case V4L2_PIX_FMT_SRGGB10:
		buf[0][offset] = odd ? g_u_s << 2 : r_y_h << 2;
		buf[0][offset + 1] = odd ? g_u_s >> 6 : r_y_h >> 6;
		buf[1][offset] = odd ? b_v << 2 : g_u_s << 2;
		buf[1][offset + 1] = odd ? b_v >> 6 : g_u_s >> 6;
		buf[0][offset] |= (buf[0][offset] >> 2) & 3;
		buf[1][offset] |= (buf[1][offset] >> 2) & 3;
		break;
	case V4L2_PIX_FMT_SBGGR12:
		buf[0][offset] = odd ? g_u_s << 4 : b_v << 4;
		buf[0][offset + 1] = odd ? g_u_s >> 4 : b_v >> 4;
		buf[1][offset] = odd ? r_y_h << 4 : g_u_s << 4;
		buf[1][offset + 1] = odd ? r_y_h >> 4 : g_u_s >> 4;
		buf[0][offset] |= (buf[0][offset] >> 4) & 0xf;
		buf[1][offset] |= (buf[1][offset] >> 4) & 0xf;
		break;
	case V4L2_PIX_FMT_SGBRG12:
		buf[0][offset] = odd ? b_v << 4 : g_u_s << 4;
		buf[0][offset + 1] = odd ? b_v >> 4 : g_u_s >> 4;
		buf[1][offset] = odd ? g_u_s << 4 : r_y_h << 4;
		buf[1][offset + 1] = odd ? g_u_s >> 4 : r_y_h >> 4;
		buf[0][offset] |= (buf[0][offset] >> 4) & 0xf;
		buf[1][offset] |= (buf[1][offset] >> 4) & 0xf;
		break;
	case V4L2_PIX_FMT_SGRBG12:
		buf[0][offset] = odd ? r_y_h << 4 : g_u_s << 4;
		buf[0][offset + 1] = odd ? r_y_h >> 4 : g_u_s >> 4;
		buf[1][offset] = odd ? g_u_s << 4 : b_v << 4;
		buf[1][offset + 1] = odd ? g_u_s >> 4 : b_v >> 4;
		buf[0][offset] |= (buf[0][offset] >> 4) & 0xf;
		buf[1][offset] |= (buf[1][offset] >> 4) & 0xf;
		break;
	case V4L2_PIX_FMT_SRGGB12:
		buf[0][offset] = odd ? g_u_s << 4 : r_y_h << 4;
		buf[0][offset + 1] = odd ? g_u_s >> 4 : r_y_h >> 4;
		buf[1][offset] = odd ? b_v << 4 : g_u_s << 4;
		buf[1][offset + 1] = odd ? b_v >> 4 : g_u_s >> 4;
		buf[0][offset] |= (buf[0][offset] >> 4) & 0xf;
		buf[1][offset] |= (buf[1][offset] >> 4) & 0xf;
		break;
	case V4L2_PIX_FMT_SBGGR16:
		buf[0][offset] = buf[0][offset + 1] = odd ? g_u_s : b_v;
		buf[1][offset] = buf[1][offset + 1] = odd ? r_y_h : g_u_s;
		break;
	case V4L2_PIX_FMT_SGBRG16:
		buf[0][offset] = buf[0][offset + 1] = odd ? b_v : g_u_s;
		buf[1][offset] = buf[1][offset + 1] = odd ? g_u_s : r_y_h;
		break;
	case V4L2_PIX_FMT_SGRBG16:
		buf[0][offset] = buf[0][offset + 1] = odd ? r_y_h : g_u_s;
		buf[1][offset] = buf[1][offset + 1] = odd ? g_u_s : b_v;
		break;
	case V4L2_PIX_FMT_SRGGB16:
		buf[0][offset] = buf[0][offset + 1] = odd ? g_u_s : r_y_h;
		buf[1][offset] = buf[1][offset + 1] = odd ? b_v : g_u_s;
		break;
	}
}

unsigned tpg_g_interleaved_plane(const struct tpg_data *tpg, unsigned buf_line)
{
	switch (tpg->fourcc) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
	case V4L2_PIX_FMT_SBGGR16:
	case V4L2_PIX_FMT_SGBRG16:
	case V4L2_PIX_FMT_SGRBG16:
	case V4L2_PIX_FMT_SRGGB16:
		return buf_line & 1;
	default:
		return 0;
	}
}
EXPORT_SYMBOL_GPL(tpg_g_interleaved_plane);

/* Return how many pattern lines are used by the current pattern. */
static unsigned tpg_get_pat_lines(const struct tpg_data *tpg)
{
	switch (tpg->pattern) {
	case TPG_PAT_CHECKERS_16X16:
	case TPG_PAT_CHECKERS_2X2:
	case TPG_PAT_CHECKERS_1X1:
	case TPG_PAT_COLOR_CHECKERS_2X2:
	case TPG_PAT_COLOR_CHECKERS_1X1:
	case TPG_PAT_ALTERNATING_HLINES:
	case TPG_PAT_CROSS_1_PIXEL:
	case TPG_PAT_CROSS_2_PIXELS:
	case TPG_PAT_CROSS_10_PIXELS:
		return 2;
	case TPG_PAT_100_COLORSQUARES:
	case TPG_PAT_100_HCOLORBAR:
		return 8;
	default:
		return 1;
	}
}

/* Which pattern line should be used for the given frame line. */
static unsigned tpg_get_pat_line(const struct tpg_data *tpg, unsigned line)
{
	switch (tpg->pattern) {
	case TPG_PAT_CHECKERS_16X16:
		return (line >> 4) & 1;
	case TPG_PAT_CHECKERS_1X1:
	case TPG_PAT_COLOR_CHECKERS_1X1:
	case TPG_PAT_ALTERNATING_HLINES:
		return line & 1;
	case TPG_PAT_CHECKERS_2X2:
	case TPG_PAT_COLOR_CHECKERS_2X2:
		return (line & 2) >> 1;
	case TPG_PAT_100_COLORSQUARES:
	case TPG_PAT_100_HCOLORBAR:
		return (line * 8) / tpg->src_height;
	case TPG_PAT_CROSS_1_PIXEL:
		return line == tpg->src_height / 2;
	case TPG_PAT_CROSS_2_PIXELS:
		return (line + 1) / 2 == tpg->src_height / 4;
	case TPG_PAT_CROSS_10_PIXELS:
		return (line + 10) / 20 == tpg->src_height / 40;
	default:
		return 0;
	}
}

/*
 * Which color should be used for the given pattern line and X coordinate.
 * Note: x is in the range 0 to 2 * tpg->src_width.
 */
static enum tpg_color tpg_get_color(const struct tpg_data *tpg,
				    unsigned pat_line, unsigned x)
{
	/* Maximum number of bars are TPG_COLOR_MAX - otherwise, the input print code
	   should be modified */
	static const enum tpg_color bars[3][8] = {
		/* Standard ITU-R 75% color bar sequence */
		{ TPG_COLOR_CSC_WHITE,   TPG_COLOR_75_YELLOW,
		  TPG_COLOR_75_CYAN,     TPG_COLOR_75_GREEN,
		  TPG_COLOR_75_MAGENTA,  TPG_COLOR_75_RED,
		  TPG_COLOR_75_BLUE,     TPG_COLOR_100_BLACK, },
		/* Standard ITU-R 100% color bar sequence */
		{ TPG_COLOR_100_WHITE,   TPG_COLOR_100_YELLOW,
		  TPG_COLOR_100_CYAN,    TPG_COLOR_100_GREEN,
		  TPG_COLOR_100_MAGENTA, TPG_COLOR_100_RED,
		  TPG_COLOR_100_BLUE,    TPG_COLOR_100_BLACK, },
		/* Color bar sequence suitable to test CSC */
		{ TPG_COLOR_CSC_WHITE,   TPG_COLOR_CSC_YELLOW,
		  TPG_COLOR_CSC_CYAN,    TPG_COLOR_CSC_GREEN,
		  TPG_COLOR_CSC_MAGENTA, TPG_COLOR_CSC_RED,
		  TPG_COLOR_CSC_BLUE,    TPG_COLOR_CSC_BLACK, },
	};

	switch (tpg->pattern) {
	case TPG_PAT_75_COLORBAR:
	case TPG_PAT_100_COLORBAR:
	case TPG_PAT_CSC_COLORBAR:
		return bars[tpg->pattern][((x * 8) / tpg->src_width) % 8];
	case TPG_PAT_100_COLORSQUARES:
		return bars[1][(pat_line + (x * 8) / tpg->src_width) % 8];
	case TPG_PAT_100_HCOLORBAR:
		return bars[1][pat_line];
	case TPG_PAT_BLACK:
		return TPG_COLOR_100_BLACK;
	case TPG_PAT_WHITE:
		return TPG_COLOR_100_WHITE;
	case TPG_PAT_RED:
		return TPG_COLOR_100_RED;
	case TPG_PAT_GREEN:
		return TPG_COLOR_100_GREEN;
	case TPG_PAT_BLUE:
		return TPG_COLOR_100_BLUE;
	case TPG_PAT_CHECKERS_16X16:
		return (((x >> 4) & 1) ^ (pat_line & 1)) ?
			TPG_COLOR_100_BLACK : TPG_COLOR_100_WHITE;
	case TPG_PAT_CHECKERS_1X1:
		return ((x & 1) ^ (pat_line & 1)) ?
			TPG_COLOR_100_WHITE : TPG_COLOR_100_BLACK;
	case TPG_PAT_COLOR_CHECKERS_1X1:
		return ((x & 1) ^ (pat_line & 1)) ?
			TPG_COLOR_100_RED : TPG_COLOR_100_BLUE;
	case TPG_PAT_CHECKERS_2X2:
		return (((x >> 1) & 1) ^ (pat_line & 1)) ?
			TPG_COLOR_100_WHITE : TPG_COLOR_100_BLACK;
	case TPG_PAT_COLOR_CHECKERS_2X2:
		return (((x >> 1) & 1) ^ (pat_line & 1)) ?
			TPG_COLOR_100_RED : TPG_COLOR_100_BLUE;
	case TPG_PAT_ALTERNATING_HLINES:
		return pat_line ? TPG_COLOR_100_WHITE : TPG_COLOR_100_BLACK;
	case TPG_PAT_ALTERNATING_VLINES:
		return (x & 1) ? TPG_COLOR_100_WHITE : TPG_COLOR_100_BLACK;
	case TPG_PAT_CROSS_1_PIXEL:
		if (pat_line || (x % tpg->src_width) == tpg->src_width / 2)
			return TPG_COLOR_100_BLACK;
		return TPG_COLOR_100_WHITE;
	case TPG_PAT_CROSS_2_PIXELS:
		if (pat_line || ((x % tpg->src_width) + 1) / 2 == tpg->src_width / 4)
			return TPG_COLOR_100_BLACK;
		return TPG_COLOR_100_WHITE;
	case TPG_PAT_CROSS_10_PIXELS:
		if (pat_line || ((x % tpg->src_width) + 10) / 20 == tpg->src_width / 40)
			return TPG_COLOR_100_BLACK;
		return TPG_COLOR_100_WHITE;
	case TPG_PAT_GRAY_RAMP:
		return TPG_COLOR_RAMP + ((x % tpg->src_width) * 256) / tpg->src_width;
	default:
		return TPG_COLOR_100_RED;
	}
}

/*
 * Given the pixel aspect ratio and video aspect ratio calculate the
 * coordinates of a centered square and the coordinates of the border of
 * the active video area. The coordinates are relative to the source
 * frame rectangle.
 */
static void tpg_calculate_square_border(struct tpg_data *tpg)
{
	unsigned w = tpg->src_width;
	unsigned h = tpg->src_height;
	unsigned sq_w, sq_h;

	sq_w = (w * 2 / 5) & ~1;
	if (((w - sq_w) / 2) & 1)
		sq_w += 2;
	sq_h = sq_w;
	tpg->square.width = sq_w;
	if (tpg->vid_aspect == TPG_VIDEO_ASPECT_16X9_ANAMORPHIC) {
		unsigned ana_sq_w = (sq_w / 4) * 3;

		if (((w - ana_sq_w) / 2) & 1)
			ana_sq_w += 2;
		tpg->square.width = ana_sq_w;
	}
	tpg->square.left = (w - tpg->square.width) / 2;
	if (tpg->pix_aspect == TPG_PIXEL_ASPECT_NTSC)
		sq_h = sq_w * 10 / 11;
	else if (tpg->pix_aspect == TPG_PIXEL_ASPECT_PAL)
		sq_h = sq_w * 59 / 54;
	tpg->square.height = sq_h;
	tpg->square.top = (h - sq_h) / 2;
	tpg->border.left = 0;
	tpg->border.width = w;
	tpg->border.top = 0;
	tpg->border.height = h;
	switch (tpg->vid_aspect) {
	case TPG_VIDEO_ASPECT_4X3:
		if (tpg->pix_aspect)
			return;
		if (3 * w >= 4 * h) {
			tpg->border.width = ((4 * h) / 3) & ~1;
			if (((w - tpg->border.width) / 2) & ~1)
				tpg->border.width -= 2;
			tpg->border.left = (w - tpg->border.width) / 2;
			break;
		}
		tpg->border.height = ((3 * w) / 4) & ~1;
		tpg->border.top = (h - tpg->border.height) / 2;
		break;
	case TPG_VIDEO_ASPECT_14X9_CENTRE:
		if (tpg->pix_aspect) {
			tpg->border.height = tpg->pix_aspect == TPG_PIXEL_ASPECT_NTSC ? 420 : 506;
			tpg->border.top = (h - tpg->border.height) / 2;
			break;
		}
		if (9 * w >= 14 * h) {
			tpg->border.width = ((14 * h) / 9) & ~1;
			if (((w - tpg->border.width) / 2) & ~1)
				tpg->border.width -= 2;
			tpg->border.left = (w - tpg->border.width) / 2;
			break;
		}
		tpg->border.height = ((9 * w) / 14) & ~1;
		tpg->border.top = (h - tpg->border.height) / 2;
		break;
	case TPG_VIDEO_ASPECT_16X9_CENTRE:
		if (tpg->pix_aspect) {
			tpg->border.height = tpg->pix_aspect == TPG_PIXEL_ASPECT_NTSC ? 368 : 442;
			tpg->border.top = (h - tpg->border.height) / 2;
			break;
		}
		if (9 * w >= 16 * h) {
			tpg->border.width = ((16 * h) / 9) & ~1;
			if (((w - tpg->border.width) / 2) & ~1)
				tpg->border.width -= 2;
			tpg->border.left = (w - tpg->border.width) / 2;
			break;
		}
		tpg->border.height = ((9 * w) / 16) & ~1;
		tpg->border.top = (h - tpg->border.height) / 2;
		break;
	default:
		break;
	}
}

static void tpg_precalculate_line(struct tpg_data *tpg)
{
	enum tpg_color contrast;
	u8 pix[TPG_MAX_PLANES][8];
	unsigned pat;
	unsigned p;
	unsigned x;

	switch (tpg->pattern) {
	case TPG_PAT_GREEN:
		contrast = TPG_COLOR_100_RED;
		break;
	case TPG_PAT_CSC_COLORBAR:
		contrast = TPG_COLOR_CSC_GREEN;
		break;
	default:
		contrast = TPG_COLOR_100_GREEN;
		break;
	}

	for (pat = 0; pat < tpg_get_pat_lines(tpg); pat++) {
		/* Coarse scaling with Bresenham */
		unsigned int_part = tpg->src_width / tpg->scaled_width;
		unsigned fract_part = tpg->src_width % tpg->scaled_width;
		unsigned src_x = 0;
		unsigned error = 0;

		for (x = 0; x < tpg->scaled_width * 2; x += 2) {
			unsigned real_x = src_x;
			enum tpg_color color1, color2;

			real_x = tpg->hflip ? tpg->src_width * 2 - real_x - 2 : real_x;
			color1 = tpg_get_color(tpg, pat, real_x);

			src_x += int_part;
			error += fract_part;
			if (error >= tpg->scaled_width) {
				error -= tpg->scaled_width;
				src_x++;
			}

			real_x = src_x;
			real_x = tpg->hflip ? tpg->src_width * 2 - real_x - 2 : real_x;
			color2 = tpg_get_color(tpg, pat, real_x);

			src_x += int_part;
			error += fract_part;
			if (error >= tpg->scaled_width) {
				error -= tpg->scaled_width;
				src_x++;
			}

			gen_twopix(tpg, pix, tpg->hflip ? color2 : color1, 0);
			gen_twopix(tpg, pix, tpg->hflip ? color1 : color2, 1);
			for (p = 0; p < tpg->planes; p++) {
				unsigned twopixsize = tpg->twopixelsize[p];
				unsigned hdiv = tpg->hdownsampling[p];
				u8 *pos = tpg->lines[pat][p] + tpg_hdiv(tpg, p, x);

				memcpy(pos, pix[p], twopixsize / hdiv);
			}
		}
	}

	if (tpg->vdownsampling[tpg->planes - 1] > 1) {
		unsigned pat_lines = tpg_get_pat_lines(tpg);

		for (pat = 0; pat < pat_lines; pat++) {
			unsigned next_pat = (pat + 1) % pat_lines;

			for (p = 1; p < tpg->planes; p++) {
				unsigned w = tpg_hdiv(tpg, p, tpg->scaled_width * 2);
				u8 *pos1 = tpg->lines[pat][p];
				u8 *pos2 = tpg->lines[next_pat][p];
				u8 *dest = tpg->downsampled_lines[pat][p];

				for (x = 0; x < w; x++, pos1++, pos2++, dest++)
					*dest = ((u16)*pos1 + (u16)*pos2) / 2;
			}
		}
	}

	gen_twopix(tpg, pix, contrast, 0);
	gen_twopix(tpg, pix, contrast, 1);
	for (p = 0; p < tpg->planes; p++) {
		unsigned twopixsize = tpg->twopixelsize[p];
		u8 *pos = tpg->contrast_line[p];

		for (x = 0; x < tpg->scaled_width; x += 2, pos += twopixsize)
			memcpy(pos, pix[p], twopixsize);
	}

	gen_twopix(tpg, pix, TPG_COLOR_100_BLACK, 0);
	gen_twopix(tpg, pix, TPG_COLOR_100_BLACK, 1);
	for (p = 0; p < tpg->planes; p++) {
		unsigned twopixsize = tpg->twopixelsize[p];
		u8 *pos = tpg->black_line[p];

		for (x = 0; x < tpg->scaled_width; x += 2, pos += twopixsize)
			memcpy(pos, pix[p], twopixsize);
	}

	for (x = 0; x < tpg->scaled_width * 2; x += 2) {
		gen_twopix(tpg, pix, TPG_COLOR_RANDOM, 0);
		gen_twopix(tpg, pix, TPG_COLOR_RANDOM, 1);
		for (p = 0; p < tpg->planes; p++) {
			unsigned twopixsize = tpg->twopixelsize[p];
			u8 *pos = tpg->random_line[p] + x * twopixsize / 2;

			memcpy(pos, pix[p], twopixsize);
		}
	}

	gen_twopix(tpg, tpg->textbg, TPG_COLOR_TEXTBG, 0);
	gen_twopix(tpg, tpg->textbg, TPG_COLOR_TEXTBG, 1);
	gen_twopix(tpg, tpg->textfg, TPG_COLOR_TEXTFG, 0);
	gen_twopix(tpg, tpg->textfg, TPG_COLOR_TEXTFG, 1);
}

/* need this to do rgb24 rendering */
typedef struct { u16 __; u8 _; } __packed x24;

#define PRINTSTR(PIXTYPE) do {	\
	unsigned vdiv = tpg->vdownsampling[p]; \
	unsigned hdiv = tpg->hdownsampling[p]; \
	int line;	\
	PIXTYPE fg;	\
	PIXTYPE bg;	\
	memcpy(&fg, tpg->textfg[p], sizeof(PIXTYPE));	\
	memcpy(&bg, tpg->textbg[p], sizeof(PIXTYPE));	\
	\
	for (line = first; line < 16; line += vdiv * step) {	\
		int l = tpg->vflip ? 15 - line : line; \
		PIXTYPE *pos = (PIXTYPE *)(basep[p][(line / vdiv) & 1] + \
			       ((y * step + l) / (vdiv * div)) * tpg->bytesperline[p] + \
			       (x / hdiv) * sizeof(PIXTYPE));	\
		unsigned s;	\
	\
		for (s = 0; s < len; s++) {	\
			u8 chr = font8x16[(u8)text[s] * 16 + line];	\
	\
			if (hdiv == 2 && tpg->hflip) { \
				pos[3] = (chr & (0x01 << 6) ? fg : bg);	\
				pos[2] = (chr & (0x01 << 4) ? fg : bg);	\
				pos[1] = (chr & (0x01 << 2) ? fg : bg);	\
				pos[0] = (chr & (0x01 << 0) ? fg : bg);	\
			} else if (hdiv == 2) { \
				pos[0] = (chr & (0x01 << 7) ? fg : bg);	\
				pos[1] = (chr & (0x01 << 5) ? fg : bg);	\
				pos[2] = (chr & (0x01 << 3) ? fg : bg);	\
				pos[3] = (chr & (0x01 << 1) ? fg : bg);	\
			} else if (tpg->hflip) { \
				pos[7] = (chr & (0x01 << 7) ? fg : bg);	\
				pos[6] = (chr & (0x01 << 6) ? fg : bg);	\
				pos[5] = (chr & (0x01 << 5) ? fg : bg);	\
				pos[4] = (chr & (0x01 << 4) ? fg : bg);	\
				pos[3] = (chr & (0x01 << 3) ? fg : bg);	\
				pos[2] = (chr & (0x01 << 2) ? fg : bg);	\
				pos[1] = (chr & (0x01 << 1) ? fg : bg);	\
				pos[0] = (chr & (0x01 << 0) ? fg : bg);	\
			} else { \
				pos[0] = (chr & (0x01 << 7) ? fg : bg);	\
				pos[1] = (chr & (0x01 << 6) ? fg : bg);	\
				pos[2] = (chr & (0x01 << 5) ? fg : bg);	\
				pos[3] = (chr & (0x01 << 4) ? fg : bg);	\
				pos[4] = (chr & (0x01 << 3) ? fg : bg);	\
				pos[5] = (chr & (0x01 << 2) ? fg : bg);	\
				pos[6] = (chr & (0x01 << 1) ? fg : bg);	\
				pos[7] = (chr & (0x01 << 0) ? fg : bg);	\
			} \
	\
			pos += (tpg->hflip ? -8 : 8) / (int)hdiv;	\
		}	\
	}	\
} while (0)

static noinline void tpg_print_str_2(const struct tpg_data *tpg, u8 *basep[TPG_MAX_PLANES][2],
			unsigned p, unsigned first, unsigned div, unsigned step,
			int y, int x, const char *text, unsigned len)
{
	PRINTSTR(u8);
}

static noinline void tpg_print_str_4(const struct tpg_data *tpg, u8 *basep[TPG_MAX_PLANES][2],
			unsigned p, unsigned first, unsigned div, unsigned step,
			int y, int x, const char *text, unsigned len)
{
	PRINTSTR(u16);
}

static noinline void tpg_print_str_6(const struct tpg_data *tpg, u8 *basep[TPG_MAX_PLANES][2],
			unsigned p, unsigned first, unsigned div, unsigned step,
			int y, int x, const char *text, unsigned len)
{
	PRINTSTR(x24);
}

static noinline void tpg_print_str_8(const struct tpg_data *tpg, u8 *basep[TPG_MAX_PLANES][2],
			unsigned p, unsigned first, unsigned div, unsigned step,
			int y, int x, const char *text, unsigned len)
{
	PRINTSTR(u32);
}

void tpg_gen_text(const struct tpg_data *tpg, u8 *basep[TPG_MAX_PLANES][2],
		  int y, int x, const char *text)
{
	unsigned step = V4L2_FIELD_HAS_T_OR_B(tpg->field) ? 2 : 1;
	unsigned div = step;
	unsigned first = 0;
	unsigned len;
	unsigned p;

	if (font8x16 == NULL || basep == NULL || text == NULL)
		return;

	len = strlen(text);

	/* Checks if it is possible to show string */
	if (y + 16 >= tpg->compose.height || x + 8 >= tpg->compose.width)
		return;

	if (len > (tpg->compose.width - x) / 8)
		len = (tpg->compose.width - x) / 8;
	if (tpg->vflip)
		y = tpg->compose.height - y - 16;
	if (tpg->hflip)
		x = tpg->compose.width - x - 8;
	y += tpg->compose.top;
	x += tpg->compose.left;
	if (tpg->field == V4L2_FIELD_BOTTOM)
		first = 1;
	else if (tpg->field == V4L2_FIELD_SEQ_TB || tpg->field == V4L2_FIELD_SEQ_BT)
		div = 2;

	for (p = 0; p < tpg->planes; p++) {
		/* Print text */
		switch (tpg->twopixelsize[p]) {
		case 2:
			tpg_print_str_2(tpg, basep, p, first, div, step, y, x,
					text, len);
			break;
		case 4:
			tpg_print_str_4(tpg, basep, p, first, div, step, y, x,
					text, len);
			break;
		case 6:
			tpg_print_str_6(tpg, basep, p, first, div, step, y, x,
					text, len);
			break;
		case 8:
			tpg_print_str_8(tpg, basep, p, first, div, step, y, x,
					text, len);
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(tpg_gen_text);

const char *tpg_g_color_order(const struct tpg_data *tpg)
{
	switch (tpg->pattern) {
	case TPG_PAT_75_COLORBAR:
	case TPG_PAT_100_COLORBAR:
	case TPG_PAT_CSC_COLORBAR:
	case TPG_PAT_100_HCOLORBAR:
		return "White, yellow, cyan, green, magenta, red, blue, black";
	case TPG_PAT_BLACK:
		return "Black";
	case TPG_PAT_WHITE:
		return "White";
	case TPG_PAT_RED:
		return "Red";
	case TPG_PAT_GREEN:
		return "Green";
	case TPG_PAT_BLUE:
		return "Blue";
	default:
		return NULL;
	}
}
EXPORT_SYMBOL_GPL(tpg_g_color_order);

void tpg_update_mv_step(struct tpg_data *tpg)
{
	int factor = tpg->mv_hor_mode > TPG_MOVE_NONE ? -1 : 1;

	if (tpg->hflip)
		factor = -factor;
	switch (tpg->mv_hor_mode) {
	case TPG_MOVE_NEG_FAST:
	case TPG_MOVE_POS_FAST:
		tpg->mv_hor_step = ((tpg->src_width + 319) / 320) * 4;
		break;
	case TPG_MOVE_NEG:
	case TPG_MOVE_POS:
		tpg->mv_hor_step = ((tpg->src_width + 639) / 640) * 4;
		break;
	case TPG_MOVE_NEG_SLOW:
	case TPG_MOVE_POS_SLOW:
		tpg->mv_hor_step = 2;
		break;
	case TPG_MOVE_NONE:
		tpg->mv_hor_step = 0;
		break;
	}
	if (factor < 0)
		tpg->mv_hor_step = tpg->src_width - tpg->mv_hor_step;

	factor = tpg->mv_vert_mode > TPG_MOVE_NONE ? -1 : 1;
	switch (tpg->mv_vert_mode) {
	case TPG_MOVE_NEG_FAST:
	case TPG_MOVE_POS_FAST:
		tpg->mv_vert_step = ((tpg->src_width + 319) / 320) * 4;
		break;
	case TPG_MOVE_NEG:
	case TPG_MOVE_POS:
		tpg->mv_vert_step = ((tpg->src_width + 639) / 640) * 4;
		break;
	case TPG_MOVE_NEG_SLOW:
	case TPG_MOVE_POS_SLOW:
		tpg->mv_vert_step = 1;
		break;
	case TPG_MOVE_NONE:
		tpg->mv_vert_step = 0;
		break;
	}
	if (factor < 0)
		tpg->mv_vert_step = tpg->src_height - tpg->mv_vert_step;
}
EXPORT_SYMBOL_GPL(tpg_update_mv_step);

/* Map the line number relative to the crop rectangle to a frame line number */
static unsigned tpg_calc_frameline(const struct tpg_data *tpg, unsigned src_y,
				    unsigned field)
{
	switch (field) {
	case V4L2_FIELD_TOP:
		return tpg->crop.top + src_y * 2;
	case V4L2_FIELD_BOTTOM:
		return tpg->crop.top + src_y * 2 + 1;
	default:
		return src_y + tpg->crop.top;
	}
}

/*
 * Map the line number relative to the compose rectangle to a destination
 * buffer line number.
 */
static unsigned tpg_calc_buffer_line(const struct tpg_data *tpg, unsigned y,
				    unsigned field)
{
	y += tpg->compose.top;
	switch (field) {
	case V4L2_FIELD_SEQ_TB:
		if (y & 1)
			return tpg->buf_height / 2 + y / 2;
		return y / 2;
	case V4L2_FIELD_SEQ_BT:
		if (y & 1)
			return y / 2;
		return tpg->buf_height / 2 + y / 2;
	default:
		return y;
	}
}

static void tpg_recalc(struct tpg_data *tpg)
{
	if (tpg->recalc_colors) {
		tpg->recalc_colors = false;
		tpg->recalc_lines = true;
		tpg->real_xfer_func = tpg->xfer_func;
		tpg->real_ycbcr_enc = tpg->ycbcr_enc;
		tpg->real_hsv_enc = tpg->hsv_enc;
		tpg->real_quantization = tpg->quantization;

		if (tpg->xfer_func == V4L2_XFER_FUNC_DEFAULT)
			tpg->real_xfer_func =
				V4L2_MAP_XFER_FUNC_DEFAULT(tpg->colorspace);

		if (tpg->ycbcr_enc == V4L2_YCBCR_ENC_DEFAULT)
			tpg->real_ycbcr_enc =
				V4L2_MAP_YCBCR_ENC_DEFAULT(tpg->colorspace);

		if (tpg->quantization == V4L2_QUANTIZATION_DEFAULT)
			tpg->real_quantization =
				V4L2_MAP_QUANTIZATION_DEFAULT(
					tpg->color_enc != TGP_COLOR_ENC_YCBCR,
					tpg->colorspace, tpg->real_ycbcr_enc);

		tpg_precalculate_colors(tpg);
	}
	if (tpg->recalc_square_border) {
		tpg->recalc_square_border = false;
		tpg_calculate_square_border(tpg);
	}
	if (tpg->recalc_lines) {
		tpg->recalc_lines = false;
		tpg_precalculate_line(tpg);
	}
}

void tpg_calc_text_basep(struct tpg_data *tpg,
		u8 *basep[TPG_MAX_PLANES][2], unsigned p, u8 *vbuf)
{
	unsigned stride = tpg->bytesperline[p];
	unsigned h = tpg->buf_height;

	tpg_recalc(tpg);

	basep[p][0] = vbuf;
	basep[p][1] = vbuf;
	h /= tpg->vdownsampling[p];
	if (tpg->field == V4L2_FIELD_SEQ_TB)
		basep[p][1] += h * stride / 2;
	else if (tpg->field == V4L2_FIELD_SEQ_BT)
		basep[p][0] += h * stride / 2;
	if (p == 0 && tpg->interleaved)
		tpg_calc_text_basep(tpg, basep, 1, vbuf);
}
EXPORT_SYMBOL_GPL(tpg_calc_text_basep);

static int tpg_pattern_avg(const struct tpg_data *tpg,
			   unsigned pat1, unsigned pat2)
{
	unsigned pat_lines = tpg_get_pat_lines(tpg);

	if (pat1 == (pat2 + 1) % pat_lines)
		return pat2;
	if (pat2 == (pat1 + 1) % pat_lines)
		return pat1;
	return -1;
}

static const char *tpg_color_enc_str(enum tgp_color_enc
						 color_enc)
{
	switch (color_enc) {
	case TGP_COLOR_ENC_HSV:
		return "HSV";
	case TGP_COLOR_ENC_YCBCR:
		return "Y'CbCr";
	case TGP_COLOR_ENC_LUMA:
		return "Luma";
	case TGP_COLOR_ENC_RGB:
	default:
		return "R'G'B";

	}
}

void tpg_log_status(struct tpg_data *tpg)
{
	pr_info("tpg source WxH: %ux%u (%s)\n",
		tpg->src_width, tpg->src_height,
		tpg_color_enc_str(tpg->color_enc));
	pr_info("tpg field: %u\n", tpg->field);
	pr_info("tpg crop: %ux%u@%dx%d\n", tpg->crop.width, tpg->crop.height,
			tpg->crop.left, tpg->crop.top);
	pr_info("tpg compose: %ux%u@%dx%d\n", tpg->compose.width, tpg->compose.height,
			tpg->compose.left, tpg->compose.top);
	pr_info("tpg colorspace: %d\n", tpg->colorspace);
	pr_info("tpg transfer function: %d/%d\n", tpg->xfer_func, tpg->real_xfer_func);
	if (tpg->color_enc == TGP_COLOR_ENC_HSV)
		pr_info("tpg HSV encoding: %d/%d\n",
			tpg->hsv_enc, tpg->real_hsv_enc);
	else if (tpg->color_enc == TGP_COLOR_ENC_YCBCR)
		pr_info("tpg Y'CbCr encoding: %d/%d\n",
			tpg->ycbcr_enc, tpg->real_ycbcr_enc);
	pr_info("tpg quantization: %d/%d\n", tpg->quantization, tpg->real_quantization);
	pr_info("tpg RGB range: %d/%d\n", tpg->rgb_range, tpg->real_rgb_range);
}
EXPORT_SYMBOL_GPL(tpg_log_status);

/*
 * This struct contains common parameters used by both the drawing of the
 * test pattern and the drawing of the extras (borders, square, etc.)
 */
struct tpg_draw_params {
	/* common data */
	bool is_tv;
	bool is_60hz;
	unsigned twopixsize;
	unsigned img_width;
	unsigned stride;
	unsigned hmax;
	unsigned frame_line;
	unsigned frame_line_next;

	/* test pattern */
	unsigned mv_hor_old;
	unsigned mv_hor_new;
	unsigned mv_vert_old;
	unsigned mv_vert_new;

	/* extras */
	unsigned wss_width;
	unsigned wss_random_offset;
	unsigned sav_eav_f;
	unsigned left_pillar_width;
	unsigned right_pillar_start;
};

static void tpg_fill_params_pattern(const struct tpg_data *tpg, unsigned p,
				    struct tpg_draw_params *params)
{
	params->mv_hor_old =
		tpg_hscale_div(tpg, p, tpg->mv_hor_count % tpg->src_width);
	params->mv_hor_new =
		tpg_hscale_div(tpg, p, (tpg->mv_hor_count + tpg->mv_hor_step) %
			       tpg->src_width);
	params->mv_vert_old = tpg->mv_vert_count % tpg->src_height;
	params->mv_vert_new =
		(tpg->mv_vert_count + tpg->mv_vert_step) % tpg->src_height;
}

static void tpg_fill_params_extras(const struct tpg_data *tpg,
				   unsigned p,
				   struct tpg_draw_params *params)
{
	unsigned left_pillar_width = 0;
	unsigned right_pillar_start = params->img_width;

	params->wss_width = tpg->crop.left < tpg->src_width / 2 ?
		tpg->src_width / 2 - tpg->crop.left : 0;
	if (params->wss_width > tpg->crop.width)
		params->wss_width = tpg->crop.width;
	params->wss_width = tpg_hscale_div(tpg, p, params->wss_width);
	params->wss_random_offset =
		params->twopixsize * get_random_u32_below(tpg->src_width / 2);

	if (tpg->crop.left < tpg->border.left) {
		left_pillar_width = tpg->border.left - tpg->crop.left;
		if (left_pillar_width > tpg->crop.width)
			left_pillar_width = tpg->crop.width;
		left_pillar_width = tpg_hscale_div(tpg, p, left_pillar_width);
	}
	params->left_pillar_width = left_pillar_width;

	if (tpg->crop.left + tpg->crop.width >
	    tpg->border.left + tpg->border.width) {
		right_pillar_start =
			tpg->border.left + tpg->border.width - tpg->crop.left;
		right_pillar_start =
			tpg_hscale_div(tpg, p, right_pillar_start);
		if (right_pillar_start > params->img_width)
			right_pillar_start = params->img_width;
	}
	params->right_pillar_start = right_pillar_start;

	params->sav_eav_f = tpg->field ==
			(params->is_60hz ? V4L2_FIELD_TOP : V4L2_FIELD_BOTTOM);
}

static void tpg_fill_plane_extras(const struct tpg_data *tpg,
				  const struct tpg_draw_params *params,
				  unsigned p, unsigned h, u8 *vbuf)
{
	unsigned twopixsize = params->twopixsize;
	unsigned img_width = params->img_width;
	unsigned frame_line = params->frame_line;
	const struct v4l2_rect *sq = &tpg->square;
	const struct v4l2_rect *b = &tpg->border;
	const struct v4l2_rect *c = &tpg->crop;

	if (params->is_tv && !params->is_60hz &&
	    frame_line == 0 && params->wss_width) {
		/*
		 * Replace the first half of the top line of a 50 Hz frame
		 * with random data to simulate a WSS signal.
		 */
		u8 *wss = tpg->random_line[p] + params->wss_random_offset;

		memcpy(vbuf, wss, params->wss_width);
	}

	if (tpg->show_border && frame_line >= b->top &&
	    frame_line < b->top + b->height) {
		unsigned bottom = b->top + b->height - 1;
		unsigned left = params->left_pillar_width;
		unsigned right = params->right_pillar_start;

		if (frame_line == b->top || frame_line == b->top + 1 ||
		    frame_line == bottom || frame_line == bottom - 1) {
			memcpy(vbuf + left, tpg->contrast_line[p],
					right - left);
		} else {
			if (b->left >= c->left &&
			    b->left < c->left + c->width)
				memcpy(vbuf + left,
					tpg->contrast_line[p], twopixsize);
			if (b->left + b->width > c->left &&
			    b->left + b->width <= c->left + c->width)
				memcpy(vbuf + right - twopixsize,
					tpg->contrast_line[p], twopixsize);
		}
	}
	if (tpg->qual != TPG_QUAL_NOISE && frame_line >= b->top &&
	    frame_line < b->top + b->height) {
		memcpy(vbuf, tpg->black_line[p], params->left_pillar_width);
		memcpy(vbuf + params->right_pillar_start, tpg->black_line[p],
		       img_width - params->right_pillar_start);
	}
	if (tpg->show_square && frame_line >= sq->top &&
	    frame_line < sq->top + sq->height &&
	    sq->left < c->left + c->width &&
	    sq->left + sq->width >= c->left) {
		unsigned left = sq->left;
		unsigned width = sq->width;

		if (c->left > left) {
			width -= c->left - left;
			left = c->left;
		}
		if (c->left + c->width < left + width)
			width -= left + width - c->left - c->width;
		left -= c->left;
		left = tpg_hscale_div(tpg, p, left);
		width = tpg_hscale_div(tpg, p, width);
		memcpy(vbuf + left, tpg->contrast_line[p], width);
	}
	if (tpg->insert_sav) {
		unsigned offset = tpg_hdiv(tpg, p, tpg->compose.width / 3);
		u8 *p = vbuf + offset;
		unsigned vact = 0, hact = 0;

		p[0] = 0xff;
		p[1] = 0;
		p[2] = 0;
		p[3] = 0x80 | (params->sav_eav_f << 6) |
			(vact << 5) | (hact << 4) |
			((hact ^ vact) << 3) |
			((hact ^ params->sav_eav_f) << 2) |
			((params->sav_eav_f ^ vact) << 1) |
			(hact ^ vact ^ params->sav_eav_f);
	}
	if (tpg->insert_eav) {
		unsigned offset = tpg_hdiv(tpg, p, tpg->compose.width * 2 / 3);
		u8 *p = vbuf + offset;
		unsigned vact = 0, hact = 1;

		p[0] = 0xff;
		p[1] = 0;
		p[2] = 0;
		p[3] = 0x80 | (params->sav_eav_f << 6) |
			(vact << 5) | (hact << 4) |
			((hact ^ vact) << 3) |
			((hact ^ params->sav_eav_f) << 2) |
			((params->sav_eav_f ^ vact) << 1) |
			(hact ^ vact ^ params->sav_eav_f);
	}
	if (tpg->insert_hdmi_video_guard_band) {
		unsigned int i;

		switch (tpg->fourcc) {
		case V4L2_PIX_FMT_BGR24:
		case V4L2_PIX_FMT_RGB24:
			for (i = 0; i < 3 * 4; i += 3) {
				vbuf[i] = 0xab;
				vbuf[i + 1] = 0x55;
				vbuf[i + 2] = 0xab;
			}
			break;
		case V4L2_PIX_FMT_RGB32:
		case V4L2_PIX_FMT_ARGB32:
		case V4L2_PIX_FMT_XRGB32:
		case V4L2_PIX_FMT_BGRX32:
		case V4L2_PIX_FMT_BGRA32:
			for (i = 0; i < 4 * 4; i += 4) {
				vbuf[i] = 0x00;
				vbuf[i + 1] = 0xab;
				vbuf[i + 2] = 0x55;
				vbuf[i + 3] = 0xab;
			}
			break;
		case V4L2_PIX_FMT_BGR32:
		case V4L2_PIX_FMT_XBGR32:
		case V4L2_PIX_FMT_ABGR32:
		case V4L2_PIX_FMT_RGBX32:
		case V4L2_PIX_FMT_RGBA32:
			for (i = 0; i < 4 * 4; i += 4) {
				vbuf[i] = 0xab;
				vbuf[i + 1] = 0x55;
				vbuf[i + 2] = 0xab;
				vbuf[i + 3] = 0x00;
			}
			break;
		}
	}
}

static void tpg_fill_plane_pattern(const struct tpg_data *tpg,
				   const struct tpg_draw_params *params,
				   unsigned p, unsigned h, u8 *vbuf)
{
	unsigned twopixsize = params->twopixsize;
	unsigned img_width = params->img_width;
	unsigned mv_hor_old = params->mv_hor_old;
	unsigned mv_hor_new = params->mv_hor_new;
	unsigned mv_vert_old = params->mv_vert_old;
	unsigned mv_vert_new = params->mv_vert_new;
	unsigned frame_line = params->frame_line;
	unsigned frame_line_next = params->frame_line_next;
	unsigned line_offset = tpg_hscale_div(tpg, p, tpg->crop.left);
	bool even;
	bool fill_blank = false;
	unsigned pat_line_old;
	unsigned pat_line_new;
	u8 *linestart_older;
	u8 *linestart_newer;
	u8 *linestart_top;
	u8 *linestart_bottom;

	even = !(frame_line & 1);

	if (h >= params->hmax) {
		if (params->hmax == tpg->compose.height)
			return;
		if (!tpg->perc_fill_blank)
			return;
		fill_blank = true;
	}

	if (tpg->vflip) {
		frame_line = tpg->src_height - frame_line - 1;
		frame_line_next = tpg->src_height - frame_line_next - 1;
	}

	if (fill_blank) {
		linestart_older = tpg->contrast_line[p];
		linestart_newer = tpg->contrast_line[p];
	} else if (tpg->qual != TPG_QUAL_NOISE &&
		   (frame_line < tpg->border.top ||
		    frame_line >= tpg->border.top + tpg->border.height)) {
		linestart_older = tpg->black_line[p];
		linestart_newer = tpg->black_line[p];
	} else if (tpg->pattern == TPG_PAT_NOISE || tpg->qual == TPG_QUAL_NOISE) {
		linestart_older = tpg->random_line[p] +
				  twopixsize * get_random_u32_below(tpg->src_width / 2);
		linestart_newer = tpg->random_line[p] +
				  twopixsize * get_random_u32_below(tpg->src_width / 2);
	} else {
		unsigned frame_line_old =
			(frame_line + mv_vert_old) % tpg->src_height;
		unsigned frame_line_new =
			(frame_line + mv_vert_new) % tpg->src_height;
		unsigned pat_line_next_old;
		unsigned pat_line_next_new;

		pat_line_old = tpg_get_pat_line(tpg, frame_line_old);
		pat_line_new = tpg_get_pat_line(tpg, frame_line_new);
		linestart_older = tpg->lines[pat_line_old][p] + mv_hor_old;
		linestart_newer = tpg->lines[pat_line_new][p] + mv_hor_new;

		if (tpg->vdownsampling[p] > 1 && frame_line != frame_line_next) {
			int avg_pat;

			/*
			 * Now decide whether we need to use downsampled_lines[].
			 * That's necessary if the two lines use different patterns.
			 */
			pat_line_next_old = tpg_get_pat_line(tpg,
					(frame_line_next + mv_vert_old) % tpg->src_height);
			pat_line_next_new = tpg_get_pat_line(tpg,
					(frame_line_next + mv_vert_new) % tpg->src_height);

			switch (tpg->field) {
			case V4L2_FIELD_INTERLACED:
			case V4L2_FIELD_INTERLACED_BT:
			case V4L2_FIELD_INTERLACED_TB:
				avg_pat = tpg_pattern_avg(tpg, pat_line_old, pat_line_new);
				if (avg_pat < 0)
					break;
				linestart_older = tpg->downsampled_lines[avg_pat][p] + mv_hor_old;
				linestart_newer = linestart_older;
				break;
			case V4L2_FIELD_NONE:
			case V4L2_FIELD_TOP:
			case V4L2_FIELD_BOTTOM:
			case V4L2_FIELD_SEQ_BT:
			case V4L2_FIELD_SEQ_TB:
				avg_pat = tpg_pattern_avg(tpg, pat_line_old, pat_line_next_old);
				if (avg_pat >= 0)
					linestart_older = tpg->downsampled_lines[avg_pat][p] +
						mv_hor_old;
				avg_pat = tpg_pattern_avg(tpg, pat_line_new, pat_line_next_new);
				if (avg_pat >= 0)
					linestart_newer = tpg->downsampled_lines[avg_pat][p] +
						mv_hor_new;
				break;
			}
		}
		linestart_older += line_offset;
		linestart_newer += line_offset;
	}
	if (tpg->field_alternate) {
		linestart_top = linestart_bottom = linestart_older;
	} else if (params->is_60hz) {
		linestart_top = linestart_newer;
		linestart_bottom = linestart_older;
	} else {
		linestart_top = linestart_older;
		linestart_bottom = linestart_newer;
	}

	switch (tpg->field) {
	case V4L2_FIELD_INTERLACED:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_SEQ_TB:
	case V4L2_FIELD_SEQ_BT:
		if (even)
			memcpy(vbuf, linestart_top, img_width);
		else
			memcpy(vbuf, linestart_bottom, img_width);
		break;
	case V4L2_FIELD_INTERLACED_BT:
		if (even)
			memcpy(vbuf, linestart_bottom, img_width);
		else
			memcpy(vbuf, linestart_top, img_width);
		break;
	case V4L2_FIELD_TOP:
		memcpy(vbuf, linestart_top, img_width);
		break;
	case V4L2_FIELD_BOTTOM:
		memcpy(vbuf, linestart_bottom, img_width);
		break;
	case V4L2_FIELD_NONE:
	default:
		memcpy(vbuf, linestart_older, img_width);
		break;
	}
}

void tpg_fill_plane_buffer(struct tpg_data *tpg, v4l2_std_id std,
			   unsigned p, u8 *vbuf)
{
	struct tpg_draw_params params;
	unsigned factor = V4L2_FIELD_HAS_T_OR_B(tpg->field) ? 2 : 1;

	/* Coarse scaling with Bresenham */
	unsigned int_part = (tpg->crop.height / factor) / tpg->compose.height;
	unsigned fract_part = (tpg->crop.height / factor) % tpg->compose.height;
	unsigned src_y = 0;
	unsigned error = 0;
	unsigned h;

	tpg_recalc(tpg);

	params.is_tv = std;
	params.is_60hz = std & V4L2_STD_525_60;
	params.twopixsize = tpg->twopixelsize[p];
	params.img_width = tpg_hdiv(tpg, p, tpg->compose.width);
	params.stride = tpg->bytesperline[p];
	params.hmax = (tpg->compose.height * tpg->perc_fill) / 100;

	tpg_fill_params_pattern(tpg, p, &params);
	tpg_fill_params_extras(tpg, p, &params);

	vbuf += tpg_hdiv(tpg, p, tpg->compose.left);

	for (h = 0; h < tpg->compose.height; h++) {
		unsigned buf_line;

		params.frame_line = tpg_calc_frameline(tpg, src_y, tpg->field);
		params.frame_line_next = params.frame_line;
		buf_line = tpg_calc_buffer_line(tpg, h, tpg->field);
		src_y += int_part;
		error += fract_part;
		if (error >= tpg->compose.height) {
			error -= tpg->compose.height;
			src_y++;
		}

		/*
		 * For line-interleaved formats determine the 'plane'
		 * based on the buffer line.
		 */
		if (tpg_g_interleaved(tpg))
			p = tpg_g_interleaved_plane(tpg, buf_line);

		if (tpg->vdownsampling[p] > 1) {
			/*
			 * When doing vertical downsampling the field setting
			 * matters: for SEQ_BT/TB we downsample each field
			 * separately (i.e. lines 0+2 are combined, as are
			 * lines 1+3), for the other field settings we combine
			 * odd and even lines. Doing that for SEQ_BT/TB would
			 * be really weird.
			 */
			if (tpg->field == V4L2_FIELD_SEQ_BT ||
			    tpg->field == V4L2_FIELD_SEQ_TB) {
				unsigned next_src_y = src_y;

				if ((h & 3) >= 2)
					continue;
				next_src_y += int_part;
				if (error + fract_part >= tpg->compose.height)
					next_src_y++;
				params.frame_line_next =
					tpg_calc_frameline(tpg, next_src_y, tpg->field);
			} else {
				if (h & 1)
					continue;
				params.frame_line_next =
					tpg_calc_frameline(tpg, src_y, tpg->field);
			}

			buf_line /= tpg->vdownsampling[p];
		}
		tpg_fill_plane_pattern(tpg, &params, p, h,
				vbuf + buf_line * params.stride);
		tpg_fill_plane_extras(tpg, &params, p, h,
				vbuf + buf_line * params.stride);
	}
}
EXPORT_SYMBOL_GPL(tpg_fill_plane_buffer);

void tpg_fillbuffer(struct tpg_data *tpg, v4l2_std_id std, unsigned p, u8 *vbuf)
{
	unsigned offset = 0;
	unsigned i;

	if (tpg->buffers > 1) {
		tpg_fill_plane_buffer(tpg, std, p, vbuf);
		return;
	}

	for (i = 0; i < tpg_g_planes(tpg); i++) {
		tpg_fill_plane_buffer(tpg, std, i, vbuf + offset);
		offset += tpg_calc_plane_size(tpg, i);
	}
}
EXPORT_SYMBOL_GPL(tpg_fillbuffer);

MODULE_DESCRIPTION("V4L2 Test Pattern Generator");
MODULE_AUTHOR("Hans Verkuil");
MODULE_LICENSE("GPL");
