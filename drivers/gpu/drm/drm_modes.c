/*
 * Copyright © 1997-2003 by The XFree86 Project, Inc.
 * Copyright © 2007 Dave Airlie
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright 2005-2006 Luc Verhaegen
 * Copyright (c) 2001, Andy Ritger  aritger@nvidia.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */

#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/export.h>
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <video/of_videomode.h>
#include <video/videomode.h>
#include <drm/drm_modes.h>

#include "drm_crtc_internal.h"

/**
 * drm_mode_debug_printmodeline - print a mode to dmesg
 * @mode: mode to print
 *
 * Describe @mode using DRM_DEBUG.
 */
void drm_mode_debug_printmodeline(const struct drm_display_mode *mode)
{
	DRM_DEBUG_KMS("Modeline " DRM_MODE_FMT "\n", DRM_MODE_ARG(mode));
}
EXPORT_SYMBOL(drm_mode_debug_printmodeline);

/**
 * drm_mode_create - create a new display mode
 * @dev: DRM device
 *
 * Create a new, cleared drm_display_mode with kzalloc, allocate an ID for it
 * and return it.
 *
 * Returns:
 * Pointer to new mode on success, NULL on error.
 */
struct drm_display_mode *drm_mode_create(struct drm_device *dev)
{
	struct drm_display_mode *nmode;

	nmode = kzalloc(sizeof(struct drm_display_mode), GFP_KERNEL);
	if (!nmode)
		return NULL;

	if (drm_mode_object_add(dev, &nmode->base, DRM_MODE_OBJECT_MODE)) {
		kfree(nmode);
		return NULL;
	}

	return nmode;
}
EXPORT_SYMBOL(drm_mode_create);

/**
 * drm_mode_destroy - remove a mode
 * @dev: DRM device
 * @mode: mode to remove
 *
 * Release @mode's unique ID, then free it @mode structure itself using kfree.
 */
void drm_mode_destroy(struct drm_device *dev, struct drm_display_mode *mode)
{
	if (!mode)
		return;

	drm_mode_object_unregister(dev, &mode->base);

	kfree(mode);
}
EXPORT_SYMBOL(drm_mode_destroy);

/**
 * drm_mode_probed_add - add a mode to a connector's probed_mode list
 * @connector: connector the new mode
 * @mode: mode data
 *
 * Add @mode to @connector's probed_mode list for later use. This list should
 * then in a second step get filtered and all the modes actually supported by
 * the hardware moved to the @connector's modes list.
 */
void drm_mode_probed_add(struct drm_connector *connector,
			 struct drm_display_mode *mode)
{
	WARN_ON(!mutex_is_locked(&connector->dev->mode_config.mutex));

	list_add_tail(&mode->head, &connector->probed_modes);
}
EXPORT_SYMBOL(drm_mode_probed_add);

/**
 * drm_cvt_mode -create a modeline based on the CVT algorithm
 * @dev: drm device
 * @hdisplay: hdisplay size
 * @vdisplay: vdisplay size
 * @vrefresh: vrefresh rate
 * @reduced: whether to use reduced blanking
 * @interlaced: whether to compute an interlaced mode
 * @margins: whether to add margins (borders)
 *
 * This function is called to generate the modeline based on CVT algorithm
 * according to the hdisplay, vdisplay, vrefresh.
 * It is based from the VESA(TM) Coordinated Video Timing Generator by
 * Graham Loveridge April 9, 2003 available at
 * http://www.elo.utfsm.cl/~elo212/docs/CVTd6r1.xls 
 *
 * And it is copied from xf86CVTmode in xserver/hw/xfree86/modes/xf86cvt.c.
 * What I have done is to translate it by using integer calculation.
 *
 * Returns:
 * The modeline based on the CVT algorithm stored in a drm_display_mode object.
 * The display mode object is allocated with drm_mode_create(). Returns NULL
 * when no mode could be allocated.
 */
struct drm_display_mode *drm_cvt_mode(struct drm_device *dev, int hdisplay,
				      int vdisplay, int vrefresh,
				      bool reduced, bool interlaced, bool margins)
{
#define HV_FACTOR			1000
	/* 1) top/bottom margin size (% of height) - default: 1.8, */
#define	CVT_MARGIN_PERCENTAGE		18
	/* 2) character cell horizontal granularity (pixels) - default 8 */
#define	CVT_H_GRANULARITY		8
	/* 3) Minimum vertical porch (lines) - default 3 */
#define	CVT_MIN_V_PORCH			3
	/* 4) Minimum number of vertical back porch lines - default 6 */
#define	CVT_MIN_V_BPORCH		6
	/* Pixel Clock step (kHz) */
#define CVT_CLOCK_STEP			250
	struct drm_display_mode *drm_mode;
	unsigned int vfieldrate, hperiod;
	int hdisplay_rnd, hmargin, vdisplay_rnd, vmargin, vsync;
	int interlace;
	u64 tmp;

	/* allocate the drm_display_mode structure. If failure, we will
	 * return directly
	 */
	drm_mode = drm_mode_create(dev);
	if (!drm_mode)
		return NULL;

	/* the CVT default refresh rate is 60Hz */
	if (!vrefresh)
		vrefresh = 60;

	/* the required field fresh rate */
	if (interlaced)
		vfieldrate = vrefresh * 2;
	else
		vfieldrate = vrefresh;

	/* horizontal pixels */
	hdisplay_rnd = hdisplay - (hdisplay % CVT_H_GRANULARITY);

	/* determine the left&right borders */
	hmargin = 0;
	if (margins) {
		hmargin = hdisplay_rnd * CVT_MARGIN_PERCENTAGE / 1000;
		hmargin -= hmargin % CVT_H_GRANULARITY;
	}
	/* find the total active pixels */
	drm_mode->hdisplay = hdisplay_rnd + 2 * hmargin;

	/* find the number of lines per field */
	if (interlaced)
		vdisplay_rnd = vdisplay / 2;
	else
		vdisplay_rnd = vdisplay;

	/* find the top & bottom borders */
	vmargin = 0;
	if (margins)
		vmargin = vdisplay_rnd * CVT_MARGIN_PERCENTAGE / 1000;

	drm_mode->vdisplay = vdisplay + 2 * vmargin;

	/* Interlaced */
	if (interlaced)
		interlace = 1;
	else
		interlace = 0;

	/* Determine VSync Width from aspect ratio */
	if (!(vdisplay % 3) && ((vdisplay * 4 / 3) == hdisplay))
		vsync = 4;
	else if (!(vdisplay % 9) && ((vdisplay * 16 / 9) == hdisplay))
		vsync = 5;
	else if (!(vdisplay % 10) && ((vdisplay * 16 / 10) == hdisplay))
		vsync = 6;
	else if (!(vdisplay % 4) && ((vdisplay * 5 / 4) == hdisplay))
		vsync = 7;
	else if (!(vdisplay % 9) && ((vdisplay * 15 / 9) == hdisplay))
		vsync = 7;
	else /* custom */
		vsync = 10;

	if (!reduced) {
		/* simplify the GTF calculation */
		/* 4) Minimum time of vertical sync + back porch interval (µs)
		 * default 550.0
		 */
		int tmp1, tmp2;
#define CVT_MIN_VSYNC_BP	550
		/* 3) Nominal HSync width (% of line period) - default 8 */
#define CVT_HSYNC_PERCENTAGE	8
		unsigned int hblank_percentage;
		int vsyncandback_porch, vback_porch, hblank;

		/* estimated the horizontal period */
		tmp1 = HV_FACTOR * 1000000  -
				CVT_MIN_VSYNC_BP * HV_FACTOR * vfieldrate;
		tmp2 = (vdisplay_rnd + 2 * vmargin + CVT_MIN_V_PORCH) * 2 +
				interlace;
		hperiod = tmp1 * 2 / (tmp2 * vfieldrate);

		tmp1 = CVT_MIN_VSYNC_BP * HV_FACTOR / hperiod + 1;
		/* 9. Find number of lines in sync + backporch */
		if (tmp1 < (vsync + CVT_MIN_V_PORCH))
			vsyncandback_porch = vsync + CVT_MIN_V_PORCH;
		else
			vsyncandback_porch = tmp1;
		/* 10. Find number of lines in back porch */
		vback_porch = vsyncandback_porch - vsync;
		drm_mode->vtotal = vdisplay_rnd + 2 * vmargin +
				vsyncandback_porch + CVT_MIN_V_PORCH;
		/* 5) Definition of Horizontal blanking time limitation */
		/* Gradient (%/kHz) - default 600 */
#define CVT_M_FACTOR	600
		/* Offset (%) - default 40 */
#define CVT_C_FACTOR	40
		/* Blanking time scaling factor - default 128 */
#define CVT_K_FACTOR	128
		/* Scaling factor weighting - default 20 */
#define CVT_J_FACTOR	20
#define CVT_M_PRIME	(CVT_M_FACTOR * CVT_K_FACTOR / 256)
#define CVT_C_PRIME	((CVT_C_FACTOR - CVT_J_FACTOR) * CVT_K_FACTOR / 256 + \
			 CVT_J_FACTOR)
		/* 12. Find ideal blanking duty cycle from formula */
		hblank_percentage = CVT_C_PRIME * HV_FACTOR - CVT_M_PRIME *
					hperiod / 1000;
		/* 13. Blanking time */
		if (hblank_percentage < 20 * HV_FACTOR)
			hblank_percentage = 20 * HV_FACTOR;
		hblank = drm_mode->hdisplay * hblank_percentage /
			 (100 * HV_FACTOR - hblank_percentage);
		hblank -= hblank % (2 * CVT_H_GRANULARITY);
		/* 14. find the total pixels per line */
		drm_mode->htotal = drm_mode->hdisplay + hblank;
		drm_mode->hsync_end = drm_mode->hdisplay + hblank / 2;
		drm_mode->hsync_start = drm_mode->hsync_end -
			(drm_mode->htotal * CVT_HSYNC_PERCENTAGE) / 100;
		drm_mode->hsync_start += CVT_H_GRANULARITY -
			drm_mode->hsync_start % CVT_H_GRANULARITY;
		/* fill the Vsync values */
		drm_mode->vsync_start = drm_mode->vdisplay + CVT_MIN_V_PORCH;
		drm_mode->vsync_end = drm_mode->vsync_start + vsync;
	} else {
		/* Reduced blanking */
		/* Minimum vertical blanking interval time (µs)- default 460 */
#define CVT_RB_MIN_VBLANK	460
		/* Fixed number of clocks for horizontal sync */
#define CVT_RB_H_SYNC		32
		/* Fixed number of clocks for horizontal blanking */
#define CVT_RB_H_BLANK		160
		/* Fixed number of lines for vertical front porch - default 3*/
#define CVT_RB_VFPORCH		3
		int vbilines;
		int tmp1, tmp2;
		/* 8. Estimate Horizontal period. */
		tmp1 = HV_FACTOR * 1000000 -
			CVT_RB_MIN_VBLANK * HV_FACTOR * vfieldrate;
		tmp2 = vdisplay_rnd + 2 * vmargin;
		hperiod = tmp1 / (tmp2 * vfieldrate);
		/* 9. Find number of lines in vertical blanking */
		vbilines = CVT_RB_MIN_VBLANK * HV_FACTOR / hperiod + 1;
		/* 10. Check if vertical blanking is sufficient */
		if (vbilines < (CVT_RB_VFPORCH + vsync + CVT_MIN_V_BPORCH))
			vbilines = CVT_RB_VFPORCH + vsync + CVT_MIN_V_BPORCH;
		/* 11. Find total number of lines in vertical field */
		drm_mode->vtotal = vdisplay_rnd + 2 * vmargin + vbilines;
		/* 12. Find total number of pixels in a line */
		drm_mode->htotal = drm_mode->hdisplay + CVT_RB_H_BLANK;
		/* Fill in HSync values */
		drm_mode->hsync_end = drm_mode->hdisplay + CVT_RB_H_BLANK / 2;
		drm_mode->hsync_start = drm_mode->hsync_end - CVT_RB_H_SYNC;
		/* Fill in VSync values */
		drm_mode->vsync_start = drm_mode->vdisplay + CVT_RB_VFPORCH;
		drm_mode->vsync_end = drm_mode->vsync_start + vsync;
	}
	/* 15/13. Find pixel clock frequency (kHz for xf86) */
	tmp = drm_mode->htotal; /* perform intermediate calcs in u64 */
	tmp *= HV_FACTOR * 1000;
	do_div(tmp, hperiod);
	tmp -= drm_mode->clock % CVT_CLOCK_STEP;
	drm_mode->clock = tmp;
	/* 18/16. Find actual vertical frame frequency */
	/* ignore - just set the mode flag for interlaced */
	if (interlaced) {
		drm_mode->vtotal *= 2;
		drm_mode->flags |= DRM_MODE_FLAG_INTERLACE;
	}
	/* Fill the mode line name */
	drm_mode_set_name(drm_mode);
	if (reduced)
		drm_mode->flags |= (DRM_MODE_FLAG_PHSYNC |
					DRM_MODE_FLAG_NVSYNC);
	else
		drm_mode->flags |= (DRM_MODE_FLAG_PVSYNC |
					DRM_MODE_FLAG_NHSYNC);

	return drm_mode;
}
EXPORT_SYMBOL(drm_cvt_mode);

/**
 * drm_gtf_mode_complex - create the modeline based on the full GTF algorithm
 * @dev: drm device
 * @hdisplay: hdisplay size
 * @vdisplay: vdisplay size
 * @vrefresh: vrefresh rate.
 * @interlaced: whether to compute an interlaced mode
 * @margins: desired margin (borders) size
 * @GTF_M: extended GTF formula parameters
 * @GTF_2C: extended GTF formula parameters
 * @GTF_K: extended GTF formula parameters
 * @GTF_2J: extended GTF formula parameters
 *
 * GTF feature blocks specify C and J in multiples of 0.5, so we pass them
 * in here multiplied by two.  For a C of 40, pass in 80.
 *
 * Returns:
 * The modeline based on the full GTF algorithm stored in a drm_display_mode object.
 * The display mode object is allocated with drm_mode_create(). Returns NULL
 * when no mode could be allocated.
 */
struct drm_display_mode *
drm_gtf_mode_complex(struct drm_device *dev, int hdisplay, int vdisplay,
		     int vrefresh, bool interlaced, int margins,
		     int GTF_M, int GTF_2C, int GTF_K, int GTF_2J)
{	/* 1) top/bottom margin size (% of height) - default: 1.8, */
#define	GTF_MARGIN_PERCENTAGE		18
	/* 2) character cell horizontal granularity (pixels) - default 8 */
#define	GTF_CELL_GRAN			8
	/* 3) Minimum vertical porch (lines) - default 3 */
#define	GTF_MIN_V_PORCH			1
	/* width of vsync in lines */
#define V_SYNC_RQD			3
	/* width of hsync as % of total line */
#define H_SYNC_PERCENT			8
	/* min time of vsync + back porch (microsec) */
#define MIN_VSYNC_PLUS_BP		550
	/* C' and M' are part of the Blanking Duty Cycle computation */
#define GTF_C_PRIME	((((GTF_2C - GTF_2J) * GTF_K / 256) + GTF_2J) / 2)
#define GTF_M_PRIME	(GTF_K * GTF_M / 256)
	struct drm_display_mode *drm_mode;
	unsigned int hdisplay_rnd, vdisplay_rnd, vfieldrate_rqd;
	int top_margin, bottom_margin;
	int interlace;
	unsigned int hfreq_est;
	int vsync_plus_bp, vback_porch;
	unsigned int vtotal_lines, vfieldrate_est, hperiod;
	unsigned int vfield_rate, vframe_rate;
	int left_margin, right_margin;
	unsigned int total_active_pixels, ideal_duty_cycle;
	unsigned int hblank, total_pixels, pixel_freq;
	int hsync, hfront_porch, vodd_front_porch_lines;
	unsigned int tmp1, tmp2;

	drm_mode = drm_mode_create(dev);
	if (!drm_mode)
		return NULL;

	/* 1. In order to give correct results, the number of horizontal
	 * pixels requested is first processed to ensure that it is divisible
	 * by the character size, by rounding it to the nearest character
	 * cell boundary:
	 */
	hdisplay_rnd = (hdisplay + GTF_CELL_GRAN / 2) / GTF_CELL_GRAN;
	hdisplay_rnd = hdisplay_rnd * GTF_CELL_GRAN;

	/* 2. If interlace is requested, the number of vertical lines assumed
	 * by the calculation must be halved, as the computation calculates
	 * the number of vertical lines per field.
	 */
	if (interlaced)
		vdisplay_rnd = vdisplay / 2;
	else
		vdisplay_rnd = vdisplay;

	/* 3. Find the frame rate required: */
	if (interlaced)
		vfieldrate_rqd = vrefresh * 2;
	else
		vfieldrate_rqd = vrefresh;

	/* 4. Find number of lines in Top margin: */
	top_margin = 0;
	if (margins)
		top_margin = (vdisplay_rnd * GTF_MARGIN_PERCENTAGE + 500) /
				1000;
	/* 5. Find number of lines in bottom margin: */
	bottom_margin = top_margin;

	/* 6. If interlace is required, then set variable interlace: */
	if (interlaced)
		interlace = 1;
	else
		interlace = 0;

	/* 7. Estimate the Horizontal frequency */
	{
		tmp1 = (1000000  - MIN_VSYNC_PLUS_BP * vfieldrate_rqd) / 500;
		tmp2 = (vdisplay_rnd + 2 * top_margin + GTF_MIN_V_PORCH) *
				2 + interlace;
		hfreq_est = (tmp2 * 1000 * vfieldrate_rqd) / tmp1;
	}

	/* 8. Find the number of lines in V sync + back porch */
	/* [V SYNC+BP] = RINT(([MIN VSYNC+BP] * hfreq_est / 1000000)) */
	vsync_plus_bp = MIN_VSYNC_PLUS_BP * hfreq_est / 1000;
	vsync_plus_bp = (vsync_plus_bp + 500) / 1000;
	/*  9. Find the number of lines in V back porch alone: */
	vback_porch = vsync_plus_bp - V_SYNC_RQD;
	/*  10. Find the total number of lines in Vertical field period: */
	vtotal_lines = vdisplay_rnd + top_margin + bottom_margin +
			vsync_plus_bp + GTF_MIN_V_PORCH;
	/*  11. Estimate the Vertical field frequency: */
	vfieldrate_est = hfreq_est / vtotal_lines;
	/*  12. Find the actual horizontal period: */
	hperiod = 1000000 / (vfieldrate_rqd * vtotal_lines);

	/*  13. Find the actual Vertical field frequency: */
	vfield_rate = hfreq_est / vtotal_lines;
	/*  14. Find the Vertical frame frequency: */
	if (interlaced)
		vframe_rate = vfield_rate / 2;
	else
		vframe_rate = vfield_rate;
	/*  15. Find number of pixels in left margin: */
	if (margins)
		left_margin = (hdisplay_rnd * GTF_MARGIN_PERCENTAGE + 500) /
				1000;
	else
		left_margin = 0;

	/* 16.Find number of pixels in right margin: */
	right_margin = left_margin;
	/* 17.Find total number of active pixels in image and left and right */
	total_active_pixels = hdisplay_rnd + left_margin + right_margin;
	/* 18.Find the ideal blanking duty cycle from blanking duty cycle */
	ideal_duty_cycle = GTF_C_PRIME * 1000 -
				(GTF_M_PRIME * 1000000 / hfreq_est);
	/* 19.Find the number of pixels in the blanking time to the nearest
	 * double character cell: */
	hblank = total_active_pixels * ideal_duty_cycle /
			(100000 - ideal_duty_cycle);
	hblank = (hblank + GTF_CELL_GRAN) / (2 * GTF_CELL_GRAN);
	hblank = hblank * 2 * GTF_CELL_GRAN;
	/* 20.Find total number of pixels: */
	total_pixels = total_active_pixels + hblank;
	/* 21.Find pixel clock frequency: */
	pixel_freq = total_pixels * hfreq_est / 1000;
	/* Stage 1 computations are now complete; I should really pass
	 * the results to another function and do the Stage 2 computations,
	 * but I only need a few more values so I'll just append the
	 * computations here for now */
	/* 17. Find the number of pixels in the horizontal sync period: */
	hsync = H_SYNC_PERCENT * total_pixels / 100;
	hsync = (hsync + GTF_CELL_GRAN / 2) / GTF_CELL_GRAN;
	hsync = hsync * GTF_CELL_GRAN;
	/* 18. Find the number of pixels in horizontal front porch period */
	hfront_porch = hblank / 2 - hsync;
	/*  36. Find the number of lines in the odd front porch period: */
	vodd_front_porch_lines = GTF_MIN_V_PORCH ;

	/* finally, pack the results in the mode struct */
	drm_mode->hdisplay = hdisplay_rnd;
	drm_mode->hsync_start = hdisplay_rnd + hfront_porch;
	drm_mode->hsync_end = drm_mode->hsync_start + hsync;
	drm_mode->htotal = total_pixels;
	drm_mode->vdisplay = vdisplay_rnd;
	drm_mode->vsync_start = vdisplay_rnd + vodd_front_porch_lines;
	drm_mode->vsync_end = drm_mode->vsync_start + V_SYNC_RQD;
	drm_mode->vtotal = vtotal_lines;

	drm_mode->clock = pixel_freq;

	if (interlaced) {
		drm_mode->vtotal *= 2;
		drm_mode->flags |= DRM_MODE_FLAG_INTERLACE;
	}

	drm_mode_set_name(drm_mode);
	if (GTF_M == 600 && GTF_2C == 80 && GTF_K == 128 && GTF_2J == 40)
		drm_mode->flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC;
	else
		drm_mode->flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC;

	return drm_mode;
}
EXPORT_SYMBOL(drm_gtf_mode_complex);

/**
 * drm_gtf_mode - create the modeline based on the GTF algorithm
 * @dev: drm device
 * @hdisplay: hdisplay size
 * @vdisplay: vdisplay size
 * @vrefresh: vrefresh rate.
 * @interlaced: whether to compute an interlaced mode
 * @margins: desired margin (borders) size
 *
 * return the modeline based on GTF algorithm
 *
 * This function is to create the modeline based on the GTF algorithm.
 * Generalized Timing Formula is derived from:
 *
 *	GTF Spreadsheet by Andy Morrish (1/5/97)
 *	available at http://www.vesa.org
 *
 * And it is copied from the file of xserver/hw/xfree86/modes/xf86gtf.c.
 * What I have done is to translate it by using integer calculation.
 * I also refer to the function of fb_get_mode in the file of
 * drivers/video/fbmon.c
 *
 * Standard GTF parameters::
 *
 *     M = 600
 *     C = 40
 *     K = 128
 *     J = 20
 *
 * Returns:
 * The modeline based on the GTF algorithm stored in a drm_display_mode object.
 * The display mode object is allocated with drm_mode_create(). Returns NULL
 * when no mode could be allocated.
 */
struct drm_display_mode *
drm_gtf_mode(struct drm_device *dev, int hdisplay, int vdisplay, int vrefresh,
	     bool interlaced, int margins)
{
	return drm_gtf_mode_complex(dev, hdisplay, vdisplay, vrefresh,
				    interlaced, margins,
				    600, 40 * 2, 128, 20 * 2);
}
EXPORT_SYMBOL(drm_gtf_mode);

#ifdef CONFIG_VIDEOMODE_HELPERS
/**
 * drm_display_mode_from_videomode - fill in @dmode using @vm,
 * @vm: videomode structure to use as source
 * @dmode: drm_display_mode structure to use as destination
 *
 * Fills out @dmode using the display mode specified in @vm.
 */
void drm_display_mode_from_videomode(const struct videomode *vm,
				     struct drm_display_mode *dmode)
{
	dmode->hdisplay = vm->hactive;
	dmode->hsync_start = dmode->hdisplay + vm->hfront_porch;
	dmode->hsync_end = dmode->hsync_start + vm->hsync_len;
	dmode->htotal = dmode->hsync_end + vm->hback_porch;

	dmode->vdisplay = vm->vactive;
	dmode->vsync_start = dmode->vdisplay + vm->vfront_porch;
	dmode->vsync_end = dmode->vsync_start + vm->vsync_len;
	dmode->vtotal = dmode->vsync_end + vm->vback_porch;

	dmode->clock = vm->pixelclock / 1000;

	dmode->flags = 0;
	if (vm->flags & DISPLAY_FLAGS_HSYNC_HIGH)
		dmode->flags |= DRM_MODE_FLAG_PHSYNC;
	else if (vm->flags & DISPLAY_FLAGS_HSYNC_LOW)
		dmode->flags |= DRM_MODE_FLAG_NHSYNC;
	if (vm->flags & DISPLAY_FLAGS_VSYNC_HIGH)
		dmode->flags |= DRM_MODE_FLAG_PVSYNC;
	else if (vm->flags & DISPLAY_FLAGS_VSYNC_LOW)
		dmode->flags |= DRM_MODE_FLAG_NVSYNC;
	if (vm->flags & DISPLAY_FLAGS_INTERLACED)
		dmode->flags |= DRM_MODE_FLAG_INTERLACE;
	if (vm->flags & DISPLAY_FLAGS_DOUBLESCAN)
		dmode->flags |= DRM_MODE_FLAG_DBLSCAN;
	if (vm->flags & DISPLAY_FLAGS_DOUBLECLK)
		dmode->flags |= DRM_MODE_FLAG_DBLCLK;
	drm_mode_set_name(dmode);
}
EXPORT_SYMBOL_GPL(drm_display_mode_from_videomode);

/**
 * drm_display_mode_to_videomode - fill in @vm using @dmode,
 * @dmode: drm_display_mode structure to use as source
 * @vm: videomode structure to use as destination
 *
 * Fills out @vm using the display mode specified in @dmode.
 */
void drm_display_mode_to_videomode(const struct drm_display_mode *dmode,
				   struct videomode *vm)
{
	vm->hactive = dmode->hdisplay;
	vm->hfront_porch = dmode->hsync_start - dmode->hdisplay;
	vm->hsync_len = dmode->hsync_end - dmode->hsync_start;
	vm->hback_porch = dmode->htotal - dmode->hsync_end;

	vm->vactive = dmode->vdisplay;
	vm->vfront_porch = dmode->vsync_start - dmode->vdisplay;
	vm->vsync_len = dmode->vsync_end - dmode->vsync_start;
	vm->vback_porch = dmode->vtotal - dmode->vsync_end;

	vm->pixelclock = dmode->clock * 1000;

	vm->flags = 0;
	if (dmode->flags & DRM_MODE_FLAG_PHSYNC)
		vm->flags |= DISPLAY_FLAGS_HSYNC_HIGH;
	else if (dmode->flags & DRM_MODE_FLAG_NHSYNC)
		vm->flags |= DISPLAY_FLAGS_HSYNC_LOW;
	if (dmode->flags & DRM_MODE_FLAG_PVSYNC)
		vm->flags |= DISPLAY_FLAGS_VSYNC_HIGH;
	else if (dmode->flags & DRM_MODE_FLAG_NVSYNC)
		vm->flags |= DISPLAY_FLAGS_VSYNC_LOW;
	if (dmode->flags & DRM_MODE_FLAG_INTERLACE)
		vm->flags |= DISPLAY_FLAGS_INTERLACED;
	if (dmode->flags & DRM_MODE_FLAG_DBLSCAN)
		vm->flags |= DISPLAY_FLAGS_DOUBLESCAN;
	if (dmode->flags & DRM_MODE_FLAG_DBLCLK)
		vm->flags |= DISPLAY_FLAGS_DOUBLECLK;
}
EXPORT_SYMBOL_GPL(drm_display_mode_to_videomode);

/**
 * drm_bus_flags_from_videomode - extract information about pixelclk and
 * DE polarity from videomode and store it in a separate variable
 * @vm: videomode structure to use
 * @bus_flags: information about pixelclk and DE polarity will be stored here
 *
 * Sets DRM_BUS_FLAG_DE_(LOW|HIGH) and DRM_BUS_FLAG_PIXDATA_(POS|NEG)EDGE
 * in @bus_flags according to DISPLAY_FLAGS found in @vm
 */
void drm_bus_flags_from_videomode(const struct videomode *vm, u32 *bus_flags)
{
	*bus_flags = 0;
	if (vm->flags & DISPLAY_FLAGS_PIXDATA_POSEDGE)
		*bus_flags |= DRM_BUS_FLAG_PIXDATA_POSEDGE;
	if (vm->flags & DISPLAY_FLAGS_PIXDATA_NEGEDGE)
		*bus_flags |= DRM_BUS_FLAG_PIXDATA_NEGEDGE;

	if (vm->flags & DISPLAY_FLAGS_DE_LOW)
		*bus_flags |= DRM_BUS_FLAG_DE_LOW;
	if (vm->flags & DISPLAY_FLAGS_DE_HIGH)
		*bus_flags |= DRM_BUS_FLAG_DE_HIGH;
}
EXPORT_SYMBOL_GPL(drm_bus_flags_from_videomode);

#ifdef CONFIG_OF
/**
 * of_get_drm_display_mode - get a drm_display_mode from devicetree
 * @np: device_node with the timing specification
 * @dmode: will be set to the return value
 * @bus_flags: information about pixelclk and DE polarity
 * @index: index into the list of display timings in devicetree
 *
 * This function is expensive and should only be used, if only one mode is to be
 * read from DT. To get multiple modes start with of_get_display_timings and
 * work with that instead.
 *
 * Returns:
 * 0 on success, a negative errno code when no of videomode node was found.
 */
int of_get_drm_display_mode(struct device_node *np,
			    struct drm_display_mode *dmode, u32 *bus_flags,
			    int index)
{
	struct videomode vm;
	int ret;

	ret = of_get_videomode(np, &vm, index);
	if (ret)
		return ret;

	drm_display_mode_from_videomode(&vm, dmode);
	if (bus_flags)
		drm_bus_flags_from_videomode(&vm, bus_flags);

	pr_debug("%pOF: got %dx%d display mode from %s\n",
		np, vm.hactive, vm.vactive, np->name);
	drm_mode_debug_printmodeline(dmode);

	return 0;
}
EXPORT_SYMBOL_GPL(of_get_drm_display_mode);
#endif /* CONFIG_OF */
#endif /* CONFIG_VIDEOMODE_HELPERS */

/**
 * drm_mode_set_name - set the name on a mode
 * @mode: name will be set in this mode
 *
 * Set the name of @mode to a standard format which is <hdisplay>x<vdisplay>
 * with an optional 'i' suffix for interlaced modes.
 */
void drm_mode_set_name(struct drm_display_mode *mode)
{
	bool interlaced = !!(mode->flags & DRM_MODE_FLAG_INTERLACE);

	snprintf(mode->name, DRM_DISPLAY_MODE_LEN, "%dx%d%s",
		 mode->hdisplay, mode->vdisplay,
		 interlaced ? "i" : "");
}
EXPORT_SYMBOL(drm_mode_set_name);

/**
 * drm_mode_hsync - get the hsync of a mode
 * @mode: mode
 *
 * Returns:
 * @modes's hsync rate in kHz, rounded to the nearest integer. Calculates the
 * value first if it is not yet set.
 */
int drm_mode_hsync(const struct drm_display_mode *mode)
{
	unsigned int calc_val;

	if (mode->hsync)
		return mode->hsync;

	if (mode->htotal < 0)
		return 0;

	calc_val = (mode->clock * 1000) / mode->htotal; /* hsync in Hz */
	calc_val += 500;				/* round to 1000Hz */
	calc_val /= 1000;				/* truncate to kHz */

	return calc_val;
}
EXPORT_SYMBOL(drm_mode_hsync);

/**
 * drm_mode_vrefresh - get the vrefresh of a mode
 * @mode: mode
 *
 * Returns:
 * @modes's vrefresh rate in Hz, rounded to the nearest integer. Calculates the
 * value first if it is not yet set.
 */
int drm_mode_vrefresh(const struct drm_display_mode *mode)
{
	int refresh = 0;

	if (mode->vrefresh > 0)
		refresh = mode->vrefresh;
	else if (mode->htotal > 0 && mode->vtotal > 0) {
		unsigned int num, den;

		num = mode->clock * 1000;
		den = mode->htotal * mode->vtotal;

		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			num *= 2;
		if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
			den *= 2;
		if (mode->vscan > 1)
			den *= mode->vscan;

		refresh = DIV_ROUND_CLOSEST(num, den);
	}
	return refresh;
}
EXPORT_SYMBOL(drm_mode_vrefresh);

/**
 * drm_mode_get_hv_timing - Fetches hdisplay/vdisplay for given mode
 * @mode: mode to query
 * @hdisplay: hdisplay value to fill in
 * @vdisplay: vdisplay value to fill in
 *
 * The vdisplay value will be doubled if the specified mode is a stereo mode of
 * the appropriate layout.
 */
void drm_mode_get_hv_timing(const struct drm_display_mode *mode,
			    int *hdisplay, int *vdisplay)
{
	struct drm_display_mode adjusted = *mode;

	drm_mode_set_crtcinfo(&adjusted, CRTC_STEREO_DOUBLE_ONLY);
	*hdisplay = adjusted.crtc_hdisplay;
	*vdisplay = adjusted.crtc_vdisplay;
}
EXPORT_SYMBOL(drm_mode_get_hv_timing);

/**
 * drm_mode_set_crtcinfo - set CRTC modesetting timing parameters
 * @p: mode
 * @adjust_flags: a combination of adjustment flags
 *
 * Setup the CRTC modesetting timing parameters for @p, adjusting if necessary.
 *
 * - The CRTC_INTERLACE_HALVE_V flag can be used to halve vertical timings of
 *   interlaced modes.
 * - The CRTC_STEREO_DOUBLE flag can be used to compute the timings for
 *   buffers containing two eyes (only adjust the timings when needed, eg. for
 *   "frame packing" or "side by side full").
 * - The CRTC_NO_DBLSCAN and CRTC_NO_VSCAN flags request that adjustment *not*
 *   be performed for doublescan and vscan > 1 modes respectively.
 */
void drm_mode_set_crtcinfo(struct drm_display_mode *p, int adjust_flags)
{
	if (!p)
		return;

	p->crtc_clock = p->clock;
	p->crtc_hdisplay = p->hdisplay;
	p->crtc_hsync_start = p->hsync_start;
	p->crtc_hsync_end = p->hsync_end;
	p->crtc_htotal = p->htotal;
	p->crtc_hskew = p->hskew;
	p->crtc_vdisplay = p->vdisplay;
	p->crtc_vsync_start = p->vsync_start;
	p->crtc_vsync_end = p->vsync_end;
	p->crtc_vtotal = p->vtotal;

	if (p->flags & DRM_MODE_FLAG_INTERLACE) {
		if (adjust_flags & CRTC_INTERLACE_HALVE_V) {
			p->crtc_vdisplay /= 2;
			p->crtc_vsync_start /= 2;
			p->crtc_vsync_end /= 2;
			p->crtc_vtotal /= 2;
		}
	}

	if (!(adjust_flags & CRTC_NO_DBLSCAN)) {
		if (p->flags & DRM_MODE_FLAG_DBLSCAN) {
			p->crtc_vdisplay *= 2;
			p->crtc_vsync_start *= 2;
			p->crtc_vsync_end *= 2;
			p->crtc_vtotal *= 2;
		}
	}

	if (!(adjust_flags & CRTC_NO_VSCAN)) {
		if (p->vscan > 1) {
			p->crtc_vdisplay *= p->vscan;
			p->crtc_vsync_start *= p->vscan;
			p->crtc_vsync_end *= p->vscan;
			p->crtc_vtotal *= p->vscan;
		}
	}

	if (adjust_flags & CRTC_STEREO_DOUBLE) {
		unsigned int layout = p->flags & DRM_MODE_FLAG_3D_MASK;

		switch (layout) {
		case DRM_MODE_FLAG_3D_FRAME_PACKING:
			p->crtc_clock *= 2;
			p->crtc_vdisplay += p->crtc_vtotal;
			p->crtc_vsync_start += p->crtc_vtotal;
			p->crtc_vsync_end += p->crtc_vtotal;
			p->crtc_vtotal += p->crtc_vtotal;
			break;
		}
	}

	p->crtc_vblank_start = min(p->crtc_vsync_start, p->crtc_vdisplay);
	p->crtc_vblank_end = max(p->crtc_vsync_end, p->crtc_vtotal);
	p->crtc_hblank_start = min(p->crtc_hsync_start, p->crtc_hdisplay);
	p->crtc_hblank_end = max(p->crtc_hsync_end, p->crtc_htotal);
}
EXPORT_SYMBOL(drm_mode_set_crtcinfo);

/**
 * drm_mode_copy - copy the mode
 * @dst: mode to overwrite
 * @src: mode to copy
 *
 * Copy an existing mode into another mode, preserving the object id and
 * list head of the destination mode.
 */
void drm_mode_copy(struct drm_display_mode *dst, const struct drm_display_mode *src)
{
	int id = dst->base.id;
	struct list_head head = dst->head;

	*dst = *src;
	dst->base.id = id;
	dst->head = head;
}
EXPORT_SYMBOL(drm_mode_copy);

/**
 * drm_mode_duplicate - allocate and duplicate an existing mode
 * @dev: drm_device to allocate the duplicated mode for
 * @mode: mode to duplicate
 *
 * Just allocate a new mode, copy the existing mode into it, and return
 * a pointer to it.  Used to create new instances of established modes.
 *
 * Returns:
 * Pointer to duplicated mode on success, NULL on error.
 */
struct drm_display_mode *drm_mode_duplicate(struct drm_device *dev,
					    const struct drm_display_mode *mode)
{
	struct drm_display_mode *nmode;

	nmode = drm_mode_create(dev);
	if (!nmode)
		return NULL;

	drm_mode_copy(nmode, mode);

	return nmode;
}
EXPORT_SYMBOL(drm_mode_duplicate);

/**
 * drm_mode_equal - test modes for equality
 * @mode1: first mode
 * @mode2: second mode
 *
 * Check to see if @mode1 and @mode2 are equivalent.
 *
 * Returns:
 * True if the modes are equal, false otherwise.
 */
bool drm_mode_equal(const struct drm_display_mode *mode1, const struct drm_display_mode *mode2)
{
	if (!mode1 && !mode2)
		return true;

	if (!mode1 || !mode2)
		return false;

	/* do clock check convert to PICOS so fb modes get matched
	 * the same */
	if (mode1->clock && mode2->clock) {
		if (KHZ2PICOS(mode1->clock) != KHZ2PICOS(mode2->clock))
			return false;
	} else if (mode1->clock != mode2->clock)
		return false;

	return drm_mode_equal_no_clocks(mode1, mode2);
}
EXPORT_SYMBOL(drm_mode_equal);

/**
 * drm_mode_equal_no_clocks - test modes for equality
 * @mode1: first mode
 * @mode2: second mode
 *
 * Check to see if @mode1 and @mode2 are equivalent, but
 * don't check the pixel clocks.
 *
 * Returns:
 * True if the modes are equal, false otherwise.
 */
bool drm_mode_equal_no_clocks(const struct drm_display_mode *mode1, const struct drm_display_mode *mode2)
{
	if ((mode1->flags & DRM_MODE_FLAG_3D_MASK) !=
	    (mode2->flags & DRM_MODE_FLAG_3D_MASK))
		return false;

	return drm_mode_equal_no_clocks_no_stereo(mode1, mode2);
}
EXPORT_SYMBOL(drm_mode_equal_no_clocks);

/**
 * drm_mode_equal_no_clocks_no_stereo - test modes for equality
 * @mode1: first mode
 * @mode2: second mode
 *
 * Check to see if @mode1 and @mode2 are equivalent, but
 * don't check the pixel clocks nor the stereo layout.
 *
 * Returns:
 * True if the modes are equal, false otherwise.
 */
bool drm_mode_equal_no_clocks_no_stereo(const struct drm_display_mode *mode1,
					const struct drm_display_mode *mode2)
{
	if (mode1->hdisplay == mode2->hdisplay &&
	    mode1->hsync_start == mode2->hsync_start &&
	    mode1->hsync_end == mode2->hsync_end &&
	    mode1->htotal == mode2->htotal &&
	    mode1->hskew == mode2->hskew &&
	    mode1->vdisplay == mode2->vdisplay &&
	    mode1->vsync_start == mode2->vsync_start &&
	    mode1->vsync_end == mode2->vsync_end &&
	    mode1->vtotal == mode2->vtotal &&
	    mode1->vscan == mode2->vscan &&
	    (mode1->flags & ~DRM_MODE_FLAG_3D_MASK) ==
	     (mode2->flags & ~DRM_MODE_FLAG_3D_MASK))
		return true;

	return false;
}
EXPORT_SYMBOL(drm_mode_equal_no_clocks_no_stereo);

static enum drm_mode_status
drm_mode_validate_basic(const struct drm_display_mode *mode)
{
	if (mode->type & ~DRM_MODE_TYPE_ALL)
		return MODE_BAD;

	if (mode->flags & ~DRM_MODE_FLAG_ALL)
		return MODE_BAD;

	if ((mode->flags & DRM_MODE_FLAG_3D_MASK) > DRM_MODE_FLAG_3D_MAX)
		return MODE_BAD;

	if (mode->clock == 0)
		return MODE_CLOCK_LOW;

	if (mode->hdisplay == 0 ||
	    mode->hsync_start < mode->hdisplay ||
	    mode->hsync_end < mode->hsync_start ||
	    mode->htotal < mode->hsync_end)
		return MODE_H_ILLEGAL;

	if (mode->vdisplay == 0 ||
	    mode->vsync_start < mode->vdisplay ||
	    mode->vsync_end < mode->vsync_start ||
	    mode->vtotal < mode->vsync_end)
		return MODE_V_ILLEGAL;

	return MODE_OK;
}

/**
 * drm_mode_validate_driver - make sure the mode is somewhat sane
 * @dev: drm device
 * @mode: mode to check
 *
 * First do basic validation on the mode, and then allow the driver
 * to check for device/driver specific limitations via the optional
 * &drm_mode_config_helper_funcs.mode_valid hook.
 *
 * Returns:
 * The mode status
 */
enum drm_mode_status
drm_mode_validate_driver(struct drm_device *dev,
			const struct drm_display_mode *mode)
{
	enum drm_mode_status status;

	status = drm_mode_validate_basic(mode);
	if (status != MODE_OK)
		return status;

	if (dev->mode_config.funcs->mode_valid)
		return dev->mode_config.funcs->mode_valid(dev, mode);
	else
		return MODE_OK;
}
EXPORT_SYMBOL(drm_mode_validate_driver);

/**
 * drm_mode_validate_size - make sure modes adhere to size constraints
 * @mode: mode to check
 * @maxX: maximum width
 * @maxY: maximum height
 *
 * This function is a helper which can be used to validate modes against size
 * limitations of the DRM device/connector. If a mode is too big its status
 * member is updated with the appropriate validation failure code. The list
 * itself is not changed.
 *
 * Returns:
 * The mode status
 */
enum drm_mode_status
drm_mode_validate_size(const struct drm_display_mode *mode,
		       int maxX, int maxY)
{
	if (maxX > 0 && mode->hdisplay > maxX)
		return MODE_VIRTUAL_X;

	if (maxY > 0 && mode->vdisplay > maxY)
		return MODE_VIRTUAL_Y;

	return MODE_OK;
}
EXPORT_SYMBOL(drm_mode_validate_size);

/**
 * drm_mode_validate_ycbcr420 - add 'ycbcr420-only' modes only when allowed
 * @mode: mode to check
 * @connector: drm connector under action
 *
 * This function is a helper which can be used to filter out any YCBCR420
 * only mode, when the source doesn't support it.
 *
 * Returns:
 * The mode status
 */
enum drm_mode_status
drm_mode_validate_ycbcr420(const struct drm_display_mode *mode,
			   struct drm_connector *connector)
{
	u8 vic = drm_match_cea_mode(mode);
	enum drm_mode_status status = MODE_OK;
	struct drm_hdmi_info *hdmi = &connector->display_info.hdmi;

	if (test_bit(vic, hdmi->y420_vdb_modes)) {
		if (!connector->ycbcr_420_allowed)
			status = MODE_NO_420;
	}

	return status;
}
EXPORT_SYMBOL(drm_mode_validate_ycbcr420);

#define MODE_STATUS(status) [MODE_ ## status + 3] = #status

static const char * const drm_mode_status_names[] = {
	MODE_STATUS(OK),
	MODE_STATUS(HSYNC),
	MODE_STATUS(VSYNC),
	MODE_STATUS(H_ILLEGAL),
	MODE_STATUS(V_ILLEGAL),
	MODE_STATUS(BAD_WIDTH),
	MODE_STATUS(NOMODE),
	MODE_STATUS(NO_INTERLACE),
	MODE_STATUS(NO_DBLESCAN),
	MODE_STATUS(NO_VSCAN),
	MODE_STATUS(MEM),
	MODE_STATUS(VIRTUAL_X),
	MODE_STATUS(VIRTUAL_Y),
	MODE_STATUS(MEM_VIRT),
	MODE_STATUS(NOCLOCK),
	MODE_STATUS(CLOCK_HIGH),
	MODE_STATUS(CLOCK_LOW),
	MODE_STATUS(CLOCK_RANGE),
	MODE_STATUS(BAD_HVALUE),
	MODE_STATUS(BAD_VVALUE),
	MODE_STATUS(BAD_VSCAN),
	MODE_STATUS(HSYNC_NARROW),
	MODE_STATUS(HSYNC_WIDE),
	MODE_STATUS(HBLANK_NARROW),
	MODE_STATUS(HBLANK_WIDE),
	MODE_STATUS(VSYNC_NARROW),
	MODE_STATUS(VSYNC_WIDE),
	MODE_STATUS(VBLANK_NARROW),
	MODE_STATUS(VBLANK_WIDE),
	MODE_STATUS(PANEL),
	MODE_STATUS(INTERLACE_WIDTH),
	MODE_STATUS(ONE_WIDTH),
	MODE_STATUS(ONE_HEIGHT),
	MODE_STATUS(ONE_SIZE),
	MODE_STATUS(NO_REDUCED),
	MODE_STATUS(NO_STEREO),
	MODE_STATUS(NO_420),
	MODE_STATUS(STALE),
	MODE_STATUS(BAD),
	MODE_STATUS(ERROR),
};

#undef MODE_STATUS

static const char *drm_get_mode_status_name(enum drm_mode_status status)
{
	int index = status + 3;

	if (WARN_ON(index < 0 || index >= ARRAY_SIZE(drm_mode_status_names)))
		return "";

	return drm_mode_status_names[index];
}

/**
 * drm_mode_prune_invalid - remove invalid modes from mode list
 * @dev: DRM device
 * @mode_list: list of modes to check
 * @verbose: be verbose about it
 *
 * This helper function can be used to prune a display mode list after
 * validation has been completed. All modes who's status is not MODE_OK will be
 * removed from the list, and if @verbose the status code and mode name is also
 * printed to dmesg.
 */
void drm_mode_prune_invalid(struct drm_device *dev,
			    struct list_head *mode_list, bool verbose)
{
	struct drm_display_mode *mode, *t;

	list_for_each_entry_safe(mode, t, mode_list, head) {
		if (mode->status != MODE_OK) {
			list_del(&mode->head);
			if (verbose) {
				drm_mode_debug_printmodeline(mode);
				DRM_DEBUG_KMS("Not using %s mode: %s\n",
					      mode->name,
					      drm_get_mode_status_name(mode->status));
			}
			drm_mode_destroy(dev, mode);
		}
	}
}
EXPORT_SYMBOL(drm_mode_prune_invalid);

/**
 * drm_mode_compare - compare modes for favorability
 * @priv: unused
 * @lh_a: list_head for first mode
 * @lh_b: list_head for second mode
 *
 * Compare two modes, given by @lh_a and @lh_b, returning a value indicating
 * which is better.
 *
 * Returns:
 * Negative if @lh_a is better than @lh_b, zero if they're equivalent, or
 * positive if @lh_b is better than @lh_a.
 */
static int drm_mode_compare(void *priv, struct list_head *lh_a, struct list_head *lh_b)
{
	struct drm_display_mode *a = list_entry(lh_a, struct drm_display_mode, head);
	struct drm_display_mode *b = list_entry(lh_b, struct drm_display_mode, head);
	int diff;

	diff = ((b->type & DRM_MODE_TYPE_PREFERRED) != 0) -
		((a->type & DRM_MODE_TYPE_PREFERRED) != 0);
	if (diff)
		return diff;
	diff = b->hdisplay * b->vdisplay - a->hdisplay * a->vdisplay;
	if (diff)
		return diff;

	diff = b->vrefresh - a->vrefresh;
	if (diff)
		return diff;

	diff = b->clock - a->clock;
	return diff;
}

/**
 * drm_mode_sort - sort mode list
 * @mode_list: list of drm_display_mode structures to sort
 *
 * Sort @mode_list by favorability, moving good modes to the head of the list.
 */
void drm_mode_sort(struct list_head *mode_list)
{
	list_sort(NULL, mode_list, drm_mode_compare);
}
EXPORT_SYMBOL(drm_mode_sort);

/**
 * drm_mode_connector_list_update - update the mode list for the connector
 * @connector: the connector to update
 *
 * This moves the modes from the @connector probed_modes list
 * to the actual mode list. It compares the probed mode against the current
 * list and only adds different/new modes.
 *
 * This is just a helper functions doesn't validate any modes itself and also
 * doesn't prune any invalid modes. Callers need to do that themselves.
 */
void drm_mode_connector_list_update(struct drm_connector *connector)
{
	struct drm_display_mode *pmode, *pt;

	WARN_ON(!mutex_is_locked(&connector->dev->mode_config.mutex));

	list_for_each_entry_safe(pmode, pt, &connector->probed_modes, head) {
		struct drm_display_mode *mode;
		bool found_it = false;

		/* go through current modes checking for the new probed mode */
		list_for_each_entry(mode, &connector->modes, head) {
			if (!drm_mode_equal(pmode, mode))
				continue;

			found_it = true;

			/*
			 * If the old matching mode is stale (ie. left over
			 * from a previous probe) just replace it outright.
			 * Otherwise just merge the type bits between all
			 * equal probed modes.
			 *
			 * If two probed modes are considered equal, pick the
			 * actual timings from the one that's marked as
			 * preferred (in case the match isn't 100%). If
			 * multiple or zero preferred modes are present, favor
			 * the mode added to the probed_modes list first.
			 */
			if (mode->status == MODE_STALE) {
				drm_mode_copy(mode, pmode);
			} else if ((mode->type & DRM_MODE_TYPE_PREFERRED) == 0 &&
				   (pmode->type & DRM_MODE_TYPE_PREFERRED) != 0) {
				pmode->type |= mode->type;
				drm_mode_copy(mode, pmode);
			} else {
				mode->type |= pmode->type;
			}

			list_del(&pmode->head);
			drm_mode_destroy(connector->dev, pmode);
			break;
		}

		if (!found_it) {
			list_move_tail(&pmode->head, &connector->modes);
		}
	}
}
EXPORT_SYMBOL(drm_mode_connector_list_update);

/**
 * drm_mode_parse_command_line_for_connector - parse command line modeline for connector
 * @mode_option: optional per connector mode option
 * @connector: connector to parse modeline for
 * @mode: preallocated drm_cmdline_mode structure to fill out
 *
 * This parses @mode_option command line modeline for modes and options to
 * configure the connector. If @mode_option is NULL the default command line
 * modeline in fb_mode_option will be parsed instead.
 *
 * This uses the same parameters as the fb modedb.c, except for an extra
 * force-enable, force-enable-digital and force-disable bit at the end::
 *
 *	<xres>x<yres>[M][R][-<bpp>][@<refresh>][i][m][eDd]
 *
 * The intermediate drm_cmdline_mode structure is required to store additional
 * options from the command line modline like the force-enable/disable flag.
 *
 * Returns:
 * True if a valid modeline has been parsed, false otherwise.
 */
bool drm_mode_parse_command_line_for_connector(const char *mode_option,
					       struct drm_connector *connector,
					       struct drm_cmdline_mode *mode)
{
	const char *name;
	unsigned int namelen;
	bool res_specified = false, bpp_specified = false, refresh_specified = false;
	unsigned int xres = 0, yres = 0, bpp = 32, refresh = 0;
	bool yres_specified = false, cvt = false, rb = false;
	bool interlace = false, margins = false, was_digit = false;
	int i;
	enum drm_connector_force force = DRM_FORCE_UNSPECIFIED;

#ifdef CONFIG_FB
	if (!mode_option)
		mode_option = fb_mode_option;
#endif

	if (!mode_option) {
		mode->specified = false;
		return false;
	}

	name = mode_option;
	namelen = strlen(name);
	for (i = namelen-1; i >= 0; i--) {
		switch (name[i]) {
		case '@':
			if (!refresh_specified && !bpp_specified &&
			    !yres_specified && !cvt && !rb && was_digit) {
				refresh = simple_strtol(&name[i+1], NULL, 10);
				refresh_specified = true;
				was_digit = false;
			} else
				goto done;
			break;
		case '-':
			if (!bpp_specified && !yres_specified && !cvt &&
			    !rb && was_digit) {
				bpp = simple_strtol(&name[i+1], NULL, 10);
				bpp_specified = true;
				was_digit = false;
			} else
				goto done;
			break;
		case 'x':
			if (!yres_specified && was_digit) {
				yres = simple_strtol(&name[i+1], NULL, 10);
				yres_specified = true;
				was_digit = false;
			} else
				goto done;
			break;
		case '0' ... '9':
			was_digit = true;
			break;
		case 'M':
			if (yres_specified || cvt || was_digit)
				goto done;
			cvt = true;
			break;
		case 'R':
			if (yres_specified || cvt || rb || was_digit)
				goto done;
			rb = true;
			break;
		case 'm':
			if (cvt || yres_specified || was_digit)
				goto done;
			margins = true;
			break;
		case 'i':
			if (cvt || yres_specified || was_digit)
				goto done;
			interlace = true;
			break;
		case 'e':
			if (yres_specified || bpp_specified || refresh_specified ||
			    was_digit || (force != DRM_FORCE_UNSPECIFIED))
				goto done;

			force = DRM_FORCE_ON;
			break;
		case 'D':
			if (yres_specified || bpp_specified || refresh_specified ||
			    was_digit || (force != DRM_FORCE_UNSPECIFIED))
				goto done;

			if ((connector->connector_type != DRM_MODE_CONNECTOR_DVII) &&
			    (connector->connector_type != DRM_MODE_CONNECTOR_HDMIB))
				force = DRM_FORCE_ON;
			else
				force = DRM_FORCE_ON_DIGITAL;
			break;
		case 'd':
			if (yres_specified || bpp_specified || refresh_specified ||
			    was_digit || (force != DRM_FORCE_UNSPECIFIED))
				goto done;

			force = DRM_FORCE_OFF;
			break;
		default:
			goto done;
		}
	}

	if (i < 0 && yres_specified) {
		char *ch;
		xres = simple_strtol(name, &ch, 10);
		if ((ch != NULL) && (*ch == 'x'))
			res_specified = true;
		else
			i = ch - name;
	} else if (!yres_specified && was_digit) {
		/* catch mode that begins with digits but has no 'x' */
		i = 0;
	}
done:
	if (i >= 0) {
		pr_warn("[drm] parse error at position %i in video mode '%s'\n",
			i, name);
		mode->specified = false;
		return false;
	}

	if (res_specified) {
		mode->specified = true;
		mode->xres = xres;
		mode->yres = yres;
	}

	if (refresh_specified) {
		mode->refresh_specified = true;
		mode->refresh = refresh;
	}

	if (bpp_specified) {
		mode->bpp_specified = true;
		mode->bpp = bpp;
	}
	mode->rb = rb;
	mode->cvt = cvt;
	mode->interlace = interlace;
	mode->margins = margins;
	mode->force = force;

	return true;
}
EXPORT_SYMBOL(drm_mode_parse_command_line_for_connector);

/**
 * drm_mode_create_from_cmdline_mode - convert a command line modeline into a DRM display mode
 * @dev: DRM device to create the new mode for
 * @cmd: input command line modeline
 *
 * Returns:
 * Pointer to converted mode on success, NULL on error.
 */
struct drm_display_mode *
drm_mode_create_from_cmdline_mode(struct drm_device *dev,
				  struct drm_cmdline_mode *cmd)
{
	struct drm_display_mode *mode;

	if (cmd->cvt)
		mode = drm_cvt_mode(dev,
				    cmd->xres, cmd->yres,
				    cmd->refresh_specified ? cmd->refresh : 60,
				    cmd->rb, cmd->interlace,
				    cmd->margins);
	else
		mode = drm_gtf_mode(dev,
				    cmd->xres, cmd->yres,
				    cmd->refresh_specified ? cmd->refresh : 60,
				    cmd->interlace,
				    cmd->margins);
	if (!mode)
		return NULL;

	mode->type |= DRM_MODE_TYPE_USERDEF;
	/* fix up 1368x768: GFT/CVT can't express 1366 width due to alignment */
	if (cmd->xres == 1366)
		drm_mode_fixup_1366x768(mode);
	drm_mode_set_crtcinfo(mode, CRTC_INTERLACE_HALVE_V);
	return mode;
}
EXPORT_SYMBOL(drm_mode_create_from_cmdline_mode);

/**
 * drm_crtc_convert_to_umode - convert a drm_display_mode into a modeinfo
 * @out: drm_mode_modeinfo struct to return to the user
 * @in: drm_display_mode to use
 *
 * Convert a drm_display_mode into a drm_mode_modeinfo structure to return to
 * the user.
 */
void drm_mode_convert_to_umode(struct drm_mode_modeinfo *out,
			       const struct drm_display_mode *in)
{
	WARN(in->hdisplay > USHRT_MAX || in->hsync_start > USHRT_MAX ||
	     in->hsync_end > USHRT_MAX || in->htotal > USHRT_MAX ||
	     in->hskew > USHRT_MAX || in->vdisplay > USHRT_MAX ||
	     in->vsync_start > USHRT_MAX || in->vsync_end > USHRT_MAX ||
	     in->vtotal > USHRT_MAX || in->vscan > USHRT_MAX,
	     "timing values too large for mode info\n");

	out->clock = in->clock;
	out->hdisplay = in->hdisplay;
	out->hsync_start = in->hsync_start;
	out->hsync_end = in->hsync_end;
	out->htotal = in->htotal;
	out->hskew = in->hskew;
	out->vdisplay = in->vdisplay;
	out->vsync_start = in->vsync_start;
	out->vsync_end = in->vsync_end;
	out->vtotal = in->vtotal;
	out->vscan = in->vscan;
	out->vrefresh = in->vrefresh;
	out->flags = in->flags;
	out->type = in->type;
	strncpy(out->name, in->name, DRM_DISPLAY_MODE_LEN);
	out->name[DRM_DISPLAY_MODE_LEN-1] = 0;
}

/**
 * drm_crtc_convert_umode - convert a modeinfo into a drm_display_mode
 * @dev: drm device
 * @out: drm_display_mode to return to the user
 * @in: drm_mode_modeinfo to use
 *
 * Convert a drm_mode_modeinfo into a drm_display_mode structure to return to
 * the caller.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_convert_umode(struct drm_device *dev,
			   struct drm_display_mode *out,
			   const struct drm_mode_modeinfo *in)
{
	if (in->clock > INT_MAX || in->vrefresh > INT_MAX)
		return -ERANGE;

	out->clock = in->clock;
	out->hdisplay = in->hdisplay;
	out->hsync_start = in->hsync_start;
	out->hsync_end = in->hsync_end;
	out->htotal = in->htotal;
	out->hskew = in->hskew;
	out->vdisplay = in->vdisplay;
	out->vsync_start = in->vsync_start;
	out->vsync_end = in->vsync_end;
	out->vtotal = in->vtotal;
	out->vscan = in->vscan;
	out->vrefresh = in->vrefresh;
	out->flags = in->flags;
	/*
	 * Old xf86-video-vmware (possibly others too) used to
	 * leave 'type' unititialized. Just ignore any bits we
	 * don't like. It's a just hint after all, and more
	 * useful for the kernel->userspace direction anyway.
	 */
	out->type = in->type & DRM_MODE_TYPE_ALL;
	strncpy(out->name, in->name, DRM_DISPLAY_MODE_LEN);
	out->name[DRM_DISPLAY_MODE_LEN-1] = 0;

	out->status = drm_mode_validate_driver(dev, out);
	if (out->status != MODE_OK)
		return -EINVAL;

	drm_mode_set_crtcinfo(out, CRTC_INTERLACE_HALVE_V);

	return 0;
}

/**
 * drm_mode_is_420_only - if a given videomode can be only supported in YCBCR420
 * output format
 *
 * @display: display under action
 * @mode: video mode to be tested.
 *
 * Returns:
 * true if the mode can be supported in YCBCR420 format
 * false if not.
 */
bool drm_mode_is_420_only(const struct drm_display_info *display,
			  const struct drm_display_mode *mode)
{
	u8 vic = drm_match_cea_mode(mode);

	return test_bit(vic, display->hdmi.y420_vdb_modes);
}
EXPORT_SYMBOL(drm_mode_is_420_only);

/**
 * drm_mode_is_420_also - if a given videomode can be supported in YCBCR420
 * output format also (along with RGB/YCBCR444/422)
 *
 * @display: display under action.
 * @mode: video mode to be tested.
 *
 * Returns:
 * true if the mode can be support YCBCR420 format
 * false if not.
 */
bool drm_mode_is_420_also(const struct drm_display_info *display,
			  const struct drm_display_mode *mode)
{
	u8 vic = drm_match_cea_mode(mode);

	return test_bit(vic, display->hdmi.y420_cmdb_modes);
}
EXPORT_SYMBOL(drm_mode_is_420_also);
/**
 * drm_mode_is_420 - if a given videomode can be supported in YCBCR420
 * output format
 *
 * @display: display under action.
 * @mode: video mode to be tested.
 *
 * Returns:
 * true if the mode can be supported in YCBCR420 format
 * false if not.
 */
bool drm_mode_is_420(const struct drm_display_info *display,
		     const struct drm_display_mode *mode)
{
	return drm_mode_is_420_only(display, mode) ||
		drm_mode_is_420_also(display, mode);
}
EXPORT_SYMBOL(drm_mode_is_420);
