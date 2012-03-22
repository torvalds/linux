/*
 * Copyright 2009 Advanced Micro Devices, Inc.
 * Copyright 2009 Red Hat Inc.
 * Copyright 2012 Alcatel-Lucent, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __RADEON_BLIT_COMMON_H__

#define DI_PT_RECTLIST        0x11
#define DI_INDEX_SIZE_16_BIT  0x0
#define DI_SRC_SEL_AUTO_INDEX 0x2

#define FMT_8                 0x1
#define FMT_5_6_5             0x8
#define FMT_8_8_8_8           0x1a
#define COLOR_8               0x1
#define COLOR_5_6_5           0x8
#define COLOR_8_8_8_8         0x1a

#define RECT_UNIT_H           32
#define RECT_UNIT_W           (RADEON_GPU_PAGE_SIZE / 4 / RECT_UNIT_H)

#define __RADEON_BLIT_COMMON_H__
#endif
