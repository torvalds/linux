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
#ifndef __RV350D_H__
#define __RV350D_H__

/* RV350, RV380 registers */
/* #define R_00000D_SCLK_CNTL                           0x00000D */
#define   S_00000D_FORCE_VAP(x)                        (((x) & 0x1) << 21)
#define   G_00000D_FORCE_VAP(x)                        (((x) >> 21) & 0x1)
#define   C_00000D_FORCE_VAP                           0xFFDFFFFF
#define   S_00000D_FORCE_SR(x)                         (((x) & 0x1) << 25)
#define   G_00000D_FORCE_SR(x)                         (((x) >> 25) & 0x1)
#define   C_00000D_FORCE_SR                            0xFDFFFFFF
#define   S_00000D_FORCE_PX(x)                         (((x) & 0x1) << 26)
#define   G_00000D_FORCE_PX(x)                         (((x) >> 26) & 0x1)
#define   C_00000D_FORCE_PX                            0xFBFFFFFF
#define   S_00000D_FORCE_TX(x)                         (((x) & 0x1) << 27)
#define   G_00000D_FORCE_TX(x)                         (((x) >> 27) & 0x1)
#define   C_00000D_FORCE_TX                            0xF7FFFFFF
#define   S_00000D_FORCE_US(x)                         (((x) & 0x1) << 28)
#define   G_00000D_FORCE_US(x)                         (((x) >> 28) & 0x1)
#define   C_00000D_FORCE_US                            0xEFFFFFFF
#define   S_00000D_FORCE_SU(x)                         (((x) & 0x1) << 30)
#define   G_00000D_FORCE_SU(x)                         (((x) >> 30) & 0x1)
#define   C_00000D_FORCE_SU                            0xBFFFFFFF

#endif
