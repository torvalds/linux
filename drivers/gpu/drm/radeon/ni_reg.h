/*
 * Copyright 2010 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */
#ifndef __NI_REG_H__
#define __NI_REG_H__

/* northern islands - DCE5 */

#define NI_INPUT_GAMMA_CONTROL                         0x6840
#       define NI_GRPH_INPUT_GAMMA_MODE(x)             (((x) & 0x3) << 0)
#       define NI_INPUT_GAMMA_USE_LUT                  0
#       define NI_INPUT_GAMMA_BYPASS                   1
#       define NI_INPUT_GAMMA_SRGB_24                  2
#       define NI_INPUT_GAMMA_XVYCC_222                3
#       define NI_OVL_INPUT_GAMMA_MODE(x)              (((x) & 0x3) << 4)

#define NI_PRESCALE_GRPH_CONTROL                       0x68b4
#       define NI_GRPH_PRESCALE_BYPASS                 (1 << 4)

#define NI_PRESCALE_OVL_CONTROL                        0x68c4
#       define NI_OVL_PRESCALE_BYPASS                  (1 << 4)

#define NI_INPUT_CSC_CONTROL                           0x68d4
#       define NI_INPUT_CSC_GRPH_MODE(x)               (((x) & 0x3) << 0)
#       define NI_INPUT_CSC_BYPASS                     0
#       define NI_INPUT_CSC_PROG_COEFF                 1
#       define NI_INPUT_CSC_PROG_SHARED_MATRIXA        2
#       define NI_INPUT_CSC_OVL_MODE(x)                (((x) & 0x3) << 4)

#define NI_OUTPUT_CSC_CONTROL                          0x68f0
#       define NI_OUTPUT_CSC_GRPH_MODE(x)              (((x) & 0x7) << 0)
#       define NI_OUTPUT_CSC_BYPASS                    0
#       define NI_OUTPUT_CSC_TV_RGB                    1
#       define NI_OUTPUT_CSC_YCBCR_601                 2
#       define NI_OUTPUT_CSC_YCBCR_709                 3
#       define NI_OUTPUT_CSC_PROG_COEFF                4
#       define NI_OUTPUT_CSC_PROG_SHARED_MATRIXB       5
#       define NI_OUTPUT_CSC_OVL_MODE(x)               (((x) & 0x7) << 4)

#define NI_DEGAMMA_CONTROL                             0x6960
#       define NI_GRPH_DEGAMMA_MODE(x)                 (((x) & 0x3) << 0)
#       define NI_DEGAMMA_BYPASS                       0
#       define NI_DEGAMMA_SRGB_24                      1
#       define NI_DEGAMMA_XVYCC_222                    2
#       define NI_OVL_DEGAMMA_MODE(x)                  (((x) & 0x3) << 4)
#       define NI_ICON_DEGAMMA_MODE(x)                 (((x) & 0x3) << 8)
#       define NI_CURSOR_DEGAMMA_MODE(x)               (((x) & 0x3) << 12)

#define NI_GAMUT_REMAP_CONTROL                         0x6964
#       define NI_GRPH_GAMUT_REMAP_MODE(x)             (((x) & 0x3) << 0)
#       define NI_GAMUT_REMAP_BYPASS                   0
#       define NI_GAMUT_REMAP_PROG_COEFF               1
#       define NI_GAMUT_REMAP_PROG_SHARED_MATRIXA      2
#       define NI_GAMUT_REMAP_PROG_SHARED_MATRIXB      3
#       define NI_OVL_GAMUT_REMAP_MODE(x)              (((x) & 0x3) << 4)

#define NI_REGAMMA_CONTROL                             0x6a80
#       define NI_GRPH_REGAMMA_MODE(x)                 (((x) & 0x7) << 0)
#       define NI_REGAMMA_BYPASS                       0
#       define NI_REGAMMA_SRGB_24                      1
#       define NI_REGAMMA_XVYCC_222                    2
#       define NI_REGAMMA_PROG_A                       3
#       define NI_REGAMMA_PROG_B                       4
#       define NI_OVL_REGAMMA_MODE(x)                  (((x) & 0x7) << 4)

#define NI_DP_MSE_LINK_TIMING                          0x73a0
#	define NI_DP_MSE_LINK_FRAME			(((x) & 0x3ff) << 0)
#	define NI_DP_MSE_LINK_LINE                      (((x) & 0x3) << 16)

#define NI_DP_MSE_MISC_CNTL                            0x736c
#       define NI_DP_MSE_BLANK_CODE                    (((x) & 0x1) << 0)
#       define NI_DP_MSE_TIMESTAMP_MODE                (((x) & 0x1) << 4)
#       define NI_DP_MSE_ZERO_ENCODER                  (((x) & 0x1) << 8)

#define NI_DP_MSE_RATE_CNTL                            0x7384
#       define NI_DP_MSE_RATE_Y(x)                   (((x) & 0x3ffffff) << 0)
#       define NI_DP_MSE_RATE_X(x)                   (((x) & 0x3f) << 26)

#define NI_DP_MSE_RATE_UPDATE                          0x738c

#define NI_DP_MSE_SAT0                                 0x7390
#       define NI_DP_MSE_SAT_SRC0(x)                   (((x) & 0x7) << 0)
#       define NI_DP_MSE_SAT_SLOT_COUNT0(x)            (((x) & 0x3f) << 8)
#       define NI_DP_MSE_SAT_SRC1(x)                   (((x) & 0x7) << 16)
#       define NI_DP_MSE_SAT_SLOT_COUNT1(x)            (((x) & 0x3f) << 24)

#define NI_DP_MSE_SAT1                                 0x7394

#define NI_DP_MSE_SAT2                                 0x7398

#define NI_DP_MSE_SAT_UPDATE                           0x739c

#define NI_DIG_BE_CNTL                                 0x7140
#       define NI_DIG_FE_SOURCE_SELECT(x)              (((x) & 0x7f) << 8)
#       define NI_DIG_FE_DIG_MODE(x)                   (((x) & 0x7) << 16)
#       define NI_DIG_MODE_DP_SST                      0
#       define NI_DIG_MODE_LVDS                        1
#       define NI_DIG_MODE_TMDS_DVI                    2
#       define NI_DIG_MODE_TMDS_HDMI                   3
#       define NI_DIG_MODE_DP_MST                      5
#       define NI_DIG_HPD_SELECT(x)                    (((x) & 0x7) << 28)

#define NI_DIG_FE_CNTL                                 0x7000
#       define NI_DIG_SOURCE_SELECT(x)                 (((x) & 0x3) << 0)
#       define NI_DIG_STEREOSYNC_SELECT(x)             (((x) & 0x3) << 4)
#       define NI_DIG_STEREOSYNC_GATE_EN(x)            (((x) & 0x1) << 8)
#       define NI_DIG_DUAL_LINK_ENABLE(x)              (((x) & 0x1) << 16)
#       define NI_DIG_SWAP(x)                          (((x) & 0x1) << 18)
#       define NI_DIG_SYMCLK_FE_ON                     (0x1 << 24)
#endif
