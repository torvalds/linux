/*
 * TVAFE register bit-field definition
 * Sorted by the appearing order of registers in am_regs.h.
 *
 * Author: Lin Xu <lin.xu@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _TVAFE_REG_H
#define _TVAFE_REG_H

// *****************************************************************************
// ******** ACD REGISTERS ********
// *****************************************************************************

#define ACD_BASE_ADD                                    0x3100//0x1900

#define ACD_REG_00                                      ((ACD_BASE_ADD+0x00)<<2)
#define MD_LUT_STEP06_BIT              28
#define MD_LUT_STEP06_WID              4
#define MD_LUT_STEP05_BIT              24
#define MD_LUT_STEP05_WID              4
#define MD_LUT_STEP04_BIT              20
#define MD_LUT_STEP04_WID              4
#define MD_LUT_STEP03_BIT              16
#define MD_LUT_STEP03_WID              4
#define MD_LUT_STEP02_BIT              12
#define MD_LUT_STEP02_WID              4
#define MD_LUT_STEP01_BIT              8
#define MD_LUT_STEP01_WID              4
#define MD_LUT_STEP00_BIT              0
#define MD_LUT_STEP00_WID              8

#define ACD_REG_01                                      ((ACD_BASE_ADD+0x01)<<2)
#define MD_LUT_STEP14_BIT              28
#define MD_LUT_STEP14_WID              4
#define MD_LUT_STEP13_BIT              24
#define MD_LUT_STEP13_WID              4
#define MD_LUT_STEP12_BIT              20
#define MD_LUT_STEP12_WID              4
#define MD_LUT_STEP11_BIT              16
#define MD_LUT_STEP11_WID              4
#define MD_LUT_STEP10_BIT              12
#define MD_LUT_STEP10_WID              4
#define MD_LUT_STEP09_BIT              8
#define MD_LUT_STEP09_WID              4
#define MD_LUT_STEP08_BIT              4
#define MD_LUT_STEP08_WID              4
#define MD_LUT_STEP07_BIT              0
#define MD_LUT_STEP07_WID              4

#define ACD_REG_02                                      ((ACD_BASE_ADD+0x02)<<2)
#define MD_LF_ADJ_EN_BIT               31
#define MD_LF_ADJ_EN_WID               1
#define MD_LF_ADJ_BIT                  24
#define MD_LF_ADJ_WID                  4
#define MD_LF_OLD_SEL_BIT              15
#define MD_LF_OLD_SEL_WID              1
#define MD_HF_FINAL_SEL_BIT            14
#define MD_HF_FINAL_SEL_WID            1
#define MD_LF_FINAL_MODE_BIT           12
#define MD_LF_FINAL_MODE_WID           2
#define MD_MODE3_BIT                   11
#define MD_MODE3_WID                   1
#define MD_MODE2_BIT                   10
#define MD_MODE2_WID                   1
#define MD_MODE1_BIT                   9
#define MD_MODE1_WID                   1
#define MD_MODE0_BIT                   8
#define MD_MODE0_WID                   1
#define MD_LUT_STEP15_BIT              0
#define MD_LUT_STEP15_WID              4

#define ACD_REG_03                                      ((ACD_BASE_ADD+0x03)<<2)
#define MD_DETAIL_SEL_BIT              31
#define MD_DETAIL_SEL_WID              1
#define MD_DETAIL_STEP3_BIT            24
#define MD_DETAIL_STEP3_WID            4
#define MD_DETAIL_STEP2_BIT            20
#define MD_DETAIL_STEP2_WID            4
#define MD_DETAIL_STEP1_BIT            16
#define MD_DETAIL_STEP1_WID            4
#define MD_HF_MIDDLE_BIT               15
#define MD_HF_MIDDLE_WID               1
#define MD_HF56_MAX_BIT                14
#define MD_HF56_MAX_WID                1
#define MD_HF56_MAX2_BIT               13
#define MD_HF56_MAX2_WID               1
#define MD_LF56_MAX_BIT                12
#define MD_LF56_MAX_WID                1
#define AML_YCSEP_MODE_BIT             11
#define AML_YCSEP_MODE_WID             1
#define MD_HF_AVG_SEL_BIT              8
#define MD_HF_AVG_SEL_WID              2
#define MD_REGION_HF_TH_BIT            0
#define MD_REGION_HF_TH_WID            7

#define ACD_REG_04                                      ((ACD_BASE_ADD+0x04)<<2)
#define HWIDTH_CENABLE_BIT             24
#define HWIDTH_CENABLE_WID             8
#define HSTART_CENABLE_BIT             16
#define HSTART_CENABLE_WID             8
#define MD_REGION_LF_TH_BIT            8
#define MD_REGION_LF_TH_WID            7
#define MD_DETAIL_TH_BIT               0
#define MD_DETAIL_TH_WID               8

#define ACD_REG_05                                      ((ACD_BASE_ADD+0x05)<<2)
#define APL_TH1_BIT                    24
#define APL_TH1_WID                    8
#define GM_NOISE_TH_BIT                16
#define GM_NOISE_TH_WID                7
#define VWIDTH_CENABLE_BIT             8
#define VWIDTH_CENABLE_WID             8
#define VSTART_CENABLE_BIT             0
#define VSTART_CENABLE_WID             8

#define ACD_REG_06                                      ((ACD_BASE_ADD+0x06)<<2)
#define GM_TH3_BIT                     24
#define GM_TH3_WID                     8
#define GM_TH2_BIT                     16
#define GM_TH2_WID                     8
#define GM_TH1_BIT                     8
#define GM_TH1_WID                     8
#define APL_TH2_BIT                    0
#define APL_TH2_WID                    8

#define ACD_REG_07                                      ((ACD_BASE_ADD+0x07)<<2)
#define GM_GAIN4_BIT                   24
#define GM_GAIN4_WID                   8
#define GM_GAIN3_BIT                   16
#define GM_GAIN3_WID                   8
#define GM_GAIN2_BIT                   8
#define GM_GAIN2_WID                   8
#define GM_GAIN1_BIT                   0
#define GM_GAIN1_WID                   8

#define ACD_REG_08                                      ((ACD_BASE_ADD+0x08)<<2)
#define MD_BPF_COEF3_BIT               24
#define MD_BPF_COEF3_WID               8
#define MD_BPF_COEF2_BIT               16
#define MD_BPF_COEF2_WID               8
#define MD_BPF_COEF1_BIT               8
#define MD_BPF_COEF1_WID               8
#define MD_BPF_COEF0_BIT               0
#define MD_BPF_COEF0_WID               8

#define ACD_REG_09                                      ((ACD_BASE_ADD+0x09)<<2)
#define GM2LUT_EN_BIT                  15
#define GM2LUT_EN_WID                  1
#define MD_BPF_EN_BIT                  8
#define MD_BPF_EN_WID                  1
#define MD_BPF_COEF4_BIT               0
#define MD_BPF_COEF4_WID               8

#define ACD_REG_0A                                      ((ACD_BASE_ADD+0x0A)<<2)
#define GM2LUT_TH2_BIT                 16
#define GM2LUT_TH2_WID                 16
#define GM2LUT_TH1_BIT                 0
#define GM2LUT_TH1_WID                 16

#define ACD_REG_0B                                      ((ACD_BASE_ADD+0x0B)<<2)
#define YCSEP_DEMO_BIT                 24
#define YCSEP_DEMO_WID                 8
#define GM2LUT_STEP2_BIT               16
#define GM2LUT_STEP2_WID               8
#define GM2LUT_STEP1_BIT               8
#define GM2LUT_STEP1_WID               8
#define GM2LUT_STEP0_BIT               0
#define GM2LUT_STEP0_WID               8

#define ACD_REG_0C                                      ((ACD_BASE_ADD+0x0C)<<2)
#define WR_ADDR5_VS_REG_BIT            0
#define WR_ADDR5_VS_REG_WID            23

#define ACD_REG_0D                                      ((ACD_BASE_ADD+0x0D)<<2)
#define GM_APL_BIT                     0
#define GM_APL_WID                     11

#define ACD_REG_0E                                      ((ACD_BASE_ADD+0x0E)<<2)
#define GLOBAL_MOTION_HF_BIT           16
#define GLOBAL_MOTION_HF_WID           16
#define GLOBAL_MOTION_HF_MIN_BIT       0
#define GLOBAL_MOTION_HF_MIN_WID       16

#define ACD_REG_0F                                      ((ACD_BASE_ADD+0x0F)<<2)
#define GLOBAL_MOTION_IIR_BIT          16
#define GLOBAL_MOTION_IIR_WID          16
#define GLOBAL_MOTION_LF_BIT           0
#define GLOBAL_MOTION_LF_WID           16

#define ACD_REG_10                                      ((ACD_BASE_ADD+0x10)<<2)
#define YCSEP_TEST43_BIT               24
#define YCSEP_TEST43_WID               8
#define YCSEP_TEST42_BIT               16
#define YCSEP_TEST42_WID               8
#define YCSEP_TEST41_BIT               8
#define YCSEP_TEST41_WID               8
#define YCSEP_TEST40_BIT               0
#define YCSEP_TEST40_WID               8

#define ACD_REG_11                                      ((ACD_BASE_ADD+0x11)<<2)
#define YCSEP_TEST47_BIT               24
#define YCSEP_TEST47_WID               8
#define YCSEP_TEST46_BIT               16
#define YCSEP_TEST46_WID               8
#define YCSEP_TEST45_BIT               8
#define YCSEP_TEST45_WID               8
#define YCSEP_TEST44_BIT               0
#define YCSEP_TEST44_WID               8

#define ACD_REG_12                                      ((ACD_BASE_ADD+0x12)<<2)
#define YCSEP_TEST4B_BIT               24
#define YCSEP_TEST4B_WID               8
#define YCSEP_TEST4A_BIT               16
#define YCSEP_TEST4A_WID               8
#define YCSEP_TEST49_BIT               8
#define YCSEP_TEST49_WID               8
#define YCSEP_TEST48_BIT               0
#define YCSEP_TEST48_WID               8

#define ACD_REG_13                                      ((ACD_BASE_ADD+0x13)<<2)
#define YCSEP_TEST4F_BIT               24
#define YCSEP_TEST4F_WID               8
#define YCSEP_TEST4E_BIT               16
#define YCSEP_TEST4E_WID               8
#define YCSEP_TEST4D_BIT               8
#define YCSEP_TEST4D_WID               8
#define YCSEP_TEST4C_BIT               0
#define YCSEP_TEST4C_WID               8

#define ACD_REG_14                                      ((ACD_BASE_ADD+0x14)<<2)
#define YCSEP_TEST53_BIT               24
#define YCSEP_TEST53_WID               8
#define YCSEP_TEST52_BIT               16
#define YCSEP_TEST52_WID               8
#define YCSEP_TEST51_BIT               8
#define YCSEP_TEST51_WID               8
#define YCSEP_TEST50_BIT               0
#define YCSEP_TEST50_WID               8

#define ACD_REG_15                                      ((ACD_BASE_ADD+0x15)<<2)
#define YCSEP_TEST57_BIT               24
#define YCSEP_TEST57_WID               8
#define YCSEP_TEST56_BIT               16
#define YCSEP_TEST56_WID               8
#define YCSEP_TEST55_BIT               8
#define YCSEP_TEST55_WID               8
#define YCSEP_TEST54_BIT               0
#define YCSEP_TEST54_WID               8

#define ACD_REG_16                                      ((ACD_BASE_ADD+0x16)<<2)
#define YCSEP_TEST5B_BIT               24
#define YCSEP_TEST5B_WID               8
#define YCSEP_TEST5A_BIT               16
#define YCSEP_TEST5A_WID               8
#define YCSEP_TEST59_BIT               8
#define YCSEP_TEST59_WID               8
#define YCSEP_TEST58_BIT               0
#define YCSEP_TEST58_WID               8

#define ACD_REG_17                                      ((ACD_BASE_ADD+0x17)<<2)
#define YCSEP_TEST5F_BIT               24
#define YCSEP_TEST5F_WID               8
#define YCSEP_TEST5E_BIT               16
#define YCSEP_TEST5E_WID               8
#define YCSEP_TEST5D_BIT               8
#define YCSEP_TEST5D_WID               8
#define YCSEP_TEST5C_BIT               0
#define YCSEP_TEST5C_WID               8

#define ACD_REG_18                                      ((ACD_BASE_ADD+0x18)<<2)
#define YCSEP_TEST63_BIT               24
#define YCSEP_TEST63_WID               8
#define YCSEP_TEST62_BIT               16
#define YCSEP_TEST62_WID               8
#define YCSEP_TEST61_BIT               8
#define YCSEP_TEST61_WID               8
#define YCSEP_TEST60_BIT               0
#define YCSEP_TEST60_WID               8

#define ACD_REG_19                                      ((ACD_BASE_ADD+0x19)<<2)
#define YCSEP_TEST67_BIT               24
#define YCSEP_TEST67_WID               8
#define YCSEP_TEST66_BIT               16
#define YCSEP_TEST66_WID               8
#define YCSEP_TEST65_BIT               8
#define YCSEP_TEST65_WID               8
#define YCSEP_TEST64_BIT               0
#define YCSEP_TEST64_WID               8

#define ACD_REG_1A                                      ((ACD_BASE_ADD+0x1A)<<2)
#define YCSEP_TEST6B_BIT               24
#define YCSEP_TEST6B_WID               8
#define YCSEP_TEST6A_BIT               16
#define YCSEP_TEST6A_WID               8
#define YCSEP_TEST69_BIT               8
#define YCSEP_TEST69_WID               8
#define YCSEP_TEST68_BIT               0
#define YCSEP_TEST68_WID               8

#define ACD_REG_1B                                      ((ACD_BASE_ADD+0x1B)<<2)
#define YCSEP_TEST6F_BIT               24
#define YCSEP_TEST6F_WID               8
#define YCSEP_TEST6E_BIT               16
#define YCSEP_TEST6E_WID               8
#define YCSEP_TEST6D_BIT               8
#define YCSEP_TEST6D_WID               8
#define YCSEP_TEST6C_BIT               0
#define YCSEP_TEST6C_WID               8

#define ACD_REG_1C                                      ((ACD_BASE_ADD+0x1C)<<2)
#define YCSEP_TEST73_BIT               24
#define YCSEP_TEST73_WID               8
#define YCSEP_TEST72_BIT               16
#define YCSEP_TEST72_WID               8
#define YCSEP_TEST71_BIT               8
#define YCSEP_TEST71_WID               8
#define YCSEP_TEST70_BIT               0
#define YCSEP_TEST70_WID               8

#define ACD_REG_1D                                      ((ACD_BASE_ADD+0x1D)<<2)
#define YCSEP_TEST75_BIT               8
#define YCSEP_TEST75_WID               8
#define YCSEP_TEST74_BIT               0
#define YCSEP_TEST74_WID               8

#define ACD_REG_1F                                      ((ACD_BASE_ADD+0x1F)<<2)
#define GLOBAL_DETAIL_BIT              16
#define GLOBAL_DETAIL_WID              16
#define GLOBAL_MOTION_PIX_BIT          0
#define GLOBAL_MOTION_PIX_WID          16

#define ACD_REG_20                                      ((ACD_BASE_ADD+0x20)<<2)
#define VBIDE_TEST76_BIT               0
#define VBIDE_TEST76_WID               8

#define ACD_REG_21                                      ((ACD_BASE_ADD+0x21)<<2)
#define AML_VBI_SIZE_BIT               16
#define AML_VBI_SIZE_WID               16
#define AML_VBI_START_ADDR_BIT         0
#define AML_VBI_START_ADDR_WID         16

#define ACD_REG_22                                      ((ACD_BASE_ADD+0x22)<<2)
#define AML_VBI_RST_BIT                31
#define AML_VBI_RST_WID                1
#define AML_ADDR_VS_EN_BIT             27
#define AML_ADDR_VS_EN_WID             1
#define AML_FLUSH_IN_EN_BIT            26
#define AML_FLUSH_IN_EN_WID            1
#define AML_DISAGENT_BIT               24
#define AML_DISAGENT_WID               2
#define AML_VBIDATA_SEL_BIT            23
#define AML_VBIDATA_SEL_WID            1
#define AML_VBI_TH_BIT                 16
#define AML_VBI_TH_WID                 7

#define ACD_REG_23                                      ((ACD_BASE_ADD+0x23)<<2)
#define YCSEP_IFCOMP3_BIT              24
#define YCSEP_IFCOMP3_WID              8
#define YCSEP_IFCOMP2_BIT              16
#define YCSEP_IFCOMP2_WID              8
#define YCSEP_IFCOMP1_BIT              8
#define YCSEP_IFCOMP1_WID              8
#define YCSEP_IFCOMP0_BIT              0
#define YCSEP_IFCOMP0_WID              8

#define ACD_REG_24                                      ((ACD_BASE_ADD+0x24)<<2)
#define YCSEP_IFCOMP_SCALE_BIT         9
#define YCSEP_IFCOMP_SCALE_WID         2
#define YCSEP_IFCOMP_EN_BIT            8
#define YCSEP_IFCOMP_EN_WID            1
#define YCSEP_IFCOMP4_BIT              0
#define YCSEP_IFCOMP4_WID              8

#define ACD_REG_25                                      ((ACD_BASE_ADD+0x25)<<2)
#define FRONT_LPF3_VIDEO_BIT           24
#define FRONT_LPF3_VIDEO_WID           8
#define FRONT_LPF2_VIDEO_BIT           16
#define FRONT_LPF2_VIDEO_WID           8
#define FRONT_LPF1_VIDEO_BIT           8
#define FRONT_LPF1_VIDEO_WID           8
#define FRONT_LPF0_VIDEO_BIT           0
#define FRONT_LPF0_VIDEO_WID           8

#define ACD_REG_26                                      ((ACD_BASE_ADD+0x26)<<2)
#define FRONT_LPF_SCALE_VIDEO_BIT      9
#define FRONT_LPF_SCALE_VIDEO_WID      1
#define FRONT_LPF_EN_VIDEO_BIT         8
#define FRONT_LPF_EN_VIDEO_WID         1
#define FRONT_LPF4_VIDEO_BIT           0
#define FRONT_LPF4_VIDEO_WID           8

#define ACD_REG_27                                      ((ACD_BASE_ADD+0x27)<<2)
#define FRONT_LPF3_VBI_BIT             24
#define FRONT_LPF3_VBI_WID             8
#define FRONT_LPF2_VBI_BIT             16
#define FRONT_LPF2_VBI_WID             8
#define FRONT_LPF1_VBI_BIT             8
#define FRONT_LPF1_VBI_WID             8
#define FRONT_LPF0_VBI_BIT             0
#define FRONT_LPF0_VBI_WID             8

#define ACD_REG_28                                      ((ACD_BASE_ADD+0x28)<<2)
#define ACD_CHROMA_MODE_BIT            14
#define ACD_CHROMA_MODE_WID            2
#define ACD_DEBYPASS_BIT               13
#define ACD_DEBYPASS_WID               1
#define GM_APL_GAIN_MANUEL_BIT         11
#define GM_APL_GAIN_MANUEL_WID         1
#define FRONT_LPF_SCALE_VBI_BIT        9
#define FRONT_LPF_SCALE_VBI_WID        1
#define FRONT_LPF_EN_VBI_BIT           8
#define FRONT_LPF_EN_VBI_WID           1
#define FRONT_LPF4_VBI_BIT             0
#define FRONT_LPF4_VBI_WID             8

#define ACD_REG_29                                      ((ACD_BASE_ADD+0x29)<<2)
#define REG_4F_BUF_END_CNT_BIT         28
#define REG_4F_BUF_END_CNT_WID         3
#define REG_4F_RD_OFFSET_BIT           24
#define REG_4F_RD_OFFSET_WID           4
#define REG_4FRAME_MODE_BIT            23
#define REG_4FRAME_MODE_WID            1
//#define REG_4F_MOTION_ADDR_OFFSET_BIT  0  //removed by IC design
//#define REG_4F_MOTION_ADDR_OFFSET_WID  23

#define ACD_REG_2A                                      ((ACD_BASE_ADD+0x2A)<<2)
#define REG_4F_DISAGENT_BIT            20
#define REG_4F_DISAGENT_WID            2
#define REG_4F_BUF_INI_CNT_BIT         17
#define REG_4F_BUF_INI_CNT_WID         3
#define REG_4F_MOTION_LENGTH_BIT       0
#define REG_4F_MOTION_LENGTH_WID       17

#define ACD_REG_2B                                      ((ACD_BASE_ADD+0x2B)<<2)
#define CPUMP_UP_OFFSET_BIT            24
#define CPUMP_UP_OFFSET_WID            8
#define CPUMP_DN_OFFSET_BIT            16
#define CPUMP_DN_OFFSET_WID            8
#define CPUMP_UPDN_AML_EN_BIT          15
#define CPUMP_UPDN_AML_EN_WID          1
#define CPUMP_UPDN_RATIO_BIT           8
#define CPUMP_UPDN_RATIO_WID           7
#define BP_GATE_VENABLE_BIT            0
#define BP_GATE_VENABLE_WID            1

#define ACD_REG_2C                                      ((ACD_BASE_ADD+0x2C)<<2)
#define BP_GATE_VSTART_BIT             24
#define BP_GATE_VSTART_WID             8
#define BP_GATE_VEND_BIT               16
#define BP_GATE_VEND_WID               8
#define BP_GATE_HSTART_BIT             8
#define BP_GATE_HSTART_WID             8
#define BP_GATE_HEND_BIT               0
#define BP_GATE_HEND_WID               8

#define ACD_REG_2D                                      ((ACD_BASE_ADD+0x2D)<<2)
#define ACD_HSTART_BIT                 16
#define ACD_HSTART_WID                 16
#define ACD_HEND_BIT                   0
#define ACD_HEND_WID                   16

#define ACD_REG_2E                                      ((ACD_BASE_ADD+0x2E)<<2)
#define ACD_VSTART_BIT                 16
#define ACD_VSTART_WID                 16
#define ACD_VEND_BIT                   0
#define ACD_VEND_WID                   16

#define ACD_REG_2F                                      ((ACD_BASE_ADD+0x2F)<<2)
#define VBI_ADDR_OFFSET_BIT            0
#define VBI_ADDR_OFFSET_WID            32

#define ACD_REG_30                                      ((ACD_BASE_ADD+0x30)<<2)
#define MOTION_ADDR_OFFSET_BIT         0
#define MOTION_ADDR_OFFSET_WID         32

#define ACD_REG_31                                      ((ACD_BASE_ADD+0x31)<<2)
#define MEM_WARNING_CLR_BIT            2
#define MEM_WARNING_CLR_WID            1
#define REG_AFIFO_SIZE_BIT             0
#define REG_AFIFO_SIZE_WID             2

#define ACD_REG_33                                      ((ACD_BASE_ADD+0x33)<<2)
#define NOTCH_BW_SEL_BIT                0
#define NOTCH_BW_SEL_WID          8

#define ACD_REG_34                                      ((ACD_BASE_ADD+0x34)<<2)
#define PK_SEARCH_EN_BIT                0
#define PK_SEARCH_EN_WID          1

#define ACD_REG_35                                      ((ACD_BASE_ADD+0x35)<<2)
#define MAX_DAT_REPORT_BIT              0
#define MAX_DAT_REPORT_WID        10

#define ACD_REG_36                                      ((ACD_BASE_ADD+0x36)<<2)
#define MIN_DAT_REPORT_BIT              0
#define MIN_DAT_REPORT_WID        10

#define ACD_REG_37                                      ((ACD_BASE_ADD+0x37)<<2)
#define HIGH_THD_BIT                            0
#define HIGH_THD_WID                      10

#define ACD_REG_38                                      ((ACD_BASE_ADD+0x38)<<2)
#define LOW_THD_BIT                            0
#define LOW_THD_WID                      10

#define ACD_REG_39                                      ((ACD_BASE_ADD+0x39)<<2)
#define MAX_CNT_REPORT_BIT             0
#define MAX_CNT_REPORT_WID       32
#define ACD_REG_3A                                      ((ACD_BASE_ADD+0x3A)<<2)
#define MIN_CNT_REPORT_BIT             0
#define MIN_CNT_REPORT_WID       32

#define ACD_REG_3B                                      ((ACD_BASE_ADD+0x3B)<<2)
#define CLAMP_LEVEL_BIT                     0
#define CLAMP_LEVEL_WID               10

#define ACD_REG_3C                                      ((ACD_BASE_ADD+0x3C)<<2)
#define NOISE_LINE_SEL_BIT                0
#define NOISE_LINE_SEL_WID          10

#define ACD_REG_3D                                      ((ACD_BASE_ADD+0x3D)<<2)
#define NOISE_LEVEL_REPORT_BIT          0
#define NOISE_LEVEL_REPORT_WID    16

#define ACD_REG_3E                                      ((ACD_BASE_ADD+0x3E)<<2)
#define DAT_SUM_REPORT_BIT              0
#define DAT_SUM_REPORT_WID        21

#define ACD_REG_3F                                      ((ACD_BASE_ADD+0x3F)<<2)
#define DAT_MEAN_BIT                           0
#define DAT_MEAN_WID                     10

#define ACD_REG_40                                      ((ACD_BASE_ADD+0x40)<<2)
#define DAT_START_POINT_BIT             0
#define DAT_START_POINT_WID       11

#define ACD_REG_41                                      ((ACD_BASE_ADD+0x41)<<2)
#define DAT_END_POINT_BIT             0
#define DAT_END_POINT_WID       11

#define ACD_REG_53                                      ((ACD_BASE_ADD+0x53)<<2)
#define K4_INPHASE2D_BIT                24
#define K4_INPHASE2D_WID          8
#define K3_TEXTURE3D_BIT               16
#define K3_TEXTURE3D_WID         8
#define K2_INPHASE3D_BIT                8
#define K2_INPHASE3D_WID          8
#define K1_INPHASE3D_BIT                0
#define K1_INPHASE3D_WID          8

#define ACD_REG_54                                      ((ACD_BASE_ADD+0x54)<<2)
#define K8_OOPHASE_B_BIT               24
#define K8_OOPHASE_B_WID         8
#define K7_OOPHASE_N_BIT               16
#define K7_OOPHASE_N_WID         8
#define K6_INPHASE2D_BIT                16
#define K6_INPHASE2D_WID          8
#define K5_TEXTURE3D_BIT               0
#define K5_TEXTURE3D_WID         8

#define ACD_REG_55                                              ((ACD_BASE_ADD+0x55)<<2)
#define HCFD_MTN_BIT                                        31
#define HCFD_MTN_WID                                  1
#define MD2_MAINPATH_MTN_EN_BIT                 30
#define MD2_MAINPATH_MTN_EN_WID           1
#define MD2_HCFD_TXT3D_INP2D_MIN_BIT        23
#define MD2_HCFD_TXT3D_INP2D_MIN_WID  1
#define MD2_INP2D_MIN_BIT                               22
#define MD2_INP2D_MIN_WID                         1
#define MAINPATH_MTN_TH_BIT                         12
#define MAINPATH_MTN_TH_WID                   10
#define HCFD_MTN_TH_BIT                                 0
#define HCFD_MTN_TH_WID                           10

#define ACD_REG_56                                      ((ACD_BASE_ADD+0x56)<<2)
#define CDETECT_CAGC_LOOPIN2_EN_BIT             31
#define CDETECT_CAGC_LOOPIN2_EN_WID       1
#define CHROMA_DUALLOWPASS_MTN2_SEL_BIT  28
#define CHROMA_DUALLOWPASS_MTN2_SEL_WID  2
#define CHROMA_DUALLOWPASS_MTN1_SEL_BIT        26
#define CHROMA_DUALLOWPASS_MTN1_SEL_WID  2
#define CHROMA_DUALLOWPASS_MTN_SEL_BIT          24
#define CHROMA_DUALLOWPASS_MTN_SEL_WID    2    
#define HXCR_MOT_FILT_SEL_BIT                               22
#define HXCR_MOT_FILT_SEL_WID                         2
#define HXCR_NOMOT_FILT_SEL_BIT                          20
#define HXCR_NOMOT_FILT_SEL_WID                    2
#define CDETECT_CHROMA_GAIN_MAX_DEC_TC_BIT         16
#define CDETECT_CHROMA_GAIN_MAX_DEC_TC_WID      3
#define CDETECT_CHROMA_GAIN_MAX_DEC_VALUE_BIT         8
#define CDETECT_CHROMA_GAIN_MAX_DEC_VALUE_WID   8    
#define CDETECT_CHROMA_ALLOWED_MAX_BIT          0
#define CDETECT_CHROMA_ALLOWED_MAX_WID    8

#define ACD_REG_57                                          ((ACD_BASE_ADD+0x57)<<2)
#define CONTRAST_AML_EN_BIT                 31
#define CONTRAST_AML_EN_WID           1
#define CONTRAST_AML_BIT                        16
#define CONTRAST_AML_WID                  10
#define BRIGHTNESS_AML_EN_BIT              15
#define BRIGHTNESS_AML_EN_WID        1
#define BRIGHTNESS_AML_BIT                     0
#define BRIGHTNESS_AML_WID               9

#define ACD_REG_58                                          ((ACD_BASE_ADD+0x58)<<2)
#define F01234_STILL_EN_BIT                             31        
#define F01234_STILL_EN_WID                       1
#define MD2_MAIN_PATH_MTN_SHIFT_BIT         25
#define MD2_MAIN_PATH_MTN_SHIFT_WID   3
#define F4_STILL_EN_BIT                                     24
#define F4_STILL_EN_WID                               1
#define F3_STILL_EN_BIT                                     23
#define F3_STILL_EN_WID                               1
#define F2_STILL_EN_BIT                                     22
#define F2_STILL_EN_WID                               1
#define F1_STILL_EN_BIT                                     21
#define F1_STILL_EN_WID                               1
#define F0_STILL_EN_BIT                                     20
#define F0_STILL_EN_WID                               1
#define F4_STILL_TH_BIT                                     16
#define F4_STILL_TH_WID                               4
#define F3_STILL_TH_BIT                                     12
#define F3_STILL_TH_WID                               4
#define F2_STILL_TH_BIT                                     8
#define F2_STILL_TH_WID                               4
#define F1_STILL_TH_BIT                                     4
#define F1_STILL_TH_WID                               4
#define F0_STILL_TH_BIT                                     0
#define F0_STILL_TH_WID                               4

#define ACD_REG_59                                      ((ACD_BASE_ADD+0x59)<<2)
#define F01234_MOV_EN_BIT                       31
#define F01234_MOV_EN_WID                 1
#define F4_MOV_EN_BIT                               24
#define F4_MOV_EN_WID                         1
#define F3_MOV_EN_BIT                               23
#define F3_MOV_EN_WID                         1
#define F2_MOV_EN_BIT                               22
#define F2_MOV_EN_WID                         1
#define F1_MOV_EN_BIT                               21
#define F1_MOV_EN_WID                         1
#define F0_MOV_EN_BIT                               20
#define F0_MOV_EN_WID                         1
#define F4_MOV_TH_BIT                               16
#define F4_MOV_TH_WID                         4
#define F3_MOV_TH_BIT                               12
#define F3_MOV_TH_WID                         4
#define F2_MOV_TH_BIT                               8
#define F2_MOV_TH_WID                         4
#define F1_MOV_TH_BIT                               4
#define F1_MOV_TH_WID                         4
#define F0_MOV_TH_BIT                               0
#define F0_MOV_TH_WID                         4

#define ACD_REG_5A                                      ((ACD_BASE_ADD+0x5A)<<2)
#define LF_DIFF_F4_F2_TH_BIT                   16
#define LF_DIFF_F4_F2_TH_WID             10
#define LF_DIFF_F0_F2_TH_BIT                   0
#define LF_DIFF_F0_F2_TH_WID             10

#define ACD_REG_5B                                  ((ACD_BASE_ADD+0x5B)<<2)
#define  REG_MIN_TXT3D_INP2D_BIT     16
#define  REG_MIN_TXT3D_INP2D_WID    1
#define  REG_K10_P0N_M625N_BIT         8
#define  REG_K10_P0N_M625N_WID        8
#define  REG_K9_P0N_P625N_BIT           0
#define  REG_K9_P0N_P625N_WID          8



#define ACD_REG_5D                                      ((ACD_BASE_ADD+0x5D)<<2)
#define CHROMA_PEAK_TAP_SEL_BIT       0
#define CHROMA_PEAK_TAP_SEL_WID     3

#define ACD_REG_64                                      ((ACD_BASE_ADD+0x64)<<2)
#define K4_IP_ERR_BIT                               24
#define K4_IP_ERR_WID                         8
#define K3_CORE_IP_BIT                            16
#define K3_CORE_IP_WID                      8
#define K2_LP_ERR_BIT                              8
#define K2_LP_ERR_WID                        8
#define K1_HP_ERR_BIT                             0
#define K1_HP_ERR_WID                       8

#define ACD_REG_65                                      ((ACD_BASE_ADD+0x65)<<2)
#define K8_DISABLE_TB_BIT                      24
#define K8_DISABLE_TB_WID                8
#define K7_DISABLE_TB_BIT                      16
#define K7_DISABLE_TB_WID                8
#define K6_DISABLE_TB_BIT                      8
#define K6_DISABLE_TB_WID                8
#define K5_FC_TB_GAIN_BIT                      0
#define K5_FC_TB_GAIN_WID                8

#define ACD_REG_66                                      ((ACD_BASE_ADD+0x66)<<2)
#define AML_2DCOMB_EN_BIT                   31
#define AML_2DCOMB_EN_WID             1
#define NOTCH_VCOMB_YIN_SEL_BIT      10  
#define NOTCH_VCOMB_YIN_SEL_WID   2
#define NOTCH_VCOMB_CIN_SEL_BIT         8  
#define NOTCH_VCOMB_CIN_SEL_WID   2
#define HP_IP_ERR_MIN_BIT                       7
#define HP_IP_ERR_MIN_WID                 1
#define AML_HDIFF_IN_SEL_BIT                 6
#define AML_HDIFF_IN_SEL_WID           1
#define VCOMB_IN_SEL_BIT                        4
#define VCOMB_IN_SEL_WID                  2
#define CORE_IP_SEL_BIT                           2
#define CORE_IP_SEL_WID                     2
#define HP_ERR_SEL_BIT                            0
#define HP_ERR_SEL_WID                      2

#define ACD_REG_67                                      ((ACD_BASE_ADD+0x67)<<2)
#define FC1D_100PCT_HP_ENERGY_TH_BIT            24
#define FC1D_100PCT_HP_ENERGY_TH_WID      8
#define HP_CONLY_CTH_BIT                                       16
#define HP_CONLY_CTH_WID                                 8
#define HP_ENERGY_SMALL_TH_BIT                          8
#define HP_ENERGY_SMALL_TH_WID                    8
#define REG_HP_CONY_YTH_BIT                           0
#define REG_HP_CONY_YTH_WID                         8
#define ACD_REG_68                                      ((ACD_BASE_ADD+0x68)<<2)
#define FC1D_100PCT_EN_BIT						31
#define FC1D_100PCT_EN_WID					1
#define FC2D_100PCT_PSBMB_MB_TH_BIT				20
#define FC2D_100PCT_PSBMB_MB_TH_WID				8
#define FC2D_100PCT_PSBMB_MB_ERR_TB_TH_BIT			8
#define FC2D_100PCT_PSBMB_MB_ERR_TB_TH_WID		12
#define FC_MIC_MB_TH_BIT							0
#define FC_MIC_MB_TH_WID							8

#define ACD_REG_69                                      ((ACD_BASE_ADD+0x69)<<2)
#define DISABLE_TB_TH1_BIT						24
#define DISABLE_TB_TH1_WID					8
#define DISABLE_TB_TH0_BIT						16
#define DISABLE_TB_TH0_WID					8
#define DIS_TB_EN_BIT							13
#define DIS_TB_EN_WID							1
#define	FC_TB_EN_BIT							12
#define FC_TB_EN_WID							1
#define FC_TB_TH_BIT                 0
#define FC_TB_TH_WID                12

#define ACD_REG_6A                                      ((ACD_BASE_ADD+0x6A)<<2)
#define DISABLE_TB_OFFSET_BIT					24
#define DISABLE_TB_OFFSET_WID					8
#define DISABLE_TB_TH3_BIT						12
#define DISABLE_TB_TH3_WID					12
#define DISABLE_TB_TH2_BIT						0
#define DISABLE_TB_TH2_WID					12

#define ACD_REG_6B                                      ((ACD_BASE_ADD+0x6B)<<2)
#define FC_MID_TH_BIT							0
#define FC_MID_TH_WID							12

#define ACD_REG_6C                                      ((ACD_BASE_ADD+0x6C)<<2)
#define AML_NTSC_LBUF_SEL_BIT					31
#define AML_NTSC_LBUF_SEL_WID					1
#define DBG_2DCOMB_OFFSET_BIT					8
#define DBG_2DCOMB_OFFSET_WID					10
#define AML_2DCOMB_DBG_SEL_BIT					0
#define AML_2DCOMB_DBG_SEL_WID				8

#define ACD_REG_6F                                      ((ACD_BASE_ADD+0x6F)<<2)
#define AML_LNOTCH_EN_BIT						31
#define AML_LNOTCH_EN_WID						1
#define LNOTCH_SCALE_BIT						29
#define LNOTCH_SCALE_WID						2
#define LNOTCH_ALPHA1_BIT						12
#define LNOTCH_ALPHA1_WID						10
#define LNOTCH_ALPHA0_BIT						0
#define LNOTCH_ALPHA0_WID						12

#define ACD_REG_70                                      ((ACD_BASE_ADD+0x70)<<2)
#define LNOTCH_ALPHA4_BIT						24
#define LNOTCH_ALPHA4_WID						8
#define LNOTCH_ALPHA3_BIT						16
#define LNOTCH_ALPHA3_WID						8
#define LNOTCH_ALPHA2_BIT						0
#define LNOTCH_ALPHA2_WID						10

#define ACD_REG_71                                      ((ACD_BASE_ADD+0x71)<<2)
#define LNOTCH_ALPHA8_BIT						24
#define LNOTCH_ALPHA8_WID						8
#define LNOTCH_ALPHA7_BIT						16
#define LNOTCH_ALPHA7_WID						8
#define LNOTCH_ALPHA6_BIT						8
#define LNOTCH_ALPHA6_WID						8
#define LNOTCH_ALPHA5_BIT						0
#define LNOTCH_ALPHA5_WID						8

#define ACD_REG_74                                      ((ACD_BASE_ADD+0x74)<<2)
#define WIND_COR_RATE_BIT						24
#define WIND_COR_RATE_WID						8
#define WIND_COR_OFST_BIT						16
#define WIND_COR_OFST_WID						8
#define WIND_NORM_BIT							8
#define WIND_NORM_WID							8
#define WIND_SIZE_BIT							0
#define WIND_SIZE_WID							8

#define ACD_REG_75                                      ((ACD_BASE_ADD+0x75)<<2)
#define ALPHA_FORCE_MAX_BIT						31
#define ALPHA_FORCE_MAX_WID					1
#define ADPT_NOTCH_DELAY_BIT					8
#define ADPT_NOTCH_DELAY_WID					11
#define WIND_ALPHA_GAIN_BIT						0
#define WIND_ALPHA_GAIN_WIDHT 					8
#define ACD_REG_78                                      ((ACD_BASE_ADD+0x78)<<2)
#define AML_2DCOMB_FULL_SIG_IP_ERR_UPLO_TH_BIT		12
#define AML_2DCOMB_FULL_SIG_IP_ERR_UPLO_TH_WID	12
#define AML_2DCOMB_FULL_SIG_IP_ERR_TH_BIT			0
#define AML_2DCOMB_FULL_SIG_IP_ERR_TH_WID			12

#define ACD_REG_79                                      ((ACD_BASE_ADD+0x79)<<2)
#define AML_2DCOMB_FULL_SIG_EN_BIT					31
#define AML_2DCOMB_FULL_SIG_EN_WID				        1
#define AML_2DCOMB_FULL_SIG_SHIFT_BIT				28
#define AML_2DCOMB_FULL_SIG_SHIFT_WID				3
#define AML_2DCOMB_FULL_SIG_WIN_BIT					24
#define AML_2DCOMB_FULL_SIG_WIN_WID				2
#define AML_2DCOMB_FULL_SIG_LP_HTRANS_TH_BIT		12
#define AML_2DCOMB_FULL_SIG_LP_HTRANS_TH_WID		10
#define AML_2DCOMB_FULL_SIG_HP_ENG_TH_BIT			0
#define AML_2DCOMB_FULL_SIG_HP_ENG_TH_WID			10

#define ACD_REG_7B                                      ((ACD_BASE_ADD+0x7B)<<2)
#define BD_VACTIVE_HEIGHT_BIT						24
#define BD_VACTIVE_HEIGHT_WID						8
#define BD_VACTIVE_START_BIT						16
#define BD_VACTIVE_START_WID						8
#define BD_BURST_GATE_END_BIT						8
#define BD_BURST_GATE_END_WID						8
#define BD_BURST_GATE_START_BIT						0
#define BD_BURST_GATE_START_WID					8

#define ACD_REG_7C                                      ((ACD_BASE_ADD+0x7C)<<2)
#define HP_IIR_COEF1_BIT							12
#define HP_IIR_COEF1_WID							12
#define HP_IIR_COEF0_BIT							0
#define HP_IIR_COEF0_WID							12

#define ACD_REG_7D                                      ((ACD_BASE_ADD+0x7D)<<2)
#define HP_IIR358_COEF1_BIT							12
#define HP_IIR358_COEF1_WID						12
#define HP_IIR358_COEF0_BIT							0
#define HP_IIR358_COEF0_WID						12

#define ACD_REG_7E                                      ((ACD_BASE_ADD+0x7E)<<2)
#define HP_IIR425_COEF1_BIT							12
#define HP_IIR425_COEF1_WID						12
#define HP_IIR425_COEF0_BIT							0
#define HP_IIR425_COEF0_WID						12

#define ACD_REG_80                                      ((ACD_BASE_ADD+0x80)<<2)
#define HP_IIR443_COEF1_BIT							12
#define HP_IIR443_COEF1_WID						12
#define HP_IIR443_COEF0_BIT							0
#define HP_IIR443_COEF0_WID						12
#define ACD_REG_81                                      ((ACD_BASE_ADD+0x81)<<2)
#define BD_BURST_VLD_TH0_BIT						16
#define BD_BURST_VLD_TH0_WID						16
#define BD_BURST_VLD_TH1_BIT						0
#define BD_BURST_VLD_TH1_WID						16

#define ACD_REG_82                                      ((ACD_BASE_ADD+0x82)<<2)
#define BD_CLEAR_SECAM_STATUS_BIT					31
#define BD_CLEAR_SECAM_STATUS_WID					1
#define BD_CLEAR_SECAM_VSYNC_BIT					30
#define BD_CLEAR_SECAM_VSYNC_WID					1
#define BD_SECAM_DETECTED_FLD_CNT_BIT				24
#define BD_SECAM_DETECTED_FLD_CNT_WID				3
#define BD_SECAM_CFD_LEVEL_TH_BIT					16
#define BD_SECAM_CFD_LEVEL_TH_WID					6
#define BD_SECAM_CFD_DEC_STEP_BIT					12
#define BD_SECAM_CFD_DEC_STEP_WID					4
#define BD_SECAM_CFD_INC_STEP_BIT					8
#define BD_SECAM_CFD_INC_STEP_WID					4
#define BD_BDDR_CFD_DEC_STEP_BIT					4
#define BD_BDDR_CFD_DEC_STEP_WID					4
#define BD_BDDR_CFD_INC_STEP_BIT					0
#define BD_BDDR_CFD_INC_STEP_WID					4

#define ACD_REG_83                                      ((ACD_BASE_ADD+0x83)<<2)
#define RO_BD_ACC4XX_CNT_BIT						24
#define RO_BD_ACC4XX_CNT_WID						8
#define RO_BD_ACC425_CNT_BIT						16
#define RO_BD_ACC425_CNT_WID						8
#define RO_BD_ACC3XX_CNT_BIT						8
#define RO_BD_ACC3XX_CNT_WID						8
#define RO_BD_ACC358_CNT_BIT						0
#define RO_BD_ACC358_CNT_WID						8

#define ACD_REG_84                                      ((ACD_BASE_ADD+0x84)<<2)
#define RO_DBDR_PHASE_BIT							1
#define RO_DBDR_PHASE_WID							1
#define RO_BD_SECAM_DETECTED_BIT					0
#define RO_BD_SECAM_DETECTED_WID					1

#define ACD_REG_85                                      ((ACD_BASE_ADD+0x85)<<2)
#define DBDR_SLICE_VCOUNTER_BIT						14
#define DBDR_SLICE_VCOUNTER_WID					11
#define IIR_ROUND_BIT								12
#define IIR_ROUND_WID								2
#define BD_IIR_MUTE_OOBW_EN_BIT						8
#define BD_IIR_MUTE_OOBW_EN_WID					4
#define BD_SECAM_CFD_INC_MAX_BIT					4
#define BD_SECAM_CFD_INC_MAX_WID					4
#define BD_DBDR_CFD_INC_MAX_BIT						0
#define BD_DBDR_CFD_INC_MAX_WID					4

#define ACD_REG_86                                      ((ACD_BASE_ADD+0x86)<<2)
#define CNARROW_SCALE_BIT							30
#define CNARROW_SCALE_WID							2
#define CNARROW_ALPHA2_BIT							20
#define CNARROW_ALPHA2_WID						10
#define CNARROW_ALPHA1_BIT							10
#define CNARROW_ALPHA1_WID						10
#define CNARROW_ALPHA0_BIT							0
#define CNARROW_ALPHA0_WID						10

#define ACD_REG_87                                      ((ACD_BASE_ADD+0x87)<<2)
#define CNARROW_ALPHA5_BIT							20
#define CNARROW_ALPHA5_WID						10
#define CNARROW_ALPHA4_BIT							10
#define CNARROW_ALPHA4_WID						10
#define CNARROW_ALPHA3_BIT							0
#define CNARROW_ALPHA3_WID						10

#define ACD_REG_88                                      ((ACD_BASE_ADD+0x88)<<2)
#define CNARROW_ALPHA8_BIT							20
#define CNARROW_ALPHA8_WID						10
#define CNARROW_ALPHA7_BIT							10
#define CNARROW_ALPHA7_WID						10
#define CNARROW_ALPHA6_BIT							0
#define CNARROW_ALPHA6_WID						10

#define ACD_REG_89                                      ((ACD_BASE_ADD+0x89)<<2)
#define MD2_DOT_SUP_MODE_EN_BIT						31
#define MD2_DOT_SUP_MODE_EN_WID					1
#define MD2_DOT_SUP_HP_ENG_BIT						12
#define MD2_DOT_SUP_HP_ENG_WID					10
#define MD2_DOT_SUP_INP3D_TH_BIT					0
#define MD2_DOT_SUP_INP3D_TH_WID					10

#define ACD_REG_A3                                      ((ACD_BASE_ADD+0xA3)<<2)
#define	DAGC_LOOPIN2_EN_BIT							31
#define DAGC_LOOPIN2_EN_WID						1
#define CLAMPAGC_COMPLUMA_MAX_TH_BIT				16
#define CLAMPAGC_COMPLUMA_MAX_TH_WID				12
#define CLAMPAGC_COMPLUMA_MIN_TH_BIT				0
#define CLAMPAGC_COMPLUMA_MIN_TH_WID				12

#define ACD_REG_A4                                      ((ACD_BASE_ADD+0xA4)<<2)
#define CLMPAGC_COMPLUMA_MAXMIN_TC_BIT				16
#define CLMPAGC_COMPLUMA_MAXMIN_TC_WID			3
#define CLMPAGC_COMPLUMA_DEC_VALUE_BIT				0
#define CLMPAGC_COMPLUMA_DEC_VALUE_WID			12

//reg of acd below of here is to be reserved

#define ACD_REG_1E					((ACD_BASE_ADD+0x1E)<<2)
#define ACD_REG_32					((ACD_BASE_ADD+0x32)<<2)
#define ACD_REG_42					((ACD_BASE_ADD+0x42)<<2)
#define ACD_REG_43					((ACD_BASE_ADD+0x43)<<2)
#define ACD_REG_44					((ACD_BASE_ADD+0x44)<<2)
#define ACD_REG_45					((ACD_BASE_ADD+0x45)<<2)
#define ACD_REG_46					((ACD_BASE_ADD+0x46)<<2)
#define ACD_REG_47					((ACD_BASE_ADD+0x47)<<2)
#define ACD_REG_48					((ACD_BASE_ADD+0x48)<<2)
#define ACD_REG_49					((ACD_BASE_ADD+0x49)<<2)
#define ACD_REG_4A					((ACD_BASE_ADD+0x4A)<<2)
#define ACD_REG_4B					((ACD_BASE_ADD+0x4B)<<2)
#define ACD_REG_4C					((ACD_BASE_ADD+0x4C)<<2)
#define ACD_REG_4D					((ACD_BASE_ADD+0x4D)<<2)
#define ACD_REG_4E					((ACD_BASE_ADD+0x4E)<<2)
#define ACD_REG_4F					((ACD_BASE_ADD+0x4F)<<2)
#define ACD_REG_50					((ACD_BASE_ADD+0x50)<<2)
#define ACD_REG_51					((ACD_BASE_ADD+0x51)<<2)
#define ACD_REG_52					((ACD_BASE_ADD+0x52)<<2)
#define ACD_REG_5C					((ACD_BASE_ADD+0x5C)<<2)
#define ACD_REG_5E					((ACD_BASE_ADD+0x5E)<<2)
#define ACD_REG_5F					((ACD_BASE_ADD+0x5F)<<2)
#define ACD_REG_60					((ACD_BASE_ADD+0x60)<<2)
#define ACD_REG_61					((ACD_BASE_ADD+0x61)<<2)
#define ACD_REG_62					((ACD_BASE_ADD+0x62)<<2)
#define ACD_REG_63					((ACD_BASE_ADD+0x63)<<2)
#define ACD_REG_6D					((ACD_BASE_ADD+0x6D)<<2)
#define ACD_REG_6E					((ACD_BASE_ADD+0x6E)<<2)
#define ACD_REG_72					((ACD_BASE_ADD+0x72)<<2)
#define ACD_REG_73					((ACD_BASE_ADD+0x73)<<2)
#define ACD_REG_76					((ACD_BASE_ADD+0x76)<<2)
#define ACD_REG_77					((ACD_BASE_ADD+0x77)<<2)
#define ACD_REG_7A					((ACD_BASE_ADD+0x7A)<<2)
#define ACD_REG_7F					((ACD_BASE_ADD+0x7F)<<2)
#define ACD_REG_8A					((ACD_BASE_ADD+0x8A)<<2)
#define ACD_REG_8B					((ACD_BASE_ADD+0x8B)<<2)
#define ACD_REG_8C					((ACD_BASE_ADD+0x8C)<<2)
#define ACD_REG_8D					((ACD_BASE_ADD+0x8D)<<2)
#define ACD_REG_8E					((ACD_BASE_ADD+0x8E)<<2)
#define ACD_REG_8F					((ACD_BASE_ADD+0x8F)<<2)
#define ACD_REG_90					((ACD_BASE_ADD+0x90)<<2)
#define ACD_REG_91					((ACD_BASE_ADD+0x91)<<2)
#define ACD_REG_92					((ACD_BASE_ADD+0x92)<<2)
#define ACD_REG_93					((ACD_BASE_ADD+0x93)<<2)
#define ACD_REG_94					((ACD_BASE_ADD+0x94)<<2)
#define ACD_REG_95					((ACD_BASE_ADD+0x95)<<2)
#define ACD_REG_96					((ACD_BASE_ADD+0x96)<<2)
#define ACD_REG_97					((ACD_BASE_ADD+0x97)<<2)
#define ACD_REG_98					((ACD_BASE_ADD+0x98)<<2)
#define ACD_REG_99					((ACD_BASE_ADD+0x99)<<2)
#define ACD_REG_9A					((ACD_BASE_ADD+0x9A)<<2)
#define ACD_REG_9B					((ACD_BASE_ADD+0x9B)<<2)
#define ACD_REG_9C					((ACD_BASE_ADD+0x9C)<<2)
#define ACD_REG_9D					((ACD_BASE_ADD+0x9D)<<2)
#define ACD_REG_9E					((ACD_BASE_ADD+0x9E)<<2)
#define ACD_REG_9F					((ACD_BASE_ADD+0x9F)<<2)
#define ACD_REG_A0					((ACD_BASE_ADD+0xA0)<<2)
#define ACD_REG_A1					((ACD_BASE_ADD+0xA1)<<2)
#define ACD_REG_A2					((ACD_BASE_ADD+0xA2)<<2)

//reg of acd below of here is to be reserved


#define ACD_REG_A5					((ACD_BASE_ADD+0xA5)<<2)
#define ACD_REG_A6					((ACD_BASE_ADD+0xA6)<<2)
#define ACD_REG_A7					((ACD_BASE_ADD+0xA7)<<2)
#define ACD_REG_A8					((ACD_BASE_ADD+0xA8)<<2)
#define ACD_REG_A9					((ACD_BASE_ADD+0xA9)<<2)
#define ACD_REG_AA					((ACD_BASE_ADD+0xAA)<<2)
#define ACD_REG_AB					((ACD_BASE_ADD+0xAB)<<2)
#define ACD_REG_AC					((ACD_BASE_ADD+0xAC)<<2)
#define ACD_REG_AD					((ACD_BASE_ADD+0xAD)<<2)
#define ACD_REG_AE					((ACD_BASE_ADD+0xAE)<<2)
#define ACD_REG_AF					((ACD_BASE_ADD+0xAF)<<2)
#define ACD_REG_B0					((ACD_BASE_ADD+0xB0)<<2)
#define ACD_REG_B1					((ACD_BASE_ADD+0xB1)<<2)
#define ACD_REG_B2					((ACD_BASE_ADD+0xB2)<<2)
#define ACD_REG_B3					((ACD_BASE_ADD+0xB3)<<2)
#define ACD_REG_B4					((ACD_BASE_ADD+0xB4)<<2)
#define ACD_REG_B5					((ACD_BASE_ADD+0xB5)<<2)
#define ACD_REG_B6					((ACD_BASE_ADD+0xB6)<<2)
#define ACD_REG_B7					((ACD_BASE_ADD+0xB7)<<2)
#define ACD_REG_B8					((ACD_BASE_ADD+0xB8)<<2)
#define ACD_REG_B9					((ACD_BASE_ADD+0xB9)<<2)
#define ACD_REG_BA					((ACD_BASE_ADD+0xBA)<<2)
#define ACD_REG_BB					((ACD_BASE_ADD+0xBB)<<2)
#define ACD_REG_BC					((ACD_BASE_ADD+0xBC)<<2)
#define ACD_REG_BD					((ACD_BASE_ADD+0xBD)<<2)
#define ACD_REG_BE					((ACD_BASE_ADD+0xBE)<<2)
#define ACD_REG_BF					((ACD_BASE_ADD+0xBF)<<2)
#define ACD_REG_C0					((ACD_BASE_ADD+0xC0)<<2)
#define ACD_REG_C1					((ACD_BASE_ADD+0xC1)<<2)
#define ACD_REG_C2					((ACD_BASE_ADD+0xC2)<<2)
#define ACD_REG_C3					((ACD_BASE_ADD+0xC3)<<2)
#define ACD_REG_C4					((ACD_BASE_ADD+0xC4)<<2)
#define ACD_REG_C5					((ACD_BASE_ADD+0xC5)<<2)
#define ACD_REG_C6					((ACD_BASE_ADD+0xC6)<<2)
#define ACD_REG_C7					((ACD_BASE_ADD+0xC7)<<2)
#define ACD_REG_C8					((ACD_BASE_ADD+0xC8)<<2)
#define ACD_REG_C9					((ACD_BASE_ADD+0xC9)<<2)
#define ACD_REG_CA					((ACD_BASE_ADD+0xCA)<<2)
#define ACD_REG_CB					((ACD_BASE_ADD+0xCB)<<2)
#define ACD_REG_CC					((ACD_BASE_ADD+0xCC)<<2)
#define ACD_REG_CD					((ACD_BASE_ADD+0xCD)<<2)
#define ACD_REG_CE					((ACD_BASE_ADD+0xCE)<<2)
#define ACD_REG_CF					((ACD_BASE_ADD+0xCF)<<2)
#define ACD_REG_D0					((ACD_BASE_ADD+0xD0)<<2)
#define ACD_REG_D1					((ACD_BASE_ADD+0xD1)<<2)
#define ACD_REG_D2					((ACD_BASE_ADD+0xD2)<<2)
#define ACD_REG_D3					((ACD_BASE_ADD+0xD3)<<2)
#define ACD_REG_D4					((ACD_BASE_ADD+0xD4)<<2)
#define ACD_REG_D5					((ACD_BASE_ADD+0xD5)<<2)
#define ACD_REG_D6					((ACD_BASE_ADD+0xD6)<<2)
#define ACD_REG_D7					((ACD_BASE_ADD+0xD7)<<2)
#define ACD_REG_D8					((ACD_BASE_ADD+0xD8)<<2)
#define ACD_REG_D9					((ACD_BASE_ADD+0xD9)<<2)
#define ACD_REG_DA					((ACD_BASE_ADD+0xDA)<<2)
#define ACD_REG_DB					((ACD_BASE_ADD+0xDB)<<2)
#define ACD_REG_DC					((ACD_BASE_ADD+0xDC)<<2)
#define ACD_REG_DD					((ACD_BASE_ADD+0xDD)<<2)
#define ACD_REG_DE					((ACD_BASE_ADD+0xDE)<<2)
#define ACD_REG_DF					((ACD_BASE_ADD+0xDF)<<2)
#define ACD_REG_E0					((ACD_BASE_ADD+0xE0)<<2)
#define ACD_REG_E1					((ACD_BASE_ADD+0xE1)<<2)
#define ACD_REG_E2					((ACD_BASE_ADD+0xE2)<<2)
#define ACD_REG_E3					((ACD_BASE_ADD+0xE3)<<2)
#define ACD_REG_E4					((ACD_BASE_ADD+0xE4)<<2)
#define ACD_REG_E5					((ACD_BASE_ADD+0xE5)<<2)
#define ACD_REG_E6					((ACD_BASE_ADD+0xE6)<<2)
#define ACD_REG_E7					((ACD_BASE_ADD+0xE7)<<2)
#define ACD_REG_E8					((ACD_BASE_ADD+0xE8)<<2)
#define ACD_REG_E9					((ACD_BASE_ADD+0xE9)<<2)
#define ACD_REG_EA					((ACD_BASE_ADD+0xEA)<<2)
#define ACD_REG_EB					((ACD_BASE_ADD+0xEB)<<2)
#define ACD_REG_EC					((ACD_BASE_ADD+0xEC)<<2)
#define ACD_REG_ED					((ACD_BASE_ADD+0xED)<<2)
#define ACD_REG_EE					((ACD_BASE_ADD+0xEE)<<2)
#define ACD_REG_EF					((ACD_BASE_ADD+0xEF)<<2)
#define ACD_REG_F0					((ACD_BASE_ADD+0xF0)<<2)
#define ACD_REG_F1					((ACD_BASE_ADD+0xF1)<<2)
#define ACD_REG_F2					((ACD_BASE_ADD+0xF2)<<2)
#define ACD_REG_F3					((ACD_BASE_ADD+0xF3)<<2)
#define ACD_REG_F4					((ACD_BASE_ADD+0xF4)<<2)
#define ACD_REG_F5					((ACD_BASE_ADD+0xF5)<<2)
#define ACD_REG_F6					((ACD_BASE_ADD+0xF6)<<2)
#define ACD_REG_F7					((ACD_BASE_ADD+0xF7)<<2)
#define ACD_REG_F8					((ACD_BASE_ADD+0xF8)<<2)
#define ACD_REG_F9					((ACD_BASE_ADD+0xF9)<<2)
#define ACD_REG_FA					((ACD_BASE_ADD+0xFA)<<2)
#define ACD_REG_FB					((ACD_BASE_ADD+0xFB)<<2)
#define ACD_REG_FC					((ACD_BASE_ADD+0xFC)<<2)
#define ACD_REG_FD					((ACD_BASE_ADD+0xFD)<<2)
#define ACD_REG_FE					((ACD_BASE_ADD+0xFE)<<2)
#define ACD_REG_FF					((ACD_BASE_ADD+0xFF)<<2)



// ****************************************************************************
// ******** ADC REGISTERS ********
// ****************************************************************************

#define ADC_BASE_ADD                                    0x3200//0x1A00

#define ADC_REG_00                                      ((ADC_BASE_ADD+0x00)<<2)
#define CHIPREV_BIT                    0
#define CHIPREV_WID                    8

#define ADC_REG_01                                      ((ADC_BASE_ADD+0x01)<<2)
#define PLLDIVRATIO_MSB_BIT            0
#define PLLDIVRATIO_MSB_WID            8

#define ADC_REG_02                                      ((ADC_BASE_ADD+0x02)<<2)
#define PLLDIVRATIO_LSB_BIT            4
#define PLLDIVRATIO_LSB_WID            4

#define ADC_REG_03                                      ((ADC_BASE_ADD+0x03)<<2)
#define CLAMPPLACEM_BIT                0
#define CLAMPPLACEM_WID                8

#define ADC_REG_04                                      ((ADC_BASE_ADD+0x04)<<2)
#define CLAMPDURATION_BIT              0
#define CLAMPDURATION_WID              8

#define ADC_REG_05                                      ((ADC_BASE_ADD+0x05)<<2)
#define PGAGAIN_BIT                    0
#define PGAGAIN_WID                    8

#define ADC_REG_06                                      ((ADC_BASE_ADD+0x06)<<2)
#define PGAMODE_BIT                    5
#define PGAMODE_WID                    1
#define ENPGA_BIT                      4
#define ENPGA_WID                      1

#define ADC_REG_07                                      ((ADC_BASE_ADD+0x07)<<2)
#define ADCGAINA_BIT                   0
#define ADCGAINA_WID                   8

#define ADC_REG_08                                      ((ADC_BASE_ADD+0x08)<<2)
#define ADCGAINB_BIT                   0
#define ADCGAINB_WID                   8

#define ADC_REG_09                                      ((ADC_BASE_ADD+0x09)<<2)
#define ADCGAINC_BIT                   0
#define ADCGAINC_WID                   8

//#define ADC_REG_0A                                    ((ADC_BASE_ADD+0x0A)<<2)
#define ADC_REG_0B                                      ((ADC_BASE_ADD+0x0B)<<2)
#define ENSTCA_BIT                     7
#define ENSTCA_WID                     1
#define CTRCLREFA_0B_BIT               0
#define CTRCLREFA_0B_WID               5

#define ADC_REG_0C                                      ((ADC_BASE_ADD+0x0C)<<2)
#define ENSTCB_BIT                     7
#define ENSTCB_WID                     1
#define CTRCLREFB_0C_BIT               0
#define CTRCLREFB_0C_WID               5

#define ADC_REG_0D                                      ((ADC_BASE_ADD+0x0D)<<2)
#define ENSTCC_BIT                     7
#define ENSTCC_WID                     1
#define CTRCLREFC_0D_BIT               0
#define CTRCLREFC_0D_WID               5

//#define ADC_REG_0E                                    ((ADC_BASE_ADD+0x0E)<<2)
#define ADC_REG_0F                                      ((ADC_BASE_ADD+0x0F)<<2)
#define ENMRCA_BIT                     7
#define ENMRCA_WID                     1
#define ENBRCA_BIT                     6
#define ENBRCA_WID                     1
#define ENMBCA_BIT                     4
#define ENMBCA_WID                     1
#define CTRCLREFA_0F_BIT               2
#define CTRCLREFA_0F_WID               2

#define ADC_REG_10                                      ((ADC_BASE_ADD+0x10)<<2)
#define ENMRCB_BIT                     7
#define ENMRCB_WID                     1
#define ENBRCB_BIT                     6
#define ENBRCB_WID                     1
#define ENMBCB_BIT                     4
#define ENMBCB_WID                     1
#define CTRCLREFB_10_BIT               2
#define CTRCLREFB_10_WID               2

#define ADC_REG_11                                      ((ADC_BASE_ADD+0x11)<<2)
#define ENMRCC_BIT                     7
#define ENMRCC_WID                     1
#define ENBRCC_BIT                     6
#define ENBRCC_WID                     1
#define ENMBCC_BIT                     4
#define ENMBCC_WID                     1
#define CTRCLREFC_11_BIT               2
#define CTRCLREFC_11_WID               2

//#define ADC_REG_12                                    ((ADC_BASE_ADD+0x12)<<2)
#define ADC_REG_13                                      ((ADC_BASE_ADD+0x13)<<2)
#define ENADCA_BIT                     5
#define ENADCA_WID                     1
#define ENADCASTDBY_BIT                4
#define ENADCASTDBY_WID                1
#define STARTADCCALA_BIT               2
#define STARTADCCALA_WID               1
#define OEA_BIT                        1
#define OEA_WID                        1
#define OFCENA_BIT                     0
#define OFCENA_WID                     1

#define ADC_REG_14                                      ((ADC_BASE_ADD+0x14)<<2)
#define ENADCB_BIT                     5
#define ENADCB_WID                     1
#define ENADCBSTDBY_BIT                4
#define ENADCBSTDBY_WID                1
#define STARTADCCALB_BIT               2
#define STARTADCCALB_WID               1
#define OEB_BIT                        1
#define OEB_WID                        1
#define OFCENB_BIT                     0
#define OFCENB_WID                     1

#define ADC_REG_15                                      ((ADC_BASE_ADD+0x15)<<2)
#define ENVSOUT2ADC_BIT                6
#define ENVSOUT2ADC_WID                1
#define ENADCC_BIT                     5
#define ENADCC_WID                     1
#define ENADCCSTDBY_BIT                4
#define ENADCCSTDBY_WID                1
#define STARTADCCALC_BIT               2
#define STARTADCCALC_WID               1
#define OEC_BIT                        1
#define OEC_WID                        1
#define OFCENC_BIT                     0
#define OFCENC_WID                     1

//#define ADC_REG_16                                    ((ADC_BASE_ADD+0x16)<<2)
#define ADC_REG_17                                      ((ADC_BASE_ADD+0x17)<<2)
#define INMUXA_BIT                     4
#define INMUXA_WID                     2
#define INMUXB_BIT                     0
#define INMUXB_WID                     3

#define ADC_REG_18                                      ((ADC_BASE_ADD+0x18)<<2)
#define INMUXC_BIT                     4
#define INMUXC_WID                     3

#define ADC_REG_19                                      ((ADC_BASE_ADD+0x19)<<2)
#define ENLPFA_BIT                     7
#define ENLPFA_WID                     1
#define ANABWCTRLA_BIT                 4
#define ANABWCTRLA_WID                 3
#define LPFBWCTRA_BIT                  0
#define LPFBWCTRA_WID                  4

#define ADC_REG_1A                                      ((ADC_BASE_ADD+0x1A)<<2)
#define ENLPFB_BIT                     7
#define ENLPFB_WID                     1
#define ANABWCTRLB_BIT                 4
#define ANABWCTRLB_WID                 3
#define LPFBWCTRB_BIT                  0
#define LPFBWCTRB_WID                  4

#define ADC_REG_1B                                      ((ADC_BASE_ADD+0x1B)<<2)
#define ENLPFC_BIT                     7
#define ENLPFC_WID                     1
#define ANABWCTRLC_BIT                 4
#define ANABWCTRLC_WID                 3
#define LPFBWCTRC_BIT                  0
#define LPFBWCTRC_WID                  4

//#define ADC_REG_1C                                    ((ADC_BASE_ADD+0x1C)<<2)
#define ADC_REG_1D                                    ((ADC_BASE_ADD+0x1D)<<2)
#define ADC_REG_1E                                      ((ADC_BASE_ADD+0x1E)<<2)
#define ENEXTBGAPV_BIT                 7
#define ENEXTBGAPV_WID                 1
#define ENIB_BIT                       5
#define ENIB_WID                       1
#define ENEXTBIAS_BIT                  4
#define ENEXTBIAS_WID                  1
#define IREFGENADJ_BIT                 0
#define IREFGENADJ_WID                 4

#define ADC_REG_1F                                      ((ADC_BASE_ADD+0x1F)<<2)
#define ENVBG_BIT                      3
#define ENVBG_WID                      1

#define ADC_REG_20                                      ((ADC_BASE_ADD+0x20)<<2)
#define ENSTCBUF_BIT                   6
#define ENSTCBUF_WID                   1
#define ENCVBSBUF_BIT                  5
#define ENCVBSBUF_WID                  1
#define CTRGAINCVBSBUF_BIT             4
#define CTRGAINCVBSBUF_WID             1
#define INMUXBUF_BIT                   0
#define INMUXBUF_WID                   2

#define ADC_REG_21                                      ((ADC_BASE_ADD+0x21)<<2)
#define SLEEPMODE_BIT                  3
#define SLEEPMODE_WID                  1
#define POWERDOWNZ_BIT                 2
#define POWERDOWNZ_WID                 1
#define RSTDIGZ_BIT                    1
#define RSTDIGZ_WID                    1
#define FULLPDZ_BIT                    0
#define FULLPDZ_WID                    1
#define ADC_REG_22                                      ((ADC_BASE_ADD+0x22)<<2)
#define OUTMODESEL_BIT                 6
#define OUTMODESEL_WID                 2

#define ADC_REG_23                                    ((ADC_BASE_ADD+0x23)<<2)
#define ADC_REG_24                                      ((ADC_BASE_ADD+0x24)<<2)
#define INMUXSOG_BIT                   0
#define INMUXSOG_WID                   3

//#define ADC_REG_25                                    ((ADC_BASE_ADD+0x25)<<2)
#define ADC_REG_26                                      ((ADC_BASE_ADD+0x26)<<2)
#define SOGSLCRTHRES_BIT               3
#define SOGSLCRTHRES_WID               5
#define SOGLPF_BIT                     0
#define SOGLPF_WID                     3

#define ADC_REG_27                                      ((ADC_BASE_ADD+0x27)<<2)
#define SOGSLCRTHRESAUX_BIT            3
#define SOGSLCRTHRESAUX_WID            5
#define SOGLPFAUX_BIT                  0
#define SOGLPFAUX_WID                  3

#define ADC_REG_28                                      ((ADC_BASE_ADD+0x28)<<2)
#define SOGBIASAUX_BIT                 6
#define SOGBIASAUX_WID                 2
#define SOGCLAMPAUX_BIT                4
#define SOGCLAMPAUX_WID                2
#define SOGBIAS_BIT                    2
#define SOGBIAS_WID                    2
#define SOGCLAMP_BIT                   0
#define SOGCLAMP_WID                   2

//#define ADC_REG_29                                    ((ADC_BASE_ADD+0x29)<<2)
#define ADC_REG_2A                                      ((ADC_BASE_ADD+0x2A)<<2)
#define SOGOFFSETAUX_BIT               4
#define SOGOFFSETAUX_WID               3
#define SOGOFFSET_BIT                  0
#define SOGOFFSET_WID                  3

#define ADC_REG_2B                                      ((ADC_BASE_ADD+0x2B)<<2)
#define SOGDETAUX_BIT                  0
#define SOGDETAUX_WID                  8

//#define ADC_REG_2C                                    ((ADC_BASE_ADD+0x2C)<<2)
//#define ADC_REG_2D                                    ((ADC_BASE_ADD+0x2D)<<2)
#define ADC_REG_2E                                      ((ADC_BASE_ADD+0x2E)<<2)
#define HSYNCPOLOVRD1_BIT              7
#define HSYNCPOLOVRD1_WID              1
#define HSYNCPOLSEL_BIT                6
#define HSYNCPOLSEL_WID                1
#define HSYNCOUTPOL_BIT                5
#define HSYNCOUTPOL_WID                1
#define HSYNCACTVOVRD_BIT              4
#define HSYNCACTVOVRD_WID              1
#define HSYNCACTVSEL_BIT               3
#define HSYNCACTVSEL_WID               1
#define VSYNCOUTPOL_BIT                2
#define VSYNCOUTPOL_WID                1
#define VSYNCACTVOVRD_BIT              1
#define VSYNCACTVOVRD_WID              1
#define VSYNCACTVSEL_BIT               0
#define VSYNCACTVSEL_WID               1

#define ADC_REG_2F                                      ((ADC_BASE_ADD+0x2F)<<2)
#define CLAMPEXT_BIT                   7
#define CLAMPEXT_WID                   1
#define CLAMPPOL_BIT                   6
#define CLAMPPOL_WID                   1
#define COASTSEL_BIT                   5
#define COASTSEL_WID                   1
#define COASTPOLOVRD_BIT               4
#define COASTPOLOVRD_WID               1
#define COASTPOLSEL_BIT                3
#define COASTPOLSEL_WID                1

#define ADC_REG_30                                      ((ADC_BASE_ADD+0x30)<<2)
#define HSYNCOUTWIDTH_BIT              0
#define HSYNCOUTWIDTH_WID              8

#define ADC_REG_31                                      ((ADC_BASE_ADD+0x31)<<2)
#define SYNCSEPTHRES_BIT               0
#define SYNCSEPTHRES_WID               8

#define ADC_REG_32                                      ((ADC_BASE_ADD+0x32)<<2)
#define PRECOAST_BIT                   0
#define PRECOAST_WID                   8

#define ADC_REG_33                                      ((ADC_BASE_ADD+0x33)<<2)
#define POSTCOAST_BIT                  0
#define POSTCOAST_WID                  8

#define ADC_REG_34                                      ((ADC_BASE_ADD+0x34)<<2)
#define HSYNCDET_BIT                   7
#define HSYNCDET_WID                   1
#define HSYNCACTV_BIT                  6
#define HSYNCACTV_WID                  1
#define HSYNCPOL_BIT                   5
#define HSYNCPOL_WID                   1
#define VSYNCDET_BIT                   4
#define VSYNCDET_WID                   1
#define VSYNCACTV_BIT                  3
#define VSYNCACTV_WID                  1
#define VSYNCPOL_BIT                   2
#define VSYNCPOL_WID                   1
#define SOGDET_BIT                     1
#define SOGDET_WID                     1
#define COASTPOL_BIT                   0
#define COASTPOL_WID                   1

#define ADC_REG_35                                      ((ADC_BASE_ADD+0x35)<<2)
#define PLLLOCKED_BIT                  0
#define PLLLOCKED_WID                  1

#define ADC_REG_36                                      ((ADC_BASE_ADD+0x36)<<2)
#define OSCBYPASS_BIT                  0
#define OSCBYPASS_WID                  1

//#define ADC_REG_37                                    ((ADC_BASE_ADD+0x37)<<2)
#define ADC_REG_38                                      ((ADC_BASE_ADD+0x38)<<2)
#define ENPLLCOASTWIN_BIT              5
#define ENPLLCOASTWIN_WID              1

#define ADC_REG_39                                      ((ADC_BASE_ADD+0x39)<<2)
#define ENCOASTFWIDTHSEL_BIT           6
#define ENCOASTFWIDTHSEL_WID           1
#define INSYNCMUXCTRL_BIT              2
#define INSYNCMUXCTRL_WID              1
#define SYNCMUXCTRLBYPASS_BIT          1
#define SYNCMUXCTRLBYPASS_WID          1
#define SYNCMUXCTRL_BIT                0
#define SYNCMUXCTRL_WID                1

#define ADC_REG_3A                                      ((ADC_BASE_ADD+0x3A)<<2)
#define ADCRDYA_BIT                    7
#define ADCRDYA_WID                    1
#define ADCRDYB_BIT                    6
#define ADCRDYB_WID                    1
#define ADCRDYC_BIT                    5
#define ADCRDYC_WID                    1

#define ADC_REG_3B                                      ((ADC_BASE_ADD+0x3B)<<2)
#define DISCLPDRGCST_BIT               4
#define DISCLPDRGCST_WID               1
#define HFSMRETRY_BIT                  2
#define HFSMRETRY_WID                  2

#define ADC_REG_3C                                      ((ADC_BASE_ADD+0x3C)<<2)
#define HSYNCFWIDTHSEL_BIT             4
#define HSYNCFWIDTHSEL_WID             4
#define COASTFWIDTHSEL_BIT             0
#define COASTFWIDTHSEL_WID             4

#define ADC_REG_3D                                      ((ADC_BASE_ADD+0x3D)<<2)
#define FILTPLLHSYNC_BIT               7
#define FILTPLLHSYNC_WID               1
#define HSYNCLOCKWINDOW_BIT            5
#define HSYNCLOCKWINDOW_WID            1

#define ADC_REG_3E                                      ((ADC_BASE_ADD+0x3E)<<2)
#define PREHSYNC_BIT                   0
#define PREHSYNC_WID                   8

#define ADC_REG_3F                                      ((ADC_BASE_ADD+0x3F)<<2)
#define POSTHSYNC_BIT                  0
#define POSTHSYNC_WID                  8

//#define ADC_REG_40                                    ((ADC_BASE_ADD+0x40)<<2)
#define ADC_REG_41                                      ((ADC_BASE_ADD+0x41)<<2)
#define GLITCHSEL_BIT                  0
#define GLITCHSEL_WID                  3

//#define ADC_REG_42                                    ((ADC_BASE_ADD+0x42)<<2)
#define ADC_REG_43                                      ((ADC_BASE_ADD+0x43)<<2)
#define GLITCHBYPASS_BIT               4
#define GLITCHBYPASS_WID               3

//#define ADC_REG_44                                    ((ADC_BASE_ADD+0x44)<<2)
//#define ADC_REG_45                                    ((ADC_BASE_ADD+0x45)<<2)
#define ADC_REG_46                                      ((ADC_BASE_ADD+0x46)<<2)
#define VSYNCLOCKWINDOW_BIT            2
#define VSYNCLOCKWINDOW_WID            1

#define ADC_REG_47                                      ((ADC_BASE_ADD+0x47)<<2)
#define HSOUTDLYCTR_BIT                4
#define HSOUTDLYCTR_WID                3

//#define ADC_REG_48                                    ((ADC_BASE_ADD+0x48)<<2)
//#define ADC_REG_49                                    ((ADC_BASE_ADD+0x49)<<2)
//#define ADC_REG_4A                                    ((ADC_BASE_ADD+0x4A)<<2)
//#define ADC_REG_4B                                    ((ADC_BASE_ADD+0x4B)<<2)
//#define ADC_REG_4C                                    ((ADC_BASE_ADD+0x4C)<<2)
//#define ADC_REG_4D                                    ((ADC_BASE_ADD+0x4D)<<2)
//#define ADC_REG_4E                                    ((ADC_BASE_ADD+0x4E)<<2)
//#define ADC_REG_4F                                    ((ADC_BASE_ADD+0x4F)<<2)
//#define ADC_REG_50                                    ((ADC_BASE_ADD+0x50)<<2)
//#define ADC_REG_51                                    ((ADC_BASE_ADD+0x51)<<2)
//#define ADC_REG_52                                    ((ADC_BASE_ADD+0x52)<<2)
//#define ADC_REG_53                                    ((ADC_BASE_ADD+0x53)<<2)
//#define ADC_REG_54                                    ((ADC_BASE_ADD+0x54)<<2)
//#define ADC_REG_55                                    ((ADC_BASE_ADD+0x55)<<2)
#define ADC_REG_56                                      ((ADC_BASE_ADD+0x56)<<2)
#define CLKPHASEADJ_BIT                0
#define CLKPHASEADJ_WID                5

//#define ADC_REG_57                                    ((ADC_BASE_ADD+0x57)<<2)
#define ADC_REG_58                                      ((ADC_BASE_ADD+0x58)<<2)
#define EXTCLKSEL_BIT                  3
#define EXTCLKSEL_WID                  1
#define PLLTESTMODE_BIT                0
#define PLLTESTMODE_WID                2

#define ADC_REG_59                                      ((ADC_BASE_ADD+0x59)<<2)
#define PLLALFA_BIT                    0
#define PLLALFA_WID                    5

#define ADC_REG_5A                                      ((ADC_BASE_ADD+0x5A)<<2)
#define PLLSEL_BIT                     5
#define PLLSEL_WID                     1
#define PLLBETA_BIT                    0
#define PLLBETA_WID                    5

#define ADC_REG_5B                                      ((ADC_BASE_ADD+0x5B)<<2)
#define PLLARMENA_BIT                  7
#define PLLARMENA_WID                  1
#define PLLARMCNT_BIT                  5
#define PLLARMCNT_WID                  2
#define PLLENCTR_BIT                   0
#define PLLENCTR_WID                   5

#define ADC_REG_5C                                      ((ADC_BASE_ADD+0x5C)<<2)
#define PLLFLOCKBW_BIT                 4
#define PLLFLOCKBW_WID                 4
#define ENAPLL_BIT                     3
#define ENAPLL_WID                     1
#define PLLFLOCKEN_BIT                 2
#define PLLFLOCKEN_WID                 1
#define PLLFLOCKCNT_BIT                0
#define PLLFLOCKCNT_WID                2

#define ADC_REG_5D                                      ((ADC_BASE_ADD+0x5D)<<2)
#define PLLGAIN_BIT                    4
#define PLLGAIN_WID                    4
#define PLLLOCKCNTTH_BIT               0
#define PLLLOCKCNTTH_WID               4

#define ADC_REG_5E                                      ((ADC_BASE_ADD+0x5E)<<2)
#define PLLLOCKTH_BIT                  0
#define PLLLOCKTH_WID                  7

#define ADC_REG_5F                                      ((ADC_BASE_ADD+0x5F)<<2)
#define PLLSDDIV_BIT                   0
#define PLLSDDIV_WID                   7

#define ADC_REG_60                                      ((ADC_BASE_ADD+0x60)<<2)
#define PLLSDRANGE_BIT                 4
#define PLLSDRANGE_WID                 4
#define PLLUNLOCKCNTTH_BIT             0
#define PLLUNLOCKCNTTH_WID             4

#define ADC_REG_61                                      ((ADC_BASE_ADD+0x61)<<2)
#define PLLRANGEEXT_BIT                4
#define PLLRANGEEXT_WID                4
#define PLLRNGNO_BIT                   0
#define PLLRNGNO_WID                   4

#define ADC_REG_62                                      ((ADC_BASE_ADD+0x62)<<2)
#define PLLUNLOCKTH_BIT                0
#define PLLUNLOCKTH_WID                7

#define ADC_REG_63                                      ((ADC_BASE_ADD+0x63)<<2)
#define PLLRANGEOVR_BIT                7
#define PLLRANGEOVR_WID                1
#define PLLLPFOUTOVR_BIT               6
#define PLLLPFOUTOVR_WID               1
#define PLLVCOOUTDIVCTR_BIT            4
#define PLLVCOOUTDIVCTR_WID            2
#define PLLT2DSENS_BIT                 2
#define PLLT2DSENS_WID                 2
#define PLLIOFFSET_BIT                 0
#define PLLIOFFSET_WID                 2

#define ADC_REG_64                                      ((ADC_BASE_ADD+0x64)<<2)
#define PLLLPFOUTEXT_MSB_BIT           0
#define PLLLPFOUTEXT_MSB_WID           8

#define ADC_REG_65                                      ((ADC_BASE_ADD+0x65)<<2)
#define PLLLPFOUTEXT_LSB_BIT           0
#define PLLLPFOUTEXT_LSB_WID           4

#define ADC_REG_66                                      ((ADC_BASE_ADD+0x66)<<2)
#define ADC_REG_PLLSDENA_BIT           4
#define ADC_REG_PLLSDENA_WID           1

//#define ADC_REG_67                                    ((ADC_BASE_ADD+0x67)<<2)
#define ADC_REG_68                                      ((ADC_BASE_ADD+0x68)<<2)
#define ENDPLL_BIT                     5
#define ENDPLL_WID                     1
#define PLLCMASKENZ_BIT                4
#define PLLCMASKENZ_WID                1
#define PLLCMASKCTR_BIT                3
#define PLLCMASKCTR_WID                1
#define ENPLLCOAST_BIT                 2
#define ENPLLCOAST_WID                 1
#define VCORANGESEL_BIT                0
#define VCORANGESEL_WID                2

#define ADC_REG_69                                      ((ADC_BASE_ADD+0x69)<<2)
#define CHARGEPUMPCURR_BIT             4
#define CHARGEPUMPCURR_WID             3
#define BIASCP_BIT                     2
#define BIASCP_WID                     2
#define LDCOUNTER_BIT                  0
#define LDCOUNTER_WID                  2

//#define ADC_REG_6A                                    (ADC_BASE_ADD+0x6A)<<2)
//#define ADC_REG_6B                                    (ADC_BASE_ADD+0x6B)<<2)
//#define ADC_REG_6C                                    (ADC_BASE_ADD+0x6C)<<2)
//#define ADC_REG_6D                                    (ADC_BASE_ADD+0x6D)<<2)

// **************************************************** ************************
// ******** TOP REGISTERS ********
// **************************************************** ************************

#define TOP_BASE_ADD                                    0x3300//0x1B00

// ******** DVSS -- YPBPR & RGB *********************** ************************

#define TVFE_DVSS_MUXCTRL                               ((TOP_BASE_ADD+0x00)<<2)
#define DVSS_CLAMP_INV_BIT              31
#define DVSS_CLAMP_INV_WID              1
#define DVSS_COAST_INV_BIT              30
#define DVSS_COAST_INV_WID              1
#define CLAMP_V_EN_BIT                  29
#define CLAMP_V_EN_WID                  1
#define DVSS_XSYNC_GEN_EN_BIT           28
#define DVSS_XSYNC_GEN_EN_WID           1
#define VS_MDET_SEL_BIT                 27
#define VS_MDET_SEL_WID                 1
#define DVSS_COAST_EN_BIT               25
#define DVSS_COAST_EN_WID               2
#define DVSS_COAST_EDGE_TEST_BIT        24
#define DVSS_COAST_EDGE_TEST_WID        1
#define DVSS_VS_REFINE_TEST_BIT         23
#define DVSS_VS_REFINE_TEST_WID         1
#define DVSS_VS_OUT_SEL_BIT             21
#define DVSS_VS_OUT_SEL_WID             2
#define DVSS_HS_COAST_SEL_BIT           19
#define DVSS_HS_COAST_SEL_WID           2
#define DVSS_HS_OUT_SEL_BIT             17
#define DVSS_HS_OUT_SEL_WID             2
#define DVSS_TO_GEN_VS_SEL_BIT          16
#define DVSS_TO_GEN_VS_SEL_WID          1
#define DVSS_TO_GEN_HS_SEL_BIT          15
#define DVSS_TO_GEN_HS_SEL_WID          1
#define DVSS_TO_NOSIG_SEL_BIT           14
#define DVSS_TO_NOSIG_SEL_WID           1
#define DVSS_TO_GATE_VS_SEL_BIT         12
#define DVSS_TO_GATE_VS_SEL_WID         2
#define DVSS_TO_GATE_HS_SEL_BIT         10
#define DVSS_TO_GATE_HS_SEL_WID         2
#define DVSS_FROM_LLPLL_INV_BIT         9
#define DVSS_FROM_LLPLL_INV_WID         1
#define DVSS_TO_LLPLL_INV_BIT           8
#define DVSS_TO_LLPLL_INV_WID           1
#define DVSS_TO_LLLPLL_SEL_BIT          6
#define DVSS_TO_LLLPLL_SEL_WID          2
#define DVSS_RM_GLITCH_BIT              4
#define DVSS_RM_GLITCH_WID              2
#define DVSS_HS_IN_INV_BIT              3
#define DVSS_HS_IN_INV_WID              1
#define DVSS_VS_IN_INV_BIT              2
#define DVSS_VS_IN_INV_WID              1
#define DVSS_XSYNCH_IN_SEL_BIT          0
#define DVSS_XSYNCH_IN_SEL_WID          2

#define TVFE_DVSS_MUXVS_REF                             ((TOP_BASE_ADD+0x01)<<2)
#define DVSS_REFINE_TH_BIT              0
#define DVSS_REFINE_TH_WID              16

#define TVFE_DVSS_MUXCOAST_V                            ((TOP_BASE_ADD+0x02)<<2)
#define MUXCOAST_VER_ED_BIT             16
#define MUXCOAST_VER_ED_WID             16
#define MUXCOAST_VER_ST_BIT             0
#define MUXCOAST_VER_ST_WID             16

#define TVFE_DVSS_SEP_HVWIDTH                           ((TOP_BASE_ADD+0x03)<<2)
#define VS_WIDTH_BIT                    16
#define VS_WIDTH_WID                    16
#define HS_WIDTH_BIT                    0
#define HS_WIDTH_WID                    16

#define TVFE_DVSS_SEP_HPARA                             ((TOP_BASE_ADD+0x04)<<2)
#define HS_MSK_BIT                      16
#define HS_MSK_WID                      16
#define HS_GEN_PRD_BIT                  16
#define HS_GEN_PRD_WID                  0

#define TVFE_DVSS_SEP_VINTEG                            ((TOP_BASE_ADD+0x05)<<2)
#define VS_INTEGRAL_TH2_BIT             16
#define VS_INTEGRAL_TH2_WID             16
#define VS_INTEGRAL_TH1_BIT             0
#define VS_INTEGRAL_TH1_WID             16

#define TVFE_DVSS_SEP_H_THR                             ((TOP_BASE_ADD+0x06)<<2)
#define DVSS_HS_WTH2_BIT                16
#define DVSS_HS_WTH2_WID                16
#define DVSS_HS_WTH1_BIT                0
#define DVSS_HS_WTH1_WID                16

#define TVFE_DVSS_SEP_CTRL                              ((TOP_BASE_ADD+0x07)<<2)
#define DVSS_VS_TEST_BIT                29
#define DVSS_VS_TEST_WID                3
#define DVSS_HD_BIT                     28
#define DVSS_HD_WID                     1
#define DVSS_SEP_RESERVED_BIT           0
#define DVSS_SEP_RESERVED_WID           16

#define TVFE_DVSS_GEN_WIDTH                             ((TOP_BASE_ADD+0x08)<<2)
#define DVSS_GEN_VS_WIDTH_BIT           16
#define DVSS_GEN_VS_WIDTH_WID           16
#define DVSS_GEN_HS_WIDTH_BIT           0
#define DVSS_GEN_HS_WIDTH_WID           16

#define TVFE_DVSS_GEN_PRD                                  ((TOP_BASE_ADD+0x09)<<2)
#define VS_GEN_PRD_BIT                  16
#define VS_GEN_PRD_WID                  16
#define HSYNC_GEN_PRD_BIT               0
#define HSYNC_GEN_PRD_WID               16

#define TVFE_DVSS_GEN_COAST                             ((TOP_BASE_ADD+0x0A)<<2)
#define HSGEN_COAST_ST_BIT              16
#define HSGEN_COAST_ST_WID              16
#define HSGEN_COAST_ED_BIT              0
#define HSGEN_COAST_ED_WID              16

#define TVFE_DVSS_NOSIG_PARA                            ((TOP_BASE_ADD+0x0B)<<2)
#define DVSS_HS_WIDTH_NOSIG_BIT         0
#define DVSS_HS_WIDTH_NOSIG_WID         10

#define TVFE_DVSS_NOSIG_PLS_TH                          ((TOP_BASE_ADD+0x0C)<<2)
#define DVSS_READ_EN_BIT                31
#define DVSS_READ_EN_WID                1
#define INTEGRAL_STEP_NOSIG_BIT         28
#define INTEGRAL_STEP_NOSIG_WID         2
#define DVSS_PLS_CNT_TH2_BIT            16
#define DVSS_PLS_CNT_TH2_WID            12
#define DVSS_PLS_CNT_TH1_BIT            0
#define DVSS_PLS_CNT_TH1_WID            12

#define TVFE_DVSS_GATE_H                                ((TOP_BASE_ADD+0x0D)<<2)
#define DVSS_BACKP_H_ED_BIT             16
#define DVSS_BACKP_H_ED_WID             16
#define DVSS_BACKP_H_ST_BIT             0
#define DVSS_BACKP_H_ST_WID             16

#define TVFE_DVSS_GATE_V                                ((TOP_BASE_ADD+0x0E)<<2)
#define DVSS_CLAMP_V_ED_BIT             16
#define DVSS_CLAMP_V_ED_WID             16
#define DVSS_CLAMP_V_ST_BIT             0
#define DVSS_CLAMP_V_ST_WID             16

#define TVFE_DVSS_INDICATOR1                            ((TOP_BASE_ADD+0x0F)<<2)
#define MVDET_BIT                       14
#define MVDET_WID                       1
#define HI_BIT                          13
#define HI_WID                          1
#define NOSIG_BIT                       12
#define NOSIG_WID                       1
#define PLS_NUM_BIT                     0
#define PLS_NUM_WID                     12

#define TVFE_DVSS_INDICATOR2                            ((TOP_BASE_ADD+0x10)<<2)
#define DVSS_HITMASK_CNT_BIT            16
#define DVSS_HITMASK_CNT_WID            16
#define DVSS_CLK_CNT_BIT                0
#define DVSS_CLK_CNT_WID                16

// ********Freerun syn gen ****************************************************
#define TVFE_FREERUN_GEN_WIDTH                       ((TOP_BASE_ADD+0x11)<<2)
#define FREERUN_VSYNC_WIDTH_BIT                 16
#define FREERUN_VSYNC_WIDTH_WID           16
#define FREERUN_HSYNC_WIDTH_BIT                0
#define FREERUN_HSYNC_WIDTH_WID          16

#define TVFE_FREERUN_GEN_PRD                           ((TOP_BASE_ADD+0x12)<<2)
#define FREERUN_VSYNC_PRD_BIT                 16
#define FREERUN_VSYNC_PRD_WID           16
#define FREERUN_HSYNC_PRD_BIT                 0
#define FREERUN_HSYNC_PRD_WID           16    

#define TVFE_FREERUN_GEN_COAST                       ((TOP_BASE_ADD+0x13)<<2)
#define FREERUN_VSYNC_ST_BIT                 16
#define FREERUN_VSYNC_ST_WID           16
#define FREERUN_HSYNC_ST_BIT                 0
#define FREERUN_HSYNC_ST_WID           16  

#define TVFE_FREERUN_GEN_CTRL                          ((TOP_BASE_ADD+0x14)<<2)
#define FREERUN_CTRL_ENABLE_BIT               0
#define FREERUN_CTRL_ENABLE_WID         1


// ******** MV DET -- YPBPR *************************** **********************

#define TVFE_DVSS_MVDET_CTRL1                           ((TOP_BASE_ADD+0x15)<<2)
#define MV_HS_RISING_END_BIT            16
#define MV_HS_RISING_END_WID            16
#define MV_HS_RISING_START_BIT          0
#define MV_HS_RISING_START_WID          16

#define TVFE_DVSS_MVDET_CTRL2                           ((TOP_BASE_ADD+0x16)<<2)
#define MV_VEND_BIT                     16
#define MV_VEND_WID                     16
#define MV_VSTART_BIT                   0
#define MV_VSTART_WID                   16

#define TVFE_DVSS_MVDET_CTRL3                           ((TOP_BASE_ADD+0x17)<<2)
#define MV_AVG_VEND_BIT                 16
#define MV_AVG_VEND_WID                 16
#define MV_AVG_VSTART_BIT               0
#define MV_AVG_VSTART_WID               16

#define TVFE_DVSS_MVDET_CTRL4                           ((TOP_BASE_ADD+0x18)<<2)
#define COAST_VER_ST_BIT                16
#define COAST_VER_ST_WID                16
#define COAST_VER_ED_BIT                0
#define COAST_VER_ED_WID                16

#define TVFE_DVSS_MVDET_CTRL5                           ((TOP_BASE_ADD+0x19)<<2)
#define HS_GATE_MSK_BIT                 16
#define HS_GATE_MSK_WID                 16
#define HS_GATE_WIDTH_BIT               0
#define HS_GATE_WIDTH_WID               16

#define TVFE_DVSS_MVDET_CTRL6                           ((TOP_BASE_ADD+0x1A)<<2)
#define DVSS_HS_GATE_GEN_PRD_BIT        16
#define DVSS_HS_GATE_GEN_PRD_WID        16
#define DVSS_HS_RISING_END_BIT          0
#define DVSS_HS_RISING_END_WID          16

#define TVFE_DVSS_MVDET_CTRL7                           ((TOP_BASE_ADD+0x1B)<<2)
#define VBI_SEL_BIT                     17
#define VBI_SEL_WID                     1
#define RST_BIT                         16
#define RST_WID                         1
#define VCENTER_BIT                     0
#define VCENTER_WID                     16

// ******** AUTO MODE AND AUTO POLARITY -- YPBP R & RGB  ***********************

#define TVFE_SYNCTOP_SPOL_MUXCTRL                       ((TOP_BASE_ADD+0x20)<<2)
#define SPOL_D_COMP_SYNCIN_BIT          26
#define SPOL_D_COMP_SYNCIN_WID          1
#define SMUX_SRC_SEL_BIT                23
#define SMUX_SRC_SEL_WID                3
#define SPOL_AUTOMODE_LN_POS_BIT        12
#define SPOL_AUTOMODE_LN_POS_WID        11
#define SPOL_AUTOMODE_LN_TH_BIT         4
#define SPOL_AUTOMODE_LN_TH_WID         8
#define SPOL_AUTOMODE_EN_BIT            3
#define SPOL_AUTOMODE_EN_WID            1
#define SPOL_MANNUAL_INV_VS_BIT         2
#define SPOL_MANNUAL_INV_VS_WID         1
#define SPOL_MANNUAL_INV_HS_BIT         1
#define SPOL_MANNUAL_INV_HS_WID         1
#define SPOL_AUTO_POL_BIT               0
#define SPOL_AUTO_POL_WID               1

#define TVFE_SYNCTOP_INDICATOR1_HCNT                    ((TOP_BASE_ADD+0x21)<<2)
#define SPOL_HCNT_NEG_BIT               16
#define SPOL_HCNT_NEG_WID               16
#define SPOL_HCNT_POS_BIT               0
#define SPOL_HCNT_POS_WID               16

#define TVFE_SYNCTOP_INDICATOR2_VCNT                    ((TOP_BASE_ADD+0x22)<<2)
#define SPOL_VCNT_NEG_BIT               16
#define SPOL_VCNT_NEG_WID               16
#define SPOL_VCNT_POS_BIT               0
#define SPOL_VCNT_POS_WID               16

#define TVFE_SYNCTOP_INDICATOR3                         ((TOP_BASE_ADD+0x23)<<2)
#define SFG_PROGRESSIVE_BIT             2
#define SFG_PROGRESSIVE_WID             1
#define SPOL_V_POL_BIT                  1
#define SPOL_V_POL_WID                  1
#define SPOL_H_POL_BIT                  0
#define SPOL_H_POL_WID                  1

// ******** FIELD GEN -- YPBPR & RGB ********** ******* ************************

#define TVFE_SYNCTOP_SFG_MUXCTRL1                       ((TOP_BASE_ADD+0x24)<<2)
#define SFG_VS_WIDTH_BIT                28
#define SFG_VS_WIDTH_WID                8
#define SFG_VFILTER_EN_BIT              27
#define SFG_VFILTER_EN_WID              1
#define SFG_FLD_MANUAL_INV_BIT          26
#define SFG_FLD_MANUAL_INV_WID          1
#define SFG_FLD_AUTO_INV_BIT            25
#define SFG_FLD_AUTO_INV_WID            1
#define SFG_DET_EN_BIT                  24
#define SFG_DET_EN_WID                  1
#define SFG_DET_HEND_BIT                12
#define SFG_DET_HEND_WID                12
#define SFG_DET_HSTART_BIT              0
#define SFG_DET_HSTART_WID              12

#define TVFE_SYNCTOP_SFG_MUXCTRL2                       ((TOP_BASE_ADD+0x25)<<2)
#define SFG_MANUAL_INV_VS_BIT           26
#define SFG_MANUAL_INV_VS_WID           1
#define SFG_MANUAL_INV_HS_BIT           25
#define SFG_MANUAL_INV_HS_WID           1
#define SFG_AUTO_POL_BIT                24
#define SFG_AUTO_POL_WID                1
#define SMUX_SP_VS_SRC_SEL_BIT          20
#define SMUX_SP_VS_SRC_SEL_WID          8
#define SMUX_SP_HS_SRC_SEL_BIT          16
#define SMUX_SP_HS_SRC_SEL_WID          4
#define SMUX_SM_VS_SRC_SEL_BIT          12
#define SMUX_SM_VS_SRC_SEL_WID          4
#define SMUX_SM_HS_SRC_SEL_BIT          8
#define SMUX_SM_HS_SRC_SEL_WID          4
#define SFG_VSIN_INV_BIT                7
#define SFG_VSIN_INV_WID                1
#define SFG_VSIN_SEL_BIT                4
#define SFG_VSIN_SEL_WID                3
#define SFG_HSIN_INV_BIT                3
#define SFG_HSIN_INV_WID                1
#define SFG_HSIN_SEL_BIT                0
#define SFG_HSIN_SEL_WID                3

// ******** AUTO MODE -- YPBPR & RGB ********** ******* ************************

#define TVFE_SYNCTOP_INDICATOR4                         ((TOP_BASE_ADD+0x26)<<2)
#define SAM_VCNT_BIT                    16
#define SAM_VCNT_WID                    16
#define SAM_HCNT_BIT                    0
#define SAM_HCNT_WID                    16

#define TVFE_SYNCTOP_SAM_MUXCTRL                        ((TOP_BASE_ADD+0x27)<<2)
#define CSYNC_SEL_BIT                   20
#define CSYNC_SEL_WID                   2
#define SAM_AUTOMODE_EN_BIT             19
#define SAM_AUTOMODE_EN_WID             1
#define SAM_AUTOMODE_LN_POS_BIT         8
#define SAM_AUTOMODE_LN_POS_WID         11
#define SAM_AUTOMODE_SIG_WIDTH_BIT      0
#define SAM_AUTOMODE_SIG_WIDTH_WID      8

// ******** WSS -- YPBPR ********************** ******* ************************

#define TVFE_MISC_WSS1_MUXCTRL1                         ((TOP_BASE_ADD+0x2A)<<2)
#define WSS1_DATA_START_BIT             11
#define WSS1_DATA_START_WID             11
#define WSS1_LN_POS_BIT                 0
#define WSS1_LN_POS_WID                 11

#define TVFE_MISC_WSS1_MUXCTRL2                         ((TOP_BASE_ADD+0x2B)<<2)
#define WSS1_TH_BIT                     19
#define WSS1_TH_WID                     8
#define WSS1_STEP_BIT                   11
#define WSS1_STEP_WID                   8
#define WSS1_DATA_END_BIT               0
#define WSS1_DATA_END_WID               11

#define TVFE_MISC_WSS2_MUXCTRL1                         ((TOP_BASE_ADD+0x2C)<<2)
#define WSS2_DATA_START_BIT             11
#define WSS2_DATA_START_WID             11
#define WSS2_LN_POS_BIT                 0
#define WSS2_LN_POS_WID                 11

#define TVFE_MISC_WSS2_MUXCTRL2                         ((TOP_BASE_ADD+0x2D)<<2)
#define WSS2_TH_BIT                     19
#define WSS2_TH_WID                     8
#define WSS2_STEP_BIT                   11
#define WSS2_STEP_WID                   8
#define WSS2_DATA_END_BIT               0
#define WSS2_DATA_END_WID               11

#define TVFE_MISC_WSS1_INDICATOR1                       ((TOP_BASE_ADD+0x2E)<<2)
#define WSS1_DATA_31_0_BIT              0
#define WSS1_DATA_31_0_WID              32

#define TVFE_MISC_WSS1_INDICATOR2                       ((TOP_BASE_ADD+0x2F)<<2)
#define WSS1_DATA_63_32_BIT             0
#define WSS1_DATA_63_32_WID             32

#define TVFE_MISC_WSS1_INDICATOR3                       ((TOP_BASE_ADD+0x30)<<2)
#define WSS1_DATA_95_64_BIT             0
#define WSS1_DATA_95_64_WID             32

#define TVFE_MISC_WSS1_INDICATOR4                       ((TOP_BASE_ADD+0x31)<<2)
#define WSS1_DATA_127_96_BIT            0
#define WSS1_DATA_127_96_WID            32

#define TVFE_MISC_WSS1_INDICATOR5                       ((TOP_BASE_ADD+0x32)<<2)
#define WSS1_DATA_143_128_BIT           0
#define WSS1_DATA_143_128_WID           16

#define TVFE_MISC_WSS2_INDICATOR1                       ((TOP_BASE_ADD+0x33)<<2)
#define WSS2_DATA_31_0_BIT              0
#define WSS2_DATA_31_0_WID              32

#define TVFE_MISC_WSS2_INDICATOR2                       ((TOP_BASE_ADD+0x34)<<2)
#define WSS2_DATA_63_32_BIT             0
#define WSS2_DATA_63_32_WID             32

#define TVFE_MISC_WSS2_INDICATOR3                       ((TOP_BASE_ADD+0x35)<<2)
#define WSS2_DATA_95_64_BIT             0
#define WSS2_DATA_95_64_WID             32

#define TVFE_MISC_WSS2_INDICATOR4                       ((TOP_BASE_ADD+0x36)<<2)
#define WSS2_DATA_127_96_BIT            0
#define WSS2_DATA_127_96_WID            32

#define TVFE_MISC_WSS2_INDICATOR5                       ((TOP_BASE_ADD+0x37)<<2)
#define WSS2_DATA_143_128_BIT           0
#define WSS2_DATA_143_128_WID           16

// ******** AUTO PHASE AND BORDER DETECTION --  RGB *** ************************

#define TVFE_AP_MUXCTRL1                                ((TOP_BASE_ADD+0x39)<<2)
#define AP_DIFFMAX_2ND_BIT              29
#define AP_DIFFMAX_2ND_WID              1
#define BD_DET_METHOD_BIT               28
#define BD_DET_METHOD_WID               1
#define BD_DET_EN_BIT                   27
#define BD_DET_EN_WID                   1
#define AP_SPECIFIC_POINT_OUT_BIT       26
#define AP_SPECIFIC_POINT_OUT_WID       1
#define AP_DIFF_SEL_BIT                 25
#define AP_DIFF_SEL_WID                 1
#define AUTOPHASE_EN_BIT                24
#define AUTOPHASE_EN_WID                1
#define AP_HEND_BIT                     12
#define AP_HEND_WID                     12
#define AP_HSTART_BIT                   0
#define AP_HSTART_WID                   12

#define TVFE_AP_MUXCTRL2                                ((TOP_BASE_ADD+0x3A)<<2)
#define AP_VEND_BIT                     12
#define AP_VEND_WID                     12
#define AP_VSTART_BIT                   0
#define AP_VSTART_WID                   12

#define TVFE_AP_MUXCTRL3                                ((TOP_BASE_ADD+0x3B)<<2)
#define BD_R_TH_BIT                     22
#define BD_R_TH_WID                     10
#define AP_SPECIFIC_MAX_HPOS_BIT        10
#define AP_SPECIFIC_MAX_HPOS_WID        12
#define AP_CORING_TH_BIT                0
#define AP_CORING_TH_WID                10

#define TVFE_AP_MUXCTRL4                                ((TOP_BASE_ADD+0x3C)<<2)
#define AP_SPECIFIC_MIN_HPOS_BIT        12
#define AP_SPECIFIC_MIN_HPOS_WID        12
#define AP_SPECIFIC_MAX_VPOS_BIT        0
#define AP_SPECIFIC_MAX_VPOS_WID        12

#define TVFE_AP_MUXCTRL5                                ((TOP_BASE_ADD+0x3D)<<2)
#define BD_B_TH_BIT                     22
#define BD_B_TH_WID                     10
#define BD_G_TH_BIT                     12
#define BD_G_TH_WID                     10
#define AP_SPECIFIC_MIN_VPOS_BIT        0
#define AP_SPECIFIC_MIN_VPOS_WID        12

#define TVFE_AP_INDICATOR1                              ((TOP_BASE_ADD+0x3E)<<2)
#define AP_R_SUM_W_BIT                  0
#define AP_R_SUM_W_WID                  32

#define TVFE_AP_INDICATOR2                              ((TOP_BASE_ADD+0x3F)<<2)
#define AP_G_SUM_W_BIT                  0
#define AP_G_SUM_W_WID                  32

#define TVFE_AP_INDICATOR3                              ((TOP_BASE_ADD+0x40)<<2)
#define AP_B_SUM_W_BIT                  0
#define AP_B_SUM_W_WID                  32

#define TVFE_AP_INDICATOR4                              ((TOP_BASE_ADD+0x41)<<2)
#define AP_R_MIN_BIT                    12
#define AP_R_MIN_WID                    11
#define AP_R_MAX_BIT                    0
#define AP_R_MAX_WID                    11

#define TVFE_AP_INDICATOR5                              ((TOP_BASE_ADD+0x42)<<2)
#define AP_G_MIN_BIT                    12
#define AP_G_MIN_WID                    11
#define AP_G_MAX_BIT                    0
#define AP_G_MAX_WID                    11

#define TVFE_AP_INDICATOR6                              ((TOP_BASE_ADD+0x43)<<2)
#define AP_B_MIN_BIT                    12
#define AP_B_MIN_WID                    11
#define AP_B_MAX_BIT                    0
#define AP_B_MAX_WID                    11

#define TVFE_AP_INDICATOR7                              ((TOP_BASE_ADD+0x44)<<2)
#define AP_R_MAX_HCNT_BIT               12
#define AP_R_MAX_HCNT_WID               12
#define AP_R_MAX_VCNT_BIT               0
#define AP_R_MAX_VCNT_WID               12

#define TVFE_AP_INDICATOR8                              ((TOP_BASE_ADD+0x45)<<2)
#define AP_G_MAX_HCNT_BIT               12
#define AP_G_MAX_HCNT_WID               12
#define AP_G_MAX_VCNT_BIT               0
#define AP_G_MAX_VCNT_WID               12

#define TVFE_AP_INDICATOR9                              ((TOP_BASE_ADD+0x46)<<2)
#define AP_B_MAX_HCNT_BIT               12
#define AP_B_MAX_HCNT_WID               12
#define AP_B_MAX_VCNT_BIT               0
#define AP_B_MAX_VCNT_WID               12

#define TVFE_AP_INDICATOR10                             ((TOP_BASE_ADD+0x47)<<2)
#define AP_R_MIN_HCNT_BIT               12
#define AP_R_MIN_HCNT_WID               12
#define AP_R_MIN_VCNT_BIT               0
#define AP_R_MIN_VCNT_WID               12

#define TVFE_AP_INDICATOR11                             ((TOP_BASE_ADD+0x48)<<2)
#define AP_G_MIN_HCNT_BIT               12
#define AP_G_MIN_HCNT_WID               12
#define AP_G_MIN_VCNT_BIT               0
#define AP_G_MIN_VCNT_WID               12

#define TVFE_AP_INDICATOR12                             ((TOP_BASE_ADD+0x49)<<2)
#define AP_B_MIN_HCNT_BIT               12
#define AP_B_MIN_HCNT_WID               12
#define AP_B_MIN_VCNT_BIT               0
#define AP_B_MIN_VCNT_WID               12

#define TVFE_AP_INDICATOR13                             ((TOP_BASE_ADD+0x4A)<<2)
#define BD_R_BOT_VCNT_BIT               12
#define BD_R_BOT_VCNT_WID               12
#define BD_R_TOP_VCNT_BIT               0
#define BD_R_TOP_VCNT_WID               12

#define TVFE_AP_INDICATOR14                             ((TOP_BASE_ADD+0x4B)<<2)
#define BD_R_RIGHT_HCNT_BIT             12
#define BD_R_RIGHT_HCNT_WID             12
#define BD_R_LEFT_HCNT_BIT              0
#define BD_R_LEFT_HCNT_WID              12

#define TVFE_AP_INDICATOR15                             ((TOP_BASE_ADD+0x4C)<<2)
#define BD_G_BOT_VCNT_BIT               12
#define BD_G_BOT_VCNT_WID               12
#define BD_G_TOP_VCNT_BIT               0
#define BD_G_TOP_VCNT_WID               12

#define TVFE_AP_INDICATOR16                             ((TOP_BASE_ADD+0x4D)<<2)
#define BD_G_RIGHT_HCNT_BIT             12
#define BD_G_RIGHT_HCNT_WID             12
#define BD_G_LEFT_HCNT_BIT              0
#define BD_G_LEFT_HCNT_WID              12

#define TVFE_AP_INDICATOR17                             ((TOP_BASE_ADD+0x4E)<<2)
#define BD_B_BOT_VCNT_BIT               12
#define BD_B_BOT_VCNT_WID               12
#define BD_B_TOP_VCNT_BIT               0
#define BD_B_TOP_VCNT_WID               12

#define TVFE_AP_INDICATOR18                             ((TOP_BASE_ADD+0x4F)<<2)
#define BD_B_RIGHT_HCNT_BIT             12
#define BD_B_RIGHT_HCNT_WID             12
#define BD_B_LEFT_HCNT_BIT              0
#define BD_B_LEFT_HCNT_WID              12

#define TVFE_AP_INDICATOR19                             ((TOP_BASE_ADD+0x50)<<2)
#define GTTH_NUM_BIT                    0
#define GTTH_NUM_WID                    12

#define TVFE_BD_MUXCTRL1                                ((TOP_BASE_ADD+0x53)<<2)
#define BD_WIN_EN_BIT                   24
#define BD_WIN_EN_WID                   1
#define BD_HEND_BIT                     12
#define BD_HEND_WID                     12
#define BD_HSTART_BIT                   0
#define BD_HSTART_WID                   12

#define TVFE_BD_MUXCTRL2                                ((TOP_BASE_ADD+0x54)<<2)
#define BD_VEND_BIT                     12
#define BD_VEND_WID                     12
#define BD_VSTART_BIT                   0
#define BD_VSTART_WID                   12

#define TVFE_BD_MUXCTRL3                                ((TOP_BASE_ADD+0x55)<<2)
#define BD_VALID_LN_EN_BIT              12
#define BD_VALID_LN_EN_WID              1
#define BD_VLD_LN_TH_BIT                0
#define BD_VLD_LN_TH_WID                12

#define TVFE_BD_MUXCTRL4                                ((TOP_BASE_ADD+0x56)<<2)
#define BD_LIMITED_FLD_RECORD_BIT       5
#define BD_LIMITED_FLD_RECORD_WID       1
#define BD_FLD_CD_NUM_BIT               0
#define BD_FLD_CD_NUM_WID               5

// ******** CLAMPING -- YPBPR & RGB *********** ******* ************************

#define TVFE_CLP_MUXCTRL1                               ((TOP_BASE_ADD+0x59)<<2)
#define CLAMPING_LOW_EN_BIT             30
#define CLAMPING_LOW_EN_WID             1
#define CLAMPING_HIGH_EN_BIT            29
#define CLAMPING_HIGH_EN_WID            1
#define CLAMPING_DLY_BIT                18
#define CLAMPING_DLY_WID                11
#define CLAMPING_SCALE_BIT              14
#define CLAMPING_SCALE_WID              4
#define CLAMPING_UPDN_RATIO_BIT         8
#define CLAMPING_UPDN_RATIO_WID         6
#define CLAMPING_IIR_COEFF_BIT          0
#define CLAMPING_IIR_COEFF_WID          4

#define TVFE_CLP_MUXCTRL2                               ((TOP_BASE_ADD+0x5A)<<2)
#define CLAMPING_PRIORITY_BIT           20
#define CLAMPING_PRIORITY_WID           2
#define CLAMPING_LOW_LENGTH_BIT         10
#define CLAMPING_LOW_LENGTH_WID         10
#define CLAMPING_HIGH_LENGTH_BIT        0
#define CLAMPING_HIGH_LENGTH_WID        10

#define TVFE_CLP_MUXCTRL3                               ((TOP_BASE_ADD+0x5B)<<2)
#define CLAMPING_TARGET_CRR_BIT         20
#define CLAMPING_TARGET_CRR_WID         10
#define CLAMPING_TARGET_CBB_BIT         10
#define CLAMPING_TARGET_CBB_WID         10
#define CLAMPING_TARGET_YG_BIT          0
#define CLAMPING_TARGET_YG_WID          10

#define TVFE_CLP_MUXCTRL4                               ((TOP_BASE_ADD+0x5C)<<2)
#define CLAMPING_UP_OFFSET_BIT          8
#define CLAMPING_UP_OFFSET_WID          8
#define CLAMPING_DN_OFFSET_BIT          0
#define CLAMPING_DN_OFFSET_WID          8

#define TVFE_CLP_INDICATOR1                             ((TOP_BASE_ADD+0x5D)<<2)
#define IIR_RESULT_CRR_BIT              20
#define IIR_RESULT_CRR_WID              10
#define IIR_RESULT_CBB_BIT              10
#define IIR_RESULT_CBB_WID              10
#define IIR_RESULT_YG_BIT               0
#define IIR_RESULT_YG_WID               10

// ******** DVSS BPG_BACKP_GATE (BACK PORCH GAT E) -- YPBPR & RGB **************

#define TVFE_BPG_BACKP_H                                ((TOP_BASE_ADD+0x61)<<2)
#define BACKP_H_ED_BIT                  16
#define BACKP_H_ED_WID                  16
#define BACKP_H_ST_BIT                  0
#define BACKP_H_ST_WID                  16

#define TVFE_BPG_BACKP_V                                ((TOP_BASE_ADD+0x62)<<2)
#define BACKP_V_ED_BIT                  16
#define BACKP_V_ED_WID                  16
#define BACKP_V_EN_BIT                  15
#define BACKP_V_EN_WID                  1
#define BACKP_V_ST_BIT                  0
#define BACKP_V_ST_WID                  15

// ******** DE GENERATION -- YPBPR & RGB ****** *********************************

#define TVFE_DEG_H                                      ((TOP_BASE_ADD+0x63)<<2)
#define DATAPROC_DLY_BIT                28
#define DATAPROC_DLY_WID                4
#define SYNCPROC_DLY_BIT                24
#define SYNCPROC_DLY_WID                4
#define DEG_HEND_BIT                    12
#define DEG_HEND_WID                    12
#define DEG_HSTART_BIT                  0
#define DEG_HSTART_WID                  12

#define TVFE_DEG_VODD                                   ((TOP_BASE_ADD+0x64)<<2)
#define DEG_VEND_ODD_BIT                12
#define DEG_VEND_ODD_WID                12
#define DEG_VSTART_ODD_BIT              0
#define DEG_VSTART_ODD_WID              12

#define TVFE_DEG_VEVEN                                  ((TOP_BASE_ADD+0x65)<<2)
#define DEG_VEND_EVEN_BIT               12
#define DEG_VEND_EVEN_WID               12
#define DEG_VSTART_EVEN_BIT             0
#define DEG_VSTART_EVEN_WID             12

// ******** OFFSET_GAIN_OFFSET -- ALL ******************************************

#define TVFE_OGO_OFFSET1                                ((TOP_BASE_ADD+0x69)<<2)
#define OGO_EN_BIT                      31
#define OGO_EN_WID                      1
#define OGO_UB_OFFSET1_BIT              12
#define OGO_UB_OFFSET1_WID              11
#define OGO_YG_OFFSET1_BIT              0
#define OGO_YG_OFFSET1_WID              11

#define TVFE_OGO_GAIN1                                  ((TOP_BASE_ADD+0x6A)<<2)
#define OGO_UB_GAIN_BIT                 12
#define OGO_UB_GAIN_WID                 12
#define OGO_YG_GAIN_BIT                 0
#define OGO_YG_GAIN_WID                 12

#define TVFE_OGO_GAIN2                                  ((TOP_BASE_ADD+0x6B)<<2)
#define OGO_VR_GAIN_BIT                 0
#define OGO_VR_GAIN_WID                 12

#define TVFE_OGO_OFFSET2                                ((TOP_BASE_ADD+0x6C)<<2)
#define OGO_UB_OFFSET2_BIT              12
#define OGO_UB_OFFSET2_WID              11
#define OGO_YG_OFFSET2_BIT              0
#define OGO_YG_OFFSET2_WID              11

#define TVFE_OGO_OFFSET3                                ((TOP_BASE_ADD+0x6D)<<2)
#define OGO_VR_OFFSET2_BIT              12
#define OGO_VR_OFFSET2_WID              11
#define OGO_VR_OFFSET1_BIT              0
#define OGO_VR_OFFSET1_WID              11

// ******** CI7740KN RELATED -- ALL ********************************************

#define TVFE_VAFE_CTRL                                  ((TOP_BASE_ADD+0x70)<<2)
#define VAFE_CLK_PHASE_BIT              16
#define VAFE_CLK_PHASE_WID              5
#define VAFE_ENGAINCAL_BIT              15
#define VAFE_ENGAINCAL_WID              1
#define VAFE_ENOFFSETCAL_BIT            14
#define VAFE_ENOFFSETCAL_WID            1
#define VAFE_SELGAINCALLVL_BIT          12
#define VAFE_SELGAINCALLVL_WID          2
#define VAFE_ADC_REG_ADDR_H_BIT         4
#define VAFE_ADC_REG_ADDR_H_WID         3
#define VAFE_HS_VS_MUX_BIT              0
#define VAFE_HS_VS_MUX_WID              1

#define TVFE_VAFE_STATUS                                ((TOP_BASE_ADD+0x71)<<2)
#define VAFE_STATUS_PLLLOCK             4
#define VAFE_STATUS_PLLLOCK_WID         1
#define VAFE_HSOUT2VALID_BIT            3
#define VAFE_HSOUT2VALID_WID            1
#define VAFE_ADCRDYC_BIT                2
#define VAFE_ADCRDYC_WID                1
#define VAFE_ADCRDYB_BIT                1
#define VAFE_ADCRDYB_WID                1
#define VAFE_ADCRDYA_BIT                0
#define VAFE_ADCRDYA_WID                1

#define TVFE_TOP_CTRL                                   ((TOP_BASE_ADD+0x72)<<2)
/*
000: abc
001: acb
010: bac
011: bca
100: cab
101: cba
 */
#define SWT_GY_BCB_RCR_IN_BIT           28
#define SWT_GY_BCB_RCR_IN_WID           3
#define ADC_AUTO_CAL_MASK_BIT           27
#define ADC_AUTO_CAL_MASK_WID           1
#define VGA_DDC_SEL_BIT_WID             22
#define VGA_DDC_SEL_WID                 1
#define ABLC_ENABLE_BIT                 21
#define ABLC_ENABLE_WID                 1
#define DEBUG_MUX_BIT                   16
#define DEBUG_MUX_WID                   5
#define COMP_CLK_ENABLE_BIT             15
#define COMP_CLK_ENABLE_WID             1
#define DCLK_ENABLE_BIT                 14
#define DCLK_ENABLE_WID                 1
#define ADC_CLK_INV_BIT                 13
#define ADC_CLK_INV_WID                 1
#define DATACK_INV_SEL_BIT              12
#define DATACK_INV_SEL_WID              1
#define VAFE_MCLK_EN_BIT                11
#define VAFE_MCLK_EN_WID                1
#define EDID_CLK_EN_BIT                 10
#define EDID_CLK_EN_WID                 1
#define TVFE_ADC_CLK_DIV_BIT            8
#define TVFE_ADC_CLK_DIV_WID            2
#define ADC_EXT_COAST_EN_BIT            6
#define ADC_EXT_COAST_EN_WID            1
#define TVFE_BACKP_GATE_MUX_BIT         4
#define TVFE_BACKP_GATE_MUX_WID         2
#define SCAN_REG_BIT                    0
#define SCAN_REG_WID                    1

#define TVFE_CLAMP_INTF                                 ((TOP_BASE_ADD+0x73)<<2)
#define CLAMP_EN_BIT                    15
#define CLAMP_EN_WID                    1
#define CLAMP_C_CURRENT_SEL_BIT         8
#define CLAMP_C_CURRENT_SEL_WID         3
#define CLAMP_SIGNAL_DLY_BIT            7
#define CLAMP_SIGNAL_DLY_WID            1
#define CLAMP_B_CURRENT_SEL_BIT         4
#define CLAMP_B_CURRENT_SEL_WID         3
#define CLAMP_UP_DN_SRC_BIT             3
#define CLAMP_UP_DN_SRC_WID             1
#define CLAMP_A_CURRENT_SEL_BIT         0
#define CLAMP_A_CURRENT_SEL_WID         3

#define TVFE_RST_CTRL                                   ((TOP_BASE_ADD+0x74)<<2)
#define DCLK_RST_BIT                    10
#define DCLK_RST_WID                    1
#define SAMPLE_OUT_RST_BIT              9
#define SAMPLE_OUT_RST_WID              1
#define ACD_REG_INF_RST_BIT             8
#define ACD_REG_INF_RST_WID             1
#define CVD_REG_INF_RST_BIT             7
#define CVD_REG_INF_RST_WID             1
#define VAFE_REG_INF_RST_BIT            6
#define VAFE_REG_INF_RST_WID            1
#define EDID_RST_BIT                    5
#define EDID_RST_WID                    1
#define VAFE_RST_BIT                    4
#define VAFE_RST_WID                    1
#define ADC_CLK_RST_BIT                 3
#define ADC_CLK_RST_WID                 1
#define MCLK_RST_BIT                    2
#define MCLK_RST_WID                    1
#define AUTO_MODE_CLK_RST_BIT           1
#define AUTO_MODE_CLK_RST_WID           1
#define ALL_CLK_RST_BIT                 0
#define ALL_CLK_RST_WID                 1

#define TVFE_EXT_VIDEO_AFE_CTRL_MUX1                    ((TOP_BASE_ADD+0x75)<<2)

// ******** EDID -- RGB ********************************************************

#define TVFE_EDID_CONFIG                                ((TOP_BASE_ADD+0x7A)<<2)
#define EDID_INT_MODE_BIT               26
#define EDID_INT_MODE_WID               2
#define EDID_SEGMENT_MISS_MSK_BIT       25
#define EDID_SEGMENT_MISS_MSK_WID       1
#define EDID_I2C_MODE_BIT               24
#define EDID_I2C_MODE_WID               1
#define EDID_I2C_8BIT_MODE_BIT          23
#define EDID_I2C_8BIT_MODE_WID          1
#define EDID_SEGMENT_INDEX_BIT          16
#define EDID_SEGMENT_INDEX_WID          7
#define EDID_I2C_EDDC_MODE_BIT          15
#define EDID_I2C_EDDC_MODE_WID          1
#define EDID_I2C_SEGMENT_ID_BIT         8
#define EDID_I2C_SEGMENT_ID_WID         7
#define EDID_I2C_DEV_ID_BIT             0
#define EDID_I2C_DEV_ID_WID             7

#define TVFE_EDID_RAM_ADDR                              ((TOP_BASE_ADD+0x7B)<<2)
#define EDID_RAM_ACCESS_MODE_BIT        8
#define EDID_RAM_ACCESS_MODE_WID        1
#define EDID_RAM_ADDR_BIT               0
#define EDID_RAM_ADDR_WID               8

#define TVFE_EDID_RAM_WDATA                             ((TOP_BASE_ADD+0x7C)<<2)
#define EDID_RAM_WDATA_BIT              0
#define EDID_RAM_WDATA_WID              8

#define TVFE_EDID_RAM_RDATA                             ((TOP_BASE_ADD+0x7D)<<2)
#define EDID_I2C_ST_BIT                 25
#define EDID_I2C_ST_WID                 3
#define EDID_RAM_SEGMENT_ST_BIT         24
#define EDID_RAM_SEGMENT_ST_WID         1
#define EDID_ACCESSED_RAM_ADDR_BIT      8
#define EDID_ACCESSED_RAM_ADDR_WID      16
#define EDID_RAM_RDATA_BIT              0
#define EDID_RAM_RDATA_WID              8

// ******** APB BUS -- ALL *****************************************************

#define TVFE_APB_ERR_CTRL_MUX1                          ((TOP_BASE_ADD+0x80)<<2)
#define ERR_CTRL_ACD_BIT                16
#define ERR_CTRL_ACD_WID                16
#define ERR_CTRL_CVD_BIT                0
#define ERR_CTRL_CVD_WID                16

#define TVFE_APB_ERR_CTRL_MUX2                          ((TOP_BASE_ADD+0x81)<<2)
#define ERR_CTRL_CVD_BIT                0
#define ERR_CTRL_CVD_WID                16

#define TVFE_APB_INDICATOR1                                  ((TOP_BASE_ADD+0x82)<<2)
#define ERR_CNT_ACD_BIT                 12
#define ERR_CNT_ACD_WID                 12
#define ERR_CNT_CVD_BIT                 0
#define ERR_CNT_CVD_WID                 12

#define TVFE_APB_INDICATOR2                                 ((TOP_BASE_ADD+0x83)<<2)
#define ERR_CNT_AFEIP_BIT               0
#define ERR_CNT_AFEIP_WID               12

// ******** ADC READBACK -- ALL ************************************************

#define TVFE_ADC_READBACK_CTRL1                     ((TOP_BASE_ADD+0x84)<<2)
#define READBACK_H_END_BIT                  16
#define READBACK_H_END_WID            13
#define READBACK_H_START_BIT              0
#define READBACK_H_START_WID        13
#define TVFE_ADC_READBACK_CTRL2                     ((TOP_BASE_ADD+0x85)<<2)
#define READBACK_V_END_BIT                  16
#define READBACK_V_END_WID            13
#define READBACK_V_START_BIT              0
#define READBACK_V_START_WID        13


#define TVFE_ADC_READBACK_CTRL                       ((TOP_BASE_ADD+0x86)<<2)
#define ADC_READBACK_MODE_BIT           31
#define ADC_READBACK_MODE_WID           1
#define ADC_READBACK_HSSEL_BIT          29
#define ADC_READBACK_HSSEL_WID          2
#define ADC_READBACK_HCNT_BIT           16
#define ADC_READBACK_HCNT_WID           13
#define ADC_READBACK_VSSEL_BIT          13
#define ADC_READBACK_VSSEL_WID          2
#define ADC_READBACK_VCNT_BIT           0
#define ADC_READBACK_VCNT_WID           13

#define TVFE_INT_CLR                                    ((TOP_BASE_ADD+0x8A)<<2)
#define INT_CLR_BIT                     0
#define INT_CLR_WID                     17

#define TVFE_INT_MSKN                                   ((TOP_BASE_ADD+0x8B)<<2)
#define INT_MSKN_BIT                    0
#define INT_MSKN_WID                    17

#define TVFE_INT_INDICATOR1                             ((TOP_BASE_ADD+0x8C)<<2)
#define WARNING_3D_BIT                  17
#define WARNING_3D_WID                  1
#define VAFE_INT_INDICATOR1_PLLLOCK_BIT 16
#define VAFE_INT_INDICATOR1_PLLLOCK_WID 1
#define VAFE_LOST_PLLLOCK_BIT           15
#define VAFE_LOST_PLLLOCK_WID           1
#define CVD2_VS_BIT                     14
#define CVD2_VS_WID                     1
#define TVFE_VS_BIT                     13
#define TVFE_VS_WID                     1
#define TVFE_EDID_INT_BIT               12
#define TVFE_EDID_INT_WID               1
#define CVD2_LOST_EXT_LOCKED_BIT        11
#define CVD2_LOST_EXT_LOCKED_WID        1
#define CVD2_EXT_LOCKED_BIT             10
#define CVD2_EXT_LOCKED_WID             1
#define CVD2_LOST_FINE_LOCK_BIT         9
#define CVD2_LOST_FINE_LOCK_WID         1
#define CVD2_FINE_LOCK_BIT              8
#define CVD2_FINE_LOCK_WID              1
#define CVD2_LOST_CHROMA_LOCK_BIT       7
#define CVD2_LOST_CHROMA_LOCK_WID       1
#define CVD2_CHROMA_LOCK_BIT            6
#define CVD2_CHROMA_LOCK_WID            1
#define CVD2_LOST_VLOCK_BIT             5
#define CVD2_LOST_VLOCK_WID             1
#define CVD2_VLOCK_BIT                  4
#define CVD2_VLOCK_WID                  1
#define CVD2_MV_DETECTED_BIT            3
#define CVD2_MV_DETECTED_WID            3
#define CVD2_NO_SIGNAL_BIT              2
#define CVD2_NO_SIGNAL_WID              1
#define INDICATOR_DVSS_MVDET_BIT        1
#define INDICATOR_DVSS_MVDET_WID        1
#define INDICATOR_DVSS_NOSIG_BIT        0
#define INDICATOR_DVSS_NOSIG_WID        1

#define TVFE_INT_SET                                    ((TOP_BASE_ADD+0x8D)<<2)
#define INT_SET_BIT                     0
#define INT_SET_WID                     17

#define TVFE_CHIP_VERSION                               ((TOP_BASE_ADD+0x90)<<2)

// ********TVAFE AA FITER ********************************************************
#define TVFE_AAFILTER_CTRL1                           ((TOP_BASE_ADD+0x91)<<2)
#define AAFILER_BYPASS_BIT                 20
//[20]:all [21]:bypass y[22]:bypass cb [23]:bypass cr
#define AAFILTER_BYPASS_WID         4
#define AAFILTER_UV_BIT                        17
#define AAFILTER_UV_WID                  1
#define AAFILTER_SCALE_BIT                 16
#define AAFILTER_SCALE_WID                1
#define AAFILTER_Y_ALPHA0_BIT           8
#define AAFILTER_Y_ALPHA0_WID     8
#define AAFILTER_Y_ALPHA1_BIT           0
#define AAFILTER_Y_ALPHA1_WID     8

#define TVFE_AAFILTER_CTRL2                           ((TOP_BASE_ADD+0x92)<<2)
#define AAFILTER_Y_ALPHA2_BIT           24
#define AAFILTER_Y_ALPHA2_WID     8
#define AAFILTER_Y_ALPHA3_BIT           16
#define AAFILTER_Y_ALPHA3_WID     8
#define AAFILTER_Y_ALPHA4_BIT           8
#define AAFILTER_Y_ALPHA4_WID     8
#define AAFILTER_Y_ALPHA5_BIT           0
#define AAFILTER_Y_ALPHA5_WID     8

#define TVFE_AAFILTER_CTRL3                           ((TOP_BASE_ADD+0x93)<<2)
#define AAFILTER_CB_ALPHA0_BIT           24
#define AAFILTER_CB_ALPHA0_WID     8
#define AAFILTER_CB_ALPHA1_BIT           16
#define AAFILTER_CB_ALPHA1_WID     8
#define AAFILTER_CB_ALPHA2_BIT           8
#define AAFILTER_CB_ALPHA2_WID     8
#define AAFILTER_CB_ALPHA3_BIT           0
#define AAFILTER_CB_ALPHA3_WID     8

#define TVFE_AAFILTER_CTRL4                          ((TOP_BASE_ADD+0x94)<<2)
#define AAFILTER_CB_ALPHA4_BIT           24
#define AAFILTER_CB_ALPHA4_WID     8
#define AAFILTER_CB_ALPHA5_BIT           16
#define AAFILTER_CB_ALPHA5_WID     8
#define AAFILTER_CR_ALPHA0_BIT           8
#define AAFILTER_CR_ALPHA0_WID     8
#define AAFILTER_CR_ALPHA1_BIT           0
#define AAFILTER_CR_ALPHA1_WID     8

#define TVFE_AAFILTER_CTRL5                          ((TOP_BASE_ADD+0x95)<<2)
#define AAFILTER_CR_ALPHA2_BIT           24
#define AAFILTER_CR_ALPHA2_WID     8
#define AAFILTER_CR_ALPHA3_BIT           16
#define AAFILTER_CR_ALPHA3_WID     8
#define AAFILTER_CR_ALPHA4_BIT           8
#define AAFILTER_CR_ALPHA4_WID     8
#define AAFILTER_CR_ALPHA5_BIT           0
#define AAFILTER_CR_ALPHA5_WID     8

//*************TVFE_SOG_MON***************************************************
#define TVFE_SOG_MON_CTRL1                           ((TOP_BASE_ADD+0x96)<<2)
#define SOGTOP_AUTO_ENABLE_BIT          31
#define SOGTOP_AUTO_ENABLE_WID    1
#define SOGTOP_INV_MASK_BIT                  30
#define SOGTOP_INV_MASK_WID            1
#define SOGTOP_INV_SOG_BIT                    29
#define SOGTOP_INV_SOG_WID              1
#define SOGTOP_AUTO_LCNT_BIT               8
#define SOGTOP_AUTO_LCNT_WID         12
#define SOGTOP_MASK_EN_BIT                  7
#define SOGTOP_MASK_EN_WID            1
#define SOGTOP_MASK_FMAT_BIT             4
#define SOGTOP_MASK_FMAT_WID       3
#define SOGTOP_VSYNC_SLE_BIT              2
#define SOGTOP_VSYNC_SLE_WID        2
#define SOGTOP_MASK_SEL_BIT                0
#define SOGTOP_MASK_SEL_WID          2

#define TVFE_SOG_MON_INDICATOR1                ((TOP_BASE_ADD+0x97)<<2)
#define SOG_CNT_NEG_BIT                         16
#define SOG_CNT_NEG_WID                   16
#define SOG_CNT_POS_BIT                         0
#define SOG_CNT_POS_WID                   16

#define TVFE_SOG_MON_INDICATOR2                ((TOP_BASE_ADD+0x98)<<2)
#define SOG_VTOTAL_BIT                         0
#define SOG_VTOTAL_WID                   16
// ********TVFE_READBACK_INDICATOR*********************************************
#define TVFE_ADC_READBACK_INDICATOR             ((TOP_BASE_ADD+0x9A)<<2)
#define ADC_READBACK_DA_BIT               20
#define ADC_READBACK_DA_WID               10
#define ADC_READBACK_DB_BIT               10
#define ADC_READBACK_DB_WID               10
#define ADC_READBACK_DC_BIT                0
#define ADC_READBACK_DC_WID               10

#define TVFE_READBACK_INDICATOR1                ((TOP_BASE_ADD+0x9B)<<2)
#define INDICATOR_RB_DA_AREA_BIT               0
#define INDICATOR_RB_DA_AREA_WID         32

#define TVFE_READBACK_INDICATOR2                ((TOP_BASE_ADD+0x9C)<<2)
#define INDICATOR_RB_DB_AREA_BIT               0
#define INDICATOR_RB_DB_AREA_WID         32

#define TVFE_READBACK_INDICATOR3                ((TOP_BASE_ADD+0x9D)<<2)
#define INDICATOR_RB_DC_AREA_BIT               0
#define INDICATOR_RB_DC_AREA_WID         32
// ********CVBS AA FITER SIGNAL***************************************************
#define TVFE_AFC_CTRL1                                       ((TOP_BASE_ADD+0xA0)<<2)
#define AFC_EN_BIT                                              31
#define AFC_EN_WID                                        1
#define AFC_BYPASS_BIT                                     30
#define AFC_BYPASS_WID                               1
#define AFC_CDELAY_BIT                                     29
#define AFC_CDELAY_WID                               1
#define AFC_YC_ALHPA0_BIT                               16
#define AFC_YC_ALHPA0_WID                         12
#define AFC_YC_ALHPA1_BIT                               0
#define AFC_YC_ALHPA1_WID                         12

#define TVFE_AFC_CTRL2                                       ((TOP_BASE_ADD+0xA1)<<2)
#define AFC_YC_ALHPA4_BIT                               20
#define AFC_YC_ALHPA4_WID                         10
#define AFC_YC_ALHPA3_BIT                               10
#define AFC_YC_ALHPA3_WID                         10
#define AFC_YC_ALHPA2_BIT                               0
#define AFC_YC_ALHPA2_WID                         10

#define TVFE_AFC_CTRL3                                       ((TOP_BASE_ADD+0xA2)<<2)
#define AFC_YC_ALHPA7_BIT                               20
#define AFC_YC_ALHPA7_WID                         8
#define AFC_YC_ALHPA6_BIT                               10
#define AFC_YC_ALHPA6_WID                         10
#define AFC_YC_ALHPA5_BIT                               0
#define AFC_YC_ALHPA5_WID                         10

#define TVFE_AFC_CTRL4                                       ((TOP_BASE_ADD+0xA3)<<2)
#define AFC_YC_ALHPA11_BIT                           24
#define AFC_YC_ALHPA11_WID                     8
#define AFC_YC_ALHPA10_BIT                           16
#define AFC_YC_ALHPA10_WID                     8
#define AFC_YC_ALHPA9_BIT                         8
#define AFC_YC_ALHPA9_WID                   8
#define AFC_YC_ALHPA8_BIT                         0
#define AFC_YC_ALHPA8_WID                   8

#define TVFE_AFC_CTRL5                                       ((TOP_BASE_ADD+0xA4)<<2)
#define AFC_VDELAY_BIT                               10
#define AFC_VDELAY_WID                         2
#define AFC_UDELAY_BIT                               8
#define AFC_UDELAY_WID                         2
#define AFC_YC_ALHPA12_BIT                       0
#define AFC_YC_ALHPA12_WID                 8


// **************************************************** *************************
// ******** CVD2 REGISTERS ********
// **************************************************** *************************

#define CVD_BASE_ADD                                    0x3000//0x1800

#define CVD2_CONTROL0                                   ((CVD_BASE_ADD+0x00)<<2)
#define HV_DLY_BIT                      7
#define HV_DLY_WID                      1
#define HPIXEL_BIT                      5
#define HPIXEL_WID                      2
#define VLINE_625_BIT                   4
#define VLINE_625_WID                   1
#define COLOUR_MODE_BIT                 1
#define COLOUR_MODE_WID                 3
#define YC_SRC_BIT                      0
#define YC_SRC_WID                      1

#define CVD2_CONTROL1                                   ((CVD_BASE_ADD+0x01)<<2)
#define CV_INV_BIT                      7
#define CV_INV_WID                      1
#define CV_SRC_BIT                      6
#define CV_SRC_WID                      1
#define LUMA_NOTCH_BW_BIT               4
#define LUMA_NOTCH_BW_WID               2
#define CHROMA_BW_LO_BIT                2
#define CHROMA_BW_LO_WID                2
#define CHROMA_BURST5OR10_BIT           1
#define CHROMA_BURST5OR10_WID           1
#define PED_BIT                         0
#define PED_WID                         1

#define CVD2_CONTROL2                                   ((CVD_BASE_ADD+0x02)<<2)
#define HAGC_FLD_MODE_BIT               7
#define HAGC_FLD_MODE_WID               1
#define MV_HAGC_MODE_BIT                6
#define MV_HAGC_MODE_WID                1
#define DC_CLAMP_MODE_BIT               4
#define DC_CLAMP_MODE_WID               2
#define DAGC_EN_BIT                     3
#define DAGC_EN_WID                     1
#define AGC_HALF_EN_BIT                 2
#define AGC_HALF_EN_WID                 1
#define CAGC_EN_BIT                     1
#define CAGC_EN_WID                     1
#define HAGC_EN_BIT                     0
#define HAGC_EN_WID                     1

#define CVD2_YC_SEPARATION_CONTROL                      ((CVD_BASE_ADD+0x03)<<2)
#define NTSC443_3DMODE_BIT              7
#define NTSC443_3DMODE_WID              1
#define ADAPTIVE_3DMODE_BIT             4
#define ADAPTIVE_3DMODE_WID             3
#define COLOUR_TRAP_BIT                 3
#define COLOUR_TRAP_WID                 1
#define ADAPTIVE_MODE_BIT               0
#define ADAPTIVE_MODE_WID               3

#define CVD2_LUMA_AGC_VALUE                             ((CVD_BASE_ADD+0x04)<<2)
#define HAGC_BIT                        0
#define HAGC_WID                        8

#define CVD2_NOISE_THRESHOLD                            ((CVD_BASE_ADD+0x05)<<2)
#define NOISE_TH_BIT                    0
#define NOISE_TH_WID                    8

#define CVD2_REG_06                                     ((CVD_BASE_ADD+0x06)<<2)
#define ADC_UPDN_SWAP_BIT               7
#define ADC_UPDN_SWAP_WID               1
#define ADC_IN_SWAP_BIT                 6
#define ADC_IN_SWAP_WID                 1
#define FORCE_VCR_EN_BIT                4
#define FORCE_VCR_EN_WID                1
#define FORCE_VCR_REW_BIT               3
#define FORCE_VCR_REW_WID               1
#define FORCE_VCR_FF_BIT                2
#define FORCE_VCR_FF_WID                1
#define FORCE_VCR_TRICK_BIT             1
#define FORCE_VCR_TRICK_WID             1
#define FORCE_VCR_BIT                   0
#define FORCE_VCR_WID                   1

#define CVD2_OUTPUT_CONTROL                             ((CVD_BASE_ADD+0x07)<<2)
#define CCIR656_EN_BIT                  7
#define CCIR656_EN_WID                  1
#define CBCR_SWAP_BIT                   6
#define CBCR_SWAP_WID                   1
#define BLUE_MODE_BIT                   4
#define BLUE_MODE_WID                   2
#define YC_DLY_BIT                      0
#define YC_DLY_WID                      4

#define CVD2_LUMA_CONTRAST_ADJUSTMENT                   ((CVD_BASE_ADD+0x08)<<2)
#define CONTRAST_BIT                    0
#define CONTRAST_WID                    8

#define CVD2_LUMA_BRIGHTNESS_ADJUSTMENT                 ((CVD_BASE_ADD+0x09)<<2)
#define BRIGHTNESS_BIT                  0
#define BRIGHTNESS_WID                  8

#define CVD2_CHROMA_SATURATION_ADJUSTMENT               ((CVD_BASE_ADD+0x0A)<<2)
#define SATURATION_BIT                  0
#define SATURATION_WID                  8

#define CVD2_CHROMA_HUE_PHASE_ADJUSTMENT                ((CVD_BASE_ADD+0x0B)<<2)
#define HUE_BIT                         0
#define HUE_WID                         8

#define CVD2_CHROMA_AGC                                 ((CVD_BASE_ADD+0x0C)<<2)
#define CAGC_BIT                        0
#define CAGC_WID                        8

#define CVD2_CHROMA_KILL                                ((CVD_BASE_ADD+0x0D)<<2)
#define USER_CKILL_MODE_BIT             6
#define USER_CKILL_MODE_WID             2
#define VBI_CKILL_BIT                   5
#define VBI_CKILL_WID                   1
#define HLOCK_CKILL_BIT                 4
#define HLOCK_CKILL_WID                 1
#define PAL60_MODE_BIT                  0
#define PAL60_MODE_WID                  1

#define CVD2_NON_STANDARD_SIGNAL_THRESHOLD              ((CVD_BASE_ADD+0x0E)<<2)
#define VNON_STD_TH_BIT                 6
#define VNON_STD_TH_WID                 2
#define HNON_STD_TH_BIT                 0
#define HNON_STD_TH_WID                 6

#define CVD2_CONTROL0F                                  ((CVD_BASE_ADD+0x0F)<<2)
#define NSTD_HYSIS_BIT                  6
#define NSTD_HYSIS_WID                  2
#define DISABLE_CLAMP_ON_VS_BIT         5
#define DISABLE_CLAMP_ON_VS_WID         1
#define BYPASS_BIT                      4
#define BYPASS_WID                      1
#define NOBURST_CKILL_BIT               0
#define NOBURST_CKILL_WID               1

#define CVD2_AGC_PEAK_NOMINAL                           ((CVD_BASE_ADD+0x10)<<2)
#define AGC_PEAK_NOMINAL_BIT            0
#define AGC_PEAK_NOMINAL_WID            7

#define CVD2_AGC_PEAK_AND_GATE_CONTROLS                 ((CVD_BASE_ADD+0x11)<<2)
#define AGC_PEAK_EN_BIT                 3
#define AGC_PEAK_EN_WID                 1
#define AGC_PEAK_CNTL_BIT               0
#define AGC_PEAK_CNTL_WID               3

#define CVD2_BLUE_SCREEN_Y                              ((CVD_BASE_ADD+0x12)<<2)
#define BLUE_SCREEN_Y_BIT               0
#define BLUE_SCREEN_Y_WID               8

#define CVD2_BLUE_SCREEN_CB                             ((CVD_BASE_ADD+0x13)<<2)
#define BLUE_SCREEN_CB_BIT              0
#define BLUE_SCREEN_CB_WID              8

#define CVD2_BLUE_SCREEN_CR                             ((CVD_BASE_ADD+0x14)<<2)
#define BLUE_SCREEN_CR_BIT              0
#define BLUE_SCREEN_CR_WID              8

#define CVD2_HDETECT_CLAMP_LEVEL                        ((CVD_BASE_ADD+0x15)<<2)
#define HDETECT_CLAMP_LVL_BIT           0
#define HDETECT_CLAMP_LVL_WID           8

#define CVD2_LOCK_COUNT                                 ((CVD_BASE_ADD+0x16)<<2)
#define HLOCK_CNT_NOISY_MAX_BIT         4
#define HLOCK_CNT_NOISY_MAX_WID         4
#define HLOCK_CNT_CLEAN_MAX_BIT         0
#define HLOCK_CNT_CLEAN_MAX_WID         4

#define CVD2_H_LOOP_MAXSTATE                            ((CVD_BASE_ADD+0x17)<<2)
#define HLOCK_VS_MODE_BIT               6
#define HLOCK_VS_MODE_WID               2
#define HSTATE_FIXED_BIT                5
#define HSTATE_FIXED_WID                1
#define DISABLE_HFINE_BIT               4
#define DISABLE_HFINE_WID               1
#define HSTATE_UNLOCKED_BIT             3
#define HSTATE_UNLOCKED_WID             1
#define HSTATE_MAX_BIT                  0
#define HSTATE_MAX_WID                  3

#define CVD2_CHROMA_DTO_INCREMENT_29_24                 ((CVD_BASE_ADD+0x18)<<2)
#define CDTO_INC_29_24_BIT              0
#define CDTO_INC_29_24_WID              6

#define CVD2_CHROMA_DTO_INCREMENT_23_16                 ((CVD_BASE_ADD+0x19)<<2)
#define CDTO_INC_23_16_BIT              0
#define CDTO_INC_23_16_WID              8

#define CVD2_CHROMA_DTO_INCREMENT_15_8                  ((CVD_BASE_ADD+0x1A)<<2)
#define CDTO_INC_15_8_BIT               0
#define CDTO_INC_15_8_WID               8

#define CVD2_CHROMA_DTO_INCREMENT_7_0                   ((CVD_BASE_ADD+0x1B)<<2)
#define CDTO_INC_7_0_BIT                0
#define CDTO_INC_7_0_WID                8

#define CVD2_HSYNC_DTO_INCREMENT_31_24                  ((CVD_BASE_ADD+0x1C)<<2)
#define HDTO_INC_31_24_BIT              0
#define HDTO_INC_31_24_WID              8

#define CVD2_HSYNC_DTO_INCREMENT_23_16                  ((CVD_BASE_ADD+0x1D)<<2)
#define HDTO_INC_23_16_BIT              0
#define HDTO_INC_23_16_WID              8

#define CVD2_HSYNC_DTO_INCREMENT_15_8                   ((CVD_BASE_ADD+0x1E)<<2)
#define HDTO_INC_15_8_BIT               0
#define HDTO_INC_15_8_WID               8

#define CVD2_HSYNC_DTO_INCREMENT_7_0                    ((CVD_BASE_ADD+0x1F)<<2)
#define HDTO_INC_7_0_BIT                0
#define HDTO_INC_7_0_WID                8

#define CVD2_HSYNC_RISING_EDGE                          ((CVD_BASE_ADD+0x20)<<2)
#define HS_RISING_BIT                   0
#define HS_RISING_WID                   8

#define CVD2_HSYNC_PHASE_OFFSET                         ((CVD_BASE_ADD+0x21)<<2)
#define HS_PHASE_OFFSET_BIT             0
#define HS_PHASE_OFFSET_WID             8

#define CVD2_HSYNC_DETECT_WINDOW_START                  ((CVD_BASE_ADD+0x22)<<2)
#define HS_GATE_START_BIT               0
#define HS_GATE_START_WID               8

#define CVD2_HSYNC_DETECT_WINDOW_END                    ((CVD_BASE_ADD+0x23)<<2)
#define HS_GATE_END_BIT                 0
#define HS_GATE_END_WID                 8

#define CVD2_CLAMPAGC_CONTROL                           ((CVD_BASE_ADD+0x24)<<2)
#define HS_SIMILAR_BIT                  7
#define HS_SIMILAR_WID                  1
#define HS_LOW_BIT                      6
#define HS_LOW_WID                      1
#define HDETECT_NOISE_EN_BIT            5
#define HDETECT_NOISE_EN_WID            1
#define HFINE_LT_COARSE_BIT             4
#define HFINE_LT_COARSE_WID             1
#define HLPF_CLAMP_SEL_BIT              3
#define HLPF_CLAMP_SEL_WID              1
#define HLFP_CLAMP_NOISY_EN_BIT         2
#define HLFP_CLAMP_NOISY_EN_WID         1
#define HLPF_CLAMP_VBI_EN_BIT           1
#define HLPF_CLAMP_VBI_EN_WID           1
#define HLPF_CLAMP_EN_BIT               0
#define HLPF_CLAMP_EN_WID               1

#define CVD2_HSYNC_WIDTH_STATUS                         ((CVD_BASE_ADD+0x25)<<2)
#define STATUS_HS_WIDTH_BIT             0
#define STATUS_HS_WIDTH_WID             8

#define CVD2_HSYNC_RISING_EDGE_START                    ((CVD_BASE_ADD+0x26)<<2)
#define HS_RISING_AUTO_BIT              6
#define HS_RISING_AUTO_WID              2
#define HS_RISING_START_BIT             0
#define HS_RISING_START_WID             6

#define CVD2_HSYNC_RISING_EDGE_END                      ((CVD_BASE_ADD+0x27)<<2)
#define HS_RISING_END_BIT               0
#define HS_RISING_END_WID               8

#define CVD2_STATUS_BURST_MAGNITUDE_LSB                 ((CVD_BASE_ADD+0x28)<<2)
#define STATUS_BURST_MAG_LSB_BIT        0
#define STATUS_BURST_MAG_LSB_WID        8

#define CVD2_STATUS_BURST_MAGNITUDE_MSB                 ((CVD_BASE_ADD+0x29)<<2)
#define STATUS_BURST_MAG_MSB_BIT        0
#define STATUS_BURST_MAG_MSB_WID        8

#define CVD2_HSYNC_FILTER_GATE_START                    ((CVD_BASE_ADD+0x2A)<<2)
#define HBLANK_START_BIT                0
#define HBLANK_START_WID                8

#define CVD2_HSYNC_FILTER_GATE_END                      ((CVD_BASE_ADD+0x2B)<<2)
#define HBLANK_END_BIT                  0
#define HBLANK_END_WID                  8

#define CVD2_CHROMA_BURST_GATE_START                    ((CVD_BASE_ADD+0x2C)<<2)
#define BURST_GATE_START_BIT            0
#define BURST_GATE_START_WID            8

#define CVD2_CHROMA_BURST_GATE_END                      ((CVD_BASE_ADD+0x2D)<<2)
#define BURST_GATE_END_BIT              0
#define BURST_GATE_END_WID              8

#define CVD2_ACTIVE_VIDEO_HSTART                        ((CVD_BASE_ADD+0x2E)<<2)
#define HACTIVE_START_BIT               0
#define HACTIVE_START_WID               8

#define CVD2_ACTIVE_VIDEO_HWIDTH                        ((CVD_BASE_ADD+0x2F)<<2)
#define HACTIVE_WIDTH_BIT               0
#define HACTIVE_WIDTH_WID               8

#define CVD2_ACTIVE_VIDEO_VSTART                        ((CVD_BASE_ADD+0x30)<<2)
#define VACTIVE_START_BIT               0
#define VACTIVE_START_WID               8

#define CVD2_ACTIVE_VIDEO_VHEIGHT                       ((CVD_BASE_ADD+0x31)<<2)
#define VACTIVE_HEIGHT_BIT              0
#define VACTIVE_HEIGHT_WID              8

#define CVD2_VSYNC_H_LOCKOUT_START                      ((CVD_BASE_ADD+0x32)<<2)
#define VS_H_MIN_BIT                    0
#define VS_H_MIN_WID                    7

#define CVD2_VSYNC_H_LOCKOUT_END                        ((CVD_BASE_ADD+0x33)<<2)
#define VS_H_MAX_BIT                    0
#define VS_H_MAX_WID                    7

#define CVD2_VSYNC_AGC_LOCKOUT_START                    ((CVD_BASE_ADD+0x34)<<2)
#define VS_AGC_MIN_BIT                  0
#define VS_AGC_MIN_WID                  7

#define CVD2_VSYNC_AGC_LOCKOUT_END                      ((CVD_BASE_ADD+0x35)<<2)
#define VS_AGC_MAX_BIT                  0
#define VS_AGC_MAX_WID                  6

#define CVD2_VSYNC_VBI_LOCKOUT_START                    ((CVD_BASE_ADD+0x36)<<2)
#define VS_VBI_MIN_BIT                  0
#define VS_VBI_MIN_WID                  7

#define CVD2_VSYNC_VBI_LOCKOUT_END                      ((CVD_BASE_ADD+0x37)<<2)
#define VLOCK_WIDE_RANGE_BIT            7
#define VLOCK_WIDE_RANGE_WID            1
#define VS_VBI_MAX_BIT                  0
#define VS_VBI_MAX_WID                  7

#define CVD2_VSYNC_CNTL                                 ((CVD_BASE_ADD+0x38)<<2)
#define PROSCAN_1FIELD_MODE_BIT         6
#define PROSCAN_1FIELD_MODE_WID         2
#define VS_CNTL_NOISY_BIT               5
#define VS_CNTL_NOISY_WID               1
#define VS_CNTL_FF_REW_BIT              4
#define VS_CNTL_FF_REW_WID              1
#define VS_CNTL_TRICK_BIT               3
#define VS_CNTL_TRICK_WID               1
#define VS_CNTL_VCR_BIT                 2
#define VS_CNTL_VCR_WID                 1
#define VS_CNTL_BIT                     0
#define VS_CNTL_WID                     2

#define CVD2_VSYNC_TIME_CONSTANT                        ((CVD_BASE_ADD+0x39)<<2)
#define FLD_POL_BIT                     7
#define FLD_POL_WID                     1
#define FLIP_FLD_BIT                    6
#define FLIP_FLD_WID                    1
#define VEVEN_DELAYED_BIT               5
#define VEVEN_DELAYED_WID               1
#define VODD_DELAYED_BIT                4
#define VODD_DELAYED_WID                1
#define FLD_DET_MODE_BIT                2
#define FLD_DET_MODE_WID                2
#define VLOOP_TC_BIT                    0
#define VLOOP_TC_WID                    2

#define CVD2_STATUS_REGISTER1                           ((CVD_BASE_ADD+0x3A)<<2)
#define MV_COLOURSTRIPES_BIT            5
#define MV_COLOURSTRIPES_WID            3
#define MV_VBI_DETECTED_BIT             4
#define MV_VBI_DETECTED_WID             1
#define CHROMALOCK_BIT                  3
#define CHROMALOCK_WID                  1
#define VLOCK_BIT                       2
#define VLOCK_WID                       1
#define HLOCK_BIT                       1
#define HLOCK_WID                       1
#define NO_SIGNAL_BIT                   0
#define NO_SIGNAL_WID                   1

#define CVD2_STATUS_REGISTER2                           ((CVD_BASE_ADD+0x3B)<<2)
#define STATUS_COMB3D_OFF_BIT           4
#define STATUS_COMB3D_OFF_WID           1
#define BKNWT_DETECTED_BIT              3
#define BKNWT_DETECTED_WID              1
#define VNON_STD_BIT                    2
#define VNON_STD_WID                    1
#define HNON_STD_BIT                    1
#define HNON_STD_WID                    1
#define PROSCAN_DETECTED_BIT            0
#define PROSCAN_DETECTED_WID            1

#define CVD2_STATUS_REGISTER3                           ((CVD_BASE_ADD+0x3C)<<2)
#define VCR_REW_BIT                     7
#define VCR_REW_WID                     1
#define VCR_FF_BIT                      6
#define VCR_FF_WID                      1
#define VCR_TRICK_BIT                   5
#define VCR_TRICK_WID                   1
#define VCR_BIT                         4
#define VCR_WID                         1
#define NOISY_BIT                       3
#define NOISY_WID                       1
#define LINES625_DETECTED_BIT           2
#define LINES625_DETECTED_WID           1
#define SECAM_DETECTED_BIT              1
#define SECAM_DETECTED_WID              1
#define PAL_DETECTED_BIT                0
#define PAL_DETECTED_WID                1

#define CVD2_DEBUG_ANALOG                               ((CVD_BASE_ADD+0x3D)<<2)
#define MUXANALOGB_BIT                  4
#define MUXANALOGB_WID                  4
#define MUXANALOGA_BIT                  0
#define MUXANALOGA_WID                  4

#define CVD2_DEBUG_DIGITAL                              ((CVD_BASE_ADD+0x3E)<<2)
#define DBG_SYNCS_BIT                   4
#define DBG_SYNCS_WID                   1
#define MUXDIGITAL_BIT                  0
#define MUXDIGITAL_WID                  3

#define CVD2_RESET_REGISTER                             ((CVD_BASE_ADD+0x3F)<<2)
#define SOFT_RST_BIT                    0
#define SOFT_RST_WID                    1

#define CVD2_HSYNC_DTO_INC_STATUS_29_24                 ((CVD_BASE_ADD+0x70)<<2)
#define STATUS_HDTO_INC_29_24_BIT       0
#define STATUS_HDTO_INC_29_24_WID       6

#define CVD2_HSYNC_DTO_INC_STATUS_23_16                 ((CVD_BASE_ADD+0x71)<<2)
#define STATUS_HDTO_INC_23_16_BIT       0
#define STATUS_HDTO_INC_23_16_WID       8

#define CVD2_HSYNC_DTO_INC_STATUS_15_8                  ((CVD_BASE_ADD+0x72)<<2)
#define STATUS_HDTO_INC_15_8_BIT        0
#define STATUS_HDTO_INC_15_8_WID        8

#define CVD2_HSYNC_DTO_INC_STATUS_7_0                   ((CVD_BASE_ADD+0x73)<<2)
#define STATUS_HDTO_INC_7_0_BIT         0
#define STATUS_HDTO_INC_7_0_WID         8

#define CVD2_CHROMA_DTO_INC_STATUS_29_24                ((CVD_BASE_ADD+0x74)<<2)
#define STATUS_CDTO_INC_29_24_BIT       0
#define STATUS_CDTO_INC_29_24_WID       6

#define CVD2_CHROMA_DTO_INC_STATUS_23_16                ((CVD_BASE_ADD+0x75)<<2)
#define STATUS_CDTO_INC_23_16_BIT       0
#define STATUS_CDTO_INC_23_16_WID       8

#define CVD2_CHROMA_DTO_INC_STATUS_15_8                 ((CVD_BASE_ADD+0x76)<<2)
#define STATUS_CDTO_INC_15_8_BIT        0
#define STATUS_CDTO_INC_15_8_WID        8

#define CVD2_CHROMA_DTO_INC_STATUS_7_0                  ((CVD_BASE_ADD+0x77)<<2)
#define STATUS_CDTO_INC_7_0_BIT         0
#define STATUS_CDTO_INC_7_0_WID         8

#define CVD2_AGC_GAIN_STATUS_11_8                       ((CVD_BASE_ADD+0x78)<<2)
#define AGC_GAIN_11_8_BIT               0
#define AGC_GAIN_11_8_WID               4

#define CVD2_AGC_GAIN_STATUS_7_0                        ((CVD_BASE_ADD+0x79)<<2)
#define AGC_GAIN_7_0_BIT                0
#define AGC_GAIN_7_0_WID                8

#define CVD2_CHROMA_MAGNITUDE_STATUS                    ((CVD_BASE_ADD+0x7A)<<2)
#define STATUS_CMAG_BIT                 0
#define STATUS_CMAG_WID                 8

#define CVD2_CHROMA_GAIN_STATUS_13_8                    ((CVD_BASE_ADD+0x7B)<<2)
#define STATUS_CGAIN_13_8_BIT           0
#define STATUS_CGAIN_13_8_WID           6

#define CVD2_CHROMA_GAIN_STATUS_7_0                     ((CVD_BASE_ADD+0x7C)<<2)
#define STATUS_CGAIN_7_0_BIT            0
#define STATUS_CGAIN_7_0_WID            8

#define CVD2_CORDIC_FREQUENCY_STATUS                    ((CVD_BASE_ADD+0x7D)<<2)
#define STATUS_CORDIQ_FRERQ_BIT         0
#define STATUS_CORDIQ_FRERQ_WID         8

#define CVD2_SYNC_HEIGHT_STATUS                         ((CVD_BASE_ADD+0x7E)<<2)
#define STATUS_SYNC_HEIGHT_BIT          0
#define STATUS_SYNC_HEIGHT_WID          8

#define CVD2_SYNC_NOISE_STATUS                          ((CVD_BASE_ADD+0x7F)<<2)
#define STATUS_NOISE_BIT                0
#define STATUS_NOISE_WID                8

#define CVD2_COMB_FILTER_THRESHOLD1                     ((CVD_BASE_ADD+0x80)<<2)
#define SECAM_YBW_BIT                   6
#define SECAM_YBW_WID                   2
#define PEAK_RANGE_BIT                  4
#define PEAK_RANGE_WID                  2
#define PEAK_GAIN_BIT                   1
#define PEAK_GAIN_WID                   3
#define PEAK_EN_BIT                     0
#define PEAK_EN_WID                     1

#define CVD2_COMB_FILTER_CONFIG                         ((CVD_BASE_ADD+0x82)<<2)
#define AUTO_SECAM_LVL_BIT              7
#define AUTO_SECAM_LVL_WID              1
#define SV_BF_BIT                       6
#define SV_BF_WID                       1
#define PALSW_LVL_BIT                   0
#define PALSW_LVL_WID                   2

#define CVD2_COMB_LOCK_CONFIG                           ((CVD_BASE_ADD+0x83)<<2)
#define LOSE_CHROMALOCK_CNT_BIT         4
#define LOSE_CHROMALOCK_CNT_WID         4
#define LOSE_CHROMALOCK_LVL_BIT         1
#define LOSE_CHROMALOCK_LVL_WID         3
#define LOSE_CHROMALOCK_CKILL_BIT       0
#define LOSE_CHROMALOCK_CKILL_WID       1

#define CVD2_COMB_LOCK_MODE                             ((CVD_BASE_ADD+0x84)<<2)
#define LOSE_CHROMALOCK_MODE_BIT        0
#define LOSE_CHROMALOCK_MODE_WID        2

#define CVD2_NONSTANDARD_SIGNAL_STATUS_10_8             ((CVD_BASE_ADD+0x85)<<2)
#define STATUS_NSTD_10_8_BIT            0
#define STATUS_NSTD_10_8_WID            3

#define CVD2_NONSTANDARD_SIGNAL_STATUS_7_0              ((CVD_BASE_ADD+0x86)<<2)
#define STATUS_NSTD_7_0_BIT             0
#define STATUS_NSTD_7_0_WID             8

#define CVD2_REG_87                                     ((CVD_BASE_ADD+0x87)<<2)
#define CDETECT_VFILTER_SEL_BIT         6
#define CDETECT_VFILTER_SEL_WID         2
#define CDETECT_HLOCK_SEL_BIT           4
#define CDETECT_HLOCK_SEL_WID           2
#define FORCE_BW_BIT                    3
#define FORCE_BW_WID                    1
#define HSTATE_EN_SEL_BIT               2
#define HSTATE_EN_SEL_WID               1
#define HDSW_SEL_BIT                    0
#define HDSW_SEL_WID                    2

#define CVD2_COLORSTRIPE_DETECTION_CONTROL              ((CVD_BASE_ADD+0x88)<<2)
#define CSTRIPE_DET_CONT_2_BIT          2
#define CSTRIPE_DET_CONT_2_WID          1
#define CSTRIPE_DET_CONT_1_BIT          1
#define CSTRIPE_DET_CONT_1_WID          1
#define CSTRIPE_DET_CONT_0_BIT          0
#define CSTRIPE_DET_CONT_0_WID          1

#define CVD2_CHROMA_LOOPFILTER_STATE                    ((CVD_BASE_ADD+0x8A)<<2)
#define CSTATE_BIT                      1
#define CSTATE_WID                      3
#define FIXED_CSTATE_BIT                0
#define FIXED_CSTATE_WID                1

#define CVD2_CHROMA_HRESAMPLER_CONTROL                  ((CVD_BASE_ADD+0x8B)<<2)
#define HFINE_VCR_TRICK_EN_BIT          5
#define HFINE_VCR_TRICK_EN_WID          1
#define HFINE_VCR_EN_BIT                4
#define HFINE_VCR_EN_WID                1
#define HRESAMPLER_2UP_BIT              0
#define HRESAMPLER_2UP_WID              1

#define CVD2_CHARGE_PUMP_DELAY_CONTROL                  ((CVD_BASE_ADD+0x8D)<<2)
#define CPUMP_DLY_EN_BIT                7
#define CPUMP_DLY_EN_WID                1
#define CPUMP_ADJ_POL_BIT               6
#define CPUMP_ADJ_POL_WID               1
#define CPUMP_ADJ_DLY_BIT               0
#define CPUMP_ADJ_DLY_WID               6

#define CVD2_CHARGE_PUMP_ADJUSTMENT                     ((CVD_BASE_ADD+0x8E)<<2)
#define CPUMP_ADJ_BIT                   0
#define CPUMP_ADJ_WID                   8

#define CVD2_CHARGE_PUMP_DELAY                          ((CVD_BASE_ADD+0x8F)<<2)
#define CPUMP_DLY_BIT                   0
#define CPUMP_DLY_WID                   8

#define CVD2_MACROVISION_SELECTION                      ((CVD_BASE_ADD+0x90)<<2)
#define MV_COLOURSTRIPES_SEL_BIT        1
#define MV_COLOURSTRIPES_SEL_WID        1
#define MV_VBI_SEL_BIT                  0
#define MV_VBI_SEL_WID                  1

#define CVD2_CPUMP_KILL                                 ((CVD_BASE_ADD+0x91)<<2)
#define CPUMP_KILL_CR_BIT               2
#define CPUMP_KILL_CR_WID               1
#define CPUMP_KILL_CB_BIT               1
#define CPUMP_KILL_CB_WID               1
#define CPUMP_KILL_Y_BIT                0
#define CPUMP_KILL_Y_WID                1

#define CVD2_CVBS_Y_DELAY                               ((CVD_BASE_ADD+0x92)<<2)
#define CVBS_Y_DLY_BIT                  0
#define CVBS_Y_DLY_WID                  5

#define CVD2_REG_93                                     ((CVD_BASE_ADD+0x93)<<2)
#define AML_TIMER_EN_BIT                31
#define AML_TIMER_EN_WID                1
#define AML_SOFT_RST_BIT                30
#define AML_SOFT_RST_WID                1
#define AML_TIMER_BIT                   24
#define AML_TIMER_WID                   6
#define AML_ADDR_OFFSET_BIT             0
#define AML_ADDR_OFFSET_WID             23

#define CVD2_REG_94                                     ((CVD_BASE_ADD+0x94)<<2)
#define MEM_BIST_SEL_BIT                31
#define MEM_BIST_SEL_WID                1
#define EXT_RST_L_BIT                   30
#define EXT_RST_L_WID                   1
#define BIST_INC_BIT                    24
#define BIST_INC_WID                    6
#define BIST_SOFT_RST_BIT               23
#define BIST_SOFT_RST_WID               1
#define PATCH4WAITINI_BIT               16
#define PATCH4WAITINI_WID               1
#define AMLOGIC_HOLD_BIT                8
#define AMLOGIC_HOLD_WID                7
#define AML_TH_BIT                      0
#define AML_TH_WID                      5

#define CVD2_REG_95                                     ((CVD_BASE_ADD+0x95)<<2)
#define MCLKCHECK_ERR_BIT               24
#define MCLKCHECK_ERR_WID               7
#define ERR_WARNING_BIT                 18
#define ERR_WARNING_WID                 5
#define SUBID_FULLWR_BIT                16
#define SUBID_FULLWR_WID                1
#define SUBID_EMPTYRD_BIT               15
#define SUBID_EMPTYRD_WID               1
#define FB_RD_WARNING_BIT               9
#define FB_RD_WARNING_WID               6
#define WARNING_3D_AML_REG_NEW_BIT      0
#define WARNING_3D_AML_REG_NEW_WID      9

#define CVD2_REG_96                                     ((CVD_BASE_ADD+0x96)<<2)
#define AML_3D_ADDR_OFFSET_BIT          0
#define AML_3D_ADDR_OFFSET_WID          32

#define CVD2_CHARGE_PUMP_AUTO_CONTROL                   ((CVD_BASE_ADD+0xA0)<<2)
#define CPUMP_NOISY_FILTER_EN_BIT       7
#define CPUMP_NOISY_FILTER_EN_WID       1
#define CPUMP_AUTO_STIP_NOBP_BIT        6
#define CPUMP_AUTO_STIP_NOBP_WID        1
#define CPUMP_AUTO_STIP_UNLOCKED_BIT    5
#define CPUMP_AUTO_STIP_UNLOCKED_WID    1
#define CPUMP_AUTO_STIP_NO_SIGNAL_BIT   4
#define CPUMP_AUTO_STIP_NO_SIGNAL_WID   1
#define CPUMP_AUTO_STIP_NOISY_BIT       3
#define CPUMP_AUTO_STIP_NOISY_WID       1
#define CPUMP_AUTO_STIP_VACTIVE_BIT     2
#define CPUMP_AUTO_STIP_VACTIVE_WID     1
#define CPUMP_AUTO_STIP_MODE_BIT        0
#define CPUMP_AUTO_STIP_MODE_WID        2

#define CVD2_CHARGE_PUMP_FILTER_CONTROL                 ((CVD_BASE_ADD+0xA1)<<2)
#define CPUMP_VS_BLANK_FILTER_BIT       7
#define CPUMP_VS_BLANK_FILTER_WID       1
#define CPUMP_VS_SYNCMID_FILTER_BIT     6
#define CPUMP_VS_SYNCMID_FILTER_WID     1
#define CPUMP_VS_MODE_BIT               4
#define CPUMP_VS_MODE_WID               2
#define CPUMP_ACCUM_MODE_BIT            3
#define CPUMP_ACCUM_MODE_WID            1
#define CPUMP_FIXED_SYNCMID_BIT         2
#define CPUMP_FIXED_SYNCMID_WID         1
#define CPUMP_LVL_FILTER_GAIN_BIT       0
#define CPUMP_LVL_FILTER_GAIN_WID       2

#define CVD2_CHARGE_PUMP_UP_MAX                         ((CVD_BASE_ADD+0xA2)<<2)
#define CPUMP_UP_MAX_BIT                0
#define CPUMP_UP_MAX_WID                7

#define CVD2_CHARGE_PUMP_DN_MAX                         ((CVD_BASE_ADD+0xA3)<<2)
#define CPUMP_DN_MAX_BIT                0
#define CPUMP_DN_MAX_WID                7

#define CVD2_CHARGE_PUMP_UP_DIFF_MAX                    ((CVD_BASE_ADD+0xA4)<<2)
#define CPUMP_DIFF_SIGNAL_ONLY_BIT      7
#define CPUMP_DIFF_SIGNAL_ONLY_WID      1
#define CPUMP_UP_DIFF_MAX_BIT           0
#define CPUMP_UP_DIFF_MAX_WID           7

#define CVD2_CHARGE_PUMP_DN_DIFF_MAX                    ((CVD_BASE_ADD+0xA5)<<2)
#define CPUMP_DIFF_NOISY_ONLY_BIT       7
#define CPUMP_DIFF_NOISY_ONLY_WID       1
#define CPUMP_DN_DIFF_MAX_BIT           0
#define CPUMP_DN_DIFF_MAX_WID           7

#define CVD2_CHARGE_PUMP_Y_OVERRIDE                     ((CVD_BASE_ADD+0xA6)<<2)
#define CPUMP_Y_OVERRIDE_BIT            0
#define CPUMP_Y_OVERRIDE_WID            8

#define CVD2_CHARGE_PUMP_PB_OVERRIDE                    ((CVD_BASE_ADD+0xA7)<<2)
#define CPUMP_PB_OVERRIDE_BIT           0
#define CPUMP_PB_OVERRIDE_WID           8

#define CVD2_CHARGE_PUMP_PR_OVERRIDE                    ((CVD_BASE_ADD+0xA8)<<2)
#define CPUMP_PR_OVERRIDE_BIT           0
#define CPUMP_PR_OVERRIDE_WID           8

#define CVD2_DR_FREQ_11_8                               ((CVD_BASE_ADD+0xA9)<<2)
#define DR_FREQ_11_8_BIT                0
#define DR_FREQ_11_8_WID                4

#define CVD2_DR_FREQ_7_0                                ((CVD_BASE_ADD+0xAA)<<2)
#define DR_FREQ_7_0_BIT                 0
#define DR_FREQ_7_0_WID                 8

#define CVD2_DB_FREQ_11_8                               ((CVD_BASE_ADD+0xAB)<<2)
#define DB_FREQ_11_8_BIT                0
#define DB_FREQ_11_8_WID                4

#define CVD2_DB_FREQ_7_0                                ((CVD_BASE_ADD+0xAC)<<2)
#define DB_FREQ_7_0_BIT                 0
#define DB_FREQ_7_0_WID                 8

#define CVD2_2DCOMB_VCHROMA_TH                          ((CVD_BASE_ADD+0xAE)<<2)
#define VACTIVITY_EN_BIT                7
#define VACTIVITY_EN_WID                1
#define VACTIVE_ON2FRAME_BIT            6
#define VACTIVE_ON2FRAME_WID            1
#define VACTIVITY_TH_BIT                0
#define VACTIVITY_TH_WID                6

#define CVD2_2DCOMB_NOISE_TH                            ((CVD_BASE_ADD+0xAF)<<2)
#define COMB_NOISE_TH_EN_BIT            7
#define COMB_NOISE_TH_EN_WID            1
#define COMB_NOISE_TH_BIT               0
#define COMB_NOISE_TH_WID               7

#define CVD2_REG_B0                                     ((CVD_BASE_ADD+0xB0)<<2)
#define HORIZ_DIFF_CGAIN_BIT            6
#define HORIZ_DIFF_CGAIN_WID            2
#define HORIZ_DIFF_YGAIN_BIT            4
#define HORIZ_DIFF_YGAIN_WID            2
#define CHROMA_VDIFF_GAIN_BIT           2
#define CHROMA_VDIFF_GAIN_WID           2
#define LOWFREQ_VDIFF_GAIN_BIT          0
#define LOWFREQ_VDIFF_GAIN_WID          2

#define CVD2_3DCOMB_FILTER                              ((CVD_BASE_ADD+0xB1)<<2)
#define VADAP_BURST_NOISE_TH_GAIN_BIT   6
#define VADAP_BURST_NOISE_TH_GAIN_WID   2
#define BURST_NOISE_TH_GAIN_BIT         4
#define BURST_NOISE_TH_GAIN_WID         2
#define C_NOISE_TH_GAIN_BIT             2
#define C_NOISE_TH_GAIN_WID             2
#define Y_NOISE_TH_GAIN_BIT             0
#define Y_NOISE_TH_GAIN_WID             2

#define CVD2_REG_B2                                     ((CVD_BASE_ADD+0xB2)<<2)
#define LBADRGEN_RST_BIT                7
#define LBADRGEN_RST_WID                1
#define COMB2D_ONLY_BIT                 6
#define COMB2D_ONLY_WID                 1
#define ADAPTIVE_CHROMA_MODE_BIT        3
#define ADAPTIVE_CHROMA_MODE_WID        2
#define DOT_SUPPRESS_MODE_BIT           1
#define DOT_SUPPRESS_MODE_WID           1
#define MOTION_MODE_BIT                 0
#define MOTION_MODE_WID                 2

#define CVD2_2DCOMB_ADAPTIVE_GAIN_CONTROL               ((CVD_BASE_ADD+0xB3)<<2)
#define PAL3DCOMB_VACTIVE_OFFSET_BIT    7
#define PAL3DCOMB_VACTIVE_OFFSET_WID    1
#define FB_SYNC_BIT                     5
#define FB_SYNC_WID                     2
#define FB_HOLD_BIT                     4
#define FB_HOLD_WID                     1
#define FB_CTL_BIT                      3
#define FB_CTL_WID                      1
#define FLD_LATENCY_BIT                 0
#define FLD_LATENCY_WID                 3

#define CVD2_MOTION_DETECTOR_NOISE_TH                   ((CVD_BASE_ADD+0xB4)<<2)
#define MD_NOISE_TH_EN_BIT              7
#define MD_NOISE_TH_EN_WID              1
#define MD_NOISE_TH_BIT                 0
#define MD_NOISE_TH_WID                 7

#define CVD2_CHROMA_EDGE_ENHANCEMENT                    ((CVD_BASE_ADD+0xB5)<<2)
#define SCHROMA_PEAK_EN_BIT             7
#define SCHROMA_PEAK_EN_WID             1
#define SCHROMA_CORING_EN_BIT           6
#define SCHROMA_CORING_EN_WID           1
#define SCHROMA_PEAK_BIT                4
#define SCHROMA_PEAK_WID                2
#define PCHROMA_PEAK_EN_BIT             3
#define PCHROMA_PEAK_EN_WID             1
#define PCHROMA_CORING_EN_BIT           2
#define PCHROMA_CORING_EN_WID           1
#define PCHROMA_PEAK_BIT                0
#define PCHROMA_PEAK_WID                2

#define CVD2_REG_B6                                     ((CVD_BASE_ADD+0xB6)<<2)
#define LDPAUSE_TH_BIT                  4
#define LDPAUSE_TH_WID                  4
#define VF_NSTD_EN_BIT                  1
#define VF_NSTD_EN_WID                  1
#define VCR_AUTO_SWT_EN_BIT             0
#define VCR_AUTO_SWT_EN_WID             1

#define CVD2_2D_COMB_NOTCH_GAIN                         ((CVD_BASE_ADD+0xB7)<<2)
#define NOTCH_GAIN_BIT                  4
#define NOTCH_GAIN_WID                  3
#define COMB_GAIN_BIT                   0
#define COMB_GAIN_WID                   3

#define CVD2_TEMPORAL_COMB_FILTER_GAIN                  ((CVD_BASE_ADD+0xB8)<<2)
#define COMB_CORING_BIT                 4
#define COMB_CORING_WID                 4
#define TCOMB_GAIN_BIT                  0
#define TCOMB_GAIN_WID                  3

#define CVD2_ACTIVE_VSTART_FRAME_BUFFER                 ((CVD_BASE_ADD+0xBA)<<2)
#define VACTIVE_FB_START_BIT            0
#define VACTIVE_FB_START_WID            8

#define CVD2_ACTIVE_VHEIGHT_FRAME_BUFFER                ((CVD_BASE_ADD+0xBB)<<2)
#define VACTIVE_FB_HEIGHT_BIT           0
#define VACTIVE_FB_HEIGHT_WID           8

#define CVD2_HSYNC_PULSE_CONFIG                         ((CVD_BASE_ADD+0xBC)<<2)
#define HS_PULSE_WIDTH_BIT              0
#define HS_PULSE_WIDTH_WID              4

#define CVD2_CAGC_TIME_CONSTANT_CONTROL                 ((CVD_BASE_ADD+0xBD)<<2)
#define CAGC_TC_P_BIT                   6
#define CAGC_TC_P_WID                   2
#define CAGC_TC_IBIG_BIT                3
#define CAGC_TC_IBIG_WID                3
#define CAGC_TC_ISMALL_BIT              0
#define CAGC_TC_ISMALL_WID              3

#define CVD2_CAGC_CORING_FUNCTION_CONTROL               ((CVD_BASE_ADD+0xBE)<<2)
#define CAGC_CORING_TH_BIT              5
#define CAGC_CORING_TH_WID              4
#define CAGC_UNITY_GAIN_BIT             4
#define CAGC_UNITY_GAIN_WID             1
#define CAGC_CORING_BIT                 0
#define CAGC_CORING_WID                 3

#define CVD2_NEW_DCRESTORE_CNTL                         ((CVD_BASE_ADD+0xC0)<<2)
#define DCRESTORE_NO_BAD_BP_BIT         7
#define DCRESTORE_NO_BAD_BP_WID         1
#define DCRESTORE_KILL_EN_BIT           6
#define DCRESTORE_KILL_EN_WID           1
#define DCRESTORE_BP_DLY_BIT            4
#define DCRESTORE_BP_DLY_WID            2
#define SYNCMID_NOBP_EN_BIT             3
#define SYNCMID_NOBP_EN_WID             1
#define SYNCMID_FILTER_EN_BIT           2
#define SYNCMID_FILTER_EN_WID           1
#define DCRESTORE_GAIN_BIT              0
#define DCRESTORE_GAIN_WID              2

#define CVD2_DCRESTORE_ACCUM_WIDTH                      ((CVD_BASE_ADD+0xC1)<<2)
#define DCRESTORE_LPF_EN_BIT            7
#define DCRESTORE_LPF_EN_WID            1
#define DCRESTORE_KILL_EN_NOISY_BIT     6
#define DCRESTORE_KILL_EN_NOISY_WID     1
#define DCRESTORE_ACCUM_WIDTH_BIT       0
#define DCRESTORE_ACCUM_WIDTH_WID       6

#define CVD2_MANUAL_GAIN_CONTROL                        ((CVD_BASE_ADD+0xC2)<<2)
#define HMGC_BIT                        0
#define HMGC_WID                        8

#define CVD2_BACKPORCH_KILL_THRESHOLD                   ((CVD_BASE_ADD+0xC3)<<2)
#define BP_KILL_TH_BIT                  0
#define BP_KILL_TH_WID                  8

#define CVD2_DCRESTORE_HSYNC_MIDPOINT                   ((CVD_BASE_ADD+0xC4)<<2)
#define DCRESTORE_HS_HMID_BIT           0
#define DCRESTORE_HS_HMID_WID           8

#define CVD2_SYNC_HEIGHT                                ((CVD_BASE_ADD+0xC5)<<2)
#define AUTO_MIN_SYNC_HEIGHT_BIT        7
#define AUTO_MIN_SYNC_HEIGHT_WID        1
#define MIN_SYNC_HEIGHT_BIT             0
#define MIN_SYNC_HEIGHT_WID             7

#define CVD2_VSYNC_SIGNAL_THRESHOLD                     ((CVD_BASE_ADD+0xC6)<<2)
#define VS_SIGNAL_TH_BIT                2
#define VS_SIGNAL_TH_WID                6
#define VS_SIGNAL_AUTO_TH_BIT           0
#define VS_SIGNAL_AUTO_TH_WID           2

#define CVD2_VSYNC_NO_SIGNAL_THRESHOLD                  ((CVD_BASE_ADD+0xC7)<<2)
#define VS_NO_SIGNAL_TH_BIT             0
#define VS_NO_SIGNAL_TH_WID             8

#define CVD2_VSYNC_CNTL2                                ((CVD_BASE_ADD+0xC8)<<2)
#define VACTIVE_HALF_LINES_BIT          6
#define VACTIVE_HALF_LINES_WID          1
#define VDETECT_NOISE_EN_BIT            5
#define VDETECT_NOISE_EN_WID            1
#define VCRTRICK_PROSCAN_BIT            4
#define VCRTRICK_PROSCAN_WID            1
#define VEVEN_EARLY_DELAYED_BIT         3
#define VEVEN_EARLY_DELAYED_WID         1
#define VODD_EARLY_DELAYED_BIT          2
#define VODD_EARLY_DELAYED_WID          1
#define VFIELD_HOFFSET_FIXED_BIT        1
#define VFIELD_HOFFSET_FIXED_WID        1
#define VFIELD_HOFFSET_MSB_BIT          0
#define VFIELD_HOFFSET_MSB_WID          1

#define CVD2_VSYNC_POLARITY_CONTROL                     ((CVD_BASE_ADD+0xC9)<<2)
#define VFIELD_HOFFSET_LSB_BIT          0
#define VFIELD_HOFFSET_LSB_WID          8

#define CVD2_VBI_HDETECT_CNTL                           ((CVD_BASE_ADD+0xCA)<<2)
#define NO_HSYNCS_MODE_BIT              6
#define NO_HSYNCS_MODE_WID              2
#define MANY_HSYNCS_MODE_BIT            5
#define MANY_HSYNCS_MODE_WID            1
#define DUAL_HEDGE_DIS_BIT              4
#define DUAL_HEDGE_DIS_WID              1
#define DUAL_HEDGE_AUTO_WIDTH_BIT       3
#define DUAL_HEDGE_AUTO_WIDTH_WID       1
#define DUAL_FINE_HEDGE_VBI_BIT         2
#define DUAL_FINE_HEDGE_VBI_WID         1
#define DUAL_COARSE_HEDGE_VBI_BIT       0
#define DUAL_COARSE_HEDGE_VBI_WID       2

#define CVD2_MV_PSEUDO_SYNC_RISING_START                ((CVD_BASE_ADD+0xCB)<<2)
#define VCR_STATE2_LONG_BIT             7
#define VCR_STATE2_LONG_WID             1
#define SLOW_HDSW_BIT                   6
#define SLOW_HDSW_WID                   1
#define HS_RISING_START_BIT             0
#define HS_RISING_START_WID             6

#define CVD2_MV_PSEUDO_SYNC_RISING_END                  ((CVD_BASE_ADD+0xCC)<<2)
#define NO_HSYNCS_WEAK_BIT              7
#define NO_HSYNCS_WEAK_WID              1
#define DISABLE_HDSW_WEAK_BIT           6
#define DISABLE_HDSW_WEAK_WID           1
#define CVD2_MV_HS_RISING_END_BIT       0
#define CVD2_MV_HS_RISING_END_WID       6

#define CVD2_REG_CD                                     ((CVD_BASE_ADD+0xCD)<<2)
#define VACTIVE_HDSW_MODE_BIT           6
#define VACTIVE_HDSW_MODE_WID           2
#define DISABLE_HDSW_MODE_BIT           4
#define DISABLE_HDSW_MODE_WID           2
#define HS_FALLING_FILTER_BIT           3
#define HS_FALLING_FILTER_WID           1
#define NO_HSYNCS_NOISY_BIT             2
#define NO_HSYNCS_NOISY_WID             1
#define HLOOP_RANGE_BIT                 0
#define HLOOP_RANGE_WID                 2

#define CVD2_BIG_HLUMA_TH                               ((CVD_BASE_ADD+0xCE)<<2)
#define MD_C_NOISE_TH_EN_BIT            7
#define MD_C_NOISE_TH_EN_WID            1
#define MD_C_NOISE_TH_BIT               0
#define MD_C_NOISE_TH_WID               7

#define CVD2_MOTION_DETECTOR_CONTROL                    ((CVD_BASE_ADD+0xD0)<<2)
#define MD_CF_ACTIVITY_EN_BIT           6
#define MD_CF_ACTIVITY_EN_WID           2
#define MD_HF_MAX_BIT                   5
#define MD_HF_MAX_WID                   1
#define MD_HF_SAD_BIT                   3
#define MD_HF_SAD_WID                   2
#define MD_LF_SAD_BIT                   2
#define MD_LF_SAD_WID                   1
#define MD_LF_SHIFT_BIT                 0
#define MD_LF_SHIFT_WID                 2

#define CVD2_MD_CF_LACTIVITY_LOW                        ((CVD_BASE_ADD+0xD1)<<2)
#define MD_CF_LACTIVITY_LOW_BIT         0
#define MD_CF_LACTIVITY_LOW_WID         8

#define CVD2_MD_CF_CACTIVITY_LOW                        ((CVD_BASE_ADD+0xD2)<<2)
#define MD_CF_CACTIVITY_LOW_BIT         0
#define MD_CF_CACTIVITY_LOW_WID         8

#define CVD2_MD_CF_LACTIVITY_HIGH                       ((CVD_BASE_ADD+0xD3)<<2)
#define MD_CF_LACTIVITY_HIGH_BIT        0
#define MD_CF_LACTIVITY_HIGH_WID        8

#define CVD2_MD_CF_CACTIVITY_HIGH                       ((CVD_BASE_ADD+0xD4)<<2)
#define MD_CF_CACTIVITY_HIGH_BIT        0
#define MD_CF_CACTIVITY_HIGH_WID        8

#define CVD2_MD_K_THRESHOLD                             ((CVD_BASE_ADD+0xD5)<<2)
#define MD_K_TH_BIT                     0
#define MD_K_TH_WID                     8

#define CVD2_CHROMA_LEVEL                               ((CVD_BASE_ADD+0xD6)<<2)
#define CHROMA_LVL_BIT                  0
#define CHROMA_LVL_WID                  8

#define CVD2_SPATIAL_LUMA_LEVEL                         ((CVD_BASE_ADD+0xD7)<<2)
#define SPATIAL_LUMA_LVL_BIT            0
#define SPATIAL_LUMA_LVL_WID            8

#define CVD2_SPATIAL_CHROMA_LEVEL                       ((CVD_BASE_ADD+0xD8)<<2)
#define HF_LUMA_CHROMA_OFFSET_BIT       0
#define HF_LUMA_CHROMA_OFFSET_WID       8

#define CVD2_TCOMB_CHROMA_LEVEL                         ((CVD_BASE_ADD+0xD9)<<2)
#define TCOMB_CHROMA_LVL_BIT            0
#define TCOMB_CHROMA_LVL_WID            8

#define CVD2_FMDLF_TH                                   ((CVD_BASE_ADD+0xDA)<<2)
#define LF_LUMA_OFFSET_BIT              0
#define LF_LUMA_OFFSET_WID              8

#define CVD2_CHROMA_ACTIVITY_LEVEL                      ((CVD_BASE_ADD+0xDB)<<2)
#define CHROMA_ACTIVITY_LVL_BIT         0
#define CHROMA_ACTIVITY_LVL_WID         8

#define CVD2_SECAM_FREQ_OFFSET_RANGE                    ((CVD_BASE_ADD+0xDC)<<2)
#define FREQ_OFFSET_RANGE_BIT           0
#define FREQ_OFFSET_RANGE_WID           8

#define CVD2_SECAM_FLAG_THRESHOLD                       ((CVD_BASE_ADD+0xDE)<<2)
#define AVG_FREQ_RANGE_BIT              6
#define AVG_FREQ_RANGE_WID              2
#define ISSECAM_TH_BIT                  0
#define ISSECAM_TH_WID                  6

#define CVD2_3DCOMB_MOTION_STATUS_31_24                 ((CVD_BASE_ADD+0xE0)<<2)
#define STATUS_COMB3D_MOTION_31_24_BIT  0
#define STATUS_COMB3D_MOTION_31_24_WID  8

#define CVD2_3DCOMB_MOTION_STATUS_23_16                 ((CVD_BASE_ADD+0xE1)<<2)
#define STATUS_COMB3D_MOTION_23_16_BIT  0
#define STATUS_COMB3D_MOTION_23_16_WID  8

#define CVD2_3DCOMB_MOTION_STATUS_15_8                  ((CVD_BASE_ADD+0xE2)<<2)
#define STATUS_COMB3D_MOTION_15_8_BIT   0
#define STATUS_COMB3D_MOTION_15_8_WID   8

#define CVD2_3DCOMB_MOTION_STATUS_7_0                   ((CVD_BASE_ADD+0xE3)<<2)
#define STATUS_COMB3D_MOTION_7_0_BIT    0
#define STATUS_COMB3D_MOTION_7_0_WID    8

#define CVD2_HACTIVE_MD_START                           ((CVD_BASE_ADD+0xE4)<<2)
#define HACTIVE_MD_START_BIT            0
#define HACTIVE_MD_START_WID            8

#define CVD2_HACTIVE_MD_WIDTH                           ((CVD_BASE_ADD+0xE5)<<2)
#define HACTIVE_MD_WIDTH_BIT            0
#define HACTIVE_MD_WIDTH_WID            8

#define CVD2_REG_E6                                     ((CVD_BASE_ADD+0xE6)<<2)
#define STATUS_VLINES_BIT               0
#define STATUS_VLINES_WID               8

#define CVD2_MOTION_CONFIG                              ((CVD_BASE_ADD+0xE7)<<2)
#define MOTION_CONFIG_BIT               0
#define MOTION_CONFIG_WID               8

#define CVD2_CHROMA_BW_MOTION                           ((CVD_BASE_ADD+0xE8)<<2)
#define CHROMA_BW_MOTION_TH_BIT         0
#define CHROMA_BW_MOTION_TH_WID         8

#define CVD2_FLAT_LUMA_SHIFT                            ((CVD_BASE_ADD+0xE9)<<2)
#define FLAT_CHROMA_SHIFT_BIT           6
#define FLAT_CHROMA_SHIFT_WID           2
#define FLAT_LUMA_MODE_BIT              4
#define FLAT_LUMA_MODE_WID              2
#define STATUS_MOTION_MODE_BIT          2
#define STATUS_MOTION_MODE_WID          2
#define CHROMA_BW_MOTION_BIT            0
#define CHROMA_BW_MOTION_WID            2

#define CVD2_FRAME_MOTION_TH                            ((CVD_BASE_ADD+0xEA)<<2)
#define FRAME_MOTION_TH_BIT             0
#define FRAME_MOTION_TH_WID             8

#define CVD2_FLAT_LUMA_OFFSET                           ((CVD_BASE_ADD+0xEB)<<2)
#define FLAT_LUMA_OFFSET_BIT            0
#define FLAT_LUMA_OFFSET_WID            8

#define CVD2_FLAT_CHROMA_OFFSET                         ((CVD_BASE_ADD+0xEC)<<2)
#define FLAT_CHROMA_OFFSET_BIT          0
#define FLAT_CHROMA_OFFSET_WID          8

#define CVD2_CF_FLAT_MOTION_SHIFT                       ((CVD_BASE_ADD+0xED)<<2)
#define CF_FLAT_MOTION_SHIFT_BIT        2
#define CF_FLAT_MOTION_SHIFT_WID        2
#define MOTION_C_MODE_BIT               0
#define MOTION_C_MODE_WID               2

#define CVD2_MOTION_DEBUG                               ((CVD_BASE_ADD+0xEE)<<2)
#define MOTION_DEBUG_BIT                0
#define MOTION_DEBUG_WID                8

#define CVD2_PHASE_OFFSE_RANGE                          ((CVD_BASE_ADD+0xF0)<<2)
#define PHASE_OFFSET_RANGE_BIT          0
#define PHASE_OFFSET_RANGE_WID          8

#define CVD2_PAL_DETECTION_THRESHOLD                    ((CVD_BASE_ADD+0xF1)<<2)
#define PAL_DET_TH_BIT                  0
#define PAL_DET_TH_WID                  8

#define CVD2_CORDIC_FREQUENCY_GATE_START                ((CVD_BASE_ADD+0xF2)<<2)
#define CORDIC_GATE_START_BIT           0
#define CORDIC_GATE_START_WID           8

#define CVD2_CORDIC_FREQUENCY_GATE_END                  ((CVD_BASE_ADD+0xF3)<<2)
#define CORDIC_GATE_END_BIT             0
#define CORDIC_GATE_END_WID             8

#define CVD2_ADC_CPUMP_SWAP                             ((CVD_BASE_ADD+0xF4)<<2)
#define PAL3TAP_ONLY_C_BIT              7
#define PAL3TAP_ONLY_C_WID              1
#define PAL3TAP_ONLY_Y_BIT              6
#define PAL3TAP_ONLY_Y_WID              1
#define ADC_CPUMP_SWAP_BIT              0
#define ADC_CPUMP_SWAP_WID              6

#define CVD2_COMB3D_CONFIG                              ((CVD_BASE_ADD+0xF9)<<2)
#define VBI_FIXGATE_EN_BIT              0
#define VBI_FIXGATE_EN_WID              1

#define CVD2_REG_FA                                     ((CVD_BASE_ADD+0xFA)<<2)
#define VLINES_SEL_BIT                  6
#define VLINES_SEL_WID                  1
#define UV_FILTER_BYPASS_BIT            5
#define UV_FILTER_BYPASS_WID            1
#define ADC_CHROMA_FOR_TB_BIT           4
#define ADC_CHROMA_FOR_TB_WID           1
#define ADC_SV_CHROMA_SEL_BIT           3
#define ADC_SV_CHROMA_SEL_WID           1
#define ADC_CV_CHROMA_SEL_BIT           0
#define ADC_CV_CHROMA_SEL_WID           2

#define CVD2_CAGC_GATE_START                            ((CVD_BASE_ADD+0xFB)<<2)
#define CAGC_GATE_START_BIT             0
#define CAGC_GATE_START_WID             8

#define CVD2_CAGC_GATE_END                              ((CVD_BASE_ADD+0xFC)<<2)
#define CAGC_GATE_END_BIT               0
#define CAGC_GATE_END_WID               8

#define CVD2_CKILL_LEVEL_15_8                           ((CVD_BASE_ADD+0xFD)<<2)
#define CKILL_15_8_BIT                  0
#define CKILL_15_8_WID                  8

#define CVD2_CKILL_LEVEL_7_0                            ((CVD_BASE_ADD+0xFE)<<2)
#define CKILL_7_0_BIT                   0
#define CKILL_7_0_WID                   8

/* VBI decoder */
#define CVD2_VBI_FRAME_CODE_CTL                         ((CVD_BASE_ADD+0x40)<<2)
#define VBI_LPF_BW_BIT                  6
#define VBI_LPF_BW_WID                  2
#define CC_SHORT_START_BIT              5
#define CC_SHORT_START_WID              1
#define VBI_MUXOUT_BIT                  4
#define VBI_MUXOUT_WID                  1
#define VBI_HSYNCOUT_BIT                3
#define VBI_HSYNCOUT_WID                1
#define ADAP_SLVL_EN_BIT                2
#define ADAP_SLVL_EN_WID                1
#define VBI_ST_ERR_IGNORED_BIT          1
#define VBI_ST_ERR_IGNORED_WID          1
#define VBI_EN_BIT                      0
#define VBI_EN_WID                      1

#define CVD2_VBI_TT_FRAME_CODE_CTL                       ((CVD_BASE_ADD+0x41)<<2)
#define START_CODE_BIT                  0
#define START_CODE_WID                  8

#define CVD2_VBI_DATA_HLVL                               ((CVD_BASE_ADD+0x42)<<2)
#define VBI_DATA_HLVL_BIT               0
#define VBI_DATA_HLVL_WID               8

#define CVD2_VBI_DATA_TYPE_LINE6                         ((CVD_BASE_ADD+0x6A)<<2)
#define VBIL6E_BIT                      4
#define VBIL6E_WID                      4
#define VBIL6O_BIT                      0
#define VBIL6O_WID                      4

#define CVD2_VBI_DATA_TYPE_LINE7                         ((CVD_BASE_ADD+0x43)<<2)
#define VBIL7E_BIT                      4
#define VBIL7E_WID                      4
#define VBIL7O_BIT                      0
#define VBIL7O_WID                      4

#define CVD2_VBI_DATA_TYPE_LINE8                         ((CVD_BASE_ADD+0x44)<<2)
#define VBIL8E_BIT                      4
#define VBIL8E_WID                      4
#define VBIL8O_BIT                      0
#define VBIL8O_WID                      4

#define CVD2_VBI_DATA_TYPE_LINE9                         ((CVD_BASE_ADD+0x45)<<2)
#define VBIL9E_BIT                      4
#define VBIL9E_WID                      4
#define VBIL9O_BIT                      0
#define VBIL9O_WID                      4

#define CVD2_VBI_DATA_TYPE_LINE10                        ((CVD_BASE_ADD+0x46)<<2)
#define VBIL10E_BIT                     4
#define VBIL10E_WID                     4
#define VBIL10O_BIT                     0
#define VBIL10O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE11                        ((CVD_BASE_ADD+0x47)<<2)
#define VBIL11E_BIT                     4
#define VBIL11E_WID                     4
#define VBIL11O_BIT                     0
#define VBIL11O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE12                        ((CVD_BASE_ADD+0x48)<<2)
#define VBIL12E_BIT                     4
#define VBIL12E_WID                     4
#define VBIL12O_BIT                     0
#define VBIL12O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE13                        ((CVD_BASE_ADD+0x49)<<2)
#define VBIL13E_BIT                     4
#define VBIL13E_WID                     4
#define VBIL13O_BIT                     0
#define VBIL13O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE14                        ((CVD_BASE_ADD+0x4A)<<2)
#define VBIL14E_BIT                     4
#define VBIL14E_WID                     4
#define VBIL14O_BIT                     0
#define VBIL14O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE15                        ((CVD_BASE_ADD+0x4B)<<2)
#define VBIL15E_BIT                     4
#define VBIL15E_WID                     4
#define VBIL15O_BIT                     0
#define VBIL15O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE16                        ((CVD_BASE_ADD+0x4C)<<2)
#define VBIL16E_BIT                     4
#define VBIL16E_WID                     4
#define VBIL16O_BIT                     0
#define VBIL16O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE17                        ((CVD_BASE_ADD+0x4D)<<2)
#define VBIL17E_BIT                     4
#define VBIL17E_WID                     4
#define VBIL17O_BIT                     0
#define VBIL17O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE18                        ((CVD_BASE_ADD+0x4E)<<2)
#define VBIL18E_BIT                     4
#define VBIL18E_WID                     4
#define VBIL18O_BIT                     0
#define VBIL18O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE19                        ((CVD_BASE_ADD+0x4F)<<2)
#define VBIL19E_BIT                     4
#define VBIL19E_WID                     4
#define VBIL19O_BIT                     0
#define VBIL19O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE20                        ((CVD_BASE_ADD+0x50)<<2)
#define VBIL20E_BIT                     4
#define VBIL20E_WID                     4
#define VBIL20O_BIT                     0
#define VBIL20O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE21                        ((CVD_BASE_ADD+0x51)<<2)
#define VBIL21E_BIT                     4
#define VBIL21E_WID                     4
#define VBIL21O_BIT                     0
#define VBIL21O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE22                        ((CVD_BASE_ADD+0x52)<<2)
#define VBIL22E_BIT                     4
#define VBIL22E_WID                     4
#define VBIL22O_BIT                     0
#define VBIL22O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE23                        ((CVD_BASE_ADD+0x53)<<2)
#define VBIL23E_BIT                     4
#define VBIL23E_WID                     4
#define VBIL23O_BIT                     0
#define VBIL23O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE24                        ((CVD_BASE_ADD+0x54)<<2)
#define VBIL24E_BIT                     4
#define VBIL24E_WID                     4
#define VBIL24O_BIT                     0
#define VBIL24O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE25                        ((CVD_BASE_ADD+0x55)<<2)
#define VBIL25E_BIT                     4
#define VBIL25E_WID                     4
#define VBIL25O_BIT                     0
#define VBIL25O_WID                     4

#define CVD2_VBI_DATA_TYPE_LINE26                        ((CVD_BASE_ADD+0x56)<<2)
#define VBIL26E_BIT                     4
#define VBIL26E_WID                     4
#define VBIL26O_BIT                     0
#define VBIL26O_WID                     4

#define CVD2_VBI_SLIER_MODE_SEL                          ((CVD_BASE_ADD+0x57)<<2)
#define VPS_SLICER_MODE_BIT             6
#define VPS_SLICER_MODE_WID             2
#define WSS_SLICER_MODE_BIT             4
#define WSS_SLICER_MODE_WID             2
#define TT_SLICER_MODE_BIT              2
#define TT_SLICER_MODE_WID              2
#define CC_SLICER_MODE_BIT              0
#define CC_SLICER_MODE_WID              2

#define CVD2_VBI_LPF_FINE_GAIN                           ((CVD_BASE_ADD+0x58)<<2)
#define VPS_LPF_FINE_GAIN_BIT           6
#define VPS_LPF_FINE_GAIN_WID           2
#define WSS_LPF_FINE_GAIN_BIT           4
#define WSS_LPF_FINE_GAIN_WID           2
#define TT_LPF_FINE_GAIN_BIT            2
#define TT_LPF_FINE_GAIN_WID            2
#define CC_LPF_FINE_GAIN_BIT            0
#define CC_LPF_FINE_GAIN_WID            2

#define CVD2_VBI_CC_DTO_MSB                              ((CVD_BASE_ADD+0x59)<<2)
#define CC_DTO_MSB_BIT                  0
#define CC_DTO_MSB_WID                  8

#define CVD2_VBI_CC_DTO_LSB                              ((CVD_BASE_ADD+0x5A)<<2)
#define CC_DTO_LSB_BIT                  0
#define CC_DTO_LSB_WID                  8

#define CVD2_VBI_TT_DTO_MSB                              ((CVD_BASE_ADD+0x5B)<<2)
#define TT_DTO_MSB_BIT                  0
#define TT_DTO_MSB_WID                  8

#define CVD2_VBI_TT_DTO_LSB                              ((CVD_BASE_ADD+0x5C)<<2)
#define TT_DTO_LSB_BIT                  0
#define TT_DTO_LSB_WID                  8

#define CVD2_VBI_WSS_DTO_MSB                             ((CVD_BASE_ADD+0x5D)<<2)
#define WSS_DTO_MSB_BIT                 0
#define WSS_DTO_MSB_WID                 8

#define CVD2_VBI_WSS_DTO_LSB                             ((CVD_BASE_ADD+0x5E)<<2)
#define WSS_DTO_LSB_BIT                 0
#define WSS_DTO_LSB_WID                 8

#define CVD2_VBI_VPS_DTO_MSB                             ((CVD_BASE_ADD+0x5F)<<2)
#define VPS_DTO_MSB_BIT                 0
#define VPS_DTO_MSB_WID                 8

#define CVD2_VBI_VPS_DTO_LSB                             ((CVD_BASE_ADD+0x60)<<2)
#define VPS_DTO_LSB_BIT                 0
#define VPS_DTO_LSB_WID                 8

#define CVD2_VBI_FRAME_START                             ((CVD_BASE_ADD+0x61)<<2)
#define VPS_FRAME_START_BIT             6
#define VPS_FRAME_START_WID             2
#define TT_FRAME_START_BIT              2
#define TT_FRAME_START_WID              2
#define WSS_FRAME_START_BIT             4
#define WSS_FRAME_START_WID             2
#define CC_FRAME_START_BIT              0
#define CC_FRAME_START_WID              2

#define CVD2_VBI_CC_DATA1                                ((CVD_BASE_ADD+0x62)<<2)
#define CC_DATA0_BIT                    0
#define CC_DATA0_WID                    8

#define CVD2_VBI_CC_DATA2                                ((CVD_BASE_ADD+0x63)<<2)
#define CC_DATA1_BIT                    0
#define CC_DATA1_WID                    8

#define CVD2_VBI_WSSJ_DELTA_AMPL                         ((CVD_BASE_ADD+0x64)<<2)
#define WSSJ_DELTA_AMPL_BIT             0
#define WSSJ_DELTA_AMPL_WID             8

#define CVD2_VBI_DATA_STATUS                             ((CVD_BASE_ADD+0x65)<<2)


#define CVD2_VBI_CC_LPF                                  ((CVD_BASE_ADD+0x66)<<2)
#define CC_LPFIL_TRACK_GAIN_BIT         4
#define CC_LPFIL_TRACK_GAIN_WID         8
#define CC_LPFIL_ACQ_GAIN_BIT           0
#define CC_LPFIL_ACQ_GAIN_WID           4

#define CVD2_VBI_TT_LPF                                  ((CVD_BASE_ADD+0x67)<<2)
#define TT_LPFIL_TRACK_GAIN_BIT         4
#define TT_LPFIL_TRACK_GAIN_WID         8
#define TT_LPFIL_ACQ_GAIN_BIT           0
#define TT_LPFIL_ACQ_GAIN_WID           4

#define CVD2_VBI_WSS_LPF                                  ((CVD_BASE_ADD+0x68)<<2)
#define WSS_LPFIL_TRACK_GAIN_BIT        4
#define WSS_LPFIL_TRACK_GAIN_WID        8
#define WSS_LPFIL_ACQ_GAIN_BIT          0
#define WSS_LPFIL_ACQ_GAIN_WID          4

#define CVD2_VBI_VPS_LPF                                  ((CVD_BASE_ADD+0x69)<<2)
#define VPS_LPFIL_TRACK_GAIN_BIT        4
#define VPS_LPFIL_TRACK_GAIN_WID        8
#define VPS_LPFIL_ACQ_GAIN_BIT          0
#define VPS_LPFIL_ACQ_GAIN_WID          4

//#define CVD2_VBI_DATA_TYPE_LINE6                         ((CVD_BASE_ADD+0x6A)<<2)
//    #define VBIL6E_BIT                      4
//    #define VBIL6E_WID                      4
//    #define VBIL6O_BIT                      0
//    #define VBIL6O_WID                      4

#define CVD2_VBI_CC_RUNIN_ACCUM_AMPLF                      ((CVD_BASE_ADD+0x6B)<<2)
#define CC_RUNIN_ACCUM_AMPLF_BIT         0
#define CC_RUNIN_ACCUM_AMPLF_WID         8

#define CVD2_VBI_TT_RUNIN_ACCUM_AMPLF                      ((CVD_BASE_ADD+0x6C)<<2)
#define TT_RUNIN_ACCUM_AMPLF_BIT         0
#define TT_RUNIN_ACCUM_AMPLF_WID         8

#define CVD2_VBI_WSS_DATA2                                 ((CVD_BASE_ADD+0x6D)<<2)
#define WSSDATA2_BIT                     0
#define WSSDATA2_WID                     8

#define CVD2_VBI_WSS_DATA1                                 ((CVD_BASE_ADD+0x6E)<<2)
#define WSSDATA1_BIT                     0
#define WSSDATA1_WID                     8

#define CVD2_VBI_WSS_DATA0                                 ((CVD_BASE_ADD+0x6F)<<2)
#define WSSDATA0_BIT                     0
#define WSSDATA0_WID                     8

#define CVD2_VBI_CC_START                                  ((CVD_BASE_ADD+0xF5)<<2)
#define CC_START_BIT                     0
#define CC_START_WID                     8

#define CVD2_VBI_WSS_START                                 ((CVD_BASE_ADD+0xF6)<<2)
#define WSS_START_BIT                    0
#define WSS_START_WID                    8

#define CVD2_VBI_TT_START                                  ((CVD_BASE_ADD+0xF7)<<2)
#define TT_START_BIT                     0
#define TT_START_WID                     8

#define CVD2_VBI_VPS_START                                 ((CVD_BASE_ADD+0xF8)<<2)
#define VPS_START_BIT                    0
#define VPS_START_WID                    8

#define CVD2_VBI_CONTROL                                   ((CVD_BASE_ADD+0xF9)<<2)
#define VBI_FIXGATE_EN_BIT               0
#define VBI_FIXGATE_EN_WID               1

#endif  // _TVAFE_REG_H
