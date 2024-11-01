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


#ifndef _cl507e_h_
#define _cl507e_h_

// class methods
#define NV507E_SET_PRESENT_CONTROL                                              (0x00000084)
#define NV507E_SET_PRESENT_CONTROL_BEGIN_MODE                                   1:0
#define NV507E_SET_PRESENT_CONTROL_BEGIN_MODE_ASAP                              (0x00000000)
#define NV507E_SET_PRESENT_CONTROL_BEGIN_MODE_TIMESTAMP                         (0x00000003)
#define NV507E_SET_PRESENT_CONTROL_MIN_PRESENT_INTERVAL                         7:4
#define NV507E_SET_CONTEXT_DMA_ISO                                              (0x000000C0)
#define NV507E_SET_CONTEXT_DMA_ISO_HANDLE                                       31:0
#define NV507E_SET_POINT_IN                                                     (0x000000E0)
#define NV507E_SET_POINT_IN_X                                                   14:0
#define NV507E_SET_POINT_IN_Y                                                   30:16
#define NV507E_SET_SIZE_IN                                                      (0x000000E4)
#define NV507E_SET_SIZE_IN_WIDTH                                                14:0
#define NV507E_SET_SIZE_IN_HEIGHT                                               30:16
#define NV507E_SET_SIZE_OUT                                                     (0x000000E8)
#define NV507E_SET_SIZE_OUT_WIDTH                                               14:0
#define NV507E_SET_COMPOSITION_CONTROL                                          (0x00000100)
#define NV507E_SET_COMPOSITION_CONTROL_MODE                                     3:0
#define NV507E_SET_COMPOSITION_CONTROL_MODE_SOURCE_COLOR_VALUE_KEYING           (0x00000000)
#define NV507E_SET_COMPOSITION_CONTROL_MODE_DESTINATION_COLOR_VALUE_KEYING      (0x00000001)
#define NV507E_SET_COMPOSITION_CONTROL_MODE_OPAQUE_SUSPEND_BASE                 (0x00000002)

#define NV507E_SURFACE_SET_OFFSET                                               (0x00000800)
#define NV507E_SURFACE_SET_OFFSET_ORIGIN                                        31:0
#define NV507E_SURFACE_SET_SIZE                                                 (0x00000808)
#define NV507E_SURFACE_SET_SIZE_WIDTH                                           14:0
#define NV507E_SURFACE_SET_SIZE_HEIGHT                                          30:16
#define NV507E_SURFACE_SET_STORAGE                                              (0x0000080C)
#define NV507E_SURFACE_SET_STORAGE_BLOCK_HEIGHT                                 3:0
#define NV507E_SURFACE_SET_STORAGE_BLOCK_HEIGHT_ONE_GOB                         (0x00000000)
#define NV507E_SURFACE_SET_STORAGE_BLOCK_HEIGHT_TWO_GOBS                        (0x00000001)
#define NV507E_SURFACE_SET_STORAGE_BLOCK_HEIGHT_FOUR_GOBS                       (0x00000002)
#define NV507E_SURFACE_SET_STORAGE_BLOCK_HEIGHT_EIGHT_GOBS                      (0x00000003)
#define NV507E_SURFACE_SET_STORAGE_BLOCK_HEIGHT_SIXTEEN_GOBS                    (0x00000004)
#define NV507E_SURFACE_SET_STORAGE_BLOCK_HEIGHT_THIRTYTWO_GOBS                  (0x00000005)
#define NV507E_SURFACE_SET_STORAGE_PITCH                                        17:8
#define NV507E_SURFACE_SET_STORAGE_MEMORY_LAYOUT                                20:20
#define NV507E_SURFACE_SET_STORAGE_MEMORY_LAYOUT_BLOCKLINEAR                    (0x00000000)
#define NV507E_SURFACE_SET_STORAGE_MEMORY_LAYOUT_PITCH                          (0x00000001)
#define NV507E_SURFACE_SET_PARAMS                                               (0x00000810)
#define NV507E_SURFACE_SET_PARAMS_FORMAT                                        15:8
#define NV507E_SURFACE_SET_PARAMS_FORMAT_VE8YO8UE8YE8                           (0x00000028)
#define NV507E_SURFACE_SET_PARAMS_FORMAT_YO8VE8YE8UE8                           (0x00000029)
#define NV507E_SURFACE_SET_PARAMS_FORMAT_A8R8G8B8                               (0x000000CF)
#define NV507E_SURFACE_SET_PARAMS_FORMAT_A1R5G5B5                               (0x000000E9)
#define NV507E_SURFACE_SET_PARAMS_COLOR_SPACE                                   1:0
#define NV507E_SURFACE_SET_PARAMS_COLOR_SPACE_RGB                               (0x00000000)
#define NV507E_SURFACE_SET_PARAMS_COLOR_SPACE_YUV_601                           (0x00000001)
#define NV507E_SURFACE_SET_PARAMS_COLOR_SPACE_YUV_709                           (0x00000002)
#define NV507E_SURFACE_SET_PARAMS_KIND                                          22:16
#define NV507E_SURFACE_SET_PARAMS_KIND_KIND_PITCH                               (0x00000000)
#define NV507E_SURFACE_SET_PARAMS_KIND_KIND_GENERIC_8BX2                        (0x00000070)
#define NV507E_SURFACE_SET_PARAMS_KIND_KIND_GENERIC_8BX2_BANKSWIZ               (0x00000072)
#define NV507E_SURFACE_SET_PARAMS_KIND_KIND_GENERIC_16BX1                       (0x00000074)
#define NV507E_SURFACE_SET_PARAMS_KIND_KIND_GENERIC_16BX1_BANKSWIZ              (0x00000076)
#define NV507E_SURFACE_SET_PARAMS_KIND_KIND_C32_MS4                             (0x00000078)
#define NV507E_SURFACE_SET_PARAMS_KIND_KIND_C32_MS8                             (0x00000079)
#define NV507E_SURFACE_SET_PARAMS_KIND_KIND_C32_MS4_BANKSWIZ                    (0x0000007A)
#define NV507E_SURFACE_SET_PARAMS_KIND_KIND_C32_MS8_BANKSWIZ                    (0x0000007B)
#define NV507E_SURFACE_SET_PARAMS_KIND_KIND_C64_MS4                             (0x0000007C)
#define NV507E_SURFACE_SET_PARAMS_KIND_KIND_C64_MS8                             (0x0000007D)
#define NV507E_SURFACE_SET_PARAMS_KIND_KIND_C128_MS4                            (0x0000007E)
#define NV507E_SURFACE_SET_PARAMS_KIND_FROM_PTE                                 (0x0000007F)
#define NV507E_SURFACE_SET_PARAMS_PART_STRIDE                                   24:24
#define NV507E_SURFACE_SET_PARAMS_PART_STRIDE_PARTSTRIDE_256                    (0x00000000)
#define NV507E_SURFACE_SET_PARAMS_PART_STRIDE_PARTSTRIDE_1024                   (0x00000001)
#endif // _cl507e_h
