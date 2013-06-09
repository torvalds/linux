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
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include "radeon.h"
#include "radeon_asic.h"
#include <drm/radeon_drm.h>
#include "sid.h"
#include "atom.h"
#include "si_blit_shaders.h"

#define SI_PFP_UCODE_SIZE 2144
#define SI_PM4_UCODE_SIZE 2144
#define SI_CE_UCODE_SIZE 2144
#define SI_RLC_UCODE_SIZE 2048
#define SI_MC_UCODE_SIZE 7769
#define OLAND_MC_UCODE_SIZE 7863

MODULE_FIRMWARE("radeon/TAHITI_pfp.bin");
MODULE_FIRMWARE("radeon/TAHITI_me.bin");
MODULE_FIRMWARE("radeon/TAHITI_ce.bin");
MODULE_FIRMWARE("radeon/TAHITI_mc.bin");
MODULE_FIRMWARE("radeon/TAHITI_rlc.bin");
MODULE_FIRMWARE("radeon/PITCAIRN_pfp.bin");
MODULE_FIRMWARE("radeon/PITCAIRN_me.bin");
MODULE_FIRMWARE("radeon/PITCAIRN_ce.bin");
MODULE_FIRMWARE("radeon/PITCAIRN_mc.bin");
MODULE_FIRMWARE("radeon/PITCAIRN_rlc.bin");
MODULE_FIRMWARE("radeon/VERDE_pfp.bin");
MODULE_FIRMWARE("radeon/VERDE_me.bin");
MODULE_FIRMWARE("radeon/VERDE_ce.bin");
MODULE_FIRMWARE("radeon/VERDE_mc.bin");
MODULE_FIRMWARE("radeon/VERDE_rlc.bin");
MODULE_FIRMWARE("radeon/OLAND_pfp.bin");
MODULE_FIRMWARE("radeon/OLAND_me.bin");
MODULE_FIRMWARE("radeon/OLAND_ce.bin");
MODULE_FIRMWARE("radeon/OLAND_mc.bin");
MODULE_FIRMWARE("radeon/OLAND_rlc.bin");
MODULE_FIRMWARE("radeon/HAINAN_pfp.bin");
MODULE_FIRMWARE("radeon/HAINAN_me.bin");
MODULE_FIRMWARE("radeon/HAINAN_ce.bin");
MODULE_FIRMWARE("radeon/HAINAN_mc.bin");
MODULE_FIRMWARE("radeon/HAINAN_rlc.bin");

extern int r600_ih_ring_alloc(struct radeon_device *rdev);
extern void r600_ih_ring_fini(struct radeon_device *rdev);
extern void evergreen_fix_pci_max_read_req_size(struct radeon_device *rdev);
extern void evergreen_mc_stop(struct radeon_device *rdev, struct evergreen_mc_save *save);
extern void evergreen_mc_resume(struct radeon_device *rdev, struct evergreen_mc_save *save);
extern u32 evergreen_get_number_of_dram_channels(struct radeon_device *rdev);
extern void evergreen_print_gpu_status_regs(struct radeon_device *rdev);
extern bool evergreen_is_display_hung(struct radeon_device *rdev);

static const u32 tahiti_golden_rlc_registers[] =
{
	0xc424, 0xffffffff, 0x00601005,
	0xc47c, 0xffffffff, 0x10104040,
	0xc488, 0xffffffff, 0x0100000a,
	0xc314, 0xffffffff, 0x00000800,
	0xc30c, 0xffffffff, 0x800000f4,
	0xf4a8, 0xffffffff, 0x00000000
};

static const u32 tahiti_golden_registers[] =
{
	0x9a10, 0x00010000, 0x00018208,
	0x9830, 0xffffffff, 0x00000000,
	0x9834, 0xf00fffff, 0x00000400,
	0x9838, 0x0002021c, 0x00020200,
	0xc78, 0x00000080, 0x00000000,
	0xd030, 0x000300c0, 0x00800040,
	0xd830, 0x000300c0, 0x00800040,
	0x5bb0, 0x000000f0, 0x00000070,
	0x5bc0, 0x00200000, 0x50100000,
	0x7030, 0x31000311, 0x00000011,
	0x277c, 0x00000003, 0x000007ff,
	0x240c, 0x000007ff, 0x00000000,
	0x8a14, 0xf000001f, 0x00000007,
	0x8b24, 0xffffffff, 0x00ffffff,
	0x8b10, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x4e000000,
	0x28350, 0x3f3f3fff, 0x2a00126a,
	0x30, 0x000000ff, 0x0040,
	0x34, 0x00000040, 0x00004040,
	0x9100, 0x07ffffff, 0x03000000,
	0x8e88, 0x01ff1f3f, 0x00000000,
	0x8e84, 0x01ff1f3f, 0x00000000,
	0x9060, 0x0000007f, 0x00000020,
	0x9508, 0x00010000, 0x00010000,
	0xac14, 0x00000200, 0x000002fb,
	0xac10, 0xffffffff, 0x0000543b,
	0xac0c, 0xffffffff, 0xa9210876,
	0x88d0, 0xffffffff, 0x000fff40,
	0x88d4, 0x0000001f, 0x00000010,
	0x1410, 0x20000000, 0x20fffed8,
	0x15c0, 0x000c0fc0, 0x000c0400
};

static const u32 tahiti_golden_registers2[] =
{
	0xc64, 0x00000001, 0x00000001
};

static const u32 pitcairn_golden_rlc_registers[] =
{
	0xc424, 0xffffffff, 0x00601004,
	0xc47c, 0xffffffff, 0x10102020,
	0xc488, 0xffffffff, 0x01000020,
	0xc314, 0xffffffff, 0x00000800,
	0xc30c, 0xffffffff, 0x800000a4
};

static const u32 pitcairn_golden_registers[] =
{
	0x9a10, 0x00010000, 0x00018208,
	0x9830, 0xffffffff, 0x00000000,
	0x9834, 0xf00fffff, 0x00000400,
	0x9838, 0x0002021c, 0x00020200,
	0xc78, 0x00000080, 0x00000000,
	0xd030, 0x000300c0, 0x00800040,
	0xd830, 0x000300c0, 0x00800040,
	0x5bb0, 0x000000f0, 0x00000070,
	0x5bc0, 0x00200000, 0x50100000,
	0x7030, 0x31000311, 0x00000011,
	0x2ae4, 0x00073ffe, 0x000022a2,
	0x240c, 0x000007ff, 0x00000000,
	0x8a14, 0xf000001f, 0x00000007,
	0x8b24, 0xffffffff, 0x00ffffff,
	0x8b10, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x4e000000,
	0x28350, 0x3f3f3fff, 0x2a00126a,
	0x30, 0x000000ff, 0x0040,
	0x34, 0x00000040, 0x00004040,
	0x9100, 0x07ffffff, 0x03000000,
	0x9060, 0x0000007f, 0x00000020,
	0x9508, 0x00010000, 0x00010000,
	0xac14, 0x000003ff, 0x000000f7,
	0xac10, 0xffffffff, 0x00000000,
	0xac0c, 0xffffffff, 0x32761054,
	0x88d4, 0x0000001f, 0x00000010,
	0x15c0, 0x000c0fc0, 0x000c0400
};

static const u32 verde_golden_rlc_registers[] =
{
	0xc424, 0xffffffff, 0x033f1005,
	0xc47c, 0xffffffff, 0x10808020,
	0xc488, 0xffffffff, 0x00800008,
	0xc314, 0xffffffff, 0x00001000,
	0xc30c, 0xffffffff, 0x80010014
};

static const u32 verde_golden_registers[] =
{
	0x9a10, 0x00010000, 0x00018208,
	0x9830, 0xffffffff, 0x00000000,
	0x9834, 0xf00fffff, 0x00000400,
	0x9838, 0x0002021c, 0x00020200,
	0xc78, 0x00000080, 0x00000000,
	0xd030, 0x000300c0, 0x00800040,
	0xd030, 0x000300c0, 0x00800040,
	0xd830, 0x000300c0, 0x00800040,
	0xd830, 0x000300c0, 0x00800040,
	0x5bb0, 0x000000f0, 0x00000070,
	0x5bc0, 0x00200000, 0x50100000,
	0x7030, 0x31000311, 0x00000011,
	0x2ae4, 0x00073ffe, 0x000022a2,
	0x2ae4, 0x00073ffe, 0x000022a2,
	0x2ae4, 0x00073ffe, 0x000022a2,
	0x240c, 0x000007ff, 0x00000000,
	0x240c, 0x000007ff, 0x00000000,
	0x240c, 0x000007ff, 0x00000000,
	0x8a14, 0xf000001f, 0x00000007,
	0x8a14, 0xf000001f, 0x00000007,
	0x8a14, 0xf000001f, 0x00000007,
	0x8b24, 0xffffffff, 0x00ffffff,
	0x8b10, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x4e000000,
	0x28350, 0x3f3f3fff, 0x0000124a,
	0x28350, 0x3f3f3fff, 0x0000124a,
	0x28350, 0x3f3f3fff, 0x0000124a,
	0x30, 0x000000ff, 0x0040,
	0x34, 0x00000040, 0x00004040,
	0x9100, 0x07ffffff, 0x03000000,
	0x9100, 0x07ffffff, 0x03000000,
	0x8e88, 0x01ff1f3f, 0x00000000,
	0x8e88, 0x01ff1f3f, 0x00000000,
	0x8e88, 0x01ff1f3f, 0x00000000,
	0x8e84, 0x01ff1f3f, 0x00000000,
	0x8e84, 0x01ff1f3f, 0x00000000,
	0x8e84, 0x01ff1f3f, 0x00000000,
	0x9060, 0x0000007f, 0x00000020,
	0x9508, 0x00010000, 0x00010000,
	0xac14, 0x000003ff, 0x00000003,
	0xac14, 0x000003ff, 0x00000003,
	0xac14, 0x000003ff, 0x00000003,
	0xac10, 0xffffffff, 0x00000000,
	0xac10, 0xffffffff, 0x00000000,
	0xac10, 0xffffffff, 0x00000000,
	0xac0c, 0xffffffff, 0x00001032,
	0xac0c, 0xffffffff, 0x00001032,
	0xac0c, 0xffffffff, 0x00001032,
	0x88d4, 0x0000001f, 0x00000010,
	0x88d4, 0x0000001f, 0x00000010,
	0x88d4, 0x0000001f, 0x00000010,
	0x15c0, 0x000c0fc0, 0x000c0400
};

static const u32 oland_golden_rlc_registers[] =
{
	0xc424, 0xffffffff, 0x00601005,
	0xc47c, 0xffffffff, 0x10104040,
	0xc488, 0xffffffff, 0x0100000a,
	0xc314, 0xffffffff, 0x00000800,
	0xc30c, 0xffffffff, 0x800000f4
};

static const u32 oland_golden_registers[] =
{
	0x9a10, 0x00010000, 0x00018208,
	0x9830, 0xffffffff, 0x00000000,
	0x9834, 0xf00fffff, 0x00000400,
	0x9838, 0x0002021c, 0x00020200,
	0xc78, 0x00000080, 0x00000000,
	0xd030, 0x000300c0, 0x00800040,
	0xd830, 0x000300c0, 0x00800040,
	0x5bb0, 0x000000f0, 0x00000070,
	0x5bc0, 0x00200000, 0x50100000,
	0x7030, 0x31000311, 0x00000011,
	0x2ae4, 0x00073ffe, 0x000022a2,
	0x240c, 0x000007ff, 0x00000000,
	0x8a14, 0xf000001f, 0x00000007,
	0x8b24, 0xffffffff, 0x00ffffff,
	0x8b10, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x4e000000,
	0x28350, 0x3f3f3fff, 0x00000082,
	0x30, 0x000000ff, 0x0040,
	0x34, 0x00000040, 0x00004040,
	0x9100, 0x07ffffff, 0x03000000,
	0x9060, 0x0000007f, 0x00000020,
	0x9508, 0x00010000, 0x00010000,
	0xac14, 0x000003ff, 0x000000f3,
	0xac10, 0xffffffff, 0x00000000,
	0xac0c, 0xffffffff, 0x00003210,
	0x88d4, 0x0000001f, 0x00000010,
	0x15c0, 0x000c0fc0, 0x000c0400
};

static const u32 hainan_golden_registers[] =
{
	0x9a10, 0x00010000, 0x00018208,
	0x9830, 0xffffffff, 0x00000000,
	0x9834, 0xf00fffff, 0x00000400,
	0x9838, 0x0002021c, 0x00020200,
	0xd0c0, 0xff000fff, 0x00000100,
	0xd030, 0x000300c0, 0x00800040,
	0xd8c0, 0xff000fff, 0x00000100,
	0xd830, 0x000300c0, 0x00800040,
	0x2ae4, 0x00073ffe, 0x000022a2,
	0x240c, 0x000007ff, 0x00000000,
	0x8a14, 0xf000001f, 0x00000007,
	0x8b24, 0xffffffff, 0x00ffffff,
	0x8b10, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x4e000000,
	0x28350, 0x3f3f3fff, 0x00000000,
	0x30, 0x000000ff, 0x0040,
	0x34, 0x00000040, 0x00004040,
	0x9100, 0x03e00000, 0x03600000,
	0x9060, 0x0000007f, 0x00000020,
	0x9508, 0x00010000, 0x00010000,
	0xac14, 0x000003ff, 0x000000f1,
	0xac10, 0xffffffff, 0x00000000,
	0xac0c, 0xffffffff, 0x00003210,
	0x88d4, 0x0000001f, 0x00000010,
	0x15c0, 0x000c0fc0, 0x000c0400
};

static const u32 hainan_golden_registers2[] =
{
	0x98f8, 0xffffffff, 0x02010001
};

static const u32 tahiti_mgcg_cgcg_init[] =
{
	0xc400, 0xffffffff, 0xfffffffc,
	0x802c, 0xffffffff, 0xe0000000,
	0x9a60, 0xffffffff, 0x00000100,
	0x92a4, 0xffffffff, 0x00000100,
	0xc164, 0xffffffff, 0x00000100,
	0x9774, 0xffffffff, 0x00000100,
	0x8984, 0xffffffff, 0x06000100,
	0x8a18, 0xffffffff, 0x00000100,
	0x92a0, 0xffffffff, 0x00000100,
	0xc380, 0xffffffff, 0x00000100,
	0x8b28, 0xffffffff, 0x00000100,
	0x9144, 0xffffffff, 0x00000100,
	0x8d88, 0xffffffff, 0x00000100,
	0x8d8c, 0xffffffff, 0x00000100,
	0x9030, 0xffffffff, 0x00000100,
	0x9034, 0xffffffff, 0x00000100,
	0x9038, 0xffffffff, 0x00000100,
	0x903c, 0xffffffff, 0x00000100,
	0xad80, 0xffffffff, 0x00000100,
	0xac54, 0xffffffff, 0x00000100,
	0x897c, 0xffffffff, 0x06000100,
	0x9868, 0xffffffff, 0x00000100,
	0x9510, 0xffffffff, 0x00000100,
	0xaf04, 0xffffffff, 0x00000100,
	0xae04, 0xffffffff, 0x00000100,
	0x949c, 0xffffffff, 0x00000100,
	0x802c, 0xffffffff, 0xe0000000,
	0x9160, 0xffffffff, 0x00010000,
	0x9164, 0xffffffff, 0x00030002,
	0x9168, 0xffffffff, 0x00040007,
	0x916c, 0xffffffff, 0x00060005,
	0x9170, 0xffffffff, 0x00090008,
	0x9174, 0xffffffff, 0x00020001,
	0x9178, 0xffffffff, 0x00040003,
	0x917c, 0xffffffff, 0x00000007,
	0x9180, 0xffffffff, 0x00060005,
	0x9184, 0xffffffff, 0x00090008,
	0x9188, 0xffffffff, 0x00030002,
	0x918c, 0xffffffff, 0x00050004,
	0x9190, 0xffffffff, 0x00000008,
	0x9194, 0xffffffff, 0x00070006,
	0x9198, 0xffffffff, 0x000a0009,
	0x919c, 0xffffffff, 0x00040003,
	0x91a0, 0xffffffff, 0x00060005,
	0x91a4, 0xffffffff, 0x00000009,
	0x91a8, 0xffffffff, 0x00080007,
	0x91ac, 0xffffffff, 0x000b000a,
	0x91b0, 0xffffffff, 0x00050004,
	0x91b4, 0xffffffff, 0x00070006,
	0x91b8, 0xffffffff, 0x0008000b,
	0x91bc, 0xffffffff, 0x000a0009,
	0x91c0, 0xffffffff, 0x000d000c,
	0x91c4, 0xffffffff, 0x00060005,
	0x91c8, 0xffffffff, 0x00080007,
	0x91cc, 0xffffffff, 0x0000000b,
	0x91d0, 0xffffffff, 0x000a0009,
	0x91d4, 0xffffffff, 0x000d000c,
	0x91d8, 0xffffffff, 0x00070006,
	0x91dc, 0xffffffff, 0x00090008,
	0x91e0, 0xffffffff, 0x0000000c,
	0x91e4, 0xffffffff, 0x000b000a,
	0x91e8, 0xffffffff, 0x000e000d,
	0x91ec, 0xffffffff, 0x00080007,
	0x91f0, 0xffffffff, 0x000a0009,
	0x91f4, 0xffffffff, 0x0000000d,
	0x91f8, 0xffffffff, 0x000c000b,
	0x91fc, 0xffffffff, 0x000f000e,
	0x9200, 0xffffffff, 0x00090008,
	0x9204, 0xffffffff, 0x000b000a,
	0x9208, 0xffffffff, 0x000c000f,
	0x920c, 0xffffffff, 0x000e000d,
	0x9210, 0xffffffff, 0x00110010,
	0x9214, 0xffffffff, 0x000a0009,
	0x9218, 0xffffffff, 0x000c000b,
	0x921c, 0xffffffff, 0x0000000f,
	0x9220, 0xffffffff, 0x000e000d,
	0x9224, 0xffffffff, 0x00110010,
	0x9228, 0xffffffff, 0x000b000a,
	0x922c, 0xffffffff, 0x000d000c,
	0x9230, 0xffffffff, 0x00000010,
	0x9234, 0xffffffff, 0x000f000e,
	0x9238, 0xffffffff, 0x00120011,
	0x923c, 0xffffffff, 0x000c000b,
	0x9240, 0xffffffff, 0x000e000d,
	0x9244, 0xffffffff, 0x00000011,
	0x9248, 0xffffffff, 0x0010000f,
	0x924c, 0xffffffff, 0x00130012,
	0x9250, 0xffffffff, 0x000d000c,
	0x9254, 0xffffffff, 0x000f000e,
	0x9258, 0xffffffff, 0x00100013,
	0x925c, 0xffffffff, 0x00120011,
	0x9260, 0xffffffff, 0x00150014,
	0x9264, 0xffffffff, 0x000e000d,
	0x9268, 0xffffffff, 0x0010000f,
	0x926c, 0xffffffff, 0x00000013,
	0x9270, 0xffffffff, 0x00120011,
	0x9274, 0xffffffff, 0x00150014,
	0x9278, 0xffffffff, 0x000f000e,
	0x927c, 0xffffffff, 0x00110010,
	0x9280, 0xffffffff, 0x00000014,
	0x9284, 0xffffffff, 0x00130012,
	0x9288, 0xffffffff, 0x00160015,
	0x928c, 0xffffffff, 0x0010000f,
	0x9290, 0xffffffff, 0x00120011,
	0x9294, 0xffffffff, 0x00000015,
	0x9298, 0xffffffff, 0x00140013,
	0x929c, 0xffffffff, 0x00170016,
	0x9150, 0xffffffff, 0x96940200,
	0x8708, 0xffffffff, 0x00900100,
	0xc478, 0xffffffff, 0x00000080,
	0xc404, 0xffffffff, 0x0020003f,
	0x30, 0xffffffff, 0x0000001c,
	0x34, 0x000f0000, 0x000f0000,
	0x160c, 0xffffffff, 0x00000100,
	0x1024, 0xffffffff, 0x00000100,
	0x102c, 0x00000101, 0x00000000,
	0x20a8, 0xffffffff, 0x00000104,
	0x264c, 0x000c0000, 0x000c0000,
	0x2648, 0x000c0000, 0x000c0000,
	0x55e4, 0xff000fff, 0x00000100,
	0x55e8, 0x00000001, 0x00000001,
	0x2f50, 0x00000001, 0x00000001,
	0x30cc, 0xc0000fff, 0x00000104,
	0xc1e4, 0x00000001, 0x00000001,
	0xd0c0, 0xfffffff0, 0x00000100,
	0xd8c0, 0xfffffff0, 0x00000100
};

static const u32 pitcairn_mgcg_cgcg_init[] =
{
	0xc400, 0xffffffff, 0xfffffffc,
	0x802c, 0xffffffff, 0xe0000000,
	0x9a60, 0xffffffff, 0x00000100,
	0x92a4, 0xffffffff, 0x00000100,
	0xc164, 0xffffffff, 0x00000100,
	0x9774, 0xffffffff, 0x00000100,
	0x8984, 0xffffffff, 0x06000100,
	0x8a18, 0xffffffff, 0x00000100,
	0x92a0, 0xffffffff, 0x00000100,
	0xc380, 0xffffffff, 0x00000100,
	0x8b28, 0xffffffff, 0x00000100,
	0x9144, 0xffffffff, 0x00000100,
	0x8d88, 0xffffffff, 0x00000100,
	0x8d8c, 0xffffffff, 0x00000100,
	0x9030, 0xffffffff, 0x00000100,
	0x9034, 0xffffffff, 0x00000100,
	0x9038, 0xffffffff, 0x00000100,
	0x903c, 0xffffffff, 0x00000100,
	0xad80, 0xffffffff, 0x00000100,
	0xac54, 0xffffffff, 0x00000100,
	0x897c, 0xffffffff, 0x06000100,
	0x9868, 0xffffffff, 0x00000100,
	0x9510, 0xffffffff, 0x00000100,
	0xaf04, 0xffffffff, 0x00000100,
	0xae04, 0xffffffff, 0x00000100,
	0x949c, 0xffffffff, 0x00000100,
	0x802c, 0xffffffff, 0xe0000000,
	0x9160, 0xffffffff, 0x00010000,
	0x9164, 0xffffffff, 0x00030002,
	0x9168, 0xffffffff, 0x00040007,
	0x916c, 0xffffffff, 0x00060005,
	0x9170, 0xffffffff, 0x00090008,
	0x9174, 0xffffffff, 0x00020001,
	0x9178, 0xffffffff, 0x00040003,
	0x917c, 0xffffffff, 0x00000007,
	0x9180, 0xffffffff, 0x00060005,
	0x9184, 0xffffffff, 0x00090008,
	0x9188, 0xffffffff, 0x00030002,
	0x918c, 0xffffffff, 0x00050004,
	0x9190, 0xffffffff, 0x00000008,
	0x9194, 0xffffffff, 0x00070006,
	0x9198, 0xffffffff, 0x000a0009,
	0x919c, 0xffffffff, 0x00040003,
	0x91a0, 0xffffffff, 0x00060005,
	0x91a4, 0xffffffff, 0x00000009,
	0x91a8, 0xffffffff, 0x00080007,
	0x91ac, 0xffffffff, 0x000b000a,
	0x91b0, 0xffffffff, 0x00050004,
	0x91b4, 0xffffffff, 0x00070006,
	0x91b8, 0xffffffff, 0x0008000b,
	0x91bc, 0xffffffff, 0x000a0009,
	0x91c0, 0xffffffff, 0x000d000c,
	0x9200, 0xffffffff, 0x00090008,
	0x9204, 0xffffffff, 0x000b000a,
	0x9208, 0xffffffff, 0x000c000f,
	0x920c, 0xffffffff, 0x000e000d,
	0x9210, 0xffffffff, 0x00110010,
	0x9214, 0xffffffff, 0x000a0009,
	0x9218, 0xffffffff, 0x000c000b,
	0x921c, 0xffffffff, 0x0000000f,
	0x9220, 0xffffffff, 0x000e000d,
	0x9224, 0xffffffff, 0x00110010,
	0x9228, 0xffffffff, 0x000b000a,
	0x922c, 0xffffffff, 0x000d000c,
	0x9230, 0xffffffff, 0x00000010,
	0x9234, 0xffffffff, 0x000f000e,
	0x9238, 0xffffffff, 0x00120011,
	0x923c, 0xffffffff, 0x000c000b,
	0x9240, 0xffffffff, 0x000e000d,
	0x9244, 0xffffffff, 0x00000011,
	0x9248, 0xffffffff, 0x0010000f,
	0x924c, 0xffffffff, 0x00130012,
	0x9250, 0xffffffff, 0x000d000c,
	0x9254, 0xffffffff, 0x000f000e,
	0x9258, 0xffffffff, 0x00100013,
	0x925c, 0xffffffff, 0x00120011,
	0x9260, 0xffffffff, 0x00150014,
	0x9150, 0xffffffff, 0x96940200,
	0x8708, 0xffffffff, 0x00900100,
	0xc478, 0xffffffff, 0x00000080,
	0xc404, 0xffffffff, 0x0020003f,
	0x30, 0xffffffff, 0x0000001c,
	0x34, 0x000f0000, 0x000f0000,
	0x160c, 0xffffffff, 0x00000100,
	0x1024, 0xffffffff, 0x00000100,
	0x102c, 0x00000101, 0x00000000,
	0x20a8, 0xffffffff, 0x00000104,
	0x55e4, 0xff000fff, 0x00000100,
	0x55e8, 0x00000001, 0x00000001,
	0x2f50, 0x00000001, 0x00000001,
	0x30cc, 0xc0000fff, 0x00000104,
	0xc1e4, 0x00000001, 0x00000001,
	0xd0c0, 0xfffffff0, 0x00000100,
	0xd8c0, 0xfffffff0, 0x00000100
};

static const u32 verde_mgcg_cgcg_init[] =
{
	0xc400, 0xffffffff, 0xfffffffc,
	0x802c, 0xffffffff, 0xe0000000,
	0x9a60, 0xffffffff, 0x00000100,
	0x92a4, 0xffffffff, 0x00000100,
	0xc164, 0xffffffff, 0x00000100,
	0x9774, 0xffffffff, 0x00000100,
	0x8984, 0xffffffff, 0x06000100,
	0x8a18, 0xffffffff, 0x00000100,
	0x92a0, 0xffffffff, 0x00000100,
	0xc380, 0xffffffff, 0x00000100,
	0x8b28, 0xffffffff, 0x00000100,
	0x9144, 0xffffffff, 0x00000100,
	0x8d88, 0xffffffff, 0x00000100,
	0x8d8c, 0xffffffff, 0x00000100,
	0x9030, 0xffffffff, 0x00000100,
	0x9034, 0xffffffff, 0x00000100,
	0x9038, 0xffffffff, 0x00000100,
	0x903c, 0xffffffff, 0x00000100,
	0xad80, 0xffffffff, 0x00000100,
	0xac54, 0xffffffff, 0x00000100,
	0x897c, 0xffffffff, 0x06000100,
	0x9868, 0xffffffff, 0x00000100,
	0x9510, 0xffffffff, 0x00000100,
	0xaf04, 0xffffffff, 0x00000100,
	0xae04, 0xffffffff, 0x00000100,
	0x949c, 0xffffffff, 0x00000100,
	0x802c, 0xffffffff, 0xe0000000,
	0x9160, 0xffffffff, 0x00010000,
	0x9164, 0xffffffff, 0x00030002,
	0x9168, 0xffffffff, 0x00040007,
	0x916c, 0xffffffff, 0x00060005,
	0x9170, 0xffffffff, 0x00090008,
	0x9174, 0xffffffff, 0x00020001,
	0x9178, 0xffffffff, 0x00040003,
	0x917c, 0xffffffff, 0x00000007,
	0x9180, 0xffffffff, 0x00060005,
	0x9184, 0xffffffff, 0x00090008,
	0x9188, 0xffffffff, 0x00030002,
	0x918c, 0xffffffff, 0x00050004,
	0x9190, 0xffffffff, 0x00000008,
	0x9194, 0xffffffff, 0x00070006,
	0x9198, 0xffffffff, 0x000a0009,
	0x919c, 0xffffffff, 0x00040003,
	0x91a0, 0xffffffff, 0x00060005,
	0x91a4, 0xffffffff, 0x00000009,
	0x91a8, 0xffffffff, 0x00080007,
	0x91ac, 0xffffffff, 0x000b000a,
	0x91b0, 0xffffffff, 0x00050004,
	0x91b4, 0xffffffff, 0x00070006,
	0x91b8, 0xffffffff, 0x0008000b,
	0x91bc, 0xffffffff, 0x000a0009,
	0x91c0, 0xffffffff, 0x000d000c,
	0x9200, 0xffffffff, 0x00090008,
	0x9204, 0xffffffff, 0x000b000a,
	0x9208, 0xffffffff, 0x000c000f,
	0x920c, 0xffffffff, 0x000e000d,
	0x9210, 0xffffffff, 0x00110010,
	0x9214, 0xffffffff, 0x000a0009,
	0x9218, 0xffffffff, 0x000c000b,
	0x921c, 0xffffffff, 0x0000000f,
	0x9220, 0xffffffff, 0x000e000d,
	0x9224, 0xffffffff, 0x00110010,
	0x9228, 0xffffffff, 0x000b000a,
	0x922c, 0xffffffff, 0x000d000c,
	0x9230, 0xffffffff, 0x00000010,
	0x9234, 0xffffffff, 0x000f000e,
	0x9238, 0xffffffff, 0x00120011,
	0x923c, 0xffffffff, 0x000c000b,
	0x9240, 0xffffffff, 0x000e000d,
	0x9244, 0xffffffff, 0x00000011,
	0x9248, 0xffffffff, 0x0010000f,
	0x924c, 0xffffffff, 0x00130012,
	0x9250, 0xffffffff, 0x000d000c,
	0x9254, 0xffffffff, 0x000f000e,
	0x9258, 0xffffffff, 0x00100013,
	0x925c, 0xffffffff, 0x00120011,
	0x9260, 0xffffffff, 0x00150014,
	0x9150, 0xffffffff, 0x96940200,
	0x8708, 0xffffffff, 0x00900100,
	0xc478, 0xffffffff, 0x00000080,
	0xc404, 0xffffffff, 0x0020003f,
	0x30, 0xffffffff, 0x0000001c,
	0x34, 0x000f0000, 0x000f0000,
	0x160c, 0xffffffff, 0x00000100,
	0x1024, 0xffffffff, 0x00000100,
	0x102c, 0x00000101, 0x00000000,
	0x20a8, 0xffffffff, 0x00000104,
	0x264c, 0x000c0000, 0x000c0000,
	0x2648, 0x000c0000, 0x000c0000,
	0x55e4, 0xff000fff, 0x00000100,
	0x55e8, 0x00000001, 0x00000001,
	0x2f50, 0x00000001, 0x00000001,
	0x30cc, 0xc0000fff, 0x00000104,
	0xc1e4, 0x00000001, 0x00000001,
	0xd0c0, 0xfffffff0, 0x00000100,
	0xd8c0, 0xfffffff0, 0x00000100
};

static const u32 oland_mgcg_cgcg_init[] =
{
	0xc400, 0xffffffff, 0xfffffffc,
	0x802c, 0xffffffff, 0xe0000000,
	0x9a60, 0xffffffff, 0x00000100,
	0x92a4, 0xffffffff, 0x00000100,
	0xc164, 0xffffffff, 0x00000100,
	0x9774, 0xffffffff, 0x00000100,
	0x8984, 0xffffffff, 0x06000100,
	0x8a18, 0xffffffff, 0x00000100,
	0x92a0, 0xffffffff, 0x00000100,
	0xc380, 0xffffffff, 0x00000100,
	0x8b28, 0xffffffff, 0x00000100,
	0x9144, 0xffffffff, 0x00000100,
	0x8d88, 0xffffffff, 0x00000100,
	0x8d8c, 0xffffffff, 0x00000100,
	0x9030, 0xffffffff, 0x00000100,
	0x9034, 0xffffffff, 0x00000100,
	0x9038, 0xffffffff, 0x00000100,
	0x903c, 0xffffffff, 0x00000100,
	0xad80, 0xffffffff, 0x00000100,
	0xac54, 0xffffffff, 0x00000100,
	0x897c, 0xffffffff, 0x06000100,
	0x9868, 0xffffffff, 0x00000100,
	0x9510, 0xffffffff, 0x00000100,
	0xaf04, 0xffffffff, 0x00000100,
	0xae04, 0xffffffff, 0x00000100,
	0x949c, 0xffffffff, 0x00000100,
	0x802c, 0xffffffff, 0xe0000000,
	0x9160, 0xffffffff, 0x00010000,
	0x9164, 0xffffffff, 0x00030002,
	0x9168, 0xffffffff, 0x00040007,
	0x916c, 0xffffffff, 0x00060005,
	0x9170, 0xffffffff, 0x00090008,
	0x9174, 0xffffffff, 0x00020001,
	0x9178, 0xffffffff, 0x00040003,
	0x917c, 0xffffffff, 0x00000007,
	0x9180, 0xffffffff, 0x00060005,
	0x9184, 0xffffffff, 0x00090008,
	0x9188, 0xffffffff, 0x00030002,
	0x918c, 0xffffffff, 0x00050004,
	0x9190, 0xffffffff, 0x00000008,
	0x9194, 0xffffffff, 0x00070006,
	0x9198, 0xffffffff, 0x000a0009,
	0x919c, 0xffffffff, 0x00040003,
	0x91a0, 0xffffffff, 0x00060005,
	0x91a4, 0xffffffff, 0x00000009,
	0x91a8, 0xffffffff, 0x00080007,
	0x91ac, 0xffffffff, 0x000b000a,
	0x91b0, 0xffffffff, 0x00050004,
	0x91b4, 0xffffffff, 0x00070006,
	0x91b8, 0xffffffff, 0x0008000b,
	0x91bc, 0xffffffff, 0x000a0009,
	0x91c0, 0xffffffff, 0x000d000c,
	0x91c4, 0xffffffff, 0x00060005,
	0x91c8, 0xffffffff, 0x00080007,
	0x91cc, 0xffffffff, 0x0000000b,
	0x91d0, 0xffffffff, 0x000a0009,
	0x91d4, 0xffffffff, 0x000d000c,
	0x9150, 0xffffffff, 0x96940200,
	0x8708, 0xffffffff, 0x00900100,
	0xc478, 0xffffffff, 0x00000080,
	0xc404, 0xffffffff, 0x0020003f,
	0x30, 0xffffffff, 0x0000001c,
	0x34, 0x000f0000, 0x000f0000,
	0x160c, 0xffffffff, 0x00000100,
	0x1024, 0xffffffff, 0x00000100,
	0x102c, 0x00000101, 0x00000000,
	0x20a8, 0xffffffff, 0x00000104,
	0x264c, 0x000c0000, 0x000c0000,
	0x2648, 0x000c0000, 0x000c0000,
	0x55e4, 0xff000fff, 0x00000100,
	0x55e8, 0x00000001, 0x00000001,
	0x2f50, 0x00000001, 0x00000001,
	0x30cc, 0xc0000fff, 0x00000104,
	0xc1e4, 0x00000001, 0x00000001,
	0xd0c0, 0xfffffff0, 0x00000100,
	0xd8c0, 0xfffffff0, 0x00000100
};

static const u32 hainan_mgcg_cgcg_init[] =
{
	0xc400, 0xffffffff, 0xfffffffc,
	0x802c, 0xffffffff, 0xe0000000,
	0x9a60, 0xffffffff, 0x00000100,
	0x92a4, 0xffffffff, 0x00000100,
	0xc164, 0xffffffff, 0x00000100,
	0x9774, 0xffffffff, 0x00000100,
	0x8984, 0xffffffff, 0x06000100,
	0x8a18, 0xffffffff, 0x00000100,
	0x92a0, 0xffffffff, 0x00000100,
	0xc380, 0xffffffff, 0x00000100,
	0x8b28, 0xffffffff, 0x00000100,
	0x9144, 0xffffffff, 0x00000100,
	0x8d88, 0xffffffff, 0x00000100,
	0x8d8c, 0xffffffff, 0x00000100,
	0x9030, 0xffffffff, 0x00000100,
	0x9034, 0xffffffff, 0x00000100,
	0x9038, 0xffffffff, 0x00000100,
	0x903c, 0xffffffff, 0x00000100,
	0xad80, 0xffffffff, 0x00000100,
	0xac54, 0xffffffff, 0x00000100,
	0x897c, 0xffffffff, 0x06000100,
	0x9868, 0xffffffff, 0x00000100,
	0x9510, 0xffffffff, 0x00000100,
	0xaf04, 0xffffffff, 0x00000100,
	0xae04, 0xffffffff, 0x00000100,
	0x949c, 0xffffffff, 0x00000100,
	0x802c, 0xffffffff, 0xe0000000,
	0x9160, 0xffffffff, 0x00010000,
	0x9164, 0xffffffff, 0x00030002,
	0x9168, 0xffffffff, 0x00040007,
	0x916c, 0xffffffff, 0x00060005,
	0x9170, 0xffffffff, 0x00090008,
	0x9174, 0xffffffff, 0x00020001,
	0x9178, 0xffffffff, 0x00040003,
	0x917c, 0xffffffff, 0x00000007,
	0x9180, 0xffffffff, 0x00060005,
	0x9184, 0xffffffff, 0x00090008,
	0x9188, 0xffffffff, 0x00030002,
	0x918c, 0xffffffff, 0x00050004,
	0x9190, 0xffffffff, 0x00000008,
	0x9194, 0xffffffff, 0x00070006,
	0x9198, 0xffffffff, 0x000a0009,
	0x919c, 0xffffffff, 0x00040003,
	0x91a0, 0xffffffff, 0x00060005,
	0x91a4, 0xffffffff, 0x00000009,
	0x91a8, 0xffffffff, 0x00080007,
	0x91ac, 0xffffffff, 0x000b000a,
	0x91b0, 0xffffffff, 0x00050004,
	0x91b4, 0xffffffff, 0x00070006,
	0x91b8, 0xffffffff, 0x0008000b,
	0x91bc, 0xffffffff, 0x000a0009,
	0x91c0, 0xffffffff, 0x000d000c,
	0x91c4, 0xffffffff, 0x00060005,
	0x91c8, 0xffffffff, 0x00080007,
	0x91cc, 0xffffffff, 0x0000000b,
	0x91d0, 0xffffffff, 0x000a0009,
	0x91d4, 0xffffffff, 0x000d000c,
	0x9150, 0xffffffff, 0x96940200,
	0x8708, 0xffffffff, 0x00900100,
	0xc478, 0xffffffff, 0x00000080,
	0xc404, 0xffffffff, 0x0020003f,
	0x30, 0xffffffff, 0x0000001c,
	0x34, 0x000f0000, 0x000f0000,
	0x160c, 0xffffffff, 0x00000100,
	0x1024, 0xffffffff, 0x00000100,
	0x20a8, 0xffffffff, 0x00000104,
	0x264c, 0x000c0000, 0x000c0000,
	0x2648, 0x000c0000, 0x000c0000,
	0x2f50, 0x00000001, 0x00000001,
	0x30cc, 0xc0000fff, 0x00000104,
	0xc1e4, 0x00000001, 0x00000001,
	0xd0c0, 0xfffffff0, 0x00000100,
	0xd8c0, 0xfffffff0, 0x00000100
};

static u32 verde_pg_init[] =
{
	0x353c, 0xffffffff, 0x40000,
	0x3538, 0xffffffff, 0x200010ff,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x7007,
	0x3538, 0xffffffff, 0x300010ff,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x400000,
	0x3538, 0xffffffff, 0x100010ff,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x120200,
	0x3538, 0xffffffff, 0x500010ff,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x1e1e16,
	0x3538, 0xffffffff, 0x600010ff,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x171f1e,
	0x3538, 0xffffffff, 0x700010ff,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x3538, 0xffffffff, 0x9ff,
	0x3500, 0xffffffff, 0x0,
	0x3504, 0xffffffff, 0x10000800,
	0x3504, 0xffffffff, 0xf,
	0x3504, 0xffffffff, 0xf,
	0x3500, 0xffffffff, 0x4,
	0x3504, 0xffffffff, 0x1000051e,
	0x3504, 0xffffffff, 0xffff,
	0x3504, 0xffffffff, 0xffff,
	0x3500, 0xffffffff, 0x8,
	0x3504, 0xffffffff, 0x80500,
	0x3500, 0xffffffff, 0x12,
	0x3504, 0xffffffff, 0x9050c,
	0x3500, 0xffffffff, 0x1d,
	0x3504, 0xffffffff, 0xb052c,
	0x3500, 0xffffffff, 0x2a,
	0x3504, 0xffffffff, 0x1053e,
	0x3500, 0xffffffff, 0x2d,
	0x3504, 0xffffffff, 0x10546,
	0x3500, 0xffffffff, 0x30,
	0x3504, 0xffffffff, 0xa054e,
	0x3500, 0xffffffff, 0x3c,
	0x3504, 0xffffffff, 0x1055f,
	0x3500, 0xffffffff, 0x3f,
	0x3504, 0xffffffff, 0x10567,
	0x3500, 0xffffffff, 0x42,
	0x3504, 0xffffffff, 0x1056f,
	0x3500, 0xffffffff, 0x45,
	0x3504, 0xffffffff, 0x10572,
	0x3500, 0xffffffff, 0x48,
	0x3504, 0xffffffff, 0x20575,
	0x3500, 0xffffffff, 0x4c,
	0x3504, 0xffffffff, 0x190801,
	0x3500, 0xffffffff, 0x67,
	0x3504, 0xffffffff, 0x1082a,
	0x3500, 0xffffffff, 0x6a,
	0x3504, 0xffffffff, 0x1b082d,
	0x3500, 0xffffffff, 0x87,
	0x3504, 0xffffffff, 0x310851,
	0x3500, 0xffffffff, 0xba,
	0x3504, 0xffffffff, 0x891,
	0x3500, 0xffffffff, 0xbc,
	0x3504, 0xffffffff, 0x893,
	0x3500, 0xffffffff, 0xbe,
	0x3504, 0xffffffff, 0x20895,
	0x3500, 0xffffffff, 0xc2,
	0x3504, 0xffffffff, 0x20899,
	0x3500, 0xffffffff, 0xc6,
	0x3504, 0xffffffff, 0x2089d,
	0x3500, 0xffffffff, 0xca,
	0x3504, 0xffffffff, 0x8a1,
	0x3500, 0xffffffff, 0xcc,
	0x3504, 0xffffffff, 0x8a3,
	0x3500, 0xffffffff, 0xce,
	0x3504, 0xffffffff, 0x308a5,
	0x3500, 0xffffffff, 0xd3,
	0x3504, 0xffffffff, 0x6d08cd,
	0x3500, 0xffffffff, 0x142,
	0x3504, 0xffffffff, 0x2000095a,
	0x3504, 0xffffffff, 0x1,
	0x3500, 0xffffffff, 0x144,
	0x3504, 0xffffffff, 0x301f095b,
	0x3500, 0xffffffff, 0x165,
	0x3504, 0xffffffff, 0xc094d,
	0x3500, 0xffffffff, 0x173,
	0x3504, 0xffffffff, 0xf096d,
	0x3500, 0xffffffff, 0x184,
	0x3504, 0xffffffff, 0x15097f,
	0x3500, 0xffffffff, 0x19b,
	0x3504, 0xffffffff, 0xc0998,
	0x3500, 0xffffffff, 0x1a9,
	0x3504, 0xffffffff, 0x409a7,
	0x3500, 0xffffffff, 0x1af,
	0x3504, 0xffffffff, 0xcdc,
	0x3500, 0xffffffff, 0x1b1,
	0x3504, 0xffffffff, 0x800,
	0x3508, 0xffffffff, 0x6c9b2000,
	0x3510, 0xfc00, 0x2000,
	0x3544, 0xffffffff, 0xfc0,
	0x28d4, 0x00000100, 0x100
};

static void si_init_golden_registers(struct radeon_device *rdev)
{
	switch (rdev->family) {
	case CHIP_TAHITI:
		radeon_program_register_sequence(rdev,
						 tahiti_golden_registers,
						 (const u32)ARRAY_SIZE(tahiti_golden_registers));
		radeon_program_register_sequence(rdev,
						 tahiti_golden_rlc_registers,
						 (const u32)ARRAY_SIZE(tahiti_golden_rlc_registers));
		radeon_program_register_sequence(rdev,
						 tahiti_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(tahiti_mgcg_cgcg_init));
		radeon_program_register_sequence(rdev,
						 tahiti_golden_registers2,
						 (const u32)ARRAY_SIZE(tahiti_golden_registers2));
		break;
	case CHIP_PITCAIRN:
		radeon_program_register_sequence(rdev,
						 pitcairn_golden_registers,
						 (const u32)ARRAY_SIZE(pitcairn_golden_registers));
		radeon_program_register_sequence(rdev,
						 pitcairn_golden_rlc_registers,
						 (const u32)ARRAY_SIZE(pitcairn_golden_rlc_registers));
		radeon_program_register_sequence(rdev,
						 pitcairn_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(pitcairn_mgcg_cgcg_init));
		break;
	case CHIP_VERDE:
		radeon_program_register_sequence(rdev,
						 verde_golden_registers,
						 (const u32)ARRAY_SIZE(verde_golden_registers));
		radeon_program_register_sequence(rdev,
						 verde_golden_rlc_registers,
						 (const u32)ARRAY_SIZE(verde_golden_rlc_registers));
		radeon_program_register_sequence(rdev,
						 verde_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(verde_mgcg_cgcg_init));
		radeon_program_register_sequence(rdev,
						 verde_pg_init,
						 (const u32)ARRAY_SIZE(verde_pg_init));
		break;
	case CHIP_OLAND:
		radeon_program_register_sequence(rdev,
						 oland_golden_registers,
						 (const u32)ARRAY_SIZE(oland_golden_registers));
		radeon_program_register_sequence(rdev,
						 oland_golden_rlc_registers,
						 (const u32)ARRAY_SIZE(oland_golden_rlc_registers));
		radeon_program_register_sequence(rdev,
						 oland_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(oland_mgcg_cgcg_init));
		break;
	case CHIP_HAINAN:
		radeon_program_register_sequence(rdev,
						 hainan_golden_registers,
						 (const u32)ARRAY_SIZE(hainan_golden_registers));
		radeon_program_register_sequence(rdev,
						 hainan_golden_registers2,
						 (const u32)ARRAY_SIZE(hainan_golden_registers2));
		radeon_program_register_sequence(rdev,
						 hainan_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(hainan_mgcg_cgcg_init));
		break;
	default:
		break;
	}
}

#define PCIE_BUS_CLK                10000
#define TCLK                        (PCIE_BUS_CLK / 10)

/**
 * si_get_xclk - get the xclk
 *
 * @rdev: radeon_device pointer
 *
 * Returns the reference clock used by the gfx engine
 * (SI).
 */
u32 si_get_xclk(struct radeon_device *rdev)
{
        u32 reference_clock = rdev->clock.spll.reference_freq;
	u32 tmp;

	tmp = RREG32(CG_CLKPIN_CNTL_2);
	if (tmp & MUX_TCLK_TO_XCLK)
		return TCLK;

	tmp = RREG32(CG_CLKPIN_CNTL);
	if (tmp & XTALIN_DIVIDE)
		return reference_clock / 4;

	return reference_clock;
}

/* get temperature in millidegrees */
int si_get_temp(struct radeon_device *rdev)
{
	u32 temp;
	int actual_temp = 0;

	temp = (RREG32(CG_MULT_THERMAL_STATUS) & CTF_TEMP_MASK) >>
		CTF_TEMP_SHIFT;

	if (temp & 0x200)
		actual_temp = 255;
	else
		actual_temp = temp & 0x1ff;

	actual_temp = (actual_temp * 1000);

	return actual_temp;
}

#define TAHITI_IO_MC_REGS_SIZE 36

static const u32 tahiti_io_mc_regs[TAHITI_IO_MC_REGS_SIZE][2] = {
	{0x0000006f, 0x03044000},
	{0x00000070, 0x0480c018},
	{0x00000071, 0x00000040},
	{0x00000072, 0x01000000},
	{0x00000074, 0x000000ff},
	{0x00000075, 0x00143400},
	{0x00000076, 0x08ec0800},
	{0x00000077, 0x040000cc},
	{0x00000079, 0x00000000},
	{0x0000007a, 0x21000409},
	{0x0000007c, 0x00000000},
	{0x0000007d, 0xe8000000},
	{0x0000007e, 0x044408a8},
	{0x0000007f, 0x00000003},
	{0x00000080, 0x00000000},
	{0x00000081, 0x01000000},
	{0x00000082, 0x02000000},
	{0x00000083, 0x00000000},
	{0x00000084, 0xe3f3e4f4},
	{0x00000085, 0x00052024},
	{0x00000087, 0x00000000},
	{0x00000088, 0x66036603},
	{0x00000089, 0x01000000},
	{0x0000008b, 0x1c0a0000},
	{0x0000008c, 0xff010000},
	{0x0000008e, 0xffffefff},
	{0x0000008f, 0xfff3efff},
	{0x00000090, 0xfff3efbf},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00a77400}
};

static const u32 pitcairn_io_mc_regs[TAHITI_IO_MC_REGS_SIZE][2] = {
	{0x0000006f, 0x03044000},
	{0x00000070, 0x0480c018},
	{0x00000071, 0x00000040},
	{0x00000072, 0x01000000},
	{0x00000074, 0x000000ff},
	{0x00000075, 0x00143400},
	{0x00000076, 0x08ec0800},
	{0x00000077, 0x040000cc},
	{0x00000079, 0x00000000},
	{0x0000007a, 0x21000409},
	{0x0000007c, 0x00000000},
	{0x0000007d, 0xe8000000},
	{0x0000007e, 0x044408a8},
	{0x0000007f, 0x00000003},
	{0x00000080, 0x00000000},
	{0x00000081, 0x01000000},
	{0x00000082, 0x02000000},
	{0x00000083, 0x00000000},
	{0x00000084, 0xe3f3e4f4},
	{0x00000085, 0x00052024},
	{0x00000087, 0x00000000},
	{0x00000088, 0x66036603},
	{0x00000089, 0x01000000},
	{0x0000008b, 0x1c0a0000},
	{0x0000008c, 0xff010000},
	{0x0000008e, 0xffffefff},
	{0x0000008f, 0xfff3efff},
	{0x00000090, 0xfff3efbf},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00a47400}
};

static const u32 verde_io_mc_regs[TAHITI_IO_MC_REGS_SIZE][2] = {
	{0x0000006f, 0x03044000},
	{0x00000070, 0x0480c018},
	{0x00000071, 0x00000040},
	{0x00000072, 0x01000000},
	{0x00000074, 0x000000ff},
	{0x00000075, 0x00143400},
	{0x00000076, 0x08ec0800},
	{0x00000077, 0x040000cc},
	{0x00000079, 0x00000000},
	{0x0000007a, 0x21000409},
	{0x0000007c, 0x00000000},
	{0x0000007d, 0xe8000000},
	{0x0000007e, 0x044408a8},
	{0x0000007f, 0x00000003},
	{0x00000080, 0x00000000},
	{0x00000081, 0x01000000},
	{0x00000082, 0x02000000},
	{0x00000083, 0x00000000},
	{0x00000084, 0xe3f3e4f4},
	{0x00000085, 0x00052024},
	{0x00000087, 0x00000000},
	{0x00000088, 0x66036603},
	{0x00000089, 0x01000000},
	{0x0000008b, 0x1c0a0000},
	{0x0000008c, 0xff010000},
	{0x0000008e, 0xffffefff},
	{0x0000008f, 0xfff3efff},
	{0x00000090, 0xfff3efbf},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00a37400}
};

static const u32 oland_io_mc_regs[TAHITI_IO_MC_REGS_SIZE][2] = {
	{0x0000006f, 0x03044000},
	{0x00000070, 0x0480c018},
	{0x00000071, 0x00000040},
	{0x00000072, 0x01000000},
	{0x00000074, 0x000000ff},
	{0x00000075, 0x00143400},
	{0x00000076, 0x08ec0800},
	{0x00000077, 0x040000cc},
	{0x00000079, 0x00000000},
	{0x0000007a, 0x21000409},
	{0x0000007c, 0x00000000},
	{0x0000007d, 0xe8000000},
	{0x0000007e, 0x044408a8},
	{0x0000007f, 0x00000003},
	{0x00000080, 0x00000000},
	{0x00000081, 0x01000000},
	{0x00000082, 0x02000000},
	{0x00000083, 0x00000000},
	{0x00000084, 0xe3f3e4f4},
	{0x00000085, 0x00052024},
	{0x00000087, 0x00000000},
	{0x00000088, 0x66036603},
	{0x00000089, 0x01000000},
	{0x0000008b, 0x1c0a0000},
	{0x0000008c, 0xff010000},
	{0x0000008e, 0xffffefff},
	{0x0000008f, 0xfff3efff},
	{0x00000090, 0xfff3efbf},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00a17730}
};

static const u32 hainan_io_mc_regs[TAHITI_IO_MC_REGS_SIZE][2] = {
	{0x0000006f, 0x03044000},
	{0x00000070, 0x0480c018},
	{0x00000071, 0x00000040},
	{0x00000072, 0x01000000},
	{0x00000074, 0x000000ff},
	{0x00000075, 0x00143400},
	{0x00000076, 0x08ec0800},
	{0x00000077, 0x040000cc},
	{0x00000079, 0x00000000},
	{0x0000007a, 0x21000409},
	{0x0000007c, 0x00000000},
	{0x0000007d, 0xe8000000},
	{0x0000007e, 0x044408a8},
	{0x0000007f, 0x00000003},
	{0x00000080, 0x00000000},
	{0x00000081, 0x01000000},
	{0x00000082, 0x02000000},
	{0x00000083, 0x00000000},
	{0x00000084, 0xe3f3e4f4},
	{0x00000085, 0x00052024},
	{0x00000087, 0x00000000},
	{0x00000088, 0x66036603},
	{0x00000089, 0x01000000},
	{0x0000008b, 0x1c0a0000},
	{0x0000008c, 0xff010000},
	{0x0000008e, 0xffffefff},
	{0x0000008f, 0xfff3efff},
	{0x00000090, 0xfff3efbf},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00a07730}
};

/* ucode loading */
static int si_mc_load_microcode(struct radeon_device *rdev)
{
	const __be32 *fw_data;
	u32 running, blackout = 0;
	u32 *io_mc_regs;
	int i, ucode_size, regs_size;

	if (!rdev->mc_fw)
		return -EINVAL;

	switch (rdev->family) {
	case CHIP_TAHITI:
		io_mc_regs = (u32 *)&tahiti_io_mc_regs;
		ucode_size = SI_MC_UCODE_SIZE;
		regs_size = TAHITI_IO_MC_REGS_SIZE;
		break;
	case CHIP_PITCAIRN:
		io_mc_regs = (u32 *)&pitcairn_io_mc_regs;
		ucode_size = SI_MC_UCODE_SIZE;
		regs_size = TAHITI_IO_MC_REGS_SIZE;
		break;
	case CHIP_VERDE:
	default:
		io_mc_regs = (u32 *)&verde_io_mc_regs;
		ucode_size = SI_MC_UCODE_SIZE;
		regs_size = TAHITI_IO_MC_REGS_SIZE;
		break;
	case CHIP_OLAND:
		io_mc_regs = (u32 *)&oland_io_mc_regs;
		ucode_size = OLAND_MC_UCODE_SIZE;
		regs_size = TAHITI_IO_MC_REGS_SIZE;
		break;
	case CHIP_HAINAN:
		io_mc_regs = (u32 *)&hainan_io_mc_regs;
		ucode_size = OLAND_MC_UCODE_SIZE;
		regs_size = TAHITI_IO_MC_REGS_SIZE;
		break;
	}

	running = RREG32(MC_SEQ_SUP_CNTL) & RUN_MASK;

	if (running == 0) {
		if (running) {
			blackout = RREG32(MC_SHARED_BLACKOUT_CNTL);
			WREG32(MC_SHARED_BLACKOUT_CNTL, blackout | 1);
		}

		/* reset the engine and set to writable */
		WREG32(MC_SEQ_SUP_CNTL, 0x00000008);
		WREG32(MC_SEQ_SUP_CNTL, 0x00000010);

		/* load mc io regs */
		for (i = 0; i < regs_size; i++) {
			WREG32(MC_SEQ_IO_DEBUG_INDEX, io_mc_regs[(i << 1)]);
			WREG32(MC_SEQ_IO_DEBUG_DATA, io_mc_regs[(i << 1) + 1]);
		}
		/* load the MC ucode */
		fw_data = (const __be32 *)rdev->mc_fw->data;
		for (i = 0; i < ucode_size; i++)
			WREG32(MC_SEQ_SUP_PGM, be32_to_cpup(fw_data++));

		/* put the engine back into the active state */
		WREG32(MC_SEQ_SUP_CNTL, 0x00000008);
		WREG32(MC_SEQ_SUP_CNTL, 0x00000004);
		WREG32(MC_SEQ_SUP_CNTL, 0x00000001);

		/* wait for training to complete */
		for (i = 0; i < rdev->usec_timeout; i++) {
			if (RREG32(MC_SEQ_TRAIN_WAKEUP_CNTL) & TRAIN_DONE_D0)
				break;
			udelay(1);
		}
		for (i = 0; i < rdev->usec_timeout; i++) {
			if (RREG32(MC_SEQ_TRAIN_WAKEUP_CNTL) & TRAIN_DONE_D1)
				break;
			udelay(1);
		}

		if (running)
			WREG32(MC_SHARED_BLACKOUT_CNTL, blackout);
	}

	return 0;
}

static int si_init_microcode(struct radeon_device *rdev)
{
	struct platform_device *pdev;
	const char *chip_name;
	const char *rlc_chip_name;
	size_t pfp_req_size, me_req_size, ce_req_size, rlc_req_size, mc_req_size;
	char fw_name[30];
	int err;

	DRM_DEBUG("\n");

	pdev = platform_device_register_simple("radeon_cp", 0, NULL, 0);
	err = IS_ERR(pdev);
	if (err) {
		printk(KERN_ERR "radeon_cp: Failed to register firmware\n");
		return -EINVAL;
	}

	switch (rdev->family) {
	case CHIP_TAHITI:
		chip_name = "TAHITI";
		rlc_chip_name = "TAHITI";
		pfp_req_size = SI_PFP_UCODE_SIZE * 4;
		me_req_size = SI_PM4_UCODE_SIZE * 4;
		ce_req_size = SI_CE_UCODE_SIZE * 4;
		rlc_req_size = SI_RLC_UCODE_SIZE * 4;
		mc_req_size = SI_MC_UCODE_SIZE * 4;
		break;
	case CHIP_PITCAIRN:
		chip_name = "PITCAIRN";
		rlc_chip_name = "PITCAIRN";
		pfp_req_size = SI_PFP_UCODE_SIZE * 4;
		me_req_size = SI_PM4_UCODE_SIZE * 4;
		ce_req_size = SI_CE_UCODE_SIZE * 4;
		rlc_req_size = SI_RLC_UCODE_SIZE * 4;
		mc_req_size = SI_MC_UCODE_SIZE * 4;
		break;
	case CHIP_VERDE:
		chip_name = "VERDE";
		rlc_chip_name = "VERDE";
		pfp_req_size = SI_PFP_UCODE_SIZE * 4;
		me_req_size = SI_PM4_UCODE_SIZE * 4;
		ce_req_size = SI_CE_UCODE_SIZE * 4;
		rlc_req_size = SI_RLC_UCODE_SIZE * 4;
		mc_req_size = SI_MC_UCODE_SIZE * 4;
		break;
	case CHIP_OLAND:
		chip_name = "OLAND";
		rlc_chip_name = "OLAND";
		pfp_req_size = SI_PFP_UCODE_SIZE * 4;
		me_req_size = SI_PM4_UCODE_SIZE * 4;
		ce_req_size = SI_CE_UCODE_SIZE * 4;
		rlc_req_size = SI_RLC_UCODE_SIZE * 4;
		mc_req_size = OLAND_MC_UCODE_SIZE * 4;
		break;
	case CHIP_HAINAN:
		chip_name = "HAINAN";
		rlc_chip_name = "HAINAN";
		pfp_req_size = SI_PFP_UCODE_SIZE * 4;
		me_req_size = SI_PM4_UCODE_SIZE * 4;
		ce_req_size = SI_CE_UCODE_SIZE * 4;
		rlc_req_size = SI_RLC_UCODE_SIZE * 4;
		mc_req_size = OLAND_MC_UCODE_SIZE * 4;
		break;
	default: BUG();
	}

	DRM_INFO("Loading %s Microcode\n", chip_name);

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_pfp.bin", chip_name);
	err = request_firmware(&rdev->pfp_fw, fw_name, &pdev->dev);
	if (err)
		goto out;
	if (rdev->pfp_fw->size != pfp_req_size) {
		printk(KERN_ERR
		       "si_cp: Bogus length %zu in firmware \"%s\"\n",
		       rdev->pfp_fw->size, fw_name);
		err = -EINVAL;
		goto out;
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_me.bin", chip_name);
	err = request_firmware(&rdev->me_fw, fw_name, &pdev->dev);
	if (err)
		goto out;
	if (rdev->me_fw->size != me_req_size) {
		printk(KERN_ERR
		       "si_cp: Bogus length %zu in firmware \"%s\"\n",
		       rdev->me_fw->size, fw_name);
		err = -EINVAL;
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_ce.bin", chip_name);
	err = request_firmware(&rdev->ce_fw, fw_name, &pdev->dev);
	if (err)
		goto out;
	if (rdev->ce_fw->size != ce_req_size) {
		printk(KERN_ERR
		       "si_cp: Bogus length %zu in firmware \"%s\"\n",
		       rdev->ce_fw->size, fw_name);
		err = -EINVAL;
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_rlc.bin", rlc_chip_name);
	err = request_firmware(&rdev->rlc_fw, fw_name, &pdev->dev);
	if (err)
		goto out;
	if (rdev->rlc_fw->size != rlc_req_size) {
		printk(KERN_ERR
		       "si_rlc: Bogus length %zu in firmware \"%s\"\n",
		       rdev->rlc_fw->size, fw_name);
		err = -EINVAL;
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_mc.bin", chip_name);
	err = request_firmware(&rdev->mc_fw, fw_name, &pdev->dev);
	if (err)
		goto out;
	if (rdev->mc_fw->size != mc_req_size) {
		printk(KERN_ERR
		       "si_mc: Bogus length %zu in firmware \"%s\"\n",
		       rdev->mc_fw->size, fw_name);
		err = -EINVAL;
	}

out:
	platform_device_unregister(pdev);

	if (err) {
		if (err != -EINVAL)
			printk(KERN_ERR
			       "si_cp: Failed to load firmware \"%s\"\n",
			       fw_name);
		release_firmware(rdev->pfp_fw);
		rdev->pfp_fw = NULL;
		release_firmware(rdev->me_fw);
		rdev->me_fw = NULL;
		release_firmware(rdev->ce_fw);
		rdev->ce_fw = NULL;
		release_firmware(rdev->rlc_fw);
		rdev->rlc_fw = NULL;
		release_firmware(rdev->mc_fw);
		rdev->mc_fw = NULL;
	}
	return err;
}

/* watermark setup */
static u32 dce6_line_buffer_adjust(struct radeon_device *rdev,
				   struct radeon_crtc *radeon_crtc,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *other_mode)
{
	u32 tmp;
	/*
	 * Line Buffer Setup
	 * There are 3 line buffers, each one shared by 2 display controllers.
	 * DC_LB_MEMORY_SPLIT controls how that line buffer is shared between
	 * the display controllers.  The paritioning is done via one of four
	 * preset allocations specified in bits 21:20:
	 *  0 - half lb
	 *  2 - whole lb, other crtc must be disabled
	 */
	/* this can get tricky if we have two large displays on a paired group
	 * of crtcs.  Ideally for multiple large displays we'd assign them to
	 * non-linked crtcs for maximum line buffer allocation.
	 */
	if (radeon_crtc->base.enabled && mode) {
		if (other_mode)
			tmp = 0; /* 1/2 */
		else
			tmp = 2; /* whole */
	} else
		tmp = 0;

	WREG32(DC_LB_MEMORY_SPLIT + radeon_crtc->crtc_offset,
	       DC_LB_MEMORY_CONFIG(tmp));

	if (radeon_crtc->base.enabled && mode) {
		switch (tmp) {
		case 0:
		default:
			return 4096 * 2;
		case 2:
			return 8192 * 2;
		}
	}

	/* controller not enabled, so no lb used */
	return 0;
}

static u32 si_get_number_of_dram_channels(struct radeon_device *rdev)
{
	u32 tmp = RREG32(MC_SHARED_CHMAP);

	switch ((tmp & NOOFCHAN_MASK) >> NOOFCHAN_SHIFT) {
	case 0:
	default:
		return 1;
	case 1:
		return 2;
	case 2:
		return 4;
	case 3:
		return 8;
	case 4:
		return 3;
	case 5:
		return 6;
	case 6:
		return 10;
	case 7:
		return 12;
	case 8:
		return 16;
	}
}

struct dce6_wm_params {
	u32 dram_channels; /* number of dram channels */
	u32 yclk;          /* bandwidth per dram data pin in kHz */
	u32 sclk;          /* engine clock in kHz */
	u32 disp_clk;      /* display clock in kHz */
	u32 src_width;     /* viewport width */
	u32 active_time;   /* active display time in ns */
	u32 blank_time;    /* blank time in ns */
	bool interlaced;    /* mode is interlaced */
	fixed20_12 vsc;    /* vertical scale ratio */
	u32 num_heads;     /* number of active crtcs */
	u32 bytes_per_pixel; /* bytes per pixel display + overlay */
	u32 lb_size;       /* line buffer allocated to pipe */
	u32 vtaps;         /* vertical scaler taps */
};

static u32 dce6_dram_bandwidth(struct dce6_wm_params *wm)
{
	/* Calculate raw DRAM Bandwidth */
	fixed20_12 dram_efficiency; /* 0.7 */
	fixed20_12 yclk, dram_channels, bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	yclk.full = dfixed_const(wm->yclk);
	yclk.full = dfixed_div(yclk, a);
	dram_channels.full = dfixed_const(wm->dram_channels * 4);
	a.full = dfixed_const(10);
	dram_efficiency.full = dfixed_const(7);
	dram_efficiency.full = dfixed_div(dram_efficiency, a);
	bandwidth.full = dfixed_mul(dram_channels, yclk);
	bandwidth.full = dfixed_mul(bandwidth, dram_efficiency);

	return dfixed_trunc(bandwidth);
}

static u32 dce6_dram_bandwidth_for_display(struct dce6_wm_params *wm)
{
	/* Calculate DRAM Bandwidth and the part allocated to display. */
	fixed20_12 disp_dram_allocation; /* 0.3 to 0.7 */
	fixed20_12 yclk, dram_channels, bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	yclk.full = dfixed_const(wm->yclk);
	yclk.full = dfixed_div(yclk, a);
	dram_channels.full = dfixed_const(wm->dram_channels * 4);
	a.full = dfixed_const(10);
	disp_dram_allocation.full = dfixed_const(3); /* XXX worse case value 0.3 */
	disp_dram_allocation.full = dfixed_div(disp_dram_allocation, a);
	bandwidth.full = dfixed_mul(dram_channels, yclk);
	bandwidth.full = dfixed_mul(bandwidth, disp_dram_allocation);

	return dfixed_trunc(bandwidth);
}

static u32 dce6_data_return_bandwidth(struct dce6_wm_params *wm)
{
	/* Calculate the display Data return Bandwidth */
	fixed20_12 return_efficiency; /* 0.8 */
	fixed20_12 sclk, bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	sclk.full = dfixed_const(wm->sclk);
	sclk.full = dfixed_div(sclk, a);
	a.full = dfixed_const(10);
	return_efficiency.full = dfixed_const(8);
	return_efficiency.full = dfixed_div(return_efficiency, a);
	a.full = dfixed_const(32);
	bandwidth.full = dfixed_mul(a, sclk);
	bandwidth.full = dfixed_mul(bandwidth, return_efficiency);

	return dfixed_trunc(bandwidth);
}

static u32 dce6_get_dmif_bytes_per_request(struct dce6_wm_params *wm)
{
	return 32;
}

static u32 dce6_dmif_request_bandwidth(struct dce6_wm_params *wm)
{
	/* Calculate the DMIF Request Bandwidth */
	fixed20_12 disp_clk_request_efficiency; /* 0.8 */
	fixed20_12 disp_clk, sclk, bandwidth;
	fixed20_12 a, b1, b2;
	u32 min_bandwidth;

	a.full = dfixed_const(1000);
	disp_clk.full = dfixed_const(wm->disp_clk);
	disp_clk.full = dfixed_div(disp_clk, a);
	a.full = dfixed_const(dce6_get_dmif_bytes_per_request(wm) / 2);
	b1.full = dfixed_mul(a, disp_clk);

	a.full = dfixed_const(1000);
	sclk.full = dfixed_const(wm->sclk);
	sclk.full = dfixed_div(sclk, a);
	a.full = dfixed_const(dce6_get_dmif_bytes_per_request(wm));
	b2.full = dfixed_mul(a, sclk);

	a.full = dfixed_const(10);
	disp_clk_request_efficiency.full = dfixed_const(8);
	disp_clk_request_efficiency.full = dfixed_div(disp_clk_request_efficiency, a);

	min_bandwidth = min(dfixed_trunc(b1), dfixed_trunc(b2));

	a.full = dfixed_const(min_bandwidth);
	bandwidth.full = dfixed_mul(a, disp_clk_request_efficiency);

	return dfixed_trunc(bandwidth);
}

static u32 dce6_available_bandwidth(struct dce6_wm_params *wm)
{
	/* Calculate the Available bandwidth. Display can use this temporarily but not in average. */
	u32 dram_bandwidth = dce6_dram_bandwidth(wm);
	u32 data_return_bandwidth = dce6_data_return_bandwidth(wm);
	u32 dmif_req_bandwidth = dce6_dmif_request_bandwidth(wm);

	return min(dram_bandwidth, min(data_return_bandwidth, dmif_req_bandwidth));
}

static u32 dce6_average_bandwidth(struct dce6_wm_params *wm)
{
	/* Calculate the display mode Average Bandwidth
	 * DisplayMode should contain the source and destination dimensions,
	 * timing, etc.
	 */
	fixed20_12 bpp;
	fixed20_12 line_time;
	fixed20_12 src_width;
	fixed20_12 bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	line_time.full = dfixed_const(wm->active_time + wm->blank_time);
	line_time.full = dfixed_div(line_time, a);
	bpp.full = dfixed_const(wm->bytes_per_pixel);
	src_width.full = dfixed_const(wm->src_width);
	bandwidth.full = dfixed_mul(src_width, bpp);
	bandwidth.full = dfixed_mul(bandwidth, wm->vsc);
	bandwidth.full = dfixed_div(bandwidth, line_time);

	return dfixed_trunc(bandwidth);
}

static u32 dce6_latency_watermark(struct dce6_wm_params *wm)
{
	/* First calcualte the latency in ns */
	u32 mc_latency = 2000; /* 2000 ns. */
	u32 available_bandwidth = dce6_available_bandwidth(wm);
	u32 worst_chunk_return_time = (512 * 8 * 1000) / available_bandwidth;
	u32 cursor_line_pair_return_time = (128 * 4 * 1000) / available_bandwidth;
	u32 dc_latency = 40000000 / wm->disp_clk; /* dc pipe latency */
	u32 other_heads_data_return_time = ((wm->num_heads + 1) * worst_chunk_return_time) +
		(wm->num_heads * cursor_line_pair_return_time);
	u32 latency = mc_latency + other_heads_data_return_time + dc_latency;
	u32 max_src_lines_per_dst_line, lb_fill_bw, line_fill_time;
	u32 tmp, dmif_size = 12288;
	fixed20_12 a, b, c;

	if (wm->num_heads == 0)
		return 0;

	a.full = dfixed_const(2);
	b.full = dfixed_const(1);
	if ((wm->vsc.full > a.full) ||
	    ((wm->vsc.full > b.full) && (wm->vtaps >= 3)) ||
	    (wm->vtaps >= 5) ||
	    ((wm->vsc.full >= a.full) && wm->interlaced))
		max_src_lines_per_dst_line = 4;
	else
		max_src_lines_per_dst_line = 2;

	a.full = dfixed_const(available_bandwidth);
	b.full = dfixed_const(wm->num_heads);
	a.full = dfixed_div(a, b);

	b.full = dfixed_const(mc_latency + 512);
	c.full = dfixed_const(wm->disp_clk);
	b.full = dfixed_div(b, c);

	c.full = dfixed_const(dmif_size);
	b.full = dfixed_div(c, b);

	tmp = min(dfixed_trunc(a), dfixed_trunc(b));

	b.full = dfixed_const(1000);
	c.full = dfixed_const(wm->disp_clk);
	b.full = dfixed_div(c, b);
	c.full = dfixed_const(wm->bytes_per_pixel);
	b.full = dfixed_mul(b, c);

	lb_fill_bw = min(tmp, dfixed_trunc(b));

	a.full = dfixed_const(max_src_lines_per_dst_line * wm->src_width * wm->bytes_per_pixel);
	b.full = dfixed_const(1000);
	c.full = dfixed_const(lb_fill_bw);
	b.full = dfixed_div(c, b);
	a.full = dfixed_div(a, b);
	line_fill_time = dfixed_trunc(a);

	if (line_fill_time < wm->active_time)
		return latency;
	else
		return latency + (line_fill_time - wm->active_time);

}

static bool dce6_average_bandwidth_vs_dram_bandwidth_for_display(struct dce6_wm_params *wm)
{
	if (dce6_average_bandwidth(wm) <=
	    (dce6_dram_bandwidth_for_display(wm) / wm->num_heads))
		return true;
	else
		return false;
};

static bool dce6_average_bandwidth_vs_available_bandwidth(struct dce6_wm_params *wm)
{
	if (dce6_average_bandwidth(wm) <=
	    (dce6_available_bandwidth(wm) / wm->num_heads))
		return true;
	else
		return false;
};

static bool dce6_check_latency_hiding(struct dce6_wm_params *wm)
{
	u32 lb_partitions = wm->lb_size / wm->src_width;
	u32 line_time = wm->active_time + wm->blank_time;
	u32 latency_tolerant_lines;
	u32 latency_hiding;
	fixed20_12 a;

	a.full = dfixed_const(1);
	if (wm->vsc.full > a.full)
		latency_tolerant_lines = 1;
	else {
		if (lb_partitions <= (wm->vtaps + 1))
			latency_tolerant_lines = 1;
		else
			latency_tolerant_lines = 2;
	}

	latency_hiding = (latency_tolerant_lines * line_time + wm->blank_time);

	if (dce6_latency_watermark(wm) <= latency_hiding)
		return true;
	else
		return false;
}

static void dce6_program_watermarks(struct radeon_device *rdev,
					 struct radeon_crtc *radeon_crtc,
					 u32 lb_size, u32 num_heads)
{
	struct drm_display_mode *mode = &radeon_crtc->base.mode;
	struct dce6_wm_params wm;
	u32 pixel_period;
	u32 line_time = 0;
	u32 latency_watermark_a = 0, latency_watermark_b = 0;
	u32 priority_a_mark = 0, priority_b_mark = 0;
	u32 priority_a_cnt = PRIORITY_OFF;
	u32 priority_b_cnt = PRIORITY_OFF;
	u32 tmp, arb_control3;
	fixed20_12 a, b, c;

	if (radeon_crtc->base.enabled && num_heads && mode) {
		pixel_period = 1000000 / (u32)mode->clock;
		line_time = min((u32)mode->crtc_htotal * pixel_period, (u32)65535);
		priority_a_cnt = 0;
		priority_b_cnt = 0;

		wm.yclk = rdev->pm.current_mclk * 10;
		wm.sclk = rdev->pm.current_sclk * 10;
		wm.disp_clk = mode->clock;
		wm.src_width = mode->crtc_hdisplay;
		wm.active_time = mode->crtc_hdisplay * pixel_period;
		wm.blank_time = line_time - wm.active_time;
		wm.interlaced = false;
		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			wm.interlaced = true;
		wm.vsc = radeon_crtc->vsc;
		wm.vtaps = 1;
		if (radeon_crtc->rmx_type != RMX_OFF)
			wm.vtaps = 2;
		wm.bytes_per_pixel = 4; /* XXX: get this from fb config */
		wm.lb_size = lb_size;
		if (rdev->family == CHIP_ARUBA)
			wm.dram_channels = evergreen_get_number_of_dram_channels(rdev);
		else
			wm.dram_channels = si_get_number_of_dram_channels(rdev);
		wm.num_heads = num_heads;

		/* set for high clocks */
		latency_watermark_a = min(dce6_latency_watermark(&wm), (u32)65535);
		/* set for low clocks */
		/* wm.yclk = low clk; wm.sclk = low clk */
		latency_watermark_b = min(dce6_latency_watermark(&wm), (u32)65535);

		/* possibly force display priority to high */
		/* should really do this at mode validation time... */
		if (!dce6_average_bandwidth_vs_dram_bandwidth_for_display(&wm) ||
		    !dce6_average_bandwidth_vs_available_bandwidth(&wm) ||
		    !dce6_check_latency_hiding(&wm) ||
		    (rdev->disp_priority == 2)) {
			DRM_DEBUG_KMS("force priority to high\n");
			priority_a_cnt |= PRIORITY_ALWAYS_ON;
			priority_b_cnt |= PRIORITY_ALWAYS_ON;
		}

		a.full = dfixed_const(1000);
		b.full = dfixed_const(mode->clock);
		b.full = dfixed_div(b, a);
		c.full = dfixed_const(latency_watermark_a);
		c.full = dfixed_mul(c, b);
		c.full = dfixed_mul(c, radeon_crtc->hsc);
		c.full = dfixed_div(c, a);
		a.full = dfixed_const(16);
		c.full = dfixed_div(c, a);
		priority_a_mark = dfixed_trunc(c);
		priority_a_cnt |= priority_a_mark & PRIORITY_MARK_MASK;

		a.full = dfixed_const(1000);
		b.full = dfixed_const(mode->clock);
		b.full = dfixed_div(b, a);
		c.full = dfixed_const(latency_watermark_b);
		c.full = dfixed_mul(c, b);
		c.full = dfixed_mul(c, radeon_crtc->hsc);
		c.full = dfixed_div(c, a);
		a.full = dfixed_const(16);
		c.full = dfixed_div(c, a);
		priority_b_mark = dfixed_trunc(c);
		priority_b_cnt |= priority_b_mark & PRIORITY_MARK_MASK;
	}

	/* select wm A */
	arb_control3 = RREG32(DPG_PIPE_ARBITRATION_CONTROL3 + radeon_crtc->crtc_offset);
	tmp = arb_control3;
	tmp &= ~LATENCY_WATERMARK_MASK(3);
	tmp |= LATENCY_WATERMARK_MASK(1);
	WREG32(DPG_PIPE_ARBITRATION_CONTROL3 + radeon_crtc->crtc_offset, tmp);
	WREG32(DPG_PIPE_LATENCY_CONTROL + radeon_crtc->crtc_offset,
	       (LATENCY_LOW_WATERMARK(latency_watermark_a) |
		LATENCY_HIGH_WATERMARK(line_time)));
	/* select wm B */
	tmp = RREG32(DPG_PIPE_ARBITRATION_CONTROL3 + radeon_crtc->crtc_offset);
	tmp &= ~LATENCY_WATERMARK_MASK(3);
	tmp |= LATENCY_WATERMARK_MASK(2);
	WREG32(DPG_PIPE_ARBITRATION_CONTROL3 + radeon_crtc->crtc_offset, tmp);
	WREG32(DPG_PIPE_LATENCY_CONTROL + radeon_crtc->crtc_offset,
	       (LATENCY_LOW_WATERMARK(latency_watermark_b) |
		LATENCY_HIGH_WATERMARK(line_time)));
	/* restore original selection */
	WREG32(DPG_PIPE_ARBITRATION_CONTROL3 + radeon_crtc->crtc_offset, arb_control3);

	/* write the priority marks */
	WREG32(PRIORITY_A_CNT + radeon_crtc->crtc_offset, priority_a_cnt);
	WREG32(PRIORITY_B_CNT + radeon_crtc->crtc_offset, priority_b_cnt);

}

void dce6_bandwidth_update(struct radeon_device *rdev)
{
	struct drm_display_mode *mode0 = NULL;
	struct drm_display_mode *mode1 = NULL;
	u32 num_heads = 0, lb_size;
	int i;

	radeon_update_display_priority(rdev);

	for (i = 0; i < rdev->num_crtc; i++) {
		if (rdev->mode_info.crtcs[i]->base.enabled)
			num_heads++;
	}
	for (i = 0; i < rdev->num_crtc; i += 2) {
		mode0 = &rdev->mode_info.crtcs[i]->base.mode;
		mode1 = &rdev->mode_info.crtcs[i+1]->base.mode;
		lb_size = dce6_line_buffer_adjust(rdev, rdev->mode_info.crtcs[i], mode0, mode1);
		dce6_program_watermarks(rdev, rdev->mode_info.crtcs[i], lb_size, num_heads);
		lb_size = dce6_line_buffer_adjust(rdev, rdev->mode_info.crtcs[i+1], mode1, mode0);
		dce6_program_watermarks(rdev, rdev->mode_info.crtcs[i+1], lb_size, num_heads);
	}
}

/*
 * Core functions
 */
static void si_tiling_mode_table_init(struct radeon_device *rdev)
{
	const u32 num_tile_mode_states = 32;
	u32 reg_offset, gb_tile_moden, split_equal_to_row_size;

	switch (rdev->config.si.mem_row_size_in_kb) {
	case 1:
		split_equal_to_row_size = ADDR_SURF_TILE_SPLIT_1KB;
		break;
	case 2:
	default:
		split_equal_to_row_size = ADDR_SURF_TILE_SPLIT_2KB;
		break;
	case 4:
		split_equal_to_row_size = ADDR_SURF_TILE_SPLIT_4KB;
		break;
	}

	if ((rdev->family == CHIP_TAHITI) ||
	    (rdev->family == CHIP_PITCAIRN)) {
		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:  /* non-AA compressed depth or any compressed stencil */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 1:  /* 2xAA/4xAA compressed depth only */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 2:  /* 8xAA compressed depth only */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 3:  /* 2xAA/4xAA compressed depth with stencil (for depth buffer) */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 4:  /* Maps w/ a dimension less than the 2D macro-tile dimensions (for mipmapped depth textures) */
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 5:  /* Uncompressed 16bpp depth - and stencil buffer allocated with it */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 6:  /* Uncompressed 32bpp depth - and stencil buffer allocated with it */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			case 7:  /* Uncompressed 8bpp stencil without depth (drivers typically do not use) */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 8:  /* 1D and 1D Array Surfaces */
				gb_tile_moden = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 9:  /* Displayable maps. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 10:  /* Display 8bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 11:  /* Display 16bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 12:  /* Display 32bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			case 13:  /* Thin. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 14:  /* Thin 8 bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			case 15:  /* Thin 16 bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			case 16:  /* Thin 32 bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			case 17:  /* Thin 64 bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			case 21:  /* 8 bpp PRT. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 22:  /* 16 bpp PRT */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 23:  /* 32 bpp PRT */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 24:  /* 64 bpp PRT */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 25:  /* 128 bpp PRT */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_1KB) |
						 NUM_BANKS(ADDR_SURF_8_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			rdev->config.si.tile_mode_array[reg_offset] = gb_tile_moden;
			WREG32(GB_TILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
	} else if ((rdev->family == CHIP_VERDE) ||
		   (rdev->family == CHIP_OLAND) ||
		   (rdev->family == CHIP_HAINAN)) {
		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:  /* non-AA compressed depth or any compressed stencil */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 1:  /* 2xAA/4xAA compressed depth only */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 2:  /* 8xAA compressed depth only */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 3:  /* 2xAA/4xAA compressed depth with stencil (for depth buffer) */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 4:  /* Maps w/ a dimension less than the 2D macro-tile dimensions (for mipmapped depth textures) */
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 5:  /* Uncompressed 16bpp depth - and stencil buffer allocated with it */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 6:  /* Uncompressed 32bpp depth - and stencil buffer allocated with it */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 7:  /* Uncompressed 8bpp stencil without depth (drivers typically do not use) */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 8:  /* 1D and 1D Array Surfaces */
				gb_tile_moden = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 9:  /* Displayable maps. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 10:  /* Display 8bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 11:  /* Display 16bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 12:  /* Display 32bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 13:  /* Thin. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 14:  /* Thin 8 bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 15:  /* Thin 16 bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 16:  /* Thin 32 bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 17:  /* Thin 64 bpp. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
						 TILE_SPLIT(split_equal_to_row_size) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 21:  /* 8 bpp PRT. */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 22:  /* 16 bpp PRT */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
				break;
			case 23:  /* 32 bpp PRT */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 24:  /* 64 bpp PRT */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
						 NUM_BANKS(ADDR_SURF_16_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
				break;
			case 25:  /* 128 bpp PRT */
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_1KB) |
						 NUM_BANKS(ADDR_SURF_8_BANK) |
						 BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			rdev->config.si.tile_mode_array[reg_offset] = gb_tile_moden;
			WREG32(GB_TILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
	} else
		DRM_ERROR("unknown asic: 0x%x\n", rdev->family);
}

static void si_select_se_sh(struct radeon_device *rdev,
			    u32 se_num, u32 sh_num)
{
	u32 data = INSTANCE_BROADCAST_WRITES;

	if ((se_num == 0xffffffff) && (sh_num == 0xffffffff))
		data |= SH_BROADCAST_WRITES | SE_BROADCAST_WRITES;
	else if (se_num == 0xffffffff)
		data |= SE_BROADCAST_WRITES | SH_INDEX(sh_num);
	else if (sh_num == 0xffffffff)
		data |= SH_BROADCAST_WRITES | SE_INDEX(se_num);
	else
		data |= SH_INDEX(sh_num) | SE_INDEX(se_num);
	WREG32(GRBM_GFX_INDEX, data);
}

static u32 si_create_bitmask(u32 bit_width)
{
	u32 i, mask = 0;

	for (i = 0; i < bit_width; i++) {
		mask <<= 1;
		mask |= 1;
	}
	return mask;
}

static u32 si_get_cu_enabled(struct radeon_device *rdev, u32 cu_per_sh)
{
	u32 data, mask;

	data = RREG32(CC_GC_SHADER_ARRAY_CONFIG);
	if (data & 1)
		data &= INACTIVE_CUS_MASK;
	else
		data = 0;
	data |= RREG32(GC_USER_SHADER_ARRAY_CONFIG);

	data >>= INACTIVE_CUS_SHIFT;

	mask = si_create_bitmask(cu_per_sh);

	return ~data & mask;
}

static void si_setup_spi(struct radeon_device *rdev,
			 u32 se_num, u32 sh_per_se,
			 u32 cu_per_sh)
{
	int i, j, k;
	u32 data, mask, active_cu;

	for (i = 0; i < se_num; i++) {
		for (j = 0; j < sh_per_se; j++) {
			si_select_se_sh(rdev, i, j);
			data = RREG32(SPI_STATIC_THREAD_MGMT_3);
			active_cu = si_get_cu_enabled(rdev, cu_per_sh);

			mask = 1;
			for (k = 0; k < 16; k++) {
				mask <<= k;
				if (active_cu & mask) {
					data &= ~mask;
					WREG32(SPI_STATIC_THREAD_MGMT_3, data);
					break;
				}
			}
		}
	}
	si_select_se_sh(rdev, 0xffffffff, 0xffffffff);
}

static u32 si_get_rb_disabled(struct radeon_device *rdev,
			      u32 max_rb_num, u32 se_num,
			      u32 sh_per_se)
{
	u32 data, mask;

	data = RREG32(CC_RB_BACKEND_DISABLE);
	if (data & 1)
		data &= BACKEND_DISABLE_MASK;
	else
		data = 0;
	data |= RREG32(GC_USER_RB_BACKEND_DISABLE);

	data >>= BACKEND_DISABLE_SHIFT;

	mask = si_create_bitmask(max_rb_num / se_num / sh_per_se);

	return data & mask;
}

static void si_setup_rb(struct radeon_device *rdev,
			u32 se_num, u32 sh_per_se,
			u32 max_rb_num)
{
	int i, j;
	u32 data, mask;
	u32 disabled_rbs = 0;
	u32 enabled_rbs = 0;

	for (i = 0; i < se_num; i++) {
		for (j = 0; j < sh_per_se; j++) {
			si_select_se_sh(rdev, i, j);
			data = si_get_rb_disabled(rdev, max_rb_num, se_num, sh_per_se);
			disabled_rbs |= data << ((i * sh_per_se + j) * TAHITI_RB_BITMAP_WIDTH_PER_SH);
		}
	}
	si_select_se_sh(rdev, 0xffffffff, 0xffffffff);

	mask = 1;
	for (i = 0; i < max_rb_num; i++) {
		if (!(disabled_rbs & mask))
			enabled_rbs |= mask;
		mask <<= 1;
	}

	for (i = 0; i < se_num; i++) {
		si_select_se_sh(rdev, i, 0xffffffff);
		data = 0;
		for (j = 0; j < sh_per_se; j++) {
			switch (enabled_rbs & 3) {
			case 1:
				data |= (RASTER_CONFIG_RB_MAP_0 << (i * sh_per_se + j) * 2);
				break;
			case 2:
				data |= (RASTER_CONFIG_RB_MAP_3 << (i * sh_per_se + j) * 2);
				break;
			case 3:
			default:
				data |= (RASTER_CONFIG_RB_MAP_2 << (i * sh_per_se + j) * 2);
				break;
			}
			enabled_rbs >>= 2;
		}
		WREG32(PA_SC_RASTER_CONFIG, data);
	}
	si_select_se_sh(rdev, 0xffffffff, 0xffffffff);
}

static void si_gpu_init(struct radeon_device *rdev)
{
	u32 gb_addr_config = 0;
	u32 mc_shared_chmap, mc_arb_ramcfg;
	u32 sx_debug_1;
	u32 hdp_host_path_cntl;
	u32 tmp;
	int i, j;

	switch (rdev->family) {
	case CHIP_TAHITI:
		rdev->config.si.max_shader_engines = 2;
		rdev->config.si.max_tile_pipes = 12;
		rdev->config.si.max_cu_per_sh = 8;
		rdev->config.si.max_sh_per_se = 2;
		rdev->config.si.max_backends_per_se = 4;
		rdev->config.si.max_texture_channel_caches = 12;
		rdev->config.si.max_gprs = 256;
		rdev->config.si.max_gs_threads = 32;
		rdev->config.si.max_hw_contexts = 8;

		rdev->config.si.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.si.sc_prim_fifo_size_backend = 0x100;
		rdev->config.si.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.si.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TAHITI_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_PITCAIRN:
		rdev->config.si.max_shader_engines = 2;
		rdev->config.si.max_tile_pipes = 8;
		rdev->config.si.max_cu_per_sh = 5;
		rdev->config.si.max_sh_per_se = 2;
		rdev->config.si.max_backends_per_se = 4;
		rdev->config.si.max_texture_channel_caches = 8;
		rdev->config.si.max_gprs = 256;
		rdev->config.si.max_gs_threads = 32;
		rdev->config.si.max_hw_contexts = 8;

		rdev->config.si.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.si.sc_prim_fifo_size_backend = 0x100;
		rdev->config.si.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.si.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TAHITI_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_VERDE:
	default:
		rdev->config.si.max_shader_engines = 1;
		rdev->config.si.max_tile_pipes = 4;
		rdev->config.si.max_cu_per_sh = 5;
		rdev->config.si.max_sh_per_se = 2;
		rdev->config.si.max_backends_per_se = 4;
		rdev->config.si.max_texture_channel_caches = 4;
		rdev->config.si.max_gprs = 256;
		rdev->config.si.max_gs_threads = 32;
		rdev->config.si.max_hw_contexts = 8;

		rdev->config.si.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.si.sc_prim_fifo_size_backend = 0x40;
		rdev->config.si.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.si.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = VERDE_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_OLAND:
		rdev->config.si.max_shader_engines = 1;
		rdev->config.si.max_tile_pipes = 4;
		rdev->config.si.max_cu_per_sh = 6;
		rdev->config.si.max_sh_per_se = 1;
		rdev->config.si.max_backends_per_se = 2;
		rdev->config.si.max_texture_channel_caches = 4;
		rdev->config.si.max_gprs = 256;
		rdev->config.si.max_gs_threads = 16;
		rdev->config.si.max_hw_contexts = 8;

		rdev->config.si.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.si.sc_prim_fifo_size_backend = 0x40;
		rdev->config.si.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.si.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = VERDE_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_HAINAN:
		rdev->config.si.max_shader_engines = 1;
		rdev->config.si.max_tile_pipes = 4;
		rdev->config.si.max_cu_per_sh = 5;
		rdev->config.si.max_sh_per_se = 1;
		rdev->config.si.max_backends_per_se = 1;
		rdev->config.si.max_texture_channel_caches = 2;
		rdev->config.si.max_gprs = 256;
		rdev->config.si.max_gs_threads = 16;
		rdev->config.si.max_hw_contexts = 8;

		rdev->config.si.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.si.sc_prim_fifo_size_backend = 0x40;
		rdev->config.si.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.si.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = HAINAN_GB_ADDR_CONFIG_GOLDEN;
		break;
	}

	/* Initialize HDP */
	for (i = 0, j = 0; i < 32; i++, j += 0x18) {
		WREG32((0x2c14 + j), 0x00000000);
		WREG32((0x2c18 + j), 0x00000000);
		WREG32((0x2c1c + j), 0x00000000);
		WREG32((0x2c20 + j), 0x00000000);
		WREG32((0x2c24 + j), 0x00000000);
	}

	WREG32(GRBM_CNTL, GRBM_READ_TIMEOUT(0xff));

	evergreen_fix_pci_max_read_req_size(rdev);

	WREG32(BIF_FB_EN, FB_READ_EN | FB_WRITE_EN);

	mc_shared_chmap = RREG32(MC_SHARED_CHMAP);
	mc_arb_ramcfg = RREG32(MC_ARB_RAMCFG);

	rdev->config.si.num_tile_pipes = rdev->config.si.max_tile_pipes;
	rdev->config.si.mem_max_burst_length_bytes = 256;
	tmp = (mc_arb_ramcfg & NOOFCOLS_MASK) >> NOOFCOLS_SHIFT;
	rdev->config.si.mem_row_size_in_kb = (4 * (1 << (8 + tmp))) / 1024;
	if (rdev->config.si.mem_row_size_in_kb > 4)
		rdev->config.si.mem_row_size_in_kb = 4;
	/* XXX use MC settings? */
	rdev->config.si.shader_engine_tile_size = 32;
	rdev->config.si.num_gpus = 1;
	rdev->config.si.multi_gpu_tile_size = 64;

	/* fix up row size */
	gb_addr_config &= ~ROW_SIZE_MASK;
	switch (rdev->config.si.mem_row_size_in_kb) {
	case 1:
	default:
		gb_addr_config |= ROW_SIZE(0);
		break;
	case 2:
		gb_addr_config |= ROW_SIZE(1);
		break;
	case 4:
		gb_addr_config |= ROW_SIZE(2);
		break;
	}

	/* setup tiling info dword.  gb_addr_config is not adequate since it does
	 * not have bank info, so create a custom tiling dword.
	 * bits 3:0   num_pipes
	 * bits 7:4   num_banks
	 * bits 11:8  group_size
	 * bits 15:12 row_size
	 */
	rdev->config.si.tile_config = 0;
	switch (rdev->config.si.num_tile_pipes) {
	case 1:
		rdev->config.si.tile_config |= (0 << 0);
		break;
	case 2:
		rdev->config.si.tile_config |= (1 << 0);
		break;
	case 4:
		rdev->config.si.tile_config |= (2 << 0);
		break;
	case 8:
	default:
		/* XXX what about 12? */
		rdev->config.si.tile_config |= (3 << 0);
		break;
	}	
	switch ((mc_arb_ramcfg & NOOFBANK_MASK) >> NOOFBANK_SHIFT) {
	case 0: /* four banks */
		rdev->config.si.tile_config |= 0 << 4;
		break;
	case 1: /* eight banks */
		rdev->config.si.tile_config |= 1 << 4;
		break;
	case 2: /* sixteen banks */
	default:
		rdev->config.si.tile_config |= 2 << 4;
		break;
	}
	rdev->config.si.tile_config |=
		((gb_addr_config & PIPE_INTERLEAVE_SIZE_MASK) >> PIPE_INTERLEAVE_SIZE_SHIFT) << 8;
	rdev->config.si.tile_config |=
		((gb_addr_config & ROW_SIZE_MASK) >> ROW_SIZE_SHIFT) << 12;

	WREG32(GB_ADDR_CONFIG, gb_addr_config);
	WREG32(DMIF_ADDR_CONFIG, gb_addr_config);
	WREG32(DMIF_ADDR_CALC, gb_addr_config);
	WREG32(HDP_ADDR_CONFIG, gb_addr_config);
	WREG32(DMA_TILING_CONFIG + DMA0_REGISTER_OFFSET, gb_addr_config);
	WREG32(DMA_TILING_CONFIG + DMA1_REGISTER_OFFSET, gb_addr_config);
	if (rdev->has_uvd) {
		WREG32(UVD_UDEC_ADDR_CONFIG, gb_addr_config);
		WREG32(UVD_UDEC_DB_ADDR_CONFIG, gb_addr_config);
		WREG32(UVD_UDEC_DBW_ADDR_CONFIG, gb_addr_config);
	}

	si_tiling_mode_table_init(rdev);

	si_setup_rb(rdev, rdev->config.si.max_shader_engines,
		    rdev->config.si.max_sh_per_se,
		    rdev->config.si.max_backends_per_se);

	si_setup_spi(rdev, rdev->config.si.max_shader_engines,
		     rdev->config.si.max_sh_per_se,
		     rdev->config.si.max_cu_per_sh);


	/* set HW defaults for 3D engine */
	WREG32(CP_QUEUE_THRESHOLDS, (ROQ_IB1_START(0x16) |
				     ROQ_IB2_START(0x2b)));
	WREG32(CP_MEQ_THRESHOLDS, MEQ1_START(0x30) | MEQ2_START(0x60));

	sx_debug_1 = RREG32(SX_DEBUG_1);
	WREG32(SX_DEBUG_1, sx_debug_1);

	WREG32(SPI_CONFIG_CNTL_1, VTX_DONE_DELAY(4));

	WREG32(PA_SC_FIFO_SIZE, (SC_FRONTEND_PRIM_FIFO_SIZE(rdev->config.si.sc_prim_fifo_size_frontend) |
				 SC_BACKEND_PRIM_FIFO_SIZE(rdev->config.si.sc_prim_fifo_size_backend) |
				 SC_HIZ_TILE_FIFO_SIZE(rdev->config.si.sc_hiz_tile_fifo_size) |
				 SC_EARLYZ_TILE_FIFO_SIZE(rdev->config.si.sc_earlyz_tile_fifo_size)));

	WREG32(VGT_NUM_INSTANCES, 1);

	WREG32(CP_PERFMON_CNTL, 0);

	WREG32(SQ_CONFIG, 0);

	WREG32(PA_SC_FORCE_EOV_MAX_CNTS, (FORCE_EOV_MAX_CLK_CNT(4095) |
					  FORCE_EOV_MAX_REZ_CNT(255)));

	WREG32(VGT_CACHE_INVALIDATION, CACHE_INVALIDATION(VC_AND_TC) |
	       AUTO_INVLD_EN(ES_AND_GS_AUTO));

	WREG32(VGT_GS_VERTEX_REUSE, 16);
	WREG32(PA_SC_LINE_STIPPLE_STATE, 0);

	WREG32(CB_PERFCOUNTER0_SELECT0, 0);
	WREG32(CB_PERFCOUNTER0_SELECT1, 0);
	WREG32(CB_PERFCOUNTER1_SELECT0, 0);
	WREG32(CB_PERFCOUNTER1_SELECT1, 0);
	WREG32(CB_PERFCOUNTER2_SELECT0, 0);
	WREG32(CB_PERFCOUNTER2_SELECT1, 0);
	WREG32(CB_PERFCOUNTER3_SELECT0, 0);
	WREG32(CB_PERFCOUNTER3_SELECT1, 0);

	tmp = RREG32(HDP_MISC_CNTL);
	tmp |= HDP_FLUSH_INVALIDATE_CACHE;
	WREG32(HDP_MISC_CNTL, tmp);

	hdp_host_path_cntl = RREG32(HDP_HOST_PATH_CNTL);
	WREG32(HDP_HOST_PATH_CNTL, hdp_host_path_cntl);

	WREG32(PA_CL_ENHANCE, CLIP_VTX_REORDER_ENA | NUM_CLIP_SEQ(3));

	udelay(50);
}

/*
 * GPU scratch registers helpers function.
 */
static void si_scratch_init(struct radeon_device *rdev)
{
	int i;

	rdev->scratch.num_reg = 7;
	rdev->scratch.reg_base = SCRATCH_REG0;
	for (i = 0; i < rdev->scratch.num_reg; i++) {
		rdev->scratch.free[i] = true;
		rdev->scratch.reg[i] = rdev->scratch.reg_base + (i * 4);
	}
}

void si_fence_ring_emit(struct radeon_device *rdev,
			struct radeon_fence *fence)
{
	struct radeon_ring *ring = &rdev->ring[fence->ring];
	u64 addr = rdev->fence_drv[fence->ring].gpu_addr;

	/* flush read cache over gart */
	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	radeon_ring_write(ring, (CP_COHER_CNTL2 - PACKET3_SET_CONFIG_REG_START) >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, PACKET3(PACKET3_SURFACE_SYNC, 3));
	radeon_ring_write(ring, PACKET3_TCL1_ACTION_ENA |
			  PACKET3_TC_ACTION_ENA |
			  PACKET3_SH_KCACHE_ACTION_ENA |
			  PACKET3_SH_ICACHE_ACTION_ENA);
	radeon_ring_write(ring, 0xFFFFFFFF);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 10); /* poll interval */
	/* EVENT_WRITE_EOP - flush caches, send int */
	radeon_ring_write(ring, PACKET3(PACKET3_EVENT_WRITE_EOP, 4));
	radeon_ring_write(ring, EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) | EVENT_INDEX(5));
	radeon_ring_write(ring, addr & 0xffffffff);
	radeon_ring_write(ring, (upper_32_bits(addr) & 0xff) | DATA_SEL(1) | INT_SEL(2));
	radeon_ring_write(ring, fence->seq);
	radeon_ring_write(ring, 0);
}

/*
 * IB stuff
 */
void si_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib)
{
	struct radeon_ring *ring = &rdev->ring[ib->ring];
	u32 header;

	if (ib->is_const_ib) {
		/* set switch buffer packet before const IB */
		radeon_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
		radeon_ring_write(ring, 0);

		header = PACKET3(PACKET3_INDIRECT_BUFFER_CONST, 2);
	} else {
		u32 next_rptr;
		if (ring->rptr_save_reg) {
			next_rptr = ring->wptr + 3 + 4 + 8;
			radeon_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
			radeon_ring_write(ring, ((ring->rptr_save_reg -
						  PACKET3_SET_CONFIG_REG_START) >> 2));
			radeon_ring_write(ring, next_rptr);
		} else if (rdev->wb.enabled) {
			next_rptr = ring->wptr + 5 + 4 + 8;
			radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
			radeon_ring_write(ring, (1 << 8));
			radeon_ring_write(ring, ring->next_rptr_gpu_addr & 0xfffffffc);
			radeon_ring_write(ring, upper_32_bits(ring->next_rptr_gpu_addr) & 0xffffffff);
			radeon_ring_write(ring, next_rptr);
		}

		header = PACKET3(PACKET3_INDIRECT_BUFFER, 2);
	}

	radeon_ring_write(ring, header);
	radeon_ring_write(ring,
#ifdef __BIG_ENDIAN
			  (2 << 0) |
#endif
			  (ib->gpu_addr & 0xFFFFFFFC));
	radeon_ring_write(ring, upper_32_bits(ib->gpu_addr) & 0xFFFF);
	radeon_ring_write(ring, ib->length_dw |
			  (ib->vm ? (ib->vm->id << 24) : 0));

	if (!ib->is_const_ib) {
		/* flush read cache over gart for this vmid */
		radeon_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
		radeon_ring_write(ring, (CP_COHER_CNTL2 - PACKET3_SET_CONFIG_REG_START) >> 2);
		radeon_ring_write(ring, ib->vm ? ib->vm->id : 0);
		radeon_ring_write(ring, PACKET3(PACKET3_SURFACE_SYNC, 3));
		radeon_ring_write(ring, PACKET3_TCL1_ACTION_ENA |
				  PACKET3_TC_ACTION_ENA |
				  PACKET3_SH_KCACHE_ACTION_ENA |
				  PACKET3_SH_ICACHE_ACTION_ENA);
		radeon_ring_write(ring, 0xFFFFFFFF);
		radeon_ring_write(ring, 0);
		radeon_ring_write(ring, 10); /* poll interval */
	}
}

/*
 * CP.
 */
static void si_cp_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32(CP_ME_CNTL, 0);
	else {
		radeon_ttm_set_active_vram_size(rdev, rdev->mc.visible_vram_size);
		WREG32(CP_ME_CNTL, (CP_ME_HALT | CP_PFP_HALT | CP_CE_HALT));
		WREG32(SCRATCH_UMSK, 0);
		rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = false;
		rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX].ready = false;
		rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX].ready = false;
	}
	udelay(50);
}

static int si_cp_load_microcode(struct radeon_device *rdev)
{
	const __be32 *fw_data;
	int i;

	if (!rdev->me_fw || !rdev->pfp_fw)
		return -EINVAL;

	si_cp_enable(rdev, false);

	/* PFP */
	fw_data = (const __be32 *)rdev->pfp_fw->data;
	WREG32(CP_PFP_UCODE_ADDR, 0);
	for (i = 0; i < SI_PFP_UCODE_SIZE; i++)
		WREG32(CP_PFP_UCODE_DATA, be32_to_cpup(fw_data++));
	WREG32(CP_PFP_UCODE_ADDR, 0);

	/* CE */
	fw_data = (const __be32 *)rdev->ce_fw->data;
	WREG32(CP_CE_UCODE_ADDR, 0);
	for (i = 0; i < SI_CE_UCODE_SIZE; i++)
		WREG32(CP_CE_UCODE_DATA, be32_to_cpup(fw_data++));
	WREG32(CP_CE_UCODE_ADDR, 0);

	/* ME */
	fw_data = (const __be32 *)rdev->me_fw->data;
	WREG32(CP_ME_RAM_WADDR, 0);
	for (i = 0; i < SI_PM4_UCODE_SIZE; i++)
		WREG32(CP_ME_RAM_DATA, be32_to_cpup(fw_data++));
	WREG32(CP_ME_RAM_WADDR, 0);

	WREG32(CP_PFP_UCODE_ADDR, 0);
	WREG32(CP_CE_UCODE_ADDR, 0);
	WREG32(CP_ME_RAM_WADDR, 0);
	WREG32(CP_ME_RAM_RADDR, 0);
	return 0;
}

static int si_cp_start(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	int r, i;

	r = radeon_ring_lock(rdev, ring, 7 + 4);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring (%d).\n", r);
		return r;
	}
	/* init the CP */
	radeon_ring_write(ring, PACKET3(PACKET3_ME_INITIALIZE, 5));
	radeon_ring_write(ring, 0x1);
	radeon_ring_write(ring, 0x0);
	radeon_ring_write(ring, rdev->config.si.max_hw_contexts - 1);
	radeon_ring_write(ring, PACKET3_ME_INITIALIZE_DEVICE_ID(1));
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 0);

	/* init the CE partitions */
	radeon_ring_write(ring, PACKET3(PACKET3_SET_BASE, 2));
	radeon_ring_write(ring, PACKET3_BASE_INDEX(CE_PARTITION_BASE));
	radeon_ring_write(ring, 0xc000);
	radeon_ring_write(ring, 0xe000);
	radeon_ring_unlock_commit(rdev, ring);

	si_cp_enable(rdev, true);

	r = radeon_ring_lock(rdev, ring, si_default_size + 10);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring (%d).\n", r);
		return r;
	}

	/* setup clear context state */
	radeon_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	radeon_ring_write(ring, PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	for (i = 0; i < si_default_size; i++)
		radeon_ring_write(ring, si_default_state[i]);

	radeon_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	radeon_ring_write(ring, PACKET3_PREAMBLE_END_CLEAR_STATE);

	/* set clear context state */
	radeon_ring_write(ring, PACKET3(PACKET3_CLEAR_STATE, 0));
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	radeon_ring_write(ring, 0x00000316);
	radeon_ring_write(ring, 0x0000000e); /* VGT_VERTEX_REUSE_BLOCK_CNTL */
	radeon_ring_write(ring, 0x00000010); /* VGT_OUT_DEALLOC_CNTL */

	radeon_ring_unlock_commit(rdev, ring);

	for (i = RADEON_RING_TYPE_GFX_INDEX; i <= CAYMAN_RING_TYPE_CP2_INDEX; ++i) {
		ring = &rdev->ring[i];
		r = radeon_ring_lock(rdev, ring, 2);

		/* clear the compute context state */
		radeon_ring_write(ring, PACKET3_COMPUTE(PACKET3_CLEAR_STATE, 0));
		radeon_ring_write(ring, 0);

		radeon_ring_unlock_commit(rdev, ring);
	}

	return 0;
}

static void si_cp_fini(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	si_cp_enable(rdev, false);

	ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	radeon_ring_fini(rdev, ring);
	radeon_scratch_free(rdev, ring->rptr_save_reg);

	ring = &rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX];
	radeon_ring_fini(rdev, ring);
	radeon_scratch_free(rdev, ring->rptr_save_reg);

	ring = &rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX];
	radeon_ring_fini(rdev, ring);
	radeon_scratch_free(rdev, ring->rptr_save_reg);
}

static int si_cp_resume(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	u32 tmp;
	u32 rb_bufsz;
	int r;

	/* Reset cp; if cp is reset, then PA, SH, VGT also need to be reset */
	WREG32(GRBM_SOFT_RESET, (SOFT_RESET_CP |
				 SOFT_RESET_PA |
				 SOFT_RESET_VGT |
				 SOFT_RESET_SPI |
				 SOFT_RESET_SX));
	RREG32(GRBM_SOFT_RESET);
	mdelay(15);
	WREG32(GRBM_SOFT_RESET, 0);
	RREG32(GRBM_SOFT_RESET);

	WREG32(CP_SEM_WAIT_TIMER, 0x0);
	WREG32(CP_SEM_INCOMPLETE_TIMER_CNTL, 0x0);

	/* Set the write pointer delay */
	WREG32(CP_RB_WPTR_DELAY, 0);

	WREG32(CP_DEBUG, 0);
	WREG32(SCRATCH_ADDR, ((rdev->wb.gpu_addr + RADEON_WB_SCRATCH_OFFSET) >> 8) & 0xFFFFFFFF);

	/* ring 0 - compute and gfx */
	/* Set ring buffer size */
	ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	rb_bufsz = drm_order(ring->ring_size / 8);
	tmp = (drm_order(RADEON_GPU_PAGE_SIZE/8) << 8) | rb_bufsz;
#ifdef __BIG_ENDIAN
	tmp |= BUF_SWAP_32BIT;
#endif
	WREG32(CP_RB0_CNTL, tmp);

	/* Initialize the ring buffer's read and write pointers */
	WREG32(CP_RB0_CNTL, tmp | RB_RPTR_WR_ENA);
	ring->wptr = 0;
	WREG32(CP_RB0_WPTR, ring->wptr);

	/* set the wb address whether it's enabled or not */
	WREG32(CP_RB0_RPTR_ADDR, (rdev->wb.gpu_addr + RADEON_WB_CP_RPTR_OFFSET) & 0xFFFFFFFC);
	WREG32(CP_RB0_RPTR_ADDR_HI, upper_32_bits(rdev->wb.gpu_addr + RADEON_WB_CP_RPTR_OFFSET) & 0xFF);

	if (rdev->wb.enabled)
		WREG32(SCRATCH_UMSK, 0xff);
	else {
		tmp |= RB_NO_UPDATE;
		WREG32(SCRATCH_UMSK, 0);
	}

	mdelay(1);
	WREG32(CP_RB0_CNTL, tmp);

	WREG32(CP_RB0_BASE, ring->gpu_addr >> 8);

	ring->rptr = RREG32(CP_RB0_RPTR);

	/* ring1  - compute only */
	/* Set ring buffer size */
	ring = &rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX];
	rb_bufsz = drm_order(ring->ring_size / 8);
	tmp = (drm_order(RADEON_GPU_PAGE_SIZE/8) << 8) | rb_bufsz;
#ifdef __BIG_ENDIAN
	tmp |= BUF_SWAP_32BIT;
#endif
	WREG32(CP_RB1_CNTL, tmp);

	/* Initialize the ring buffer's read and write pointers */
	WREG32(CP_RB1_CNTL, tmp | RB_RPTR_WR_ENA);
	ring->wptr = 0;
	WREG32(CP_RB1_WPTR, ring->wptr);

	/* set the wb address whether it's enabled or not */
	WREG32(CP_RB1_RPTR_ADDR, (rdev->wb.gpu_addr + RADEON_WB_CP1_RPTR_OFFSET) & 0xFFFFFFFC);
	WREG32(CP_RB1_RPTR_ADDR_HI, upper_32_bits(rdev->wb.gpu_addr + RADEON_WB_CP1_RPTR_OFFSET) & 0xFF);

	mdelay(1);
	WREG32(CP_RB1_CNTL, tmp);

	WREG32(CP_RB1_BASE, ring->gpu_addr >> 8);

	ring->rptr = RREG32(CP_RB1_RPTR);

	/* ring2 - compute only */
	/* Set ring buffer size */
	ring = &rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX];
	rb_bufsz = drm_order(ring->ring_size / 8);
	tmp = (drm_order(RADEON_GPU_PAGE_SIZE/8) << 8) | rb_bufsz;
#ifdef __BIG_ENDIAN
	tmp |= BUF_SWAP_32BIT;
#endif
	WREG32(CP_RB2_CNTL, tmp);

	/* Initialize the ring buffer's read and write pointers */
	WREG32(CP_RB2_CNTL, tmp | RB_RPTR_WR_ENA);
	ring->wptr = 0;
	WREG32(CP_RB2_WPTR, ring->wptr);

	/* set the wb address whether it's enabled or not */
	WREG32(CP_RB2_RPTR_ADDR, (rdev->wb.gpu_addr + RADEON_WB_CP2_RPTR_OFFSET) & 0xFFFFFFFC);
	WREG32(CP_RB2_RPTR_ADDR_HI, upper_32_bits(rdev->wb.gpu_addr + RADEON_WB_CP2_RPTR_OFFSET) & 0xFF);

	mdelay(1);
	WREG32(CP_RB2_CNTL, tmp);

	WREG32(CP_RB2_BASE, ring->gpu_addr >> 8);

	ring->rptr = RREG32(CP_RB2_RPTR);

	/* start the rings */
	si_cp_start(rdev);
	rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = true;
	rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX].ready = true;
	rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX].ready = true;
	r = radeon_ring_test(rdev, RADEON_RING_TYPE_GFX_INDEX, &rdev->ring[RADEON_RING_TYPE_GFX_INDEX]);
	if (r) {
		rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = false;
		rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX].ready = false;
		rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX].ready = false;
		return r;
	}
	r = radeon_ring_test(rdev, CAYMAN_RING_TYPE_CP1_INDEX, &rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX]);
	if (r) {
		rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX].ready = false;
	}
	r = radeon_ring_test(rdev, CAYMAN_RING_TYPE_CP2_INDEX, &rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX]);
	if (r) {
		rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX].ready = false;
	}

	return 0;
}

static u32 si_gpu_check_soft_reset(struct radeon_device *rdev)
{
	u32 reset_mask = 0;
	u32 tmp;

	/* GRBM_STATUS */
	tmp = RREG32(GRBM_STATUS);
	if (tmp & (PA_BUSY | SC_BUSY |
		   BCI_BUSY | SX_BUSY |
		   TA_BUSY | VGT_BUSY |
		   DB_BUSY | CB_BUSY |
		   GDS_BUSY | SPI_BUSY |
		   IA_BUSY | IA_BUSY_NO_DMA))
		reset_mask |= RADEON_RESET_GFX;

	if (tmp & (CF_RQ_PENDING | PF_RQ_PENDING |
		   CP_BUSY | CP_COHERENCY_BUSY))
		reset_mask |= RADEON_RESET_CP;

	if (tmp & GRBM_EE_BUSY)
		reset_mask |= RADEON_RESET_GRBM | RADEON_RESET_GFX | RADEON_RESET_CP;

	/* GRBM_STATUS2 */
	tmp = RREG32(GRBM_STATUS2);
	if (tmp & (RLC_RQ_PENDING | RLC_BUSY))
		reset_mask |= RADEON_RESET_RLC;

	/* DMA_STATUS_REG 0 */
	tmp = RREG32(DMA_STATUS_REG + DMA0_REGISTER_OFFSET);
	if (!(tmp & DMA_IDLE))
		reset_mask |= RADEON_RESET_DMA;

	/* DMA_STATUS_REG 1 */
	tmp = RREG32(DMA_STATUS_REG + DMA1_REGISTER_OFFSET);
	if (!(tmp & DMA_IDLE))
		reset_mask |= RADEON_RESET_DMA1;

	/* SRBM_STATUS2 */
	tmp = RREG32(SRBM_STATUS2);
	if (tmp & DMA_BUSY)
		reset_mask |= RADEON_RESET_DMA;

	if (tmp & DMA1_BUSY)
		reset_mask |= RADEON_RESET_DMA1;

	/* SRBM_STATUS */
	tmp = RREG32(SRBM_STATUS);

	if (tmp & IH_BUSY)
		reset_mask |= RADEON_RESET_IH;

	if (tmp & SEM_BUSY)
		reset_mask |= RADEON_RESET_SEM;

	if (tmp & GRBM_RQ_PENDING)
		reset_mask |= RADEON_RESET_GRBM;

	if (tmp & VMC_BUSY)
		reset_mask |= RADEON_RESET_VMC;

	if (tmp & (MCB_BUSY | MCB_NON_DISPLAY_BUSY |
		   MCC_BUSY | MCD_BUSY))
		reset_mask |= RADEON_RESET_MC;

	if (evergreen_is_display_hung(rdev))
		reset_mask |= RADEON_RESET_DISPLAY;

	/* VM_L2_STATUS */
	tmp = RREG32(VM_L2_STATUS);
	if (tmp & L2_BUSY)
		reset_mask |= RADEON_RESET_VMC;

	/* Skip MC reset as it's mostly likely not hung, just busy */
	if (reset_mask & RADEON_RESET_MC) {
		DRM_DEBUG("MC busy: 0x%08X, clearing.\n", reset_mask);
		reset_mask &= ~RADEON_RESET_MC;
	}

	return reset_mask;
}

static void si_gpu_soft_reset(struct radeon_device *rdev, u32 reset_mask)
{
	struct evergreen_mc_save save;
	u32 grbm_soft_reset = 0, srbm_soft_reset = 0;
	u32 tmp;

	if (reset_mask == 0)
		return;

	dev_info(rdev->dev, "GPU softreset: 0x%08X\n", reset_mask);

	evergreen_print_gpu_status_regs(rdev);
	dev_info(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_ADDR   0x%08X\n",
		 RREG32(VM_CONTEXT1_PROTECTION_FAULT_ADDR));
	dev_info(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_STATUS 0x%08X\n",
		 RREG32(VM_CONTEXT1_PROTECTION_FAULT_STATUS));

	/* Disable CP parsing/prefetching */
	WREG32(CP_ME_CNTL, CP_ME_HALT | CP_PFP_HALT | CP_CE_HALT);

	if (reset_mask & RADEON_RESET_DMA) {
		/* dma0 */
		tmp = RREG32(DMA_RB_CNTL + DMA0_REGISTER_OFFSET);
		tmp &= ~DMA_RB_ENABLE;
		WREG32(DMA_RB_CNTL + DMA0_REGISTER_OFFSET, tmp);
	}
	if (reset_mask & RADEON_RESET_DMA1) {
		/* dma1 */
		tmp = RREG32(DMA_RB_CNTL + DMA1_REGISTER_OFFSET);
		tmp &= ~DMA_RB_ENABLE;
		WREG32(DMA_RB_CNTL + DMA1_REGISTER_OFFSET, tmp);
	}

	udelay(50);

	evergreen_mc_stop(rdev, &save);
	if (evergreen_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}

	if (reset_mask & (RADEON_RESET_GFX | RADEON_RESET_COMPUTE | RADEON_RESET_CP)) {
		grbm_soft_reset = SOFT_RESET_CB |
			SOFT_RESET_DB |
			SOFT_RESET_GDS |
			SOFT_RESET_PA |
			SOFT_RESET_SC |
			SOFT_RESET_BCI |
			SOFT_RESET_SPI |
			SOFT_RESET_SX |
			SOFT_RESET_TC |
			SOFT_RESET_TA |
			SOFT_RESET_VGT |
			SOFT_RESET_IA;
	}

	if (reset_mask & RADEON_RESET_CP) {
		grbm_soft_reset |= SOFT_RESET_CP | SOFT_RESET_VGT;

		srbm_soft_reset |= SOFT_RESET_GRBM;
	}

	if (reset_mask & RADEON_RESET_DMA)
		srbm_soft_reset |= SOFT_RESET_DMA;

	if (reset_mask & RADEON_RESET_DMA1)
		srbm_soft_reset |= SOFT_RESET_DMA1;

	if (reset_mask & RADEON_RESET_DISPLAY)
		srbm_soft_reset |= SOFT_RESET_DC;

	if (reset_mask & RADEON_RESET_RLC)
		grbm_soft_reset |= SOFT_RESET_RLC;

	if (reset_mask & RADEON_RESET_SEM)
		srbm_soft_reset |= SOFT_RESET_SEM;

	if (reset_mask & RADEON_RESET_IH)
		srbm_soft_reset |= SOFT_RESET_IH;

	if (reset_mask & RADEON_RESET_GRBM)
		srbm_soft_reset |= SOFT_RESET_GRBM;

	if (reset_mask & RADEON_RESET_VMC)
		srbm_soft_reset |= SOFT_RESET_VMC;

	if (reset_mask & RADEON_RESET_MC)
		srbm_soft_reset |= SOFT_RESET_MC;

	if (grbm_soft_reset) {
		tmp = RREG32(GRBM_SOFT_RESET);
		tmp |= grbm_soft_reset;
		dev_info(rdev->dev, "GRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(GRBM_SOFT_RESET, tmp);
		tmp = RREG32(GRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~grbm_soft_reset;
		WREG32(GRBM_SOFT_RESET, tmp);
		tmp = RREG32(GRBM_SOFT_RESET);
	}

	if (srbm_soft_reset) {
		tmp = RREG32(SRBM_SOFT_RESET);
		tmp |= srbm_soft_reset;
		dev_info(rdev->dev, "SRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(SRBM_SOFT_RESET, tmp);
		tmp = RREG32(SRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~srbm_soft_reset;
		WREG32(SRBM_SOFT_RESET, tmp);
		tmp = RREG32(SRBM_SOFT_RESET);
	}

	/* Wait a little for things to settle down */
	udelay(50);

	evergreen_mc_resume(rdev, &save);
	udelay(50);

	evergreen_print_gpu_status_regs(rdev);
}

int si_asic_reset(struct radeon_device *rdev)
{
	u32 reset_mask;

	reset_mask = si_gpu_check_soft_reset(rdev);

	if (reset_mask)
		r600_set_bios_scratch_engine_hung(rdev, true);

	si_gpu_soft_reset(rdev, reset_mask);

	reset_mask = si_gpu_check_soft_reset(rdev);

	if (!reset_mask)
		r600_set_bios_scratch_engine_hung(rdev, false);

	return 0;
}

/**
 * si_gfx_is_lockup - Check if the GFX engine is locked up
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Check if the GFX engine is locked up.
 * Returns true if the engine appears to be locked up, false if not.
 */
bool si_gfx_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring)
{
	u32 reset_mask = si_gpu_check_soft_reset(rdev);

	if (!(reset_mask & (RADEON_RESET_GFX |
			    RADEON_RESET_COMPUTE |
			    RADEON_RESET_CP))) {
		radeon_ring_lockup_update(ring);
		return false;
	}
	/* force CP activities */
	radeon_ring_force_activity(rdev, ring);
	return radeon_ring_test_lockup(rdev, ring);
}

/**
 * si_dma_is_lockup - Check if the DMA engine is locked up
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Check if the async DMA engine is locked up.
 * Returns true if the engine appears to be locked up, false if not.
 */
bool si_dma_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring)
{
	u32 reset_mask = si_gpu_check_soft_reset(rdev);
	u32 mask;

	if (ring->idx == R600_RING_TYPE_DMA_INDEX)
		mask = RADEON_RESET_DMA;
	else
		mask = RADEON_RESET_DMA1;

	if (!(reset_mask & mask)) {
		radeon_ring_lockup_update(ring);
		return false;
	}
	/* force ring activities */
	radeon_ring_force_activity(rdev, ring);
	return radeon_ring_test_lockup(rdev, ring);
}

/* MC */
static void si_mc_program(struct radeon_device *rdev)
{
	struct evergreen_mc_save save;
	u32 tmp;
	int i, j;

	/* Initialize HDP */
	for (i = 0, j = 0; i < 32; i++, j += 0x18) {
		WREG32((0x2c14 + j), 0x00000000);
		WREG32((0x2c18 + j), 0x00000000);
		WREG32((0x2c1c + j), 0x00000000);
		WREG32((0x2c20 + j), 0x00000000);
		WREG32((0x2c24 + j), 0x00000000);
	}
	WREG32(HDP_REG_COHERENCY_FLUSH_CNTL, 0);

	evergreen_mc_stop(rdev, &save);
	if (radeon_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}
	if (!ASIC_IS_NODCE(rdev))
		/* Lockout access through VGA aperture*/
		WREG32(VGA_HDP_CONTROL, VGA_MEMORY_DISABLE);
	/* Update configuration */
	WREG32(MC_VM_SYSTEM_APERTURE_LOW_ADDR,
	       rdev->mc.vram_start >> 12);
	WREG32(MC_VM_SYSTEM_APERTURE_HIGH_ADDR,
	       rdev->mc.vram_end >> 12);
	WREG32(MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR,
	       rdev->vram_scratch.gpu_addr >> 12);
	tmp = ((rdev->mc.vram_end >> 24) & 0xFFFF) << 16;
	tmp |= ((rdev->mc.vram_start >> 24) & 0xFFFF);
	WREG32(MC_VM_FB_LOCATION, tmp);
	/* XXX double check these! */
	WREG32(HDP_NONSURFACE_BASE, (rdev->mc.vram_start >> 8));
	WREG32(HDP_NONSURFACE_INFO, (2 << 7) | (1 << 30));
	WREG32(HDP_NONSURFACE_SIZE, 0x3FFFFFFF);
	WREG32(MC_VM_AGP_BASE, 0);
	WREG32(MC_VM_AGP_TOP, 0x0FFFFFFF);
	WREG32(MC_VM_AGP_BOT, 0x0FFFFFFF);
	if (radeon_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}
	evergreen_mc_resume(rdev, &save);
	if (!ASIC_IS_NODCE(rdev)) {
		/* we need to own VRAM, so turn off the VGA renderer here
		 * to stop it overwriting our objects */
		rv515_vga_render_disable(rdev);
	}
}

static void si_vram_gtt_location(struct radeon_device *rdev,
				 struct radeon_mc *mc)
{
	if (mc->mc_vram_size > 0xFFC0000000ULL) {
		/* leave room for at least 1024M GTT */
		dev_warn(rdev->dev, "limiting VRAM\n");
		mc->real_vram_size = 0xFFC0000000ULL;
		mc->mc_vram_size = 0xFFC0000000ULL;
	}
	radeon_vram_location(rdev, &rdev->mc, 0);
	rdev->mc.gtt_base_align = 0;
	radeon_gtt_location(rdev, mc);
}

static int si_mc_init(struct radeon_device *rdev)
{
	u32 tmp;
	int chansize, numchan;

	/* Get VRAM informations */
	rdev->mc.vram_is_ddr = true;
	tmp = RREG32(MC_ARB_RAMCFG);
	if (tmp & CHANSIZE_OVERRIDE) {
		chansize = 16;
	} else if (tmp & CHANSIZE_MASK) {
		chansize = 64;
	} else {
		chansize = 32;
	}
	tmp = RREG32(MC_SHARED_CHMAP);
	switch ((tmp & NOOFCHAN_MASK) >> NOOFCHAN_SHIFT) {
	case 0:
	default:
		numchan = 1;
		break;
	case 1:
		numchan = 2;
		break;
	case 2:
		numchan = 4;
		break;
	case 3:
		numchan = 8;
		break;
	case 4:
		numchan = 3;
		break;
	case 5:
		numchan = 6;
		break;
	case 6:
		numchan = 10;
		break;
	case 7:
		numchan = 12;
		break;
	case 8:
		numchan = 16;
		break;
	}
	rdev->mc.vram_width = numchan * chansize;
	/* Could aper size report 0 ? */
	rdev->mc.aper_base = pci_resource_start(rdev->pdev, 0);
	rdev->mc.aper_size = pci_resource_len(rdev->pdev, 0);
	/* size in MB on si */
	rdev->mc.mc_vram_size = RREG32(CONFIG_MEMSIZE) * 1024ULL * 1024ULL;
	rdev->mc.real_vram_size = RREG32(CONFIG_MEMSIZE) * 1024ULL * 1024ULL;
	rdev->mc.visible_vram_size = rdev->mc.aper_size;
	si_vram_gtt_location(rdev, &rdev->mc);
	radeon_update_bandwidth_info(rdev);

	return 0;
}

/*
 * GART
 */
void si_pcie_gart_tlb_flush(struct radeon_device *rdev)
{
	/* flush hdp cache */
	WREG32(HDP_MEM_COHERENCY_FLUSH_CNTL, 0x1);

	/* bits 0-15 are the VM contexts0-15 */
	WREG32(VM_INVALIDATE_REQUEST, 1);
}

static int si_pcie_gart_enable(struct radeon_device *rdev)
{
	int r, i;

	if (rdev->gart.robj == NULL) {
		dev_err(rdev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}
	r = radeon_gart_table_vram_pin(rdev);
	if (r)
		return r;
	radeon_gart_restore(rdev);
	/* Setup TLB control */
	WREG32(MC_VM_MX_L1_TLB_CNTL,
	       (0xA << 7) |
	       ENABLE_L1_TLB |
	       SYSTEM_ACCESS_MODE_NOT_IN_SYS |
	       ENABLE_ADVANCED_DRIVER_MODEL |
	       SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU);
	/* Setup L2 cache */
	WREG32(VM_L2_CNTL, ENABLE_L2_CACHE |
	       ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE |
	       ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE |
	       EFFECTIVE_L2_QUEUE_SIZE(7) |
	       CONTEXT1_IDENTITY_ACCESS_MODE(1));
	WREG32(VM_L2_CNTL2, INVALIDATE_ALL_L1_TLBS | INVALIDATE_L2_CACHE);
	WREG32(VM_L2_CNTL3, L2_CACHE_BIGK_ASSOCIATIVITY |
	       L2_CACHE_BIGK_FRAGMENT_SIZE(0));
	/* setup context0 */
	WREG32(VM_CONTEXT0_PAGE_TABLE_START_ADDR, rdev->mc.gtt_start >> 12);
	WREG32(VM_CONTEXT0_PAGE_TABLE_END_ADDR, rdev->mc.gtt_end >> 12);
	WREG32(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR, rdev->gart.table_addr >> 12);
	WREG32(VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR,
			(u32)(rdev->dummy_page.addr >> 12));
	WREG32(VM_CONTEXT0_CNTL2, 0);
	WREG32(VM_CONTEXT0_CNTL, (ENABLE_CONTEXT | PAGE_TABLE_DEPTH(0) |
				  RANGE_PROTECTION_FAULT_ENABLE_DEFAULT));

	WREG32(0x15D4, 0);
	WREG32(0x15D8, 0);
	WREG32(0x15DC, 0);

	/* empty context1-15 */
	/* set vm size, must be a multiple of 4 */
	WREG32(VM_CONTEXT1_PAGE_TABLE_START_ADDR, 0);
	WREG32(VM_CONTEXT1_PAGE_TABLE_END_ADDR, rdev->vm_manager.max_pfn);
	/* Assign the pt base to something valid for now; the pts used for
	 * the VMs are determined by the application and setup and assigned
	 * on the fly in the vm part of radeon_gart.c
	 */
	for (i = 1; i < 16; i++) {
		if (i < 8)
			WREG32(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (i << 2),
			       rdev->gart.table_addr >> 12);
		else
			WREG32(VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + ((i - 8) << 2),
			       rdev->gart.table_addr >> 12);
	}

	/* enable context1-15 */
	WREG32(VM_CONTEXT1_PROTECTION_FAULT_DEFAULT_ADDR,
	       (u32)(rdev->dummy_page.addr >> 12));
	WREG32(VM_CONTEXT1_CNTL2, 4);
	WREG32(VM_CONTEXT1_CNTL, ENABLE_CONTEXT | PAGE_TABLE_DEPTH(1) |
				RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT |
				RANGE_PROTECTION_FAULT_ENABLE_DEFAULT |
				DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT |
				DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT |
				PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT |
				PDE0_PROTECTION_FAULT_ENABLE_DEFAULT |
				VALID_PROTECTION_FAULT_ENABLE_INTERRUPT |
				VALID_PROTECTION_FAULT_ENABLE_DEFAULT |
				READ_PROTECTION_FAULT_ENABLE_INTERRUPT |
				READ_PROTECTION_FAULT_ENABLE_DEFAULT |
				WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT |
				WRITE_PROTECTION_FAULT_ENABLE_DEFAULT);

	si_pcie_gart_tlb_flush(rdev);
	DRM_INFO("PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(rdev->mc.gtt_size >> 20),
		 (unsigned long long)rdev->gart.table_addr);
	rdev->gart.ready = true;
	return 0;
}

static void si_pcie_gart_disable(struct radeon_device *rdev)
{
	/* Disable all tables */
	WREG32(VM_CONTEXT0_CNTL, 0);
	WREG32(VM_CONTEXT1_CNTL, 0);
	/* Setup TLB control */
	WREG32(MC_VM_MX_L1_TLB_CNTL, SYSTEM_ACCESS_MODE_NOT_IN_SYS |
	       SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU);
	/* Setup L2 cache */
	WREG32(VM_L2_CNTL, ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE |
	       ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE |
	       EFFECTIVE_L2_QUEUE_SIZE(7) |
	       CONTEXT1_IDENTITY_ACCESS_MODE(1));
	WREG32(VM_L2_CNTL2, 0);
	WREG32(VM_L2_CNTL3, L2_CACHE_BIGK_ASSOCIATIVITY |
	       L2_CACHE_BIGK_FRAGMENT_SIZE(0));
	radeon_gart_table_vram_unpin(rdev);
}

static void si_pcie_gart_fini(struct radeon_device *rdev)
{
	si_pcie_gart_disable(rdev);
	radeon_gart_table_vram_free(rdev);
	radeon_gart_fini(rdev);
}

/* vm parser */
static bool si_vm_reg_valid(u32 reg)
{
	/* context regs are fine */
	if (reg >= 0x28000)
		return true;

	/* check config regs */
	switch (reg) {
	case GRBM_GFX_INDEX:
	case CP_STRMOUT_CNTL:
	case VGT_VTX_VECT_EJECT_REG:
	case VGT_CACHE_INVALIDATION:
	case VGT_ESGS_RING_SIZE:
	case VGT_GSVS_RING_SIZE:
	case VGT_GS_VERTEX_REUSE:
	case VGT_PRIMITIVE_TYPE:
	case VGT_INDEX_TYPE:
	case VGT_NUM_INDICES:
	case VGT_NUM_INSTANCES:
	case VGT_TF_RING_SIZE:
	case VGT_HS_OFFCHIP_PARAM:
	case VGT_TF_MEMORY_BASE:
	case PA_CL_ENHANCE:
	case PA_SU_LINE_STIPPLE_VALUE:
	case PA_SC_LINE_STIPPLE_STATE:
	case PA_SC_ENHANCE:
	case SQC_CACHES:
	case SPI_STATIC_THREAD_MGMT_1:
	case SPI_STATIC_THREAD_MGMT_2:
	case SPI_STATIC_THREAD_MGMT_3:
	case SPI_PS_MAX_WAVE_ID:
	case SPI_CONFIG_CNTL:
	case SPI_CONFIG_CNTL_1:
	case TA_CNTL_AUX:
		return true;
	default:
		DRM_ERROR("Invalid register 0x%x in CS\n", reg);
		return false;
	}
}

static int si_vm_packet3_ce_check(struct radeon_device *rdev,
				  u32 *ib, struct radeon_cs_packet *pkt)
{
	switch (pkt->opcode) {
	case PACKET3_NOP:
	case PACKET3_SET_BASE:
	case PACKET3_SET_CE_DE_COUNTERS:
	case PACKET3_LOAD_CONST_RAM:
	case PACKET3_WRITE_CONST_RAM:
	case PACKET3_WRITE_CONST_RAM_OFFSET:
	case PACKET3_DUMP_CONST_RAM:
	case PACKET3_INCREMENT_CE_COUNTER:
	case PACKET3_WAIT_ON_DE_COUNTER:
	case PACKET3_CE_WRITE:
		break;
	default:
		DRM_ERROR("Invalid CE packet3: 0x%x\n", pkt->opcode);
		return -EINVAL;
	}
	return 0;
}

static int si_vm_packet3_gfx_check(struct radeon_device *rdev,
				   u32 *ib, struct radeon_cs_packet *pkt)
{
	u32 idx = pkt->idx + 1;
	u32 idx_value = ib[idx];
	u32 start_reg, end_reg, reg, i;
	u32 command, info;

	switch (pkt->opcode) {
	case PACKET3_NOP:
	case PACKET3_SET_BASE:
	case PACKET3_CLEAR_STATE:
	case PACKET3_INDEX_BUFFER_SIZE:
	case PACKET3_DISPATCH_DIRECT:
	case PACKET3_DISPATCH_INDIRECT:
	case PACKET3_ALLOC_GDS:
	case PACKET3_WRITE_GDS_RAM:
	case PACKET3_ATOMIC_GDS:
	case PACKET3_ATOMIC:
	case PACKET3_OCCLUSION_QUERY:
	case PACKET3_SET_PREDICATION:
	case PACKET3_COND_EXEC:
	case PACKET3_PRED_EXEC:
	case PACKET3_DRAW_INDIRECT:
	case PACKET3_DRAW_INDEX_INDIRECT:
	case PACKET3_INDEX_BASE:
	case PACKET3_DRAW_INDEX_2:
	case PACKET3_CONTEXT_CONTROL:
	case PACKET3_INDEX_TYPE:
	case PACKET3_DRAW_INDIRECT_MULTI:
	case PACKET3_DRAW_INDEX_AUTO:
	case PACKET3_DRAW_INDEX_IMMD:
	case PACKET3_NUM_INSTANCES:
	case PACKET3_DRAW_INDEX_MULTI_AUTO:
	case PACKET3_STRMOUT_BUFFER_UPDATE:
	case PACKET3_DRAW_INDEX_OFFSET_2:
	case PACKET3_DRAW_INDEX_MULTI_ELEMENT:
	case PACKET3_DRAW_INDEX_INDIRECT_MULTI:
	case PACKET3_MPEG_INDEX:
	case PACKET3_WAIT_REG_MEM:
	case PACKET3_MEM_WRITE:
	case PACKET3_PFP_SYNC_ME:
	case PACKET3_SURFACE_SYNC:
	case PACKET3_EVENT_WRITE:
	case PACKET3_EVENT_WRITE_EOP:
	case PACKET3_EVENT_WRITE_EOS:
	case PACKET3_SET_CONTEXT_REG:
	case PACKET3_SET_CONTEXT_REG_INDIRECT:
	case PACKET3_SET_SH_REG:
	case PACKET3_SET_SH_REG_OFFSET:
	case PACKET3_INCREMENT_DE_COUNTER:
	case PACKET3_WAIT_ON_CE_COUNTER:
	case PACKET3_WAIT_ON_AVAIL_BUFFER:
	case PACKET3_ME_WRITE:
		break;
	case PACKET3_COPY_DATA:
		if ((idx_value & 0xf00) == 0) {
			reg = ib[idx + 3] * 4;
			if (!si_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_WRITE_DATA:
		if ((idx_value & 0xf00) == 0) {
			start_reg = ib[idx + 1] * 4;
			if (idx_value & 0x10000) {
				if (!si_vm_reg_valid(start_reg))
					return -EINVAL;
			} else {
				for (i = 0; i < (pkt->count - 2); i++) {
					reg = start_reg + (4 * i);
					if (!si_vm_reg_valid(reg))
						return -EINVAL;
				}
			}
		}
		break;
	case PACKET3_COND_WRITE:
		if (idx_value & 0x100) {
			reg = ib[idx + 5] * 4;
			if (!si_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_COPY_DW:
		if (idx_value & 0x2) {
			reg = ib[idx + 3] * 4;
			if (!si_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_SET_CONFIG_REG:
		start_reg = (idx_value << 2) + PACKET3_SET_CONFIG_REG_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_CONFIG_REG_START) ||
		    (start_reg >= PACKET3_SET_CONFIG_REG_END) ||
		    (end_reg >= PACKET3_SET_CONFIG_REG_END)) {
			DRM_ERROR("bad PACKET3_SET_CONFIG_REG\n");
			return -EINVAL;
		}
		for (i = 0; i < pkt->count; i++) {
			reg = start_reg + (4 * i);
			if (!si_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_CP_DMA:
		command = ib[idx + 4];
		info = ib[idx + 1];
		if (command & PACKET3_CP_DMA_CMD_SAS) {
			/* src address space is register */
			if (((info & 0x60000000) >> 29) == 0) {
				start_reg = idx_value << 2;
				if (command & PACKET3_CP_DMA_CMD_SAIC) {
					reg = start_reg;
					if (!si_vm_reg_valid(reg)) {
						DRM_ERROR("CP DMA Bad SRC register\n");
						return -EINVAL;
					}
				} else {
					for (i = 0; i < (command & 0x1fffff); i++) {
						reg = start_reg + (4 * i);
						if (!si_vm_reg_valid(reg)) {
							DRM_ERROR("CP DMA Bad SRC register\n");
							return -EINVAL;
						}
					}
				}
			}
		}
		if (command & PACKET3_CP_DMA_CMD_DAS) {
			/* dst address space is register */
			if (((info & 0x00300000) >> 20) == 0) {
				start_reg = ib[idx + 2];
				if (command & PACKET3_CP_DMA_CMD_DAIC) {
					reg = start_reg;
					if (!si_vm_reg_valid(reg)) {
						DRM_ERROR("CP DMA Bad DST register\n");
						return -EINVAL;
					}
				} else {
					for (i = 0; i < (command & 0x1fffff); i++) {
						reg = start_reg + (4 * i);
						if (!si_vm_reg_valid(reg)) {
							DRM_ERROR("CP DMA Bad DST register\n");
							return -EINVAL;
						}
					}
				}
			}
		}
		break;
	default:
		DRM_ERROR("Invalid GFX packet3: 0x%x\n", pkt->opcode);
		return -EINVAL;
	}
	return 0;
}

static int si_vm_packet3_compute_check(struct radeon_device *rdev,
				       u32 *ib, struct radeon_cs_packet *pkt)
{
	u32 idx = pkt->idx + 1;
	u32 idx_value = ib[idx];
	u32 start_reg, reg, i;

	switch (pkt->opcode) {
	case PACKET3_NOP:
	case PACKET3_SET_BASE:
	case PACKET3_CLEAR_STATE:
	case PACKET3_DISPATCH_DIRECT:
	case PACKET3_DISPATCH_INDIRECT:
	case PACKET3_ALLOC_GDS:
	case PACKET3_WRITE_GDS_RAM:
	case PACKET3_ATOMIC_GDS:
	case PACKET3_ATOMIC:
	case PACKET3_OCCLUSION_QUERY:
	case PACKET3_SET_PREDICATION:
	case PACKET3_COND_EXEC:
	case PACKET3_PRED_EXEC:
	case PACKET3_CONTEXT_CONTROL:
	case PACKET3_STRMOUT_BUFFER_UPDATE:
	case PACKET3_WAIT_REG_MEM:
	case PACKET3_MEM_WRITE:
	case PACKET3_PFP_SYNC_ME:
	case PACKET3_SURFACE_SYNC:
	case PACKET3_EVENT_WRITE:
	case PACKET3_EVENT_WRITE_EOP:
	case PACKET3_EVENT_WRITE_EOS:
	case PACKET3_SET_CONTEXT_REG:
	case PACKET3_SET_CONTEXT_REG_INDIRECT:
	case PACKET3_SET_SH_REG:
	case PACKET3_SET_SH_REG_OFFSET:
	case PACKET3_INCREMENT_DE_COUNTER:
	case PACKET3_WAIT_ON_CE_COUNTER:
	case PACKET3_WAIT_ON_AVAIL_BUFFER:
	case PACKET3_ME_WRITE:
		break;
	case PACKET3_COPY_DATA:
		if ((idx_value & 0xf00) == 0) {
			reg = ib[idx + 3] * 4;
			if (!si_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_WRITE_DATA:
		if ((idx_value & 0xf00) == 0) {
			start_reg = ib[idx + 1] * 4;
			if (idx_value & 0x10000) {
				if (!si_vm_reg_valid(start_reg))
					return -EINVAL;
			} else {
				for (i = 0; i < (pkt->count - 2); i++) {
					reg = start_reg + (4 * i);
					if (!si_vm_reg_valid(reg))
						return -EINVAL;
				}
			}
		}
		break;
	case PACKET3_COND_WRITE:
		if (idx_value & 0x100) {
			reg = ib[idx + 5] * 4;
			if (!si_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_COPY_DW:
		if (idx_value & 0x2) {
			reg = ib[idx + 3] * 4;
			if (!si_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	default:
		DRM_ERROR("Invalid Compute packet3: 0x%x\n", pkt->opcode);
		return -EINVAL;
	}
	return 0;
}

int si_ib_parse(struct radeon_device *rdev, struct radeon_ib *ib)
{
	int ret = 0;
	u32 idx = 0;
	struct radeon_cs_packet pkt;

	do {
		pkt.idx = idx;
		pkt.type = RADEON_CP_PACKET_GET_TYPE(ib->ptr[idx]);
		pkt.count = RADEON_CP_PACKET_GET_COUNT(ib->ptr[idx]);
		pkt.one_reg_wr = 0;
		switch (pkt.type) {
		case RADEON_PACKET_TYPE0:
			dev_err(rdev->dev, "Packet0 not allowed!\n");
			ret = -EINVAL;
			break;
		case RADEON_PACKET_TYPE2:
			idx += 1;
			break;
		case RADEON_PACKET_TYPE3:
			pkt.opcode = RADEON_CP_PACKET3_GET_OPCODE(ib->ptr[idx]);
			if (ib->is_const_ib)
				ret = si_vm_packet3_ce_check(rdev, ib->ptr, &pkt);
			else {
				switch (ib->ring) {
				case RADEON_RING_TYPE_GFX_INDEX:
					ret = si_vm_packet3_gfx_check(rdev, ib->ptr, &pkt);
					break;
				case CAYMAN_RING_TYPE_CP1_INDEX:
				case CAYMAN_RING_TYPE_CP2_INDEX:
					ret = si_vm_packet3_compute_check(rdev, ib->ptr, &pkt);
					break;
				default:
					dev_err(rdev->dev, "Non-PM4 ring %d !\n", ib->ring);
					ret = -EINVAL;
					break;
				}
			}
			idx += pkt.count + 2;
			break;
		default:
			dev_err(rdev->dev, "Unknown packet type %d !\n", pkt.type);
			ret = -EINVAL;
			break;
		}
		if (ret)
			break;
	} while (idx < ib->length_dw);

	return ret;
}

/*
 * vm
 */
int si_vm_init(struct radeon_device *rdev)
{
	/* number of VMs */
	rdev->vm_manager.nvm = 16;
	/* base offset of vram pages */
	rdev->vm_manager.vram_base_offset = 0;

	return 0;
}

void si_vm_fini(struct radeon_device *rdev)
{
}

/**
 * si_vm_set_page - update the page tables using the CP
 *
 * @rdev: radeon_device pointer
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: access flags
 *
 * Update the page tables using the CP (SI).
 */
void si_vm_set_page(struct radeon_device *rdev,
		    struct radeon_ib *ib,
		    uint64_t pe,
		    uint64_t addr, unsigned count,
		    uint32_t incr, uint32_t flags)
{
	uint32_t r600_flags = cayman_vm_page_flags(rdev, flags);
	uint64_t value;
	unsigned ndw;

	if (rdev->asic->vm.pt_ring_index == RADEON_RING_TYPE_GFX_INDEX) {
		while (count) {
			ndw = 2 + count * 2;
			if (ndw > 0x3FFE)
				ndw = 0x3FFE;

			ib->ptr[ib->length_dw++] = PACKET3(PACKET3_WRITE_DATA, ndw);
			ib->ptr[ib->length_dw++] = (WRITE_DATA_ENGINE_SEL(0) |
					WRITE_DATA_DST_SEL(1));
			ib->ptr[ib->length_dw++] = pe;
			ib->ptr[ib->length_dw++] = upper_32_bits(pe);
			for (; ndw > 2; ndw -= 2, --count, pe += 8) {
				if (flags & RADEON_VM_PAGE_SYSTEM) {
					value = radeon_vm_map_gart(rdev, addr);
					value &= 0xFFFFFFFFFFFFF000ULL;
				} else if (flags & RADEON_VM_PAGE_VALID) {
					value = addr;
				} else {
					value = 0;
				}
				addr += incr;
				value |= r600_flags;
				ib->ptr[ib->length_dw++] = value;
				ib->ptr[ib->length_dw++] = upper_32_bits(value);
			}
		}
	} else {
		/* DMA */
		if (flags & RADEON_VM_PAGE_SYSTEM) {
			while (count) {
				ndw = count * 2;
				if (ndw > 0xFFFFE)
					ndw = 0xFFFFE;

				/* for non-physically contiguous pages (system) */
				ib->ptr[ib->length_dw++] = DMA_PACKET(DMA_PACKET_WRITE, 0, 0, 0, ndw);
				ib->ptr[ib->length_dw++] = pe;
				ib->ptr[ib->length_dw++] = upper_32_bits(pe) & 0xff;
				for (; ndw > 0; ndw -= 2, --count, pe += 8) {
					if (flags & RADEON_VM_PAGE_SYSTEM) {
						value = radeon_vm_map_gart(rdev, addr);
						value &= 0xFFFFFFFFFFFFF000ULL;
					} else if (flags & RADEON_VM_PAGE_VALID) {
						value = addr;
					} else {
						value = 0;
					}
					addr += incr;
					value |= r600_flags;
					ib->ptr[ib->length_dw++] = value;
					ib->ptr[ib->length_dw++] = upper_32_bits(value);
				}
			}
		} else {
			while (count) {
				ndw = count * 2;
				if (ndw > 0xFFFFE)
					ndw = 0xFFFFE;

				if (flags & RADEON_VM_PAGE_VALID)
					value = addr;
				else
					value = 0;
				/* for physically contiguous pages (vram) */
				ib->ptr[ib->length_dw++] = DMA_PTE_PDE_PACKET(ndw);
				ib->ptr[ib->length_dw++] = pe; /* dst addr */
				ib->ptr[ib->length_dw++] = upper_32_bits(pe) & 0xff;
				ib->ptr[ib->length_dw++] = r600_flags; /* mask */
				ib->ptr[ib->length_dw++] = 0;
				ib->ptr[ib->length_dw++] = value; /* value */
				ib->ptr[ib->length_dw++] = upper_32_bits(value);
				ib->ptr[ib->length_dw++] = incr; /* increment size */
				ib->ptr[ib->length_dw++] = 0;
				pe += ndw * 4;
				addr += (ndw / 2) * incr;
				count -= ndw / 2;
			}
		}
		while (ib->length_dw & 0x7)
			ib->ptr[ib->length_dw++] = DMA_PACKET(DMA_PACKET_NOP, 0, 0, 0, 0);
	}
}

void si_vm_flush(struct radeon_device *rdev, int ridx, struct radeon_vm *vm)
{
	struct radeon_ring *ring = &rdev->ring[ridx];

	if (vm == NULL)
		return;

	/* write new base address */
	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(0)));

	if (vm->id < 8) {
		radeon_ring_write(ring,
				  (VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (vm->id << 2)) >> 2);
	} else {
		radeon_ring_write(ring,
				  (VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + ((vm->id - 8) << 2)) >> 2);
	}
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, vm->pd_gpu_addr >> 12);

	/* flush hdp cache */
	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(0)));
	radeon_ring_write(ring, HDP_MEM_COHERENCY_FLUSH_CNTL >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 0x1);

	/* bits 0-15 are the VM contexts0-15 */
	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(0)));
	radeon_ring_write(ring, VM_INVALIDATE_REQUEST >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 1 << vm->id);

	/* sync PFP to ME, otherwise we might get invalid PFP reads */
	radeon_ring_write(ring, PACKET3(PACKET3_PFP_SYNC_ME, 0));
	radeon_ring_write(ring, 0x0);
}

void si_dma_vm_flush(struct radeon_device *rdev, int ridx, struct radeon_vm *vm)
{
	struct radeon_ring *ring = &rdev->ring[ridx];

	if (vm == NULL)
		return;

	radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_SRBM_WRITE, 0, 0, 0, 0));
	if (vm->id < 8) {
		radeon_ring_write(ring, (0xf << 16) | ((VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (vm->id << 2)) >> 2));
	} else {
		radeon_ring_write(ring, (0xf << 16) | ((VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + ((vm->id - 8) << 2)) >> 2));
	}
	radeon_ring_write(ring, vm->pd_gpu_addr >> 12);

	/* flush hdp cache */
	radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_SRBM_WRITE, 0, 0, 0, 0));
	radeon_ring_write(ring, (0xf << 16) | (HDP_MEM_COHERENCY_FLUSH_CNTL >> 2));
	radeon_ring_write(ring, 1);

	/* bits 0-7 are the VM contexts0-7 */
	radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_SRBM_WRITE, 0, 0, 0, 0));
	radeon_ring_write(ring, (0xf << 16) | (VM_INVALIDATE_REQUEST >> 2));
	radeon_ring_write(ring, 1 << vm->id);
}

/*
 * RLC
 */
void si_rlc_fini(struct radeon_device *rdev)
{
	int r;

	/* save restore block */
	if (rdev->rlc.save_restore_obj) {
		r = radeon_bo_reserve(rdev->rlc.save_restore_obj, false);
		if (unlikely(r != 0))
			dev_warn(rdev->dev, "(%d) reserve RLC sr bo failed\n", r);
		radeon_bo_unpin(rdev->rlc.save_restore_obj);
		radeon_bo_unreserve(rdev->rlc.save_restore_obj);

		radeon_bo_unref(&rdev->rlc.save_restore_obj);
		rdev->rlc.save_restore_obj = NULL;
	}

	/* clear state block */
	if (rdev->rlc.clear_state_obj) {
		r = radeon_bo_reserve(rdev->rlc.clear_state_obj, false);
		if (unlikely(r != 0))
			dev_warn(rdev->dev, "(%d) reserve RLC c bo failed\n", r);
		radeon_bo_unpin(rdev->rlc.clear_state_obj);
		radeon_bo_unreserve(rdev->rlc.clear_state_obj);

		radeon_bo_unref(&rdev->rlc.clear_state_obj);
		rdev->rlc.clear_state_obj = NULL;
	}
}

int si_rlc_init(struct radeon_device *rdev)
{
	int r;

	/* save restore block */
	if (rdev->rlc.save_restore_obj == NULL) {
		r = radeon_bo_create(rdev, RADEON_GPU_PAGE_SIZE, PAGE_SIZE, true,
				     RADEON_GEM_DOMAIN_VRAM, NULL,
				     &rdev->rlc.save_restore_obj);
		if (r) {
			dev_warn(rdev->dev, "(%d) create RLC sr bo failed\n", r);
			return r;
		}
	}

	r = radeon_bo_reserve(rdev->rlc.save_restore_obj, false);
	if (unlikely(r != 0)) {
		si_rlc_fini(rdev);
		return r;
	}
	r = radeon_bo_pin(rdev->rlc.save_restore_obj, RADEON_GEM_DOMAIN_VRAM,
			  &rdev->rlc.save_restore_gpu_addr);
	radeon_bo_unreserve(rdev->rlc.save_restore_obj);
	if (r) {
		dev_warn(rdev->dev, "(%d) pin RLC sr bo failed\n", r);
		si_rlc_fini(rdev);
		return r;
	}

	/* clear state block */
	if (rdev->rlc.clear_state_obj == NULL) {
		r = radeon_bo_create(rdev, RADEON_GPU_PAGE_SIZE, PAGE_SIZE, true,
				     RADEON_GEM_DOMAIN_VRAM, NULL,
				     &rdev->rlc.clear_state_obj);
		if (r) {
			dev_warn(rdev->dev, "(%d) create RLC c bo failed\n", r);
			si_rlc_fini(rdev);
			return r;
		}
	}
	r = radeon_bo_reserve(rdev->rlc.clear_state_obj, false);
	if (unlikely(r != 0)) {
		si_rlc_fini(rdev);
		return r;
	}
	r = radeon_bo_pin(rdev->rlc.clear_state_obj, RADEON_GEM_DOMAIN_VRAM,
			  &rdev->rlc.clear_state_gpu_addr);
	radeon_bo_unreserve(rdev->rlc.clear_state_obj);
	if (r) {
		dev_warn(rdev->dev, "(%d) pin RLC c bo failed\n", r);
		si_rlc_fini(rdev);
		return r;
	}

	return 0;
}

static void si_rlc_stop(struct radeon_device *rdev)
{
	WREG32(RLC_CNTL, 0);
}

static void si_rlc_start(struct radeon_device *rdev)
{
	WREG32(RLC_CNTL, RLC_ENABLE);
}

static int si_rlc_resume(struct radeon_device *rdev)
{
	u32 i;
	const __be32 *fw_data;

	if (!rdev->rlc_fw)
		return -EINVAL;

	si_rlc_stop(rdev);

	WREG32(RLC_RL_BASE, 0);
	WREG32(RLC_RL_SIZE, 0);
	WREG32(RLC_LB_CNTL, 0);
	WREG32(RLC_LB_CNTR_MAX, 0xffffffff);
	WREG32(RLC_LB_CNTR_INIT, 0);

	WREG32(RLC_SAVE_AND_RESTORE_BASE, rdev->rlc.save_restore_gpu_addr >> 8);
	WREG32(RLC_CLEAR_STATE_RESTORE_BASE, rdev->rlc.clear_state_gpu_addr >> 8);

	WREG32(RLC_MC_CNTL, 0);
	WREG32(RLC_UCODE_CNTL, 0);

	fw_data = (const __be32 *)rdev->rlc_fw->data;
	for (i = 0; i < SI_RLC_UCODE_SIZE; i++) {
		WREG32(RLC_UCODE_ADDR, i);
		WREG32(RLC_UCODE_DATA, be32_to_cpup(fw_data++));
	}
	WREG32(RLC_UCODE_ADDR, 0);

	si_rlc_start(rdev);

	return 0;
}

static void si_enable_interrupts(struct radeon_device *rdev)
{
	u32 ih_cntl = RREG32(IH_CNTL);
	u32 ih_rb_cntl = RREG32(IH_RB_CNTL);

	ih_cntl |= ENABLE_INTR;
	ih_rb_cntl |= IH_RB_ENABLE;
	WREG32(IH_CNTL, ih_cntl);
	WREG32(IH_RB_CNTL, ih_rb_cntl);
	rdev->ih.enabled = true;
}

static void si_disable_interrupts(struct radeon_device *rdev)
{
	u32 ih_rb_cntl = RREG32(IH_RB_CNTL);
	u32 ih_cntl = RREG32(IH_CNTL);

	ih_rb_cntl &= ~IH_RB_ENABLE;
	ih_cntl &= ~ENABLE_INTR;
	WREG32(IH_RB_CNTL, ih_rb_cntl);
	WREG32(IH_CNTL, ih_cntl);
	/* set rptr, wptr to 0 */
	WREG32(IH_RB_RPTR, 0);
	WREG32(IH_RB_WPTR, 0);
	rdev->ih.enabled = false;
	rdev->ih.rptr = 0;
}

static void si_disable_interrupt_state(struct radeon_device *rdev)
{
	u32 tmp;

	WREG32(CP_INT_CNTL_RING0, CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE);
	WREG32(CP_INT_CNTL_RING1, 0);
	WREG32(CP_INT_CNTL_RING2, 0);
	tmp = RREG32(DMA_CNTL + DMA0_REGISTER_OFFSET) & ~TRAP_ENABLE;
	WREG32(DMA_CNTL + DMA0_REGISTER_OFFSET, tmp);
	tmp = RREG32(DMA_CNTL + DMA1_REGISTER_OFFSET) & ~TRAP_ENABLE;
	WREG32(DMA_CNTL + DMA1_REGISTER_OFFSET, tmp);
	WREG32(GRBM_INT_CNTL, 0);
	if (rdev->num_crtc >= 2) {
		WREG32(INT_MASK + EVERGREEN_CRTC0_REGISTER_OFFSET, 0);
		WREG32(INT_MASK + EVERGREEN_CRTC1_REGISTER_OFFSET, 0);
	}
	if (rdev->num_crtc >= 4) {
		WREG32(INT_MASK + EVERGREEN_CRTC2_REGISTER_OFFSET, 0);
		WREG32(INT_MASK + EVERGREEN_CRTC3_REGISTER_OFFSET, 0);
	}
	if (rdev->num_crtc >= 6) {
		WREG32(INT_MASK + EVERGREEN_CRTC4_REGISTER_OFFSET, 0);
		WREG32(INT_MASK + EVERGREEN_CRTC5_REGISTER_OFFSET, 0);
	}

	if (rdev->num_crtc >= 2) {
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC0_REGISTER_OFFSET, 0);
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC1_REGISTER_OFFSET, 0);
	}
	if (rdev->num_crtc >= 4) {
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC2_REGISTER_OFFSET, 0);
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC3_REGISTER_OFFSET, 0);
	}
	if (rdev->num_crtc >= 6) {
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC4_REGISTER_OFFSET, 0);
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC5_REGISTER_OFFSET, 0);
	}

	if (!ASIC_IS_NODCE(rdev)) {
		WREG32(DACA_AUTODETECT_INT_CONTROL, 0);

		tmp = RREG32(DC_HPD1_INT_CONTROL) & DC_HPDx_INT_POLARITY;
		WREG32(DC_HPD1_INT_CONTROL, tmp);
		tmp = RREG32(DC_HPD2_INT_CONTROL) & DC_HPDx_INT_POLARITY;
		WREG32(DC_HPD2_INT_CONTROL, tmp);
		tmp = RREG32(DC_HPD3_INT_CONTROL) & DC_HPDx_INT_POLARITY;
		WREG32(DC_HPD3_INT_CONTROL, tmp);
		tmp = RREG32(DC_HPD4_INT_CONTROL) & DC_HPDx_INT_POLARITY;
		WREG32(DC_HPD4_INT_CONTROL, tmp);
		tmp = RREG32(DC_HPD5_INT_CONTROL) & DC_HPDx_INT_POLARITY;
		WREG32(DC_HPD5_INT_CONTROL, tmp);
		tmp = RREG32(DC_HPD6_INT_CONTROL) & DC_HPDx_INT_POLARITY;
		WREG32(DC_HPD6_INT_CONTROL, tmp);
	}
}

static int si_irq_init(struct radeon_device *rdev)
{
	int ret = 0;
	int rb_bufsz;
	u32 interrupt_cntl, ih_cntl, ih_rb_cntl;

	/* allocate ring */
	ret = r600_ih_ring_alloc(rdev);
	if (ret)
		return ret;

	/* disable irqs */
	si_disable_interrupts(rdev);

	/* init rlc */
	ret = si_rlc_resume(rdev);
	if (ret) {
		r600_ih_ring_fini(rdev);
		return ret;
	}

	/* setup interrupt control */
	/* set dummy read address to ring address */
	WREG32(INTERRUPT_CNTL2, rdev->ih.gpu_addr >> 8);
	interrupt_cntl = RREG32(INTERRUPT_CNTL);
	/* IH_DUMMY_RD_OVERRIDE=0 - dummy read disabled with msi, enabled without msi
	 * IH_DUMMY_RD_OVERRIDE=1 - dummy read controlled by IH_DUMMY_RD_EN
	 */
	interrupt_cntl &= ~IH_DUMMY_RD_OVERRIDE;
	/* IH_REQ_NONSNOOP_EN=1 if ring is in non-cacheable memory, e.g., vram */
	interrupt_cntl &= ~IH_REQ_NONSNOOP_EN;
	WREG32(INTERRUPT_CNTL, interrupt_cntl);

	WREG32(IH_RB_BASE, rdev->ih.gpu_addr >> 8);
	rb_bufsz = drm_order(rdev->ih.ring_size / 4);

	ih_rb_cntl = (IH_WPTR_OVERFLOW_ENABLE |
		      IH_WPTR_OVERFLOW_CLEAR |
		      (rb_bufsz << 1));

	if (rdev->wb.enabled)
		ih_rb_cntl |= IH_WPTR_WRITEBACK_ENABLE;

	/* set the writeback address whether it's enabled or not */
	WREG32(IH_RB_WPTR_ADDR_LO, (rdev->wb.gpu_addr + R600_WB_IH_WPTR_OFFSET) & 0xFFFFFFFC);
	WREG32(IH_RB_WPTR_ADDR_HI, upper_32_bits(rdev->wb.gpu_addr + R600_WB_IH_WPTR_OFFSET) & 0xFF);

	WREG32(IH_RB_CNTL, ih_rb_cntl);

	/* set rptr, wptr to 0 */
	WREG32(IH_RB_RPTR, 0);
	WREG32(IH_RB_WPTR, 0);

	/* Default settings for IH_CNTL (disabled at first) */
	ih_cntl = MC_WRREQ_CREDIT(0x10) | MC_WR_CLEAN_CNT(0x10) | MC_VMID(0);
	/* RPTR_REARM only works if msi's are enabled */
	if (rdev->msi_enabled)
		ih_cntl |= RPTR_REARM;
	WREG32(IH_CNTL, ih_cntl);

	/* force the active interrupt state to all disabled */
	si_disable_interrupt_state(rdev);

	pci_set_master(rdev->pdev);

	/* enable irqs */
	si_enable_interrupts(rdev);

	return ret;
}

int si_irq_set(struct radeon_device *rdev)
{
	u32 cp_int_cntl = CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE;
	u32 cp_int_cntl1 = 0, cp_int_cntl2 = 0;
	u32 crtc1 = 0, crtc2 = 0, crtc3 = 0, crtc4 = 0, crtc5 = 0, crtc6 = 0;
	u32 hpd1 = 0, hpd2 = 0, hpd3 = 0, hpd4 = 0, hpd5 = 0, hpd6 = 0;
	u32 grbm_int_cntl = 0;
	u32 grph1 = 0, grph2 = 0, grph3 = 0, grph4 = 0, grph5 = 0, grph6 = 0;
	u32 dma_cntl, dma_cntl1;

	if (!rdev->irq.installed) {
		WARN(1, "Can't enable IRQ/MSI because no handler is installed\n");
		return -EINVAL;
	}
	/* don't enable anything if the ih is disabled */
	if (!rdev->ih.enabled) {
		si_disable_interrupts(rdev);
		/* force the active interrupt state to all disabled */
		si_disable_interrupt_state(rdev);
		return 0;
	}

	if (!ASIC_IS_NODCE(rdev)) {
		hpd1 = RREG32(DC_HPD1_INT_CONTROL) & ~DC_HPDx_INT_EN;
		hpd2 = RREG32(DC_HPD2_INT_CONTROL) & ~DC_HPDx_INT_EN;
		hpd3 = RREG32(DC_HPD3_INT_CONTROL) & ~DC_HPDx_INT_EN;
		hpd4 = RREG32(DC_HPD4_INT_CONTROL) & ~DC_HPDx_INT_EN;
		hpd5 = RREG32(DC_HPD5_INT_CONTROL) & ~DC_HPDx_INT_EN;
		hpd6 = RREG32(DC_HPD6_INT_CONTROL) & ~DC_HPDx_INT_EN;
	}

	dma_cntl = RREG32(DMA_CNTL + DMA0_REGISTER_OFFSET) & ~TRAP_ENABLE;
	dma_cntl1 = RREG32(DMA_CNTL + DMA1_REGISTER_OFFSET) & ~TRAP_ENABLE;

	/* enable CP interrupts on all rings */
	if (atomic_read(&rdev->irq.ring_int[RADEON_RING_TYPE_GFX_INDEX])) {
		DRM_DEBUG("si_irq_set: sw int gfx\n");
		cp_int_cntl |= TIME_STAMP_INT_ENABLE;
	}
	if (atomic_read(&rdev->irq.ring_int[CAYMAN_RING_TYPE_CP1_INDEX])) {
		DRM_DEBUG("si_irq_set: sw int cp1\n");
		cp_int_cntl1 |= TIME_STAMP_INT_ENABLE;
	}
	if (atomic_read(&rdev->irq.ring_int[CAYMAN_RING_TYPE_CP2_INDEX])) {
		DRM_DEBUG("si_irq_set: sw int cp2\n");
		cp_int_cntl2 |= TIME_STAMP_INT_ENABLE;
	}
	if (atomic_read(&rdev->irq.ring_int[R600_RING_TYPE_DMA_INDEX])) {
		DRM_DEBUG("si_irq_set: sw int dma\n");
		dma_cntl |= TRAP_ENABLE;
	}

	if (atomic_read(&rdev->irq.ring_int[CAYMAN_RING_TYPE_DMA1_INDEX])) {
		DRM_DEBUG("si_irq_set: sw int dma1\n");
		dma_cntl1 |= TRAP_ENABLE;
	}
	if (rdev->irq.crtc_vblank_int[0] ||
	    atomic_read(&rdev->irq.pflip[0])) {
		DRM_DEBUG("si_irq_set: vblank 0\n");
		crtc1 |= VBLANK_INT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[1] ||
	    atomic_read(&rdev->irq.pflip[1])) {
		DRM_DEBUG("si_irq_set: vblank 1\n");
		crtc2 |= VBLANK_INT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[2] ||
	    atomic_read(&rdev->irq.pflip[2])) {
		DRM_DEBUG("si_irq_set: vblank 2\n");
		crtc3 |= VBLANK_INT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[3] ||
	    atomic_read(&rdev->irq.pflip[3])) {
		DRM_DEBUG("si_irq_set: vblank 3\n");
		crtc4 |= VBLANK_INT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[4] ||
	    atomic_read(&rdev->irq.pflip[4])) {
		DRM_DEBUG("si_irq_set: vblank 4\n");
		crtc5 |= VBLANK_INT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[5] ||
	    atomic_read(&rdev->irq.pflip[5])) {
		DRM_DEBUG("si_irq_set: vblank 5\n");
		crtc6 |= VBLANK_INT_MASK;
	}
	if (rdev->irq.hpd[0]) {
		DRM_DEBUG("si_irq_set: hpd 1\n");
		hpd1 |= DC_HPDx_INT_EN;
	}
	if (rdev->irq.hpd[1]) {
		DRM_DEBUG("si_irq_set: hpd 2\n");
		hpd2 |= DC_HPDx_INT_EN;
	}
	if (rdev->irq.hpd[2]) {
		DRM_DEBUG("si_irq_set: hpd 3\n");
		hpd3 |= DC_HPDx_INT_EN;
	}
	if (rdev->irq.hpd[3]) {
		DRM_DEBUG("si_irq_set: hpd 4\n");
		hpd4 |= DC_HPDx_INT_EN;
	}
	if (rdev->irq.hpd[4]) {
		DRM_DEBUG("si_irq_set: hpd 5\n");
		hpd5 |= DC_HPDx_INT_EN;
	}
	if (rdev->irq.hpd[5]) {
		DRM_DEBUG("si_irq_set: hpd 6\n");
		hpd6 |= DC_HPDx_INT_EN;
	}

	WREG32(CP_INT_CNTL_RING0, cp_int_cntl);
	WREG32(CP_INT_CNTL_RING1, cp_int_cntl1);
	WREG32(CP_INT_CNTL_RING2, cp_int_cntl2);

	WREG32(DMA_CNTL + DMA0_REGISTER_OFFSET, dma_cntl);
	WREG32(DMA_CNTL + DMA1_REGISTER_OFFSET, dma_cntl1);

	WREG32(GRBM_INT_CNTL, grbm_int_cntl);

	if (rdev->num_crtc >= 2) {
		WREG32(INT_MASK + EVERGREEN_CRTC0_REGISTER_OFFSET, crtc1);
		WREG32(INT_MASK + EVERGREEN_CRTC1_REGISTER_OFFSET, crtc2);
	}
	if (rdev->num_crtc >= 4) {
		WREG32(INT_MASK + EVERGREEN_CRTC2_REGISTER_OFFSET, crtc3);
		WREG32(INT_MASK + EVERGREEN_CRTC3_REGISTER_OFFSET, crtc4);
	}
	if (rdev->num_crtc >= 6) {
		WREG32(INT_MASK + EVERGREEN_CRTC4_REGISTER_OFFSET, crtc5);
		WREG32(INT_MASK + EVERGREEN_CRTC5_REGISTER_OFFSET, crtc6);
	}

	if (rdev->num_crtc >= 2) {
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC0_REGISTER_OFFSET, grph1);
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC1_REGISTER_OFFSET, grph2);
	}
	if (rdev->num_crtc >= 4) {
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC2_REGISTER_OFFSET, grph3);
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC3_REGISTER_OFFSET, grph4);
	}
	if (rdev->num_crtc >= 6) {
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC4_REGISTER_OFFSET, grph5);
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC5_REGISTER_OFFSET, grph6);
	}

	if (!ASIC_IS_NODCE(rdev)) {
		WREG32(DC_HPD1_INT_CONTROL, hpd1);
		WREG32(DC_HPD2_INT_CONTROL, hpd2);
		WREG32(DC_HPD3_INT_CONTROL, hpd3);
		WREG32(DC_HPD4_INT_CONTROL, hpd4);
		WREG32(DC_HPD5_INT_CONTROL, hpd5);
		WREG32(DC_HPD6_INT_CONTROL, hpd6);
	}

	return 0;
}

static inline void si_irq_ack(struct radeon_device *rdev)
{
	u32 tmp;

	if (ASIC_IS_NODCE(rdev))
		return;

	rdev->irq.stat_regs.evergreen.disp_int = RREG32(DISP_INTERRUPT_STATUS);
	rdev->irq.stat_regs.evergreen.disp_int_cont = RREG32(DISP_INTERRUPT_STATUS_CONTINUE);
	rdev->irq.stat_regs.evergreen.disp_int_cont2 = RREG32(DISP_INTERRUPT_STATUS_CONTINUE2);
	rdev->irq.stat_regs.evergreen.disp_int_cont3 = RREG32(DISP_INTERRUPT_STATUS_CONTINUE3);
	rdev->irq.stat_regs.evergreen.disp_int_cont4 = RREG32(DISP_INTERRUPT_STATUS_CONTINUE4);
	rdev->irq.stat_regs.evergreen.disp_int_cont5 = RREG32(DISP_INTERRUPT_STATUS_CONTINUE5);
	rdev->irq.stat_regs.evergreen.d1grph_int = RREG32(GRPH_INT_STATUS + EVERGREEN_CRTC0_REGISTER_OFFSET);
	rdev->irq.stat_regs.evergreen.d2grph_int = RREG32(GRPH_INT_STATUS + EVERGREEN_CRTC1_REGISTER_OFFSET);
	if (rdev->num_crtc >= 4) {
		rdev->irq.stat_regs.evergreen.d3grph_int = RREG32(GRPH_INT_STATUS + EVERGREEN_CRTC2_REGISTER_OFFSET);
		rdev->irq.stat_regs.evergreen.d4grph_int = RREG32(GRPH_INT_STATUS + EVERGREEN_CRTC3_REGISTER_OFFSET);
	}
	if (rdev->num_crtc >= 6) {
		rdev->irq.stat_regs.evergreen.d5grph_int = RREG32(GRPH_INT_STATUS + EVERGREEN_CRTC4_REGISTER_OFFSET);
		rdev->irq.stat_regs.evergreen.d6grph_int = RREG32(GRPH_INT_STATUS + EVERGREEN_CRTC5_REGISTER_OFFSET);
	}

	if (rdev->irq.stat_regs.evergreen.d1grph_int & GRPH_PFLIP_INT_OCCURRED)
		WREG32(GRPH_INT_STATUS + EVERGREEN_CRTC0_REGISTER_OFFSET, GRPH_PFLIP_INT_CLEAR);
	if (rdev->irq.stat_regs.evergreen.d2grph_int & GRPH_PFLIP_INT_OCCURRED)
		WREG32(GRPH_INT_STATUS + EVERGREEN_CRTC1_REGISTER_OFFSET, GRPH_PFLIP_INT_CLEAR);
	if (rdev->irq.stat_regs.evergreen.disp_int & LB_D1_VBLANK_INTERRUPT)
		WREG32(VBLANK_STATUS + EVERGREEN_CRTC0_REGISTER_OFFSET, VBLANK_ACK);
	if (rdev->irq.stat_regs.evergreen.disp_int & LB_D1_VLINE_INTERRUPT)
		WREG32(VLINE_STATUS + EVERGREEN_CRTC0_REGISTER_OFFSET, VLINE_ACK);
	if (rdev->irq.stat_regs.evergreen.disp_int_cont & LB_D2_VBLANK_INTERRUPT)
		WREG32(VBLANK_STATUS + EVERGREEN_CRTC1_REGISTER_OFFSET, VBLANK_ACK);
	if (rdev->irq.stat_regs.evergreen.disp_int_cont & LB_D2_VLINE_INTERRUPT)
		WREG32(VLINE_STATUS + EVERGREEN_CRTC1_REGISTER_OFFSET, VLINE_ACK);

	if (rdev->num_crtc >= 4) {
		if (rdev->irq.stat_regs.evergreen.d3grph_int & GRPH_PFLIP_INT_OCCURRED)
			WREG32(GRPH_INT_STATUS + EVERGREEN_CRTC2_REGISTER_OFFSET, GRPH_PFLIP_INT_CLEAR);
		if (rdev->irq.stat_regs.evergreen.d4grph_int & GRPH_PFLIP_INT_OCCURRED)
			WREG32(GRPH_INT_STATUS + EVERGREEN_CRTC3_REGISTER_OFFSET, GRPH_PFLIP_INT_CLEAR);
		if (rdev->irq.stat_regs.evergreen.disp_int_cont2 & LB_D3_VBLANK_INTERRUPT)
			WREG32(VBLANK_STATUS + EVERGREEN_CRTC2_REGISTER_OFFSET, VBLANK_ACK);
		if (rdev->irq.stat_regs.evergreen.disp_int_cont2 & LB_D3_VLINE_INTERRUPT)
			WREG32(VLINE_STATUS + EVERGREEN_CRTC2_REGISTER_OFFSET, VLINE_ACK);
		if (rdev->irq.stat_regs.evergreen.disp_int_cont3 & LB_D4_VBLANK_INTERRUPT)
			WREG32(VBLANK_STATUS + EVERGREEN_CRTC3_REGISTER_OFFSET, VBLANK_ACK);
		if (rdev->irq.stat_regs.evergreen.disp_int_cont3 & LB_D4_VLINE_INTERRUPT)
			WREG32(VLINE_STATUS + EVERGREEN_CRTC3_REGISTER_OFFSET, VLINE_ACK);
	}

	if (rdev->num_crtc >= 6) {
		if (rdev->irq.stat_regs.evergreen.d5grph_int & GRPH_PFLIP_INT_OCCURRED)
			WREG32(GRPH_INT_STATUS + EVERGREEN_CRTC4_REGISTER_OFFSET, GRPH_PFLIP_INT_CLEAR);
		if (rdev->irq.stat_regs.evergreen.d6grph_int & GRPH_PFLIP_INT_OCCURRED)
			WREG32(GRPH_INT_STATUS + EVERGREEN_CRTC5_REGISTER_OFFSET, GRPH_PFLIP_INT_CLEAR);
		if (rdev->irq.stat_regs.evergreen.disp_int_cont4 & LB_D5_VBLANK_INTERRUPT)
			WREG32(VBLANK_STATUS + EVERGREEN_CRTC4_REGISTER_OFFSET, VBLANK_ACK);
		if (rdev->irq.stat_regs.evergreen.disp_int_cont4 & LB_D5_VLINE_INTERRUPT)
			WREG32(VLINE_STATUS + EVERGREEN_CRTC4_REGISTER_OFFSET, VLINE_ACK);
		if (rdev->irq.stat_regs.evergreen.disp_int_cont5 & LB_D6_VBLANK_INTERRUPT)
			WREG32(VBLANK_STATUS + EVERGREEN_CRTC5_REGISTER_OFFSET, VBLANK_ACK);
		if (rdev->irq.stat_regs.evergreen.disp_int_cont5 & LB_D6_VLINE_INTERRUPT)
			WREG32(VLINE_STATUS + EVERGREEN_CRTC5_REGISTER_OFFSET, VLINE_ACK);
	}

	if (rdev->irq.stat_regs.evergreen.disp_int & DC_HPD1_INTERRUPT) {
		tmp = RREG32(DC_HPD1_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD1_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.evergreen.disp_int_cont & DC_HPD2_INTERRUPT) {
		tmp = RREG32(DC_HPD2_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD2_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.evergreen.disp_int_cont2 & DC_HPD3_INTERRUPT) {
		tmp = RREG32(DC_HPD3_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD3_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.evergreen.disp_int_cont3 & DC_HPD4_INTERRUPT) {
		tmp = RREG32(DC_HPD4_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD4_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.evergreen.disp_int_cont4 & DC_HPD5_INTERRUPT) {
		tmp = RREG32(DC_HPD5_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD5_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.evergreen.disp_int_cont5 & DC_HPD6_INTERRUPT) {
		tmp = RREG32(DC_HPD5_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD6_INT_CONTROL, tmp);
	}
}

static void si_irq_disable(struct radeon_device *rdev)
{
	si_disable_interrupts(rdev);
	/* Wait and acknowledge irq */
	mdelay(1);
	si_irq_ack(rdev);
	si_disable_interrupt_state(rdev);
}

static void si_irq_suspend(struct radeon_device *rdev)
{
	si_irq_disable(rdev);
	si_rlc_stop(rdev);
}

static void si_irq_fini(struct radeon_device *rdev)
{
	si_irq_suspend(rdev);
	r600_ih_ring_fini(rdev);
}

static inline u32 si_get_ih_wptr(struct radeon_device *rdev)
{
	u32 wptr, tmp;

	if (rdev->wb.enabled)
		wptr = le32_to_cpu(rdev->wb.wb[R600_WB_IH_WPTR_OFFSET/4]);
	else
		wptr = RREG32(IH_RB_WPTR);

	if (wptr & RB_OVERFLOW) {
		/* When a ring buffer overflow happen start parsing interrupt
		 * from the last not overwritten vector (wptr + 16). Hopefully
		 * this should allow us to catchup.
		 */
		dev_warn(rdev->dev, "IH ring buffer overflow (0x%08X, %d, %d)\n",
			wptr, rdev->ih.rptr, (wptr + 16) + rdev->ih.ptr_mask);
		rdev->ih.rptr = (wptr + 16) & rdev->ih.ptr_mask;
		tmp = RREG32(IH_RB_CNTL);
		tmp |= IH_WPTR_OVERFLOW_CLEAR;
		WREG32(IH_RB_CNTL, tmp);
	}
	return (wptr & rdev->ih.ptr_mask);
}

/*        SI IV Ring
 * Each IV ring entry is 128 bits:
 * [7:0]    - interrupt source id
 * [31:8]   - reserved
 * [59:32]  - interrupt source data
 * [63:60]  - reserved
 * [71:64]  - RINGID
 * [79:72]  - VMID
 * [127:80] - reserved
 */
int si_irq_process(struct radeon_device *rdev)
{
	u32 wptr;
	u32 rptr;
	u32 src_id, src_data, ring_id;
	u32 ring_index;
	bool queue_hotplug = false;

	if (!rdev->ih.enabled || rdev->shutdown)
		return IRQ_NONE;

	wptr = si_get_ih_wptr(rdev);

restart_ih:
	/* is somebody else already processing irqs? */
	if (atomic_xchg(&rdev->ih.lock, 1))
		return IRQ_NONE;

	rptr = rdev->ih.rptr;
	DRM_DEBUG("si_irq_process start: rptr %d, wptr %d\n", rptr, wptr);

	/* Order reading of wptr vs. reading of IH ring data */
	rmb();

	/* display interrupts */
	si_irq_ack(rdev);

	while (rptr != wptr) {
		/* wptr/rptr are in bytes! */
		ring_index = rptr / 4;
		src_id =  le32_to_cpu(rdev->ih.ring[ring_index]) & 0xff;
		src_data = le32_to_cpu(rdev->ih.ring[ring_index + 1]) & 0xfffffff;
		ring_id = le32_to_cpu(rdev->ih.ring[ring_index + 2]) & 0xff;

		switch (src_id) {
		case 1: /* D1 vblank/vline */
			switch (src_data) {
			case 0: /* D1 vblank */
				if (rdev->irq.stat_regs.evergreen.disp_int & LB_D1_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[0]) {
						drm_handle_vblank(rdev->ddev, 0);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[0]))
						radeon_crtc_handle_flip(rdev, 0);
					rdev->irq.stat_regs.evergreen.disp_int &= ~LB_D1_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D1 vblank\n");
				}
				break;
			case 1: /* D1 vline */
				if (rdev->irq.stat_regs.evergreen.disp_int & LB_D1_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.evergreen.disp_int &= ~LB_D1_VLINE_INTERRUPT;
					DRM_DEBUG("IH: D1 vline\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 2: /* D2 vblank/vline */
			switch (src_data) {
			case 0: /* D2 vblank */
				if (rdev->irq.stat_regs.evergreen.disp_int_cont & LB_D2_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[1]) {
						drm_handle_vblank(rdev->ddev, 1);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[1]))
						radeon_crtc_handle_flip(rdev, 1);
					rdev->irq.stat_regs.evergreen.disp_int_cont &= ~LB_D2_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D2 vblank\n");
				}
				break;
			case 1: /* D2 vline */
				if (rdev->irq.stat_regs.evergreen.disp_int_cont & LB_D2_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.evergreen.disp_int_cont &= ~LB_D2_VLINE_INTERRUPT;
					DRM_DEBUG("IH: D2 vline\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 3: /* D3 vblank/vline */
			switch (src_data) {
			case 0: /* D3 vblank */
				if (rdev->irq.stat_regs.evergreen.disp_int_cont2 & LB_D3_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[2]) {
						drm_handle_vblank(rdev->ddev, 2);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[2]))
						radeon_crtc_handle_flip(rdev, 2);
					rdev->irq.stat_regs.evergreen.disp_int_cont2 &= ~LB_D3_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D3 vblank\n");
				}
				break;
			case 1: /* D3 vline */
				if (rdev->irq.stat_regs.evergreen.disp_int_cont2 & LB_D3_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.evergreen.disp_int_cont2 &= ~LB_D3_VLINE_INTERRUPT;
					DRM_DEBUG("IH: D3 vline\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 4: /* D4 vblank/vline */
			switch (src_data) {
			case 0: /* D4 vblank */
				if (rdev->irq.stat_regs.evergreen.disp_int_cont3 & LB_D4_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[3]) {
						drm_handle_vblank(rdev->ddev, 3);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[3]))
						radeon_crtc_handle_flip(rdev, 3);
					rdev->irq.stat_regs.evergreen.disp_int_cont3 &= ~LB_D4_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D4 vblank\n");
				}
				break;
			case 1: /* D4 vline */
				if (rdev->irq.stat_regs.evergreen.disp_int_cont3 & LB_D4_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.evergreen.disp_int_cont3 &= ~LB_D4_VLINE_INTERRUPT;
					DRM_DEBUG("IH: D4 vline\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 5: /* D5 vblank/vline */
			switch (src_data) {
			case 0: /* D5 vblank */
				if (rdev->irq.stat_regs.evergreen.disp_int_cont4 & LB_D5_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[4]) {
						drm_handle_vblank(rdev->ddev, 4);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[4]))
						radeon_crtc_handle_flip(rdev, 4);
					rdev->irq.stat_regs.evergreen.disp_int_cont4 &= ~LB_D5_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D5 vblank\n");
				}
				break;
			case 1: /* D5 vline */
				if (rdev->irq.stat_regs.evergreen.disp_int_cont4 & LB_D5_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.evergreen.disp_int_cont4 &= ~LB_D5_VLINE_INTERRUPT;
					DRM_DEBUG("IH: D5 vline\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 6: /* D6 vblank/vline */
			switch (src_data) {
			case 0: /* D6 vblank */
				if (rdev->irq.stat_regs.evergreen.disp_int_cont5 & LB_D6_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[5]) {
						drm_handle_vblank(rdev->ddev, 5);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[5]))
						radeon_crtc_handle_flip(rdev, 5);
					rdev->irq.stat_regs.evergreen.disp_int_cont5 &= ~LB_D6_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D6 vblank\n");
				}
				break;
			case 1: /* D6 vline */
				if (rdev->irq.stat_regs.evergreen.disp_int_cont5 & LB_D6_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.evergreen.disp_int_cont5 &= ~LB_D6_VLINE_INTERRUPT;
					DRM_DEBUG("IH: D6 vline\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 42: /* HPD hotplug */
			switch (src_data) {
			case 0:
				if (rdev->irq.stat_regs.evergreen.disp_int & DC_HPD1_INTERRUPT) {
					rdev->irq.stat_regs.evergreen.disp_int &= ~DC_HPD1_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD1\n");
				}
				break;
			case 1:
				if (rdev->irq.stat_regs.evergreen.disp_int_cont & DC_HPD2_INTERRUPT) {
					rdev->irq.stat_regs.evergreen.disp_int_cont &= ~DC_HPD2_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD2\n");
				}
				break;
			case 2:
				if (rdev->irq.stat_regs.evergreen.disp_int_cont2 & DC_HPD3_INTERRUPT) {
					rdev->irq.stat_regs.evergreen.disp_int_cont2 &= ~DC_HPD3_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD3\n");
				}
				break;
			case 3:
				if (rdev->irq.stat_regs.evergreen.disp_int_cont3 & DC_HPD4_INTERRUPT) {
					rdev->irq.stat_regs.evergreen.disp_int_cont3 &= ~DC_HPD4_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD4\n");
				}
				break;
			case 4:
				if (rdev->irq.stat_regs.evergreen.disp_int_cont4 & DC_HPD5_INTERRUPT) {
					rdev->irq.stat_regs.evergreen.disp_int_cont4 &= ~DC_HPD5_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD5\n");
				}
				break;
			case 5:
				if (rdev->irq.stat_regs.evergreen.disp_int_cont5 & DC_HPD6_INTERRUPT) {
					rdev->irq.stat_regs.evergreen.disp_int_cont5 &= ~DC_HPD6_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD6\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 146:
		case 147:
			dev_err(rdev->dev, "GPU fault detected: %d 0x%08x\n", src_id, src_data);
			dev_err(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_ADDR   0x%08X\n",
				RREG32(VM_CONTEXT1_PROTECTION_FAULT_ADDR));
			dev_err(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_STATUS 0x%08X\n",
				RREG32(VM_CONTEXT1_PROTECTION_FAULT_STATUS));
			/* reset addr and status */
			WREG32_P(VM_CONTEXT1_CNTL2, 1, ~1);
			break;
		case 176: /* RINGID0 CP_INT */
			radeon_fence_process(rdev, RADEON_RING_TYPE_GFX_INDEX);
			break;
		case 177: /* RINGID1 CP_INT */
			radeon_fence_process(rdev, CAYMAN_RING_TYPE_CP1_INDEX);
			break;
		case 178: /* RINGID2 CP_INT */
			radeon_fence_process(rdev, CAYMAN_RING_TYPE_CP2_INDEX);
			break;
		case 181: /* CP EOP event */
			DRM_DEBUG("IH: CP EOP\n");
			switch (ring_id) {
			case 0:
				radeon_fence_process(rdev, RADEON_RING_TYPE_GFX_INDEX);
				break;
			case 1:
				radeon_fence_process(rdev, CAYMAN_RING_TYPE_CP1_INDEX);
				break;
			case 2:
				radeon_fence_process(rdev, CAYMAN_RING_TYPE_CP2_INDEX);
				break;
			}
			break;
		case 224: /* DMA trap event */
			DRM_DEBUG("IH: DMA trap\n");
			radeon_fence_process(rdev, R600_RING_TYPE_DMA_INDEX);
			break;
		case 233: /* GUI IDLE */
			DRM_DEBUG("IH: GUI idle\n");
			break;
		case 244: /* DMA trap event */
			DRM_DEBUG("IH: DMA1 trap\n");
			radeon_fence_process(rdev, CAYMAN_RING_TYPE_DMA1_INDEX);
			break;
		default:
			DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
			break;
		}

		/* wptr/rptr are in bytes! */
		rptr += 16;
		rptr &= rdev->ih.ptr_mask;
	}
	if (queue_hotplug)
		schedule_work(&rdev->hotplug_work);
	rdev->ih.rptr = rptr;
	WREG32(IH_RB_RPTR, rdev->ih.rptr);
	atomic_set(&rdev->ih.lock, 0);

	/* make sure wptr hasn't changed while processing */
	wptr = si_get_ih_wptr(rdev);
	if (wptr != rptr)
		goto restart_ih;

	return IRQ_HANDLED;
}

/**
 * si_copy_dma - copy pages using the DMA engine
 *
 * @rdev: radeon_device pointer
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @num_gpu_pages: number of GPU pages to xfer
 * @fence: radeon fence object
 *
 * Copy GPU paging using the DMA engine (SI).
 * Used by the radeon ttm implementation to move pages if
 * registered as the asic copy callback.
 */
int si_copy_dma(struct radeon_device *rdev,
		uint64_t src_offset, uint64_t dst_offset,
		unsigned num_gpu_pages,
		struct radeon_fence **fence)
{
	struct radeon_semaphore *sem = NULL;
	int ring_index = rdev->asic->copy.dma_ring_index;
	struct radeon_ring *ring = &rdev->ring[ring_index];
	u32 size_in_bytes, cur_size_in_bytes;
	int i, num_loops;
	int r = 0;

	r = radeon_semaphore_create(rdev, &sem);
	if (r) {
		DRM_ERROR("radeon: moving bo (%d).\n", r);
		return r;
	}

	size_in_bytes = (num_gpu_pages << RADEON_GPU_PAGE_SHIFT);
	num_loops = DIV_ROUND_UP(size_in_bytes, 0xfffff);
	r = radeon_ring_lock(rdev, ring, num_loops * 5 + 11);
	if (r) {
		DRM_ERROR("radeon: moving bo (%d).\n", r);
		radeon_semaphore_free(rdev, &sem, NULL);
		return r;
	}

	if (radeon_fence_need_sync(*fence, ring->idx)) {
		radeon_semaphore_sync_rings(rdev, sem, (*fence)->ring,
					    ring->idx);
		radeon_fence_note_sync(*fence, ring->idx);
	} else {
		radeon_semaphore_free(rdev, &sem, NULL);
	}

	for (i = 0; i < num_loops; i++) {
		cur_size_in_bytes = size_in_bytes;
		if (cur_size_in_bytes > 0xFFFFF)
			cur_size_in_bytes = 0xFFFFF;
		size_in_bytes -= cur_size_in_bytes;
		radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_COPY, 1, 0, 0, cur_size_in_bytes));
		radeon_ring_write(ring, dst_offset & 0xffffffff);
		radeon_ring_write(ring, src_offset & 0xffffffff);
		radeon_ring_write(ring, upper_32_bits(dst_offset) & 0xff);
		radeon_ring_write(ring, upper_32_bits(src_offset) & 0xff);
		src_offset += cur_size_in_bytes;
		dst_offset += cur_size_in_bytes;
	}

	r = radeon_fence_emit(rdev, fence, ring->idx);
	if (r) {
		radeon_ring_unlock_undo(rdev, ring);
		return r;
	}

	radeon_ring_unlock_commit(rdev, ring);
	radeon_semaphore_free(rdev, &sem, *fence);

	return r;
}

/*
 * startup/shutdown callbacks
 */
static int si_startup(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	int r;

	if (!rdev->me_fw || !rdev->pfp_fw || !rdev->ce_fw ||
	    !rdev->rlc_fw || !rdev->mc_fw) {
		r = si_init_microcode(rdev);
		if (r) {
			DRM_ERROR("Failed to load firmware!\n");
			return r;
		}
	}

	r = si_mc_load_microcode(rdev);
	if (r) {
		DRM_ERROR("Failed to load MC firmware!\n");
		return r;
	}

	r = r600_vram_scratch_init(rdev);
	if (r)
		return r;

	si_mc_program(rdev);
	r = si_pcie_gart_enable(rdev);
	if (r)
		return r;
	si_gpu_init(rdev);

	/* allocate rlc buffers */
	r = si_rlc_init(rdev);
	if (r) {
		DRM_ERROR("Failed to init rlc BOs!\n");
		return r;
	}

	/* allocate wb buffer */
	r = radeon_wb_init(rdev);
	if (r)
		return r;

	r = radeon_fence_driver_start_ring(rdev, RADEON_RING_TYPE_GFX_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP fences (%d).\n", r);
		return r;
	}

	r = radeon_fence_driver_start_ring(rdev, CAYMAN_RING_TYPE_CP1_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP fences (%d).\n", r);
		return r;
	}

	r = radeon_fence_driver_start_ring(rdev, CAYMAN_RING_TYPE_CP2_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP fences (%d).\n", r);
		return r;
	}

	r = radeon_fence_driver_start_ring(rdev, R600_RING_TYPE_DMA_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing DMA fences (%d).\n", r);
		return r;
	}

	r = radeon_fence_driver_start_ring(rdev, CAYMAN_RING_TYPE_DMA1_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing DMA fences (%d).\n", r);
		return r;
	}

	if (rdev->has_uvd) {
		r = rv770_uvd_resume(rdev);
		if (!r) {
			r = radeon_fence_driver_start_ring(rdev,
							   R600_RING_TYPE_UVD_INDEX);
			if (r)
				dev_err(rdev->dev, "UVD fences init error (%d).\n", r);
		}
		if (r)
			rdev->ring[R600_RING_TYPE_UVD_INDEX].ring_size = 0;
	}

	/* Enable IRQ */
	if (!rdev->irq.installed) {
		r = radeon_irq_kms_init(rdev);
		if (r)
			return r;
	}

	r = si_irq_init(rdev);
	if (r) {
		DRM_ERROR("radeon: IH init failed (%d).\n", r);
		radeon_irq_kms_fini(rdev);
		return r;
	}
	si_irq_set(rdev);

	ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, RADEON_WB_CP_RPTR_OFFSET,
			     CP_RB0_RPTR, CP_RB0_WPTR,
			     0, 0xfffff, RADEON_CP_PACKET2);
	if (r)
		return r;

	ring = &rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, RADEON_WB_CP1_RPTR_OFFSET,
			     CP_RB1_RPTR, CP_RB1_WPTR,
			     0, 0xfffff, RADEON_CP_PACKET2);
	if (r)
		return r;

	ring = &rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, RADEON_WB_CP2_RPTR_OFFSET,
			     CP_RB2_RPTR, CP_RB2_WPTR,
			     0, 0xfffff, RADEON_CP_PACKET2);
	if (r)
		return r;

	ring = &rdev->ring[R600_RING_TYPE_DMA_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, R600_WB_DMA_RPTR_OFFSET,
			     DMA_RB_RPTR + DMA0_REGISTER_OFFSET,
			     DMA_RB_WPTR + DMA0_REGISTER_OFFSET,
			     2, 0x3fffc, DMA_PACKET(DMA_PACKET_NOP, 0, 0, 0, 0));
	if (r)
		return r;

	ring = &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, CAYMAN_WB_DMA1_RPTR_OFFSET,
			     DMA_RB_RPTR + DMA1_REGISTER_OFFSET,
			     DMA_RB_WPTR + DMA1_REGISTER_OFFSET,
			     2, 0x3fffc, DMA_PACKET(DMA_PACKET_NOP, 0, 0, 0, 0));
	if (r)
		return r;

	r = si_cp_load_microcode(rdev);
	if (r)
		return r;
	r = si_cp_resume(rdev);
	if (r)
		return r;

	r = cayman_dma_resume(rdev);
	if (r)
		return r;

	if (rdev->has_uvd) {
		ring = &rdev->ring[R600_RING_TYPE_UVD_INDEX];
		if (ring->ring_size) {
			r = radeon_ring_init(rdev, ring, ring->ring_size,
					     R600_WB_UVD_RPTR_OFFSET,
					     UVD_RBC_RB_RPTR, UVD_RBC_RB_WPTR,
					     0, 0xfffff, RADEON_CP_PACKET2);
			if (!r)
				r = r600_uvd_init(rdev);
			if (r)
				DRM_ERROR("radeon: failed initializing UVD (%d).\n", r);
		}
	}

	r = radeon_ib_pool_init(rdev);
	if (r) {
		dev_err(rdev->dev, "IB initialization failed (%d).\n", r);
		return r;
	}

	r = radeon_vm_manager_init(rdev);
	if (r) {
		dev_err(rdev->dev, "vm manager initialization failed (%d).\n", r);
		return r;
	}

	return 0;
}

int si_resume(struct radeon_device *rdev)
{
	int r;

	/* Do not reset GPU before posting, on rv770 hw unlike on r500 hw,
	 * posting will perform necessary task to bring back GPU into good
	 * shape.
	 */
	/* post card */
	atom_asic_init(rdev->mode_info.atom_context);

	/* init golden registers */
	si_init_golden_registers(rdev);

	rdev->accel_working = true;
	r = si_startup(rdev);
	if (r) {
		DRM_ERROR("si startup failed on resume\n");
		rdev->accel_working = false;
		return r;
	}

	return r;

}

int si_suspend(struct radeon_device *rdev)
{
	radeon_vm_manager_fini(rdev);
	si_cp_enable(rdev, false);
	cayman_dma_stop(rdev);
	if (rdev->has_uvd) {
		r600_uvd_rbc_stop(rdev);
		radeon_uvd_suspend(rdev);
	}
	si_irq_suspend(rdev);
	radeon_wb_disable(rdev);
	si_pcie_gart_disable(rdev);
	return 0;
}

/* Plan is to move initialization in that function and use
 * helper function so that radeon_device_init pretty much
 * do nothing more than calling asic specific function. This
 * should also allow to remove a bunch of callback function
 * like vram_info.
 */
int si_init(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	int r;

	/* Read BIOS */
	if (!radeon_get_bios(rdev)) {
		if (ASIC_IS_AVIVO(rdev))
			return -EINVAL;
	}
	/* Must be an ATOMBIOS */
	if (!rdev->is_atom_bios) {
		dev_err(rdev->dev, "Expecting atombios for cayman GPU\n");
		return -EINVAL;
	}
	r = radeon_atombios_init(rdev);
	if (r)
		return r;

	/* Post card if necessary */
	if (!radeon_card_posted(rdev)) {
		if (!rdev->bios) {
			dev_err(rdev->dev, "Card not posted and no BIOS - ignoring\n");
			return -EINVAL;
		}
		DRM_INFO("GPU not posted. posting now...\n");
		atom_asic_init(rdev->mode_info.atom_context);
	}
	/* init golden registers */
	si_init_golden_registers(rdev);
	/* Initialize scratch registers */
	si_scratch_init(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	/* Initialize clocks */
	radeon_get_clock_info(rdev->ddev);

	/* Fence driver */
	r = radeon_fence_driver_init(rdev);
	if (r)
		return r;

	/* initialize memory controller */
	r = si_mc_init(rdev);
	if (r)
		return r;
	/* Memory manager */
	r = radeon_bo_init(rdev);
	if (r)
		return r;

	ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 1024 * 1024);

	ring = &rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 1024 * 1024);

	ring = &rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 1024 * 1024);

	ring = &rdev->ring[R600_RING_TYPE_DMA_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 64 * 1024);

	ring = &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 64 * 1024);

	if (rdev->has_uvd) {
		r = radeon_uvd_init(rdev);
		if (!r) {
			ring = &rdev->ring[R600_RING_TYPE_UVD_INDEX];
			ring->ring_obj = NULL;
			r600_ring_init(rdev, ring, 4096);
		}
	}

	rdev->ih.ring_obj = NULL;
	r600_ih_ring_init(rdev, 64 * 1024);

	r = r600_pcie_gart_init(rdev);
	if (r)
		return r;

	rdev->accel_working = true;
	r = si_startup(rdev);
	if (r) {
		dev_err(rdev->dev, "disabling GPU acceleration\n");
		si_cp_fini(rdev);
		cayman_dma_fini(rdev);
		si_irq_fini(rdev);
		si_rlc_fini(rdev);
		radeon_wb_fini(rdev);
		radeon_ib_pool_fini(rdev);
		radeon_vm_manager_fini(rdev);
		radeon_irq_kms_fini(rdev);
		si_pcie_gart_fini(rdev);
		rdev->accel_working = false;
	}

	/* Don't start up if the MC ucode is missing.
	 * The default clocks and voltages before the MC ucode
	 * is loaded are not suffient for advanced operations.
	 */
	if (!rdev->mc_fw) {
		DRM_ERROR("radeon: MC ucode required for NI+.\n");
		return -EINVAL;
	}

	return 0;
}

void si_fini(struct radeon_device *rdev)
{
	si_cp_fini(rdev);
	cayman_dma_fini(rdev);
	si_irq_fini(rdev);
	si_rlc_fini(rdev);
	radeon_wb_fini(rdev);
	radeon_vm_manager_fini(rdev);
	radeon_ib_pool_fini(rdev);
	radeon_irq_kms_fini(rdev);
	if (rdev->has_uvd)
		radeon_uvd_fini(rdev);
	si_pcie_gart_fini(rdev);
	r600_vram_scratch_fini(rdev);
	radeon_gem_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_bo_fini(rdev);
	radeon_atombios_fini(rdev);
	kfree(rdev->bios);
	rdev->bios = NULL;
}

/**
 * si_get_gpu_clock_counter - return GPU clock counter snapshot
 *
 * @rdev: radeon_device pointer
 *
 * Fetches a GPU clock counter snapshot (SI).
 * Returns the 64 bit clock counter snapshot.
 */
uint64_t si_get_gpu_clock_counter(struct radeon_device *rdev)
{
	uint64_t clock;

	mutex_lock(&rdev->gpu_clock_mutex);
	WREG32(RLC_CAPTURE_GPU_CLOCK_COUNT, 1);
	clock = (uint64_t)RREG32(RLC_GPU_CLOCK_COUNT_LSB) |
	        ((uint64_t)RREG32(RLC_GPU_CLOCK_COUNT_MSB) << 32ULL);
	mutex_unlock(&rdev->gpu_clock_mutex);
	return clock;
}

int si_set_uvd_clocks(struct radeon_device *rdev, u32 vclk, u32 dclk)
{
	unsigned fb_div = 0, vclk_div = 0, dclk_div = 0;
	int r;

	/* bypass vclk and dclk with bclk */
	WREG32_P(CG_UPLL_FUNC_CNTL_2,
		VCLK_SRC_SEL(1) | DCLK_SRC_SEL(1),
		~(VCLK_SRC_SEL_MASK | DCLK_SRC_SEL_MASK));

	/* put PLL in bypass mode */
	WREG32_P(CG_UPLL_FUNC_CNTL, UPLL_BYPASS_EN_MASK, ~UPLL_BYPASS_EN_MASK);

	if (!vclk || !dclk) {
		/* keep the Bypass mode, put PLL to sleep */
		WREG32_P(CG_UPLL_FUNC_CNTL, UPLL_SLEEP_MASK, ~UPLL_SLEEP_MASK);
		return 0;
	}

	r = radeon_uvd_calc_upll_dividers(rdev, vclk, dclk, 125000, 250000,
					  16384, 0x03FFFFFF, 0, 128, 5,
					  &fb_div, &vclk_div, &dclk_div);
	if (r)
		return r;

	/* set RESET_ANTI_MUX to 0 */
	WREG32_P(CG_UPLL_FUNC_CNTL_5, 0, ~RESET_ANTI_MUX_MASK);

	/* set VCO_MODE to 1 */
	WREG32_P(CG_UPLL_FUNC_CNTL, UPLL_VCO_MODE_MASK, ~UPLL_VCO_MODE_MASK);

	/* toggle UPLL_SLEEP to 1 then back to 0 */
	WREG32_P(CG_UPLL_FUNC_CNTL, UPLL_SLEEP_MASK, ~UPLL_SLEEP_MASK);
	WREG32_P(CG_UPLL_FUNC_CNTL, 0, ~UPLL_SLEEP_MASK);

	/* deassert UPLL_RESET */
	WREG32_P(CG_UPLL_FUNC_CNTL, 0, ~UPLL_RESET_MASK);

	mdelay(1);

	r = radeon_uvd_send_upll_ctlreq(rdev, CG_UPLL_FUNC_CNTL);
	if (r)
		return r;

	/* assert UPLL_RESET again */
	WREG32_P(CG_UPLL_FUNC_CNTL, UPLL_RESET_MASK, ~UPLL_RESET_MASK);

	/* disable spread spectrum. */
	WREG32_P(CG_UPLL_SPREAD_SPECTRUM, 0, ~SSEN_MASK);

	/* set feedback divider */
	WREG32_P(CG_UPLL_FUNC_CNTL_3, UPLL_FB_DIV(fb_div), ~UPLL_FB_DIV_MASK);

	/* set ref divider to 0 */
	WREG32_P(CG_UPLL_FUNC_CNTL, 0, ~UPLL_REF_DIV_MASK);

	if (fb_div < 307200)
		WREG32_P(CG_UPLL_FUNC_CNTL_4, 0, ~UPLL_SPARE_ISPARE9);
	else
		WREG32_P(CG_UPLL_FUNC_CNTL_4, UPLL_SPARE_ISPARE9, ~UPLL_SPARE_ISPARE9);

	/* set PDIV_A and PDIV_B */
	WREG32_P(CG_UPLL_FUNC_CNTL_2,
		UPLL_PDIV_A(vclk_div) | UPLL_PDIV_B(dclk_div),
		~(UPLL_PDIV_A_MASK | UPLL_PDIV_B_MASK));

	/* give the PLL some time to settle */
	mdelay(15);

	/* deassert PLL_RESET */
	WREG32_P(CG_UPLL_FUNC_CNTL, 0, ~UPLL_RESET_MASK);

	mdelay(15);

	/* switch from bypass mode to normal mode */
	WREG32_P(CG_UPLL_FUNC_CNTL, 0, ~UPLL_BYPASS_EN_MASK);

	r = radeon_uvd_send_upll_ctlreq(rdev, CG_UPLL_FUNC_CNTL);
	if (r)
		return r;

	/* switch VCLK and DCLK selection */
	WREG32_P(CG_UPLL_FUNC_CNTL_2,
		VCLK_SRC_SEL(2) | DCLK_SRC_SEL(2),
		~(VCLK_SRC_SEL_MASK | DCLK_SRC_SEL_MASK));

	mdelay(100);

	return 0;
}
