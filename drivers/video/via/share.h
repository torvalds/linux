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

/* Define Return Value */
#define FAIL        -1
#define OK          1

#ifndef NULL
#define NULL 0
#endif

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

/* standard VGA IO port
*/
#define VIARMisc    0x3CC
#define VIAWMisc    0x3C2
#define VIAStatus   0x3DA
#define VIACR       0x3D4
#define VIASR       0x3C4
#define VIAGR       0x3CE
#define VIAAR       0x3C0

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

/* Definition Refresh Rate */
#define REFRESH_50      50
#define REFRESH_60      60
#define REFRESH_75      75
#define REFRESH_85      85
#define REFRESH_100     100
#define REFRESH_120     120

/* Definition Sync Polarity*/
#define NEGATIVE        1
#define POSITIVE        0

/*480x640@60 Sync Polarity (GTF)
*/
#define M480X640_R60_HSP        NEGATIVE
#define M480X640_R60_VSP        POSITIVE

/*640x480@60 Sync Polarity (VESA Mode)
*/
#define M640X480_R60_HSP        NEGATIVE
#define M640X480_R60_VSP        NEGATIVE

/*640x480@75 Sync Polarity (VESA Mode)
*/
#define M640X480_R75_HSP        NEGATIVE
#define M640X480_R75_VSP        NEGATIVE

/*640x480@85 Sync Polarity (VESA Mode)
*/
#define M640X480_R85_HSP        NEGATIVE
#define M640X480_R85_VSP        NEGATIVE

/*640x480@100 Sync Polarity (GTF Mode)
*/
#define M640X480_R100_HSP       NEGATIVE
#define M640X480_R100_VSP       POSITIVE

/*640x480@120 Sync Polarity (GTF Mode)
*/
#define M640X480_R120_HSP       NEGATIVE
#define M640X480_R120_VSP       POSITIVE

/*720x480@60 Sync Polarity  (GTF Mode)
*/
#define M720X480_R60_HSP        NEGATIVE
#define M720X480_R60_VSP        POSITIVE

/*720x576@60 Sync Polarity  (GTF Mode)
*/
#define M720X576_R60_HSP        NEGATIVE
#define M720X576_R60_VSP        POSITIVE

/*800x600@60 Sync Polarity (VESA Mode)
*/
#define M800X600_R60_HSP        POSITIVE
#define M800X600_R60_VSP        POSITIVE

/*800x600@75 Sync Polarity (VESA Mode)
*/
#define M800X600_R75_HSP        POSITIVE
#define M800X600_R75_VSP        POSITIVE

/*800x600@85 Sync Polarity (VESA Mode)
*/
#define M800X600_R85_HSP        POSITIVE
#define M800X600_R85_VSP        POSITIVE

/*800x600@100 Sync Polarity (GTF Mode)
*/
#define M800X600_R100_HSP       NEGATIVE
#define M800X600_R100_VSP       POSITIVE

/*800x600@120 Sync Polarity (GTF Mode)
*/
#define M800X600_R120_HSP       NEGATIVE
#define M800X600_R120_VSP       POSITIVE

/*800x480@60 Sync Polarity  (CVT Mode)
*/
#define M800X480_R60_HSP        NEGATIVE
#define M800X480_R60_VSP        POSITIVE

/*848x480@60 Sync Polarity  (CVT Mode)
*/
#define M848X480_R60_HSP        NEGATIVE
#define M848X480_R60_VSP        POSITIVE

/*852x480@60 Sync Polarity  (GTF Mode)
*/
#define M852X480_R60_HSP        NEGATIVE
#define M852X480_R60_VSP        POSITIVE

/*1024x512@60 Sync Polarity (GTF Mode)
*/
#define M1024X512_R60_HSP       NEGATIVE
#define M1024X512_R60_VSP       POSITIVE

/*1024x600@60 Sync Polarity (GTF Mode)
*/
#define M1024X600_R60_HSP       NEGATIVE
#define M1024X600_R60_VSP       POSITIVE

/*1024x768@60 Sync Polarity (VESA Mode)
*/
#define M1024X768_R60_HSP       NEGATIVE
#define M1024X768_R60_VSP       NEGATIVE

/*1024x768@75 Sync Polarity (VESA Mode)
*/
#define M1024X768_R75_HSP       POSITIVE
#define M1024X768_R75_VSP       POSITIVE

/*1024x768@85 Sync Polarity (VESA Mode)
*/
#define M1024X768_R85_HSP       POSITIVE
#define M1024X768_R85_VSP       POSITIVE

/*1024x768@100 Sync Polarity (GTF Mode)
*/
#define M1024X768_R100_HSP      NEGATIVE
#define M1024X768_R100_VSP      POSITIVE

/*1152x864@75 Sync Polarity (VESA Mode)
*/
#define M1152X864_R75_HSP       POSITIVE
#define M1152X864_R75_VSP       POSITIVE

/*1280x720@60 Sync Polarity  (GTF Mode)
*/
#define M1280X720_R60_HSP       NEGATIVE
#define M1280X720_R60_VSP       POSITIVE

/* 1280x768@50 Sync Polarity  (GTF Mode) */
#define M1280X768_R50_HSP       NEGATIVE
#define M1280X768_R50_VSP       POSITIVE

/*1280x768@60 Sync Polarity  (GTF Mode)
*/
#define M1280X768_R60_HSP       NEGATIVE
#define M1280X768_R60_VSP       POSITIVE

/*1280x800@60 Sync Polarity  (CVT Mode)
*/
#define M1280X800_R60_HSP       NEGATIVE
#define M1280X800_R60_VSP       POSITIVE

/*1280x960@60 Sync Polarity (VESA Mode)
*/
#define M1280X960_R60_HSP       POSITIVE
#define M1280X960_R60_VSP       POSITIVE

/*1280x1024@60 Sync Polarity (VESA Mode)
*/
#define M1280X1024_R60_HSP      POSITIVE
#define M1280X1024_R60_VSP      POSITIVE

/* 1360x768@60 Sync Polarity (CVT Mode) */
#define M1360X768_R60_HSP       POSITIVE
#define M1360X768_R60_VSP       POSITIVE

/* 1360x768@60 Sync Polarity (CVT Reduce Blanking Mode) */
#define M1360X768_RB_R60_HSP       POSITIVE
#define M1360X768_RB_R60_VSP       NEGATIVE

/* 1368x768@50 Sync Polarity (GTF Mode) */
#define M1368X768_R50_HSP       NEGATIVE
#define M1368X768_R50_VSP       POSITIVE

/* 1368x768@60 Sync Polarity (VESA Mode) */
#define M1368X768_R60_HSP       NEGATIVE
#define M1368X768_R60_VSP       POSITIVE

/*1280x1024@75 Sync Polarity (VESA Mode)
*/
#define M1280X1024_R75_HSP      POSITIVE
#define M1280X1024_R75_VSP      POSITIVE

/*1280x1024@85 Sync Polarity (VESA Mode)
*/
#define M1280X1024_R85_HSP      POSITIVE
#define M1280X1024_R85_VSP      POSITIVE

/*1440x1050@60 Sync Polarity (GTF Mode)
*/
#define M1440X1050_R60_HSP      NEGATIVE
#define M1440X1050_R60_VSP      POSITIVE

/*1600x1200@60 Sync Polarity (VESA Mode)
*/
#define M1600X1200_R60_HSP      POSITIVE
#define M1600X1200_R60_VSP      POSITIVE

/*1600x1200@75 Sync Polarity (VESA Mode)
*/
#define M1600X1200_R75_HSP      POSITIVE
#define M1600X1200_R75_VSP      POSITIVE

/* 1680x1050@60 Sync Polarity (CVT Mode) */
#define M1680x1050_R60_HSP      NEGATIVE
#define M1680x1050_R60_VSP      NEGATIVE

/* 1680x1050@60 Sync Polarity (CVT Reduce Blanking Mode) */
#define M1680x1050_RB_R60_HSP      POSITIVE
#define M1680x1050_RB_R60_VSP      NEGATIVE

/* 1680x1050@75 Sync Polarity (CVT Mode) */
#define M1680x1050_R75_HSP      NEGATIVE
#define M1680x1050_R75_VSP      POSITIVE

/*1920x1080@60 Sync Polarity (CVT Mode)
*/
#define M1920X1080_R60_HSP      NEGATIVE
#define M1920X1080_R60_VSP      POSITIVE

/* 1920x1080@60 Sync Polarity (CVT Reduce Blanking Mode) */
#define M1920X1080_RB_R60_HSP  POSITIVE
#define M1920X1080_RB_R60_VSP  NEGATIVE

/*1920x1440@60 Sync Polarity (VESA Mode)
*/
#define M1920X1440_R60_HSP      NEGATIVE
#define M1920X1440_R60_VSP      POSITIVE

/*1920x1440@75 Sync Polarity (VESA Mode)
*/
#define M1920X1440_R75_HSP      NEGATIVE
#define M1920X1440_R75_VSP      POSITIVE

#if 0
/* 1400x1050@60 Sync Polarity (VESA Mode) */
#define M1400X1050_R60_HSP      NEGATIVE
#define M1400X1050_R60_VSP      NEGATIVE
#endif

/* 1400x1050@60 Sync Polarity (CVT Mode) */
#define M1400X1050_R60_HSP      NEGATIVE
#define M1400X1050_R60_VSP      POSITIVE

/* 1400x1050@60 Sync Polarity (CVT Reduce Blanking Mode) */
#define M1400X1050_RB_R60_HSP      POSITIVE
#define M1400X1050_RB_R60_VSP      NEGATIVE

/* 1400x1050@75 Sync Polarity (CVT Mode) */
#define M1400X1050_R75_HSP      NEGATIVE
#define M1400X1050_R75_VSP      POSITIVE

/* 960x600@60 Sync Polarity (CVT Mode) */
#define M960X600_R60_HSP        NEGATIVE
#define M960X600_R60_VSP        POSITIVE

/* 1000x600@60 Sync Polarity (GTF Mode) */
#define M1000X600_R60_HSP       NEGATIVE
#define M1000X600_R60_VSP       POSITIVE

/* 1024x576@60 Sync Polarity (GTF Mode) */
#define M1024X576_R60_HSP       NEGATIVE
#define M1024X576_R60_VSP       POSITIVE

/*1024x600@60 Sync Polarity (GTF Mode)*/
#define M1024X600_R60_HSP       NEGATIVE
#define M1024X600_R60_VSP       POSITIVE

/* 1088x612@60 Sync Polarity (CVT Mode) */
#define M1088X612_R60_HSP       NEGATIVE
#define M1088X612_R60_VSP       POSITIVE

/* 1152x720@60 Sync Polarity (CVT Mode) */
#define M1152X720_R60_HSP       NEGATIVE
#define M1152X720_R60_VSP       POSITIVE

/* 1200x720@60 Sync Polarity (GTF Mode) */
#define M1200X720_R60_HSP       NEGATIVE
#define M1200X720_R60_VSP       POSITIVE

/* 1280x600@60 Sync Polarity (GTF Mode) */
#define M1280x600_R60_HSP       NEGATIVE
#define M1280x600_R60_VSP       POSITIVE

/* 1280x720@50 Sync Polarity  (GTF Mode) */
#define M1280X720_R50_HSP       NEGATIVE
#define M1280X720_R50_VSP       POSITIVE

/* 1280x720@60 Sync Polarity  (CEA Mode) */
#define M1280X720_CEA_R60_HSP       POSITIVE
#define M1280X720_CEA_R60_VSP       POSITIVE

/* 1440x900@60 Sync Polarity (CVT Mode) */
#define M1440X900_R60_HSP       NEGATIVE
#define M1440X900_R60_VSP       POSITIVE

/* 1440x900@75 Sync Polarity (CVT Mode) */
#define M1440X900_R75_HSP       NEGATIVE
#define M1440X900_R75_VSP       POSITIVE

/* 1440x900@60 Sync Polarity (CVT Reduce Blanking Mode) */
#define M1440X900_RB_R60_HSP       POSITIVE
#define M1440X900_RB_R60_VSP       NEGATIVE

/* 1600x900@60 Sync Polarity (CVT Mode) */
#define M1600X900_R60_HSP       NEGATIVE
#define M1600X900_R60_VSP       POSITIVE

/* 1600x900@60 Sync Polarity (CVT Reduce Blanking Mode) */
#define M1600X900_RB_R60_HSP       POSITIVE
#define M1600X900_RB_R60_VSP       NEGATIVE

/* 1600x1024@60 Sync Polarity (GTF Mode) */
#define M1600X1024_R60_HSP      NEGATIVE
#define M1600X1024_R60_VSP      POSITIVE

/* 1792x1344@60 Sync Polarity (DMT Mode) */
#define M1792x1344_R60_HSP      NEGATIVE
#define M1792x1344_R60_VSP      POSITIVE

/* 1856x1392@60 Sync Polarity (DMT Mode) */
#define M1856x1392_R60_HSP      NEGATIVE
#define M1856x1392_R60_VSP      POSITIVE

/* 1920x1200@60 Sync Polarity (CVT Mode) */
#define M1920X1200_R60_HSP      NEGATIVE
#define M1920X1200_R60_VSP      POSITIVE

/* 1920x1200@60 Sync Polarity (CVT Reduce Blanking Mode) */
#define M1920X1200_RB_R60_HSP  POSITIVE
#define M1920X1200_RB_R60_VSP  NEGATIVE

/* 1920x1080@60 Sync Polarity  (CEA Mode) */
#define M1920X1080_CEA_R60_HSP       POSITIVE
#define M1920X1080_CEA_R60_VSP       POSITIVE

/* 2048x1536@60 Sync Polarity (CVT Mode) */
#define M2048x1536_R60_HSP      NEGATIVE
#define M2048x1536_R60_VSP      POSITIVE

/* define PLL index: */
#define CLK_25_175M     25175000
#define CLK_26_880M     26880000
#define CLK_29_581M     29581000
#define CLK_31_490M     31490000
#define CLK_31_500M     31500000
#define CLK_31_728M     31728000
#define CLK_32_668M     32688000
#define CLK_36_000M     36000000
#define CLK_40_000M     40000000
#define CLK_41_291M     41291000
#define CLK_43_163M     43163000
#define CLK_45_250M     45250000	/* 45.46MHz */
#define CLK_46_000M     46000000
#define CLK_46_996M     46996000
#define CLK_48_000M     48000000
#define CLK_48_875M     48875000
#define CLK_49_500M     49500000
#define CLK_52_406M     52406000
#define CLK_52_977M     52977000
#define CLK_56_250M     56250000
#define CLK_60_466M     60466000
#define CLK_61_500M     61500000
#define CLK_65_000M     65000000
#define CLK_65_178M     65178000
#define CLK_66_750M     66750000	/* 67.116MHz */
#define CLK_68_179M     68179000
#define CLK_69_924M     69924000
#define CLK_70_159M     70159000
#define CLK_72_000M     72000000
#define CLK_74_270M     74270000
#define CLK_78_750M     78750000
#define CLK_80_136M     80136000
#define CLK_83_375M     83375000
#define CLK_83_950M     83950000
#define CLK_84_750M     84750000	/* 84.537Mhz */
#define CLK_85_860M     85860000
#define CLK_88_750M     88750000
#define CLK_94_500M     94500000
#define CLK_97_750M     97750000
#define CLK_101_000M    101000000
#define CLK_106_500M    106500000
#define CLK_108_000M    108000000
#define CLK_113_309M    113309000
#define CLK_118_840M    118840000
#define CLK_119_000M    119000000
#define CLK_121_750M    121750000	/* 121.704MHz */
#define CLK_125_104M    125104000
#define CLK_133_308M    133308000
#define CLK_135_000M    135000000
#define CLK_136_700M    136700000
#define CLK_138_400M    138400000
#define CLK_146_760M    146760000
#define CLK_148_500M    148500000

#define CLK_153_920M    153920000
#define CLK_156_000M    156000000
#define CLK_157_500M    157500000
#define CLK_162_000M    162000000
#define CLK_187_000M    187000000
#define CLK_193_295M    193295000
#define CLK_202_500M    202500000
#define CLK_204_000M    204000000
#define CLK_218_500M    218500000
#define CLK_234_000M    234000000
#define CLK_267_250M    267250000
#define CLK_297_500M    297500000
#define CLK_74_481M     74481000
#define CLK_172_798M    172798000
#define CLK_122_614M    122614000

/* CLE266 PLL value
*/
#define CLE266_PLL_25_175M     0x0000C763
#define CLE266_PLL_26_880M     0x0000440F
#define CLE266_PLL_29_581M     0x00008421
#define CLE266_PLL_31_490M     0x00004721
#define CLE266_PLL_31_500M     0x0000C3B5
#define CLE266_PLL_31_728M     0x0000471F
#define CLE266_PLL_32_668M     0x0000C449
#define CLE266_PLL_36_000M     0x0000C5E5
#define CLE266_PLL_40_000M     0x0000C459
#define CLE266_PLL_41_291M     0x00004417
#define CLE266_PLL_43_163M     0x0000C579
#define CLE266_PLL_45_250M     0x0000C57F	/* 45.46MHz */
#define CLE266_PLL_46_000M     0x0000875A
#define CLE266_PLL_46_996M     0x0000C4E9
#define CLE266_PLL_48_000M     0x00001443
#define CLE266_PLL_48_875M     0x00001D63
#define CLE266_PLL_49_500M     0x00008653
#define CLE266_PLL_52_406M     0x0000C475
#define CLE266_PLL_52_977M     0x00004525
#define CLE266_PLL_56_250M     0x000047B7
#define CLE266_PLL_60_466M     0x0000494C
#define CLE266_PLL_61_500M     0x00001456
#define CLE266_PLL_65_000M     0x000086ED
#define CLE266_PLL_65_178M     0x0000855B
#define CLE266_PLL_66_750M     0x0000844B	/* 67.116MHz */
#define CLE266_PLL_68_179M     0x00000413
#define CLE266_PLL_69_924M     0x00001153
#define CLE266_PLL_70_159M     0x00001462
#define CLE266_PLL_72_000M     0x00001879
#define CLE266_PLL_74_270M     0x00004853
#define CLE266_PLL_78_750M     0x00004321
#define CLE266_PLL_80_136M     0x0000051C
#define CLE266_PLL_83_375M     0x0000C25D
#define CLE266_PLL_83_950M     0x00000729
#define CLE266_PLL_84_750M     0x00008576	/* 84.537MHz */
#define CLE266_PLL_85_860M     0x00004754
#define CLE266_PLL_88_750M     0x0000051F
#define CLE266_PLL_94_500M     0x00000521
#define CLE266_PLL_97_750M     0x00004652
#define CLE266_PLL_101_000M    0x0000497F
#define CLE266_PLL_106_500M    0x00008477	/* 106.491463 MHz */
#define CLE266_PLL_108_000M    0x00008479
#define CLE266_PLL_113_309M    0x00000C5F
#define CLE266_PLL_118_840M    0x00004553
#define CLE266_PLL_119_000M    0x00000D6C
#define CLE266_PLL_121_750M    0x00004555	/* 121.704MHz */
#define CLE266_PLL_125_104M    0x000006B5
#define CLE266_PLL_133_308M    0x0000465F
#define CLE266_PLL_135_000M    0x0000455E
#define CLE266_PLL_136_700M    0x00000C73
#define CLE266_PLL_138_400M    0x00000957
#define CLE266_PLL_146_760M    0x00004567
#define CLE266_PLL_148_500M    0x00000853
#define CLE266_PLL_153_920M    0x00000856
#define CLE266_PLL_156_000M    0x0000456D
#define CLE266_PLL_157_500M    0x000005B7
#define CLE266_PLL_162_000M    0x00004571
#define CLE266_PLL_187_000M    0x00000976
#define CLE266_PLL_193_295M    0x0000086C
#define CLE266_PLL_202_500M    0x00000763
#define CLE266_PLL_204_000M    0x00000764
#define CLE266_PLL_218_500M    0x0000065C
#define CLE266_PLL_234_000M    0x00000662
#define CLE266_PLL_267_250M    0x00000670
#define CLE266_PLL_297_500M    0x000005E6
#define CLE266_PLL_74_481M     0x0000051A
#define CLE266_PLL_172_798M    0x00004579
#define CLE266_PLL_122_614M    0x0000073C

/* K800 PLL value
*/
#define K800_PLL_25_175M     0x00539001
#define K800_PLL_26_880M     0x001C8C80
#define K800_PLL_29_581M     0x00409080
#define K800_PLL_31_490M     0x006F9001
#define K800_PLL_31_500M     0x008B9002
#define K800_PLL_31_728M     0x00AF9003
#define K800_PLL_32_668M     0x00909002
#define K800_PLL_36_000M     0x009F9002
#define K800_PLL_40_000M     0x00578C02
#define K800_PLL_41_291M     0x00438C01
#define K800_PLL_43_163M     0x00778C03
#define K800_PLL_45_250M     0x007D8C83	/* 45.46MHz */
#define K800_PLL_46_000M     0x00658C02
#define K800_PLL_46_996M     0x00818C83
#define K800_PLL_48_000M     0x00848C83
#define K800_PLL_48_875M     0x00508C81
#define K800_PLL_49_500M     0x00518C01
#define K800_PLL_52_406M     0x00738C02
#define K800_PLL_52_977M     0x00928C83
#define K800_PLL_56_250M     0x007C8C02
#define K800_PLL_60_466M     0x00A78C83
#define K800_PLL_61_500M     0x00AA8C83
#define K800_PLL_65_000M     0x006B8C01
#define K800_PLL_65_178M     0x00B48C83
#define K800_PLL_66_750M     0x00948C82	/* 67.116MHz */
#define K800_PLL_68_179M     0x00708C01
#define K800_PLL_69_924M     0x00C18C83
#define K800_PLL_70_159M     0x00C28C83
#define K800_PLL_72_000M     0x009F8C82
#define K800_PLL_74_270M     0x00ce0c03
#define K800_PLL_78_750M     0x00408801
#define K800_PLL_80_136M     0x00428801
#define K800_PLL_83_375M     0x005B0882
#define K800_PLL_83_950M     0x00738803
#define K800_PLL_84_750M     0x00748883	/* 84.477MHz */
#define K800_PLL_85_860M     0x00768883
#define K800_PLL_88_750M     0x007A8883
#define K800_PLL_94_500M     0x00828803
#define K800_PLL_97_750M     0x00878883
#define K800_PLL_101_000M    0x008B8883
#define K800_PLL_106_500M    0x00758882	/* 106.491463 MHz */
#define K800_PLL_108_000M    0x00778882
#define K800_PLL_113_309M    0x005D8881
#define K800_PLL_118_840M    0x00A48883
#define K800_PLL_119_000M    0x00838882
#define K800_PLL_121_750M    0x00A88883	/* 121.704MHz */
#define K800_PLL_125_104M    0x00688801
#define K800_PLL_133_308M    0x005D8801
#define K800_PLL_135_000M    0x001A4081
#define K800_PLL_136_700M    0x00BD8883
#define K800_PLL_138_400M    0x00728881
#define K800_PLL_146_760M    0x00CC8883
#define K800_PLL_148_500M    0x00ce0803
#define K800_PLL_153_920M    0x00548482
#define K800_PLL_156_000M    0x006B8483
#define K800_PLL_157_500M    0x00142080
#define K800_PLL_162_000M    0x006F8483
#define K800_PLL_187_000M    0x00818483
#define K800_PLL_193_295M    0x004F8481
#define K800_PLL_202_500M    0x00538481
#define K800_PLL_204_000M    0x008D8483
#define K800_PLL_218_500M    0x00978483
#define K800_PLL_234_000M    0x00608401
#define K800_PLL_267_250M    0x006E8481
#define K800_PLL_297_500M    0x00A48402
#define K800_PLL_74_481M     0x007B8C81
#define K800_PLL_172_798M    0x00778483
#define K800_PLL_122_614M    0x00878882

/* PLL for VT3324 */
#define CX700_25_175M     0x008B1003
#define CX700_26_719M     0x00931003
#define CX700_26_880M     0x00941003
#define CX700_29_581M     0x00A49003
#define CX700_31_490M     0x00AE1003
#define CX700_31_500M     0x00AE1003
#define CX700_31_728M     0x00AF1003
#define CX700_32_668M     0x00B51003
#define CX700_36_000M     0x00C81003
#define CX700_40_000M     0x006E0C03
#define CX700_41_291M     0x00710C03
#define CX700_43_163M     0x00770C03
#define CX700_45_250M     0x007D0C03	/* 45.46MHz */
#define CX700_46_000M     0x007F0C03
#define CX700_46_996M     0x00818C83
#define CX700_48_000M     0x00840C03
#define CX700_48_875M     0x00508C81
#define CX700_49_500M     0x00880C03
#define CX700_52_406M     0x00730C02
#define CX700_52_977M     0x00920C03
#define CX700_56_250M     0x009B0C03
#define CX700_60_466M     0x00460C00
#define CX700_61_500M     0x00AA0C03
#define CX700_65_000M     0x006B0C01
#define CX700_65_178M     0x006B0C01
#define CX700_66_750M     0x00940C02	/*67.116MHz */
#define CX700_68_179M     0x00BC0C03
#define CX700_69_924M     0x00C10C03
#define CX700_70_159M     0x00C20C03
#define CX700_72_000M     0x009F0C02
#define CX700_74_270M     0x00CE0C03
#define CX700_74_481M     0x00CE0C03
#define CX700_78_750M     0x006C0803
#define CX700_80_136M     0x006E0803
#define CX700_83_375M     0x005B0882
#define CX700_83_950M     0x00730803
#define CX700_84_750M     0x00740803	/* 84.537Mhz */
#define CX700_85_860M     0x00760803
#define CX700_88_750M     0x00AC8885
#define CX700_94_500M     0x00820803
#define CX700_97_750M     0x00870803
#define CX700_101_000M    0x008B0803
#define CX700_106_500M    0x00750802
#define CX700_108_000M    0x00950803
#define CX700_113_309M    0x005D0801
#define CX700_118_840M    0x00A40803
#define CX700_119_000M    0x00830802
#define CX700_121_750M    0x00420800	/* 121.704MHz */
#define CX700_125_104M    0x00AD0803
#define CX700_133_308M    0x00930802
#define CX700_135_000M    0x00950802
#define CX700_136_700M    0x00BD0803
#define CX700_138_400M    0x00720801
#define CX700_146_760M    0x00CC0803
#define CX700_148_500M    0x00a40802
#define CX700_153_920M    0x00540402
#define CX700_156_000M    0x006B0403
#define CX700_157_500M    0x006C0403
#define CX700_162_000M    0x006F0403
#define CX700_172_798M    0x00770403
#define CX700_187_000M    0x00810403
#define CX700_193_295M    0x00850403
#define CX700_202_500M    0x008C0403
#define CX700_204_000M    0x008D0403
#define CX700_218_500M    0x00970403
#define CX700_234_000M    0x00600401
#define CX700_267_250M    0x00B90403
#define CX700_297_500M    0x00CE0403
#define CX700_122_614M    0x00870802

/* PLL for VX855 */
#define VX855_22_000M     0x007B1005
#define VX855_25_175M     0x008D1005
#define VX855_26_719M     0x00961005
#define VX855_26_880M     0x00961005
#define VX855_27_000M     0x00971005
#define VX855_29_581M     0x00A51005
#define VX855_29_829M     0x00641003
#define VX855_31_490M     0x00B01005
#define VX855_31_500M     0x00B01005
#define VX855_31_728M     0x008E1004
#define VX855_32_668M     0x00921004
#define VX855_36_000M     0x00A11004
#define VX855_40_000M     0x00700C05
#define VX855_41_291M     0x00730C05
#define VX855_43_163M     0x00790C05
#define VX855_45_250M     0x007F0C05      /* 45.46MHz */
#define VX855_46_000M     0x00670C04
#define VX855_46_996M     0x00690C04
#define VX855_48_000M     0x00860C05
#define VX855_48_875M     0x00890C05
#define VX855_49_500M     0x00530C03
#define VX855_52_406M     0x00580C03
#define VX855_52_977M     0x00940C05
#define VX855_56_250M     0x009D0C05
#define VX855_60_466M     0x00A90C05
#define VX855_61_500M     0x00AC0C05
#define VX855_65_000M     0x006D0C03
#define VX855_65_178M     0x00B60C05
#define VX855_66_750M     0x00700C03    /*67.116MHz */
#define VX855_67_295M     0x00BC0C05
#define VX855_68_179M     0x00BF0C05
#define VX855_68_369M     0x00BF0C05
#define VX855_69_924M     0x00C30C05
#define VX855_70_159M     0x00C30C05
#define VX855_72_000M     0x00A10C04
#define VX855_73_023M     0x00CC0C05
#define VX855_74_481M     0x00D10C05
#define VX855_78_750M     0x006E0805
#define VX855_79_466M     0x006F0805
#define VX855_80_136M     0x00700805
#define VX855_81_627M     0x00720805
#define VX855_83_375M     0x00750805
#define VX855_83_527M     0x00750805
#define VX855_83_950M     0x00750805
#define VX855_84_537M     0x00760805
#define VX855_84_750M     0x00760805     /* 84.537Mhz */
#define VX855_85_500M     0x00760805        /* 85.909080 MHz*/
#define VX855_85_860M     0x00760805
#define VX855_85_909M     0x00760805
#define VX855_88_750M     0x007C0805
#define VX855_89_489M     0x007D0805
#define VX855_94_500M     0x00840805
#define VX855_96_648M     0x00870805
#define VX855_97_750M     0x00890805
#define VX855_101_000M    0x008D0805
#define VX855_106_500M    0x00950805
#define VX855_108_000M    0x00970805
#define VX855_110_125M    0x00990805
#define VX855_112_000M    0x009D0805
#define VX855_113_309M    0x009F0805
#define VX855_115_000M    0x00A10805
#define VX855_118_840M    0x00A60805
#define VX855_119_000M    0x00A70805
#define VX855_121_750M    0x00AA0805       /* 121.704MHz */
#define VX855_122_614M    0x00AC0805
#define VX855_126_266M    0x00B10805
#define VX855_130_250M    0x00B60805      /* 130.250 */
#define VX855_135_000M    0x00BD0805
#define VX855_136_700M    0x00BF0805
#define VX855_137_750M    0x00C10805
#define VX855_138_400M    0x00C20805
#define VX855_144_300M    0x00CA0805
#define VX855_146_760M    0x00CE0805
#define VX855_148_500M	  0x00D00805
#define VX855_153_920M    0x00540402
#define VX855_156_000M    0x006C0405
#define VX855_156_867M    0x006E0405
#define VX855_157_500M    0x006E0405
#define VX855_162_000M    0x00710405
#define VX855_172_798M    0x00790405
#define VX855_187_000M    0x00830405
#define VX855_193_295M    0x00870405
#define VX855_202_500M    0x008E0405
#define VX855_204_000M    0x008F0405
#define VX855_218_500M    0x00990405
#define VX855_229_500M    0x00A10405
#define VX855_234_000M    0x00A40405
#define VX855_267_250M    0x00BB0405
#define VX855_297_500M    0x00D00405
#define VX855_339_500M    0x00770005
#define VX855_340_772M    0x00770005


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

/* Definition Video Mode Pixel Clock (picoseconds)
*/
#define RES_480X640_60HZ_PIXCLOCK    39722
#define RES_640X480_60HZ_PIXCLOCK    39722
#define RES_640X480_75HZ_PIXCLOCK    31747
#define RES_640X480_85HZ_PIXCLOCK    27777
#define RES_640X480_100HZ_PIXCLOCK   23168
#define RES_640X480_120HZ_PIXCLOCK   19081
#define RES_720X480_60HZ_PIXCLOCK    37020
#define RES_720X576_60HZ_PIXCLOCK    30611
#define RES_800X600_60HZ_PIXCLOCK    25000
#define RES_800X600_75HZ_PIXCLOCK    20203
#define RES_800X600_85HZ_PIXCLOCK    17777
#define RES_800X600_100HZ_PIXCLOCK   14667
#define RES_800X600_120HZ_PIXCLOCK   11912
#define RES_800X480_60HZ_PIXCLOCK    33805
#define RES_848X480_60HZ_PIXCLOCK    31756
#define RES_856X480_60HZ_PIXCLOCK    31518
#define RES_1024X512_60HZ_PIXCLOCK   24218
#define RES_1024X600_60HZ_PIXCLOCK   20460
#define RES_1024X768_60HZ_PIXCLOCK   15385
#define RES_1024X768_75HZ_PIXCLOCK   12699
#define RES_1024X768_85HZ_PIXCLOCK   10582
#define RES_1024X768_100HZ_PIXCLOCK  8825
#define RES_1152X864_75HZ_PIXCLOCK   9259
#define RES_1280X768_60HZ_PIXCLOCK   12480
#define RES_1280X800_60HZ_PIXCLOCK   11994
#define RES_1280X960_60HZ_PIXCLOCK   9259
#define RES_1280X1024_60HZ_PIXCLOCK  9260
#define RES_1280X1024_75HZ_PIXCLOCK  7408
#define RES_1280X768_85HZ_PIXCLOCK   6349
#define RES_1440X1050_60HZ_PIXCLOCK  7993
#define RES_1600X1200_60HZ_PIXCLOCK  6172
#define RES_1600X1200_75HZ_PIXCLOCK  4938
#define RES_1280X720_60HZ_PIXCLOCK   13426
#define RES_1920X1080_60HZ_PIXCLOCK  5787
#define RES_1400X1050_60HZ_PIXCLOCK  8214
#define RES_1400X1050_75HZ_PIXCLOCK  6410
#define RES_1368X768_60HZ_PIXCLOCK   11647
#define RES_960X600_60HZ_PIXCLOCK      22099
#define RES_1000X600_60HZ_PIXCLOCK    20834
#define RES_1024X576_60HZ_PIXCLOCK    21278
#define RES_1088X612_60HZ_PIXCLOCK    18877
#define RES_1152X720_60HZ_PIXCLOCK    14981
#define RES_1200X720_60HZ_PIXCLOCK    14253
#define RES_1280X600_60HZ_PIXCLOCK    16260
#define RES_1280X720_50HZ_PIXCLOCK    16538
#define RES_1280X768_50HZ_PIXCLOCK    15342
#define RES_1366X768_50HZ_PIXCLOCK    14301
#define RES_1366X768_60HZ_PIXCLOCK    11646
#define RES_1360X768_60HZ_PIXCLOCK    11799
#define RES_1440X900_60HZ_PIXCLOCK    9390
#define RES_1440X900_75HZ_PIXCLOCK    7315
#define RES_1600X900_60HZ_PIXCLOCK    8415
#define RES_1600X1024_60HZ_PIXCLOCK   7315
#define RES_1680X1050_60HZ_PIXCLOCK   6814
#define RES_1680X1050_75HZ_PIXCLOCK   5348
#define RES_1792X1344_60HZ_PIXCLOCK   4902
#define RES_1856X1392_60HZ_PIXCLOCK   4577
#define RES_1920X1200_60HZ_PIXCLOCK   5173
#define RES_1920X1440_60HZ_PIXCLOCK   4274
#define RES_1920X1440_75HZ_PIXCLOCK   3367
#define RES_2048X1536_60HZ_PIXCLOCK   3742

#define RES_1360X768_RB_60HZ_PIXCLOCK 13889
#define RES_1400X1050_RB_60HZ_PIXCLOCK 9901
#define RES_1440X900_RB_60HZ_PIXCLOCK   11268
#define RES_1600X900_RB_60HZ_PIXCLOCK   10230
#define RES_1680X1050_RB_60HZ_PIXCLOCK 8403
#define RES_1920X1080_RB_60HZ_PIXCLOCK 7225
#define RES_1920X1200_RB_60HZ_PIXCLOCK 6497

/* LCD display method
*/
#define     LCD_EXPANDSION              0x00
#define     LCD_CENTERING               0x01

/* LCD mode
*/
#define     LCD_OPENLDI               0x00
#define     LCD_SPWG                  0x01

/* Define display timing
*/
struct display_timing {
	u16 hor_total;
	u16 hor_addr;
	u16 hor_blank_start;
	u16 hor_blank_end;
	u16 hor_sync_start;
	u16 hor_sync_end;
	u16 ver_total;
	u16 ver_addr;
	u16 ver_blank_start;
	u16 ver_blank_end;
	u16 ver_sync_start;
	u16 ver_sync_end;
};

struct crt_mode_table {
	int refresh_rate;
	unsigned long clk;
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
