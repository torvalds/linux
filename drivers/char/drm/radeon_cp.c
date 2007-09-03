/* radeon_cp.c -- CP support for Radeon -*- linux-c -*- */
/*
 * Copyright 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
 * All Rights Reserved.
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Kevin E. Martin <martin@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */

#include "drmP.h"
#include "drm.h"
#include "radeon_drm.h"
#include "radeon_drv.h"
#include "r300_reg.h"

#define RADEON_FIFO_DEBUG	0

static int radeon_do_cleanup_cp(struct drm_device * dev);

/* CP microcode (from ATI) */
static const u32 R200_cp_microcode[][2] = {
	{0x21007000, 0000000000},
	{0x20007000, 0000000000},
	{0x000000ab, 0x00000004},
	{0x000000af, 0x00000004},
	{0x66544a49, 0000000000},
	{0x49494174, 0000000000},
	{0x54517d83, 0000000000},
	{0x498d8b64, 0000000000},
	{0x49494949, 0000000000},
	{0x49da493c, 0000000000},
	{0x49989898, 0000000000},
	{0xd34949d5, 0000000000},
	{0x9dc90e11, 0000000000},
	{0xce9b9b9b, 0000000000},
	{0x000f0000, 0x00000016},
	{0x352e232c, 0000000000},
	{0x00000013, 0x00000004},
	{0x000f0000, 0x00000016},
	{0x352e272c, 0000000000},
	{0x000f0001, 0x00000016},
	{0x3239362f, 0000000000},
	{0x000077ef, 0x00000002},
	{0x00061000, 0x00000002},
	{0x00000020, 0x0000001a},
	{0x00004000, 0x0000001e},
	{0x00061000, 0x00000002},
	{0x00000020, 0x0000001a},
	{0x00004000, 0x0000001e},
	{0x00061000, 0x00000002},
	{0x00000020, 0x0000001a},
	{0x00004000, 0x0000001e},
	{0x00000016, 0x00000004},
	{0x0003802a, 0x00000002},
	{0x040067e0, 0x00000002},
	{0x00000016, 0x00000004},
	{0x000077e0, 0x00000002},
	{0x00065000, 0x00000002},
	{0x000037e1, 0x00000002},
	{0x040067e1, 0x00000006},
	{0x000077e0, 0x00000002},
	{0x000077e1, 0x00000002},
	{0x000077e1, 0x00000006},
	{0xffffffff, 0000000000},
	{0x10000000, 0000000000},
	{0x0003802a, 0x00000002},
	{0x040067e0, 0x00000006},
	{0x00007675, 0x00000002},
	{0x00007676, 0x00000002},
	{0x00007677, 0x00000002},
	{0x00007678, 0x00000006},
	{0x0003802b, 0x00000002},
	{0x04002676, 0x00000002},
	{0x00007677, 0x00000002},
	{0x00007678, 0x00000006},
	{0x0000002e, 0x00000018},
	{0x0000002e, 0x00000018},
	{0000000000, 0x00000006},
	{0x0000002f, 0x00000018},
	{0x0000002f, 0x00000018},
	{0000000000, 0x00000006},
	{0x01605000, 0x00000002},
	{0x00065000, 0x00000002},
	{0x00098000, 0x00000002},
	{0x00061000, 0x00000002},
	{0x64c0603d, 0x00000004},
	{0x00080000, 0x00000016},
	{0000000000, 0000000000},
	{0x0400251d, 0x00000002},
	{0x00007580, 0x00000002},
	{0x00067581, 0x00000002},
	{0x04002580, 0x00000002},
	{0x00067581, 0x00000002},
	{0x00000046, 0x00000004},
	{0x00005000, 0000000000},
	{0x00061000, 0x00000002},
	{0x0000750e, 0x00000002},
	{0x00019000, 0x00000002},
	{0x00011055, 0x00000014},
	{0x00000055, 0x00000012},
	{0x0400250f, 0x00000002},
	{0x0000504a, 0x00000004},
	{0x00007565, 0x00000002},
	{0x00007566, 0x00000002},
	{0x00000051, 0x00000004},
	{0x01e655b4, 0x00000002},
	{0x4401b0dc, 0x00000002},
	{0x01c110dc, 0x00000002},
	{0x2666705d, 0x00000018},
	{0x040c2565, 0x00000002},
	{0x0000005d, 0x00000018},
	{0x04002564, 0x00000002},
	{0x00007566, 0x00000002},
	{0x00000054, 0x00000004},
	{0x00401060, 0x00000008},
	{0x00101000, 0x00000002},
	{0x000d80ff, 0x00000002},
	{0x00800063, 0x00000008},
	{0x000f9000, 0x00000002},
	{0x000e00ff, 0x00000002},
	{0000000000, 0x00000006},
	{0x00000080, 0x00000018},
	{0x00000054, 0x00000004},
	{0x00007576, 0x00000002},
	{0x00065000, 0x00000002},
	{0x00009000, 0x00000002},
	{0x00041000, 0x00000002},
	{0x0c00350e, 0x00000002},
	{0x00049000, 0x00000002},
	{0x00051000, 0x00000002},
	{0x01e785f8, 0x00000002},
	{0x00200000, 0x00000002},
	{0x00600073, 0x0000000c},
	{0x00007563, 0x00000002},
	{0x006075f0, 0x00000021},
	{0x20007068, 0x00000004},
	{0x00005068, 0x00000004},
	{0x00007576, 0x00000002},
	{0x00007577, 0x00000002},
	{0x0000750e, 0x00000002},
	{0x0000750f, 0x00000002},
	{0x00a05000, 0x00000002},
	{0x00600076, 0x0000000c},
	{0x006075f0, 0x00000021},
	{0x000075f8, 0x00000002},
	{0x00000076, 0x00000004},
	{0x000a750e, 0x00000002},
	{0x0020750f, 0x00000002},
	{0x00600079, 0x00000004},
	{0x00007570, 0x00000002},
	{0x00007571, 0x00000002},
	{0x00007572, 0x00000006},
	{0x00005000, 0x00000002},
	{0x00a05000, 0x00000002},
	{0x00007568, 0x00000002},
	{0x00061000, 0x00000002},
	{0x00000084, 0x0000000c},
	{0x00058000, 0x00000002},
	{0x0c607562, 0x00000002},
	{0x00000086, 0x00000004},
	{0x00600085, 0x00000004},
	{0x400070dd, 0000000000},
	{0x000380dd, 0x00000002},
	{0x00000093, 0x0000001c},
	{0x00065095, 0x00000018},
	{0x040025bb, 0x00000002},
	{0x00061096, 0x00000018},
	{0x040075bc, 0000000000},
	{0x000075bb, 0x00000002},
	{0x000075bc, 0000000000},
	{0x00090000, 0x00000006},
	{0x00090000, 0x00000002},
	{0x000d8002, 0x00000006},
	{0x00005000, 0x00000002},
	{0x00007821, 0x00000002},
	{0x00007800, 0000000000},
	{0x00007821, 0x00000002},
	{0x00007800, 0000000000},
	{0x01665000, 0x00000002},
	{0x000a0000, 0x00000002},
	{0x000671cc, 0x00000002},
	{0x0286f1cd, 0x00000002},
	{0x000000a3, 0x00000010},
	{0x21007000, 0000000000},
	{0x000000aa, 0x0000001c},
	{0x00065000, 0x00000002},
	{0x000a0000, 0x00000002},
	{0x00061000, 0x00000002},
	{0x000b0000, 0x00000002},
	{0x38067000, 0x00000002},
	{0x000a00a6, 0x00000004},
	{0x20007000, 0000000000},
	{0x01200000, 0x00000002},
	{0x20077000, 0x00000002},
	{0x01200000, 0x00000002},
	{0x20007000, 0000000000},
	{0x00061000, 0x00000002},
	{0x0120751b, 0x00000002},
	{0x8040750a, 0x00000002},
	{0x8040750b, 0x00000002},
	{0x00110000, 0x00000002},
	{0x000380dd, 0x00000002},
	{0x000000bd, 0x0000001c},
	{0x00061096, 0x00000018},
	{0x844075bd, 0x00000002},
	{0x00061095, 0x00000018},
	{0x840075bb, 0x00000002},
	{0x00061096, 0x00000018},
	{0x844075bc, 0x00000002},
	{0x000000c0, 0x00000004},
	{0x804075bd, 0x00000002},
	{0x800075bb, 0x00000002},
	{0x804075bc, 0x00000002},
	{0x00108000, 0x00000002},
	{0x01400000, 0x00000002},
	{0x006000c4, 0x0000000c},
	{0x20c07000, 0x00000020},
	{0x000000c6, 0x00000012},
	{0x00800000, 0x00000006},
	{0x0080751d, 0x00000006},
	{0x000025bb, 0x00000002},
	{0x000040c0, 0x00000004},
	{0x0000775c, 0x00000002},
	{0x00a05000, 0x00000002},
	{0x00661000, 0x00000002},
	{0x0460275d, 0x00000020},
	{0x00004000, 0000000000},
	{0x00007999, 0x00000002},
	{0x00a05000, 0x00000002},
	{0x00661000, 0x00000002},
	{0x0460299b, 0x00000020},
	{0x00004000, 0000000000},
	{0x01e00830, 0x00000002},
	{0x21007000, 0000000000},
	{0x00005000, 0x00000002},
	{0x00038042, 0x00000002},
	{0x040025e0, 0x00000002},
	{0x000075e1, 0000000000},
	{0x00000001, 0000000000},
	{0x000380d9, 0x00000002},
	{0x04007394, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
};

static const u32 radeon_cp_microcode[][2] = {
	{0x21007000, 0000000000},
	{0x20007000, 0000000000},
	{0x000000b4, 0x00000004},
	{0x000000b8, 0x00000004},
	{0x6f5b4d4c, 0000000000},
	{0x4c4c427f, 0000000000},
	{0x5b568a92, 0000000000},
	{0x4ca09c6d, 0000000000},
	{0xad4c4c4c, 0000000000},
	{0x4ce1af3d, 0000000000},
	{0xd8afafaf, 0000000000},
	{0xd64c4cdc, 0000000000},
	{0x4cd10d10, 0000000000},
	{0x000f0000, 0x00000016},
	{0x362f242d, 0000000000},
	{0x00000012, 0x00000004},
	{0x000f0000, 0x00000016},
	{0x362f282d, 0000000000},
	{0x000380e7, 0x00000002},
	{0x04002c97, 0x00000002},
	{0x000f0001, 0x00000016},
	{0x333a3730, 0000000000},
	{0x000077ef, 0x00000002},
	{0x00061000, 0x00000002},
	{0x00000021, 0x0000001a},
	{0x00004000, 0x0000001e},
	{0x00061000, 0x00000002},
	{0x00000021, 0x0000001a},
	{0x00004000, 0x0000001e},
	{0x00061000, 0x00000002},
	{0x00000021, 0x0000001a},
	{0x00004000, 0x0000001e},
	{0x00000017, 0x00000004},
	{0x0003802b, 0x00000002},
	{0x040067e0, 0x00000002},
	{0x00000017, 0x00000004},
	{0x000077e0, 0x00000002},
	{0x00065000, 0x00000002},
	{0x000037e1, 0x00000002},
	{0x040067e1, 0x00000006},
	{0x000077e0, 0x00000002},
	{0x000077e1, 0x00000002},
	{0x000077e1, 0x00000006},
	{0xffffffff, 0000000000},
	{0x10000000, 0000000000},
	{0x0003802b, 0x00000002},
	{0x040067e0, 0x00000006},
	{0x00007675, 0x00000002},
	{0x00007676, 0x00000002},
	{0x00007677, 0x00000002},
	{0x00007678, 0x00000006},
	{0x0003802c, 0x00000002},
	{0x04002676, 0x00000002},
	{0x00007677, 0x00000002},
	{0x00007678, 0x00000006},
	{0x0000002f, 0x00000018},
	{0x0000002f, 0x00000018},
	{0000000000, 0x00000006},
	{0x00000030, 0x00000018},
	{0x00000030, 0x00000018},
	{0000000000, 0x00000006},
	{0x01605000, 0x00000002},
	{0x00065000, 0x00000002},
	{0x00098000, 0x00000002},
	{0x00061000, 0x00000002},
	{0x64c0603e, 0x00000004},
	{0x000380e6, 0x00000002},
	{0x040025c5, 0x00000002},
	{0x00080000, 0x00000016},
	{0000000000, 0000000000},
	{0x0400251d, 0x00000002},
	{0x00007580, 0x00000002},
	{0x00067581, 0x00000002},
	{0x04002580, 0x00000002},
	{0x00067581, 0x00000002},
	{0x00000049, 0x00000004},
	{0x00005000, 0000000000},
	{0x000380e6, 0x00000002},
	{0x040025c5, 0x00000002},
	{0x00061000, 0x00000002},
	{0x0000750e, 0x00000002},
	{0x00019000, 0x00000002},
	{0x00011055, 0x00000014},
	{0x00000055, 0x00000012},
	{0x0400250f, 0x00000002},
	{0x0000504f, 0x00000004},
	{0x000380e6, 0x00000002},
	{0x040025c5, 0x00000002},
	{0x00007565, 0x00000002},
	{0x00007566, 0x00000002},
	{0x00000058, 0x00000004},
	{0x000380e6, 0x00000002},
	{0x040025c5, 0x00000002},
	{0x01e655b4, 0x00000002},
	{0x4401b0e4, 0x00000002},
	{0x01c110e4, 0x00000002},
	{0x26667066, 0x00000018},
	{0x040c2565, 0x00000002},
	{0x00000066, 0x00000018},
	{0x04002564, 0x00000002},
	{0x00007566, 0x00000002},
	{0x0000005d, 0x00000004},
	{0x00401069, 0x00000008},
	{0x00101000, 0x00000002},
	{0x000d80ff, 0x00000002},
	{0x0080006c, 0x00000008},
	{0x000f9000, 0x00000002},
	{0x000e00ff, 0x00000002},
	{0000000000, 0x00000006},
	{0x0000008f, 0x00000018},
	{0x0000005b, 0x00000004},
	{0x000380e6, 0x00000002},
	{0x040025c5, 0x00000002},
	{0x00007576, 0x00000002},
	{0x00065000, 0x00000002},
	{0x00009000, 0x00000002},
	{0x00041000, 0x00000002},
	{0x0c00350e, 0x00000002},
	{0x00049000, 0x00000002},
	{0x00051000, 0x00000002},
	{0x01e785f8, 0x00000002},
	{0x00200000, 0x00000002},
	{0x0060007e, 0x0000000c},
	{0x00007563, 0x00000002},
	{0x006075f0, 0x00000021},
	{0x20007073, 0x00000004},
	{0x00005073, 0x00000004},
	{0x000380e6, 0x00000002},
	{0x040025c5, 0x00000002},
	{0x00007576, 0x00000002},
	{0x00007577, 0x00000002},
	{0x0000750e, 0x00000002},
	{0x0000750f, 0x00000002},
	{0x00a05000, 0x00000002},
	{0x00600083, 0x0000000c},
	{0x006075f0, 0x00000021},
	{0x000075f8, 0x00000002},
	{0x00000083, 0x00000004},
	{0x000a750e, 0x00000002},
	{0x000380e6, 0x00000002},
	{0x040025c5, 0x00000002},
	{0x0020750f, 0x00000002},
	{0x00600086, 0x00000004},
	{0x00007570, 0x00000002},
	{0x00007571, 0x00000002},
	{0x00007572, 0x00000006},
	{0x000380e6, 0x00000002},
	{0x040025c5, 0x00000002},
	{0x00005000, 0x00000002},
	{0x00a05000, 0x00000002},
	{0x00007568, 0x00000002},
	{0x00061000, 0x00000002},
	{0x00000095, 0x0000000c},
	{0x00058000, 0x00000002},
	{0x0c607562, 0x00000002},
	{0x00000097, 0x00000004},
	{0x000380e6, 0x00000002},
	{0x040025c5, 0x00000002},
	{0x00600096, 0x00000004},
	{0x400070e5, 0000000000},
	{0x000380e6, 0x00000002},
	{0x040025c5, 0x00000002},
	{0x000380e5, 0x00000002},
	{0x000000a8, 0x0000001c},
	{0x000650aa, 0x00000018},
	{0x040025bb, 0x00000002},
	{0x000610ab, 0x00000018},
	{0x040075bc, 0000000000},
	{0x000075bb, 0x00000002},
	{0x000075bc, 0000000000},
	{0x00090000, 0x00000006},
	{0x00090000, 0x00000002},
	{0x000d8002, 0x00000006},
	{0x00007832, 0x00000002},
	{0x00005000, 0x00000002},
	{0x000380e7, 0x00000002},
	{0x04002c97, 0x00000002},
	{0x00007820, 0x00000002},
	{0x00007821, 0x00000002},
	{0x00007800, 0000000000},
	{0x01200000, 0x00000002},
	{0x20077000, 0x00000002},
	{0x01200000, 0x00000002},
	{0x20007000, 0x00000002},
	{0x00061000, 0x00000002},
	{0x0120751b, 0x00000002},
	{0x8040750a, 0x00000002},
	{0x8040750b, 0x00000002},
	{0x00110000, 0x00000002},
	{0x000380e5, 0x00000002},
	{0x000000c6, 0x0000001c},
	{0x000610ab, 0x00000018},
	{0x844075bd, 0x00000002},
	{0x000610aa, 0x00000018},
	{0x840075bb, 0x00000002},
	{0x000610ab, 0x00000018},
	{0x844075bc, 0x00000002},
	{0x000000c9, 0x00000004},
	{0x804075bd, 0x00000002},
	{0x800075bb, 0x00000002},
	{0x804075bc, 0x00000002},
	{0x00108000, 0x00000002},
	{0x01400000, 0x00000002},
	{0x006000cd, 0x0000000c},
	{0x20c07000, 0x00000020},
	{0x000000cf, 0x00000012},
	{0x00800000, 0x00000006},
	{0x0080751d, 0x00000006},
	{0000000000, 0000000000},
	{0x0000775c, 0x00000002},
	{0x00a05000, 0x00000002},
	{0x00661000, 0x00000002},
	{0x0460275d, 0x00000020},
	{0x00004000, 0000000000},
	{0x01e00830, 0x00000002},
	{0x21007000, 0000000000},
	{0x6464614d, 0000000000},
	{0x69687420, 0000000000},
	{0x00000073, 0000000000},
	{0000000000, 0000000000},
	{0x00005000, 0x00000002},
	{0x000380d0, 0x00000002},
	{0x040025e0, 0x00000002},
	{0x000075e1, 0000000000},
	{0x00000001, 0000000000},
	{0x000380e0, 0x00000002},
	{0x04002394, 0x00000002},
	{0x00005000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0x00000008, 0000000000},
	{0x00000004, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
};

static const u32 R300_cp_microcode[][2] = {
	{0x4200e000, 0000000000},
	{0x4000e000, 0000000000},
	{0x000000af, 0x00000008},
	{0x000000b3, 0x00000008},
	{0x6c5a504f, 0000000000},
	{0x4f4f497a, 0000000000},
	{0x5a578288, 0000000000},
	{0x4f91906a, 0000000000},
	{0x4f4f4f4f, 0000000000},
	{0x4fe24f44, 0000000000},
	{0x4f9c9c9c, 0000000000},
	{0xdc4f4fde, 0000000000},
	{0xa1cd4f4f, 0000000000},
	{0xd29d9d9d, 0000000000},
	{0x4f0f9fd7, 0000000000},
	{0x000ca000, 0x00000004},
	{0x000d0012, 0x00000038},
	{0x0000e8b4, 0x00000004},
	{0x000d0014, 0x00000038},
	{0x0000e8b6, 0x00000004},
	{0x000d0016, 0x00000038},
	{0x0000e854, 0x00000004},
	{0x000d0018, 0x00000038},
	{0x0000e855, 0x00000004},
	{0x000d001a, 0x00000038},
	{0x0000e856, 0x00000004},
	{0x000d001c, 0x00000038},
	{0x0000e857, 0x00000004},
	{0x000d001e, 0x00000038},
	{0x0000e824, 0x00000004},
	{0x000d0020, 0x00000038},
	{0x0000e825, 0x00000004},
	{0x000d0022, 0x00000038},
	{0x0000e830, 0x00000004},
	{0x000d0024, 0x00000038},
	{0x0000f0c0, 0x00000004},
	{0x000d0026, 0x00000038},
	{0x0000f0c1, 0x00000004},
	{0x000d0028, 0x00000038},
	{0x0000f041, 0x00000004},
	{0x000d002a, 0x00000038},
	{0x0000f184, 0x00000004},
	{0x000d002c, 0x00000038},
	{0x0000f185, 0x00000004},
	{0x000d002e, 0x00000038},
	{0x0000f186, 0x00000004},
	{0x000d0030, 0x00000038},
	{0x0000f187, 0x00000004},
	{0x000d0032, 0x00000038},
	{0x0000f180, 0x00000004},
	{0x000d0034, 0x00000038},
	{0x0000f393, 0x00000004},
	{0x000d0036, 0x00000038},
	{0x0000f38a, 0x00000004},
	{0x000d0038, 0x00000038},
	{0x0000f38e, 0x00000004},
	{0x0000e821, 0x00000004},
	{0x0140a000, 0x00000004},
	{0x00000043, 0x00000018},
	{0x00cce800, 0x00000004},
	{0x001b0001, 0x00000004},
	{0x08004800, 0x00000004},
	{0x001b0001, 0x00000004},
	{0x08004800, 0x00000004},
	{0x001b0001, 0x00000004},
	{0x08004800, 0x00000004},
	{0x0000003a, 0x00000008},
	{0x0000a000, 0000000000},
	{0x02c0a000, 0x00000004},
	{0x000ca000, 0x00000004},
	{0x00130000, 0x00000004},
	{0x000c2000, 0x00000004},
	{0xc980c045, 0x00000008},
	{0x2000451d, 0x00000004},
	{0x0000e580, 0x00000004},
	{0x000ce581, 0x00000004},
	{0x08004580, 0x00000004},
	{0x000ce581, 0x00000004},
	{0x0000004c, 0x00000008},
	{0x0000a000, 0000000000},
	{0x000c2000, 0x00000004},
	{0x0000e50e, 0x00000004},
	{0x00032000, 0x00000004},
	{0x00022056, 0x00000028},
	{0x00000056, 0x00000024},
	{0x0800450f, 0x00000004},
	{0x0000a050, 0x00000008},
	{0x0000e565, 0x00000004},
	{0x0000e566, 0x00000004},
	{0x00000057, 0x00000008},
	{0x03cca5b4, 0x00000004},
	{0x05432000, 0x00000004},
	{0x00022000, 0x00000004},
	{0x4ccce063, 0x00000030},
	{0x08274565, 0x00000004},
	{0x00000063, 0x00000030},
	{0x08004564, 0x00000004},
	{0x0000e566, 0x00000004},
	{0x0000005a, 0x00000008},
	{0x00802066, 0x00000010},
	{0x00202000, 0x00000004},
	{0x001b00ff, 0x00000004},
	{0x01000069, 0x00000010},
	{0x001f2000, 0x00000004},
	{0x001c00ff, 0x00000004},
	{0000000000, 0x0000000c},
	{0x00000085, 0x00000030},
	{0x0000005a, 0x00000008},
	{0x0000e576, 0x00000004},
	{0x000ca000, 0x00000004},
	{0x00012000, 0x00000004},
	{0x00082000, 0x00000004},
	{0x1800650e, 0x00000004},
	{0x00092000, 0x00000004},
	{0x000a2000, 0x00000004},
	{0x000f0000, 0x00000004},
	{0x00400000, 0x00000004},
	{0x00000079, 0x00000018},
	{0x0000e563, 0x00000004},
	{0x00c0e5f9, 0x000000c2},
	{0x0000006e, 0x00000008},
	{0x0000a06e, 0x00000008},
	{0x0000e576, 0x00000004},
	{0x0000e577, 0x00000004},
	{0x0000e50e, 0x00000004},
	{0x0000e50f, 0x00000004},
	{0x0140a000, 0x00000004},
	{0x0000007c, 0x00000018},
	{0x00c0e5f9, 0x000000c2},
	{0x0000007c, 0x00000008},
	{0x0014e50e, 0x00000004},
	{0x0040e50f, 0x00000004},
	{0x00c0007f, 0x00000008},
	{0x0000e570, 0x00000004},
	{0x0000e571, 0x00000004},
	{0x0000e572, 0x0000000c},
	{0x0000a000, 0x00000004},
	{0x0140a000, 0x00000004},
	{0x0000e568, 0x00000004},
	{0x000c2000, 0x00000004},
	{0x00000089, 0x00000018},
	{0x000b0000, 0x00000004},
	{0x18c0e562, 0x00000004},
	{0x0000008b, 0x00000008},
	{0x00c0008a, 0x00000008},
	{0x000700e4, 0x00000004},
	{0x00000097, 0x00000038},
	{0x000ca099, 0x00000030},
	{0x080045bb, 0x00000004},
	{0x000c209a, 0x00000030},
	{0x0800e5bc, 0000000000},
	{0x0000e5bb, 0x00000004},
	{0x0000e5bc, 0000000000},
	{0x00120000, 0x0000000c},
	{0x00120000, 0x00000004},
	{0x001b0002, 0x0000000c},
	{0x0000a000, 0x00000004},
	{0x0000e821, 0x00000004},
	{0x0000e800, 0000000000},
	{0x0000e821, 0x00000004},
	{0x0000e82e, 0000000000},
	{0x02cca000, 0x00000004},
	{0x00140000, 0x00000004},
	{0x000ce1cc, 0x00000004},
	{0x050de1cd, 0x00000004},
	{0x000000a7, 0x00000020},
	{0x4200e000, 0000000000},
	{0x000000ae, 0x00000038},
	{0x000ca000, 0x00000004},
	{0x00140000, 0x00000004},
	{0x000c2000, 0x00000004},
	{0x00160000, 0x00000004},
	{0x700ce000, 0x00000004},
	{0x001400aa, 0x00000008},
	{0x4000e000, 0000000000},
	{0x02400000, 0x00000004},
	{0x400ee000, 0x00000004},
	{0x02400000, 0x00000004},
	{0x4000e000, 0000000000},
	{0x000c2000, 0x00000004},
	{0x0240e51b, 0x00000004},
	{0x0080e50a, 0x00000005},
	{0x0080e50b, 0x00000005},
	{0x00220000, 0x00000004},
	{0x000700e4, 0x00000004},
	{0x000000c1, 0x00000038},
	{0x000c209a, 0x00000030},
	{0x0880e5bd, 0x00000005},
	{0x000c2099, 0x00000030},
	{0x0800e5bb, 0x00000005},
	{0x000c209a, 0x00000030},
	{0x0880e5bc, 0x00000005},
	{0x000000c4, 0x00000008},
	{0x0080e5bd, 0x00000005},
	{0x0000e5bb, 0x00000005},
	{0x0080e5bc, 0x00000005},
	{0x00210000, 0x00000004},
	{0x02800000, 0x00000004},
	{0x00c000c8, 0x00000018},
	{0x4180e000, 0x00000040},
	{0x000000ca, 0x00000024},
	{0x01000000, 0x0000000c},
	{0x0100e51d, 0x0000000c},
	{0x000045bb, 0x00000004},
	{0x000080c4, 0x00000008},
	{0x0000f3ce, 0x00000004},
	{0x0140a000, 0x00000004},
	{0x00cc2000, 0x00000004},
	{0x08c053cf, 0x00000040},
	{0x00008000, 0000000000},
	{0x0000f3d2, 0x00000004},
	{0x0140a000, 0x00000004},
	{0x00cc2000, 0x00000004},
	{0x08c053d3, 0x00000040},
	{0x00008000, 0000000000},
	{0x0000f39d, 0x00000004},
	{0x0140a000, 0x00000004},
	{0x00cc2000, 0x00000004},
	{0x08c0539e, 0x00000040},
	{0x00008000, 0000000000},
	{0x03c00830, 0x00000004},
	{0x4200e000, 0000000000},
	{0x0000a000, 0x00000004},
	{0x200045e0, 0x00000004},
	{0x0000e5e1, 0000000000},
	{0x00000001, 0000000000},
	{0x000700e1, 0x00000004},
	{0x0800e394, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
	{0000000000, 0000000000},
};

static int RADEON_READ_PLL(struct drm_device * dev, int addr)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	RADEON_WRITE8(RADEON_CLOCK_CNTL_INDEX, addr & 0x1f);
	return RADEON_READ(RADEON_CLOCK_CNTL_DATA);
}

static int RADEON_READ_PCIE(drm_radeon_private_t *dev_priv, int addr)
{
	RADEON_WRITE8(RADEON_PCIE_INDEX, addr & 0xff);
	return RADEON_READ(RADEON_PCIE_DATA);
}

static u32 RADEON_READ_IGPGART(drm_radeon_private_t *dev_priv, int addr)
{
	u32 ret;
	RADEON_WRITE(RADEON_IGPGART_INDEX, addr & 0x7f);
	ret = RADEON_READ(RADEON_IGPGART_DATA);
	RADEON_WRITE(RADEON_IGPGART_INDEX, 0x7f);
	return ret;
}

#if RADEON_FIFO_DEBUG
static void radeon_status(drm_radeon_private_t * dev_priv)
{
	printk("%s:\n", __FUNCTION__);
	printk("RBBM_STATUS = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_RBBM_STATUS));
	printk("CP_RB_RTPR = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_CP_RB_RPTR));
	printk("CP_RB_WTPR = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_CP_RB_WPTR));
	printk("AIC_CNTL = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_AIC_CNTL));
	printk("AIC_STAT = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_AIC_STAT));
	printk("AIC_PT_BASE = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_AIC_PT_BASE));
	printk("TLB_ADDR = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_AIC_TLB_ADDR));
	printk("TLB_DATA = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_AIC_TLB_DATA));
}
#endif

/* ================================================================
 * Engine, FIFO control
 */

static int radeon_do_pixcache_flush(drm_radeon_private_t * dev_priv)
{
	u32 tmp;
	int i;

	dev_priv->stats.boxes |= RADEON_BOX_WAIT_IDLE;

	tmp = RADEON_READ(RADEON_RB3D_DSTCACHE_CTLSTAT);
	tmp |= RADEON_RB3D_DC_FLUSH_ALL;
	RADEON_WRITE(RADEON_RB3D_DSTCACHE_CTLSTAT, tmp);

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		if (!(RADEON_READ(RADEON_RB3D_DSTCACHE_CTLSTAT)
		      & RADEON_RB3D_DC_BUSY)) {
			return 0;
		}
		DRM_UDELAY(1);
	}

#if RADEON_FIFO_DEBUG
	DRM_ERROR("failed!\n");
	radeon_status(dev_priv);
#endif
	return -EBUSY;
}

static int radeon_do_wait_for_fifo(drm_radeon_private_t * dev_priv, int entries)
{
	int i;

	dev_priv->stats.boxes |= RADEON_BOX_WAIT_IDLE;

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		int slots = (RADEON_READ(RADEON_RBBM_STATUS)
			     & RADEON_RBBM_FIFOCNT_MASK);
		if (slots >= entries)
			return 0;
		DRM_UDELAY(1);
	}

#if RADEON_FIFO_DEBUG
	DRM_ERROR("failed!\n");
	radeon_status(dev_priv);
#endif
	return -EBUSY;
}

static int radeon_do_wait_for_idle(drm_radeon_private_t * dev_priv)
{
	int i, ret;

	dev_priv->stats.boxes |= RADEON_BOX_WAIT_IDLE;

	ret = radeon_do_wait_for_fifo(dev_priv, 64);
	if (ret)
		return ret;

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		if (!(RADEON_READ(RADEON_RBBM_STATUS)
		      & RADEON_RBBM_ACTIVE)) {
			radeon_do_pixcache_flush(dev_priv);
			return 0;
		}
		DRM_UDELAY(1);
	}

#if RADEON_FIFO_DEBUG
	DRM_ERROR("failed!\n");
	radeon_status(dev_priv);
#endif
	return -EBUSY;
}

/* ================================================================
 * CP control, initialization
 */

/* Load the microcode for the CP */
static void radeon_cp_load_microcode(drm_radeon_private_t * dev_priv)
{
	int i;
	DRM_DEBUG("\n");

	radeon_do_wait_for_idle(dev_priv);

	RADEON_WRITE(RADEON_CP_ME_RAM_ADDR, 0);

	if (dev_priv->microcode_version == UCODE_R200) {
		DRM_INFO("Loading R200 Microcode\n");
		for (i = 0; i < 256; i++) {
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAH,
				     R200_cp_microcode[i][1]);
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAL,
				     R200_cp_microcode[i][0]);
		}
	} else if (dev_priv->microcode_version == UCODE_R300) {
		DRM_INFO("Loading R300 Microcode\n");
		for (i = 0; i < 256; i++) {
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAH,
				     R300_cp_microcode[i][1]);
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAL,
				     R300_cp_microcode[i][0]);
		}
	} else {
		for (i = 0; i < 256; i++) {
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAH,
				     radeon_cp_microcode[i][1]);
			RADEON_WRITE(RADEON_CP_ME_RAM_DATAL,
				     radeon_cp_microcode[i][0]);
		}
	}
}

/* Flush any pending commands to the CP.  This should only be used just
 * prior to a wait for idle, as it informs the engine that the command
 * stream is ending.
 */
static void radeon_do_cp_flush(drm_radeon_private_t * dev_priv)
{
	DRM_DEBUG("\n");
#if 0
	u32 tmp;

	tmp = RADEON_READ(RADEON_CP_RB_WPTR) | (1 << 31);
	RADEON_WRITE(RADEON_CP_RB_WPTR, tmp);
#endif
}

/* Wait for the CP to go idle.
 */
int radeon_do_cp_idle(drm_radeon_private_t * dev_priv)
{
	RING_LOCALS;
	DRM_DEBUG("\n");

	BEGIN_RING(6);

	RADEON_PURGE_CACHE();
	RADEON_PURGE_ZCACHE();
	RADEON_WAIT_UNTIL_IDLE();

	ADVANCE_RING();
	COMMIT_RING();

	return radeon_do_wait_for_idle(dev_priv);
}

/* Start the Command Processor.
 */
static void radeon_do_cp_start(drm_radeon_private_t * dev_priv)
{
	RING_LOCALS;
	DRM_DEBUG("\n");

	radeon_do_wait_for_idle(dev_priv);

	RADEON_WRITE(RADEON_CP_CSQ_CNTL, dev_priv->cp_mode);

	dev_priv->cp_running = 1;

	BEGIN_RING(6);

	RADEON_PURGE_CACHE();
	RADEON_PURGE_ZCACHE();
	RADEON_WAIT_UNTIL_IDLE();

	ADVANCE_RING();
	COMMIT_RING();
}

/* Reset the Command Processor.  This will not flush any pending
 * commands, so you must wait for the CP command stream to complete
 * before calling this routine.
 */
static void radeon_do_cp_reset(drm_radeon_private_t * dev_priv)
{
	u32 cur_read_ptr;
	DRM_DEBUG("\n");

	cur_read_ptr = RADEON_READ(RADEON_CP_RB_RPTR);
	RADEON_WRITE(RADEON_CP_RB_WPTR, cur_read_ptr);
	SET_RING_HEAD(dev_priv, cur_read_ptr);
	dev_priv->ring.tail = cur_read_ptr;
}

/* Stop the Command Processor.  This will not flush any pending
 * commands, so you must flush the command stream and wait for the CP
 * to go idle before calling this routine.
 */
static void radeon_do_cp_stop(drm_radeon_private_t * dev_priv)
{
	DRM_DEBUG("\n");

	RADEON_WRITE(RADEON_CP_CSQ_CNTL, RADEON_CSQ_PRIDIS_INDDIS);

	dev_priv->cp_running = 0;
}

/* Reset the engine.  This will stop the CP if it is running.
 */
static int radeon_do_engine_reset(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	u32 clock_cntl_index, mclk_cntl, rbbm_soft_reset;
	DRM_DEBUG("\n");

	radeon_do_pixcache_flush(dev_priv);

	clock_cntl_index = RADEON_READ(RADEON_CLOCK_CNTL_INDEX);
	mclk_cntl = RADEON_READ_PLL(dev, RADEON_MCLK_CNTL);

	RADEON_WRITE_PLL(RADEON_MCLK_CNTL, (mclk_cntl |
					    RADEON_FORCEON_MCLKA |
					    RADEON_FORCEON_MCLKB |
					    RADEON_FORCEON_YCLKA |
					    RADEON_FORCEON_YCLKB |
					    RADEON_FORCEON_MC |
					    RADEON_FORCEON_AIC));

	rbbm_soft_reset = RADEON_READ(RADEON_RBBM_SOFT_RESET);

	RADEON_WRITE(RADEON_RBBM_SOFT_RESET, (rbbm_soft_reset |
					      RADEON_SOFT_RESET_CP |
					      RADEON_SOFT_RESET_HI |
					      RADEON_SOFT_RESET_SE |
					      RADEON_SOFT_RESET_RE |
					      RADEON_SOFT_RESET_PP |
					      RADEON_SOFT_RESET_E2 |
					      RADEON_SOFT_RESET_RB));
	RADEON_READ(RADEON_RBBM_SOFT_RESET);
	RADEON_WRITE(RADEON_RBBM_SOFT_RESET, (rbbm_soft_reset &
					      ~(RADEON_SOFT_RESET_CP |
						RADEON_SOFT_RESET_HI |
						RADEON_SOFT_RESET_SE |
						RADEON_SOFT_RESET_RE |
						RADEON_SOFT_RESET_PP |
						RADEON_SOFT_RESET_E2 |
						RADEON_SOFT_RESET_RB)));
	RADEON_READ(RADEON_RBBM_SOFT_RESET);

	RADEON_WRITE_PLL(RADEON_MCLK_CNTL, mclk_cntl);
	RADEON_WRITE(RADEON_CLOCK_CNTL_INDEX, clock_cntl_index);
	RADEON_WRITE(RADEON_RBBM_SOFT_RESET, rbbm_soft_reset);

	/* Reset the CP ring */
	radeon_do_cp_reset(dev_priv);

	/* The CP is no longer running after an engine reset */
	dev_priv->cp_running = 0;

	/* Reset any pending vertex, indirect buffers */
	radeon_freelist_reset(dev);

	return 0;
}

static void radeon_cp_init_ring_buffer(struct drm_device * dev,
				       drm_radeon_private_t * dev_priv)
{
	u32 ring_start, cur_read_ptr;
	u32 tmp;
	
	/* Initialize the memory controller. With new memory map, the fb location
	 * is not changed, it should have been properly initialized already. Part
	 * of the problem is that the code below is bogus, assuming the GART is
	 * always appended to the fb which is not necessarily the case
	 */
	if (!dev_priv->new_memmap)
		RADEON_WRITE(RADEON_MC_FB_LOCATION,
			     ((dev_priv->gart_vm_start - 1) & 0xffff0000)
			     | (dev_priv->fb_location >> 16));

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		RADEON_WRITE(RADEON_AGP_BASE, (unsigned int)dev->agp->base);
		RADEON_WRITE(RADEON_MC_AGP_LOCATION,
			     (((dev_priv->gart_vm_start - 1 +
				dev_priv->gart_size) & 0xffff0000) |
			      (dev_priv->gart_vm_start >> 16)));

		ring_start = (dev_priv->cp_ring->offset
			      - dev->agp->base
			      + dev_priv->gart_vm_start);
	} else
#endif
		ring_start = (dev_priv->cp_ring->offset
			      - (unsigned long)dev->sg->virtual
			      + dev_priv->gart_vm_start);

	RADEON_WRITE(RADEON_CP_RB_BASE, ring_start);

	/* Set the write pointer delay */
	RADEON_WRITE(RADEON_CP_RB_WPTR_DELAY, 0);

	/* Initialize the ring buffer's read and write pointers */
	cur_read_ptr = RADEON_READ(RADEON_CP_RB_RPTR);
	RADEON_WRITE(RADEON_CP_RB_WPTR, cur_read_ptr);
	SET_RING_HEAD(dev_priv, cur_read_ptr);
	dev_priv->ring.tail = cur_read_ptr;

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		RADEON_WRITE(RADEON_CP_RB_RPTR_ADDR,
			     dev_priv->ring_rptr->offset
			     - dev->agp->base + dev_priv->gart_vm_start);
	} else
#endif
	{
		struct drm_sg_mem *entry = dev->sg;
		unsigned long tmp_ofs, page_ofs;

		tmp_ofs = dev_priv->ring_rptr->offset -
				(unsigned long)dev->sg->virtual;
		page_ofs = tmp_ofs >> PAGE_SHIFT;

		RADEON_WRITE(RADEON_CP_RB_RPTR_ADDR, entry->busaddr[page_ofs]);
		DRM_DEBUG("ring rptr: offset=0x%08lx handle=0x%08lx\n",
			  (unsigned long)entry->busaddr[page_ofs],
			  entry->handle + tmp_ofs);
	}

	/* Set ring buffer size */
#ifdef __BIG_ENDIAN
	RADEON_WRITE(RADEON_CP_RB_CNTL,
		     dev_priv->ring.size_l2qw | RADEON_BUF_SWAP_32BIT);
#else
	RADEON_WRITE(RADEON_CP_RB_CNTL, dev_priv->ring.size_l2qw);
#endif

	/* Start with assuming that writeback doesn't work */
	dev_priv->writeback_works = 0;

	/* Initialize the scratch register pointer.  This will cause
	 * the scratch register values to be written out to memory
	 * whenever they are updated.
	 *
	 * We simply put this behind the ring read pointer, this works
	 * with PCI GART as well as (whatever kind of) AGP GART
	 */
	RADEON_WRITE(RADEON_SCRATCH_ADDR, RADEON_READ(RADEON_CP_RB_RPTR_ADDR)
		     + RADEON_SCRATCH_REG_OFFSET);

	dev_priv->scratch = ((__volatile__ u32 *)
			     dev_priv->ring_rptr->handle +
			     (RADEON_SCRATCH_REG_OFFSET / sizeof(u32)));

	RADEON_WRITE(RADEON_SCRATCH_UMSK, 0x7);

	/* Turn on bus mastering */
	tmp = RADEON_READ(RADEON_BUS_CNTL) & ~RADEON_BUS_MASTER_DIS;
	RADEON_WRITE(RADEON_BUS_CNTL, tmp);

	dev_priv->sarea_priv->last_frame = dev_priv->scratch[0] = 0;
	RADEON_WRITE(RADEON_LAST_FRAME_REG, dev_priv->sarea_priv->last_frame);

	dev_priv->sarea_priv->last_dispatch = dev_priv->scratch[1] = 0;
	RADEON_WRITE(RADEON_LAST_DISPATCH_REG,
		     dev_priv->sarea_priv->last_dispatch);

	dev_priv->sarea_priv->last_clear = dev_priv->scratch[2] = 0;
	RADEON_WRITE(RADEON_LAST_CLEAR_REG, dev_priv->sarea_priv->last_clear);

	radeon_do_wait_for_idle(dev_priv);

	/* Sync everything up */
	RADEON_WRITE(RADEON_ISYNC_CNTL,
		     (RADEON_ISYNC_ANY2D_IDLE3D |
		      RADEON_ISYNC_ANY3D_IDLE2D |
		      RADEON_ISYNC_WAIT_IDLEGUI |
		      RADEON_ISYNC_CPSCRATCH_IDLEGUI));

}

static void radeon_test_writeback(drm_radeon_private_t * dev_priv)
{
	u32 tmp;

	/* Writeback doesn't seem to work everywhere, test it here and possibly
	 * enable it if it appears to work
	 */
	DRM_WRITE32(dev_priv->ring_rptr, RADEON_SCRATCHOFF(1), 0);
	RADEON_WRITE(RADEON_SCRATCH_REG1, 0xdeadbeef);

	for (tmp = 0; tmp < dev_priv->usec_timeout; tmp++) {
		if (DRM_READ32(dev_priv->ring_rptr, RADEON_SCRATCHOFF(1)) ==
		    0xdeadbeef)
			break;
		DRM_UDELAY(1);
	}

	if (tmp < dev_priv->usec_timeout) {
		dev_priv->writeback_works = 1;
		DRM_INFO("writeback test succeeded in %d usecs\n", tmp);
	} else {
		dev_priv->writeback_works = 0;
		DRM_INFO("writeback test failed\n");
	}
	if (radeon_no_wb == 1) {
		dev_priv->writeback_works = 0;
		DRM_INFO("writeback forced off\n");
	}

	if (!dev_priv->writeback_works) {
		/* Disable writeback to avoid unnecessary bus master transfer */
		RADEON_WRITE(RADEON_CP_RB_CNTL, RADEON_READ(RADEON_CP_RB_CNTL) |
			     RADEON_RB_NO_UPDATE);
		RADEON_WRITE(RADEON_SCRATCH_UMSK, 0);
	}
}

/* Enable or disable IGP GART on the chip */
static void radeon_set_igpgart(drm_radeon_private_t * dev_priv, int on)
{
	u32 temp, tmp;

	tmp = RADEON_READ(RADEON_AIC_CNTL);
	if (on) {
		DRM_DEBUG("programming igpgart %08X %08lX %08X\n",
			 dev_priv->gart_vm_start,
			 (long)dev_priv->gart_info.bus_addr,
			 dev_priv->gart_size);

		RADEON_WRITE_IGPGART(RADEON_IGPGART_UNK_18, 0x1000);
		RADEON_WRITE_IGPGART(RADEON_IGPGART_ENABLE, 0x1);
		RADEON_WRITE_IGPGART(RADEON_IGPGART_CTRL, 0x42040800);
		RADEON_WRITE_IGPGART(RADEON_IGPGART_BASE_ADDR,
				     dev_priv->gart_info.bus_addr);

		temp = RADEON_READ_IGPGART(dev_priv, RADEON_IGPGART_UNK_39);
		RADEON_WRITE_IGPGART(RADEON_IGPGART_UNK_39, temp);

		RADEON_WRITE(RADEON_AGP_BASE, (unsigned int)dev_priv->gart_vm_start);
		dev_priv->gart_size = 32*1024*1024;
		RADEON_WRITE(RADEON_MC_AGP_LOCATION,
			     (((dev_priv->gart_vm_start - 1 +
			       dev_priv->gart_size) & 0xffff0000) |
			     (dev_priv->gart_vm_start >> 16)));

		temp = RADEON_READ_IGPGART(dev_priv, RADEON_IGPGART_ENABLE);
		RADEON_WRITE_IGPGART(RADEON_IGPGART_ENABLE, temp);

		RADEON_READ_IGPGART(dev_priv, RADEON_IGPGART_FLUSH);
		RADEON_WRITE_IGPGART(RADEON_IGPGART_FLUSH, 0x1);
		RADEON_READ_IGPGART(dev_priv, RADEON_IGPGART_FLUSH);
		RADEON_WRITE_IGPGART(RADEON_IGPGART_FLUSH, 0x0);
       }
}

static void radeon_set_pciegart(drm_radeon_private_t * dev_priv, int on)
{
	u32 tmp = RADEON_READ_PCIE(dev_priv, RADEON_PCIE_TX_GART_CNTL);
	if (on) {

		DRM_DEBUG("programming pcie %08X %08lX %08X\n",
			  dev_priv->gart_vm_start,
			  (long)dev_priv->gart_info.bus_addr,
			  dev_priv->gart_size);
		RADEON_WRITE_PCIE(RADEON_PCIE_TX_DISCARD_RD_ADDR_LO,
				  dev_priv->gart_vm_start);
		RADEON_WRITE_PCIE(RADEON_PCIE_TX_GART_BASE,
				  dev_priv->gart_info.bus_addr);
		RADEON_WRITE_PCIE(RADEON_PCIE_TX_GART_START_LO,
				  dev_priv->gart_vm_start);
		RADEON_WRITE_PCIE(RADEON_PCIE_TX_GART_END_LO,
				  dev_priv->gart_vm_start +
				  dev_priv->gart_size - 1);

		RADEON_WRITE(RADEON_MC_AGP_LOCATION, 0xffffffc0);	/* ?? */

		RADEON_WRITE_PCIE(RADEON_PCIE_TX_GART_CNTL,
				  RADEON_PCIE_TX_GART_EN);
	} else {
		RADEON_WRITE_PCIE(RADEON_PCIE_TX_GART_CNTL,
				  tmp & ~RADEON_PCIE_TX_GART_EN);
	}
}

/* Enable or disable PCI GART on the chip */
static void radeon_set_pcigart(drm_radeon_private_t * dev_priv, int on)
{
	u32 tmp;

	if (dev_priv->flags & RADEON_IS_IGPGART) {
		radeon_set_igpgart(dev_priv, on);
		return;
	}

	if (dev_priv->flags & RADEON_IS_PCIE) {
		radeon_set_pciegart(dev_priv, on);
		return;
	}

 	tmp = RADEON_READ(RADEON_AIC_CNTL);

	if (on) {
		RADEON_WRITE(RADEON_AIC_CNTL,
			     tmp | RADEON_PCIGART_TRANSLATE_EN);

		/* set PCI GART page-table base address
		 */
		RADEON_WRITE(RADEON_AIC_PT_BASE, dev_priv->gart_info.bus_addr);

		/* set address range for PCI address translate
		 */
		RADEON_WRITE(RADEON_AIC_LO_ADDR, dev_priv->gart_vm_start);
		RADEON_WRITE(RADEON_AIC_HI_ADDR, dev_priv->gart_vm_start
			     + dev_priv->gart_size - 1);

		/* Turn off AGP aperture -- is this required for PCI GART?
		 */
		RADEON_WRITE(RADEON_MC_AGP_LOCATION, 0xffffffc0);	/* ?? */
		RADEON_WRITE(RADEON_AGP_COMMAND, 0);	/* clear AGP_COMMAND */
	} else {
		RADEON_WRITE(RADEON_AIC_CNTL,
			     tmp & ~RADEON_PCIGART_TRANSLATE_EN);
	}
}

static int radeon_do_init_cp(struct drm_device * dev, drm_radeon_init_t * init)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG("\n");

	/* if we require new memory map but we don't have it fail */
	if ((dev_priv->flags & RADEON_NEW_MEMMAP) && !dev_priv->new_memmap) {
		DRM_ERROR("Cannot initialise DRM on this card\nThis card requires a new X.org DDX for 3D\n");
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}

	if (init->is_pci && (dev_priv->flags & RADEON_IS_AGP)) {
		DRM_DEBUG("Forcing AGP card to PCI mode\n");
		dev_priv->flags &= ~RADEON_IS_AGP;
	} else if (!(dev_priv->flags & (RADEON_IS_AGP | RADEON_IS_PCI | RADEON_IS_PCIE))
		   && !init->is_pci) {
		DRM_DEBUG("Restoring AGP flag\n");
		dev_priv->flags |= RADEON_IS_AGP;
	}

	if ((!(dev_priv->flags & RADEON_IS_AGP)) && !dev->sg) {
		DRM_ERROR("PCI GART memory not allocated!\n");
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}

	dev_priv->usec_timeout = init->usec_timeout;
	if (dev_priv->usec_timeout < 1 ||
	    dev_priv->usec_timeout > RADEON_MAX_USEC_TIMEOUT) {
		DRM_DEBUG("TIMEOUT problem!\n");
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}

	/* Enable vblank on CRTC1 for older X servers
	 */
	dev_priv->vblank_crtc = DRM_RADEON_VBLANK_CRTC1;

	switch(init->func) {
	case RADEON_INIT_R200_CP:
		dev_priv->microcode_version = UCODE_R200;
		break;
	case RADEON_INIT_R300_CP:
		dev_priv->microcode_version = UCODE_R300;
		break;
	default:
		dev_priv->microcode_version = UCODE_R100;
	}

	dev_priv->do_boxes = 0;
	dev_priv->cp_mode = init->cp_mode;

	/* We don't support anything other than bus-mastering ring mode,
	 * but the ring can be in either AGP or PCI space for the ring
	 * read pointer.
	 */
	if ((init->cp_mode != RADEON_CSQ_PRIBM_INDDIS) &&
	    (init->cp_mode != RADEON_CSQ_PRIBM_INDBM)) {
		DRM_DEBUG("BAD cp_mode (%x)!\n", init->cp_mode);
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}

	switch (init->fb_bpp) {
	case 16:
		dev_priv->color_fmt = RADEON_COLOR_FORMAT_RGB565;
		break;
	case 32:
	default:
		dev_priv->color_fmt = RADEON_COLOR_FORMAT_ARGB8888;
		break;
	}
	dev_priv->front_offset = init->front_offset;
	dev_priv->front_pitch = init->front_pitch;
	dev_priv->back_offset = init->back_offset;
	dev_priv->back_pitch = init->back_pitch;

	switch (init->depth_bpp) {
	case 16:
		dev_priv->depth_fmt = RADEON_DEPTH_FORMAT_16BIT_INT_Z;
		break;
	case 32:
	default:
		dev_priv->depth_fmt = RADEON_DEPTH_FORMAT_24BIT_INT_Z;
		break;
	}
	dev_priv->depth_offset = init->depth_offset;
	dev_priv->depth_pitch = init->depth_pitch;

	/* Hardware state for depth clears.  Remove this if/when we no
	 * longer clear the depth buffer with a 3D rectangle.  Hard-code
	 * all values to prevent unwanted 3D state from slipping through
	 * and screwing with the clear operation.
	 */
	dev_priv->depth_clear.rb3d_cntl = (RADEON_PLANE_MASK_ENABLE |
					   (dev_priv->color_fmt << 10) |
					   (dev_priv->microcode_version ==
					    UCODE_R100 ? RADEON_ZBLOCK16 : 0));

	dev_priv->depth_clear.rb3d_zstencilcntl =
	    (dev_priv->depth_fmt |
	     RADEON_Z_TEST_ALWAYS |
	     RADEON_STENCIL_TEST_ALWAYS |
	     RADEON_STENCIL_S_FAIL_REPLACE |
	     RADEON_STENCIL_ZPASS_REPLACE |
	     RADEON_STENCIL_ZFAIL_REPLACE | RADEON_Z_WRITE_ENABLE);

	dev_priv->depth_clear.se_cntl = (RADEON_FFACE_CULL_CW |
					 RADEON_BFACE_SOLID |
					 RADEON_FFACE_SOLID |
					 RADEON_FLAT_SHADE_VTX_LAST |
					 RADEON_DIFFUSE_SHADE_FLAT |
					 RADEON_ALPHA_SHADE_FLAT |
					 RADEON_SPECULAR_SHADE_FLAT |
					 RADEON_FOG_SHADE_FLAT |
					 RADEON_VTX_PIX_CENTER_OGL |
					 RADEON_ROUND_MODE_TRUNC |
					 RADEON_ROUND_PREC_8TH_PIX);


	dev_priv->ring_offset = init->ring_offset;
	dev_priv->ring_rptr_offset = init->ring_rptr_offset;
	dev_priv->buffers_offset = init->buffers_offset;
	dev_priv->gart_textures_offset = init->gart_textures_offset;

	dev_priv->sarea = drm_getsarea(dev);
	if (!dev_priv->sarea) {
		DRM_ERROR("could not find sarea!\n");
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}

	dev_priv->cp_ring = drm_core_findmap(dev, init->ring_offset);
	if (!dev_priv->cp_ring) {
		DRM_ERROR("could not find cp ring region!\n");
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}
	dev_priv->ring_rptr = drm_core_findmap(dev, init->ring_rptr_offset);
	if (!dev_priv->ring_rptr) {
		DRM_ERROR("could not find ring read pointer!\n");
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}
	dev->agp_buffer_token = init->buffers_offset;
	dev->agp_buffer_map = drm_core_findmap(dev, init->buffers_offset);
	if (!dev->agp_buffer_map) {
		DRM_ERROR("could not find dma buffer region!\n");
		radeon_do_cleanup_cp(dev);
		return -EINVAL;
	}

	if (init->gart_textures_offset) {
		dev_priv->gart_textures =
		    drm_core_findmap(dev, init->gart_textures_offset);
		if (!dev_priv->gart_textures) {
			DRM_ERROR("could not find GART texture region!\n");
			radeon_do_cleanup_cp(dev);
			return -EINVAL;
		}
	}

	dev_priv->sarea_priv =
	    (drm_radeon_sarea_t *) ((u8 *) dev_priv->sarea->handle +
				    init->sarea_priv_offset);

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		drm_core_ioremap(dev_priv->cp_ring, dev);
		drm_core_ioremap(dev_priv->ring_rptr, dev);
		drm_core_ioremap(dev->agp_buffer_map, dev);
		if (!dev_priv->cp_ring->handle ||
		    !dev_priv->ring_rptr->handle ||
		    !dev->agp_buffer_map->handle) {
			DRM_ERROR("could not find ioremap agp regions!\n");
			radeon_do_cleanup_cp(dev);
			return -EINVAL;
		}
	} else
#endif
	{
		dev_priv->cp_ring->handle = (void *)dev_priv->cp_ring->offset;
		dev_priv->ring_rptr->handle =
		    (void *)dev_priv->ring_rptr->offset;
		dev->agp_buffer_map->handle =
		    (void *)dev->agp_buffer_map->offset;

		DRM_DEBUG("dev_priv->cp_ring->handle %p\n",
			  dev_priv->cp_ring->handle);
		DRM_DEBUG("dev_priv->ring_rptr->handle %p\n",
			  dev_priv->ring_rptr->handle);
		DRM_DEBUG("dev->agp_buffer_map->handle %p\n",
			  dev->agp_buffer_map->handle);
	}

	dev_priv->fb_location = (RADEON_READ(RADEON_MC_FB_LOCATION)
				 & 0xffff) << 16;
	dev_priv->fb_size = 
		((RADEON_READ(RADEON_MC_FB_LOCATION) & 0xffff0000u) + 0x10000)
		- dev_priv->fb_location;

	dev_priv->front_pitch_offset = (((dev_priv->front_pitch / 64) << 22) |
					((dev_priv->front_offset
					  + dev_priv->fb_location) >> 10));

	dev_priv->back_pitch_offset = (((dev_priv->back_pitch / 64) << 22) |
				       ((dev_priv->back_offset
					 + dev_priv->fb_location) >> 10));

	dev_priv->depth_pitch_offset = (((dev_priv->depth_pitch / 64) << 22) |
					((dev_priv->depth_offset
					  + dev_priv->fb_location) >> 10));

	dev_priv->gart_size = init->gart_size;

	/* New let's set the memory map ... */
	if (dev_priv->new_memmap) {
		u32 base = 0;

		DRM_INFO("Setting GART location based on new memory map\n");

		/* If using AGP, try to locate the AGP aperture at the same
		 * location in the card and on the bus, though we have to
		 * align it down.
		 */
#if __OS_HAS_AGP
		if (dev_priv->flags & RADEON_IS_AGP) {
			base = dev->agp->base;
			/* Check if valid */
			if ((base + dev_priv->gart_size - 1) >= dev_priv->fb_location &&
			    base < (dev_priv->fb_location + dev_priv->fb_size - 1)) {
				DRM_INFO("Can't use AGP base @0x%08lx, won't fit\n",
					 dev->agp->base);
				base = 0;
			}
		}
#endif
		/* If not or if AGP is at 0 (Macs), try to put it elsewhere */
		if (base == 0) {
			base = dev_priv->fb_location + dev_priv->fb_size;
			if (base < dev_priv->fb_location ||
			    ((base + dev_priv->gart_size) & 0xfffffffful) < base)
				base = dev_priv->fb_location
					- dev_priv->gart_size;
		}		
		dev_priv->gart_vm_start = base & 0xffc00000u;
		if (dev_priv->gart_vm_start != base)
			DRM_INFO("GART aligned down from 0x%08x to 0x%08x\n",
				 base, dev_priv->gart_vm_start);
	} else {
		DRM_INFO("Setting GART location based on old memory map\n");
		dev_priv->gart_vm_start = dev_priv->fb_location +
			RADEON_READ(RADEON_CONFIG_APER_SIZE);
	}

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP)
		dev_priv->gart_buffers_offset = (dev->agp_buffer_map->offset
						 - dev->agp->base
						 + dev_priv->gart_vm_start);
	else
#endif
		dev_priv->gart_buffers_offset = (dev->agp_buffer_map->offset
					- (unsigned long)dev->sg->virtual
					+ dev_priv->gart_vm_start);

	DRM_DEBUG("dev_priv->gart_size %d\n", dev_priv->gart_size);
	DRM_DEBUG("dev_priv->gart_vm_start 0x%x\n", dev_priv->gart_vm_start);
	DRM_DEBUG("dev_priv->gart_buffers_offset 0x%lx\n",
		  dev_priv->gart_buffers_offset);

	dev_priv->ring.start = (u32 *) dev_priv->cp_ring->handle;
	dev_priv->ring.end = ((u32 *) dev_priv->cp_ring->handle
			      + init->ring_size / sizeof(u32));
	dev_priv->ring.size = init->ring_size;
	dev_priv->ring.size_l2qw = drm_order(init->ring_size / 8);

	dev_priv->ring.tail_mask = (dev_priv->ring.size / sizeof(u32)) - 1;

	dev_priv->ring.high_mark = RADEON_RING_HIGH_MARK;

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		/* Turn off PCI GART */
		radeon_set_pcigart(dev_priv, 0);
	} else
#endif
	{
		/* if we have an offset set from userspace */
		if (dev_priv->pcigart_offset_set) {
			dev_priv->gart_info.bus_addr =
			    dev_priv->pcigart_offset + dev_priv->fb_location;
			dev_priv->gart_info.mapping.offset =
			    dev_priv->gart_info.bus_addr;
			dev_priv->gart_info.mapping.size =
			    dev_priv->gart_info.table_size;

			drm_core_ioremap(&dev_priv->gart_info.mapping, dev);
			dev_priv->gart_info.addr =
			    dev_priv->gart_info.mapping.handle;

			if (dev_priv->flags & RADEON_IS_PCIE)
				dev_priv->gart_info.gart_reg_if = DRM_ATI_GART_PCIE;
			else
				dev_priv->gart_info.gart_reg_if = DRM_ATI_GART_PCI;
			dev_priv->gart_info.gart_table_location =
			    DRM_ATI_GART_FB;

			DRM_DEBUG("Setting phys_pci_gart to %p %08lX\n",
				  dev_priv->gart_info.addr,
				  dev_priv->pcigart_offset);
		} else {
			if (dev_priv->flags & RADEON_IS_IGPGART)
				dev_priv->gart_info.gart_reg_if = DRM_ATI_GART_IGP;
			else
				dev_priv->gart_info.gart_reg_if = DRM_ATI_GART_PCI;
			dev_priv->gart_info.gart_table_location =
			    DRM_ATI_GART_MAIN;
			dev_priv->gart_info.addr = NULL;
			dev_priv->gart_info.bus_addr = 0;
			if (dev_priv->flags & RADEON_IS_PCIE) {
				DRM_ERROR
				    ("Cannot use PCI Express without GART in FB memory\n");
				radeon_do_cleanup_cp(dev);
				return -EINVAL;
			}
		}

		if (!drm_ati_pcigart_init(dev, &dev_priv->gart_info)) {
			DRM_ERROR("failed to init PCI GART!\n");
			radeon_do_cleanup_cp(dev);
			return -ENOMEM;
		}

		/* Turn on PCI GART */
		radeon_set_pcigart(dev_priv, 1);
	}

	radeon_cp_load_microcode(dev_priv);
	radeon_cp_init_ring_buffer(dev, dev_priv);

	dev_priv->last_buf = 0;

	radeon_do_engine_reset(dev);
	radeon_test_writeback(dev_priv);

	return 0;
}

static int radeon_do_cleanup_cp(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG("\n");

	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if (dev->irq_enabled)
		drm_irq_uninstall(dev);

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		if (dev_priv->cp_ring != NULL) {
			drm_core_ioremapfree(dev_priv->cp_ring, dev);
			dev_priv->cp_ring = NULL;
		}
		if (dev_priv->ring_rptr != NULL) {
			drm_core_ioremapfree(dev_priv->ring_rptr, dev);
			dev_priv->ring_rptr = NULL;
		}
		if (dev->agp_buffer_map != NULL) {
			drm_core_ioremapfree(dev->agp_buffer_map, dev);
			dev->agp_buffer_map = NULL;
		}
	} else
#endif
	{

		if (dev_priv->gart_info.bus_addr) {
			/* Turn off PCI GART */
			radeon_set_pcigart(dev_priv, 0);
			if (!drm_ati_pcigart_cleanup(dev, &dev_priv->gart_info))
				DRM_ERROR("failed to cleanup PCI GART!\n");
		}

		if (dev_priv->gart_info.gart_table_location == DRM_ATI_GART_FB)
		{
			drm_core_ioremapfree(&dev_priv->gart_info.mapping, dev);
			dev_priv->gart_info.addr = 0;
		}
	}
	/* only clear to the start of flags */
	memset(dev_priv, 0, offsetof(drm_radeon_private_t, flags));

	return 0;
}

/* This code will reinit the Radeon CP hardware after a resume from disc.
 * AFAIK, it would be very difficult to pickle the state at suspend time, so
 * here we make sure that all Radeon hardware initialisation is re-done without
 * affecting running applications.
 *
 * Charl P. Botha <http://cpbotha.net>
 */
static int radeon_do_resume_cp(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	if (!dev_priv) {
		DRM_ERROR("Called with no initialization\n");
		return -EINVAL;
	}

	DRM_DEBUG("Starting radeon_do_resume_cp()\n");

#if __OS_HAS_AGP
	if (dev_priv->flags & RADEON_IS_AGP) {
		/* Turn off PCI GART */
		radeon_set_pcigart(dev_priv, 0);
	} else
#endif
	{
		/* Turn on PCI GART */
		radeon_set_pcigart(dev_priv, 1);
	}

	radeon_cp_load_microcode(dev_priv);
	radeon_cp_init_ring_buffer(dev, dev_priv);

	radeon_do_engine_reset(dev);

	DRM_DEBUG("radeon_do_resume_cp() complete\n");

	return 0;
}

int radeon_cp_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_radeon_init_t *init = data;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (init->func == RADEON_INIT_R300_CP)
		r300_init_reg_flags();

	switch (init->func) {
	case RADEON_INIT_CP:
	case RADEON_INIT_R200_CP:
	case RADEON_INIT_R300_CP:
		return radeon_do_init_cp(dev, init);
	case RADEON_CLEANUP_CP:
		return radeon_do_cleanup_cp(dev);
	}

	return -EINVAL;
}

int radeon_cp_start(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (dev_priv->cp_running) {
		DRM_DEBUG("%s while CP running\n", __FUNCTION__);
		return 0;
	}
	if (dev_priv->cp_mode == RADEON_CSQ_PRIDIS_INDDIS) {
		DRM_DEBUG("%s called with bogus CP mode (%d)\n",
			  __FUNCTION__, dev_priv->cp_mode);
		return 0;
	}

	radeon_do_cp_start(dev_priv);

	return 0;
}

/* Stop the CP.  The engine must have been idled before calling this
 * routine.
 */
int radeon_cp_stop(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_cp_stop_t *stop = data;
	int ret;
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (!dev_priv->cp_running)
		return 0;

	/* Flush any pending CP commands.  This ensures any outstanding
	 * commands are exectuted by the engine before we turn it off.
	 */
	if (stop->flush) {
		radeon_do_cp_flush(dev_priv);
	}

	/* If we fail to make the engine go idle, we return an error
	 * code so that the DRM ioctl wrapper can try again.
	 */
	if (stop->idle) {
		ret = radeon_do_cp_idle(dev_priv);
		if (ret)
			return ret;
	}

	/* Finally, we can turn off the CP.  If the engine isn't idle,
	 * we will get some dropped triangles as they won't be fully
	 * rendered before the CP is shut down.
	 */
	radeon_do_cp_stop(dev_priv);

	/* Reset the engine */
	radeon_do_engine_reset(dev);

	return 0;
}

void radeon_do_release(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	int i, ret;

	if (dev_priv) {
		if (dev_priv->cp_running) {
			/* Stop the cp */
			while ((ret = radeon_do_cp_idle(dev_priv)) != 0) {
				DRM_DEBUG("radeon_do_cp_idle %d\n", ret);
#ifdef __linux__
				schedule();
#else
				tsleep(&ret, PZERO, "rdnrel", 1);
#endif
			}
			radeon_do_cp_stop(dev_priv);
			radeon_do_engine_reset(dev);
		}

		/* Disable *all* interrupts */
		if (dev_priv->mmio)	/* remove this after permanent addmaps */
			RADEON_WRITE(RADEON_GEN_INT_CNTL, 0);

		if (dev_priv->mmio) {	/* remove all surfaces */
			for (i = 0; i < RADEON_MAX_SURFACES; i++) {
				RADEON_WRITE(RADEON_SURFACE0_INFO + 16 * i, 0);
				RADEON_WRITE(RADEON_SURFACE0_LOWER_BOUND +
					     16 * i, 0);
				RADEON_WRITE(RADEON_SURFACE0_UPPER_BOUND +
					     16 * i, 0);
			}
		}

		/* Free memory heap structures */
		radeon_mem_takedown(&(dev_priv->gart_heap));
		radeon_mem_takedown(&(dev_priv->fb_heap));

		/* deallocate kernel resources */
		radeon_do_cleanup_cp(dev);
	}
}

/* Just reset the CP ring.  Called as part of an X Server engine reset.
 */
int radeon_cp_reset(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (!dev_priv) {
		DRM_DEBUG("%s called before init done\n", __FUNCTION__);
		return -EINVAL;
	}

	radeon_do_cp_reset(dev_priv);

	/* The CP is no longer running after an engine reset */
	dev_priv->cp_running = 0;

	return 0;
}

int radeon_cp_idle(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	return radeon_do_cp_idle(dev_priv);
}

/* Added by Charl P. Botha to call radeon_do_resume_cp().
 */
int radeon_cp_resume(struct drm_device *dev, void *data, struct drm_file *file_priv)
{

	return radeon_do_resume_cp(dev);
}

int radeon_engine_reset(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	return radeon_do_engine_reset(dev);
}

/* ================================================================
 * Fullscreen mode
 */

/* KW: Deprecated to say the least:
 */
int radeon_fullscreen(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return 0;
}

/* ================================================================
 * Freelist management
 */

/* Original comment: FIXME: ROTATE_BUFS is a hack to cycle through
 *   bufs until freelist code is used.  Note this hides a problem with
 *   the scratch register * (used to keep track of last buffer
 *   completed) being written to before * the last buffer has actually
 *   completed rendering.
 *
 * KW:  It's also a good way to find free buffers quickly.
 *
 * KW: Ideally this loop wouldn't exist, and freelist_get wouldn't
 * sleep.  However, bugs in older versions of radeon_accel.c mean that
 * we essentially have to do this, else old clients will break.
 *
 * However, it does leave open a potential deadlock where all the
 * buffers are held by other clients, which can't release them because
 * they can't get the lock.
 */

struct drm_buf *radeon_freelist_get(struct drm_device * dev)
{
	struct drm_device_dma *dma = dev->dma;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_buf_priv_t *buf_priv;
	struct drm_buf *buf;
	int i, t;
	int start;

	if (++dev_priv->last_buf >= dma->buf_count)
		dev_priv->last_buf = 0;

	start = dev_priv->last_buf;

	for (t = 0; t < dev_priv->usec_timeout; t++) {
		u32 done_age = GET_SCRATCH(1);
		DRM_DEBUG("done_age = %d\n", done_age);
		for (i = start; i < dma->buf_count; i++) {
			buf = dma->buflist[i];
			buf_priv = buf->dev_private;
			if (buf->file_priv == NULL || (buf->pending &&
						       buf_priv->age <=
						       done_age)) {
				dev_priv->stats.requested_bufs++;
				buf->pending = 0;
				return buf;
			}
			start = 0;
		}

		if (t) {
			DRM_UDELAY(1);
			dev_priv->stats.freelist_loops++;
		}
	}

	DRM_DEBUG("returning NULL!\n");
	return NULL;
}

#if 0
struct drm_buf *radeon_freelist_get(struct drm_device * dev)
{
	struct drm_device_dma *dma = dev->dma;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_buf_priv_t *buf_priv;
	struct drm_buf *buf;
	int i, t;
	int start;
	u32 done_age = DRM_READ32(dev_priv->ring_rptr, RADEON_SCRATCHOFF(1));

	if (++dev_priv->last_buf >= dma->buf_count)
		dev_priv->last_buf = 0;

	start = dev_priv->last_buf;
	dev_priv->stats.freelist_loops++;

	for (t = 0; t < 2; t++) {
		for (i = start; i < dma->buf_count; i++) {
			buf = dma->buflist[i];
			buf_priv = buf->dev_private;
			if (buf->file_priv == 0 || (buf->pending &&
						    buf_priv->age <=
						    done_age)) {
				dev_priv->stats.requested_bufs++;
				buf->pending = 0;
				return buf;
			}
		}
		start = 0;
	}

	return NULL;
}
#endif

void radeon_freelist_reset(struct drm_device * dev)
{
	struct drm_device_dma *dma = dev->dma;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	int i;

	dev_priv->last_buf = 0;
	for (i = 0; i < dma->buf_count; i++) {
		struct drm_buf *buf = dma->buflist[i];
		drm_radeon_buf_priv_t *buf_priv = buf->dev_private;
		buf_priv->age = 0;
	}
}

/* ================================================================
 * CP command submission
 */

int radeon_wait_ring(drm_radeon_private_t * dev_priv, int n)
{
	drm_radeon_ring_buffer_t *ring = &dev_priv->ring;
	int i;
	u32 last_head = GET_RING_HEAD(dev_priv);

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		u32 head = GET_RING_HEAD(dev_priv);

		ring->space = (head - ring->tail) * sizeof(u32);
		if (ring->space <= 0)
			ring->space += ring->size;
		if (ring->space > n)
			return 0;

		dev_priv->stats.boxes |= RADEON_BOX_WAIT_IDLE;

		if (head != last_head)
			i = 0;
		last_head = head;

		DRM_UDELAY(1);
	}

	/* FIXME: This return value is ignored in the BEGIN_RING macro! */
#if RADEON_FIFO_DEBUG
	radeon_status(dev_priv);
	DRM_ERROR("failed!\n");
#endif
	return -EBUSY;
}

static int radeon_cp_get_buffers(struct drm_device *dev,
				 struct drm_file *file_priv,
				 struct drm_dma * d)
{
	int i;
	struct drm_buf *buf;

	for (i = d->granted_count; i < d->request_count; i++) {
		buf = radeon_freelist_get(dev);
		if (!buf)
			return -EBUSY;	/* NOTE: broken client */

		buf->file_priv = file_priv;

		if (DRM_COPY_TO_USER(&d->request_indices[i], &buf->idx,
				     sizeof(buf->idx)))
			return -EFAULT;
		if (DRM_COPY_TO_USER(&d->request_sizes[i], &buf->total,
				     sizeof(buf->total)))
			return -EFAULT;

		d->granted_count++;
	}
	return 0;
}

int radeon_cp_buffers(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_device_dma *dma = dev->dma;
	int ret = 0;
	struct drm_dma *d = data;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	/* Please don't send us buffers.
	 */
	if (d->send_count != 0) {
		DRM_ERROR("Process %d trying to send %d buffers via drmDMA\n",
			  DRM_CURRENTPID, d->send_count);
		return -EINVAL;
	}

	/* We'll send you buffers.
	 */
	if (d->request_count < 0 || d->request_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to get %d buffers (of %d max)\n",
			  DRM_CURRENTPID, d->request_count, dma->buf_count);
		return -EINVAL;
	}

	d->granted_count = 0;

	if (d->request_count) {
		ret = radeon_cp_get_buffers(dev, file_priv, d);
	}

	return ret;
}

int radeon_driver_load(struct drm_device *dev, unsigned long flags)
{
	drm_radeon_private_t *dev_priv;
	int ret = 0;

	dev_priv = drm_alloc(sizeof(drm_radeon_private_t), DRM_MEM_DRIVER);
	if (dev_priv == NULL)
		return -ENOMEM;

	memset(dev_priv, 0, sizeof(drm_radeon_private_t));
	dev->dev_private = (void *)dev_priv;
	dev_priv->flags = flags;

	switch (flags & RADEON_FAMILY_MASK) {
	case CHIP_R100:
	case CHIP_RV200:
	case CHIP_R200:
	case CHIP_R300:
	case CHIP_R350:
	case CHIP_R420:
	case CHIP_RV410:
		dev_priv->flags |= RADEON_HAS_HIERZ;
		break;
	default:
		/* all other chips have no hierarchical z buffer */
		break;
	}

	if (drm_device_is_agp(dev))
		dev_priv->flags |= RADEON_IS_AGP;
	else if (drm_device_is_pcie(dev))
		dev_priv->flags |= RADEON_IS_PCIE;
	else
		dev_priv->flags |= RADEON_IS_PCI;

	DRM_DEBUG("%s card detected\n",
		  ((dev_priv->flags & RADEON_IS_AGP) ? "AGP" : (((dev_priv->flags & RADEON_IS_PCIE) ? "PCIE" : "PCI"))));
	return ret;
}

/* Create mappings for registers and framebuffer so userland doesn't necessarily
 * have to find them.
 */
int radeon_driver_firstopen(struct drm_device *dev)
{
	int ret;
	drm_local_map_t *map;
	drm_radeon_private_t *dev_priv = dev->dev_private;

	dev_priv->gart_info.table_size = RADEON_PCIGART_TABLE_SIZE;

	ret = drm_addmap(dev, drm_get_resource_start(dev, 2),
			 drm_get_resource_len(dev, 2), _DRM_REGISTERS,
			 _DRM_READ_ONLY, &dev_priv->mmio);
	if (ret != 0)
		return ret;

	ret = drm_addmap(dev, drm_get_resource_start(dev, 0),
			 drm_get_resource_len(dev, 0), _DRM_FRAME_BUFFER,
			 _DRM_WRITE_COMBINING, &map);
	if (ret != 0)
		return ret;

	return 0;
}

int radeon_driver_unload(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG("\n");
	drm_free(dev_priv, sizeof(*dev_priv), DRM_MEM_DRIVER);

	dev->dev_private = NULL;
	return 0;
}
