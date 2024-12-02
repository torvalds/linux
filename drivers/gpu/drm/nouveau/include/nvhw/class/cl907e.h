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


#ifndef _cl907e_h_
#define _cl907e_h_

// class methods
#define NV907E_SET_PRESENT_CONTROL                                              (0x00000084)
#define NV907E_SET_PRESENT_CONTROL_BEGIN_MODE                                   1:0
#define NV907E_SET_PRESENT_CONTROL_BEGIN_MODE_ASAP                              (0x00000000)
#define NV907E_SET_PRESENT_CONTROL_BEGIN_MODE_TIMESTAMP                         (0x00000003)
#define NV907E_SET_PRESENT_CONTROL_MIN_PRESENT_INTERVAL                         7:4
#define NV907E_SET_CONTEXT_DMA_ISO                                              (0x000000C0)
#define NV907E_SET_CONTEXT_DMA_ISO_HANDLE                                       31:0
#define NV907E_SET_COMPOSITION_CONTROL                                          (0x00000100)
#define NV907E_SET_COMPOSITION_CONTROL_MODE                                     3:0
#define NV907E_SET_COMPOSITION_CONTROL_MODE_SOURCE_COLOR_VALUE_KEYING           (0x00000000)
#define NV907E_SET_COMPOSITION_CONTROL_MODE_DESTINATION_COLOR_VALUE_KEYING      (0x00000001)
#define NV907E_SET_COMPOSITION_CONTROL_MODE_OPAQUE                              (0x00000002)

#define NV907E_SURFACE_SET_OFFSET                                               (0x00000400)
#define NV907E_SURFACE_SET_OFFSET_ORIGIN                                        31:0
#define NV907E_SURFACE_SET_SIZE                                                 (0x00000408)
#define NV907E_SURFACE_SET_SIZE_WIDTH                                           15:0
#define NV907E_SURFACE_SET_SIZE_HEIGHT                                          31:16
#define NV907E_SURFACE_SET_STORAGE                                              (0x0000040C)
#define NV907E_SURFACE_SET_STORAGE_BLOCK_HEIGHT                                 3:0
#define NV907E_SURFACE_SET_STORAGE_BLOCK_HEIGHT_ONE_GOB                         (0x00000000)
#define NV907E_SURFACE_SET_STORAGE_BLOCK_HEIGHT_TWO_GOBS                        (0x00000001)
#define NV907E_SURFACE_SET_STORAGE_BLOCK_HEIGHT_FOUR_GOBS                       (0x00000002)
#define NV907E_SURFACE_SET_STORAGE_BLOCK_HEIGHT_EIGHT_GOBS                      (0x00000003)
#define NV907E_SURFACE_SET_STORAGE_BLOCK_HEIGHT_SIXTEEN_GOBS                    (0x00000004)
#define NV907E_SURFACE_SET_STORAGE_BLOCK_HEIGHT_THIRTYTWO_GOBS                  (0x00000005)
#define NV907E_SURFACE_SET_STORAGE_PITCH                                        20:8
#define NV907E_SURFACE_SET_STORAGE_MEMORY_LAYOUT                                24:24
#define NV907E_SURFACE_SET_STORAGE_MEMORY_LAYOUT_BLOCKLINEAR                    (0x00000000)
#define NV907E_SURFACE_SET_STORAGE_MEMORY_LAYOUT_PITCH                          (0x00000001)
#define NV907E_SURFACE_SET_PARAMS                                               (0x00000410)
#define NV907E_SURFACE_SET_PARAMS_FORMAT                                        15:8
#define NV907E_SURFACE_SET_PARAMS_FORMAT_VE8YO8UE8YE8                           (0x00000028)
#define NV907E_SURFACE_SET_PARAMS_FORMAT_YO8VE8YE8UE8                           (0x00000029)
#define NV907E_SURFACE_SET_PARAMS_FORMAT_A2B10G10R10                            (0x000000D1)
#define NV907E_SURFACE_SET_PARAMS_FORMAT_X2BL10GL10RL10_XRBIAS                  (0x00000022)
#define NV907E_SURFACE_SET_PARAMS_FORMAT_A8R8G8B8                               (0x000000CF)
#define NV907E_SURFACE_SET_PARAMS_FORMAT_A1R5G5B5                               (0x000000E9)
#define NV907E_SURFACE_SET_PARAMS_FORMAT_RF16_GF16_BF16_AF16                    (0x000000CA)
#define NV907E_SURFACE_SET_PARAMS_FORMAT_R16_G16_B16_A16                        (0x000000C6)
#define NV907E_SURFACE_SET_PARAMS_FORMAT_R16_G16_B16_A16_NVBIAS                 (0x00000023)
#define NV907E_SURFACE_SET_PARAMS_COLOR_SPACE                                   1:0
#define NV907E_SURFACE_SET_PARAMS_COLOR_SPACE_RGB                               (0x00000000)
#define NV907E_SURFACE_SET_PARAMS_COLOR_SPACE_YUV_601                           (0x00000001)
#define NV907E_SURFACE_SET_PARAMS_COLOR_SPACE_YUV_709                           (0x00000002)
#endif // _cl907e_h
