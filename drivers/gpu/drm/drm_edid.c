/*
 * Copyright (c) 2006 Luc Verhaegen (quirks list)
 * Copyright (c) 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright 2010 Red Hat, Inc.
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
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include <drm/drm_edid.h>
#include "drm_edid_modes.h"

#define version_greater(edid, maj, min) \
	(((edid)->version > (maj)) || \
	 ((edid)->version == (maj) && (edid)->revision > (min)))

#define EDID_EST_TIMINGS 16
#define EDID_STD_TIMINGS 8
#define EDID_DETAILED_TIMINGS 4

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
/* Force reduced-blanking timings for detailed modes */
#define EDID_QUIRK_FORCE_REDUCED_BLANKING	(1 << 7)

struct detailed_mode_closure {
	struct drm_connector *connector;
	struct edid *edid;
	bool preferred;
	u32 quirks;
	int modes;
};

#define LEVEL_DMT	0
#define LEVEL_GTF	1
#define LEVEL_GTF2	2
#define LEVEL_CVT	3

static struct edid_quirk {
	char vendor[4];
	int product_id;
	u32 quirks;
} edid_quirk_list[] = {
	/* ASUS VW222S */
	{ "ACI", 0x22a2, EDID_QUIRK_FORCE_REDUCED_BLANKING },

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
	/* Envision EN2028 */
	{ "EPI", 8232, EDID_QUIRK_PREFER_LARGE_60 },

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

	/* ViewSonic VA2026w */
	{ "VSC", 5020, EDID_QUIRK_FORCE_REDUCED_BLANKING },
};

/*** DDC fetch and block validation ***/

static const u8 edid_header[] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
};

 /*
 * Sanity check the header of the base EDID block.  Return 8 if the header
 * is perfect, down to 0 if it's totally wrong.
 */
int drm_edid_header_is_valid(const u8 *raw_edid)
{
	int i, score = 0;

	for (i = 0; i < sizeof(edid_header); i++)
		if (raw_edid[i] == edid_header[i])
			score++;

	return score;
}
EXPORT_SYMBOL(drm_edid_header_is_valid);

static int edid_fixup __read_mostly = 6;
module_param_named(edid_fixup, edid_fixup, int, 0400);
MODULE_PARM_DESC(edid_fixup,
		 "Minimum number of valid EDID header bytes (0-8, default 6)");

/*
 * Sanity check the EDID block (base or extension).  Return 0 if the block
 * doesn't check out, or 1 if it's valid.
 */
bool drm_edid_block_valid(u8 *raw_edid, int block, bool print_bad_edid)
{
	int i;
	u8 csum = 0;
	struct edid *edid = (struct edid *)raw_edid;

	if (edid_fixup > 8 || edid_fixup < 0)
		edid_fixup = 6;

	if (block == 0) {
		int score = drm_edid_header_is_valid(raw_edid);
		if (score == 8) ;
		else if (score >= edid_fixup) {
			DRM_DEBUG("Fixing EDID header, your hardware may be failing\n");
			memcpy(raw_edid, edid_header, sizeof(edid_header));
		} else {
			goto bad;
		}
	}

	for (i = 0; i < EDID_LENGTH; i++)
		csum += raw_edid[i];
	if (csum) {
		if (print_bad_edid) {
			DRM_ERROR("EDID checksum is invalid, remainder is %d\n", csum);
		}

		/* allow CEA to slide through, switches mangle this */
		if (raw_edid[0] != 0x02)
			goto bad;
	}

	/* per-block-type checks */
	switch (raw_edid[0]) {
	case 0: /* base */
		if (edid->version != 1) {
			DRM_ERROR("EDID has major version %d, instead of 1\n", edid->version);
			goto bad;
		}

		if (edid->revision > 4)
			DRM_DEBUG("EDID minor > 4, assuming backward compatibility\n");
		break;

	default:
		break;
	}

	return 1;

bad:
	if (raw_edid && print_bad_edid) {
		printk(KERN_ERR "Raw EDID:\n");
		print_hex_dump(KERN_ERR, " \t", DUMP_PREFIX_NONE, 16, 1,
			       raw_edid, EDID_LENGTH, false);
	}
	return 0;
}
EXPORT_SYMBOL(drm_edid_block_valid);

/**
 * drm_edid_is_valid - sanity check EDID data
 * @edid: EDID data
 *
 * Sanity-check an entire EDID record (including extensions)
 */
bool drm_edid_is_valid(struct edid *edid)
{
	int i;
	u8 *raw = (u8 *)edid;

	if (!edid)
		return false;

	for (i = 0; i <= edid->extensions; i++)
		if (!drm_edid_block_valid(raw + i * EDID_LENGTH, i, true))
			return false;

	return true;
}
EXPORT_SYMBOL(drm_edid_is_valid);

#define DDC_SEGMENT_ADDR 0x30
/**
 * Get EDID information via I2C.
 *
 * \param adapter : i2c device adaptor
 * \param buf     : EDID data buffer to be filled
 * \param len     : EDID data buffer length
 * \return 0 on success or -1 on failure.
 *
 * Try to fetch EDID information by calling i2c driver function.
 */
static int
drm_do_probe_ddc_edid(struct i2c_adapter *adapter, unsigned char *buf,
		      int block, int len)
{
	unsigned char start = block * EDID_LENGTH;
	unsigned char segment = block >> 1;
	unsigned char xfers = segment ? 3 : 2;
	int ret, retries = 5;

	/* The core i2c driver will automatically retry the transfer if the
	 * adapter reports EAGAIN. However, we find that bit-banging transfers
	 * are susceptible to errors under a heavily loaded machine and
	 * generate spurious NAKs and timeouts. Retrying the transfer
	 * of the individual block a few times seems to overcome this.
	 */
	do {
		struct i2c_msg msgs[] = {
			{
				.addr	= DDC_SEGMENT_ADDR,
				.flags	= 0,
				.len	= 1,
				.buf	= &segment,
			}, {
				.addr	= DDC_ADDR,
				.flags	= 0,
				.len	= 1,
				.buf	= &start,
			}, {
				.addr	= DDC_ADDR,
				.flags	= I2C_M_RD,
				.len	= len,
				.buf	= buf,
			}
		};

	/*
	 * Avoid sending the segment addr to not upset non-compliant ddc
	 * monitors.
	 */
		ret = i2c_transfer(adapter, &msgs[3 - xfers], xfers);

		if (ret == -ENXIO) {
			DRM_DEBUG_KMS("drm: skipping non-existent adapter %s\n",
					adapter->name);
			break;
		}
	} while (ret != xfers && --retries);

	return ret == xfers ? 0 : -1;
}

static bool drm_edid_is_zero(u8 *in_edid, int length)
{
	if (memchr_inv(in_edid, 0, length))
		return false;

	return true;
}

static u8 *
drm_do_get_edid(struct drm_connector *connector, struct i2c_adapter *adapter)
{
	int i, j = 0, valid_extensions = 0;
	u8 *block, *new;
	bool print_bad_edid = !connector->bad_edid_counter || (drm_debug & DRM_UT_KMS);

	if ((block = kmalloc(EDID_LENGTH, GFP_KERNEL)) == NULL)
		return NULL;

	/* base block fetch */
	for (i = 0; i < 4; i++) {
		if (drm_do_probe_ddc_edid(adapter, block, 0, EDID_LENGTH))
			goto out;
		if (drm_edid_block_valid(block, 0, print_bad_edid))
			break;
		if (i == 0 && drm_edid_is_zero(block, EDID_LENGTH)) {
			connector->null_edid_counter++;
			goto carp;
		}
	}
	if (i == 4)
		goto carp;

	/* if there's no extensions, we're done */
	if (block[0x7e] == 0)
		return block;

	new = krealloc(block, (block[0x7e] + 1) * EDID_LENGTH, GFP_KERNEL);
	if (!new)
		goto out;
	block = new;

	for (j = 1; j <= block[0x7e]; j++) {
		for (i = 0; i < 4; i++) {
			if (drm_do_probe_ddc_edid(adapter,
				  block + (valid_extensions + 1) * EDID_LENGTH,
				  j, EDID_LENGTH))
				goto out;
			if (drm_edid_block_valid(block + (valid_extensions + 1) * EDID_LENGTH, j, print_bad_edid)) {
				valid_extensions++;
				break;
			}
		}
		if (i == 4)
			dev_warn(connector->dev->dev,
			 "%s: Ignoring invalid EDID block %d.\n",
			 drm_get_connector_name(connector), j);
	}

	if (valid_extensions != block[0x7e]) {
		block[EDID_LENGTH-1] += block[0x7e] - valid_extensions;
		block[0x7e] = valid_extensions;
		new = krealloc(block, (valid_extensions + 1) * EDID_LENGTH, GFP_KERNEL);
		if (!new)
			goto out;
		block = new;
	}

	return block;

carp:
	if (print_bad_edid) {
		dev_warn(connector->dev->dev, "%s: EDID block %d invalid.\n",
			 drm_get_connector_name(connector), j);
	}
	connector->bad_edid_counter++;

out:
	kfree(block);
	return NULL;
}

/**
 * Probe DDC presence.
 *
 * \param adapter : i2c device adaptor
 * \return 1 on success
 */
bool
drm_probe_ddc(struct i2c_adapter *adapter)
{
	unsigned char out;

	return (drm_do_probe_ddc_edid(adapter, &out, 0, 1) == 0);
}
EXPORT_SYMBOL(drm_probe_ddc);

/**
 * drm_get_edid - get EDID data, if available
 * @connector: connector we're probing
 * @adapter: i2c adapter to use for DDC
 *
 * Poke the given i2c channel to grab EDID data if possible.  If found,
 * attach it to the connector.
 *
 * Return edid data or NULL if we couldn't find any.
 */
struct edid *drm_get_edid(struct drm_connector *connector,
			  struct i2c_adapter *adapter)
{
	struct edid *edid = NULL;

	if (drm_probe_ddc(adapter))
		edid = (struct edid *)drm_do_get_edid(connector, adapter);

	return edid;
}
EXPORT_SYMBOL(drm_get_edid);

/*** EDID parsing ***/

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
	edid_vendor[2] = (edid->mfg_id[1] & 0x1f) + '@';

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

static bool
mode_is_rb(const struct drm_display_mode *mode)
{
	return (mode->htotal - mode->hdisplay == 160) &&
	       (mode->hsync_end - mode->hdisplay == 80) &&
	       (mode->hsync_end - mode->hsync_start == 32) &&
	       (mode->vsync_start - mode->vdisplay == 3);
}

/*
 * drm_mode_find_dmt - Create a copy of a mode if present in DMT
 * @dev: Device to duplicate against
 * @hsize: Mode width
 * @vsize: Mode height
 * @fresh: Mode refresh rate
 * @rb: Mode reduced-blanking-ness
 *
 * Walk the DMT mode list looking for a match for the given parameters.
 * Return a newly allocated copy of the mode, or NULL if not found.
 */
struct drm_display_mode *drm_mode_find_dmt(struct drm_device *dev,
					   int hsize, int vsize, int fresh,
					   bool rb)
{
	int i;

	for (i = 0; i < drm_num_dmt_modes; i++) {
		const struct drm_display_mode *ptr = &drm_dmt_modes[i];
		if (hsize != ptr->hdisplay)
			continue;
		if (vsize != ptr->vdisplay)
			continue;
		if (fresh != drm_mode_vrefresh(ptr))
			continue;
		if (rb != mode_is_rb(ptr))
			continue;

		return drm_mode_duplicate(dev, ptr);
	}

	return NULL;
}
EXPORT_SYMBOL(drm_mode_find_dmt);

typedef void detailed_cb(struct detailed_timing *timing, void *closure);

static void
cea_for_each_detailed_block(u8 *ext, detailed_cb *cb, void *closure)
{
	int i, n = 0;
	u8 d = ext[0x02];
	u8 *det_base = ext + d;

	n = (127 - d) / 18;
	for (i = 0; i < n; i++)
		cb((struct detailed_timing *)(det_base + 18 * i), closure);
}

static void
vtb_for_each_detailed_block(u8 *ext, detailed_cb *cb, void *closure)
{
	unsigned int i, n = min((int)ext[0x02], 6);
	u8 *det_base = ext + 5;

	if (ext[0x01] != 1)
		return; /* unknown version */

	for (i = 0; i < n; i++)
		cb((struct detailed_timing *)(det_base + 18 * i), closure);
}

static void
drm_for_each_detailed_block(u8 *raw_edid, detailed_cb *cb, void *closure)
{
	int i;
	struct edid *edid = (struct edid *)raw_edid;

	if (edid == NULL)
		return;

	for (i = 0; i < EDID_DETAILED_TIMINGS; i++)
		cb(&(edid->detailed_timings[i]), closure);

	for (i = 1; i <= raw_edid[0x7e]; i++) {
		u8 *ext = raw_edid + (i * EDID_LENGTH);
		switch (*ext) {
		case CEA_EXT:
			cea_for_each_detailed_block(ext, cb, closure);
			break;
		case VTB_EXT:
			vtb_for_each_detailed_block(ext, cb, closure);
			break;
		default:
			break;
		}
	}
}

static void
is_rb(struct detailed_timing *t, void *data)
{
	u8 *r = (u8 *)t;
	if (r[3] == EDID_DETAIL_MONITOR_RANGE)
		if (r[15] & 0x10)
			*(bool *)data = true;
}

/* EDID 1.4 defines this explicitly.  For EDID 1.3, we guess, badly. */
static bool
drm_monitor_supports_rb(struct edid *edid)
{
	if (edid->revision >= 4) {
		bool ret = false;
		drm_for_each_detailed_block((u8 *)edid, is_rb, &ret);
		return ret;
	}

	return ((edid->input & DRM_EDID_INPUT_DIGITAL) != 0);
}

static void
find_gtf2(struct detailed_timing *t, void *data)
{
	u8 *r = (u8 *)t;
	if (r[3] == EDID_DETAIL_MONITOR_RANGE && r[10] == 0x02)
		*(u8 **)data = r;
}

/* Secondary GTF curve kicks in above some break frequency */
static int
drm_gtf2_hbreak(struct edid *edid)
{
	u8 *r = NULL;
	drm_for_each_detailed_block((u8 *)edid, find_gtf2, &r);
	return r ? (r[12] * 2) : 0;
}

static int
drm_gtf2_2c(struct edid *edid)
{
	u8 *r = NULL;
	drm_for_each_detailed_block((u8 *)edid, find_gtf2, &r);
	return r ? r[13] : 0;
}

static int
drm_gtf2_m(struct edid *edid)
{
	u8 *r = NULL;
	drm_for_each_detailed_block((u8 *)edid, find_gtf2, &r);
	return r ? (r[15] << 8) + r[14] : 0;
}

static int
drm_gtf2_k(struct edid *edid)
{
	u8 *r = NULL;
	drm_for_each_detailed_block((u8 *)edid, find_gtf2, &r);
	return r ? r[16] : 0;
}

static int
drm_gtf2_2j(struct edid *edid)
{
	u8 *r = NULL;
	drm_for_each_detailed_block((u8 *)edid, find_gtf2, &r);
	return r ? r[17] : 0;
}

/**
 * standard_timing_level - get std. timing level(CVT/GTF/DMT)
 * @edid: EDID block to scan
 */
static int standard_timing_level(struct edid *edid)
{
	if (edid->revision >= 2) {
		if (edid->revision >= 4 && (edid->features & DRM_EDID_FEATURE_DEFAULT_GTF))
			return LEVEL_CVT;
		if (drm_gtf2_hbreak(edid))
			return LEVEL_GTF2;
		return LEVEL_GTF;
	}
	return LEVEL_DMT;
}

/*
 * 0 is reserved.  The spec says 0x01 fill for unused timings.  Some old
 * monitors fill with ascii space (0x20) instead.
 */
static int
bad_std_timing(u8 a, u8 b)
{
	return (a == 0x00 && b == 0x00) ||
	       (a == 0x01 && b == 0x01) ||
	       (a == 0x20 && b == 0x20);
}

/**
 * drm_mode_std - convert standard mode info (width, height, refresh) into mode
 * @t: standard timing params
 * @timing_level: standard timing level
 *
 * Take the standard timing params (in this case width, aspect, and refresh)
 * and convert them into a real mode using CVT/GTF/DMT.
 */
static struct drm_display_mode *
drm_mode_std(struct drm_connector *connector, struct edid *edid,
	     struct std_timing *t, int revision)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *m, *mode = NULL;
	int hsize, vsize;
	int vrefresh_rate;
	unsigned aspect_ratio = (t->vfreq_aspect & EDID_TIMING_ASPECT_MASK)
		>> EDID_TIMING_ASPECT_SHIFT;
	unsigned vfreq = (t->vfreq_aspect & EDID_TIMING_VFREQ_MASK)
		>> EDID_TIMING_VFREQ_SHIFT;
	int timing_level = standard_timing_level(edid);

	if (bad_std_timing(t->hsize, t->vfreq_aspect))
		return NULL;

	/* According to the EDID spec, the hdisplay = hsize * 8 + 248 */
	hsize = t->hsize * 8 + 248;
	/* vrefresh_rate = vfreq + 60 */
	vrefresh_rate = vfreq + 60;
	/* the vdisplay is calculated based on the aspect ratio */
	if (aspect_ratio == 0) {
		if (revision < 3)
			vsize = hsize;
		else
			vsize = (hsize * 10) / 16;
	} else if (aspect_ratio == 1)
		vsize = (hsize * 3) / 4;
	else if (aspect_ratio == 2)
		vsize = (hsize * 4) / 5;
	else
		vsize = (hsize * 9) / 16;

	/* HDTV hack, part 1 */
	if (vrefresh_rate == 60 &&
	    ((hsize == 1360 && vsize == 765) ||
	     (hsize == 1368 && vsize == 769))) {
		hsize = 1366;
		vsize = 768;
	}

	/*
	 * If this connector already has a mode for this size and refresh
	 * rate (because it came from detailed or CVT info), use that
	 * instead.  This way we don't have to guess at interlace or
	 * reduced blanking.
	 */
	list_for_each_entry(m, &connector->probed_modes, head)
		if (m->hdisplay == hsize && m->vdisplay == vsize &&
		    drm_mode_vrefresh(m) == vrefresh_rate)
			return NULL;

	/* HDTV hack, part 2 */
	if (hsize == 1366 && vsize == 768 && vrefresh_rate == 60) {
		mode = drm_cvt_mode(dev, 1366, 768, vrefresh_rate, 0, 0,
				    false);
		mode->hdisplay = 1366;
		mode->hsync_start = mode->hsync_start - 1;
		mode->hsync_end = mode->hsync_end - 1;
		return mode;
	}

	/* check whether it can be found in default mode table */
	if (drm_monitor_supports_rb(edid)) {
		mode = drm_mode_find_dmt(dev, hsize, vsize, vrefresh_rate,
					 true);
		if (mode)
			return mode;
	}
	mode = drm_mode_find_dmt(dev, hsize, vsize, vrefresh_rate, false);
	if (mode)
		return mode;

	/* okay, generate it */
	switch (timing_level) {
	case LEVEL_DMT:
		break;
	case LEVEL_GTF:
		mode = drm_gtf_mode(dev, hsize, vsize, vrefresh_rate, 0, 0);
		break;
	case LEVEL_GTF2:
		/*
		 * This is potentially wrong if there's ever a monitor with
		 * more than one ranges section, each claiming a different
		 * secondary GTF curve.  Please don't do that.
		 */
		mode = drm_gtf_mode(dev, hsize, vsize, vrefresh_rate, 0, 0);
		if (!mode)
			return NULL;
		if (drm_mode_hsync(mode) > drm_gtf2_hbreak(edid)) {
			drm_mode_destroy(dev, mode);
			mode = drm_gtf_mode_complex(dev, hsize, vsize,
						    vrefresh_rate, 0, 0,
						    drm_gtf2_m(edid),
						    drm_gtf2_2c(edid),
						    drm_gtf2_k(edid),
						    drm_gtf2_2j(edid));
		}
		break;
	case LEVEL_CVT:
		mode = drm_cvt_mode(dev, hsize, vsize, vrefresh_rate, 0, 0,
				    false);
		break;
	}
	return mode;
}

/*
 * EDID is delightfully ambiguous about how interlaced modes are to be
 * encoded.  Our internal representation is of frame height, but some
 * HDTV detailed timings are encoded as field height.
 *
 * The format list here is from CEA, in frame size.  Technically we
 * should be checking refresh rate too.  Whatever.
 */
static void
drm_mode_do_interlace_quirk(struct drm_display_mode *mode,
			    struct detailed_pixel_timing *pt)
{
	int i;
	static const struct {
		int w, h;
	} cea_interlaced[] = {
		{ 1920, 1080 },
		{  720,  480 },
		{ 1440,  480 },
		{ 2880,  480 },
		{  720,  576 },
		{ 1440,  576 },
		{ 2880,  576 },
	};

	if (!(pt->misc & DRM_EDID_PT_INTERLACED))
		return;

	for (i = 0; i < ARRAY_SIZE(cea_interlaced); i++) {
		if ((mode->hdisplay == cea_interlaced[i].w) &&
		    (mode->vdisplay == cea_interlaced[i].h / 2)) {
			mode->vdisplay *= 2;
			mode->vsync_start *= 2;
			mode->vsync_end *= 2;
			mode->vtotal *= 2;
			mode->vtotal |= 1;
		}
	}

	mode->flags |= DRM_MODE_FLAG_INTERLACE;
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
	unsigned hactive = (pt->hactive_hblank_hi & 0xf0) << 4 | pt->hactive_lo;
	unsigned vactive = (pt->vactive_vblank_hi & 0xf0) << 4 | pt->vactive_lo;
	unsigned hblank = (pt->hactive_hblank_hi & 0xf) << 8 | pt->hblank_lo;
	unsigned vblank = (pt->vactive_vblank_hi & 0xf) << 8 | pt->vblank_lo;
	unsigned hsync_offset = (pt->hsync_vsync_offset_pulse_width_hi & 0xc0) << 2 | pt->hsync_offset_lo;
	unsigned hsync_pulse_width = (pt->hsync_vsync_offset_pulse_width_hi & 0x30) << 4 | pt->hsync_pulse_width_lo;
	unsigned vsync_offset = (pt->hsync_vsync_offset_pulse_width_hi & 0xc) >> 2 | pt->vsync_offset_pulse_width_lo >> 4;
	unsigned vsync_pulse_width = (pt->hsync_vsync_offset_pulse_width_hi & 0x3) << 4 | (pt->vsync_offset_pulse_width_lo & 0xf);

	/* ignore tiny modes */
	if (hactive < 64 || vactive < 64)
		return NULL;

	if (pt->misc & DRM_EDID_PT_STEREO) {
		printk(KERN_WARNING "stereo mode not supported\n");
		return NULL;
	}
	if (!(pt->misc & DRM_EDID_PT_SEPARATE_SYNC)) {
		printk(KERN_WARNING "composite sync not supported\n");
	}

	/* it is incorrect if hsync/vsync width is zero */
	if (!hsync_pulse_width || !vsync_pulse_width) {
		DRM_DEBUG_KMS("Incorrect Detailed timing. "
				"Wrong Hsync/Vsync pulse width\n");
		return NULL;
	}

	if (quirks & EDID_QUIRK_FORCE_REDUCED_BLANKING) {
		mode = drm_cvt_mode(dev, hactive, vactive, 60, true, false, false);
		if (!mode)
			return NULL;

		goto set_size;
	}

	mode = drm_mode_create(dev);
	if (!mode)
		return NULL;

	if (quirks & EDID_QUIRK_135_CLOCK_TOO_HIGH)
		timing->pixel_clock = cpu_to_le16(1088);

	mode->clock = le16_to_cpu(timing->pixel_clock) * 10;

	mode->hdisplay = hactive;
	mode->hsync_start = mode->hdisplay + hsync_offset;
	mode->hsync_end = mode->hsync_start + hsync_pulse_width;
	mode->htotal = mode->hdisplay + hblank;

	mode->vdisplay = vactive;
	mode->vsync_start = mode->vdisplay + vsync_offset;
	mode->vsync_end = mode->vsync_start + vsync_pulse_width;
	mode->vtotal = mode->vdisplay + vblank;

	/* Some EDIDs have bogus h/vtotal values */
	if (mode->hsync_end > mode->htotal)
		mode->htotal = mode->hsync_end + 1;
	if (mode->vsync_end > mode->vtotal)
		mode->vtotal = mode->vsync_end + 1;

	drm_mode_do_interlace_quirk(mode, pt);

	if (quirks & EDID_QUIRK_DETAILED_SYNC_PP) {
		pt->misc |= DRM_EDID_PT_HSYNC_POSITIVE | DRM_EDID_PT_VSYNC_POSITIVE;
	}

	mode->flags |= (pt->misc & DRM_EDID_PT_HSYNC_POSITIVE) ?
		DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC;
	mode->flags |= (pt->misc & DRM_EDID_PT_VSYNC_POSITIVE) ?
		DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC;

set_size:
	mode->width_mm = pt->width_mm_lo | (pt->width_height_mm_hi & 0xf0) << 4;
	mode->height_mm = pt->height_mm_lo | (pt->width_height_mm_hi & 0xf) << 8;

	if (quirks & EDID_QUIRK_DETAILED_IN_CM) {
		mode->width_mm *= 10;
		mode->height_mm *= 10;
	}

	if (quirks & EDID_QUIRK_DETAILED_USE_MAXIMUM_SIZE) {
		mode->width_mm = edid->width_cm * 10;
		mode->height_mm = edid->height_cm * 10;
	}

	mode->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_set_name(mode);

	return mode;
}

static bool
mode_in_hsync_range(const struct drm_display_mode *mode,
		    struct edid *edid, u8 *t)
{
	int hsync, hmin, hmax;

	hmin = t[7];
	if (edid->revision >= 4)
	    hmin += ((t[4] & 0x04) ? 255 : 0);
	hmax = t[8];
	if (edid->revision >= 4)
	    hmax += ((t[4] & 0x08) ? 255 : 0);
	hsync = drm_mode_hsync(mode);

	return (hsync <= hmax && hsync >= hmin);
}

static bool
mode_in_vsync_range(const struct drm_display_mode *mode,
		    struct edid *edid, u8 *t)
{
	int vsync, vmin, vmax;

	vmin = t[5];
	if (edid->revision >= 4)
	    vmin += ((t[4] & 0x01) ? 255 : 0);
	vmax = t[6];
	if (edid->revision >= 4)
	    vmax += ((t[4] & 0x02) ? 255 : 0);
	vsync = drm_mode_vrefresh(mode);

	return (vsync <= vmax && vsync >= vmin);
}

static u32
range_pixel_clock(struct edid *edid, u8 *t)
{
	/* unspecified */
	if (t[9] == 0 || t[9] == 255)
		return 0;

	/* 1.4 with CVT support gives us real precision, yay */
	if (edid->revision >= 4 && t[10] == 0x04)
		return (t[9] * 10000) - ((t[12] >> 2) * 250);

	/* 1.3 is pathetic, so fuzz up a bit */
	return t[9] * 10000 + 5001;
}

static bool
mode_in_range(const struct drm_display_mode *mode, struct edid *edid,
	      struct detailed_timing *timing)
{
	u32 max_clock;
	u8 *t = (u8 *)timing;

	if (!mode_in_hsync_range(mode, edid, t))
		return false;

	if (!mode_in_vsync_range(mode, edid, t))
		return false;

	if ((max_clock = range_pixel_clock(edid, t)))
		if (mode->clock > max_clock)
			return false;

	/* 1.4 max horizontal check */
	if (edid->revision >= 4 && t[10] == 0x04)
		if (t[13] && mode->hdisplay > 8 * (t[13] + (256 * (t[12]&0x3))))
			return false;

	if (mode_is_rb(mode) && !drm_monitor_supports_rb(edid))
		return false;

	return true;
}

static bool valid_inferred_mode(const struct drm_connector *connector,
				const struct drm_display_mode *mode)
{
	struct drm_display_mode *m;
	bool ok = false;

	list_for_each_entry(m, &connector->probed_modes, head) {
		if (mode->hdisplay == m->hdisplay &&
		    mode->vdisplay == m->vdisplay &&
		    drm_mode_vrefresh(mode) == drm_mode_vrefresh(m))
			return false; /* duplicated */
		if (mode->hdisplay <= m->hdisplay &&
		    mode->vdisplay <= m->vdisplay)
			ok = true;
	}
	return ok;
}

static int
drm_dmt_modes_for_range(struct drm_connector *connector, struct edid *edid,
			struct detailed_timing *timing)
{
	int i, modes = 0;
	struct drm_display_mode *newmode;
	struct drm_device *dev = connector->dev;

	for (i = 0; i < drm_num_dmt_modes; i++) {
		if (mode_in_range(drm_dmt_modes + i, edid, timing) &&
		    valid_inferred_mode(connector, drm_dmt_modes + i)) {
			newmode = drm_mode_duplicate(dev, &drm_dmt_modes[i]);
			if (newmode) {
				drm_mode_probed_add(connector, newmode);
				modes++;
			}
		}
	}

	return modes;
}

/* fix up 1366x768 mode from 1368x768;
 * GFT/CVT can't express 1366 width which isn't dividable by 8
 */
static void fixup_mode_1366x768(struct drm_display_mode *mode)
{
	if (mode->hdisplay == 1368 && mode->vdisplay == 768) {
		mode->hdisplay = 1366;
		mode->hsync_start--;
		mode->hsync_end--;
		drm_mode_set_name(mode);
	}
}

static int
drm_gtf_modes_for_range(struct drm_connector *connector, struct edid *edid,
			struct detailed_timing *timing)
{
	int i, modes = 0;
	struct drm_display_mode *newmode;
	struct drm_device *dev = connector->dev;

	for (i = 0; i < num_extra_modes; i++) {
		const struct minimode *m = &extra_modes[i];
		newmode = drm_gtf_mode(dev, m->w, m->h, m->r, 0, 0);
		if (!newmode)
			return modes;

		fixup_mode_1366x768(newmode);
		if (!mode_in_range(newmode, edid, timing) ||
		    !valid_inferred_mode(connector, newmode)) {
			drm_mode_destroy(dev, newmode);
			continue;
		}

		drm_mode_probed_add(connector, newmode);
		modes++;
	}

	return modes;
}

static int
drm_cvt_modes_for_range(struct drm_connector *connector, struct edid *edid,
			struct detailed_timing *timing)
{
	int i, modes = 0;
	struct drm_display_mode *newmode;
	struct drm_device *dev = connector->dev;
	bool rb = drm_monitor_supports_rb(edid);

	for (i = 0; i < num_extra_modes; i++) {
		const struct minimode *m = &extra_modes[i];
		newmode = drm_cvt_mode(dev, m->w, m->h, m->r, rb, 0, 0);
		if (!newmode)
			return modes;

		fixup_mode_1366x768(newmode);
		if (!mode_in_range(newmode, edid, timing) ||
		    !valid_inferred_mode(connector, newmode)) {
			drm_mode_destroy(dev, newmode);
			continue;
		}

		drm_mode_probed_add(connector, newmode);
		modes++;
	}

	return modes;
}

static void
do_inferred_modes(struct detailed_timing *timing, void *c)
{
	struct detailed_mode_closure *closure = c;
	struct detailed_non_pixel *data = &timing->data.other_data;
	struct detailed_data_monitor_range *range = &data->data.range;

	if (data->type != EDID_DETAIL_MONITOR_RANGE)
		return;

	closure->modes += drm_dmt_modes_for_range(closure->connector,
						  closure->edid,
						  timing);
	
	if (!version_greater(closure->edid, 1, 1))
		return; /* GTF not defined yet */

	switch (range->flags) {
	case 0x02: /* secondary gtf, XXX could do more */
	case 0x00: /* default gtf */
		closure->modes += drm_gtf_modes_for_range(closure->connector,
							  closure->edid,
							  timing);
		break;
	case 0x04: /* cvt, only in 1.4+ */
		if (!version_greater(closure->edid, 1, 3))
			break;

		closure->modes += drm_cvt_modes_for_range(closure->connector,
							  closure->edid,
							  timing);
		break;
	case 0x01: /* just the ranges, no formula */
	default:
		break;
	}
}

static int
add_inferred_modes(struct drm_connector *connector, struct edid *edid)
{
	struct detailed_mode_closure closure = {
		connector, edid, 0, 0, 0
	};

	if (version_greater(edid, 1, 0))
		drm_for_each_detailed_block((u8 *)edid, do_inferred_modes,
					    &closure);

	return closure.modes;
}

static int
drm_est3_modes(struct drm_connector *connector, struct detailed_timing *timing)
{
	int i, j, m, modes = 0;
	struct drm_display_mode *mode;
	u8 *est = ((u8 *)timing) + 5;

	for (i = 0; i < 6; i++) {
		for (j = 7; j > 0; j--) {
			m = (i * 8) + (7 - j);
			if (m >= ARRAY_SIZE(est3_modes))
				break;
			if (est[i] & (1 << j)) {
				mode = drm_mode_find_dmt(connector->dev,
							 est3_modes[m].w,
							 est3_modes[m].h,
							 est3_modes[m].r,
							 est3_modes[m].rb);
				if (mode) {
					drm_mode_probed_add(connector, mode);
					modes++;
				}
			}
		}
	}

	return modes;
}

static void
do_established_modes(struct detailed_timing *timing, void *c)
{
	struct detailed_mode_closure *closure = c;
	struct detailed_non_pixel *data = &timing->data.other_data;

	if (data->type == EDID_DETAIL_EST_TIMINGS)
		closure->modes += drm_est3_modes(closure->connector, timing);
}

/**
 * add_established_modes - get est. modes from EDID and add them
 * @edid: EDID block to scan
 *
 * Each EDID block contains a bitmap of the supported "established modes" list
 * (defined above).  Tease them out and add them to the global modes list.
 */
static int
add_established_modes(struct drm_connector *connector, struct edid *edid)
{
	struct drm_device *dev = connector->dev;
	unsigned long est_bits = edid->established_timings.t1 |
		(edid->established_timings.t2 << 8) |
		((edid->established_timings.mfg_rsvd & 0x80) << 9);
	int i, modes = 0;
	struct detailed_mode_closure closure = {
		connector, edid, 0, 0, 0
	};

	for (i = 0; i <= EDID_EST_TIMINGS; i++) {
		if (est_bits & (1<<i)) {
			struct drm_display_mode *newmode;
			newmode = drm_mode_duplicate(dev, &edid_est_modes[i]);
			if (newmode) {
				drm_mode_probed_add(connector, newmode);
				modes++;
			}
		}
	}

	if (version_greater(edid, 1, 0))
		    drm_for_each_detailed_block((u8 *)edid,
						do_established_modes, &closure);

	return modes + closure.modes;
}

static void
do_standard_modes(struct detailed_timing *timing, void *c)
{
	struct detailed_mode_closure *closure = c;
	struct detailed_non_pixel *data = &timing->data.other_data;
	struct drm_connector *connector = closure->connector;
	struct edid *edid = closure->edid;

	if (data->type == EDID_DETAIL_STD_MODES) {
		int i;
		for (i = 0; i < 6; i++) {
			struct std_timing *std;
			struct drm_display_mode *newmode;

			std = &data->data.timings[i];
			newmode = drm_mode_std(connector, edid, std,
					       edid->revision);
			if (newmode) {
				drm_mode_probed_add(connector, newmode);
				closure->modes++;
			}
		}
	}
}

/**
 * add_standard_modes - get std. modes from EDID and add them
 * @edid: EDID block to scan
 *
 * Standard modes can be calculated using the appropriate standard (DMT,
 * GTF or CVT. Grab them from @edid and add them to the list.
 */
static int
add_standard_modes(struct drm_connector *connector, struct edid *edid)
{
	int i, modes = 0;
	struct detailed_mode_closure closure = {
		connector, edid, 0, 0, 0
	};

	for (i = 0; i < EDID_STD_TIMINGS; i++) {
		struct drm_display_mode *newmode;

		newmode = drm_mode_std(connector, edid,
				       &edid->standard_timings[i],
				       edid->revision);
		if (newmode) {
			drm_mode_probed_add(connector, newmode);
			modes++;
		}
	}

	if (version_greater(edid, 1, 0))
		drm_for_each_detailed_block((u8 *)edid, do_standard_modes,
					    &closure);

	/* XXX should also look for standard codes in VTB blocks */

	return modes + closure.modes;
}

static int drm_cvt_modes(struct drm_connector *connector,
			 struct detailed_timing *timing)
{
	int i, j, modes = 0;
	struct drm_display_mode *newmode;
	struct drm_device *dev = connector->dev;
	struct cvt_timing *cvt;
	const int rates[] = { 60, 85, 75, 60, 50 };
	const u8 empty[3] = { 0, 0, 0 };

	for (i = 0; i < 4; i++) {
		int uninitialized_var(width), height;
		cvt = &(timing->data.other_data.data.cvt[i]);

		if (!memcmp(cvt->code, empty, 3))
			continue;

		height = (cvt->code[0] + ((cvt->code[1] & 0xf0) << 4) + 1) * 2;
		switch (cvt->code[1] & 0x0c) {
		case 0x00:
			width = height * 4 / 3;
			break;
		case 0x04:
			width = height * 16 / 9;
			break;
		case 0x08:
			width = height * 16 / 10;
			break;
		case 0x0c:
			width = height * 15 / 9;
			break;
		}

		for (j = 1; j < 5; j++) {
			if (cvt->code[2] & (1 << j)) {
				newmode = drm_cvt_mode(dev, width, height,
						       rates[j], j == 0,
						       false, false);
				if (newmode) {
					drm_mode_probed_add(connector, newmode);
					modes++;
				}
			}
		}
	}

	return modes;
}

static void
do_cvt_mode(struct detailed_timing *timing, void *c)
{
	struct detailed_mode_closure *closure = c;
	struct detailed_non_pixel *data = &timing->data.other_data;

	if (data->type == EDID_DETAIL_CVT_3BYTE)
		closure->modes += drm_cvt_modes(closure->connector, timing);
}

static int
add_cvt_modes(struct drm_connector *connector, struct edid *edid)
{	
	struct detailed_mode_closure closure = {
		connector, edid, 0, 0, 0
	};

	if (version_greater(edid, 1, 2))
		drm_for_each_detailed_block((u8 *)edid, do_cvt_mode, &closure);

	/* XXX should also look for CVT codes in VTB blocks */

	return closure.modes;
}

static void
do_detailed_mode(struct detailed_timing *timing, void *c)
{
	struct detailed_mode_closure *closure = c;
	struct drm_display_mode *newmode;

	if (timing->pixel_clock) {
		newmode = drm_mode_detailed(closure->connector->dev,
					    closure->edid, timing,
					    closure->quirks);
		if (!newmode)
			return;

		if (closure->preferred)
			newmode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_probed_add(closure->connector, newmode);
		closure->modes++;
		closure->preferred = 0;
	}
}

/*
 * add_detailed_modes - Add modes from detailed timings
 * @connector: attached connector
 * @edid: EDID block to scan
 * @quirks: quirks to apply
 */
static int
add_detailed_modes(struct drm_connector *connector, struct edid *edid,
		   u32 quirks)
{
	struct detailed_mode_closure closure = {
		connector,
		edid,
		1,
		quirks,
		0
	};

	if (closure.preferred && !version_greater(edid, 1, 3))
		closure.preferred =
		    (edid->features & DRM_EDID_FEATURE_PREFERRED_TIMING);

	drm_for_each_detailed_block((u8 *)edid, do_detailed_mode, &closure);

	return closure.modes;
}

#define HDMI_IDENTIFIER 0x000C03
#define AUDIO_BLOCK	0x01
#define VIDEO_BLOCK     0x02
#define VENDOR_BLOCK    0x03
#define SPEAKER_BLOCK	0x04
#define VIDEO_CAPABILITY_BLOCK	0x07
#define EDID_BASIC_AUDIO	(1 << 6)
#define EDID_CEA_YCRCB444	(1 << 5)
#define EDID_CEA_YCRCB422	(1 << 4)
#define EDID_CEA_VCDB_QS	(1 << 6)

/**
 * Search EDID for CEA extension block.
 */
u8 *drm_find_cea_extension(struct edid *edid)
{
	u8 *edid_ext = NULL;
	int i;

	/* No EDID or EDID extensions */
	if (edid == NULL || edid->extensions == 0)
		return NULL;

	/* Find CEA extension */
	for (i = 0; i < edid->extensions; i++) {
		edid_ext = (u8 *)edid + EDID_LENGTH * (i + 1);
		if (edid_ext[0] == CEA_EXT)
			break;
	}

	if (i == edid->extensions)
		return NULL;

	return edid_ext;
}
EXPORT_SYMBOL(drm_find_cea_extension);

/*
 * Looks for a CEA mode matching given drm_display_mode.
 * Returns its CEA Video ID code, or 0 if not found.
 */
u8 drm_match_cea_mode(struct drm_display_mode *to_match)
{
	struct drm_display_mode *cea_mode;
	u8 mode;

	for (mode = 0; mode < drm_num_cea_modes; mode++) {
		cea_mode = (struct drm_display_mode *)&edid_cea_modes[mode];

		if (drm_mode_equal(to_match, cea_mode))
			return mode + 1;
	}
	return 0;
}
EXPORT_SYMBOL(drm_match_cea_mode);


static int
do_cea_modes (struct drm_connector *connector, u8 *db, u8 len)
{
	struct drm_device *dev = connector->dev;
	u8 * mode, cea_mode;
	int modes = 0;

	for (mode = db; mode < db + len; mode++) {
		cea_mode = (*mode & 127) - 1; /* CEA modes are numbered 1..127 */
		if (cea_mode < drm_num_cea_modes) {
			struct drm_display_mode *newmode;
			newmode = drm_mode_duplicate(dev,
						     &edid_cea_modes[cea_mode]);
			if (newmode) {
				drm_mode_probed_add(connector, newmode);
				modes++;
			}
		}
	}

	return modes;
}

static int
cea_db_payload_len(const u8 *db)
{
	return db[0] & 0x1f;
}

static int
cea_db_tag(const u8 *db)
{
	return db[0] >> 5;
}

static int
cea_revision(const u8 *cea)
{
	return cea[1];
}

static int
cea_db_offsets(const u8 *cea, int *start, int *end)
{
	/* Data block offset in CEA extension block */
	*start = 4;
	*end = cea[2];
	if (*end == 0)
		*end = 127;
	if (*end < 4 || *end > 127)
		return -ERANGE;
	return 0;
}

#define for_each_cea_db(cea, i, start, end) \
	for ((i) = (start); (i) < (end) && (i) + cea_db_payload_len(&(cea)[(i)]) < (end); (i) += cea_db_payload_len(&(cea)[(i)]) + 1)

static int
add_cea_modes(struct drm_connector *connector, struct edid *edid)
{
	u8 * cea = drm_find_cea_extension(edid);
	u8 * db, dbl;
	int modes = 0;

	if (cea && cea_revision(cea) >= 3) {
		int i, start, end;

		if (cea_db_offsets(cea, &start, &end))
			return 0;

		for_each_cea_db(cea, i, start, end) {
			db = &cea[i];
			dbl = cea_db_payload_len(db);

			if (cea_db_tag(db) == VIDEO_BLOCK)
				modes += do_cea_modes (connector, db+1, dbl);
		}
	}

	return modes;
}

static void
parse_hdmi_vsdb(struct drm_connector *connector, const u8 *db)
{
	u8 len = cea_db_payload_len(db);

	if (len >= 6) {
		connector->eld[5] |= (db[6] >> 7) << 1;  /* Supports_AI */
		connector->dvi_dual = db[6] & 1;
	}
	if (len >= 7)
		connector->max_tmds_clock = db[7] * 5;
	if (len >= 8) {
		connector->latency_present[0] = db[8] >> 7;
		connector->latency_present[1] = (db[8] >> 6) & 1;
	}
	if (len >= 9)
		connector->video_latency[0] = db[9];
	if (len >= 10)
		connector->audio_latency[0] = db[10];
	if (len >= 11)
		connector->video_latency[1] = db[11];
	if (len >= 12)
		connector->audio_latency[1] = db[12];

	DRM_DEBUG_KMS("HDMI: DVI dual %d, "
		    "max TMDS clock %d, "
		    "latency present %d %d, "
		    "video latency %d %d, "
		    "audio latency %d %d\n",
		    connector->dvi_dual,
		    connector->max_tmds_clock,
	      (int) connector->latency_present[0],
	      (int) connector->latency_present[1],
		    connector->video_latency[0],
		    connector->video_latency[1],
		    connector->audio_latency[0],
		    connector->audio_latency[1]);
}

static void
monitor_name(struct detailed_timing *t, void *data)
{
	if (t->data.other_data.type == EDID_DETAIL_MONITOR_NAME)
		*(u8 **)data = t->data.other_data.data.str.str;
}

static bool cea_db_is_hdmi_vsdb(const u8 *db)
{
	int hdmi_id;

	if (cea_db_tag(db) != VENDOR_BLOCK)
		return false;

	if (cea_db_payload_len(db) < 5)
		return false;

	hdmi_id = db[1] | (db[2] << 8) | (db[3] << 16);

	return hdmi_id == HDMI_IDENTIFIER;
}

/**
 * drm_edid_to_eld - build ELD from EDID
 * @connector: connector corresponding to the HDMI/DP sink
 * @edid: EDID to parse
 *
 * Fill the ELD (EDID-Like Data) buffer for passing to the audio driver.
 * Some ELD fields are left to the graphics driver caller:
 * - Conn_Type
 * - HDCP
 * - Port_ID
 */
void drm_edid_to_eld(struct drm_connector *connector, struct edid *edid)
{
	uint8_t *eld = connector->eld;
	u8 *cea;
	u8 *name;
	u8 *db;
	int sad_count = 0;
	int mnl;
	int dbl;

	memset(eld, 0, sizeof(connector->eld));

	cea = drm_find_cea_extension(edid);
	if (!cea) {
		DRM_DEBUG_KMS("ELD: no CEA Extension found\n");
		return;
	}

	name = NULL;
	drm_for_each_detailed_block((u8 *)edid, monitor_name, &name);
	for (mnl = 0; name && mnl < 13; mnl++) {
		if (name[mnl] == 0x0a)
			break;
		eld[20 + mnl] = name[mnl];
	}
	eld[4] = (cea[1] << 5) | mnl;
	DRM_DEBUG_KMS("ELD monitor %s\n", eld + 20);

	eld[0] = 2 << 3;		/* ELD version: 2 */

	eld[16] = edid->mfg_id[0];
	eld[17] = edid->mfg_id[1];
	eld[18] = edid->prod_code[0];
	eld[19] = edid->prod_code[1];

	if (cea_revision(cea) >= 3) {
		int i, start, end;

		if (cea_db_offsets(cea, &start, &end)) {
			start = 0;
			end = 0;
		}

		for_each_cea_db(cea, i, start, end) {
			db = &cea[i];
			dbl = cea_db_payload_len(db);

			switch (cea_db_tag(db)) {
			case AUDIO_BLOCK:
				/* Audio Data Block, contains SADs */
				sad_count = dbl / 3;
				if (dbl >= 1)
					memcpy(eld + 20 + mnl, &db[1], dbl);
				break;
			case SPEAKER_BLOCK:
				/* Speaker Allocation Data Block */
				if (dbl >= 1)
					eld[7] = db[1];
				break;
			case VENDOR_BLOCK:
				/* HDMI Vendor-Specific Data Block */
				if (cea_db_is_hdmi_vsdb(db))
					parse_hdmi_vsdb(connector, db);
				break;
			default:
				break;
			}
		}
	}
	eld[5] |= sad_count << 4;
	eld[2] = (20 + mnl + sad_count * 3 + 3) / 4;

	DRM_DEBUG_KMS("ELD size %d, SAD count %d\n", (int)eld[2], sad_count);
}
EXPORT_SYMBOL(drm_edid_to_eld);

/**
 * drm_av_sync_delay - HDMI/DP sink audio-video sync delay in millisecond
 * @connector: connector associated with the HDMI/DP sink
 * @mode: the display mode
 */
int drm_av_sync_delay(struct drm_connector *connector,
		      struct drm_display_mode *mode)
{
	int i = !!(mode->flags & DRM_MODE_FLAG_INTERLACE);
	int a, v;

	if (!connector->latency_present[0])
		return 0;
	if (!connector->latency_present[1])
		i = 0;

	a = connector->audio_latency[i];
	v = connector->video_latency[i];

	/*
	 * HDMI/DP sink doesn't support audio or video?
	 */
	if (a == 255 || v == 255)
		return 0;

	/*
	 * Convert raw EDID values to millisecond.
	 * Treat unknown latency as 0ms.
	 */
	if (a)
		a = min(2 * (a - 1), 500);
	if (v)
		v = min(2 * (v - 1), 500);

	return max(v - a, 0);
}
EXPORT_SYMBOL(drm_av_sync_delay);

/**
 * drm_select_eld - select one ELD from multiple HDMI/DP sinks
 * @encoder: the encoder just changed display mode
 * @mode: the adjusted display mode
 *
 * It's possible for one encoder to be associated with multiple HDMI/DP sinks.
 * The policy is now hard coded to simply use the first HDMI/DP sink's ELD.
 */
struct drm_connector *drm_select_eld(struct drm_encoder *encoder,
				     struct drm_display_mode *mode)
{
	struct drm_connector *connector;
	struct drm_device *dev = encoder->dev;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		if (connector->encoder == encoder && connector->eld[0])
			return connector;

	return NULL;
}
EXPORT_SYMBOL(drm_select_eld);

/**
 * drm_detect_hdmi_monitor - detect whether monitor is hdmi.
 * @edid: monitor EDID information
 *
 * Parse the CEA extension according to CEA-861-B.
 * Return true if HDMI, false if not or unknown.
 */
bool drm_detect_hdmi_monitor(struct edid *edid)
{
	u8 *edid_ext;
	int i;
	int start_offset, end_offset;

	edid_ext = drm_find_cea_extension(edid);
	if (!edid_ext)
		return false;

	if (cea_db_offsets(edid_ext, &start_offset, &end_offset))
		return false;

	/*
	 * Because HDMI identifier is in Vendor Specific Block,
	 * search it from all data blocks of CEA extension.
	 */
	for_each_cea_db(edid_ext, i, start_offset, end_offset) {
		if (cea_db_is_hdmi_vsdb(&edid_ext[i]))
			return true;
	}

	return false;
}
EXPORT_SYMBOL(drm_detect_hdmi_monitor);

/**
 * drm_detect_monitor_audio - check monitor audio capability
 *
 * Monitor should have CEA extension block.
 * If monitor has 'basic audio', but no CEA audio blocks, it's 'basic
 * audio' only. If there is any audio extension block and supported
 * audio format, assume at least 'basic audio' support, even if 'basic
 * audio' is not defined in EDID.
 *
 */
bool drm_detect_monitor_audio(struct edid *edid)
{
	u8 *edid_ext;
	int i, j;
	bool has_audio = false;
	int start_offset, end_offset;

	edid_ext = drm_find_cea_extension(edid);
	if (!edid_ext)
		goto end;

	has_audio = ((edid_ext[3] & EDID_BASIC_AUDIO) != 0);

	if (has_audio) {
		DRM_DEBUG_KMS("Monitor has basic audio support\n");
		goto end;
	}

	if (cea_db_offsets(edid_ext, &start_offset, &end_offset))
		goto end;

	for_each_cea_db(edid_ext, i, start_offset, end_offset) {
		if (cea_db_tag(&edid_ext[i]) == AUDIO_BLOCK) {
			has_audio = true;
			for (j = 1; j < cea_db_payload_len(&edid_ext[i]) + 1; j += 3)
				DRM_DEBUG_KMS("CEA audio format %d\n",
					      (edid_ext[i + j] >> 3) & 0xf);
			goto end;
		}
	}
end:
	return has_audio;
}
EXPORT_SYMBOL(drm_detect_monitor_audio);

/**
 * drm_rgb_quant_range_selectable - is RGB quantization range selectable?
 *
 * Check whether the monitor reports the RGB quantization range selection
 * as supported. The AVI infoframe can then be used to inform the monitor
 * which quantization range (full or limited) is used.
 */
bool drm_rgb_quant_range_selectable(struct edid *edid)
{
	u8 *edid_ext;
	int i, start, end;

	edid_ext = drm_find_cea_extension(edid);
	if (!edid_ext)
		return false;

	if (cea_db_offsets(edid_ext, &start, &end))
		return false;

	for_each_cea_db(edid_ext, i, start, end) {
		if (cea_db_tag(&edid_ext[i]) == VIDEO_CAPABILITY_BLOCK &&
		    cea_db_payload_len(&edid_ext[i]) == 2) {
			DRM_DEBUG_KMS("CEA VCDB 0x%02x\n", edid_ext[i + 2]);
			return edid_ext[i + 2] & EDID_CEA_VCDB_QS;
		}
	}

	return false;
}
EXPORT_SYMBOL(drm_rgb_quant_range_selectable);

/**
 * drm_add_display_info - pull display info out if present
 * @edid: EDID data
 * @info: display info (attached to connector)
 *
 * Grab any available display info and stuff it into the drm_display_info
 * structure that's part of the connector.  Useful for tracking bpp and
 * color spaces.
 */
static void drm_add_display_info(struct edid *edid,
				 struct drm_display_info *info)
{
	u8 *edid_ext;

	info->width_mm = edid->width_cm * 10;
	info->height_mm = edid->height_cm * 10;

	/* driver figures it out in this case */
	info->bpc = 0;
	info->color_formats = 0;

	if (edid->revision < 3)
		return;

	if (!(edid->input & DRM_EDID_INPUT_DIGITAL))
		return;

	/* Get data from CEA blocks if present */
	edid_ext = drm_find_cea_extension(edid);
	if (edid_ext) {
		info->cea_rev = edid_ext[1];

		/* The existence of a CEA block should imply RGB support */
		info->color_formats = DRM_COLOR_FORMAT_RGB444;
		if (edid_ext[3] & EDID_CEA_YCRCB444)
			info->color_formats |= DRM_COLOR_FORMAT_YCRCB444;
		if (edid_ext[3] & EDID_CEA_YCRCB422)
			info->color_formats |= DRM_COLOR_FORMAT_YCRCB422;
	}

	/* Only defined for 1.4 with digital displays */
	if (edid->revision < 4)
		return;

	switch (edid->input & DRM_EDID_DIGITAL_DEPTH_MASK) {
	case DRM_EDID_DIGITAL_DEPTH_6:
		info->bpc = 6;
		break;
	case DRM_EDID_DIGITAL_DEPTH_8:
		info->bpc = 8;
		break;
	case DRM_EDID_DIGITAL_DEPTH_10:
		info->bpc = 10;
		break;
	case DRM_EDID_DIGITAL_DEPTH_12:
		info->bpc = 12;
		break;
	case DRM_EDID_DIGITAL_DEPTH_14:
		info->bpc = 14;
		break;
	case DRM_EDID_DIGITAL_DEPTH_16:
		info->bpc = 16;
		break;
	case DRM_EDID_DIGITAL_DEPTH_UNDEF:
	default:
		info->bpc = 0;
		break;
	}

	info->color_formats |= DRM_COLOR_FORMAT_RGB444;
	if (edid->features & DRM_EDID_FEATURE_RGB_YCRCB444)
		info->color_formats |= DRM_COLOR_FORMAT_YCRCB444;
	if (edid->features & DRM_EDID_FEATURE_RGB_YCRCB422)
		info->color_formats |= DRM_COLOR_FORMAT_YCRCB422;
}

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
	if (!drm_edid_is_valid(edid)) {
		dev_warn(connector->dev->dev, "%s: EDID invalid.\n",
			 drm_get_connector_name(connector));
		return 0;
	}

	quirks = edid_get_quirks(edid);

	/*
	 * EDID spec says modes should be preferred in this order:
	 * - preferred detailed mode
	 * - other detailed modes from base block
	 * - detailed modes from extension blocks
	 * - CVT 3-byte code modes
	 * - standard timing codes
	 * - established timing codes
	 * - modes inferred from GTF or CVT range information
	 *
	 * We get this pretty much right.
	 *
	 * XXX order for additional mode types in extension blocks?
	 */
	num_modes += add_detailed_modes(connector, edid, quirks);
	num_modes += add_cvt_modes(connector, edid);
	num_modes += add_standard_modes(connector, edid);
	num_modes += add_established_modes(connector, edid);
	num_modes += add_inferred_modes(connector, edid);
	num_modes += add_cea_modes(connector, edid);

	if (quirks & (EDID_QUIRK_PREFER_LARGE_60 | EDID_QUIRK_PREFER_LARGE_75))
		edid_fixup_preferred(connector, quirks);

	drm_add_display_info(edid, &connector->display_info);

	return num_modes;
}
EXPORT_SYMBOL(drm_add_edid_modes);

/**
 * drm_add_modes_noedid - add modes for the connectors without EDID
 * @connector: connector we're probing
 * @hdisplay: the horizontal display limit
 * @vdisplay: the vertical display limit
 *
 * Add the specified modes to the connector's mode list. Only when the
 * hdisplay/vdisplay is not beyond the given limit, it will be added.
 *
 * Return number of modes added or 0 if we couldn't find any.
 */
int drm_add_modes_noedid(struct drm_connector *connector,
			int hdisplay, int vdisplay)
{
	int i, count, num_modes = 0;
	struct drm_display_mode *mode;
	struct drm_device *dev = connector->dev;

	count = sizeof(drm_dmt_modes) / sizeof(struct drm_display_mode);
	if (hdisplay < 0)
		hdisplay = 0;
	if (vdisplay < 0)
		vdisplay = 0;

	for (i = 0; i < count; i++) {
		const struct drm_display_mode *ptr = &drm_dmt_modes[i];
		if (hdisplay && vdisplay) {
			/*
			 * Only when two are valid, they will be used to check
			 * whether the mode should be added to the mode list of
			 * the connector.
			 */
			if (ptr->hdisplay > hdisplay ||
					ptr->vdisplay > vdisplay)
				continue;
		}
		if (drm_mode_vrefresh(ptr) > 61)
			continue;
		mode = drm_mode_duplicate(dev, ptr);
		if (mode) {
			drm_mode_probed_add(connector, mode);
			num_modes++;
		}
	}
	return num_modes;
}
EXPORT_SYMBOL(drm_add_modes_noedid);

/**
 * drm_mode_cea_vic - return the CEA-861 VIC of a given mode
 * @mode: mode
 *
 * RETURNS:
 * The VIC number, 0 in case it's not a CEA-861 mode.
 */
uint8_t drm_mode_cea_vic(const struct drm_display_mode *mode)
{
	uint8_t i;

	for (i = 0; i < drm_num_cea_modes; i++)
		if (drm_mode_equal(mode, &edid_cea_modes[i]))
			return i + 1;

	return 0;
}
EXPORT_SYMBOL(drm_mode_cea_vic);
