/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#ifndef __DAL_BIOS_PARSER_TYPES_BIOS2_H__
#define __DAL_BIOS_PARSER_TYPES_BIOS2_H__

#include "dc_bios_types.h"
#include "bios_parser_helper.h"

/* use atomfirmware_bringup.h only. Not atombios.h anymore */

struct atom_data_revision {
	uint32_t major;
	uint32_t minor;
};

struct object_info_table {
	struct atom_data_revision revision;
	union {
		struct display_object_info_table_v1_4 *v1_4;
	};
};

enum spread_spectrum_id {
	SS_ID_UNKNOWN = 0,
	SS_ID_DP1 = 0xf1,
	SS_ID_DP2 = 0xf2,
	SS_ID_LVLINK_2700MHZ = 0xf3,
	SS_ID_LVLINK_1620MHZ = 0xf4
};

struct bios_parser {
	struct dc_bios base;

	struct object_info_table object_info_tbl;
	uint32_t object_info_tbl_offset;
	struct atom_master_data_table_v2_1 *master_data_tbl;


	const struct bios_parser_helper *bios_helper;

	const struct command_table_helper *cmd_helper;
	struct cmd_tbl cmd_tbl;

	bool remap_device_tags;
};

/* Bios Parser from DC Bios */
#define BP_FROM_DCB(dc_bios) \
	container_of(dc_bios, struct bios_parser, base)

#endif
