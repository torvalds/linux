/*
 * Copyright (c) 2006 Luc Verhaegen (quirks list)
 * Copyright (c) 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * DDC probing routines (drm_ddc_read & drm_do_probe_ddc_edid) originally from
 * FB layer.
 *   Copyright (C) 2006 Dennis Munsie <dmunsie@cecropia.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include "drmP.h"
#include "drm_edid.h"

/*
 * TODO:
 *   - support EDID 1.4 (incl. CE blocks)
 */

/*
 * EDID blocks out in the wild have a variety of bugs, try to collect
 * them here (note that userspace may work around broken monitors first,
 * but fixes should make their way here so that the kernel "just works"
 * on as many displays as possible).
 */

/* First detailed mode wrong, use largest 60Hz mode */
#define EDID_QUIRK_PREFER_LARGE_60		(1 << 0)
/* Reported 135MHz pixel clock is too high, needs adjustment */
#define EDID_QUIRK_135_CLOCK_TOO_HIGH		(1 << 1)
/* Prefer the largest mode at 75 Hz */
#define EDID_QUIRK_PREFER_LARGE_75		(1 << 2)
/* Detail timing is in cm not mm */
#define EDID_QUIRK_DETAILED_IN_CM		(1 << 3)
/* Detailed timing descriptors have bogus size values, so just take the
 * maximum size and use that.
 */
#define EDID_QUIRK_DETAILED_USE_MAXIMUM_SIZE	(1 << 4)
/* Monitor forgot to set the first detailed is preferred bit. */
#define EDID_QUIRK_FIRST_DETAILED_PREFERRED	(1 << 5)
/* use +hsync +vsync for detailed mode */
#define EDID_QUIRK_DETAILED_SYNC_PP		(1 << 6)

static struct edid_quirk {
	char *vendor;
	int product_id;
	u32 quirks;
} edid_quirk_list[] = {
	/* Acer AL1706 */
	{ "ACR", 44358, EDID_QUIRK_PREFER_LARGE_60 },
	/* Acer F51 */
	{ "API", 0x7602, EDID_QUIRK_PREFER_LARGE_60 },
	/* Unknown Acer */
	{ "ACR", 2423, EDID_QUIRK_FIRST_DETAILED_PREFERRED },

	/* Belinea 10 15 55 */
	{ "MAX", 1516, EDID_QUIRK_PREFER_LARGE_60 },
	{ "MAX", 0x77e, EDID_QUIRK_PREFER_LARGE_60 },

	/* Envision Peripherals, Inc. EN-7100e */
	{ "EPI", 59264, EDID_QUIRK_135_CLOCK_TOO_HIGH },

	/* Funai Electronics PM36B */
	{ "FCM", 13600, EDID_QUIRK_PREFER_LARGE_75 |
	  EDID_QUIRK_DETAILED_IN_CM },

	/* LG Philips LCD LP154W01-A5 */
	{ "LPL", 0, EDID_QUIRK_DETAILED_USE_MAXIMUM_SIZE },
	{ "LPL", 0x2a00, EDID_QUIRK_DETAILED_USE_MAXIMUM_SIZE },

	/* Philips 107p5 CRT */
	{ "PHL", 57364, EDID_QUIRK_FIRST_DETAILED_PREFERRED },

	/* Proview AY765C */
	{ "PTS", 765, EDID_QUIRK_FIRST_DETAILED_PREFERRED },

	/* Samsung SyncMaster 205BW.  Note: irony */
	{ "SAM", 541, EDID_QUIRK_DETAILED_SYNC_PP },
	/* Samsung SyncMaster 22[5-6]BW */
	{ "SAM", 596, EDID_QUIRK_PREFER_LARGE_60 },
	{ "SAM", 638, EDID_QUIRK_PREFER_LARGE_60 },
};


/* Valid EDID header has these bytes */
static u8 edid_header[] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };

/**
 * edid_is_valid - sanity check EDID data
 * @edid: EDID data
 *
 * Sanity check the EDID block by looking at the header, the version number
 * and the checksum.  Return 0 if the EDID doesn't check out, or 1 if it's
 * valid.
 */
static bool edid_is_valid(struct edid *edid)
{
	int i;
	u8 csum = 0;
	u8 *raw_edid = (u8 *)edid;

	if (memcmp(edid->header, edid_header, sizeof(edid_header)))
		goto bad;
	if (edid->version != 1) {
		DRM_ERROR("EDID has major version %d, instead of 1\n", edid->version);
		goto bad;
	}
	if (edid->revision > 3) {
		DRM_ERROR("EDID has minor version %d, which is not between 0-3\n", edid->revision);
		goto bad;
	}

	for (i = 0; i < EDID_LENGTH; i++)
		csum += raw_edid[i];
	if (csum) {
		DRM_ERROR("EDID checksum is invalid, remainder is %d\n", csum);
		goto bad;
	}

	return 1;

bad:
	if (raw_edid) {
		DRM_ERROR("Raw EDID:\n");
		print_hex_dump_bytes(KERN_ERR, DUMP_PREFIX_NONE, raw_edid, EDID_LENGTH);
		printk("\n");
	}
	return 0;
}

/**
 * edid_vendor - match a string against EDID's obfuscated vendor field
 * @edid: EDID to match
 * @vendor: vendor string
 *
 * Returns true if @vendor is in @edid, false otherwise
 */
static bool edid_vendor(struct edid *edid, char *vendor)
{
	char edid_vendor[3];

	edid_vendor[0] = ((edid->mfg_id[0] & 0x7c) >> 2) + '@';
	edid_vendor[1] = (((edid->mfg_id[0] & 0x3) << 3) |
			  ((edid->mfg_id[1] & 0xe0) >> 5)) + '@';
	edid_vendor[2] = (edid->mfg_id[2] & 0x1f) + '@';

	return !strncmp(edid_vendor, vendor, 3);
}

/**
 * edid_get_quirks - return quirk flags for a given EDID
 * @edid: EDID to process
 *
 * This tells subsequent routines what fixes they need to apply.
 */
static u32 edid_get_quirks(struct edid *edid)
{
	struct edid_quirk *quirk;
	int i;

	for (i = 0; i < ARRAY_SIZE(edid_quirk_list); i++) {
		quirk = &edid_quirk_list[i];

		if (edid_vendor(edid, quirk->vendor) &&
		    (EDID_PRODUCT_ID(edid) == quirk->product_id))
			return quirk->quirks;
	}

	return 0;
}

#define MODE_SIZE(m) ((m)->hdisplay * (m)->vdisplay)
#define MODE_REFRESH_DIFF(m,r) (abs((m)->vrefresh - target_refresh))


/**
 * edid_fixup_preferred - set preferred modes based on quirk list
 * @connector: has mode list to fix up
 * @quirks: quirks list
 *
 * Walk the mode list for @connector, clearing the preferred status
 * on existing modes and setting it anew for the right mode ala @quirks.
 */
static void edid_fixup_preferred(struct drm_connector *connector,
				 u32 quirks)
{
	struct drm_display_mode *t, *cur_mode, *preferred_mode;
	int target_refresh = 0;

	if (list_empty(&connector->probed_modes))
		return;

	if (quirks & EDID_QUIRK_PREFER_LARGE_60)
		target_refresh = 60;
	if (quirks & EDID_QUIRK_PREFER_LARGE_75)
		target_refresh = 75;

	preferred_mode = list_first_entry(&connector->probed_modes,
					  struct drm_display_mode, head);

	list_for_each_entry_safe(cur_mode, t, &connector->probed_modes, head) {
		cur_mode->type &= ~DRM_MODE_TYPE_PREFERRED;

		if (cur_mode == preferred_mode)
			continue;

		/* Largest mode is preferred */
		if (MODE_SIZE(cur_mode) > MODE_SIZE(preferred_mode))
			preferred_mode = cur_mode;

		/* At a given size, try to get closest to target refresh */
		if ((MODE_SIZE(cur_mode) == MODE_SIZE(preferred_mode)) &&
		    MODE_REFRESH_DIFF(cur_mode, target_refresh) <
		    MODE_REFRESH_DIFF(preferred_mode, target_refresh)) {
			preferred_mode = cur_mode;
		}
	}

	preferred_mode->type |= DRM_MODE_TYPE_PREFERRED;
}

/**
 * drm_mode_std - convert standard mode info (width, height, refresh) into mode
 * @t: standard timing params
 *
 * Take the standard timing params (in this case width, aspect, and refresh)
 * and convert them into a real mode using CVT.
 *
 * Punts for now, but should eventually use the FB layer's CVT based mode
 * generation code.
 */
struct drm_display_mode *drm_mode_std(struct drm_device *dev,
				      struct std_timing *t)
{
	struct drm_display_mode *mode;
	int hsize = t->hsize * 8 + 248, vsize;

	mode = drm_mode_create(dev);
	if (!mode)
		return NULL;

	if (t->aspect_ratio == 0)
		vsize = (hsize * 10) / 16;
	else if (t->aspect_ratio == 1)
		vsize = (hsize * 3) / 4;
	else if (t->aspect_ratio == 2)
		vsize = (hsize * 4) / 5;
	else
		vsize = (hsize * 9) / 16;

	drm_mode_set_name(mode);

	return mode;
}

/**
 * drm_mode_detailed - create a new mode from an EDID detailed timing section
 * @dev: DRM device (needed to create new mode)
 * @edid: EDID block
 * @timing: EDID detailed timing info
 * @quirks: quirks to apply
 *
 * An EDID detailed timing block contains enough info for us to create and
 * return a new struct drm_display_mode.
 */
static struct drm_display_mode *drm_mode_detailed(struct drm_device *dev,
						  struct edid *edid,
						  struct detailed_timing *timing,
						  u32 quirks)
{
	struct drm_display_mode *mode;
	struct detailed_pixel_timing *pt = &timing->data.pixel_data;

	if (pt->stereo) {
		printk(KERN_WARNING "stereo mode not supported\n");
		return NULL;
	}
	if (!pt->separate_sync) {
		printk(KERN_WARNING "integrated sync not supported\n");
		return NULL;
	}

	mode = drm_mode_create(dev);
	if (!mode)
		return NULL;

	mode->type = DRM_MODE_TYPE_DRIVER;

	if (quirks & EDID_QUIRK_135_CLOCK_TOO_HIGH)
		timing->pixel_clock = 1088;

	mode->clock = timing->pixel_clock * 10;

	mode->hdisplay = (pt->hactive_hi << 8) | pt->hactive_lo;
	mode->hsync_start = mode->hdisplay + ((pt->hsync_offset_hi << 8) |
					      pt->hsync_offset_lo);
	mode->hsync_end = mode->hsync_start +
		((pt->hsync_pulse_width_hi << 8) |
		 pt->hsync_pulse_width_lo);
	mode->htotal = mode->hdisplay + ((pt->hblank_hi << 8) | pt->hblank_lo);

	mode->vdisplay = (pt->vactive_hi << 8) | pt->vactive_lo;
	mode->vsync_start = mode->vdisplay + ((pt->vsync_offset_hi << 4) |
					      pt->vsync_offset_lo);
	mode->vsync_end = mode->vsync_start +
		((pt->vsync_pulse_width_hi << 4) |
		 pt->vsync_pulse_width_lo);
	mode->vtotal = mode->vdisplay + ((pt->vblank_hi << 8) | pt->vblank_lo);

	drm_mode_set_name(mode);

	if (pt->interlaced)
		mode->flags |= DRM_MODE_FLAG_INTERLACE;

	if (quirks & EDID_QUIRK_DETAILED_SYNC_PP) {
		pt->hsync_positive = 1;
		pt->vsync_positive = 1;
	}

	mode->flags |= pt->hsync_positive ? DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC;
	mode->flags |= pt->vsync_positive ? DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC;

	mode->width_mm = pt->width_mm_lo | (pt->width_mm_hi << 8);
	mode->height_mm = pt->height_mm_lo | (pt->height_mm_hi << 8);

	if (quirks & EDID_QUIRK_DETAILED_IN_CM) {
		mode->width_mm *= 10;
		mode->height_mm *= 10;
	}

	if (quirks & EDID_QUIRK_DETAILED_USE_MAXIMUM_SIZE) {
		mode->width_mm = edid->width_cm * 10;
		mode->height_mm = edid->height_cm * 10;
	}

	return mode;
}

/*
 * Detailed mode info for the EDID "established modes" data to use.
 */
static struct drm_display_mode edid_est_modes[] = {
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DRIVER, 40000, 800, 840,
		   968, 1056, 0, 600, 601, 605, 628, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) }, /* 800x600@60Hz */
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DRIVER, 36000, 800, 824,
		   896, 1024, 0, 600, 601, 603,  625, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) }, /* 800x600@56Hz */
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 31500, 640, 656,
		   720, 840, 0, 480, 481, 484, 500, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) }, /* 640x480@75Hz */
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 31500, 640, 664,
		   704,  832, 0, 480, 489, 491, 520, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) }, /* 640x480@72Hz */
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 30240, 640, 704,
		   768,  864, 0, 480, 483, 486, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) }, /* 640x480@67Hz */
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 25200, 640, 656,
		   752, 800, 0, 480, 490, 492, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) }, /* 640x480@60Hz */
	{ DRM_MODE("720x400", DRM_MODE_TYPE_DRIVER, 35500, 720, 738,
		   846, 900, 0, 400, 421, 423,  449, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) }, /* 720x400@88Hz */
	{ DRM_MODE("720x400", DRM_MODE_TYPE_DRIVER, 28320, 720, 738,
		   846,  900, 0, 400, 412, 414, 449, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) }, /* 720x400@70Hz */
	{ DRM_MODE("1280x1024", DRM_MODE_TYPE_DRIVER, 135000, 1280, 1296,
		   1440, 1688, 0, 1024, 1025, 1028, 1066, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) }, /* 1280x1024@75Hz */
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 78800, 1024, 1040,
		   1136, 1312, 0,  768, 769, 772, 800, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) }, /* 1024x768@75Hz */
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 75000, 1024, 1048,
		   1184, 1328, 0,  768, 771, 777, 806, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) }, /* 1024x768@70Hz */
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 65000, 1024, 1048,
		   1184, 1344, 0,  768, 771, 777, 806, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) }, /* 1024x768@60Hz */
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER,44900, 1024, 1032,
		   1208, 1264, 0, 768, 768, 776, 817, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_INTERLACE) }, /* 1024x768@43Hz */
	{ DRM_MODE("832x624", DRM_MODE_TYPE_DRIVER, 57284, 832, 864,
		   928, 1152, 0, 624, 625, 628, 667, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) }, /* 832x624@75Hz */
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DRIVER, 49500, 800, 816,
		   896, 1056, 0, 600, 601, 604,  625, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) }, /* 800x600@75Hz */
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DRIVER, 50000, 800, 856,
		   976, 1040, 0, 600, 637, 643, 666, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) }, /* 800x600@72Hz */
	{ DRM_MODE("1152x864", DRM_MODE_TYPE_DRIVER, 108000, 1152, 1216,
		   1344, 1600, 0,  864, 865, 868, 900, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) }, /* 1152x864@75Hz */
};

#define EDID_EST_TIMINGS 16
#define EDID_STD_TIMINGS 8
#define EDID_DETAILED_TIMINGS 4

/**
 * add_established_modes - get est. modes from EDID and add them
 * @edid: EDID block to scan
 *
 * Each EDID block contains a bitmap of the supported "established modes" list
 * (defined above).  Tease them out and add them to the global modes list.
 */
static int add_established_modes(struct drm_connector *connector, struct edid *edid)
{
	struct drm_device *dev = connector->dev;
	unsigned long est_bits = edid->established_timings.t1 |
		(edid->established_timings.t2 << 8) |
		((edid->established_timings.mfg_rsvd & 0x80) << 9);
	int i, modes = 0;

	for (i = 0; i <= EDID_EST_TIMINGS; i++)
		if (est_bits & (1<<i)) {
			struct drm_display_mode *newmode;
			newmode = drm_mode_duplicate(dev, &edid_est_modes[i]);
			if (newmode) {
				drm_mode_probed_add(connector, newmode);
				modes++;
			}
		}

	return modes;
}

/**
 * add_standard_modes - get std. modes from EDID and add them
 * @edid: EDID block to scan
 *
 * Standard modes can be calculated using the CVT standard.  Grab them from
 * @edid, calculate them, and add them to the list.
 */
static int add_standard_modes(struct drm_connector *connector, struct edid *edid)
{
	struct drm_device *dev = connector->dev;
	int i, modes = 0;

	for (i = 0; i < EDID_STD_TIMINGS; i++) {
		struct std_timing *t = &edid->standard_timings[i];
		struct drm_display_mode *newmode;

		/* If std timings bytes are 1, 1 it's empty */
		if (t->hsize == 1 && (t->aspect_ratio | t->vfreq) == 1)
			continue;

		newmode = drm_mode_std(dev, &edid->standard_timings[i]);
		if (newmode) {
			drm_mode_probed_add(connector, newmode);
			modes++;
		}
	}

	return modes;
}

/**
 * add_detailed_modes - get detailed mode info from EDID data
 * @connector: attached connector
 * @edid: EDID block to scan
 * @quirks: quirks to apply
 *
 * Some of the detailed timing sections may contain mode information.  Grab
 * it and add it to the list.
 */
static int add_detailed_info(struct drm_connector *connector,
			     struct edid *edid, u32 quirks)
{
	struct drm_device *dev = connector->dev;
	int i, j, modes = 0;

	for (i = 0; i < EDID_DETAILED_TIMINGS; i++) {
		struct detailed_timing *timing = &edid->detailed_timings[i];
		struct detailed_non_pixel *data = &timing->data.other_data;
		struct drm_display_mode *newmode;

		/* EDID up to and including 1.2 may put monitor info here */
		if (edid->version == 1 && edid->revision < 3)
			continue;

		/* Detailed mode timing */
		if (timing->pixel_clock) {
			newmode = drm_mode_detailed(dev, edid, timing, quirks);
			if (!newmode)
				continue;

			/* First detailed mode is preferred */
			if (i == 0 && edid->preferred_timing)
				newmode->type |= DRM_MODE_TYPE_PREFERRED;
			drm_mode_probed_add(connector, newmode);

			modes++;
			continue;
		}

		/* Other timing or info */
		switch (data->type) {
		case EDID_DETAIL_MONITOR_SERIAL:
			break;
		case EDID_DETAIL_MONITOR_STRING:
			break;
		case EDID_DETAIL_MONITOR_RANGE:
			/* Get monitor range data */
			break;
		case EDID_DETAIL_MONITOR_NAME:
			break;
		case EDID_DETAIL_MONITOR_CPDATA:
			break;
		case EDID_DETAIL_STD_MODES:
			/* Five modes per detailed section */
			for (j = 0; j < 5; i++) {
				struct std_timing *std;
				struct drm_display_mode *newmode;

				std = &data->data.timings[j];
				newmode = drm_mode_std(dev, std);
				if (newmode) {
					drm_mode_probed_add(connector, newmode);
					modes++;
				}
			}
			break;
		default:
			break;
		}
	}

	return modes;
}

#define DDC_ADDR 0x50

unsigned char *drm_do_probe_ddc_edid(struct i2c_adapter *adapter)
{
	unsigned char start = 0x0;
	unsigned char *buf = kmalloc(EDID_LENGTH, GFP_KERNEL);
	struct i2c_msg msgs[] = {
		{
			.addr	= DDC_ADDR,
			.flags	= 0,
			.len	= 1,
			.buf	= &start,
		}, {
			.addr	= DDC_ADDR,
			.flags	= I2C_M_RD,
			.len	= EDID_LENGTH,
			.buf	= buf,
		}
	};

	if (!buf) {
		dev_warn(&adapter->dev, "unable to allocate memory for EDID "
			 "block.\n");
		return NULL;
	}

	if (i2c_transfer(adapter, msgs, 2) == 2)
		return buf;

	dev_info(&adapter->dev, "unable to read EDID block.\n");
	kfree(buf);
	return NULL;
}
EXPORT_SYMBOL(drm_do_probe_ddc_edid);

static unsigned char *drm_ddc_read(struct i2c_adapter *adapter)
{
	struct i2c_algo_bit_data *algo_data = adapter->algo_data;
	unsigned char *edid = NULL;
	int i, j;

	algo_data->setscl(algo_data->data, 1);

	for (i = 0; i < 1; i++) {
		/* For some old monitors we need the
		 * following process to initialize/stop DDC
		 */
		algo_data->setsda(algo_data->data, 1);
		msleep(13);

		algo_data->setscl(algo_data->data, 1);
		for (j = 0; j < 5; j++) {
			msleep(10);
			if (algo_data->getscl(algo_data->data))
				break;
		}
		if (j == 5)
			continue;

		algo_data->setsda(algo_data->data, 0);
		msleep(15);
		algo_data->setscl(algo_data->data, 0);
		msleep(15);
		algo_data->setsda(algo_data->data, 1);
		msleep(15);

		/* Do the real work */
		edid = drm_do_probe_ddc_edid(adapter);
		algo_data->setsda(algo_data->data, 0);
		algo_data->setscl(algo_data->data, 0);
		msleep(15);

		algo_data->setscl(algo_data->data, 1);
		for (j = 0; j < 10; j++) {
			msleep(10);
			if (algo_data->getscl(algo_data->data))
				break;
		}

		algo_data->setsda(algo_data->data, 1);
		msleep(15);
		algo_data->setscl(algo_data->data, 0);
		algo_data->setsda(algo_data->data, 0);
		if (edid)
			break;
	}
	/* Release the DDC lines when done or the Apple Cinema HD display
	 * will switch off
	 */
	algo_data->setsda(algo_data->data, 1);
	algo_data->setscl(algo_data->data, 1);

	return edid;
}

/**
 * drm_get_edid - get EDID data, if available
 * @connector: connector we're probing
 * @adapter: i2c adapter to use for DDC
 *
 * Poke the given connector's i2c channel to grab EDID data if possible.
 *
 * Return edid data or NULL if we couldn't find any.
 */
struct edid *drm_get_edid(struct drm_connector *connector,
			  struct i2c_adapter *adapter)
{
	struct edid *edid;

	edid = (struct edid *)drm_ddc_read(adapter);
	if (!edid) {
		dev_info(&connector->dev->pdev->dev, "%s: no EDID data\n",
			 drm_get_connector_name(connector));
		return NULL;
	}
	if (!edid_is_valid(edid)) {
		dev_warn(&connector->dev->pdev->dev, "%s: EDID invalid.\n",
			 drm_get_connector_name(connector));
		kfree(edid);
		return NULL;
	}

	connector->display_info.raw_edid = (char *)edid;

	return edid;
}
EXPORT_SYMBOL(drm_get_edid);

/**
 * drm_add_edid_modes - add modes from EDID data, if available
 * @connector: connector we're probing
 * @edid: edid data
 *
 * Add the specified modes to the connector's mode list.
 *
 * Return number of modes added or 0 if we couldn't find any.
 */
int drm_add_edid_modes(struct drm_connector *connector, struct edid *edid)
{
	int num_modes = 0;
	u32 quirks;

	if (edid == NULL) {
		return 0;
	}
	if (!edid_is_valid(edid)) {
		dev_warn(&connector->dev->pdev->dev, "%s: EDID invalid.\n",
			 drm_get_connector_name(connector));
		return 0;
	}

	quirks = edid_get_quirks(edid);

	num_modes += add_established_modes(connector, edid);
	num_modes += add_standard_modes(connector, edid);
	num_modes += add_detailed_info(connector, edid, quirks);

	if (quirks & (EDID_QUIRK_PREFER_LARGE_60 | EDID_QUIRK_PREFER_LARGE_75))
		edid_fixup_preferred(connector, quirks);

	connector->display_info.serration_vsync = edid->serration_vsync;
	connector->display_info.sync_on_green = edid->sync_on_green;
	connector->display_info.composite_sync = edid->composite_sync;
	connector->display_info.separate_syncs = edid->separate_syncs;
	connector->display_info.blank_to_black = edid->blank_to_black;
	connector->display_info.video_level = edid->video_level;
	connector->display_info.digital = edid->digital;
	connector->display_info.width_mm = edid->width_cm * 10;
	connector->display_info.height_mm = edid->height_cm * 10;
	connector->display_info.gamma = edid->gamma;
	connector->display_info.gtf_supported = edid->default_gtf;
	connector->display_info.standard_color = edid->standard_color;
	connector->display_info.display_type = edid->display_type;
	connector->display_info.active_off_supported = edid->pm_active_off;
	connector->display_info.suspend_supported = edid->pm_suspend;
	connector->display_info.standby_supported = edid->pm_standby;
	connector->display_info.gamma = edid->gamma;

	return num_modes;
}
EXPORT_SYMBOL(drm_add_edid_modes);
