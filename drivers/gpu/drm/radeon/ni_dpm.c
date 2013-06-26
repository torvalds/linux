/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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

#include "drmP.h"
#include "radeon.h"
#include "nid.h"
#include "r600_dpm.h"
#include "ni_dpm.h"
#include "atom.h"

#define MC_CG_ARB_FREQ_F0           0x0a
#define MC_CG_ARB_FREQ_F1           0x0b
#define MC_CG_ARB_FREQ_F2           0x0c
#define MC_CG_ARB_FREQ_F3           0x0d

#define SMC_RAM_END 0xC000

static const struct ni_cac_weights cac_weights_cayman_xt =
{
	0x15,
	0x2,
	0x19,
	0x2,
	0x8,
	0x14,
	0x2,
	0x16,
	0xE,
	0x17,
	0x13,
	0x2B,
	0x10,
	0x7,
	0x5,
	0x5,
	0x5,
	0x2,
	0x3,
	0x9,
	0x10,
	0x10,
	0x2B,
	0xA,
	0x9,
	0x4,
	0xD,
	0xD,
	0x3E,
	0x18,
	0x14,
	0,
	0x3,
	0x3,
	0x5,
	0,
	0x2,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0x1CC,
	0,
	0x164,
	1,
	1,
	1,
	1,
	12,
	12,
	12,
	0x12,
	0x1F,
	132,
	5,
	7,
	0,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	true
};

static const struct ni_cac_weights cac_weights_cayman_pro =
{
	0x16,
	0x4,
	0x10,
	0x2,
	0xA,
	0x16,
	0x2,
	0x18,
	0x10,
	0x1A,
	0x16,
	0x2D,
	0x12,
	0xA,
	0x6,
	0x6,
	0x6,
	0x2,
	0x4,
	0xB,
	0x11,
	0x11,
	0x2D,
	0xC,
	0xC,
	0x7,
	0x10,
	0x10,
	0x3F,
	0x1A,
	0x16,
	0,
	0x7,
	0x4,
	0x6,
	1,
	0x2,
	0x1,
	0,
	0,
	0,
	0,
	0,
	0,
	0x30,
	0,
	0x1CF,
	0,
	0x166,
	1,
	1,
	1,
	1,
	12,
	12,
	12,
	0x15,
	0x1F,
	132,
	6,
	6,
	0,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	true
};

static const struct ni_cac_weights cac_weights_cayman_le =
{
	0x7,
	0xE,
	0x1,
	0xA,
	0x1,
	0x3F,
	0x2,
	0x18,
	0x10,
	0x1A,
	0x1,
	0x3F,
	0x1,
	0xE,
	0x6,
	0x6,
	0x6,
	0x2,
	0x4,
	0x9,
	0x1A,
	0x1A,
	0x2C,
	0xA,
	0x11,
	0x8,
	0x19,
	0x19,
	0x1,
	0x1,
	0x1A,
	0,
	0x8,
	0x5,
	0x8,
	0x1,
	0x3,
	0x1,
	0,
	0,
	0,
	0,
	0,
	0,
	0x38,
	0x38,
	0x239,
	0x3,
	0x18A,
	1,
	1,
	1,
	1,
	12,
	12,
	12,
	0x15,
	0x22,
	132,
	6,
	6,
	0,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	true
};

#define NISLANDS_MGCG_SEQUENCE  300

static const u32 cayman_cgcg_cgls_default[] =
{
	0x000008f8, 0x00000010, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000011, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000012, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000013, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000014, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000015, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000016, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000017, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000018, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000019, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x0000001a, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x0000001b, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000020, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000021, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000022, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000023, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000024, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000025, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000026, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000027, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000028, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000029, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x0000002a, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x0000002b, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff
};
#define CAYMAN_CGCG_CGLS_DEFAULT_LENGTH sizeof(cayman_cgcg_cgls_default) / (3 * sizeof(u32))

static const u32 cayman_cgcg_cgls_disable[] =
{
	0x000008f8, 0x00000010, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000011, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000012, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000013, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000014, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000015, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000016, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000017, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000018, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000019, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x0000001a, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x0000001b, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000020, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000021, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000022, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000023, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000024, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000025, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000026, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000027, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000028, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000029, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x0000002a, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x0000002b, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x00000644, 0x000f7902, 0x001f4180,
	0x00000644, 0x000f3802, 0x001f4180
};
#define CAYMAN_CGCG_CGLS_DISABLE_LENGTH sizeof(cayman_cgcg_cgls_disable) / (3 * sizeof(u32))

static const u32 cayman_cgcg_cgls_enable[] =
{
	0x00000644, 0x000f7882, 0x001f4080,
	0x000008f8, 0x00000010, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000011, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000012, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000013, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000014, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000015, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000016, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000017, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000018, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000019, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x0000001a, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x0000001b, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000020, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000021, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000022, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000023, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000024, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000025, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000026, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000027, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000028, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000029, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x0000002a, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x0000002b, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff
};
#define CAYMAN_CGCG_CGLS_ENABLE_LENGTH  sizeof(cayman_cgcg_cgls_enable) / (3 * sizeof(u32))

static const u32 cayman_mgcg_default[] =
{
	0x0000802c, 0xc0000000, 0xffffffff,
	0x00003fc4, 0xc0000000, 0xffffffff,
	0x00005448, 0x00000100, 0xffffffff,
	0x000055e4, 0x00000100, 0xffffffff,
	0x0000160c, 0x00000100, 0xffffffff,
	0x00008984, 0x06000100, 0xffffffff,
	0x0000c164, 0x00000100, 0xffffffff,
	0x00008a18, 0x00000100, 0xffffffff,
	0x0000897c, 0x06000100, 0xffffffff,
	0x00008b28, 0x00000100, 0xffffffff,
	0x00009144, 0x00800200, 0xffffffff,
	0x00009a60, 0x00000100, 0xffffffff,
	0x00009868, 0x00000100, 0xffffffff,
	0x00008d58, 0x00000100, 0xffffffff,
	0x00009510, 0x00000100, 0xffffffff,
	0x0000949c, 0x00000100, 0xffffffff,
	0x00009654, 0x00000100, 0xffffffff,
	0x00009030, 0x00000100, 0xffffffff,
	0x00009034, 0x00000100, 0xffffffff,
	0x00009038, 0x00000100, 0xffffffff,
	0x0000903c, 0x00000100, 0xffffffff,
	0x00009040, 0x00000100, 0xffffffff,
	0x0000a200, 0x00000100, 0xffffffff,
	0x0000a204, 0x00000100, 0xffffffff,
	0x0000a208, 0x00000100, 0xffffffff,
	0x0000a20c, 0x00000100, 0xffffffff,
	0x00009744, 0x00000100, 0xffffffff,
	0x00003f80, 0x00000100, 0xffffffff,
	0x0000a210, 0x00000100, 0xffffffff,
	0x0000a214, 0x00000100, 0xffffffff,
	0x000004d8, 0x00000100, 0xffffffff,
	0x00009664, 0x00000100, 0xffffffff,
	0x00009698, 0x00000100, 0xffffffff,
	0x000004d4, 0x00000200, 0xffffffff,
	0x000004d0, 0x00000000, 0xffffffff,
	0x000030cc, 0x00000104, 0xffffffff,
	0x0000d0c0, 0x00000100, 0xffffffff,
	0x0000d8c0, 0x00000100, 0xffffffff,
	0x0000802c, 0x40000000, 0xffffffff,
	0x00003fc4, 0x40000000, 0xffffffff,
	0x0000915c, 0x00010000, 0xffffffff,
	0x00009160, 0x00030002, 0xffffffff,
	0x00009164, 0x00050004, 0xffffffff,
	0x00009168, 0x00070006, 0xffffffff,
	0x00009178, 0x00070000, 0xffffffff,
	0x0000917c, 0x00030002, 0xffffffff,
	0x00009180, 0x00050004, 0xffffffff,
	0x0000918c, 0x00010006, 0xffffffff,
	0x00009190, 0x00090008, 0xffffffff,
	0x00009194, 0x00070000, 0xffffffff,
	0x00009198, 0x00030002, 0xffffffff,
	0x0000919c, 0x00050004, 0xffffffff,
	0x000091a8, 0x00010006, 0xffffffff,
	0x000091ac, 0x00090008, 0xffffffff,
	0x000091b0, 0x00070000, 0xffffffff,
	0x000091b4, 0x00030002, 0xffffffff,
	0x000091b8, 0x00050004, 0xffffffff,
	0x000091c4, 0x00010006, 0xffffffff,
	0x000091c8, 0x00090008, 0xffffffff,
	0x000091cc, 0x00070000, 0xffffffff,
	0x000091d0, 0x00030002, 0xffffffff,
	0x000091d4, 0x00050004, 0xffffffff,
	0x000091e0, 0x00010006, 0xffffffff,
	0x000091e4, 0x00090008, 0xffffffff,
	0x000091e8, 0x00000000, 0xffffffff,
	0x000091ec, 0x00070000, 0xffffffff,
	0x000091f0, 0x00030002, 0xffffffff,
	0x000091f4, 0x00050004, 0xffffffff,
	0x00009200, 0x00010006, 0xffffffff,
	0x00009204, 0x00090008, 0xffffffff,
	0x00009208, 0x00070000, 0xffffffff,
	0x0000920c, 0x00030002, 0xffffffff,
	0x00009210, 0x00050004, 0xffffffff,
	0x0000921c, 0x00010006, 0xffffffff,
	0x00009220, 0x00090008, 0xffffffff,
	0x00009224, 0x00070000, 0xffffffff,
	0x00009228, 0x00030002, 0xffffffff,
	0x0000922c, 0x00050004, 0xffffffff,
	0x00009238, 0x00010006, 0xffffffff,
	0x0000923c, 0x00090008, 0xffffffff,
	0x00009240, 0x00070000, 0xffffffff,
	0x00009244, 0x00030002, 0xffffffff,
	0x00009248, 0x00050004, 0xffffffff,
	0x00009254, 0x00010006, 0xffffffff,
	0x00009258, 0x00090008, 0xffffffff,
	0x0000925c, 0x00070000, 0xffffffff,
	0x00009260, 0x00030002, 0xffffffff,
	0x00009264, 0x00050004, 0xffffffff,
	0x00009270, 0x00010006, 0xffffffff,
	0x00009274, 0x00090008, 0xffffffff,
	0x00009278, 0x00070000, 0xffffffff,
	0x0000927c, 0x00030002, 0xffffffff,
	0x00009280, 0x00050004, 0xffffffff,
	0x0000928c, 0x00010006, 0xffffffff,
	0x00009290, 0x00090008, 0xffffffff,
	0x000092a8, 0x00070000, 0xffffffff,
	0x000092ac, 0x00030002, 0xffffffff,
	0x000092b0, 0x00050004, 0xffffffff,
	0x000092bc, 0x00010006, 0xffffffff,
	0x000092c0, 0x00090008, 0xffffffff,
	0x000092c4, 0x00070000, 0xffffffff,
	0x000092c8, 0x00030002, 0xffffffff,
	0x000092cc, 0x00050004, 0xffffffff,
	0x000092d8, 0x00010006, 0xffffffff,
	0x000092dc, 0x00090008, 0xffffffff,
	0x00009294, 0x00000000, 0xffffffff,
	0x0000802c, 0x40010000, 0xffffffff,
	0x00003fc4, 0x40010000, 0xffffffff,
	0x0000915c, 0x00010000, 0xffffffff,
	0x00009160, 0x00030002, 0xffffffff,
	0x00009164, 0x00050004, 0xffffffff,
	0x00009168, 0x00070006, 0xffffffff,
	0x00009178, 0x00070000, 0xffffffff,
	0x0000917c, 0x00030002, 0xffffffff,
	0x00009180, 0x00050004, 0xffffffff,
	0x0000918c, 0x00010006, 0xffffffff,
	0x00009190, 0x00090008, 0xffffffff,
	0x00009194, 0x00070000, 0xffffffff,
	0x00009198, 0x00030002, 0xffffffff,
	0x0000919c, 0x00050004, 0xffffffff,
	0x000091a8, 0x00010006, 0xffffffff,
	0x000091ac, 0x00090008, 0xffffffff,
	0x000091b0, 0x00070000, 0xffffffff,
	0x000091b4, 0x00030002, 0xffffffff,
	0x000091b8, 0x00050004, 0xffffffff,
	0x000091c4, 0x00010006, 0xffffffff,
	0x000091c8, 0x00090008, 0xffffffff,
	0x000091cc, 0x00070000, 0xffffffff,
	0x000091d0, 0x00030002, 0xffffffff,
	0x000091d4, 0x00050004, 0xffffffff,
	0x000091e0, 0x00010006, 0xffffffff,
	0x000091e4, 0x00090008, 0xffffffff,
	0x000091e8, 0x00000000, 0xffffffff,
	0x000091ec, 0x00070000, 0xffffffff,
	0x000091f0, 0x00030002, 0xffffffff,
	0x000091f4, 0x00050004, 0xffffffff,
	0x00009200, 0x00010006, 0xffffffff,
	0x00009204, 0x00090008, 0xffffffff,
	0x00009208, 0x00070000, 0xffffffff,
	0x0000920c, 0x00030002, 0xffffffff,
	0x00009210, 0x00050004, 0xffffffff,
	0x0000921c, 0x00010006, 0xffffffff,
	0x00009220, 0x00090008, 0xffffffff,
	0x00009224, 0x00070000, 0xffffffff,
	0x00009228, 0x00030002, 0xffffffff,
	0x0000922c, 0x00050004, 0xffffffff,
	0x00009238, 0x00010006, 0xffffffff,
	0x0000923c, 0x00090008, 0xffffffff,
	0x00009240, 0x00070000, 0xffffffff,
	0x00009244, 0x00030002, 0xffffffff,
	0x00009248, 0x00050004, 0xffffffff,
	0x00009254, 0x00010006, 0xffffffff,
	0x00009258, 0x00090008, 0xffffffff,
	0x0000925c, 0x00070000, 0xffffffff,
	0x00009260, 0x00030002, 0xffffffff,
	0x00009264, 0x00050004, 0xffffffff,
	0x00009270, 0x00010006, 0xffffffff,
	0x00009274, 0x00090008, 0xffffffff,
	0x00009278, 0x00070000, 0xffffffff,
	0x0000927c, 0x00030002, 0xffffffff,
	0x00009280, 0x00050004, 0xffffffff,
	0x0000928c, 0x00010006, 0xffffffff,
	0x00009290, 0x00090008, 0xffffffff,
	0x000092a8, 0x00070000, 0xffffffff,
	0x000092ac, 0x00030002, 0xffffffff,
	0x000092b0, 0x00050004, 0xffffffff,
	0x000092bc, 0x00010006, 0xffffffff,
	0x000092c0, 0x00090008, 0xffffffff,
	0x000092c4, 0x00070000, 0xffffffff,
	0x000092c8, 0x00030002, 0xffffffff,
	0x000092cc, 0x00050004, 0xffffffff,
	0x000092d8, 0x00010006, 0xffffffff,
	0x000092dc, 0x00090008, 0xffffffff,
	0x00009294, 0x00000000, 0xffffffff,
	0x0000802c, 0xc0000000, 0xffffffff,
	0x00003fc4, 0xc0000000, 0xffffffff,
	0x000008f8, 0x00000010, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000011, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000012, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000013, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000014, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000015, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000016, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000017, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000018, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000019, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x0000001a, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x0000001b, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff
};
#define CAYMAN_MGCG_DEFAULT_LENGTH sizeof(cayman_mgcg_default) / (3 * sizeof(u32))

static const u32 cayman_mgcg_disable[] =
{
	0x0000802c, 0xc0000000, 0xffffffff,
	0x000008f8, 0x00000000, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000001, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000002, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x000008f8, 0x00000003, 0xffffffff,
	0x000008fc, 0xffffffff, 0xffffffff,
	0x00009150, 0x00600000, 0xffffffff
};
#define CAYMAN_MGCG_DISABLE_LENGTH   sizeof(cayman_mgcg_disable) / (3 * sizeof(u32))

static const u32 cayman_mgcg_enable[] =
{
	0x0000802c, 0xc0000000, 0xffffffff,
	0x000008f8, 0x00000000, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000001, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000002, 0xffffffff,
	0x000008fc, 0x00600000, 0xffffffff,
	0x000008f8, 0x00000003, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x00009150, 0x96944200, 0xffffffff
};

#define CAYMAN_MGCG_ENABLE_LENGTH   sizeof(cayman_mgcg_enable) / (3 * sizeof(u32))

#define NISLANDS_SYSLS_SEQUENCE  100

static const u32 cayman_sysls_default[] =
{
	/* Register,   Value,     Mask bits */
	0x000055e8, 0x00000000, 0xffffffff,
	0x0000d0bc, 0x00000000, 0xffffffff,
	0x0000d8bc, 0x00000000, 0xffffffff,
	0x000015c0, 0x000c1401, 0xffffffff,
	0x0000264c, 0x000c0400, 0xffffffff,
	0x00002648, 0x000c0400, 0xffffffff,
	0x00002650, 0x000c0400, 0xffffffff,
	0x000020b8, 0x000c0400, 0xffffffff,
	0x000020bc, 0x000c0400, 0xffffffff,
	0x000020c0, 0x000c0c80, 0xffffffff,
	0x0000f4a0, 0x000000c0, 0xffffffff,
	0x0000f4a4, 0x00680fff, 0xffffffff,
	0x00002f50, 0x00000404, 0xffffffff,
	0x000004c8, 0x00000001, 0xffffffff,
	0x000064ec, 0x00000000, 0xffffffff,
	0x00000c7c, 0x00000000, 0xffffffff,
	0x00008dfc, 0x00000000, 0xffffffff
};
#define CAYMAN_SYSLS_DEFAULT_LENGTH sizeof(cayman_sysls_default) / (3 * sizeof(u32))

static const u32 cayman_sysls_disable[] =
{
	/* Register,   Value,     Mask bits */
	0x0000d0c0, 0x00000000, 0xffffffff,
	0x0000d8c0, 0x00000000, 0xffffffff,
	0x000055e8, 0x00000000, 0xffffffff,
	0x0000d0bc, 0x00000000, 0xffffffff,
	0x0000d8bc, 0x00000000, 0xffffffff,
	0x000015c0, 0x00041401, 0xffffffff,
	0x0000264c, 0x00040400, 0xffffffff,
	0x00002648, 0x00040400, 0xffffffff,
	0x00002650, 0x00040400, 0xffffffff,
	0x000020b8, 0x00040400, 0xffffffff,
	0x000020bc, 0x00040400, 0xffffffff,
	0x000020c0, 0x00040c80, 0xffffffff,
	0x0000f4a0, 0x000000c0, 0xffffffff,
	0x0000f4a4, 0x00680000, 0xffffffff,
	0x00002f50, 0x00000404, 0xffffffff,
	0x000004c8, 0x00000001, 0xffffffff,
	0x000064ec, 0x00007ffd, 0xffffffff,
	0x00000c7c, 0x0000ff00, 0xffffffff,
	0x00008dfc, 0x0000007f, 0xffffffff
};
#define CAYMAN_SYSLS_DISABLE_LENGTH sizeof(cayman_sysls_disable) / (3 * sizeof(u32))

static const u32 cayman_sysls_enable[] =
{
	/* Register,   Value,     Mask bits */
	0x000055e8, 0x00000001, 0xffffffff,
	0x0000d0bc, 0x00000100, 0xffffffff,
	0x0000d8bc, 0x00000100, 0xffffffff,
	0x000015c0, 0x000c1401, 0xffffffff,
	0x0000264c, 0x000c0400, 0xffffffff,
	0x00002648, 0x000c0400, 0xffffffff,
	0x00002650, 0x000c0400, 0xffffffff,
	0x000020b8, 0x000c0400, 0xffffffff,
	0x000020bc, 0x000c0400, 0xffffffff,
	0x000020c0, 0x000c0c80, 0xffffffff,
	0x0000f4a0, 0x000000c0, 0xffffffff,
	0x0000f4a4, 0x00680fff, 0xffffffff,
	0x00002f50, 0x00000903, 0xffffffff,
	0x000004c8, 0x00000000, 0xffffffff,
	0x000064ec, 0x00000000, 0xffffffff,
	0x00000c7c, 0x00000000, 0xffffffff,
	0x00008dfc, 0x00000000, 0xffffffff
};
#define CAYMAN_SYSLS_ENABLE_LENGTH sizeof(cayman_sysls_enable) / (3 * sizeof(u32))

struct rv7xx_power_info *rv770_get_pi(struct radeon_device *rdev);
struct evergreen_power_info *evergreen_get_pi(struct radeon_device *rdev);

static struct ni_power_info *ni_get_pi(struct radeon_device *rdev)
{
        struct ni_power_info *pi = rdev->pm.dpm.priv;

        return pi;
}

struct ni_ps *ni_get_ps(struct radeon_ps *rps)
{
	struct ni_ps *ps = rps->ps_priv;

	return ps;
}

/* XXX: fix for kernel use  */
#if 0
static double ni_exp(double x)
{
	int count = 1;
	double sum = 1.0, term, tolerance = 0.000000001, y = x;

	if (x < 0)
		y = -1 * x;
	term  = y;

	while (term >= tolerance) {
		sum = sum + term;
		count = count + 1;
		term  = term * (y / count);
	}

	if (x < 0)
		sum = 1.0 / sum;

	return sum;
}
#endif

static void ni_calculate_leakage_for_v_and_t_formula(const struct ni_leakage_coeffients *coeff,
						     u16 v, s32 t,
						     u32 ileakage,
						     u32 *leakage)
{
/* XXX: fix for kernel use  */
#if 0
	double kt, kv, leakage_w, i_leakage, vddc, temperature;

	i_leakage   = ((double)ileakage) / 1000;
	vddc        = ((double)v) / 1000;
	temperature = ((double)t) / 1000;

	kt = (((double)(coeff->at)) / 1000) * ni_exp((((double)(coeff->bt)) / 1000) * temperature);
	kv = (((double)(coeff->av)) / 1000) * ni_exp((((double)(coeff->bv)) / 1000) * vddc);

	leakage_w = i_leakage * kt * kv * vddc;

	*leakage = (u32)(leakage_w * 1000);
#endif
}

static void ni_calculate_leakage_for_v_and_t(struct radeon_device *rdev,
					     const struct ni_leakage_coeffients *coeff,
					     u16 v,
					     s32 t,
					     u32 i_leakage,
					     u32 *leakage)
{
	ni_calculate_leakage_for_v_and_t_formula(coeff, v, t, i_leakage, leakage);
}

static void ni_apply_state_adjust_rules(struct radeon_device *rdev)
{
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	struct radeon_ps *rps = rdev->pm.dpm.requested_ps;
	struct ni_ps *ps = ni_get_ps(rps);
	struct radeon_clock_and_voltage_limits *max_limits;
	bool disable_mclk_switching;
	u32 mclk, sclk;
	u16 vddc, vddci;
	int i;

	/* point to the hw copy since this function will modify the ps */
	ni_pi->hw_ps = *ps;
	rdev->pm.dpm.hw_ps.ps_priv = &ni_pi->hw_ps;
	ps = &ni_pi->hw_ps;

	if (rdev->pm.dpm.new_active_crtc_count > 1)
		disable_mclk_switching = true;
	else
		disable_mclk_switching = false;

	if (rdev->pm.dpm.ac_power)
		max_limits = &rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac;
	else
		max_limits = &rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc;

	if (rdev->pm.dpm.ac_power == false) {
		for (i = 0; i < ps->performance_level_count; i++) {
			if (ps->performance_levels[i].mclk > max_limits->mclk)
				ps->performance_levels[i].mclk = max_limits->mclk;
			if (ps->performance_levels[i].sclk > max_limits->sclk)
				ps->performance_levels[i].sclk = max_limits->sclk;
			if (ps->performance_levels[i].vddc > max_limits->vddc)
				ps->performance_levels[i].vddc = max_limits->vddc;
			if (ps->performance_levels[i].vddci > max_limits->vddci)
				ps->performance_levels[i].vddci = max_limits->vddci;
		}
	}

	/* XXX validate the min clocks required for display */

	if (disable_mclk_switching) {
		mclk  = ps->performance_levels[ps->performance_level_count - 1].mclk;
		sclk = ps->performance_levels[0].sclk;
		vddc = ps->performance_levels[0].vddc;
		vddci = ps->performance_levels[ps->performance_level_count - 1].vddci;
	} else {
		sclk = ps->performance_levels[0].sclk;
		mclk = ps->performance_levels[0].mclk;
		vddc = ps->performance_levels[0].vddc;
		vddci = ps->performance_levels[0].vddci;
	}

	/* adjusted low state */
	ps->performance_levels[0].sclk = sclk;
	ps->performance_levels[0].mclk = mclk;
	ps->performance_levels[0].vddc = vddc;
	ps->performance_levels[0].vddci = vddci;

	btc_skip_blacklist_clocks(rdev, max_limits->sclk, max_limits->mclk,
				  &ps->performance_levels[0].sclk,
				  &ps->performance_levels[0].mclk);

	for (i = 1; i < ps->performance_level_count; i++) {
		if (ps->performance_levels[i].sclk < ps->performance_levels[i - 1].sclk)
			ps->performance_levels[i].sclk = ps->performance_levels[i - 1].sclk;
		if (ps->performance_levels[i].vddc < ps->performance_levels[i - 1].vddc)
			ps->performance_levels[i].vddc = ps->performance_levels[i - 1].vddc;
	}

	if (disable_mclk_switching) {
		mclk = ps->performance_levels[0].mclk;
		for (i = 1; i < ps->performance_level_count; i++) {
			if (mclk < ps->performance_levels[i].mclk)
				mclk = ps->performance_levels[i].mclk;
		}
		for (i = 0; i < ps->performance_level_count; i++) {
			ps->performance_levels[i].mclk = mclk;
			ps->performance_levels[i].vddci = vddci;
		}
	} else {
		for (i = 1; i < ps->performance_level_count; i++) {
			if (ps->performance_levels[i].mclk < ps->performance_levels[i - 1].mclk)
				ps->performance_levels[i].mclk = ps->performance_levels[i - 1].mclk;
			if (ps->performance_levels[i].vddci < ps->performance_levels[i - 1].vddci)
				ps->performance_levels[i].vddci = ps->performance_levels[i - 1].vddci;
		}
	}

	for (i = 1; i < ps->performance_level_count; i++)
		btc_skip_blacklist_clocks(rdev, max_limits->sclk, max_limits->mclk,
					  &ps->performance_levels[i].sclk,
					  &ps->performance_levels[i].mclk);

	for (i = 0; i < ps->performance_level_count; i++)
		btc_adjust_clock_combinations(rdev, max_limits,
					      &ps->performance_levels[i]);

	for (i = 0; i < ps->performance_level_count; i++) {
		btc_apply_voltage_dependency_rules(&rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk,
						   ps->performance_levels[i].sclk,
						   max_limits->vddc,  &ps->performance_levels[i].vddc);
		btc_apply_voltage_dependency_rules(&rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk,
						   ps->performance_levels[i].mclk,
						   max_limits->vddci, &ps->performance_levels[i].vddci);
		btc_apply_voltage_dependency_rules(&rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk,
						   ps->performance_levels[i].mclk,
						   max_limits->vddc,  &ps->performance_levels[i].vddc);
		/* XXX validate the voltage required for display */
	}

	for (i = 0; i < ps->performance_level_count; i++) {
		btc_apply_voltage_delta_rules(rdev,
					      max_limits->vddc, max_limits->vddci,
					      &ps->performance_levels[i].vddc,
					      &ps->performance_levels[i].vddci);
	}

	ps->dc_compatible = true;
	for (i = 0; i < ps->performance_level_count; i++) {
		if (ps->performance_levels[i].vddc > rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc.vddc)
			ps->dc_compatible = false;

		if (ps->performance_levels[i].vddc < rdev->pm.dpm.dyn_state.min_vddc_for_pcie_gen2)
			ps->performance_levels[i].flags &= ~ATOM_PPLIB_R600_FLAGS_PCIEGEN2;
	}
}

static void ni_cg_clockgating_default(struct radeon_device *rdev)
{
	u32 count;
	const u32 *ps = NULL;

	ps = (const u32 *)&cayman_cgcg_cgls_default;
	count = CAYMAN_CGCG_CGLS_DEFAULT_LENGTH;

	btc_program_mgcg_hw_sequence(rdev, ps, count);
}

static void ni_gfx_clockgating_enable(struct radeon_device *rdev,
				      bool enable)
{
	u32 count;
	const u32 *ps = NULL;

	if (enable) {
		ps = (const u32 *)&cayman_cgcg_cgls_enable;
		count = CAYMAN_CGCG_CGLS_ENABLE_LENGTH;
	} else {
		ps = (const u32 *)&cayman_cgcg_cgls_disable;
		count = CAYMAN_CGCG_CGLS_DISABLE_LENGTH;
	}

	btc_program_mgcg_hw_sequence(rdev, ps, count);
}

static void ni_mg_clockgating_default(struct radeon_device *rdev)
{
	u32 count;
	const u32 *ps = NULL;

	ps = (const u32 *)&cayman_mgcg_default;
	count = CAYMAN_MGCG_DEFAULT_LENGTH;

	btc_program_mgcg_hw_sequence(rdev, ps, count);
}

static void ni_mg_clockgating_enable(struct radeon_device *rdev,
				     bool enable)
{
	u32 count;
	const u32 *ps = NULL;

	if (enable) {
		ps = (const u32 *)&cayman_mgcg_enable;
		count = CAYMAN_MGCG_ENABLE_LENGTH;
	} else {
		ps = (const u32 *)&cayman_mgcg_disable;
		count = CAYMAN_MGCG_DISABLE_LENGTH;
	}

	btc_program_mgcg_hw_sequence(rdev, ps, count);
}

static void ni_ls_clockgating_default(struct radeon_device *rdev)
{
	u32 count;
	const u32 *ps = NULL;

	ps = (const u32 *)&cayman_sysls_default;
	count = CAYMAN_SYSLS_DEFAULT_LENGTH;

	btc_program_mgcg_hw_sequence(rdev, ps, count);
}

static void ni_ls_clockgating_enable(struct radeon_device *rdev,
				     bool enable)
{
	u32 count;
	const u32 *ps = NULL;

	if (enable) {
		ps = (const u32 *)&cayman_sysls_enable;
		count = CAYMAN_SYSLS_ENABLE_LENGTH;
	} else {
		ps = (const u32 *)&cayman_sysls_disable;
		count = CAYMAN_SYSLS_DISABLE_LENGTH;
	}

	btc_program_mgcg_hw_sequence(rdev, ps, count);

}

static int ni_patch_single_dependency_table_based_on_leakage(struct radeon_device *rdev,
							     struct radeon_clock_voltage_dependency_table *table)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 i;

	if (table) {
		for (i = 0; i < table->count; i++) {
			if (0xff01 == table->entries[i].v) {
				if (pi->max_vddc == 0)
					return -EINVAL;
				table->entries[i].v = pi->max_vddc;
			}
		}
	}
	return 0;
}

static int ni_patch_dependency_tables_based_on_leakage(struct radeon_device *rdev)
{
	int ret = 0;

	ret = ni_patch_single_dependency_table_based_on_leakage(rdev,
								&rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk);

	ret = ni_patch_single_dependency_table_based_on_leakage(rdev,
								&rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk);
	return ret;
}

static void ni_stop_dpm(struct radeon_device *rdev)
{
	WREG32_P(GENERAL_PWRMGT, 0, ~GLOBAL_PWRMGT_EN);
}

#if 0
static int ni_notify_hw_of_power_source(struct radeon_device *rdev,
					bool ac_power)
{
	if (ac_power)
		return (rv770_send_msg_to_smc(rdev, PPSMC_MSG_RunningOnAC) == PPSMC_Result_OK) ?
			0 : -EINVAL;

	return 0;
}
#endif

static PPSMC_Result ni_send_msg_to_smc_with_parameter(struct radeon_device *rdev,
						      PPSMC_Msg msg, u32 parameter)
{
	WREG32(SMC_SCRATCH0, parameter);
	return rv770_send_msg_to_smc(rdev, msg);
}

static int ni_restrict_performance_levels_before_switch(struct radeon_device *rdev)
{
	if (rv770_send_msg_to_smc(rdev, PPSMC_MSG_NoForcedLevel) != PPSMC_Result_OK)
		return -EINVAL;

	return (ni_send_msg_to_smc_with_parameter(rdev, PPSMC_MSG_SetEnabledLevels, 1) == PPSMC_Result_OK) ?
		0 : -EINVAL;
}

#if 0
static int ni_unrestrict_performance_levels_after_switch(struct radeon_device *rdev)
{
	if (ni_send_msg_to_smc_with_parameter(rdev, PPSMC_MSG_SetForcedLevels, 0) != PPSMC_Result_OK)
		return -EINVAL;

	return (ni_send_msg_to_smc_with_parameter(rdev, PPSMC_MSG_SetEnabledLevels, 0) == PPSMC_Result_OK) ?
		0 : -EINVAL;
}
#endif

static void ni_stop_smc(struct radeon_device *rdev)
{
	u32 tmp;
	int i;

	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(LB_SYNC_RESET_SEL) & LB_SYNC_RESET_SEL_MASK;
		if (tmp != 1)
			break;
		udelay(1);
	}

	udelay(100);

	r7xx_stop_smc(rdev);
}

static int ni_process_firmware_header(struct radeon_device *rdev)
{
        struct rv7xx_power_info *pi = rv770_get_pi(rdev);
        struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
        struct ni_power_info *ni_pi = ni_get_pi(rdev);
	u32 tmp;
	int ret;

	ret = rv770_read_smc_sram_dword(rdev,
					NISLANDS_SMC_FIRMWARE_HEADER_LOCATION +
					NISLANDS_SMC_FIRMWARE_HEADER_stateTable,
					&tmp, pi->sram_end);

	if (ret)
		return ret;

	pi->state_table_start = (u16)tmp;

	ret = rv770_read_smc_sram_dword(rdev,
					NISLANDS_SMC_FIRMWARE_HEADER_LOCATION +
					NISLANDS_SMC_FIRMWARE_HEADER_softRegisters,
					&tmp, pi->sram_end);

	if (ret)
		return ret;

	pi->soft_regs_start = (u16)tmp;

	ret = rv770_read_smc_sram_dword(rdev,
					NISLANDS_SMC_FIRMWARE_HEADER_LOCATION +
					NISLANDS_SMC_FIRMWARE_HEADER_mcRegisterTable,
					&tmp, pi->sram_end);

	if (ret)
		return ret;

	eg_pi->mc_reg_table_start = (u16)tmp;

	ret = rv770_read_smc_sram_dword(rdev,
					NISLANDS_SMC_FIRMWARE_HEADER_LOCATION +
					NISLANDS_SMC_FIRMWARE_HEADER_fanTable,
					&tmp, pi->sram_end);

	if (ret)
		return ret;

	ni_pi->fan_table_start = (u16)tmp;

	ret = rv770_read_smc_sram_dword(rdev,
					NISLANDS_SMC_FIRMWARE_HEADER_LOCATION +
					NISLANDS_SMC_FIRMWARE_HEADER_mcArbDramAutoRefreshTable,
					&tmp, pi->sram_end);

	if (ret)
		return ret;

	ni_pi->arb_table_start = (u16)tmp;

	ret = rv770_read_smc_sram_dword(rdev,
					NISLANDS_SMC_FIRMWARE_HEADER_LOCATION +
					NISLANDS_SMC_FIRMWARE_HEADER_cacTable,
					&tmp, pi->sram_end);

	if (ret)
		return ret;

	ni_pi->cac_table_start = (u16)tmp;

	ret = rv770_read_smc_sram_dword(rdev,
					NISLANDS_SMC_FIRMWARE_HEADER_LOCATION +
					NISLANDS_SMC_FIRMWARE_HEADER_spllTable,
					&tmp, pi->sram_end);

	if (ret)
		return ret;

	ni_pi->spll_table_start = (u16)tmp;


	return ret;
}

static void ni_read_clock_registers(struct radeon_device *rdev)
{
	struct ni_power_info *ni_pi = ni_get_pi(rdev);

	ni_pi->clock_registers.cg_spll_func_cntl = RREG32(CG_SPLL_FUNC_CNTL);
	ni_pi->clock_registers.cg_spll_func_cntl_2 = RREG32(CG_SPLL_FUNC_CNTL_2);
	ni_pi->clock_registers.cg_spll_func_cntl_3 = RREG32(CG_SPLL_FUNC_CNTL_3);
	ni_pi->clock_registers.cg_spll_func_cntl_4 = RREG32(CG_SPLL_FUNC_CNTL_4);
	ni_pi->clock_registers.cg_spll_spread_spectrum = RREG32(CG_SPLL_SPREAD_SPECTRUM);
	ni_pi->clock_registers.cg_spll_spread_spectrum_2 = RREG32(CG_SPLL_SPREAD_SPECTRUM_2);
	ni_pi->clock_registers.mpll_ad_func_cntl = RREG32(MPLL_AD_FUNC_CNTL);
	ni_pi->clock_registers.mpll_ad_func_cntl_2 = RREG32(MPLL_AD_FUNC_CNTL_2);
	ni_pi->clock_registers.mpll_dq_func_cntl = RREG32(MPLL_DQ_FUNC_CNTL);
	ni_pi->clock_registers.mpll_dq_func_cntl_2 = RREG32(MPLL_DQ_FUNC_CNTL_2);
	ni_pi->clock_registers.mclk_pwrmgt_cntl = RREG32(MCLK_PWRMGT_CNTL);
	ni_pi->clock_registers.dll_cntl = RREG32(DLL_CNTL);
	ni_pi->clock_registers.mpll_ss1 = RREG32(MPLL_SS1);
	ni_pi->clock_registers.mpll_ss2 = RREG32(MPLL_SS2);
}

#if 0
static int ni_enter_ulp_state(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	if (pi->gfx_clock_gating) {
                WREG32_P(SCLK_PWRMGT_CNTL, 0, ~DYN_GFX_CLK_OFF_EN);
		WREG32_P(SCLK_PWRMGT_CNTL, GFX_CLK_FORCE_ON, ~GFX_CLK_FORCE_ON);
                WREG32_P(SCLK_PWRMGT_CNTL, 0, ~GFX_CLK_FORCE_ON);
		RREG32(GB_ADDR_CONFIG);
        }

	WREG32_P(SMC_MSG, HOST_SMC_MSG(PPSMC_MSG_SwitchToMinimumPower),
                 ~HOST_SMC_MSG_MASK);

	udelay(25000);

	return 0;
}
#endif

static void ni_program_response_times(struct radeon_device *rdev)
{
	u32 voltage_response_time, backbias_response_time, acpi_delay_time, vbi_time_out;
	u32 vddc_dly, bb_dly, acpi_dly, vbi_dly, mclk_switch_limit;
	u32 reference_clock;

	rv770_write_smc_soft_register(rdev, NI_SMC_SOFT_REGISTER_mvdd_chg_time, 1);

	voltage_response_time = (u32)rdev->pm.dpm.voltage_response_time;
	backbias_response_time = (u32)rdev->pm.dpm.backbias_response_time;

	if (voltage_response_time == 0)
		voltage_response_time = 1000;

	if (backbias_response_time == 0)
		backbias_response_time = 1000;

	acpi_delay_time = 15000;
	vbi_time_out = 100000;

	reference_clock = radeon_get_xclk(rdev);

	vddc_dly = (voltage_response_time  * reference_clock) / 1600;
	bb_dly   = (backbias_response_time * reference_clock) / 1600;
	acpi_dly = (acpi_delay_time * reference_clock) / 1600;
	vbi_dly  = (vbi_time_out * reference_clock) / 1600;

	mclk_switch_limit = (460 * reference_clock) / 100;

	rv770_write_smc_soft_register(rdev, NI_SMC_SOFT_REGISTER_delay_vreg,  vddc_dly);
	rv770_write_smc_soft_register(rdev, NI_SMC_SOFT_REGISTER_delay_bbias, bb_dly);
	rv770_write_smc_soft_register(rdev, NI_SMC_SOFT_REGISTER_delay_acpi,  acpi_dly);
	rv770_write_smc_soft_register(rdev, NI_SMC_SOFT_REGISTER_mclk_chg_timeout, vbi_dly);
	rv770_write_smc_soft_register(rdev, NI_SMC_SOFT_REGISTER_mc_block_delay, 0xAA);
	rv770_write_smc_soft_register(rdev, NI_SMC_SOFT_REGISTER_mclk_switch_lim, mclk_switch_limit);
}

static void ni_populate_smc_voltage_table(struct radeon_device *rdev,
					  struct atom_voltage_table *voltage_table,
					  NISLANDS_SMC_STATETABLE *table)
{
	unsigned int i;

	for (i = 0; i < voltage_table->count; i++) {
		table->highSMIO[i] = 0;
		table->lowSMIO[i] |= cpu_to_be32(voltage_table->entries[i].smio_low);
	}
}

static void ni_populate_smc_voltage_tables(struct radeon_device *rdev,
					   NISLANDS_SMC_STATETABLE *table)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	unsigned char i;

	if (eg_pi->vddc_voltage_table.count) {
		ni_populate_smc_voltage_table(rdev, &eg_pi->vddc_voltage_table, table);
		table->voltageMaskTable.highMask[NISLANDS_SMC_VOLTAGEMASK_VDDC] = 0;
		table->voltageMaskTable.lowMask[NISLANDS_SMC_VOLTAGEMASK_VDDC] =
			cpu_to_be32(eg_pi->vddc_voltage_table.mask_low);

		for (i = 0; i < eg_pi->vddc_voltage_table.count; i++) {
			if (pi->max_vddc_in_table <= eg_pi->vddc_voltage_table.entries[i].value) {
				table->maxVDDCIndexInPPTable = i;
				break;
			}
		}
	}

	if (eg_pi->vddci_voltage_table.count) {
		ni_populate_smc_voltage_table(rdev, &eg_pi->vddci_voltage_table, table);

		table->voltageMaskTable.highMask[NISLANDS_SMC_VOLTAGEMASK_VDDCI] = 0;
		table->voltageMaskTable.lowMask[NISLANDS_SMC_VOLTAGEMASK_VDDCI] =
			cpu_to_be32(eg_pi->vddc_voltage_table.mask_low);
	}
}

static int ni_populate_voltage_value(struct radeon_device *rdev,
				     struct atom_voltage_table *table,
				     u16 value,
				     NISLANDS_SMC_VOLTAGE_VALUE *voltage)
{
	unsigned int i;

	for (i = 0; i < table->count; i++) {
		if (value <= table->entries[i].value) {
			voltage->index = (u8)i;
			voltage->value = cpu_to_be16(table->entries[i].value);
			break;
		}
	}

	if (i >= table->count)
		return -EINVAL;

	return 0;
}

static void ni_populate_mvdd_value(struct radeon_device *rdev,
				   u32 mclk,
				   NISLANDS_SMC_VOLTAGE_VALUE *voltage)
{
        struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	if (!pi->mvdd_control) {
		voltage->index = eg_pi->mvdd_high_index;
                voltage->value = cpu_to_be16(MVDD_HIGH_VALUE);
		return;
	}

	if (mclk <= pi->mvdd_split_frequency) {
		voltage->index = eg_pi->mvdd_low_index;
		voltage->value = cpu_to_be16(MVDD_LOW_VALUE);
	} else {
		voltage->index = eg_pi->mvdd_high_index;
		voltage->value = cpu_to_be16(MVDD_HIGH_VALUE);
	}
}

static int ni_get_std_voltage_value(struct radeon_device *rdev,
				    NISLANDS_SMC_VOLTAGE_VALUE *voltage,
				    u16 *std_voltage)
{
	if (rdev->pm.dpm.dyn_state.cac_leakage_table.entries &&
	    ((u32)voltage->index < rdev->pm.dpm.dyn_state.cac_leakage_table.count))
		*std_voltage = rdev->pm.dpm.dyn_state.cac_leakage_table.entries[voltage->index].vddc;
	else
		*std_voltage = be16_to_cpu(voltage->value);

	return 0;
}

static void ni_populate_std_voltage_value(struct radeon_device *rdev,
					  u16 value, u8 index,
					  NISLANDS_SMC_VOLTAGE_VALUE *voltage)
{
	voltage->index = index;
	voltage->value = cpu_to_be16(value);
}

static u32 ni_get_smc_power_scaling_factor(struct radeon_device *rdev)
{
	u32 xclk_period;
	u32 xclk = radeon_get_xclk(rdev);
	u32 tmp = RREG32(CG_CAC_CTRL) & TID_CNT_MASK;

	xclk_period = (1000000000UL / xclk);
	xclk_period /= 10000UL;

	return tmp * xclk_period;
}

static u32 ni_scale_power_for_smc(u32 power_in_watts, u32 scaling_factor)
{
	return (power_in_watts * scaling_factor) << 2;
}

static u32 ni_calculate_power_boost_limit(struct radeon_device *rdev,
					  struct radeon_ps *radeon_state,
					  u32 near_tdp_limit)
{
	struct ni_ps *state = ni_get_ps(radeon_state);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	u32 power_boost_limit = 0;
	int ret;

	if (ni_pi->enable_power_containment &&
	    ni_pi->use_power_boost_limit) {
		NISLANDS_SMC_VOLTAGE_VALUE vddc;
		u16 std_vddc_med;
		u16 std_vddc_high;
		u64 tmp, n, d;

		if (state->performance_level_count < 3)
			return 0;

		ret = ni_populate_voltage_value(rdev, &eg_pi->vddc_voltage_table,
						state->performance_levels[state->performance_level_count - 2].vddc,
						&vddc);
		if (ret)
			return 0;

		ret = ni_get_std_voltage_value(rdev, &vddc, &std_vddc_med);
		if (ret)
			return 0;

		ret = ni_populate_voltage_value(rdev, &eg_pi->vddc_voltage_table,
						state->performance_levels[state->performance_level_count - 1].vddc,
						&vddc);
		if (ret)
			return 0;

		ret = ni_get_std_voltage_value(rdev, &vddc, &std_vddc_high);
		if (ret)
			return 0;

		n = ((u64)near_tdp_limit * ((u64)std_vddc_med * (u64)std_vddc_med) * 90);
		d = ((u64)std_vddc_high * (u64)std_vddc_high * 100);
		tmp = div64_u64(n, d);

		if (tmp >> 32)
			return 0;
		power_boost_limit = (u32)tmp;
	}

	return power_boost_limit;
}

static int ni_calculate_adjusted_tdp_limits(struct radeon_device *rdev,
					    bool adjust_polarity,
					    u32 tdp_adjustment,
					    u32 *tdp_limit,
					    u32 *near_tdp_limit)
{
	if (tdp_adjustment > (u32)rdev->pm.dpm.tdp_od_limit)
		return -EINVAL;

	if (adjust_polarity) {
		*tdp_limit = ((100 + tdp_adjustment) * rdev->pm.dpm.tdp_limit) / 100;
		*near_tdp_limit = rdev->pm.dpm.near_tdp_limit + (*tdp_limit - rdev->pm.dpm.tdp_limit);
	} else {
		*tdp_limit = ((100 - tdp_adjustment) * rdev->pm.dpm.tdp_limit) / 100;
		*near_tdp_limit = rdev->pm.dpm.near_tdp_limit - (rdev->pm.dpm.tdp_limit - *tdp_limit);
	}

	return 0;
}

static int ni_populate_smc_tdp_limits(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct ni_power_info *ni_pi = ni_get_pi(rdev);

	if (ni_pi->enable_power_containment) {
		struct radeon_ps *radeon_state = rdev->pm.dpm.requested_ps;
		NISLANDS_SMC_STATETABLE *smc_table = &ni_pi->smc_statetable;
		u32 scaling_factor = ni_get_smc_power_scaling_factor(rdev);
		u32 tdp_limit;
		u32 near_tdp_limit;
		u32 power_boost_limit;
		int ret;

		if (scaling_factor == 0)
			return -EINVAL;

		memset(smc_table, 0, sizeof(NISLANDS_SMC_STATETABLE));

		ret = ni_calculate_adjusted_tdp_limits(rdev,
						       false, /* ??? */
						       rdev->pm.dpm.tdp_adjustment,
						       &tdp_limit,
						       &near_tdp_limit);
		if (ret)
			return ret;

		power_boost_limit = ni_calculate_power_boost_limit(rdev, radeon_state,
								   near_tdp_limit);

		smc_table->dpm2Params.TDPLimit =
			cpu_to_be32(ni_scale_power_for_smc(tdp_limit, scaling_factor));
		smc_table->dpm2Params.NearTDPLimit =
			cpu_to_be32(ni_scale_power_for_smc(near_tdp_limit, scaling_factor));
		smc_table->dpm2Params.SafePowerLimit =
			cpu_to_be32(ni_scale_power_for_smc((near_tdp_limit * NISLANDS_DPM2_TDP_SAFE_LIMIT_PERCENT) / 100,
							   scaling_factor));
		smc_table->dpm2Params.PowerBoostLimit =
			cpu_to_be32(ni_scale_power_for_smc(power_boost_limit, scaling_factor));

		ret = rv770_copy_bytes_to_smc(rdev,
					      (u16)(pi->state_table_start + offsetof(NISLANDS_SMC_STATETABLE, dpm2Params) +
						    offsetof(PP_NIslands_DPM2Parameters, TDPLimit)),
					      (u8 *)(&smc_table->dpm2Params.TDPLimit),
					      sizeof(u32) * 4, pi->sram_end);
		if (ret)
			return ret;
	}

	return 0;
}

static int ni_copy_and_switch_arb_sets(struct radeon_device *rdev,
				       u32 arb_freq_src, u32 arb_freq_dest)
{
	u32 mc_arb_dram_timing;
	u32 mc_arb_dram_timing2;
	u32 burst_time;
	u32 mc_cg_config;

	switch (arb_freq_src) {
        case MC_CG_ARB_FREQ_F0:
		mc_arb_dram_timing  = RREG32(MC_ARB_DRAM_TIMING);
		mc_arb_dram_timing2 = RREG32(MC_ARB_DRAM_TIMING2);
		burst_time = (RREG32(MC_ARB_BURST_TIME) & STATE0_MASK) >> STATE0_SHIFT;
		break;
        case MC_CG_ARB_FREQ_F1:
		mc_arb_dram_timing  = RREG32(MC_ARB_DRAM_TIMING_1);
		mc_arb_dram_timing2 = RREG32(MC_ARB_DRAM_TIMING2_1);
		burst_time = (RREG32(MC_ARB_BURST_TIME) & STATE1_MASK) >> STATE1_SHIFT;
		break;
        case MC_CG_ARB_FREQ_F2:
		mc_arb_dram_timing  = RREG32(MC_ARB_DRAM_TIMING_2);
		mc_arb_dram_timing2 = RREG32(MC_ARB_DRAM_TIMING2_2);
		burst_time = (RREG32(MC_ARB_BURST_TIME) & STATE2_MASK) >> STATE2_SHIFT;
		break;
        case MC_CG_ARB_FREQ_F3:
		mc_arb_dram_timing  = RREG32(MC_ARB_DRAM_TIMING_3);
		mc_arb_dram_timing2 = RREG32(MC_ARB_DRAM_TIMING2_3);
		burst_time = (RREG32(MC_ARB_BURST_TIME) & STATE3_MASK) >> STATE3_SHIFT;
		break;
        default:
		return -EINVAL;
	}

	switch (arb_freq_dest) {
        case MC_CG_ARB_FREQ_F0:
		WREG32(MC_ARB_DRAM_TIMING, mc_arb_dram_timing);
		WREG32(MC_ARB_DRAM_TIMING2, mc_arb_dram_timing2);
		WREG32_P(MC_ARB_BURST_TIME, STATE0(burst_time), ~STATE0_MASK);
		break;
        case MC_CG_ARB_FREQ_F1:
		WREG32(MC_ARB_DRAM_TIMING_1, mc_arb_dram_timing);
		WREG32(MC_ARB_DRAM_TIMING2_1, mc_arb_dram_timing2);
		WREG32_P(MC_ARB_BURST_TIME, STATE1(burst_time), ~STATE1_MASK);
		break;
        case MC_CG_ARB_FREQ_F2:
		WREG32(MC_ARB_DRAM_TIMING_2, mc_arb_dram_timing);
		WREG32(MC_ARB_DRAM_TIMING2_2, mc_arb_dram_timing2);
		WREG32_P(MC_ARB_BURST_TIME, STATE2(burst_time), ~STATE2_MASK);
		break;
        case MC_CG_ARB_FREQ_F3:
		WREG32(MC_ARB_DRAM_TIMING_3, mc_arb_dram_timing);
		WREG32(MC_ARB_DRAM_TIMING2_3, mc_arb_dram_timing2);
		WREG32_P(MC_ARB_BURST_TIME, STATE3(burst_time), ~STATE3_MASK);
		break;
	default:
		return -EINVAL;
	}

	mc_cg_config = RREG32(MC_CG_CONFIG) | 0x0000000F;
	WREG32(MC_CG_CONFIG, mc_cg_config);
	WREG32_P(MC_ARB_CG, CG_ARB_REQ(arb_freq_dest), ~CG_ARB_REQ_MASK);

	return 0;
}

static int ni_init_arb_table_index(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	u32 tmp;
	int ret;

	ret = rv770_read_smc_sram_dword(rdev, ni_pi->arb_table_start,
					&tmp, pi->sram_end);
	if (ret)
		return ret;

	tmp &= 0x00FFFFFF;
	tmp |= ((u32)MC_CG_ARB_FREQ_F1) << 24;

	return rv770_write_smc_sram_dword(rdev, ni_pi->arb_table_start,
					  tmp, pi->sram_end);
}

static int ni_initial_switch_from_arb_f0_to_f1(struct radeon_device *rdev)
{
	return ni_copy_and_switch_arb_sets(rdev, MC_CG_ARB_FREQ_F0, MC_CG_ARB_FREQ_F1);
}

static int ni_force_switch_to_arb_f0(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	u32 tmp;
	int ret;

	ret = rv770_read_smc_sram_dword(rdev, ni_pi->arb_table_start,
					&tmp, pi->sram_end);
	if (ret)
		return ret;

	tmp = (tmp >> 24) & 0xff;

	if (tmp == MC_CG_ARB_FREQ_F0)
		return 0;

	return ni_copy_and_switch_arb_sets(rdev, tmp, MC_CG_ARB_FREQ_F0);
}

static int ni_populate_memory_timing_parameters(struct radeon_device *rdev,
						struct rv7xx_pl *pl,
						SMC_NIslands_MCArbDramTimingRegisterSet *arb_regs)
{
	u32 dram_timing;
	u32 dram_timing2;

	arb_regs->mc_arb_rfsh_rate =
		(u8)rv770_calculate_memory_refresh_rate(rdev, pl->sclk);


	radeon_atom_set_engine_dram_timings(rdev,
                                            pl->sclk,
                                            pl->mclk);

	dram_timing = RREG32(MC_ARB_DRAM_TIMING);
	dram_timing2 = RREG32(MC_ARB_DRAM_TIMING2);

	arb_regs->mc_arb_dram_timing  = cpu_to_be32(dram_timing);
	arb_regs->mc_arb_dram_timing2 = cpu_to_be32(dram_timing2);

	return 0;
}

static int ni_do_program_memory_timing_parameters(struct radeon_device *rdev,
						  struct radeon_ps *radeon_state,
						  unsigned int first_arb_set)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	struct ni_ps *state = ni_get_ps(radeon_state);
	SMC_NIslands_MCArbDramTimingRegisterSet arb_regs = { 0 };
	int i, ret = 0;

	for (i = 0; i < state->performance_level_count; i++) {
		ret = ni_populate_memory_timing_parameters(rdev, &state->performance_levels[i], &arb_regs);
		if (ret)
			break;

		ret = rv770_copy_bytes_to_smc(rdev,
					      (u16)(ni_pi->arb_table_start +
						    offsetof(SMC_NIslands_MCArbDramTimingRegisters, data) +
						    sizeof(SMC_NIslands_MCArbDramTimingRegisterSet) * (first_arb_set + i)),
					      (u8 *)&arb_regs,
					      (u16)sizeof(SMC_NIslands_MCArbDramTimingRegisterSet),
					      pi->sram_end);
		if (ret)
			break;
	}
	return ret;
}

static int ni_program_memory_timing_parameters(struct radeon_device *rdev)
{
	struct radeon_ps *radeon_new_state = rdev->pm.dpm.requested_ps;

	return ni_do_program_memory_timing_parameters(rdev, radeon_new_state,
						      NISLANDS_DRIVER_STATE_ARB_INDEX);
}

static void ni_populate_initial_mvdd_value(struct radeon_device *rdev,
					   struct NISLANDS_SMC_VOLTAGE_VALUE *voltage)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	voltage->index = eg_pi->mvdd_high_index;
	voltage->value = cpu_to_be16(MVDD_HIGH_VALUE);
}

static int ni_populate_smc_initial_state(struct radeon_device *rdev,
					 struct radeon_ps *radeon_initial_state,
					 NISLANDS_SMC_STATETABLE *table)
{
	struct ni_ps *initial_state = ni_get_ps(radeon_initial_state);
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	u32 reg;
	int ret;

	table->initialState.levels[0].mclk.vMPLL_AD_FUNC_CNTL =
		cpu_to_be32(ni_pi->clock_registers.mpll_ad_func_cntl);
	table->initialState.levels[0].mclk.vMPLL_AD_FUNC_CNTL_2 =
		cpu_to_be32(ni_pi->clock_registers.mpll_ad_func_cntl_2);
	table->initialState.levels[0].mclk.vMPLL_DQ_FUNC_CNTL =
		cpu_to_be32(ni_pi->clock_registers.mpll_dq_func_cntl);
	table->initialState.levels[0].mclk.vMPLL_DQ_FUNC_CNTL_2 =
		cpu_to_be32(ni_pi->clock_registers.mpll_dq_func_cntl_2);
	table->initialState.levels[0].mclk.vMCLK_PWRMGT_CNTL =
		cpu_to_be32(ni_pi->clock_registers.mclk_pwrmgt_cntl);
	table->initialState.levels[0].mclk.vDLL_CNTL =
		cpu_to_be32(ni_pi->clock_registers.dll_cntl);
	table->initialState.levels[0].mclk.vMPLL_SS =
		cpu_to_be32(ni_pi->clock_registers.mpll_ss1);
	table->initialState.levels[0].mclk.vMPLL_SS2 =
		cpu_to_be32(ni_pi->clock_registers.mpll_ss2);
	table->initialState.levels[0].mclk.mclk_value =
		cpu_to_be32(initial_state->performance_levels[0].mclk);

	table->initialState.levels[0].sclk.vCG_SPLL_FUNC_CNTL =
		cpu_to_be32(ni_pi->clock_registers.cg_spll_func_cntl);
	table->initialState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_2 =
		cpu_to_be32(ni_pi->clock_registers.cg_spll_func_cntl_2);
	table->initialState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_3 =
		cpu_to_be32(ni_pi->clock_registers.cg_spll_func_cntl_3);
	table->initialState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_4 =
		cpu_to_be32(ni_pi->clock_registers.cg_spll_func_cntl_4);
	table->initialState.levels[0].sclk.vCG_SPLL_SPREAD_SPECTRUM =
		cpu_to_be32(ni_pi->clock_registers.cg_spll_spread_spectrum);
	table->initialState.levels[0].sclk.vCG_SPLL_SPREAD_SPECTRUM_2 =
		cpu_to_be32(ni_pi->clock_registers.cg_spll_spread_spectrum_2);
	table->initialState.levels[0].sclk.sclk_value =
		cpu_to_be32(initial_state->performance_levels[0].sclk);
	table->initialState.levels[0].arbRefreshState =
		NISLANDS_INITIAL_STATE_ARB_INDEX;

	table->initialState.levels[0].ACIndex = 0;

	ret = ni_populate_voltage_value(rdev, &eg_pi->vddc_voltage_table,
					initial_state->performance_levels[0].vddc,
					&table->initialState.levels[0].vddc);
	if (!ret) {
		u16 std_vddc;

		ret = ni_get_std_voltage_value(rdev,
					       &table->initialState.levels[0].vddc,
					       &std_vddc);
		if (!ret)
			ni_populate_std_voltage_value(rdev, std_vddc,
						      table->initialState.levels[0].vddc.index,
						      &table->initialState.levels[0].std_vddc);
	}

	if (eg_pi->vddci_control)
		ni_populate_voltage_value(rdev,
					  &eg_pi->vddci_voltage_table,
					  initial_state->performance_levels[0].vddci,
					  &table->initialState.levels[0].vddci);

	ni_populate_initial_mvdd_value(rdev, &table->initialState.levels[0].mvdd);

	reg = CG_R(0xffff) | CG_L(0);
	table->initialState.levels[0].aT = cpu_to_be32(reg);

	table->initialState.levels[0].bSP = cpu_to_be32(pi->dsp);

	if (pi->boot_in_gen2)
		table->initialState.levels[0].gen2PCIE = 1;
	else
		table->initialState.levels[0].gen2PCIE = 0;

	if (pi->mem_gddr5) {
		table->initialState.levels[0].strobeMode =
			cypress_get_strobe_mode_settings(rdev,
							 initial_state->performance_levels[0].mclk);

		if (initial_state->performance_levels[0].mclk > pi->mclk_edc_enable_threshold)
			table->initialState.levels[0].mcFlags = NISLANDS_SMC_MC_EDC_RD_FLAG | NISLANDS_SMC_MC_EDC_WR_FLAG;
		else
			table->initialState.levels[0].mcFlags =  0;
	}

	table->initialState.levelCount = 1;

	table->initialState.flags |= PPSMC_SWSTATE_FLAG_DC;

	table->initialState.levels[0].dpm2.MaxPS = 0;
	table->initialState.levels[0].dpm2.NearTDPDec = 0;
	table->initialState.levels[0].dpm2.AboveSafeInc = 0;
	table->initialState.levels[0].dpm2.BelowSafeInc = 0;

	reg = MIN_POWER_MASK | MAX_POWER_MASK;
	table->initialState.levels[0].SQPowerThrottle = cpu_to_be32(reg);

	reg = MAX_POWER_DELTA_MASK | STI_SIZE_MASK | LTI_RATIO_MASK;
	table->initialState.levels[0].SQPowerThrottle_2 = cpu_to_be32(reg);

	return 0;
}

static int ni_populate_smc_acpi_state(struct radeon_device *rdev,
				      NISLANDS_SMC_STATETABLE *table)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	u32 mpll_ad_func_cntl   = ni_pi->clock_registers.mpll_ad_func_cntl;
	u32 mpll_ad_func_cntl_2 = ni_pi->clock_registers.mpll_ad_func_cntl_2;
	u32 mpll_dq_func_cntl   = ni_pi->clock_registers.mpll_dq_func_cntl;
	u32 mpll_dq_func_cntl_2 = ni_pi->clock_registers.mpll_dq_func_cntl_2;
	u32 spll_func_cntl      = ni_pi->clock_registers.cg_spll_func_cntl;
	u32 spll_func_cntl_2    = ni_pi->clock_registers.cg_spll_func_cntl_2;
	u32 spll_func_cntl_3    = ni_pi->clock_registers.cg_spll_func_cntl_3;
	u32 spll_func_cntl_4    = ni_pi->clock_registers.cg_spll_func_cntl_4;
	u32 mclk_pwrmgt_cntl    = ni_pi->clock_registers.mclk_pwrmgt_cntl;
	u32 dll_cntl            = ni_pi->clock_registers.dll_cntl;
	u32 reg;
	int ret;

	table->ACPIState = table->initialState;

	table->ACPIState.flags &= ~PPSMC_SWSTATE_FLAG_DC;

	if (pi->acpi_vddc) {
		ret = ni_populate_voltage_value(rdev,
						&eg_pi->vddc_voltage_table,
						pi->acpi_vddc, &table->ACPIState.levels[0].vddc);
		if (!ret) {
			u16 std_vddc;

			ret = ni_get_std_voltage_value(rdev,
						       &table->ACPIState.levels[0].vddc, &std_vddc);
			if (!ret)
				ni_populate_std_voltage_value(rdev, std_vddc,
							      table->ACPIState.levels[0].vddc.index,
							      &table->ACPIState.levels[0].std_vddc);
		}

		if (pi->pcie_gen2) {
			if (pi->acpi_pcie_gen2)
				table->ACPIState.levels[0].gen2PCIE = 1;
			else
				table->ACPIState.levels[0].gen2PCIE = 0;
		} else {
			table->ACPIState.levels[0].gen2PCIE = 0;
		}
	} else {
		ret = ni_populate_voltage_value(rdev,
						&eg_pi->vddc_voltage_table,
						pi->min_vddc_in_table,
						&table->ACPIState.levels[0].vddc);
		if (!ret) {
			u16 std_vddc;

			ret = ni_get_std_voltage_value(rdev,
						       &table->ACPIState.levels[0].vddc,
						       &std_vddc);
			if (!ret)
				ni_populate_std_voltage_value(rdev, std_vddc,
							      table->ACPIState.levels[0].vddc.index,
							      &table->ACPIState.levels[0].std_vddc);
		}
		table->ACPIState.levels[0].gen2PCIE = 0;
	}

	if (eg_pi->acpi_vddci) {
		if (eg_pi->vddci_control)
			ni_populate_voltage_value(rdev,
						  &eg_pi->vddci_voltage_table,
						  eg_pi->acpi_vddci,
						  &table->ACPIState.levels[0].vddci);
	}


	mpll_ad_func_cntl &= ~PDNB;

	mpll_ad_func_cntl_2 |= BIAS_GEN_PDNB | RESET_EN;

        if (pi->mem_gddr5)
                mpll_dq_func_cntl &= ~PDNB;
        mpll_dq_func_cntl_2 |= BIAS_GEN_PDNB | RESET_EN | BYPASS;


	mclk_pwrmgt_cntl |= (MRDCKA0_RESET |
			     MRDCKA1_RESET |
			     MRDCKB0_RESET |
			     MRDCKB1_RESET |
			     MRDCKC0_RESET |
			     MRDCKC1_RESET |
			     MRDCKD0_RESET |
			     MRDCKD1_RESET);

	mclk_pwrmgt_cntl &= ~(MRDCKA0_PDNB |
			      MRDCKA1_PDNB |
			      MRDCKB0_PDNB |
			      MRDCKB1_PDNB |
			      MRDCKC0_PDNB |
			      MRDCKC1_PDNB |
			      MRDCKD0_PDNB |
			      MRDCKD1_PDNB);

	dll_cntl |= (MRDCKA0_BYPASS |
                     MRDCKA1_BYPASS |
                     MRDCKB0_BYPASS |
                     MRDCKB1_BYPASS |
                     MRDCKC0_BYPASS |
                     MRDCKC1_BYPASS |
                     MRDCKD0_BYPASS |
                     MRDCKD1_BYPASS);

        spll_func_cntl_2 &= ~SCLK_MUX_SEL_MASK;
	spll_func_cntl_2 |= SCLK_MUX_SEL(4);

	table->ACPIState.levels[0].mclk.vMPLL_AD_FUNC_CNTL = cpu_to_be32(mpll_ad_func_cntl);
	table->ACPIState.levels[0].mclk.vMPLL_AD_FUNC_CNTL_2 = cpu_to_be32(mpll_ad_func_cntl_2);
	table->ACPIState.levels[0].mclk.vMPLL_DQ_FUNC_CNTL = cpu_to_be32(mpll_dq_func_cntl);
	table->ACPIState.levels[0].mclk.vMPLL_DQ_FUNC_CNTL_2 = cpu_to_be32(mpll_dq_func_cntl_2);
	table->ACPIState.levels[0].mclk.vMCLK_PWRMGT_CNTL = cpu_to_be32(mclk_pwrmgt_cntl);
	table->ACPIState.levels[0].mclk.vDLL_CNTL = cpu_to_be32(dll_cntl);

	table->ACPIState.levels[0].mclk.mclk_value = 0;

	table->ACPIState.levels[0].sclk.vCG_SPLL_FUNC_CNTL = cpu_to_be32(spll_func_cntl);
	table->ACPIState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_2 = cpu_to_be32(spll_func_cntl_2);
	table->ACPIState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_3 = cpu_to_be32(spll_func_cntl_3);
	table->ACPIState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_4 = cpu_to_be32(spll_func_cntl_4);

	table->ACPIState.levels[0].sclk.sclk_value = 0;

	ni_populate_mvdd_value(rdev, 0, &table->ACPIState.levels[0].mvdd);

	if (eg_pi->dynamic_ac_timing)
		table->ACPIState.levels[0].ACIndex = 1;

	table->ACPIState.levels[0].dpm2.MaxPS = 0;
	table->ACPIState.levels[0].dpm2.NearTDPDec = 0;
	table->ACPIState.levels[0].dpm2.AboveSafeInc = 0;
	table->ACPIState.levels[0].dpm2.BelowSafeInc = 0;

	reg = MIN_POWER_MASK | MAX_POWER_MASK;
	table->ACPIState.levels[0].SQPowerThrottle = cpu_to_be32(reg);

	reg = MAX_POWER_DELTA_MASK | STI_SIZE_MASK | LTI_RATIO_MASK;
	table->ACPIState.levels[0].SQPowerThrottle_2 = cpu_to_be32(reg);

	return 0;
}

static int ni_init_smc_table(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	int ret;
	struct radeon_ps *radeon_boot_state = rdev->pm.dpm.boot_ps;
	NISLANDS_SMC_STATETABLE *table = &ni_pi->smc_statetable;

	memset(table, 0, sizeof(NISLANDS_SMC_STATETABLE));

	ni_populate_smc_voltage_tables(rdev, table);

	switch (rdev->pm.int_thermal_type) {
	case THERMAL_TYPE_NI:
	case THERMAL_TYPE_EMC2103_WITH_INTERNAL:
		table->thermalProtectType = PPSMC_THERMAL_PROTECT_TYPE_INTERNAL;
		break;
	case THERMAL_TYPE_NONE:
		table->thermalProtectType = PPSMC_THERMAL_PROTECT_TYPE_NONE;
		break;
	default:
		table->thermalProtectType = PPSMC_THERMAL_PROTECT_TYPE_EXTERNAL;
		break;
	}

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_HARDWAREDC)
		table->systemFlags |= PPSMC_SYSTEMFLAG_GPIO_DC;

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_REGULATOR_HOT)
		table->systemFlags |= PPSMC_SYSTEMFLAG_REGULATOR_HOT;

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_STEPVDDC)
		table->systemFlags |= PPSMC_SYSTEMFLAG_STEPVDDC;

	if (pi->mem_gddr5)
		table->systemFlags |= PPSMC_SYSTEMFLAG_GDDR5;

	ret = ni_populate_smc_initial_state(rdev, radeon_boot_state, table);
	if (ret)
		return ret;

	ret = ni_populate_smc_acpi_state(rdev, table);
	if (ret)
		return ret;

	table->driverState = table->initialState;

	table->ULVState = table->initialState;

	ret = ni_do_program_memory_timing_parameters(rdev, radeon_boot_state,
						     NISLANDS_INITIAL_STATE_ARB_INDEX);
	if (ret)
		return ret;

	return rv770_copy_bytes_to_smc(rdev, pi->state_table_start, (u8 *)table,
				       sizeof(NISLANDS_SMC_STATETABLE), pi->sram_end);
}

static int ni_calculate_sclk_params(struct radeon_device *rdev,
				    u32 engine_clock,
				    NISLANDS_SMC_SCLK_VALUE *sclk)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	struct atom_clock_dividers dividers;
	u32 spll_func_cntl = ni_pi->clock_registers.cg_spll_func_cntl;
	u32 spll_func_cntl_2 = ni_pi->clock_registers.cg_spll_func_cntl_2;
	u32 spll_func_cntl_3 = ni_pi->clock_registers.cg_spll_func_cntl_3;
	u32 spll_func_cntl_4 = ni_pi->clock_registers.cg_spll_func_cntl_4;
	u32 cg_spll_spread_spectrum = ni_pi->clock_registers.cg_spll_spread_spectrum;
	u32 cg_spll_spread_spectrum_2 = ni_pi->clock_registers.cg_spll_spread_spectrum_2;
	u64 tmp;
	u32 reference_clock = rdev->clock.spll.reference_freq;
	u32 reference_divider;
	u32 fbdiv;
	int ret;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
					     engine_clock, false, &dividers);
	if (ret)
		return ret;

	reference_divider = 1 + dividers.ref_div;


	tmp = (u64) engine_clock * reference_divider * dividers.post_div * 16834;
	do_div(tmp, reference_clock);
	fbdiv = (u32) tmp;

	spll_func_cntl &= ~(SPLL_PDIV_A_MASK | SPLL_REF_DIV_MASK);
	spll_func_cntl |= SPLL_REF_DIV(dividers.ref_div);
	spll_func_cntl |= SPLL_PDIV_A(dividers.post_div);

	spll_func_cntl_2 &= ~SCLK_MUX_SEL_MASK;
	spll_func_cntl_2 |= SCLK_MUX_SEL(2);

	spll_func_cntl_3 &= ~SPLL_FB_DIV_MASK;
	spll_func_cntl_3 |= SPLL_FB_DIV(fbdiv);
	spll_func_cntl_3 |= SPLL_DITHEN;

	if (pi->sclk_ss) {
		struct radeon_atom_ss ss;
		u32 vco_freq = engine_clock * dividers.post_div;

		if (radeon_atombios_get_asic_ss_info(rdev, &ss,
						     ASIC_INTERNAL_ENGINE_SS, vco_freq)) {
			u32 clk_s = reference_clock * 5 / (reference_divider * ss.rate);
			u32 clk_v = 4 * ss.percentage * fbdiv / (clk_s * 10000);

			cg_spll_spread_spectrum &= ~CLK_S_MASK;
			cg_spll_spread_spectrum |= CLK_S(clk_s);
			cg_spll_spread_spectrum |= SSEN;

			cg_spll_spread_spectrum_2 &= ~CLK_V_MASK;
			cg_spll_spread_spectrum_2 |= CLK_V(clk_v);
		}
	}

	sclk->sclk_value = engine_clock;
	sclk->vCG_SPLL_FUNC_CNTL = spll_func_cntl;
	sclk->vCG_SPLL_FUNC_CNTL_2 = spll_func_cntl_2;
	sclk->vCG_SPLL_FUNC_CNTL_3 = spll_func_cntl_3;
	sclk->vCG_SPLL_FUNC_CNTL_4 = spll_func_cntl_4;
	sclk->vCG_SPLL_SPREAD_SPECTRUM = cg_spll_spread_spectrum;
	sclk->vCG_SPLL_SPREAD_SPECTRUM_2 = cg_spll_spread_spectrum_2;

	return 0;
}

static int ni_populate_sclk_value(struct radeon_device *rdev,
				  u32 engine_clock,
				  NISLANDS_SMC_SCLK_VALUE *sclk)
{
	NISLANDS_SMC_SCLK_VALUE sclk_tmp;
	int ret;

	ret = ni_calculate_sclk_params(rdev, engine_clock, &sclk_tmp);
	if (!ret) {
		sclk->sclk_value = cpu_to_be32(sclk_tmp.sclk_value);
		sclk->vCG_SPLL_FUNC_CNTL = cpu_to_be32(sclk_tmp.vCG_SPLL_FUNC_CNTL);
		sclk->vCG_SPLL_FUNC_CNTL_2 = cpu_to_be32(sclk_tmp.vCG_SPLL_FUNC_CNTL_2);
		sclk->vCG_SPLL_FUNC_CNTL_3 = cpu_to_be32(sclk_tmp.vCG_SPLL_FUNC_CNTL_3);
		sclk->vCG_SPLL_FUNC_CNTL_4 = cpu_to_be32(sclk_tmp.vCG_SPLL_FUNC_CNTL_4);
		sclk->vCG_SPLL_SPREAD_SPECTRUM = cpu_to_be32(sclk_tmp.vCG_SPLL_SPREAD_SPECTRUM);
		sclk->vCG_SPLL_SPREAD_SPECTRUM_2 = cpu_to_be32(sclk_tmp.vCG_SPLL_SPREAD_SPECTRUM_2);
	}

	return ret;
}

static int ni_init_smc_spll_table(struct radeon_device *rdev)
{
        struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	SMC_NISLANDS_SPLL_DIV_TABLE *spll_table;
	NISLANDS_SMC_SCLK_VALUE sclk_params;
	u32 fb_div;
	u32 p_div;
	u32 clk_s;
	u32 clk_v;
	u32 sclk = 0;
	int i, ret;
	u32 tmp;

	if (ni_pi->spll_table_start == 0)
		return -EINVAL;

	spll_table = kzalloc(sizeof(SMC_NISLANDS_SPLL_DIV_TABLE), GFP_KERNEL);
	if (spll_table == NULL)
		return -ENOMEM;

	for (i = 0; i < 256; i++) {
		ret = ni_calculate_sclk_params(rdev, sclk, &sclk_params);
		if (ret)
			break;

		p_div = (sclk_params.vCG_SPLL_FUNC_CNTL & SPLL_PDIV_A_MASK) >> SPLL_PDIV_A_SHIFT;
		fb_div = (sclk_params.vCG_SPLL_FUNC_CNTL_3 & SPLL_FB_DIV_MASK) >> SPLL_FB_DIV_SHIFT;
		clk_s = (sclk_params.vCG_SPLL_SPREAD_SPECTRUM & CLK_S_MASK) >> CLK_S_SHIFT;
		clk_v = (sclk_params.vCG_SPLL_SPREAD_SPECTRUM_2 & CLK_V_MASK) >> CLK_V_SHIFT;

		fb_div &= ~0x00001FFF;
		fb_div >>= 1;
		clk_v >>= 6;

		if (p_div & ~(SMC_NISLANDS_SPLL_DIV_TABLE_PDIV_MASK >> SMC_NISLANDS_SPLL_DIV_TABLE_PDIV_SHIFT))
			ret = -EINVAL;

		if (clk_s & ~(SMC_NISLANDS_SPLL_DIV_TABLE_CLKS_MASK >> SMC_NISLANDS_SPLL_DIV_TABLE_CLKS_SHIFT))
			ret = -EINVAL;

		if (clk_s & ~(SMC_NISLANDS_SPLL_DIV_TABLE_CLKS_MASK >> SMC_NISLANDS_SPLL_DIV_TABLE_CLKS_SHIFT))
			ret = -EINVAL;

		if (clk_v & ~(SMC_NISLANDS_SPLL_DIV_TABLE_CLKV_MASK >> SMC_NISLANDS_SPLL_DIV_TABLE_CLKV_SHIFT))
			ret = -EINVAL;

		if (ret)
			break;

		tmp = ((fb_div << SMC_NISLANDS_SPLL_DIV_TABLE_FBDIV_SHIFT) & SMC_NISLANDS_SPLL_DIV_TABLE_FBDIV_MASK) |
			((p_div << SMC_NISLANDS_SPLL_DIV_TABLE_PDIV_SHIFT) & SMC_NISLANDS_SPLL_DIV_TABLE_PDIV_MASK);
		spll_table->freq[i] = cpu_to_be32(tmp);

		tmp = ((clk_v << SMC_NISLANDS_SPLL_DIV_TABLE_CLKV_SHIFT) & SMC_NISLANDS_SPLL_DIV_TABLE_CLKV_MASK) |
			((clk_s << SMC_NISLANDS_SPLL_DIV_TABLE_CLKS_SHIFT) & SMC_NISLANDS_SPLL_DIV_TABLE_CLKS_MASK);
		spll_table->ss[i] = cpu_to_be32(tmp);

		sclk += 512;
	}

	if (!ret)
		ret = rv770_copy_bytes_to_smc(rdev, ni_pi->spll_table_start, (u8 *)spll_table,
					      sizeof(SMC_NISLANDS_SPLL_DIV_TABLE), pi->sram_end);

	kfree(spll_table);

	return ret;
}

static int ni_populate_mclk_value(struct radeon_device *rdev,
				  u32 engine_clock,
				  u32 memory_clock,
				  NISLANDS_SMC_MCLK_VALUE *mclk,
				  bool strobe_mode,
				  bool dll_state_on)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	u32 mpll_ad_func_cntl = ni_pi->clock_registers.mpll_ad_func_cntl;
	u32 mpll_ad_func_cntl_2 = ni_pi->clock_registers.mpll_ad_func_cntl_2;
	u32 mpll_dq_func_cntl = ni_pi->clock_registers.mpll_dq_func_cntl;
	u32 mpll_dq_func_cntl_2 = ni_pi->clock_registers.mpll_dq_func_cntl_2;
	u32 mclk_pwrmgt_cntl = ni_pi->clock_registers.mclk_pwrmgt_cntl;
	u32 dll_cntl = ni_pi->clock_registers.dll_cntl;
	u32 mpll_ss1 = ni_pi->clock_registers.mpll_ss1;
	u32 mpll_ss2 = ni_pi->clock_registers.mpll_ss2;
	struct atom_clock_dividers dividers;
	u32 ibias;
	u32 dll_speed;
	int ret;
	u32 mc_seq_misc7;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_MEMORY_PLL_PARAM,
					     memory_clock, strobe_mode, &dividers);
	if (ret)
		return ret;

	if (!strobe_mode) {
		mc_seq_misc7 = RREG32(MC_SEQ_MISC7);

		if (mc_seq_misc7 & 0x8000000)
			dividers.post_div = 1;
	}

	ibias = cypress_map_clkf_to_ibias(rdev, dividers.whole_fb_div);

	mpll_ad_func_cntl &= ~(CLKR_MASK |
			       YCLK_POST_DIV_MASK |
			       CLKF_MASK |
			       CLKFRAC_MASK |
			       IBIAS_MASK);
	mpll_ad_func_cntl |= CLKR(dividers.ref_div);
	mpll_ad_func_cntl |= YCLK_POST_DIV(dividers.post_div);
	mpll_ad_func_cntl |= CLKF(dividers.whole_fb_div);
	mpll_ad_func_cntl |= CLKFRAC(dividers.frac_fb_div);
	mpll_ad_func_cntl |= IBIAS(ibias);

	if (dividers.vco_mode)
		mpll_ad_func_cntl_2 |= VCO_MODE;
	else
		mpll_ad_func_cntl_2 &= ~VCO_MODE;

	if (pi->mem_gddr5) {
		mpll_dq_func_cntl &= ~(CLKR_MASK |
				       YCLK_POST_DIV_MASK |
				       CLKF_MASK |
				       CLKFRAC_MASK |
				       IBIAS_MASK);
		mpll_dq_func_cntl |= CLKR(dividers.ref_div);
		mpll_dq_func_cntl |= YCLK_POST_DIV(dividers.post_div);
		mpll_dq_func_cntl |= CLKF(dividers.whole_fb_div);
		mpll_dq_func_cntl |= CLKFRAC(dividers.frac_fb_div);
		mpll_dq_func_cntl |= IBIAS(ibias);

		if (strobe_mode)
			mpll_dq_func_cntl &= ~PDNB;
		else
			mpll_dq_func_cntl |= PDNB;

		if (dividers.vco_mode)
			mpll_dq_func_cntl_2 |= VCO_MODE;
		else
			mpll_dq_func_cntl_2 &= ~VCO_MODE;
	}

	if (pi->mclk_ss) {
		struct radeon_atom_ss ss;
		u32 vco_freq = memory_clock * dividers.post_div;

		if (radeon_atombios_get_asic_ss_info(rdev, &ss,
						     ASIC_INTERNAL_MEMORY_SS, vco_freq)) {
			u32 reference_clock = rdev->clock.mpll.reference_freq;
			u32 decoded_ref = rv740_get_decoded_reference_divider(dividers.ref_div);
			u32 clk_s = reference_clock * 5 / (decoded_ref * ss.rate);
			u32 clk_v = ss.percentage *
				(0x4000 * dividers.whole_fb_div + 0x800 * dividers.frac_fb_div) / (clk_s * 625);

			mpll_ss1 &= ~CLKV_MASK;
			mpll_ss1 |= CLKV(clk_v);

			mpll_ss2 &= ~CLKS_MASK;
			mpll_ss2 |= CLKS(clk_s);
		}
	}

	dll_speed = rv740_get_dll_speed(pi->mem_gddr5,
					memory_clock);

	mclk_pwrmgt_cntl &= ~DLL_SPEED_MASK;
	mclk_pwrmgt_cntl |= DLL_SPEED(dll_speed);
	if (dll_state_on)
		mclk_pwrmgt_cntl |= (MRDCKA0_PDNB |
				     MRDCKA1_PDNB |
				     MRDCKB0_PDNB |
				     MRDCKB1_PDNB |
				     MRDCKC0_PDNB |
				     MRDCKC1_PDNB |
				     MRDCKD0_PDNB |
				     MRDCKD1_PDNB);
	else
		mclk_pwrmgt_cntl &= ~(MRDCKA0_PDNB |
				      MRDCKA1_PDNB |
				      MRDCKB0_PDNB |
				      MRDCKB1_PDNB |
				      MRDCKC0_PDNB |
				      MRDCKC1_PDNB |
				      MRDCKD0_PDNB |
				      MRDCKD1_PDNB);


	mclk->mclk_value = cpu_to_be32(memory_clock);
	mclk->vMPLL_AD_FUNC_CNTL = cpu_to_be32(mpll_ad_func_cntl);
	mclk->vMPLL_AD_FUNC_CNTL_2 = cpu_to_be32(mpll_ad_func_cntl_2);
	mclk->vMPLL_DQ_FUNC_CNTL = cpu_to_be32(mpll_dq_func_cntl);
	mclk->vMPLL_DQ_FUNC_CNTL_2 = cpu_to_be32(mpll_dq_func_cntl_2);
	mclk->vMCLK_PWRMGT_CNTL = cpu_to_be32(mclk_pwrmgt_cntl);
	mclk->vDLL_CNTL = cpu_to_be32(dll_cntl);
	mclk->vMPLL_SS = cpu_to_be32(mpll_ss1);
	mclk->vMPLL_SS2 = cpu_to_be32(mpll_ss2);

	return 0;
}

static void ni_populate_smc_sp(struct radeon_device *rdev,
			       struct radeon_ps *radeon_state,
			       NISLANDS_SMC_SWSTATE *smc_state)
{
	struct ni_ps *ps = ni_get_ps(radeon_state);
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	int i;

	for (i = 0; i < ps->performance_level_count - 1; i++)
		smc_state->levels[i].bSP = cpu_to_be32(pi->dsp);

	smc_state->levels[ps->performance_level_count - 1].bSP =
		cpu_to_be32(pi->psp);
}

static int ni_convert_power_level_to_smc(struct radeon_device *rdev,
					 struct rv7xx_pl *pl,
					 NISLANDS_SMC_HW_PERFORMANCE_LEVEL *level)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
        struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
        struct ni_power_info *ni_pi = ni_get_pi(rdev);
	int ret;
	bool dll_state_on;
	u16 std_vddc;
	u32 tmp = RREG32(DC_STUTTER_CNTL);

	level->gen2PCIE = pi->pcie_gen2 ?
		((pl->flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2) ? 1 : 0) : 0;

	ret = ni_populate_sclk_value(rdev, pl->sclk, &level->sclk);
	if (ret)
		return ret;

	level->mcFlags =  0;
	if (pi->mclk_stutter_mode_threshold &&
	    (pl->mclk <= pi->mclk_stutter_mode_threshold) &&
	    !eg_pi->uvd_enabled &&
	    (tmp & DC_STUTTER_ENABLE_A) &&
	    (tmp & DC_STUTTER_ENABLE_B))
		level->mcFlags |= NISLANDS_SMC_MC_STUTTER_EN;

	if (pi->mem_gddr5) {
		if (pl->mclk > pi->mclk_edc_enable_threshold)
			level->mcFlags |= NISLANDS_SMC_MC_EDC_RD_FLAG;
		if (pl->mclk > eg_pi->mclk_edc_wr_enable_threshold)
			level->mcFlags |= NISLANDS_SMC_MC_EDC_WR_FLAG;

		level->strobeMode = cypress_get_strobe_mode_settings(rdev, pl->mclk);

		if (level->strobeMode & NISLANDS_SMC_STROBE_ENABLE) {
			if (cypress_get_mclk_frequency_ratio(rdev, pl->mclk, true) >=
			    ((RREG32(MC_SEQ_MISC7) >> 16) & 0xf))
				dll_state_on = ((RREG32(MC_SEQ_MISC5) >> 1) & 0x1) ? true : false;
			else
				dll_state_on = ((RREG32(MC_SEQ_MISC6) >> 1) & 0x1) ? true : false;
		} else {
			dll_state_on = false;
			if (pl->mclk > ni_pi->mclk_rtt_mode_threshold)
				level->mcFlags |= NISLANDS_SMC_MC_RTT_ENABLE;
		}

		ret = ni_populate_mclk_value(rdev, pl->sclk, pl->mclk,
					     &level->mclk,
					     (level->strobeMode & NISLANDS_SMC_STROBE_ENABLE) != 0,
					     dll_state_on);
	} else
		ret = ni_populate_mclk_value(rdev, pl->sclk, pl->mclk, &level->mclk, 1, 1);

	if (ret)
		return ret;

	ret = ni_populate_voltage_value(rdev, &eg_pi->vddc_voltage_table,
					pl->vddc, &level->vddc);
	if (ret)
		return ret;

	ret = ni_get_std_voltage_value(rdev, &level->vddc, &std_vddc);
	if (ret)
		return ret;

	ni_populate_std_voltage_value(rdev, std_vddc,
				      level->vddc.index, &level->std_vddc);

	if (eg_pi->vddci_control) {
		ret = ni_populate_voltage_value(rdev, &eg_pi->vddci_voltage_table,
						pl->vddci, &level->vddci);
		if (ret)
			return ret;
	}

	ni_populate_mvdd_value(rdev, pl->mclk, &level->mvdd);

	return ret;
}

static int ni_populate_smc_t(struct radeon_device *rdev,
			     struct radeon_ps *radeon_state,
			     NISLANDS_SMC_SWSTATE *smc_state)
{
        struct rv7xx_power_info *pi = rv770_get_pi(rdev);
        struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct ni_ps *state = ni_get_ps(radeon_state);
	u32 a_t;
	u32 t_l, t_h;
	u32 high_bsp;
	int i, ret;

	if (state->performance_level_count >= 9)
		return -EINVAL;

	if (state->performance_level_count < 2) {
		a_t = CG_R(0xffff) | CG_L(0);
		smc_state->levels[0].aT = cpu_to_be32(a_t);
		return 0;
	}

	smc_state->levels[0].aT = cpu_to_be32(0);

	for (i = 0; i <= state->performance_level_count - 2; i++) {
		if (eg_pi->uvd_enabled)
			ret = r600_calculate_at(
				1000 * (i * (eg_pi->smu_uvd_hs ? 2 : 8) + 2),
				100 * R600_AH_DFLT,
				state->performance_levels[i + 1].sclk,
				state->performance_levels[i].sclk,
				&t_l,
				&t_h);
		else
			ret = r600_calculate_at(
				1000 * (i + 1),
				100 * R600_AH_DFLT,
				state->performance_levels[i + 1].sclk,
				state->performance_levels[i].sclk,
				&t_l,
				&t_h);

		if (ret) {
			t_h = (i + 1) * 1000 - 50 * R600_AH_DFLT;
			t_l = (i + 1) * 1000 + 50 * R600_AH_DFLT;
		}

		a_t = be32_to_cpu(smc_state->levels[i].aT) & ~CG_R_MASK;
		a_t |= CG_R(t_l * pi->bsp / 20000);
		smc_state->levels[i].aT = cpu_to_be32(a_t);

		high_bsp = (i == state->performance_level_count - 2) ?
			pi->pbsp : pi->bsp;

		a_t = CG_R(0xffff) | CG_L(t_h * high_bsp / 20000);
		smc_state->levels[i + 1].aT = cpu_to_be32(a_t);
	}

	return 0;
}

static int ni_populate_power_containment_values(struct radeon_device *rdev,
						struct radeon_ps *radeon_state,
						NISLANDS_SMC_SWSTATE *smc_state)
{
        struct rv7xx_power_info *pi = rv770_get_pi(rdev);
        struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	struct ni_ps *state = ni_get_ps(radeon_state);
	u32 prev_sclk;
	u32 max_sclk;
	u32 min_sclk;
	int i, ret;
	u32 tdp_limit;
	u32 near_tdp_limit;
	u32 power_boost_limit;
	u8 max_ps_percent;

	if (ni_pi->enable_power_containment == false)
		return 0;

	if (state->performance_level_count == 0)
		return -EINVAL;

	if (smc_state->levelCount != state->performance_level_count)
		return -EINVAL;

	ret = ni_calculate_adjusted_tdp_limits(rdev,
					       false, /* ??? */
					       rdev->pm.dpm.tdp_adjustment,
					       &tdp_limit,
					       &near_tdp_limit);
	if (ret)
		return ret;

	power_boost_limit = ni_calculate_power_boost_limit(rdev, radeon_state, near_tdp_limit);

	ret = rv770_write_smc_sram_dword(rdev,
					 pi->state_table_start +
					 offsetof(NISLANDS_SMC_STATETABLE, dpm2Params) +
					 offsetof(PP_NIslands_DPM2Parameters, PowerBoostLimit),
					 ni_scale_power_for_smc(power_boost_limit, ni_get_smc_power_scaling_factor(rdev)),
					 pi->sram_end);
	if (ret)
		power_boost_limit = 0;

	smc_state->levels[0].dpm2.MaxPS = 0;
	smc_state->levels[0].dpm2.NearTDPDec = 0;
	smc_state->levels[0].dpm2.AboveSafeInc = 0;
	smc_state->levels[0].dpm2.BelowSafeInc = 0;
	smc_state->levels[0].stateFlags |= power_boost_limit ? PPSMC_STATEFLAG_POWERBOOST : 0;

	for (i = 1; i < state->performance_level_count; i++) {
		prev_sclk = state->performance_levels[i-1].sclk;
		max_sclk  = state->performance_levels[i].sclk;
		max_ps_percent = (i != (state->performance_level_count - 1)) ?
			NISLANDS_DPM2_MAXPS_PERCENT_M : NISLANDS_DPM2_MAXPS_PERCENT_H;

		if (max_sclk < prev_sclk)
			return -EINVAL;

		if ((max_ps_percent == 0) || (prev_sclk == max_sclk) || eg_pi->uvd_enabled)
			min_sclk = max_sclk;
		else if (1 == i)
			min_sclk = prev_sclk;
		else
			min_sclk = (prev_sclk * (u32)max_ps_percent) / 100;

		if (min_sclk < state->performance_levels[0].sclk)
			min_sclk = state->performance_levels[0].sclk;

		if (min_sclk == 0)
			return -EINVAL;

		smc_state->levels[i].dpm2.MaxPS =
			(u8)((NISLANDS_DPM2_MAX_PULSE_SKIP * (max_sclk - min_sclk)) / max_sclk);
		smc_state->levels[i].dpm2.NearTDPDec = NISLANDS_DPM2_NEAR_TDP_DEC;
		smc_state->levels[i].dpm2.AboveSafeInc = NISLANDS_DPM2_ABOVE_SAFE_INC;
		smc_state->levels[i].dpm2.BelowSafeInc = NISLANDS_DPM2_BELOW_SAFE_INC;
		smc_state->levels[i].stateFlags |=
			((i != (state->performance_level_count - 1)) && power_boost_limit) ?
			PPSMC_STATEFLAG_POWERBOOST : 0;
	}

	return 0;
}

static int ni_populate_sq_ramping_values(struct radeon_device *rdev,
					 struct radeon_ps *radeon_state,
					 NISLANDS_SMC_SWSTATE *smc_state)
{
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	struct ni_ps *state = ni_get_ps(radeon_state);
	u32 sq_power_throttle;
	u32 sq_power_throttle2;
	bool enable_sq_ramping = ni_pi->enable_sq_ramping;
	int i;

	if (state->performance_level_count == 0)
		return -EINVAL;

	if (smc_state->levelCount != state->performance_level_count)
		return -EINVAL;

	if (rdev->pm.dpm.sq_ramping_threshold == 0)
		return -EINVAL;

	if (NISLANDS_DPM2_SQ_RAMP_MAX_POWER > (MAX_POWER_MASK >> MAX_POWER_SHIFT))
		enable_sq_ramping = false;

	if (NISLANDS_DPM2_SQ_RAMP_MIN_POWER > (MIN_POWER_MASK >> MIN_POWER_SHIFT))
		enable_sq_ramping = false;

	if (NISLANDS_DPM2_SQ_RAMP_MAX_POWER_DELTA > (MAX_POWER_DELTA_MASK >> MAX_POWER_DELTA_SHIFT))
		enable_sq_ramping = false;

	if (NISLANDS_DPM2_SQ_RAMP_STI_SIZE > (STI_SIZE_MASK >> STI_SIZE_SHIFT))
		enable_sq_ramping = false;

	if (NISLANDS_DPM2_SQ_RAMP_LTI_RATIO <= (LTI_RATIO_MASK >> LTI_RATIO_SHIFT))
		enable_sq_ramping = false;

	for (i = 0; i < state->performance_level_count; i++) {
		sq_power_throttle  = 0;
		sq_power_throttle2 = 0;

		if ((state->performance_levels[i].sclk >= rdev->pm.dpm.sq_ramping_threshold) &&
		    enable_sq_ramping) {
			sq_power_throttle |= MAX_POWER(NISLANDS_DPM2_SQ_RAMP_MAX_POWER);
			sq_power_throttle |= MIN_POWER(NISLANDS_DPM2_SQ_RAMP_MIN_POWER);
			sq_power_throttle2 |= MAX_POWER_DELTA(NISLANDS_DPM2_SQ_RAMP_MAX_POWER_DELTA);
			sq_power_throttle2 |= STI_SIZE(NISLANDS_DPM2_SQ_RAMP_STI_SIZE);
			sq_power_throttle2 |= LTI_RATIO(NISLANDS_DPM2_SQ_RAMP_LTI_RATIO);
		} else {
			sq_power_throttle |= MAX_POWER_MASK | MIN_POWER_MASK;
			sq_power_throttle2 |= MAX_POWER_DELTA_MASK | STI_SIZE_MASK | LTI_RATIO_MASK;
		}

		smc_state->levels[i].SQPowerThrottle   = cpu_to_be32(sq_power_throttle);
		smc_state->levels[i].SQPowerThrottle_2 = cpu_to_be32(sq_power_throttle2);
	}

	return 0;
}

static int ni_enable_power_containment(struct radeon_device *rdev, bool enable)
{
        struct ni_power_info *ni_pi = ni_get_pi(rdev);
	PPSMC_Result smc_result;
	int ret = 0;

	if (ni_pi->enable_power_containment) {
		if (enable) {
			struct radeon_ps *radeon_new_state = rdev->pm.dpm.requested_ps;

			if (!r600_is_uvd_state(radeon_new_state->class, radeon_new_state->class2)) {
				smc_result = rv770_send_msg_to_smc(rdev, PPSMC_TDPClampingActive);
				if (smc_result != PPSMC_Result_OK) {
					ret = -EINVAL;
					ni_pi->pc_enabled = false;
				} else {
					ni_pi->pc_enabled = true;
				}
			}
		} else {
			smc_result = rv770_send_msg_to_smc(rdev, PPSMC_TDPClampingInactive);
			if (smc_result != PPSMC_Result_OK)
				ret = -EINVAL;
			ni_pi->pc_enabled = false;
		}
	}

	return ret;
}

static int ni_convert_power_state_to_smc(struct radeon_device *rdev,
					 struct radeon_ps *radeon_state,
					 NISLANDS_SMC_SWSTATE *smc_state)
{
        struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	struct ni_ps *state = ni_get_ps(radeon_state);
	int i, ret;
	u32 threshold = state->performance_levels[state->performance_level_count - 1].sclk * 100 / 100;

	if (!(radeon_state->caps & ATOM_PPLIB_DISALLOW_ON_DC))
		smc_state->flags |= PPSMC_SWSTATE_FLAG_DC;

	smc_state->levelCount = 0;

	if (state->performance_level_count > NISLANDS_MAX_SMC_PERFORMANCE_LEVELS_PER_SWSTATE)
		return -EINVAL;

	for (i = 0; i < state->performance_level_count; i++) {
		ret = ni_convert_power_level_to_smc(rdev, &state->performance_levels[i],
						    &smc_state->levels[i]);
		smc_state->levels[i].arbRefreshState =
			(u8)(NISLANDS_DRIVER_STATE_ARB_INDEX + i);

		if (ret)
			return ret;

		if (ni_pi->enable_power_containment)
			smc_state->levels[i].displayWatermark =
				(state->performance_levels[i].sclk < threshold) ?
				PPSMC_DISPLAY_WATERMARK_LOW : PPSMC_DISPLAY_WATERMARK_HIGH;
		else
			smc_state->levels[i].displayWatermark = (i < 2) ?
				PPSMC_DISPLAY_WATERMARK_LOW : PPSMC_DISPLAY_WATERMARK_HIGH;

		if (eg_pi->dynamic_ac_timing)
			smc_state->levels[i].ACIndex = NISLANDS_MCREGISTERTABLE_FIRST_DRIVERSTATE_SLOT + i;
		else
			smc_state->levels[i].ACIndex = 0;

		smc_state->levelCount++;
	}

	rv770_write_smc_soft_register(rdev, NI_SMC_SOFT_REGISTER_watermark_threshold,
				      cpu_to_be32(threshold / 512));

	ni_populate_smc_sp(rdev, radeon_state, smc_state);

	ret = ni_populate_power_containment_values(rdev, radeon_state, smc_state);
	if (ret)
		ni_pi->enable_power_containment = false;

	ret = ni_populate_sq_ramping_values(rdev, radeon_state, smc_state);
	if (ret)
		ni_pi->enable_sq_ramping = false;

	return ni_populate_smc_t(rdev, radeon_state, smc_state);
}

static int ni_upload_sw_state(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct radeon_ps *radeon_new_state = rdev->pm.dpm.requested_ps;
	u16 address = pi->state_table_start +
		offsetof(NISLANDS_SMC_STATETABLE, driverState);
	u16 state_size = sizeof(NISLANDS_SMC_SWSTATE) +
		((NISLANDS_MAX_SMC_PERFORMANCE_LEVELS_PER_SWSTATE - 1) * sizeof(NISLANDS_SMC_HW_PERFORMANCE_LEVEL));
	int ret;
	NISLANDS_SMC_SWSTATE *smc_state = kzalloc(state_size, GFP_KERNEL);

	if (smc_state == NULL)
		return -ENOMEM;

	ret = ni_convert_power_state_to_smc(rdev, radeon_new_state, smc_state);
	if (ret)
		goto done;

	ret = rv770_copy_bytes_to_smc(rdev, address, (u8 *)smc_state, state_size, pi->sram_end);

done:
	kfree(smc_state);

	return ret;
}

static int ni_set_mc_special_registers(struct radeon_device *rdev,
				       struct ni_mc_reg_table *table)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u8 i, j, k;
	u32 temp_reg;

	for (i = 0, j = table->last; i < table->last; i++) {
		switch (table->mc_reg_address[i].s1) {
		case MC_SEQ_MISC1 >> 2:
			if (j >= SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE)
				return -EINVAL;
			temp_reg = RREG32(MC_PMG_CMD_EMRS);
			table->mc_reg_address[j].s1 = MC_PMG_CMD_EMRS >> 2;
			table->mc_reg_address[j].s0 = MC_SEQ_PMG_CMD_EMRS_LP >> 2;
			for (k = 0; k < table->num_entries; k++)
				table->mc_reg_table_entry[k].mc_data[j] =
					((temp_reg & 0xffff0000)) |
					((table->mc_reg_table_entry[k].mc_data[i] & 0xffff0000) >> 16);
			j++;
			if (j >= SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE)
				return -EINVAL;

			temp_reg = RREG32(MC_PMG_CMD_MRS);
			table->mc_reg_address[j].s1 = MC_PMG_CMD_MRS >> 2;
			table->mc_reg_address[j].s0 = MC_SEQ_PMG_CMD_MRS_LP >> 2;
			for(k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					(temp_reg & 0xffff0000) |
					(table->mc_reg_table_entry[k].mc_data[i] & 0x0000ffff);
				if (!pi->mem_gddr5)
					table->mc_reg_table_entry[k].mc_data[j] |= 0x100;
			}
			j++;
			if (j > SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE)
				return -EINVAL;
			break;
		case MC_SEQ_RESERVE_M >> 2:
			temp_reg = RREG32(MC_PMG_CMD_MRS1);
			table->mc_reg_address[j].s1 = MC_PMG_CMD_MRS1 >> 2;
			table->mc_reg_address[j].s0 = MC_SEQ_PMG_CMD_MRS1_LP >> 2;
			for (k = 0; k < table->num_entries; k++)
				table->mc_reg_table_entry[k].mc_data[j] =
					(temp_reg & 0xffff0000) |
					(table->mc_reg_table_entry[k].mc_data[i] & 0x0000ffff);
			j++;
			if (j > SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE)
				return -EINVAL;
			break;
		default:
			break;
		}
	}

	table->last = j;

	return 0;
}

static bool ni_check_s0_mc_reg_index(u16 in_reg, u16 *out_reg)
{
	bool result = true;

	switch (in_reg) {
        case  MC_SEQ_RAS_TIMING >> 2:
		*out_reg = MC_SEQ_RAS_TIMING_LP >> 2;
		break;
        case MC_SEQ_CAS_TIMING >> 2:
		*out_reg = MC_SEQ_CAS_TIMING_LP >> 2;
		break;
        case MC_SEQ_MISC_TIMING >> 2:
		*out_reg = MC_SEQ_MISC_TIMING_LP >> 2;
		break;
        case MC_SEQ_MISC_TIMING2 >> 2:
		*out_reg = MC_SEQ_MISC_TIMING2_LP >> 2;
		break;
        case MC_SEQ_RD_CTL_D0 >> 2:
		*out_reg = MC_SEQ_RD_CTL_D0_LP >> 2;
		break;
        case MC_SEQ_RD_CTL_D1 >> 2:
		*out_reg = MC_SEQ_RD_CTL_D1_LP >> 2;
		break;
        case MC_SEQ_WR_CTL_D0 >> 2:
		*out_reg = MC_SEQ_WR_CTL_D0_LP >> 2;
		break;
        case MC_SEQ_WR_CTL_D1 >> 2:
		*out_reg = MC_SEQ_WR_CTL_D1_LP >> 2;
		break;
        case MC_PMG_CMD_EMRS >> 2:
		*out_reg = MC_SEQ_PMG_CMD_EMRS_LP >> 2;
		break;
        case MC_PMG_CMD_MRS >> 2:
		*out_reg = MC_SEQ_PMG_CMD_MRS_LP >> 2;
		break;
        case MC_PMG_CMD_MRS1 >> 2:
		*out_reg = MC_SEQ_PMG_CMD_MRS1_LP >> 2;
		break;
        case MC_SEQ_PMG_TIMING >> 2:
		*out_reg = MC_SEQ_PMG_TIMING_LP >> 2;
		break;
        case MC_PMG_CMD_MRS2 >> 2:
		*out_reg = MC_SEQ_PMG_CMD_MRS2_LP >> 2;
		break;
        default:
		result = false;
		break;
	}

	return result;
}

static void ni_set_valid_flag(struct ni_mc_reg_table *table)
{
	u8 i, j;

	for (i = 0; i < table->last; i++) {
		for (j = 1; j < table->num_entries; j++) {
			if (table->mc_reg_table_entry[j-1].mc_data[i] != table->mc_reg_table_entry[j].mc_data[i]) {
				table->valid_flag |= 1 << i;
				break;
			}
		}
	}
}

static void ni_set_s0_mc_reg_index(struct ni_mc_reg_table *table)
{
	u32 i;
	u16 address;

	for (i = 0; i < table->last; i++)
		table->mc_reg_address[i].s0 =
			ni_check_s0_mc_reg_index(table->mc_reg_address[i].s1, &address) ?
			address : table->mc_reg_address[i].s1;
}

static int ni_copy_vbios_mc_reg_table(struct atom_mc_reg_table *table,
				      struct ni_mc_reg_table *ni_table)
{
	u8 i, j;

	if (table->last > SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE)
		return -EINVAL;
	if (table->num_entries > MAX_AC_TIMING_ENTRIES)
		return -EINVAL;

	for (i = 0; i < table->last; i++)
		ni_table->mc_reg_address[i].s1 = table->mc_reg_address[i].s1;
	ni_table->last = table->last;

	for (i = 0; i < table->num_entries; i++) {
		ni_table->mc_reg_table_entry[i].mclk_max =
			table->mc_reg_table_entry[i].mclk_max;
		for (j = 0; j < table->last; j++)
			ni_table->mc_reg_table_entry[i].mc_data[j] =
				table->mc_reg_table_entry[i].mc_data[j];
	}
	ni_table->num_entries = table->num_entries;

	return 0;
}

static int ni_initialize_mc_reg_table(struct radeon_device *rdev)
{
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	int ret;
	struct atom_mc_reg_table *table;
	struct ni_mc_reg_table *ni_table = &ni_pi->mc_reg_table;
	u8 module_index = rv770_get_memory_module_index(rdev);

        table = kzalloc(sizeof(struct atom_mc_reg_table), GFP_KERNEL);
        if (!table)
                return -ENOMEM;

	WREG32(MC_SEQ_RAS_TIMING_LP, RREG32(MC_SEQ_RAS_TIMING));
	WREG32(MC_SEQ_CAS_TIMING_LP, RREG32(MC_SEQ_CAS_TIMING));
	WREG32(MC_SEQ_MISC_TIMING_LP, RREG32(MC_SEQ_MISC_TIMING));
	WREG32(MC_SEQ_MISC_TIMING2_LP, RREG32(MC_SEQ_MISC_TIMING2));
	WREG32(MC_SEQ_PMG_CMD_EMRS_LP, RREG32(MC_PMG_CMD_EMRS));
	WREG32(MC_SEQ_PMG_CMD_MRS_LP, RREG32(MC_PMG_CMD_MRS));
	WREG32(MC_SEQ_PMG_CMD_MRS1_LP, RREG32(MC_PMG_CMD_MRS1));
	WREG32(MC_SEQ_WR_CTL_D0_LP, RREG32(MC_SEQ_WR_CTL_D0));
	WREG32(MC_SEQ_WR_CTL_D1_LP, RREG32(MC_SEQ_WR_CTL_D1));
	WREG32(MC_SEQ_RD_CTL_D0_LP, RREG32(MC_SEQ_RD_CTL_D0));
	WREG32(MC_SEQ_RD_CTL_D1_LP, RREG32(MC_SEQ_RD_CTL_D1));
	WREG32(MC_SEQ_PMG_TIMING_LP, RREG32(MC_SEQ_PMG_TIMING));
	WREG32(MC_SEQ_PMG_CMD_MRS2_LP, RREG32(MC_PMG_CMD_MRS2));

	ret = radeon_atom_init_mc_reg_table(rdev, module_index, table);

        if (ret)
                goto init_mc_done;

	ret = ni_copy_vbios_mc_reg_table(table, ni_table);

        if (ret)
                goto init_mc_done;

	ni_set_s0_mc_reg_index(ni_table);

	ret = ni_set_mc_special_registers(rdev, ni_table);

        if (ret)
                goto init_mc_done;

	ni_set_valid_flag(ni_table);

init_mc_done:
        kfree(table);

	return ret;
}

static void ni_populate_mc_reg_addresses(struct radeon_device *rdev,
					 SMC_NIslands_MCRegisters *mc_reg_table)
{
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	u32 i, j;

	for (i = 0, j = 0; j < ni_pi->mc_reg_table.last; j++) {
		if (ni_pi->mc_reg_table.valid_flag & (1 << j)) {
			if (i >= SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE)
				break;
			mc_reg_table->address[i].s0 =
				cpu_to_be16(ni_pi->mc_reg_table.mc_reg_address[j].s0);
			mc_reg_table->address[i].s1 =
				cpu_to_be16(ni_pi->mc_reg_table.mc_reg_address[j].s1);
			i++;
		}
	}
	mc_reg_table->last = (u8)i;
}


static void ni_convert_mc_registers(struct ni_mc_reg_entry *entry,
				    SMC_NIslands_MCRegisterSet *data,
				    u32 num_entries, u32 valid_flag)
{
	u32 i, j;

	for (i = 0, j = 0; j < num_entries; j++) {
		if (valid_flag & (1 << j)) {
			data->value[i] = cpu_to_be32(entry->mc_data[j]);
			i++;
		}
	}
}

static void ni_convert_mc_reg_table_entry_to_smc(struct radeon_device *rdev,
						 struct rv7xx_pl *pl,
						 SMC_NIslands_MCRegisterSet *mc_reg_table_data)
{
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	u32 i = 0;

	for (i = 0; i < ni_pi->mc_reg_table.num_entries; i++) {
		if (pl->mclk <= ni_pi->mc_reg_table.mc_reg_table_entry[i].mclk_max)
			break;
	}

	if ((i == ni_pi->mc_reg_table.num_entries) && (i > 0))
		--i;

	ni_convert_mc_registers(&ni_pi->mc_reg_table.mc_reg_table_entry[i],
				mc_reg_table_data,
				ni_pi->mc_reg_table.last,
				ni_pi->mc_reg_table.valid_flag);
}

static void ni_convert_mc_reg_table_to_smc(struct radeon_device *rdev,
					   struct radeon_ps *radeon_state,
					   SMC_NIslands_MCRegisters *mc_reg_table)
{
	struct ni_ps *state = ni_get_ps(radeon_state);
	int i;

	for (i = 0; i < state->performance_level_count; i++) {
		ni_convert_mc_reg_table_entry_to_smc(rdev,
						     &state->performance_levels[i],
						     &mc_reg_table->data[NISLANDS_MCREGISTERTABLE_FIRST_DRIVERSTATE_SLOT + i]);
	}
}

static int ni_populate_mc_reg_table(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
        struct ni_power_info *ni_pi = ni_get_pi(rdev);
        struct radeon_ps *radeon_boot_state = rdev->pm.dpm.boot_ps;
	struct ni_ps *boot_state = ni_get_ps(radeon_boot_state);
	SMC_NIslands_MCRegisters *mc_reg_table = &ni_pi->smc_mc_reg_table;

	memset(mc_reg_table, 0, sizeof(SMC_NIslands_MCRegisters));

	rv770_write_smc_soft_register(rdev, NI_SMC_SOFT_REGISTER_seq_index, 1);

	ni_populate_mc_reg_addresses(rdev, mc_reg_table);

	ni_convert_mc_reg_table_entry_to_smc(rdev, &boot_state->performance_levels[0],
					     &mc_reg_table->data[0]);

	ni_convert_mc_registers(&ni_pi->mc_reg_table.mc_reg_table_entry[0],
				&mc_reg_table->data[1],
				ni_pi->mc_reg_table.last,
				ni_pi->mc_reg_table.valid_flag);

	ni_convert_mc_reg_table_to_smc(rdev, radeon_boot_state, mc_reg_table);

	return rv770_copy_bytes_to_smc(rdev, eg_pi->mc_reg_table_start,
				       (u8 *)mc_reg_table,
				       sizeof(SMC_NIslands_MCRegisters),
				       pi->sram_end);
}

static int ni_upload_mc_reg_table(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
        struct ni_power_info *ni_pi = ni_get_pi(rdev);
	struct radeon_ps *radeon_new_state = rdev->pm.dpm.requested_ps;
	struct ni_ps *ni_new_state = ni_get_ps(radeon_new_state);
	SMC_NIslands_MCRegisters *mc_reg_table = &ni_pi->smc_mc_reg_table;
	u16 address;

	memset(mc_reg_table, 0, sizeof(SMC_NIslands_MCRegisters));

	ni_convert_mc_reg_table_to_smc(rdev, radeon_new_state, mc_reg_table);

	address = eg_pi->mc_reg_table_start +
		(u16)offsetof(SMC_NIslands_MCRegisters, data[NISLANDS_MCREGISTERTABLE_FIRST_DRIVERSTATE_SLOT]);

	return rv770_copy_bytes_to_smc(rdev, address,
				       (u8 *)&mc_reg_table->data[NISLANDS_MCREGISTERTABLE_FIRST_DRIVERSTATE_SLOT],
				       sizeof(SMC_NIslands_MCRegisterSet) * ni_new_state->performance_level_count,
				       pi->sram_end);
}

static int ni_init_driver_calculated_leakage_table(struct radeon_device *rdev,
						   PP_NIslands_CACTABLES *cac_tables)
{
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	u32 leakage = 0;
	unsigned int i, j, table_size;
	s32 t;
	u32 smc_leakage, max_leakage = 0;
	u32 scaling_factor;

	table_size = eg_pi->vddc_voltage_table.count;

	if (SMC_NISLANDS_LKGE_LUT_NUM_OF_VOLT_ENTRIES < table_size)
		table_size = SMC_NISLANDS_LKGE_LUT_NUM_OF_VOLT_ENTRIES;

	scaling_factor = ni_get_smc_power_scaling_factor(rdev);

	for (i = 0; i < SMC_NISLANDS_LKGE_LUT_NUM_OF_TEMP_ENTRIES; i++) {
		for (j = 0; j < table_size; j++) {
			t = (1000 * ((i + 1) * 8));

			if (t < ni_pi->cac_data.leakage_minimum_temperature)
				t = ni_pi->cac_data.leakage_minimum_temperature;

			ni_calculate_leakage_for_v_and_t(rdev,
							 &ni_pi->cac_data.leakage_coefficients,
							 eg_pi->vddc_voltage_table.entries[j].value,
							 t,
							 ni_pi->cac_data.i_leakage,
							 &leakage);

			smc_leakage = ni_scale_power_for_smc(leakage, scaling_factor) / 1000;
			if (smc_leakage > max_leakage)
				max_leakage = smc_leakage;

			cac_tables->cac_lkge_lut[i][j] = cpu_to_be32(smc_leakage);
		}
	}

	for (j = table_size; j < SMC_NISLANDS_LKGE_LUT_NUM_OF_VOLT_ENTRIES; j++) {
		for (i = 0; i < SMC_NISLANDS_LKGE_LUT_NUM_OF_TEMP_ENTRIES; i++)
			cac_tables->cac_lkge_lut[i][j] = cpu_to_be32(max_leakage);
	}
	return 0;
}

static int ni_init_simplified_leakage_table(struct radeon_device *rdev,
					    PP_NIslands_CACTABLES *cac_tables)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct radeon_cac_leakage_table *leakage_table =
		&rdev->pm.dpm.dyn_state.cac_leakage_table;
	u32 i, j, table_size;
	u32 smc_leakage, max_leakage = 0;
	u32 scaling_factor;

	if (!leakage_table)
		return -EINVAL;

	table_size = leakage_table->count;

	if (eg_pi->vddc_voltage_table.count != table_size)
		table_size = (eg_pi->vddc_voltage_table.count < leakage_table->count) ?
			eg_pi->vddc_voltage_table.count : leakage_table->count;

	if (SMC_NISLANDS_LKGE_LUT_NUM_OF_VOLT_ENTRIES < table_size)
		table_size = SMC_NISLANDS_LKGE_LUT_NUM_OF_VOLT_ENTRIES;

	if (table_size == 0)
		return -EINVAL;

	scaling_factor = ni_get_smc_power_scaling_factor(rdev);

	for (j = 0; j < table_size; j++) {
		smc_leakage = leakage_table->entries[j].leakage;

		if (smc_leakage > max_leakage)
			max_leakage = smc_leakage;

		for (i = 0; i < SMC_NISLANDS_LKGE_LUT_NUM_OF_TEMP_ENTRIES; i++)
			cac_tables->cac_lkge_lut[i][j] =
				cpu_to_be32(ni_scale_power_for_smc(smc_leakage, scaling_factor));
	}

	for (j = table_size; j < SMC_NISLANDS_LKGE_LUT_NUM_OF_VOLT_ENTRIES; j++) {
		for (i = 0; i < SMC_NISLANDS_LKGE_LUT_NUM_OF_TEMP_ENTRIES; i++)
			cac_tables->cac_lkge_lut[i][j] =
				cpu_to_be32(ni_scale_power_for_smc(max_leakage, scaling_factor));
	}
	return 0;
}

static int ni_initialize_smc_cac_tables(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	PP_NIslands_CACTABLES *cac_tables = NULL;
	int i, ret;
        u32 reg;

	if (ni_pi->enable_cac == false)
		return 0;

	cac_tables = kzalloc(sizeof(PP_NIslands_CACTABLES), GFP_KERNEL);
	if (!cac_tables)
		return -ENOMEM;

	reg = RREG32(CG_CAC_CTRL) & ~(TID_CNT_MASK | TID_UNIT_MASK);
	reg |= (TID_CNT(ni_pi->cac_weights->tid_cnt) |
		TID_UNIT(ni_pi->cac_weights->tid_unit));
	WREG32(CG_CAC_CTRL, reg);

	for (i = 0; i < NISLANDS_DCCAC_MAX_LEVELS; i++)
		ni_pi->dc_cac_table[i] = ni_pi->cac_weights->dc_cac[i];

	for (i = 0; i < SMC_NISLANDS_BIF_LUT_NUM_OF_ENTRIES; i++)
		cac_tables->cac_bif_lut[i] = ni_pi->cac_weights->pcie_cac[i];

	ni_pi->cac_data.i_leakage = rdev->pm.dpm.cac_leakage;
	ni_pi->cac_data.pwr_const = 0;
	ni_pi->cac_data.dc_cac_value = ni_pi->dc_cac_table[NISLANDS_DCCAC_LEVEL_0];
	ni_pi->cac_data.bif_cac_value = 0;
	ni_pi->cac_data.mc_wr_weight = ni_pi->cac_weights->mc_write_weight;
	ni_pi->cac_data.mc_rd_weight = ni_pi->cac_weights->mc_read_weight;
	ni_pi->cac_data.allow_ovrflw = 0;
	ni_pi->cac_data.l2num_win_tdp = ni_pi->lta_window_size;
	ni_pi->cac_data.num_win_tdp = 0;
	ni_pi->cac_data.lts_truncate_n = ni_pi->lts_truncate;

	if (ni_pi->driver_calculate_cac_leakage)
		ret = ni_init_driver_calculated_leakage_table(rdev, cac_tables);
	else
		ret = ni_init_simplified_leakage_table(rdev, cac_tables);

	if (ret)
		goto done_free;

	cac_tables->pwr_const      = cpu_to_be32(ni_pi->cac_data.pwr_const);
	cac_tables->dc_cacValue    = cpu_to_be32(ni_pi->cac_data.dc_cac_value);
	cac_tables->bif_cacValue   = cpu_to_be32(ni_pi->cac_data.bif_cac_value);
	cac_tables->AllowOvrflw    = ni_pi->cac_data.allow_ovrflw;
	cac_tables->MCWrWeight     = ni_pi->cac_data.mc_wr_weight;
	cac_tables->MCRdWeight     = ni_pi->cac_data.mc_rd_weight;
	cac_tables->numWin_TDP     = ni_pi->cac_data.num_win_tdp;
	cac_tables->l2numWin_TDP   = ni_pi->cac_data.l2num_win_tdp;
	cac_tables->lts_truncate_n = ni_pi->cac_data.lts_truncate_n;

	ret = rv770_copy_bytes_to_smc(rdev, ni_pi->cac_table_start, (u8 *)cac_tables,
				      sizeof(PP_NIslands_CACTABLES), pi->sram_end);

done_free:
	if (ret) {
		ni_pi->enable_cac = false;
		ni_pi->enable_power_containment = false;
	}

	kfree(cac_tables);

	return 0;
}

static int ni_initialize_hardware_cac_manager(struct radeon_device *rdev)
{
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	u32 reg;

	if (!ni_pi->enable_cac ||
	    !ni_pi->cac_configuration_required)
		return 0;

	if (ni_pi->cac_weights == NULL)
		return -EINVAL;

	reg = RREG32_CG(CG_CAC_REGION_1_WEIGHT_0) & ~(WEIGHT_TCP_SIG0_MASK |
						      WEIGHT_TCP_SIG1_MASK |
						      WEIGHT_TA_SIG_MASK);
	reg |= (WEIGHT_TCP_SIG0(ni_pi->cac_weights->weight_tcp_sig0) |
		WEIGHT_TCP_SIG1(ni_pi->cac_weights->weight_tcp_sig1) |
		WEIGHT_TA_SIG(ni_pi->cac_weights->weight_ta_sig));
	WREG32_CG(CG_CAC_REGION_1_WEIGHT_0, reg);

	reg = RREG32_CG(CG_CAC_REGION_1_WEIGHT_1) & ~(WEIGHT_TCC_EN0_MASK |
						      WEIGHT_TCC_EN1_MASK |
						      WEIGHT_TCC_EN2_MASK);
	reg |= (WEIGHT_TCC_EN0(ni_pi->cac_weights->weight_tcc_en0) |
		WEIGHT_TCC_EN1(ni_pi->cac_weights->weight_tcc_en1) |
		WEIGHT_TCC_EN2(ni_pi->cac_weights->weight_tcc_en2));
	WREG32_CG(CG_CAC_REGION_1_WEIGHT_1, reg);

	reg = RREG32_CG(CG_CAC_REGION_2_WEIGHT_0) & ~(WEIGHT_CB_EN0_MASK |
						      WEIGHT_CB_EN1_MASK |
						      WEIGHT_CB_EN2_MASK |
						      WEIGHT_CB_EN3_MASK);
	reg |= (WEIGHT_CB_EN0(ni_pi->cac_weights->weight_cb_en0) |
		WEIGHT_CB_EN1(ni_pi->cac_weights->weight_cb_en1) |
		WEIGHT_CB_EN2(ni_pi->cac_weights->weight_cb_en2) |
		WEIGHT_CB_EN3(ni_pi->cac_weights->weight_cb_en3));
	WREG32_CG(CG_CAC_REGION_2_WEIGHT_0, reg);

	reg = RREG32_CG(CG_CAC_REGION_2_WEIGHT_1) & ~(WEIGHT_DB_SIG0_MASK |
						      WEIGHT_DB_SIG1_MASK |
						      WEIGHT_DB_SIG2_MASK |
						      WEIGHT_DB_SIG3_MASK);
	reg |= (WEIGHT_DB_SIG0(ni_pi->cac_weights->weight_db_sig0) |
		WEIGHT_DB_SIG1(ni_pi->cac_weights->weight_db_sig1) |
		WEIGHT_DB_SIG2(ni_pi->cac_weights->weight_db_sig2) |
		WEIGHT_DB_SIG3(ni_pi->cac_weights->weight_db_sig3));
	WREG32_CG(CG_CAC_REGION_2_WEIGHT_1, reg);

	reg = RREG32_CG(CG_CAC_REGION_2_WEIGHT_2) & ~(WEIGHT_SXM_SIG0_MASK |
						      WEIGHT_SXM_SIG1_MASK |
						      WEIGHT_SXM_SIG2_MASK |
						      WEIGHT_SXS_SIG0_MASK |
						      WEIGHT_SXS_SIG1_MASK);
	reg |= (WEIGHT_SXM_SIG0(ni_pi->cac_weights->weight_sxm_sig0) |
		WEIGHT_SXM_SIG1(ni_pi->cac_weights->weight_sxm_sig1) |
		WEIGHT_SXM_SIG2(ni_pi->cac_weights->weight_sxm_sig2) |
		WEIGHT_SXS_SIG0(ni_pi->cac_weights->weight_sxs_sig0) |
		WEIGHT_SXS_SIG1(ni_pi->cac_weights->weight_sxs_sig1));
	WREG32_CG(CG_CAC_REGION_2_WEIGHT_2, reg);

	reg = RREG32_CG(CG_CAC_REGION_3_WEIGHT_0) & ~(WEIGHT_XBR_0_MASK |
						      WEIGHT_XBR_1_MASK |
						      WEIGHT_XBR_2_MASK |
						      WEIGHT_SPI_SIG0_MASK);
	reg |= (WEIGHT_XBR_0(ni_pi->cac_weights->weight_xbr_0) |
		WEIGHT_XBR_1(ni_pi->cac_weights->weight_xbr_1) |
		WEIGHT_XBR_2(ni_pi->cac_weights->weight_xbr_2) |
		WEIGHT_SPI_SIG0(ni_pi->cac_weights->weight_spi_sig0));
	WREG32_CG(CG_CAC_REGION_3_WEIGHT_0, reg);

	reg = RREG32_CG(CG_CAC_REGION_3_WEIGHT_1) & ~(WEIGHT_SPI_SIG1_MASK |
						      WEIGHT_SPI_SIG2_MASK |
						      WEIGHT_SPI_SIG3_MASK |
						      WEIGHT_SPI_SIG4_MASK |
						      WEIGHT_SPI_SIG5_MASK);
	reg |= (WEIGHT_SPI_SIG1(ni_pi->cac_weights->weight_spi_sig1) |
		WEIGHT_SPI_SIG2(ni_pi->cac_weights->weight_spi_sig2) |
		WEIGHT_SPI_SIG3(ni_pi->cac_weights->weight_spi_sig3) |
		WEIGHT_SPI_SIG4(ni_pi->cac_weights->weight_spi_sig4) |
		WEIGHT_SPI_SIG5(ni_pi->cac_weights->weight_spi_sig5));
	WREG32_CG(CG_CAC_REGION_3_WEIGHT_1, reg);

	reg = RREG32_CG(CG_CAC_REGION_4_WEIGHT_0) & ~(WEIGHT_LDS_SIG0_MASK |
						      WEIGHT_LDS_SIG1_MASK |
						      WEIGHT_SC_MASK);
	reg |= (WEIGHT_LDS_SIG0(ni_pi->cac_weights->weight_lds_sig0) |
		WEIGHT_LDS_SIG1(ni_pi->cac_weights->weight_lds_sig1) |
		WEIGHT_SC(ni_pi->cac_weights->weight_sc));
	WREG32_CG(CG_CAC_REGION_4_WEIGHT_0, reg);

	reg = RREG32_CG(CG_CAC_REGION_4_WEIGHT_1) & ~(WEIGHT_BIF_MASK |
						      WEIGHT_CP_MASK |
						      WEIGHT_PA_SIG0_MASK |
						      WEIGHT_PA_SIG1_MASK |
						      WEIGHT_VGT_SIG0_MASK);
	reg |= (WEIGHT_BIF(ni_pi->cac_weights->weight_bif) |
		WEIGHT_CP(ni_pi->cac_weights->weight_cp) |
		WEIGHT_PA_SIG0(ni_pi->cac_weights->weight_pa_sig0) |
		WEIGHT_PA_SIG1(ni_pi->cac_weights->weight_pa_sig1) |
		WEIGHT_VGT_SIG0(ni_pi->cac_weights->weight_vgt_sig0));
	WREG32_CG(CG_CAC_REGION_4_WEIGHT_1, reg);

	reg = RREG32_CG(CG_CAC_REGION_4_WEIGHT_2) & ~(WEIGHT_VGT_SIG1_MASK |
						      WEIGHT_VGT_SIG2_MASK |
						      WEIGHT_DC_SIG0_MASK |
						      WEIGHT_DC_SIG1_MASK |
						      WEIGHT_DC_SIG2_MASK);
	reg |= (WEIGHT_VGT_SIG1(ni_pi->cac_weights->weight_vgt_sig1) |
		WEIGHT_VGT_SIG2(ni_pi->cac_weights->weight_vgt_sig2) |
		WEIGHT_DC_SIG0(ni_pi->cac_weights->weight_dc_sig0) |
		WEIGHT_DC_SIG1(ni_pi->cac_weights->weight_dc_sig1) |
		WEIGHT_DC_SIG2(ni_pi->cac_weights->weight_dc_sig2));
	WREG32_CG(CG_CAC_REGION_4_WEIGHT_2, reg);

	reg = RREG32_CG(CG_CAC_REGION_4_WEIGHT_3) & ~(WEIGHT_DC_SIG3_MASK |
						      WEIGHT_UVD_SIG0_MASK |
						      WEIGHT_UVD_SIG1_MASK |
						      WEIGHT_SPARE0_MASK |
						      WEIGHT_SPARE1_MASK);
	reg |= (WEIGHT_DC_SIG3(ni_pi->cac_weights->weight_dc_sig3) |
		WEIGHT_UVD_SIG0(ni_pi->cac_weights->weight_uvd_sig0) |
		WEIGHT_UVD_SIG1(ni_pi->cac_weights->weight_uvd_sig1) |
		WEIGHT_SPARE0(ni_pi->cac_weights->weight_spare0) |
		WEIGHT_SPARE1(ni_pi->cac_weights->weight_spare1));
	WREG32_CG(CG_CAC_REGION_4_WEIGHT_3, reg);

	reg = RREG32_CG(CG_CAC_REGION_5_WEIGHT_0) & ~(WEIGHT_SQ_VSP_MASK |
						      WEIGHT_SQ_VSP0_MASK);
	reg |= (WEIGHT_SQ_VSP(ni_pi->cac_weights->weight_sq_vsp) |
		WEIGHT_SQ_VSP0(ni_pi->cac_weights->weight_sq_vsp0));
	WREG32_CG(CG_CAC_REGION_5_WEIGHT_0, reg);

	reg = RREG32_CG(CG_CAC_REGION_5_WEIGHT_1) & ~(WEIGHT_SQ_GPR_MASK);
	reg |= WEIGHT_SQ_GPR(ni_pi->cac_weights->weight_sq_gpr);
	WREG32_CG(CG_CAC_REGION_5_WEIGHT_1, reg);

	reg = RREG32_CG(CG_CAC_REGION_4_OVERRIDE_4) & ~(OVR_MODE_SPARE_0_MASK |
							OVR_VAL_SPARE_0_MASK |
							OVR_MODE_SPARE_1_MASK |
							OVR_VAL_SPARE_1_MASK);
	reg |= (OVR_MODE_SPARE_0(ni_pi->cac_weights->ovr_mode_spare_0) |
		OVR_VAL_SPARE_0(ni_pi->cac_weights->ovr_val_spare_0) |
		OVR_MODE_SPARE_1(ni_pi->cac_weights->ovr_mode_spare_1) |
		OVR_VAL_SPARE_1(ni_pi->cac_weights->ovr_val_spare_1));
	WREG32_CG(CG_CAC_REGION_4_OVERRIDE_4, reg);

	reg = RREG32(SQ_CAC_THRESHOLD) & ~(VSP_MASK |
					   VSP0_MASK |
					   GPR_MASK);
	reg |= (VSP(ni_pi->cac_weights->vsp) |
		VSP0(ni_pi->cac_weights->vsp0) |
		GPR(ni_pi->cac_weights->gpr));
	WREG32(SQ_CAC_THRESHOLD, reg);

	reg = (MCDW_WR_ENABLE |
	       MCDX_WR_ENABLE |
	       MCDY_WR_ENABLE |
	       MCDZ_WR_ENABLE |
	       INDEX(0x09D4));
	WREG32(MC_CG_CONFIG, reg);

	reg = (READ_WEIGHT(ni_pi->cac_weights->mc_read_weight) |
	       WRITE_WEIGHT(ni_pi->cac_weights->mc_write_weight) |
	       ALLOW_OVERFLOW);
	WREG32(MC_CG_DATAPORT, reg);

	return 0;
}

static int ni_enable_smc_cac(struct radeon_device *rdev, bool enable)
{
	struct ni_power_info *ni_pi = ni_get_pi(rdev);
	int ret = 0;
	PPSMC_Result smc_result;

	if (ni_pi->enable_cac) {
		if (enable) {
			struct radeon_ps *radeon_new_state = rdev->pm.dpm.requested_ps;

			if (!r600_is_uvd_state(radeon_new_state->class, radeon_new_state->class2)) {
				smc_result = rv770_send_msg_to_smc(rdev, PPSMC_MSG_CollectCAC_PowerCorreln);

				if (ni_pi->support_cac_long_term_average) {
					smc_result = rv770_send_msg_to_smc(rdev, PPSMC_CACLongTermAvgEnable);
					if (PPSMC_Result_OK != smc_result)
						ni_pi->support_cac_long_term_average = false;
				}

				smc_result = rv770_send_msg_to_smc(rdev, PPSMC_MSG_EnableCac);
				if (PPSMC_Result_OK != smc_result)
					ret = -EINVAL;

				ni_pi->cac_enabled = (PPSMC_Result_OK == smc_result) ? true : false;
			}
		} else if (ni_pi->cac_enabled) {
			smc_result = rv770_send_msg_to_smc(rdev, PPSMC_MSG_DisableCac);

			ni_pi->cac_enabled = false;

			if (ni_pi->support_cac_long_term_average) {
				smc_result = rv770_send_msg_to_smc(rdev, PPSMC_CACLongTermAvgDisable);
				if (PPSMC_Result_OK != smc_result)
					ni_pi->support_cac_long_term_average = false;
			}
		}
	}

	return ret;
}

static int ni_pcie_performance_request(struct radeon_device *rdev,
				       u8 perf_req, bool advertise)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

#if defined(CONFIG_ACPI)
	if ((perf_req == PCIE_PERF_REQ_PECI_GEN1) ||
            (perf_req == PCIE_PERF_REQ_PECI_GEN2)) {
		if (eg_pi->pcie_performance_request_registered == false)
			radeon_acpi_pcie_notify_device_ready(rdev);
		eg_pi->pcie_performance_request_registered = true;
		return radeon_acpi_pcie_performance_request(rdev, perf_req, advertise);
	} else if ((perf_req == PCIE_PERF_REQ_REMOVE_REGISTRY) &&
                   eg_pi->pcie_performance_request_registered) {
		eg_pi->pcie_performance_request_registered = false;
		return radeon_acpi_pcie_performance_request(rdev, perf_req, advertise);
	}
#endif
	return 0;
}

static int ni_advertise_gen2_capability(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 tmp;

        tmp = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);

        if ((tmp & LC_OTHER_SIDE_EVER_SENT_GEN2) &&
            (tmp & LC_OTHER_SIDE_SUPPORTS_GEN2))
                pi->pcie_gen2 = true;
        else
		pi->pcie_gen2 = false;

	if (!pi->pcie_gen2)
		ni_pcie_performance_request(rdev, PCIE_PERF_REQ_PECI_GEN2, true);

	return 0;
}

static void ni_enable_bif_dynamic_pcie_gen2(struct radeon_device *rdev,
					    bool enable)
{
        struct rv7xx_power_info *pi = rv770_get_pi(rdev);
        u32 tmp, bif;

	tmp = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);

	if ((tmp & LC_OTHER_SIDE_EVER_SENT_GEN2) &&
	    (tmp & LC_OTHER_SIDE_SUPPORTS_GEN2)) {
		if (enable) {
			if (!pi->boot_in_gen2) {
				bif = RREG32(CG_BIF_REQ_AND_RSP) & ~CG_CLIENT_REQ_MASK;
				bif |= CG_CLIENT_REQ(0xd);
				WREG32(CG_BIF_REQ_AND_RSP, bif);
			}
			tmp &= ~LC_HW_VOLTAGE_IF_CONTROL_MASK;
			tmp |= LC_HW_VOLTAGE_IF_CONTROL(1);
			tmp |= LC_GEN2_EN_STRAP;

			tmp |= LC_CLR_FAILED_SPD_CHANGE_CNT;
			WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, tmp);
			udelay(10);
			tmp &= ~LC_CLR_FAILED_SPD_CHANGE_CNT;
			WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, tmp);
		} else {
			if (!pi->boot_in_gen2) {
				bif = RREG32(CG_BIF_REQ_AND_RSP) & ~CG_CLIENT_REQ_MASK;
				bif |= CG_CLIENT_REQ(0xd);
				WREG32(CG_BIF_REQ_AND_RSP, bif);

				tmp &= ~LC_HW_VOLTAGE_IF_CONTROL_MASK;
				tmp &= ~LC_GEN2_EN_STRAP;
			}
			WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, tmp);
		}
	}
}

static void ni_enable_dynamic_pcie_gen2(struct radeon_device *rdev,
					bool enable)
{
	ni_enable_bif_dynamic_pcie_gen2(rdev, enable);

	if (enable)
		WREG32_P(GENERAL_PWRMGT, ENABLE_GEN2PCIE, ~ENABLE_GEN2PCIE);
	else
                WREG32_P(GENERAL_PWRMGT, 0, ~ENABLE_GEN2PCIE);
}

void ni_dpm_setup_asic(struct radeon_device *rdev)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	ni_read_clock_registers(rdev);
	btc_read_arb_registers(rdev);
	rv770_get_memory_type(rdev);
	if (eg_pi->pcie_performance_request)
		ni_advertise_gen2_capability(rdev);
	rv770_get_pcie_gen2_status(rdev);
	rv770_enable_acpi_pm(rdev);
}

int ni_dpm_enable(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	if (pi->gfx_clock_gating)
		ni_cg_clockgating_default(rdev);
        if (btc_dpm_enabled(rdev))
                return -EINVAL;
	if (pi->mg_clock_gating)
		ni_mg_clockgating_default(rdev);
	if (eg_pi->ls_clock_gating)
		ni_ls_clockgating_default(rdev);
	if (pi->voltage_control) {
		rv770_enable_voltage_control(rdev, true);
		cypress_construct_voltage_tables(rdev);
	}
	if (eg_pi->dynamic_ac_timing)
		ni_initialize_mc_reg_table(rdev);
	if (pi->dynamic_ss)
		cypress_enable_spread_spectrum(rdev, true);
	if (pi->thermal_protection)
		rv770_enable_thermal_protection(rdev, true);
	rv770_setup_bsp(rdev);
	rv770_program_git(rdev);
	rv770_program_tp(rdev);
	rv770_program_tpp(rdev);
	rv770_program_sstp(rdev);
	cypress_enable_display_gap(rdev);
	rv770_program_vc(rdev);
	if (pi->dynamic_pcie_gen2)
		ni_enable_dynamic_pcie_gen2(rdev, true);
	if (rv770_upload_firmware(rdev))
		return -EINVAL;
	ni_process_firmware_header(rdev);
	ni_initial_switch_from_arb_f0_to_f1(rdev);
	ni_init_smc_table(rdev);
	ni_init_smc_spll_table(rdev);
	ni_init_arb_table_index(rdev);
	if (eg_pi->dynamic_ac_timing)
		ni_populate_mc_reg_table(rdev);
	ni_initialize_smc_cac_tables(rdev);
	ni_initialize_hardware_cac_manager(rdev);
	ni_populate_smc_tdp_limits(rdev);
	ni_program_response_times(rdev);
	r7xx_start_smc(rdev);
	cypress_notify_smc_display_change(rdev, false);
	cypress_enable_sclk_control(rdev, true);
	if (eg_pi->memory_transition)
		cypress_enable_mclk_control(rdev, true);
	cypress_start_dpm(rdev);
	if (pi->gfx_clock_gating)
		ni_gfx_clockgating_enable(rdev, true);
	if (pi->mg_clock_gating)
		ni_mg_clockgating_enable(rdev, true);
	if (eg_pi->ls_clock_gating)
		ni_ls_clockgating_enable(rdev, true);

	if (rdev->irq.installed &&
	    r600_is_internal_thermal_sensor(rdev->pm.int_thermal_type)) {
		PPSMC_Result result;

		rv770_set_thermal_temperature_range(rdev, R600_TEMP_RANGE_MIN, 0xff * 1000);
		rdev->irq.dpm_thermal = true;
		radeon_irq_set(rdev);
		result = rv770_send_msg_to_smc(rdev, PPSMC_MSG_EnableThermalInterrupt);

		if (result != PPSMC_Result_OK)
			DRM_DEBUG_KMS("Could not enable thermal interrupts.\n");
	}

	rv770_enable_auto_throttle_source(rdev, RADEON_DPM_AUTO_THROTTLE_SRC_THERMAL, true);

	return 0;
}

void ni_dpm_disable(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	if (!btc_dpm_enabled(rdev))
		return;
	rv770_clear_vc(rdev);
	if (pi->thermal_protection)
		rv770_enable_thermal_protection(rdev, false);
	ni_enable_power_containment(rdev, false);
	ni_enable_smc_cac(rdev, false);
	cypress_enable_spread_spectrum(rdev, false);
	rv770_enable_auto_throttle_source(rdev, RADEON_DPM_AUTO_THROTTLE_SRC_THERMAL, false);
	if (pi->dynamic_pcie_gen2)
		ni_enable_dynamic_pcie_gen2(rdev, false);

	if (rdev->irq.installed &&
	    r600_is_internal_thermal_sensor(rdev->pm.int_thermal_type)) {
		rdev->irq.dpm_thermal = false;
		radeon_irq_set(rdev);
	}

	if (pi->gfx_clock_gating)
		ni_gfx_clockgating_enable(rdev, false);
	if (pi->mg_clock_gating)
		ni_mg_clockgating_enable(rdev, false);
	if (eg_pi->ls_clock_gating)
		ni_ls_clockgating_enable(rdev, false);
	ni_stop_dpm(rdev);
	btc_reset_to_default(rdev);
	ni_stop_smc(rdev);
	ni_force_switch_to_arb_f0(rdev);
}

int ni_power_control_set_level(struct radeon_device *rdev)
{
	ni_restrict_performance_levels_before_switch(rdev);
	rv770_halt_smc(rdev);
	ni_populate_smc_tdp_limits(rdev);
	rv770_resume_smc(rdev);
	rv770_set_sw_state(rdev);

	return 0;
}

int ni_dpm_set_power_state(struct radeon_device *rdev)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct radeon_ps *new_ps = rdev->pm.dpm.requested_ps;
	int ret;

	ni_apply_state_adjust_rules(rdev);

	ni_restrict_performance_levels_before_switch(rdev);
	ni_enable_power_containment(rdev, false);
	ni_enable_smc_cac(rdev, false);
	rv770_halt_smc(rdev);
	if (eg_pi->smu_uvd_hs)
		btc_notify_uvd_to_smc(rdev, new_ps);
	ni_upload_sw_state(rdev);
	if (eg_pi->dynamic_ac_timing)
		ni_upload_mc_reg_table(rdev);
	ret = ni_program_memory_timing_parameters(rdev);
	if (ret)
		return ret;
	ni_populate_smc_tdp_limits(rdev);
	rv770_resume_smc(rdev);
	rv770_set_sw_state(rdev);
	ni_enable_smc_cac(rdev, true);
	ni_enable_power_containment(rdev, true);

#if 0
	/* XXX */
	ni_unrestrict_performance_levels_after_switch(rdev);
#endif

	return 0;
}

void ni_dpm_reset_asic(struct radeon_device *rdev)
{
	ni_restrict_performance_levels_before_switch(rdev);
	rv770_set_boot_state(rdev);
}

union power_info {
	struct _ATOM_POWERPLAY_INFO info;
	struct _ATOM_POWERPLAY_INFO_V2 info_2;
	struct _ATOM_POWERPLAY_INFO_V3 info_3;
	struct _ATOM_PPLIB_POWERPLAYTABLE pplib;
	struct _ATOM_PPLIB_POWERPLAYTABLE2 pplib2;
	struct _ATOM_PPLIB_POWERPLAYTABLE3 pplib3;
};

union pplib_clock_info {
	struct _ATOM_PPLIB_R600_CLOCK_INFO r600;
	struct _ATOM_PPLIB_RS780_CLOCK_INFO rs780;
	struct _ATOM_PPLIB_EVERGREEN_CLOCK_INFO evergreen;
	struct _ATOM_PPLIB_SUMO_CLOCK_INFO sumo;
};

union pplib_power_state {
	struct _ATOM_PPLIB_STATE v1;
	struct _ATOM_PPLIB_STATE_V2 v2;
};

static void ni_parse_pplib_non_clock_info(struct radeon_device *rdev,
					  struct radeon_ps *rps,
					  struct _ATOM_PPLIB_NONCLOCK_INFO *non_clock_info,
					  u8 table_rev)
{
	rps->caps = le32_to_cpu(non_clock_info->ulCapsAndSettings);
	rps->class = le16_to_cpu(non_clock_info->usClassification);
	rps->class2 = le16_to_cpu(non_clock_info->usClassification2);

	if (ATOM_PPLIB_NONCLOCKINFO_VER1 < table_rev) {
		rps->vclk = le32_to_cpu(non_clock_info->ulVCLK);
		rps->dclk = le32_to_cpu(non_clock_info->ulDCLK);
	} else if (r600_is_uvd_state(rps->class, rps->class2)) {
		rps->vclk = RV770_DEFAULT_VCLK_FREQ;
		rps->dclk = RV770_DEFAULT_DCLK_FREQ;
	} else {
		rps->vclk = 0;
		rps->dclk = 0;
	}

	if (rps->class & ATOM_PPLIB_CLASSIFICATION_BOOT)
		rdev->pm.dpm.boot_ps = rps;
	if (rps->class & ATOM_PPLIB_CLASSIFICATION_UVDSTATE)
		rdev->pm.dpm.uvd_ps = rps;
}

static void ni_parse_pplib_clock_info(struct radeon_device *rdev,
				      struct radeon_ps *rps, int index,
				      union pplib_clock_info *clock_info)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct ni_ps *ps = ni_get_ps(rps);
	u16 vddc;
	struct rv7xx_pl *pl = &ps->performance_levels[index];

	ps->performance_level_count = index + 1;

	pl->sclk = le16_to_cpu(clock_info->evergreen.usEngineClockLow);
	pl->sclk |= clock_info->evergreen.ucEngineClockHigh << 16;
	pl->mclk = le16_to_cpu(clock_info->evergreen.usMemoryClockLow);
	pl->mclk |= clock_info->evergreen.ucMemoryClockHigh << 16;

	pl->vddc = le16_to_cpu(clock_info->evergreen.usVDDC);
	pl->vddci = le16_to_cpu(clock_info->evergreen.usVDDCI);
	pl->flags = le32_to_cpu(clock_info->evergreen.ulFlags);

	/* patch up vddc if necessary */
	if (pl->vddc == 0xff01) {
		if (radeon_atom_get_max_vddc(rdev, 0, 0, &vddc) == 0)
			pl->vddc = vddc;
	}

	if (rps->class & ATOM_PPLIB_CLASSIFICATION_ACPI) {
		pi->acpi_vddc = pl->vddc;
		eg_pi->acpi_vddci = pl->vddci;
		if (ps->performance_levels[0].flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2)
			pi->acpi_pcie_gen2 = true;
		else
			pi->acpi_pcie_gen2 = false;
	}

	if (rps->class2 & ATOM_PPLIB_CLASSIFICATION2_ULV) {
		eg_pi->ulv.supported = true;
		eg_pi->ulv.pl = pl;
	}

	if (pi->min_vddc_in_table > pl->vddc)
		pi->min_vddc_in_table = pl->vddc;

	if (pi->max_vddc_in_table < pl->vddc)
		pi->max_vddc_in_table = pl->vddc;

	/* patch up boot state */
	if (rps->class & ATOM_PPLIB_CLASSIFICATION_BOOT) {
		u16 vddc, vddci;
		radeon_atombios_get_default_voltages(rdev, &vddc, &vddci);
		pl->mclk = rdev->clock.default_mclk;
		pl->sclk = rdev->clock.default_sclk;
		pl->vddc = vddc;
		pl->vddci = vddci;
	}

	if ((rps->class & ATOM_PPLIB_CLASSIFICATION_UI_MASK) ==
	    ATOM_PPLIB_CLASSIFICATION_UI_PERFORMANCE) {
		rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac.sclk = pl->sclk;
		rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac.mclk = pl->mclk;
		rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac.vddc = pl->vddc;
		rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac.vddci = pl->vddci;
	}
}

static int ni_parse_power_table(struct radeon_device *rdev)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	struct _ATOM_PPLIB_NONCLOCK_INFO *non_clock_info;
	union pplib_power_state *power_state;
	int i, j;
	union pplib_clock_info *clock_info;
	union power_info *power_info;
	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
        u16 data_offset;
	u8 frev, crev;
	struct ni_ps *ps;

	if (!atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset))
		return -EINVAL;
	power_info = (union power_info *)(mode_info->atom_context->bios + data_offset);

	rdev->pm.dpm.ps = kzalloc(sizeof(struct radeon_ps) *
				  power_info->pplib.ucNumStates, GFP_KERNEL);
	if (!rdev->pm.dpm.ps)
		return -ENOMEM;
	rdev->pm.dpm.platform_caps = le32_to_cpu(power_info->pplib.ulPlatformCaps);
	rdev->pm.dpm.backbias_response_time = le16_to_cpu(power_info->pplib.usBackbiasTime);
	rdev->pm.dpm.voltage_response_time = le16_to_cpu(power_info->pplib.usVoltageTime);

	for (i = 0; i < power_info->pplib.ucNumStates; i++) {
		power_state = (union pplib_power_state *)
			(mode_info->atom_context->bios + data_offset +
			 le16_to_cpu(power_info->pplib.usStateArrayOffset) +
			 i * power_info->pplib.ucStateEntrySize);
		non_clock_info = (struct _ATOM_PPLIB_NONCLOCK_INFO *)
			(mode_info->atom_context->bios + data_offset +
			 le16_to_cpu(power_info->pplib.usNonClockInfoArrayOffset) +
			 (power_state->v1.ucNonClockStateIndex *
			  power_info->pplib.ucNonClockSize));
		if (power_info->pplib.ucStateEntrySize - 1) {
			ps = kzalloc(sizeof(struct ni_ps), GFP_KERNEL);
			if (ps == NULL) {
				kfree(rdev->pm.dpm.ps);
				return -ENOMEM;
			}
			rdev->pm.dpm.ps[i].ps_priv = ps;
			ni_parse_pplib_non_clock_info(rdev, &rdev->pm.dpm.ps[i],
							 non_clock_info,
							 power_info->pplib.ucNonClockSize);
			for (j = 0; j < (power_info->pplib.ucStateEntrySize - 1); j++) {
				clock_info = (union pplib_clock_info *)
					(mode_info->atom_context->bios + data_offset +
					 le16_to_cpu(power_info->pplib.usClockInfoArrayOffset) +
					 (power_state->v1.ucClockStateIndices[j] *
					  power_info->pplib.ucClockInfoSize));
				ni_parse_pplib_clock_info(rdev,
							  &rdev->pm.dpm.ps[i], j,
							  clock_info);
			}
		}
	}
	rdev->pm.dpm.num_ps = power_info->pplib.ucNumStates;
	return 0;
}

int ni_dpm_init(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi;
	struct evergreen_power_info *eg_pi;
	struct ni_power_info *ni_pi;
	int index = GetIndexIntoMasterTable(DATA, ASIC_InternalSS_Info);
	u16 data_offset, size;
	u8 frev, crev;
	struct atom_clock_dividers dividers;
	int ret;

	ni_pi = kzalloc(sizeof(struct ni_power_info), GFP_KERNEL);
	if (ni_pi == NULL)
		return -ENOMEM;
	rdev->pm.dpm.priv = ni_pi;
	eg_pi = &ni_pi->eg;
	pi = &eg_pi->rv7xx;

	rv770_get_max_vddc(rdev);

	eg_pi->ulv.supported = false;
	pi->acpi_vddc = 0;
	eg_pi->acpi_vddci = 0;
	pi->min_vddc_in_table = 0;
	pi->max_vddc_in_table = 0;

	ret = ni_parse_power_table(rdev);
	if (ret)
		return ret;
	ret = r600_parse_extended_power_table(rdev);
	if (ret)
		return ret;

	ni_patch_dependency_tables_based_on_leakage(rdev);

	if (rdev->pm.dpm.voltage_response_time == 0)
		rdev->pm.dpm.voltage_response_time = R600_VOLTAGERESPONSETIME_DFLT;
	if (rdev->pm.dpm.backbias_response_time == 0)
		rdev->pm.dpm.backbias_response_time = R600_BACKBIASRESPONSETIME_DFLT;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
					     0, false, &dividers);
	if (ret)
		pi->ref_div = dividers.ref_div + 1;
	else
		pi->ref_div = R600_REFERENCEDIVIDER_DFLT;

	pi->rlp = RV770_RLP_DFLT;
	pi->rmp = RV770_RMP_DFLT;
	pi->lhp = RV770_LHP_DFLT;
	pi->lmp = RV770_LMP_DFLT;

	eg_pi->ats[0].rlp = RV770_RLP_DFLT;
	eg_pi->ats[0].rmp = RV770_RMP_DFLT;
	eg_pi->ats[0].lhp = RV770_LHP_DFLT;
	eg_pi->ats[0].lmp = RV770_LMP_DFLT;

	eg_pi->ats[1].rlp = BTC_RLP_UVD_DFLT;
	eg_pi->ats[1].rmp = BTC_RMP_UVD_DFLT;
	eg_pi->ats[1].lhp = BTC_LHP_UVD_DFLT;
	eg_pi->ats[1].lmp = BTC_LMP_UVD_DFLT;

	eg_pi->smu_uvd_hs = true;

	if (rdev->pdev->device == 0x6707) {
		pi->mclk_strobe_mode_threshold = 55000;
		pi->mclk_edc_enable_threshold = 55000;
		eg_pi->mclk_edc_wr_enable_threshold = 55000;
	} else {
		pi->mclk_strobe_mode_threshold = 40000;
		pi->mclk_edc_enable_threshold = 40000;
		eg_pi->mclk_edc_wr_enable_threshold = 40000;
	}
	ni_pi->mclk_rtt_mode_threshold = eg_pi->mclk_edc_wr_enable_threshold;

	pi->voltage_control =
		radeon_atom_is_voltage_gpio(rdev, SET_VOLTAGE_TYPE_ASIC_VDDC);

	pi->mvdd_control =
		radeon_atom_is_voltage_gpio(rdev, SET_VOLTAGE_TYPE_ASIC_MVDDC);

	eg_pi->vddci_control =
		radeon_atom_is_voltage_gpio(rdev, SET_VOLTAGE_TYPE_ASIC_VDDCI);

	if (atom_parse_data_header(rdev->mode_info.atom_context, index, &size,
                                   &frev, &crev, &data_offset)) {
		pi->sclk_ss = true;
		pi->mclk_ss = true;
		pi->dynamic_ss = true;
	} else {
		pi->sclk_ss = false;
		pi->mclk_ss = false;
		pi->dynamic_ss = true;
	}

	pi->asi = RV770_ASI_DFLT;
	pi->pasi = CYPRESS_HASI_DFLT;
	pi->vrc = CYPRESS_VRC_DFLT;

	pi->power_gating = false;

	pi->gfx_clock_gating = true;

	pi->mg_clock_gating = true;
	pi->mgcgtssm = true;
	eg_pi->ls_clock_gating = false;
	eg_pi->sclk_deep_sleep = false;

	pi->dynamic_pcie_gen2 = true;

	if (pi->gfx_clock_gating &&
	    (rdev->pm.int_thermal_type != THERMAL_TYPE_NONE))
		pi->thermal_protection = true;
	else
		pi->thermal_protection = false;

	pi->display_gap = true;

	pi->dcodt = true;

	pi->ulps = true;

	eg_pi->dynamic_ac_timing = true;
	eg_pi->abm = true;
	eg_pi->mcls = true;
	eg_pi->light_sleep = true;
	eg_pi->memory_transition = true;
#if defined(CONFIG_ACPI)
	eg_pi->pcie_performance_request =
		radeon_acpi_is_pcie_performance_request_supported(rdev);
#else
	eg_pi->pcie_performance_request = false;
#endif

	eg_pi->dll_default_on = false;

	eg_pi->sclk_deep_sleep = false;

	pi->mclk_stutter_mode_threshold = 0;

	pi->sram_end = SMC_RAM_END;

	rdev->pm.dpm.dyn_state.mclk_sclk_ratio = 3;
	rdev->pm.dpm.dyn_state.vddc_vddci_delta = 200;
	rdev->pm.dpm.dyn_state.min_vddc_for_pcie_gen2 = 900;
	rdev->pm.dpm.dyn_state.valid_sclk_values.count = ARRAY_SIZE(btc_valid_sclk);
	rdev->pm.dpm.dyn_state.valid_sclk_values.values = btc_valid_sclk;
	rdev->pm.dpm.dyn_state.valid_mclk_values.count = 0;
	rdev->pm.dpm.dyn_state.valid_mclk_values.values = NULL;
	rdev->pm.dpm.dyn_state.sclk_mclk_delta = 12500;

	ni_pi->cac_data.leakage_coefficients.at = 516;
	ni_pi->cac_data.leakage_coefficients.bt = 18;
	ni_pi->cac_data.leakage_coefficients.av = 51;
	ni_pi->cac_data.leakage_coefficients.bv = 2957;

	switch (rdev->pdev->device) {
	case 0x6700:
	case 0x6701:
	case 0x6702:
	case 0x6703:
	case 0x6718:
		ni_pi->cac_weights = &cac_weights_cayman_xt;
		break;
	case 0x6705:
	case 0x6719:
	case 0x671D:
	case 0x671C:
	default:
		ni_pi->cac_weights = &cac_weights_cayman_pro;
		break;
	case 0x6704:
	case 0x6706:
	case 0x6707:
	case 0x6708:
	case 0x6709:
		ni_pi->cac_weights = &cac_weights_cayman_le;
		break;
	}

	if (ni_pi->cac_weights->enable_power_containment_by_default) {
		ni_pi->enable_power_containment = true;
		ni_pi->enable_cac = true;
		ni_pi->enable_sq_ramping = true;
	} else {
		ni_pi->enable_power_containment = false;
		ni_pi->enable_cac = false;
		ni_pi->enable_sq_ramping = false;
	}

	ni_pi->driver_calculate_cac_leakage = false;
	ni_pi->cac_configuration_required = true;

	if (ni_pi->cac_configuration_required) {
		ni_pi->support_cac_long_term_average = true;
		ni_pi->lta_window_size = ni_pi->cac_weights->l2_lta_window_size;
		ni_pi->lts_truncate = ni_pi->cac_weights->lts_truncate;
	} else {
		ni_pi->support_cac_long_term_average = false;
		ni_pi->lta_window_size = 0;
		ni_pi->lts_truncate = 0;
	}

	ni_pi->use_power_boost_limit = true;

	return 0;
}

void ni_dpm_fini(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < rdev->pm.dpm.num_ps; i++) {
		kfree(rdev->pm.dpm.ps[i].ps_priv);
	}
	kfree(rdev->pm.dpm.ps);
	kfree(rdev->pm.dpm.priv);
	r600_free_extended_power_table(rdev);
}

void ni_dpm_print_power_state(struct radeon_device *rdev,
			      struct radeon_ps *rps)
{
	struct ni_ps *ps = ni_get_ps(rps);
	struct rv7xx_pl *pl;
	int i;

	r600_dpm_print_class_info(rps->class, rps->class2);
	r600_dpm_print_cap_info(rps->caps);
	printk("\tuvd    vclk: %d dclk: %d\n", rps->vclk, rps->dclk);
	for (i = 0; i < ps->performance_level_count; i++) {
		pl = &ps->performance_levels[i];
		printk("\t\tpower level 0    sclk: %u mclk: %u vddc: %u vddci: %u\n",
		       pl->sclk, pl->mclk, pl->vddc, pl->vddci);
	}
	r600_dpm_print_ps_status(rdev, rps);
}

u32 ni_dpm_get_sclk(struct radeon_device *rdev, bool low)
{
	struct ni_ps *requested_state = ni_get_ps(rdev->pm.dpm.requested_ps);

	if (low)
		return requested_state->performance_levels[0].sclk;
	else
		return requested_state->performance_levels[requested_state->performance_level_count - 1].sclk;
}

u32 ni_dpm_get_mclk(struct radeon_device *rdev, bool low)
{
	struct ni_ps *requested_state = ni_get_ps(rdev->pm.dpm.requested_ps);

	if (low)
		return requested_state->performance_levels[0].mclk;
	else
		return requested_state->performance_levels[requested_state->performance_level_count - 1].mclk;
}

