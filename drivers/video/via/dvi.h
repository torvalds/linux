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

#ifndef __DVI_H__
#define __DVI_H__

/*Definition TMDS Device ID register*/
#define     VT1632_DEVICE_ID_REG        0x02
#define     VT1632_DEVICE_ID            0x92

#define     GET_DVI_SIZE_BY_SYSTEM_BIOS     0x01
#define     GET_DVI_SIZE_BY_VGA_BIOS        0x02
#define     GET_DVI_SZIE_BY_HW_STRAPPING    0x03

/* Definition DVI Panel ID*/
/* Resolution: 640x480,   Channel: single, Dithering: Enable */
#define     DVI_PANEL_ID0_640X480       0x00
/* Resolution: 800x600,   Channel: single, Dithering: Enable */
#define     DVI_PANEL_ID1_800x600       0x01
/* Resolution: 1024x768,  Channel: single, Dithering: Enable */
#define     DVI_PANEL_ID1_1024x768      0x02
/* Resolution: 1280x768,  Channel: single, Dithering: Enable */
#define     DVI_PANEL_ID1_1280x768      0x03
/* Resolution: 1280x1024, Channel: dual,   Dithering: Enable */
#define     DVI_PANEL_ID1_1280x1024     0x04
/* Resolution: 1400x1050, Channel: dual,   Dithering: Enable */
#define     DVI_PANEL_ID1_1400x1050     0x05
/* Resolution: 1600x1200, Channel: dual,   Dithering: Enable */
#define     DVI_PANEL_ID1_1600x1200     0x06

/* Define the version of EDID*/
#define     EDID_VERSION_1      1
#define     EDID_VERSION_2      2

#define     DEV_CONNECT_DVI     0x01
#define     DEV_CONNECT_HDMI    0x02

struct VideoModeTable *viafb_get_cea_mode_tbl_pointer(int Index);
int viafb_dvi_sense(void);
void viafb_dvi_disable(void);
void viafb_dvi_enable(void);
int viafb_tmds_trasmitter_identify(void);
void viafb_init_dvi_size(void);
void viafb_dvi_set_mode(int video_index, int mode_bpp, int set_iga);

#endif /* __DVI_H__ */
