/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 */

#ifndef __VIAMODE_H__
#define __VIAMODE_H__

#include "global.h"

struct VPITTable {
	unsigned char Misc;
	unsigned char SR[StdSR];
	unsigned char GR[StdGR];
	unsigned char AR[StdAR];
};

struct patch_table {
	int table_length;
	struct io_reg *io_reg_table;
};

extern int NUM_TOTAL_CN400_ModeXregs;
extern int NUM_TOTAL_CN700_ModeXregs;
extern int NUM_TOTAL_KM400_ModeXregs;
extern int NUM_TOTAL_CX700_ModeXregs;
extern int NUM_TOTAL_VX855_ModeXregs;
extern int NUM_TOTAL_CLE266_ModeXregs;
extern int NUM_TOTAL_PATCH_MODE;

extern struct io_reg CN400_ModeXregs[];
extern struct io_reg CN700_ModeXregs[];
extern struct io_reg KM400_ModeXregs[];
extern struct io_reg CX700_ModeXregs[];
extern struct io_reg VX800_ModeXregs[];
extern struct io_reg VX855_ModeXregs[];
extern struct io_reg CLE266_ModeXregs[];
extern struct io_reg PM1024x768[];
extern struct patch_table res_patch_table[];
extern struct VPITTable VPIT;

const struct fb_videomode *viafb_get_best_mode(int hres, int vres,
	int refresh);
const struct fb_videomode *viafb_get_best_rb_mode(int hres, int vres,
	int refresh);

#endif /* __VIAMODE_H__ */
