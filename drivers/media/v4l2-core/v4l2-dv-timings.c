/*
 * v4l2-dv-timings - dv-timings helper functions
 *
 * Copyright 2013 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
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
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/videodev2.h>
#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-dv-timings.h>

static const struct v4l2_dv_timings timings[] = {
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
};

bool v4l2_dv_valid_timings(const struct v4l2_dv_timings *t,
			   const struct v4l2_dv_timings_cap *dvcap)
{
	const struct v4l2_bt_timings *bt = &t->bt;
	const struct v4l2_bt_timings_cap *cap = &dvcap->bt;
	u32 caps = cap->capabilities;

	if (t->type != V4L2_DV_BT_656_1120)
		return false;
	if (t->type != dvcap->type ||
	    bt->height < cap->min_height ||
	    bt->height > cap->max_height ||
	    bt->width < cap->min_width ||
	    bt->width > cap->max_width ||
	    bt->pixelclock < cap->min_pixelclock ||
	    bt->pixelclock > cap->max_pixelclock ||
	    (cap->standards && !(bt->standards & cap->standards)) ||
	    (bt->interlaced && !(caps & V4L2_DV_BT_CAP_INTERLACED)) ||
	    (!bt->interlaced && !(caps & V4L2_DV_BT_CAP_PROGRESSIVE)))
		return false;
	return true;
}
EXPORT_SYMBOL_GPL(v4l2_dv_valid_timings);

int v4l2_enum_dv_timings_cap(struct v4l2_enum_dv_timings *t,
			     const struct v4l2_dv_timings_cap *cap)
{
	u32 i, idx;

	memset(t->reserved, 0, sizeof(t->reserved));
	for (i = idx = 0; i < ARRAY_SIZE(timings); i++) {
		if (v4l2_dv_valid_timings(timings + i, cap) &&
		    idx++ == t->index) {
			t->timings = timings[i];
			return 0;
		}
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(v4l2_enum_dv_timings_cap);

bool v4l2_find_dv_timings_cap(struct v4l2_dv_timings *t,
			      const struct v4l2_dv_timings_cap *cap,
			      unsigned pclock_delta)
{
	int i;

	if (!v4l2_dv_valid_timings(t, cap))
		return false;

	for (i = 0; i < ARRAY_SIZE(timings); i++) {
		if (v4l2_dv_valid_timings(timings + i, cap) &&
		    v4l_match_dv_timings(t, timings + i, pclock_delta)) {
			*t = timings[i];
			return true;
		}
	}
	return false;
}
EXPORT_SYMBOL_GPL(v4l2_find_dv_timings_cap);

/**
 * v4l_match_dv_timings - check if two timings match
 * @t1 - compare this v4l2_dv_timings struct...
 * @t2 - with this struct.
 * @pclock_delta - the allowed pixelclock deviation.
 *
 * Compare t1 with t2 with a given margin of error for the pixelclock.
 */
bool v4l_match_dv_timings(const struct v4l2_dv_timings *t1,
			  const struct v4l2_dv_timings *t2,
			  unsigned pclock_delta)
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
	    t1->bt.vfrontporch == t2->bt.vfrontporch &&
	    t1->bt.vsync == t2->bt.vsync &&
	    t1->bt.vbackporch == t2->bt.vbackporch &&
	    (!t1->bt.interlaced ||
		(t1->bt.il_vfrontporch == t2->bt.il_vfrontporch &&
		 t1->bt.il_vsync == t2->bt.il_vsync &&
		 t1->bt.il_vbackporch == t2->bt.il_vbackporch)))
		return true;
	return false;
}
EXPORT_SYMBOL_GPL(v4l_match_dv_timings);

/*
 * CVT defines
 * Based on Coordinated Video Timings Standard
 * version 1.1 September 10, 2003
 */

#define CVT_PXL_CLK_GRAN	250000	/* pixel clock granularity */

/* Normal blanking */
#define CVT_MIN_V_BPORCH	7	/* lines */
#define CVT_MIN_V_PORCH_RND	3	/* lines */
#define CVT_MIN_VSYNC_BP	550	/* min time of vsync + back porch (us) */

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
#define CVT_RB_MIN_V_BLANK   460     /* us     */
#define CVT_RB_H_SYNC         32       /* pixels */
#define CVT_RB_H_BPORCH       80       /* pixels */
#define CVT_RB_H_BLANK       160       /* pixels */

/** v4l2_detect_cvt - detect if the given timings follow the CVT standard
 * @frame_height - the total height of the frame (including blanking) in lines.
 * @hfreq - the horizontal frequency in Hz.
 * @vsync - the height of the vertical sync in lines.
 * @polarities - the horizontal and vertical polarities (same as struct
 *		v4l2_bt_timings polarities).
 * @fmt - the resulting timings.
 *
 * This function will attempt to detect if the given values correspond to a
 * valid CVT format. If so, then it will return true, and fmt will be filled
 * in with the found CVT timings.
 */
bool v4l2_detect_cvt(unsigned frame_height, unsigned hfreq, unsigned vsync,
		u32 polarities, struct v4l2_dv_timings *fmt)
{
	int  v_fp, v_bp, h_fp, h_bp, hsync;
	int  frame_width, image_height, image_width;
	bool reduced_blanking;
	unsigned pix_clk;

	if (vsync < 4 || vsync > 7)
		return false;

	if (polarities == V4L2_DV_VSYNC_POS_POL)
		reduced_blanking = false;
	else if (polarities == V4L2_DV_HSYNC_POS_POL)
		reduced_blanking = true;
	else
		return false;

	/* Vertical */
	if (reduced_blanking) {
		v_fp = CVT_RB_V_FPORCH;
		v_bp = (CVT_RB_MIN_V_BLANK * hfreq + 1999999) / 1000000;
		v_bp -= vsync + v_fp;

		if (v_bp < CVT_RB_MIN_V_BPORCH)
			v_bp = CVT_RB_MIN_V_BPORCH;
	} else {
		v_fp = CVT_MIN_V_PORCH_RND;
		v_bp = (CVT_MIN_VSYNC_BP * hfreq + 1999999) / 1000000 - vsync;

		if (v_bp < CVT_MIN_V_BPORCH)
			v_bp = CVT_MIN_V_BPORCH;
	}
	image_height = (frame_height - v_fp - vsync - v_bp + 1) & ~0x1;

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
	default:
		return false;
	}

	image_width = image_width & ~7;

	/* Horizontal */
	if (reduced_blanking) {
		pix_clk = (image_width + CVT_RB_H_BLANK) * hfreq;
		pix_clk = (pix_clk / CVT_PXL_CLK_GRAN) * CVT_PXL_CLK_GRAN;

		h_bp = CVT_RB_H_BPORCH;
		hsync = CVT_RB_H_SYNC;
		h_fp = CVT_RB_H_BLANK - h_bp - hsync;

		frame_width = image_width + CVT_RB_H_BLANK;
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

		hsync = (frame_width * 8 + 50) / 100;
		hsync = hsync - hsync % CVT_CELL_GRAN;
		h_fp = h_blank - hsync - h_bp;
	}

	fmt->bt.polarities = polarities;
	fmt->bt.width = image_width;
	fmt->bt.height = image_height;
	fmt->bt.hfrontporch = h_fp;
	fmt->bt.vfrontporch = v_fp;
	fmt->bt.hsync = hsync;
	fmt->bt.vsync = vsync;
	fmt->bt.hbackporch = frame_width - image_width - h_fp - hsync;
	fmt->bt.vbackporch = frame_height - image_height - v_fp - vsync;
	fmt->bt.pixelclock = pix_clk;
	fmt->bt.standards = V4L2_DV_BT_STD_CVT;
	if (reduced_blanking)
		fmt->bt.flags |= V4L2_DV_FL_REDUCED_BLANKING;
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
 * @aspect - preferred aspect ratio. GTF has no method of determining the
 *		aspect ratio in order to derive the image width from the
 *		image height, so it has to be passed explicitly. Usually
 *		the native screen aspect ratio is used for this. If it
 *		is not filled in correctly, then 16:9 will be assumed.
 * @fmt - the resulting timings.
 *
 * This function will attempt to detect if the given values correspond to a
 * valid GTF format. If so, then it will return true, and fmt will be filled
 * in with the found GTF timings.
 */
bool v4l2_detect_gtf(unsigned frame_height,
		unsigned hfreq,
		unsigned vsync,
		u32 polarities,
		struct v4l2_fract aspect,
		struct v4l2_dv_timings *fmt)
{
	int pix_clk;
	int  v_fp, v_bp, h_fp, hsync;
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

	/* Vertical */
	v_fp = GTF_V_FP;
	v_bp = (GTF_MIN_VSYNC_BP * hfreq + 999999) / 1000000 - vsync;
	image_height = (frame_height - v_fp - vsync - v_bp + 1) & ~0x1;

	if (aspect.numerator == 0 || aspect.denominator == 0) {
		aspect.numerator = 16;
		aspect.denominator = 9;
	}
	image_width = ((image_height * aspect.numerator) / aspect.denominator);

	/* Horizontal */
	if (default_gtf)
		h_blank = ((image_width * GTF_D_C_PRIME * hfreq) -
					(image_width * GTF_D_M_PRIME * 1000) +
			(hfreq * (100 - GTF_D_C_PRIME) + GTF_D_M_PRIME * 1000) / 2) /
			(hfreq * (100 - GTF_D_C_PRIME) + GTF_D_M_PRIME * 1000);
	else
		h_blank = ((image_width * GTF_S_C_PRIME * hfreq) -
					(image_width * GTF_S_M_PRIME * 1000) +
			(hfreq * (100 - GTF_S_C_PRIME) + GTF_S_M_PRIME * 1000) / 2) /
			(hfreq * (100 - GTF_S_C_PRIME) + GTF_S_M_PRIME * 1000);

	h_blank = h_blank - h_blank % (2 * GTF_CELL_GRAN);
	frame_width = image_width + h_blank;

	pix_clk = (image_width + h_blank) * hfreq;
	pix_clk = pix_clk / GTF_PXL_CLK_GRAN * GTF_PXL_CLK_GRAN;

	hsync = (frame_width * 8 + 50) / 100;
	hsync = hsync - hsync % GTF_CELL_GRAN;

	h_fp = h_blank / 2 - hsync;

	fmt->bt.polarities = polarities;
	fmt->bt.width = image_width;
	fmt->bt.height = image_height;
	fmt->bt.hfrontporch = h_fp;
	fmt->bt.vfrontporch = v_fp;
	fmt->bt.hsync = hsync;
	fmt->bt.vsync = vsync;
	fmt->bt.hbackporch = frame_width - image_width - h_fp - hsync;
	fmt->bt.vbackporch = frame_height - image_height - v_fp - vsync;
	fmt->bt.pixelclock = pix_clk;
	fmt->bt.standards = V4L2_DV_BT_STD_GTF;
	if (!default_gtf)
		fmt->bt.flags |= V4L2_DV_FL_REDUCED_BLANKING;
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
	u32 tmp;
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
		aspect.numerator = 3;
	} else if (ratio == 68) {
		aspect.numerator = 15;
		aspect.numerator = 9;
	} else {
		aspect.numerator = hor_landscape + 99;
		aspect.denominator = 100;
	}
	if (hor_landscape)
		return aspect;
	/* The aspect ratio is for portrait, so swap numerator and denominator */
	tmp = aspect.denominator;
	aspect.denominator = aspect.numerator;
	aspect.numerator = tmp;
	return aspect;
}
EXPORT_SYMBOL_GPL(v4l2_calc_aspect_ratio);
