/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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
 */
#ifndef __RADEON_UCODE_H__
#define __RADEON_UCODE_H__

/* CP */
#define R600_PFP_UCODE_SIZE          576
#define R600_PM4_UCODE_SIZE          1792
#define R700_PFP_UCODE_SIZE          848
#define R700_PM4_UCODE_SIZE          1360
#define EVERGREEN_PFP_UCODE_SIZE     1120
#define EVERGREEN_PM4_UCODE_SIZE     1376
#define CAYMAN_PFP_UCODE_SIZE        2176
#define CAYMAN_PM4_UCODE_SIZE        2176
#define SI_PFP_UCODE_SIZE            2144
#define SI_PM4_UCODE_SIZE            2144
#define SI_CE_UCODE_SIZE             2144

/* RLC */
#define R600_RLC_UCODE_SIZE          768
#define R700_RLC_UCODE_SIZE          1024
#define EVERGREEN_RLC_UCODE_SIZE     768
#define CAYMAN_RLC_UCODE_SIZE        1024
#define ARUBA_RLC_UCODE_SIZE         1536
#define SI_RLC_UCODE_SIZE            2048

/* MC */
#define BTC_MC_UCODE_SIZE            6024
#define CAYMAN_MC_UCODE_SIZE         6037
#define SI_MC_UCODE_SIZE             7769
#define OLAND_MC_UCODE_SIZE          7863

/* SMC */
#define RV770_SMC_UCODE_START        0x0100
#define RV770_SMC_UCODE_SIZE         0x410d
#define RV770_SMC_INT_VECTOR_START   0xffc0
#define RV770_SMC_INT_VECTOR_SIZE    0x0040

#define RV730_SMC_UCODE_START        0x0100
#define RV730_SMC_UCODE_SIZE         0x412c
#define RV730_SMC_INT_VECTOR_START   0xffc0
#define RV730_SMC_INT_VECTOR_SIZE    0x0040

#define RV710_SMC_UCODE_START        0x0100
#define RV710_SMC_UCODE_SIZE         0x3f1f
#define RV710_SMC_INT_VECTOR_START   0xffc0
#define RV710_SMC_INT_VECTOR_SIZE    0x0040

#define RV740_SMC_UCODE_START        0x0100
#define RV740_SMC_UCODE_SIZE         0x41c5
#define RV740_SMC_INT_VECTOR_START   0xffc0
#define RV740_SMC_INT_VECTOR_SIZE    0x0040

#define CEDAR_SMC_UCODE_START        0x0100
#define CEDAR_SMC_UCODE_SIZE         0x5d50
#define CEDAR_SMC_INT_VECTOR_START   0xffc0
#define CEDAR_SMC_INT_VECTOR_SIZE    0x0040

#define REDWOOD_SMC_UCODE_START      0x0100
#define REDWOOD_SMC_UCODE_SIZE       0x5f0a
#define REDWOOD_SMC_INT_VECTOR_START 0xffc0
#define REDWOOD_SMC_INT_VECTOR_SIZE  0x0040

#define JUNIPER_SMC_UCODE_START      0x0100
#define JUNIPER_SMC_UCODE_SIZE       0x5f1f
#define JUNIPER_SMC_INT_VECTOR_START 0xffc0
#define JUNIPER_SMC_INT_VECTOR_SIZE  0x0040

#define CYPRESS_SMC_UCODE_START      0x0100
#define CYPRESS_SMC_UCODE_SIZE       0x61f7
#define CYPRESS_SMC_INT_VECTOR_START 0xffc0
#define CYPRESS_SMC_INT_VECTOR_SIZE  0x0040

#define BARTS_SMC_UCODE_START        0x0100
#define BARTS_SMC_UCODE_SIZE         0x6107
#define BARTS_SMC_INT_VECTOR_START   0xffc0
#define BARTS_SMC_INT_VECTOR_SIZE    0x0040

#define TURKS_SMC_UCODE_START        0x0100
#define TURKS_SMC_UCODE_SIZE         0x605b
#define TURKS_SMC_INT_VECTOR_START   0xffc0
#define TURKS_SMC_INT_VECTOR_SIZE    0x0040

#define CAICOS_SMC_UCODE_START       0x0100
#define CAICOS_SMC_UCODE_SIZE        0x5fbd
#define CAICOS_SMC_INT_VECTOR_START  0xffc0
#define CAICOS_SMC_INT_VECTOR_SIZE   0x0040

#define CAYMAN_SMC_UCODE_START       0x0100
#define CAYMAN_SMC_UCODE_SIZE        0x79ec
#define CAYMAN_SMC_INT_VECTOR_START  0xffc0
#define CAYMAN_SMC_INT_VECTOR_SIZE   0x0040

#endif
