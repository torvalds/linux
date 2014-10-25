/*
 * vivid-color.c - A table that converts colors to various colorspaces
 *
 * The test pattern generator uses the tpg_colors for its test patterns.
 * For testing colorspaces the first 8 colors of that table need to be
 * converted to their equivalent in the target colorspace.
 *
 * The tpg_csc_colors[] table is the result of that conversion and since
 * it is precalculated the colorspace conversion is just a simple table
 * lookup.
 *
 * This source also contains the code used to generate the tpg_csc_colors
 * table. Run the following command to compile it:
 *
 *	gcc vivid-colors.c -DCOMPILE_APP -o gen-colors -lm
 *
 * and run the utility.
 *
 * Note that the converted colors are in the range 0x000-0xff0 (so times 16)
 * in order to preserve precision.
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

#include <linux/videodev2.h>

#include "vivid-tpg-colors.h"

/* sRGB colors with range [0-255] */
const struct color tpg_colors[TPG_COLOR_MAX] = {
	/*
	 * Colors to test colorspace conversion: converting these colors
	 * to other colorspaces will never lead to out-of-gamut colors.
	 */
	{ 191, 191, 191 }, /* TPG_COLOR_CSC_WHITE */
	{ 191, 191,  50 }, /* TPG_COLOR_CSC_YELLOW */
	{  50, 191, 191 }, /* TPG_COLOR_CSC_CYAN */
	{  50, 191,  50 }, /* TPG_COLOR_CSC_GREEN */
	{ 191,  50, 191 }, /* TPG_COLOR_CSC_MAGENTA */
	{ 191,  50,  50 }, /* TPG_COLOR_CSC_RED */
	{  50,  50, 191 }, /* TPG_COLOR_CSC_BLUE */
	{  50,  50,  50 }, /* TPG_COLOR_CSC_BLACK */

	/* 75% colors */
	{ 191, 191,   0 }, /* TPG_COLOR_75_YELLOW */
	{   0, 191, 191 }, /* TPG_COLOR_75_CYAN */
	{   0, 191,   0 }, /* TPG_COLOR_75_GREEN */
	{ 191,   0, 191 }, /* TPG_COLOR_75_MAGENTA */
	{ 191,   0,   0 }, /* TPG_COLOR_75_RED */
	{   0,   0, 191 }, /* TPG_COLOR_75_BLUE */

	/* 100% colors */
	{ 255, 255, 255 }, /* TPG_COLOR_100_WHITE */
	{ 255, 255,   0 }, /* TPG_COLOR_100_YELLOW */
	{   0, 255, 255 }, /* TPG_COLOR_100_CYAN */
	{   0, 255,   0 }, /* TPG_COLOR_100_GREEN */
	{ 255,   0, 255 }, /* TPG_COLOR_100_MAGENTA */
	{ 255,   0,   0 }, /* TPG_COLOR_100_RED */
	{   0,   0, 255 }, /* TPG_COLOR_100_BLUE */
	{   0,   0,   0 }, /* TPG_COLOR_100_BLACK */

	{   0,   0,   0 }, /* TPG_COLOR_RANDOM placeholder */
};

#ifndef COMPILE_APP

/* Generated table */
const struct color16 tpg_csc_colors[V4L2_COLORSPACE_SRGB + 1][TPG_COLOR_CSC_BLACK + 1] = {
	[V4L2_COLORSPACE_SMPTE170M][0] = { 2953, 2939, 2939 },
	[V4L2_COLORSPACE_SMPTE170M][1] = { 2954, 2963, 585 },
	[V4L2_COLORSPACE_SMPTE170M][2] = { 84, 2967, 2937 },
	[V4L2_COLORSPACE_SMPTE170M][3] = { 93, 2990, 575 },
	[V4L2_COLORSPACE_SMPTE170M][4] = { 3030, 259, 2933 },
	[V4L2_COLORSPACE_SMPTE170M][5] = { 3031, 406, 557 },
	[V4L2_COLORSPACE_SMPTE170M][6] = { 544, 428, 2931 },
	[V4L2_COLORSPACE_SMPTE170M][7] = { 551, 547, 547 },
	[V4L2_COLORSPACE_SMPTE240M][0] = { 2926, 2926, 2926 },
	[V4L2_COLORSPACE_SMPTE240M][1] = { 2926, 2926, 857 },
	[V4L2_COLORSPACE_SMPTE240M][2] = { 1594, 2901, 2901 },
	[V4L2_COLORSPACE_SMPTE240M][3] = { 1594, 2901, 774 },
	[V4L2_COLORSPACE_SMPTE240M][4] = { 2484, 618, 2858 },
	[V4L2_COLORSPACE_SMPTE240M][5] = { 2484, 618, 617 },
	[V4L2_COLORSPACE_SMPTE240M][6] = { 507, 507, 2832 },
	[V4L2_COLORSPACE_SMPTE240M][7] = { 507, 507, 507 },
	[V4L2_COLORSPACE_REC709][0] = { 2939, 2939, 2939 },
	[V4L2_COLORSPACE_REC709][1] = { 2939, 2939, 547 },
	[V4L2_COLORSPACE_REC709][2] = { 547, 2939, 2939 },
	[V4L2_COLORSPACE_REC709][3] = { 547, 2939, 547 },
	[V4L2_COLORSPACE_REC709][4] = { 2939, 547, 2939 },
	[V4L2_COLORSPACE_REC709][5] = { 2939, 547, 547 },
	[V4L2_COLORSPACE_REC709][6] = { 547, 547, 2939 },
	[V4L2_COLORSPACE_REC709][7] = { 547, 547, 547 },
	[V4L2_COLORSPACE_470_SYSTEM_M][0] = { 2894, 2988, 2808 },
	[V4L2_COLORSPACE_470_SYSTEM_M][1] = { 2847, 3070, 843 },
	[V4L2_COLORSPACE_470_SYSTEM_M][2] = { 1656, 2962, 2783 },
	[V4L2_COLORSPACE_470_SYSTEM_M][3] = { 1572, 3045, 763 },
	[V4L2_COLORSPACE_470_SYSTEM_M][4] = { 2477, 229, 2743 },
	[V4L2_COLORSPACE_470_SYSTEM_M][5] = { 2422, 672, 614 },
	[V4L2_COLORSPACE_470_SYSTEM_M][6] = { 725, 63, 2718 },
	[V4L2_COLORSPACE_470_SYSTEM_M][7] = { 534, 561, 509 },
	[V4L2_COLORSPACE_470_SYSTEM_BG][0] = { 2939, 2939, 2939 },
	[V4L2_COLORSPACE_470_SYSTEM_BG][1] = { 2939, 2939, 621 },
	[V4L2_COLORSPACE_470_SYSTEM_BG][2] = { 786, 2939, 2939 },
	[V4L2_COLORSPACE_470_SYSTEM_BG][3] = { 786, 2939, 621 },
	[V4L2_COLORSPACE_470_SYSTEM_BG][4] = { 2879, 547, 2923 },
	[V4L2_COLORSPACE_470_SYSTEM_BG][5] = { 2879, 547, 547 },
	[V4L2_COLORSPACE_470_SYSTEM_BG][6] = { 547, 547, 2923 },
	[V4L2_COLORSPACE_470_SYSTEM_BG][7] = { 547, 547, 547 },
	[V4L2_COLORSPACE_SRGB][0] = { 3056, 3056, 3056 },
	[V4L2_COLORSPACE_SRGB][1] = { 3056, 3056, 800 },
	[V4L2_COLORSPACE_SRGB][2] = { 800, 3056, 3056 },
	[V4L2_COLORSPACE_SRGB][3] = { 800, 3056, 800 },
	[V4L2_COLORSPACE_SRGB][4] = { 3056, 800, 3056 },
	[V4L2_COLORSPACE_SRGB][5] = { 3056, 800, 800 },
	[V4L2_COLORSPACE_SRGB][6] = { 800, 800, 3056 },
	[V4L2_COLORSPACE_SRGB][7] = { 800, 800, 800 },
};

#else

/* This code generates the table above */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const double rec709_to_ntsc1953[3][3] = {
	{ 0.6698, 0.2678,  0.0323 },
	{ 0.0185, 1.0742, -0.0603 },
	{ 0.0162, 0.0432,  0.8551 }
};

static const double rec709_to_ebu[3][3] = {
	{ 0.9578, 0.0422, 0      },
	{ 0     , 1     , 0      },
	{ 0     , 0.0118, 0.9882 }
};

static const double rec709_to_170m[3][3] = {
	{  1.0654, -0.0554, -0.0010 },
	{ -0.0196,  1.0364, -0.0167 },
	{  0.0016,  0.0044,  0.9940 }
};

static const double rec709_to_240m[3][3] = {
	{ 0.7151, 0.2849, 0      },
	{ 0.0179, 0.9821, 0      },
	{ 0.0177, 0.0472, 0.9350 }
};


static void mult_matrix(double *r, double *g, double *b, const double m[3][3])
{
	double ir, ig, ib;

	ir = m[0][0] * (*r) + m[0][1] * (*g) + m[0][2] * (*b);
	ig = m[1][0] * (*r) + m[1][1] * (*g) + m[1][2] * (*b);
	ib = m[2][0] * (*r) + m[2][1] * (*g) + m[2][2] * (*b);
	*r = ir;
	*g = ig;
	*b = ib;
}

static double transfer_srgb_to_rgb(double v)
{
	return (v <= 0.03928) ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

static double transfer_rgb_to_smpte240m(double v)
{
	return (v <= 0.0228) ? v * 4.0 : 1.1115 * pow(v, 0.45) - 0.1115;
}

static double transfer_rgb_to_rec709(double v)
{
	return (v < 0.018) ? v * 4.5 : 1.099 * pow(v, 0.45) - 0.099;
}

static double transfer_srgb_to_rec709(double v)
{
	return transfer_rgb_to_rec709(transfer_srgb_to_rgb(v));
}

static void csc(enum v4l2_colorspace colorspace, double *r, double *g, double *b)
{
	/* Convert the primaries of Rec. 709 Linear RGB */
	switch (colorspace) {
	case V4L2_COLORSPACE_SMPTE240M:
		*r = transfer_srgb_to_rgb(*r);
		*g = transfer_srgb_to_rgb(*g);
		*b = transfer_srgb_to_rgb(*b);
		mult_matrix(r, g, b, rec709_to_240m);
		break;
	case V4L2_COLORSPACE_SMPTE170M:
		*r = transfer_srgb_to_rgb(*r);
		*g = transfer_srgb_to_rgb(*g);
		*b = transfer_srgb_to_rgb(*b);
		mult_matrix(r, g, b, rec709_to_170m);
		break;
	case V4L2_COLORSPACE_470_SYSTEM_BG:
		*r = transfer_srgb_to_rgb(*r);
		*g = transfer_srgb_to_rgb(*g);
		*b = transfer_srgb_to_rgb(*b);
		mult_matrix(r, g, b, rec709_to_ebu);
		break;
	case V4L2_COLORSPACE_470_SYSTEM_M:
		*r = transfer_srgb_to_rgb(*r);
		*g = transfer_srgb_to_rgb(*g);
		*b = transfer_srgb_to_rgb(*b);
		mult_matrix(r, g, b, rec709_to_ntsc1953);
		break;
	case V4L2_COLORSPACE_SRGB:
	case V4L2_COLORSPACE_REC709:
	default:
		break;
	}

	*r = ((*r) < 0) ? 0 : (((*r) > 1) ? 1 : (*r));
	*g = ((*g) < 0) ? 0 : (((*g) > 1) ? 1 : (*g));
	*b = ((*b) < 0) ? 0 : (((*b) > 1) ? 1 : (*b));

	/* Encode to gamma corrected colorspace */
	switch (colorspace) {
	case V4L2_COLORSPACE_SMPTE240M:
		*r = transfer_rgb_to_smpte240m(*r);
		*g = transfer_rgb_to_smpte240m(*g);
		*b = transfer_rgb_to_smpte240m(*b);
		break;
	case V4L2_COLORSPACE_SMPTE170M:
	case V4L2_COLORSPACE_470_SYSTEM_M:
	case V4L2_COLORSPACE_470_SYSTEM_BG:
		*r = transfer_rgb_to_rec709(*r);
		*g = transfer_rgb_to_rec709(*g);
		*b = transfer_rgb_to_rec709(*b);
		break;
	case V4L2_COLORSPACE_SRGB:
		break;
	case V4L2_COLORSPACE_REC709:
	default:
		*r = transfer_srgb_to_rec709(*r);
		*g = transfer_srgb_to_rec709(*g);
		*b = transfer_srgb_to_rec709(*b);
		break;
	}
}

int main(int argc, char **argv)
{
	static const unsigned colorspaces[] = {
		0,
		V4L2_COLORSPACE_SMPTE170M,
		V4L2_COLORSPACE_SMPTE240M,
		V4L2_COLORSPACE_REC709,
		0,
		V4L2_COLORSPACE_470_SYSTEM_M,
		V4L2_COLORSPACE_470_SYSTEM_BG,
		0,
		V4L2_COLORSPACE_SRGB,
	};
	static const char * const colorspace_names[] = {
		"",
		"V4L2_COLORSPACE_SMPTE170M",
		"V4L2_COLORSPACE_SMPTE240M",
		"V4L2_COLORSPACE_REC709",
		"",
		"V4L2_COLORSPACE_470_SYSTEM_M",
		"V4L2_COLORSPACE_470_SYSTEM_BG",
		"",
		"V4L2_COLORSPACE_SRGB",
	};
	int i;
	int c;

	printf("/* Generated table */\n");
	printf("const struct color16 tpg_csc_colors[V4L2_COLORSPACE_SRGB + 1][TPG_COLOR_CSC_BLACK + 1] = {\n");
	for (c = 0; c <= V4L2_COLORSPACE_SRGB; c++) {
		for (i = 0; i <= TPG_COLOR_CSC_BLACK; i++) {
			double r, g, b;

			if (colorspaces[c] == 0)
				continue;

			r = tpg_colors[i].r / 255.0;
			g = tpg_colors[i].g / 255.0;
			b = tpg_colors[i].b / 255.0;

			csc(c, &r, &g, &b);

			printf("\t[%s][%d] = { %d, %d, %d },\n", colorspace_names[c], i,
				(int)(r * 4080), (int)(g * 4080), (int)(b * 4080));
		}
	}
	printf("};\n\n");
	return 0;
}

#endif
