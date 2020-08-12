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


#ifndef _cl917d_h_
#define _cl917d_h_

// class methods
#define NV917D_SOR_SET_CONTROL(a)                                               (0x00000200 + (a)*0x00000020)
#define NV917D_SOR_SET_CONTROL_OWNER_MASK                                       3:0
#define NV917D_SOR_SET_CONTROL_OWNER_MASK_NONE                                  (0x00000000)
#define NV917D_SOR_SET_CONTROL_OWNER_MASK_HEAD0                                 (0x00000001)
#define NV917D_SOR_SET_CONTROL_OWNER_MASK_HEAD1                                 (0x00000002)
#define NV917D_SOR_SET_CONTROL_OWNER_MASK_HEAD2                                 (0x00000004)
#define NV917D_SOR_SET_CONTROL_OWNER_MASK_HEAD3                                 (0x00000008)
#define NV917D_SOR_SET_CONTROL_PROTOCOL                                         11:8
#define NV917D_SOR_SET_CONTROL_PROTOCOL_LVDS_CUSTOM                             (0x00000000)
#define NV917D_SOR_SET_CONTROL_PROTOCOL_SINGLE_TMDS_A                           (0x00000001)
#define NV917D_SOR_SET_CONTROL_PROTOCOL_SINGLE_TMDS_B                           (0x00000002)
#define NV917D_SOR_SET_CONTROL_PROTOCOL_DUAL_TMDS                               (0x00000005)
#define NV917D_SOR_SET_CONTROL_PROTOCOL_DP_A                                    (0x00000008)
#define NV917D_SOR_SET_CONTROL_PROTOCOL_DP_B                                    (0x00000009)
#define NV917D_SOR_SET_CONTROL_PROTOCOL_CUSTOM                                  (0x0000000F)
#define NV917D_SOR_SET_CONTROL_DE_SYNC_POLARITY                                 14:14
#define NV917D_SOR_SET_CONTROL_DE_SYNC_POLARITY_POSITIVE_TRUE                   (0x00000000)
#define NV917D_SOR_SET_CONTROL_DE_SYNC_POLARITY_NEGATIVE_TRUE                   (0x00000001)
#define NV917D_SOR_SET_CONTROL_PIXEL_REPLICATE_MODE                             21:20
#define NV917D_SOR_SET_CONTROL_PIXEL_REPLICATE_MODE_OFF                         (0x00000000)
#define NV917D_SOR_SET_CONTROL_PIXEL_REPLICATE_MODE_X2                          (0x00000001)
#define NV917D_SOR_SET_CONTROL_PIXEL_REPLICATE_MODE_X4                          (0x00000002)

#define NV917D_HEAD_SET_CONTROL_CURSOR(a)                                       (0x00000480 + (a)*0x00000300)
#define NV917D_HEAD_SET_CONTROL_CURSOR_ENABLE                                   31:31
#define NV917D_HEAD_SET_CONTROL_CURSOR_ENABLE_DISABLE                           (0x00000000)
#define NV917D_HEAD_SET_CONTROL_CURSOR_ENABLE_ENABLE                            (0x00000001)
#define NV917D_HEAD_SET_CONTROL_CURSOR_FORMAT                                   25:24
#define NV917D_HEAD_SET_CONTROL_CURSOR_FORMAT_A1R5G5B5                          (0x00000000)
#define NV917D_HEAD_SET_CONTROL_CURSOR_FORMAT_A8R8G8B8                          (0x00000001)
#define NV917D_HEAD_SET_CONTROL_CURSOR_SIZE                                     27:26
#define NV917D_HEAD_SET_CONTROL_CURSOR_SIZE_W32_H32                             (0x00000000)
#define NV917D_HEAD_SET_CONTROL_CURSOR_SIZE_W64_H64                             (0x00000001)
#define NV917D_HEAD_SET_CONTROL_CURSOR_SIZE_W128_H128                           (0x00000002)
#define NV917D_HEAD_SET_CONTROL_CURSOR_SIZE_W256_H256                           (0x00000003)
#define NV917D_HEAD_SET_CONTROL_CURSOR_HOT_SPOT_X                               15:8
#define NV917D_HEAD_SET_CONTROL_CURSOR_HOT_SPOT_Y                               23:16
#define NV917D_HEAD_SET_CONTROL_CURSOR_COMPOSITION                              29:28
#define NV917D_HEAD_SET_CONTROL_CURSOR_COMPOSITION_ALPHA_BLEND                  (0x00000000)
#define NV917D_HEAD_SET_CONTROL_CURSOR_COMPOSITION_PREMULT_ALPHA_BLEND          (0x00000001)
#define NV917D_HEAD_SET_CONTROL_CURSOR_COMPOSITION_XOR                          (0x00000002)
#define NV917D_HEAD_SET_DITHER_CONTROL(a)                                       (0x000004A0 + (a)*0x00000300)
#define NV917D_HEAD_SET_DITHER_CONTROL_ENABLE                                   0:0
#define NV917D_HEAD_SET_DITHER_CONTROL_ENABLE_DISABLE                           (0x00000000)
#define NV917D_HEAD_SET_DITHER_CONTROL_ENABLE_ENABLE                            (0x00000001)
#define NV917D_HEAD_SET_DITHER_CONTROL_BITS                                     2:1
#define NV917D_HEAD_SET_DITHER_CONTROL_BITS_DITHER_TO_6_BITS                    (0x00000000)
#define NV917D_HEAD_SET_DITHER_CONTROL_BITS_DITHER_TO_8_BITS                    (0x00000001)
#define NV917D_HEAD_SET_DITHER_CONTROL_BITS_DITHER_TO_10_BITS                   (0x00000002)
#define NV917D_HEAD_SET_DITHER_CONTROL_MODE                                     6:3
#define NV917D_HEAD_SET_DITHER_CONTROL_MODE_DYNAMIC_ERR_ACC                     (0x00000000)
#define NV917D_HEAD_SET_DITHER_CONTROL_MODE_STATIC_ERR_ACC                      (0x00000001)
#define NV917D_HEAD_SET_DITHER_CONTROL_MODE_DYNAMIC_2X2                         (0x00000002)
#define NV917D_HEAD_SET_DITHER_CONTROL_MODE_STATIC_2X2                          (0x00000003)
#define NV917D_HEAD_SET_DITHER_CONTROL_MODE_TEMPORAL                            (0x00000004)
#define NV917D_HEAD_SET_DITHER_CONTROL_PHASE                                    8:7
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS(a)                            (0x000004D0 + (a)*0x00000300)
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_USABLE                        0:0
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_USABLE_FALSE                  (0x00000000)
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_USABLE_TRUE                   (0x00000001)
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_PIXEL_DEPTH                   11:8
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_PIXEL_DEPTH_BPP_8             (0x00000000)
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_PIXEL_DEPTH_BPP_16            (0x00000001)
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_PIXEL_DEPTH_BPP_32            (0x00000003)
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_PIXEL_DEPTH_BPP_64            (0x00000005)
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_SUPER_SAMPLE                  13:12
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_SUPER_SAMPLE_X1_AA            (0x00000000)
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_SUPER_SAMPLE_X4_AA            (0x00000002)
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_BASE_LUT                      17:16
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_BASE_LUT_USAGE_NONE           (0x00000000)
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_BASE_LUT_USAGE_257            (0x00000001)
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_BASE_LUT_USAGE_1025           (0x00000002)
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_OUTPUT_LUT                    21:20
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_OUTPUT_LUT_USAGE_NONE         (0x00000000)
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_OUTPUT_LUT_USAGE_257          (0x00000001)
#define NV917D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_OUTPUT_LUT_USAGE_1025         (0x00000002)
#endif // _cl917d_h
