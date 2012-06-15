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

#ifndef __SHARE_H__
#define __SHARE_H__

#include "via_modesetting.h"

/* Define Bit Field */
#define BIT0    0x01
#define BIT1    0x02
#define BIT2    0x04
#define BIT3    0x08
#define BIT4    0x10
#define BIT5    0x20
#define BIT6    0x40
#define BIT7    0x80

/* Video Memory Size */
#define VIDEO_MEMORY_SIZE_16M    0x1000000

/*
 * Lengths of the VPIT structure arrays.
 */
#define StdCR       0x19
#define StdSR       0x04
#define StdGR       0x09
#define StdAR       0x14

#define PatchCR     11

/* Display path */
#define IGA1        1
#define IGA2        2

/* Define Color Depth  */
#define MODE_8BPP       1
#define MODE_16BPP      2
#define MODE_32BPP      4

#define GR20    0x20
#define GR21    0x21
#define GR22    0x22

/* Sequencer Registers */
#define SR01    0x01
#define SR10    0x10
#define SR12    0x12
#define SR15    0x15
#define SR16    0x16
#define SR17    0x17
#define SR18    0x18
#define SR1B    0x1B
#define SR1A    0x1A
#define SR1C    0x1C
#define SR1D    0x1D
#define SR1E    0x1E
#define SR1F    0x1F
#define SR20    0x20
#define SR21    0x21
#define SR22    0x22
#define SR2A    0x2A
#define SR2D    0x2D
#define SR2E    0x2E

#define SR30    0x30
#define SR39    0x39
#define SR3D    0x3D
#define SR3E    0x3E
#define SR3F    0x3F
#define SR40    0x40
#define SR43    0x43
#define SR44    0x44
#define SR45    0x45
#define SR46    0x46
#define SR47    0x47
#define SR48    0x48
#define SR49    0x49
#define SR4A    0x4A
#define SR4B    0x4B
#define SR4C    0x4C
#define SR52    0x52
#define SR57	0x57
#define SR58	0x58
#define SR59	0x59
#define SR5D    0x5D
#define SR5E    0x5E
#define SR65    0x65

/* CRT Controller Registers */
#define CR00    0x00
#define CR01    0x01
#define CR02    0x02
#define CR03    0x03
#define CR04    0x04
#define CR05    0x05
#define CR06    0x06
#define CR07    0x07
#define CR08    0x08
#define CR09    0x09
#define CR0A    0x0A
#define CR0B    0x0B
#define CR0C    0x0C
#define CR0D    0x0D
#define CR0E    0x0E
#define CR0F    0x0F
#define CR10    0x10
#define CR11    0x11
#define CR12    0x12
#define CR13    0x13
#define CR14    0x14
#define CR15    0x15
#define CR16    0x16
#define CR17    0x17
#define CR18    0x18

/* Extend CRT Controller Registers */
#define CR30    0x30
#define CR31    0x31
#define CR32    0x32
#define CR33    0x33
#define CR34    0x34
#define CR35    0x35
#define CR36    0x36
#define CR37    0x37
#define CR38    0x38
#define CR39    0x39
#define CR3A    0x3A
#define CR3B    0x3B
#define CR3C    0x3C
#define CR3D    0x3D
#define CR3E    0x3E
#define CR3F    0x3F
#define CR40    0x40
#define CR41    0x41
#define CR42    0x42
#define CR43    0x43
#define CR44    0x44
#define CR45    0x45
#define CR46    0x46
#define CR47    0x47
#define CR48    0x48
#define CR49    0x49
#define CR4A    0x4A
#define CR4B    0x4B
#define CR4C    0x4C
#define CR4D    0x4D
#define CR4E    0x4E
#define CR4F    0x4F
#define CR50    0x50
#define CR51    0x51
#define CR52    0x52
#define CR53    0x53
#define CR54    0x54
#define CR55    0x55
#define CR56    0x56
#define CR57    0x57
#define CR58    0x58
#define CR59    0x59
#define CR5A    0x5A
#define CR5B    0x5B
#define CR5C    0x5C
#define CR5D    0x5D
#define CR5E    0x5E
#define CR5F    0x5F
#define CR60    0x60
#define CR61    0x61
#define CR62    0x62
#define CR63    0x63
#define CR64    0x64
#define CR65    0x65
#define CR66    0x66
#define CR67    0x67
#define CR68    0x68
#define CR69    0x69
#define CR6A    0x6A
#define CR6B    0x6B
#define CR6C    0x6C
#define CR6D    0x6D
#define CR6E    0x6E
#define CR6F    0x6F
#define CR70    0x70
#define CR71    0x71
#define CR72    0x72
#define CR73    0x73
#define CR74    0x74
#define CR75    0x75
#define CR76    0x76
#define CR77    0x77
#define CR78    0x78
#define CR79    0x79
#define CR7A    0x7A
#define CR7B    0x7B
#define CR7C    0x7C
#define CR7D    0x7D
#define CR7E    0x7E
#define CR7F    0x7F
#define CR80    0x80
#define CR81    0x81
#define CR82    0x82
#define CR83    0x83
#define CR84    0x84
#define CR85    0x85
#define CR86    0x86
#define CR87    0x87
#define CR88    0x88
#define CR89    0x89
#define CR8A    0x8A
#define CR8B    0x8B
#define CR8C    0x8C
#define CR8D    0x8D
#define CR8E    0x8E
#define CR8F    0x8F
#define CR90    0x90
#define CR91    0x91
#define CR92    0x92
#define CR93    0x93
#define CR94    0x94
#define CR95    0x95
#define CR96    0x96
#define CR97    0x97
#define CR98    0x98
#define CR99    0x99
#define CR9A    0x9A
#define CR9B    0x9B
#define CR9C    0x9C
#define CR9D    0x9D
#define CR9E    0x9E
#define CR9F    0x9F
#define CRA0    0xA0
#define CRA1    0xA1
#define CRA2    0xA2
#define CRA3    0xA3
#define CRD2    0xD2
#define CRD3    0xD3
#define CRD4    0xD4

/* LUT Table*/
#define LUT_DATA             0x3C9	/* DACDATA */
#define LUT_INDEX_READ       0x3C7	/* DACRX */
#define LUT_INDEX_WRITE      0x3C8	/* DACWX */
#define DACMASK              0x3C6

/* Definition Device */
#define DEVICE_CRT  0x01
#define DEVICE_DVI  0x03
#define DEVICE_LCD  0x04

/* Device output interface */
#define INTERFACE_NONE          0x00
#define INTERFACE_ANALOG_RGB    0x01
#define INTERFACE_DVP0          0x02
#define INTERFACE_DVP1          0x03
#define INTERFACE_DFP_HIGH      0x04
#define INTERFACE_DFP_LOW       0x05
#define INTERFACE_DFP           0x06
#define INTERFACE_LVDS0         0x07
#define INTERFACE_LVDS1         0x08
#define INTERFACE_LVDS0LVDS1    0x09
#define INTERFACE_TMDS          0x0A

#define HW_LAYOUT_LCD_ONLY      0x01
#define HW_LAYOUT_DVI_ONLY      0x02
#define HW_LAYOUT_LCD_DVI       0x03
#define HW_LAYOUT_LCD1_LCD2     0x04
#define HW_LAYOUT_LCD_EXTERNAL_LCD2 0x10

/* Definition CRTC Timing Index */
#define H_TOTAL_INDEX               0
#define H_ADDR_INDEX                1
#define H_BLANK_START_INDEX         2
#define H_BLANK_END_INDEX           3
#define H_SYNC_START_INDEX          4
#define H_SYNC_END_INDEX            5
#define V_TOTAL_INDEX               6
#define V_ADDR_INDEX                7
#define V_BLANK_START_INDEX         8
#define V_BLANK_END_INDEX           9
#define V_SYNC_START_INDEX          10
#define V_SYNC_END_INDEX            11
#define H_TOTAL_SHADOW_INDEX        12
#define H_BLANK_END_SHADOW_INDEX    13
#define V_TOTAL_SHADOW_INDEX        14
#define V_ADDR_SHADOW_INDEX         15
#define V_BLANK_SATRT_SHADOW_INDEX  16
#define V_BLANK_END_SHADOW_INDEX    17
#define V_SYNC_SATRT_SHADOW_INDEX   18
#define V_SYNC_END_SHADOW_INDEX     19

/* LCD display method
*/
#define     LCD_EXPANDSION              0x00
#define     LCD_CENTERING               0x01

/* LCD mode
*/
#define     LCD_OPENLDI               0x00
#define     LCD_SPWG                  0x01

struct crt_mode_table {
	int refresh_rate;
	int h_sync_polarity;
	int v_sync_polarity;
	struct display_timing crtc;
};

struct io_reg {
	int port;
	u8 index;
	u8 mask;
	u8 value;
};

#endif /* __SHARE_H__ */
