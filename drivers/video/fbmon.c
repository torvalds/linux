/*
 * linux/drivers/video/fbmon.c
 *
 * Copyright (C) 2002 James Simmons <jsimmons@users.sf.net>
 *
 * Credits:
 * 
 * The EDID Parser is a conglomeration from the following sources:
 *
 *   1. SciTech SNAP Graphics Architecture
 *      Copyright (C) 1991-2002 SciTech Software, Inc. All rights reserved.
 *
 *   2. XFree86 4.3.0, interpret_edid.c
 *      Copyright 1998 by Egbert Eich <Egbert.Eich@Physik.TU-Darmstadt.DE>
 * 
 *   3. John Fremlin <vii@users.sourceforge.net> and 
 *      Ani Joshi <ajoshi@unixbox.com>
 *  
 * Generalized Timing Formula is derived from:
 *
 *      GTF Spreadsheet by Andy Morrish (1/5/97) 
 *      available at http://www.vesa.org
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <video/edid.h>
#ifdef CONFIG_PPC_OF
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#endif
#include "edid.h"

/* 
 * EDID parser
 */

#undef DEBUG  /* define this for verbose EDID parsing output */

#ifdef DEBUG
#define DPRINTK(fmt, args...) printk(fmt,## args)
#else
#define DPRINTK(fmt, args...)
#endif

#define FBMON_FIX_HEADER  1
#define FBMON_FIX_INPUT   2
#define FBMON_FIX_TIMINGS 3

#ifdef CONFIG_FB_MODE_HELPERS
struct broken_edid {
	u8  manufacturer[4];
	u32 model;
	u32 fix;
};

static const struct broken_edid brokendb[] = {
	/* DEC FR-PCXAV-YZ */
	{
		.manufacturer = "DEC",
		.model        = 0x073a,
		.fix          = FBMON_FIX_HEADER,
	},
	/* ViewSonic PF775a */
	{
		.manufacturer = "VSC",
		.model        = 0x5a44,
		.fix          = FBMON_FIX_INPUT,
	},
	/* Sharp UXGA? */
	{
		.manufacturer = "SHP",
		.model        = 0x138e,
		.fix          = FBMON_FIX_TIMINGS,
	},
};

static const unsigned char edid_v1_header[] = { 0x00, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0x00
};

static void copy_string(unsigned char *c, unsigned char *s)
{
  int i;
  c = c + 5;
  for (i = 0; (i < 13 && *c != 0x0A); i++)
    *(s++) = *(c++);
  *s = 0;
  while (i-- && (*--s == 0x20)) *s = 0;
}

static int edid_is_serial_block(unsigned char *block)
{
	if ((block[0] == 0x00) && (block[1] == 0x00) &&
	    (block[2] == 0x00) && (block[3] == 0xff) &&
	    (block[4] == 0x00))
		return 1;
	else
		return 0;
}

static int edid_is_ascii_block(unsigned char *block)
{
	if ((block[0] == 0x00) && (block[1] == 0x00) &&
	    (block[2] == 0x00) && (block[3] == 0xfe) &&
	    (block[4] == 0x00))
		return 1;
	else
		return 0;
}

static int edid_is_limits_block(unsigned char *block)
{
	if ((block[0] == 0x00) && (block[1] == 0x00) &&
	    (block[2] == 0x00) && (block[3] == 0xfd) &&
	    (block[4] == 0x00))
		return 1;
	else
		return 0;
}

static int edid_is_monitor_block(unsigned char *block)
{
	if ((block[0] == 0x00) && (block[1] == 0x00) &&
	    (block[2] == 0x00) && (block[3] == 0xfc) &&
	    (block[4] == 0x00))
		return 1;
	else
		return 0;
}

static int edid_is_timing_block(unsigned char *block)
{
	if ((block[0] != 0x00) || (block[1] != 0x00) ||
	    (block[2] != 0x00) || (block[4] != 0x00))
		return 1;
	else
		return 0;
}

static int check_edid(unsigned char *edid)
{
	unsigned char *block = edid + ID_MANUFACTURER_NAME, manufacturer[4];
	unsigned char *b;
	u32 model;
	int i, fix = 0, ret = 0;

	manufacturer[0] = ((block[0] & 0x7c) >> 2) + '@';
	manufacturer[1] = ((block[0] & 0x03) << 3) +
		((block[1] & 0xe0) >> 5) + '@';
	manufacturer[2] = (block[1] & 0x1f) + '@';
	manufacturer[3] = 0;
	model = block[2] + (block[3] << 8);

	for (i = 0; i < ARRAY_SIZE(brokendb); i++) {
		if (!strncmp(manufacturer, brokendb[i].manufacturer, 4) &&
			brokendb[i].model == model) {
 			fix = brokendb[i].fix;
 			break;
		}
	}

	switch (fix) {
	case FBMON_FIX_HEADER:
		for (i = 0; i < 8; i++) {
			if (edid[i] != edid_v1_header[i]) {
				ret = fix;
				break;
			}
		}
		break;
	case FBMON_FIX_INPUT:
		b = edid + EDID_STRUCT_DISPLAY;
		/* Only if display is GTF capable will
		   the input type be reset to analog */
		if (b[4] & 0x01 && b[0] & 0x80)
			ret = fix;
		break;
	case FBMON_FIX_TIMINGS:
		b = edid + DETAILED_TIMING_DESCRIPTIONS_START;
		ret = fix;

		for (i = 0; i < 4; i++) {
			if (edid_is_limits_block(b)) {
				ret = 0;
				break;
			}

			b += DETAILED_TIMING_DESCRIPTION_SIZE;
		}

		break;
	}

	if (ret)
		printk("fbmon: The EDID Block of "
		       "Manufacturer: %s Model: 0x%x is known to "
		       "be broken,\n",  manufacturer, model);

	return ret;
}

static void fix_edid(unsigned char *edid, int fix)
{
	int i;
	unsigned char *b, csum = 0;

	switch (fix) {
	case FBMON_FIX_HEADER:
		printk("fbmon: trying a header reconstruct\n");
		memcpy(edid, edid_v1_header, 8);
		break;
	case FBMON_FIX_INPUT:
		printk("fbmon: trying to fix input type\n");
		b = edid + EDID_STRUCT_DISPLAY;
		b[0] &= ~0x80;
		edid[127] += 0x80;
		break;
	case FBMON_FIX_TIMINGS:
		printk("fbmon: trying to fix monitor timings\n");
		b = edid + DETAILED_TIMING_DESCRIPTIONS_START;
		for (i = 0; i < 4; i++) {
			if (!(edid_is_serial_block(b) ||
			      edid_is_ascii_block(b) ||
			      edid_is_monitor_block(b) ||
			      edid_is_timing_block(b))) {
				b[0] = 0x00;
				b[1] = 0x00;
				b[2] = 0x00;
				b[3] = 0xfd;
				b[4] = 0x00;
				b[5] = 60;   /* vfmin */
				b[6] = 60;   /* vfmax */
				b[7] = 30;   /* hfmin */
				b[8] = 75;   /* hfmax */
				b[9] = 17;   /* pixclock - 170 MHz*/
				b[10] = 0;   /* GTF */
				break;
			}

			b += DETAILED_TIMING_DESCRIPTION_SIZE;
		}

		for (i = 0; i < EDID_LENGTH - 1; i++)
			csum += edid[i];

		edid[127] = 256 - csum;
		break;
	}
}

static int edid_checksum(unsigned char *edid)
{
	unsigned char i, csum = 0, all_null = 0;
	int err = 0, fix = check_edid(edid);

	if (fix)
		fix_edid(edid, fix);

	for (i = 0; i < EDID_LENGTH; i++) {
		csum += edid[i];
		all_null |= edid[i];
	}

	if (csum == 0x00 && all_null) {
		/* checksum passed, everything's good */
		err = 1;
	}

	return err;
}

static int edid_check_header(unsigned char *edid)
{
	int i, err = 1, fix = check_edid(edid);

	if (fix)
		fix_edid(edid, fix);

	for (i = 0; i < 8; i++) {
		if (edid[i] != edid_v1_header[i])
			err = 0;
	}

	return err;
}

static void parse_vendor_block(unsigned char *block, struct fb_monspecs *specs)
{
	specs->manufacturer[0] = ((block[0] & 0x7c) >> 2) + '@';
	specs->manufacturer[1] = ((block[0] & 0x03) << 3) +
		((block[1] & 0xe0) >> 5) + '@';
	specs->manufacturer[2] = (block[1] & 0x1f) + '@';
	specs->manufacturer[3] = 0;
	specs->model = block[2] + (block[3] << 8);
	specs->serial = block[4] + (block[5] << 8) +
	       (block[6] << 16) + (block[7] << 24);
	specs->year = block[9] + 1990;
	specs->week = block[8];
	DPRINTK("   Manufacturer: %s\n", specs->manufacturer);
	DPRINTK("   Model: %x\n", specs->model);
	DPRINTK("   Serial#: %u\n", specs->serial);
	DPRINTK("   Year: %u Week %u\n", specs->year, specs->week);
}

static void get_dpms_capabilities(unsigned char flags,
				  struct fb_monspecs *specs)
{
	specs->dpms = 0;
	if (flags & DPMS_ACTIVE_OFF)
		specs->dpms |= FB_DPMS_ACTIVE_OFF;
	if (flags & DPMS_SUSPEND)
		specs->dpms |= FB_DPMS_SUSPEND;
	if (flags & DPMS_STANDBY)
		specs->dpms |= FB_DPMS_STANDBY;
	DPRINTK("      DPMS: Active %s, Suspend %s, Standby %s\n",
	       (flags & DPMS_ACTIVE_OFF) ? "yes" : "no",
	       (flags & DPMS_SUSPEND)    ? "yes" : "no",
	       (flags & DPMS_STANDBY)    ? "yes" : "no");
}
	
static void get_chroma(unsigned char *block, struct fb_monspecs *specs)
{
	int tmp;

	DPRINTK("      Chroma\n");
	/* Chromaticity data */
	tmp = ((block[5] & (3 << 6)) >> 6) | (block[0x7] << 2);
	tmp *= 1000;
	tmp += 512;
	specs->chroma.redx = tmp/1024;
	DPRINTK("         RedX:     0.%03d ", specs->chroma.redx);

	tmp = ((block[5] & (3 << 4)) >> 4) | (block[0x8] << 2);
	tmp *= 1000;
	tmp += 512;
	specs->chroma.redy = tmp/1024;
	DPRINTK("RedY:     0.%03d\n", specs->chroma.redy);

	tmp = ((block[5] & (3 << 2)) >> 2) | (block[0x9] << 2);
	tmp *= 1000;
	tmp += 512;
	specs->chroma.greenx = tmp/1024;
	DPRINTK("         GreenX:   0.%03d ", specs->chroma.greenx);

	tmp = (block[5] & 3) | (block[0xa] << 2);
	tmp *= 1000;
	tmp += 512;
	specs->chroma.greeny = tmp/1024;
	DPRINTK("GreenY:   0.%03d\n", specs->chroma.greeny);

	tmp = ((block[6] & (3 << 6)) >> 6) | (block[0xb] << 2);
	tmp *= 1000;
	tmp += 512;
	specs->chroma.bluex = tmp/1024;
	DPRINTK("         BlueX:    0.%03d ", specs->chroma.bluex);

	tmp = ((block[6] & (3 << 4)) >> 4) | (block[0xc] << 2);
	tmp *= 1000;
	tmp += 512;
	specs->chroma.bluey = tmp/1024;
	DPRINTK("BlueY:    0.%03d\n", specs->chroma.bluey);
	
	tmp = ((block[6] & (3 << 2)) >> 2) | (block[0xd] << 2);
	tmp *= 1000;
	tmp += 512;
	specs->chroma.whitex = tmp/1024;
	DPRINTK("         WhiteX:   0.%03d ", specs->chroma.whitex);

	tmp = (block[6] & 3) | (block[0xe] << 2);
	tmp *= 1000;
	tmp += 512;
	specs->chroma.whitey = tmp/1024;
	DPRINTK("WhiteY:   0.%03d\n", specs->chroma.whitey);
}

static void calc_mode_timings(int xres, int yres, int refresh,
			      struct fb_videomode *mode)
{
	struct fb_var_screeninfo *var;
	
	var = kzalloc(sizeof(struct fb_var_screeninfo), GFP_KERNEL);

	if (var) {
		var->xres = xres;
		var->yres = yres;
		fb_get_mode(FB_VSYNCTIMINGS | FB_IGNOREMON,
			    refresh, var, NULL);
		mode->xres = xres;
		mode->yres = yres;
		mode->pixclock = var->pixclock;
		mode->refresh = refresh;
		mode->left_margin = var->left_margin;
		mode->right_margin = var->right_margin;
		mode->upper_margin = var->upper_margin;
		mode->lower_margin = var->lower_margin;
		mode->hsync_len = var->hsync_len;
		mode->vsync_len = var->vsync_len;
		mode->vmode = 0;
		mode->sync = 0;
		kfree(var);
	}
}

static int get_est_timing(unsigned char *block, struct fb_videomode *mode)
{
	int num = 0;
	unsigned char c;

	c = block[0];
	if (c&0x80) {
		calc_mode_timings(720, 400, 70, &mode[num]);
		mode[num++].flag = FB_MODE_IS_CALCULATED;
		DPRINTK("      720x400@70Hz\n");
	}
	if (c&0x40) {
		calc_mode_timings(720, 400, 88, &mode[num]);
		mode[num++].flag = FB_MODE_IS_CALCULATED;
		DPRINTK("      720x400@88Hz\n");
	}
	if (c&0x20) {
		mode[num++] = vesa_modes[3];
		DPRINTK("      640x480@60Hz\n");
	}
	if (c&0x10) {
		calc_mode_timings(640, 480, 67, &mode[num]);
		mode[num++].flag = FB_MODE_IS_CALCULATED;
		DPRINTK("      640x480@67Hz\n");
	}
	if (c&0x08) {
		mode[num++] = vesa_modes[4];
		DPRINTK("      640x480@72Hz\n");
	}
	if (c&0x04) {
		mode[num++] = vesa_modes[5];
		DPRINTK("      640x480@75Hz\n");
	}
	if (c&0x02) {
		mode[num++] = vesa_modes[7];
		DPRINTK("      800x600@56Hz\n");
	}
	if (c&0x01) {
		mode[num++] = vesa_modes[8];
		DPRINTK("      800x600@60Hz\n");
	}

	c = block[1];
	if (c&0x80) {
 		mode[num++] = vesa_modes[9];
		DPRINTK("      800x600@72Hz\n");
	}
	if (c&0x40) {
 		mode[num++] = vesa_modes[10];
		DPRINTK("      800x600@75Hz\n");
	}
	if (c&0x20) {
		calc_mode_timings(832, 624, 75, &mode[num]);
		mode[num++].flag = FB_MODE_IS_CALCULATED;
		DPRINTK("      832x624@75Hz\n");
	}
	if (c&0x10) {
		mode[num++] = vesa_modes[12];
		DPRINTK("      1024x768@87Hz Interlaced\n");
	}
	if (c&0x08) {
		mode[num++] = vesa_modes[13];
		DPRINTK("      1024x768@60Hz\n");
	}
	if (c&0x04) {
		mode[num++] = vesa_modes[14];
		DPRINTK("      1024x768@70Hz\n");
	}
	if (c&0x02) {
		mode[num++] = vesa_modes[15];
		DPRINTK("      1024x768@75Hz\n");
	}
	if (c&0x01) {
		mode[num++] = vesa_modes[21];
		DPRINTK("      1280x1024@75Hz\n");
	}
	c = block[2];
	if (c&0x80) {
		mode[num++] = vesa_modes[17];
		DPRINTK("      1152x870@75Hz\n");
	}
	DPRINTK("      Manufacturer's mask: %x\n",c&0x7F);
	return num;
}

static int get_std_timing(unsigned char *block, struct fb_videomode *mode)
{
	int xres, yres = 0, refresh, ratio, i;
	
	xres = (block[0] + 31) * 8;
	if (xres <= 256)
		return 0;

	ratio = (block[1] & 0xc0) >> 6;
	switch (ratio) {
	case 0:
		yres = xres;
		break;
	case 1:
		yres = (xres * 3)/4;
		break;
	case 2:
		yres = (xres * 4)/5;
		break;
	case 3:
		yres = (xres * 9)/16;
		break;
	}
	refresh = (block[1] & 0x3f) + 60;

	DPRINTK("      %dx%d@%dHz\n", xres, yres, refresh);
	for (i = 0; i < VESA_MODEDB_SIZE; i++) {
		if (vesa_modes[i].xres == xres && 
		    vesa_modes[i].yres == yres &&
		    vesa_modes[i].refresh == refresh) {
			*mode = vesa_modes[i];
			mode->flag |= FB_MODE_IS_STANDARD;
			return 1;
		}
	}
	calc_mode_timings(xres, yres, refresh, mode);
	return 1;
}

static int get_dst_timing(unsigned char *block,
			  struct fb_videomode *mode)
{
	int j, num = 0;

	for (j = 0; j < 6; j++, block+= STD_TIMING_DESCRIPTION_SIZE) 
		num += get_std_timing(block, &mode[num]);

	return num;
}

static void get_detailed_timing(unsigned char *block, 
				struct fb_videomode *mode)
{
	mode->xres = H_ACTIVE;
	mode->yres = V_ACTIVE;
	mode->pixclock = PIXEL_CLOCK;
	mode->pixclock /= 1000;
	mode->pixclock = KHZ2PICOS(mode->pixclock);
	mode->right_margin = H_SYNC_OFFSET;
	mode->left_margin = (H_ACTIVE + H_BLANKING) -
		(H_ACTIVE + H_SYNC_OFFSET + H_SYNC_WIDTH);
	mode->upper_margin = V_BLANKING - V_SYNC_OFFSET - 
		V_SYNC_WIDTH;
	mode->lower_margin = V_SYNC_OFFSET;
	mode->hsync_len = H_SYNC_WIDTH;
	mode->vsync_len = V_SYNC_WIDTH;
	if (HSYNC_POSITIVE)
		mode->sync |= FB_SYNC_HOR_HIGH_ACT;
	if (VSYNC_POSITIVE)
		mode->sync |= FB_SYNC_VERT_HIGH_ACT;
	mode->refresh = PIXEL_CLOCK/((H_ACTIVE + H_BLANKING) *
				     (V_ACTIVE + V_BLANKING));
	mode->vmode = 0;
	mode->flag = FB_MODE_IS_DETAILED;

	DPRINTK("      %d MHz ",  PIXEL_CLOCK/1000000);
	DPRINTK("%d %d %d %d ", H_ACTIVE, H_ACTIVE + H_SYNC_OFFSET,
	       H_ACTIVE + H_SYNC_OFFSET + H_SYNC_WIDTH, H_ACTIVE + H_BLANKING);
	DPRINTK("%d %d %d %d ", V_ACTIVE, V_ACTIVE + V_SYNC_OFFSET,
	       V_ACTIVE + V_SYNC_OFFSET + V_SYNC_WIDTH, V_ACTIVE + V_BLANKING);
	DPRINTK("%sHSync %sVSync\n\n", (HSYNC_POSITIVE) ? "+" : "-",
	       (VSYNC_POSITIVE) ? "+" : "-");
}

/**
 * fb_create_modedb - create video mode database
 * @edid: EDID data
 * @dbsize: database size
 *
 * RETURNS: struct fb_videomode, @dbsize contains length of database
 *
 * DESCRIPTION:
 * This function builds a mode database using the contents of the EDID
 * data
 */
static struct fb_videomode *fb_create_modedb(unsigned char *edid, int *dbsize)
{
	struct fb_videomode *mode, *m;
	unsigned char *block;
	int num = 0, i, first = 1;

	mode = kzalloc(50 * sizeof(struct fb_videomode), GFP_KERNEL);
	if (mode == NULL)
		return NULL;

	if (edid == NULL || !edid_checksum(edid) || 
	    !edid_check_header(edid)) {
		kfree(mode);
		return NULL;
	}

	*dbsize = 0;

	DPRINTK("   Detailed Timings\n");
	block = edid + DETAILED_TIMING_DESCRIPTIONS_START;
	for (i = 0; i < 4; i++, block+= DETAILED_TIMING_DESCRIPTION_SIZE) {
		if (!(block[0] == 0x00 && block[1] == 0x00)) {
			get_detailed_timing(block, &mode[num]);
			if (first) {
			        mode[num].flag |= FB_MODE_IS_FIRST;
				first = 0;
			}
			num++;
		}
	}

	DPRINTK("   Supported VESA Modes\n");
	block = edid + ESTABLISHED_TIMING_1;
	num += get_est_timing(block, &mode[num]);

	DPRINTK("   Standard Timings\n");
	block = edid + STD_TIMING_DESCRIPTIONS_START;
	for (i = 0; i < STD_TIMING; i++, block += STD_TIMING_DESCRIPTION_SIZE)
		num += get_std_timing(block, &mode[num]);

	block = edid + DETAILED_TIMING_DESCRIPTIONS_START;
	for (i = 0; i < 4; i++, block+= DETAILED_TIMING_DESCRIPTION_SIZE) {
		if (block[0] == 0x00 && block[1] == 0x00 && block[3] == 0xfa)
			num += get_dst_timing(block + 5, &mode[num]);
	}
	
	/* Yikes, EDID data is totally useless */
	if (!num) {
		kfree(mode);
		return NULL;
	}

	*dbsize = num;
	m = kmalloc(num * sizeof(struct fb_videomode), GFP_KERNEL);
	if (!m)
		return mode;
	memmove(m, mode, num * sizeof(struct fb_videomode));
	kfree(mode);
	return m;
}

/**
 * fb_destroy_modedb - destroys mode database
 * @modedb: mode database to destroy
 *
 * DESCRIPTION:
 * Destroy mode database created by fb_create_modedb
 */
void fb_destroy_modedb(struct fb_videomode *modedb)
{
	kfree(modedb);
}

static int fb_get_monitor_limits(unsigned char *edid, struct fb_monspecs *specs)
{
	int i, retval = 1;
	unsigned char *block;

	block = edid + DETAILED_TIMING_DESCRIPTIONS_START;

	DPRINTK("      Monitor Operating Limits: ");

	for (i = 0; i < 4; i++, block += DETAILED_TIMING_DESCRIPTION_SIZE) {
		if (edid_is_limits_block(block)) {
			specs->hfmin = H_MIN_RATE * 1000;
			specs->hfmax = H_MAX_RATE * 1000;
			specs->vfmin = V_MIN_RATE;
			specs->vfmax = V_MAX_RATE;
			specs->dclkmax = MAX_PIXEL_CLOCK * 1000000;
			specs->gtf = (GTF_SUPPORT) ? 1 : 0;
			retval = 0;
			DPRINTK("From EDID\n");
			break;
		}
	}

	/* estimate monitor limits based on modes supported */
	if (retval) {
		struct fb_videomode *modes, *mode;
		int num_modes, hz, hscan, pixclock;
		int vtotal, htotal;

		modes = fb_create_modedb(edid, &num_modes);
		if (!modes) {
			DPRINTK("None Available\n");
			return 1;
		}

		retval = 0;
		for (i = 0; i < num_modes; i++) {
			mode = &modes[i];
			pixclock = PICOS2KHZ(modes[i].pixclock) * 1000;
			htotal = mode->xres + mode->right_margin + mode->hsync_len
				+ mode->left_margin;
			vtotal = mode->yres + mode->lower_margin + mode->vsync_len
				+ mode->upper_margin;

			if (mode->vmode & FB_VMODE_INTERLACED)
				vtotal /= 2;

			if (mode->vmode & FB_VMODE_DOUBLE)
				vtotal *= 2;

			hscan = (pixclock + htotal / 2) / htotal;
			hscan = (hscan + 500) / 1000 * 1000;
			hz = (hscan + vtotal / 2) / vtotal;
			
			if (specs->dclkmax == 0 || specs->dclkmax < pixclock)
				specs->dclkmax = pixclock;

			if (specs->dclkmin == 0 || specs->dclkmin > pixclock)
				specs->dclkmin = pixclock;

			if (specs->hfmax == 0 || specs->hfmax < hscan)
				specs->hfmax = hscan;

			if (specs->hfmin == 0 || specs->hfmin > hscan)
				specs->hfmin = hscan;

			if (specs->vfmax == 0 || specs->vfmax < hz)
				specs->vfmax = hz;

			if (specs->vfmin == 0 || specs->vfmin > hz)
				specs->vfmin = hz;
		}
		DPRINTK("Extrapolated\n");
		fb_destroy_modedb(modes);
	}
	DPRINTK("           H: %d-%dKHz V: %d-%dHz DCLK: %dMHz\n",
		specs->hfmin/1000, specs->hfmax/1000, specs->vfmin,
		specs->vfmax, specs->dclkmax/1000000);
	return retval;
}

static void get_monspecs(unsigned char *edid, struct fb_monspecs *specs)
{
	unsigned char c, *block;

	block = edid + EDID_STRUCT_DISPLAY;

	fb_get_monitor_limits(edid, specs);

	c = block[0] & 0x80;
	specs->input = 0;
	if (c) {
		specs->input |= FB_DISP_DDI;
		DPRINTK("      Digital Display Input");
	} else {
		DPRINTK("      Analog Display Input: Input Voltage - ");
		switch ((block[0] & 0x60) >> 5) {
		case 0:
			DPRINTK("0.700V/0.300V");
			specs->input |= FB_DISP_ANA_700_300;
			break;
		case 1:
			DPRINTK("0.714V/0.286V");
			specs->input |= FB_DISP_ANA_714_286;
			break;
		case 2:
			DPRINTK("1.000V/0.400V");
			specs->input |= FB_DISP_ANA_1000_400;
			break;
		case 3:
			DPRINTK("0.700V/0.000V");
			specs->input |= FB_DISP_ANA_700_000;
			break;
		}
	}
	DPRINTK("\n      Sync: ");
	c = block[0] & 0x10;
	if (c)
		DPRINTK("      Configurable signal level\n");
	c = block[0] & 0x0f;
	specs->signal = 0;
	if (c & 0x10) {
		DPRINTK("Blank to Blank ");
		specs->signal |= FB_SIGNAL_BLANK_BLANK;
	}
	if (c & 0x08) {
		DPRINTK("Separate ");
		specs->signal |= FB_SIGNAL_SEPARATE;
	}
	if (c & 0x04) {
		DPRINTK("Composite ");
		specs->signal |= FB_SIGNAL_COMPOSITE;
	}
	if (c & 0x02) {
		DPRINTK("Sync on Green ");
		specs->signal |= FB_SIGNAL_SYNC_ON_GREEN;
	}
	if (c & 0x01) {
		DPRINTK("Serration on ");
		specs->signal |= FB_SIGNAL_SERRATION_ON;
	}
	DPRINTK("\n");
	specs->max_x = block[1];
	specs->max_y = block[2];
	DPRINTK("      Max H-size in cm: ");
	if (specs->max_x)
		DPRINTK("%d\n", specs->max_x);
	else
		DPRINTK("variable\n");
	DPRINTK("      Max V-size in cm: ");
	if (specs->max_y)
		DPRINTK("%d\n", specs->max_y);
	else
		DPRINTK("variable\n");

	c = block[3];
	specs->gamma = c+100;
	DPRINTK("      Gamma: ");
	DPRINTK("%d.%d\n", specs->gamma/100, specs->gamma % 100);

	get_dpms_capabilities(block[4], specs);

	switch ((block[4] & 0x18) >> 3) {
	case 0:
		DPRINTK("      Monochrome/Grayscale\n");
		specs->input |= FB_DISP_MONO;
		break;
	case 1:
		DPRINTK("      RGB Color Display\n");
		specs->input |= FB_DISP_RGB;
		break;
	case 2:
		DPRINTK("      Non-RGB Multicolor Display\n");
		specs->input |= FB_DISP_MULTI;
		break;
	default:
		DPRINTK("      Unknown\n");
		specs->input |= FB_DISP_UNKNOWN;
		break;
	}

	get_chroma(block, specs);

	specs->misc = 0;
	c = block[4] & 0x7;
	if (c & 0x04) {
		DPRINTK("      Default color format is primary\n");
		specs->misc |= FB_MISC_PRIM_COLOR;
	}
	if (c & 0x02) {
		DPRINTK("      First DETAILED Timing is preferred\n");
		specs->misc |= FB_MISC_1ST_DETAIL;
	}
	if (c & 0x01) {
		printk("      Display is GTF capable\n");
		specs->gtf = 1;
	}
}

int fb_parse_edid(unsigned char *edid, struct fb_var_screeninfo *var)
{
	int i;
	unsigned char *block;

	if (edid == NULL || var == NULL)
		return 1;

	if (!(edid_checksum(edid)))
		return 1;

	if (!(edid_check_header(edid)))
		return 1;

	block = edid + DETAILED_TIMING_DESCRIPTIONS_START;

	for (i = 0; i < 4; i++, block += DETAILED_TIMING_DESCRIPTION_SIZE) {
		if (edid_is_timing_block(block)) {
			var->xres = var->xres_virtual = H_ACTIVE;
			var->yres = var->yres_virtual = V_ACTIVE;
			var->height = var->width = -1;
			var->right_margin = H_SYNC_OFFSET;
			var->left_margin = (H_ACTIVE + H_BLANKING) -
				(H_ACTIVE + H_SYNC_OFFSET + H_SYNC_WIDTH);
			var->upper_margin = V_BLANKING - V_SYNC_OFFSET -
				V_SYNC_WIDTH;
			var->lower_margin = V_SYNC_OFFSET;
			var->hsync_len = H_SYNC_WIDTH;
			var->vsync_len = V_SYNC_WIDTH;
			var->pixclock = PIXEL_CLOCK;
			var->pixclock /= 1000;
			var->pixclock = KHZ2PICOS(var->pixclock);

			if (HSYNC_POSITIVE)
				var->sync |= FB_SYNC_HOR_HIGH_ACT;
			if (VSYNC_POSITIVE)
				var->sync |= FB_SYNC_VERT_HIGH_ACT;
			return 0;
		}
	}
	return 1;
}

void fb_edid_to_monspecs(unsigned char *edid, struct fb_monspecs *specs)
{
	unsigned char *block;
	int i, found = 0;

	if (edid == NULL)
		return;

	if (!(edid_checksum(edid)))
		return;

	if (!(edid_check_header(edid)))
		return;

	memset(specs, 0, sizeof(struct fb_monspecs));

	specs->version = edid[EDID_STRUCT_VERSION];
	specs->revision = edid[EDID_STRUCT_REVISION];

	DPRINTK("========================================\n");
	DPRINTK("Display Information (EDID)\n");
	DPRINTK("========================================\n");
	DPRINTK("   EDID Version %d.%d\n", (int) specs->version,
	       (int) specs->revision);

	parse_vendor_block(edid + ID_MANUFACTURER_NAME, specs);

	block = edid + DETAILED_TIMING_DESCRIPTIONS_START;
	for (i = 0; i < 4; i++, block += DETAILED_TIMING_DESCRIPTION_SIZE) {
		if (edid_is_serial_block(block)) {
			copy_string(block, specs->serial_no);
			DPRINTK("   Serial Number: %s\n", specs->serial_no);
		} else if (edid_is_ascii_block(block)) {
			copy_string(block, specs->ascii);
			DPRINTK("   ASCII Block: %s\n", specs->ascii);
		} else if (edid_is_monitor_block(block)) {
			copy_string(block, specs->monitor);
			DPRINTK("   Monitor Name: %s\n", specs->monitor);
		}
	}

	DPRINTK("   Display Characteristics:\n");
	get_monspecs(edid, specs);

	specs->modedb = fb_create_modedb(edid, &specs->modedb_len);

	/*
	 * Workaround for buggy EDIDs that sets that the first
	 * detailed timing is preferred but has not detailed
	 * timing specified
	 */
	for (i = 0; i < specs->modedb_len; i++) {
		if (specs->modedb[i].flag & FB_MODE_IS_DETAILED) {
			found = 1;
			break;
		}
	}

	if (!found)
		specs->misc &= ~FB_MISC_1ST_DETAIL;

	DPRINTK("========================================\n");
}

/* 
 * VESA Generalized Timing Formula (GTF) 
 */

#define FLYBACK                     550
#define V_FRONTPORCH                1
#define H_OFFSET                    40
#define H_SCALEFACTOR               20
#define H_BLANKSCALE                128
#define H_GRADIENT                  600
#define C_VAL                       30
#define M_VAL                       300

struct __fb_timings {
	u32 dclk;
	u32 hfreq;
	u32 vfreq;
	u32 hactive;
	u32 vactive;
	u32 hblank;
	u32 vblank;
	u32 htotal;
	u32 vtotal;
};

/**
 * fb_get_vblank - get vertical blank time
 * @hfreq: horizontal freq
 *
 * DESCRIPTION:
 * vblank = right_margin + vsync_len + left_margin 
 *
 *    given: right_margin = 1 (V_FRONTPORCH)
 *           vsync_len    = 3
 *           flyback      = 550
 *
 *                          flyback * hfreq
 *           left_margin  = --------------- - vsync_len
 *                           1000000
 */
static u32 fb_get_vblank(u32 hfreq)
{
	u32 vblank;

	vblank = (hfreq * FLYBACK)/1000; 
	vblank = (vblank + 500)/1000;
	return (vblank + V_FRONTPORCH);
}

/** 
 * fb_get_hblank_by_freq - get horizontal blank time given hfreq
 * @hfreq: horizontal freq
 * @xres: horizontal resolution in pixels
 *
 * DESCRIPTION:
 *
 *           xres * duty_cycle
 * hblank = ------------------
 *           100 - duty_cycle
 *
 * duty cycle = percent of htotal assigned to inactive display
 * duty cycle = C - (M/Hfreq)
 *
 * where: C = ((offset - scale factor) * blank_scale)
 *            -------------------------------------- + scale factor
 *                        256 
 *        M = blank_scale * gradient
 *
 */
static u32 fb_get_hblank_by_hfreq(u32 hfreq, u32 xres)
{
	u32 c_val, m_val, duty_cycle, hblank;

	c_val = (((H_OFFSET - H_SCALEFACTOR) * H_BLANKSCALE)/256 + 
		 H_SCALEFACTOR) * 1000;
	m_val = (H_BLANKSCALE * H_GRADIENT)/256;
	m_val = (m_val * 1000000)/hfreq;
	duty_cycle = c_val - m_val;
	hblank = (xres * duty_cycle)/(100000 - duty_cycle);
	return (hblank);
}

/** 
 * fb_get_hblank_by_dclk - get horizontal blank time given pixelclock
 * @dclk: pixelclock in Hz
 * @xres: horizontal resolution in pixels
 *
 * DESCRIPTION:
 *
 *           xres * duty_cycle
 * hblank = ------------------
 *           100 - duty_cycle
 *
 * duty cycle = percent of htotal assigned to inactive display
 * duty cycle = C - (M * h_period)
 * 
 * where: h_period = SQRT(100 - C + (0.4 * xres * M)/dclk) + C - 100
 *                   -----------------------------------------------
 *                                    2 * M
 *        M = 300;
 *        C = 30;

 */
static u32 fb_get_hblank_by_dclk(u32 dclk, u32 xres)
{
	u32 duty_cycle, h_period, hblank;

	dclk /= 1000;
	h_period = 100 - C_VAL;
	h_period *= h_period;
	h_period += (M_VAL * xres * 2 * 1000)/(5 * dclk);
	h_period *=10000; 

	h_period = int_sqrt(h_period);
	h_period -= (100 - C_VAL) * 100;
	h_period *= 1000; 
	h_period /= 2 * M_VAL;

	duty_cycle = C_VAL * 1000 - (M_VAL * h_period)/100;
	hblank = (xres * duty_cycle)/(100000 - duty_cycle) + 8;
	hblank &= ~15;
	return (hblank);
}
	
/**
 * fb_get_hfreq - estimate hsync
 * @vfreq: vertical refresh rate
 * @yres: vertical resolution
 *
 * DESCRIPTION:
 *
 *          (yres + front_port) * vfreq * 1000000
 * hfreq = -------------------------------------
 *          (1000000 - (vfreq * FLYBACK)
 * 
 */

static u32 fb_get_hfreq(u32 vfreq, u32 yres)
{
	u32 divisor, hfreq;
	
	divisor = (1000000 - (vfreq * FLYBACK))/1000;
	hfreq = (yres + V_FRONTPORCH) * vfreq  * 1000;
	return (hfreq/divisor);
}

static void fb_timings_vfreq(struct __fb_timings *timings)
{
	timings->hfreq = fb_get_hfreq(timings->vfreq, timings->vactive);
	timings->vblank = fb_get_vblank(timings->hfreq);
	timings->vtotal = timings->vactive + timings->vblank;
	timings->hblank = fb_get_hblank_by_hfreq(timings->hfreq, 
						 timings->hactive);
	timings->htotal = timings->hactive + timings->hblank;
	timings->dclk = timings->htotal * timings->hfreq;
}

static void fb_timings_hfreq(struct __fb_timings *timings)
{
	timings->vblank = fb_get_vblank(timings->hfreq);
	timings->vtotal = timings->vactive + timings->vblank;
	timings->vfreq = timings->hfreq/timings->vtotal;
	timings->hblank = fb_get_hblank_by_hfreq(timings->hfreq, 
						 timings->hactive);
	timings->htotal = timings->hactive + timings->hblank;
	timings->dclk = timings->htotal * timings->hfreq;
}

static void fb_timings_dclk(struct __fb_timings *timings)
{
	timings->hblank = fb_get_hblank_by_dclk(timings->dclk, 
						timings->hactive);
	timings->htotal = timings->hactive + timings->hblank;
	timings->hfreq = timings->dclk/timings->htotal;
	timings->vblank = fb_get_vblank(timings->hfreq);
	timings->vtotal = timings->vactive + timings->vblank;
	timings->vfreq = timings->hfreq/timings->vtotal;
}

/*
 * fb_get_mode - calculates video mode using VESA GTF
 * @flags: if: 0 - maximize vertical refresh rate
 *             1 - vrefresh-driven calculation;
 *             2 - hscan-driven calculation;
 *             3 - pixelclock-driven calculation;
 * @val: depending on @flags, ignored, vrefresh, hsync or pixelclock
 * @var: pointer to fb_var_screeninfo
 * @info: pointer to fb_info
 *
 * DESCRIPTION:
 * Calculates video mode based on monitor specs using VESA GTF. 
 * The GTF is best for VESA GTF compliant monitors but is 
 * specifically formulated to work for older monitors as well.
 *
 * If @flag==0, the function will attempt to maximize the 
 * refresh rate.  Otherwise, it will calculate timings based on
 * the flag and accompanying value.  
 *
 * If FB_IGNOREMON bit is set in @flags, monitor specs will be 
 * ignored and @var will be filled with the calculated timings.
 *
 * All calculations are based on the VESA GTF Spreadsheet
 * available at VESA's public ftp (http://www.vesa.org).
 * 
 * NOTES:
 * The timings generated by the GTF will be different from VESA
 * DMT.  It might be a good idea to keep a table of standard
 * VESA modes as well.  The GTF may also not work for some displays,
 * such as, and especially, analog TV.
 *   
 * REQUIRES:
 * A valid info->monspecs, otherwise 'safe numbers' will be used.
 */ 
int fb_get_mode(int flags, u32 val, struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct __fb_timings *timings;
	u32 interlace = 1, dscan = 1;
	u32 hfmin, hfmax, vfmin, vfmax, dclkmin, dclkmax, err = 0;


	timings = kzalloc(sizeof(struct __fb_timings), GFP_KERNEL);

	if (!timings)
		return -ENOMEM;

	/* 
	 * If monspecs are invalid, use values that are enough
	 * for 640x480@60
	 */
	if (!info || !info->monspecs.hfmax || !info->monspecs.vfmax ||
	    !info->monspecs.dclkmax ||
	    info->monspecs.hfmax < info->monspecs.hfmin ||
	    info->monspecs.vfmax < info->monspecs.vfmin ||
	    info->monspecs.dclkmax < info->monspecs.dclkmin) {
		hfmin = 29000; hfmax = 30000;
		vfmin = 60; vfmax = 60;
		dclkmin = 0; dclkmax = 25000000;
	} else {
		hfmin = info->monspecs.hfmin;
		hfmax = info->monspecs.hfmax;
		vfmin = info->monspecs.vfmin;
		vfmax = info->monspecs.vfmax;
		dclkmin = info->monspecs.dclkmin;
		dclkmax = info->monspecs.dclkmax;
	}

	timings->hactive = var->xres;
	timings->vactive = var->yres;
	if (var->vmode & FB_VMODE_INTERLACED) { 
		timings->vactive /= 2;
		interlace = 2;
	}
	if (var->vmode & FB_VMODE_DOUBLE) {
		timings->vactive *= 2;
		dscan = 2;
	}

	switch (flags & ~FB_IGNOREMON) {
	case FB_MAXTIMINGS: /* maximize refresh rate */
		timings->hfreq = hfmax;
		fb_timings_hfreq(timings);
		if (timings->vfreq > vfmax) {
			timings->vfreq = vfmax;
			fb_timings_vfreq(timings);
		}
		if (timings->dclk > dclkmax) {
			timings->dclk = dclkmax;
			fb_timings_dclk(timings);
		}
		break;
	case FB_VSYNCTIMINGS: /* vrefresh driven */
		timings->vfreq = val;
		fb_timings_vfreq(timings);
		break;
	case FB_HSYNCTIMINGS: /* hsync driven */
		timings->hfreq = val;
		fb_timings_hfreq(timings);
		break;
	case FB_DCLKTIMINGS: /* pixelclock driven */
		timings->dclk = PICOS2KHZ(val) * 1000;
		fb_timings_dclk(timings);
		break;
	default:
		err = -EINVAL;
		
	} 
	
	if (err || (!(flags & FB_IGNOREMON) &&
	    (timings->vfreq < vfmin || timings->vfreq > vfmax ||
	     timings->hfreq < hfmin || timings->hfreq > hfmax ||
	     timings->dclk < dclkmin || timings->dclk > dclkmax))) {
		err = -EINVAL;
	} else {
		var->pixclock = KHZ2PICOS(timings->dclk/1000);
		var->hsync_len = (timings->htotal * 8)/100;
		var->right_margin = (timings->hblank/2) - var->hsync_len;
		var->left_margin = timings->hblank - var->right_margin -
			var->hsync_len;
		var->vsync_len = (3 * interlace)/dscan;
		var->lower_margin = (1 * interlace)/dscan;
		var->upper_margin = (timings->vblank * interlace)/dscan -
			(var->vsync_len + var->lower_margin);
	}
	
	kfree(timings);
	return err;
}
#else
int fb_parse_edid(unsigned char *edid, struct fb_var_screeninfo *var)
{
	return 1;
}
void fb_edid_to_monspecs(unsigned char *edid, struct fb_monspecs *specs)
{
	specs = NULL;
}
void fb_destroy_modedb(struct fb_videomode *modedb)
{
}
int fb_get_mode(int flags, u32 val, struct fb_var_screeninfo *var,
		struct fb_info *info)
{
	return -EINVAL;
}
#endif /* CONFIG_FB_MODE_HELPERS */
	
/*
 * fb_validate_mode - validates var against monitor capabilities
 * @var: pointer to fb_var_screeninfo
 * @info: pointer to fb_info
 *
 * DESCRIPTION:
 * Validates video mode against monitor capabilities specified in
 * info->monspecs.
 *
 * REQUIRES:
 * A valid info->monspecs.
 */
int fb_validate_mode(const struct fb_var_screeninfo *var, struct fb_info *info)
{
	u32 hfreq, vfreq, htotal, vtotal, pixclock;
	u32 hfmin, hfmax, vfmin, vfmax, dclkmin, dclkmax;

	/* 
	 * If monspecs are invalid, use values that are enough
	 * for 640x480@60
	 */
	if (!info->monspecs.hfmax || !info->monspecs.vfmax ||
	    !info->monspecs.dclkmax ||
	    info->monspecs.hfmax < info->monspecs.hfmin ||
	    info->monspecs.vfmax < info->monspecs.vfmin ||
	    info->monspecs.dclkmax < info->monspecs.dclkmin) {
		hfmin = 29000; hfmax = 30000;
		vfmin = 60; vfmax = 60;
		dclkmin = 0; dclkmax = 25000000;
	} else {
		hfmin = info->monspecs.hfmin;
		hfmax = info->monspecs.hfmax;
		vfmin = info->monspecs.vfmin;
		vfmax = info->monspecs.vfmax;
		dclkmin = info->monspecs.dclkmin;
		dclkmax = info->monspecs.dclkmax;
	}

	if (!var->pixclock)
		return -EINVAL;
	pixclock = PICOS2KHZ(var->pixclock) * 1000;
	   
	htotal = var->xres + var->right_margin + var->hsync_len + 
		var->left_margin;
	vtotal = var->yres + var->lower_margin + var->vsync_len + 
		var->upper_margin;

	if (var->vmode & FB_VMODE_INTERLACED)
		vtotal /= 2;
	if (var->vmode & FB_VMODE_DOUBLE)
		vtotal *= 2;

	hfreq = pixclock/htotal;
	hfreq = (hfreq + 500) / 1000 * 1000;

	vfreq = hfreq/vtotal;

	return (vfreq < vfmin || vfreq > vfmax || 
		hfreq < hfmin || hfreq > hfmax ||
		pixclock < dclkmin || pixclock > dclkmax) ?
		-EINVAL : 0;
}

#if defined(CONFIG_FIRMWARE_EDID) && defined(CONFIG_X86)

/*
 * We need to ensure that the EDID block is only returned for
 * the primary graphics adapter.
 */

const unsigned char *fb_firmware_edid(struct device *device)
{
	struct pci_dev *dev = NULL;
	struct resource *res = NULL;
	unsigned char *edid = NULL;

	if (device)
		dev = to_pci_dev(device);

	if (dev)
		res = &dev->resource[PCI_ROM_RESOURCE];

	if (res && res->flags & IORESOURCE_ROM_SHADOW)
		edid = edid_info.dummy;

	return edid;
}
#else
const unsigned char *fb_firmware_edid(struct device *device)
{
	return NULL;
}
#endif
EXPORT_SYMBOL(fb_firmware_edid);

EXPORT_SYMBOL(fb_parse_edid);
EXPORT_SYMBOL(fb_edid_to_monspecs);
EXPORT_SYMBOL(fb_get_mode);
EXPORT_SYMBOL(fb_validate_mode);
EXPORT_SYMBOL(fb_destroy_modedb);
