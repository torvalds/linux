/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * CCS static data binary format definitions
 *
 * Copyright 2019--2020 Intel Corporation
 */

#ifndef __CCS_DATA_DEFS_H__
#define __CCS_DATA_DEFS_H__

#include "ccs-data.h"

#define CCS_STATIC_DATA_VERSION	0

enum __ccs_data_length_specifier_id {
	CCS_DATA_LENGTH_SPECIFIER_1 = 0,
	CCS_DATA_LENGTH_SPECIFIER_2 = 1,
	CCS_DATA_LENGTH_SPECIFIER_3 = 2
};

#define CCS_DATA_LENGTH_SPECIFIER_SIZE_SHIFT	6

struct __ccs_data_length_specifier {
	u8 length;
} __packed;

struct __ccs_data_length_specifier2 {
	u8 length[2];
} __packed;

struct __ccs_data_length_specifier3 {
	u8 length[3];
} __packed;

struct __ccs_data_block {
	u8 id;
	struct __ccs_data_length_specifier length;
} __packed;

#define CCS_DATA_BLOCK_HEADER_ID_VERSION_SHIFT	5

struct __ccs_data_block3 {
	u8 id;
	struct __ccs_data_length_specifier2 length;
} __packed;

struct __ccs_data_block4 {
	u8 id;
	struct __ccs_data_length_specifier3 length;
} __packed;

enum __ccs_data_block_id {
	CCS_DATA_BLOCK_ID_DUMMY	= 1,
	CCS_DATA_BLOCK_ID_DATA_VERSION = 2,
	CCS_DATA_BLOCK_ID_SENSOR_READ_ONLY_REGS = 3,
	CCS_DATA_BLOCK_ID_MODULE_READ_ONLY_REGS = 4,
	CCS_DATA_BLOCK_ID_SENSOR_MANUFACTURER_REGS = 5,
	CCS_DATA_BLOCK_ID_MODULE_MANUFACTURER_REGS = 6,
	CCS_DATA_BLOCK_ID_SENSOR_RULE_BASED_BLOCK = 32,
	CCS_DATA_BLOCK_ID_MODULE_RULE_BASED_BLOCK = 33,
	CCS_DATA_BLOCK_ID_SENSOR_PDAF_PIXEL_LOCATION = 36,
	CCS_DATA_BLOCK_ID_MODULE_PDAF_PIXEL_LOCATION = 37,
	CCS_DATA_BLOCK_ID_LICENSE = 40,
	CCS_DATA_BLOCK_ID_END = 127,
};

struct __ccs_data_block_version {
	u8 static_data_version_major[2];
	u8 static_data_version_minor[2];
	u8 year[2];
	u8 month;
	u8 day;
} __packed;

struct __ccs_data_block_regs {
	u8 reg_len;
} __packed;

#define CCS_DATA_BLOCK_REGS_ADDR_MASK		0x07
#define CCS_DATA_BLOCK_REGS_LEN_SHIFT		3
#define CCS_DATA_BLOCK_REGS_LEN_MASK		0x38
#define CCS_DATA_BLOCK_REGS_SEL_SHIFT		6

enum ccs_data_block_regs_sel {
	CCS_DATA_BLOCK_REGS_SEL_REGS = 0,
	CCS_DATA_BLOCK_REGS_SEL_REGS2 = 1,
	CCS_DATA_BLOCK_REGS_SEL_REGS3 = 2,
};

struct __ccs_data_block_regs2 {
	u8 reg_len;
	u8 addr;
} __packed;

#define CCS_DATA_BLOCK_REGS_2_ADDR_MASK		0x01
#define CCS_DATA_BLOCK_REGS_2_LEN_SHIFT		1
#define CCS_DATA_BLOCK_REGS_2_LEN_MASK		0x3e

struct __ccs_data_block_regs3 {
	u8 reg_len;
	u8 addr[2];
} __packed;

#define CCS_DATA_BLOCK_REGS_3_LEN_MASK		0x3f

enum __ccs_data_ffd_pixelcode {
	CCS_DATA_BLOCK_FFD_PIXELCODE_EMBEDDED = 1,
	CCS_DATA_BLOCK_FFD_PIXELCODE_DUMMY = 2,
	CCS_DATA_BLOCK_FFD_PIXELCODE_BLACK = 3,
	CCS_DATA_BLOCK_FFD_PIXELCODE_DARK = 4,
	CCS_DATA_BLOCK_FFD_PIXELCODE_VISIBLE = 5,
	CCS_DATA_BLOCK_FFD_PIXELCODE_MS_0 = 8,
	CCS_DATA_BLOCK_FFD_PIXELCODE_MS_1 = 9,
	CCS_DATA_BLOCK_FFD_PIXELCODE_MS_2 = 10,
	CCS_DATA_BLOCK_FFD_PIXELCODE_MS_3 = 11,
	CCS_DATA_BLOCK_FFD_PIXELCODE_MS_4 = 12,
	CCS_DATA_BLOCK_FFD_PIXELCODE_MS_5 = 13,
	CCS_DATA_BLOCK_FFD_PIXELCODE_MS_6 = 14,
	CCS_DATA_BLOCK_FFD_PIXELCODE_TOP_OB = 16,
	CCS_DATA_BLOCK_FFD_PIXELCODE_BOTTOM_OB = 17,
	CCS_DATA_BLOCK_FFD_PIXELCODE_LEFT_OB = 18,
	CCS_DATA_BLOCK_FFD_PIXELCODE_RIGHT_OB = 19,
	CCS_DATA_BLOCK_FFD_PIXELCODE_TOP_LEFT_OB = 20,
	CCS_DATA_BLOCK_FFD_PIXELCODE_TOP_RIGHT_OB = 21,
	CCS_DATA_BLOCK_FFD_PIXELCODE_BOTTOM_LEFT_OB = 22,
	CCS_DATA_BLOCK_FFD_PIXELCODE_BOTTOM_RIGHT_OB = 23,
	CCS_DATA_BLOCK_FFD_PIXELCODE_TOTAL = 24,
	CCS_DATA_BLOCK_FFD_PIXELCODE_TOP_PDAF = 32,
	CCS_DATA_BLOCK_FFD_PIXELCODE_BOTTOM_PDAF = 33,
	CCS_DATA_BLOCK_FFD_PIXELCODE_LEFT_PDAF = 34,
	CCS_DATA_BLOCK_FFD_PIXELCODE_RIGHT_PDAF = 35,
	CCS_DATA_BLOCK_FFD_PIXELCODE_TOP_LEFT_PDAF = 36,
	CCS_DATA_BLOCK_FFD_PIXELCODE_TOP_RIGHT_PDAF = 37,
	CCS_DATA_BLOCK_FFD_PIXELCODE_BOTTOM_LEFT_PDAF = 38,
	CCS_DATA_BLOCK_FFD_PIXELCODE_BOTTOM_RIGHT_PDAF = 39,
	CCS_DATA_BLOCK_FFD_PIXELCODE_SEPARATED_PDAF = 40,
	CCS_DATA_BLOCK_FFD_PIXELCODE_ORIGINAL_ORDER_PDAF = 41,
	CCS_DATA_BLOCK_FFD_PIXELCODE_VENDOR_PDAF = 41,
};

struct __ccs_data_block_ffd_entry {
	u8 pixelcode;
	u8 reserved;
	u8 value[2];
} __packed;

struct __ccs_data_block_ffd {
	u8 num_column_descs;
	u8 num_row_descs;
} __packed;

enum __ccs_data_block_rule_id {
	CCS_DATA_BLOCK_RULE_ID_IF = 1,
	CCS_DATA_BLOCK_RULE_ID_READ_ONLY_REGS = 2,
	CCS_DATA_BLOCK_RULE_ID_FFD = 3,
	CCS_DATA_BLOCK_RULE_ID_MSR = 4,
	CCS_DATA_BLOCK_RULE_ID_PDAF_READOUT = 5,
};

struct __ccs_data_block_rule_if {
	u8 addr[2];
	u8 value;
	u8 mask;
} __packed;

enum __ccs_data_block_pdaf_readout_order {
	CCS_DATA_BLOCK_PDAF_READOUT_ORDER_ORIGINAL = 1,
	CCS_DATA_BLOCK_PDAF_READOUT_ORDER_SEPARATE_WITHIN_LINE = 2,
	CCS_DATA_BLOCK_PDAF_READOUT_ORDER_SEPARATE_TYPES_SEPARATE_LINES = 3,
};

struct __ccs_data_block_pdaf_readout {
	u8 pdaf_readout_info_reserved;
	u8 pdaf_readout_info_order;
} __packed;

struct __ccs_data_block_pdaf_pix_loc_block_desc {
	u8 block_type_id;
	u8 repeat_x[2];
} __packed;

struct __ccs_data_block_pdaf_pix_loc_block_desc_group {
	u8 num_block_descs[2];
	u8 repeat_y;
} __packed;

enum __ccs_data_block_pdaf_pix_loc_pixel_type {
	CCS_DATA_PDAF_PIXEL_TYPE_LEFT_SEPARATED = 0,
	CCS_DATA_PDAF_PIXEL_TYPE_RIGHT_SEPARATED = 1,
	CCS_DATA_PDAF_PIXEL_TYPE_TOP_SEPARATED = 2,
	CCS_DATA_PDAF_PIXEL_TYPE_BOTTOM_SEPARATED = 3,
	CCS_DATA_PDAF_PIXEL_TYPE_LEFT_SIDE_BY_SIDE = 4,
	CCS_DATA_PDAF_PIXEL_TYPE_RIGHT_SIDE_BY_SIDE = 5,
	CCS_DATA_PDAF_PIXEL_TYPE_TOP_SIDE_BY_SIDE = 6,
	CCS_DATA_PDAF_PIXEL_TYPE_BOTTOM_SIDE_BY_SIDE = 7,
	CCS_DATA_PDAF_PIXEL_TYPE_TOP_LEFT = 8,
	CCS_DATA_PDAF_PIXEL_TYPE_TOP_RIGHT = 9,
	CCS_DATA_PDAF_PIXEL_TYPE_BOTTOM_LEFT = 10,
	CCS_DATA_PDAF_PIXEL_TYPE_BOTTOM_RIGHT = 11,
};

struct __ccs_data_block_pdaf_pix_loc_pixel_desc {
	u8 pixel_type;
	u8 small_offset_x;
	u8 small_offset_y;
} __packed;

struct __ccs_data_block_pdaf_pix_loc {
	u8 main_offset_x[2];
	u8 main_offset_y[2];
	u8 global_pdaf_type;
	u8 block_width;
	u8 block_height;
	u8 num_block_desc_groups[2];
} __packed;

struct __ccs_data_block_end {
	u8 crc[4];
} __packed;

#endif /* __CCS_DATA_DEFS_H__ */
