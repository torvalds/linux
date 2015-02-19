/*
 * vivid-tpg.c - Test Pattern Generator
 *
 * Note: gen_twopix and tpg_gen_text are based on code from vivi.c. See the
 * vivi.c source for the copyright information of those functions.
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

#include "vivid-tpg.h"

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
	"1x1 Checkers",
	"Alternating Hor Lines",
	"Alternating Vert Lines",
	"One Pixel Wide Cross",
	"Two Pixels Wide Cross",
	"Ten Pixels Wide Cross",
	"Gray Ramp",
	"Noise",
	NULL
};

/* Must remain in sync with enum tpg_aspect */
const char * const tpg_aspect_strings[] = {
	"Source Width x Height",
	"4x3",
	"14x9",
	"16x9",
	"16x9 Anamorphic",
	NULL
};

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
}

int tpg_alloc(struct tpg_data *tpg, unsigned max_w)
{
	unsigned pat;
	unsigned plane;

	tpg->max_line_width = max_w;
	for (pat = 0; pat < TPG_MAX_PAT_LINES; pat++) {
		for (plane = 0; plane < TPG_MAX_PLANES; plane++) {
			unsigned pixelsz = plane ? 1 : 4;

			tpg->lines[pat][plane] = vzalloc(max_w * 2 * pixelsz);
			if (!tpg->lines[pat][plane])
				return -ENOMEM;
		}
	}
	for (plane = 0; plane < TPG_MAX_PLANES; plane++) {
		unsigned pixelsz = plane ? 1 : 4;

		tpg->contrast_line[plane] = vzalloc(max_w * pixelsz);
		if (!tpg->contrast_line[plane])
			return -ENOMEM;
		tpg->black_line[plane] = vzalloc(max_w * pixelsz);
		if (!tpg->black_line[plane])
			return -ENOMEM;
		tpg->random_line[plane] = vzalloc(max_w * 2 * pixelsz);
		if (!tpg->random_line[plane])
			return -ENOMEM;
	}
	return 0;
}

void tpg_free(struct tpg_data *tpg)
{
	unsigned pat;
	unsigned plane;

	for (pat = 0; pat < TPG_MAX_PAT_LINES; pat++)
		for (plane = 0; plane < TPG_MAX_PLANES; plane++) {
			vfree(tpg->lines[pat][plane]);
			tpg->lines[pat][plane] = NULL;
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

bool tpg_s_fourcc(struct tpg_data *tpg, u32 fourcc)
{
	tpg->fourcc = fourcc;
	tpg->planes = 1;
	tpg->recalc_colors = true;
	switch (fourcc) {
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB565X:
	case V4L2_PIX_FMT_RGB555:
	case V4L2_PIX_FMT_XRGB555:
	case V4L2_PIX_FMT_ARGB555:
	case V4L2_PIX_FMT_RGB555X:
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_BGR32:
	case V4L2_PIX_FMT_XRGB32:
	case V4L2_PIX_FMT_XBGR32:
	case V4L2_PIX_FMT_ARGB32:
	case V4L2_PIX_FMT_ABGR32:
		tpg->is_yuv = false;
		break;
	case V4L2_PIX_FMT_NV16M:
	case V4L2_PIX_FMT_NV61M:
		tpg->planes = 2;
		/* fall-through */
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_VYUY:
		tpg->is_yuv = true;
		break;
	default:
		return false;
	}

	switch (fourcc) {
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB565X:
	case V4L2_PIX_FMT_RGB555:
	case V4L2_PIX_FMT_XRGB555:
	case V4L2_PIX_FMT_ARGB555:
	case V4L2_PIX_FMT_RGB555X:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_VYUY:
		tpg->twopixelsize[0] = 2 * 2;
		break;
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
		tpg->twopixelsize[0] = 2 * 3;
		break;
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_BGR32:
	case V4L2_PIX_FMT_XRGB32:
	case V4L2_PIX_FMT_XBGR32:
	case V4L2_PIX_FMT_ARGB32:
	case V4L2_PIX_FMT_ABGR32:
		tpg->twopixelsize[0] = 2 * 4;
		break;
	case V4L2_PIX_FMT_NV16M:
	case V4L2_PIX_FMT_NV61M:
		tpg->twopixelsize[0] = 2;
		tpg->twopixelsize[1] = 2;
		break;
	}
	return true;
}

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
		tpg->bytesperline[p] = width * tpg->twopixelsize[p] / 2;
	tpg->recalc_square_border = true;
}

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
		{ COEFF(0.299, 219),  COEFF(0.587, 219),  COEFF(0.114, 219)  },
		{ COEFF(-0.169, 224), COEFF(-0.331, 224), COEFF(0.5, 224)    },
		{ COEFF(0.5, 224),    COEFF(-0.419, 224), COEFF(-0.081, 224) },
	};
	static const int bt601_full[3][3] = {
		{ COEFF(0.299, 255),  COEFF(0.587, 255),  COEFF(0.114, 255)  },
		{ COEFF(-0.169, 255), COEFF(-0.331, 255), COEFF(0.5, 255)    },
		{ COEFF(0.5, 255),    COEFF(-0.419, 255), COEFF(-0.081, 255) },
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
	static const int bt2020[3][3] = {
		{ COEFF(0.2726, 219),  COEFF(0.6780, 219),  COEFF(0.0593, 219)  },
		{ COEFF(-0.1396, 224), COEFF(-0.3604, 224), COEFF(0.5, 224)     },
		{ COEFF(0.5, 224),     COEFF(-0.4629, 224), COEFF(-0.0405, 224) },
	};
	bool full = tpg->real_quantization == V4L2_QUANTIZATION_FULL_RANGE;
	unsigned y_offset = full ? 0 : 16;
	int lin_y, yc;

	switch (tpg->real_ycbcr_enc) {
	case V4L2_YCBCR_ENC_601:
	case V4L2_YCBCR_ENC_XV601:
	case V4L2_YCBCR_ENC_SYCC:
		rgb2ycbcr(full ? bt601_full : bt601, r, g, b, y_offset, y, cb, cr);
		break;
	case V4L2_YCBCR_ENC_BT2020:
		rgb2ycbcr(bt2020, r, g, b, 16, y, cb, cr);
		break;
	case V4L2_YCBCR_ENC_BT2020_CONST_LUM:
		lin_y = (COEFF(0.2627, 255) * rec709_to_linear(r) +
			 COEFF(0.6780, 255) * rec709_to_linear(g) +
			 COEFF(0.0593, 255) * rec709_to_linear(b)) >> 16;
		yc = linear_to_rec709(lin_y);
		*y = (yc * 219) / 255 + (16 << 4);
		if (b <= yc)
			*cb = (((b - yc) * COEFF(1.0 / 1.9404, 224)) >> 16) + (128 << 4);
		else
			*cb = (((b - yc) * COEFF(1.0 / 1.5816, 224)) >> 16) + (128 << 4);
		if (r <= yc)
			*cr = (((r - yc) * COEFF(1.0 / 1.7184, 224)) >> 16) + (128 << 4);
		else
			*cr = (((r - yc) * COEFF(1.0 / 0.9936, 224)) >> 16) + (128 << 4);
		break;
	case V4L2_YCBCR_ENC_SMPTE240M:
		rgb2ycbcr(smpte240m, r, g, b, 16, y, cb, cr);
		break;
	case V4L2_YCBCR_ENC_709:
	case V4L2_YCBCR_ENC_XV709:
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
	static const int bt2020[3][3] = {
		{ COEFF(1, 219), COEFF(0, 224),       COEFF(1.4746, 224)  },
		{ COEFF(1, 219), COEFF(-0.1646, 224), COEFF(-0.5714, 224) },
		{ COEFF(1, 219), COEFF(1.8814, 224),  COEFF(0, 224)       },
	};
	bool full = tpg->real_quantization == V4L2_QUANTIZATION_FULL_RANGE;
	unsigned y_offset = full ? 0 : 16;
	int lin_r, lin_g, lin_b, lin_y;

	switch (tpg->real_ycbcr_enc) {
	case V4L2_YCBCR_ENC_601:
	case V4L2_YCBCR_ENC_XV601:
	case V4L2_YCBCR_ENC_SYCC:
		ycbcr2rgb(full ? bt601_full : bt601, y, cb, cr, y_offset, r, g, b);
		break;
	case V4L2_YCBCR_ENC_BT2020:
		ycbcr2rgb(bt2020, y, cb, cr, 16, r, g, b);
		break;
	case V4L2_YCBCR_ENC_BT2020_CONST_LUM:
		y -= 16 << 4;
		cb -= 128 << 4;
		cr -= 128 << 4;

		if (cb <= 0)
			*b = COEFF(1.0, 219) * y + COEFF(1.9404, 224) * cb;
		else
			*b = COEFF(1.0, 219) * y + COEFF(1.5816, 224) * cb;
		*b = *b >> 12;
		if (cr <= 0)
			*r = COEFF(1.0, 219) * y + COEFF(1.7184, 224) * cr;
		else
			*r = COEFF(1.0, 219) * y + COEFF(0.9936, 224) * cr;
		*r = *r >> 12;
		lin_r = rec709_to_linear(*r);
		lin_b = rec709_to_linear(*b);
		lin_y = rec709_to_linear((y * 255) / 219);

		lin_g = COEFF(1.0 / 0.6780, 255) * lin_y -
			COEFF(0.2627 / 0.6780, 255) * lin_r -
			COEFF(0.0593 / 0.6780, 255) * lin_b;
		*g = linear_to_rec709(lin_g >> 12);
		break;
	case V4L2_YCBCR_ENC_SMPTE240M:
		ycbcr2rgb(smpte240m, y, cb, cr, 16, r, g, b);
		break;
	case V4L2_YCBCR_ENC_709:
	case V4L2_YCBCR_ENC_XV709:
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
		r = g = b = prandom_u32_max(256);
	} else if (k == TPG_COLOR_RANDOM) {
		r = g = b = tpg->qual_offset + prandom_u32_max(196);
	} else if (k >= TPG_COLOR_RAMP) {
		r = g = b = k - TPG_COLOR_RAMP;
	}

	if (tpg->pattern == TPG_PAT_CSC_COLORBAR && col <= TPG_COLOR_CSC_BLACK) {
		r = tpg_csc_colors[tpg->colorspace][col].r;
		g = tpg_csc_colors[tpg->colorspace][col].g;
		b = tpg_csc_colors[tpg->colorspace][col].b;
	} else {
		r <<= 4;
		g <<= 4;
		b <<= 4;
	}
	if (tpg->qual == TPG_QUAL_GRAY) {
		/* Rec. 709 Luma function */
		/* (0.2126, 0.7152, 0.0722) * (255 * 256) */
		r = g = b = ((13879 * r + 46688 * g + 4713 * b) >> 16) + (16 << 4);
	}

	/*
	 * The assumption is that the RGB output is always full range,
	 * so only if the rgb_range overrides the 'real' rgb range do
	 * we need to convert the RGB values.
	 *
	 * Remember that r, g and b are still in the 0 - 0xff0 range.
	 */
	if (tpg->real_rgb_range == V4L2_DV_RGB_RANGE_LIMITED &&
	    tpg->rgb_range == V4L2_DV_RGB_RANGE_FULL) {
		/*
		 * Convert from full range (which is what r, g and b are)
		 * to limited range (which is the 'real' RGB range), which
		 * is then interpreted as full range.
		 */
		r = (r * 219) / 255 + (16 << 4);
		g = (g * 219) / 255 + (16 << 4);
		b = (b * 219) / 255 + (16 << 4);
	} else if (tpg->real_rgb_range != V4L2_DV_RGB_RANGE_LIMITED &&
		   tpg->rgb_range == V4L2_DV_RGB_RANGE_LIMITED) {
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

	if (tpg->brightness != 128 || tpg->contrast != 128 ||
	    tpg->saturation != 128 || tpg->hue) {
		/* Implement these operations */
		int y, cb, cr;
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
		if (tpg->is_yuv) {
			tpg->colors[k][0] = clamp(y >> 4, 1, 254);
			tpg->colors[k][1] = clamp(cb >> 4, 1, 254);
			tpg->colors[k][2] = clamp(cr >> 4, 1, 254);
			return;
		}
		ycbcr_to_color(tpg, y, cb, cr, &r, &g, &b);
	}

	if (tpg->is_yuv) {
		/* Convert to YCbCr */
		int y, cb, cr;

		color_to_ycbcr(tpg, r, g, b, &y, &cb, &cr);

		if (tpg->real_quantization == V4L2_QUANTIZATION_LIM_RANGE) {
			y = clamp(y, 16 << 4, 235 << 4);
			cb = clamp(cb, 16 << 4, 240 << 4);
			cr = clamp(cr, 16 << 4, 240 << 4);
		}
		tpg->colors[k][0] = clamp(y >> 4, 1, 254);
		tpg->colors[k][1] = clamp(cb >> 4, 1, 254);
		tpg->colors[k][2] = clamp(cr >> 4, 1, 254);
	} else {
		if (tpg->real_quantization == V4L2_QUANTIZATION_LIM_RANGE) {
			r = (r * 219) / 255 + (16 << 4);
			g = (g * 219) / 255 + (16 << 4);
			b = (b * 219) / 255 + (16 << 4);
		}
		switch (tpg->fourcc) {
		case V4L2_PIX_FMT_RGB565:
		case V4L2_PIX_FMT_RGB565X:
			r >>= 7;
			g >>= 6;
			b >>= 7;
			break;
		case V4L2_PIX_FMT_RGB555:
		case V4L2_PIX_FMT_XRGB555:
		case V4L2_PIX_FMT_ARGB555:
		case V4L2_PIX_FMT_RGB555X:
			r >>= 7;
			g >>= 7;
			b >>= 7;
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
	u8 r_y, g_u, b_v;

	if (tpg->alpha_red_only && color != TPG_COLOR_CSC_RED &&
				   color != TPG_COLOR_100_RED &&
				   color != TPG_COLOR_75_RED)
		alpha = 0;
	if (color == TPG_COLOR_RANDOM)
		precalculate_color(tpg, color);
	r_y = tpg->colors[color][0]; /* R or precalculated Y */
	g_u = tpg->colors[color][1]; /* G or precalculated U */
	b_v = tpg->colors[color][2]; /* B or precalculated V */

	switch (tpg->fourcc) {
	case V4L2_PIX_FMT_NV16M:
		buf[0][offset] = r_y;
		buf[1][offset] = odd ? b_v : g_u;
		break;
	case V4L2_PIX_FMT_NV61M:
		buf[0][offset] = r_y;
		buf[1][offset] = odd ? g_u : b_v;
		break;

	case V4L2_PIX_FMT_YUYV:
		buf[0][offset] = r_y;
		buf[0][offset + 1] = odd ? b_v : g_u;
		break;
	case V4L2_PIX_FMT_UYVY:
		buf[0][offset] = odd ? b_v : g_u;
		buf[0][offset + 1] = r_y;
		break;
	case V4L2_PIX_FMT_YVYU:
		buf[0][offset] = r_y;
		buf[0][offset + 1] = odd ? g_u : b_v;
		break;
	case V4L2_PIX_FMT_VYUY:
		buf[0][offset] = odd ? g_u : b_v;
		buf[0][offset + 1] = r_y;
		break;
	case V4L2_PIX_FMT_RGB565:
		buf[0][offset] = (g_u << 5) | b_v;
		buf[0][offset + 1] = (r_y << 3) | (g_u >> 3);
		break;
	case V4L2_PIX_FMT_RGB565X:
		buf[0][offset] = (r_y << 3) | (g_u >> 3);
		buf[0][offset + 1] = (g_u << 5) | b_v;
		break;
	case V4L2_PIX_FMT_RGB555:
	case V4L2_PIX_FMT_XRGB555:
		alpha = 0;
		/* fall through */
	case V4L2_PIX_FMT_ARGB555:
		buf[0][offset] = (g_u << 5) | b_v;
		buf[0][offset + 1] = (alpha & 0x80) | (r_y << 2) | (g_u >> 3);
		break;
	case V4L2_PIX_FMT_RGB555X:
		buf[0][offset] = (alpha & 0x80) | (r_y << 2) | (g_u >> 3);
		buf[0][offset + 1] = (g_u << 5) | b_v;
		break;
	case V4L2_PIX_FMT_RGB24:
		buf[0][offset] = r_y;
		buf[0][offset + 1] = g_u;
		buf[0][offset + 2] = b_v;
		break;
	case V4L2_PIX_FMT_BGR24:
		buf[0][offset] = b_v;
		buf[0][offset + 1] = g_u;
		buf[0][offset + 2] = r_y;
		break;
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_XRGB32:
		alpha = 0;
		/* fall through */
	case V4L2_PIX_FMT_ARGB32:
		buf[0][offset] = alpha;
		buf[0][offset + 1] = r_y;
		buf[0][offset + 2] = g_u;
		buf[0][offset + 3] = b_v;
		break;
	case V4L2_PIX_FMT_BGR32:
	case V4L2_PIX_FMT_XBGR32:
		alpha = 0;
		/* fall through */
	case V4L2_PIX_FMT_ABGR32:
		buf[0][offset] = b_v;
		buf[0][offset + 1] = g_u;
		buf[0][offset + 2] = r_y;
		buf[0][offset + 3] = alpha;
		break;
	}
}

/* Return how many pattern lines are used by the current pattern. */
static unsigned tpg_get_pat_lines(struct tpg_data *tpg)
{
	switch (tpg->pattern) {
	case TPG_PAT_CHECKERS_16X16:
	case TPG_PAT_CHECKERS_1X1:
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
static unsigned tpg_get_pat_line(struct tpg_data *tpg, unsigned line)
{
	switch (tpg->pattern) {
	case TPG_PAT_CHECKERS_16X16:
		return (line >> 4) & 1;
	case TPG_PAT_CHECKERS_1X1:
	case TPG_PAT_ALTERNATING_HLINES:
		return line & 1;
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
static enum tpg_color tpg_get_color(struct tpg_data *tpg, unsigned pat_line, unsigned x)
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
			u8 pix[TPG_MAX_PLANES][8];

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
				u8 *pos = tpg->lines[pat][p] + x * twopixsize / 2;

				memcpy(pos, pix[p], twopixsize);
			}
		}
	}
	for (x = 0; x < tpg->scaled_width; x += 2) {
		u8 pix[TPG_MAX_PLANES][8];

		gen_twopix(tpg, pix, contrast, 0);
		gen_twopix(tpg, pix, contrast, 1);
		for (p = 0; p < tpg->planes; p++) {
			unsigned twopixsize = tpg->twopixelsize[p];
			u8 *pos = tpg->contrast_line[p] + x * twopixsize / 2;

			memcpy(pos, pix[p], twopixsize);
		}
	}
	for (x = 0; x < tpg->scaled_width; x += 2) {
		u8 pix[TPG_MAX_PLANES][8];

		gen_twopix(tpg, pix, TPG_COLOR_100_BLACK, 0);
		gen_twopix(tpg, pix, TPG_COLOR_100_BLACK, 1);
		for (p = 0; p < tpg->planes; p++) {
			unsigned twopixsize = tpg->twopixelsize[p];
			u8 *pos = tpg->black_line[p] + x * twopixsize / 2;

			memcpy(pos, pix[p], twopixsize);
		}
	}
	for (x = 0; x < tpg->scaled_width * 2; x += 2) {
		u8 pix[TPG_MAX_PLANES][8];

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

void tpg_gen_text(struct tpg_data *tpg, u8 *basep[TPG_MAX_PLANES][2],
		int y, int x, char *text)
{
	int line;
	unsigned step = V4L2_FIELD_HAS_T_OR_B(tpg->field) ? 2 : 1;
	unsigned div = step;
	unsigned first = 0;
	unsigned len = strlen(text);
	unsigned p;

	if (font8x16 == NULL || basep == NULL)
		return;

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
		/* Print stream time */
#define PRINTSTR(PIXTYPE) do {	\
	PIXTYPE fg;	\
	PIXTYPE bg;	\
	memcpy(&fg, tpg->textfg[p], sizeof(PIXTYPE));	\
	memcpy(&bg, tpg->textbg[p], sizeof(PIXTYPE));	\
	\
	for (line = first; line < 16; line += step) {	\
		int l = tpg->vflip ? 15 - line : line; \
		PIXTYPE *pos = (PIXTYPE *)(basep[p][line & 1] + \
			       ((y * step + l) / div) * tpg->bytesperline[p] + \
			       x * sizeof(PIXTYPE));	\
		unsigned s;	\
	\
		for (s = 0; s < len; s++) {	\
			u8 chr = font8x16[text[s] * 16 + line];	\
	\
			if (tpg->hflip) { \
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
			pos += tpg->hflip ? -8 : 8;	\
		}	\
	}	\
} while (0)

		switch (tpg->twopixelsize[p]) {
		case 2:
			PRINTSTR(u8); break;
		case 4:
			PRINTSTR(u16); break;
		case 6:
			PRINTSTR(x24); break;
		case 8:
			PRINTSTR(u32); break;
		}
	}
}

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

/* Map the line number relative to the crop rectangle to a frame line number */
static unsigned tpg_calc_frameline(struct tpg_data *tpg, unsigned src_y,
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
static unsigned tpg_calc_buffer_line(struct tpg_data *tpg, unsigned y,
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
		tpg->real_ycbcr_enc = tpg->ycbcr_enc;
		tpg->real_quantization = tpg->quantization;
		if (tpg->ycbcr_enc == V4L2_YCBCR_ENC_DEFAULT) {
			switch (tpg->colorspace) {
			case V4L2_COLORSPACE_REC709:
				tpg->real_ycbcr_enc = V4L2_YCBCR_ENC_709;
				break;
			case V4L2_COLORSPACE_SRGB:
				tpg->real_ycbcr_enc = V4L2_YCBCR_ENC_SYCC;
				break;
			case V4L2_COLORSPACE_BT2020:
				tpg->real_ycbcr_enc = V4L2_YCBCR_ENC_BT2020;
				break;
			case V4L2_COLORSPACE_SMPTE240M:
				tpg->real_ycbcr_enc = V4L2_YCBCR_ENC_SMPTE240M;
				break;
			case V4L2_COLORSPACE_SMPTE170M:
			case V4L2_COLORSPACE_470_SYSTEM_M:
			case V4L2_COLORSPACE_470_SYSTEM_BG:
			case V4L2_COLORSPACE_ADOBERGB:
			default:
				tpg->real_ycbcr_enc = V4L2_YCBCR_ENC_601;
				break;
			}
		}
		if (tpg->quantization == V4L2_QUANTIZATION_DEFAULT) {
			tpg->real_quantization = V4L2_QUANTIZATION_FULL_RANGE;
			if (tpg->is_yuv) {
				switch (tpg->real_ycbcr_enc) {
				case V4L2_YCBCR_ENC_SYCC:
				case V4L2_YCBCR_ENC_XV601:
				case V4L2_YCBCR_ENC_XV709:
					break;
				default:
					tpg->real_quantization =
						V4L2_QUANTIZATION_LIM_RANGE;
					break;
				}
			}
		}
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

	tpg_recalc(tpg);

	basep[p][0] = vbuf;
	basep[p][1] = vbuf;
	if (tpg->field == V4L2_FIELD_SEQ_TB)
		basep[p][1] += tpg->buf_height * stride / 2;
	else if (tpg->field == V4L2_FIELD_SEQ_BT)
		basep[p][0] += tpg->buf_height * stride / 2;
}

void tpg_fillbuffer(struct tpg_data *tpg, v4l2_std_id std, unsigned p, u8 *vbuf)
{
	bool is_tv = std;
	bool is_60hz = is_tv && (std & V4L2_STD_525_60);
	unsigned mv_hor_old = tpg->mv_hor_count % tpg->src_width;
	unsigned mv_hor_new = (tpg->mv_hor_count + tpg->mv_hor_step) % tpg->src_width;
	unsigned mv_vert_old = tpg->mv_vert_count % tpg->src_height;
	unsigned mv_vert_new = (tpg->mv_vert_count + tpg->mv_vert_step) % tpg->src_height;
	unsigned wss_width;
	unsigned f;
	int hmax = (tpg->compose.height * tpg->perc_fill) / 100;
	int h;
	unsigned twopixsize = tpg->twopixelsize[p];
	unsigned img_width = tpg->compose.width * twopixsize / 2;
	unsigned line_offset;
	unsigned left_pillar_width = 0;
	unsigned right_pillar_start = img_width;
	unsigned stride = tpg->bytesperline[p];
	unsigned factor = V4L2_FIELD_HAS_T_OR_B(tpg->field) ? 2 : 1;
	u8 *orig_vbuf = vbuf;

	/* Coarse scaling with Bresenham */
	unsigned int_part = (tpg->crop.height / factor) / tpg->compose.height;
	unsigned fract_part = (tpg->crop.height / factor) % tpg->compose.height;
	unsigned src_y = 0;
	unsigned error = 0;

	tpg_recalc(tpg);

	mv_hor_old = (mv_hor_old * tpg->scaled_width / tpg->src_width) & ~1;
	mv_hor_new = (mv_hor_new * tpg->scaled_width / tpg->src_width) & ~1;
	wss_width = tpg->crop.left < tpg->src_width / 2 ?
			tpg->src_width / 2 - tpg->crop.left : 0;
	if (wss_width > tpg->crop.width)
		wss_width = tpg->crop.width;
	wss_width = wss_width * tpg->scaled_width / tpg->src_width;

	vbuf += tpg->compose.left * twopixsize / 2;
	line_offset = tpg->crop.left * tpg->scaled_width / tpg->src_width;
	line_offset = (line_offset & ~1) * twopixsize / 2;
	if (tpg->crop.left < tpg->border.left) {
		left_pillar_width = tpg->border.left - tpg->crop.left;
		if (left_pillar_width > tpg->crop.width)
			left_pillar_width = tpg->crop.width;
		left_pillar_width = (left_pillar_width * tpg->scaled_width) / tpg->src_width;
		left_pillar_width = (left_pillar_width & ~1) * twopixsize / 2;
	}
	if (tpg->crop.left + tpg->crop.width > tpg->border.left + tpg->border.width) {
		right_pillar_start = tpg->border.left + tpg->border.width - tpg->crop.left;
		right_pillar_start = (right_pillar_start * tpg->scaled_width) / tpg->src_width;
		right_pillar_start = (right_pillar_start & ~1) * twopixsize / 2;
		if (right_pillar_start > img_width)
			right_pillar_start = img_width;
	}

	f = tpg->field == (is_60hz ? V4L2_FIELD_TOP : V4L2_FIELD_BOTTOM);

	for (h = 0; h < tpg->compose.height; h++) {
		bool even;
		bool fill_blank = false;
		unsigned frame_line;
		unsigned buf_line;
		unsigned pat_line_old;
		unsigned pat_line_new;
		u8 *linestart_older;
		u8 *linestart_newer;
		u8 *linestart_top;
		u8 *linestart_bottom;

		frame_line = tpg_calc_frameline(tpg, src_y, tpg->field);
		even = !(frame_line & 1);
		buf_line = tpg_calc_buffer_line(tpg, h, tpg->field);
		src_y += int_part;
		error += fract_part;
		if (error >= tpg->compose.height) {
			error -= tpg->compose.height;
			src_y++;
		}

		if (h >= hmax) {
			if (hmax == tpg->compose.height)
				continue;
			if (!tpg->perc_fill_blank)
				continue;
			fill_blank = true;
		}

		if (tpg->vflip)
			frame_line = tpg->src_height - frame_line - 1;

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
					  twopixsize * prandom_u32_max(tpg->src_width / 2);
			linestart_newer = tpg->random_line[p] +
					  twopixsize * prandom_u32_max(tpg->src_width / 2);
		} else {
			pat_line_old = tpg_get_pat_line(tpg,
						(frame_line + mv_vert_old) % tpg->src_height);
			pat_line_new = tpg_get_pat_line(tpg,
						(frame_line + mv_vert_new) % tpg->src_height);
			linestart_older = tpg->lines[pat_line_old][p] +
					  mv_hor_old * twopixsize / 2;
			linestart_newer = tpg->lines[pat_line_new][p] +
					  mv_hor_new * twopixsize / 2;
			linestart_older += line_offset;
			linestart_newer += line_offset;
		}
		if (is_60hz) {
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
				memcpy(vbuf + buf_line * stride, linestart_top, img_width);
			else
				memcpy(vbuf + buf_line * stride, linestart_bottom, img_width);
			break;
		case V4L2_FIELD_INTERLACED_BT:
			if (even)
				memcpy(vbuf + buf_line * stride, linestart_bottom, img_width);
			else
				memcpy(vbuf + buf_line * stride, linestart_top, img_width);
			break;
		case V4L2_FIELD_TOP:
			memcpy(vbuf + buf_line * stride, linestart_top, img_width);
			break;
		case V4L2_FIELD_BOTTOM:
			memcpy(vbuf + buf_line * stride, linestart_bottom, img_width);
			break;
		case V4L2_FIELD_NONE:
		default:
			memcpy(vbuf + buf_line * stride, linestart_older, img_width);
			break;
		}

		if (is_tv && !is_60hz && frame_line == 0 && wss_width) {
			/*
			 * Replace the first half of the top line of a 50 Hz frame
			 * with random data to simulate a WSS signal.
			 */
			u8 *wss = tpg->random_line[p] +
				  twopixsize * prandom_u32_max(tpg->src_width / 2);

			memcpy(vbuf + buf_line * stride, wss, wss_width * twopixsize / 2);
		}
	}

	vbuf = orig_vbuf;
	vbuf += tpg->compose.left * twopixsize / 2;
	src_y = 0;
	error = 0;
	for (h = 0; h < tpg->compose.height; h++) {
		unsigned frame_line = tpg_calc_frameline(tpg, src_y, tpg->field);
		unsigned buf_line = tpg_calc_buffer_line(tpg, h, tpg->field);
		const struct v4l2_rect *sq = &tpg->square;
		const struct v4l2_rect *b = &tpg->border;
		const struct v4l2_rect *c = &tpg->crop;

		src_y += int_part;
		error += fract_part;
		if (error >= tpg->compose.height) {
			error -= tpg->compose.height;
			src_y++;
		}

		if (tpg->show_border && frame_line >= b->top &&
		    frame_line < b->top + b->height) {
			unsigned bottom = b->top + b->height - 1;
			unsigned left = left_pillar_width;
			unsigned right = right_pillar_start;

			if (frame_line == b->top || frame_line == b->top + 1 ||
			    frame_line == bottom || frame_line == bottom - 1) {
				memcpy(vbuf + buf_line * stride + left, tpg->contrast_line[p],
						right - left);
			} else {
				if (b->left >= c->left &&
				    b->left < c->left + c->width)
					memcpy(vbuf + buf_line * stride + left,
						tpg->contrast_line[p], twopixsize);
				if (b->left + b->width > c->left &&
				    b->left + b->width <= c->left + c->width)
					memcpy(vbuf + buf_line * stride + right - twopixsize,
						tpg->contrast_line[p], twopixsize);
			}
		}
		if (tpg->qual != TPG_QUAL_NOISE && frame_line >= b->top &&
		    frame_line < b->top + b->height) {
			memcpy(vbuf + buf_line * stride, tpg->black_line[p], left_pillar_width);
			memcpy(vbuf + buf_line * stride + right_pillar_start, tpg->black_line[p],
			       img_width - right_pillar_start);
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
			left = (left * tpg->scaled_width) / tpg->src_width;
			left = (left & ~1) * twopixsize / 2;
			width = (width * tpg->scaled_width) / tpg->src_width;
			width = (width & ~1) * twopixsize / 2;
			memcpy(vbuf + buf_line * stride + left, tpg->contrast_line[p], width);
		}
		if (tpg->insert_sav) {
			unsigned offset = (tpg->compose.width / 6) * twopixsize;
			u8 *p = vbuf + buf_line * stride + offset;
			unsigned vact = 0, hact = 0;

			p[0] = 0xff;
			p[1] = 0;
			p[2] = 0;
			p[3] = 0x80 | (f << 6) | (vact << 5) | (hact << 4) |
				((hact ^ vact) << 3) |
				((hact ^ f) << 2) |
				((f ^ vact) << 1) |
				(hact ^ vact ^ f);
		}
		if (tpg->insert_eav) {
			unsigned offset = (tpg->compose.width / 6) * 2 * twopixsize;
			u8 *p = vbuf + buf_line * stride + offset;
			unsigned vact = 0, hact = 1;

			p[0] = 0xff;
			p[1] = 0;
			p[2] = 0;
			p[3] = 0x80 | (f << 6) | (vact << 5) | (hact << 4) |
				((hact ^ vact) << 3) |
				((hact ^ f) << 2) |
				((f ^ vact) << 1) |
				(hact ^ vact ^ f);
		}
	}
}
