/*
 * Copyright 2010 Advanced Micro Devices, Inc.
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
#include <linux/slab.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include "radeon.h"
#include "radeon_asic.h"
#include "radeon_audio.h"
#include <drm/radeon_drm.h>
#include "nid.h"
#include "atom.h"
#include "ni_reg.h"
#include "cayman_blit_shaders.h"
#include "radeon_ucode.h"
#include "clearstate_cayman.h"

/*
 * Indirect registers accessor
 */
u32 tn_smc_rreg(struct radeon_device *rdev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&rdev->smc_idx_lock, flags);
	WREG32(TN_SMC_IND_INDEX_0, (reg));
	r = RREG32(TN_SMC_IND_DATA_0);
	spin_unlock_irqrestore(&rdev->smc_idx_lock, flags);
	return r;
}

void tn_smc_wreg(struct radeon_device *rdev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&rdev->smc_idx_lock, flags);
	WREG32(TN_SMC_IND_INDEX_0, (reg));
	WREG32(TN_SMC_IND_DATA_0, (v));
	spin_unlock_irqrestore(&rdev->smc_idx_lock, flags);
}

static const u32 tn_rlc_save_restore_register_list[] =
{
	0x98fc,
	0x98f0,
	0x9834,
	0x9838,
	0x9870,
	0x9874,
	0x8a14,
	0x8b24,
	0x8bcc,
	0x8b10,
	0x8c30,
	0x8d00,
	0x8d04,
	0x8c00,
	0x8c04,
	0x8c10,
	0x8c14,
	0x8d8c,
	0x8cf0,
	0x8e38,
	0x9508,
	0x9688,
	0x9608,
	0x960c,
	0x9610,
	0x9614,
	0x88c4,
	0x8978,
	0x88d4,
	0x900c,
	0x9100,
	0x913c,
	0x90e8,
	0x9354,
	0xa008,
	0x98f8,
	0x9148,
	0x914c,
	0x3f94,
	0x98f4,
	0x9b7c,
	0x3f8c,
	0x8950,
	0x8954,
	0x8a18,
	0x8b28,
	0x9144,
	0x3f90,
	0x915c,
	0x9160,
	0x9178,
	0x917c,
	0x9180,
	0x918c,
	0x9190,
	0x9194,
	0x9198,
	0x919c,
	0x91a8,
	0x91ac,
	0x91b0,
	0x91b4,
	0x91b8,
	0x91c4,
	0x91c8,
	0x91cc,
	0x91d0,
	0x91d4,
	0x91e0,
	0x91e4,
	0x91ec,
	0x91f0,
	0x91f4,
	0x9200,
	0x9204,
	0x929c,
	0x8030,
	0x9150,
	0x9a60,
	0x920c,
	0x9210,
	0x9228,
	0x922c,
	0x9244,
	0x9248,
	0x91e8,
	0x9294,
	0x9208,
	0x9224,
	0x9240,
	0x9220,
	0x923c,
	0x9258,
	0x9744,
	0xa200,
	0xa204,
	0xa208,
	0xa20c,
	0x8d58,
	0x9030,
	0x9034,
	0x9038,
	0x903c,
	0x9040,
	0x9654,
	0x897c,
	0xa210,
	0xa214,
	0x9868,
	0xa02c,
	0x9664,
	0x9698,
	0x949c,
	0x8e10,
	0x8e18,
	0x8c50,
	0x8c58,
	0x8c60,
	0x8c68,
	0x89b4,
	0x9830,
	0x802c,
};

extern bool evergreen_is_display_hung(struct radeon_device *rdev);
extern void evergreen_print_gpu_status_regs(struct radeon_device *rdev);
extern void evergreen_mc_stop(struct radeon_device *rdev, struct evergreen_mc_save *save);
extern void evergreen_mc_resume(struct radeon_device *rdev, struct evergreen_mc_save *save);
extern int evergreen_mc_wait_for_idle(struct radeon_device *rdev);
extern void evergreen_mc_program(struct radeon_device *rdev);
extern void evergreen_irq_suspend(struct radeon_device *rdev);
extern int evergreen_mc_init(struct radeon_device *rdev);
extern void evergreen_fix_pci_max_read_req_size(struct radeon_device *rdev);
extern void evergreen_pcie_gen2_enable(struct radeon_device *rdev);
extern void evergreen_program_aspm(struct radeon_device *rdev);
extern void sumo_rlc_fini(struct radeon_device *rdev);
extern int sumo_rlc_init(struct radeon_device *rdev);
extern void evergreen_gpu_pci_config_reset(struct radeon_device *rdev);

/* Firmware Names */
MODULE_FIRMWARE("radeon/BARTS_pfp.bin");
MODULE_FIRMWARE("radeon/BARTS_me.bin");
MODULE_FIRMWARE("radeon/BARTS_mc.bin");
MODULE_FIRMWARE("radeon/BARTS_smc.bin");
MODULE_FIRMWARE("radeon/BTC_rlc.bin");
MODULE_FIRMWARE("radeon/TURKS_pfp.bin");
MODULE_FIRMWARE("radeon/TURKS_me.bin");
MODULE_FIRMWARE("radeon/TURKS_mc.bin");
MODULE_FIRMWARE("radeon/TURKS_smc.bin");
MODULE_FIRMWARE("radeon/CAICOS_pfp.bin");
MODULE_FIRMWARE("radeon/CAICOS_me.bin");
MODULE_FIRMWARE("radeon/CAICOS_mc.bin");
MODULE_FIRMWARE("radeon/CAICOS_smc.bin");
MODULE_FIRMWARE("radeon/CAYMAN_pfp.bin");
MODULE_FIRMWARE("radeon/CAYMAN_me.bin");
MODULE_FIRMWARE("radeon/CAYMAN_mc.bin");
MODULE_FIRMWARE("radeon/CAYMAN_rlc.bin");
MODULE_FIRMWARE("radeon/CAYMAN_smc.bin");
MODULE_FIRMWARE("radeon/ARUBA_pfp.bin");
MODULE_FIRMWARE("radeon/ARUBA_me.bin");
MODULE_FIRMWARE("radeon/ARUBA_rlc.bin");


static const u32 cayman_golden_registers2[] =
{
	0x3e5c, 0xffffffff, 0x00000000,
	0x3e48, 0xffffffff, 0x00000000,
	0x3e4c, 0xffffffff, 0x00000000,
	0x3e64, 0xffffffff, 0x00000000,
	0x3e50, 0xffffffff, 0x00000000,
	0x3e60, 0xffffffff, 0x00000000
};

static const u32 cayman_golden_registers[] =
{
	0x5eb4, 0xffffffff, 0x00000002,
	0x5e78, 0x8f311ff1, 0x001000f0,
	0x3f90, 0xffff0000, 0xff000000,
	0x9148, 0xffff0000, 0xff000000,
	0x3f94, 0xffff0000, 0xff000000,
	0x914c, 0xffff0000, 0xff000000,
	0xc78, 0x00000080, 0x00000080,
	0xbd4, 0x70073777, 0x00011003,
	0xd02c, 0xbfffff1f, 0x08421000,
	0xd0b8, 0x73773777, 0x02011003,
	0x5bc0, 0x00200000, 0x50100000,
	0x98f8, 0x33773777, 0x02011003,
	0x98fc, 0xffffffff, 0x76541032,
	0x7030, 0x31000311, 0x00000011,
	0x2f48, 0x33773777, 0x42010001,
	0x6b28, 0x00000010, 0x00000012,
	0x7728, 0x00000010, 0x00000012,
	0x10328, 0x00000010, 0x00000012,
	0x10f28, 0x00000010, 0x00000012,
	0x11b28, 0x00000010, 0x00000012,
	0x12728, 0x00000010, 0x00000012,
	0x240c, 0x000007ff, 0x00000000,
	0x8a14, 0xf000001f, 0x00000007,
	0x8b24, 0x3fff3fff, 0x00ff0fff,
	0x8b10, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x06000000,
	0x10c, 0x00000001, 0x00010003,
	0xa02c, 0xffffffff, 0x0000009b,
	0x913c, 0x0000010f, 0x01000100,
	0x8c04, 0xf8ff00ff, 0x40600060,
	0x28350, 0x00000f01, 0x00000000,
	0x9508, 0x3700001f, 0x00000002,
	0x960c, 0xffffffff, 0x54763210,
	0x88c4, 0x001f3ae3, 0x00000082,
	0x88d0, 0xffffffff, 0x0f40df40,
	0x88d4, 0x0000001f, 0x00000010,
	0x8974, 0xffffffff, 0x00000000
};

static const u32 dvst_golden_registers2[] =
{
	0x8f8, 0xffffffff, 0,
	0x8fc, 0x00380000, 0,
	0x8f8, 0xffffffff, 1,
	0x8fc, 0x0e000000, 0
};

static const u32 dvst_golden_registers[] =
{
	0x690, 0x3fff3fff, 0x20c00033,
	0x918c, 0x0fff0fff, 0x00010006,
	0x91a8, 0x0fff0fff, 0x00010006,
	0x9150, 0xffffdfff, 0x6e944040,
	0x917c, 0x0fff0fff, 0x00030002,
	0x9198, 0x0fff0fff, 0x00030002,
	0x915c, 0x0fff0fff, 0x00010000,
	0x3f90, 0xffff0001, 0xff000000,
	0x9178, 0x0fff0fff, 0x00070000,
	0x9194, 0x0fff0fff, 0x00070000,
	0x9148, 0xffff0001, 0xff000000,
	0x9190, 0x0fff0fff, 0x00090008,
	0x91ac, 0x0fff0fff, 0x00090008,
	0x3f94, 0xffff0000, 0xff000000,
	0x914c, 0xffff0000, 0xff000000,
	0x929c, 0x00000fff, 0x00000001,
	0x55e4, 0xff607fff, 0xfc000100,
	0x8a18, 0xff000fff, 0x00000100,
	0x8b28, 0xff000fff, 0x00000100,
	0x9144, 0xfffc0fff, 0x00000100,
	0x6ed8, 0x00010101, 0x00010000,
	0x9830, 0xffffffff, 0x00000000,
	0x9834, 0xf00fffff, 0x00000400,
	0x9838, 0xfffffffe, 0x00000000,
	0xd0c0, 0xff000fff, 0x00000100,
	0xd02c, 0xbfffff1f, 0x08421000,
	0xd0b8, 0x73773777, 0x12010001,
	0x5bb0, 0x000000f0, 0x00000070,
	0x98f8, 0x73773777, 0x12010001,
	0x98fc, 0xffffffff, 0x00000010,
	0x9b7c, 0x00ff0000, 0x00fc0000,
	0x8030, 0x00001f0f, 0x0000100a,
	0x2f48, 0x73773777, 0x12010001,
	0x2408, 0x00030000, 0x000c007f,
	0x8a14, 0xf000003f, 0x00000007,
	0x8b24, 0x3fff3fff, 0x00ff0fff,
	0x8b10, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x06000000,
	0x4d8, 0x00000fff, 0x00000100,
	0xa008, 0xffffffff, 0x00010000,
	0x913c, 0xffff03ff, 0x01000100,
	0x8c00, 0x000000ff, 0x00000003,
	0x8c04, 0xf8ff00ff, 0x40600060,
	0x8cf0, 0x1fff1fff, 0x08e00410,
	0x28350, 0x00000f01, 0x00000000,
	0x9508, 0xf700071f, 0x00000002,
	0x960c, 0xffffffff, 0x54763210,
	0x20ef8, 0x01ff01ff, 0x00000002,
	0x20e98, 0xfffffbff, 0x00200000,
	0x2015c, 0xffffffff, 0x00000f40,
	0x88c4, 0x001f3ae3, 0x00000082,
	0x8978, 0x3fffffff, 0x04050140,
	0x88d4, 0x0000001f, 0x00000010,
	0x8974, 0xffffffff, 0x00000000
};

static const u32 scrapper_golden_registers[] =
{
	0x690, 0x3fff3fff, 0x20c00033,
	0x918c, 0x0fff0fff, 0x00010006,
	0x918c, 0x0fff0fff, 0x00010006,
	0x91a8, 0x0fff0fff, 0x00010006,
	0x91a8, 0x0fff0fff, 0x00010006,
	0x9150, 0xffffdfff, 0x6e944040,
	0x9150, 0xffffdfff, 0x6e944040,
	0x917c, 0x0fff0fff, 0x00030002,
	0x917c, 0x0fff0fff, 0x00030002,
	0x9198, 0x0fff0fff, 0x00030002,
	0x9198, 0x0fff0fff, 0x00030002,
	0x915c, 0x0fff0fff, 0x00010000,
	0x915c, 0x0fff0fff, 0x00010000,
	0x3f90, 0xffff0001, 0xff000000,
	0x3f90, 0xffff0001, 0xff000000,
	0x9178, 0x0fff0fff, 0x00070000,
	0x9178, 0x0fff0fff, 0x00070000,
	0x9194, 0x0fff0fff, 0x00070000,
	0x9194, 0x0fff0fff, 0x00070000,
	0x9148, 0xffff0001, 0xff000000,
	0x9148, 0xffff0001, 0xff000000,
	0x9190, 0x0fff0fff, 0x00090008,
	0x9190, 0x0fff0fff, 0x00090008,
	0x91ac, 0x0fff0fff, 0x00090008,
	0x91ac, 0x0fff0fff, 0x00090008,
	0x3f94, 0xffff0000, 0xff000000,
	0x3f94, 0xffff0000, 0xff000000,
	0x914c, 0xffff0000, 0xff000000,
	0x914c, 0xffff0000, 0xff000000,
	0x929c, 0x00000fff, 0x00000001,
	0x929c, 0x00000fff, 0x00000001,
	0x55e4, 0xff607fff, 0xfc000100,
	0x8a18, 0xff000fff, 0x00000100,
	0x8a18, 0xff000fff, 0x00000100,
	0x8b28, 0xff000fff, 0x00000100,
	0x8b28, 0xff000fff, 0x00000100,
	0x9144, 0xfffc0fff, 0x00000100,
	0x9144, 0xfffc0fff, 0x00000100,
	0x6ed8, 0x00010101, 0x00010000,
	0x9830, 0xffffffff, 0x00000000,
	0x9830, 0xffffffff, 0x00000000,
	0x9834, 0xf00fffff, 0x00000400,
	0x9834, 0xf00fffff, 0x00000400,
	0x9838, 0xfffffffe, 0x00000000,
	0x9838, 0xfffffffe, 0x00000000,
	0xd0c0, 0xff000fff, 0x00000100,
	0xd02c, 0xbfffff1f, 0x08421000,
	0xd02c, 0xbfffff1f, 0x08421000,
	0xd0b8, 0x73773777, 0x12010001,
	0xd0b8, 0x73773777, 0x12010001,
	0x5bb0, 0x000000f0, 0x00000070,
	0x98f8, 0x73773777, 0x12010001,
	0x98f8, 0x73773777, 0x12010001,
	0x98fc, 0xffffffff, 0x00000010,
	0x98fc, 0xffffffff, 0x00000010,
	0x9b7c, 0x00ff0000, 0x00fc0000,
	0x9b7c, 0x00ff0000, 0x00fc0000,
	0x8030, 0x00001f0f, 0x0000100a,
	0x8030, 0x00001f0f, 0x0000100a,
	0x2f48, 0x73773777, 0x12010001,
	0x2f48, 0x73773777, 0x12010001,
	0x2408, 0x00030000, 0x000c007f,
	0x8a14, 0xf000003f, 0x00000007,
	0x8a14, 0xf000003f, 0x00000007,
	0x8b24, 0x3fff3fff, 0x00ff0fff,
	0x8b24, 0x3fff3fff, 0x00ff0fff,
	0x8b10, 0x0000ff0f, 0x00000000,
	0x8b10, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x06000000,
	0x28a4c, 0x07ffffff, 0x06000000,
	0x4d8, 0x00000fff, 0x00000100,
	0x4d8, 0x00000fff, 0x00000100,
	0xa008, 0xffffffff, 0x00010000,
	0xa008, 0xffffffff, 0x00010000,
	0x913c, 0xffff03ff, 0x01000100,
	0x913c, 0xffff03ff, 0x01000100,
	0x90e8, 0x001fffff, 0x010400c0,
	0x8c00, 0x000000ff, 0x00000003,
	0x8c00, 0x000000ff, 0x00000003,
	0x8c04, 0xf8ff00ff, 0x40600060,
	0x8c04, 0xf8ff00ff, 0x40600060,
	0x8c30, 0x0000000f, 0x00040005,
	0x8cf0, 0x1fff1fff, 0x08e00410,
	0x8cf0, 0x1fff1fff, 0x08e00410,
	0x900c, 0x00ffffff, 0x0017071f,
	0x28350, 0x00000f01, 0x00000000,
	0x28350, 0x00000f01, 0x00000000,
	0x9508, 0xf700071f, 0x00000002,
	0x9508, 0xf700071f, 0x00000002,
	0x9688, 0x00300000, 0x0017000f,
	0x960c, 0xffffffff, 0x54763210,
	0x960c, 0xffffffff, 0x54763210,
	0x20ef8, 0x01ff01ff, 0x00000002,
	0x20e98, 0xfffffbff, 0x00200000,
	0x2015c, 0xffffffff, 0x00000f40,
	0x88c4, 0x001f3ae3, 0x00000082,
	0x88c4, 0x001f3ae3, 0x00000082,
	0x8978, 0x3fffffff, 0x04050140,
	0x8978, 0x3fffffff, 0x04050140,
	0x88d4, 0x0000001f, 0x00000010,
	0x88d4, 0x0000001f, 0x00000010,
	0x8974, 0xffffffff, 0x00000000,
	0x8974, 0xffffffff, 0x00000000
};

static void ni_init_golden_registers(struct radeon_device *rdev)
{
	switch (rdev->family) {
	case CHIP_CAYMAN:
		radeon_program_register_sequence(rdev,
						 cayman_golden_registers,
						 (const u32)ARRAY_SIZE(cayman_golden_registers));
		radeon_program_register_sequence(rdev,
						 cayman_golden_registers2,
						 (const u32)ARRAY_SIZE(cayman_golden_registers2));
		break;
	case CHIP_ARUBA:
		if ((rdev->pdev->device == 0x9900) ||
		    (rdev->pdev->device == 0x9901) ||
		    (rdev->pdev->device == 0x9903) ||
		    (rdev->pdev->device == 0x9904) ||
		    (rdev->pdev->device == 0x9905) ||
		    (rdev->pdev->device == 0x9906) ||
		    (rdev->pdev->device == 0x9907) ||
		    (rdev->pdev->device == 0x9908) ||
		    (rdev->pdev->device == 0x9909) ||
		    (rdev->pdev->device == 0x990A) ||
		    (rdev->pdev->device == 0x990B) ||
		    (rdev->pdev->device == 0x990C) ||
		    (rdev->pdev->device == 0x990D) ||
		    (rdev->pdev->device == 0x990E) ||
		    (rdev->pdev->device == 0x990F) ||
		    (rdev->pdev->device == 0x9910) ||
		    (rdev->pdev->device == 0x9913) ||
		    (rdev->pdev->device == 0x9917) ||
		    (rdev->pdev->device == 0x9918)) {
			radeon_program_register_sequence(rdev,
							 dvst_golden_registers,
							 (const u32)ARRAY_SIZE(dvst_golden_registers));
			radeon_program_register_sequence(rdev,
							 dvst_golden_registers2,
							 (const u32)ARRAY_SIZE(dvst_golden_registers2));
		} else {
			radeon_program_register_sequence(rdev,
							 scrapper_golden_registers,
							 (const u32)ARRAY_SIZE(scrapper_golden_registers));
			radeon_program_register_sequence(rdev,
							 dvst_golden_registers2,
							 (const u32)ARRAY_SIZE(dvst_golden_registers2));
		}
		break;
	default:
		break;
	}
}

#define BTC_IO_MC_REGS_SIZE 29

static const u32 barts_io_mc_regs[BTC_IO_MC_REGS_SIZE][2] = {
	{0x00000077, 0xff010100},
	{0x00000078, 0x00000000},
	{0x00000079, 0x00001434},
	{0x0000007a, 0xcc08ec08},
	{0x0000007b, 0x00040000},
	{0x0000007c, 0x000080c0},
	{0x0000007d, 0x09000000},
	{0x0000007e, 0x00210404},
	{0x00000081, 0x08a8e800},
	{0x00000082, 0x00030444},
	{0x00000083, 0x00000000},
	{0x00000085, 0x00000001},
	{0x00000086, 0x00000002},
	{0x00000087, 0x48490000},
	{0x00000088, 0x20244647},
	{0x00000089, 0x00000005},
	{0x0000008b, 0x66030000},
	{0x0000008c, 0x00006603},
	{0x0000008d, 0x00000100},
	{0x0000008f, 0x00001c0a},
	{0x00000090, 0xff000001},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00946a00}
};

static const u32 turks_io_mc_regs[BTC_IO_MC_REGS_SIZE][2] = {
	{0x00000077, 0xff010100},
	{0x00000078, 0x00000000},
	{0x00000079, 0x00001434},
	{0x0000007a, 0xcc08ec08},
	{0x0000007b, 0x00040000},
	{0x0000007c, 0x000080c0},
	{0x0000007d, 0x09000000},
	{0x0000007e, 0x00210404},
	{0x00000081, 0x08a8e800},
	{0x00000082, 0x00030444},
	{0x00000083, 0x00000000},
	{0x00000085, 0x00000001},
	{0x00000086, 0x00000002},
	{0x00000087, 0x48490000},
	{0x00000088, 0x20244647},
	{0x00000089, 0x00000005},
	{0x0000008b, 0x66030000},
	{0x0000008c, 0x00006603},
	{0x0000008d, 0x00000100},
	{0x0000008f, 0x00001c0a},
	{0x00000090, 0xff000001},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00936a00}
};

static const u32 caicos_io_mc_regs[BTC_IO_MC_REGS_SIZE][2] = {
	{0x00000077, 0xff010100},
	{0x00000078, 0x00000000},
	{0x00000079, 0x00001434},
	{0x0000007a, 0xcc08ec08},
	{0x0000007b, 0x00040000},
	{0x0000007c, 0x000080c0},
	{0x0000007d, 0x09000000},
	{0x0000007e, 0x00210404},
	{0x00000081, 0x08a8e800},
	{0x00000082, 0x00030444},
	{0x00000083, 0x00000000},
	{0x00000085, 0x00000001},
	{0x00000086, 0x00000002},
	{0x00000087, 0x48490000},
	{0x00000088, 0x20244647},
	{0x00000089, 0x00000005},
	{0x0000008b, 0x66030000},
	{0x0000008c, 0x00006603},
	{0x0000008d, 0x00000100},
	{0x0000008f, 0x00001c0a},
	{0x00000090, 0xff000001},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00916a00}
};

static const u32 cayman_io_mc_regs[BTC_IO_MC_REGS_SIZE][2] = {
	{0x00000077, 0xff010100},
	{0x00000078, 0x00000000},
	{0x00000079, 0x00001434},
	{0x0000007a, 0xcc08ec08},
	{0x0000007b, 0x00040000},
	{0x0000007c, 0x000080c0},
	{0x0000007d, 0x09000000},
	{0x0000007e, 0x00210404},
	{0x00000081, 0x08a8e800},
	{0x00000082, 0x00030444},
	{0x00000083, 0x00000000},
	{0x00000085, 0x00000001},
	{0x00000086, 0x00000002},
	{0x00000087, 0x48490000},
	{0x00000088, 0x20244647},
	{0x00000089, 0x00000005},
	{0x0000008b, 0x66030000},
	{0x0000008c, 0x00006603},
	{0x0000008d, 0x00000100},
	{0x0000008f, 0x00001c0a},
	{0x00000090, 0xff000001},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00976b00}
};

int ni_mc_load_microcode(struct radeon_device *rdev)
{
	const __be32 *fw_data;
	u32 mem_type, running, blackout = 0;
	u32 *io_mc_regs;
	int i, ucode_size, regs_size;

	if (!rdev->mc_fw)
		return -EINVAL;

	switch (rdev->family) {
	case CHIP_BARTS:
		io_mc_regs = (u32 *)&barts_io_mc_regs;
		ucode_size = BTC_MC_UCODE_SIZE;
		regs_size = BTC_IO_MC_REGS_SIZE;
		break;
	case CHIP_TURKS:
		io_mc_regs = (u32 *)&turks_io_mc_regs;
		ucode_size = BTC_MC_UCODE_SIZE;
		regs_size = BTC_IO_MC_REGS_SIZE;
		break;
	case CHIP_CAICOS:
	default:
		io_mc_regs = (u32 *)&caicos_io_mc_regs;
		ucode_size = BTC_MC_UCODE_SIZE;
		regs_size = BTC_IO_MC_REGS_SIZE;
		break;
	case CHIP_CAYMAN:
		io_mc_regs = (u32 *)&cayman_io_mc_regs;
		ucode_size = CAYMAN_MC_UCODE_SIZE;
		regs_size = BTC_IO_MC_REGS_SIZE;
		break;
	}

	mem_type = (RREG32(MC_SEQ_MISC0) & MC_SEQ_MISC0_GDDR5_MASK) >> MC_SEQ_MISC0_GDDR5_SHIFT;
	running = RREG32(MC_SEQ_SUP_CNTL) & RUN_MASK;

	if ((mem_type == MC_SEQ_MISC0_GDDR5_VALUE) && (running == 0)) {
		if (running) {
			blackout = RREG32(MC_SHARED_BLACKOUT_CNTL);
			WREG32(MC_SHARED_BLACKOUT_CNTL, 1);
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
			if (RREG32(MC_IO_PAD_CNTL_D0) & MEM_FALL_OUT_CMD)
				break;
			udelay(1);
		}

		if (running)
			WREG32(MC_SHARED_BLACKOUT_CNTL, blackout);
	}

	return 0;
}

int ni_init_microcode(struct radeon_device *rdev)
{
	const char *chip_name;
	const char *rlc_chip_name;
	size_t pfp_req_size, me_req_size, rlc_req_size, mc_req_size;
	size_t smc_req_size = 0;
	char fw_name[30];
	int err;

	DRM_DEBUG("\n");

	switch (rdev->family) {
	case CHIP_BARTS:
		chip_name = "BARTS";
		rlc_chip_name = "BTC";
		pfp_req_size = EVERGREEN_PFP_UCODE_SIZE * 4;
		me_req_size = EVERGREEN_PM4_UCODE_SIZE * 4;
		rlc_req_size = EVERGREEN_RLC_UCODE_SIZE * 4;
		mc_req_size = BTC_MC_UCODE_SIZE * 4;
		smc_req_size = ALIGN(BARTS_SMC_UCODE_SIZE, 4);
		break;
	case CHIP_TURKS:
		chip_name = "TURKS";
		rlc_chip_name = "BTC";
		pfp_req_size = EVERGREEN_PFP_UCODE_SIZE * 4;
		me_req_size = EVERGREEN_PM4_UCODE_SIZE * 4;
		rlc_req_size = EVERGREEN_RLC_UCODE_SIZE * 4;
		mc_req_size = BTC_MC_UCODE_SIZE * 4;
		smc_req_size = ALIGN(TURKS_SMC_UCODE_SIZE, 4);
		break;
	case CHIP_CAICOS:
		chip_name = "CAICOS";
		rlc_chip_name = "BTC";
		pfp_req_size = EVERGREEN_PFP_UCODE_SIZE * 4;
		me_req_size = EVERGREEN_PM4_UCODE_SIZE * 4;
		rlc_req_size = EVERGREEN_RLC_UCODE_SIZE * 4;
		mc_req_size = BTC_MC_UCODE_SIZE * 4;
		smc_req_size = ALIGN(CAICOS_SMC_UCODE_SIZE, 4);
		break;
	case CHIP_CAYMAN:
		chip_name = "CAYMAN";
		rlc_chip_name = "CAYMAN";
		pfp_req_size = CAYMAN_PFP_UCODE_SIZE * 4;
		me_req_size = CAYMAN_PM4_UCODE_SIZE * 4;
		rlc_req_size = CAYMAN_RLC_UCODE_SIZE * 4;
		mc_req_size = CAYMAN_MC_UCODE_SIZE * 4;
		smc_req_size = ALIGN(CAYMAN_SMC_UCODE_SIZE, 4);
		break;
	case CHIP_ARUBA:
		chip_name = "ARUBA";
		rlc_chip_name = "ARUBA";
		/* pfp/me same size as CAYMAN */
		pfp_req_size = CAYMAN_PFP_UCODE_SIZE * 4;
		me_req_size = CAYMAN_PM4_UCODE_SIZE * 4;
		rlc_req_size = ARUBA_RLC_UCODE_SIZE * 4;
		mc_req_size = 0;
		break;
	default: BUG();
	}

	DRM_INFO("Loading %s Microcode\n", chip_name);

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_pfp.bin", chip_name);
	err = request_firmware(&rdev->pfp_fw, fw_name, rdev->dev);
	if (err)
		goto out;
	if (rdev->pfp_fw->size != pfp_req_size) {
		printk(KERN_ERR
		       "ni_cp: Bogus length %zu in firmware \"%s\"\n",
		       rdev->pfp_fw->size, fw_name);
		err = -EINVAL;
		goto out;
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_me.bin", chip_name);
	err = request_firmware(&rdev->me_fw, fw_name, rdev->dev);
	if (err)
		goto out;
	if (rdev->me_fw->size != me_req_size) {
		printk(KERN_ERR
		       "ni_cp: Bogus length %zu in firmware \"%s\"\n",
		       rdev->me_fw->size, fw_name);
		err = -EINVAL;
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_rlc.bin", rlc_chip_name);
	err = request_firmware(&rdev->rlc_fw, fw_name, rdev->dev);
	if (err)
		goto out;
	if (rdev->rlc_fw->size != rlc_req_size) {
		printk(KERN_ERR
		       "ni_rlc: Bogus length %zu in firmware \"%s\"\n",
		       rdev->rlc_fw->size, fw_name);
		err = -EINVAL;
	}

	/* no MC ucode on TN */
	if (!(rdev->flags & RADEON_IS_IGP)) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_mc.bin", chip_name);
		err = request_firmware(&rdev->mc_fw, fw_name, rdev->dev);
		if (err)
			goto out;
		if (rdev->mc_fw->size != mc_req_size) {
			printk(KERN_ERR
			       "ni_mc: Bogus length %zu in firmware \"%s\"\n",
			       rdev->mc_fw->size, fw_name);
			err = -EINVAL;
		}
	}

	if ((rdev->family >= CHIP_BARTS) && (rdev->family <= CHIP_CAYMAN)) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_smc.bin", chip_name);
		err = request_firmware(&rdev->smc_fw, fw_name, rdev->dev);
		if (err) {
			printk(KERN_ERR
			       "smc: error loading firmware \"%s\"\n",
			       fw_name);
			release_firmware(rdev->smc_fw);
			rdev->smc_fw = NULL;
			err = 0;
		} else if (rdev->smc_fw->size != smc_req_size) {
			printk(KERN_ERR
			       "ni_mc: Bogus length %zu in firmware \"%s\"\n",
			       rdev->mc_fw->size, fw_name);
			err = -EINVAL;
		}
	}

out:
	if (err) {
		if (err != -EINVAL)
			printk(KERN_ERR
			       "ni_cp: Failed to load firmware \"%s\"\n",
			       fw_name);
		release_firmware(rdev->pfp_fw);
		rdev->pfp_fw = NULL;
		release_firmware(rdev->me_fw);
		rdev->me_fw = NULL;
		release_firmware(rdev->rlc_fw);
		rdev->rlc_fw = NULL;
		release_firmware(rdev->mc_fw);
		rdev->mc_fw = NULL;
	}
	return err;
}

/**
 * cayman_get_allowed_info_register - fetch the register for the info ioctl
 *
 * @rdev: radeon_device pointer
 * @reg: register offset in bytes
 * @val: register value
 *
 * Returns 0 for success or -EINVAL for an invalid register
 *
 */
int cayman_get_allowed_info_register(struct radeon_device *rdev,
				     u32 reg, u32 *val)
{
	switch (reg) {
	case GRBM_STATUS:
	case GRBM_STATUS_SE0:
	case GRBM_STATUS_SE1:
	case SRBM_STATUS:
	case SRBM_STATUS2:
	case (DMA_STATUS_REG + DMA0_REGISTER_OFFSET):
	case (DMA_STATUS_REG + DMA1_REGISTER_OFFSET):
	case UVD_STATUS:
		*val = RREG32(reg);
		return 0;
	default:
		return -EINVAL;
	}
}

int tn_get_temp(struct radeon_device *rdev)
{
	u32 temp = RREG32_SMC(TN_CURRENT_GNB_TEMP) & 0x7ff;
	int actual_temp = (temp / 8) - 49;

	return actual_temp * 1000;
}

/*
 * Core functions
 */
static void cayman_gpu_init(struct radeon_device *rdev)
{
	u32 gb_addr_config = 0;
	u32 mc_shared_chmap, mc_arb_ramcfg;
	u32 cgts_tcc_disable;
	u32 sx_debug_1;
	u32 smx_dc_ctl0;
	u32 cgts_sm_ctrl_reg;
	u32 hdp_host_path_cntl;
	u32 tmp;
	u32 disabled_rb_mask;
	int i, j;

	switch (rdev->family) {
	case CHIP_CAYMAN:
		rdev->config.cayman.max_shader_engines = 2;
		rdev->config.cayman.max_pipes_per_simd = 4;
		rdev->config.cayman.max_tile_pipes = 8;
		rdev->config.cayman.max_simds_per_se = 12;
		rdev->config.cayman.max_backends_per_se = 4;
		rdev->config.cayman.max_texture_channel_caches = 8;
		rdev->config.cayman.max_gprs = 256;
		rdev->config.cayman.max_threads = 256;
		rdev->config.cayman.max_gs_threads = 32;
		rdev->config.cayman.max_stack_entries = 512;
		rdev->config.cayman.sx_num_of_sets = 8;
		rdev->config.cayman.sx_max_export_size = 256;
		rdev->config.cayman.sx_max_export_pos_size = 64;
		rdev->config.cayman.sx_max_export_smx_size = 192;
		rdev->config.cayman.max_hw_contexts = 8;
		rdev->config.cayman.sq_num_cf_insts = 2;

		rdev->config.cayman.sc_prim_fifo_size = 0x100;
		rdev->config.cayman.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.cayman.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = CAYMAN_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_ARUBA:
	default:
		rdev->config.cayman.max_shader_engines = 1;
		rdev->config.cayman.max_pipes_per_simd = 4;
		rdev->config.cayman.max_tile_pipes = 2;
		if ((rdev->pdev->device == 0x9900) ||
		    (rdev->pdev->device == 0x9901) ||
		    (rdev->pdev->device == 0x9905) ||
		    (rdev->pdev->device == 0x9906) ||
		    (rdev->pdev->device == 0x9907) ||
		    (rdev->pdev->device == 0x9908) ||
		    (rdev->pdev->device == 0x9909) ||
		    (rdev->pdev->device == 0x990B) ||
		    (rdev->pdev->device == 0x990C) ||
		    (rdev->pdev->device == 0x990F) ||
		    (rdev->pdev->device == 0x9910) ||
		    (rdev->pdev->device == 0x9917) ||
		    (rdev->pdev->device == 0x9999) ||
		    (rdev->pdev->device == 0x999C)) {
			rdev->config.cayman.max_simds_per_se = 6;
			rdev->config.cayman.max_backends_per_se = 2;
			rdev->config.cayman.max_hw_contexts = 8;
			rdev->config.cayman.sx_max_export_size = 256;
			rdev->config.cayman.sx_max_export_pos_size = 64;
			rdev->config.cayman.sx_max_export_smx_size = 192;
		} else if ((rdev->pdev->device == 0x9903) ||
			   (rdev->pdev->device == 0x9904) ||
			   (rdev->pdev->device == 0x990A) ||
			   (rdev->pdev->device == 0x990D) ||
			   (rdev->pdev->device == 0x990E) ||
			   (rdev->pdev->device == 0x9913) ||
			   (rdev->pdev->device == 0x9918) ||
			   (rdev->pdev->device == 0x999D)) {
			rdev->config.cayman.max_simds_per_se = 4;
			rdev->config.cayman.max_backends_per_se = 2;
			rdev->config.cayman.max_hw_contexts = 8;
			rdev->config.cayman.sx_max_export_size = 256;
			rdev->config.cayman.sx_max_export_pos_size = 64;
			rdev->config.cayman.sx_max_export_smx_size = 192;
		} else if ((rdev->pdev->device == 0x9919) ||
			   (rdev->pdev->device == 0x9990) ||
			   (rdev->pdev->device == 0x9991) ||
			   (rdev->pdev->device == 0x9994) ||
			   (rdev->pdev->device == 0x9995) ||
			   (rdev->pdev->device == 0x9996) ||
			   (rdev->pdev->device == 0x999A) ||
			   (rdev->pdev->device == 0x99A0)) {
			rdev->config.cayman.max_simds_per_se = 3;
			rdev->config.cayman.max_backends_per_se = 1;
			rdev->config.cayman.max_hw_contexts = 4;
			rdev->config.cayman.sx_max_export_size = 128;
			rdev->config.cayman.sx_max_export_pos_size = 32;
			rdev->config.cayman.sx_max_export_smx_size = 96;
		} else {
			rdev->config.cayman.max_simds_per_se = 2;
			rdev->config.cayman.max_backends_per_se = 1;
			rdev->config.cayman.max_hw_contexts = 4;
			rdev->config.cayman.sx_max_export_size = 128;
			rdev->config.cayman.sx_max_export_pos_size = 32;
			rdev->config.cayman.sx_max_export_smx_size = 96;
		}
		rdev->config.cayman.max_texture_channel_caches = 2;
		rdev->config.cayman.max_gprs = 256;
		rdev->config.cayman.max_threads = 256;
		rdev->config.cayman.max_gs_threads = 32;
		rdev->config.cayman.max_stack_entries = 512;
		rdev->config.cayman.sx_num_of_sets = 8;
		rdev->config.cayman.sq_num_cf_insts = 2;

		rdev->config.cayman.sc_prim_fifo_size = 0x40;
		rdev->config.cayman.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.cayman.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = ARUBA_GB_ADDR_CONFIG_GOLDEN;
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
	WREG32(SRBM_INT_CNTL, 0x1);
	WREG32(SRBM_INT_ACK, 0x1);

	evergreen_fix_pci_max_read_req_size(rdev);

	mc_shared_chmap = RREG32(MC_SHARED_CHMAP);
	mc_arb_ramcfg = RREG32(MC_ARB_RAMCFG);

	tmp = (mc_arb_ramcfg & NOOFCOLS_MASK) >> NOOFCOLS_SHIFT;
	rdev->config.cayman.mem_row_size_in_kb = (4 * (1 << (8 + tmp))) / 1024;
	if (rdev->config.cayman.mem_row_size_in_kb > 4)
		rdev->config.cayman.mem_row_size_in_kb = 4;
	/* XXX use MC settings? */
	rdev->config.cayman.shader_engine_tile_size = 32;
	rdev->config.cayman.num_gpus = 1;
	rdev->config.cayman.multi_gpu_tile_size = 64;

	tmp = (gb_addr_config & NUM_PIPES_MASK) >> NUM_PIPES_SHIFT;
	rdev->config.cayman.num_tile_pipes = (1 << tmp);
	tmp = (gb_addr_config & PIPE_INTERLEAVE_SIZE_MASK) >> PIPE_INTERLEAVE_SIZE_SHIFT;
	rdev->config.cayman.mem_max_burst_length_bytes = (tmp + 1) * 256;
	tmp = (gb_addr_config & NUM_SHADER_ENGINES_MASK) >> NUM_SHADER_ENGINES_SHIFT;
	rdev->config.cayman.num_shader_engines = tmp + 1;
	tmp = (gb_addr_config & NUM_GPUS_MASK) >> NUM_GPUS_SHIFT;
	rdev->config.cayman.num_gpus = tmp + 1;
	tmp = (gb_addr_config & MULTI_GPU_TILE_SIZE_MASK) >> MULTI_GPU_TILE_SIZE_SHIFT;
	rdev->config.cayman.multi_gpu_tile_size = 1 << tmp;
	tmp = (gb_addr_config & ROW_SIZE_MASK) >> ROW_SIZE_SHIFT;
	rdev->config.cayman.mem_row_size_in_kb = 1 << tmp;


	/* setup tiling info dword.  gb_addr_config is not adequate since it does
	 * not have bank info, so create a custom tiling dword.
	 * bits 3:0   num_pipes
	 * bits 7:4   num_banks
	 * bits 11:8  group_size
	 * bits 15:12 row_size
	 */
	rdev->config.cayman.tile_config = 0;
	switch (rdev->config.cayman.num_tile_pipes) {
	case 1:
	default:
		rdev->config.cayman.tile_config |= (0 << 0);
		break;
	case 2:
		rdev->config.cayman.tile_config |= (1 << 0);
		break;
	case 4:
		rdev->config.cayman.tile_config |= (2 << 0);
		break;
	case 8:
		rdev->config.cayman.tile_config |= (3 << 0);
		break;
	}

	/* num banks is 8 on all fusion asics. 0 = 4, 1 = 8, 2 = 16 */
	if (rdev->flags & RADEON_IS_IGP)
		rdev->config.cayman.tile_config |= 1 << 4;
	else {
		switch ((mc_arb_ramcfg & NOOFBANK_MASK) >> NOOFBANK_SHIFT) {
		case 0: /* four banks */
			rdev->config.cayman.tile_config |= 0 << 4;
			break;
		case 1: /* eight banks */
			rdev->config.cayman.tile_config |= 1 << 4;
			break;
		case 2: /* sixteen banks */
		default:
			rdev->config.cayman.tile_config |= 2 << 4;
			break;
		}
	}
	rdev->config.cayman.tile_config |=
		((gb_addr_config & PIPE_INTERLEAVE_SIZE_MASK) >> PIPE_INTERLEAVE_SIZE_SHIFT) << 8;
	rdev->config.cayman.tile_config |=
		((gb_addr_config & ROW_SIZE_MASK) >> ROW_SIZE_SHIFT) << 12;

	tmp = 0;
	for (i = (rdev->config.cayman.max_shader_engines - 1); i >= 0; i--) {
		u32 rb_disable_bitmap;

		WREG32(GRBM_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_INDEX(i));
		WREG32(RLC_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_INDEX(i));
		rb_disable_bitmap = (RREG32(CC_RB_BACKEND_DISABLE) & 0x00ff0000) >> 16;
		tmp <<= 4;
		tmp |= rb_disable_bitmap;
	}
	/* enabled rb are just the one not disabled :) */
	disabled_rb_mask = tmp;
	tmp = 0;
	for (i = 0; i < (rdev->config.cayman.max_backends_per_se * rdev->config.cayman.max_shader_engines); i++)
		tmp |= (1 << i);
	/* if all the backends are disabled, fix it up here */
	if ((disabled_rb_mask & tmp) == tmp) {
		for (i = 0; i < (rdev->config.cayman.max_backends_per_se * rdev->config.cayman.max_shader_engines); i++)
			disabled_rb_mask &= ~(1 << i);
	}

	for (i = 0; i < rdev->config.cayman.max_shader_engines; i++) {
		u32 simd_disable_bitmap;

		WREG32(GRBM_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_INDEX(i));
		WREG32(RLC_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_INDEX(i));
		simd_disable_bitmap = (RREG32(CC_GC_SHADER_PIPE_CONFIG) & 0xffff0000) >> 16;
		simd_disable_bitmap |= 0xffffffff << rdev->config.cayman.max_simds_per_se;
		tmp <<= 16;
		tmp |= simd_disable_bitmap;
	}
	rdev->config.cayman.active_simds = hweight32(~tmp);

	WREG32(GRBM_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_BROADCAST_WRITES);
	WREG32(RLC_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_BROADCAST_WRITES);

	WREG32(GB_ADDR_CONFIG, gb_addr_config);
	WREG32(DMIF_ADDR_CONFIG, gb_addr_config);
	if (ASIC_IS_DCE6(rdev))
		WREG32(DMIF_ADDR_CALC, gb_addr_config);
	WREG32(HDP_ADDR_CONFIG, gb_addr_config);
	WREG32(DMA_TILING_CONFIG + DMA0_REGISTER_OFFSET, gb_addr_config);
	WREG32(DMA_TILING_CONFIG + DMA1_REGISTER_OFFSET, gb_addr_config);
	WREG32(UVD_UDEC_ADDR_CONFIG, gb_addr_config);
	WREG32(UVD_UDEC_DB_ADDR_CONFIG, gb_addr_config);
	WREG32(UVD_UDEC_DBW_ADDR_CONFIG, gb_addr_config);

	if ((rdev->config.cayman.max_backends_per_se == 1) &&
	    (rdev->flags & RADEON_IS_IGP)) {
		if ((disabled_rb_mask & 3) == 2) {
			/* RB1 disabled, RB0 enabled */
			tmp = 0x00000000;
		} else {
			/* RB0 disabled, RB1 enabled */
			tmp = 0x11111111;
		}
	} else {
		tmp = gb_addr_config & NUM_PIPES_MASK;
		tmp = r6xx_remap_render_backend(rdev, tmp,
						rdev->config.cayman.max_backends_per_se *
						rdev->config.cayman.max_shader_engines,
						CAYMAN_MAX_BACKENDS, disabled_rb_mask);
	}
	WREG32(GB_BACKEND_MAP, tmp);

	cgts_tcc_disable = 0xffff0000;
	for (i = 0; i < rdev->config.cayman.max_texture_channel_caches; i++)
		cgts_tcc_disable &= ~(1 << (16 + i));
	WREG32(CGTS_TCC_DISABLE, cgts_tcc_disable);
	WREG32(CGTS_SYS_TCC_DISABLE, cgts_tcc_disable);
	WREG32(CGTS_USER_SYS_TCC_DISABLE, cgts_tcc_disable);
	WREG32(CGTS_USER_TCC_DISABLE, cgts_tcc_disable);

	/* reprogram the shader complex */
	cgts_sm_ctrl_reg = RREG32(CGTS_SM_CTRL_REG);
	for (i = 0; i < 16; i++)
		WREG32(CGTS_SM_CTRL_REG, OVERRIDE);
	WREG32(CGTS_SM_CTRL_REG, cgts_sm_ctrl_reg);

	/* set HW defaults for 3D engine */
	WREG32(CP_MEQ_THRESHOLDS, MEQ1_START(0x30) | MEQ2_START(0x60));

	sx_debug_1 = RREG32(SX_DEBUG_1);
	sx_debug_1 |= ENABLE_NEW_SMX_ADDRESS;
	WREG32(SX_DEBUG_1, sx_debug_1);

	smx_dc_ctl0 = RREG32(SMX_DC_CTL0);
	smx_dc_ctl0 &= ~NUMBER_OF_SETS(0x1ff);
	smx_dc_ctl0 |= NUMBER_OF_SETS(rdev->config.cayman.sx_num_of_sets);
	WREG32(SMX_DC_CTL0, smx_dc_ctl0);

	WREG32(SPI_CONFIG_CNTL_1, VTX_DONE_DELAY(4) | CRC_SIMD_ID_WADDR_DISABLE);

	/* need to be explicitly zero-ed */
	WREG32(VGT_OFFCHIP_LDS_BASE, 0);
	WREG32(SQ_LSTMP_RING_BASE, 0);
	WREG32(SQ_HSTMP_RING_BASE, 0);
	WREG32(SQ_ESTMP_RING_BASE, 0);
	WREG32(SQ_GSTMP_RING_BASE, 0);
	WREG32(SQ_VSTMP_RING_BASE, 0);
	WREG32(SQ_PSTMP_RING_BASE, 0);

	WREG32(TA_CNTL_AUX, DISABLE_CUBE_ANISO);

	WREG32(SX_EXPORT_BUFFER_SIZES, (COLOR_BUFFER_SIZE((rdev->config.cayman.sx_max_export_size / 4) - 1) |
					POSITION_BUFFER_SIZE((rdev->config.cayman.sx_max_export_pos_size / 4) - 1) |
					SMX_BUFFER_SIZE((rdev->config.cayman.sx_max_export_smx_size / 4) - 1)));

	WREG32(PA_SC_FIFO_SIZE, (SC_PRIM_FIFO_SIZE(rdev->config.cayman.sc_prim_fifo_size) |
				 SC_HIZ_TILE_FIFO_SIZE(rdev->config.cayman.sc_hiz_tile_fifo_size) |
				 SC_EARLYZ_TILE_FIFO_SIZE(rdev->config.cayman.sc_earlyz_tile_fifo_size)));


	WREG32(VGT_NUM_INSTANCES, 1);

	WREG32(CP_PERFMON_CNTL, 0);

	WREG32(SQ_MS_FIFO_SIZES, (CACHE_FIFO_SIZE(16 * rdev->config.cayman.sq_num_cf_insts) |
				  FETCH_FIFO_HIWATER(0x4) |
				  DONE_FIFO_HIWATER(0xe0) |
				  ALU_UPDATE_FIFO_HIWATER(0x8)));

	WREG32(SQ_GPR_RESOURCE_MGMT_1, NUM_CLAUSE_TEMP_GPRS(4));
	WREG32(SQ_CONFIG, (VC_ENABLE |
			   EXPORT_SRC_C |
			   GFX_PRIO(0) |
			   CS1_PRIO(0) |
			   CS2_PRIO(1)));
	WREG32(SQ_DYN_GPR_CNTL_PS_FLUSH_REQ, DYN_GPR_ENABLE);

	WREG32(PA_SC_FORCE_EOV_MAX_CNTS, (FORCE_EOV_MAX_CLK_CNT(4095) |
					  FORCE_EOV_MAX_REZ_CNT(255)));

	WREG32(VGT_CACHE_INVALIDATION, CACHE_INVALIDATION(VC_AND_TC) |
	       AUTO_INVLD_EN(ES_AND_GS_AUTO));

	WREG32(VGT_GS_VERTEX_REUSE, 16);
	WREG32(PA_SC_LINE_STIPPLE_STATE, 0);

	WREG32(CB_PERF_CTR0_SEL_0, 0);
	WREG32(CB_PERF_CTR0_SEL_1, 0);
	WREG32(CB_PERF_CTR1_SEL_0, 0);
	WREG32(CB_PERF_CTR1_SEL_1, 0);
	WREG32(CB_PERF_CTR2_SEL_0, 0);
	WREG32(CB_PERF_CTR2_SEL_1, 0);
	WREG32(CB_PERF_CTR3_SEL_0, 0);
	WREG32(CB_PERF_CTR3_SEL_1, 0);

	tmp = RREG32(HDP_MISC_CNTL);
	tmp |= HDP_FLUSH_INVALIDATE_CACHE;
	WREG32(HDP_MISC_CNTL, tmp);

	hdp_host_path_cntl = RREG32(HDP_HOST_PATH_CNTL);
	WREG32(HDP_HOST_PATH_CNTL, hdp_host_path_cntl);

	WREG32(PA_CL_ENHANCE, CLIP_VTX_REORDER_ENA | NUM_CLIP_SEQ(3));

	udelay(50);

	/* set clockgating golden values on TN */
	if (rdev->family == CHIP_ARUBA) {
		tmp = RREG32_CG(CG_CGTT_LOCAL_0);
		tmp &= ~0x00380000;
		WREG32_CG(CG_CGTT_LOCAL_0, tmp);
		tmp = RREG32_CG(CG_CGTT_LOCAL_1);
		tmp &= ~0x0e000000;
		WREG32_CG(CG_CGTT_LOCAL_1, tmp);
	}
}

/*
 * GART
 */
void cayman_pcie_gart_tlb_flush(struct radeon_device *rdev)
{
	/* flush hdp cache */
	WREG32(HDP_MEM_COHERENCY_FLUSH_CNTL, 0x1);

	/* bits 0-7 are the VM contexts0-7 */
	WREG32(VM_INVALIDATE_REQUEST, 1);
}

static int cayman_pcie_gart_enable(struct radeon_device *rdev)
{
	int i, r;

	if (rdev->gart.robj == NULL) {
		dev_err(rdev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}
	r = radeon_gart_table_vram_pin(rdev);
	if (r)
		return r;
	/* Setup TLB control */
	WREG32(MC_VM_MX_L1_TLB_CNTL,
	       (0xA << 7) |
	       ENABLE_L1_TLB |
	       ENABLE_L1_FRAGMENT_PROCESSING |
	       SYSTEM_ACCESS_MODE_NOT_IN_SYS |
	       ENABLE_ADVANCED_DRIVER_MODEL |
	       SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU);
	/* Setup L2 cache */
	WREG32(VM_L2_CNTL, ENABLE_L2_CACHE |
	       ENABLE_L2_FRAGMENT_PROCESSING |
	       ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE |
	       ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE |
	       EFFECTIVE_L2_QUEUE_SIZE(7) |
	       CONTEXT1_IDENTITY_ACCESS_MODE(1));
	WREG32(VM_L2_CNTL2, INVALIDATE_ALL_L1_TLBS | INVALIDATE_L2_CACHE);
	WREG32(VM_L2_CNTL3, L2_CACHE_BIGK_ASSOCIATIVITY |
	       BANK_SELECT(6) |
	       L2_CACHE_BIGK_FRAGMENT_SIZE(6));
	/* setup context0 */
	WREG32(VM_CONTEXT0_PAGE_TABLE_START_ADDR, rdev->mc.gtt_start >> 12);
	WREG32(VM_CONTEXT0_PAGE_TABLE_END_ADDR, rdev->mc.gtt_end >> 12);
	WREG32(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR, rdev->gart.table_addr >> 12);
	WREG32(VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR,
			(u32)(rdev->dummy_page.addr >> 12));
	WREG32(VM_CONTEXT0_CNTL2, 0);
	WREG32(VM_CONTEXT0_CNTL, ENABLE_CONTEXT | PAGE_TABLE_DEPTH(0) |
				RANGE_PROTECTION_FAULT_ENABLE_DEFAULT);

	WREG32(0x15D4, 0);
	WREG32(0x15D8, 0);
	WREG32(0x15DC, 0);

	/* empty context1-7 */
	/* Assign the pt base to something valid for now; the pts used for
	 * the VMs are determined by the application and setup and assigned
	 * on the fly in the vm part of radeon_gart.c
	 */
	for (i = 1; i < 8; i++) {
		WREG32(VM_CONTEXT0_PAGE_TABLE_START_ADDR + (i << 2), 0);
		WREG32(VM_CONTEXT0_PAGE_TABLE_END_ADDR + (i << 2),
			rdev->vm_manager.max_pfn - 1);
		WREG32(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (i << 2),
		       rdev->vm_manager.saved_table_addr[i]);
	}

	/* enable context1-7 */
	WREG32(VM_CONTEXT1_PROTECTION_FAULT_DEFAULT_ADDR,
	       (u32)(rdev->dummy_page.addr >> 12));
	WREG32(VM_CONTEXT1_CNTL2, 4);
	WREG32(VM_CONTEXT1_CNTL, ENABLE_CONTEXT | PAGE_TABLE_DEPTH(1) |
				PAGE_TABLE_BLOCK_SIZE(radeon_vm_block_size - 9) |
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

	cayman_pcie_gart_tlb_flush(rdev);
	DRM_INFO("PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(rdev->mc.gtt_size >> 20),
		 (unsigned long long)rdev->gart.table_addr);
	rdev->gart.ready = true;
	return 0;
}

static void cayman_pcie_gart_disable(struct radeon_device *rdev)
{
	unsigned i;

	for (i = 1; i < 8; ++i) {
		rdev->vm_manager.saved_table_addr[i] = RREG32(
			VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (i << 2));
	}

	/* Disable all tables */
	WREG32(VM_CONTEXT0_CNTL, 0);
	WREG32(VM_CONTEXT1_CNTL, 0);
	/* Setup TLB control */
	WREG32(MC_VM_MX_L1_TLB_CNTL, ENABLE_L1_FRAGMENT_PROCESSING |
	       SYSTEM_ACCESS_MODE_NOT_IN_SYS |
	       SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU);
	/* Setup L2 cache */
	WREG32(VM_L2_CNTL, ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE |
	       ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE |
	       EFFECTIVE_L2_QUEUE_SIZE(7) |
	       CONTEXT1_IDENTITY_ACCESS_MODE(1));
	WREG32(VM_L2_CNTL2, 0);
	WREG32(VM_L2_CNTL3, L2_CACHE_BIGK_ASSOCIATIVITY |
	       L2_CACHE_BIGK_FRAGMENT_SIZE(6));
	radeon_gart_table_vram_unpin(rdev);
}

static void cayman_pcie_gart_fini(struct radeon_device *rdev)
{
	cayman_pcie_gart_disable(rdev);
	radeon_gart_table_vram_free(rdev);
	radeon_gart_fini(rdev);
}

void cayman_cp_int_cntl_setup(struct radeon_device *rdev,
			      int ring, u32 cp_int_cntl)
{
	u32 srbm_gfx_cntl = RREG32(SRBM_GFX_CNTL) & ~3;

	WREG32(SRBM_GFX_CNTL, srbm_gfx_cntl | (ring & 3));
	WREG32(CP_INT_CNTL, cp_int_cntl);
}

/*
 * CP.
 */
void cayman_fence_ring_emit(struct radeon_device *rdev,
			    struct radeon_fence *fence)
{
	struct radeon_ring *ring = &rdev->ring[fence->ring];
	u64 addr = rdev->fence_drv[fence->ring].gpu_addr;
	u32 cp_coher_cntl = PACKET3_FULL_CACHE_ENA | PACKET3_TC_ACTION_ENA |
		PACKET3_SH_ACTION_ENA;

	/* flush read cache over gart for this vmid */
	radeon_ring_write(ring, PACKET3(PACKET3_SURFACE_SYNC, 3));
	radeon_ring_write(ring, PACKET3_ENGINE_ME | cp_coher_cntl);
	radeon_ring_write(ring, 0xFFFFFFFF);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 10); /* poll interval */
	/* EVENT_WRITE_EOP - flush caches, send int */
	radeon_ring_write(ring, PACKET3(PACKET3_EVENT_WRITE_EOP, 4));
	radeon_ring_write(ring, EVENT_TYPE(CACHE_FLUSH_AND_INV_EVENT_TS) | EVENT_INDEX(5));
	radeon_ring_write(ring, lower_32_bits(addr));
	radeon_ring_write(ring, (upper_32_bits(addr) & 0xff) | DATA_SEL(1) | INT_SEL(2));
	radeon_ring_write(ring, fence->seq);
	radeon_ring_write(ring, 0);
}

void cayman_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib)
{
	struct radeon_ring *ring = &rdev->ring[ib->ring];
	unsigned vm_id = ib->vm ? ib->vm->ids[ib->ring].id : 0;
	u32 cp_coher_cntl = PACKET3_FULL_CACHE_ENA | PACKET3_TC_ACTION_ENA |
		PACKET3_SH_ACTION_ENA;

	/* set to DX10/11 mode */
	radeon_ring_write(ring, PACKET3(PACKET3_MODE_CONTROL, 0));
	radeon_ring_write(ring, 1);

	if (ring->rptr_save_reg) {
		uint32_t next_rptr = ring->wptr + 3 + 4 + 8;
		radeon_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
		radeon_ring_write(ring, ((ring->rptr_save_reg - 
					  PACKET3_SET_CONFIG_REG_START) >> 2));
		radeon_ring_write(ring, next_rptr);
	}

	radeon_ring_write(ring, PACKET3(PACKET3_INDIRECT_BUFFER, 2));
	radeon_ring_write(ring,
#ifdef __BIG_ENDIAN
			  (2 << 0) |
#endif
			  (ib->gpu_addr & 0xFFFFFFFC));
	radeon_ring_write(ring, upper_32_bits(ib->gpu_addr) & 0xFF);
	radeon_ring_write(ring, ib->length_dw | (vm_id << 24));

	/* flush read cache over gart for this vmid */
	radeon_ring_write(ring, PACKET3(PACKET3_SURFACE_SYNC, 3));
	radeon_ring_write(ring, PACKET3_ENGINE_ME | cp_coher_cntl);
	radeon_ring_write(ring, 0xFFFFFFFF);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, (vm_id << 24) | 10); /* poll interval */
}

static void cayman_cp_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32(CP_ME_CNTL, 0);
	else {
		if (rdev->asic->copy.copy_ring_index == RADEON_RING_TYPE_GFX_INDEX)
			radeon_ttm_set_active_vram_size(rdev, rdev->mc.visible_vram_size);
		WREG32(CP_ME_CNTL, (CP_ME_HALT | CP_PFP_HALT));
		WREG32(SCRATCH_UMSK, 0);
		rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = false;
	}
}

u32 cayman_gfx_get_rptr(struct radeon_device *rdev,
			struct radeon_ring *ring)
{
	u32 rptr;

	if (rdev->wb.enabled)
		rptr = rdev->wb.wb[ring->rptr_offs/4];
	else {
		if (ring->idx == RADEON_RING_TYPE_GFX_INDEX)
			rptr = RREG32(CP_RB0_RPTR);
		else if (ring->idx == CAYMAN_RING_TYPE_CP1_INDEX)
			rptr = RREG32(CP_RB1_RPTR);
		else
			rptr = RREG32(CP_RB2_RPTR);
	}

	return rptr;
}

u32 cayman_gfx_get_wptr(struct radeon_device *rdev,
			struct radeon_ring *ring)
{
	u32 wptr;

	if (ring->idx == RADEON_RING_TYPE_GFX_INDEX)
		wptr = RREG32(CP_RB0_WPTR);
	else if (ring->idx == CAYMAN_RING_TYPE_CP1_INDEX)
		wptr = RREG32(CP_RB1_WPTR);
	else
		wptr = RREG32(CP_RB2_WPTR);

	return wptr;
}

void cayman_gfx_set_wptr(struct radeon_device *rdev,
			 struct radeon_ring *ring)
{
	if (ring->idx == RADEON_RING_TYPE_GFX_INDEX) {
		WREG32(CP_RB0_WPTR, ring->wptr);
		(void)RREG32(CP_RB0_WPTR);
	} else if (ring->idx == CAYMAN_RING_TYPE_CP1_INDEX) {
		WREG32(CP_RB1_WPTR, ring->wptr);
		(void)RREG32(CP_RB1_WPTR);
	} else {
		WREG32(CP_RB2_WPTR, ring->wptr);
		(void)RREG32(CP_RB2_WPTR);
	}
}

static int cayman_cp_load_microcode(struct radeon_device *rdev)
{
	const __be32 *fw_data;
	int i;

	if (!rdev->me_fw || !rdev->pfp_fw)
		return -EINVAL;

	cayman_cp_enable(rdev, false);

	fw_data = (const __be32 *)rdev->pfp_fw->data;
	WREG32(CP_PFP_UCODE_ADDR, 0);
	for (i = 0; i < CAYMAN_PFP_UCODE_SIZE; i++)
		WREG32(CP_PFP_UCODE_DATA, be32_to_cpup(fw_data++));
	WREG32(CP_PFP_UCODE_ADDR, 0);

	fw_data = (const __be32 *)rdev->me_fw->data;
	WREG32(CP_ME_RAM_WADDR, 0);
	for (i = 0; i < CAYMAN_PM4_UCODE_SIZE; i++)
		WREG32(CP_ME_RAM_DATA, be32_to_cpup(fw_data++));

	WREG32(CP_PFP_UCODE_ADDR, 0);
	WREG32(CP_ME_RAM_WADDR, 0);
	WREG32(CP_ME_RAM_RADDR, 0);
	return 0;
}

static int cayman_cp_start(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	int r, i;

	r = radeon_ring_lock(rdev, ring, 7);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring (%d).\n", r);
		return r;
	}
	radeon_ring_write(ring, PACKET3(PACKET3_ME_INITIALIZE, 5));
	radeon_ring_write(ring, 0x1);
	radeon_ring_write(ring, 0x0);
	radeon_ring_write(ring, rdev->config.cayman.max_hw_contexts - 1);
	radeon_ring_write(ring, PACKET3_ME_INITIALIZE_DEVICE_ID(1));
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 0);
	radeon_ring_unlock_commit(rdev, ring, false);

	cayman_cp_enable(rdev, true);

	r = radeon_ring_lock(rdev, ring, cayman_default_size + 19);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring (%d).\n", r);
		return r;
	}

	/* setup clear context state */
	radeon_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	radeon_ring_write(ring, PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	for (i = 0; i < cayman_default_size; i++)
		radeon_ring_write(ring, cayman_default_state[i]);

	radeon_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	radeon_ring_write(ring, PACKET3_PREAMBLE_END_CLEAR_STATE);

	/* set clear context state */
	radeon_ring_write(ring, PACKET3(PACKET3_CLEAR_STATE, 0));
	radeon_ring_write(ring, 0);

	/* SQ_VTX_BASE_VTX_LOC */
	radeon_ring_write(ring, 0xc0026f00);
	radeon_ring_write(ring, 0x00000000);
	radeon_ring_write(ring, 0x00000000);
	radeon_ring_write(ring, 0x00000000);

	/* Clear consts */
	radeon_ring_write(ring, 0xc0036f00);
	radeon_ring_write(ring, 0x00000bc4);
	radeon_ring_write(ring, 0xffffffff);
	radeon_ring_write(ring, 0xffffffff);
	radeon_ring_write(ring, 0xffffffff);

	radeon_ring_write(ring, 0xc0026900);
	radeon_ring_write(ring, 0x00000316);
	radeon_ring_write(ring, 0x0000000e); /* VGT_VERTEX_REUSE_BLOCK_CNTL */
	radeon_ring_write(ring, 0x00000010); /*  */

	radeon_ring_unlock_commit(rdev, ring, false);

	/* XXX init other rings */

	return 0;
}

static void cayman_cp_fini(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	cayman_cp_enable(rdev, false);
	radeon_ring_fini(rdev, ring);
	radeon_scratch_free(rdev, ring->rptr_save_reg);
}

static int cayman_cp_resume(struct radeon_device *rdev)
{
	static const int ridx[] = {
		RADEON_RING_TYPE_GFX_INDEX,
		CAYMAN_RING_TYPE_CP1_INDEX,
		CAYMAN_RING_TYPE_CP2_INDEX
	};
	static const unsigned cp_rb_cntl[] = {
		CP_RB0_CNTL,
		CP_RB1_CNTL,
		CP_RB2_CNTL,
	};
	static const unsigned cp_rb_rptr_addr[] = {
		CP_RB0_RPTR_ADDR,
		CP_RB1_RPTR_ADDR,
		CP_RB2_RPTR_ADDR
	};
	static const unsigned cp_rb_rptr_addr_hi[] = {
		CP_RB0_RPTR_ADDR_HI,
		CP_RB1_RPTR_ADDR_HI,
		CP_RB2_RPTR_ADDR_HI
	};
	static const unsigned cp_rb_base[] = {
		CP_RB0_BASE,
		CP_RB1_BASE,
		CP_RB2_BASE
	};
	static const unsigned cp_rb_rptr[] = {
		CP_RB0_RPTR,
		CP_RB1_RPTR,
		CP_RB2_RPTR
	};
	static const unsigned cp_rb_wptr[] = {
		CP_RB0_WPTR,
		CP_RB1_WPTR,
		CP_RB2_WPTR
	};
	struct radeon_ring *ring;
	int i, r;

	/* Reset cp; if cp is reset, then PA, SH, VGT also need to be reset */
	WREG32(GRBM_SOFT_RESET, (SOFT_RESET_CP |
				 SOFT_RESET_PA |
				 SOFT_RESET_SH |
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

	WREG32(CP_DEBUG, (1 << 27));

	/* set the wb address whether it's enabled or not */
	WREG32(SCRATCH_ADDR, ((rdev->wb.gpu_addr + RADEON_WB_SCRATCH_OFFSET) >> 8) & 0xFFFFFFFF);
	WREG32(SCRATCH_UMSK, 0xff);

	for (i = 0; i < 3; ++i) {
		uint32_t rb_cntl;
		uint64_t addr;

		/* Set ring buffer size */
		ring = &rdev->ring[ridx[i]];
		rb_cntl = order_base_2(ring->ring_size / 8);
		rb_cntl |= order_base_2(RADEON_GPU_PAGE_SIZE/8) << 8;
#ifdef __BIG_ENDIAN
		rb_cntl |= BUF_SWAP_32BIT;
#endif
		WREG32(cp_rb_cntl[i], rb_cntl);

		/* set the wb address whether it's enabled or not */
		addr = rdev->wb.gpu_addr + RADEON_WB_CP_RPTR_OFFSET;
		WREG32(cp_rb_rptr_addr[i], addr & 0xFFFFFFFC);
		WREG32(cp_rb_rptr_addr_hi[i], upper_32_bits(addr) & 0xFF);
	}

	/* set the rb base addr, this causes an internal reset of ALL rings */
	for (i = 0; i < 3; ++i) {
		ring = &rdev->ring[ridx[i]];
		WREG32(cp_rb_base[i], ring->gpu_addr >> 8);
	}

	for (i = 0; i < 3; ++i) {
		/* Initialize the ring buffer's read and write pointers */
		ring = &rdev->ring[ridx[i]];
		WREG32_P(cp_rb_cntl[i], RB_RPTR_WR_ENA, ~RB_RPTR_WR_ENA);

		ring->wptr = 0;
		WREG32(cp_rb_rptr[i], 0);
		WREG32(cp_rb_wptr[i], ring->wptr);

		mdelay(1);
		WREG32_P(cp_rb_cntl[i], 0, ~RB_RPTR_WR_ENA);
	}

	/* start the rings */
	cayman_cp_start(rdev);
	rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = true;
	rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX].ready = false;
	rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX].ready = false;
	/* this only test cp0 */
	r = radeon_ring_test(rdev, RADEON_RING_TYPE_GFX_INDEX, &rdev->ring[RADEON_RING_TYPE_GFX_INDEX]);
	if (r) {
		rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = false;
		rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX].ready = false;
		rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX].ready = false;
		return r;
	}

	if (rdev->asic->copy.copy_ring_index == RADEON_RING_TYPE_GFX_INDEX)
		radeon_ttm_set_active_vram_size(rdev, rdev->mc.real_vram_size);

	return 0;
}

u32 cayman_gpu_check_soft_reset(struct radeon_device *rdev)
{
	u32 reset_mask = 0;
	u32 tmp;

	/* GRBM_STATUS */
	tmp = RREG32(GRBM_STATUS);
	if (tmp & (PA_BUSY | SC_BUSY |
		   SH_BUSY | SX_BUSY |
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
	if (tmp & (RLC_RQ_PENDING | RLC_BUSY))
		reset_mask |= RADEON_RESET_RLC;

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

static void cayman_gpu_soft_reset(struct radeon_device *rdev, u32 reset_mask)
{
	struct evergreen_mc_save save;
	u32 grbm_soft_reset = 0, srbm_soft_reset = 0;
	u32 tmp;

	if (reset_mask == 0)
		return;

	dev_info(rdev->dev, "GPU softreset: 0x%08X\n", reset_mask);

	evergreen_print_gpu_status_regs(rdev);
	dev_info(rdev->dev, "  VM_CONTEXT0_PROTECTION_FAULT_ADDR   0x%08X\n",
		 RREG32(0x14F8));
	dev_info(rdev->dev, "  VM_CONTEXT0_PROTECTION_FAULT_STATUS 0x%08X\n",
		 RREG32(0x14D8));
	dev_info(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_ADDR   0x%08X\n",
		 RREG32(0x14FC));
	dev_info(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_STATUS 0x%08X\n",
		 RREG32(0x14DC));

	/* Disable CP parsing/prefetching */
	WREG32(CP_ME_CNTL, CP_ME_HALT | CP_PFP_HALT);

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

	if (reset_mask & (RADEON_RESET_GFX | RADEON_RESET_COMPUTE)) {
		grbm_soft_reset = SOFT_RESET_CB |
			SOFT_RESET_DB |
			SOFT_RESET_GDS |
			SOFT_RESET_PA |
			SOFT_RESET_SC |
			SOFT_RESET_SPI |
			SOFT_RESET_SH |
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
		srbm_soft_reset |= SOFT_RESET_RLC;

	if (reset_mask & RADEON_RESET_SEM)
		srbm_soft_reset |= SOFT_RESET_SEM;

	if (reset_mask & RADEON_RESET_IH)
		srbm_soft_reset |= SOFT_RESET_IH;

	if (reset_mask & RADEON_RESET_GRBM)
		srbm_soft_reset |= SOFT_RESET_GRBM;

	if (reset_mask & RADEON_RESET_VMC)
		srbm_soft_reset |= SOFT_RESET_VMC;

	if (!(rdev->flags & RADEON_IS_IGP)) {
		if (reset_mask & RADEON_RESET_MC)
			srbm_soft_reset |= SOFT_RESET_MC;
	}

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

int cayman_asic_reset(struct radeon_device *rdev, bool hard)
{
	u32 reset_mask;

	if (hard) {
		evergreen_gpu_pci_config_reset(rdev);
		return 0;
	}

	reset_mask = cayman_gpu_check_soft_reset(rdev);

	if (reset_mask)
		r600_set_bios_scratch_engine_hung(rdev, true);

	cayman_gpu_soft_reset(rdev, reset_mask);

	reset_mask = cayman_gpu_check_soft_reset(rdev);

	if (reset_mask)
		evergreen_gpu_pci_config_reset(rdev);

	r600_set_bios_scratch_engine_hung(rdev, false);

	return 0;
}

/**
 * cayman_gfx_is_lockup - Check if the GFX engine is locked up
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Check if the GFX engine is locked up.
 * Returns true if the engine appears to be locked up, false if not.
 */
bool cayman_gfx_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring)
{
	u32 reset_mask = cayman_gpu_check_soft_reset(rdev);

	if (!(reset_mask & (RADEON_RESET_GFX |
			    RADEON_RESET_COMPUTE |
			    RADEON_RESET_CP))) {
		radeon_ring_lockup_update(rdev, ring);
		return false;
	}
	return radeon_ring_test_lockup(rdev, ring);
}

static void cayman_uvd_init(struct radeon_device *rdev)
{
	int r;

	if (!rdev->has_uvd)
		return;

	r = radeon_uvd_init(rdev);
	if (r) {
		dev_err(rdev->dev, "failed UVD (%d) init.\n", r);
		/*
		 * At this point rdev->uvd.vcpu_bo is NULL which trickles down
		 * to early fails uvd_v2_2_resume() and thus nothing happens
		 * there. So it is pointless to try to go through that code
		 * hence why we disable uvd here.
		 */
		rdev->has_uvd = 0;
		return;
	}
	rdev->ring[R600_RING_TYPE_UVD_INDEX].ring_obj = NULL;
	r600_ring_init(rdev, &rdev->ring[R600_RING_TYPE_UVD_INDEX], 4096);
}

static void cayman_uvd_start(struct radeon_device *rdev)
{
	int r;

	if (!rdev->has_uvd)
		return;

	r = uvd_v2_2_resume(rdev);
	if (r) {
		dev_err(rdev->dev, "failed UVD resume (%d).\n", r);
		goto error;
	}
	r = radeon_fence_driver_start_ring(rdev, R600_RING_TYPE_UVD_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing UVD fences (%d).\n", r);
		goto error;
	}
	return;

error:
	rdev->ring[R600_RING_TYPE_UVD_INDEX].ring_size = 0;
}

static void cayman_uvd_resume(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	int r;

	if (!rdev->has_uvd || !rdev->ring[R600_RING_TYPE_UVD_INDEX].ring_size)
		return;

	ring = &rdev->ring[R600_RING_TYPE_UVD_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, 0, RADEON_CP_PACKET2);
	if (r) {
		dev_err(rdev->dev, "failed initializing UVD ring (%d).\n", r);
		return;
	}
	r = uvd_v1_0_init(rdev);
	if (r) {
		dev_err(rdev->dev, "failed initializing UVD (%d).\n", r);
		return;
	}
}

static void cayman_vce_init(struct radeon_device *rdev)
{
	int r;

	/* Only set for CHIP_ARUBA */
	if (!rdev->has_vce)
		return;

	r = radeon_vce_init(rdev);
	if (r) {
		dev_err(rdev->dev, "failed VCE (%d) init.\n", r);
		/*
		 * At this point rdev->vce.vcpu_bo is NULL which trickles down
		 * to early fails cayman_vce_start() and thus nothing happens
		 * there. So it is pointless to try to go through that code
		 * hence why we disable vce here.
		 */
		rdev->has_vce = 0;
		return;
	}
	rdev->ring[TN_RING_TYPE_VCE1_INDEX].ring_obj = NULL;
	r600_ring_init(rdev, &rdev->ring[TN_RING_TYPE_VCE1_INDEX], 4096);
	rdev->ring[TN_RING_TYPE_VCE2_INDEX].ring_obj = NULL;
	r600_ring_init(rdev, &rdev->ring[TN_RING_TYPE_VCE2_INDEX], 4096);
}

static void cayman_vce_start(struct radeon_device *rdev)
{
	int r;

	if (!rdev->has_vce)
		return;

	r = radeon_vce_resume(rdev);
	if (r) {
		dev_err(rdev->dev, "failed VCE resume (%d).\n", r);
		goto error;
	}
	r = vce_v1_0_resume(rdev);
	if (r) {
		dev_err(rdev->dev, "failed VCE resume (%d).\n", r);
		goto error;
	}
	r = radeon_fence_driver_start_ring(rdev, TN_RING_TYPE_VCE1_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing VCE1 fences (%d).\n", r);
		goto error;
	}
	r = radeon_fence_driver_start_ring(rdev, TN_RING_TYPE_VCE2_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing VCE2 fences (%d).\n", r);
		goto error;
	}
	return;

error:
	rdev->ring[TN_RING_TYPE_VCE1_INDEX].ring_size = 0;
	rdev->ring[TN_RING_TYPE_VCE2_INDEX].ring_size = 0;
}

static void cayman_vce_resume(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	int r;

	if (!rdev->has_vce || !rdev->ring[TN_RING_TYPE_VCE1_INDEX].ring_size)
		return;

	ring = &rdev->ring[TN_RING_TYPE_VCE1_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, 0, 0x0);
	if (r) {
		dev_err(rdev->dev, "failed initializing VCE1 ring (%d).\n", r);
		return;
	}
	ring = &rdev->ring[TN_RING_TYPE_VCE2_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, 0, 0x0);
	if (r) {
		dev_err(rdev->dev, "failed initializing VCE1 ring (%d).\n", r);
		return;
	}
	r = vce_v1_0_init(rdev);
	if (r) {
		dev_err(rdev->dev, "failed initializing VCE (%d).\n", r);
		return;
	}
}

static int cayman_startup(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	int r;

	/* enable pcie gen2 link */
	evergreen_pcie_gen2_enable(rdev);
	/* enable aspm */
	evergreen_program_aspm(rdev);

	/* scratch needs to be initialized before MC */
	r = r600_vram_scratch_init(rdev);
	if (r)
		return r;

	evergreen_mc_program(rdev);

	if (!(rdev->flags & RADEON_IS_IGP) && !rdev->pm.dpm_enabled) {
		r = ni_mc_load_microcode(rdev);
		if (r) {
			DRM_ERROR("Failed to load MC firmware!\n");
			return r;
		}
	}

	r = cayman_pcie_gart_enable(rdev);
	if (r)
		return r;
	cayman_gpu_init(rdev);

	/* allocate rlc buffers */
	if (rdev->flags & RADEON_IS_IGP) {
		rdev->rlc.reg_list = tn_rlc_save_restore_register_list;
		rdev->rlc.reg_list_size =
			(u32)ARRAY_SIZE(tn_rlc_save_restore_register_list);
		rdev->rlc.cs_data = cayman_cs_data;
		r = sumo_rlc_init(rdev);
		if (r) {
			DRM_ERROR("Failed to init rlc BOs!\n");
			return r;
		}
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

	cayman_uvd_start(rdev);
	cayman_vce_start(rdev);

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

	/* Enable IRQ */
	if (!rdev->irq.installed) {
		r = radeon_irq_kms_init(rdev);
		if (r)
			return r;
	}

	r = r600_irq_init(rdev);
	if (r) {
		DRM_ERROR("radeon: IH init failed (%d).\n", r);
		radeon_irq_kms_fini(rdev);
		return r;
	}
	evergreen_irq_set(rdev);

	r = radeon_ring_init(rdev, ring, ring->ring_size, RADEON_WB_CP_RPTR_OFFSET,
			     RADEON_CP_PACKET2);
	if (r)
		return r;

	ring = &rdev->ring[R600_RING_TYPE_DMA_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, R600_WB_DMA_RPTR_OFFSET,
			     DMA_PACKET(DMA_PACKET_NOP, 0, 0, 0));
	if (r)
		return r;

	ring = &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, CAYMAN_WB_DMA1_RPTR_OFFSET,
			     DMA_PACKET(DMA_PACKET_NOP, 0, 0, 0));
	if (r)
		return r;

	r = cayman_cp_load_microcode(rdev);
	if (r)
		return r;
	r = cayman_cp_resume(rdev);
	if (r)
		return r;

	r = cayman_dma_resume(rdev);
	if (r)
		return r;

	cayman_uvd_resume(rdev);
	cayman_vce_resume(rdev);

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

	r = radeon_audio_init(rdev);
	if (r)
		return r;

	return 0;
}

int cayman_resume(struct radeon_device *rdev)
{
	int r;

	/* Do not reset GPU before posting, on rv770 hw unlike on r500 hw,
	 * posting will perform necessary task to bring back GPU into good
	 * shape.
	 */
	/* post card */
	atom_asic_init(rdev->mode_info.atom_context);

	/* init golden registers */
	ni_init_golden_registers(rdev);

	if (rdev->pm.pm_method == PM_METHOD_DPM)
		radeon_pm_resume(rdev);

	rdev->accel_working = true;
	r = cayman_startup(rdev);
	if (r) {
		DRM_ERROR("cayman startup failed on resume\n");
		rdev->accel_working = false;
		return r;
	}
	return r;
}

int cayman_suspend(struct radeon_device *rdev)
{
	radeon_pm_suspend(rdev);
	radeon_audio_fini(rdev);
	radeon_vm_manager_fini(rdev);
	cayman_cp_enable(rdev, false);
	cayman_dma_stop(rdev);
	if (rdev->has_uvd) {
		uvd_v1_0_fini(rdev);
		radeon_uvd_suspend(rdev);
	}
	evergreen_irq_suspend(rdev);
	radeon_wb_disable(rdev);
	cayman_pcie_gart_disable(rdev);
	return 0;
}

/* Plan is to move initialization in that function and use
 * helper function so that radeon_device_init pretty much
 * do nothing more than calling asic specific function. This
 * should also allow to remove a bunch of callback function
 * like vram_info.
 */
int cayman_init(struct radeon_device *rdev)
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
	ni_init_golden_registers(rdev);
	/* Initialize scratch registers */
	r600_scratch_init(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	/* Initialize clocks */
	radeon_get_clock_info(rdev->ddev);
	/* Fence driver */
	r = radeon_fence_driver_init(rdev);
	if (r)
		return r;
	/* initialize memory controller */
	r = evergreen_mc_init(rdev);
	if (r)
		return r;
	/* Memory manager */
	r = radeon_bo_init(rdev);
	if (r)
		return r;

	if (rdev->flags & RADEON_IS_IGP) {
		if (!rdev->me_fw || !rdev->pfp_fw || !rdev->rlc_fw) {
			r = ni_init_microcode(rdev);
			if (r) {
				DRM_ERROR("Failed to load firmware!\n");
				return r;
			}
		}
	} else {
		if (!rdev->me_fw || !rdev->pfp_fw || !rdev->rlc_fw || !rdev->mc_fw) {
			r = ni_init_microcode(rdev);
			if (r) {
				DRM_ERROR("Failed to load firmware!\n");
				return r;
			}
		}
	}

	/* Initialize power management */
	radeon_pm_init(rdev);

	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 1024 * 1024);

	ring = &rdev->ring[R600_RING_TYPE_DMA_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 64 * 1024);

	ring = &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 64 * 1024);

	cayman_uvd_init(rdev);
	cayman_vce_init(rdev);

	rdev->ih.ring_obj = NULL;
	r600_ih_ring_init(rdev, 64 * 1024);

	r = r600_pcie_gart_init(rdev);
	if (r)
		return r;

	rdev->accel_working = true;
	r = cayman_startup(rdev);
	if (r) {
		dev_err(rdev->dev, "disabling GPU acceleration\n");
		cayman_cp_fini(rdev);
		cayman_dma_fini(rdev);
		r600_irq_fini(rdev);
		if (rdev->flags & RADEON_IS_IGP)
			sumo_rlc_fini(rdev);
		radeon_wb_fini(rdev);
		radeon_ib_pool_fini(rdev);
		radeon_vm_manager_fini(rdev);
		radeon_irq_kms_fini(rdev);
		cayman_pcie_gart_fini(rdev);
		rdev->accel_working = false;
	}

	/* Don't start up if the MC ucode is missing.
	 * The default clocks and voltages before the MC ucode
	 * is loaded are not suffient for advanced operations.
	 *
	 * We can skip this check for TN, because there is no MC
	 * ucode.
	 */
	if (!rdev->mc_fw && !(rdev->flags & RADEON_IS_IGP)) {
		DRM_ERROR("radeon: MC ucode required for NI+.\n");
		return -EINVAL;
	}

	return 0;
}

void cayman_fini(struct radeon_device *rdev)
{
	radeon_pm_fini(rdev);
	cayman_cp_fini(rdev);
	cayman_dma_fini(rdev);
	r600_irq_fini(rdev);
	if (rdev->flags & RADEON_IS_IGP)
		sumo_rlc_fini(rdev);
	radeon_wb_fini(rdev);
	radeon_vm_manager_fini(rdev);
	radeon_ib_pool_fini(rdev);
	radeon_irq_kms_fini(rdev);
	uvd_v1_0_fini(rdev);
	radeon_uvd_fini(rdev);
	if (rdev->has_vce)
		radeon_vce_fini(rdev);
	cayman_pcie_gart_fini(rdev);
	r600_vram_scratch_fini(rdev);
	radeon_gem_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_bo_fini(rdev);
	radeon_atombios_fini(rdev);
	kfree(rdev->bios);
	rdev->bios = NULL;
}

/*
 * vm
 */
int cayman_vm_init(struct radeon_device *rdev)
{
	/* number of VMs */
	rdev->vm_manager.nvm = 8;
	/* base offset of vram pages */
	if (rdev->flags & RADEON_IS_IGP) {
		u64 tmp = RREG32(FUS_MC_VM_FB_OFFSET);
		tmp <<= 22;
		rdev->vm_manager.vram_base_offset = tmp;
	} else
		rdev->vm_manager.vram_base_offset = 0;
	return 0;
}

void cayman_vm_fini(struct radeon_device *rdev)
{
}

/**
 * cayman_vm_decode_fault - print human readable fault info
 *
 * @rdev: radeon_device pointer
 * @status: VM_CONTEXT1_PROTECTION_FAULT_STATUS register value
 * @addr: VM_CONTEXT1_PROTECTION_FAULT_ADDR register value
 *
 * Print human readable fault information (cayman/TN).
 */
void cayman_vm_decode_fault(struct radeon_device *rdev,
			    u32 status, u32 addr)
{
	u32 mc_id = (status & MEMORY_CLIENT_ID_MASK) >> MEMORY_CLIENT_ID_SHIFT;
	u32 vmid = (status & FAULT_VMID_MASK) >> FAULT_VMID_SHIFT;
	u32 protections = (status & PROTECTIONS_MASK) >> PROTECTIONS_SHIFT;
	char *block;

	switch (mc_id) {
	case 32:
	case 16:
	case 96:
	case 80:
	case 160:
	case 144:
	case 224:
	case 208:
		block = "CB";
		break;
	case 33:
	case 17:
	case 97:
	case 81:
	case 161:
	case 145:
	case 225:
	case 209:
		block = "CB_FMASK";
		break;
	case 34:
	case 18:
	case 98:
	case 82:
	case 162:
	case 146:
	case 226:
	case 210:
		block = "CB_CMASK";
		break;
	case 35:
	case 19:
	case 99:
	case 83:
	case 163:
	case 147:
	case 227:
	case 211:
		block = "CB_IMMED";
		break;
	case 36:
	case 20:
	case 100:
	case 84:
	case 164:
	case 148:
	case 228:
	case 212:
		block = "DB";
		break;
	case 37:
	case 21:
	case 101:
	case 85:
	case 165:
	case 149:
	case 229:
	case 213:
		block = "DB_HTILE";
		break;
	case 38:
	case 22:
	case 102:
	case 86:
	case 166:
	case 150:
	case 230:
	case 214:
		block = "SX";
		break;
	case 39:
	case 23:
	case 103:
	case 87:
	case 167:
	case 151:
	case 231:
	case 215:
		block = "DB_STEN";
		break;
	case 40:
	case 24:
	case 104:
	case 88:
	case 232:
	case 216:
	case 168:
	case 152:
		block = "TC_TFETCH";
		break;
	case 41:
	case 25:
	case 105:
	case 89:
	case 233:
	case 217:
	case 169:
	case 153:
		block = "TC_VFETCH";
		break;
	case 42:
	case 26:
	case 106:
	case 90:
	case 234:
	case 218:
	case 170:
	case 154:
		block = "VC";
		break;
	case 112:
		block = "CP";
		break;
	case 113:
	case 114:
		block = "SH";
		break;
	case 115:
		block = "VGT";
		break;
	case 178:
		block = "IH";
		break;
	case 51:
		block = "RLC";
		break;
	case 55:
		block = "DMA";
		break;
	case 56:
		block = "HDP";
		break;
	default:
		block = "unknown";
		break;
	}

	printk("VM fault (0x%02x, vmid %d) at page %u, %s from %s (%d)\n",
	       protections, vmid, addr,
	       (status & MEMORY_CLIENT_RW_MASK) ? "write" : "read",
	       block, mc_id);
}

/**
 * cayman_vm_flush - vm flush using the CP
 *
 * @rdev: radeon_device pointer
 *
 * Update the page table base and flush the VM TLB
 * using the CP (cayman-si).
 */
void cayman_vm_flush(struct radeon_device *rdev, struct radeon_ring *ring,
		     unsigned vm_id, uint64_t pd_addr)
{
	radeon_ring_write(ring, PACKET0(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (vm_id << 2), 0));
	radeon_ring_write(ring, pd_addr >> 12);

	/* flush hdp cache */
	radeon_ring_write(ring, PACKET0(HDP_MEM_COHERENCY_FLUSH_CNTL, 0));
	radeon_ring_write(ring, 0x1);

	/* bits 0-7 are the VM contexts0-7 */
	radeon_ring_write(ring, PACKET0(VM_INVALIDATE_REQUEST, 0));
	radeon_ring_write(ring, 1 << vm_id);

	/* wait for the invalidate to complete */
	radeon_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	radeon_ring_write(ring, (WAIT_REG_MEM_FUNCTION(0) |  /* always */
				 WAIT_REG_MEM_ENGINE(0))); /* me */
	radeon_ring_write(ring, VM_INVALIDATE_REQUEST >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 0); /* ref */
	radeon_ring_write(ring, 0); /* mask */
	radeon_ring_write(ring, 0x20); /* poll interval */

	/* sync PFP to ME, otherwise we might get invalid PFP reads */
	radeon_ring_write(ring, PACKET3(PACKET3_PFP_SYNC_ME, 0));
	radeon_ring_write(ring, 0x0);
}

int tn_set_vce_clocks(struct radeon_device *rdev, u32 evclk, u32 ecclk)
{
	struct atom_clock_dividers dividers;
	int r, i;

	r = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
					   ecclk, false, &dividers);
	if (r)
		return r;

	for (i = 0; i < 100; i++) {
		if (RREG32(CG_ECLK_STATUS) & ECLK_STATUS)
			break;
		mdelay(10);
	}
	if (i == 100)
		return -ETIMEDOUT;

	WREG32_P(CG_ECLK_CNTL, dividers.post_div, ~(ECLK_DIR_CNTL_EN|ECLK_DIVIDER_MASK));

	for (i = 0; i < 100; i++) {
		if (RREG32(CG_ECLK_STATUS) & ECLK_STATUS)
			break;
		mdelay(10);
	}
	if (i == 100)
		return -ETIMEDOUT;

	return 0;
}
