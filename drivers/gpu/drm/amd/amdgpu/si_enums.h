/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 */
#ifndef SI_ENUMS_H
#define SI_ENUMS_H

#define PRIORITY_MARK_MASK             0x7fff
#define PRIORITY_OFF                   (1 << 16)
#define PRIORITY_ALWAYS_ON             (1 << 20)

#define GFX_POWER_STATUS                           (1 << 1)
#define GFX_CLOCK_STATUS                           (1 << 2)
#define GFX_LS_STATUS                              (1 << 3)

#define RLC_BUSY_STATUS                            (1 << 0)
#define RLC_PUD(x)                               ((x) << 0)
#define RLC_PUD_MASK                             (0xff << 0)
#define RLC_PDD(x)                               ((x) << 8)
#define RLC_PDD_MASK                             (0xff << 8)
#define RLC_TTPD(x)                              ((x) << 16)
#define RLC_TTPD_MASK                            (0xff << 16)
#define RLC_MSD(x)                               ((x) << 24)
#define RLC_MSD_MASK                             (0xff << 24)

#define RLC_SAVE_AND_RESTORE_STARTING_OFFSET 0x90
#define RLC_CLEAR_STATE_DESCRIPTOR_OFFSET    0x3D

#endif
