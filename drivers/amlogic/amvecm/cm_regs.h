/*
 * Color Management
 * registers' definition only access-able by port registers VPP_CHROMA_ADDR_PORT & VPP_CHROMA_DATA_PORT
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

#ifndef __CM_REG_H
#define __CM_REG_H

// *****************************************************************************
// ******** COLOR MANAGEMENT INDIRECT REGISTERS ********
// *****************************************************************************

//#define CHROMA_GAIN_REG00   0x00
//#define CHROMA_GAIN_REG01   0x06
//#define CHROMA_GAIN_REG02   0x0C
//#define CHROMA_GAIN_REG03   0x12
//#define CHROMA_GAIN_REG04   0x18
//#define CHROMA_GAIN_REG05   0x1E
//#define CHROMA_GAIN_REG06   0x24
//#define CHROMA_GAIN_REG07   0x2A
#define SAT_EN_BIT          31
#define SAT_EN_WID           1
#define SAT_INC_BIT         27
#define SAT_INC_WID          1
#define SAT_CENTRAL_EN_BIT  25
#define SAT_CENTRAL_EN_WID   2
#define SAT_SHAPE_BIT       24
#define SAT_SHAPE_WID        1
#define SAT_GAIN_BIT        16
#define SAT_GAIN_WID         8
#define HUE_EN_BIT          15
#define HUE_EN_WID           1
#define HUE_CLOCKWISE_BIT   11
#define HUE_CLOCKWISE_WID    1
#define HUE_CENTRAL_EN_BIT   9
#define HUE_CENTRAL_EN_WID   2
#define HUE_SHAPE_BIT        8
#define HUE_SHAPE_WID        1
#define HUE_GAIN_BIT         0
#define HUE_GAIN_WID         8

//#define HUE_HUE_RANGE_REG00 0x01
//#define HUE_HUE_RANGE_REG01 0x07
//#define HUE_HUE_RANGE_REG02 0x0D
//#define HUE_HUE_RANGE_REG03 0x13
//#define HUE_HUE_RANGE_REG04 0x19
//#define HUE_HUE_RANGE_REG05 0x1F
//#define HUE_HUE_RANGE_REG06 0x25
//#define HUE_HUE_RANGE_REG07 0x2B
#define HUE_SHF_RAN_BIT     16
#define HUE_SHF_RAN_WID      8
#define SYM_EN_BIT          15
#define SYM_EN_WID           1
#define HUE_SHF_STA_BIT      0
#define HUE_SHF_STA_WID     15

//#define HUE_RANGE_INV_REG00 0x02
//#define HUE_RANGE_INV_REG01 0x08
//#define HUE_RANGE_INV_REG02 0x0E
//#define HUE_RANGE_INV_REG03 0x14
//#define HUE_RANGE_INV_REG04 0x1A
//#define HUE_RANGE_INV_REG05 0x20
//#define HUE_RANGE_INV_REG06 0x26
//#define HUE_RANGE_INV_REG07 0x2C
#define HUE_SHF_RAN_INV_BIT  0
#define HUE_SHF_RAN_INV_WID 16

//#define HUE_LUM_RANGE_REG00 0x03
//#define HUE_LUM_RANGE_REG01 0x09
//#define HUE_LUM_RANGE_REG02 0x0F
//#define HUE_LUM_RANGE_REG03 0x15
//#define HUE_LUM_RANGE_REG04 0x1B
//#define HUE_LUM_RANGE_REG05 0x21
//#define HUE_LUM_RANGE_REG06 0x27
//#define HUE_LUM_RANGE_REG07 0x2D
//  for belowing each low, high, low_slope, high_slope group:
//            a_____________b
//            /             \               a = low  + 2^low_slope
//           /               \              b = high - 2^high_slope
//          /                 \             low_slope <= 7; high_slope <= 7
//         /                   \            b >= a
//  ______/_____________________\________
//       low                    high
#define SAT_LUM_L_BIT       24
#define SAT_LUM_L_WID        8
#define HUE_LUM_H_SLOPE_BIT 20
#define HUE_LUM_H_SLOPE_WID  4
#define HUE_LUM_L_SLOPE_BIT 16
#define HUE_LUM_L_SLOPE_WID  4
#define HUE_LUM_H_BIT        8
#define HUE_LUM_H_WID        8
#define HUE_LUM_L_BIT        0
#define HUE_LUM_L_WID        8

//#define HUE_SAT_RANGE_REG00 0x04
//#define HUE_SAT_RANGE_REG01 0x0A
//#define HUE_SAT_RANGE_REG02 0x10
//#define HUE_SAT_RANGE_REG03 0x16
//#define HUE_SAT_RANGE_REG04 0x1C
//#define HUE_SAT_RANGE_REG05 0x22
//#define HUE_SAT_RANGE_REG06 0x28
//#define HUE_SAT_RANGE_REG07 0x2E
#define SAT_LUM_H_BIT       24
#define SAT_LUM_H_WID        8
#define HUE_SAT_H_SLOPE_BIT 20
#define HUE_SAT_H_SLOPE_WID  4
#define HUE_SAT_L_SLOPE_BIT 16
#define HUE_SAT_L_SLOPE_WID  4
#define HUE_SAT_H_BIT        8
#define HUE_SAT_H_WID        8
#define HUE_SAT_L_BIT        0
#define HUE_SAT_L_WID        8

//#define SAT_SAT_RANGE_REG00 0x05
//#define SAT_SAT_RANGE_REG01 0x0B
//#define SAT_SAT_RANGE_REG02 0x11
//#define SAT_SAT_RANGE_REG03 0x17
//#define SAT_SAT_RANGE_REG04 0x1D
//#define SAT_SAT_RANGE_REG05 0x23
//#define SAT_SAT_RANGE_REG06 0x29
//#define SAT_SAT_RANGE_REG07 0x2F
#define SAT_LUM_H_SLOPE_BIT 28
#define SAT_LUM_H_SLOPE_WID  4
#define SAT_LUM_L_SLOPE_BIT 24
#define SAT_LUM_L_SLOPE_WID  4
#define SAT_SAT_H_SLOPE_BIT 20
#define SAT_SAT_H_SLOPE_WID  4
#define SAT_SAT_L_SLOPE_BIT 16
#define SAT_SAT_L_SLOPE_WID  4
#define SAT_SAT_H_BIT        8
#define SAT_SAT_H_WID        8
#define SAT_SAT_L_BIT        0
#define SAT_SAT_L_WID        8

//#define REG_CHROMA_CONTROL  0x30
#define CHROMA_EN_BIT       31
#define CHROMA_EN_WID        1
#if defined(CONFIG_ARCH_MESON)
//1'b0: demo adjust on right, 1'b1: demo adjust on left
#elif defined(CONFIG_ARCH_MESON2)
//2'b00: demo adjust on top, 2'b01: demo adjust on bottom
//2'b10: demo adjust on left,2'b11: demo adjust on right
#endif
#define CM_DEMO_POS_BIT        22
#define CM_DEMO_POS_WID         2
#define DEMO_HLIGHT_ADJ_BIT 21
#define DEMO_HLIGHT_ADJ_WID  1
#define DEMO_EN_BIT         20
#define DEMO_EN_WID          1
#define CM_DEMO_WID_BIT         8
#define CM_DEMO_WID_WID        12
#define SAT_SEL_BIT          6
#define SAT_SEL_WID          1
#define UV_ADJ_EN_BIT        5
#define UV_ADJ_EN_WID        1
#define RGB_TO_HUE_EN_BIT    2
#define RGB_TO_HUE_EN_WID    1
//2'b00: 601(16-235)  2'b01: 709(16-235)
//2'b10: 601(0-255)   2'b11: 709(0-255)
#define CSC_SEL_BIT          0
#define CSC_SEL_WID          2

#if defined(CONFIG_ARCH_MESON2)
//#define REG_DEMO_CENTER_BAR   0x31   // default 32h'0
#define CM_CBAR_EN_BIT      31  //center bar enable
#define CM_CBAR_EN_WID       1
#define CM_CBAR_WID_BIT     24  //center bar width    (*2)
#define CM_CBAR_WID_WID      4
#define CM_CBAR_CR_BIT      16  //center bar Cr       (*4)
#define CM_CBAR_CR_WID       8
#define CM_CBAR_CB_BIT       8  //center bar Cb       (*4)
#define CM_CBAR_CB_WID       8
#define CM_CBAR_Y_BIT        0  //center bar y        (*4)
#define CM_CBAR_Y_WID        8
#endif

#endif  // _CM_REG_H
