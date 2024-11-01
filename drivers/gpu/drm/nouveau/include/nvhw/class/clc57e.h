/*
 * Copyright (c) 1993-2020, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef _clC57e_h_
#define _clC57e_h_

// class methods
#define NVC57E_SET_SIZE                                                         (0x00000224)
#define NVC57E_SET_SIZE_WIDTH                                                   15:0
#define NVC57E_SET_SIZE_HEIGHT                                                  31:16
#define NVC57E_SET_STORAGE                                                      (0x00000228)
#define NVC57E_SET_STORAGE_BLOCK_HEIGHT                                         3:0
#define NVC57E_SET_STORAGE_BLOCK_HEIGHT_NVD_BLOCK_HEIGHT_ONE_GOB                (0x00000000)
#define NVC57E_SET_STORAGE_BLOCK_HEIGHT_NVD_BLOCK_HEIGHT_TWO_GOBS               (0x00000001)
#define NVC57E_SET_STORAGE_BLOCK_HEIGHT_NVD_BLOCK_HEIGHT_FOUR_GOBS              (0x00000002)
#define NVC57E_SET_STORAGE_BLOCK_HEIGHT_NVD_BLOCK_HEIGHT_EIGHT_GOBS             (0x00000003)
#define NVC57E_SET_STORAGE_BLOCK_HEIGHT_NVD_BLOCK_HEIGHT_SIXTEEN_GOBS           (0x00000004)
#define NVC57E_SET_STORAGE_BLOCK_HEIGHT_NVD_BLOCK_HEIGHT_THIRTYTWO_GOBS         (0x00000005)
#define NVC57E_SET_STORAGE_MEMORY_LAYOUT                                        4:4
#define NVC57E_SET_STORAGE_MEMORY_LAYOUT_BLOCKLINEAR                            (0x00000000)
#define NVC57E_SET_STORAGE_MEMORY_LAYOUT_PITCH                                  (0x00000001)
#define NVC57E_SET_PARAMS                                                       (0x0000022C)
#define NVC57E_SET_PARAMS_FORMAT                                                7:0
#define NVC57E_SET_PARAMS_FORMAT_I8                                             (0x0000001E)
#define NVC57E_SET_PARAMS_FORMAT_R4G4B4A4                                       (0x0000002F)
#define NVC57E_SET_PARAMS_FORMAT_R5G6B5                                         (0x000000E8)
#define NVC57E_SET_PARAMS_FORMAT_A1R5G5B5                                       (0x000000E9)
#define NVC57E_SET_PARAMS_FORMAT_R5G5B5A1                                       (0x0000002E)
#define NVC57E_SET_PARAMS_FORMAT_A8R8G8B8                                       (0x000000CF)
#define NVC57E_SET_PARAMS_FORMAT_X8R8G8B8                                       (0x000000E6)
#define NVC57E_SET_PARAMS_FORMAT_A8B8G8R8                                       (0x000000D5)
#define NVC57E_SET_PARAMS_FORMAT_X8B8G8R8                                       (0x000000F9)
#define NVC57E_SET_PARAMS_FORMAT_A2R10G10B10                                    (0x000000DF)
#define NVC57E_SET_PARAMS_FORMAT_A2B10G10R10                                    (0x000000D1)
#define NVC57E_SET_PARAMS_FORMAT_X2BL10GL10RL10_XRBIAS                          (0x00000022)
#define NVC57E_SET_PARAMS_FORMAT_X2BL10GL10RL10_XVYCC                           (0x00000024)
#define NVC57E_SET_PARAMS_FORMAT_R16_G16_B16_A16_NVBIAS                         (0x00000023)
#define NVC57E_SET_PARAMS_FORMAT_R16_G16_B16_A16                                (0x000000C6)
#define NVC57E_SET_PARAMS_FORMAT_RF16_GF16_BF16_AF16                            (0x000000CA)
#define NVC57E_SET_PARAMS_FORMAT_Y8_U8__Y8_V8_N422                              (0x00000028)
#define NVC57E_SET_PARAMS_FORMAT_U8_Y8__V8_Y8_N422                              (0x00000029)
#define NVC57E_SET_PARAMS_FORMAT_Y8___U8V8_N444                                 (0x00000035)
#define NVC57E_SET_PARAMS_FORMAT_Y8___U8V8_N422                                 (0x00000036)
#define NVC57E_SET_PARAMS_FORMAT_Y8___V8U8_N420                                 (0x00000038)
#define NVC57E_SET_PARAMS_FORMAT_Y10___U10V10_N444                              (0x00000055)
#define NVC57E_SET_PARAMS_FORMAT_Y10___U10V10_N422                              (0x00000056)
#define NVC57E_SET_PARAMS_FORMAT_Y10___V10U10_N420                              (0x00000058)
#define NVC57E_SET_PARAMS_FORMAT_Y12___U12V12_N444                              (0x00000075)
#define NVC57E_SET_PARAMS_FORMAT_Y12___U12V12_N422                              (0x00000076)
#define NVC57E_SET_PARAMS_FORMAT_Y12___V12U12_N420                              (0x00000078)
#define NVC57E_SET_PARAMS_CLAMP_BEFORE_BLEND                                    18:18
#define NVC57E_SET_PARAMS_CLAMP_BEFORE_BLEND_DISABLE                            (0x00000000)
#define NVC57E_SET_PARAMS_CLAMP_BEFORE_BLEND_ENABLE                             (0x00000001)
#define NVC57E_SET_PARAMS_SWAP_UV                                               19:19
#define NVC57E_SET_PARAMS_SWAP_UV_DISABLE                                       (0x00000000)
#define NVC57E_SET_PARAMS_SWAP_UV_ENABLE                                        (0x00000001)
#define NVC57E_SET_PARAMS_FMT_ROUNDING_MODE                                     22:22
#define NVC57E_SET_PARAMS_FMT_ROUNDING_MODE_ROUND_TO_NEAREST                    (0x00000000)
#define NVC57E_SET_PARAMS_FMT_ROUNDING_MODE_ROUND_DOWN                          (0x00000001)
#define NVC57E_SET_PLANAR_STORAGE(b)                                            (0x00000230 + (b)*0x00000004)
#define NVC57E_SET_PLANAR_STORAGE_PITCH                                         12:0
#define NVC57E_SET_CONTEXT_DMA_ISO(b)                                           (0x00000240 + (b)*0x00000004)
#define NVC57E_SET_CONTEXT_DMA_ISO_HANDLE                                       31:0
#define NVC57E_SET_OFFSET(b)                                                    (0x00000260 + (b)*0x00000004)
#define NVC57E_SET_OFFSET_ORIGIN                                                31:0
#define NVC57E_SET_POINT_IN(b)                                                  (0x00000290 + (b)*0x00000004)
#define NVC57E_SET_POINT_IN_X                                                   15:0
#define NVC57E_SET_POINT_IN_Y                                                   31:16
#define NVC57E_SET_SIZE_IN                                                      (0x00000298)
#define NVC57E_SET_SIZE_IN_WIDTH                                                15:0
#define NVC57E_SET_SIZE_IN_HEIGHT                                               31:16
#define NVC57E_SET_SIZE_OUT                                                     (0x000002A4)
#define NVC57E_SET_SIZE_OUT_WIDTH                                               15:0
#define NVC57E_SET_SIZE_OUT_HEIGHT                                              31:16
#define NVC57E_SET_PRESENT_CONTROL                                              (0x00000308)
#define NVC57E_SET_PRESENT_CONTROL_MIN_PRESENT_INTERVAL                         3:0
#define NVC57E_SET_PRESENT_CONTROL_BEGIN_MODE                                   6:4
#define NVC57E_SET_PRESENT_CONTROL_BEGIN_MODE_NON_TEARING                       (0x00000000)
#define NVC57E_SET_PRESENT_CONTROL_BEGIN_MODE_IMMEDIATE                         (0x00000001)
#define NVC57E_SET_PRESENT_CONTROL_TIMESTAMP_MODE                               8:8
#define NVC57E_SET_PRESENT_CONTROL_TIMESTAMP_MODE_DISABLE                       (0x00000000)
#define NVC57E_SET_PRESENT_CONTROL_TIMESTAMP_MODE_ENABLE                        (0x00000001)
#define NVC57E_SET_FMT_COEFFICIENT_C00                                          (0x00000400)
#define NVC57E_SET_FMT_COEFFICIENT_C00_VALUE                                    20:0
#define NVC57E_SET_FMT_COEFFICIENT_C01                                          (0x00000404)
#define NVC57E_SET_FMT_COEFFICIENT_C01_VALUE                                    20:0
#define NVC57E_SET_FMT_COEFFICIENT_C02                                          (0x00000408)
#define NVC57E_SET_FMT_COEFFICIENT_C02_VALUE                                    20:0
#define NVC57E_SET_FMT_COEFFICIENT_C03                                          (0x0000040C)
#define NVC57E_SET_FMT_COEFFICIENT_C03_VALUE                                    20:0
#define NVC57E_SET_FMT_COEFFICIENT_C10                                          (0x00000410)
#define NVC57E_SET_FMT_COEFFICIENT_C10_VALUE                                    20:0
#define NVC57E_SET_FMT_COEFFICIENT_C11                                          (0x00000414)
#define NVC57E_SET_FMT_COEFFICIENT_C11_VALUE                                    20:0
#define NVC57E_SET_FMT_COEFFICIENT_C12                                          (0x00000418)
#define NVC57E_SET_FMT_COEFFICIENT_C12_VALUE                                    20:0
#define NVC57E_SET_FMT_COEFFICIENT_C13                                          (0x0000041C)
#define NVC57E_SET_FMT_COEFFICIENT_C13_VALUE                                    20:0
#define NVC57E_SET_FMT_COEFFICIENT_C20                                          (0x00000420)
#define NVC57E_SET_FMT_COEFFICIENT_C20_VALUE                                    20:0
#define NVC57E_SET_FMT_COEFFICIENT_C21                                          (0x00000424)
#define NVC57E_SET_FMT_COEFFICIENT_C21_VALUE                                    20:0
#define NVC57E_SET_FMT_COEFFICIENT_C22                                          (0x00000428)
#define NVC57E_SET_FMT_COEFFICIENT_C22_VALUE                                    20:0
#define NVC57E_SET_FMT_COEFFICIENT_C23                                          (0x0000042C)
#define NVC57E_SET_FMT_COEFFICIENT_C23_VALUE                                    20:0
#define NVC57E_SET_ILUT_CONTROL                                                 (0x00000440)
#define NVC57E_SET_ILUT_CONTROL_INTERPOLATE                                     0:0
#define NVC57E_SET_ILUT_CONTROL_INTERPOLATE_DISABLE                             (0x00000000)
#define NVC57E_SET_ILUT_CONTROL_INTERPOLATE_ENABLE                              (0x00000001)
#define NVC57E_SET_ILUT_CONTROL_MIRROR                                          1:1
#define NVC57E_SET_ILUT_CONTROL_MIRROR_DISABLE                                  (0x00000000)
#define NVC57E_SET_ILUT_CONTROL_MIRROR_ENABLE                                   (0x00000001)
#define NVC57E_SET_ILUT_CONTROL_MODE                                            3:2
#define NVC57E_SET_ILUT_CONTROL_MODE_SEGMENTED                                  (0x00000000)
#define NVC57E_SET_ILUT_CONTROL_MODE_DIRECT8                                    (0x00000001)
#define NVC57E_SET_ILUT_CONTROL_MODE_DIRECT10                                   (0x00000002)
#define NVC57E_SET_ILUT_CONTROL_SIZE                                            18:8
#define NVC57E_SET_CONTEXT_DMA_ILUT                                             (0x00000444)
#define NVC57E_SET_CONTEXT_DMA_ILUT_HANDLE                                      31:0
#define NVC57E_SET_OFFSET_ILUT                                                  (0x00000448)
#define NVC57E_SET_OFFSET_ILUT_ORIGIN                                           31:0
#endif // _clC57e_h
