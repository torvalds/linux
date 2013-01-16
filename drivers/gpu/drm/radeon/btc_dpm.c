/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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

#include "drmP.h"
#include "radeon.h"
#include "btcd.h"
#include "r600_dpm.h"
#include "cypress_dpm.h"
#include "btc_dpm.h"
#include "atom.h"

#define MC_CG_ARB_FREQ_F0           0x0a
#define MC_CG_ARB_FREQ_F1           0x0b
#define MC_CG_ARB_FREQ_F2           0x0c
#define MC_CG_ARB_FREQ_F3           0x0d

#define MC_CG_SEQ_DRAMCONF_S0       0x05
#define MC_CG_SEQ_DRAMCONF_S1       0x06
#define MC_CG_SEQ_YCLK_SUSPEND      0x04
#define MC_CG_SEQ_YCLK_RESUME       0x0a

#define SMC_RAM_END 0x8000

#ifndef BTC_MGCG_SEQUENCE
#define BTC_MGCG_SEQUENCE  300

struct rv7xx_ps *rv770_get_ps(struct radeon_ps *rps);
struct rv7xx_power_info *rv770_get_pi(struct radeon_device *rdev);
struct evergreen_power_info *evergreen_get_pi(struct radeon_device *rdev);


//********* BARTS **************//
static const u32 barts_cgcg_cgls_default[] =
{
	/* Register,   Value,     Mask bits */
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
#define BARTS_CGCG_CGLS_DEFAULT_LENGTH sizeof(barts_cgcg_cgls_default) / (3 * sizeof(u32))

static const u32 barts_cgcg_cgls_disable[] =
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
	0x00000644, 0x000f7912, 0x001f4180,
	0x00000644, 0x000f3812, 0x001f4180
};
#define BARTS_CGCG_CGLS_DISABLE_LENGTH sizeof(barts_cgcg_cgls_disable) / (3 * sizeof(u32))

static const u32 barts_cgcg_cgls_enable[] =
{
	/* 0x0000c124, 0x84180000, 0x00180000, */
	0x00000644, 0x000f7892, 0x001f4080,
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
#define BARTS_CGCG_CGLS_ENABLE_LENGTH sizeof(barts_cgcg_cgls_enable) / (3 * sizeof(u32))

static const u32 barts_mgcg_default[] =
{
	0x0000802c, 0xc0000000, 0xffffffff,
	0x00005448, 0x00000100, 0xffffffff,
	0x000055e4, 0x00600100, 0xffffffff,
	0x0000160c, 0x00000100, 0xffffffff,
	0x0000c164, 0x00000100, 0xffffffff,
	0x00008a18, 0x00000100, 0xffffffff,
	0x0000897c, 0x06000100, 0xffffffff,
	0x00008b28, 0x00000100, 0xffffffff,
	0x00009144, 0x00000100, 0xffffffff,
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
	0x0000977c, 0x00000100, 0xffffffff,
	0x00003f80, 0x00000100, 0xffffffff,
	0x0000a210, 0x00000100, 0xffffffff,
	0x0000a214, 0x00000100, 0xffffffff,
	0x000004d8, 0x00000100, 0xffffffff,
	0x00009784, 0x00000100, 0xffffffff,
	0x00009698, 0x00000100, 0xffffffff,
	0x000004d4, 0x00000200, 0xffffffff,
	0x000004d0, 0x00000000, 0xffffffff,
	0x000030cc, 0x00000100, 0xffffffff,
	0x0000d0c0, 0xff000100, 0xffffffff,
	0x0000802c, 0x40000000, 0xffffffff,
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
	0x00009294, 0x00000000, 0xffffffff,
	0x0000802c, 0x40010000, 0xffffffff,
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
	0x00009294, 0x00000000, 0xffffffff,
	0x0000802c, 0xc0000000, 0xffffffff,
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
#define BARTS_MGCG_DEFAULT_LENGTH sizeof(barts_mgcg_default) / (3 * sizeof(u32))

static const u32 barts_mgcg_disable[] =
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
#define BARTS_MGCG_DISABLE_LENGTH sizeof(barts_mgcg_disable) / (3 * sizeof(u32))

static const u32 barts_mgcg_enable[] =
{
	0x0000802c, 0xc0000000, 0xffffffff,
	0x000008f8, 0x00000000, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000001, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000002, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000003, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x00009150, 0x81944000, 0xffffffff
};
#define BARTS_MGCG_ENABLE_LENGTH sizeof(barts_mgcg_enable) / (3 * sizeof(u32))

//********* CAICOS **************//
static const u32 caicos_cgcg_cgls_default[] =
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
#define CAICOS_CGCG_CGLS_DEFAULT_LENGTH sizeof(caicos_cgcg_cgls_default) / (3 * sizeof(u32))

static const u32 caicos_cgcg_cgls_disable[] =
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
	0x00000644, 0x000f7912, 0x001f4180,
	0x00000644, 0x000f3812, 0x001f4180
};
#define CAICOS_CGCG_CGLS_DISABLE_LENGTH sizeof(caicos_cgcg_cgls_disable) / (3 * sizeof(u32))

static const u32 caicos_cgcg_cgls_enable[] =
{
	/* 0x0000c124, 0x84180000, 0x00180000, */
	0x00000644, 0x000f7892, 0x001f4080,
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
#define CAICOS_CGCG_CGLS_ENABLE_LENGTH sizeof(caicos_cgcg_cgls_enable) / (3 * sizeof(u32))

static const u32 caicos_mgcg_default[] =
{
	0x0000802c, 0xc0000000, 0xffffffff,
	0x00005448, 0x00000100, 0xffffffff,
	0x000055e4, 0x00600100, 0xffffffff,
	0x0000160c, 0x00000100, 0xffffffff,
	0x0000c164, 0x00000100, 0xffffffff,
	0x00008a18, 0x00000100, 0xffffffff,
	0x0000897c, 0x06000100, 0xffffffff,
	0x00008b28, 0x00000100, 0xffffffff,
	0x00009144, 0x00000100, 0xffffffff,
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
	0x0000977c, 0x00000100, 0xffffffff,
	0x00003f80, 0x00000100, 0xffffffff,
	0x0000a210, 0x00000100, 0xffffffff,
	0x0000a214, 0x00000100, 0xffffffff,
	0x000004d8, 0x00000100, 0xffffffff,
	0x00009784, 0x00000100, 0xffffffff,
	0x00009698, 0x00000100, 0xffffffff,
	0x000004d4, 0x00000200, 0xffffffff,
	0x000004d0, 0x00000000, 0xffffffff,
	0x000030cc, 0x00000100, 0xffffffff,
	0x0000d0c0, 0xff000100, 0xffffffff,
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
	0x000091e8, 0x00000000, 0xffffffff,
	0x00009294, 0x00000000, 0xffffffff,
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
#define CAICOS_MGCG_DEFAULT_LENGTH sizeof(caicos_mgcg_default) / (3 * sizeof(u32))

static const u32 caicos_mgcg_disable[] =
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
#define CAICOS_MGCG_DISABLE_LENGTH sizeof(caicos_mgcg_disable) / (3 * sizeof(u32))

static const u32 caicos_mgcg_enable[] =
{
	0x0000802c, 0xc0000000, 0xffffffff,
	0x000008f8, 0x00000000, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000001, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000002, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000003, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x00009150, 0x46944040, 0xffffffff
};
#define CAICOS_MGCG_ENABLE_LENGTH sizeof(caicos_mgcg_enable) / (3 * sizeof(u32))

//********* TURKS **************//
static const u32 turks_cgcg_cgls_default[] =
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
#define TURKS_CGCG_CGLS_DEFAULT_LENGTH  sizeof(turks_cgcg_cgls_default) / (3 * sizeof(u32))

static const u32 turks_cgcg_cgls_disable[] =
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
	0x00000644, 0x000f7912, 0x001f4180,
	0x00000644, 0x000f3812, 0x001f4180
};
#define TURKS_CGCG_CGLS_DISABLE_LENGTH sizeof(turks_cgcg_cgls_disable) / (3 * sizeof(u32))

static const u32 turks_cgcg_cgls_enable[] =
{
	/* 0x0000c124, 0x84180000, 0x00180000, */
	0x00000644, 0x000f7892, 0x001f4080,
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
#define TURKS_CGCG_CGLS_ENABLE_LENGTH sizeof(turks_cgcg_cgls_enable) / (3 * sizeof(u32))

// These are the sequences for turks_mgcg_shls
static const u32 turks_mgcg_default[] =
{
	0x0000802c, 0xc0000000, 0xffffffff,
	0x00005448, 0x00000100, 0xffffffff,
	0x000055e4, 0x00600100, 0xffffffff,
	0x0000160c, 0x00000100, 0xffffffff,
	0x0000c164, 0x00000100, 0xffffffff,
	0x00008a18, 0x00000100, 0xffffffff,
	0x0000897c, 0x06000100, 0xffffffff,
	0x00008b28, 0x00000100, 0xffffffff,
	0x00009144, 0x00000100, 0xffffffff,
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
	0x0000977c, 0x00000100, 0xffffffff,
	0x00003f80, 0x00000100, 0xffffffff,
	0x0000a210, 0x00000100, 0xffffffff,
	0x0000a214, 0x00000100, 0xffffffff,
	0x000004d8, 0x00000100, 0xffffffff,
	0x00009784, 0x00000100, 0xffffffff,
	0x00009698, 0x00000100, 0xffffffff,
	0x000004d4, 0x00000200, 0xffffffff,
	0x000004d0, 0x00000000, 0xffffffff,
	0x000030cc, 0x00000100, 0xffffffff,
	0x0000d0c0, 0x00000100, 0xffffffff,
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
	0x00009294, 0x00000000, 0xffffffff,
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
#define TURKS_MGCG_DEFAULT_LENGTH sizeof(turks_mgcg_default) / (3 * sizeof(u32))

static const u32 turks_mgcg_disable[] =
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
#define TURKS_MGCG_DISABLE_LENGTH sizeof(turks_mgcg_disable) / (3 * sizeof(u32))

static const u32 turks_mgcg_enable[] =
{
	0x0000802c, 0xc0000000, 0xffffffff,
	0x000008f8, 0x00000000, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000001, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000002, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x000008f8, 0x00000003, 0xffffffff,
	0x000008fc, 0x00000000, 0xffffffff,
	0x00009150, 0x6e944000, 0xffffffff
};
#define TURKS_MGCG_ENABLE_LENGTH sizeof(turks_mgcg_enable) / (3 * sizeof(u32))

#endif

#ifndef BTC_SYSLS_SEQUENCE
#define BTC_SYSLS_SEQUENCE  100


//********* BARTS **************//
static const u32 barts_sysls_default[] =
{
	/* Register,   Value,     Mask bits */
	0x000055e8, 0x00000000, 0xffffffff,
	0x0000d0bc, 0x00000000, 0xffffffff,
	0x000015c0, 0x000c1401, 0xffffffff,
	0x0000264c, 0x000c0400, 0xffffffff,
	0x00002648, 0x000c0400, 0xffffffff,
	0x00002650, 0x000c0400, 0xffffffff,
	0x000020b8, 0x000c0400, 0xffffffff,
	0x000020bc, 0x000c0400, 0xffffffff,
	0x000020c0, 0x000c0c80, 0xffffffff,
	0x0000f4a0, 0x000000c0, 0xffffffff,
	0x0000f4a4, 0x00680fff, 0xffffffff,
	0x000004c8, 0x00000001, 0xffffffff,
	0x000064ec, 0x00000000, 0xffffffff,
	0x00000c7c, 0x00000000, 0xffffffff,
	0x00006dfc, 0x00000000, 0xffffffff
};
#define BARTS_SYSLS_DEFAULT_LENGTH sizeof(barts_sysls_default) / (3 * sizeof(u32))

static const u32 barts_sysls_disable[] =
{
	0x000055e8, 0x00000000, 0xffffffff,
	0x0000d0bc, 0x00000000, 0xffffffff,
	0x000015c0, 0x00041401, 0xffffffff,
	0x0000264c, 0x00040400, 0xffffffff,
	0x00002648, 0x00040400, 0xffffffff,
	0x00002650, 0x00040400, 0xffffffff,
	0x000020b8, 0x00040400, 0xffffffff,
	0x000020bc, 0x00040400, 0xffffffff,
	0x000020c0, 0x00040c80, 0xffffffff,
	0x0000f4a0, 0x000000c0, 0xffffffff,
	0x0000f4a4, 0x00680000, 0xffffffff,
	0x000004c8, 0x00000001, 0xffffffff,
	0x000064ec, 0x00007ffd, 0xffffffff,
	0x00000c7c, 0x0000ff00, 0xffffffff,
	0x00006dfc, 0x0000007f, 0xffffffff
};
#define BARTS_SYSLS_DISABLE_LENGTH sizeof(barts_sysls_disable) / (3 * sizeof(u32))

static const u32 barts_sysls_enable[] =
{
	0x000055e8, 0x00000001, 0xffffffff,
	0x0000d0bc, 0x00000100, 0xffffffff,
	0x000015c0, 0x000c1401, 0xffffffff,
	0x0000264c, 0x000c0400, 0xffffffff,
	0x00002648, 0x000c0400, 0xffffffff,
	0x00002650, 0x000c0400, 0xffffffff,
	0x000020b8, 0x000c0400, 0xffffffff,
	0x000020bc, 0x000c0400, 0xffffffff,
	0x000020c0, 0x000c0c80, 0xffffffff,
	0x0000f4a0, 0x000000c0, 0xffffffff,
	0x0000f4a4, 0x00680fff, 0xffffffff,
	0x000004c8, 0x00000000, 0xffffffff,
	0x000064ec, 0x00000000, 0xffffffff,
	0x00000c7c, 0x00000000, 0xffffffff,
	0x00006dfc, 0x00000000, 0xffffffff
};
#define BARTS_SYSLS_ENABLE_LENGTH sizeof(barts_sysls_enable) / (3 * sizeof(u32))

//********* CAICOS **************//
static const u32 caicos_sysls_default[] =
{
	0x000055e8, 0x00000000, 0xffffffff,
	0x0000d0bc, 0x00000000, 0xffffffff,
	0x000015c0, 0x000c1401, 0xffffffff,
	0x0000264c, 0x000c0400, 0xffffffff,
	0x00002648, 0x000c0400, 0xffffffff,
	0x00002650, 0x000c0400, 0xffffffff,
	0x000020b8, 0x000c0400, 0xffffffff,
	0x000020bc, 0x000c0400, 0xffffffff,
	0x0000f4a0, 0x000000c0, 0xffffffff,
	0x0000f4a4, 0x00680fff, 0xffffffff,
	0x000004c8, 0x00000001, 0xffffffff,
	0x000064ec, 0x00000000, 0xffffffff,
	0x00000c7c, 0x00000000, 0xffffffff,
	0x00006dfc, 0x00000000, 0xffffffff
};
#define CAICOS_SYSLS_DEFAULT_LENGTH sizeof(caicos_sysls_default) / (3 * sizeof(u32))

static const u32 caicos_sysls_disable[] =
{
	0x000055e8, 0x00000000, 0xffffffff,
	0x0000d0bc, 0x00000000, 0xffffffff,
	0x000015c0, 0x00041401, 0xffffffff,
	0x0000264c, 0x00040400, 0xffffffff,
	0x00002648, 0x00040400, 0xffffffff,
	0x00002650, 0x00040400, 0xffffffff,
	0x000020b8, 0x00040400, 0xffffffff,
	0x000020bc, 0x00040400, 0xffffffff,
	0x0000f4a0, 0x000000c0, 0xffffffff,
	0x0000f4a4, 0x00680000, 0xffffffff,
	0x000004c8, 0x00000001, 0xffffffff,
	0x000064ec, 0x00007ffd, 0xffffffff,
	0x00000c7c, 0x0000ff00, 0xffffffff,
	0x00006dfc, 0x0000007f, 0xffffffff
};
#define CAICOS_SYSLS_DISABLE_LENGTH sizeof(caicos_sysls_disable) / (3 * sizeof(u32))

static const u32 caicos_sysls_enable[] =
{
	0x000055e8, 0x00000001, 0xffffffff,
	0x0000d0bc, 0x00000100, 0xffffffff,
	0x000015c0, 0x000c1401, 0xffffffff,
	0x0000264c, 0x000c0400, 0xffffffff,
	0x00002648, 0x000c0400, 0xffffffff,
	0x00002650, 0x000c0400, 0xffffffff,
	0x000020b8, 0x000c0400, 0xffffffff,
	0x000020bc, 0x000c0400, 0xffffffff,
	0x0000f4a0, 0x000000c0, 0xffffffff,
	0x0000f4a4, 0x00680fff, 0xffffffff,
	0x000064ec, 0x00000000, 0xffffffff,
	0x00000c7c, 0x00000000, 0xffffffff,
	0x00006dfc, 0x00000000, 0xffffffff,
	0x000004c8, 0x00000000, 0xffffffff
};
#define CAICOS_SYSLS_ENABLE_LENGTH sizeof(caicos_sysls_enable) / (3 * sizeof(u32))

//********* TURKS **************//
static const u32 turks_sysls_default[] =
{
	0x000055e8, 0x00000000, 0xffffffff,
	0x0000d0bc, 0x00000000, 0xffffffff,
	0x000015c0, 0x000c1401, 0xffffffff,
	0x0000264c, 0x000c0400, 0xffffffff,
	0x00002648, 0x000c0400, 0xffffffff,
	0x00002650, 0x000c0400, 0xffffffff,
	0x000020b8, 0x000c0400, 0xffffffff,
	0x000020bc, 0x000c0400, 0xffffffff,
	0x000020c0, 0x000c0c80, 0xffffffff,
	0x0000f4a0, 0x000000c0, 0xffffffff,
	0x0000f4a4, 0x00680fff, 0xffffffff,
	0x000004c8, 0x00000001, 0xffffffff,
	0x000064ec, 0x00000000, 0xffffffff,
	0x00000c7c, 0x00000000, 0xffffffff,
	0x00006dfc, 0x00000000, 0xffffffff
};
#define TURKS_SYSLS_DEFAULT_LENGTH sizeof(turks_sysls_default) / (3 * sizeof(u32))

static const u32 turks_sysls_disable[] =
{
	0x000055e8, 0x00000000, 0xffffffff,
	0x0000d0bc, 0x00000000, 0xffffffff,
	0x000015c0, 0x00041401, 0xffffffff,
	0x0000264c, 0x00040400, 0xffffffff,
	0x00002648, 0x00040400, 0xffffffff,
	0x00002650, 0x00040400, 0xffffffff,
	0x000020b8, 0x00040400, 0xffffffff,
	0x000020bc, 0x00040400, 0xffffffff,
	0x000020c0, 0x00040c80, 0xffffffff,
	0x0000f4a0, 0x000000c0, 0xffffffff,
	0x0000f4a4, 0x00680000, 0xffffffff,
	0x000004c8, 0x00000001, 0xffffffff,
	0x000064ec, 0x00007ffd, 0xffffffff,
	0x00000c7c, 0x0000ff00, 0xffffffff,
	0x00006dfc, 0x0000007f, 0xffffffff
};
#define TURKS_SYSLS_DISABLE_LENGTH sizeof(turks_sysls_disable) / (3 * sizeof(u32))

static const u32 turks_sysls_enable[] =
{
	0x000055e8, 0x00000001, 0xffffffff,
	0x0000d0bc, 0x00000100, 0xffffffff,
	0x000015c0, 0x000c1401, 0xffffffff,
	0x0000264c, 0x000c0400, 0xffffffff,
	0x00002648, 0x000c0400, 0xffffffff,
	0x00002650, 0x000c0400, 0xffffffff,
	0x000020b8, 0x000c0400, 0xffffffff,
	0x000020bc, 0x000c0400, 0xffffffff,
	0x000020c0, 0x000c0c80, 0xffffffff,
	0x0000f4a0, 0x000000c0, 0xffffffff,
	0x0000f4a4, 0x00680fff, 0xffffffff,
	0x000004c8, 0x00000000, 0xffffffff,
	0x000064ec, 0x00000000, 0xffffffff,
	0x00000c7c, 0x00000000, 0xffffffff,
	0x00006dfc, 0x00000000, 0xffffffff
};
#define TURKS_SYSLS_ENABLE_LENGTH sizeof(turks_sysls_enable) / (3 * sizeof(u32))

#endif

u32 btc_valid_sclk[40] =
{
	5000,   10000,  15000,  20000,  25000,  30000,  35000,  40000,  45000,  50000,
	55000,  60000,  65000,  70000,  75000,  80000,  85000,  90000,  95000,  100000,
	105000, 110000, 11500,  120000, 125000, 130000, 135000, 140000, 145000, 150000,
	155000, 160000, 165000, 170000, 175000, 180000, 185000, 190000, 195000, 200000
};

static const struct radeon_blacklist_clocks btc_blacklist_clocks[] =
{
        { 10000, 30000, RADEON_SCLK_UP },
        { 15000, 30000, RADEON_SCLK_UP },
        { 20000, 30000, RADEON_SCLK_UP },
        { 25000, 30000, RADEON_SCLK_UP }
};

void btc_apply_voltage_dependency_rules(struct radeon_clock_voltage_dependency_table *table,
					u32 clock, u16 max_voltage, u16 *voltage)
{
	u32 i;

	if ((table == NULL) || (table->count == 0))
		return;

	for (i= 0; i < table->count; i++) {
		if (clock <= table->entries[i].clk) {
			if (*voltage < table->entries[i].v)
				*voltage = (u16)((table->entries[i].v < max_voltage) ?
						  table->entries[i].v : max_voltage);
			return;
		}
	}

	*voltage = (*voltage > max_voltage) ? *voltage : max_voltage;
}

static u32 btc_find_valid_clock(struct radeon_clock_array *clocks,
				u32 max_clock, u32 requested_clock)
{
	unsigned int i;

	if ((clocks == NULL) || (clocks->count == 0))
		return (requested_clock < max_clock) ? requested_clock : max_clock;

	for (i = 0; i < clocks->count; i++) {
		if (clocks->values[i] >= requested_clock)
			return (clocks->values[i] < max_clock) ? clocks->values[i] : max_clock;
	}

	return (clocks->values[clocks->count - 1] < max_clock) ?
		clocks->values[clocks->count - 1] : max_clock;
}

static u32 btc_get_valid_mclk(struct radeon_device *rdev,
			      u32 max_mclk, u32 requested_mclk)
{
	return btc_find_valid_clock(&rdev->pm.dpm.dyn_state.valid_mclk_values,
				    max_mclk, requested_mclk);
}

static u32 btc_get_valid_sclk(struct radeon_device *rdev,
			      u32 max_sclk, u32 requested_sclk)
{
	return btc_find_valid_clock(&rdev->pm.dpm.dyn_state.valid_sclk_values,
				    max_sclk, requested_sclk);
}

void btc_skip_blacklist_clocks(struct radeon_device *rdev,
			       const u32 max_sclk, const u32 max_mclk,
			       u32 *sclk, u32 *mclk)
{
	int i, num_blacklist_clocks;

	if ((sclk == NULL) || (mclk == NULL))
		return;

	num_blacklist_clocks = ARRAY_SIZE(btc_blacklist_clocks);

	for (i = 0; i < num_blacklist_clocks; i++) {
		if ((btc_blacklist_clocks[i].sclk == *sclk) &&
		    (btc_blacklist_clocks[i].mclk == *mclk))
			break;
	}

	if (i < num_blacklist_clocks) {
		if (btc_blacklist_clocks[i].action == RADEON_SCLK_UP) {
			*sclk = btc_get_valid_sclk(rdev, max_sclk, *sclk + 1);

			if (*sclk < max_sclk)
				btc_skip_blacklist_clocks(rdev, max_sclk, max_mclk, sclk, mclk);
		}
	}
}

void btc_adjust_clock_combinations(struct radeon_device *rdev,
				   const struct radeon_clock_and_voltage_limits *max_limits,
				   struct rv7xx_pl *pl)
{

	if ((pl->mclk == 0) || (pl->sclk == 0))
		return;

	if (pl->mclk == pl->sclk)
		return;

	if (pl->mclk > pl->sclk) {
		if (((pl->mclk + (pl->sclk - 1)) / pl->sclk) > rdev->pm.dpm.dyn_state.mclk_sclk_ratio)
			pl->sclk = btc_get_valid_sclk(rdev,
						      max_limits->sclk,
						      (pl->mclk +
						       (rdev->pm.dpm.dyn_state.mclk_sclk_ratio - 1)) /
						      rdev->pm.dpm.dyn_state.mclk_sclk_ratio);
	} else {
		if ((pl->sclk - pl->mclk) > rdev->pm.dpm.dyn_state.sclk_mclk_delta)
			pl->mclk = btc_get_valid_mclk(rdev,
						      max_limits->mclk,
						      pl->sclk -
						      rdev->pm.dpm.dyn_state.sclk_mclk_delta);
	}
}

static u16 btc_find_voltage(struct atom_voltage_table *table, u16 voltage)
{
	unsigned int i;

	for (i = 0; i < table->count; i++) {
		if (voltage <= table->entries[i].value)
			return table->entries[i].value;
	}

	return table->entries[table->count - 1].value;
}

void btc_apply_voltage_delta_rules(struct radeon_device *rdev,
				   u16 max_vddc, u16 max_vddci,
				   u16 *vddc, u16 *vddci)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	u16 new_voltage;

	if ((0 == *vddc) || (0 == *vddci))
		return;

	if (*vddc > *vddci) {
		if ((*vddc - *vddci) > rdev->pm.dpm.dyn_state.vddc_vddci_delta) {
			new_voltage = btc_find_voltage(&eg_pi->vddci_voltage_table,
						       (*vddc - rdev->pm.dpm.dyn_state.vddc_vddci_delta));
			*vddci = (new_voltage < max_vddci) ? new_voltage : max_vddci;
		}
	} else {
		if ((*vddci - *vddc) > rdev->pm.dpm.dyn_state.vddc_vddci_delta) {
			new_voltage = btc_find_voltage(&eg_pi->vddc_voltage_table,
						       (*vddci - rdev->pm.dpm.dyn_state.vddc_vddci_delta));
			*vddc = (new_voltage < max_vddc) ? new_voltage : max_vddc;
		}
	}
}

static void btc_enable_bif_dynamic_pcie_gen2(struct radeon_device *rdev,
					     bool enable)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 tmp, bif;

	tmp = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
	if (enable) {
		if ((tmp & LC_OTHER_SIDE_EVER_SENT_GEN2) &&
		    (tmp & LC_OTHER_SIDE_SUPPORTS_GEN2)) {
			if (!pi->boot_in_gen2) {
				bif = RREG32(CG_BIF_REQ_AND_RSP) & ~CG_CLIENT_REQ_MASK;
				bif |= CG_CLIENT_REQ(0xd);
				WREG32(CG_BIF_REQ_AND_RSP, bif);

				tmp &= ~LC_HW_VOLTAGE_IF_CONTROL_MASK;
				tmp |= LC_HW_VOLTAGE_IF_CONTROL(1);
				tmp |= LC_GEN2_EN_STRAP;

				tmp |= LC_CLR_FAILED_SPD_CHANGE_CNT;
				WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, tmp);
				udelay(10);
				tmp &= ~LC_CLR_FAILED_SPD_CHANGE_CNT;
				WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, tmp);
			}
		}
	} else {
		if ((tmp & LC_OTHER_SIDE_EVER_SENT_GEN2) ||
		    (tmp & LC_OTHER_SIDE_SUPPORTS_GEN2)) {
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

static void btc_enable_dynamic_pcie_gen2(struct radeon_device *rdev,
					 bool enable)
{
	btc_enable_bif_dynamic_pcie_gen2(rdev, enable);

	if (enable)
		WREG32_P(GENERAL_PWRMGT, ENABLE_GEN2PCIE, ~ENABLE_GEN2PCIE);
	else
		WREG32_P(GENERAL_PWRMGT, 0, ~ENABLE_GEN2PCIE);
}

static int btc_disable_ulv(struct radeon_device *rdev)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	if (eg_pi->ulv.supported) {
		if (rv770_send_msg_to_smc(rdev, PPSMC_MSG_DisableULV) != PPSMC_Result_OK)
			return -EINVAL;
	}
	return 0;
}

static int btc_populate_ulv_state(struct radeon_device *rdev,
				  RV770_SMC_STATETABLE *table)
{
	int ret = -EINVAL;
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct rv7xx_pl *ulv_pl = eg_pi->ulv.pl;

	if (ulv_pl->vddc) {
		ret = cypress_convert_power_level_to_smc(rdev,
							 ulv_pl,
							 &table->ULVState.levels[0],
							 PPSMC_DISPLAY_WATERMARK_LOW);
		if (ret == 0) {
			table->ULVState.levels[0].arbValue = MC_CG_ARB_FREQ_F0;
			table->ULVState.levels[0].ACIndex = 1;

			table->ULVState.levels[1] = table->ULVState.levels[0];
			table->ULVState.levels[2] = table->ULVState.levels[0];

			table->ULVState.flags |= PPSMC_SWSTATE_FLAG_DC;

			WREG32(CG_ULV_CONTROL, BTC_CGULVCONTROL_DFLT);
			WREG32(CG_ULV_PARAMETER, BTC_CGULVPARAMETER_DFLT);
		}
	}

	return ret;
}

static int btc_populate_smc_acpi_state(struct radeon_device *rdev,
				       RV770_SMC_STATETABLE *table)
{
	int ret = cypress_populate_smc_acpi_state(rdev, table);

	if (ret == 0) {
		table->ACPIState.levels[0].ACIndex = 0;
		table->ACPIState.levels[1].ACIndex = 0;
		table->ACPIState.levels[2].ACIndex = 0;
	}

	return ret;
}

void btc_program_mgcg_hw_sequence(struct radeon_device *rdev,
				  const u32 *sequence, u32 count)
{
	u32 i, length = count * 3;
	u32 tmp;

	for (i = 0; i < length; i+=3) {
		tmp = RREG32(sequence[i]);
		tmp &= ~sequence[i+2];
		tmp |= sequence[i+1] & sequence[i+2];
		WREG32(sequence[i], tmp);
	}
}

static void btc_cg_clock_gating_default(struct radeon_device *rdev)
{
	u32 count;
	const u32 *p = NULL;

	if (rdev->family == CHIP_BARTS) {
		p = (const u32 *)&barts_cgcg_cgls_default;
		count = BARTS_CGCG_CGLS_DEFAULT_LENGTH;
	} else if (rdev->family == CHIP_TURKS) {
		p = (const u32 *)&turks_cgcg_cgls_default;
		count = TURKS_CGCG_CGLS_DEFAULT_LENGTH;
	} else if (rdev->family == CHIP_CAICOS) {
		p = (const u32 *)&caicos_cgcg_cgls_default;
		count = CAICOS_CGCG_CGLS_DEFAULT_LENGTH;
	} else
		return;

	btc_program_mgcg_hw_sequence(rdev, p, count);
}

static void btc_cg_clock_gating_enable(struct radeon_device *rdev,
				       bool enable)
{
	u32 count;
	const u32 *p = NULL;

	if (enable) {
		if (rdev->family == CHIP_BARTS) {
			p = (const u32 *)&barts_cgcg_cgls_enable;
			count = BARTS_CGCG_CGLS_ENABLE_LENGTH;
		} else if (rdev->family == CHIP_TURKS) {
			p = (const u32 *)&turks_cgcg_cgls_enable;
			count = TURKS_CGCG_CGLS_ENABLE_LENGTH;
		} else if (rdev->family == CHIP_CAICOS) {
			p = (const u32 *)&caicos_cgcg_cgls_enable;
			count = CAICOS_CGCG_CGLS_ENABLE_LENGTH;
		} else
			return;
	} else {
		if (rdev->family == CHIP_BARTS) {
			p = (const u32 *)&barts_cgcg_cgls_disable;
			count = BARTS_CGCG_CGLS_DISABLE_LENGTH;
		} else if (rdev->family == CHIP_TURKS) {
			p = (const u32 *)&turks_cgcg_cgls_disable;
			count = TURKS_CGCG_CGLS_DISABLE_LENGTH;
		} else if (rdev->family == CHIP_CAICOS) {
			p = (const u32 *)&caicos_cgcg_cgls_disable;
			count = CAICOS_CGCG_CGLS_DISABLE_LENGTH;
		} else
			return;
	}

	btc_program_mgcg_hw_sequence(rdev, p, count);
}

static void btc_mg_clock_gating_default(struct radeon_device *rdev)
{
	u32 count;
	const u32 *p = NULL;

	if (rdev->family == CHIP_BARTS) {
		p = (const u32 *)&barts_mgcg_default;
		count = BARTS_MGCG_DEFAULT_LENGTH;
	} else if (rdev->family == CHIP_TURKS) {
		p = (const u32 *)&turks_mgcg_default;
		count = TURKS_MGCG_DEFAULT_LENGTH;
	} else if (rdev->family == CHIP_CAICOS) {
		p = (const u32 *)&caicos_mgcg_default;
		count = CAICOS_MGCG_DEFAULT_LENGTH;
	} else
		return;

	btc_program_mgcg_hw_sequence(rdev, p, count);
}

static void btc_mg_clock_gating_enable(struct radeon_device *rdev,
				       bool enable)
{
	u32 count;
	const u32 *p = NULL;

	if (enable) {
		if (rdev->family == CHIP_BARTS) {
			p = (const u32 *)&barts_mgcg_enable;
			count = BARTS_MGCG_ENABLE_LENGTH;
		} else if (rdev->family == CHIP_TURKS) {
			p = (const u32 *)&turks_mgcg_enable;
			count = TURKS_MGCG_ENABLE_LENGTH;
		} else if (rdev->family == CHIP_CAICOS) {
			p = (const u32 *)&caicos_mgcg_enable;
			count = CAICOS_MGCG_ENABLE_LENGTH;
		} else
			return;
	} else {
		if (rdev->family == CHIP_BARTS) {
			p = (const u32 *)&barts_mgcg_disable[0];
			count = BARTS_MGCG_DISABLE_LENGTH;
		} else if (rdev->family == CHIP_TURKS) {
			p = (const u32 *)&turks_mgcg_disable[0];
			count = TURKS_MGCG_DISABLE_LENGTH;
		} else if (rdev->family == CHIP_CAICOS) {
			p = (const u32 *)&caicos_mgcg_disable[0];
			count = CAICOS_MGCG_DISABLE_LENGTH;
		} else
			return;
	}

	btc_program_mgcg_hw_sequence(rdev, p, count);
}

static void btc_ls_clock_gating_default(struct radeon_device *rdev)
{
	u32 count;
	const u32 *p = NULL;

	if (rdev->family == CHIP_BARTS) {
		p = (const u32 *)&barts_sysls_default;
		count = BARTS_SYSLS_DEFAULT_LENGTH;
	} else if (rdev->family == CHIP_TURKS) {
		p = (const u32 *)&turks_sysls_default;
		count = TURKS_SYSLS_DEFAULT_LENGTH;
	} else if (rdev->family == CHIP_CAICOS) {
		p = (const u32 *)&caicos_sysls_default;
		count = CAICOS_SYSLS_DEFAULT_LENGTH;
	} else
		return;

	btc_program_mgcg_hw_sequence(rdev, p, count);
}

static void btc_ls_clock_gating_enable(struct radeon_device *rdev,
				       bool enable)
{
	u32 count;
	const u32 *p = NULL;

	if (enable) {
		if (rdev->family == CHIP_BARTS) {
			p = (const u32 *)&barts_sysls_enable;
			count = BARTS_SYSLS_ENABLE_LENGTH;
		} else if (rdev->family == CHIP_TURKS) {
			p = (const u32 *)&turks_sysls_enable;
			count = TURKS_SYSLS_ENABLE_LENGTH;
		} else if (rdev->family == CHIP_CAICOS) {
			p = (const u32 *)&caicos_sysls_enable;
			count = CAICOS_SYSLS_ENABLE_LENGTH;
		} else
			return;
	} else {
		if (rdev->family == CHIP_BARTS) {
			p = (const u32 *)&barts_sysls_disable;
			count = BARTS_SYSLS_DISABLE_LENGTH;
		} else if (rdev->family == CHIP_TURKS) {
			p = (const u32 *)&turks_sysls_disable;
			count = TURKS_SYSLS_DISABLE_LENGTH;
		} else if (rdev->family == CHIP_CAICOS) {
			p = (const u32 *)&caicos_sysls_disable;
			count = CAICOS_SYSLS_DISABLE_LENGTH;
		} else
			return;
	}

	btc_program_mgcg_hw_sequence(rdev, p, count);
}

bool btc_dpm_enabled(struct radeon_device *rdev)
{
	if (rv770_is_smc_running(rdev))
		return true;
	else
		return false;
}

static int btc_init_smc_table(struct radeon_device *rdev,
			      struct radeon_ps *radeon_boot_state)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	RV770_SMC_STATETABLE *table = &pi->smc_statetable;
	int ret;

	memset(table, 0, sizeof(RV770_SMC_STATETABLE));

	cypress_populate_smc_voltage_tables(rdev, table);

	switch (rdev->pm.int_thermal_type) {
        case THERMAL_TYPE_EVERGREEN:
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

	ret = cypress_populate_smc_initial_state(rdev, radeon_boot_state, table);
	if (ret)
		return ret;

	if (eg_pi->sclk_deep_sleep)
		WREG32_P(SCLK_PSKIP_CNTL, PSKIP_ON_ALLOW_STOP_HI(32),
			 ~PSKIP_ON_ALLOW_STOP_HI_MASK);

	ret = btc_populate_smc_acpi_state(rdev, table);
	if (ret)
		return ret;

	if (eg_pi->ulv.supported) {
		ret = btc_populate_ulv_state(rdev, table);
		if (ret)
			eg_pi->ulv.supported = false;
	}

	table->driverState = table->initialState;

	return rv770_copy_bytes_to_smc(rdev,
				       pi->state_table_start,
				       (u8 *)table,
				       sizeof(RV770_SMC_STATETABLE),
				       pi->sram_end);
}

static void btc_set_at_for_uvd(struct radeon_device *rdev,
			       struct radeon_ps *radeon_new_state)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	int idx = 0;

	if (r600_is_uvd_state(radeon_new_state->class, radeon_new_state->class2))
		idx = 1;

	if ((idx == 1) && !eg_pi->smu_uvd_hs) {
		pi->rlp = 10;
		pi->rmp = 100;
		pi->lhp = 100;
		pi->lmp = 10;
	} else {
		pi->rlp = eg_pi->ats[idx].rlp;
		pi->rmp = eg_pi->ats[idx].rmp;
		pi->lhp = eg_pi->ats[idx].lhp;
		pi->lmp = eg_pi->ats[idx].lmp;
	}

}

void btc_notify_uvd_to_smc(struct radeon_device *rdev,
			   struct radeon_ps *radeon_new_state)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	if (r600_is_uvd_state(radeon_new_state->class, radeon_new_state->class2)) {
		rv770_write_smc_soft_register(rdev,
					      RV770_SMC_SOFT_REGISTER_uvd_enabled, 1);
		eg_pi->uvd_enabled = true;
	} else {
		rv770_write_smc_soft_register(rdev,
					      RV770_SMC_SOFT_REGISTER_uvd_enabled, 0);
		eg_pi->uvd_enabled = false;
	}
}

int btc_reset_to_default(struct radeon_device *rdev)
{
	if (rv770_send_msg_to_smc(rdev, PPSMC_MSG_ResetToDefaults) != PPSMC_Result_OK)
		return -EINVAL;

	return 0;
}

static void btc_stop_smc(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (((RREG32(LB_SYNC_RESET_SEL) & LB_SYNC_RESET_SEL_MASK) >> LB_SYNC_RESET_SEL_SHIFT) != 1)
			break;
		udelay(1);
	}
	udelay(100);

	r7xx_stop_smc(rdev);
}

void btc_read_arb_registers(struct radeon_device *rdev)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct evergreen_arb_registers *arb_registers =
		&eg_pi->bootup_arb_registers;

	arb_registers->mc_arb_dram_timing = RREG32(MC_ARB_DRAM_TIMING);
	arb_registers->mc_arb_dram_timing2 = RREG32(MC_ARB_DRAM_TIMING2);
	arb_registers->mc_arb_rfsh_rate = RREG32(MC_ARB_RFSH_RATE);
	arb_registers->mc_arb_burst_time = RREG32(MC_ARB_BURST_TIME);
}


static void btc_set_arb0_registers(struct radeon_device *rdev,
				   struct evergreen_arb_registers *arb_registers)
{
	u32 val;

	WREG32(MC_ARB_DRAM_TIMING,  arb_registers->mc_arb_dram_timing);
	WREG32(MC_ARB_DRAM_TIMING2, arb_registers->mc_arb_dram_timing2);

	val = (arb_registers->mc_arb_rfsh_rate & POWERMODE0_MASK) >>
		POWERMODE0_SHIFT;
	WREG32_P(MC_ARB_RFSH_RATE, POWERMODE0(val), ~POWERMODE0_MASK);

	val = (arb_registers->mc_arb_burst_time & STATE0_MASK) >>
		STATE0_SHIFT;
	WREG32_P(MC_ARB_BURST_TIME, STATE0(val), ~STATE0_MASK);
}

static void btc_set_boot_state_timing(struct radeon_device *rdev)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	if (eg_pi->ulv.supported)
		btc_set_arb0_registers(rdev, &eg_pi->bootup_arb_registers);
}

static bool btc_is_state_ulv_compatible(struct radeon_device *rdev,
					struct radeon_ps *radeon_state)
{
	struct rv7xx_ps *state = rv770_get_ps(radeon_state);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct rv7xx_pl *ulv_pl = eg_pi->ulv.pl;

	if (state->low.mclk != ulv_pl->mclk)
		return false;

	if (state->low.vddci != ulv_pl->vddci)
		return false;

	/* XXX check minclocks, etc. */

	return true;
}


static int btc_set_ulv_dram_timing(struct radeon_device *rdev)
{
	u32 val;
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct rv7xx_pl *ulv_pl = eg_pi->ulv.pl;

	radeon_atom_set_engine_dram_timings(rdev,
					    ulv_pl->sclk,
					    ulv_pl->mclk);

	val = rv770_calculate_memory_refresh_rate(rdev, ulv_pl->sclk);
	WREG32_P(MC_ARB_RFSH_RATE, POWERMODE0(val), ~POWERMODE0_MASK);

	val = cypress_calculate_burst_time(rdev, ulv_pl->sclk, ulv_pl->mclk);
	WREG32_P(MC_ARB_BURST_TIME, STATE0(val), ~STATE0_MASK);

	return 0;
}

static int btc_enable_ulv(struct radeon_device *rdev)
{
	if (rv770_send_msg_to_smc(rdev, PPSMC_MSG_EnableULV) != PPSMC_Result_OK)
		return -EINVAL;

	return 0;
}

static int btc_set_power_state_conditionally_enable_ulv(struct radeon_device *rdev,
							struct radeon_ps *radeon_new_state)
{
	int ret = 0;
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	if (eg_pi->ulv.supported) {
		if (btc_is_state_ulv_compatible(rdev, radeon_new_state)) {
			// Set ARB[0] to reflect the DRAM timing needed for ULV.
			ret = btc_set_ulv_dram_timing(rdev);
			if (ret == 0)
				ret = btc_enable_ulv(rdev);
		}
	}

	return ret;
}

static bool btc_check_s0_mc_reg_index(u16 in_reg, u16 *out_reg)
{
	bool result = true;

	switch (in_reg) {
	case MC_SEQ_RAS_TIMING >> 2:
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
        default:
		result = false;
		break;
	}

	return result;
}

static void btc_set_valid_flag(struct evergreen_mc_reg_table *table)
{
	u8 i, j;

	for (i = 0; i < table->last; i++) {
		for (j = 1; j < table->num_entries; j++) {
			if (table->mc_reg_table_entry[j-1].mc_data[i] !=
			    table->mc_reg_table_entry[j].mc_data[i]) {
				table->valid_flag |= (1 << i);
				break;
			}
		}
	}
}

static int btc_set_mc_special_registers(struct radeon_device *rdev,
					struct evergreen_mc_reg_table *table)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u8 i, j, k;
	u32 tmp;

	for (i = 0, j = table->last; i < table->last; i++) {
		switch (table->mc_reg_address[i].s1) {
		case MC_SEQ_MISC1 >> 2:
			tmp = RREG32(MC_PMG_CMD_EMRS);
			table->mc_reg_address[j].s1 = MC_PMG_CMD_EMRS >> 2;
			table->mc_reg_address[j].s0 = MC_SEQ_PMG_CMD_EMRS_LP >> 2;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					((tmp & 0xffff0000)) |
					((table->mc_reg_table_entry[k].mc_data[i] & 0xffff0000) >> 16);
			}
			j++;

			if (j > SMC_EVERGREEN_MC_REGISTER_ARRAY_SIZE)
				return -EINVAL;

			tmp = RREG32(MC_PMG_CMD_MRS);
			table->mc_reg_address[j].s1 = MC_PMG_CMD_MRS >> 2;
			table->mc_reg_address[j].s0 = MC_SEQ_PMG_CMD_MRS_LP >> 2;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					(tmp & 0xffff0000) |
					(table->mc_reg_table_entry[k].mc_data[i] & 0x0000ffff);
				if (!pi->mem_gddr5)
					table->mc_reg_table_entry[k].mc_data[j] |= 0x100;
			}
			j++;

			if (j > SMC_EVERGREEN_MC_REGISTER_ARRAY_SIZE)
				return -EINVAL;
			break;
		case MC_SEQ_RESERVE_M >> 2:
			tmp = RREG32(MC_PMG_CMD_MRS1);
			table->mc_reg_address[j].s1 = MC_PMG_CMD_MRS1 >> 2;
			table->mc_reg_address[j].s0 = MC_SEQ_PMG_CMD_MRS1_LP >> 2;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					(tmp & 0xffff0000) |
					(table->mc_reg_table_entry[k].mc_data[i] & 0x0000ffff);
			}
			j++;

			if (j > SMC_EVERGREEN_MC_REGISTER_ARRAY_SIZE)
				return -EINVAL;
			break;
		default:
			break;
		}
	}

	table->last = j;

	return 0;
}

static void btc_set_s0_mc_reg_index(struct evergreen_mc_reg_table *table)
{
	u32 i;
	u16 address;

	for (i = 0; i < table->last; i++) {
		table->mc_reg_address[i].s0 =
			btc_check_s0_mc_reg_index(table->mc_reg_address[i].s1, &address) ?
			address : table->mc_reg_address[i].s1;
	}
}

static int btc_copy_vbios_mc_reg_table(struct atom_mc_reg_table *table,
				       struct evergreen_mc_reg_table *eg_table)
{
	u8 i, j;

	if (table->last > SMC_EVERGREEN_MC_REGISTER_ARRAY_SIZE)
		return -EINVAL;

	if (table->num_entries > MAX_AC_TIMING_ENTRIES)
		return -EINVAL;

	for (i = 0; i < table->last; i++)
		eg_table->mc_reg_address[i].s1 = table->mc_reg_address[i].s1;
	eg_table->last = table->last;

	for (i = 0; i < table->num_entries; i++) {
		eg_table->mc_reg_table_entry[i].mclk_max =
			table->mc_reg_table_entry[i].mclk_max;
		for(j = 0; j < table->last; j++)
			eg_table->mc_reg_table_entry[i].mc_data[j] =
				table->mc_reg_table_entry[i].mc_data[j];
	}
	eg_table->num_entries = table->num_entries;

	return 0;
}

static int btc_initialize_mc_reg_table(struct radeon_device *rdev)
{
	int ret;
	struct atom_mc_reg_table *table;
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct evergreen_mc_reg_table *eg_table = &eg_pi->mc_reg_table;
	u8 module_index = rv770_get_memory_module_index(rdev);

	table = kzalloc(sizeof(struct atom_mc_reg_table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	/* Program additional LP registers that are no longer programmed by VBIOS */
	WREG32(MC_SEQ_RAS_TIMING_LP, RREG32(MC_SEQ_RAS_TIMING));
	WREG32(MC_SEQ_CAS_TIMING_LP, RREG32(MC_SEQ_CAS_TIMING));
	WREG32(MC_SEQ_MISC_TIMING_LP, RREG32(MC_SEQ_MISC_TIMING));
	WREG32(MC_SEQ_MISC_TIMING2_LP, RREG32(MC_SEQ_MISC_TIMING2));
	WREG32(MC_SEQ_RD_CTL_D0_LP, RREG32(MC_SEQ_RD_CTL_D0));
	WREG32(MC_SEQ_RD_CTL_D1_LP, RREG32(MC_SEQ_RD_CTL_D1));
	WREG32(MC_SEQ_WR_CTL_D0_LP, RREG32(MC_SEQ_WR_CTL_D0));
	WREG32(MC_SEQ_WR_CTL_D1_LP, RREG32(MC_SEQ_WR_CTL_D1));
	WREG32(MC_SEQ_PMG_CMD_EMRS_LP, RREG32(MC_PMG_CMD_EMRS));
	WREG32(MC_SEQ_PMG_CMD_MRS_LP, RREG32(MC_PMG_CMD_MRS));
	WREG32(MC_SEQ_PMG_CMD_MRS1_LP, RREG32(MC_PMG_CMD_MRS1));

	ret = radeon_atom_init_mc_reg_table(rdev, module_index, table);

	if (ret)
		goto init_mc_done;

	ret = btc_copy_vbios_mc_reg_table(table, eg_table);

	if (ret)
		goto init_mc_done;

	btc_set_s0_mc_reg_index(eg_table);
	ret = btc_set_mc_special_registers(rdev, eg_table);

	if (ret)
		goto init_mc_done;

	btc_set_valid_flag(eg_table);

init_mc_done:
	kfree(table);

	return ret;
}

static void btc_init_stutter_mode(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 tmp;

	if (pi->mclk_stutter_mode_threshold) {
		if (pi->mem_gddr5) {
			tmp = RREG32(MC_PMG_AUTO_CFG);
			if ((0x200 & tmp) == 0) {
				tmp = (tmp & 0xfffffc0b) | 0x204;
				WREG32(MC_PMG_AUTO_CFG, tmp);
			}
		}
	}
}

static void btc_apply_state_adjust_rules(struct radeon_device *rdev,
					 struct radeon_ps *rps)
{
	struct rv7xx_ps *ps = rv770_get_ps(rps);
	struct radeon_clock_and_voltage_limits *max_limits;
	bool disable_mclk_switching;
	u32 mclk, sclk;
	u16 vddc, vddci;

	if (rdev->pm.dpm.new_active_crtc_count > 1)
		disable_mclk_switching = true;
	else
		disable_mclk_switching = false;

	if (rdev->pm.dpm.ac_power)
		max_limits = &rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac;
	else
		max_limits = &rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc;

	if (rdev->pm.dpm.ac_power == false) {
		if (ps->high.mclk > max_limits->mclk)
			ps->high.mclk = max_limits->mclk;
		if (ps->high.sclk > max_limits->sclk)
			ps->high.sclk = max_limits->sclk;
		if (ps->high.vddc > max_limits->vddc)
			ps->high.vddc = max_limits->vddc;
		if (ps->high.vddci > max_limits->vddci)
			ps->high.vddci = max_limits->vddci;

		if (ps->medium.mclk > max_limits->mclk)
			ps->medium.mclk = max_limits->mclk;
		if (ps->medium.sclk > max_limits->sclk)
			ps->medium.sclk = max_limits->sclk;
		if (ps->medium.vddc > max_limits->vddc)
			ps->medium.vddc = max_limits->vddc;
		if (ps->medium.vddci > max_limits->vddci)
			ps->medium.vddci = max_limits->vddci;

		if (ps->low.mclk > max_limits->mclk)
			ps->low.mclk = max_limits->mclk;
		if (ps->low.sclk > max_limits->sclk)
			ps->low.sclk = max_limits->sclk;
		if (ps->low.vddc > max_limits->vddc)
			ps->low.vddc = max_limits->vddc;
		if (ps->low.vddci > max_limits->vddci)
			ps->low.vddci = max_limits->vddci;
	}

	/* XXX validate the min clocks required for display */

	if (disable_mclk_switching) {
		sclk = ps->low.sclk;
		mclk = ps->high.mclk;
		vddc = ps->low.vddc;
		vddci = ps->high.vddci;
	} else {
		sclk = ps->low.sclk;
		mclk = ps->low.mclk;
		vddc = ps->low.vddc;
		vddci = ps->low.vddci;
	}

	/* adjusted low state */
	ps->low.sclk = sclk;
	ps->low.mclk = mclk;
	ps->low.vddc = vddc;
	ps->low.vddci = vddci;

	btc_skip_blacklist_clocks(rdev, max_limits->sclk, max_limits->mclk,
				  &ps->low.sclk, &ps->low.mclk);

	/* adjusted medium, high states */
	if (ps->medium.sclk < ps->low.sclk)
		ps->medium.sclk = ps->low.sclk;
	if (ps->medium.vddc < ps->low.vddc)
		ps->medium.vddc = ps->low.vddc;
	if (ps->high.sclk < ps->medium.sclk)
		ps->high.sclk = ps->medium.sclk;
	if (ps->high.vddc < ps->medium.vddc)
		ps->high.vddc = ps->medium.vddc;

	if (disable_mclk_switching) {
		mclk = ps->low.mclk;
		if (mclk < ps->medium.mclk)
			mclk = ps->medium.mclk;
		if (mclk < ps->high.mclk)
			mclk = ps->high.mclk;
		ps->low.mclk = mclk;
		ps->low.vddci = vddci;
		ps->medium.mclk = mclk;
		ps->medium.vddci = vddci;
		ps->high.mclk = mclk;
		ps->high.vddci = vddci;
	} else {
		if (ps->medium.mclk < ps->low.mclk)
			ps->medium.mclk = ps->low.mclk;
		if (ps->medium.vddci < ps->low.vddci)
			ps->medium.vddci = ps->low.vddci;
		if (ps->high.mclk < ps->medium.mclk)
			ps->high.mclk = ps->medium.mclk;
		if (ps->high.vddci < ps->medium.vddci)
			ps->high.vddci = ps->medium.vddci;
	}

	btc_skip_blacklist_clocks(rdev, max_limits->sclk, max_limits->mclk,
				  &ps->medium.sclk, &ps->medium.mclk);
	btc_skip_blacklist_clocks(rdev, max_limits->sclk, max_limits->mclk,
				  &ps->high.sclk, &ps->high.mclk);

	btc_adjust_clock_combinations(rdev, max_limits, &ps->low);
	btc_adjust_clock_combinations(rdev, max_limits, &ps->medium);
	btc_adjust_clock_combinations(rdev, max_limits, &ps->high);

	btc_apply_voltage_dependency_rules(&rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk,
					   ps->low.sclk, max_limits->vddc, &ps->low.vddc);
	btc_apply_voltage_dependency_rules(&rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk,
					   ps->low.mclk, max_limits->vddci, &ps->low.vddci);
	btc_apply_voltage_dependency_rules(&rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk,
					   ps->low.mclk, max_limits->vddc, &ps->low.vddc);
	/* XXX validate the voltage required for display */
	btc_apply_voltage_dependency_rules(&rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk,
					   ps->medium.sclk, max_limits->vddc, &ps->medium.vddc);
	btc_apply_voltage_dependency_rules(&rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk,
					   ps->medium.mclk, max_limits->vddci, &ps->medium.vddci);
	btc_apply_voltage_dependency_rules(&rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk,
					   ps->medium.mclk, max_limits->vddc, &ps->medium.vddc);
	/* XXX validate the voltage required for display */
	btc_apply_voltage_dependency_rules(&rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk,
					   ps->high.sclk, max_limits->vddc, &ps->high.vddc);
	btc_apply_voltage_dependency_rules(&rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk,
					   ps->high.mclk, max_limits->vddci, &ps->high.vddci);
	btc_apply_voltage_dependency_rules(&rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk,
					   ps->high.mclk, max_limits->vddc, &ps->high.vddc);
	/* XXX validate the voltage required for display */

	btc_apply_voltage_delta_rules(rdev, max_limits->vddc, max_limits->vddci,
				      &ps->low.vddc, &ps->low.vddci);
	btc_apply_voltage_delta_rules(rdev, max_limits->vddc, max_limits->vddci,
				      &ps->medium.vddc, &ps->medium.vddci);
	btc_apply_voltage_delta_rules(rdev, max_limits->vddc, max_limits->vddci,
				      &ps->high.vddc, &ps->high.vddci);

	if ((ps->high.vddc <= rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc.vddc) &&
	    (ps->medium.vddc <= rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc.vddc) &&
	    (ps->low.vddc <= rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc.vddc))
		ps->dc_compatible = true;
	else
		ps->dc_compatible = false;

	if (ps->low.vddc < rdev->pm.dpm.dyn_state.min_vddc_for_pcie_gen2)
		ps->low.flags &= ~ATOM_PPLIB_R600_FLAGS_PCIEGEN2;
	if (ps->medium.vddc < rdev->pm.dpm.dyn_state.min_vddc_for_pcie_gen2)
		ps->medium.flags &= ~ATOM_PPLIB_R600_FLAGS_PCIEGEN2;
	if (ps->high.vddc < rdev->pm.dpm.dyn_state.min_vddc_for_pcie_gen2)
		ps->high.flags &= ~ATOM_PPLIB_R600_FLAGS_PCIEGEN2;
}

static void btc_update_current_ps(struct radeon_device *rdev,
				  struct radeon_ps *rps)
{
	struct rv7xx_ps *new_ps = rv770_get_ps(rps);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	eg_pi->current_rps = *rps;
	eg_pi->current_ps = *new_ps;
	eg_pi->current_rps.ps_priv = &eg_pi->current_ps;
}

static void btc_update_requested_ps(struct radeon_device *rdev,
				    struct radeon_ps *rps)
{
	struct rv7xx_ps *new_ps = rv770_get_ps(rps);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	eg_pi->requested_rps = *rps;
	eg_pi->requested_ps = *new_ps;
	eg_pi->requested_rps.ps_priv = &eg_pi->requested_ps;
}

void btc_dpm_reset_asic(struct radeon_device *rdev)
{
	rv770_restrict_performance_levels_before_switch(rdev);
	btc_disable_ulv(rdev);
	btc_set_boot_state_timing(rdev);
	rv770_set_boot_state(rdev);
}

int btc_dpm_pre_set_power_state(struct radeon_device *rdev)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct radeon_ps requested_ps = *rdev->pm.dpm.requested_ps;
	struct radeon_ps *new_ps = &requested_ps;

	btc_update_requested_ps(rdev, new_ps);

	btc_apply_state_adjust_rules(rdev, &eg_pi->requested_rps);

	return 0;
}

int btc_dpm_set_power_state(struct radeon_device *rdev)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct radeon_ps *new_ps = &eg_pi->requested_rps;
	struct radeon_ps *old_ps = &eg_pi->current_rps;

	btc_disable_ulv(rdev);
	btc_set_boot_state_timing(rdev);
	rv770_restrict_performance_levels_before_switch(rdev);

	if (eg_pi->pcie_performance_request)
		cypress_notify_link_speed_change_before_state_change(rdev, new_ps, old_ps);

	rv770_set_uvd_clock_before_set_eng_clock(rdev, new_ps, old_ps);
	rv770_halt_smc(rdev);
	btc_set_at_for_uvd(rdev, new_ps);
	if (eg_pi->smu_uvd_hs)
		btc_notify_uvd_to_smc(rdev, new_ps);
	cypress_upload_sw_state(rdev, new_ps);

	if (eg_pi->dynamic_ac_timing)
		cypress_upload_mc_reg_table(rdev, new_ps);

	cypress_program_memory_timing_parameters(rdev, new_ps);

	rv770_resume_smc(rdev);
	rv770_set_sw_state(rdev);
	rv770_set_uvd_clock_after_set_eng_clock(rdev, new_ps, old_ps);

	if (eg_pi->pcie_performance_request)
		cypress_notify_link_speed_change_after_state_change(rdev, new_ps, old_ps);

	btc_set_power_state_conditionally_enable_ulv(rdev, new_ps);

#if 0
	/* XXX */
	rv770_unrestrict_performance_levels_after_switch(rdev);
#endif

	return 0;
}

void btc_dpm_post_set_power_state(struct radeon_device *rdev)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct radeon_ps *new_ps = &eg_pi->requested_rps;

	btc_update_current_ps(rdev, new_ps);
}

int btc_dpm_enable(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct radeon_ps *boot_ps = rdev->pm.dpm.boot_ps;

	if (pi->gfx_clock_gating)
		btc_cg_clock_gating_default(rdev);

	if (btc_dpm_enabled(rdev))
		return -EINVAL;

	if (pi->mg_clock_gating)
		btc_mg_clock_gating_default(rdev);

	if (eg_pi->ls_clock_gating)
		btc_ls_clock_gating_default(rdev);

	if (pi->voltage_control) {
		rv770_enable_voltage_control(rdev, true);
		cypress_construct_voltage_tables(rdev);
	}

	if (pi->mvdd_control)
		cypress_get_mvdd_configuration(rdev);

	if (eg_pi->dynamic_ac_timing)
		btc_initialize_mc_reg_table(rdev);

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_BACKBIAS)
		rv770_enable_backbias(rdev, true);

	if (pi->dynamic_ss)
		cypress_enable_spread_spectrum(rdev, true);

	if (pi->thermal_protection)
		rv770_enable_thermal_protection(rdev, true);

	rv770_setup_bsp(rdev);
	rv770_program_git(rdev);
	rv770_program_tp(rdev);
	rv770_program_tpp(rdev);
	rv770_program_sstp(rdev);
	rv770_program_engine_speed_parameters(rdev);
	cypress_enable_display_gap(rdev);
	rv770_program_vc(rdev);

	if (pi->dynamic_pcie_gen2)
		btc_enable_dynamic_pcie_gen2(rdev, true);

	if (rv770_upload_firmware(rdev))
		return -EINVAL;

	cypress_get_table_locations(rdev);
	btc_init_smc_table(rdev, boot_ps);

	if (eg_pi->dynamic_ac_timing)
		cypress_populate_mc_reg_table(rdev, boot_ps);

	cypress_program_response_times(rdev);
	r7xx_start_smc(rdev);
	cypress_notify_smc_display_change(rdev, false);
	cypress_enable_sclk_control(rdev, true);

	if (eg_pi->memory_transition)
		cypress_enable_mclk_control(rdev, true);

	cypress_start_dpm(rdev);

	if (pi->gfx_clock_gating)
		btc_cg_clock_gating_enable(rdev, true);

	if (pi->mg_clock_gating)
		btc_mg_clock_gating_enable(rdev, true);

	if (eg_pi->ls_clock_gating)
		btc_ls_clock_gating_enable(rdev, true);

	if (rdev->irq.installed &&
	    r600_is_internal_thermal_sensor(rdev->pm.int_thermal_type)) {
		PPSMC_Result result;

		rv770_set_thermal_temperature_range(rdev, R600_TEMP_RANGE_MIN, R600_TEMP_RANGE_MAX);
		rdev->irq.dpm_thermal = true;
		radeon_irq_set(rdev);
		result = rv770_send_msg_to_smc(rdev, PPSMC_MSG_EnableThermalInterrupt);

		if (result != PPSMC_Result_OK)
			DRM_DEBUG_KMS("Could not enable thermal interrupts.\n");
	}

	rv770_enable_auto_throttle_source(rdev, RADEON_DPM_AUTO_THROTTLE_SRC_THERMAL, true);

	btc_init_stutter_mode(rdev);

	btc_update_current_ps(rdev, rdev->pm.dpm.boot_ps);

	return 0;
};

void btc_dpm_disable(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	if (!btc_dpm_enabled(rdev))
		return;

	rv770_clear_vc(rdev);

	if (pi->thermal_protection)
		rv770_enable_thermal_protection(rdev, false);

	if (pi->dynamic_pcie_gen2)
		btc_enable_dynamic_pcie_gen2(rdev, false);

	if (rdev->irq.installed &&
	    r600_is_internal_thermal_sensor(rdev->pm.int_thermal_type)) {
		rdev->irq.dpm_thermal = false;
		radeon_irq_set(rdev);
	}

	if (pi->gfx_clock_gating)
		btc_cg_clock_gating_enable(rdev, false);

	if (pi->mg_clock_gating)
		btc_mg_clock_gating_enable(rdev, false);

	if (eg_pi->ls_clock_gating)
		btc_ls_clock_gating_enable(rdev, false);

	rv770_stop_dpm(rdev);
	btc_reset_to_default(rdev);
	btc_stop_smc(rdev);
	cypress_enable_spread_spectrum(rdev, false);

	btc_update_current_ps(rdev, rdev->pm.dpm.boot_ps);
}

void btc_dpm_setup_asic(struct radeon_device *rdev)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	rv770_get_memory_type(rdev);
	rv740_read_clock_registers(rdev);
	btc_read_arb_registers(rdev);
	rv770_read_voltage_smio_registers(rdev);

	if (eg_pi->pcie_performance_request)
		cypress_advertise_gen2_capability(rdev);

	rv770_get_pcie_gen2_status(rdev);
	rv770_enable_acpi_pm(rdev);
}

int btc_dpm_init(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi;
	struct evergreen_power_info *eg_pi;
	int index = GetIndexIntoMasterTable(DATA, ASIC_InternalSS_Info);
	u16 data_offset, size;
	u8 frev, crev;
	struct atom_clock_dividers dividers;
	int ret;

	eg_pi = kzalloc(sizeof(struct evergreen_power_info), GFP_KERNEL);
	if (eg_pi == NULL)
		return -ENOMEM;
	rdev->pm.dpm.priv = eg_pi;
	pi = &eg_pi->rv7xx;

	rv770_get_max_vddc(rdev);

	eg_pi->ulv.supported = false;
	pi->acpi_vddc = 0;
	eg_pi->acpi_vddci = 0;
	pi->min_vddc_in_table = 0;
	pi->max_vddc_in_table = 0;

	ret = rv7xx_parse_power_table(rdev);
	if (ret)
		return ret;
	ret = r600_parse_extended_power_table(rdev);
	if (ret)
		return ret;

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

	pi->mclk_strobe_mode_threshold = 40000;
	pi->mclk_edc_enable_threshold = 40000;
	eg_pi->mclk_edc_wr_enable_threshold = 40000;

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

	if (rdev->flags & RADEON_IS_MOBILITY)
		pi->dcodt = true;
	else
		pi->dcodt = false;

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

	if (rdev->family == CHIP_BARTS)
		eg_pi->dll_default_on = true;
	else
		eg_pi->dll_default_on = false;

	eg_pi->sclk_deep_sleep = false;
	if (ASIC_IS_LOMBOK(rdev))
		pi->mclk_stutter_mode_threshold = 30000;
	else
		pi->mclk_stutter_mode_threshold = 0;

	pi->sram_end = SMC_RAM_END;

	rdev->pm.dpm.dyn_state.mclk_sclk_ratio = 4;
	rdev->pm.dpm.dyn_state.vddc_vddci_delta = 200;
	rdev->pm.dpm.dyn_state.min_vddc_for_pcie_gen2 = 900;
	rdev->pm.dpm.dyn_state.valid_sclk_values.count = ARRAY_SIZE(btc_valid_sclk);
	rdev->pm.dpm.dyn_state.valid_sclk_values.values = btc_valid_sclk;
	rdev->pm.dpm.dyn_state.valid_mclk_values.count = 0;
	rdev->pm.dpm.dyn_state.valid_mclk_values.values = NULL;

	if (rdev->family == CHIP_TURKS)
		rdev->pm.dpm.dyn_state.sclk_mclk_delta = 15000;
	else
		rdev->pm.dpm.dyn_state.sclk_mclk_delta = 10000;

	return 0;
}

void btc_dpm_fini(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < rdev->pm.dpm.num_ps; i++) {
		kfree(rdev->pm.dpm.ps[i].ps_priv);
	}
	kfree(rdev->pm.dpm.ps);
	kfree(rdev->pm.dpm.priv);
	r600_free_extended_power_table(rdev);
}

u32 btc_dpm_get_sclk(struct radeon_device *rdev, bool low)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct rv7xx_ps *requested_state = rv770_get_ps(&eg_pi->requested_rps);

	if (low)
		return requested_state->low.sclk;
	else
		return requested_state->high.sclk;
}

u32 btc_dpm_get_mclk(struct radeon_device *rdev, bool low)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct rv7xx_ps *requested_state = rv770_get_ps(&eg_pi->requested_rps);

	if (low)
		return requested_state->low.mclk;
	else
		return requested_state->high.mclk;
}
