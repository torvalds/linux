/*
 * Copyright © 2014 Red Hat Inc.
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
 */

#ifndef __DRM_DISPLAYID_INTERNAL_H__
#define __DRM_DISPLAYID_INTERNAL_H__

#include <linux/types.h>
#include <linux/bits.h>

struct drm_edid;

#define VESA_IEEE_OUI				0x3a0292

/* DisplayID Structure versions */
#define DISPLAY_ID_STRUCTURE_VER_20		0x20

/* DisplayID Structure v1r2 Data Blocks */
#define DATA_BLOCK_PRODUCT_ID			0x00
#define DATA_BLOCK_DISPLAY_PARAMETERS		0x01
#define DATA_BLOCK_COLOR_CHARACTERISTICS	0x02
#define DATA_BLOCK_TYPE_1_DETAILED_TIMING	0x03
#define DATA_BLOCK_TYPE_2_DETAILED_TIMING	0x04
#define DATA_BLOCK_TYPE_3_SHORT_TIMING		0x05
#define DATA_BLOCK_TYPE_4_DMT_TIMING		0x06
#define DATA_BLOCK_VESA_TIMING			0x07
#define DATA_BLOCK_CEA_TIMING			0x08
#define DATA_BLOCK_VIDEO_TIMING_RANGE		0x09
#define DATA_BLOCK_PRODUCT_SERIAL_NUMBER	0x0a
#define DATA_BLOCK_GP_ASCII_STRING		0x0b
#define DATA_BLOCK_DISPLAY_DEVICE_DATA		0x0c
#define DATA_BLOCK_INTERFACE_POWER_SEQUENCING	0x0d
#define DATA_BLOCK_TRANSFER_CHARACTERISTICS	0x0e
#define DATA_BLOCK_DISPLAY_INTERFACE		0x0f
#define DATA_BLOCK_STEREO_DISPLAY_INTERFACE	0x10
#define DATA_BLOCK_TILED_DISPLAY		0x12
#define DATA_BLOCK_VENDOR_SPECIFIC		0x7f
#define DATA_BLOCK_CTA				0x81

/* DisplayID Structure v2r0 Data Blocks */
#define DATA_BLOCK_2_PRODUCT_ID			0x20
#define DATA_BLOCK_2_DISPLAY_PARAMETERS		0x21
#define DATA_BLOCK_2_TYPE_7_DETAILED_TIMING	0x22
#define DATA_BLOCK_2_TYPE_8_ENUMERATED_TIMING	0x23
#define DATA_BLOCK_2_TYPE_9_FORMULA_TIMING	0x24
#define DATA_BLOCK_2_DYNAMIC_VIDEO_TIMING	0x25
#define DATA_BLOCK_2_DISPLAY_INTERFACE_FEATURES	0x26
#define DATA_BLOCK_2_STEREO_DISPLAY_INTERFACE	0x27
#define DATA_BLOCK_2_TILED_DISPLAY_TOPOLOGY	0x28
#define DATA_BLOCK_2_CONTAINER_ID		0x29
#define DATA_BLOCK_2_TYPE_10_FORMULA_TIMING	0x2a
#define DATA_BLOCK_2_VENDOR_SPECIFIC		0x7e
#define DATA_BLOCK_2_CTA_DISPLAY_ID		0x81

/* DisplayID Structure v1r2 Product Type */
#define PRODUCT_TYPE_EXTENSION			0
#define PRODUCT_TYPE_TEST			1
#define PRODUCT_TYPE_PANEL			2
#define PRODUCT_TYPE_MONITOR			3
#define PRODUCT_TYPE_TV				4
#define PRODUCT_TYPE_REPEATER			5
#define PRODUCT_TYPE_DIRECT_DRIVE		6

/* DisplayID Structure v2r0 Display Product Primary Use Case (~Product Type) */
#define PRIMARY_USE_EXTENSION			0
#define PRIMARY_USE_TEST			1
#define PRIMARY_USE_GENERIC			2
#define PRIMARY_USE_TV				3
#define PRIMARY_USE_DESKTOP_PRODUCTIVITY	4
#define PRIMARY_USE_DESKTOP_GAMING		5
#define PRIMARY_USE_PRESENTATION		6
#define PRIMARY_USE_HEAD_MOUNTED_VR		7
#define PRIMARY_USE_HEAD_MOUNTED_AR		8

struct displayid_header {
	u8 rev;
	u8 bytes;
	u8 prod_id;
	u8 ext_count;
} __packed;

struct displayid_block {
	u8 tag;
	u8 rev;
	u8 num_bytes;
} __packed;

struct displayid_tiled_block {
	struct displayid_block base;
	u8 tile_cap;
	u8 topo[3];
	u8 tile_size[4];
	u8 tile_pixel_bezel[5];
	u8 topology_id[8];
} __packed;

struct displayid_detailed_timings_1 {
	u8 pixel_clock[3];
	u8 flags;
	__le16 hactive;
	__le16 hblank;
	__le16 hsync;
	__le16 hsw;
	__le16 vactive;
	__le16 vblank;
	__le16 vsync;
	__le16 vsw;
} __packed;

struct displayid_detailed_timing_block {
	struct displayid_block base;
	struct displayid_detailed_timings_1 timings[];
} __packed;

struct displayid_formula_timings_9 {
	u8 flags;
	__le16 hactive;
	__le16 vactive;
	u8 vrefresh;
} __packed;

struct displayid_formula_timing_block {
	struct displayid_block base;
	struct displayid_formula_timings_9 timings[];
} __packed;

#define DISPLAYID_VESA_MSO_OVERLAP	GENMASK(3, 0)
#define DISPLAYID_VESA_MSO_MODE		GENMASK(6, 5)

struct displayid_vesa_vendor_specific_block {
	struct displayid_block base;
	u8 oui[3];
	u8 data_structure_type;
	u8 mso;
} __packed;

/*
 * DisplayID iteration.
 *
 * Do not access directly, this is private.
 */
struct displayid_iter {
	const struct drm_edid *drm_edid;

	const u8 *section;
	int length;
	int idx;
	int ext_index;

	u8 version;
	u8 primary_use;
};

void displayid_iter_edid_begin(const struct drm_edid *drm_edid,
			       struct displayid_iter *iter);
const struct displayid_block *
__displayid_iter_next(struct displayid_iter *iter);
#define displayid_iter_for_each(__block, __iter) \
	while (((__block) = __displayid_iter_next(__iter)))
void displayid_iter_end(struct displayid_iter *iter);

u8 displayid_version(const struct displayid_iter *iter);
u8 displayid_primary_use(const struct displayid_iter *iter);

#endif
