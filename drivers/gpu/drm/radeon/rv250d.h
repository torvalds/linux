/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#ifndef __RV250D_H__
#define __RV250D_H__

#define R_00000D_SCLK_CNTL_M6                        0x00000D
#define   S_00000D_SCLK_SRC_SEL(x)                     (((x) & 0x7) << 0)
#define   G_00000D_SCLK_SRC_SEL(x)                     (((x) >> 0) & 0x7)
#define   C_00000D_SCLK_SRC_SEL                        0xFFFFFFF8
#define   S_00000D_CP_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 3)
#define   G_00000D_CP_MAX_DYN_STOP_LAT(x)              (((x) >> 3) & 0x1)
#define   C_00000D_CP_MAX_DYN_STOP_LAT                 0xFFFFFFF7
#define   S_00000D_HDP_MAX_DYN_STOP_LAT(x)             (((x) & 0x1) << 4)
#define   G_00000D_HDP_MAX_DYN_STOP_LAT(x)             (((x) >> 4) & 0x1)
#define   C_00000D_HDP_MAX_DYN_STOP_LAT                0xFFFFFFEF
#define   S_00000D_TV_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 5)
#define   G_00000D_TV_MAX_DYN_STOP_LAT(x)              (((x) >> 5) & 0x1)
#define   C_00000D_TV_MAX_DYN_STOP_LAT                 0xFFFFFFDF
#define   S_00000D_E2_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 6)
#define   G_00000D_E2_MAX_DYN_STOP_LAT(x)              (((x) >> 6) & 0x1)
#define   C_00000D_E2_MAX_DYN_STOP_LAT                 0xFFFFFFBF
#define   S_00000D_SE_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 7)
#define   G_00000D_SE_MAX_DYN_STOP_LAT(x)              (((x) >> 7) & 0x1)
#define   C_00000D_SE_MAX_DYN_STOP_LAT                 0xFFFFFF7F
#define   S_00000D_IDCT_MAX_DYN_STOP_LAT(x)            (((x) & 0x1) << 8)
#define   G_00000D_IDCT_MAX_DYN_STOP_LAT(x)            (((x) >> 8) & 0x1)
#define   C_00000D_IDCT_MAX_DYN_STOP_LAT               0xFFFFFEFF
#define   S_00000D_VIP_MAX_DYN_STOP_LAT(x)             (((x) & 0x1) << 9)
#define   G_00000D_VIP_MAX_DYN_STOP_LAT(x)             (((x) >> 9) & 0x1)
#define   C_00000D_VIP_MAX_DYN_STOP_LAT                0xFFFFFDFF
#define   S_00000D_RE_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 10)
#define   G_00000D_RE_MAX_DYN_STOP_LAT(x)              (((x) >> 10) & 0x1)
#define   C_00000D_RE_MAX_DYN_STOP_LAT                 0xFFFFFBFF
#define   S_00000D_PB_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 11)
#define   G_00000D_PB_MAX_DYN_STOP_LAT(x)              (((x) >> 11) & 0x1)
#define   C_00000D_PB_MAX_DYN_STOP_LAT                 0xFFFFF7FF
#define   S_00000D_TAM_MAX_DYN_STOP_LAT(x)             (((x) & 0x1) << 12)
#define   G_00000D_TAM_MAX_DYN_STOP_LAT(x)             (((x) >> 12) & 0x1)
#define   C_00000D_TAM_MAX_DYN_STOP_LAT                0xFFFFEFFF
#define   S_00000D_TDM_MAX_DYN_STOP_LAT(x)             (((x) & 0x1) << 13)
#define   G_00000D_TDM_MAX_DYN_STOP_LAT(x)             (((x) >> 13) & 0x1)
#define   C_00000D_TDM_MAX_DYN_STOP_LAT                0xFFFFDFFF
#define   S_00000D_RB_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 14)
#define   G_00000D_RB_MAX_DYN_STOP_LAT(x)              (((x) >> 14) & 0x1)
#define   C_00000D_RB_MAX_DYN_STOP_LAT                 0xFFFFBFFF
#define   S_00000D_FORCE_DISP2(x)                      (((x) & 0x1) << 15)
#define   G_00000D_FORCE_DISP2(x)                      (((x) >> 15) & 0x1)
#define   C_00000D_FORCE_DISP2                         0xFFFF7FFF
#define   S_00000D_FORCE_CP(x)                         (((x) & 0x1) << 16)
#define   G_00000D_FORCE_CP(x)                         (((x) >> 16) & 0x1)
#define   C_00000D_FORCE_CP                            0xFFFEFFFF
#define   S_00000D_FORCE_HDP(x)                        (((x) & 0x1) << 17)
#define   G_00000D_FORCE_HDP(x)                        (((x) >> 17) & 0x1)
#define   C_00000D_FORCE_HDP                           0xFFFDFFFF
#define   S_00000D_FORCE_DISP1(x)                      (((x) & 0x1) << 18)
#define   G_00000D_FORCE_DISP1(x)                      (((x) >> 18) & 0x1)
#define   C_00000D_FORCE_DISP1                         0xFFFBFFFF
#define   S_00000D_FORCE_TOP(x)                        (((x) & 0x1) << 19)
#define   G_00000D_FORCE_TOP(x)                        (((x) >> 19) & 0x1)
#define   C_00000D_FORCE_TOP                           0xFFF7FFFF
#define   S_00000D_FORCE_E2(x)                         (((x) & 0x1) << 20)
#define   G_00000D_FORCE_E2(x)                         (((x) >> 20) & 0x1)
#define   C_00000D_FORCE_E2                            0xFFEFFFFF
#define   S_00000D_FORCE_SE(x)                         (((x) & 0x1) << 21)
#define   G_00000D_FORCE_SE(x)                         (((x) >> 21) & 0x1)
#define   C_00000D_FORCE_SE                            0xFFDFFFFF
#define   S_00000D_FORCE_IDCT(x)                       (((x) & 0x1) << 22)
#define   G_00000D_FORCE_IDCT(x)                       (((x) >> 22) & 0x1)
#define   C_00000D_FORCE_IDCT                          0xFFBFFFFF
#define   S_00000D_FORCE_VIP(x)                        (((x) & 0x1) << 23)
#define   G_00000D_FORCE_VIP(x)                        (((x) >> 23) & 0x1)
#define   C_00000D_FORCE_VIP                           0xFF7FFFFF
#define   S_00000D_FORCE_RE(x)                         (((x) & 0x1) << 24)
#define   G_00000D_FORCE_RE(x)                         (((x) >> 24) & 0x1)
#define   C_00000D_FORCE_RE                            0xFEFFFFFF
#define   S_00000D_FORCE_PB(x)                         (((x) & 0x1) << 25)
#define   G_00000D_FORCE_PB(x)                         (((x) >> 25) & 0x1)
#define   C_00000D_FORCE_PB                            0xFDFFFFFF
#define   S_00000D_FORCE_TAM(x)                        (((x) & 0x1) << 26)
#define   G_00000D_FORCE_TAM(x)                        (((x) >> 26) & 0x1)
#define   C_00000D_FORCE_TAM                           0xFBFFFFFF
#define   S_00000D_FORCE_TDM(x)                        (((x) & 0x1) << 27)
#define   G_00000D_FORCE_TDM(x)                        (((x) >> 27) & 0x1)
#define   C_00000D_FORCE_TDM                           0xF7FFFFFF
#define   S_00000D_FORCE_RB(x)                         (((x) & 0x1) << 28)
#define   G_00000D_FORCE_RB(x)                         (((x) >> 28) & 0x1)
#define   C_00000D_FORCE_RB                            0xEFFFFFFF
#define   S_00000D_FORCE_TV_SCLK(x)                    (((x) & 0x1) << 29)
#define   G_00000D_FORCE_TV_SCLK(x)                    (((x) >> 29) & 0x1)
#define   C_00000D_FORCE_TV_SCLK                       0xDFFFFFFF
#define   S_00000D_FORCE_SUBPIC(x)                     (((x) & 0x1) << 30)
#define   G_00000D_FORCE_SUBPIC(x)                     (((x) >> 30) & 0x1)
#define   C_00000D_FORCE_SUBPIC                        0xBFFFFFFF
#define   S_00000D_FORCE_OV0(x)                        (((x) & 0x1) << 31)
#define   G_00000D_FORCE_OV0(x)                        (((x) >> 31) & 0x1)
#define   C_00000D_FORCE_OV0                           0x7FFFFFFF

#endif
