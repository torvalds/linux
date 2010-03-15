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


#define LEVEL_DMT	0
#define LEVEL_GTF	1
#define LEVEL_CVT	2

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
static const u8 edid_header[] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
};

/**
 * drm_edid_is_valid - sanity check EDID data
 * @edid: EDID data
 *
 * Sanity check the EDID block by looking at the header, the version number
 * and the checksum.  Return 0 if the EDID doesn't check out, or 1 if it's
 * valid.
 */
bool drm_edid_is_valid(struct edid *edid)
{
	int i, score = 0;
	u8 csum = 0;
	u8 *raw_edid = (u8 *)edid;

	for (i = 0; i < sizeof(edid_header); i++)
		if (raw_edid[i] == edid_header[i])
			score++;

	if (score == 8) ;
	else if (score >= 6) {
		DRM_DEBUG("Fixing EDID header, your hardware may be failing\n");
		memcpy(raw_edid, edid_header, sizeof(edid_header));
	} else
		goto bad;

	for (i = 0; i < EDID_LENGTH; i++)
		csum += raw_edid[i];
	if (csum) {
		DRM_ERROR("EDID checksum is invalid, remainder is %d\n", csum);
		goto bad;
	}

	if (edid->version != 1) {
		DRM_ERROR("EDID has major version %d, instead of 1\n", edid->version);
		goto bad;
	}

	if (edid->revision > 4)
		DRM_DEBUG("EDID minor > 4, assuming backward compatibility\n");

	return 1;

bad:
	if (raw_edid) {
		DRM_ERROR("Raw EDID:\n");
		print_hex_dump_bytes(KERN_ERR, DUMP_PREFIX_NONE, raw_edid, EDID_LENGTH);
		printk("\n");
	}
	return 0;
}
EXPORT_SYMBOL(drm_edid_is_valid);

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

/*
 * Add the Autogenerated from the DMT spec.
 * This table is copied from xfree86/modes/xf86EdidModes.c.
 * But the mode with Reduced blank feature is deleted.
 */
static struct drm_display_mode drm_dmt_modes[] = {
	/* 640x350@85Hz */
	{ DRM_MODE("640x350", DRM_MODE_TYPE_DRIVER, 31500, 640, 672,
		   736, 832, 0, 350, 382, 385, 445, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 640x400@85Hz */
	{ DRM_MODE("640x400", DRM_MODE_TYPE_DRIVER, 31500, 640, 672,
		   736, 832, 0, 400, 401, 404, 445, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 720x400@85Hz */
	{ DRM_MODE("720x400", DRM_MODE_TYPE_DRIVER, 35500, 720, 756,
		   828, 936, 0, 400, 401, 404, 446, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 640x480@60Hz */
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 25175, 640, 656,
		   752, 800, 0, 480, 489, 492, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 640x480@72Hz */
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 31500, 640, 664,
		   704, 832, 0, 480, 489, 492, 520, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 640x480@75Hz */
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 31500, 640, 656,
		   720, 840, 0, 480, 481, 484, 500, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 640x480@85Hz */
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 36000, 640, 696,
		   752, 832, 0, 480, 481, 484, 509, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 800x600@56Hz */
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DRIVER, 36000, 800, 824,
		   896, 1024, 0, 600, 601, 603, 625, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 800x600@60Hz */
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DRIVER, 40000, 800, 840,
		   968, 1056, 0, 600, 601, 605, 628, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 800x600@72Hz */
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DRIVER, 50000, 800, 856,
		   976, 1040, 0, 600, 637, 643, 666, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 800x600@75Hz */
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DRIVER, 49500, 800, 816,
		   896, 1056, 0, 600, 601, 604, 625, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 800x600@85Hz */
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DRIVER, 56250, 800, 832,
		   896, 1048, 0, 600, 601, 604, 631, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 848x480@60Hz */
	{ DRM_MODE("848x480", DRM_MODE_TYPE_DRIVER, 33750, 848, 864,
		   976, 1088, 0, 480, 486, 494, 517, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1024x768@43Hz, interlace */
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 44900, 1024, 1032,
		   1208, 1264, 0, 768, 768, 772, 817, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
			DRM_MODE_FLAG_INTERLACE) },
	/* 1024x768@60Hz */
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 65000, 1024, 1048,
		   1184, 1344, 0, 768, 771, 777, 806, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 1024x768@70Hz */
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 75000, 1024, 1048,
		   1184, 1328, 0, 768, 771, 777, 806, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 1024x768@75Hz */
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 78750, 1024, 1040,
		   1136, 1312, 0, 768, 769, 772, 800, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1024x768@85Hz */
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 94500, 1024, 1072,
		   1072, 1376, 0, 768, 769, 772, 808, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1152x864@75Hz */
	{ DRM_MODE("1152x864", DRM_MODE_TYPE_DRIVER, 108000, 1152, 1216,
		   1344, 1600, 0, 864, 865, 868, 900, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1280x768@60Hz */
	{ DRM_MODE("1280x768", DRM_MODE_TYPE_DRIVER, 79500, 1280, 1344,
		   1472, 1664, 0, 768, 771, 778, 798, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1280x768@75Hz */
	{ DRM_MODE("1280x768", DRM_MODE_TYPE_DRIVER, 102250, 1280, 1360,
		   1488, 1696, 0, 768, 771, 778, 805, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 1280x768@85Hz */
	{ DRM_MODE("1280x768", DRM_MODE_TYPE_DRIVER, 117500, 1280, 1360,
		   1496, 1712, 0, 768, 771, 778, 809, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1280x800@60Hz */
	{ DRM_MODE("1280x800", DRM_MODE_TYPE_DRIVER, 83500, 1280, 1352,
		   1480, 1680, 0, 800, 803, 809, 831, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 1280x800@75Hz */
	{ DRM_MODE("1280x800", DRM_MODE_TYPE_DRIVER, 106500, 1280, 1360,
		   1488, 1696, 0, 800, 803, 809, 838, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1280x800@85Hz */
	{ DRM_MODE("1280x800", DRM_MODE_TYPE_DRIVER, 122500, 1280, 1360,
		   1496, 1712, 0, 800, 803, 809, 843, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1280x960@60Hz */
	{ DRM_MODE("1280x960", DRM_MODE_TYPE_DRIVER, 108000, 1280, 1376,
		   1488, 1800, 0, 960, 961, 964, 1000, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1280x960@85Hz */
	{ DRM_MODE("1280x960", DRM_MODE_TYPE_DRIVER, 148500, 1280, 1344,
		   1504, 1728, 0, 960, 961, 964, 1011, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1280x1024@60Hz */
	{ DRM_MODE("1280x1024", DRM_MODE_TYPE_DRIVER, 108000, 1280, 1328,
		   1440, 1688, 0, 1024, 1025, 1028, 1066, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1280x1024@75Hz */
	{ DRM_MODE("1280x1024", DRM_MODE_TYPE_DRIVER, 135000, 1280, 1296,
		   1440, 1688, 0, 1024, 1025, 1028, 1066, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1280x1024@85Hz */
	{ DRM_MODE("1280x1024", DRM_MODE_TYPE_DRIVER, 157500, 1280, 1344,
		   1504, 1728, 0, 1024, 1025, 1028, 1072, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1360x768@60Hz */
	{ DRM_MODE("1360x768", DRM_MODE_TYPE_DRIVER, 85500, 1360, 1424,
		   1536, 1792, 0, 768, 771, 777, 795, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1440x1050@60Hz */
	{ DRM_MODE("1400x1050", DRM_MODE_TYPE_DRIVER, 121750, 1400, 1488,
		   1632, 1864, 0, 1050, 1053, 1057, 1089, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1440x1050@75Hz */
	{ DRM_MODE("1400x1050", DRM_MODE_TYPE_DRIVER, 156000, 1400, 1504,
		   1648, 1896, 0, 1050, 1053, 1057, 1099, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1440x1050@85Hz */
	{ DRM_MODE("1400x1050", DRM_MODE_TYPE_DRIVER, 179500, 1400, 1504,
		   1656, 1912, 0, 1050, 1053, 1057, 1105, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1440x900@60Hz */
	{ DRM_MODE("1440x900", DRM_MODE_TYPE_DRIVER, 106500, 1440, 1520,
		   1672, 1904, 0, 900, 903, 909, 934, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1440x900@75Hz */
	{ DRM_MODE("1440x900", DRM_MODE_TYPE_DRIVER, 136750, 1440, 1536,
		   1688, 1936, 0, 900, 903, 909, 942, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1440x900@85Hz */
	{ DRM_MODE("1440x900", DRM_MODE_TYPE_DRIVER, 157000, 1440, 1544,
		   1696, 1952, 0, 900, 903, 909, 948, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1600x1200@60Hz */
	{ DRM_MODE("1600x1200", DRM_MODE_TYPE_DRIVER, 162000, 1600, 1664,
		   1856, 2160, 0, 1200, 1201, 1204, 1250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1600x1200@65Hz */
	{ DRM_MODE("1600x1200", DRM_MODE_TYPE_DRIVER, 175500, 1600, 1664,
		   1856, 2160, 0, 1200, 1201, 1204, 1250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1600x1200@70Hz */
	{ DRM_MODE("1600x1200", DRM_MODE_TYPE_DRIVER, 189000, 1600, 1664,
		   1856, 2160, 0, 1200, 1201, 1204, 1250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1600x1200@75Hz */
	{ DRM_MODE("1600x1200", DRM_MODE_TYPE_DRIVER, 2025000, 1600, 1664,
		   1856, 2160, 0, 1200, 1201, 1204, 1250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1600x1200@85Hz */
	{ DRM_MODE("1600x1200", DRM_MODE_TYPE_DRIVER, 229500, 1600, 1664,
		   1856, 2160, 0, 1200, 1201, 1204, 1250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1680x1050@60Hz */
	{ DRM_MODE("1680x1050", DRM_MODE_TYPE_DRIVER, 146250, 1680, 1784,
		   1960, 2240, 0, 1050, 1053, 1059, 1089, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1680x1050@75Hz */
	{ DRM_MODE("1680x1050", DRM_MODE_TYPE_DRIVER, 187000, 1680, 1800,
		   1976, 2272, 0, 1050, 1053, 1059, 1099, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1680x1050@85Hz */
	{ DRM_MODE("1680x1050", DRM_MODE_TYPE_DRIVER, 214750, 1680, 1808,
		   1984, 2288, 0, 1050, 1053, 1059, 1105, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1792x1344@60Hz */
	{ DRM_MODE("1792x1344", DRM_MODE_TYPE_DRIVER, 204750, 1792, 1920,
		   2120, 2448, 0, 1344, 1345, 1348, 1394, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1729x1344@75Hz */
	{ DRM_MODE("1792x1344", DRM_MODE_TYPE_DRIVER, 261000, 1792, 1888,
		   2104, 2456, 0, 1344, 1345, 1348, 1417, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1853x1392@60Hz */
	{ DRM_MODE("1856x1392", DRM_MODE_TYPE_DRIVER, 218250, 1856, 1952,
		   2176, 2528, 0, 1392, 1393, 1396, 1439, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1856x1392@75Hz */
	{ DRM_MODE("1856x1392", DRM_MODE_TYPE_DRIVER, 288000, 1856, 1984,
		   2208, 2560, 0, 1392, 1395, 1399, 1500, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1920x1200@60Hz */
	{ DRM_MODE("1920x1200", DRM_MODE_TYPE_DRIVER, 193250, 1920, 2056,
		   2256, 2592, 0, 1200, 1203, 1209, 1245, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1920x1200@75Hz */
	{ DRM_MODE("1920x1200", DRM_MODE_TYPE_DRIVER, 245250, 1920, 2056,
		   2264, 2608, 0, 1200, 1203, 1209, 1255, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1920x1200@85Hz */
	{ DRM_MODE("1920x1200", DRM_MODE_TYPE_DRIVER, 281250, 1920, 2064,
		   2272, 2624, 0, 1200, 1203, 1209, 1262, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1920x1440@60Hz */
	{ DRM_MODE("1920x1440", DRM_MODE_TYPE_DRIVER, 234000, 1920, 2048,
		   2256, 2600, 0, 1440, 1441, 1444, 1500, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1920x1440@75Hz */
	{ DRM_MODE("1920x1440", DRM_MODE_TYPE_DRIVER, 297000, 1920, 2064,
		   2288, 2640, 0, 1440, 1441, 1444, 1500, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 2560x1600@60Hz */
	{ DRM_MODE("2560x1600", DRM_MODE_TYPE_DRIVER, 348500, 2560, 2752,
		   3032, 3504, 0, 1600, 1603, 1609, 1658, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 2560x1600@75HZ */
	{ DRM_MODE("2560x1600", DRM_MODE_TYPE_DRIVER, 443250, 2560, 2768,
		   3048, 3536, 0, 1600, 1603, 1609, 1672, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 2560x1600@85HZ */
	{ DRM_MODE("2560x1600", DRM_MODE_TYPE_DRIVER, 505250, 2560, 2768,
		   3048, 3536, 0, 1600, 1603, 1609, 1682, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
};
static const int drm_num_dmt_modes =
	sizeof(drm_dmt_modes) / sizeof(struct drm_display_mode);

static struct drm_display_mode *drm_find_dmt(struct drm_device *dev,
			int hsize, int vsize, int fresh)
{
	int i;
	struct drm_display_mode *ptr, *mode;

	mode = NULL;
	for (i = 0; i < drm_num_dmt_modes; i++) {
		ptr = &drm_dmt_modes[i];
		if (hsize == ptr->hdisplay &&
			vsize == ptr->vdisplay &&
			fresh == drm_mode_vrefresh(ptr)) {
			/* get the expected default mode */
			mode = drm_mode_duplicate(dev, ptr);
			break;
		}
	}
	return mode;
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
 *
 * Punts for now, but should eventually use the FB layer's CVT based mode
 * generation code.
 */
struct drm_display_mode *drm_mode_std(struct drm_device *dev,
				      struct std_timing *t,
				      int revision,
				      int timing_level)
{
	struct drm_display_mode *mode;
	int hsize, vsize;
	int vrefresh_rate;
	unsigned aspect_ratio = (t->vfreq_aspect & EDID_TIMING_ASPECT_MASK)
		>> EDID_TIMING_ASPECT_SHIFT;
	unsigned vfreq = (t->vfreq_aspect & EDID_TIMING_VFREQ_MASK)
		>> EDID_TIMING_VFREQ_SHIFT;

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
	/* HDTV hack */
	if (hsize == 1360 && vsize == 765 && vrefresh_rate == 60) {
		mode = drm_cvt_mode(dev, hsize, vsize, vrefresh_rate, 0, 0,
				    false);
		mode->hdisplay = 1366;
		mode->vsync_start = mode->vsync_start - 1;
		mode->vsync_end = mode->vsync_end - 1;
		return mode;
	}
	mode = NULL;
	/* check whether it can be found in default mode table */
	mode = drm_find_dmt(dev, hsize, vsize, vrefresh_rate);
	if (mode)
		return mode;

	switch (timing_level) {
	case LEVEL_DMT:
		break;
	case LEVEL_GTF:
		mode = drm_gtf_mode(dev, hsize, vsize, vrefresh_rate, 0, 0);
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
	static const int n_sizes =
		sizeof(cea_interlaced)/sizeof(cea_interlaced[0]);

	if (!(pt->misc & DRM_EDID_PT_INTERLACED))
		return;

	for (i = 0; i < n_sizes; i++) {
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
	mode = drm_mode_create(dev);
	if (!mode)
		return NULL;

	mode->type = DRM_MODE_TYPE_DRIVER;

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

	/* perform the basic check for the detailed timing */
	if (mode->hsync_end > mode->htotal ||
		mode->vsync_end > mode->vtotal) {
		drm_mode_destroy(dev, mode);
		DRM_DEBUG_KMS("Incorrect detailed timing. "
				"Sync is beyond the blank.\n");
		return NULL;
	}

	/* Some EDIDs have bogus h/vtotal values */
	if (mode->hsync_end > mode->htotal)
		mode->htotal = mode->hsync_end + 1;
	if (mode->vsync_end > mode->vtotal)
		mode->vtotal = mode->vsync_end + 1;

	drm_mode_set_name(mode);

	drm_mode_do_interlace_quirk(mode, pt);

	if (quirks & EDID_QUIRK_DETAILED_SYNC_PP) {
		pt->misc |= DRM_EDID_PT_HSYNC_POSITIVE | DRM_EDID_PT_VSYNC_POSITIVE;
	}

	mode->flags |= (pt->misc & DRM_EDID_PT_HSYNC_POSITIVE) ?
		DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC;
	mode->flags |= (pt->misc & DRM_EDID_PT_VSYNC_POSITIVE) ?
		DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC;

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
 * stanard_timing_level - get std. timing level(CVT/GTF/DMT)
 * @edid: EDID block to scan
 */
static int standard_timing_level(struct edid *edid)
{
	if (edid->revision >= 2) {
		if (edid->revision >= 4 && (edid->features & DRM_EDID_FEATURE_DEFAULT_GTF))
			return LEVEL_CVT;
		return LEVEL_GTF;
	}
	return LEVEL_DMT;
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
	int timing_level;

	timing_level = standard_timing_level(edid);

	for (i = 0; i < EDID_STD_TIMINGS; i++) {
		struct std_timing *t = &edid->standard_timings[i];
		struct drm_display_mode *newmode;

		/* If std timings bytes are 1, 1 it's empty */
		if (t->hsize == 1 && t->vfreq_aspect == 1)
			continue;

		newmode = drm_mode_std(dev, &edid->standard_timings[i],
				       edid->revision, timing_level);
		if (newmode) {
			drm_mode_probed_add(connector, newmode);
			modes++;
		}
	}

	return modes;
}

/*
 * XXX fix this for:
 * - GTF secondary curve formula
 * - EDID 1.4 range offsets
 * - CVT extended bits
 */
static bool
mode_in_range(struct drm_display_mode *mode, struct detailed_timing *timing)
{
	struct detailed_data_monitor_range *range;
	int hsync, vrefresh;

	range = &timing->data.other_data.data.range;

	hsync = drm_mode_hsync(mode);
	vrefresh = drm_mode_vrefresh(mode);

	if (hsync < range->min_hfreq_khz || hsync > range->max_hfreq_khz)
		return false;

	if (vrefresh < range->min_vfreq || vrefresh > range->max_vfreq)
		return false;

	if (range->pixel_clock_mhz && range->pixel_clock_mhz != 0xff) {
		/* be forgiving since it's in units of 10MHz */
		int max_clock = range->pixel_clock_mhz * 10 + 9;
		max_clock *= 1000;
		if (mode->clock > max_clock)
			return false;
	}

	return true;
}

/*
 * XXX If drm_dmt_modes ever regrows the CVT-R modes (and it will) this will
 * need to account for them.
 */
static int drm_gtf_modes_for_range(struct drm_connector *connector,
				   struct detailed_timing *timing)
{
	int i, modes = 0;
	struct drm_display_mode *newmode;
	struct drm_device *dev = connector->dev;

	for (i = 0; i < drm_num_dmt_modes; i++) {
		if (mode_in_range(drm_dmt_modes + i, timing)) {
			newmode = drm_mode_duplicate(dev, &drm_dmt_modes[i]);
			if (newmode) {
				drm_mode_probed_add(connector, newmode);
				modes++;
			}
		}
	}

	return modes;
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

static int add_detailed_modes(struct drm_connector *connector,
			      struct detailed_timing *timing,
			      struct edid *edid, u32 quirks, int preferred)
{
	int i, modes = 0;
	struct detailed_non_pixel *data = &timing->data.other_data;
	int timing_level = standard_timing_level(edid);
	int gtf = (edid->features & DRM_EDID_FEATURE_DEFAULT_GTF);
	struct drm_display_mode *newmode;
	struct drm_device *dev = connector->dev;

	if (timing->pixel_clock) {
		newmode = drm_mode_detailed(dev, edid, timing, quirks);
		if (!newmode)
			return 0;

		if (preferred)
			newmode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_probed_add(connector, newmode);
		return 1;
	}

	/* other timing types */
	switch (data->type) {
	case EDID_DETAIL_MONITOR_RANGE:
		if (gtf)
			modes += drm_gtf_modes_for_range(connector, timing);
		break;
	case EDID_DETAIL_STD_MODES:
		/* Six modes per detailed section */
		for (i = 0; i < 6; i++) {
			struct std_timing *std;
			struct drm_display_mode *newmode;

			std = &data->data.timings[i];
			newmode = drm_mode_std(dev, std, edid->revision,
					       timing_level);
			if (newmode) {
				drm_mode_probed_add(connector, newmode);
				modes++;
			}
		}
		break;
	case EDID_DETAIL_CVT_3BYTE:
		modes += drm_cvt_modes(connector, timing);
		break;
	default:
		break;
	}

	return modes;
}

/**
 * add_detailed_info - get detailed mode info from EDID data
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
	int i, modes = 0;

	for (i = 0; i < EDID_DETAILED_TIMINGS; i++) {
		struct detailed_timing *timing = &edid->detailed_timings[i];
		int preferred = (i == 0) && (edid->features & DRM_EDID_FEATURE_PREFERRED_TIMING);

		/* In 1.0, only timings are allowed */
		if (!timing->pixel_clock && edid->version == 1 &&
			edid->revision == 0)
			continue;

		modes += add_detailed_modes(connector, timing, edid, quirks,
					    preferred);
	}

	return modes;
}

/**
 * add_detailed_mode_eedid - get detailed mode info from addtional timing
 * 			EDID block
 * @connector: attached connector
 * @edid: EDID block to scan(It is only to get addtional timing EDID block)
 * @quirks: quirks to apply
 *
 * Some of the detailed timing sections may contain mode information.  Grab
 * it and add it to the list.
 */
static int add_detailed_info_eedid(struct drm_connector *connector,
			     struct edid *edid, u32 quirks)
{
	int i, modes = 0;
	char *edid_ext = NULL;
	struct detailed_timing *timing;
	int edid_ext_num;
	int start_offset, end_offset;
	int timing_level;

	if (edid->version == 1 && edid->revision < 3) {
		/* If the EDID version is less than 1.3, there is no
		 * extension EDID.
		 */
		return 0;
	}
	if (!edid->extensions) {
		/* if there is no extension EDID, it is unnecessary to
		 * parse the E-EDID to get detailed info
		 */
		return 0;
	}

	/* Chose real EDID extension number */
	edid_ext_num = edid->extensions > DRM_MAX_EDID_EXT_NUM ?
		DRM_MAX_EDID_EXT_NUM : edid->extensions;

	/* Find CEA extension */
	for (i = 0; i < edid_ext_num; i++) {
		edid_ext = (char *)edid + EDID_LENGTH * (i + 1);
		/* This block is CEA extension */
		if (edid_ext[0] == 0x02)
			break;
	}

	if (i == edid_ext_num) {
		/* if there is no additional timing EDID block, return */
		return 0;
	}

	/* Get the start offset of detailed timing block */
	start_offset = edid_ext[2];
	if (start_offset == 0) {
		/* If the start_offset is zero, it means that neither detailed
		 * info nor data block exist. In such case it is also
		 * unnecessary to parse the detailed timing info.
		 */
		return 0;
	}

	timing_level = standard_timing_level(edid);
	end_offset = EDID_LENGTH;
	end_offset -= sizeof(struct detailed_timing);
	for (i = start_offset; i < end_offset;
			i += sizeof(struct detailed_timing)) {
		timing = (struct detailed_timing *)(edid_ext + i);
		modes += add_detailed_modes(connector, timing, edid, quirks, 0);
	}

	return modes;
}

#define DDC_ADDR 0x50
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
int drm_do_probe_ddc_edid(struct i2c_adapter *adapter,
			  unsigned char *buf, int len)
{
	unsigned char start = 0x0;
	struct i2c_msg msgs[] = {
		{
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

	if (i2c_transfer(adapter, msgs, 2) == 2)
		return 0;

	return -1;
}
EXPORT_SYMBOL(drm_do_probe_ddc_edid);

static int drm_ddc_read_edid(struct drm_connector *connector,
			     struct i2c_adapter *adapter,
			     char *buf, int len)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (drm_do_probe_ddc_edid(adapter, buf, len))
			return -1;
		if (drm_edid_is_valid((struct edid *)buf))
			return 0;
	}

	/* repeated checksum failures; warn, but carry on */
	dev_warn(&connector->dev->pdev->dev, "%s: EDID invalid.\n",
		 drm_get_connector_name(connector));
	return -1;
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
	int ret;
	struct edid *edid;

	edid = kmalloc(EDID_LENGTH * (DRM_MAX_EDID_EXT_NUM + 1),
		       GFP_KERNEL);
	if (edid == NULL) {
		dev_warn(&connector->dev->pdev->dev,
			 "Failed to allocate EDID\n");
		goto end;
	}

	/* Read first EDID block */
	ret = drm_ddc_read_edid(connector, adapter,
				(unsigned char *)edid, EDID_LENGTH);
	if (ret != 0)
		goto clean_up;

	/* There are EDID extensions to be read */
	if (edid->extensions != 0) {
		int edid_ext_num = edid->extensions;

		if (edid_ext_num > DRM_MAX_EDID_EXT_NUM) {
			dev_warn(&connector->dev->pdev->dev,
				 "The number of extension(%d) is "
				 "over max (%d), actually read number (%d)\n",
				 edid_ext_num, DRM_MAX_EDID_EXT_NUM,
				 DRM_MAX_EDID_EXT_NUM);
			/* Reset EDID extension number to be read */
			edid_ext_num = DRM_MAX_EDID_EXT_NUM;
		}
		/* Read EDID including extensions too */
		ret = drm_ddc_read_edid(connector, adapter, (char *)edid,
					EDID_LENGTH * (edid_ext_num + 1));
		if (ret != 0)
			goto clean_up;

	}

	connector->display_info.raw_edid = (char *)edid;
	goto end;

clean_up:
	kfree(edid);
	edid = NULL;
end:
	return edid;

}
EXPORT_SYMBOL(drm_get_edid);

#define HDMI_IDENTIFIER 0x000C03
#define VENDOR_BLOCK    0x03
/**
 * drm_detect_hdmi_monitor - detect whether monitor is hdmi.
 * @edid: monitor EDID information
 *
 * Parse the CEA extension according to CEA-861-B.
 * Return true if HDMI, false if not or unknown.
 */
bool drm_detect_hdmi_monitor(struct edid *edid)
{
	char *edid_ext = NULL;
	int i, hdmi_id, edid_ext_num;
	int start_offset, end_offset;
	bool is_hdmi = false;

	/* No EDID or EDID extensions */
	if (edid == NULL || edid->extensions == 0)
		goto end;

	/* Chose real EDID extension number */
	edid_ext_num = edid->extensions > DRM_MAX_EDID_EXT_NUM ?
		       DRM_MAX_EDID_EXT_NUM : edid->extensions;

	/* Find CEA extension */
	for (i = 0; i < edid_ext_num; i++) {
		edid_ext = (char *)edid + EDID_LENGTH * (i + 1);
		/* This block is CEA extension */
		if (edid_ext[0] == 0x02)
			break;
	}

	if (i == edid_ext_num)
		goto end;

	/* Data block offset in CEA extension block */
	start_offset = 4;
	end_offset = edid_ext[2];

	/*
	 * Because HDMI identifier is in Vendor Specific Block,
	 * search it from all data blocks of CEA extension.
	 */
	for (i = start_offset; i < end_offset;
		/* Increased by data block len */
		i += ((edid_ext[i] & 0x1f) + 1)) {
		/* Find vendor specific block */
		if ((edid_ext[i] >> 5) == VENDOR_BLOCK) {
			hdmi_id = edid_ext[i + 1] | (edid_ext[i + 2] << 8) |
				  edid_ext[i + 3] << 16;
			/* Find HDMI identifier */
			if (hdmi_id == HDMI_IDENTIFIER)
				is_hdmi = true;
			break;
		}
	}

end:
	return is_hdmi;
}
EXPORT_SYMBOL(drm_detect_hdmi_monitor);

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
		dev_warn(&connector->dev->pdev->dev, "%s: EDID invalid.\n",
			 drm_get_connector_name(connector));
		return 0;
	}

	quirks = edid_get_quirks(edid);

	num_modes += add_established_modes(connector, edid);
	num_modes += add_standard_modes(connector, edid);
	num_modes += add_detailed_info(connector, edid, quirks);
	num_modes += add_detailed_info_eedid(connector, edid, quirks);

	if (quirks & (EDID_QUIRK_PREFER_LARGE_60 | EDID_QUIRK_PREFER_LARGE_75))
		edid_fixup_preferred(connector, quirks);

	connector->display_info.serration_vsync = (edid->input & DRM_EDID_INPUT_SERRATION_VSYNC) ? 1 : 0;
	connector->display_info.sync_on_green = (edid->input & DRM_EDID_INPUT_SYNC_ON_GREEN) ? 1 : 0;
	connector->display_info.composite_sync = (edid->input & DRM_EDID_INPUT_COMPOSITE_SYNC) ? 1 : 0;
	connector->display_info.separate_syncs = (edid->input & DRM_EDID_INPUT_SEPARATE_SYNCS) ? 1 : 0;
	connector->display_info.blank_to_black = (edid->input & DRM_EDID_INPUT_BLANK_TO_BLACK) ? 1 : 0;
	connector->display_info.video_level = (edid->input & DRM_EDID_INPUT_VIDEO_LEVEL) >> 5;
	connector->display_info.digital = (edid->input & DRM_EDID_INPUT_DIGITAL) ? 1 : 0;
	connector->display_info.width_mm = edid->width_cm * 10;
	connector->display_info.height_mm = edid->height_cm * 10;
	connector->display_info.gamma = edid->gamma;
	connector->display_info.gtf_supported = (edid->features & DRM_EDID_FEATURE_DEFAULT_GTF) ? 1 : 0;
	connector->display_info.standard_color = (edid->features & DRM_EDID_FEATURE_STANDARD_COLOR) ? 1 : 0;
	connector->display_info.display_type = (edid->features & DRM_EDID_FEATURE_DISPLAY_TYPE) >> 3;
	connector->display_info.active_off_supported = (edid->features & DRM_EDID_FEATURE_PM_ACTIVE_OFF) ? 1 : 0;
	connector->display_info.suspend_supported = (edid->features & DRM_EDID_FEATURE_PM_SUSPEND) ? 1 : 0;
	connector->display_info.standby_supported = (edid->features & DRM_EDID_FEATURE_PM_STANDBY) ? 1 : 0;
	connector->display_info.gamma = edid->gamma;

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
	struct drm_display_mode *mode, *ptr;
	struct drm_device *dev = connector->dev;

	count = sizeof(drm_dmt_modes) / sizeof(struct drm_display_mode);
	if (hdisplay < 0)
		hdisplay = 0;
	if (vdisplay < 0)
		vdisplay = 0;

	for (i = 0; i < count; i++) {
		ptr = &drm_dmt_modes[i];
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
