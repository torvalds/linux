/*
 * Samsung HDMI driver
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *
 * Jiun Yu <jiun.yu@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundiation. either version 2 of the License,
 * or (at your option) any later version
 */

#include "hdmi.h"
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>
#include <plat/devs.h>
#include <plat/tv-core.h>
#include <plat/cpu.h>

#include "regs-hdmi-5xx0.h"

static const struct hdmi_preset_conf hdmi_conf_480p60 = {
	.core = {
		.h_blank = {0x8a, 0x00},
		.v2_blank = {0x0d, 0x02},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x0d, 0x02},
		.h_line = {0x5a, 0x03},
		.hsync_pol = {0x01},
		.vsync_pol = {0x01},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x0e, 0x00},
		.h_sync_end = {0x4c, 0x00},
		.v_sync_line_bef_2 = {0x0f, 0x00},
		.v_sync_line_bef_1 = {0x09, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x5a, 0x03, /* h_fsz */
		0x8a, 0x00, 0xd0, 0x02, /* hact */
		0x0d, 0x02, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0xe0, 0x01, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 720,
		.height = 480,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 3,
};

static const struct hdmi_preset_conf hdmi_conf_720p60 = {
	.core = {
		.h_blank = {0x72, 0x01},
		.v2_blank = {0xee, 0x02},
		.v1_blank = {0x1e, 0x00},
		.v_line = {0xee, 0x02},
		.h_line = {0x72, 0x06},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x6c, 0x00},
		.h_sync_end = {0x94, 0x00},
		.v_sync_line_bef_2 = {0x0a, 0x00},
		.v_sync_line_bef_1 = {0x05, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x72, 0x06, /* h_fsz */
		0x72, 0x01, 0x00, 0x05, /* hact */
		0xee, 0x02, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x1e, 0x00, 0xd0, 0x02, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1280,
		.height = 720,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 4,
};

static const struct hdmi_preset_conf hdmi_conf_1080i60 = {
	.core = {
		.h_blank = {0x18, 0x01},
		.v2_blank = {0x32, 0x02},
		.v1_blank = {0x16, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0x98, 0x08},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x01},
		.v_blank_f0 = {0x49, 0x02},
		.v_blank_f1 = {0x65, 0x04},
		.h_sync_start = {0x56, 0x00},
		.h_sync_end = {0x82, 0x00},
		.v_sync_line_bef_2 = {0x07, 0x00},
		.v_sync_line_bef_1 = {0x02, 0x00},
		.v_sync_line_aft_2 = {0x39, 0x02},
		.v_sync_line_aft_1 = {0x34, 0x02},
		.v_sync_line_aft_pxl_2 = {0xa4, 0x04},
		.v_sync_line_aft_pxl_1 = {0xa4, 0x04},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x98, 0x08, /* h_fsz */
		0x18, 0x01, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x16, 0x00, 0x1c, 0x02, /* vact */
		0x33, 0x02, /* field_chg */
		0x49, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x33, 0x02, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_INTERLACED,
	},
	.vic = 5,
};

static const struct hdmi_preset_conf hdmi_conf_1080p60 = {
	.core = {
		.h_blank = {0x18, 0x01},
		.v2_blank = {0x65, 0x04},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0x98, 0x08},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x56, 0x00},
		.h_sync_end = {0x82, 0x00},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x98, 0x08, /* h_fsz */
		0x18, 0x01, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 16,
};

static const struct hdmi_preset_conf hdmi_conf_576p50 = {
	.core = {
		.h_blank = {0x90, 0x00},
		.v2_blank = {0x71, 0x02},
		.v1_blank = {0x31, 0x00},
		.v_line = {0x71, 0x02},
		.h_line = {0x60, 0x03},
		.hsync_pol = {0x01},
		.vsync_pol = {0x01},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x0a, 0x00},
		.h_sync_end = {0x4a, 0x00},
		.v_sync_line_bef_2 = {0x0a, 0x00},
		.v_sync_line_bef_1 = {0x05, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x60, 0x03, /* h_fsz */
		0x90, 0x00, 0xd0, 0x02, /* hact */
		0x71, 0x02, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x31, 0x00, 0x40, 0x02, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 720,
		.height = 576,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 18,
};

static const struct hdmi_preset_conf hdmi_conf_720p50 = {
	.core = {
		.h_blank = {0xbc, 0x02},
		.v2_blank = {0xee, 0x02},
		.v1_blank = {0x1e, 0x00},
		.v_line = {0xee, 0x02},
		.h_line = {0xbc, 0x07},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0xb6, 0x01},
		.h_sync_end = {0xde, 0x01},
		.v_sync_line_bef_2 = {0x0a, 0x00},
		.v_sync_line_bef_1 = {0x05, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0xbc, 0x07, /* h_fsz */
		0xbc, 0x02, 0x00, 0x05, /* hact */
		0xee, 0x02, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x1e, 0x00, 0xd0, 0x02, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1280,
		.height = 720,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 19,
};

static const struct hdmi_preset_conf hdmi_conf_1080i50 = {
	.core = {
		.h_blank = {0xd0, 0x02},
		.v2_blank = {0x32, 0x02},
		.v1_blank = {0x16, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0x50, 0x0a},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x01},
		.v_blank_f0 = {0x49, 0x02},
		.v_blank_f1 = {0x65, 0x04},
		.h_sync_start = {0x0e, 0x02},
		.h_sync_end = {0x3a, 0x02},
		.v_sync_line_bef_2 = {0x07, 0x00},
		.v_sync_line_bef_1 = {0x02, 0x00},
		.v_sync_line_aft_2 = {0x39, 0x02},
		.v_sync_line_aft_1 = {0x34, 0x02},
		.v_sync_line_aft_pxl_2 = {0x38, 0x07},
		.v_sync_line_aft_pxl_1 = {0x38, 0x07},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x50, 0x0a, /* h_fsz */
		0xd0, 0x02, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x16, 0x00, 0x1c, 0x02, /* vact */
		0x33, 0x02, /* field_chg */
		0x49, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x33, 0x02, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_INTERLACED,
	},
	.vic = 20,
};

static const struct hdmi_preset_conf hdmi_conf_1080p50 = {
	.core = {
		.h_blank = {0xd0, 0x02},
		.v2_blank = {0x65, 0x04},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0x50, 0x0a},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x0e, 0x02},
		.h_sync_end = {0x3a, 0x02},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x50, 0x0a, /* h_fsz */
		0xd0, 0x02, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 31,
};

static const struct hdmi_preset_conf hdmi_conf_1080p30 = {
	.core = {
		.h_blank = {0x18, 0x01},
		.v2_blank = {0x65, 0x04},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0x98, 0x08},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x56, 0x00},
		.h_sync_end = {0x82, 0x00},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x98, 0x08, /* h_fsz */
		0x18, 0x01, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 34,
};

static const struct hdmi_preset_conf hdmi_conf_1080p24 = {
	.core = {
		.h_blank = {0x3e, 0x03},
		.v2_blank = {0x65, 0x04},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0xbe, 0x0a},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x7c, 0x02},
		.h_sync_end = {0xa8, 0x02},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0xbe, 0x0a, /* h_fsz */
		0x3e, 0x03, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 32,
};

static const struct hdmi_preset_conf hdmi_conf_1080p25 = {
	.core = {
		.h_blank = {0xd0, 0x02},
		.v2_blank = {0x65, 0x04},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0x50, 0x0a},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x0e, 0x02},
		.h_sync_end = {0x3a, 0x02},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x50, 0x0a, /* h_fsz */
		0xd0, 0x02, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 33,
};

static const struct hdmi_preset_conf hdmi_conf_480p59_94 = {
	.core = {
		.h_blank = {0x8a, 0x00},
		.v2_blank = {0x0d, 0x02},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x0d, 0x02},
		.h_line = {0x5a, 0x03},
		.hsync_pol = {0x01},
		.vsync_pol = {0x01},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x0e, 0x00},
		.h_sync_end = {0x4c, 0x00},
		.v_sync_line_bef_2 = {0x0f, 0x00},
		.v_sync_line_bef_1 = {0x09, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x5a, 0x03, /* h_fsz */
		0x8a, 0x00, 0xd0, 0x02, /* hact */
		0x0d, 0x02, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0xe0, 0x01, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 720,
		.height = 480,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 3,
};

static const struct hdmi_preset_conf hdmi_conf_720p59_94 = {
	.core = {
		.h_blank = {0x72, 0x01},
		.v2_blank = {0xee, 0x02},
		.v1_blank = {0x1e, 0x00},
		.v_line = {0xee, 0x02},
		.h_line = {0x72, 0x06},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x6c, 0x00},
		.h_sync_end = {0x94, 0x00},
		.v_sync_line_bef_2 = {0x0a, 0x00},
		.v_sync_line_bef_1 = {0x05, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x72, 0x06, /* h_fsz */
		0x72, 0x01, 0x00, 0x05, /* hact */
		0xee, 0x02, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x1e, 0x00, 0xd0, 0x02, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1280,
		.height = 720,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 4,
};

static const struct hdmi_preset_conf hdmi_conf_1080i59_94 = {
	.core = {
		.h_blank = {0x18, 0x01},
		.v2_blank = {0x32, 0x02},
		.v1_blank = {0x16, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0x98, 0x08},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x01},
		.v_blank_f0 = {0x49, 0x02},
		.v_blank_f1 = {0x65, 0x04},
		.h_sync_start = {0x56, 0x00},
		.h_sync_end = {0x82, 0x00},
		.v_sync_line_bef_2 = {0x07, 0x00},
		.v_sync_line_bef_1 = {0x02, 0x00},
		.v_sync_line_aft_2 = {0x39, 0x02},
		.v_sync_line_aft_1 = {0x34, 0x02},
		.v_sync_line_aft_pxl_2 = {0xa4, 0x04},
		.v_sync_line_aft_pxl_1 = {0xa4, 0x04},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x98, 0x08, /* h_fsz */
		0x18, 0x01, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x16, 0x00, 0x1c, 0x02, /* vact */
		0x33, 0x02, /* field_chg */
		0x49, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x33, 0x02, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_INTERLACED,
	},
	.vic = 5,
};

static const struct hdmi_preset_conf hdmi_conf_1080p59_94 = {
	.core = {
		.h_blank = {0x18, 0x01},
		.v2_blank = {0x65, 0x04},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0x98, 0x08},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x56, 0x00},
		.h_sync_end = {0x82, 0x00},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x98, 0x08, /* h_fsz */
		0x18, 0x01, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 16,
};

static const struct hdmi_preset_conf hdmi_conf_720p60_sb_half = {
	.core = {
		.h_blank = {0x72, 0x01},
		.v2_blank = {0xee, 0x02},
		.v1_blank = {0x1e, 0x00},
		.v_line = {0xee, 0x02},
		.h_line = {0x72, 0x06},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x6c, 0x00},
		.h_sync_end = {0x94, 0x00},
		.v_sync_line_bef_2 = {0x0a, 0x00},
		.v_sync_line_bef_1 = {0x05, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x72, 0x06, /* h_fsz */
		0x72, 0x01, 0x00, 0x05, /* hact */
		0xee, 0x02, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x1e, 0x00, 0xd0, 0x02, /* vact */
		0x33, 0x02, /* field_chg */
		0x0c, 0x03, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1280,
		.height = 720,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 4,
};

static const struct hdmi_preset_conf hdmi_conf_720p60_tb = {
	.core = {
		.h_blank = {0x72, 0x01},
		.v2_blank = {0xee, 0x02},
		.v1_blank = {0x1e, 0x00},
		.v_line = {0xee, 0x02},
		.h_line = {0x72, 0x06},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x6c, 0x00},
		.h_sync_end = {0x94, 0x00},
		.v_sync_line_bef_2 = {0x0a, 0x00},
		.v_sync_line_bef_1 = {0x05, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x72, 0x06, /* h_fsz */
		0x72, 0x01, 0x00, 0x05, /* hact */
		0xee, 0x02, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x1e, 0x00, 0xd0, 0x02, /* vact */
		0x33, 0x02, /* field_chg */
		0x0c, 0x03, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1280,
		.height = 720,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 4,
};

static const struct hdmi_preset_conf hdmi_conf_720p59_94_sb_half = {
	.core = {
		.h_blank = {0x72, 0x01},
		.v2_blank = {0xee, 0x02},
		.v1_blank = {0x1e, 0x00},
		.v_line = {0xee, 0x02},
		.h_line = {0x72, 0x06},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x6c, 0x00},
		.h_sync_end = {0x94, 0x00},
		.v_sync_line_bef_2 = {0x0a, 0x00},
		.v_sync_line_bef_1 = {0x05, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x72, 0x06, /* h_fsz */
		0x72, 0x01, 0x00, 0x05, /* hact */
		0xee, 0x02, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x1e, 0x00, 0xd0, 0x02, /* vact */
		0x00, 0x00, /* field_chg */
		0x0c, 0x03, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1280,
		.height = 720,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 4,
};

static const struct hdmi_preset_conf hdmi_conf_720p59_94_tb = {
	.core = {
		.h_blank = {0x72, 0x01},
		.v2_blank = {0xee, 0x02},
		.v1_blank = {0x1e, 0x00},
		.v_line = {0xee, 0x02},
		.h_line = {0x72, 0x06},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x6c, 0x00},
		.h_sync_end = {0x94, 0x00},
		.v_sync_line_bef_2 = {0x0a, 0x00},
		.v_sync_line_bef_1 = {0x05, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x72, 0x06, /* h_fsz */
		0x72, 0x01, 0x00, 0x05, /* hact */
		0xee, 0x02, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x1e, 0x00, 0xd0, 0x02, /* vact */
		0x00, 0x00, /* field_chg */
		0x0c, 0x03, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1280,
		.height = 720,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 4,
};

static const struct hdmi_preset_conf hdmi_conf_720p50_sb_half = {
	.core = {
		.h_blank = {0xbc, 0x02},
		.v2_blank = {0xdc, 0x05},
		.v1_blank = {0x1e, 0x00},
		.v_line = {0xdc, 0x05},
		.h_line = {0xbc, 0x07},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0xb6, 0x01},
		.h_sync_end = {0xde, 0x01},
		.v_sync_line_bef_2 = {0x0a, 0x00},
		.v_sync_line_bef_1 = {0x05, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xee, 0x02},
		.vact_space_2 = {0x0c, 0x03},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0xbc, 0x07, /* h_fsz */
		0xbc, 0x02, 0x00, 0x05, /* hact */
		0xee, 0x02, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x1e, 0x00, 0xd0, 0x02, /* vact */
		0x00, 0x00, /* field_chg */
		0x0c, 0x03, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1280,
		.height = 720,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 19,
};

static const struct hdmi_preset_conf hdmi_conf_720p50_tb = {
	.core = {
		.h_blank = {0xbc, 0x02},
		.v2_blank = {0xdc, 0x05},
		.v1_blank = {0x1e, 0x00},
		.v_line = {0xdc, 0x05},
		.h_line = {0xbc, 0x07},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0xb6, 0x01},
		.h_sync_end = {0xde, 0x01},
		.v_sync_line_bef_2 = {0x0a, 0x00},
		.v_sync_line_bef_1 = {0x05, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xee, 0x02},
		.vact_space_2 = {0x0c, 0x03},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0xbc, 0x07, /* h_fsz */
		0xbc, 0x02, 0x00, 0x05, /* hact */
		0xee, 0x02, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x1e, 0x00, 0xd0, 0x02, /* vact */
		0x00, 0x00, /* field_chg */
		0x0c, 0x03, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1280,
		.height = 720,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 19,
};

static const struct hdmi_preset_conf hdmi_conf_1080p24_fp = {
	.core = {
		.h_blank = {0x3e, 0x03},
		.v2_blank = {0xca, 0x08},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0xca, 0x08},
		.h_line = {0xbe, 0x0a},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x7c, 0x02},
		.h_sync_end = {0xa8, 0x02},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0x65, 0x04},
		.vact_space_2 = {0x92, 0x04},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0xbe, 0x0a, /* h_fsz */
		0x3e, 0x03, 0x80, 0x07, /* hact */
		0xca, 0x08, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x92, 0x04, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x01, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 32,
};

static const struct hdmi_preset_conf hdmi_conf_1080p24_sb_half = {
	.core = {
		.h_blank = {0x3e, 0x03},
		.v2_blank = {0x65, 0x04},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0xbe, 0x0a},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x7c, 0x02},
		.h_sync_end = {0xa8, 0x02},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0xbe, 0x0a, /* h_fsz */
		0x3e, 0x03, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 32,
};

static const struct hdmi_preset_conf hdmi_conf_1080p24_tb = {
	.core = {
		.h_blank = {0x3e, 0x03},
		.v2_blank = {0x65, 0x04},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0xbe, 0x0a},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x7c, 0x02},
		.h_sync_end = {0xa8, 0x02},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0xbe, 0x0a, /* h_fsz */
		0x3e, 0x03, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 32,
};

static const struct hdmi_preset_conf hdmi_conf_1080p23_98_fp = {
	.core = {
		.h_blank = {0x3e, 0x03},
		.v2_blank = {0xca, 0x08},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0xca, 0x08},
		.h_line = {0xbe, 0x0a},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x7c, 0x02},
		.h_sync_end = {0xa8, 0x02},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0x65, 0x04},
		.vact_space_2 = {0x92, 0x04},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0xbe, 0x0a, /* h_fsz */
		0x3e, 0x03, 0x80, 0x07, /* hact */
		0xca, 0x08, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x92, 0x04, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x01, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 32,
};

static const struct hdmi_preset_conf hdmi_conf_1080p23_98_sb_half = {
	.core = {
		.h_blank = {0x3e, 0x03},
		.v2_blank = {0x65, 0x04},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0xbe, 0x0a},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x7c, 0x02},
		.h_sync_end = {0xa8, 0x02},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0xbe, 0x0a, /* h_fsz */
		0x3e, 0x03, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 32,
};

static const struct hdmi_preset_conf hdmi_conf_1080p23_98_tb = {
	.core = {
		.h_blank = {0x3e, 0x03},
		.v2_blank = {0x65, 0x04},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0xbe, 0x0a},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x7c, 0x02},
		.h_sync_end = {0xa8, 0x02},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0xbe, 0x0a, /* h_fsz */
		0x3e, 0x03, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 32,
};

static const struct hdmi_preset_conf hdmi_conf_1080i60_sb_half = {
	.core = {
		.h_blank = {0x18, 0x01},
		.v2_blank = {0x32, 0x02},
		.v1_blank = {0x16, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0x98, 0x08},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x01},
		.v_blank_f0 = {0x49, 0x02},
		.v_blank_f1 = {0x65, 0x04},
		.h_sync_start = {0x56, 0x00},
		.h_sync_end = {0x82, 0x00},
		.v_sync_line_bef_2 = {0x07, 0x00},
		.v_sync_line_bef_1 = {0x02, 0x00},
		.v_sync_line_aft_2 = {0x39, 0x02},
		.v_sync_line_aft_1 = {0x34, 0x02},
		.v_sync_line_aft_pxl_2 = {0xa4, 0x04},
		.v_sync_line_aft_pxl_1 = {0xa4, 0x04},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x98, 0x08, /* h_fsz */
		0x18, 0x01, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x16, 0x00, 0x1c, 0x02, /* vact */
		0x65, 0x04, /* field_chg */
		0x49, 0x02, /* vact_st2 */
		0x7b, 0x04, /* vact_st3 */
		0xae, 0x06, /* vact_st4 */
		0x01, 0x00, 0x33, 0x02, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_INTERLACED,
	},
	.vic = 5,
};

static const struct hdmi_preset_conf hdmi_conf_1080i59_94_sb_half = {
	.core = {
		.h_blank = {0x18, 0x01},
		.v2_blank = {0x32, 0x02},
		.v1_blank = {0x16, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0x98, 0x08},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x01},
		.v_blank_f0 = {0x49, 0x02},
		.v_blank_f1 = {0x65, 0x04},
		.h_sync_start = {0x56, 0x00},
		.h_sync_end = {0x82, 0x00},
		.v_sync_line_bef_2 = {0x07, 0x00},
		.v_sync_line_bef_1 = {0x02, 0x00},
		.v_sync_line_aft_2 = {0x39, 0x02},
		.v_sync_line_aft_1 = {0x34, 0x02},
		.v_sync_line_aft_pxl_2 = {0xa4, 0x04},
		.v_sync_line_aft_pxl_1 = {0xa4, 0x04},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x98, 0x08, /* h_fsz */
		0x18, 0x01, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x16, 0x00, 0x1c, 0x02, /* vact */
		0x65, 0x04, /* field_chg */
		0x49, 0x02, /* vact_st2 */
		0x7b, 0x04, /* vact_st3 */
		0xae, 0x06, /* vact_st4 */
		0x01, 0x00, 0x33, 0x02, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_INTERLACED,
	},
	.vic = 5,
};

static const struct hdmi_preset_conf hdmi_conf_1080i50_sb_half = {
	.core = {
		.h_blank = {0xd0, 0x02},
		.v2_blank = {0x32, 0x02},
		.v1_blank = {0x16, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0x50, 0x0a},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x01},
		.v_blank_f0 = {0x49, 0x02},
		.v_blank_f1 = {0x65, 0x04},
		.h_sync_start = {0x0e, 0x02},
		.h_sync_end = {0x3a, 0x02},
		.v_sync_line_bef_2 = {0x07, 0x00},
		.v_sync_line_bef_1 = {0x02, 0x00},
		.v_sync_line_aft_2 = {0x39, 0x02},
		.v_sync_line_aft_1 = {0x34, 0x02},
		.v_sync_line_aft_pxl_2 = {0x38, 0x07},
		.v_sync_line_aft_pxl_1 = {0x38, 0x07},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x50, 0x0a, /* h_fsz */
		0xd0, 0x02, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x16, 0x00, 0x1c, 0x02, /* vact */
		0x65, 0x04, /* field_chg */
		0x49, 0x02, /* vact_st2 */
		0x7b, 0x04, /* vact_st3 */
		0xae, 0x06, /* vact_st4 */
		0x01, 0x00, 0x33, 0x02, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_INTERLACED,
	},
	.vic = 20,
};

static const struct hdmi_preset_conf hdmi_conf_1080p60_sb_half = {
	.core = {
		.h_blank = {0x18, 0x01},
		.v2_blank = {0x65, 0x04},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0x98, 0x08},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x56, 0x00},
		.h_sync_end = {0x82, 0x00},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x98, 0x08, /* h_fsz */
		0x18, 0x01, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 16,
};

static const struct hdmi_preset_conf hdmi_conf_1080p60_tb = {
	.core = {
		.h_blank = {0x18, 0x01},
		.v2_blank = {0x65, 0x04},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0x98, 0x08},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x56, 0x00},
		.h_sync_end = {0x82, 0x00},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x98, 0x08, /* h_fsz */
		0x18, 0x01, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 16,
};

static const struct hdmi_preset_conf hdmi_conf_1080p30_sb_half = {
	.core = {
		.h_blank = {0x18, 0x01},
		.v2_blank = {0x65, 0x04},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0x98, 0x08},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x56, 0x00},
		.h_sync_end = {0x82, 0x00},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x98, 0x08, /* h_fsz */
		0x18, 0x01, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 34,
};

static const struct hdmi_preset_conf hdmi_conf_1080p30_tb = {
	.core = {
		.h_blank = {0x18, 0x01},
		.v2_blank = {0x65, 0x04},
		.v1_blank = {0x2d, 0x00},
		.v_line = {0x65, 0x04},
		.h_line = {0x98, 0x08},
		.hsync_pol = {0x00},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f0 = {0xff, 0xff},
		.v_blank_f1 = {0xff, 0xff},
		.h_sync_start = {0x56, 0x00},
		.h_sync_end = {0x82, 0x00},
		.v_sync_line_bef_2 = {0x09, 0x00},
		.v_sync_line_bef_1 = {0x04, 0x00},
		.v_sync_line_aft_2 = {0xff, 0xff},
		.v_sync_line_aft_1 = {0xff, 0xff},
		.v_sync_line_aft_pxl_2 = {0xff, 0xff},
		.v_sync_line_aft_pxl_1 = {0xff, 0xff},
		.v_blank_f2 = {0xff, 0xff},
		.v_blank_f3 = {0xff, 0xff},
		.v_blank_f4 = {0xff, 0xff},
		.v_blank_f5 = {0xff, 0xff},
		.v_sync_line_aft_3 = {0xff, 0xff},
		.v_sync_line_aft_4 = {0xff, 0xff},
		.v_sync_line_aft_5 = {0xff, 0xff},
		.v_sync_line_aft_6 = {0xff, 0xff},
		.v_sync_line_aft_pxl_3 = {0xff, 0xff},
		.v_sync_line_aft_pxl_4 = {0xff, 0xff},
		.v_sync_line_aft_pxl_5 = {0xff, 0xff},
		.v_sync_line_aft_pxl_6 = {0xff, 0xff},
		.vact_space_1 = {0xff, 0xff},
		.vact_space_2 = {0xff, 0xff},
		.vact_space_3 = {0xff, 0xff},
		.vact_space_4 = {0xff, 0xff},
		.vact_space_5 = {0xff, 0xff},
		.vact_space_6 = {0xff, 0xff},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x98, 0x08, /* h_fsz */
		0x18, 0x01, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x00, 0x00, /* vact_st3 */
		0x00, 0x00, /* vact_st4 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
		0x00, /* 3d FP */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
	.vic = 34,
};

static const struct hdmi_3d_info info_2d = {
	.is_3d = HDMI_VIDEO_FORMAT_2D,
};

static const struct hdmi_3d_info info_3d_sb_h = {
	.is_3d = HDMI_VIDEO_FORMAT_3D,
	.fmt_3d = HDMI_3D_FORMAT_SB_HALF,
};

static const struct hdmi_3d_info info_3d_tb = {
	.is_3d = HDMI_VIDEO_FORMAT_3D,
	.fmt_3d = HDMI_3D_FORMAT_TB,
};

static const struct hdmi_3d_info info_3d_fp = {
	.is_3d = HDMI_VIDEO_FORMAT_3D,
	.fmt_3d = HDMI_3D_FORMAT_FP,
};

const struct hdmi_conf hdmi_conf[] = {
	{ V4L2_DV_480P59_94,	   &hdmi_conf_480p59_94,	&info_2d },
	{ V4L2_DV_480P60,	   &hdmi_conf_480p60,		&info_2d },
	{ V4L2_DV_576P50,	   &hdmi_conf_576p50,		&info_2d },
	{ V4L2_DV_720P50,	   &hdmi_conf_720p50,		&info_2d },
	{ V4L2_DV_720P59_94,	   &hdmi_conf_720p59_94,	&info_2d },
	{ V4L2_DV_720P60,	   &hdmi_conf_720p60,		&info_2d },
	{ V4L2_DV_1080I50,	   &hdmi_conf_1080i50,		&info_2d },
	{ V4L2_DV_1080I59_94,	   &hdmi_conf_1080i59_94,	&info_2d },
	{ V4L2_DV_1080I60,	   &hdmi_conf_1080i60,		&info_2d },
	{ V4L2_DV_1080P24,	   &hdmi_conf_1080p24,		&info_2d },
	{ V4L2_DV_1080P25,	   &hdmi_conf_1080p25,		&info_2d },
	{ V4L2_DV_1080P30,	   &hdmi_conf_1080p30,		&info_2d },
	{ V4L2_DV_1080P50,	   &hdmi_conf_1080p50,		&info_2d },
	{ V4L2_DV_1080P59_94,	   &hdmi_conf_1080p59_94,	&info_2d },
	{ V4L2_DV_1080P60,	   &hdmi_conf_1080p60,		&info_2d },
	{ V4L2_DV_720P60_SB_HALF,  &hdmi_conf_720p60_sb_half,	&info_3d_sb_h },
	{ V4L2_DV_720P60_TB,	   &hdmi_conf_720p60_tb,	&info_3d_tb },
	{ V4L2_DV_720P59_94_SB_HALF, &hdmi_conf_720p59_94_sb_half,
		&info_3d_sb_h },
	{ V4L2_DV_720P59_94_TB,	   &hdmi_conf_720p59_94_tb,	&info_3d_tb },
	{ V4L2_DV_720P50_SB_HALF,  &hdmi_conf_720p50_sb_half,	&info_3d_sb_h },
	{ V4L2_DV_720P50_TB,	   &hdmi_conf_720p50_tb,	&info_3d_tb },
	{ V4L2_DV_1080P24_FP,	   &hdmi_conf_1080p24_fp,	&info_3d_fp },
	{ V4L2_DV_1080P24_SB_HALF, &hdmi_conf_1080p24_sb_half,	&info_3d_sb_h },
	{ V4L2_DV_1080P24_TB,	   &hdmi_conf_1080p24_tb,	&info_3d_tb },
	{ V4L2_DV_1080P23_98_FP,   &hdmi_conf_1080p23_98_fp,	&info_3d_fp },
	{ V4L2_DV_1080P23_98_SB_HALF, &hdmi_conf_1080p23_98_sb_half,
		&info_3d_sb_h },
	{ V4L2_DV_1080P23_98_TB,   &hdmi_conf_1080p23_98_tb,	&info_3d_tb },
	{ V4L2_DV_1080I60_SB_HALF, &hdmi_conf_1080i60_sb_half,	&info_3d_sb_h },
	{ V4L2_DV_1080I59_94_SB_HALF, &hdmi_conf_1080i59_94_sb_half,
		&info_3d_sb_h },
	{ V4L2_DV_1080I50_SB_HALF, &hdmi_conf_1080i50_sb_half, &info_3d_sb_h },
	{ V4L2_DV_1080P60_SB_HALF, &hdmi_conf_1080p60_sb_half, &info_3d_sb_h },
	{ V4L2_DV_1080P60_TB, &hdmi_conf_1080p60_tb, &info_3d_tb },
	{ V4L2_DV_1080P30_SB_HALF, &hdmi_conf_1080p30_sb_half, &info_3d_sb_h },
	{ V4L2_DV_1080P30_TB, &hdmi_conf_1080p30_tb, &info_3d_tb },
};

const int hdmi_pre_cnt = ARRAY_SIZE(hdmi_conf);

irqreturn_t hdmi_irq_handler(int irq, void *dev_data)
{
	struct hdmi_device *hdev = dev_data;
	u32 intc_flag;

	if (hdev->streaming == HDMI_STREAMING) {
		intc_flag = hdmi_read(hdev, HDMI_INTC_FLAG_0);
		/* clearing flags for HPD plug/unplug */
		if (intc_flag & HDMI_INTC_FLAG_HPD_UNPLUG) {
			if (hdev->hdcp_info.hdcp_enable)
				hdcp_stop(hdev);
			hdmi_write_mask(hdev, HDMI_INTC_FLAG_0, ~0,
					HDMI_INTC_FLAG_HPD_UNPLUG);
			queue_work(system_nrt_wq, &hdev->hpd_work);
		}
		if (intc_flag & HDMI_INTC_FLAG_HPD_PLUG) {
			hdmi_write_mask(hdev, HDMI_INTC_FLAG_0, ~0,
					HDMI_INTC_FLAG_HPD_PLUG);
		}

		if (intc_flag & HDMI_INTC_FLAG_HDCP) {
			pr_debug("%s: hdcp interrupt occur\n", __func__);
			hdcp_irq_handler(hdev);
			hdmi_write_mask(hdev, HDMI_INTC_FLAG_0, ~0,
					HDMI_INTC_FLAG_HDCP);
		}
	}

	return IRQ_HANDLED;
}

void hdmi_reg_init(struct hdmi_device *hdev)
{
	/* enable HPD interrupts */
	hdmi_write_mask(hdev, HDMI_INTC_CON_0, ~0, HDMI_INTC_EN_GLOBAL |
		HDMI_INTC_EN_HPD_PLUG | HDMI_INTC_EN_HPD_UNPLUG);
	/* hdcp interrupt must be prohibitted before starting hdcp */
	hdmi_write_mask(hdev, HDMI_INTC_CON_0, 0, HDMI_INTC_EN_HDCP);
	/* choose HDMI mode */
	hdmi_write_mask(hdev, HDMI_MODE_SEL,
		HDMI_MODE_HDMI_EN, HDMI_MODE_MASK);
	/* disable bluescreen */
	hdmi_write_mask(hdev, HDMI_CON_0, 0, HDMI_BLUE_SCR_EN);
	/* HDMI_CON_0 should be masked the reserved bit
	 * if the bit [3] is enabled, abnormal color could be displayed on TV */
	hdmi_write_mask(hdev, HDMI_CON_0, 0, HDMI_CON_0_MASK);
	/* enable AVI packet every vsync, fixes purple line problem */
	hdmi_writeb(hdev, HDMI_AVI_CON, HDMI_AVI_CON_EVERY_VSYNC);
	/* RGB888 is default output format of HDMI,
	 * look to CEA-861-D, table 7 for more detail */
	hdmi_writeb(hdev, HDMI_AVI_BYTE(1), 0 << 5);
}

void hdmi_set_dvi_mode(struct hdmi_device *hdev)
{
	u32 val;

	hdmi_write_mask(hdev, HDMI_MODE_SEL, hdev->dvi_mode ? HDMI_MODE_DVI_EN :
		HDMI_MODE_HDMI_EN, HDMI_MODE_MASK);

	if (hdev->dvi_mode)
		val = HDMI_VID_PREAMBLE_DIS | HDMI_GUARD_BAND_DIS;
	else
		val = HDMI_VID_PREAMBLE_EN | HDMI_GUARD_BAND_EN;
	hdmi_write(hdev, HDMI_CON_2, val);
}

void hdmi_timing_apply(struct hdmi_device *hdev,
	const struct hdmi_preset_conf *conf)
{
	const struct hdmi_core_regs *core = &conf->core;
	const struct hdmi_tg_regs *tg = &conf->tg;

	/* setting core registers */
	hdmi_writeb(hdev, HDMI_H_BLANK_0, core->h_blank[0]);
	hdmi_writeb(hdev, HDMI_H_BLANK_1, core->h_blank[1]);
	hdmi_writeb(hdev, HDMI_V2_BLANK_0, core->v2_blank[0]);
	hdmi_writeb(hdev, HDMI_V2_BLANK_1, core->v2_blank[1]);
	hdmi_writeb(hdev, HDMI_V1_BLANK_0, core->v1_blank[0]);
	hdmi_writeb(hdev, HDMI_V1_BLANK_1, core->v1_blank[1]);
	hdmi_writeb(hdev, HDMI_V_LINE_0, core->v_line[0]);
	hdmi_writeb(hdev, HDMI_V_LINE_1, core->v_line[1]);
	hdmi_writeb(hdev, HDMI_H_LINE_0, core->h_line[0]);
	hdmi_writeb(hdev, HDMI_H_LINE_1, core->h_line[1]);
	hdmi_writeb(hdev, HDMI_HSYNC_POL, core->hsync_pol[0]);
	hdmi_writeb(hdev, HDMI_VSYNC_POL, core->vsync_pol[0]);
	hdmi_writeb(hdev, HDMI_INT_PRO_MODE, core->int_pro_mode[0]);
	hdmi_writeb(hdev, HDMI_V_BLANK_F0_0, core->v_blank_f0[0]);
	hdmi_writeb(hdev, HDMI_V_BLANK_F0_1, core->v_blank_f0[1]);
	hdmi_writeb(hdev, HDMI_V_BLANK_F1_0, core->v_blank_f1[0]);
	hdmi_writeb(hdev, HDMI_V_BLANK_F1_1, core->v_blank_f1[1]);
	hdmi_writeb(hdev, HDMI_H_SYNC_START_0, core->h_sync_start[0]);
	hdmi_writeb(hdev, HDMI_H_SYNC_START_1, core->h_sync_start[1]);
	hdmi_writeb(hdev, HDMI_H_SYNC_END_0, core->h_sync_end[0]);
	hdmi_writeb(hdev, HDMI_H_SYNC_END_1, core->h_sync_end[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_BEF_2_0, core->v_sync_line_bef_2[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_BEF_2_1, core->v_sync_line_bef_2[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_BEF_1_0, core->v_sync_line_bef_1[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_BEF_1_1, core->v_sync_line_bef_1[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_2_0, core->v_sync_line_aft_2[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_2_1, core->v_sync_line_aft_2[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_1_0, core->v_sync_line_aft_1[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_1_1, core->v_sync_line_aft_1[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_PXL_2_0,
			core->v_sync_line_aft_pxl_2[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_PXL_2_1,
			core->v_sync_line_aft_pxl_2[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_PXL_1_0,
			core->v_sync_line_aft_pxl_1[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_PXL_1_1,
			core->v_sync_line_aft_pxl_1[1]);
	hdmi_writeb(hdev, HDMI_V_BLANK_F2_0, core->v_blank_f2[0]);
	hdmi_writeb(hdev, HDMI_V_BLANK_F2_1, core->v_blank_f2[1]);
	hdmi_writeb(hdev, HDMI_V_BLANK_F3_0, core->v_blank_f3[0]);
	hdmi_writeb(hdev, HDMI_V_BLANK_F3_1, core->v_blank_f3[1]);
	hdmi_writeb(hdev, HDMI_V_BLANK_F4_0, core->v_blank_f4[0]);
	hdmi_writeb(hdev, HDMI_V_BLANK_F4_1, core->v_blank_f4[1]);
	hdmi_writeb(hdev, HDMI_V_BLANK_F5_0, core->v_blank_f5[0]);
	hdmi_writeb(hdev, HDMI_V_BLANK_F5_1, core->v_blank_f5[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_3_0, core->v_sync_line_aft_3[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_3_1, core->v_sync_line_aft_3[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_4_0, core->v_sync_line_aft_4[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_4_1, core->v_sync_line_aft_4[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_5_0, core->v_sync_line_aft_5[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_5_1, core->v_sync_line_aft_5[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_6_0, core->v_sync_line_aft_6[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_6_1, core->v_sync_line_aft_6[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_PXL_3_0,
			core->v_sync_line_aft_pxl_3[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_PXL_3_1,
			core->v_sync_line_aft_pxl_3[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_PXL_4_0,
			core->v_sync_line_aft_pxl_4[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_PXL_4_1,
			core->v_sync_line_aft_pxl_4[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_PXL_5_0,
			core->v_sync_line_aft_pxl_5[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_PXL_5_1,
			core->v_sync_line_aft_pxl_5[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_PXL_6_0,
			core->v_sync_line_aft_pxl_6[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_LINE_AFT_PXL_6_1,
			core->v_sync_line_aft_pxl_6[1]);
	hdmi_writeb(hdev, HDMI_VACT_SPACE_1_0, core->vact_space_1[0]);
	hdmi_writeb(hdev, HDMI_VACT_SPACE_1_1, core->vact_space_1[1]);
	hdmi_writeb(hdev, HDMI_VACT_SPACE_2_0, core->vact_space_2[0]);
	hdmi_writeb(hdev, HDMI_VACT_SPACE_2_1, core->vact_space_2[1]);
	hdmi_writeb(hdev, HDMI_VACT_SPACE_3_0, core->vact_space_3[0]);
	hdmi_writeb(hdev, HDMI_VACT_SPACE_3_1, core->vact_space_3[1]);
	hdmi_writeb(hdev, HDMI_VACT_SPACE_4_0, core->vact_space_4[0]);
	hdmi_writeb(hdev, HDMI_VACT_SPACE_4_1, core->vact_space_4[1]);
	hdmi_writeb(hdev, HDMI_VACT_SPACE_5_0, core->vact_space_5[0]);
	hdmi_writeb(hdev, HDMI_VACT_SPACE_5_1, core->vact_space_5[1]);
	hdmi_writeb(hdev, HDMI_VACT_SPACE_6_0, core->vact_space_6[0]);
	hdmi_writeb(hdev, HDMI_VACT_SPACE_6_1, core->vact_space_6[1]);

	/* Timing generator registers */
	hdmi_writeb(hdev, HDMI_TG_H_FSZ_L, tg->h_fsz_l);
	hdmi_writeb(hdev, HDMI_TG_H_FSZ_H, tg->h_fsz_h);
	hdmi_writeb(hdev, HDMI_TG_HACT_ST_L, tg->hact_st_l);
	hdmi_writeb(hdev, HDMI_TG_HACT_ST_H, tg->hact_st_h);
	hdmi_writeb(hdev, HDMI_TG_HACT_SZ_L, tg->hact_sz_l);
	hdmi_writeb(hdev, HDMI_TG_HACT_SZ_H, tg->hact_sz_h);
	hdmi_writeb(hdev, HDMI_TG_V_FSZ_L, tg->v_fsz_l);
	hdmi_writeb(hdev, HDMI_TG_V_FSZ_H, tg->v_fsz_h);
	hdmi_writeb(hdev, HDMI_TG_VSYNC_L, tg->vsync_l);
	hdmi_writeb(hdev, HDMI_TG_VSYNC_H, tg->vsync_h);
	hdmi_writeb(hdev, HDMI_TG_VSYNC2_L, tg->vsync2_l);
	hdmi_writeb(hdev, HDMI_TG_VSYNC2_H, tg->vsync2_h);
	hdmi_writeb(hdev, HDMI_TG_VACT_ST_L, tg->vact_st_l);
	hdmi_writeb(hdev, HDMI_TG_VACT_ST_H, tg->vact_st_h);
	hdmi_writeb(hdev, HDMI_TG_VACT_SZ_L, tg->vact_sz_l);
	hdmi_writeb(hdev, HDMI_TG_VACT_SZ_H, tg->vact_sz_h);
	hdmi_writeb(hdev, HDMI_TG_FIELD_CHG_L, tg->field_chg_l);
	hdmi_writeb(hdev, HDMI_TG_FIELD_CHG_H, tg->field_chg_h);
	hdmi_writeb(hdev, HDMI_TG_VACT_ST2_L, tg->vact_st2_l);
	hdmi_writeb(hdev, HDMI_TG_VACT_ST2_H, tg->vact_st2_h);
	hdmi_writeb(hdev, HDMI_TG_VACT_ST3_L, tg->vact_st3_l);
	hdmi_writeb(hdev, HDMI_TG_VACT_ST3_H, tg->vact_st3_h);
	hdmi_writeb(hdev, HDMI_TG_VACT_ST4_L, tg->vact_st4_l);
	hdmi_writeb(hdev, HDMI_TG_VACT_ST4_H, tg->vact_st4_h);
	hdmi_writeb(hdev, HDMI_TG_VSYNC_TOP_HDMI_L, tg->vsync_top_hdmi_l);
	hdmi_writeb(hdev, HDMI_TG_VSYNC_TOP_HDMI_H, tg->vsync_top_hdmi_h);
	hdmi_writeb(hdev, HDMI_TG_VSYNC_BOT_HDMI_L, tg->vsync_bot_hdmi_l);
	hdmi_writeb(hdev, HDMI_TG_VSYNC_BOT_HDMI_H, tg->vsync_bot_hdmi_h);
	hdmi_writeb(hdev, HDMI_TG_FIELD_TOP_HDMI_L, tg->field_top_hdmi_l);
	hdmi_writeb(hdev, HDMI_TG_FIELD_TOP_HDMI_H, tg->field_top_hdmi_h);
	hdmi_writeb(hdev, HDMI_TG_FIELD_BOT_HDMI_L, tg->field_bot_hdmi_l);
	hdmi_writeb(hdev, HDMI_TG_FIELD_BOT_HDMI_H, tg->field_bot_hdmi_h);
	hdmi_writeb(hdev, HDMI_TG_3D, tg->tg_3d);
}

const u8 *hdmiphy_preset2conf(u32 preset)
{
	int i;
	for (i = 0; i < hdmiphy_conf_cnt; ++i)
		if (hdmiphy_conf[i].preset == preset)
			return hdmiphy_conf[i].data;
	return NULL;
}

int hdmi_conf_apply(struct hdmi_device *hdmi_dev)
{
	struct device *dev = hdmi_dev->dev;
	const struct hdmi_preset_conf *conf = hdmi_dev->cur_conf;
	struct v4l2_dv_preset preset;
	const u8 *data;
	u8 buffer[32];
	int ret, tries;

	dev_dbg(dev, "%s\n", __func__);

	/* configure presets */
	preset.preset = hdmi_dev->cur_preset;

	if (soc_is_exynos5410()) {
		data = hdmiphy_preset2conf(preset.preset);
		if (!data) {
			dev_err(dev, "format not supported\n");
			return -EINVAL;
		}

		memcpy(buffer, data, 32);
		hdmiphy_set_mode(hdmi_dev, 0);
		hdmiphy_conf_store(hdmi_dev, buffer);
		hdmiphy_set_mode(hdmi_dev, 1);
	} else {
		ret = v4l2_subdev_call(hdmi_dev->phy_sd, video, s_dv_preset, &preset);
		if (ret) {
			dev_err(dev, "failed to set preset (%u)\n", preset.preset);
			return ret;
		}
	}

	hdmi_reg_init(hdmi_dev);

	/* waiting for HDMIPHY's PLL to get to steady state */
	for (tries = 100; tries; --tries) {
		if (is_hdmiphy_ready(hdmi_dev))
			break;

		msleep(1);
	}

	/* steady state not achieved */
	if (tries == 0) {
		dev_err(dev, "hdmiphy's pll could not reach steady state.\n");
		return -EIO;
	}

	/* setting core registers */
	hdmi_timing_apply(hdmi_dev, conf);

	return 0;
}

int is_hdmiphy_ready(struct hdmi_device *hdev)
{
	u32 val = hdmi_read(hdev, HDMI_PHY_STATUS_0);
	if (val & HDMI_PHY_STATUS_READY)
		return 1;

	return 0;
}

void hdmi_enable(struct hdmi_device *hdev, int on)
{
	if (on) {
		hdmi_write_mask(hdev, HDMI_CON_0, ~0, HDMI_EN);
		hdmi_write_mask(hdev, HDMI_CON_0, 0, HDMI_CON_0_MASK);
	} else {
		hdmi_write_mask(hdev, HDMI_CON_0, 0, HDMI_EN);
		hdmi_write_mask(hdev, HDMI_CON_0, 0, HDMI_CON_0_MASK);
	}
}

void hdmi_hpd_enable(struct hdmi_device *hdev, int on)
{
	/* enable/disable HPD interrupts */
	if (on)
		hdmi_write_mask(hdev, HDMI_INTC_CON_0, ~0, HDMI_INTC_EN_GLOBAL |
				HDMI_INTC_EN_HPD_PLUG | HDMI_INTC_EN_HPD_UNPLUG);
	else
		hdmi_write_mask(hdev, HDMI_INTC_CON_0, 0, HDMI_INTC_EN_GLOBAL |
				HDMI_INTC_EN_HPD_PLUG | HDMI_INTC_EN_HPD_UNPLUG);
}

void hdmi_hpd_clear_int(struct hdmi_device *hdev)
{
	hdmi_write_mask(hdev, HDMI_INTC_FLAG_0, ~0,
			HDMI_INTC_FLAG_HPD_PLUG | HDMI_INTC_FLAG_HPD_UNPLUG);
}

void hdmi_tg_enable(struct hdmi_device *hdev, int on)
{
	u32 mask;

	mask = (hdev->cur_conf->mbus_fmt.field == V4L2_FIELD_INTERLACED) ?
			HDMI_TG_EN | HDMI_FIELD_EN : HDMI_TG_EN;

	if (on)
		hdmi_write_mask(hdev, HDMI_TG_CMD, ~0, mask);
	else
		hdmi_write_mask(hdev, HDMI_TG_CMD, 0, mask);
}

static u8 hdmi_chksum(struct hdmi_device *hdev, u32 start, u8 len, u32 hdr_sum)
{
	int i;

	/* hdr_sum : header0 + header1 + header2
	 * start : start address of packet byte1
	 * len : packet bytes - 1 */
	for (i = 0; i < len; ++i)
		hdr_sum += hdmi_read(hdev, start + i * 4);

	return (u8)(0x100 - (hdr_sum & 0xff));
}

void hdmi_reg_stop_vsi(struct hdmi_device *hdev)
{
	hdmi_writeb(hdev, HDMI_VSI_CON, HDMI_VSI_CON_DO_NOT_TRANSMIT);
}

void hdmi_reg_infoframe(struct hdmi_device *hdev,
		struct hdmi_infoframe *infoframe)
{
	struct device *dev = hdev->dev;
	const struct hdmi_3d_info *info = hdmi_preset2info(hdev->cur_preset);
	u32 hdr_sum;
	u8 chksum;
	u32 aspect_ratio;
	u32 vic;

	dev_dbg(dev, "%s: InfoFrame type = 0x%x\n", __func__, infoframe->type);

	/* Packets must NOT be transferred in case of DVI mode
	 * DVI mode doesn't allow Data Island Period. If any packet is tranferred,
	 * It makes Data Island Period */
	if (hdev->dvi_mode) {
		hdmi_writeb(hdev, HDMI_VSI_CON, HDMI_VSI_CON_DO_NOT_TRANSMIT);
		hdmi_writeb(hdev, HDMI_AVI_CON, HDMI_AVI_CON_DO_NOT_TRANSMIT);
		hdmi_write(hdev, HDMI_AUI_CON, HDMI_AUI_CON_NO_TRAN);
		return;
	}

	switch (infoframe->type) {
	case HDMI_PACKET_TYPE_VSI:
		hdmi_writeb(hdev, HDMI_VSI_CON, HDMI_VSI_CON_EVERY_VSYNC);
		hdmi_writeb(hdev, HDMI_VSI_HEADER0, infoframe->type);
		hdmi_writeb(hdev, HDMI_VSI_HEADER1, infoframe->ver);
		/* 0x000C03 : 24-bit IEEE Registration Identifier */
		hdmi_writeb(hdev, HDMI_VSI_DATA(1), 0x03);
		hdmi_writeb(hdev, HDMI_VSI_DATA(2), 0x0c);
		hdmi_writeb(hdev, HDMI_VSI_DATA(3), 0x00);
		hdmi_writeb(hdev, HDMI_VSI_DATA(4),
			HDMI_VSI_DATA04_VIDEO_FORMAT(info->is_3d));
		hdmi_writeb(hdev, HDMI_VSI_DATA(5),
			HDMI_VSI_DATA05_3D_STRUCTURE(info->fmt_3d));
		if (info->fmt_3d == HDMI_3D_FORMAT_SB_HALF) {
			infoframe->len += 1;
			hdmi_writeb(hdev, HDMI_VSI_DATA(6),
			(u8)HDMI_VSI_DATA06_3D_EXT_DATA(HDMI_H_SUB_SAMPLE));
		}
		hdmi_writeb(hdev, HDMI_VSI_HEADER2, infoframe->len);
		hdr_sum = infoframe->type + infoframe->ver + infoframe->len;
		chksum = hdmi_chksum(hdev, HDMI_VSI_DATA(1), infoframe->len, hdr_sum);
		dev_dbg(dev, "VSI checksum = 0x%x\n", chksum);
		hdmi_writeb(hdev, HDMI_VSI_DATA(0), chksum);
		break;
	case HDMI_PACKET_TYPE_AVI:
		hdmi_writeb(hdev, HDMI_AVI_CON, HDMI_AVI_CON_EVERY_VSYNC);
		hdmi_writeb(hdev, HDMI_AVI_HEADER0, infoframe->type);
		hdmi_writeb(hdev, HDMI_AVI_HEADER1, infoframe->ver);
		hdmi_writeb(hdev, HDMI_AVI_HEADER2, infoframe->len);
		hdr_sum = infoframe->type + infoframe->ver + infoframe->len;
		hdmi_writeb(hdev, HDMI_AVI_BYTE(1), hdev->output_fmt << 5 |
				AVI_ACTIVE_FORMAT_VALID);
		if (hdev->aspect == HDMI_ASPECT_RATIO_4_3 &&
				(hdev->cur_preset == V4L2_DV_480P59_94 ||
				 hdev->cur_preset == V4L2_DV_480P60)) {
			aspect_ratio = AVI_PIC_ASPECT_RATIO_4_3;
			/* 2 : 480P59.94/60Hz 4:3 aspect ratio */
			vic = 2;
		} else if (hdev->aspect == HDMI_ASPECT_RATIO_4_3 &&
				hdev->cur_preset == V4L2_DV_576P50) {
			aspect_ratio = AVI_PIC_ASPECT_RATIO_4_3;
			/* 17 : 576P50Hz 4:3 aspect ratio */
			vic = 17;
		} else {
			aspect_ratio = AVI_PIC_ASPECT_RATIO_16_9;
			vic = hdev->cur_conf->vic;
		}

		hdmi_writeb(hdev, HDMI_AVI_BYTE(2), aspect_ratio |
				AVI_SAME_AS_PIC_ASPECT_RATIO);
		dev_dbg(dev, "VIC code = %d\n", vic);
		hdmi_writeb(hdev, HDMI_AVI_BYTE(4), vic);
		chksum = hdmi_chksum(hdev, HDMI_AVI_BYTE(1), infoframe->len, hdr_sum);
		dev_dbg(dev, "AVI checksum = 0x%x\n", chksum);
		hdmi_writeb(hdev, HDMI_AVI_CHECK_SUM, chksum);
		break;
	case HDMI_PACKET_TYPE_AUI:
		hdmi_write(hdev, HDMI_AUI_CON, HDMI_AUI_CON_TRANS_EVERY_VSYNC);
		hdmi_writeb(hdev, HDMI_AUI_HEADER0, infoframe->type);
		hdmi_writeb(hdev, HDMI_AUI_HEADER1, infoframe->ver);
		hdmi_writeb(hdev, HDMI_AUI_HEADER2, infoframe->len);
		hdr_sum = infoframe->type + infoframe->ver + infoframe->len;
		/*
		 * AUI_BYTE(1)[2:0] : Audio Channel Count
		 * AUI_BYTE(4)[7:0] : Speaker Placement
		 */
		if (hdev->audio_channel_count == 6) {
			hdmi_writeb(hdev, HDMI_AUI_BYTE(1), HDMI_AUI_DATA_CC_6CH);
			hdmi_writeb(hdev, HDMI_AUI_BYTE(4), HDMI_AUI_DATA_CA_6CH);
		} else if (hdev->audio_channel_count == 8) {
			hdmi_writeb(hdev, HDMI_AUI_BYTE(1), HDMI_AUI_DATA_CC_8CH);
			hdmi_writeb(hdev, HDMI_AUI_BYTE(4), HDMI_AUI_DATA_CA_8CH);
		} else {
			hdmi_writeb(hdev, HDMI_AUI_BYTE(1), HDMI_AUI_DATA_CC_2CH);
			hdmi_writeb(hdev, HDMI_AUI_BYTE(4), HDMI_AUI_DATA_CA_2CH);
		}
		/* AUI_BYTE(3)[7:0] : Should Be Zero */
		hdmi_writeb(hdev, HDMI_AUI_BYTE(3), 0x00);
		/* AUI_BYTE(2)[4:2] : Sampling Frequency */
		if (hdev->sample_rate == 32000)
			hdmi_writeb(hdev, HDMI_AUI_BYTE(2), HDMI_AUI_DATA_SF_32);
		else if (hdev->sample_rate == 48000)
			hdmi_writeb(hdev, HDMI_AUI_BYTE(2), HDMI_AUI_DATA_SF_48);
		else if (hdev->sample_rate == 96000)
			hdmi_writeb(hdev, HDMI_AUI_BYTE(2), HDMI_AUI_DATA_SF_96);
		else
			hdmi_writeb(hdev, HDMI_AUI_BYTE(2), HDMI_AUI_DATA_SF_44_1);
		/* AUI_BYTE(2)[1:0] : Sample Size */
		if (hdev->sample_size == 16)
			hdmi_writeb(hdev, HDMI_AUI_BYTE(2), HDMI_AUI_DATA_SS_16BIT);
		else if (hdev->sample_size == 20)
			hdmi_writeb(hdev, HDMI_AUI_BYTE(2), HDMI_AUI_DATA_SS_20BIT);
		else
			hdmi_writeb(hdev, HDMI_AUI_BYTE(2), HDMI_AUI_DATA_SS_24BIT);
		chksum = hdmi_chksum(hdev, HDMI_AUI_BYTE(1), infoframe->len, hdr_sum);
		dev_dbg(dev, "AUI checksum = 0x%x\n", chksum);
		hdmi_writeb(hdev, HDMI_AUI_CHECK_SUM, chksum);
		break;
	default:
		break;
	}
}

void hdmi_reg_set_limits(struct hdmi_device *hdev)
{
	if (hdev->output_fmt)
		hdmi_write(hdev, HDMI_CON_1, HDMI_PX_LMT_CTRL(2));
	else
		hdmi_write(hdev, HDMI_CON_1, HDMI_PX_LMT_CTRL(1));

	if (hdev->color_range == 1 || hdev->color_range == 3) {
		hdmi_write(hdev, HDMI_YMAX, HDMI_YMAX_VAL(235));
		hdmi_write(hdev, HDMI_YMIN, HDMI_YMIN_VAL(16));
		hdmi_write(hdev, HDMI_CMAX, HDMI_CMAX_VAL(235));
		hdmi_write(hdev, HDMI_CMIN, HDMI_CMIN_VAL(16));
	} else {
		hdmi_write(hdev, HDMI_YMAX, HDMI_YMAX_VAL(255));
		hdmi_write(hdev, HDMI_YMIN, HDMI_YMIN_VAL(0));
		hdmi_write(hdev, HDMI_CMAX, HDMI_CMAX_VAL(255));
		hdmi_write(hdev, HDMI_CMIN, HDMI_CMIN_VAL(0));
	}
}

void hdmi_reg_set_acr(struct hdmi_device *hdev)
{
	u32 n, cts;
	int sample_rate = hdev->sample_rate;

	if (hdev->dvi_mode) {
		hdmi_write(hdev, HDMI_ACR_CON, HDMI_ACR_CON_TX_MODE_NO_TX);
		return;
	}

	if (sample_rate == 32000) {
		n = 4096;
		cts = 27000;
	} else if (sample_rate == 44100) {
		n = 6272;
		cts = 30000;
	} else if (sample_rate == 48000) {
		n = 6144;
		cts = 27000;
	} else if (sample_rate == 88200) {
		n = 12544;
		cts = 30000;
	} else if (sample_rate == 96000) {
		n = 12288;
		cts = 27000;
	} else if (sample_rate == 176400) {
		n = 25088;
		cts = 30000;
	} else if (sample_rate == 192000) {
		n = 24576;
		cts = 27000;
	} else {
		n = 0;
		cts = 0;
	}

	hdmi_write(hdev, HDMI_ACR_N0, HDMI_ACR_N0_VAL(n));
	hdmi_write(hdev, HDMI_ACR_N1, HDMI_ACR_N1_VAL(n));
	hdmi_write(hdev, HDMI_ACR_N2, HDMI_ACR_N2_VAL(n));

	/* transfer ACR packet */
	hdmi_write(hdev, HDMI_ACR_CON, HDMI_ACR_CON_TX_MODE_MESURED_CTS);
}

void hdmi_reg_spdif_audio_init(struct hdmi_device *hdev)
{
	u32 val;
	int bps, rep_time;

	hdmi_write(hdev, HDMI_I2S_CLK_CON, HDMI_I2S_CLK_ENABLE);

	val = HDMI_SPDIFIN_CFG_NOISE_FILTER_2_SAMPLE |
		HDMI_SPDIFIN_CFG_PCPD_MANUAL |
		HDMI_SPDIFIN_CFG_WORD_LENGTH_MANUAL |
		HDMI_SPDIFIN_CFG_UVCP_REPORT |
		HDMI_SPDIFIN_CFG_HDMI_2_BURST |
		HDMI_SPDIFIN_CFG_DATA_ALIGN_32;
	hdmi_write(hdev, HDMI_SPDIFIN_CONFIG_1, val);
	hdmi_write(hdev, HDMI_SPDIFIN_CONFIG_2, 0);

	bps = hdev->audio_codec == HDMI_AUDIO_PCM ? hdev->bits_per_sample : 16;
	rep_time = hdev->audio_codec == HDMI_AUDIO_AC3 ? 1536 * 2 - 1 : 0;
	val = HDMI_SPDIFIN_USER_VAL_REPETITION_TIME_LOW(rep_time) |
		HDMI_SPDIFIN_USER_VAL_WORD_LENGTH_24;
	hdmi_write(hdev, HDMI_SPDIFIN_USER_VALUE_1, val);
	val = HDMI_SPDIFIN_USER_VAL_REPETITION_TIME_HIGH(rep_time);
	hdmi_write(hdev, HDMI_SPDIFIN_USER_VALUE_2, val);
	hdmi_write(hdev, HDMI_SPDIFIN_USER_VALUE_3, 0);
	hdmi_write(hdev, HDMI_SPDIFIN_USER_VALUE_4, 0);

	val = HDMI_I2S_IN_ENABLE | HDMI_I2S_AUD_SPDIF | HDMI_I2S_MUX_ENABLE;
	hdmi_write(hdev, HDMI_I2S_IN_MUX_CON, val);

	hdmi_write(hdev, HDMI_I2S_MUX_CH, HDMI_I2S_CH_ALL_EN);
	hdmi_write(hdev, HDMI_I2S_MUX_CUV, HDMI_I2S_CUV_RL_EN);

	hdmi_write_mask(hdev, HDMI_SPDIFIN_CLK_CTRL, 0, HDMI_SPDIFIN_CLK_ON);
	hdmi_write_mask(hdev, HDMI_SPDIFIN_CLK_CTRL, ~0, HDMI_SPDIFIN_CLK_ON);

	hdmi_write(hdev, HDMI_SPDIFIN_OP_CTRL, HDMI_SPDIFIN_STATUS_CHECK_MODE);
	hdmi_write(hdev, HDMI_SPDIFIN_OP_CTRL,
			HDMI_SPDIFIN_STATUS_CHECK_MODE_HDMI);
}

void hdmi_reg_i2s_audio_init(struct hdmi_device *hdev)
{
	u32 data_num, bit_ch, sample_frq, val;
	int sample_rate = hdev->sample_rate;
	int bits_per_sample = hdev->bits_per_sample;

	if (bits_per_sample == 16) {
		data_num = 1;
		bit_ch = 0;
	} else if (bits_per_sample == 20) {
		data_num = 2;
		bit_ch  = 1;
	} else if (bits_per_sample == 24) {
		data_num = 3;
		bit_ch  = 1;
	} else if (bits_per_sample == 32) {
		data_num = 1;
		bit_ch  = 2;
	} else {
		data_num = 1;
		bit_ch = 0;
	}

	/* reset I2S */
	hdmi_write(hdev, HDMI_I2S_CLK_CON, HDMI_I2S_CLK_DISABLE);
	hdmi_write(hdev, HDMI_I2S_CLK_CON, HDMI_I2S_CLK_ENABLE);

	hdmi_write_mask(hdev, HDMI_I2S_DSD_CON, 0, HDMI_I2S_DSD_ENABLE);

	/* Configuration I2S input ports. Configure I2S_PIN_SEL_0~4 */
	val = HDMI_I2S_SEL_SCLK(5) | HDMI_I2S_SEL_LRCK(6);
	hdmi_write(hdev, HDMI_I2S_PIN_SEL_0, val);
	if (hdev->audio_channel_count == 6 || hdev->audio_channel_count == 8)
		val = HDMI_I2S_SEL_SDATA1(3) | HDMI_I2S_SEL_SDATA0(4);
	else
		val = HDMI_I2S_SEL_SDATA1(1) | HDMI_I2S_SEL_SDATA0(4);
	hdmi_write(hdev, HDMI_I2S_PIN_SEL_1, val);
	val = HDMI_I2S_SEL_SDATA3(1) | HDMI_I2S_SEL_SDATA2(2);
	hdmi_write(hdev, HDMI_I2S_PIN_SEL_2, val);
	hdmi_write(hdev, HDMI_I2S_PIN_SEL_3, HDMI_I2S_SEL_DSD(0));

	/* I2S_CON_1 & 2 */
	val = HDMI_I2S_SCLK_FALLING_EDGE | HDMI_I2S_L_CH_LOW_POL;
	hdmi_write(hdev, HDMI_I2S_CON_1, val);
	val = HDMI_I2S_MSB_FIRST_MODE | HDMI_I2S_SET_BIT_CH(bit_ch) |
		HDMI_I2S_SET_SDATA_BIT(data_num) | HDMI_I2S_BASIC_FORMAT;
	hdmi_write(hdev, HDMI_I2S_CON_2, val);

	if (sample_rate == 32000)
		sample_frq = 0x3;
	else if (sample_rate == 44100)
		sample_frq = 0x0;
	else if (sample_rate == 48000)
		sample_frq = 0x2;
	else if (sample_rate == 96000)
		sample_frq = 0xa;
	else
		sample_frq = 0;

	/* Configure register related to CUV information */
	val = HDMI_I2S_CH_STATUS_MODE_0 | HDMI_I2S_2AUD_CH_WITHOUT_PREEMPH |
		HDMI_I2S_COPYRIGHT | HDMI_I2S_LINEAR_PCM |
		HDMI_I2S_CONSUMER_FORMAT;
	hdmi_write(hdev, HDMI_I2S_CH_ST_0, val);
	hdmi_write(hdev, HDMI_I2S_CH_ST_1, HDMI_I2S_CD_PLAYER);

	if (hdev->audio_channel_count == 6 || hdev->audio_channel_count == 8) {
		hdmi_writeb(hdev, HDMI_I2S_CH_ST_2,
			HDMI_I2S_SET_SOURCE_NUM(0) | HDMI_I2S_SET_CHANNEL_NUM(0x6));
		hdmi_writeb(hdev, HDMI_ASP_CON,
			HDMI_AUD_MODE_MULTI_CH | HDMI_AUD_SP_AUD2_EN |
			HDMI_AUD_SP_AUD1_EN | HDMI_AUD_SP_AUD0_EN);
		hdmi_writeb(hdev, HDMI_ASP_CHCFG0,
			HDMI_SPK0R_SEL_I_PCM0R | HDMI_SPK0L_SEL_I_PCM0L);
		hdmi_writeb(hdev, HDMI_ASP_CHCFG1,
			HDMI_SPK0L_SEL_I_PCM1R | HDMI_SPK0R_SEL_I_PCM1L);
		hdmi_writeb(hdev, HDMI_ASP_CHCFG2,
			HDMI_SPK0R_SEL_I_PCM2R | HDMI_SPK0L_SEL_I_PCM2L);
		hdmi_writeb(hdev, HDMI_ASP_CHCFG3,
			HDMI_SPK0R_SEL_I_PCM3R | HDMI_SPK0L_SEL_I_PCM3L);
	} else {
		hdmi_writeb(hdev, HDMI_I2S_CH_ST_2, HDMI_I2S_SET_SOURCE_NUM(0));
		hdmi_writeb(hdev, HDMI_ASP_CON,
			HDMI_AUD_NO_DST_DOUBLE | HDMI_AUD_TYPE_SAMPLE |
			HDMI_AUD_MODE_TWO_CH | HDMI_AUD_SP_ALL_DIS);
		hdmi_writeb(hdev, HDMI_ASP_CHCFG0,
			HDMI_SPK0R_SEL_I_PCM0R | HDMI_SPK0L_SEL_I_PCM0L);
		hdmi_writeb(hdev, HDMI_ASP_CHCFG1,
			HDMI_SPK0R_SEL_I_PCM0R | HDMI_SPK0L_SEL_I_PCM0L);
		hdmi_writeb(hdev, HDMI_ASP_CHCFG2,
			HDMI_SPK0R_SEL_I_PCM0R | HDMI_SPK0L_SEL_I_PCM0L);
		hdmi_writeb(hdev, HDMI_ASP_CHCFG3,
			HDMI_SPK0R_SEL_I_PCM0R | HDMI_SPK0L_SEL_I_PCM0L);
	}
	hdmi_writeb(hdev, HDMI_ASP_SP_FLAT, HDMI_ASP_SP_FLAT_AUD_SAMPLE);

	val = HDMI_I2S_CLK_ACCUR_LEVEL_1 |
		HDMI_I2S_SET_SAMPLING_FREQ(sample_frq);
	hdmi_write(hdev, HDMI_I2S_CH_ST_3, val);
	if (hdev->sample_size == 16)
		val = HDMI_I2S_ORG_SAMPLING_FREQ_44_1 |
			HDMI_I2S_WORD_LENGTH_MAX20_16BITS |
			HDMI_I2S_WORD_LENGTH_MAX_20BITS;
	else if (hdev->sample_size == 20)
		val = HDMI_I2S_ORG_SAMPLING_FREQ_44_1 |
			HDMI_I2S_WORD_LENGTH_MAX24_20BITS |
			HDMI_I2S_WORD_LENGTH_MAX_24BITS;
	else
		val = HDMI_I2S_ORG_SAMPLING_FREQ_44_1 |
			HDMI_I2S_WORD_LENGTH_MAX24_24BITS |
			HDMI_I2S_WORD_LENGTH_MAX_24BITS;
	hdmi_write(hdev, HDMI_I2S_CH_ST_4, val);

	hdmi_write(hdev, HDMI_I2S_CH_ST_CON, HDMI_I2S_CH_STATUS_RELOAD);

	val = HDMI_I2S_IN_ENABLE | HDMI_I2S_AUD_I2S | HDMI_I2S_CUV_I2S_ENABLE
		| HDMI_I2S_MUX_ENABLE;
	hdmi_write(hdev, HDMI_I2S_IN_MUX_CON, val);

	val = HDMI_I2S_CH0_L_EN | HDMI_I2S_CH0_R_EN | HDMI_I2S_CH1_L_EN |
		HDMI_I2S_CH1_R_EN | HDMI_I2S_CH2_L_EN | HDMI_I2S_CH2_R_EN |
		HDMI_I2S_CH3_L_EN | HDMI_I2S_CH3_R_EN;
	hdmi_write(hdev, HDMI_I2S_MUX_CH, val);

	val = HDMI_I2S_CUV_L_EN | HDMI_I2S_CUV_R_EN;
	hdmi_write(hdev, HDMI_I2S_MUX_CUV, val);
}

void hdmi_audio_enable(struct hdmi_device *hdev, int on)
{
	if (on) {
		if (hdev->dvi_mode || !hdev->audio_enable)
			return;
		hdmi_write_mask(hdev, HDMI_CON_0, ~0, HDMI_ASP_ENABLE);
		hdmi_write_mask(hdev, HDMI_CON_0, 0, HDMI_CON_0_MASK);
	} else {
		hdmi_write_mask(hdev, HDMI_CON_0, 0, HDMI_ASP_ENABLE);
		hdmi_write_mask(hdev, HDMI_CON_0, 0, HDMI_CON_0_MASK);
	}
}

void hdmi_bluescreen_enable(struct hdmi_device *hdev, int on)
{
	if (on) {
		hdmi_write_mask(hdev, HDMI_CON_0, ~0, HDMI_BLUE_SCR_EN);
		hdmi_write_mask(hdev, HDMI_CON_0, 0, HDMI_CON_0_MASK);
	} else {
		hdmi_write_mask(hdev, HDMI_CON_0, 0, HDMI_BLUE_SCR_EN);
		hdmi_write_mask(hdev, HDMI_CON_0, 0, HDMI_CON_0_MASK);
	}
}

void hdmi_reg_mute(struct hdmi_device *hdev, int on)
{
	hdmi_bluescreen_enable(hdev, on);
	hdmi_audio_enable(hdev, !on);
}

void hdmi_reg_set_int_hpd(struct hdmi_device *hdev)
{
	hdmi_write_mask(hdev, HDMI_INTC_CON_0, 0, HDMI_INTC_EN_GLOBAL);
	s5p_v4l2_int_src_hdmi_hpd();
	hdmi_write(hdev, HDMI_HPD_GEN0, 0xff);
	hdmi_write_mask(hdev, HDMI_INTC_CON_0, ~0, HDMI_INTC_EN_GLOBAL |
			HDMI_INTC_EN_HPD_PLUG | HDMI_INTC_EN_HPD_UNPLUG);
}

void hdmi_reg_set_ext_hpd(struct hdmi_device *hdev)
{
	hdmi_write_mask(hdev, HDMI_INTC_FLAG_0, ~0, HDMI_INTC_FLAG_HPD_PLUG |
			HDMI_INTC_FLAG_HPD_UNPLUG);
	hdmi_write_mask(hdev, HDMI_INTC_CON_0, 0, HDMI_INTC_EN_GLOBAL);
	s5p_v4l2_int_src_ext_hpd();
}

int hdmi_hpd_status(struct hdmi_device *hdev)
{
	return hdmi_read(hdev, HDMI_HPD_STATUS);
}

int is_hdmi_streaming(struct hdmi_device *hdev)
{
	if (switch_get_state(&hdev->hpd_switch) &&
			(hdev->streaming == HDMI_STREAMING))
		return 1;
	return 0;
}

u8 hdmi_get_int_mask(struct hdmi_device *hdev)
{
	return hdmi_readb(hdev, HDMI_INTC_CON_0);
}

void hdmi_set_int_mask(struct hdmi_device *hdev, u8 mask, int en)
{
	if (en) {
		mask |= HDMI_INTC_EN_GLOBAL;
		hdmi_write_mask(hdev, HDMI_INTC_CON_0, ~0, mask);
	} else
		hdmi_write_mask(hdev, HDMI_INTC_CON_0, 0, mask);
}

void hdmi_sw_hpd_enable(struct hdmi_device *hdev, int en)
{
	if (en)
		hdmi_write_mask(hdev, HDMI_HPD, ~0, HDMI_HPD_SEL_I_HPD);
	else
		hdmi_write_mask(hdev, HDMI_HPD, 0, HDMI_HPD_SEL_I_HPD);
}

void hdmi_sw_hpd_plug(struct hdmi_device *hdev, int en)
{
	if (en)
		hdmi_write_mask(hdev, HDMI_HPD, ~0, HDMI_SW_HPD_PLUGGED);
	else
		hdmi_write_mask(hdev, HDMI_HPD, 0, HDMI_SW_HPD_PLUGGED);
}

void hdmi_phy_sw_reset(struct hdmi_device *hdev)
{
	hdmi_write_mask(hdev, HDMI_PHY_RSTOUT, ~0, HDMI_PHY_SW_RSTOUT);
	mdelay(10);
	hdmi_write_mask(hdev, HDMI_PHY_RSTOUT,  0, HDMI_PHY_SW_RSTOUT);
}

void hdmi_sw_reset(struct hdmi_device *hdev)
{
	hdmi_write_mask(hdev, HDMI_CORE_RSTOUT, 0, HDMI_CORE_SW_RSTOUT);
	mdelay(10);
	hdmi_write_mask(hdev, HDMI_CORE_RSTOUT, ~0, HDMI_CORE_SW_RSTOUT);
}

void hdmiphy_set_mode(struct hdmi_device *hdev, int en)
{
	if (en)
		hdmiphy_write_mask(hdev, HDMI_PHY_REG7C, ~0,
				HDMI_PHY_MODE_SET_DONE);
	else
		hdmiphy_write_mask(hdev, HDMI_PHY_REG7C, 0,
				HDMI_PHY_MODE_SET_DONE);
}

void hdmiphy_set_power(struct hdmi_device *hdev, int en)
{
	if (en)
		hdmi_write_mask(hdev, HDMI_PHY_CON_0, 0, HDMI_PHY_POWER_OFF);
	else
		hdmi_write_mask(hdev, HDMI_PHY_CON_0, ~0, HDMI_PHY_POWER_OFF);
}

void hdmiphy_conf_store(struct hdmi_device *hdev, const char *buf)
{
	int i;

	for (i = 1; i < 32; i++)
		writeb(buf[i], hdev->phy_regs + HDMI_PHY_REG00 + (i * 4));
}

static int hdmi_debugfs_show(struct seq_file *s, void *unused)
{
	struct hdmi_device *hdev = s->private;
	int i;

	mutex_lock(&hdev->mutex);

	if (!hdev->streaming) {
		mutex_unlock(&hdev->mutex);
		seq_printf(s, "Not streaming\n");
		return 0;
	}

#define DUMPREG(reg_id) \
		seq_printf(s, "%-20s %08x\n", #reg_id, \
			   readl(hdev->regs + reg_id))

	DUMPREG(HDMI_INTC_CON_0);
	DUMPREG(HDMI_INTC_FLAG_0);
	DUMPREG(HDMI_HPD_STATUS);
	DUMPREG(HDMI_INTC_CON_1);
	DUMPREG(HDMI_INTC_FLAG_1);
	DUMPREG(HDMI_PHY_STATUS_0);
	DUMPREG(HDMI_PHY_STATUS_PLL);
	DUMPREG(HDMI_PHY_CON_0);
	DUMPREG(HDMI_PHY_RSTOUT);
	DUMPREG(HDMI_PHY_VPLL);
	DUMPREG(HDMI_PHY_CMU);
	DUMPREG(HDMI_CORE_RSTOUT);

	DUMPREG(HDMI_CON_0);
	DUMPREG(HDMI_CON_1);
	DUMPREG(HDMI_CON_2);
	DUMPREG(HDMI_STATUS);
	DUMPREG(HDMI_STATUS_EN);
	DUMPREG(HDMI_HPD);
	DUMPREG(HDMI_MODE_SEL);
	DUMPREG(HDMI_ENC_EN);
	DUMPREG(HDMI_DC_CONTROL);
	DUMPREG(HDMI_VIDEO_PATTERN_GEN);

	DUMPREG(HDMI_AVI_CON);
	DUMPREG(HDMI_AVI_HEADER0);
	DUMPREG(HDMI_AVI_HEADER1);
	DUMPREG(HDMI_AVI_HEADER2);
	DUMPREG(HDMI_AVI_CHECK_SUM);
	for (i = 1; i < 6; ++i)
		DUMPREG(HDMI_AVI_BYTE(i));

	DUMPREG(HDMI_VSI_CON);
	DUMPREG(HDMI_VSI_HEADER0);
	DUMPREG(HDMI_VSI_HEADER1);
	DUMPREG(HDMI_VSI_HEADER2);
	for (i = 0; i < 7; ++i)
		DUMPREG(HDMI_VSI_DATA(i));
	DUMPREG(HDMI_AUI_CON);
	DUMPREG(HDMI_ACR_CON);

#undef DUMPREG

	mutex_unlock(&hdev->mutex);
	return 0;
}

static int hdmi_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, hdmi_debugfs_show, inode->i_private);
}

static const struct file_operations hdmi_debugfs_fops = {
	.open           = hdmi_debugfs_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

void hdmi_debugfs_init(struct hdmi_device *hdev)
{
	debugfs_create_file(dev_name(hdev->dev), S_IRUGO, NULL,
			    hdev, &hdmi_debugfs_fops);
}

void hdmi_dumpregs(struct hdmi_device *hdev, char *prefix)
{
#define DUMPREG(reg_id) \
	dev_dbg(hdev->dev, "%s:" #reg_id " = %08x\n", prefix, \
		readl(hdev->regs + reg_id))

	int i;

	dev_dbg(hdev->dev, "%s: ---- CONTROL REGISTERS ----\n", prefix);
	DUMPREG(HDMI_INTC_CON_0);
	DUMPREG(HDMI_INTC_FLAG_0);
	DUMPREG(HDMI_HPD_STATUS);
	DUMPREG(HDMI_INTC_CON_1);
	DUMPREG(HDMI_INTC_FLAG_1);
	DUMPREG(HDMI_PHY_STATUS_0);
	DUMPREG(HDMI_PHY_STATUS_PLL);
	DUMPREG(HDMI_PHY_CON_0);
	DUMPREG(HDMI_PHY_RSTOUT);
	DUMPREG(HDMI_PHY_VPLL);
	DUMPREG(HDMI_PHY_CMU);
	DUMPREG(HDMI_CORE_RSTOUT);

	dev_dbg(hdev->dev, "%s: ---- CORE REGISTERS ----\n", prefix);
	DUMPREG(HDMI_CON_0);
	DUMPREG(HDMI_CON_1);
	DUMPREG(HDMI_CON_2);
	DUMPREG(HDMI_STATUS);
	DUMPREG(HDMI_STATUS_EN);
	DUMPREG(HDMI_HPD);
	DUMPREG(HDMI_MODE_SEL);
	DUMPREG(HDMI_ENC_EN);
	DUMPREG(HDMI_DC_CONTROL);
	DUMPREG(HDMI_VIDEO_PATTERN_GEN);

	dev_dbg(hdev->dev, "%s: ---- CORE SYNC REGISTERS ----\n", prefix);
	DUMPREG(HDMI_H_BLANK_0);
	DUMPREG(HDMI_H_BLANK_1);
	DUMPREG(HDMI_V2_BLANK_0);
	DUMPREG(HDMI_V2_BLANK_1);
	DUMPREG(HDMI_V1_BLANK_0);
	DUMPREG(HDMI_V1_BLANK_1);
	DUMPREG(HDMI_V_LINE_0);
	DUMPREG(HDMI_V_LINE_1);
	DUMPREG(HDMI_H_LINE_0);
	DUMPREG(HDMI_H_LINE_1);
	DUMPREG(HDMI_HSYNC_POL);

	DUMPREG(HDMI_VSYNC_POL);
	DUMPREG(HDMI_INT_PRO_MODE);
	DUMPREG(HDMI_V_BLANK_F0_0);
	DUMPREG(HDMI_V_BLANK_F0_1);
	DUMPREG(HDMI_V_BLANK_F1_0);
	DUMPREG(HDMI_V_BLANK_F1_1);

	DUMPREG(HDMI_H_SYNC_START_0);
	DUMPREG(HDMI_H_SYNC_START_1);
	DUMPREG(HDMI_H_SYNC_END_0);
	DUMPREG(HDMI_H_SYNC_END_1);

	DUMPREG(HDMI_V_SYNC_LINE_BEF_2_0);
	DUMPREG(HDMI_V_SYNC_LINE_BEF_2_1);
	DUMPREG(HDMI_V_SYNC_LINE_BEF_1_0);
	DUMPREG(HDMI_V_SYNC_LINE_BEF_1_1);

	DUMPREG(HDMI_V_SYNC_LINE_AFT_2_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_2_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_1_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_1_1);

	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_2_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_2_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_1_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_1_1);

	DUMPREG(HDMI_V_BLANK_F2_0);
	DUMPREG(HDMI_V_BLANK_F2_1);
	DUMPREG(HDMI_V_BLANK_F3_0);
	DUMPREG(HDMI_V_BLANK_F3_1);
	DUMPREG(HDMI_V_BLANK_F4_0);
	DUMPREG(HDMI_V_BLANK_F4_1);
	DUMPREG(HDMI_V_BLANK_F5_0);
	DUMPREG(HDMI_V_BLANK_F5_1);

	DUMPREG(HDMI_V_SYNC_LINE_AFT_3_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_3_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_4_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_4_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_5_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_5_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_6_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_6_1);

	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_3_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_3_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_4_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_4_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_5_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_5_1);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_6_0);
	DUMPREG(HDMI_V_SYNC_LINE_AFT_PXL_6_1);

	DUMPREG(HDMI_VACT_SPACE_1_0);
	DUMPREG(HDMI_VACT_SPACE_1_1);
	DUMPREG(HDMI_VACT_SPACE_2_0);
	DUMPREG(HDMI_VACT_SPACE_2_1);
	DUMPREG(HDMI_VACT_SPACE_3_0);
	DUMPREG(HDMI_VACT_SPACE_3_1);
	DUMPREG(HDMI_VACT_SPACE_4_0);
	DUMPREG(HDMI_VACT_SPACE_4_1);
	DUMPREG(HDMI_VACT_SPACE_5_0);
	DUMPREG(HDMI_VACT_SPACE_5_1);
	DUMPREG(HDMI_VACT_SPACE_6_0);
	DUMPREG(HDMI_VACT_SPACE_6_1);

	dev_dbg(hdev->dev, "%s: ---- TG REGISTERS ----\n", prefix);
	DUMPREG(HDMI_TG_CMD);
	DUMPREG(HDMI_TG_H_FSZ_L);
	DUMPREG(HDMI_TG_H_FSZ_H);
	DUMPREG(HDMI_TG_HACT_ST_L);
	DUMPREG(HDMI_TG_HACT_ST_H);
	DUMPREG(HDMI_TG_HACT_SZ_L);
	DUMPREG(HDMI_TG_HACT_SZ_H);
	DUMPREG(HDMI_TG_V_FSZ_L);
	DUMPREG(HDMI_TG_V_FSZ_H);
	DUMPREG(HDMI_TG_VSYNC_L);
	DUMPREG(HDMI_TG_VSYNC_H);
	DUMPREG(HDMI_TG_VSYNC2_L);
	DUMPREG(HDMI_TG_VSYNC2_H);
	DUMPREG(HDMI_TG_VACT_ST_L);
	DUMPREG(HDMI_TG_VACT_ST_H);
	DUMPREG(HDMI_TG_VACT_SZ_L);
	DUMPREG(HDMI_TG_VACT_SZ_H);
	DUMPREG(HDMI_TG_FIELD_CHG_L);
	DUMPREG(HDMI_TG_FIELD_CHG_H);
	DUMPREG(HDMI_TG_VACT_ST2_L);
	DUMPREG(HDMI_TG_VACT_ST2_H);
	DUMPREG(HDMI_TG_VACT_ST3_L);
	DUMPREG(HDMI_TG_VACT_ST3_H);
	DUMPREG(HDMI_TG_VACT_ST4_L);
	DUMPREG(HDMI_TG_VACT_ST4_H);
	DUMPREG(HDMI_TG_VSYNC_TOP_HDMI_L);
	DUMPREG(HDMI_TG_VSYNC_TOP_HDMI_H);
	DUMPREG(HDMI_TG_VSYNC_BOT_HDMI_L);
	DUMPREG(HDMI_TG_VSYNC_BOT_HDMI_H);
	DUMPREG(HDMI_TG_FIELD_TOP_HDMI_L);
	DUMPREG(HDMI_TG_FIELD_TOP_HDMI_H);
	DUMPREG(HDMI_TG_FIELD_BOT_HDMI_L);
	DUMPREG(HDMI_TG_FIELD_BOT_HDMI_H);
	DUMPREG(HDMI_TG_3D);

	dev_dbg(hdev->dev, "%s: ---- PACKET REGISTERS ----\n", prefix);
	DUMPREG(HDMI_AVI_CON);
	DUMPREG(HDMI_AVI_HEADER0);
	DUMPREG(HDMI_AVI_HEADER1);
	DUMPREG(HDMI_AVI_HEADER2);
	DUMPREG(HDMI_AVI_CHECK_SUM);
	for (i = 1; i < 6; ++i)
		DUMPREG(HDMI_AVI_BYTE(i));

	DUMPREG(HDMI_VSI_CON);
	DUMPREG(HDMI_VSI_HEADER0);
	DUMPREG(HDMI_VSI_HEADER1);
	DUMPREG(HDMI_VSI_HEADER2);
	for (i = 0; i < 7; ++i)
		DUMPREG(HDMI_VSI_DATA(i));
	DUMPREG(HDMI_AUI_CON);
	DUMPREG(HDMI_ACR_CON);

#undef DUMPREG
}
