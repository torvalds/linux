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
	int ModeIndex;
	struct crt_mode_table *crtc;
	int mode_array;
};

struct patch_table {
	int mode_index;
	int table_length;
	struct io_reg *io_reg_table;
};

struct res_map_refresh {
	int hres;
	int vres;
	int pixclock;
	int vmode_refresh;
};

#define NUM_TOTAL_RES_MAP_REFRESH ARRAY_SIZE(res_map_refresh_tbl)
#define NUM_TOTAL_CEA_MODES  ARRAY_SIZE(CEA_HDMI_Modes)
#define NUM_TOTAL_CN400_ModeXregs ARRAY_SIZE(CN400_ModeXregs)
#define NUM_TOTAL_CN700_ModeXregs ARRAY_SIZE(CN700_ModeXregs)
#define NUM_TOTAL_KM400_ModeXregs ARRAY_SIZE(KM400_ModeXregs)
#define NUM_TOTAL_CX700_ModeXregs ARRAY_SIZE(CX700_ModeXregs)
#define NUM_TOTAL_VX800_ModeXregs ARRAY_SIZE(VX800_ModeXregs)
#define NUM_TOTAL_CLE266_ModeXregs ARRAY_SIZE(CLE266_ModeXregs)
#define NUM_TOTAL_PATCH_MODE ARRAY_SIZE(res_patch_table)
#define NUM_TOTAL_MODETABLE ARRAY_SIZE(CLE266Modes)

/********************/
/* Mode Table       */
/********************/

/* 480x640 */
extern struct crt_mode_table CRTM480x640[1];
/* 640x480*/
extern struct crt_mode_table CRTM640x480[5];
/*720x480 (GTF)*/
extern struct crt_mode_table CRTM720x480[1];
/*720x576 (GTF)*/
extern struct crt_mode_table CRTM720x576[1];
/* 800x480 (CVT) */
extern struct crt_mode_table CRTM800x480[1];
/* 800x600*/
extern struct crt_mode_table CRTM800x600[5];
/* 848x480 (CVT) */
extern struct crt_mode_table CRTM848x480[1];
/*856x480 (GTF) convert to 852x480*/
extern struct crt_mode_table CRTM852x480[1];
/*1024x512 (GTF)*/
extern struct crt_mode_table CRTM1024x512[1];
/* 1024x600*/
extern struct crt_mode_table CRTM1024x600[1];
/* 1024x768*/
extern struct crt_mode_table CRTM1024x768[4];
/* 1152x864*/
extern struct crt_mode_table CRTM1152x864[1];
/* 1280x720 (HDMI 720P)*/
extern struct crt_mode_table CRTM1280x720[2];
/*1280x768 (GTF)*/
extern struct crt_mode_table CRTM1280x768[2];
/* 1280x800 (CVT) */
extern struct crt_mode_table CRTM1280x800[1];
/*1280x960*/
extern struct crt_mode_table CRTM1280x960[1];
/* 1280x1024*/
extern struct crt_mode_table CRTM1280x1024[3];
/* 1368x768 (GTF) */
extern struct crt_mode_table CRTM1368x768[1];
/*1440x1050 (GTF)*/
extern struct crt_mode_table CRTM1440x1050[1];
/* 1600x1200*/
extern struct crt_mode_table CRTM1600x1200[2];
/* 1680x1050 (CVT) */
extern struct crt_mode_table CRTM1680x1050[2];
/* 1680x1050 (CVT Reduce Blanking) */
extern struct crt_mode_table CRTM1680x1050_RB[1];
/* 1920x1080 (CVT)*/
extern struct crt_mode_table CRTM1920x1080[1];
/* 1920x1080 (CVT with Reduce Blanking) */
extern struct crt_mode_table CRTM1920x1080_RB[1];
/* 1920x1440*/
extern struct crt_mode_table CRTM1920x1440[2];
/* 1400x1050 (CVT) */
extern struct crt_mode_table CRTM1400x1050[2];
/* 1400x1050 (CVT Reduce Blanking) */
extern struct crt_mode_table CRTM1400x1050_RB[1];
/* 960x600 (CVT) */
extern struct crt_mode_table CRTM960x600[1];
/* 1000x600 (GTF) */
extern struct crt_mode_table CRTM1000x600[1];
/* 1024x576 (GTF) */
extern struct crt_mode_table CRTM1024x576[1];
/* 1088x612 (CVT) */
extern struct crt_mode_table CRTM1088x612[1];
/* 1152x720 (CVT) */
extern struct crt_mode_table CRTM1152x720[1];
/* 1200x720 (GTF) */
extern struct crt_mode_table CRTM1200x720[1];
/* 1280x600 (GTF) */
extern struct crt_mode_table CRTM1280x600[1];
/* 1360x768 (CVT) */
extern struct crt_mode_table CRTM1360x768[1];
/* 1360x768 (CVT Reduce Blanking) */
extern struct crt_mode_table CRTM1360x768_RB[1];
/* 1366x768 (GTF) */
extern struct crt_mode_table CRTM1366x768[2];
/* 1440x900 (CVT) */
extern struct crt_mode_table CRTM1440x900[2];
/* 1440x900 (CVT Reduce Blanking) */
extern struct crt_mode_table CRTM1440x900_RB[1];
/* 1600x900 (CVT) */
extern struct crt_mode_table CRTM1600x900[1];
/* 1600x900 (CVT Reduce Blanking) */
extern struct crt_mode_table CRTM1600x900_RB[1];
/* 1600x1024 (GTF) */
extern struct crt_mode_table CRTM1600x1024[1];
/* 1792x1344 (DMT) */
extern struct crt_mode_table CRTM1792x1344[1];
/* 1856x1392 (DMT) */
extern struct crt_mode_table CRTM1856x1392[1];
/* 1920x1200 (CVT) */
extern struct crt_mode_table CRTM1920x1200[1];
/* 1920x1200 (CVT with Reduce Blanking) */
extern struct crt_mode_table CRTM1920x1200_RB[1];
/* 2048x1536 (CVT) */
extern struct crt_mode_table CRTM2048x1536[1];
extern struct VideoModeTable CLE266Modes[47];
extern struct crt_mode_table CEAM1280x720[1];
extern struct crt_mode_table CEAM1920x1080[1];
extern struct VideoModeTable CEA_HDMI_Modes[2];

extern struct res_map_refresh res_map_refresh_tbl[61];
extern struct io_reg CN400_ModeXregs[52];
extern struct io_reg CN700_ModeXregs[66];
extern struct io_reg KM400_ModeXregs[55];
extern struct io_reg CX700_ModeXregs[58];
extern struct io_reg VX800_ModeXregs[58];
extern struct io_reg CLE266_ModeXregs[32];
extern struct io_reg PM1024x768[2];
extern struct patch_table res_patch_table[1];
extern struct VPITTable VPIT;
#endif /* __VIAMODE_H__ */
