/*
 * Copyright 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */
#ifndef SMU13_DRIVER_IF_DCN32_H
#define SMU13_DRIVER_IF_DCN32_H

// *** IMPORTANT ***
// PMFW TEAM: Always increment the interface version on any change to this file
#define SMU13_DRIVER_IF_VERSION  0x18

//Only Clks that have DPM descriptors are listed here
typedef enum {
  PPCLK_GFXCLK = 0,
  PPCLK_SOCCLK,
  PPCLK_UCLK,
  PPCLK_FCLK,
  PPCLK_DCLK_0,
  PPCLK_VCLK_0,
  PPCLK_DCLK_1,
  PPCLK_VCLK_1,
  PPCLK_DISPCLK,
  PPCLK_DPPCLK,
  PPCLK_DPREFCLK,
  PPCLK_DCFCLK,
  PPCLK_DTBCLK,
  PPCLK_COUNT,
} PPCLK_e;

typedef enum {
  UCLK_DIV_BY_1 = 0,
  UCLK_DIV_BY_2,
  UCLK_DIV_BY_4,
  UCLK_DIV_BY_8,
} UCLK_DIV_e;

typedef struct {
  uint8_t  WmSetting;
  uint8_t  Flags;
  uint8_t  Padding[2];

} WatermarkRowGeneric_t;

#define NUM_WM_RANGES 4

typedef enum {
  WATERMARKS_CLOCK_RANGE = 0,
  WATERMARKS_DUMMY_PSTATE,
  WATERMARKS_MALL,
  WATERMARKS_COUNT,
} WATERMARKS_FLAGS_e;

typedef struct {
  // Watermarks
  WatermarkRowGeneric_t WatermarkRow[NUM_WM_RANGES];
} Watermarks_t;

typedef struct {
  Watermarks_t Watermarks;
  uint32_t  Spare[16];

  uint32_t     MmHubPadding[8]; // SMU internal use
} WatermarksExternal_t;

// These defines are used with the following messages:
// SMC_MSG_TransferTableDram2Smu
// SMC_MSG_TransferTableSmu2Dram

// Table transfer status
#define TABLE_TRANSFER_OK         0x0
#define TABLE_TRANSFER_FAILED     0xFF
#define TABLE_TRANSFER_PENDING    0xAB

// Table types
#define TABLE_PMFW_PPTABLE            0
#define TABLE_COMBO_PPTABLE           1
#define TABLE_WATERMARKS              2
#define TABLE_AVFS_PSM_DEBUG          3
#define TABLE_PMSTATUSLOG             4
#define TABLE_SMU_METRICS             5
#define TABLE_DRIVER_SMU_CONFIG       6
#define TABLE_ACTIVITY_MONITOR_COEFF  7
#define TABLE_OVERDRIVE               8
#define TABLE_I2C_COMMANDS            9
#define TABLE_DRIVER_INFO             10
#define TABLE_COUNT                   11

#endif
