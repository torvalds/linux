/*
 * Copyright (C) 2020  Advanced Micro Devices, Inc.
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
#ifndef _smuio_11_0_6_SH_MASK_HEADER
#define _smuio_11_0_6_SH_MASK_HEADER


//CGTT_ROM_CLK_CTRL0
#define CGTT_ROM_CLK_CTRL0__ON_DELAY__SHIFT                                                                   0x0
#define CGTT_ROM_CLK_CTRL0__OFF_HYSTERESIS__SHIFT                                                             0x4
#define CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE1__SHIFT                                                             0x1e
#define CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0__SHIFT                                                             0x1f
#define CGTT_ROM_CLK_CTRL0__ON_DELAY_MASK                                                                     0x0000000FL
#define CGTT_ROM_CLK_CTRL0__OFF_HYSTERESIS_MASK                                                               0x00000FF0L
#define CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE1_MASK                                                               0x40000000L
#define CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0_MASK                                                               0x80000000L
//ROM_INDEX
#define ROM_INDEX__ROM_INDEX__SHIFT                                                                           0x0
#define ROM_INDEX__ROM_INDEX_MASK                                                                             0x01FFFFFFL
//ROM_DATA
#define ROM_DATA__ROM_DATA__SHIFT                                                                             0x0
#define ROM_DATA__ROM_DATA_MASK                                                                               0xFFFFFFFFL

#endif
