// SPDX-License-Identifier: GPL-2.0-only
/*
 * v4l2-dv-timings - dv-timings helper functions
 *
 * Copyright 2013 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/rational.h>
#include <linux/videodev2.h>
#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-dv-timings.h>
#include <linux/math64.h>
#include <linux/hdmi.h>
#include <media/cec.h>

MODULE_AUTHOR("Hans Verkuil");
MODULE_DESCRIPTION("V4L2 DV Timings Helper Functions");
MODULE_LICENSE("GPL");

const struct v4l2_dv_timings v4l2_dv_timings_presets[] = {
	V4L2_DV_BT_CEA_640X480P59_94,
	V4L2_DV_BT_CEA_720X480I59_94,
	V4L2_DV_BT_CEA_720X480P59_94,
	V4L2_DV_BT_CEA_720X576I50,
	V4L2_DV_BT_CEA_720X576P50,
	V4L2_DV_BT_CEA_1280X720P24,
	V4L2_DV_BT_CEA_1280X720P25,
	V4L2_DV_BT_CEA_1280X720P30,
	V4L2_DV_BT_CEA_1280X720P50,
	V4L2_DV_BT_CEA_1280X720P60,
	V4L2_DV_BT_CEA_1920X1080P24,
	V4L2_DV_BT_CEA_1920X1080P25,
	V4L2_DV_BT_CEA_1920X1080P30,
	V4L2_DV_BT_CEA_1920X1080I50,
	V4L2_DV_BT_CEA_1920X1080P50,
	V4L2_DV_BT_CEA_1920X1080I60,
	V4L2_DV_BT_CEA_1920X1080P60,
	V4L2_DV_BT_DMT_640X350P85,
	V4L2_DV_BT_DMT_640X400P85,
	V4L2_DV_BT_DMT_720X400P85,
	V4L2_DV_BT_DMT_640X480P72,
	V4L2_DV_BT_DMT_640X480P75,
	V4L2_DV_BT_DMT_640X480P85,
	V4L2_DV_BT_DMT_800X600P56,
	V4L2_DV_BT_DMT_800X600P60,
	V4L2_DV_BT_DMT_800X600P72,
	V4L2_DV_BT_DMT_800X600P75,
	V4L2_DV_BT_DMT_800X600P85,
	V4L2_DV_BT_DMT_800X600P120_RB,
	V4L2_DV_BT_DMT_848X480P60,
	V4L2_DV_BT_DMT_1024X768I43,
	V4L2_DV_BT_DMT_1024X768P60,
	V4L2_DV_BT_DMT_1024X768P70,
	V4L2_DV_BT_DMT_1024X768P75,
	V4L2_DV_BT_DMT_1024X768P85,
	V4L2_DV_BT_DMT_1024X768P120_RB,
	V4L2_DV_BT_DMT_1152X864P75,
	V4L2_DV_BT_DMT_1280X768P60_RB,
	V4L2_DV_BT_DMT_1280X768P60,
	V4L2_DV_BT_DMT_1280X768P75,
	V4L2_DV_BT_DMT_1280X768P85,
	V4L2_DV_BT_DMT_1280X768P120_RB,
	V4L2_DV_BT_DMT_1280X800P60_RB,
	V4L2_DV_BT_DMT_1280X800P60,
	V4L2_DV_BT_DMT_1280X800P75,
	V4L2_DV_BT_DMT_1280X800P85,
	V4L2_DV_BT_DMT_1280X800P120_RB,
	V4L2_DV_BT_DMT_1280X960P60,
	V4L2_DV_BT_DMT_1280X960P85,
	V4L2_DV_BT_DMT_1280X960P120_RB,
	V4L2_DV_BT_DMT_1280X1024P60,
	V4L2_DV_BT_DMT_1280X1024P75,
	V4L2_DV_BT_DMT_1280X1024P85,
	V4L2_DV_BT_DMT_1280X1024P120_RB,
	V4L2_DV_BT_DMT_1360X768P60,
	V4L2_DV_BT_DMT_1360X768P120_RB,
	V4L2_DV_BT_DMT_1366X768P60,
	V4L2_DV_BT_DMT_1366X768P60_RB,
	V4L2_DV_BT_DMT_1400X1050P60_RB,
	V4L2_DV_BT_DMT_1400X1050P60,
	V4L2_DV_BT_DMT_1400X1050P75,
	V4L2_DV_BT_DMT_1400X1050P85,
	V4L2_DV_BT_DMT_1400X1050P120_RB,
	V4L2_DV_BT_DMT_1440X900P60_RB,
	V4L2_DV_BT_DMT_1440X900P60,
	V4L2_DV_BT_DMT_1440X900P75,
	V4L2_DV_BT_DMT_1440X900P85,
	V4L2_DV_BT_DMT_1440X900P120_RB,
	V4L2_DV_BT_DMT_1600X900P60_RB,
	V4L2_DV_BT_DMT_1600X1200P60,
	V4L2_DV_BT_DMT_1600X1200P65,
	V4L2_DV_BT_DMT_1600X1200P70,
	V4L2_DV_BT_DMT_1600X1200P75,
	V4L2_DV_BT_DMT_1600X1200P85,
	V4L2_DV_BT_DMT_1600X1200P120_RB,
	V4L2_DV_BT_DMT_1680X1050P60_RB,
	V4L2_DV_BT_DMT_1680X1050P60,
	V4L2_DV_BT_DMT_1680X1050P75,
	V4L2_DV_BT_DMT_1680X1050P85,
	V4L2_DV_BT_DMT_1680X1050P120_RB,
	V4L2_DV_BT_DMT_1792X1344P60,
	V4L2_DV_BT_DMT_1792X1344P75,
	V4L2_DV_BT_DMT_1792X1344P120_RB,
	V4L2_DV_BT_DMT_1856X1392P60,
	V4L2_DV_BT_DMT_1856X1392P75,
	V4L2_DV_BT_DMT_1856X1392P120_RB,
	V4L2_DV_BT_DMT_1920X1200P60_RB,
	V4L2_DV_BT_DMT_1920X1200P60,
	V4L2_DV_BT_DMT_1920X1200P75,
	V4L2_DV_BT_DMT_1920X1200P85,
	V4L2_DV_BT_DMT_1920X1200P120_RB,
	V4L2_DV_BT_DMT_1920X1440P60,
	V4L2_DV_BT_DMT_1920X1440P75,
	V4L2_DV_BT_DMT_1920X1440P120_RB,
	V4L2_DV_BT_DMT_2048X1152P60_RB,
	V4L2_DV_BT_DMT_2560X1600P60_RB,
	V4L2_DV_BT_DMT_2560X1600P60,
	V4L2_DV_BT_DMT_2560X1600P75,
	V4L2_DV_BT_DMT_2560X1600P85,
	V4L2_DV_BT_DMT_2560X1600P120_RB,
	V4L2_DV_BT_CEA_3840X2160P24,
	V4L2_DV_BT_CEA_3840X2160P25,
	V4L2_DV_BT_CEA_3840X2160P30,
	V4L2_DV_BT_CEA_3840X2160P50,
	V4L2_DV_BT_CEA_3840X2160P60,
	V4L2_DV_BT_CEA_4096X2160P24,
	V4L2_DV_BT_CEA_4096X2160P25,
	V4L2_DV_BT_CEA_4096X2160P30,
	V4L2_DV_BT_CEA_4096X2160P50,
	V4L2_DV_BT_DMT_4096X2160P59_94_RB,
	V4L2_DV_BT_CEA_4096X2160P60,
	{ }
};
EXPORT_SYMBOL_GPL(v4l2_dv_timings_presets);

bool v4l2_valid_dv_timings(const struct v4l2_dv_timings *t,
			   const struct v4l2_dv_timings_cap *dvcap,
			   v4l2_check_dv_timings_fnc fnc,
			   void *fnc_handle)
{
	const struct v4l2_bt_timings *bt = &t->bt;
	const struct v4l2_bt_timings_cap *cap = &dvcap->bt;
	u32 caps = cap->capabilities;
	const u32 max_vert = 10240;
	u32 max_hor = 3 * bt->width;

	if (t->type != V4L2_DV_BT_656_1120)
		return false;
	if (t->type != dvcap->type ||
	    bt->height < cap->min_height ||
	    bt->height > cap->max_height ||
	    bt->width < cap->min_width ||
	    bt->width > cap->max_width ||
	    bt->pixelclock < cap->min_pixelclock ||
	    bt->pixelclock > cap->max_pixelclock ||
	    (!(caps & V4L2_DV_BT_CAP_CUSTOM) &&
	     cap->standards && bt->standards &&
	     !(bt->standards & cap->standards)) ||
	    (bt->interlaced && !(caps & V4L2_DV_BT_CAP_INTERLACED)) ||
	    (!bt->interlaced && !(caps & V4L2_DV_BT_CAP_PROGRESSIVE)))
		return false;

	/* sanity checks for the blanking timings */
	if (!bt->interlaced &&
	    (bt->il_vbackporch || bt->il_vsync || bt->il_vfrontporch))
		return false;
	/*
	 * Some video receivers cannot properly separate the frontporch,
	 * backporch and sync values, and instead they only have the total
	 * blanking. That can be assigned to any of these three fields.
	 * So just check that none of these are way out of range.
	 */
	if (bt->hfrontporch > max_hor ||
	    bt->hsync > max_hor || bt->hbackporch > max_hor)
		return false;
	if (bt->vfrontporch > max_vert ||
	    bt->vsync > max_vert || bt->vbackporch > max_vert)
		return false;
	if (bt->interlaced && (bt->il_vfrontporch > max_vert ||
	    bt->il_vsync > max_vert || bt->il_vbackporch > max_vert))
		return false;
	return fnc == NULL || fnc(t, fnc_handle);
}
EXPORT_SYMBOL_GPL(v4l2_valid_dv_timings);

int v4l2_enum_dv_timings_cap(struct v4l2_enum_dv_timings *t,
			     const struct v4l2_dv_timings_cap *cap,
			     v4l2_check_dv_timings_fnc fnc,
			     void *fnc_handle)
{
	u32 i, idx;

	memset(t->reserved, 0, sizeof(t->reserved));
	for (i = idx = 0; v4l2_dv_timings_presets[i].bt.width; i++) {
		if (v4l2_valid_dv_timings(v4l2_dv_timings_presets + i, cap,
					  fnc, fnc_handle) &&
		    idx++ == t->index) {
			t->timings = v4l2_dv_timings_presets[i];
			return 0;
		}
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(v4l2_enum_dv_timings_cap);

bool v4l2_find_dv_timings_cap(struct v4l2_dv_timings *t,
			      const struct v4l2_dv_timings_cap *cap,
			      unsigned pclock_delta,
			      v4l2_check_dv_timings_fnc fnc,
			      void *fnc_handle)
{
	int i;

	if (!v4l2_valid_dv_timings(t, cap, fnc, fnc_handle))
		return false;

	for (i = 0; v4l2_dv_timings_presets[i].bt.width; i++) {
		if (v4l2_valid_dv_timings(v4l2_dv_timings_presets + i, cap,
					  fnc, fnc_handle) &&
		    v4l2_match_dv_timings(t, v4l2_dv_timings_presets + i,
					  pclock_delta, false)) {
			u32 flags = t->bt.flags & V4L2_DV_FL_REDUCED_FPS;

			*t = v4l2_dv_timings_presets[i];
			if (can_reduce_fps(&t->bt))
				t->bt.flags |= flags;

			return true;
		}
	}
	return false;
}
EXPORT_SYMBOL_GPL(v4l2_find_dv_timings_cap);

bool v4l2_find_dv_timings_cea861_vic(struct v4l2_dv_timings *t, u8 vic)
{
	unsigned int i;

	for (i = 0; v4l2_dv_timings_presets[i].bt.width; i++) {
		const struct v4l2_bt_timings *bt =
			&v4l2_dv_timings_presets[i].bt;

		if ((bt->flags & V4L2_DV_FL_HAS_CEA861_VIC) &&
		    bt->cea861_vic == vic) {
			*t = v4l2_dv_timings_presets[i];
			return true;
		}
	}
	return false;
}
EXPORT_SYMBOL_GPL(v4l2_find_dv_timings_cea861_vic);

/**
 * v4l2_match_dv_timings - check if two timings match
 * @t1: compare this v4l2_dv_timings struct...
 * @t2: with this struct.
 * @pclock_delta: the allowed pixelclock deviation.
 * @match_reduced_fps: if true, then fail if V4L2_DV_FL_REDUCED_FPS does not
 *	match.
 *
 * Compare t1 with t2 with a given margin of error for the pixelclock.
 */
bool v4l2_match_dv_timings(const struct v4l2_dv_timings *t1,
			   const struct v4l2_dv_timings *t2,
			   unsigned pclock_delta, bool match_reduced_fps)
{
	if (t1->type != t2->type || t1->type != V4L2_DV_BT_656_1120)
		return false;
	if (t1->bt.width == t2->bt.width &&
	    t1->bt.height == t2->bt.height &&
	    t1->bt.interlaced == t2->bt.interlaced &&
	    t1->bt.polarities == t2->bt.polarities &&
	    t1->bt.pixelclock >= t2->bt.pixelclock - pclock_delta &&
	    t1->bt.pixelclock <= t2->bt.pixelclock + pclock_delta &&
	    t1->bt.hfrontporch == t2->bt.hfrontporch &&
	    t1->bt.hsync == t2->bt.hsync &&
	    t1->bt.hbackporch == t2->bt.hbackporch &&
	    t1->bt.vfrontporch == t2->bt.vfrontporch &&
	    t1->bt.vsync == t2->bt.vsync &&
	    t1->bt.vbackporch == t2->bt.vbackporch &&
	    (!match_reduced_fps ||
	     (t1->bt.flags & V4L2_DV_FL_REDUCED_FPS) ==
		(t2->bt.flags & V4L2_DV_FL_REDUCED_FPS)) &&
	    (!t1->bt.interlaced ||
		(t1->bt.il_vfrontporch == t2->bt.il_vfrontporch &&
		 t1->bt.il_vsync == t2->bt.il_vsync &&
		 t1->bt.il_vbackporch == t2->bt.il_vbackporch)))
		return true;
	return false;
}
EXPORT_SYMBOL_GPL(v4l2_match_dv_timings);

void v4l2_print_dv_timings(const char *dev_prefix, const char *prefix,
			   const struct v4l2_dv_timings *t, bool detailed)
{
	const struct v4l2_bt_timings *bt = &t->bt;
	u32 htot, vtot;
	u32 fps;

	if (t->type != V4L2_DV_BT_656_1120)
		return;

	htot = V4L2_DV_BT_FRAME_WIDTH(bt);
	vtot = V4L2_DV_BT_FRAME_HEIGHT(bt);
	if (bt->interlaced)
		vtot /= 2;

	fps = (htot * vtot) > 0 ? div_u64((100 * (u64)bt->pixelclock),
				  (htot * vtot)) : 0;

	if (prefix == NULL)
		prefix = "";

	pr_info("%s: %s%ux%u%s%u.%02u (%ux%u)\n", dev_prefix, prefix,
		bt->width, bt->height, bt->interlaced ? "i" : "p",
		fps / 100, fps % 100, htot, vtot);

	if (!detailed)
		return;

	pr_info("%s: horizontal: fp = %u, %ssync = %u, bp = %u\n",
			dev_prefix, bt->hfrontporch,
			(bt->polarities & V4L2_DV_HSYNC_POS_POL) ? "+" : "-",
			bt->hsync, bt->hbackporch);
	pr_info("%s: vertical: fp = %u, %ssync = %u, bp = %u\n",
			dev_prefix, bt->vfrontporch,
			(bt->polarities & V4L2_DV_VSYNC_POS_POL) ? "+" : "-",
			bt->vsync, bt->vbackporch);
	if (bt->interlaced)
		pr_info("%s: vertical bottom field: fp = %u, %ssync = %u, bp = %u\n",
			dev_prefix, bt->il_vfrontporch,
			(bt->polarities & V4L2_DV_VSYNC_POS_POL) ? "+" : "-",
			bt->il_vsync, bt->il_vbackporch);
	pr_info("%s: pixelclock: %llu\n", dev_prefix, bt->pixelclock);
	pr_info("%s: flags (0x%x):%s%s%s%s%s%s%s%s%s%s\n",
			dev_prefix, bt->flags,
			(bt->flags & V4L2_DV_FL_REDUCED_BLANKING) ?
			" REDUCED_BLANKING" : "",
			((bt->flags & V4L2_DV_FL_REDUCED_BLANKING) &&
			 bt->vsync == 8) ? " (V2)" : "",
			(bt->flags & V4L2_DV_FL_CAN_REDUCE_FPS) ?
			" CAN_REDUCE_FPS" : "",
			(bt->flags & V4L2_DV_FL_REDUCED_FPS) ?
			" REDUCED_FPS" : "",
			(bt->flags & V4L2_DV_FL_HALF_LINE) ?
			" HALF_LINE" : "",
			(bt->flags & V4L2_DV_FL_IS_CE_VIDEO) ?
			" CE_VIDEO" : "",
			(bt->flags & V4L2_DV_FL_FIRST_FIELD_EXTRA_LINE) ?
			" FIRST_FIELD_EXTRA_LINE" : "",
			(bt->flags & V4L2_DV_FL_HAS_PICTURE_ASPECT) ?
			" HAS_PICTURE_ASPECT" : "",
			(bt->flags & V4L2_DV_FL_HAS_CEA861_VIC) ?
			" HAS_CEA861_VIC" : "",
			(bt->flags & V4L2_DV_FL_HAS_HDMI_VIC) ?
			" HAS_HDMI_VIC" : "");
	pr_info("%s: standards (0x%x):%s%s%s%s%s\n", dev_prefix, bt->standards,
			(bt->standards & V4L2_DV_BT_STD_CEA861) ?  " CEA" : "",
			(bt->standards & V4L2_DV_BT_STD_DMT) ?  " DMT" : "",
			(bt->standards & V4L2_DV_BT_STD_CVT) ?  " CVT" : "",
			(bt->standards & V4L2_DV_BT_STD_GTF) ?  " GTF" : "",
			(bt->standards & V4L2_DV_BT_STD_SDI) ?  " SDI" : "");
	if (bt->flags & V4L2_DV_FL_HAS_PICTURE_ASPECT)
		pr_info("%s: picture aspect (hor:vert): %u:%u\n", dev_prefix,
			bt->picture_aspect.numerator,
			bt->picture_aspect.denominator);
	if (bt->flags & V4L2_DV_FL_HAS_CEA861_VIC)
		pr_info("%s: CEA-861 VIC: %u\n", dev_prefix, bt->cea861_vic);
	if (bt->flags & V4L2_DV_FL_HAS_HDMI_VIC)
		pr_info("%s: HDMI VIC: %u\n", dev_prefix, bt->hdmi_vic);
}
EXPORT_SYMBOL_GPL(v4l2_print_dv_timings);

struct v4l2_fract v4l2_dv_timings_aspect_ratio(const struct v4l2_dv_timings *t)
{
	struct v4l2_fract ratio = { 1, 1 };
	unsigned long n, d;

	if (t->type != V4L2_DV_BT_656_1120)
		return ratio;
	if (!(t->bt.flags & V4L2_DV_FL_HAS_PICTURE_ASPECT))
		return ratio;

	ratio.numerator = t->bt.width * t->bt.picture_aspect.denominator;
	ratio.denominator = t->bt.height * t->bt.picture_aspect.numerator;

	rational_best_approximation(ratio.numerator, ratio.denominator,
				    ratio.numerator, ratio.denominator, &n, &d);
	ratio.numerator = n;
	ratio.denominator = d;
	return ratio;
}
EXPORT_SYMBOL_GPL(v4l2_dv_timings_aspect_ratio);

/** v4l2_calc_timeperframe - helper function to calculate timeperframe based
 *	v4l2_dv_timings fields.
 * @t - Timings for the video mode.
 *
 * Calculates the expected timeperframe using the pixel clock value and
 * horizontal/vertical measures. This means that v4l2_dv_timings structure
 * must be correctly and fully filled.
 */
struct v4l2_fract v4l2_calc_timeperframe(const struct v4l2_dv_timings *t)
{
	const struct v4l2_bt_timings *bt = &t->bt;
	struct v4l2_fract fps_fract = { 1, 1 };
	unsigned long n, d;
	u32 htot, vtot, fps;
	u64 pclk;

	if (t->type != V4L2_DV_BT_656_1120)
		return fps_fract;

	htot = V4L2_DV_BT_FRAME_WIDTH(bt);
	vtot = V4L2_DV_BT_FRAME_HEIGHT(bt);
	pclk = bt->pixelclock;

	if ((bt->flags & V4L2_DV_FL_CAN_DETECT_REDUCED_FPS) &&
	    (bt->flags & V4L2_DV_FL_REDUCED_FPS))
		pclk = div_u64(pclk * 1000ULL, 1001);

	fps = (htot * vtot) > 0 ? div_u64((100 * pclk), (htot * vtot)) : 0;
	if (!fps)
		return fps_fract;

	rational_best_approximation(fps, 100, fps, 100, &n, &d);

	fps_fract.numerator = d;
	fps_fract.denominator = n;
	return fps_fract;
}
EXPORT_SYMBOL_GPL(v4l2_calc_timeperframe);

/*
 * CVT defines
 * Based on Coordinated Video Timings Standard
 * version 1.1 September 10, 2003
 */

#define CVT_PXL_CLK_GRAN	250000	/* pixel clock granularity */
#define CVT_PXL_CLK_GRAN_RB_V2 1000	/* granularity for reduced blanking v2*/

/* Normal blanking */
#define CVT_MIN_V_BPORCH	7	/* lines */
#define CVT_MIN_V_PORCH_RND	3	/* lines */
#define CVT_MIN_VSYNC_BP	550	/* min time of vsync + back porch (us) */
#define CVT_HSYNC_PERCENT       8       /* nominal hsync as percentage of line */

/* Normal blanking for CVT uses GTF to calculate horizontal blanking */
#define CVT_CELL_GRAN		8	/* character cell granularity */
#define CVT_M			600	/* blanking formula gradient */
#define CVT_C			40	/* blanking formula offset */
#define CVT_K			128	/* blanking formula scaling factor */
#define CVT_J			20	/* blanking formula scaling factor */
#define CVT_C_PRIME (((CVT_C - CVT_J) * CVT_K / 256) + CVT_J)
#define CVT_M_PRIME (CVT_K * CVT_M / 256)

/* Reduced Blanking */
#define CVT_RB_MIN_V_BPORCH    7       /* lines  */
#define CVT_RB_V_FPORCH        3       /* lines  */
#define CVT_RB_MIN_V_BLANK   460       /* us     */
#define CVT_RB_H_SYNC         32       /* pixels */
#define CVT_RB_H_BLANK       160       /* pixels */
/* Reduce blanking Version 2 */
#define CVT_RB_V2_H_BLANK     80       /* pixels */
#define CVT_RB_MIN_V_FPORCH    3       /* lines  */
#define CVT_RB_V2_MIN_V_FPORCH 1       /* lines  */
#define CVT_RB_V_BPORCH        6       /* lines  */

/** v4l2_detect_cvt - detect if the given timings follow the CVT standard
 * @frame_height - the total height of the frame (including blanking) in lines.
 * @hfreq - the horizontal frequency in Hz.
 * @vsync - the height of the vertical sync in lines.
 * @active_width - active width of image (does not include blanking). This
 * information is needed only in case of version 2 of reduced blanking.
 * In other cases, this parameter does not have any effect on timings.
 * @polarities - the horizontal and vertical polarities (same as struct
 *		v4l2_bt_timings polarities).
 * @interlaced - if this flag is true, it indicates interlaced format
 * @cap - the v4l2_dv_timings_cap capabilities.
 * @timings - the resulting timings.
 *
 * This function will attempt to detect if the given values correspond to a
 * valid CVT format. If so, then it will return true, and fmt will be filled
 * in with the found CVT timings.
 */
bool v4l2_detect_cvt(unsigned int frame_height,
		     unsigned int hfreq,
		     unsigned int vsync,
		     unsigned int active_width,
		     u32 polarities,
		     bool interlaced,
		     const struct v4l2_dv_timings_cap *cap,
		     struct v4l2_dv_timings *timings)
{
	struct v4l2_dv_timings t = {};
	int v_fp, v_bp, h_fp, h_bp, hsync;
	int frame_width, image_height, image_width;
	bool reduced_blanking;
	bool rb_v2 = false;
	unsigned int pix_clk;

	if (vsync < 4 || vsync > 8)
		return false;

	if (polarities == V4L2_DV_VSYNC_POS_POL)
		reduced_blanking = false;
	else if (polarities == V4L2_DV_HSYNC_POS_POL)
		reduced_blanking = true;
	else
		return false;

	if (reduced_blanking && vsync == 8)
		rb_v2 = true;

	if (rb_v2 && active_width == 0)
		return false;

	if (!rb_v2 && vsync > 7)
		return false;

	if (hfreq == 0)
		return false;

	/* Vertical */
	if (reduced_blanking) {
		if (rb_v2) {
			v_bp = CVT_RB_V_BPORCH;
			v_fp = (CVT_RB_MIN_V_BLANK * hfreq) / 1000000 + 1;
			v_fp -= vsync + v_bp;

			if (v_fp < CVT_RB_V2_MIN_V_FPORCH)
				v_fp = CVT_RB_V2_MIN_V_FPORCH;
		} else {
			v_fp = CVT_RB_V_FPORCH;
			v_bp = (CVT_RB_MIN_V_BLANK * hfreq) / 1000000 + 1;
			v_bp -= vsync + v_fp;

			if (v_bp < CVT_RB_MIN_V_BPORCH)
				v_bp = CVT_RB_MIN_V_BPORCH;
		}
	} else {
		v_fp = CVT_MIN_V_PORCH_RND;
		v_bp = (CVT_MIN_VSYNC_BP * hfreq) / 1000000 + 1 - vsync;

		if (v_bp < CVT_MIN_V_BPORCH)
			v_bp = CVT_MIN_V_BPORCH;
	}

	if (interlaced)
		image_height = (frame_height - 2 * v_fp - 2 * vsync - 2 * v_bp) & ~0x1;
	else
		image_height = (frame_height - v_fp - vsync - v_bp + 1) & ~0x1;

	if (image_height < 0)
		return false;

	/* Aspect ratio based on vsync */
	switch (vsync) {
	case 4:
		image_width = (image_height * 4) / 3;
		break;
	case 5:
		image_width = (image_height * 16) / 9;
		break;
	case 6:
		image_width = (image_height * 16) / 10;
		break;
	case 7:
		/* special case */
		if (image_height == 1024)
			image_width = (image_height * 5) / 4;
		else if (image_height == 768)
			image_width = (image_height * 15) / 9;
		else
			return false;
		break;
	case 8:
		image_width = active_width;
		break;
	default:
		return false;
	}

	if (!rb_v2)
		image_width = image_width & ~7;

	/* Horizontal */
	if (reduced_blanking) {
		int h_blank;
		int clk_gran;

		h_blank = rb_v2 ? CVT_RB_V2_H_BLANK : CVT_RB_H_BLANK;
		clk_gran = rb_v2 ? CVT_PXL_CLK_GRAN_RB_V2 : CVT_PXL_CLK_GRAN;

		pix_clk = (image_width + h_blank) * hfreq;
		pix_clk = (pix_clk / clk_gran) * clk_gran;

		h_bp  = h_blank / 2;
		hsync = CVT_RB_H_SYNC;
		h_fp  = h_blank - h_bp - hsync;

		frame_width = image_width + h_blank;
	} else {
		unsigned ideal_duty_cycle_per_myriad =
			100 * CVT_C_PRIME - (CVT_M_PRIME * 100000) / hfreq;
		int h_blank;

		if (ideal_duty_cycle_per_myriad < 2000)
			ideal_duty_cycle_per_myriad = 2000;

		h_blank = image_width * ideal_duty_cycle_per_myriad /
					(10000 - ideal_duty_cycle_per_myriad);
		h_blank = (h_blank / (2 * CVT_CELL_GRAN)) * 2 * CVT_CELL_GRAN;

		pix_clk = (image_width + h_blank) * hfreq;
		pix_clk = (pix_clk / CVT_PXL_CLK_GRAN) * CVT_PXL_CLK_GRAN;

		h_bp = h_blank / 2;
		frame_width = image_width + h_blank;

		hsync = frame_width * CVT_HSYNC_PERCENT / 100;
		hsync = (hsync / CVT_CELL_GRAN) * CVT_CELL_GRAN;
		h_fp = h_blank - hsync - h_bp;
	}

	t.type = V4L2_DV_BT_656_1120;
	t.bt.polarities = polarities;
	t.bt.width = image_width;
	t.bt.height = image_height;
	t.bt.hfrontporch = h_fp;
	t.bt.vfrontporch = v_fp;
	t.bt.hsync = hsync;
	t.bt.vsync = vsync;
	t.bt.hbackporch = frame_width - image_width - h_fp - hsync;

	if (!interlaced) {
		t.bt.vbackporch = frame_height - image_height - v_fp - vsync;
		t.bt.interlaced = V4L2_DV_PROGRESSIVE;
	} else {
		t.bt.vbackporch = (frame_height - image_height - 2 * v_fp -
				      2 * vsync) / 2;
		t.bt.il_vbackporch = frame_height - image_height - 2 * v_fp -
					2 * vsync - t.bt.vbackporch;
		t.bt.il_vfrontporch = v_fp;
		t.bt.il_vsync = vsync;
		t.bt.flags |= V4L2_DV_FL_HALF_LINE;
		t.bt.interlaced = V4L2_DV_INTERLACED;
	}

	t.bt.pixelclock = pix_clk;
	t.bt.standards = V4L2_DV_BT_STD_CVT;

	if (reduced_blanking)
		t.bt.flags |= V4L2_DV_FL_REDUCED_BLANKING;

	if (!v4l2_valid_dv_timings(&t, cap, NULL, NULL))
		return false;
	*timings = t;
	return true;
}
EXPORT_SYMBOL_GPL(v4l2_detect_cvt);

/*
 * GTF defines
 * Based on Generalized Timing Formula Standard
 * Version 1.1 September 2, 1999
 */

#define GTF_PXL_CLK_GRAN	250000	/* pixel clock granularity */

#define GTF_MIN_VSYNC_BP	550	/* min time of vsync + back porch (us) */
#define GTF_V_FP		1	/* vertical front porch (lines) */
#define GTF_CELL_GRAN		8	/* character cell granularity */

/* Default */
#define GTF_D_M			600	/* blanking formula gradient */
#define GTF_D_C			40	/* blanking formula offset */
#define GTF_D_K			128	/* blanking formula scaling factor */
#define GTF_D_J			20	/* blanking formula scaling factor */
#define GTF_D_C_PRIME ((((GTF_D_C - GTF_D_J) * GTF_D_K) / 256) + GTF_D_J)
#define GTF_D_M_PRIME ((GTF_D_K * GTF_D_M) / 256)

/* Secondary */
#define GTF_S_M			3600	/* blanking formula gradient */
#define GTF_S_C			40	/* blanking formula offset */
#define GTF_S_K			128	/* blanking formula scaling factor */
#define GTF_S_J			35	/* blanking formula scaling factor */
#define GTF_S_C_PRIME ((((GTF_S_C - GTF_S_J) * GTF_S_K) / 256) + GTF_S_J)
#define GTF_S_M_PRIME ((GTF_S_K * GTF_S_M) / 256)

/** v4l2_detect_gtf - detect if the given timings follow the GTF standard
 * @frame_height - the total height of the frame (including blanking) in lines.
 * @hfreq - the horizontal frequency in Hz.
 * @vsync - the height of the vertical sync in lines.
 * @polarities - the horizontal and vertical polarities (same as struct
 *		v4l2_bt_timings polarities).
 * @interlaced - if this flag is true, it indicates interlaced format
 * @aspect - preferred aspect ratio. GTF has no method of determining the
 *		aspect ratio in order to derive the image width from the
 *		image height, so it has to be passed explicitly. Usually
 *		the native screen aspect ratio is used for this. If it
 *		is not filled in correctly, then 16:9 will be assumed.
 * @cap - the v4l2_dv_timings_cap capabilities.
 * @timings - the resulting timings.
 *
 * This function will attempt to detect if the given values correspond to a
 * valid GTF format. If so, then it will return true, and fmt will be filled
 * in with the found GTF timings.
 */
bool v4l2_detect_gtf(unsigned int frame_height,
		     unsigned int hfreq,
		     unsigned int vsync,
		     u32 polarities,
		     bool interlaced,
		     struct v4l2_fract aspect,
		     const struct v4l2_dv_timings_cap *cap,
		     struct v4l2_dv_timings *timings)
{
	struct v4l2_dv_timings t = {};
	int pix_clk;
	int v_fp, v_bp, h_fp, hsync;
	int frame_width, image_height, image_width;
	bool default_gtf;
	int h_blank;

	if (vsync != 3)
		return false;

	if (polarities == V4L2_DV_VSYNC_POS_POL)
		default_gtf = true;
	else if (polarities == V4L2_DV_HSYNC_POS_POL)
		default_gtf = false;
	else
		return false;

	if (hfreq == 0)
		return false;

	/* Vertical */
	v_fp = GTF_V_FP;
	v_bp = (GTF_MIN_VSYNC_BP * hfreq + 500000) / 1000000 - vsync;
	if (interlaced)
		image_height = (frame_height - 2 * v_fp - 2 * vsync - 2 * v_bp) & ~0x1;
	else
		image_height = (frame_height - v_fp - vsync - v_bp + 1) & ~0x1;

	if (image_height < 0)
		return false;

	if (aspect.numerator == 0 || aspect.denominator == 0) {
		aspect.numerator = 16;
		aspect.denominator = 9;
	}
	image_width = ((image_height * aspect.numerator) / aspect.denominator);
	image_width = (image_width + GTF_CELL_GRAN/2) & ~(GTF_CELL_GRAN - 1);

	/* Horizontal */
	if (default_gtf) {
		u64 num;
		u32 den;

		num = (((u64)image_width * GTF_D_C_PRIME * hfreq) -
		      ((u64)image_width * GTF_D_M_PRIME * 1000));
		den = (hfreq * (100 - GTF_D_C_PRIME) + GTF_D_M_PRIME * 1000) *
		      (2 * GTF_CELL_GRAN);
		h_blank = div_u64((num + (den >> 1)), den);
		h_blank *= (2 * GTF_CELL_GRAN);
	} else {
		u64 num;
		u32 den;

		num = (((u64)image_width * GTF_S_C_PRIME * hfreq) -
		      ((u64)image_width * GTF_S_M_PRIME * 1000));
		den = (hfreq * (100 - GTF_S_C_PRIME) + GTF_S_M_PRIME * 1000) *
		      (2 * GTF_CELL_GRAN);
		h_blank = div_u64((num + (den >> 1)), den);
		h_blank *= (2 * GTF_CELL_GRAN);
	}

	frame_width = image_width + h_blank;

	pix_clk = (image_width + h_blank) * hfreq;
	pix_clk = pix_clk / GTF_PXL_CLK_GRAN * GTF_PXL_CLK_GRAN;

	hsync = (frame_width * 8 + 50) / 100;
	hsync = DIV_ROUND_CLOSEST(hsync, GTF_CELL_GRAN) * GTF_CELL_GRAN;

	h_fp = h_blank / 2 - hsync;

	t.type = V4L2_DV_BT_656_1120;
	t.bt.polarities = polarities;
	t.bt.width = image_width;
	t.bt.height = image_height;
	t.bt.hfrontporch = h_fp;
	t.bt.vfrontporch = v_fp;
	t.bt.hsync = hsync;
	t.bt.vsync = vsync;
	t.bt.hbackporch = frame_width - image_width - h_fp - hsync;

	if (!interlaced) {
		t.bt.vbackporch = frame_height - image_height - v_fp - vsync;
		t.bt.interlaced = V4L2_DV_PROGRESSIVE;
	} else {
		t.bt.vbackporch = (frame_height - image_height - 2 * v_fp -
				      2 * vsync) / 2;
		t.bt.il_vbackporch = frame_height - image_height - 2 * v_fp -
					2 * vsync - t.bt.vbackporch;
		t.bt.il_vfrontporch = v_fp;
		t.bt.il_vsync = vsync;
		t.bt.flags |= V4L2_DV_FL_HALF_LINE;
		t.bt.interlaced = V4L2_DV_INTERLACED;
	}

	t.bt.pixelclock = pix_clk;
	t.bt.standards = V4L2_DV_BT_STD_GTF;

	if (!default_gtf)
		t.bt.flags |= V4L2_DV_FL_REDUCED_BLANKING;

	if (!v4l2_valid_dv_timings(&t, cap, NULL, NULL))
		return false;
	*timings = t;
	return true;
}
EXPORT_SYMBOL_GPL(v4l2_detect_gtf);

/** v4l2_calc_aspect_ratio - calculate the aspect ratio based on bytes
 *	0x15 and 0x16 from the EDID.
 * @hor_landscape - byte 0x15 from the EDID.
 * @vert_portrait - byte 0x16 from the EDID.
 *
 * Determines the aspect ratio from the EDID.
 * See VESA Enhanced EDID standard, release A, rev 2, section 3.6.2:
 * "Horizontal and Vertical Screen Size or Aspect Ratio"
 */
struct v4l2_fract v4l2_calc_aspect_ratio(u8 hor_landscape, u8 vert_portrait)
{
	struct v4l2_fract aspect = { 16, 9 };
	u8 ratio;

	/* Nothing filled in, fallback to 16:9 */
	if (!hor_landscape && !vert_portrait)
		return aspect;
	/* Both filled in, so they are interpreted as the screen size in cm */
	if (hor_landscape && vert_portrait) {
		aspect.numerator = hor_landscape;
		aspect.denominator = vert_portrait;
		return aspect;
	}
	/* Only one is filled in, so interpret them as a ratio:
	   (val + 99) / 100 */
	ratio = hor_landscape | vert_portrait;
	/* Change some rounded values into the exact aspect ratio */
	if (ratio == 79) {
		aspect.numerator = 16;
		aspect.denominator = 9;
	} else if (ratio == 34) {
		aspect.numerator = 4;
		aspect.denominator = 3;
	} else if (ratio == 68) {
		aspect.numerator = 15;
		aspect.denominator = 9;
	} else {
		aspect.numerator = hor_landscape + 99;
		aspect.denominator = 100;
	}
	if (hor_landscape)
		return aspect;
	/* The aspect ratio is for portrait, so swap numerator and denominator */
	swap(aspect.denominator, aspect.numerator);
	return aspect;
}
EXPORT_SYMBOL_GPL(v4l2_calc_aspect_ratio);

/** v4l2_hdmi_rx_colorimetry - determine HDMI colorimetry information
 *	based on various InfoFrames.
 * @avi: the AVI InfoFrame
 * @hdmi: the HDMI Vendor InfoFrame, may be NULL
 * @height: the frame height
 *
 * Determines the HDMI colorimetry information, i.e. how the HDMI
 * pixel color data should be interpreted.
 *
 * Note that some of the newer features (DCI-P3, HDR) are not yet
 * implemented: the hdmi.h header needs to be updated to the HDMI 2.0
 * and CTA-861-G standards.
 */
struct v4l2_hdmi_colorimetry
v4l2_hdmi_rx_colorimetry(const struct hdmi_avi_infoframe *avi,
			 const struct hdmi_vendor_infoframe *hdmi,
			 unsigned int height)
{
	struct v4l2_hdmi_colorimetry c = {
		V4L2_COLORSPACE_SRGB,
		V4L2_YCBCR_ENC_DEFAULT,
		V4L2_QUANTIZATION_FULL_RANGE,
		V4L2_XFER_FUNC_SRGB
	};
	bool is_ce = avi->video_code || (hdmi && hdmi->vic);
	bool is_sdtv = height <= 576;
	bool default_is_lim_range_rgb = avi->video_code > 1;

	switch (avi->colorspace) {
	case HDMI_COLORSPACE_RGB:
		/* RGB pixel encoding */
		switch (avi->colorimetry) {
		case HDMI_COLORIMETRY_EXTENDED:
			switch (avi->extended_colorimetry) {
			case HDMI_EXTENDED_COLORIMETRY_OPRGB:
				c.colorspace = V4L2_COLORSPACE_OPRGB;
				c.xfer_func = V4L2_XFER_FUNC_OPRGB;
				break;
			case HDMI_EXTENDED_COLORIMETRY_BT2020:
				c.colorspace = V4L2_COLORSPACE_BT2020;
				c.xfer_func = V4L2_XFER_FUNC_709;
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
		switch (avi->quantization_range) {
		case HDMI_QUANTIZATION_RANGE_LIMITED:
			c.quantization = V4L2_QUANTIZATION_LIM_RANGE;
			break;
		case HDMI_QUANTIZATION_RANGE_FULL:
			break;
		default:
			if (default_is_lim_range_rgb)
				c.quantization = V4L2_QUANTIZATION_LIM_RANGE;
			break;
		}
		break;

	default:
		/* YCbCr pixel encoding */
		c.quantization = V4L2_QUANTIZATION_LIM_RANGE;
		switch (avi->colorimetry) {
		case HDMI_COLORIMETRY_NONE:
			if (!is_ce)
				break;
			if (is_sdtv) {
				c.colorspace = V4L2_COLORSPACE_SMPTE170M;
				c.ycbcr_enc = V4L2_YCBCR_ENC_601;
			} else {
				c.colorspace = V4L2_COLORSPACE_REC709;
				c.ycbcr_enc = V4L2_YCBCR_ENC_709;
			}
			c.xfer_func = V4L2_XFER_FUNC_709;
			break;
		case HDMI_COLORIMETRY_ITU_601:
			c.colorspace = V4L2_COLORSPACE_SMPTE170M;
			c.ycbcr_enc = V4L2_YCBCR_ENC_601;
			c.xfer_func = V4L2_XFER_FUNC_709;
			break;
		case HDMI_COLORIMETRY_ITU_709:
			c.colorspace = V4L2_COLORSPACE_REC709;
			c.ycbcr_enc = V4L2_YCBCR_ENC_709;
			c.xfer_func = V4L2_XFER_FUNC_709;
			break;
		case HDMI_COLORIMETRY_EXTENDED:
			switch (avi->extended_colorimetry) {
			case HDMI_EXTENDED_COLORIMETRY_XV_YCC_601:
				c.colorspace = V4L2_COLORSPACE_REC709;
				c.ycbcr_enc = V4L2_YCBCR_ENC_XV709;
				c.xfer_func = V4L2_XFER_FUNC_709;
				break;
			case HDMI_EXTENDED_COLORIMETRY_XV_YCC_709:
				c.colorspace = V4L2_COLORSPACE_REC709;
				c.ycbcr_enc = V4L2_YCBCR_ENC_XV601;
				c.xfer_func = V4L2_XFER_FUNC_709;
				break;
			case HDMI_EXTENDED_COLORIMETRY_S_YCC_601:
				c.colorspace = V4L2_COLORSPACE_SRGB;
				c.ycbcr_enc = V4L2_YCBCR_ENC_601;
				c.xfer_func = V4L2_XFER_FUNC_SRGB;
				break;
			case HDMI_EXTENDED_COLORIMETRY_OPYCC_601:
				c.colorspace = V4L2_COLORSPACE_OPRGB;
				c.ycbcr_enc = V4L2_YCBCR_ENC_601;
				c.xfer_func = V4L2_XFER_FUNC_OPRGB;
				break;
			case HDMI_EXTENDED_COLORIMETRY_BT2020:
				c.colorspace = V4L2_COLORSPACE_BT2020;
				c.ycbcr_enc = V4L2_YCBCR_ENC_BT2020;
				c.xfer_func = V4L2_XFER_FUNC_709;
				break;
			case HDMI_EXTENDED_COLORIMETRY_BT2020_CONST_LUM:
				c.colorspace = V4L2_COLORSPACE_BT2020;
				c.ycbcr_enc = V4L2_YCBCR_ENC_BT2020_CONST_LUM;
				c.xfer_func = V4L2_XFER_FUNC_709;
				break;
			default: /* fall back to ITU_709 */
				c.colorspace = V4L2_COLORSPACE_REC709;
				c.ycbcr_enc = V4L2_YCBCR_ENC_709;
				c.xfer_func = V4L2_XFER_FUNC_709;
				break;
			}
			break;
		default:
			break;
		}
		/*
		 * YCC Quantization Range signaling is more-or-less broken,
		 * let's just ignore this.
		 */
		break;
	}
	return c;
}
EXPORT_SYMBOL_GPL(v4l2_hdmi_rx_colorimetry);

/**
 * v4l2_num_edid_blocks() - return the number of EDID blocks
 *
 * @edid:	pointer to the EDID data
 * @max_blocks:	maximum number of supported EDID blocks
 *
 * Return: the number of EDID blocks based on the contents of the EDID.
 *	   This supports the HDMI Forum EDID Extension Override Data Block.
 */
unsigned int v4l2_num_edid_blocks(const u8 *edid, unsigned int max_blocks)
{
	unsigned int blocks;

	if (!edid || !max_blocks)
		return 0;

	// The number of extension blocks is recorded at byte 126 of the
	// first 128-byte block in the EDID.
	//
	// If there is an HDMI Forum EDID Extension Override Data Block
	// present, then it is in bytes 4-6 of the first CTA-861 extension
	// block of the EDID.
	blocks = edid[126] + 1;
	// Check for HDMI Forum EDID Extension Override Data Block
	if (blocks >= 2 &&	// The EDID must be at least 2 blocks
	    max_blocks >= 3 &&  // The caller supports at least 3 blocks
	    edid[128] == 2 &&	// The first extension block is type CTA-861
	    edid[133] == 0x78 && // Identifier for the EEODB
	    (edid[132] & 0xe0) == 0xe0 && // Tag Code == 7
	    (edid[132] & 0x1f) >= 2 &&	// Length >= 2
	    edid[134] > 1)	// Number of extension blocks is sane
		blocks = edid[134] + 1;
	return blocks > max_blocks ? max_blocks : blocks;
}
EXPORT_SYMBOL_GPL(v4l2_num_edid_blocks);

/**
 * v4l2_get_edid_phys_addr() - find and return the physical address
 *
 * @edid:	pointer to the EDID data
 * @size:	size in bytes of the EDID data
 * @offset:	If not %NULL then the location of the physical address
 *		bytes in the EDID will be returned here. This is set to 0
 *		if there is no physical address found.
 *
 * Return: the physical address or CEC_PHYS_ADDR_INVALID if there is none.
 */
u16 v4l2_get_edid_phys_addr(const u8 *edid, unsigned int size,
			    unsigned int *offset)
{
	unsigned int loc = cec_get_edid_spa_location(edid, size);

	if (offset)
		*offset = loc;
	if (loc == 0)
		return CEC_PHYS_ADDR_INVALID;
	return (edid[loc] << 8) | edid[loc + 1];
}
EXPORT_SYMBOL_GPL(v4l2_get_edid_phys_addr);

/**
 * v4l2_set_edid_phys_addr() - find and set the physical address
 *
 * @edid:	pointer to the EDID data
 * @size:	size in bytes of the EDID data
 * @phys_addr:	the new physical address
 *
 * This function finds the location of the physical address in the EDID
 * and fills in the given physical address and updates the checksum
 * at the end of the EDID block. It does nothing if the EDID doesn't
 * contain a physical address.
 */
void v4l2_set_edid_phys_addr(u8 *edid, unsigned int size, u16 phys_addr)
{
	unsigned int loc = cec_get_edid_spa_location(edid, size);
	u8 sum = 0;
	unsigned int i;

	if (loc == 0)
		return;
	edid[loc] = phys_addr >> 8;
	edid[loc + 1] = phys_addr & 0xff;
	loc &= ~0x7f;

	/* update the checksum */
	for (i = loc; i < loc + 127; i++)
		sum += edid[i];
	edid[i] = 256 - sum;
}
EXPORT_SYMBOL_GPL(v4l2_set_edid_phys_addr);

/**
 * v4l2_phys_addr_for_input() - calculate the PA for an input
 *
 * @phys_addr:	the physical address of the parent
 * @input:	the number of the input port, must be between 1 and 15
 *
 * This function calculates a new physical address based on the input
 * port number. For example:
 *
 * PA = 0.0.0.0 and input = 2 becomes 2.0.0.0
 *
 * PA = 3.0.0.0 and input = 1 becomes 3.1.0.0
 *
 * PA = 3.2.1.0 and input = 5 becomes 3.2.1.5
 *
 * PA = 3.2.1.3 and input = 5 becomes f.f.f.f since it maxed out the depth.
 *
 * Return: the new physical address or CEC_PHYS_ADDR_INVALID.
 */
u16 v4l2_phys_addr_for_input(u16 phys_addr, u8 input)
{
	/* Check if input is sane */
	if (WARN_ON(input == 0 || input > 0xf))
		return CEC_PHYS_ADDR_INVALID;

	if (phys_addr == 0)
		return input << 12;

	if ((phys_addr & 0x0fff) == 0)
		return phys_addr | (input << 8);

	if ((phys_addr & 0x00ff) == 0)
		return phys_addr | (input << 4);

	if ((phys_addr & 0x000f) == 0)
		return phys_addr | input;

	/*
	 * All nibbles are used so no valid physical addresses can be assigned
	 * to the input.
	 */
	return CEC_PHYS_ADDR_INVALID;
}
EXPORT_SYMBOL_GPL(v4l2_phys_addr_for_input);

/**
 * v4l2_phys_addr_validate() - validate a physical address from an EDID
 *
 * @phys_addr:	the physical address to validate
 * @parent:	if not %NULL, then this is filled with the parents PA.
 * @port:	if not %NULL, then this is filled with the input port.
 *
 * This validates a physical address as read from an EDID. If the
 * PA is invalid (such as 1.0.1.0 since '0' is only allowed at the end),
 * then it will return -EINVAL.
 *
 * The parent PA is passed into %parent and the input port is passed into
 * %port. For example:
 *
 * PA = 0.0.0.0: has parent 0.0.0.0 and input port 0.
 *
 * PA = 1.0.0.0: has parent 0.0.0.0 and input port 1.
 *
 * PA = 3.2.0.0: has parent 3.0.0.0 and input port 2.
 *
 * PA = f.f.f.f: has parent f.f.f.f and input port 0.
 *
 * Return: 0 if the PA is valid, -EINVAL if not.
 */
int v4l2_phys_addr_validate(u16 phys_addr, u16 *parent, u16 *port)
{
	int i;

	if (parent)
		*parent = phys_addr;
	if (port)
		*port = 0;
	if (phys_addr == CEC_PHYS_ADDR_INVALID)
		return 0;
	for (i = 0; i < 16; i += 4)
		if (phys_addr & (0xf << i))
			break;
	if (i == 16)
		return 0;
	if (parent)
		*parent = phys_addr & (0xfff0 << i);
	if (port)
		*port = (phys_addr >> i) & 0xf;
	for (i += 4; i < 16; i += 4)
		if ((phys_addr & (0xf << i)) == 0)
			return -EINVAL;
	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_phys_addr_validate);

#ifdef CONFIG_DEBUG_FS

#define DEBUGFS_FOPS(type, flag)					\
static ssize_t								\
infoframe_read_##type(struct file *filp,				\
		      char __user *ubuf, size_t count, loff_t *ppos)	\
{									\
	struct v4l2_debugfs_if *infoframes = filp->private_data;	\
									\
	return infoframes->if_read((flag), infoframes->priv, filp,	\
				   ubuf, count, ppos);			\
}									\
									\
static const struct file_operations infoframe_##type##_fops = {		\
	.owner   = THIS_MODULE,						\
	.open    = simple_open,						\
	.read    = infoframe_read_##type,				\
}

DEBUGFS_FOPS(avi, V4L2_DEBUGFS_IF_AVI);
DEBUGFS_FOPS(audio, V4L2_DEBUGFS_IF_AUDIO);
DEBUGFS_FOPS(spd, V4L2_DEBUGFS_IF_SPD);
DEBUGFS_FOPS(hdmi, V4L2_DEBUGFS_IF_HDMI);

struct v4l2_debugfs_if *v4l2_debugfs_if_alloc(struct dentry *root, u32 if_types,
					      void *priv,
					      v4l2_debugfs_if_read_t if_read)
{
	struct v4l2_debugfs_if *infoframes;

	if (IS_ERR_OR_NULL(root) || !if_types || !if_read)
		return NULL;

	infoframes = kzalloc(sizeof(*infoframes), GFP_KERNEL);
	if (!infoframes)
		return NULL;

	infoframes->if_dir = debugfs_create_dir("infoframes", root);
	infoframes->priv = priv;
	infoframes->if_read = if_read;
	if (if_types & V4L2_DEBUGFS_IF_AVI)
		debugfs_create_file("avi", 0400, infoframes->if_dir,
				    infoframes, &infoframe_avi_fops);
	if (if_types & V4L2_DEBUGFS_IF_AUDIO)
		debugfs_create_file("audio", 0400, infoframes->if_dir,
				    infoframes, &infoframe_audio_fops);
	if (if_types & V4L2_DEBUGFS_IF_SPD)
		debugfs_create_file("spd", 0400, infoframes->if_dir,
				    infoframes, &infoframe_spd_fops);
	if (if_types & V4L2_DEBUGFS_IF_HDMI)
		debugfs_create_file("hdmi", 0400, infoframes->if_dir,
				    infoframes, &infoframe_hdmi_fops);
	return infoframes;
}
EXPORT_SYMBOL_GPL(v4l2_debugfs_if_alloc);

void v4l2_debugfs_if_free(struct v4l2_debugfs_if *infoframes)
{
	if (infoframes) {
		debugfs_remove_recursive(infoframes->if_dir);
		kfree(infoframes);
	}
}
EXPORT_SYMBOL_GPL(v4l2_debugfs_if_free);

#endif
