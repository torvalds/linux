/*
 * Copyright (C) 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _clk_11_0_1_SH_MASK_HEADER
#define _clk_11_0_1_SH_MASK_HEADER

//CLK4_0_CLK4_CLK_PLL_REQ
#define CLK4_0_CLK4_CLK_PLL_REQ__FbMult_int__SHIFT                                                            0x0
#define CLK4_0_CLK4_CLK_PLL_REQ__PllSpineDiv__SHIFT                                                           0xc
#define CLK4_0_CLK4_CLK_PLL_REQ__FbMult_frac__SHIFT                                                           0x10
#define CLK4_0_CLK4_CLK_PLL_REQ__FbMult_int_MASK                                                              0x000001FFL
#define CLK4_0_CLK4_CLK_PLL_REQ__PllSpineDiv_MASK                                                             0x0000F000L
#define CLK4_0_CLK4_CLK_PLL_REQ__FbMult_frac_MASK                                                             0xFFFF0000L

//CLK4_0_CLK4_CLK2_CURRENT_CNT
#define CLK4_0_CLK4_CLK2_CURRENT_CNT__CURRENT_COUNT__SHIFT                                                    0x0
#define CLK4_0_CLK4_CLK2_CURRENT_CNT__CURRENT_COUNT_MASK                                                      0xFFFFFFFFL

#endif