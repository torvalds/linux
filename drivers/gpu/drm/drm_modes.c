/*
 * The list_sort function is (presumably) licensed under the GPL (see the
 * top level "COPYING" file for details).
 *
 * The remainder of this file is:
 *
 * Copyright © 1997-2003 by The XFree86 Project, Inc.
 * Copyright © 2007 Dave Airlie
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright 2005-2006 Luc Verhaegen
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
#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"

#define DRM_MODESET_DEBUG	"drm_mode"
/**
 * drm_mode_debug_printmodeline - debug print a mode
 * @dev: DRM device
 * @mode: mode to print
 *
 * LOCKING:
 * None.
 *
 * Describe @mode using DRM_DEBUG.
 */
void drm_mode_debug_printmodeline(struct drm_display_mode *mode)
{
	DRM_DEBUG_MODE(DRM_MODESET_DEBUG,
		"Modeline %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x\n",
		mode->base.id, mode->name, mode->vrefresh, mode->clock,
		mode->hdisplay, mode->hsync_start,
		mode->hsync_end, mode->htotal,
		mode->vdisplay, mode->vsync_start,
		mode->vsync_end, mode->vtotal, mode->type, mode->flags);
}
EXPORT_SYMBOL(drm_mode_debug_printmodeline);

/**
 * drm_cvt_mode -create a modeline based on CVT algorithm
 * @dev: DRM device
 * @hdisplay: hdisplay size
 * @vdisplay: vdisplay size
 * @vrefresh  : vrefresh rate
 * @reduced : Whether the GTF calculation is simplified
 * @interlaced:Whether the interlace is supported
 *
 * LOCKING:
 * none.
 *
 * return the modeline based on CVT algorithm
 *
 * This function is called to generate the modeline based on CVT algorithm
 * according to the hdisplay, vdisplay, vrefresh.
 * It is based from the VESA(TM) Coordinated Video Timing Generator by
 * Graham Loveridge April 9, 2003 available at
 * http://www.vesa.org/public/CVT/CVTd6r1.xls
 *
 * And it is copied from xf86CVTmode in xserver/hw/xfree86/modes/xf86cvt.c.
 * What I have done is to translate it by using integer calculation.
 */
#define HV_FACTOR			1000
struct drm_display_mode *drm_cvt_mode(struct drm_device *dev, int hdisplay,
				      int vdisplay, int vrefresh,
				      bool reduced, bool interlaced)
{
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
	bool margins = false;
	unsigned int vfieldrate, hperiod;
	int hdisplay_rnd, hmargin, vdisplay_rnd, vmargin, vsync;
	int interlace;

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

	drm_mode->vdisplay = vdisplay_rnd + 2 * vmargin;

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
		/* 14. find the total pixes per line */
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
		drm_mode->hsync_start = drm_mode->hsync_end = CVT_RB_H_SYNC;
	}
	/* 15/13. Find pixel clock frequency (kHz for xf86) */
	drm_mode->clock = drm_mode->htotal * HV_FACTOR * 1000 / hperiod;
	drm_mode->clock -= drm_mode->clock % CVT_CLOCK_STEP;
	/* 18/16. Find actual vertical frame frequency */
	/* ignore - just set the mode flag for interlaced */
	if (interlaced)
		drm_mode->vtotal *= 2;
	/* Fill the mode line name */
	drm_mode_set_name(drm_mode);
	if (reduced)
		drm_mode->flags |= (DRM_MODE_FLAG_PHSYNC |
					DRM_MODE_FLAG_NVSYNC);
	else
		drm_mode->flags |= (DRM_MODE_FLAG_PVSYNC |
					DRM_MODE_FLAG_NHSYNC);
	if (interlaced)
		drm_mode->flags |= DRM_MODE_FLAG_INTERLACE;

    return drm_mode;
}
EXPORT_SYMBOL(drm_cvt_mode);

/**
 * drm_mode_set_name - set the name on a mode
 * @mode: name will be set in this mode
 *
 * LOCKING:
 * None.
 *
 * Set the name of @mode to a standard format.
 */
void drm_mode_set_name(struct drm_display_mode *mode)
{
	snprintf(mode->name, DRM_DISPLAY_MODE_LEN, "%dx%d", mode->hdisplay,
		 mode->vdisplay);
}
EXPORT_SYMBOL(drm_mode_set_name);

/**
 * drm_mode_list_concat - move modes from one list to another
 * @head: source list
 * @new: dst list
 *
 * LOCKING:
 * Caller must ensure both lists are locked.
 *
 * Move all the modes from @head to @new.
 */
void drm_mode_list_concat(struct list_head *head, struct list_head *new)
{

	struct list_head *entry, *tmp;

	list_for_each_safe(entry, tmp, head) {
		list_move_tail(entry, new);
	}
}
EXPORT_SYMBOL(drm_mode_list_concat);

/**
 * drm_mode_width - get the width of a mode
 * @mode: mode
 *
 * LOCKING:
 * None.
 *
 * Return @mode's width (hdisplay) value.
 *
 * FIXME: is this needed?
 *
 * RETURNS:
 * @mode->hdisplay
 */
int drm_mode_width(struct drm_display_mode *mode)
{
	return mode->hdisplay;

}
EXPORT_SYMBOL(drm_mode_width);

/**
 * drm_mode_height - get the height of a mode
 * @mode: mode
 *
 * LOCKING:
 * None.
 *
 * Return @mode's height (vdisplay) value.
 *
 * FIXME: is this needed?
 *
 * RETURNS:
 * @mode->vdisplay
 */
int drm_mode_height(struct drm_display_mode *mode)
{
	return mode->vdisplay;
}
EXPORT_SYMBOL(drm_mode_height);

/**
 * drm_mode_vrefresh - get the vrefresh of a mode
 * @mode: mode
 *
 * LOCKING:
 * None.
 *
 * Return @mode's vrefresh rate or calculate it if necessary.
 *
 * FIXME: why is this needed?  shouldn't vrefresh be set already?
 *
 * RETURNS:
 * Vertical refresh rate of @mode x 1000. For precision reasons.
 */
int drm_mode_vrefresh(struct drm_display_mode *mode)
{
	int refresh = 0;
	unsigned int calc_val;

	if (mode->vrefresh > 0)
		refresh = mode->vrefresh;
	else if (mode->htotal > 0 && mode->vtotal > 0) {
		/* work out vrefresh the value will be x1000 */
		calc_val = (mode->clock * 1000);

		calc_val /= mode->htotal;
		calc_val *= 1000;
		calc_val /= mode->vtotal;

		refresh = calc_val;
		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			refresh *= 2;
		if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
			refresh /= 2;
		if (mode->vscan > 1)
			refresh /= mode->vscan;
	}
	return refresh;
}
EXPORT_SYMBOL(drm_mode_vrefresh);

/**
 * drm_mode_set_crtcinfo - set CRTC modesetting parameters
 * @p: mode
 * @adjust_flags: unused? (FIXME)
 *
 * LOCKING:
 * None.
 *
 * Setup the CRTC modesetting parameters for @p, adjusting if necessary.
 */
void drm_mode_set_crtcinfo(struct drm_display_mode *p, int adjust_flags)
{
	if ((p == NULL) || ((p->type & DRM_MODE_TYPE_CRTC_C) == DRM_MODE_TYPE_BUILTIN))
		return;

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

		p->crtc_vtotal |= 1;
	}

	if (p->flags & DRM_MODE_FLAG_DBLSCAN) {
		p->crtc_vdisplay *= 2;
		p->crtc_vsync_start *= 2;
		p->crtc_vsync_end *= 2;
		p->crtc_vtotal *= 2;
	}

	if (p->vscan > 1) {
		p->crtc_vdisplay *= p->vscan;
		p->crtc_vsync_start *= p->vscan;
		p->crtc_vsync_end *= p->vscan;
		p->crtc_vtotal *= p->vscan;
	}

	p->crtc_vblank_start = min(p->crtc_vsync_start, p->crtc_vdisplay);
	p->crtc_vblank_end = max(p->crtc_vsync_end, p->crtc_vtotal);
	p->crtc_hblank_start = min(p->crtc_hsync_start, p->crtc_hdisplay);
	p->crtc_hblank_end = max(p->crtc_hsync_end, p->crtc_htotal);

	p->crtc_hadjusted = false;
	p->crtc_vadjusted = false;
}
EXPORT_SYMBOL(drm_mode_set_crtcinfo);


/**
 * drm_mode_duplicate - allocate and duplicate an existing mode
 * @m: mode to duplicate
 *
 * LOCKING:
 * None.
 *
 * Just allocate a new mode, copy the existing mode into it, and return
 * a pointer to it.  Used to create new instances of established modes.
 */
struct drm_display_mode *drm_mode_duplicate(struct drm_device *dev,
					    struct drm_display_mode *mode)
{
	struct drm_display_mode *nmode;
	int new_id;

	nmode = drm_mode_create(dev);
	if (!nmode)
		return NULL;

	new_id = nmode->base.id;
	*nmode = *mode;
	nmode->base.id = new_id;
	INIT_LIST_HEAD(&nmode->head);
	return nmode;
}
EXPORT_SYMBOL(drm_mode_duplicate);

/**
 * drm_mode_equal - test modes for equality
 * @mode1: first mode
 * @mode2: second mode
 *
 * LOCKING:
 * None.
 *
 * Check to see if @mode1 and @mode2 are equivalent.
 *
 * RETURNS:
 * True if the modes are equal, false otherwise.
 */
bool drm_mode_equal(struct drm_display_mode *mode1, struct drm_display_mode *mode2)
{
	/* do clock check convert to PICOS so fb modes get matched
	 * the same */
	if (mode1->clock && mode2->clock) {
		if (KHZ2PICOS(mode1->clock) != KHZ2PICOS(mode2->clock))
			return false;
	} else if (mode1->clock != mode2->clock)
		return false;

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
	    mode1->flags == mode2->flags)
		return true;

	return false;
}
EXPORT_SYMBOL(drm_mode_equal);

/**
 * drm_mode_validate_size - make sure modes adhere to size constraints
 * @dev: DRM device
 * @mode_list: list of modes to check
 * @maxX: maximum width
 * @maxY: maximum height
 * @maxPitch: max pitch
 *
 * LOCKING:
 * Caller must hold a lock protecting @mode_list.
 *
 * The DRM device (@dev) has size and pitch limits.  Here we validate the
 * modes we probed for @dev against those limits and set their status as
 * necessary.
 */
void drm_mode_validate_size(struct drm_device *dev,
			    struct list_head *mode_list,
			    int maxX, int maxY, int maxPitch)
{
	struct drm_display_mode *mode;

	list_for_each_entry(mode, mode_list, head) {
		if (maxPitch > 0 && mode->hdisplay > maxPitch)
			mode->status = MODE_BAD_WIDTH;

		if (maxX > 0 && mode->hdisplay > maxX)
			mode->status = MODE_VIRTUAL_X;

		if (maxY > 0 && mode->vdisplay > maxY)
			mode->status = MODE_VIRTUAL_Y;
	}
}
EXPORT_SYMBOL(drm_mode_validate_size);

/**
 * drm_mode_validate_clocks - validate modes against clock limits
 * @dev: DRM device
 * @mode_list: list of modes to check
 * @min: minimum clock rate array
 * @max: maximum clock rate array
 * @n_ranges: number of clock ranges (size of arrays)
 *
 * LOCKING:
 * Caller must hold a lock protecting @mode_list.
 *
 * Some code may need to check a mode list against the clock limits of the
 * device in question.  This function walks the mode list, testing to make
 * sure each mode falls within a given range (defined by @min and @max
 * arrays) and sets @mode->status as needed.
 */
void drm_mode_validate_clocks(struct drm_device *dev,
			      struct list_head *mode_list,
			      int *min, int *max, int n_ranges)
{
	struct drm_display_mode *mode;
	int i;

	list_for_each_entry(mode, mode_list, head) {
		bool good = false;
		for (i = 0; i < n_ranges; i++) {
			if (mode->clock >= min[i] && mode->clock <= max[i]) {
				good = true;
				break;
			}
		}
		if (!good)
			mode->status = MODE_CLOCK_RANGE;
	}
}
EXPORT_SYMBOL(drm_mode_validate_clocks);

/**
 * drm_mode_prune_invalid - remove invalid modes from mode list
 * @dev: DRM device
 * @mode_list: list of modes to check
 * @verbose: be verbose about it
 *
 * LOCKING:
 * Caller must hold a lock protecting @mode_list.
 *
 * Once mode list generation is complete, a caller can use this routine to
 * remove invalid modes from a mode list.  If any of the modes have a
 * status other than %MODE_OK, they are removed from @mode_list and freed.
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
				DRM_DEBUG_MODE(DRM_MODESET_DEBUG,
					"Not using %s mode %d\n",
					mode->name, mode->status);
			}
			drm_mode_destroy(dev, mode);
		}
	}
}
EXPORT_SYMBOL(drm_mode_prune_invalid);

/**
 * drm_mode_compare - compare modes for favorability
 * @lh_a: list_head for first mode
 * @lh_b: list_head for second mode
 *
 * LOCKING:
 * None.
 *
 * Compare two modes, given by @lh_a and @lh_b, returning a value indicating
 * which is better.
 *
 * RETURNS:
 * Negative if @lh_a is better than @lh_b, zero if they're equivalent, or
 * positive if @lh_b is better than @lh_a.
 */
static int drm_mode_compare(struct list_head *lh_a, struct list_head *lh_b)
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
	diff = b->clock - a->clock;
	return diff;
}

/* FIXME: what we don't have a list sort function? */
/* list sort from Mark J Roberts (mjr@znex.org) */
void list_sort(struct list_head *head,
	       int (*cmp)(struct list_head *a, struct list_head *b))
{
	struct list_head *p, *q, *e, *list, *tail, *oldhead;
	int insize, nmerges, psize, qsize, i;

	list = head->next;
	list_del(head);
	insize = 1;
	for (;;) {
		p = oldhead = list;
		list = tail = NULL;
		nmerges = 0;

		while (p) {
			nmerges++;
			q = p;
			psize = 0;
			for (i = 0; i < insize; i++) {
				psize++;
				q = q->next == oldhead ? NULL : q->next;
				if (!q)
					break;
			}

			qsize = insize;
			while (psize > 0 || (qsize > 0 && q)) {
				if (!psize) {
					e = q;
					q = q->next;
					qsize--;
					if (q == oldhead)
						q = NULL;
				} else if (!qsize || !q) {
					e = p;
					p = p->next;
					psize--;
					if (p == oldhead)
						p = NULL;
				} else if (cmp(p, q) <= 0) {
					e = p;
					p = p->next;
					psize--;
					if (p == oldhead)
						p = NULL;
				} else {
					e = q;
					q = q->next;
					qsize--;
					if (q == oldhead)
						q = NULL;
				}
				if (tail)
					tail->next = e;
				else
					list = e;
				e->prev = tail;
				tail = e;
			}
			p = q;
		}

		tail->next = list;
		list->prev = tail;

		if (nmerges <= 1)
			break;

		insize *= 2;
	}

	head->next = list;
	head->prev = list->prev;
	list->prev->next = head;
	list->prev = head;
}

/**
 * drm_mode_sort - sort mode list
 * @mode_list: list to sort
 *
 * LOCKING:
 * Caller must hold a lock protecting @mode_list.
 *
 * Sort @mode_list by favorability, putting good modes first.
 */
void drm_mode_sort(struct list_head *mode_list)
{
	list_sort(mode_list, drm_mode_compare);
}
EXPORT_SYMBOL(drm_mode_sort);

/**
 * drm_mode_connector_list_update - update the mode list for the connector
 * @connector: the connector to update
 *
 * LOCKING:
 * Caller must hold a lock protecting @mode_list.
 *
 * This moves the modes from the @connector probed_modes list
 * to the actual mode list. It compares the probed mode against the current
 * list and only adds different modes. All modes unverified after this point
 * will be removed by the prune invalid modes.
 */
void drm_mode_connector_list_update(struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *pmode, *pt;
	int found_it;

	list_for_each_entry_safe(pmode, pt, &connector->probed_modes,
				 head) {
		found_it = 0;
		/* go through current modes checking for the new probed mode */
		list_for_each_entry(mode, &connector->modes, head) {
			if (drm_mode_equal(pmode, mode)) {
				found_it = 1;
				/* if equal delete the probed mode */
				mode->status = pmode->status;
				list_del(&pmode->head);
				drm_mode_destroy(connector->dev, pmode);
				break;
			}
		}

		if (!found_it) {
			list_move_tail(&pmode->head, &connector->modes);
		}
	}
}
EXPORT_SYMBOL(drm_mode_connector_list_update);
