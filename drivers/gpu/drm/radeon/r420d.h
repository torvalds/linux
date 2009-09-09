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
#ifndef R420D_H
#define R420D_H

#define R_0001F8_MC_IND_INDEX                        0x0001F8
#define   S_0001F8_MC_IND_ADDR(x)                      (((x) & 0x7F) << 0)
#define   G_0001F8_MC_IND_ADDR(x)                      (((x) >> 0) & 0x7F)
#define   C_0001F8_MC_IND_ADDR                         0xFFFFFF80
#define   S_0001F8_MC_IND_WR_EN(x)                     (((x) & 0x1) << 8)
#define   G_0001F8_MC_IND_WR_EN(x)                     (((x) >> 8) & 0x1)
#define   C_0001F8_MC_IND_WR_EN                        0xFFFFFEFF
#define R_0001FC_MC_IND_DATA                         0x0001FC
#define   S_0001FC_MC_IND_DATA(x)                      (((x) & 0xFFFFFFFF) << 0)
#define   G_0001FC_MC_IND_DATA(x)                      (((x) >> 0) & 0xFFFFFFFF)
#define   C_0001FC_MC_IND_DATA                         0x00000000

#endif
