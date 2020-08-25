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


#ifndef _cl827d_h_
#define _cl827d_h_

// class methods
#define NV827D_HEAD_SET_BASE_LUT_LO(a)                                          (0x00000840 + (a)*0x00000400)
#define NV827D_HEAD_SET_BASE_LUT_LO_ENABLE                                      31:31
#define NV827D_HEAD_SET_BASE_LUT_LO_ENABLE_DISABLE                              (0x00000000)
#define NV827D_HEAD_SET_BASE_LUT_LO_ENABLE_ENABLE                               (0x00000001)
#define NV827D_HEAD_SET_BASE_LUT_LO_MODE                                        30:30
#define NV827D_HEAD_SET_BASE_LUT_LO_MODE_LORES                                  (0x00000000)
#define NV827D_HEAD_SET_BASE_LUT_LO_MODE_HIRES                                  (0x00000001)
#define NV827D_HEAD_SET_BASE_LUT_LO_ORIGIN                                      7:2
#define NV827D_HEAD_SET_BASE_LUT_HI(a)                                          (0x00000844 + (a)*0x00000400)
#define NV827D_HEAD_SET_BASE_LUT_HI_ORIGIN                                      31:0
#define NV827D_HEAD_SET_CONTEXT_DMA_LUT(a)                                      (0x0000085C + (a)*0x00000400)
#define NV827D_HEAD_SET_CONTEXT_DMA_LUT_HANDLE                                  31:0
#define NV827D_HEAD_SET_OFFSET(a,b)                                             (0x00000860 + (a)*0x00000400 + (b)*0x00000004)
#define NV827D_HEAD_SET_OFFSET_ORIGIN                                           31:0
#define NV827D_HEAD_SET_SIZE(a)                                                 (0x00000868 + (a)*0x00000400)
#define NV827D_HEAD_SET_SIZE_WIDTH                                              14:0
#define NV827D_HEAD_SET_SIZE_HEIGHT                                             30:16
#define NV827D_HEAD_SET_STORAGE(a)                                              (0x0000086C + (a)*0x00000400)
#define NV827D_HEAD_SET_STORAGE_BLOCK_HEIGHT                                    3:0
#define NV827D_HEAD_SET_STORAGE_BLOCK_HEIGHT_ONE_GOB                            (0x00000000)
#define NV827D_HEAD_SET_STORAGE_BLOCK_HEIGHT_TWO_GOBS                           (0x00000001)
#define NV827D_HEAD_SET_STORAGE_BLOCK_HEIGHT_FOUR_GOBS                          (0x00000002)
#define NV827D_HEAD_SET_STORAGE_BLOCK_HEIGHT_EIGHT_GOBS                         (0x00000003)
#define NV827D_HEAD_SET_STORAGE_BLOCK_HEIGHT_SIXTEEN_GOBS                       (0x00000004)
#define NV827D_HEAD_SET_STORAGE_BLOCK_HEIGHT_THIRTYTWO_GOBS                     (0x00000005)
#define NV827D_HEAD_SET_STORAGE_PITCH                                           17:8
#define NV827D_HEAD_SET_STORAGE_MEMORY_LAYOUT                                   20:20
#define NV827D_HEAD_SET_STORAGE_MEMORY_LAYOUT_BLOCKLINEAR                       (0x00000000)
#define NV827D_HEAD_SET_STORAGE_MEMORY_LAYOUT_PITCH                             (0x00000001)
#define NV827D_HEAD_SET_PARAMS(a)                                               (0x00000870 + (a)*0x00000400)
#define NV827D_HEAD_SET_PARAMS_FORMAT                                           15:8
#define NV827D_HEAD_SET_PARAMS_FORMAT_I8                                        (0x0000001E)
#define NV827D_HEAD_SET_PARAMS_FORMAT_VOID16                                    (0x0000001F)
#define NV827D_HEAD_SET_PARAMS_FORMAT_VOID32                                    (0x0000002E)
#define NV827D_HEAD_SET_PARAMS_FORMAT_RF16_GF16_BF16_AF16                       (0x000000CA)
#define NV827D_HEAD_SET_PARAMS_FORMAT_A8R8G8B8                                  (0x000000CF)
#define NV827D_HEAD_SET_PARAMS_FORMAT_A2B10G10R10                               (0x000000D1)
#define NV827D_HEAD_SET_PARAMS_FORMAT_A8B8G8R8                                  (0x000000D5)
#define NV827D_HEAD_SET_PARAMS_FORMAT_R5G6B5                                    (0x000000E8)
#define NV827D_HEAD_SET_PARAMS_FORMAT_A1R5G5B5                                  (0x000000E9)
#define NV827D_HEAD_SET_PARAMS_SUPER_SAMPLE                                     1:0
#define NV827D_HEAD_SET_PARAMS_SUPER_SAMPLE_X1_AA                               (0x00000000)
#define NV827D_HEAD_SET_PARAMS_SUPER_SAMPLE_X4_AA                               (0x00000002)
#define NV827D_HEAD_SET_PARAMS_GAMMA                                            2:2
#define NV827D_HEAD_SET_PARAMS_GAMMA_LINEAR                                     (0x00000000)
#define NV827D_HEAD_SET_PARAMS_GAMMA_SRGB                                       (0x00000001)
#define NV827D_HEAD_SET_PARAMS_RESERVED0                                        22:16
#define NV827D_HEAD_SET_PARAMS_RESERVED1                                        24:24
#define NV827D_HEAD_SET_CONTEXT_DMAS_ISO(a,b)                                   (0x00000874 + (a)*0x00000400 + (b)*0x00000004)
#define NV827D_HEAD_SET_CONTEXT_DMAS_ISO_HANDLE                                 31:0
#define NV827D_HEAD_SET_CONTROL_CURSOR(a)                                       (0x00000880 + (a)*0x00000400)
#define NV827D_HEAD_SET_CONTROL_CURSOR_ENABLE                                   31:31
#define NV827D_HEAD_SET_CONTROL_CURSOR_ENABLE_DISABLE                           (0x00000000)
#define NV827D_HEAD_SET_CONTROL_CURSOR_ENABLE_ENABLE                            (0x00000001)
#define NV827D_HEAD_SET_CONTROL_CURSOR_FORMAT                                   25:24
#define NV827D_HEAD_SET_CONTROL_CURSOR_FORMAT_A1R5G5B5                          (0x00000000)
#define NV827D_HEAD_SET_CONTROL_CURSOR_FORMAT_A8R8G8B8                          (0x00000001)
#define NV827D_HEAD_SET_CONTROL_CURSOR_SIZE                                     26:26
#define NV827D_HEAD_SET_CONTROL_CURSOR_SIZE_W32_H32                             (0x00000000)
#define NV827D_HEAD_SET_CONTROL_CURSOR_SIZE_W64_H64                             (0x00000001)
#define NV827D_HEAD_SET_CONTROL_CURSOR_HOT_SPOT_X                               13:8
#define NV827D_HEAD_SET_CONTROL_CURSOR_HOT_SPOT_Y                               21:16
#define NV827D_HEAD_SET_CONTROL_CURSOR_COMPOSITION                              29:28
#define NV827D_HEAD_SET_CONTROL_CURSOR_COMPOSITION_ALPHA_BLEND                  (0x00000000)
#define NV827D_HEAD_SET_CONTROL_CURSOR_COMPOSITION_PREMULT_ALPHA_BLEND          (0x00000001)
#define NV827D_HEAD_SET_CONTROL_CURSOR_COMPOSITION_XOR                          (0x00000002)
#define NV827D_HEAD_SET_CONTROL_CURSOR_SUB_OWNER                                5:4
#define NV827D_HEAD_SET_CONTROL_CURSOR_SUB_OWNER_NONE                           (0x00000000)
#define NV827D_HEAD_SET_CONTROL_CURSOR_SUB_OWNER_SUBHEAD0                       (0x00000001)
#define NV827D_HEAD_SET_CONTROL_CURSOR_SUB_OWNER_SUBHEAD1                       (0x00000002)
#define NV827D_HEAD_SET_CONTROL_CURSOR_SUB_OWNER_BOTH                           (0x00000003)
#define NV827D_HEAD_SET_OFFSET_CURSOR(a)                                        (0x00000884 + (a)*0x00000400)
#define NV827D_HEAD_SET_OFFSET_CURSOR_ORIGIN                                    31:0
#define NV827D_HEAD_SET_CONTEXT_DMA_CURSOR(a)                                   (0x0000089C + (a)*0x00000400)
#define NV827D_HEAD_SET_CONTEXT_DMA_CURSOR_HANDLE                               31:0
#define NV827D_HEAD_SET_VIEWPORT_POINT_IN(a,b)                                  (0x000008C0 + (a)*0x00000400 + (b)*0x00000004)
#define NV827D_HEAD_SET_VIEWPORT_POINT_IN_X                                     14:0
#define NV827D_HEAD_SET_VIEWPORT_POINT_IN_Y                                     30:16
#endif // _cl827d_h
