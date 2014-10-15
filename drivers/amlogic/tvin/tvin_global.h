/*
 * TVIN global definition
 * enum, structure & global parameters used in all TVIN modules.
 *
 * Author: Lin Xu <lin.xu@amlogic.com>
 *         Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __TVIN_GLOBAL_H
#define __TVIN_GLOBAL_H

#include <linux/amlogic/tvin/tvin.h>

// ***************************************************************************
// *** enum definitions *********************************************
// ***************************************************************************

#define STATUS_ANTI_SHOCKING    3
#define MINIMUM_H_CNT           1400

#define ADC_REG_NUM             112
#define CVD_PART1_REG_NUM       64
#define CVD_PART1_REG_MIN       0x00
#define CVD_PART2_REG_NUM       144
#define CVD_PART2_REG_MIN       0x70
#define CVD_PART3_REG_NUM       7 // 0x87, 0x93, 0x94, 0x95, 0x96, 0xe6, 0xfa
#define CVD_PART3_REG_0         0x87
#define CVD_PART3_REG_1         0x93
#define CVD_PART3_REG_2         0x94
#define CVD_PART3_REG_3         0x95
#define CVD_PART3_REG_4         0x96
#define CVD_PART3_REG_5         0xe6
#define CVD_PART3_REG_6         0xfa

//#define ACD_REG_NUM1            0x32  //0x00-0x32 except 0x1E&0x31
//#define ACD_REG_NUM2            0x39  //the sum of the part2 acd register 
#define ACD_REG_NUM            0xff//the sum all of the acd register

//#define CRYSTAL_24M

#ifndef CRYSTAL_24M
#define CRYSTAL_25M
#endif

#ifdef CRYSTAL_24M
#define CVD2_CHROMA_DTO_NTSC_M   0x262e8ba2
#define CVD2_CHROMA_DTO_NTSC_443 0x2f4abc24
#define CVD2_CHROMA_DTO_PAL_I    0x2f4abc24
#define CVD2_CHROMA_DTO_PAL_M    0x2623cd98
#define CVD2_CHROMA_DTO_PAL_CN   0x263566cf
#define CVD2_CHROMA_DTO_PAL_60   0x2f4abc24
#define CVD2_CHROMA_DTO_SECAM    0x2db7a328
#define CVD2_HSYNC_DTO_NTSC_M    0x24000000
#define CVD2_HSYNC_DTO_NTSC_443  0x24000000
#define CVD2_HSYNC_DTO_PAL_I     0x24000000
#define CVD2_HSYNC_DTO_PAL_M     0x24000000
#define CVD2_HSYNC_DTO_PAL_CN    0x24000000
#define CVD2_HSYNC_DTO_PAL_60    0x24000000
#define CVD2_HSYNC_DTO_SECAM     0x24000000
#define CVD2_DCRESTORE_ACCUM     0x98       // [5:0] = 24(MHz)
#endif

#ifdef CRYSTAL_25M
#define CVD2_CHROMA_DTO_NTSC_M   0x24a7904a
#define CVD2_CHROMA_DTO_NTSC_443 0x2d66772d
#define CVD2_CHROMA_DTO_PAL_I    0x2d66772d
#define CVD2_CHROMA_DTO_PAL_M    0x249d4040
#define CVD2_CHROMA_DTO_PAL_CN   0x24ae2541
#define CVD2_CHROMA_DTO_PAL_60   0x2d66772d
#define CVD2_CHROMA_DTO_SECAM    0x2be37de9
#define CVD2_HSYNC_DTO_NTSC_M    0x228f5c28
#define CVD2_HSYNC_DTO_NTSC_443  0x228f5c28
#define CVD2_HSYNC_DTO_PAL_I     0x228f5c28
#define CVD2_HSYNC_DTO_PAL_M     0x228f5c28
#define CVD2_HSYNC_DTO_PAL_CN    0x228f5c28
#define CVD2_HSYNC_DTO_PAL_60    0x228f5c28
#define CVD2_HSYNC_DTO_SECAM     0x228f5c28
#define CVD2_DCRESTORE_ACCUM     0x99       // [5:0] = 25(MHz)
#endif




#define TVAFE_SET_CVBS_PGA_EN
#ifdef TVAFE_SET_CVBS_PGA_EN
#define TVAFE_SET_CVBS_PGA_START    5
#define TVAFE_SET_CVBS_PGA_STEP     1
#define CVD2_DGAIN_MIDDLE           0x0200
#define CVD2_DGAIN_WINDOW           0x000F
#define CVD2_DGAIN_LIMITH CVD2_DGAIN_MIDDLE + CVD2_DGAIN_WINDOW
#define CVD2_DGAIN_LIMITL CVD2_DGAIN_MIDDLE - CVD2_DGAIN_WINDOW
#endif

#define TVAFE_SET_CVBS_CDTO_EN
#ifdef TVAFE_SET_CVBS_CDTO_EN
#define TVAFE_SET_CVBS_CDTO_START   300
#define TVAFE_SET_CVBS_CDTO_STEP    0
#define HS_CNT_STANDARD             0x17a00
#endif

typedef enum tvin_sync_pol_e {
        TVIN_SYNC_POL_NULL = 0,
        TVIN_SYNC_POL_NEGATIVE,
        TVIN_SYNC_POL_POSITIVE,
} tvin_sync_pol_t;

typedef enum tvin_color_space_e {
        TVIN_CS_RGB444 = 0,
        TVIN_CS_YUV444,
        TVIN_CS_YUV422_16BITS,
        TVIN_CS_YCbCr422_8BITS,
        TVIN_CS_MAX
} tvin_color_space_t;
/*vdin buffer control for tvin frontend*/
typedef enum tvin_buffer_ctl_e {
	TVIN_BUF_NULL,
	TVIN_BUF_SKIP,
	TVIN_BUF_TMP,
	TVIN_BUF_RECYCLE_TMP,
} tvin_buffer_ctl_t;


// ***************************************************************************
// *** structure definitions *********************************************
// ***************************************************************************
//      Hs_cnt        Pixel_Clk(Khz/10)

typedef struct tvin_format_s {
        unsigned short         h_active;        //Th in the unit of pixel
        unsigned short         v_active;        //Tv in the unit of line
        unsigned short         h_cnt;           //Th in the unit of T, while 1/T = 24MHz or 27MHz or even 100MHz
        unsigned short         h_cnt_offset;    //Tolerance of h_cnt
        unsigned short         v_cnt_offset;    //Tolerance of v_cnt
        unsigned short         hs_cnt;          //Ths in the unit of T, while 1/T = 24MHz or 27MHz or even 100MHz
        unsigned short         hs_cnt_offset;   //Tolerance of hs_cnt
        unsigned short         h_total;         //Th in the unit of pixel
        unsigned short         v_total;         //Tv in the unit of line
        unsigned short         hs_front;        //h front proch
        unsigned short         hs_width;        //HS in the unit of pixel
        unsigned short         hs_bp;           //HS in the unit of pixel
        unsigned short         vs_front;        //vs front proch in the unit of line
        unsigned short         vs_width;        //VS width in the unit of line
        unsigned short         vs_bp;           //vs back proch in the unit of line
        enum tvin_sync_pol_e   hs_pol;
        enum tvin_sync_pol_e   vs_pol;
        enum tvin_scan_mode_e  scan_mode;
        unsigned short         pixel_clk;       //(Khz/10)
        unsigned short         vbi_line_start;
        unsigned short         vbi_line_end;
        unsigned int           duration;
} tvin_format_t;

typedef enum tvin_aspect_ratio_e {
        TVIN_ASPECT_NULL = 0,
        TVIN_ASPECT_1x1,
        TVIN_ASPECT_4x3,
        TVIN_ASPECT_16x9,
        TVIN_ASPECT_MAX,
} tvin_aspect_ratio_t;

const char *tvin_aspect_ratio_str(enum tvin_aspect_ratio_e aspect_ratio);

typedef struct tvin_sig_property_s {
        enum tvin_trans_fmt      trans_fmt;
        enum tvin_color_fmt_e    color_format;
	enum tvin_color_fmt_e    dest_cfmt;//for vdin matrix destination color fmt
        enum tvin_aspect_ratio_e aspect_ratio;
        unsigned int             dvi_info;
	unsigned short           scaling4h;//for vscaler
        unsigned short           scaling4w;//for hscaler
        unsigned int 		 hs;//for horizontal start cut window
        unsigned int 		 he;//for horizontal end cut window
        unsigned int 		 vs;//for vertical start cut window
        unsigned int 		 ve;//for vertical end cut window
        unsigned int           decimation_ratio;//for decimation
} tvin_sig_property_t;

#define TVAFE_VF_POOL_SIZE              6//8
#define VDIN_VF_POOL_MAX_SIZE           6//8
#define TVHDMI_VF_POOL_SIZE              6//8

#define BT656IN_ANCI_DATA_SIZE          0x4000 //save anci data from bt656in
#define CAMERA_IN_ANCI_DATA_SIZE        0x4000 //save anci data from bt656in

#endif // __TVIN_GLOBAL_H

