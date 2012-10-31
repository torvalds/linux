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

#ifndef __HDMI_HAL_H__
#define __HDMI_HAL_H__

#include "drv_hdmi_i.h"

#define HDMI_State_Idle			0x00
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

extern void hdmi_delay_ms(__u32 t);

extern void Hdmi_set_reg_base(void __iomem *base);
extern __s32 Hdmi_hal_init(void);
extern __s32 Hdmi_hal_exit(void);
extern __s32 Hdmi_hal_video_enable(__bool enable);
extern __s32 Hdmi_hal_set_display_mode(__u32 hdmi_mode);
extern __s32 Hdmi_hal_audio_enable(__u8 mode, __u8 channel);
extern __s32 Hdmi_hal_set_audio_para(hdmi_audio_t *audio_para);
extern __s32 Hdmi_hal_mode_support(__u32 mode);
extern __s32 Hdmi_hal_get_HPD(void);
extern __s32 Hdmi_hal_get_state(void);
extern __s32 Hdmi_hal_main_task(void);
extern __s32 Hdmi_hal_set_pll(__u32 pll, __u32 clk);

#endif
