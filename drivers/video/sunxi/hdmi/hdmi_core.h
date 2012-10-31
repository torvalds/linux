/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __HDMI_CORE_H__
#define __HDMI_CORE_H__

#include "hdmi_hal.h"

extern void __iomem *HDMI_BASE;

#define HDMI_WUINT32(offset, value)	writel(value, HDMI_BASE + offset)
#define HDMI_RUINT32(offset)		readl(HDMI_BASE + offset)
#define HDMI_WUINT16(offset, value)	writew(value, HDMI_BASE + offset)
#define HDMI_RUINT16(offset)		readw(HDMI_BASE + offset)
#define HDMI_WUINT8(offset, value)	writeb(value, HDMI_BASE + offset)
#define HDMI_RUINT8(offset)		readb(HDMI_BASE + offset)

#define Abort_Current_Operation			0
#define Special_Offset_Address_Read		1
#define Explicit_Offset_Address_Write		2
#define Implicit_Offset_Address_Write		3
#define Explicit_Offset_Address_Read		4
#define Implicit_Offset_Address_Read		5
#define Explicit_Offset_Address_E_DDC_Read	6
#define Implicit_Offset_Address_E_DDC_Read	7

typedef struct video_timing {
	__s32 VIC;
	__s32 PCLK;
	__s32 AVI_PR;

	__s32 INPUTX;
	__s32 INPUTY;
	__s32 HT;
	__s32 HBP;
	__s32 HFP;
	__s32 HPSW;
	__s32 VT;
	__s32 VBP;
	__s32 VFP;
	__s32 VPSW;
} HDMI_VIDE_INFO;

typedef struct audio_timing {

	__s32 audio_en;
	__s32 sample_rate;
	__s32 channel_num;

	__s32 CTS;
	__s32 ACR_N;
	__s32 CH_STATUS0;
	__s32 CH_STATUS1;

} HDMI_AUDIO_INFO;

__s32 hdmi_core_initial(void);
__s32 hdmi_core_open(void);
__s32 hdmi_core_close(void);
__s32 hdmi_main_task_loop(void);
__s32 Hpd_Check(void);
__s32 ParseEDID(void);
__s32 video_config(__s32 vic);
__s32 audio_config(void);

extern __u32 hdmi_pll; /* 0: video pll 0; 1: video pll 1 */
extern __u32 hdmi_clk;

void DDC_Init(void);
void send_ini_sequence(void);
__s32 DDC_Read(char cmd, char pointer, char offset, int nbyte, char *pbuf);

extern __u8 EDID_Buf[1024];
extern __u8 Device_Support_VIC[512];

extern __bool video_enable;
extern __s32 hdmi_state;
extern __s32 video_mode;
extern HDMI_AUDIO_INFO audio_info;

#endif
