/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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

struct VideoModeTable {
	struct crt_mode_table *crtc;
	int mode_array;
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

struct VideoModeTable *viafb_get_mode(int hres, int vres);
struct VideoModeTable *viafb_get_rb_mode(int hres, int vres);

#endif /* __VIAMODE_H__ */
