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

#include "drv_hdmi_i.h"

extern void __iomem *HDMI_BASE;

#define HDMI_State_Wait_Hpd		0x02
#define HDMI_State_Rx_Sense		0x03
#define HDMI_State_EDID_Parse		0x04
#define HDMI_State_Wait_Video_config	0x05
#define HDMI_State_Video_config		0x06
#define HDMI_State_Audio_config		0x07
#define HDMI_State_Playback		0x09

#define HDMI1440_480I		6
#define HDMI1440_576I		21
#define HDMI480P		2
#define HDMI576P		17
#define HDMI720P_50		19
#define HDMI720P_60		4
#define HDMI1080I_50		20
#define HDMI1080I_60		5
#define HDMI1080P_50		31
#define HDMI1080P_60		16
#define HDMI1080P_24		32
#define HDMI1080P_25		33
#define HDMI1080P_24_3D_FP	(HDMI1080P_24 + 0x80)
#define HDMI720P_50_3D_FP	(HDMI720P_50  + 0x80)
#define HDMI720P_60_3D_FP	(HDMI720P_60  + 0x80)

/* Non CEA-861-D modes */
#define HDMI1360_768_60		256
#define HDMI1280_1024_60	257
#define HDMI_EDID		511

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

typedef struct audio_timing {

	unsigned long supported_rates;

	__s32 audio_en;
	__s32 sample_rate;
	__s32 channel_num;

	__s32 CTS;
	__s32 ACR_N;
	__s32 CH_STATUS0;
	__s32 CH_STATUS1;

} HDMI_AUDIO_INFO;

void hdmi_delay_ms(__u32 t);
__s32 hdmi_core_initial(void);
__s32 hdmi_core_open(void);
__s32 hdmi_core_close(void);
__s32 hdmi_main_task_loop(void);
__s32 Hpd_Check(void);
__s32 ParseEDID(void);
__s32 video_config(__s32 vic);
__s32 audio_config(void);
__s32 get_video_info(__s32 vic);

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

extern struct __disp_video_timing video_timing[];
extern const int video_timing_edid;

#endif
