/*
 * linux/drivers/video/fbcvt.c - VESA(TM) Coordinated Video Timings
 *
 * Copyright (C) 2005 Antonino Daplas <adaplas@pol.net>
 *
 *      Based from the VESA(TM) Coordinated Video Timing Generator by
 *      Graham Loveridge April 9, 2003 available at
 *      http://www.elo.utfsm.cl/~elo212/docs/CVTd6r1.xls
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */
#include <linux/fb.h>
#include <linux/slab.h>

#define FB_CVT_CELLSIZE               8
#define FB_CVT_GTF_C                 40
#define FB_CVT_GTF_J                 20
#define FB_CVT_GTF_K                128
#define FB_CVT_GTF_M                600
#define FB_CVT_MIN_VSYNC_BP         550
#define FB_CVT_MIN_VPORCH             3
#define FB_CVT_MIN_BPORCH             6

#define FB_CVT_RB_MIN_VBLANK        460
#define FB_CVT_RB_HBLANK            160
#define FB_CVT_RB_V_FPORCH            3

#define FB_CVT_FLAG_REDUCED_BLANK 1
#define FB_CVT_FLAG_MARGINS       2
#define FB_CVT_FLAG_INTERLACED    4

struct fb_cvt_data {
	u32 xres;
	u32 yres;
	u32 refresh;
	u32 f_refresh;
	u32 pixclock;
	u32 hperiod;
	u32 hblank;
	u32 hfreq;
	u32 htotal;
	u32 vtotal;
	u32 vsync;
	u32 hsync;
	u32 h_front_porch;
	u32 h_back_porch;
	u32 v_front_porch;
	u32 v_back_porch;
	u32 h_margin;
	u32 v_margin;
	u32 interlace;
	u32 aspect_ratio;
	u32 active_pixels;
	u32 flags;
	u32 status;
};

static const unsigned char fb_cvt_vbi_tab[] = {
	4,        /* 4:3      */
	5,        /* 16:9     */
	6,        /* 16:10    */
	7,        /* 5:4      */
	7,        /* 15:9     */
	8,        /* reserved */
	9,        /* reserved */
	10        /* custom   */
};

/* returns hperiod * 1000 */
static u32 fb_cvt_hperiod(struct fb_cvt_data *cvt)
{
	u32 num = 1000000000/cvt->f_refresh;
	u32 den;

	if (cvt->flags & FB_CVT_FLAG_REDUCED_BLANK) {
		num -= FB_CVT_RB_MIN_VBLANK * 1000;
		den = 2 * (cvt->yres/cvt->interlace + 2 * cvt->v_margin);
	} else {
		num -= FB_CVT_MIN_VSYNC_BP * 1000;
		den = 2 * (cvt->yres/cvt->interlace + cvt->v_margin * 2
			   + FB_CVT_MIN_VPORCH + cvt->interlace/2);
	}

	return 2 * (num/den);
}

/* returns ideal duty cycle * 1000 */
static u32 fb_cvt_ideal_duty_cycle(struct fb_cvt_data *cvt)
{
	u32 c_prime = (FB_CVT_GTF_C - FB_CVT_GTF_J) *
		(FB_CVT_GTF_K) + 256 * FB_CVT_GTF_J;
	u32 m_prime = (FB_CVT_GTF_K * FB_CVT_GTF_M);
	u32 h_period_est = cvt->hperiod;

	return (1000 * c_prime  - ((m_prime * h_period_est)/1000))/256;
}

static u32 fb_cvt_hblank(struct fb_cvt_data *cvt)
{
	u32 hblank = 0;

	if (cvt->flags & FB_CVT_FLAG_REDUCED_BLANK)
		hblank = FB_CVT_RB_HBLANK;
	else {
		u32 ideal_duty_cycle = fb_cvt_ideal_duty_cycle(cvt);
		u32 active_pixels = cvt->active_pixels;

		if (ideal_duty_cycle < 20000)
			hblank = (active_pixels * 20000)/
				(100000 - 20000);
		else {
			hblank = (active_pixels * ideal_duty_cycle)/
				(100000 - ideal_duty_cycle);
		}
	}

	hblank &= ~((2 * FB_CVT_CELLSIZE) - 1);

	return hblank;
}

static u32 fb_cvt_hsync(struct fb_cvt_data *cvt)
{
	u32 hsync;

	if (cvt->flags & FB_CVT_FLAG_REDUCED_BLANK)
		hsync = 32;
	else
		hsync = (FB_CVT_CELLSIZE * cvt->htotal)/100;

	hsync &= ~(FB_CVT_CELLSIZE - 1);
	return hsync;
}

static u32 fb_cvt_vbi_lines(struct fb_cvt_data *cvt)
{
	u32 vbi_lines, min_vbi_lines, act_vbi_lines;

	if (cvt->flags & FB_CVT_FLAG_REDUCED_BLANK) {
		vbi_lines = (1000 * FB_CVT_RB_MIN_VBLANK)/cvt->hperiod + 1;
		min_vbi_lines =  FB_CVT_RB_V_FPORCH + cvt->vsync +
			FB_CVT_MIN_BPORCH;

	} else {
		vbi_lines = (FB_CVT_MIN_VSYNC_BP * 1000)/cvt->hperiod + 1 +
			 FB_CVT_MIN_VPORCH;
		min_vbi_lines = cvt->vsync + FB_CVT_MIN_BPORCH +
			FB_CVT_MIN_VPORCH;
	}

	if (vbi_lines < min_vbi_lines)
		act_vbi_lines = min_vbi_lines;
	else
		act_vbi_lines = vbi_lines;

	return act_vbi_lines;
}

static u32 fb_cvt_vtotal(struct fb_cvt_data *cvt)
{
	u32 vtotal = cvt->yres/cvt->interlace;

	vtotal += 2 * cvt->v_margin + cvt->interlace/2 + fb_cvt_vbi_lines(cvt);
	vtotal |= cvt->interlace/2;

	return vtotal;
}

static u32 fb_cvt_pixclock(struct fb_cvt_data *cvt)
{
	u32 pixclock;

	if (cvt->flags & FB_CVT_FLAG_REDUCED_BLANK)
		pixclock = (cvt->f_refresh * cvt->vtotal * cvt->htotal)/1000;
	else
		pixclock = (cvt->htotal * 1000000)/cvt->hperiod;

	pixclock /= 250;
	pixclock *= 250;
	pixclock *= 1000;

	return pixclock;
}

static u32 fb_cvt_aspect_ratio(struct fb_cvt_data *cvt)
{
	u32 xres = cvt->xres;
	u32 yres = cvt->yres;
	u32 aspect = -1;

	if (xres == (yres * 4)/3 && !((yres * 4) % 3))
		aspect = 0;
	else if (xres == (yres * 16)/9 && !((yres * 16) % 9))
		aspect = 1;
	else if (xres == (yres * 16)/10 && !((yres * 16) % 10))
		aspect = 2;
	else if (xres == (yres * 5)/4 && !((yres * 5) % 4))
		aspect = 3;
	else if (xres == (yres * 15)/9 && !((yres * 15) % 9))
		aspect = 4;
	else {
		printk(KERN_INFO "fbcvt: Aspect ratio not CVT "
		       "standard\n");
		aspect = 7;
		cvt->status = 1;
	}

	return aspect;
}

static void fb_cvt_print_name(struct fb_cvt_data *cvt)
{
	u32 pixcount, pixcount_mod;
	int cnt = 255, offset = 0, read = 0;
	u8 *buf = kzalloc(256, GFP_KERNEL);

	if (!buf)
		return;

	pixcount = (cvt->xres * (cvt->yres/cvt->interlace))/1000000;
	pixcount_mod = (cvt->xres * (cvt->yres/cvt->interlace)) % 1000000;
	pixcount_mod /= 1000;

	read = snprintf(buf+offset, cnt, "fbcvt: %dx%d@%d: CVT Name - ",
			cvt->xres, cvt->yres, cvt->refresh);
	offset += read;
	cnt -= read;

	if (cvt->status)
		snprintf(buf+offset, cnt, "Not a CVT standard - %d.%03d Mega "
			 "Pixel Image\n", pixcount, pixcount_mod);
	else {
		if (pixcount) {
			read = snprintf(buf+offset, cnt, "%d", pixcount);
			cnt -= read;
			offset += read;
		}

		read = snprintf(buf+offset, cnt, ".%03dM", pixcount_mod);
		cnt -= read;
		offset += read;

		if (cvt->aspect_ratio == 0)
			read = snprintf(buf+offset, cnt, "3");
		else if (cvt->aspect_ratio == 3)
			read = snprintf(buf+offset, cnt, "4");
		else if (cvt->aspect_ratio == 1 || cvt->aspect_ratio == 4)
			read = snprintf(buf+offset, cnt, "9");
		else if (cvt->aspect_ratio == 2)
			read = snprintf(buf+offset, cnt, "A");
		else
			read = 0;
		cnt -= read;
		offset += read;

		if (cvt->flags & FB_CVT_FLAG_REDUCED_BLANK) {
			read = snprintf(buf+offset, cnt, "-R");
			cnt -= read;
			offset += read;
		}
	}

	printk(KERN_INFO "%s\n", buf);
	kfree(buf);
}

static void fb_cvt_convert_to_mode(struct fb_cvt_data *cvt,
				   struct fb_videomode *mode)
{
	mode->refresh = cvt->f_refresh;
	mode->pixclock = KHZ2PICOS(cvt->pixclock/1000);
	mode->left_margin = cvt->h_back_porch;
	mode->right_margin = cvt->h_front_porch;
	mode->hsync_len = cvt->hsync;
	mode->upper_margin = cvt->v_back_porch;
	mode->lower_margin = cvt->v_front_porch;
	mode->vsync_len = cvt->vsync;

	mode->sync &= ~(FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT);

	if (cvt->flags & FB_CVT_FLAG_REDUCED_BLANK)
		mode->sync |= FB_SYNC_HOR_HIGH_ACT;
	else
		mode->sync |= FB_SYNC_VERT_HIGH_ACT;
}

/*
 * fb_find_mode_cvt - calculate mode using VESA(TM) CVT
 * @mode: pointer to fb_videomode; xres, yres, refresh and vmode must be
 *        pre-filled with the desired values
 * @margins: add margin to calculation (1.8% of xres and yres)
 * @rb: compute with reduced blanking (for flatpanels)
 *
 * RETURNS:
 * 0 for success
 * @mode is filled with computed values.  If interlaced, the refresh field
 * will be filled with the field rate (2x the frame rate)
 *
 * DESCRIPTION:
 * Computes video timings using VESA(TM) Coordinated Video Timings
 */
int fb_find_mode_cvt(struct fb_videomode *mode, int margins, int rb)
{
	struct fb_cvt_data cvt;

	memset(&cvt, 0, sizeof(cvt));

	if (margins)
	    cvt.flags |= FB_CVT_FLAG_MARGINS;

	if (rb)
	    cvt.flags |= FB_CVT_FLAG_REDUCED_BLANK;

	if (mode->vmode & FB_VMODE_INTERLACED)
	    cvt.flags |= FB_CVT_FLAG_INTERLACED;

	cvt.xres = mode->xres;
	cvt.yres = mode->yres;
	cvt.refresh = mode->refresh;
	cvt.f_refresh = cvt.refresh;
	cvt.interlace = 1;

	if (!cvt.xres || !cvt.yres || !cvt.refresh) {
		printk(KERN_INFO "fbcvt: Invalid input parameters\n");
		return 1;
	}

	if (!(cvt.refresh == 50 || cvt.refresh == 60 || cvt.refresh == 70 ||
	      cvt.refresh == 85)) {
		printk(KERN_INFO "fbcvt: Refresh rate not CVT "
		       "standard\n");
		cvt.status = 1;
	}

	cvt.xres &= ~(FB_CVT_CELLSIZE - 1);

	if (cvt.flags & FB_CVT_FLAG_INTERLACED) {
		cvt.interlace = 2;
		cvt.f_refresh *= 2;
	}

	if (cvt.flags & FB_CVT_FLAG_REDUCED_BLANK) {
		if (cvt.refresh != 60) {
			printk(KERN_INFO "fbcvt: 60Hz refresh rate "
			       "advised for reduced blanking\n");
			cvt.status = 1;
		}
	}

	if (cvt.flags & FB_CVT_FLAG_MARGINS) {
		cvt.h_margin = (cvt.xres * 18)/1000;
		cvt.h_margin &= ~(FB_CVT_CELLSIZE - 1);
		cvt.v_margin = ((cvt.yres/cvt.interlace)* 18)/1000;
	}

	cvt.aspect_ratio = fb_cvt_aspect_ratio(&cvt);
	cvt.active_pixels = cvt.xres + 2 * cvt.h_margin;
	cvt.hperiod = fb_cvt_hperiod(&cvt);
	cvt.vsync = fb_cvt_vbi_tab[cvt.aspect_ratio];
	cvt.vtotal = fb_cvt_vtotal(&cvt);
	cvt.hblank = fb_cvt_hblank(&cvt);
	cvt.htotal = cvt.active_pixels + cvt.hblank;
	cvt.hsync = fb_cvt_hsync(&cvt);
	cvt.pixclock = fb_cvt_pixclock(&cvt);
	cvt.hfreq = cvt.pixclock/cvt.htotal;
	cvt.h_back_porch = cvt.hblank/2 + cvt.h_margin;
	cvt.h_front_porch = cvt.hblank - cvt.hsync - cvt.h_back_porch +
		2 * cvt.h_margin;
	cvt.v_front_porch = 3 + cvt.v_margin;
	cvt.v_back_porch = cvt.vtotal - cvt.yres/cvt.interlace -
	    cvt.v_front_porch - cvt.vsync;
	fb_cvt_print_name(&cvt);
	fb_cvt_convert_to_mode(&cvt, mode);

	return 0;
}
