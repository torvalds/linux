/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef HRT_GDC_v2_defs_h_
#define HRT_GDC_v2_defs_h_

#define HRT_GDC_IS_V2

#define HRT_GDC_N                     1024 /* Top-level design constant, equal to the number of entries in the LUT      */
#define HRT_GDC_FRAC_BITS               10 /* Number of fractional bits in the GDC block, driven by the size of the LUT */

#define HRT_GDC_BLI_FRAC_BITS            4 /* Number of fractional bits for the bi-linear interpolation type            */
#define HRT_GDC_BLI_COEF_ONE             BIT(HRT_GDC_BLI_FRAC_BITS)

#define HRT_GDC_BCI_COEF_BITS           14 /* 14 bits per coefficient                                                   */
#define HRT_GDC_BCI_COEF_ONE             (1 << (HRT_GDC_BCI_COEF_BITS - 2))  /* We represent signed 10 bit coefficients.  */
/* The supported range is [-256, .., +256]      */
/* in 14-bit signed notation,                   */
/* We need all ten bits (MSB must be zero).     */
/* -s is inserted to solve this issue, and      */
/* therefore "1" is equal to +256.              */
#define HRT_GDC_BCI_COEF_MASK            ((1 << HRT_GDC_BCI_COEF_BITS) - 1)

#define HRT_GDC_LUT_BYTES                (HRT_GDC_N * 4 * 2)                /* 1024 addresses, 4 coefficients per address,  */
/* 2 bytes per coefficient                      */

#define _HRT_GDC_REG_ALIGN               4

//     31  30  29    25 24                     0
//  |-----|---|--------|------------------------|
//  | CMD | C | Reg_ID |        Value           |

// There are just two commands possible for the GDC block:
// 1 - Configure reg
// 0 - Data token

// C      - Reserved bit
//          Used in protocol to indicate whether it is C-run or other type of runs
//          In case of C-run, this bit has a value of 1, for all the other runs, it is 0.

// Reg_ID - Address of the register to be configured

// Value  - Value to store to the addressed register, maximum of 24 bits

// Configure reg command is not followed by any other token.
// The address of the register and the data to be filled in is contained in the same token

// When the first data token is received, it must be:
//   1. FRX and FRY (device configured in one of the  scaling modes) ***DEFAULT MODE***, or,
//   2. P0'X        (device configured in one of the tetragon modes)
// After the first data token is received, pre-defined number of tokens with the following meaning follow:
//   1. two  tokens: SRC address ; DST address
//   2. nine tokens: P0'Y, .., P3'Y ; SRC address ; DST address

#define HRT_GDC_CONFIG_CMD             1
#define HRT_GDC_DATA_CMD               0

#define HRT_GDC_CMD_POS               31
#define HRT_GDC_CMD_BITS               1
#define HRT_GDC_CRUN_POS              30
#define HRT_GDC_REG_ID_POS            25
#define HRT_GDC_REG_ID_BITS            5
#define HRT_GDC_DATA_POS               0
#define HRT_GDC_DATA_BITS             25

#define HRT_GDC_FRYIPXFRX_BITS        26
#define HRT_GDC_P0X_BITS              23

#define HRT_GDC_MAX_OXDIM           (8192 - 64)
#define HRT_GDC_MAX_OYDIM           4095
#define HRT_GDC_MAX_IXDIM           (8192 - 64)
#define HRT_GDC_MAX_IYDIM           4095
#define HRT_GDC_MAX_DS_FAC            16
#define HRT_GDC_MAX_DX                 (HRT_GDC_MAX_DS_FAC * HRT_GDC_N - 1)
#define HRT_GDC_MAX_DY                 HRT_GDC_MAX_DX

/* GDC lookup tables entries are 10 bits values, but they're
   stored 2 by 2 as 32 bit values, yielding 16 bits per entry.
   A GDC lookup table contains 64 * 4 elements */

#define HRT_GDC_PERF_1_1_pix          0
#define HRT_GDC_PERF_2_1_pix          1
#define HRT_GDC_PERF_1_2_pix          2
#define HRT_GDC_PERF_2_2_pix          3

#define HRT_GDC_NND_MODE              0
#define HRT_GDC_BLI_MODE              1
#define HRT_GDC_BCI_MODE              2
#define HRT_GDC_LUT_MODE              3

#define HRT_GDC_SCAN_STB              0
#define HRT_GDC_SCAN_STR              1

#define HRT_GDC_MODE_SCALING          0
#define HRT_GDC_MODE_TETRAGON         1

#define HRT_GDC_LUT_COEFF_OFFSET     16
#define HRT_GDC_FRY_BIT_OFFSET       16
// FRYIPXFRX is the only register where we store two values in one field,
// to save one token in the scaling protocol.
// Like this, we have three tokens in the scaling protocol,
// Otherwise, we would have had four.
// The register bit-map is:
//   31  26 25      16 15  10 9        0
//  |------|----------|------|----------|
//  | XXXX |   FRY    |  IPX |   FRX    |

#define HRT_GDC_CE_FSM0_POS           0
#define HRT_GDC_CE_FSM0_LEN           2
#define HRT_GDC_CE_OPY_POS            2
#define HRT_GDC_CE_OPY_LEN           14
#define HRT_GDC_CE_OPX_POS           16
#define HRT_GDC_CE_OPX_LEN           16
// CHK_ENGINE register bit-map:
//   31            16 15        2 1  0
//  |----------------|-----------|----|
//  |      OPX       |    OPY    |FSM0|
// However, for the time being at least,
// this implementation is meaningless in hss model,
// So, we just return 0

#define HRT_GDC_CHK_ENGINE_IDX        0
#define HRT_GDC_WOIX_IDX              1
#define HRT_GDC_WOIY_IDX              2
#define HRT_GDC_BPP_IDX               3
#define HRT_GDC_FRYIPXFRX_IDX         4
#define HRT_GDC_OXDIM_IDX             5
#define HRT_GDC_OYDIM_IDX             6
#define HRT_GDC_SRC_ADDR_IDX          7
#define HRT_GDC_SRC_END_ADDR_IDX      8
#define HRT_GDC_SRC_WRAP_ADDR_IDX     9
#define HRT_GDC_SRC_STRIDE_IDX       10
#define HRT_GDC_DST_ADDR_IDX         11
#define HRT_GDC_DST_STRIDE_IDX       12
#define HRT_GDC_DX_IDX               13
#define HRT_GDC_DY_IDX               14
#define HRT_GDC_P0X_IDX              15
#define HRT_GDC_P0Y_IDX              16
#define HRT_GDC_P1X_IDX              17
#define HRT_GDC_P1Y_IDX              18
#define HRT_GDC_P2X_IDX              19
#define HRT_GDC_P2Y_IDX              20
#define HRT_GDC_P3X_IDX              21
#define HRT_GDC_P3Y_IDX              22
#define HRT_GDC_PERF_POINT_IDX       23  // 1x1 ; 1x2 ; 2x1 ; 2x2 pixels per cc
#define HRT_GDC_INTERP_TYPE_IDX      24  // NND ; BLI ; BCI ; LUT
#define HRT_GDC_SCAN_IDX             25  // 0 = STB (Slide To Bottom) ; 1 = STR (Slide To Right)
#define HRT_GDC_PROC_MODE_IDX        26  // 0 = Scaling ; 1 = Tetragon

#define HRT_GDC_LUT_IDX              32

#endif /* HRT_GDC_v2_defs_h_ */
