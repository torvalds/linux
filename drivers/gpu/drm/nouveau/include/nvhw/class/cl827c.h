/*
 * Copyright (c) 1993-2014, NVIDIA CORPORATION. All rights reserved.
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


#ifndef _cl827c_h_
#define _cl827c_h_

// class methods
#define NV827C_SET_PRESENT_CONTROL                                              (0x00000084)
#define NV827C_SET_PRESENT_CONTROL_BEGIN_MODE                                   9:8
#define NV827C_SET_PRESENT_CONTROL_BEGIN_MODE_NON_TEARING                       (0x00000000)
#define NV827C_SET_PRESENT_CONTROL_BEGIN_MODE_IMMEDIATE                         (0x00000001)
#define NV827C_SET_PRESENT_CONTROL_BEGIN_MODE_ON_LINE                           (0x00000002)
#define NV827C_SET_PRESENT_CONTROL_MIN_PRESENT_INTERVAL                         7:4
#define NV827C_SET_PRESENT_CONTROL_BEGIN_LINE                                   30:16
#define NV827C_SET_PRESENT_CONTROL_ON_LINE_MARGIN                               15:10
#define NV827C_SET_CONTEXT_DMAS_ISO(b)                                          (0x000000C0 + (b)*0x00000004)
#define NV827C_SET_CONTEXT_DMAS_ISO_HANDLE                                      31:0
#define NV827C_SET_PROCESSING                                                   (0x00000110)
#define NV827C_SET_PROCESSING_USE_GAIN_OFS                                      0:0
#define NV827C_SET_PROCESSING_USE_GAIN_OFS_DISABLE                              (0x00000000)
#define NV827C_SET_PROCESSING_USE_GAIN_OFS_ENABLE                               (0x00000001)
#define NV827C_SET_CONVERSION                                                   (0x00000114)
#define NV827C_SET_CONVERSION_GAIN                                              15:0
#define NV827C_SET_CONVERSION_OFS                                               31:16

#define NV827C_SURFACE_SET_OFFSET(a,b)                                          (0x00000800 + (a)*0x00000020 + (b)*0x00000004)
#define NV827C_SURFACE_SET_OFFSET_ORIGIN                                        31:0
#define NV827C_SURFACE_SET_SIZE(a)                                              (0x00000808 + (a)*0x00000020)
#define NV827C_SURFACE_SET_SIZE_WIDTH                                           14:0
#define NV827C_SURFACE_SET_SIZE_HEIGHT                                          30:16
#define NV827C_SURFACE_SET_STORAGE(a)                                           (0x0000080C + (a)*0x00000020)
#define NV827C_SURFACE_SET_STORAGE_BLOCK_HEIGHT                                 3:0
#define NV827C_SURFACE_SET_STORAGE_BLOCK_HEIGHT_ONE_GOB                         (0x00000000)
#define NV827C_SURFACE_SET_STORAGE_BLOCK_HEIGHT_TWO_GOBS                        (0x00000001)
#define NV827C_SURFACE_SET_STORAGE_BLOCK_HEIGHT_FOUR_GOBS                       (0x00000002)
#define NV827C_SURFACE_SET_STORAGE_BLOCK_HEIGHT_EIGHT_GOBS                      (0x00000003)
#define NV827C_SURFACE_SET_STORAGE_BLOCK_HEIGHT_SIXTEEN_GOBS                    (0x00000004)
#define NV827C_SURFACE_SET_STORAGE_BLOCK_HEIGHT_THIRTYTWO_GOBS                  (0x00000005)
#define NV827C_SURFACE_SET_STORAGE_PITCH                                        17:8
#define NV827C_SURFACE_SET_STORAGE_MEMORY_LAYOUT                                20:20
#define NV827C_SURFACE_SET_STORAGE_MEMORY_LAYOUT_BLOCKLINEAR                    (0x00000000)
#define NV827C_SURFACE_SET_STORAGE_MEMORY_LAYOUT_PITCH                          (0x00000001)
#define NV827C_SURFACE_SET_PARAMS(a)                                            (0x00000810 + (a)*0x00000020)
#define NV827C_SURFACE_SET_PARAMS_FORMAT                                        15:8
#define NV827C_SURFACE_SET_PARAMS_FORMAT_I8                                     (0x0000001E)
#define NV827C_SURFACE_SET_PARAMS_FORMAT_VOID16                                 (0x0000001F)
#define NV827C_SURFACE_SET_PARAMS_FORMAT_VOID32                                 (0x0000002E)
#define NV827C_SURFACE_SET_PARAMS_FORMAT_RF16_GF16_BF16_AF16                    (0x000000CA)
#define NV827C_SURFACE_SET_PARAMS_FORMAT_A8R8G8B8                               (0x000000CF)
#define NV827C_SURFACE_SET_PARAMS_FORMAT_A2B10G10R10                            (0x000000D1)
#define NV827C_SURFACE_SET_PARAMS_FORMAT_A8B8G8R8                               (0x000000D5)
#define NV827C_SURFACE_SET_PARAMS_FORMAT_R5G6B5                                 (0x000000E8)
#define NV827C_SURFACE_SET_PARAMS_FORMAT_A1R5G5B5                               (0x000000E9)
#define NV827C_SURFACE_SET_PARAMS_SUPER_SAMPLE                                  1:0
#define NV827C_SURFACE_SET_PARAMS_SUPER_SAMPLE_X1_AA                            (0x00000000)
#define NV827C_SURFACE_SET_PARAMS_SUPER_SAMPLE_X4_AA                            (0x00000002)
#define NV827C_SURFACE_SET_PARAMS_GAMMA                                         2:2
#define NV827C_SURFACE_SET_PARAMS_GAMMA_LINEAR                                  (0x00000000)
#define NV827C_SURFACE_SET_PARAMS_GAMMA_SRGB                                    (0x00000001)
#define NV827C_SURFACE_SET_PARAMS_LAYOUT                                        5:4
#define NV827C_SURFACE_SET_PARAMS_LAYOUT_FRM                                    (0x00000000)
#define NV827C_SURFACE_SET_PARAMS_LAYOUT_FLD1                                   (0x00000001)
#define NV827C_SURFACE_SET_PARAMS_LAYOUT_FLD2                                   (0x00000002)
#define NV827C_SURFACE_SET_PARAMS_RESERVED0                                     22:16
#define NV827C_SURFACE_SET_PARAMS_RESERVED1                                     24:24
#endif // _cl827c_h
