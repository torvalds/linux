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
 * Authors: Alex Deucher
 */
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/module.h>
#include "drmP.h"
#include "radeon.h"
#include "radeon_asic.h"
#include "radeon_audio.h"
#include "cikd.h"
#include "atom.h"
#include "cik_blit_shaders.h"
#include "radeon_ucode.h"
#include "clearstate_ci.h"
#include "radeon_kfd.h"

MODULE_FIRMWARE("radeon/BONAIRE_pfp.bin");
MODULE_FIRMWARE("radeon/BONAIRE_me.bin");
MODULE_FIRMWARE("radeon/BONAIRE_ce.bin");
MODULE_FIRMWARE("radeon/BONAIRE_mec.bin");
MODULE_FIRMWARE("radeon/BONAIRE_mc.bin");
MODULE_FIRMWARE("radeon/BONAIRE_mc2.bin");
MODULE_FIRMWARE("radeon/BONAIRE_rlc.bin");
MODULE_FIRMWARE("radeon/BONAIRE_sdma.bin");
MODULE_FIRMWARE("radeon/BONAIRE_smc.bin");

MODULE_FIRMWARE("radeon/bonaire_pfp.bin");
MODULE_FIRMWARE("radeon/bonaire_me.bin");
MODULE_FIRMWARE("radeon/bonaire_ce.bin");
MODULE_FIRMWARE("radeon/bonaire_mec.bin");
MODULE_FIRMWARE("radeon/bonaire_mc.bin");
MODULE_FIRMWARE("radeon/bonaire_rlc.bin");
MODULE_FIRMWARE("radeon/bonaire_sdma.bin");
MODULE_FIRMWARE("radeon/bonaire_smc.bin");

MODULE_FIRMWARE("radeon/HAWAII_pfp.bin");
MODULE_FIRMWARE("radeon/HAWAII_me.bin");
MODULE_FIRMWARE("radeon/HAWAII_ce.bin");
MODULE_FIRMWARE("radeon/HAWAII_mec.bin");
MODULE_FIRMWARE("radeon/HAWAII_mc.bin");
MODULE_FIRMWARE("radeon/HAWAII_mc2.bin");
MODULE_FIRMWARE("radeon/HAWAII_rlc.bin");
MODULE_FIRMWARE("radeon/HAWAII_sdma.bin");
MODULE_FIRMWARE("radeon/HAWAII_smc.bin");

MODULE_FIRMWARE("radeon/hawaii_pfp.bin");
MODULE_FIRMWARE("radeon/hawaii_me.bin");
MODULE_FIRMWARE("radeon/hawaii_ce.bin");
MODULE_FIRMWARE("radeon/hawaii_mec.bin");
MODULE_FIRMWARE("radeon/hawaii_mc.bin");
MODULE_FIRMWARE("radeon/hawaii_rlc.bin");
MODULE_FIRMWARE("radeon/hawaii_sdma.bin");
MODULE_FIRMWARE("radeon/hawaii_smc.bin");

MODULE_FIRMWARE("radeon/KAVERI_pfp.bin");
MODULE_FIRMWARE("radeon/KAVERI_me.bin");
MODULE_FIRMWARE("radeon/KAVERI_ce.bin");
MODULE_FIRMWARE("radeon/KAVERI_mec.bin");
MODULE_FIRMWARE("radeon/KAVERI_rlc.bin");
MODULE_FIRMWARE("radeon/KAVERI_sdma.bin");

MODULE_FIRMWARE("radeon/kaveri_pfp.bin");
MODULE_FIRMWARE("radeon/kaveri_me.bin");
MODULE_FIRMWARE("radeon/kaveri_ce.bin");
MODULE_FIRMWARE("radeon/kaveri_mec.bin");
MODULE_FIRMWARE("radeon/kaveri_mec2.bin");
MODULE_FIRMWARE("radeon/kaveri_rlc.bin");
MODULE_FIRMWARE("radeon/kaveri_sdma.bin");

MODULE_FIRMWARE("radeon/KABINI_pfp.bin");
MODULE_FIRMWARE("radeon/KABINI_me.bin");
MODULE_FIRMWARE("radeon/KABINI_ce.bin");
MODULE_FIRMWARE("radeon/KABINI_mec.bin");
MODULE_FIRMWARE("radeon/KABINI_rlc.bin");
MODULE_FIRMWARE("radeon/KABINI_sdma.bin");

MODULE_FIRMWARE("radeon/kabini_pfp.bin");
MODULE_FIRMWARE("radeon/kabini_me.bin");
MODULE_FIRMWARE("radeon/kabini_ce.bin");
MODULE_FIRMWARE("radeon/kabini_mec.bin");
MODULE_FIRMWARE("radeon/kabini_rlc.bin");
MODULE_FIRMWARE("radeon/kabini_sdma.bin");

MODULE_FIRMWARE("radeon/MULLINS_pfp.bin");
MODULE_FIRMWARE("radeon/MULLINS_me.bin");
MODULE_FIRMWARE("radeon/MULLINS_ce.bin");
MODULE_FIRMWARE("radeon/MULLINS_mec.bin");
MODULE_FIRMWARE("radeon/MULLINS_rlc.bin");
MODULE_FIRMWARE("radeon/MULLINS_sdma.bin");

MODULE_FIRMWARE("radeon/mullins_pfp.bin");
MODULE_FIRMWARE("radeon/mullins_me.bin");
MODULE_FIRMWARE("radeon/mullins_ce.bin");
MODULE_FIRMWARE("radeon/mullins_mec.bin");
MODULE_FIRMWARE("radeon/mullins_rlc.bin");
MODULE_FIRMWARE("radeon/mullins_sdma.bin");

extern int r600_ih_ring_alloc(struct radeon_device *rdev);
extern void r600_ih_ring_fini(struct radeon_device *rdev);
extern void evergreen_mc_stop(struct radeon_device *rdev, struct evergreen_mc_save *save);
extern void evergreen_mc_resume(struct radeon_device *rdev, struct evergreen_mc_save *save);
extern bool evergreen_is_display_hung(struct radeon_device *rdev);
extern void sumo_rlc_fini(struct radeon_device *rdev);
extern int sumo_rlc_init(struct radeon_device *rdev);
extern void si_vram_gtt_location(struct radeon_device *rdev, struct radeon_mc *mc);
extern void si_rlc_reset(struct radeon_device *rdev);
extern void si_init_uvd_internal_cg(struct radeon_device *rdev);
static u32 cik_get_cu_active_bitmap(struct radeon_device *rdev, u32 se, u32 sh);
extern int cik_sdma_resume(struct radeon_device *rdev);
extern void cik_sdma_enable(struct radeon_device *rdev, bool enable);
extern void cik_sdma_fini(struct radeon_device *rdev);
extern void vce_v2_0_enable_mgcg(struct radeon_device *rdev, bool enable);
static void cik_rlc_stop(struct radeon_device *rdev);
static void cik_pcie_gen3_enable(struct radeon_device *rdev);
static void cik_program_aspm(struct radeon_device *rdev);
static void cik_init_pg(struct radeon_device *rdev);
static void cik_init_cg(struct radeon_device *rdev);
static void cik_fini_pg(struct radeon_device *rdev);
static void cik_fini_cg(struct radeon_device *rdev);
static void cik_enable_gui_idle_interrupt(struct radeon_device *rdev,
					  bool enable);

/**
 * cik_get_allowed_info_register - fetch the register for the info ioctl
 *
 * @rdev: radeon_device pointer
 * @reg: register offset in bytes
 * @val: register value
 *
 * Returns 0 for success or -EINVAL for an invalid register
 *
 */
int cik_get_allowed_info_register(struct radeon_device *rdev,
				  u32 reg, u32 *val)
{
	switch (reg) {
	case GRBM_STATUS:
	case GRBM_STATUS2:
	case GRBM_STATUS_SE0:
	case GRBM_STATUS_SE1:
	case GRBM_STATUS_SE2:
	case GRBM_STATUS_SE3:
	case SRBM_STATUS:
	case SRBM_STATUS2:
	case (SDMA0_STATUS_REG + SDMA0_REGISTER_OFFSET):
	case (SDMA0_STATUS_REG + SDMA1_REGISTER_OFFSET):
	case UVD_STATUS:
	/* TODO VCE */
		*val = RREG32(reg);
		return 0;
	default:
		return -EINVAL;
	}
}

/* get temperature in millidegrees */
int ci_get_temp(struct radeon_device *rdev)
{
	u32 temp;
	int actual_temp = 0;

	temp = (RREG32_SMC(CG_MULT_THERMAL_STATUS) & CTF_TEMP_MASK) >>
		CTF_TEMP_SHIFT;

	if (temp & 0x200)
		actual_temp = 255;
	else
		actual_temp = temp & 0x1ff;

	actual_temp = actual_temp * 1000;

	return actual_temp;
}

/* get temperature in millidegrees */
int kv_get_temp(struct radeon_device *rdev)
{
	u32 temp;
	int actual_temp = 0;

	temp = RREG32_SMC(0xC0300E0C);

	if (temp)
		actual_temp = (temp / 8) - 49;
	else
		actual_temp = 0;

	actual_temp = actual_temp * 1000;

	return actual_temp;
}

/*
 * Indirect registers accessor
 */
u32 cik_pciep_rreg(struct radeon_device *rdev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&rdev->pciep_idx_lock, flags);
	WREG32(PCIE_INDEX, reg);
	(void)RREG32(PCIE_INDEX);
	r = RREG32(PCIE_DATA);
	spin_unlock_irqrestore(&rdev->pciep_idx_lock, flags);
	return r;
}

void cik_pciep_wreg(struct radeon_device *rdev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&rdev->pciep_idx_lock, flags);
	WREG32(PCIE_INDEX, reg);
	(void)RREG32(PCIE_INDEX);
	WREG32(PCIE_DATA, v);
	(void)RREG32(PCIE_DATA);
	spin_unlock_irqrestore(&rdev->pciep_idx_lock, flags);
}

static const u32 spectre_rlc_save_restore_register_list[] =
{
	(0x0e00 << 16) | (0xc12c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc140 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc150 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc15c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc168 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc170 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc178 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc204 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2b4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2b8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2bc >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2c0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8228 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x829c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x869c >> 2),
	0x00000000,
	(0x0600 << 16) | (0x98f4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x98f8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x9900 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc260 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x90e8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3c000 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3c00c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8c1c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x9700 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xcd20 >> 2),
	0x00000000,
	(0x4e00 << 16) | (0xcd20 >> 2),
	0x00000000,
	(0x5e00 << 16) | (0xcd20 >> 2),
	0x00000000,
	(0x6e00 << 16) | (0xcd20 >> 2),
	0x00000000,
	(0x7e00 << 16) | (0xcd20 >> 2),
	0x00000000,
	(0x8e00 << 16) | (0xcd20 >> 2),
	0x00000000,
	(0x9e00 << 16) | (0xcd20 >> 2),
	0x00000000,
	(0xae00 << 16) | (0xcd20 >> 2),
	0x00000000,
	(0xbe00 << 16) | (0xcd20 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x89bc >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8900 >> 2),
	0x00000000,
	0x3,
	(0x0e00 << 16) | (0xc130 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc134 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc1fc >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc208 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc264 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc268 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc26c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc270 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc274 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc278 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc27c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc280 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc284 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc288 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc28c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc290 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc294 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc298 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc29c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2a0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2a4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2a8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2ac  >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2b0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x301d0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30238 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30250 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30254 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30258 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3025c >> 2),
	0x00000000,
	(0x4e00 << 16) | (0xc900 >> 2),
	0x00000000,
	(0x5e00 << 16) | (0xc900 >> 2),
	0x00000000,
	(0x6e00 << 16) | (0xc900 >> 2),
	0x00000000,
	(0x7e00 << 16) | (0xc900 >> 2),
	0x00000000,
	(0x8e00 << 16) | (0xc900 >> 2),
	0x00000000,
	(0x9e00 << 16) | (0xc900 >> 2),
	0x00000000,
	(0xae00 << 16) | (0xc900 >> 2),
	0x00000000,
	(0xbe00 << 16) | (0xc900 >> 2),
	0x00000000,
	(0x4e00 << 16) | (0xc904 >> 2),
	0x00000000,
	(0x5e00 << 16) | (0xc904 >> 2),
	0x00000000,
	(0x6e00 << 16) | (0xc904 >> 2),
	0x00000000,
	(0x7e00 << 16) | (0xc904 >> 2),
	0x00000000,
	(0x8e00 << 16) | (0xc904 >> 2),
	0x00000000,
	(0x9e00 << 16) | (0xc904 >> 2),
	0x00000000,
	(0xae00 << 16) | (0xc904 >> 2),
	0x00000000,
	(0xbe00 << 16) | (0xc904 >> 2),
	0x00000000,
	(0x4e00 << 16) | (0xc908 >> 2),
	0x00000000,
	(0x5e00 << 16) | (0xc908 >> 2),
	0x00000000,
	(0x6e00 << 16) | (0xc908 >> 2),
	0x00000000,
	(0x7e00 << 16) | (0xc908 >> 2),
	0x00000000,
	(0x8e00 << 16) | (0xc908 >> 2),
	0x00000000,
	(0x9e00 << 16) | (0xc908 >> 2),
	0x00000000,
	(0xae00 << 16) | (0xc908 >> 2),
	0x00000000,
	(0xbe00 << 16) | (0xc908 >> 2),
	0x00000000,
	(0x4e00 << 16) | (0xc90c >> 2),
	0x00000000,
	(0x5e00 << 16) | (0xc90c >> 2),
	0x00000000,
	(0x6e00 << 16) | (0xc90c >> 2),
	0x00000000,
	(0x7e00 << 16) | (0xc90c >> 2),
	0x00000000,
	(0x8e00 << 16) | (0xc90c >> 2),
	0x00000000,
	(0x9e00 << 16) | (0xc90c >> 2),
	0x00000000,
	(0xae00 << 16) | (0xc90c >> 2),
	0x00000000,
	(0xbe00 << 16) | (0xc90c >> 2),
	0x00000000,
	(0x4e00 << 16) | (0xc910 >> 2),
	0x00000000,
	(0x5e00 << 16) | (0xc910 >> 2),
	0x00000000,
	(0x6e00 << 16) | (0xc910 >> 2),
	0x00000000,
	(0x7e00 << 16) | (0xc910 >> 2),
	0x00000000,
	(0x8e00 << 16) | (0xc910 >> 2),
	0x00000000,
	(0x9e00 << 16) | (0xc910 >> 2),
	0x00000000,
	(0xae00 << 16) | (0xc910 >> 2),
	0x00000000,
	(0xbe00 << 16) | (0xc910 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc99c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x9834 >> 2),
	0x00000000,
	(0x0000 << 16) | (0x30f00 >> 2),
	0x00000000,
	(0x0001 << 16) | (0x30f00 >> 2),
	0x00000000,
	(0x0000 << 16) | (0x30f04 >> 2),
	0x00000000,
	(0x0001 << 16) | (0x30f04 >> 2),
	0x00000000,
	(0x0000 << 16) | (0x30f08 >> 2),
	0x00000000,
	(0x0001 << 16) | (0x30f08 >> 2),
	0x00000000,
	(0x0000 << 16) | (0x30f0c >> 2),
	0x00000000,
	(0x0001 << 16) | (0x30f0c >> 2),
	0x00000000,
	(0x0600 << 16) | (0x9b7c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8a14 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8a18 >> 2),
	0x00000000,
	(0x0600 << 16) | (0x30a00 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8bf0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8bcc >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8b24 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30a04 >> 2),
	0x00000000,
	(0x0600 << 16) | (0x30a10 >> 2),
	0x00000000,
	(0x0600 << 16) | (0x30a14 >> 2),
	0x00000000,
	(0x0600 << 16) | (0x30a18 >> 2),
	0x00000000,
	(0x0600 << 16) | (0x30a2c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc700 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc704 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc708 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc768 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc770 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc774 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc778 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc77c >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc780 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc784 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc788 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc78c >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc798 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc79c >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc7a0 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc7a4 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc7a8 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc7ac >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc7b0 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc7b4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x9100 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3c010 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x92a8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x92ac >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x92b4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x92b8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x92bc >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x92c0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x92c4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x92c8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x92cc >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x92d0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8c00 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8c04 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8c20 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8c38 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8c3c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xae00 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x9604 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac08 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac0c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac10 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac14 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac58 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac68 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac6c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac70 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac74 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac78 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac7c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac80 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac84 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac88 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac8c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x970c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x9714 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x9718 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x971c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x31068 >> 2),
	0x00000000,
	(0x4e00 << 16) | (0x31068 >> 2),
	0x00000000,
	(0x5e00 << 16) | (0x31068 >> 2),
	0x00000000,
	(0x6e00 << 16) | (0x31068 >> 2),
	0x00000000,
	(0x7e00 << 16) | (0x31068 >> 2),
	0x00000000,
	(0x8e00 << 16) | (0x31068 >> 2),
	0x00000000,
	(0x9e00 << 16) | (0x31068 >> 2),
	0x00000000,
	(0xae00 << 16) | (0x31068 >> 2),
	0x00000000,
	(0xbe00 << 16) | (0x31068 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xcd10 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xcd14 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88b0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88b4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88b8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88bc >> 2),
	0x00000000,
	(0x0400 << 16) | (0x89c0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88c4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88c8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88d0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88d4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88d8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8980 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30938 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3093c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30940 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x89a0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30900 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30904 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x89b4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3c210 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3c214 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3c218 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8904 >> 2),
	0x00000000,
	0x5,
	(0x0e00 << 16) | (0x8c28 >> 2),
	(0x0e00 << 16) | (0x8c2c >> 2),
	(0x0e00 << 16) | (0x8c30 >> 2),
	(0x0e00 << 16) | (0x8c34 >> 2),
	(0x0e00 << 16) | (0x9600 >> 2),
};

static const u32 kalindi_rlc_save_restore_register_list[] =
{
	(0x0e00 << 16) | (0xc12c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc140 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc150 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc15c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc168 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc170 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc204 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2b4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2b8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2bc >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2c0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8228 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x829c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x869c >> 2),
	0x00000000,
	(0x0600 << 16) | (0x98f4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x98f8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x9900 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc260 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x90e8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3c000 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3c00c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8c1c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x9700 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xcd20 >> 2),
	0x00000000,
	(0x4e00 << 16) | (0xcd20 >> 2),
	0x00000000,
	(0x5e00 << 16) | (0xcd20 >> 2),
	0x00000000,
	(0x6e00 << 16) | (0xcd20 >> 2),
	0x00000000,
	(0x7e00 << 16) | (0xcd20 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x89bc >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8900 >> 2),
	0x00000000,
	0x3,
	(0x0e00 << 16) | (0xc130 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc134 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc1fc >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc208 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc264 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc268 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc26c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc270 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc274 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc28c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc290 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc294 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc298 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2a0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2a4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2a8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc2ac >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x301d0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30238 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30250 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30254 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30258 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3025c >> 2),
	0x00000000,
	(0x4e00 << 16) | (0xc900 >> 2),
	0x00000000,
	(0x5e00 << 16) | (0xc900 >> 2),
	0x00000000,
	(0x6e00 << 16) | (0xc900 >> 2),
	0x00000000,
	(0x7e00 << 16) | (0xc900 >> 2),
	0x00000000,
	(0x4e00 << 16) | (0xc904 >> 2),
	0x00000000,
	(0x5e00 << 16) | (0xc904 >> 2),
	0x00000000,
	(0x6e00 << 16) | (0xc904 >> 2),
	0x00000000,
	(0x7e00 << 16) | (0xc904 >> 2),
	0x00000000,
	(0x4e00 << 16) | (0xc908 >> 2),
	0x00000000,
	(0x5e00 << 16) | (0xc908 >> 2),
	0x00000000,
	(0x6e00 << 16) | (0xc908 >> 2),
	0x00000000,
	(0x7e00 << 16) | (0xc908 >> 2),
	0x00000000,
	(0x4e00 << 16) | (0xc90c >> 2),
	0x00000000,
	(0x5e00 << 16) | (0xc90c >> 2),
	0x00000000,
	(0x6e00 << 16) | (0xc90c >> 2),
	0x00000000,
	(0x7e00 << 16) | (0xc90c >> 2),
	0x00000000,
	(0x4e00 << 16) | (0xc910 >> 2),
	0x00000000,
	(0x5e00 << 16) | (0xc910 >> 2),
	0x00000000,
	(0x6e00 << 16) | (0xc910 >> 2),
	0x00000000,
	(0x7e00 << 16) | (0xc910 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc99c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x9834 >> 2),
	0x00000000,
	(0x0000 << 16) | (0x30f00 >> 2),
	0x00000000,
	(0x0000 << 16) | (0x30f04 >> 2),
	0x00000000,
	(0x0000 << 16) | (0x30f08 >> 2),
	0x00000000,
	(0x0000 << 16) | (0x30f0c >> 2),
	0x00000000,
	(0x0600 << 16) | (0x9b7c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8a14 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8a18 >> 2),
	0x00000000,
	(0x0600 << 16) | (0x30a00 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8bf0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8bcc >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8b24 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30a04 >> 2),
	0x00000000,
	(0x0600 << 16) | (0x30a10 >> 2),
	0x00000000,
	(0x0600 << 16) | (0x30a14 >> 2),
	0x00000000,
	(0x0600 << 16) | (0x30a18 >> 2),
	0x00000000,
	(0x0600 << 16) | (0x30a2c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc700 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc704 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc708 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xc768 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc770 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc774 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc798 >> 2),
	0x00000000,
	(0x0400 << 16) | (0xc79c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x9100 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3c010 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8c00 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8c04 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8c20 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8c38 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8c3c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xae00 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x9604 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac08 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac0c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac10 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac14 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac58 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac68 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac6c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac70 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac74 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac78 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac7c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac80 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac84 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac88 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xac8c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x970c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x9714 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x9718 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x971c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x31068 >> 2),
	0x00000000,
	(0x4e00 << 16) | (0x31068 >> 2),
	0x00000000,
	(0x5e00 << 16) | (0x31068 >> 2),
	0x00000000,
	(0x6e00 << 16) | (0x31068 >> 2),
	0x00000000,
	(0x7e00 << 16) | (0x31068 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xcd10 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0xcd14 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88b0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88b4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88b8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88bc >> 2),
	0x00000000,
	(0x0400 << 16) | (0x89c0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88c4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88c8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88d0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88d4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x88d8 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8980 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30938 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3093c >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30940 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x89a0 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30900 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x30904 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x89b4 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3e1fc >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3c210 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3c214 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x3c218 >> 2),
	0x00000000,
	(0x0e00 << 16) | (0x8904 >> 2),
	0x00000000,
	0x5,
	(0x0e00 << 16) | (0x8c28 >> 2),
	(0x0e00 << 16) | (0x8c2c >> 2),
	(0x0e00 << 16) | (0x8c30 >> 2),
	(0x0e00 << 16) | (0x8c34 >> 2),
	(0x0e00 << 16) | (0x9600 >> 2),
};

static const u32 bonaire_golden_spm_registers[] =
{
	0x30800, 0xe0ffffff, 0xe0000000
};

static const u32 bonaire_golden_common_registers[] =
{
	0xc770, 0xffffffff, 0x00000800,
	0xc774, 0xffffffff, 0x00000800,
	0xc798, 0xffffffff, 0x00007fbf,
	0xc79c, 0xffffffff, 0x00007faf
};

static const u32 bonaire_golden_registers[] =
{
	0x3354, 0x00000333, 0x00000333,
	0x3350, 0x000c0fc0, 0x00040200,
	0x9a10, 0x00010000, 0x00058208,
	0x3c000, 0xffff1fff, 0x00140000,
	0x3c200, 0xfdfc0fff, 0x00000100,
	0x3c234, 0x40000000, 0x40000200,
	0x9830, 0xffffffff, 0x00000000,
	0x9834, 0xf00fffff, 0x00000400,
	0x9838, 0x0002021c, 0x00020200,
	0xc78, 0x00000080, 0x00000000,
	0x5bb0, 0x000000f0, 0x00000070,
	0x5bc0, 0xf0311fff, 0x80300000,
	0x98f8, 0x73773777, 0x12010001,
	0x350c, 0x00810000, 0x408af000,
	0x7030, 0x31000111, 0x00000011,
	0x2f48, 0x73773777, 0x12010001,
	0x220c, 0x00007fb6, 0x0021a1b1,
	0x2210, 0x00007fb6, 0x002021b1,
	0x2180, 0x00007fb6, 0x00002191,
	0x2218, 0x00007fb6, 0x002121b1,
	0x221c, 0x00007fb6, 0x002021b1,
	0x21dc, 0x00007fb6, 0x00002191,
	0x21e0, 0x00007fb6, 0x00002191,
	0x3628, 0x0000003f, 0x0000000a,
	0x362c, 0x0000003f, 0x0000000a,
	0x2ae4, 0x00073ffe, 0x000022a2,
	0x240c, 0x000007ff, 0x00000000,
	0x8a14, 0xf000003f, 0x00000007,
	0x8bf0, 0x00002001, 0x00000001,
	0x8b24, 0xffffffff, 0x00ffffff,
	0x30a04, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x06000000,
	0x4d8, 0x00000fff, 0x00000100,
	0x3e78, 0x00000001, 0x00000002,
	0x9100, 0x03000000, 0x0362c688,
	0x8c00, 0x000000ff, 0x00000001,
	0xe40, 0x00001fff, 0x00001fff,
	0x9060, 0x0000007f, 0x00000020,
	0x9508, 0x00010000, 0x00010000,
	0xac14, 0x000003ff, 0x000000f3,
	0xac0c, 0xffffffff, 0x00001032
};

static const u32 bonaire_mgcg_cgcg_init[] =
{
	0xc420, 0xffffffff, 0xfffffffc,
	0x30800, 0xffffffff, 0xe0000000,
	0x3c2a0, 0xffffffff, 0x00000100,
	0x3c208, 0xffffffff, 0x00000100,
	0x3c2c0, 0xffffffff, 0xc0000100,
	0x3c2c8, 0xffffffff, 0xc0000100,
	0x3c2c4, 0xffffffff, 0xc0000100,
	0x55e4, 0xffffffff, 0x00600100,
	0x3c280, 0xffffffff, 0x00000100,
	0x3c214, 0xffffffff, 0x06000100,
	0x3c220, 0xffffffff, 0x00000100,
	0x3c218, 0xffffffff, 0x06000100,
	0x3c204, 0xffffffff, 0x00000100,
	0x3c2e0, 0xffffffff, 0x00000100,
	0x3c224, 0xffffffff, 0x00000100,
	0x3c200, 0xffffffff, 0x00000100,
	0x3c230, 0xffffffff, 0x00000100,
	0x3c234, 0xffffffff, 0x00000100,
	0x3c250, 0xffffffff, 0x00000100,
	0x3c254, 0xffffffff, 0x00000100,
	0x3c258, 0xffffffff, 0x00000100,
	0x3c25c, 0xffffffff, 0x00000100,
	0x3c260, 0xffffffff, 0x00000100,
	0x3c27c, 0xffffffff, 0x00000100,
	0x3c278, 0xffffffff, 0x00000100,
	0x3c210, 0xffffffff, 0x06000100,
	0x3c290, 0xffffffff, 0x00000100,
	0x3c274, 0xffffffff, 0x00000100,
	0x3c2b4, 0xffffffff, 0x00000100,
	0x3c2b0, 0xffffffff, 0x00000100,
	0x3c270, 0xffffffff, 0x00000100,
	0x30800, 0xffffffff, 0xe0000000,
	0x3c020, 0xffffffff, 0x00010000,
	0x3c024, 0xffffffff, 0x00030002,
	0x3c028, 0xffffffff, 0x00040007,
	0x3c02c, 0xffffffff, 0x00060005,
	0x3c030, 0xffffffff, 0x00090008,
	0x3c034, 0xffffffff, 0x00010000,
	0x3c038, 0xffffffff, 0x00030002,
	0x3c03c, 0xffffffff, 0x00040007,
	0x3c040, 0xffffffff, 0x00060005,
	0x3c044, 0xffffffff, 0x00090008,
	0x3c048, 0xffffffff, 0x00010000,
	0x3c04c, 0xffffffff, 0x00030002,
	0x3c050, 0xffffffff, 0x00040007,
	0x3c054, 0xffffffff, 0x00060005,
	0x3c058, 0xffffffff, 0x00090008,
	0x3c05c, 0xffffffff, 0x00010000,
	0x3c060, 0xffffffff, 0x00030002,
	0x3c064, 0xffffffff, 0x00040007,
	0x3c068, 0xffffffff, 0x00060005,
	0x3c06c, 0xffffffff, 0x00090008,
	0x3c070, 0xffffffff, 0x00010000,
	0x3c074, 0xffffffff, 0x00030002,
	0x3c078, 0xffffffff, 0x00040007,
	0x3c07c, 0xffffffff, 0x00060005,
	0x3c080, 0xffffffff, 0x00090008,
	0x3c084, 0xffffffff, 0x00010000,
	0x3c088, 0xffffffff, 0x00030002,
	0x3c08c, 0xffffffff, 0x00040007,
	0x3c090, 0xffffffff, 0x00060005,
	0x3c094, 0xffffffff, 0x00090008,
	0x3c098, 0xffffffff, 0x00010000,
	0x3c09c, 0xffffffff, 0x00030002,
	0x3c0a0, 0xffffffff, 0x00040007,
	0x3c0a4, 0xffffffff, 0x00060005,
	0x3c0a8, 0xffffffff, 0x00090008,
	0x3c000, 0xffffffff, 0x96e00200,
	0x8708, 0xffffffff, 0x00900100,
	0xc424, 0xffffffff, 0x0020003f,
	0x38, 0xffffffff, 0x0140001c,
	0x3c, 0x000f0000, 0x000f0000,
	0x220, 0xffffffff, 0xC060000C,
	0x224, 0xc0000fff, 0x00000100,
	0xf90, 0xffffffff, 0x00000100,
	0xf98, 0x00000101, 0x00000000,
	0x20a8, 0xffffffff, 0x00000104,
	0x55e4, 0xff000fff, 0x00000100,
	0x30cc, 0xc0000fff, 0x00000104,
	0xc1e4, 0x00000001, 0x00000001,
	0xd00c, 0xff000ff0, 0x00000100,
	0xd80c, 0xff000ff0, 0x00000100
};

static const u32 spectre_golden_spm_registers[] =
{
	0x30800, 0xe0ffffff, 0xe0000000
};

static const u32 spectre_golden_common_registers[] =
{
	0xc770, 0xffffffff, 0x00000800,
	0xc774, 0xffffffff, 0x00000800,
	0xc798, 0xffffffff, 0x00007fbf,
	0xc79c, 0xffffffff, 0x00007faf
};

static const u32 spectre_golden_registers[] =
{
	0x3c000, 0xffff1fff, 0x96940200,
	0x3c00c, 0xffff0001, 0xff000000,
	0x3c200, 0xfffc0fff, 0x00000100,
	0x6ed8, 0x00010101, 0x00010000,
	0x9834, 0xf00fffff, 0x00000400,
	0x9838, 0xfffffffc, 0x00020200,
	0x5bb0, 0x000000f0, 0x00000070,
	0x5bc0, 0xf0311fff, 0x80300000,
	0x98f8, 0x73773777, 0x12010001,
	0x9b7c, 0x00ff0000, 0x00fc0000,
	0x2f48, 0x73773777, 0x12010001,
	0x8a14, 0xf000003f, 0x00000007,
	0x8b24, 0xffffffff, 0x00ffffff,
	0x28350, 0x3f3f3fff, 0x00000082,
	0x28354, 0x0000003f, 0x00000000,
	0x3e78, 0x00000001, 0x00000002,
	0x913c, 0xffff03df, 0x00000004,
	0xc768, 0x00000008, 0x00000008,
	0x8c00, 0x000008ff, 0x00000800,
	0x9508, 0x00010000, 0x00010000,
	0xac0c, 0xffffffff, 0x54763210,
	0x214f8, 0x01ff01ff, 0x00000002,
	0x21498, 0x007ff800, 0x00200000,
	0x2015c, 0xffffffff, 0x00000f40,
	0x30934, 0xffffffff, 0x00000001
};

static const u32 spectre_mgcg_cgcg_init[] =
{
	0xc420, 0xffffffff, 0xfffffffc,
	0x30800, 0xffffffff, 0xe0000000,
	0x3c2a0, 0xffffffff, 0x00000100,
	0x3c208, 0xffffffff, 0x00000100,
	0x3c2c0, 0xffffffff, 0x00000100,
	0x3c2c8, 0xffffffff, 0x00000100,
	0x3c2c4, 0xffffffff, 0x00000100,
	0x55e4, 0xffffffff, 0x00600100,
	0x3c280, 0xffffffff, 0x00000100,
	0x3c214, 0xffffffff, 0x06000100,
	0x3c220, 0xffffffff, 0x00000100,
	0x3c218, 0xffffffff, 0x06000100,
	0x3c204, 0xffffffff, 0x00000100,
	0x3c2e0, 0xffffffff, 0x00000100,
	0x3c224, 0xffffffff, 0x00000100,
	0x3c200, 0xffffffff, 0x00000100,
	0x3c230, 0xffffffff, 0x00000100,
	0x3c234, 0xffffffff, 0x00000100,
	0x3c250, 0xffffffff, 0x00000100,
	0x3c254, 0xffffffff, 0x00000100,
	0x3c258, 0xffffffff, 0x00000100,
	0x3c25c, 0xffffffff, 0x00000100,
	0x3c260, 0xffffffff, 0x00000100,
	0x3c27c, 0xffffffff, 0x00000100,
	0x3c278, 0xffffffff, 0x00000100,
	0x3c210, 0xffffffff, 0x06000100,
	0x3c290, 0xffffffff, 0x00000100,
	0x3c274, 0xffffffff, 0x00000100,
	0x3c2b4, 0xffffffff, 0x00000100,
	0x3c2b0, 0xffffffff, 0x00000100,
	0x3c270, 0xffffffff, 0x00000100,
	0x30800, 0xffffffff, 0xe0000000,
	0x3c020, 0xffffffff, 0x00010000,
	0x3c024, 0xffffffff, 0x00030002,
	0x3c028, 0xffffffff, 0x00040007,
	0x3c02c, 0xffffffff, 0x00060005,
	0x3c030, 0xffffffff, 0x00090008,
	0x3c034, 0xffffffff, 0x00010000,
	0x3c038, 0xffffffff, 0x00030002,
	0x3c03c, 0xffffffff, 0x00040007,
	0x3c040, 0xffffffff, 0x00060005,
	0x3c044, 0xffffffff, 0x00090008,
	0x3c048, 0xffffffff, 0x00010000,
	0x3c04c, 0xffffffff, 0x00030002,
	0x3c050, 0xffffffff, 0x00040007,
	0x3c054, 0xffffffff, 0x00060005,
	0x3c058, 0xffffffff, 0x00090008,
	0x3c05c, 0xffffffff, 0x00010000,
	0x3c060, 0xffffffff, 0x00030002,
	0x3c064, 0xffffffff, 0x00040007,
	0x3c068, 0xffffffff, 0x00060005,
	0x3c06c, 0xffffffff, 0x00090008,
	0x3c070, 0xffffffff, 0x00010000,
	0x3c074, 0xffffffff, 0x00030002,
	0x3c078, 0xffffffff, 0x00040007,
	0x3c07c, 0xffffffff, 0x00060005,
	0x3c080, 0xffffffff, 0x00090008,
	0x3c084, 0xffffffff, 0x00010000,
	0x3c088, 0xffffffff, 0x00030002,
	0x3c08c, 0xffffffff, 0x00040007,
	0x3c090, 0xffffffff, 0x00060005,
	0x3c094, 0xffffffff, 0x00090008,
	0x3c098, 0xffffffff, 0x00010000,
	0x3c09c, 0xffffffff, 0x00030002,
	0x3c0a0, 0xffffffff, 0x00040007,
	0x3c0a4, 0xffffffff, 0x00060005,
	0x3c0a8, 0xffffffff, 0x00090008,
	0x3c0ac, 0xffffffff, 0x00010000,
	0x3c0b0, 0xffffffff, 0x00030002,
	0x3c0b4, 0xffffffff, 0x00040007,
	0x3c0b8, 0xffffffff, 0x00060005,
	0x3c0bc, 0xffffffff, 0x00090008,
	0x3c000, 0xffffffff, 0x96e00200,
	0x8708, 0xffffffff, 0x00900100,
	0xc424, 0xffffffff, 0x0020003f,
	0x38, 0xffffffff, 0x0140001c,
	0x3c, 0x000f0000, 0x000f0000,
	0x220, 0xffffffff, 0xC060000C,
	0x224, 0xc0000fff, 0x00000100,
	0xf90, 0xffffffff, 0x00000100,
	0xf98, 0x00000101, 0x00000000,
	0x20a8, 0xffffffff, 0x00000104,
	0x55e4, 0xff000fff, 0x00000100,
	0x30cc, 0xc0000fff, 0x00000104,
	0xc1e4, 0x00000001, 0x00000001,
	0xd00c, 0xff000ff0, 0x00000100,
	0xd80c, 0xff000ff0, 0x00000100
};

static const u32 kalindi_golden_spm_registers[] =
{
	0x30800, 0xe0ffffff, 0xe0000000
};

static const u32 kalindi_golden_common_registers[] =
{
	0xc770, 0xffffffff, 0x00000800,
	0xc774, 0xffffffff, 0x00000800,
	0xc798, 0xffffffff, 0x00007fbf,
	0xc79c, 0xffffffff, 0x00007faf
};

static const u32 kalindi_golden_registers[] =
{
	0x3c000, 0xffffdfff, 0x6e944040,
	0x55e4, 0xff607fff, 0xfc000100,
	0x3c220, 0xff000fff, 0x00000100,
	0x3c224, 0xff000fff, 0x00000100,
	0x3c200, 0xfffc0fff, 0x00000100,
	0x6ed8, 0x00010101, 0x00010000,
	0x9830, 0xffffffff, 0x00000000,
	0x9834, 0xf00fffff, 0x00000400,
	0x5bb0, 0x000000f0, 0x00000070,
	0x5bc0, 0xf0311fff, 0x80300000,
	0x98f8, 0x73773777, 0x12010001,
	0x98fc, 0xffffffff, 0x00000010,
	0x9b7c, 0x00ff0000, 0x00fc0000,
	0x8030, 0x00001f0f, 0x0000100a,
	0x2f48, 0x73773777, 0x12010001,
	0x2408, 0x000fffff, 0x000c007f,
	0x8a14, 0xf000003f, 0x00000007,
	0x8b24, 0x3fff3fff, 0x00ffcfff,
	0x30a04, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x06000000,
	0x4d8, 0x00000fff, 0x00000100,
	0x3e78, 0x00000001, 0x00000002,
	0xc768, 0x00000008, 0x00000008,
	0x8c00, 0x000000ff, 0x00000003,
	0x214f8, 0x01ff01ff, 0x00000002,
	0x21498, 0x007ff800, 0x00200000,
	0x2015c, 0xffffffff, 0x00000f40,
	0x88c4, 0x001f3ae3, 0x00000082,
	0x88d4, 0x0000001f, 0x00000010,
	0x30934, 0xffffffff, 0x00000000
};

static const u32 kalindi_mgcg_cgcg_init[] =
{
	0xc420, 0xffffffff, 0xfffffffc,
	0x30800, 0xffffffff, 0xe0000000,
	0x3c2a0, 0xffffffff, 0x00000100,
	0x3c208, 0xffffffff, 0x00000100,
	0x3c2c0, 0xffffffff, 0x00000100,
	0x3c2c8, 0xffffffff, 0x00000100,
	0x3c2c4, 0xffffffff, 0x00000100,
	0x55e4, 0xffffffff, 0x00600100,
	0x3c280, 0xffffffff, 0x00000100,
	0x3c214, 0xffffffff, 0x06000100,
	0x3c220, 0xffffffff, 0x00000100,
	0x3c218, 0xffffffff, 0x06000100,
	0x3c204, 0xffffffff, 0x00000100,
	0x3c2e0, 0xffffffff, 0x00000100,
	0x3c224, 0xffffffff, 0x00000100,
	0x3c200, 0xffffffff, 0x00000100,
	0x3c230, 0xffffffff, 0x00000100,
	0x3c234, 0xffffffff, 0x00000100,
	0x3c250, 0xffffffff, 0x00000100,
	0x3c254, 0xffffffff, 0x00000100,
	0x3c258, 0xffffffff, 0x00000100,
	0x3c25c, 0xffffffff, 0x00000100,
	0x3c260, 0xffffffff, 0x00000100,
	0x3c27c, 0xffffffff, 0x00000100,
	0x3c278, 0xffffffff, 0x00000100,
	0x3c210, 0xffffffff, 0x06000100,
	0x3c290, 0xffffffff, 0x00000100,
	0x3c274, 0xffffffff, 0x00000100,
	0x3c2b4, 0xffffffff, 0x00000100,
	0x3c2b0, 0xffffffff, 0x00000100,
	0x3c270, 0xffffffff, 0x00000100,
	0x30800, 0xffffffff, 0xe0000000,
	0x3c020, 0xffffffff, 0x00010000,
	0x3c024, 0xffffffff, 0x00030002,
	0x3c028, 0xffffffff, 0x00040007,
	0x3c02c, 0xffffffff, 0x00060005,
	0x3c030, 0xffffffff, 0x00090008,
	0x3c034, 0xffffffff, 0x00010000,
	0x3c038, 0xffffffff, 0x00030002,
	0x3c03c, 0xffffffff, 0x00040007,
	0x3c040, 0xffffffff, 0x00060005,
	0x3c044, 0xffffffff, 0x00090008,
	0x3c000, 0xffffffff, 0x96e00200,
	0x8708, 0xffffffff, 0x00900100,
	0xc424, 0xffffffff, 0x0020003f,
	0x38, 0xffffffff, 0x0140001c,
	0x3c, 0x000f0000, 0x000f0000,
	0x220, 0xffffffff, 0xC060000C,
	0x224, 0xc0000fff, 0x00000100,
	0x20a8, 0xffffffff, 0x00000104,
	0x55e4, 0xff000fff, 0x00000100,
	0x30cc, 0xc0000fff, 0x00000104,
	0xc1e4, 0x00000001, 0x00000001,
	0xd00c, 0xff000ff0, 0x00000100,
	0xd80c, 0xff000ff0, 0x00000100
};

static const u32 hawaii_golden_spm_registers[] =
{
	0x30800, 0xe0ffffff, 0xe0000000
};

static const u32 hawaii_golden_common_registers[] =
{
	0x30800, 0xffffffff, 0xe0000000,
	0x28350, 0xffffffff, 0x3a00161a,
	0x28354, 0xffffffff, 0x0000002e,
	0x9a10, 0xffffffff, 0x00018208,
	0x98f8, 0xffffffff, 0x12011003
};

static const u32 hawaii_golden_registers[] =
{
	0x3354, 0x00000333, 0x00000333,
	0x9a10, 0x00010000, 0x00058208,
	0x9830, 0xffffffff, 0x00000000,
	0x9834, 0xf00fffff, 0x00000400,
	0x9838, 0x0002021c, 0x00020200,
	0xc78, 0x00000080, 0x00000000,
	0x5bb0, 0x000000f0, 0x00000070,
	0x5bc0, 0xf0311fff, 0x80300000,
	0x350c, 0x00810000, 0x408af000,
	0x7030, 0x31000111, 0x00000011,
	0x2f48, 0x73773777, 0x12010001,
	0x2120, 0x0000007f, 0x0000001b,
	0x21dc, 0x00007fb6, 0x00002191,
	0x3628, 0x0000003f, 0x0000000a,
	0x362c, 0x0000003f, 0x0000000a,
	0x2ae4, 0x00073ffe, 0x000022a2,
	0x240c, 0x000007ff, 0x00000000,
	0x8bf0, 0x00002001, 0x00000001,
	0x8b24, 0xffffffff, 0x00ffffff,
	0x30a04, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x06000000,
	0x3e78, 0x00000001, 0x00000002,
	0xc768, 0x00000008, 0x00000008,
	0xc770, 0x00000f00, 0x00000800,
	0xc774, 0x00000f00, 0x00000800,
	0xc798, 0x00ffffff, 0x00ff7fbf,
	0xc79c, 0x00ffffff, 0x00ff7faf,
	0x8c00, 0x000000ff, 0x00000800,
	0xe40, 0x00001fff, 0x00001fff,
	0x9060, 0x0000007f, 0x00000020,
	0x9508, 0x00010000, 0x00010000,
	0xae00, 0x00100000, 0x000ff07c,
	0xac14, 0x000003ff, 0x0000000f,
	0xac10, 0xffffffff, 0x7564fdec,
	0xac0c, 0xffffffff, 0x3120b9a8,
	0xac08, 0x20000000, 0x0f9c0000
};

static const u32 hawaii_mgcg_cgcg_init[] =
{
	0xc420, 0xffffffff, 0xfffffffd,
	0x30800, 0xffffffff, 0xe0000000,
	0x3c2a0, 0xffffffff, 0x00000100,
	0x3c208, 0xffffffff, 0x00000100,
	0x3c2c0, 0xffffffff, 0x00000100,
	0x3c2c8, 0xffffffff, 0x00000100,
	0x3c2c4, 0xffffffff, 0x00000100,
	0x55e4, 0xffffffff, 0x00200100,
	0x3c280, 0xffffffff, 0x00000100,
	0x3c214, 0xffffffff, 0x06000100,
	0x3c220, 0xffffffff, 0x00000100,
	0x3c218, 0xffffffff, 0x06000100,
	0x3c204, 0xffffffff, 0x00000100,
	0x3c2e0, 0xffffffff, 0x00000100,
	0x3c224, 0xffffffff, 0x00000100,
	0x3c200, 0xffffffff, 0x00000100,
	0x3c230, 0xffffffff, 0x00000100,
	0x3c234, 0xffffffff, 0x00000100,
	0x3c250, 0xffffffff, 0x00000100,
	0x3c254, 0xffffffff, 0x00000100,
	0x3c258, 0xffffffff, 0x00000100,
	0x3c25c, 0xffffffff, 0x00000100,
	0x3c260, 0xffffffff, 0x00000100,
	0x3c27c, 0xffffffff, 0x00000100,
	0x3c278, 0xffffffff, 0x00000100,
	0x3c210, 0xffffffff, 0x06000100,
	0x3c290, 0xffffffff, 0x00000100,
	0x3c274, 0xffffffff, 0x00000100,
	0x3c2b4, 0xffffffff, 0x00000100,
	0x3c2b0, 0xffffffff, 0x00000100,
	0x3c270, 0xffffffff, 0x00000100,
	0x30800, 0xffffffff, 0xe0000000,
	0x3c020, 0xffffffff, 0x00010000,
	0x3c024, 0xffffffff, 0x00030002,
	0x3c028, 0xffffffff, 0x00040007,
	0x3c02c, 0xffffffff, 0x00060005,
	0x3c030, 0xffffffff, 0x00090008,
	0x3c034, 0xffffffff, 0x00010000,
	0x3c038, 0xffffffff, 0x00030002,
	0x3c03c, 0xffffffff, 0x00040007,
	0x3c040, 0xffffffff, 0x00060005,
	0x3c044, 0xffffffff, 0x00090008,
	0x3c048, 0xffffffff, 0x00010000,
	0x3c04c, 0xffffffff, 0x00030002,
	0x3c050, 0xffffffff, 0x00040007,
	0x3c054, 0xffffffff, 0x00060005,
	0x3c058, 0xffffffff, 0x00090008,
	0x3c05c, 0xffffffff, 0x00010000,
	0x3c060, 0xffffffff, 0x00030002,
	0x3c064, 0xffffffff, 0x00040007,
	0x3c068, 0xffffffff, 0x00060005,
	0x3c06c, 0xffffffff, 0x00090008,
	0x3c070, 0xffffffff, 0x00010000,
	0x3c074, 0xffffffff, 0x00030002,
	0x3c078, 0xffffffff, 0x00040007,
	0x3c07c, 0xffffffff, 0x00060005,
	0x3c080, 0xffffffff, 0x00090008,
	0x3c084, 0xffffffff, 0x00010000,
	0x3c088, 0xffffffff, 0x00030002,
	0x3c08c, 0xffffffff, 0x00040007,
	0x3c090, 0xffffffff, 0x00060005,
	0x3c094, 0xffffffff, 0x00090008,
	0x3c098, 0xffffffff, 0x00010000,
	0x3c09c, 0xffffffff, 0x00030002,
	0x3c0a0, 0xffffffff, 0x00040007,
	0x3c0a4, 0xffffffff, 0x00060005,
	0x3c0a8, 0xffffffff, 0x00090008,
	0x3c0ac, 0xffffffff, 0x00010000,
	0x3c0b0, 0xffffffff, 0x00030002,
	0x3c0b4, 0xffffffff, 0x00040007,
	0x3c0b8, 0xffffffff, 0x00060005,
	0x3c0bc, 0xffffffff, 0x00090008,
	0x3c0c0, 0xffffffff, 0x00010000,
	0x3c0c4, 0xffffffff, 0x00030002,
	0x3c0c8, 0xffffffff, 0x00040007,
	0x3c0cc, 0xffffffff, 0x00060005,
	0x3c0d0, 0xffffffff, 0x00090008,
	0x3c0d4, 0xffffffff, 0x00010000,
	0x3c0d8, 0xffffffff, 0x00030002,
	0x3c0dc, 0xffffffff, 0x00040007,
	0x3c0e0, 0xffffffff, 0x00060005,
	0x3c0e4, 0xffffffff, 0x00090008,
	0x3c0e8, 0xffffffff, 0x00010000,
	0x3c0ec, 0xffffffff, 0x00030002,
	0x3c0f0, 0xffffffff, 0x00040007,
	0x3c0f4, 0xffffffff, 0x00060005,
	0x3c0f8, 0xffffffff, 0x00090008,
	0xc318, 0xffffffff, 0x00020200,
	0x3350, 0xffffffff, 0x00000200,
	0x15c0, 0xffffffff, 0x00000400,
	0x55e8, 0xffffffff, 0x00000000,
	0x2f50, 0xffffffff, 0x00000902,
	0x3c000, 0xffffffff, 0x96940200,
	0x8708, 0xffffffff, 0x00900100,
	0xc424, 0xffffffff, 0x0020003f,
	0x38, 0xffffffff, 0x0140001c,
	0x3c, 0x000f0000, 0x000f0000,
	0x220, 0xffffffff, 0xc060000c,
	0x224, 0xc0000fff, 0x00000100,
	0xf90, 0xffffffff, 0x00000100,
	0xf98, 0x00000101, 0x00000000,
	0x20a8, 0xffffffff, 0x00000104,
	0x55e4, 0xff000fff, 0x00000100,
	0x30cc, 0xc0000fff, 0x00000104,
	0xc1e4, 0x00000001, 0x00000001,
	0xd00c, 0xff000ff0, 0x00000100,
	0xd80c, 0xff000ff0, 0x00000100
};

static const u32 godavari_golden_registers[] =
{
	0x55e4, 0xff607fff, 0xfc000100,
	0x6ed8, 0x00010101, 0x00010000,
	0x9830, 0xffffffff, 0x00000000,
	0x98302, 0xf00fffff, 0x00000400,
	0x6130, 0xffffffff, 0x00010000,
	0x5bb0, 0x000000f0, 0x00000070,
	0x5bc0, 0xf0311fff, 0x80300000,
	0x98f8, 0x73773777, 0x12010001,
	0x98fc, 0xffffffff, 0x00000010,
	0x8030, 0x00001f0f, 0x0000100a,
	0x2f48, 0x73773777, 0x12010001,
	0x2408, 0x000fffff, 0x000c007f,
	0x8a14, 0xf000003f, 0x00000007,
	0x8b24, 0xffffffff, 0x00ff0fff,
	0x30a04, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x06000000,
	0x4d8, 0x00000fff, 0x00000100,
	0xd014, 0x00010000, 0x00810001,
	0xd814, 0x00010000, 0x00810001,
	0x3e78, 0x00000001, 0x00000002,
	0xc768, 0x00000008, 0x00000008,
	0xc770, 0x00000f00, 0x00000800,
	0xc774, 0x00000f00, 0x00000800,
	0xc798, 0x00ffffff, 0x00ff7fbf,
	0xc79c, 0x00ffffff, 0x00ff7faf,
	0x8c00, 0x000000ff, 0x00000001,
	0x214f8, 0x01ff01ff, 0x00000002,
	0x21498, 0x007ff800, 0x00200000,
	0x2015c, 0xffffffff, 0x00000f40,
	0x88c4, 0x001f3ae3, 0x00000082,
	0x88d4, 0x0000001f, 0x00000010,
	0x30934, 0xffffffff, 0x00000000
};


static void cik_init_golden_registers(struct radeon_device *rdev)
{
	/* Some of the registers might be dependent on GRBM_GFX_INDEX */
	mutex_lock(&rdev->grbm_idx_mutex);
	switch (rdev->family) {
	case CHIP_BONAIRE:
		radeon_program_register_sequence(rdev,
						 bonaire_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(bonaire_mgcg_cgcg_init));
		radeon_program_register_sequence(rdev,
						 bonaire_golden_registers,
						 (const u32)ARRAY_SIZE(bonaire_golden_registers));
		radeon_program_register_sequence(rdev,
						 bonaire_golden_common_registers,
						 (const u32)ARRAY_SIZE(bonaire_golden_common_registers));
		radeon_program_register_sequence(rdev,
						 bonaire_golden_spm_registers,
						 (const u32)ARRAY_SIZE(bonaire_golden_spm_registers));
		break;
	case CHIP_KABINI:
		radeon_program_register_sequence(rdev,
						 kalindi_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(kalindi_mgcg_cgcg_init));
		radeon_program_register_sequence(rdev,
						 kalindi_golden_registers,
						 (const u32)ARRAY_SIZE(kalindi_golden_registers));
		radeon_program_register_sequence(rdev,
						 kalindi_golden_common_registers,
						 (const u32)ARRAY_SIZE(kalindi_golden_common_registers));
		radeon_program_register_sequence(rdev,
						 kalindi_golden_spm_registers,
						 (const u32)ARRAY_SIZE(kalindi_golden_spm_registers));
		break;
	case CHIP_MULLINS:
		radeon_program_register_sequence(rdev,
						 kalindi_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(kalindi_mgcg_cgcg_init));
		radeon_program_register_sequence(rdev,
						 godavari_golden_registers,
						 (const u32)ARRAY_SIZE(godavari_golden_registers));
		radeon_program_register_sequence(rdev,
						 kalindi_golden_common_registers,
						 (const u32)ARRAY_SIZE(kalindi_golden_common_registers));
		radeon_program_register_sequence(rdev,
						 kalindi_golden_spm_registers,
						 (const u32)ARRAY_SIZE(kalindi_golden_spm_registers));
		break;
	case CHIP_KAVERI:
		radeon_program_register_sequence(rdev,
						 spectre_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(spectre_mgcg_cgcg_init));
		radeon_program_register_sequence(rdev,
						 spectre_golden_registers,
						 (const u32)ARRAY_SIZE(spectre_golden_registers));
		radeon_program_register_sequence(rdev,
						 spectre_golden_common_registers,
						 (const u32)ARRAY_SIZE(spectre_golden_common_registers));
		radeon_program_register_sequence(rdev,
						 spectre_golden_spm_registers,
						 (const u32)ARRAY_SIZE(spectre_golden_spm_registers));
		break;
	case CHIP_HAWAII:
		radeon_program_register_sequence(rdev,
						 hawaii_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(hawaii_mgcg_cgcg_init));
		radeon_program_register_sequence(rdev,
						 hawaii_golden_registers,
						 (const u32)ARRAY_SIZE(hawaii_golden_registers));
		radeon_program_register_sequence(rdev,
						 hawaii_golden_common_registers,
						 (const u32)ARRAY_SIZE(hawaii_golden_common_registers));
		radeon_program_register_sequence(rdev,
						 hawaii_golden_spm_registers,
						 (const u32)ARRAY_SIZE(hawaii_golden_spm_registers));
		break;
	default:
		break;
	}
	mutex_unlock(&rdev->grbm_idx_mutex);
}

/**
 * cik_get_xclk - get the xclk
 *
 * @rdev: radeon_device pointer
 *
 * Returns the reference clock used by the gfx engine
 * (CIK).
 */
u32 cik_get_xclk(struct radeon_device *rdev)
{
        u32 reference_clock = rdev->clock.spll.reference_freq;

	if (rdev->flags & RADEON_IS_IGP) {
		if (RREG32_SMC(GENERAL_PWRMGT) & GPU_COUNTER_CLK)
			return reference_clock / 2;
	} else {
		if (RREG32_SMC(CG_CLKPIN_CNTL) & XTALIN_DIVIDE)
			return reference_clock / 4;
	}
	return reference_clock;
}

/**
 * cik_mm_rdoorbell - read a doorbell dword
 *
 * @rdev: radeon_device pointer
 * @index: doorbell index
 *
 * Returns the value in the doorbell aperture at the
 * requested doorbell index (CIK).
 */
u32 cik_mm_rdoorbell(struct radeon_device *rdev, u32 index)
{
	if (index < rdev->doorbell.num_doorbells) {
		return readl(rdev->doorbell.ptr + index);
	} else {
		DRM_ERROR("reading beyond doorbell aperture: 0x%08x!\n", index);
		return 0;
	}
}

/**
 * cik_mm_wdoorbell - write a doorbell dword
 *
 * @rdev: radeon_device pointer
 * @index: doorbell index
 * @v: value to write
 *
 * Writes @v to the doorbell aperture at the
 * requested doorbell index (CIK).
 */
void cik_mm_wdoorbell(struct radeon_device *rdev, u32 index, u32 v)
{
	if (index < rdev->doorbell.num_doorbells) {
		writel(v, rdev->doorbell.ptr + index);
	} else {
		DRM_ERROR("writing beyond doorbell aperture: 0x%08x!\n", index);
	}
}

#define BONAIRE_IO_MC_REGS_SIZE 36

static const u32 bonaire_io_mc_regs[BONAIRE_IO_MC_REGS_SIZE][2] =
{
	{0x00000070, 0x04400000},
	{0x00000071, 0x80c01803},
	{0x00000072, 0x00004004},
	{0x00000073, 0x00000100},
	{0x00000074, 0x00ff0000},
	{0x00000075, 0x34000000},
	{0x00000076, 0x08000014},
	{0x00000077, 0x00cc08ec},
	{0x00000078, 0x00000400},
	{0x00000079, 0x00000000},
	{0x0000007a, 0x04090000},
	{0x0000007c, 0x00000000},
	{0x0000007e, 0x4408a8e8},
	{0x0000007f, 0x00000304},
	{0x00000080, 0x00000000},
	{0x00000082, 0x00000001},
	{0x00000083, 0x00000002},
	{0x00000084, 0xf3e4f400},
	{0x00000085, 0x052024e3},
	{0x00000087, 0x00000000},
	{0x00000088, 0x01000000},
	{0x0000008a, 0x1c0a0000},
	{0x0000008b, 0xff010000},
	{0x0000008d, 0xffffefff},
	{0x0000008e, 0xfff3efff},
	{0x0000008f, 0xfff3efbf},
	{0x00000092, 0xf7ffffff},
	{0x00000093, 0xffffff7f},
	{0x00000095, 0x00101101},
	{0x00000096, 0x00000fff},
	{0x00000097, 0x00116fff},
	{0x00000098, 0x60010000},
	{0x00000099, 0x10010000},
	{0x0000009a, 0x00006000},
	{0x0000009b, 0x00001000},
	{0x0000009f, 0x00b48000}
};

#define HAWAII_IO_MC_REGS_SIZE 22

static const u32 hawaii_io_mc_regs[HAWAII_IO_MC_REGS_SIZE][2] =
{
	{0x0000007d, 0x40000000},
	{0x0000007e, 0x40180304},
	{0x0000007f, 0x0000ff00},
	{0x00000081, 0x00000000},
	{0x00000083, 0x00000800},
	{0x00000086, 0x00000000},
	{0x00000087, 0x00000100},
	{0x00000088, 0x00020100},
	{0x00000089, 0x00000000},
	{0x0000008b, 0x00040000},
	{0x0000008c, 0x00000100},
	{0x0000008e, 0xff010000},
	{0x00000090, 0xffffefff},
	{0x00000091, 0xfff3efff},
	{0x00000092, 0xfff3efbf},
	{0x00000093, 0xf7ffffff},
	{0x00000094, 0xffffff7f},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x0000009f, 0x00c79000}
};


/**
 * cik_srbm_select - select specific register instances
 *
 * @rdev: radeon_device pointer
 * @me: selected ME (micro engine)
 * @pipe: pipe
 * @queue: queue
 * @vmid: VMID
 *
 * Switches the currently active registers instances.  Some
 * registers are instanced per VMID, others are instanced per
 * me/pipe/queue combination.
 */
static void cik_srbm_select(struct radeon_device *rdev,
			    u32 me, u32 pipe, u32 queue, u32 vmid)
{
	u32 srbm_gfx_cntl = (PIPEID(pipe & 0x3) |
			     MEID(me & 0x3) |
			     VMID(vmid & 0xf) |
			     QUEUEID(queue & 0x7));
	WREG32(SRBM_GFX_CNTL, srbm_gfx_cntl);
}

/* ucode loading */
/**
 * ci_mc_load_microcode - load MC ucode into the hw
 *
 * @rdev: radeon_device pointer
 *
 * Load the GDDR MC ucode into the hw (CIK).
 * Returns 0 on success, error on failure.
 */
int ci_mc_load_microcode(struct radeon_device *rdev)
{
	const __be32 *fw_data = NULL;
	const __le32 *new_fw_data = NULL;
	u32 running, blackout = 0, tmp;
	u32 *io_mc_regs = NULL;
	const __le32 *new_io_mc_regs = NULL;
	int i, regs_size, ucode_size;

	if (!rdev->mc_fw)
		return -EINVAL;

	if (rdev->new_fw) {
		const struct mc_firmware_header_v1_0 *hdr =
			(const struct mc_firmware_header_v1_0 *)rdev->mc_fw->data;

		radeon_ucode_print_mc_hdr(&hdr->header);

		regs_size = le32_to_cpu(hdr->io_debug_size_bytes) / (4 * 2);
		new_io_mc_regs = (const __le32 *)
			(rdev->mc_fw->data + le32_to_cpu(hdr->io_debug_array_offset_bytes));
		ucode_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;
		new_fw_data = (const __le32 *)
			(rdev->mc_fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));
	} else {
		ucode_size = rdev->mc_fw->size / 4;

		switch (rdev->family) {
		case CHIP_BONAIRE:
			io_mc_regs = (u32 *)&bonaire_io_mc_regs;
			regs_size = BONAIRE_IO_MC_REGS_SIZE;
			break;
		case CHIP_HAWAII:
			io_mc_regs = (u32 *)&hawaii_io_mc_regs;
			regs_size = HAWAII_IO_MC_REGS_SIZE;
			break;
		default:
			return -EINVAL;
		}
		fw_data = (const __be32 *)rdev->mc_fw->data;
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
			if (rdev->new_fw) {
				WREG32(MC_SEQ_IO_DEBUG_INDEX, le32_to_cpup(new_io_mc_regs++));
				WREG32(MC_SEQ_IO_DEBUG_DATA, le32_to_cpup(new_io_mc_regs++));
			} else {
				WREG32(MC_SEQ_IO_DEBUG_INDEX, io_mc_regs[(i << 1)]);
				WREG32(MC_SEQ_IO_DEBUG_DATA, io_mc_regs[(i << 1) + 1]);
			}
		}

		tmp = RREG32(MC_SEQ_MISC0);
		if ((rdev->pdev->device == 0x6649) && ((tmp & 0xff00) == 0x5600)) {
			WREG32(MC_SEQ_IO_DEBUG_INDEX, 5);
			WREG32(MC_SEQ_IO_DEBUG_DATA, 0x00000023);
			WREG32(MC_SEQ_IO_DEBUG_INDEX, 9);
			WREG32(MC_SEQ_IO_DEBUG_DATA, 0x000001f0);
		}

		/* load the MC ucode */
		for (i = 0; i < ucode_size; i++) {
			if (rdev->new_fw)
				WREG32(MC_SEQ_SUP_PGM, le32_to_cpup(new_fw_data++));
			else
				WREG32(MC_SEQ_SUP_PGM, be32_to_cpup(fw_data++));
		}

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

/**
 * cik_init_microcode - load ucode images from disk
 *
 * @rdev: radeon_device pointer
 *
 * Use the firmware interface to load the ucode images into
 * the driver (not loaded into hw).
 * Returns 0 on success, error on failure.
 */
static int cik_init_microcode(struct radeon_device *rdev)
{
	const char *chip_name;
	const char *new_chip_name;
	size_t pfp_req_size, me_req_size, ce_req_size,
		mec_req_size, rlc_req_size, mc_req_size = 0,
		sdma_req_size, smc_req_size = 0, mc2_req_size = 0;
	char fw_name[30];
	int new_fw = 0;
	int err;
	int num_fw;

	DRM_DEBUG("\n");

	switch (rdev->family) {
	case CHIP_BONAIRE:
		chip_name = "BONAIRE";
		new_chip_name = "bonaire";
		pfp_req_size = CIK_PFP_UCODE_SIZE * 4;
		me_req_size = CIK_ME_UCODE_SIZE * 4;
		ce_req_size = CIK_CE_UCODE_SIZE * 4;
		mec_req_size = CIK_MEC_UCODE_SIZE * 4;
		rlc_req_size = BONAIRE_RLC_UCODE_SIZE * 4;
		mc_req_size = BONAIRE_MC_UCODE_SIZE * 4;
		mc2_req_size = BONAIRE_MC2_UCODE_SIZE * 4;
		sdma_req_size = CIK_SDMA_UCODE_SIZE * 4;
		smc_req_size = ALIGN(BONAIRE_SMC_UCODE_SIZE, 4);
		num_fw = 8;
		break;
	case CHIP_HAWAII:
		chip_name = "HAWAII";
		new_chip_name = "hawaii";
		pfp_req_size = CIK_PFP_UCODE_SIZE * 4;
		me_req_size = CIK_ME_UCODE_SIZE * 4;
		ce_req_size = CIK_CE_UCODE_SIZE * 4;
		mec_req_size = CIK_MEC_UCODE_SIZE * 4;
		rlc_req_size = BONAIRE_RLC_UCODE_SIZE * 4;
		mc_req_size = HAWAII_MC_UCODE_SIZE * 4;
		mc2_req_size = HAWAII_MC2_UCODE_SIZE * 4;
		sdma_req_size = CIK_SDMA_UCODE_SIZE * 4;
		smc_req_size = ALIGN(HAWAII_SMC_UCODE_SIZE, 4);
		num_fw = 8;
		break;
	case CHIP_KAVERI:
		chip_name = "KAVERI";
		new_chip_name = "kaveri";
		pfp_req_size = CIK_PFP_UCODE_SIZE * 4;
		me_req_size = CIK_ME_UCODE_SIZE * 4;
		ce_req_size = CIK_CE_UCODE_SIZE * 4;
		mec_req_size = CIK_MEC_UCODE_SIZE * 4;
		rlc_req_size = KV_RLC_UCODE_SIZE * 4;
		sdma_req_size = CIK_SDMA_UCODE_SIZE * 4;
		num_fw = 7;
		break;
	case CHIP_KABINI:
		chip_name = "KABINI";
		new_chip_name = "kabini";
		pfp_req_size = CIK_PFP_UCODE_SIZE * 4;
		me_req_size = CIK_ME_UCODE_SIZE * 4;
		ce_req_size = CIK_CE_UCODE_SIZE * 4;
		mec_req_size = CIK_MEC_UCODE_SIZE * 4;
		rlc_req_size = KB_RLC_UCODE_SIZE * 4;
		sdma_req_size = CIK_SDMA_UCODE_SIZE * 4;
		num_fw = 6;
		break;
	case CHIP_MULLINS:
		chip_name = "MULLINS";
		new_chip_name = "mullins";
		pfp_req_size = CIK_PFP_UCODE_SIZE * 4;
		me_req_size = CIK_ME_UCODE_SIZE * 4;
		ce_req_size = CIK_CE_UCODE_SIZE * 4;
		mec_req_size = CIK_MEC_UCODE_SIZE * 4;
		rlc_req_size = ML_RLC_UCODE_SIZE * 4;
		sdma_req_size = CIK_SDMA_UCODE_SIZE * 4;
		num_fw = 6;
		break;
	default: BUG();
	}

	DRM_INFO("Loading %s Microcode\n", new_chip_name);

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_pfp.bin", new_chip_name);
	err = request_firmware(&rdev->pfp_fw, fw_name, rdev->dev);
	if (err) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_pfp.bin", chip_name);
		err = request_firmware(&rdev->pfp_fw, fw_name, rdev->dev);
		if (err)
			goto out;
		if (rdev->pfp_fw->size != pfp_req_size) {
			printk(KERN_ERR
			       "cik_cp: Bogus length %zu in firmware \"%s\"\n",
			       rdev->pfp_fw->size, fw_name);
			err = -EINVAL;
			goto out;
		}
	} else {
		err = radeon_ucode_validate(rdev->pfp_fw);
		if (err) {
			printk(KERN_ERR
			       "cik_fw: validation failed for firmware \"%s\"\n",
			       fw_name);
			goto out;
		} else {
			new_fw++;
		}
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_me.bin", new_chip_name);
	err = request_firmware(&rdev->me_fw, fw_name, rdev->dev);
	if (err) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_me.bin", chip_name);
		err = request_firmware(&rdev->me_fw, fw_name, rdev->dev);
		if (err)
			goto out;
		if (rdev->me_fw->size != me_req_size) {
			printk(KERN_ERR
			       "cik_cp: Bogus length %zu in firmware \"%s\"\n",
			       rdev->me_fw->size, fw_name);
			err = -EINVAL;
		}
	} else {
		err = radeon_ucode_validate(rdev->me_fw);
		if (err) {
			printk(KERN_ERR
			       "cik_fw: validation failed for firmware \"%s\"\n",
			       fw_name);
			goto out;
		} else {
			new_fw++;
		}
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_ce.bin", new_chip_name);
	err = request_firmware(&rdev->ce_fw, fw_name, rdev->dev);
	if (err) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_ce.bin", chip_name);
		err = request_firmware(&rdev->ce_fw, fw_name, rdev->dev);
		if (err)
			goto out;
		if (rdev->ce_fw->size != ce_req_size) {
			printk(KERN_ERR
			       "cik_cp: Bogus length %zu in firmware \"%s\"\n",
			       rdev->ce_fw->size, fw_name);
			err = -EINVAL;
		}
	} else {
		err = radeon_ucode_validate(rdev->ce_fw);
		if (err) {
			printk(KERN_ERR
			       "cik_fw: validation failed for firmware \"%s\"\n",
			       fw_name);
			goto out;
		} else {
			new_fw++;
		}
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_mec.bin", new_chip_name);
	err = request_firmware(&rdev->mec_fw, fw_name, rdev->dev);
	if (err) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_mec.bin", chip_name);
		err = request_firmware(&rdev->mec_fw, fw_name, rdev->dev);
		if (err)
			goto out;
		if (rdev->mec_fw->size != mec_req_size) {
			printk(KERN_ERR
			       "cik_cp: Bogus length %zu in firmware \"%s\"\n",
			       rdev->mec_fw->size, fw_name);
			err = -EINVAL;
		}
	} else {
		err = radeon_ucode_validate(rdev->mec_fw);
		if (err) {
			printk(KERN_ERR
			       "cik_fw: validation failed for firmware \"%s\"\n",
			       fw_name);
			goto out;
		} else {
			new_fw++;
		}
	}

	if (rdev->family == CHIP_KAVERI) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_mec2.bin", new_chip_name);
		err = request_firmware(&rdev->mec2_fw, fw_name, rdev->dev);
		if (err) {
			goto out;
		} else {
			err = radeon_ucode_validate(rdev->mec2_fw);
			if (err) {
				goto out;
			} else {
				new_fw++;
			}
		}
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_rlc.bin", new_chip_name);
	err = request_firmware(&rdev->rlc_fw, fw_name, rdev->dev);
	if (err) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_rlc.bin", chip_name);
		err = request_firmware(&rdev->rlc_fw, fw_name, rdev->dev);
		if (err)
			goto out;
		if (rdev->rlc_fw->size != rlc_req_size) {
			printk(KERN_ERR
			       "cik_rlc: Bogus length %zu in firmware \"%s\"\n",
			       rdev->rlc_fw->size, fw_name);
			err = -EINVAL;
		}
	} else {
		err = radeon_ucode_validate(rdev->rlc_fw);
		if (err) {
			printk(KERN_ERR
			       "cik_fw: validation failed for firmware \"%s\"\n",
			       fw_name);
			goto out;
		} else {
			new_fw++;
		}
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_sdma.bin", new_chip_name);
	err = request_firmware(&rdev->sdma_fw, fw_name, rdev->dev);
	if (err) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_sdma.bin", chip_name);
		err = request_firmware(&rdev->sdma_fw, fw_name, rdev->dev);
		if (err)
			goto out;
		if (rdev->sdma_fw->size != sdma_req_size) {
			printk(KERN_ERR
			       "cik_sdma: Bogus length %zu in firmware \"%s\"\n",
			       rdev->sdma_fw->size, fw_name);
			err = -EINVAL;
		}
	} else {
		err = radeon_ucode_validate(rdev->sdma_fw);
		if (err) {
			printk(KERN_ERR
			       "cik_fw: validation failed for firmware \"%s\"\n",
			       fw_name);
			goto out;
		} else {
			new_fw++;
		}
	}

	/* No SMC, MC ucode on APUs */
	if (!(rdev->flags & RADEON_IS_IGP)) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_mc.bin", new_chip_name);
		err = request_firmware(&rdev->mc_fw, fw_name, rdev->dev);
		if (err) {
			snprintf(fw_name, sizeof(fw_name), "radeon/%s_mc2.bin", chip_name);
			err = request_firmware(&rdev->mc_fw, fw_name, rdev->dev);
			if (err) {
				snprintf(fw_name, sizeof(fw_name), "radeon/%s_mc.bin", chip_name);
				err = request_firmware(&rdev->mc_fw, fw_name, rdev->dev);
				if (err)
					goto out;
			}
			if ((rdev->mc_fw->size != mc_req_size) &&
			    (rdev->mc_fw->size != mc2_req_size)){
				printk(KERN_ERR
				       "cik_mc: Bogus length %zu in firmware \"%s\"\n",
				       rdev->mc_fw->size, fw_name);
				err = -EINVAL;
			}
			DRM_INFO("%s: %zu bytes\n", fw_name, rdev->mc_fw->size);
		} else {
			err = radeon_ucode_validate(rdev->mc_fw);
			if (err) {
				printk(KERN_ERR
				       "cik_fw: validation failed for firmware \"%s\"\n",
				       fw_name);
				goto out;
			} else {
				new_fw++;
			}
		}

		snprintf(fw_name, sizeof(fw_name), "radeon/%s_smc.bin", new_chip_name);
		err = request_firmware(&rdev->smc_fw, fw_name, rdev->dev);
		if (err) {
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
				       "cik_smc: Bogus length %zu in firmware \"%s\"\n",
				       rdev->smc_fw->size, fw_name);
				err = -EINVAL;
			}
		} else {
			err = radeon_ucode_validate(rdev->smc_fw);
			if (err) {
				printk(KERN_ERR
				       "cik_fw: validation failed for firmware \"%s\"\n",
				       fw_name);
				goto out;
			} else {
				new_fw++;
			}
		}
	}

	if (new_fw == 0) {
		rdev->new_fw = false;
	} else if (new_fw < num_fw) {
		printk(KERN_ERR "ci_fw: mixing new and old firmware!\n");
		err = -EINVAL;
	} else {
		rdev->new_fw = true;
	}

out:
	if (err) {
		if (err != -EINVAL)
			printk(KERN_ERR
			       "cik_cp: Failed to load firmware \"%s\"\n",
			       fw_name);
		release_firmware(rdev->pfp_fw);
		rdev->pfp_fw = NULL;
		release_firmware(rdev->me_fw);
		rdev->me_fw = NULL;
		release_firmware(rdev->ce_fw);
		rdev->ce_fw = NULL;
		release_firmware(rdev->mec_fw);
		rdev->mec_fw = NULL;
		release_firmware(rdev->mec2_fw);
		rdev->mec2_fw = NULL;
		release_firmware(rdev->rlc_fw);
		rdev->rlc_fw = NULL;
		release_firmware(rdev->sdma_fw);
		rdev->sdma_fw = NULL;
		release_firmware(rdev->mc_fw);
		rdev->mc_fw = NULL;
		release_firmware(rdev->smc_fw);
		rdev->smc_fw = NULL;
	}
	return err;
}

/*
 * Core functions
 */
/**
 * cik_tiling_mode_table_init - init the hw tiling table
 *
 * @rdev: radeon_device pointer
 *
 * Starting with SI, the tiling setup is done globally in a
 * set of 32 tiling modes.  Rather than selecting each set of
 * parameters per surface as on older asics, we just select
 * which index in the tiling table we want to use, and the
 * surface uses those parameters (CIK).
 */
static void cik_tiling_mode_table_init(struct radeon_device *rdev)
{
	const u32 num_tile_mode_states = 32;
	const u32 num_secondary_tile_mode_states = 16;
	u32 reg_offset, gb_tile_moden, split_equal_to_row_size;
	u32 num_pipe_configs;
	u32 num_rbs = rdev->config.cik.max_backends_per_se *
		rdev->config.cik.max_shader_engines;

	switch (rdev->config.cik.mem_row_size_in_kb) {
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

	num_pipe_configs = rdev->config.cik.max_tile_pipes;
	if (num_pipe_configs > 8)
		num_pipe_configs = 16;

	if (num_pipe_configs == 16) {
		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B));
				break;
			case 1:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B));
				break;
			case 2:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
				break;
			case 3:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B));
				break;
			case 4:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 TILE_SPLIT(split_equal_to_row_size));
				break;
			case 5:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
				break;
			case 6:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
				break;
			case 7:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 TILE_SPLIT(split_equal_to_row_size));
				break;
			case 8:
				gb_tile_moden = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16));
				break;
			case 9:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING));
				break;
			case 10:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 11:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_8x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 12:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 13:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING));
				break;
			case 14:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 16:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_8x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 17:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 27:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING));
				break;
			case 28:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 29:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_8x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 30:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			rdev->config.cik.tile_mode_array[reg_offset] = gb_tile_moden;
			WREG32(GB_TILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 1:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 2:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 3:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 4:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 5:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_4_BANK));
				break;
			case 6:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_2_BANK));
				break;
			case 8:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 9:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 10:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 11:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 12:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_4_BANK));
				break;
			case 13:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_2_BANK));
				break;
			case 14:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_2_BANK));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			rdev->config.cik.macrotile_mode_array[reg_offset] = gb_tile_moden;
			WREG32(GB_MACROTILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
	} else if (num_pipe_configs == 8) {
		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B));
				break;
			case 1:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B));
				break;
			case 2:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
				break;
			case 3:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B));
				break;
			case 4:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(split_equal_to_row_size));
				break;
			case 5:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
				break;
			case 6:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
				break;
			case 7:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(split_equal_to_row_size));
				break;
			case 8:
				gb_tile_moden = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16));
				break;
			case 9:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING));
				break;
			case 10:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 11:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 12:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 13:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING));
				break;
			case 14:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 16:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 17:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 27:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING));
				break;
			case 28:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 29:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 30:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			rdev->config.cik.tile_mode_array[reg_offset] = gb_tile_moden;
			WREG32(GB_TILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 1:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 2:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 3:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 4:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 5:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_4_BANK));
				break;
			case 6:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_2_BANK));
				break;
			case 8:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 9:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 10:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 11:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 12:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 13:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_4_BANK));
				break;
			case 14:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_2_BANK));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			rdev->config.cik.macrotile_mode_array[reg_offset] = gb_tile_moden;
			WREG32(GB_MACROTILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
	} else if (num_pipe_configs == 4) {
		if (num_rbs == 4) {
			for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
				switch (reg_offset) {
				case 0:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B));
					break;
				case 1:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B));
					break;
				case 2:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
					break;
				case 3:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B));
					break;
				case 4:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(split_equal_to_row_size));
					break;
				case 5:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
					break;
				case 6:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
					break;
				case 7:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(split_equal_to_row_size));
					break;
				case 8:
					gb_tile_moden = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16));
					break;
				case 9:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING));
					break;
				case 10:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 11:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 12:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 13:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING));
					break;
				case 14:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 16:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 17:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 27:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING));
					break;
				case 28:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 29:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 30:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				default:
					gb_tile_moden = 0;
					break;
				}
				rdev->config.cik.tile_mode_array[reg_offset] = gb_tile_moden;
				WREG32(GB_TILE_MODE0 + (reg_offset * 4), gb_tile_moden);
			}
		} else if (num_rbs < 4) {
			for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
				switch (reg_offset) {
				case 0:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B));
					break;
				case 1:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B));
					break;
				case 2:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
					break;
				case 3:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B));
					break;
				case 4:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(split_equal_to_row_size));
					break;
				case 5:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
					break;
				case 6:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
					break;
				case 7:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(split_equal_to_row_size));
					break;
				case 8:
					gb_tile_moden = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16));
					break;
				case 9:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING));
					break;
				case 10:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 11:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 12:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 13:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING));
					break;
				case 14:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 16:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 17:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 27:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING));
					break;
				case 28:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 29:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 30:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				default:
					gb_tile_moden = 0;
					break;
				}
				rdev->config.cik.tile_mode_array[reg_offset] = gb_tile_moden;
				WREG32(GB_TILE_MODE0 + (reg_offset * 4), gb_tile_moden);
			}
		}
		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 1:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 2:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 3:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 4:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 5:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 6:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_4_BANK));
				break;
			case 8:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 9:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 10:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 11:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 12:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 13:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 14:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_4_BANK));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			rdev->config.cik.macrotile_mode_array[reg_offset] = gb_tile_moden;
			WREG32(GB_MACROTILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
	} else if (num_pipe_configs == 2) {
		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B));
				break;
			case 1:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B));
				break;
			case 2:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
				break;
			case 3:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B));
				break;
			case 4:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(split_equal_to_row_size));
				break;
			case 5:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
				break;
			case 6:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
				break;
			case 7:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(split_equal_to_row_size));
				break;
			case 8:
				gb_tile_moden = ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
						PIPE_CONFIG(ADDR_SURF_P2);
				break;
			case 9:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2));
				break;
			case 10:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 11:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 12:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 13:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING));
				break;
			case 14:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 16:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 17:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 27:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2));
				break;
			case 28:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 29:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 30:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			rdev->config.cik.tile_mode_array[reg_offset] = gb_tile_moden;
			WREG32(GB_TILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 1:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 2:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 3:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 4:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 5:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 6:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 8:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 9:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 10:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 11:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 12:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 13:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 14:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			rdev->config.cik.macrotile_mode_array[reg_offset] = gb_tile_moden;
			WREG32(GB_MACROTILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
	} else
		DRM_ERROR("unknown num pipe config: 0x%x\n", num_pipe_configs);
}

/**
 * cik_select_se_sh - select which SE, SH to address
 *
 * @rdev: radeon_device pointer
 * @se_num: shader engine to address
 * @sh_num: sh block to address
 *
 * Select which SE, SH combinations to address. Certain
 * registers are instanced per SE or SH.  0xffffffff means
 * broadcast to all SEs or SHs (CIK).
 */
static void cik_select_se_sh(struct radeon_device *rdev,
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

/**
 * cik_create_bitmask - create a bitmask
 *
 * @bit_width: length of the mask
 *
 * create a variable length bit mask (CIK).
 * Returns the bitmask.
 */
static u32 cik_create_bitmask(u32 bit_width)
{
	u32 i, mask = 0;

	for (i = 0; i < bit_width; i++) {
		mask <<= 1;
		mask |= 1;
	}
	return mask;
}

/**
 * cik_get_rb_disabled - computes the mask of disabled RBs
 *
 * @rdev: radeon_device pointer
 * @max_rb_num: max RBs (render backends) for the asic
 * @se_num: number of SEs (shader engines) for the asic
 * @sh_per_se: number of SH blocks per SE for the asic
 *
 * Calculates the bitmask of disabled RBs (CIK).
 * Returns the disabled RB bitmask.
 */
static u32 cik_get_rb_disabled(struct radeon_device *rdev,
			      u32 max_rb_num_per_se,
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

	mask = cik_create_bitmask(max_rb_num_per_se / sh_per_se);

	return data & mask;
}

/**
 * cik_setup_rb - setup the RBs on the asic
 *
 * @rdev: radeon_device pointer
 * @se_num: number of SEs (shader engines) for the asic
 * @sh_per_se: number of SH blocks per SE for the asic
 * @max_rb_num: max RBs (render backends) for the asic
 *
 * Configures per-SE/SH RB registers (CIK).
 */
static void cik_setup_rb(struct radeon_device *rdev,
			 u32 se_num, u32 sh_per_se,
			 u32 max_rb_num_per_se)
{
	int i, j;
	u32 data, mask;
	u32 disabled_rbs = 0;
	u32 enabled_rbs = 0;

	mutex_lock(&rdev->grbm_idx_mutex);
	for (i = 0; i < se_num; i++) {
		for (j = 0; j < sh_per_se; j++) {
			cik_select_se_sh(rdev, i, j);
			data = cik_get_rb_disabled(rdev, max_rb_num_per_se, sh_per_se);
			if (rdev->family == CHIP_HAWAII)
				disabled_rbs |= data << ((i * sh_per_se + j) * HAWAII_RB_BITMAP_WIDTH_PER_SH);
			else
				disabled_rbs |= data << ((i * sh_per_se + j) * CIK_RB_BITMAP_WIDTH_PER_SH);
		}
	}
	cik_select_se_sh(rdev, 0xffffffff, 0xffffffff);
	mutex_unlock(&rdev->grbm_idx_mutex);

	mask = 1;
	for (i = 0; i < max_rb_num_per_se * se_num; i++) {
		if (!(disabled_rbs & mask))
			enabled_rbs |= mask;
		mask <<= 1;
	}

	rdev->config.cik.backend_enable_mask = enabled_rbs;

	mutex_lock(&rdev->grbm_idx_mutex);
	for (i = 0; i < se_num; i++) {
		cik_select_se_sh(rdev, i, 0xffffffff);
		data = 0;
		for (j = 0; j < sh_per_se; j++) {
			switch (enabled_rbs & 3) {
			case 0:
				if (j == 0)
					data |= PKR_MAP(RASTER_CONFIG_RB_MAP_3);
				else
					data |= PKR_MAP(RASTER_CONFIG_RB_MAP_0);
				break;
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
	cik_select_se_sh(rdev, 0xffffffff, 0xffffffff);
	mutex_unlock(&rdev->grbm_idx_mutex);
}

/**
 * cik_gpu_init - setup the 3D engine
 *
 * @rdev: radeon_device pointer
 *
 * Configures the 3D engine and tiling configuration
 * registers so that the 3D engine is usable.
 */
static void cik_gpu_init(struct radeon_device *rdev)
{
	u32 gb_addr_config = RREG32(GB_ADDR_CONFIG);
	u32 mc_shared_chmap, mc_arb_ramcfg;
	u32 hdp_host_path_cntl;
	u32 tmp;
	int i, j;

	switch (rdev->family) {
	case CHIP_BONAIRE:
		rdev->config.cik.max_shader_engines = 2;
		rdev->config.cik.max_tile_pipes = 4;
		rdev->config.cik.max_cu_per_sh = 7;
		rdev->config.cik.max_sh_per_se = 1;
		rdev->config.cik.max_backends_per_se = 2;
		rdev->config.cik.max_texture_channel_caches = 4;
		rdev->config.cik.max_gprs = 256;
		rdev->config.cik.max_gs_threads = 32;
		rdev->config.cik.max_hw_contexts = 8;

		rdev->config.cik.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.cik.sc_prim_fifo_size_backend = 0x100;
		rdev->config.cik.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.cik.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = BONAIRE_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_HAWAII:
		rdev->config.cik.max_shader_engines = 4;
		rdev->config.cik.max_tile_pipes = 16;
		rdev->config.cik.max_cu_per_sh = 11;
		rdev->config.cik.max_sh_per_se = 1;
		rdev->config.cik.max_backends_per_se = 4;
		rdev->config.cik.max_texture_channel_caches = 16;
		rdev->config.cik.max_gprs = 256;
		rdev->config.cik.max_gs_threads = 32;
		rdev->config.cik.max_hw_contexts = 8;

		rdev->config.cik.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.cik.sc_prim_fifo_size_backend = 0x100;
		rdev->config.cik.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.cik.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = HAWAII_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_KAVERI:
		rdev->config.cik.max_shader_engines = 1;
		rdev->config.cik.max_tile_pipes = 4;
		if ((rdev->pdev->device == 0x1304) ||
		    (rdev->pdev->device == 0x1305) ||
		    (rdev->pdev->device == 0x130C) ||
		    (rdev->pdev->device == 0x130F) ||
		    (rdev->pdev->device == 0x1310) ||
		    (rdev->pdev->device == 0x1311) ||
		    (rdev->pdev->device == 0x131C)) {
			rdev->config.cik.max_cu_per_sh = 8;
			rdev->config.cik.max_backends_per_se = 2;
		} else if ((rdev->pdev->device == 0x1309) ||
			   (rdev->pdev->device == 0x130A) ||
			   (rdev->pdev->device == 0x130D) ||
			   (rdev->pdev->device == 0x1313) ||
			   (rdev->pdev->device == 0x131D)) {
			rdev->config.cik.max_cu_per_sh = 6;
			rdev->config.cik.max_backends_per_se = 2;
		} else if ((rdev->pdev->device == 0x1306) ||
			   (rdev->pdev->device == 0x1307) ||
			   (rdev->pdev->device == 0x130B) ||
			   (rdev->pdev->device == 0x130E) ||
			   (rdev->pdev->device == 0x1315) ||
			   (rdev->pdev->device == 0x1318) ||
			   (rdev->pdev->device == 0x131B)) {
			rdev->config.cik.max_cu_per_sh = 4;
			rdev->config.cik.max_backends_per_se = 1;
		} else {
			rdev->config.cik.max_cu_per_sh = 3;
			rdev->config.cik.max_backends_per_se = 1;
		}
		rdev->config.cik.max_sh_per_se = 1;
		rdev->config.cik.max_texture_channel_caches = 4;
		rdev->config.cik.max_gprs = 256;
		rdev->config.cik.max_gs_threads = 16;
		rdev->config.cik.max_hw_contexts = 8;

		rdev->config.cik.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.cik.sc_prim_fifo_size_backend = 0x100;
		rdev->config.cik.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.cik.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = BONAIRE_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_KABINI:
	case CHIP_MULLINS:
	default:
		rdev->config.cik.max_shader_engines = 1;
		rdev->config.cik.max_tile_pipes = 2;
		rdev->config.cik.max_cu_per_sh = 2;
		rdev->config.cik.max_sh_per_se = 1;
		rdev->config.cik.max_backends_per_se = 1;
		rdev->config.cik.max_texture_channel_caches = 2;
		rdev->config.cik.max_gprs = 256;
		rdev->config.cik.max_gs_threads = 16;
		rdev->config.cik.max_hw_contexts = 8;

		rdev->config.cik.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.cik.sc_prim_fifo_size_backend = 0x100;
		rdev->config.cik.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.cik.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = BONAIRE_GB_ADDR_CONFIG_GOLDEN;
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

	WREG32(BIF_FB_EN, FB_READ_EN | FB_WRITE_EN);

	mc_shared_chmap = RREG32(MC_SHARED_CHMAP);
	mc_arb_ramcfg = RREG32(MC_ARB_RAMCFG);

	rdev->config.cik.num_tile_pipes = rdev->config.cik.max_tile_pipes;
	rdev->config.cik.mem_max_burst_length_bytes = 256;
	tmp = (mc_arb_ramcfg & NOOFCOLS_MASK) >> NOOFCOLS_SHIFT;
	rdev->config.cik.mem_row_size_in_kb = (4 * (1 << (8 + tmp))) / 1024;
	if (rdev->config.cik.mem_row_size_in_kb > 4)
		rdev->config.cik.mem_row_size_in_kb = 4;
	/* XXX use MC settings? */
	rdev->config.cik.shader_engine_tile_size = 32;
	rdev->config.cik.num_gpus = 1;
	rdev->config.cik.multi_gpu_tile_size = 64;

	/* fix up row size */
	gb_addr_config &= ~ROW_SIZE_MASK;
	switch (rdev->config.cik.mem_row_size_in_kb) {
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
	rdev->config.cik.tile_config = 0;
	switch (rdev->config.cik.num_tile_pipes) {
	case 1:
		rdev->config.cik.tile_config |= (0 << 0);
		break;
	case 2:
		rdev->config.cik.tile_config |= (1 << 0);
		break;
	case 4:
		rdev->config.cik.tile_config |= (2 << 0);
		break;
	case 8:
	default:
		/* XXX what about 12? */
		rdev->config.cik.tile_config |= (3 << 0);
		break;
	}
	rdev->config.cik.tile_config |=
		((mc_arb_ramcfg & NOOFBANK_MASK) >> NOOFBANK_SHIFT) << 4;
	rdev->config.cik.tile_config |=
		((gb_addr_config & PIPE_INTERLEAVE_SIZE_MASK) >> PIPE_INTERLEAVE_SIZE_SHIFT) << 8;
	rdev->config.cik.tile_config |=
		((gb_addr_config & ROW_SIZE_MASK) >> ROW_SIZE_SHIFT) << 12;

	WREG32(GB_ADDR_CONFIG, gb_addr_config);
	WREG32(HDP_ADDR_CONFIG, gb_addr_config);
	WREG32(DMIF_ADDR_CALC, gb_addr_config);
	WREG32(SDMA0_TILING_CONFIG + SDMA0_REGISTER_OFFSET, gb_addr_config & 0x70);
	WREG32(SDMA0_TILING_CONFIG + SDMA1_REGISTER_OFFSET, gb_addr_config & 0x70);
	WREG32(UVD_UDEC_ADDR_CONFIG, gb_addr_config);
	WREG32(UVD_UDEC_DB_ADDR_CONFIG, gb_addr_config);
	WREG32(UVD_UDEC_DBW_ADDR_CONFIG, gb_addr_config);

	cik_tiling_mode_table_init(rdev);

	cik_setup_rb(rdev, rdev->config.cik.max_shader_engines,
		     rdev->config.cik.max_sh_per_se,
		     rdev->config.cik.max_backends_per_se);

	rdev->config.cik.active_cus = 0;
	for (i = 0; i < rdev->config.cik.max_shader_engines; i++) {
		for (j = 0; j < rdev->config.cik.max_sh_per_se; j++) {
			rdev->config.cik.active_cus +=
				hweight32(cik_get_cu_active_bitmap(rdev, i, j));
		}
	}

	/* set HW defaults for 3D engine */
	WREG32(CP_MEQ_THRESHOLDS, MEQ1_START(0x30) | MEQ2_START(0x60));

	mutex_lock(&rdev->grbm_idx_mutex);
	/*
	 * making sure that the following register writes will be broadcasted
	 * to all the shaders
	 */
	cik_select_se_sh(rdev, 0xffffffff, 0xffffffff);
	WREG32(SX_DEBUG_1, 0x20);

	WREG32(TA_CNTL_AUX, 0x00010000);

	tmp = RREG32(SPI_CONFIG_CNTL);
	tmp |= 0x03000000;
	WREG32(SPI_CONFIG_CNTL, tmp);

	WREG32(SQ_CONFIG, 1);

	WREG32(DB_DEBUG, 0);

	tmp = RREG32(DB_DEBUG2) & ~0xf00fffff;
	tmp |= 0x00000400;
	WREG32(DB_DEBUG2, tmp);

	tmp = RREG32(DB_DEBUG3) & ~0x0002021c;
	tmp |= 0x00020200;
	WREG32(DB_DEBUG3, tmp);

	tmp = RREG32(CB_HW_CONTROL) & ~0x00010000;
	tmp |= 0x00018208;
	WREG32(CB_HW_CONTROL, tmp);

	WREG32(SPI_CONFIG_CNTL_1, VTX_DONE_DELAY(4));

	WREG32(PA_SC_FIFO_SIZE, (SC_FRONTEND_PRIM_FIFO_SIZE(rdev->config.cik.sc_prim_fifo_size_frontend) |
				 SC_BACKEND_PRIM_FIFO_SIZE(rdev->config.cik.sc_prim_fifo_size_backend) |
				 SC_HIZ_TILE_FIFO_SIZE(rdev->config.cik.sc_hiz_tile_fifo_size) |
				 SC_EARLYZ_TILE_FIFO_SIZE(rdev->config.cik.sc_earlyz_tile_fifo_size)));

	WREG32(VGT_NUM_INSTANCES, 1);

	WREG32(CP_PERFMON_CNTL, 0);

	WREG32(SQ_CONFIG, 0);

	WREG32(PA_SC_FORCE_EOV_MAX_CNTS, (FORCE_EOV_MAX_CLK_CNT(4095) |
					  FORCE_EOV_MAX_REZ_CNT(255)));

	WREG32(VGT_CACHE_INVALIDATION, CACHE_INVALIDATION(VC_AND_TC) |
	       AUTO_INVLD_EN(ES_AND_GS_AUTO));

	WREG32(VGT_GS_VERTEX_REUSE, 16);
	WREG32(PA_SC_LINE_STIPPLE_STATE, 0);

	tmp = RREG32(HDP_MISC_CNTL);
	tmp |= HDP_FLUSH_INVALIDATE_CACHE;
	WREG32(HDP_MISC_CNTL, tmp);

	hdp_host_path_cntl = RREG32(HDP_HOST_PATH_CNTL);
	WREG32(HDP_HOST_PATH_CNTL, hdp_host_path_cntl);

	WREG32(PA_CL_ENHANCE, CLIP_VTX_REORDER_ENA | NUM_CLIP_SEQ(3));
	WREG32(PA_SC_ENHANCE, ENABLE_PA_SC_OUT_OF_ORDER);
	mutex_unlock(&rdev->grbm_idx_mutex);

	udelay(50);
}

/*
 * GPU scratch registers helpers function.
 */
/**
 * cik_scratch_init - setup driver info for CP scratch regs
 *
 * @rdev: radeon_device pointer
 *
 * Set up the number and offset of the CP scratch registers.
 * NOTE: use of CP scratch registers is a legacy inferface and
 * is not used by default on newer asics (r6xx+).  On newer asics,
 * memory buffers are used for fences rather than scratch regs.
 */
static void cik_scratch_init(struct radeon_device *rdev)
{
	int i;

	rdev->scratch.num_reg = 7;
	rdev->scratch.reg_base = SCRATCH_REG0;
	for (i = 0; i < rdev->scratch.num_reg; i++) {
		rdev->scratch.free[i] = true;
		rdev->scratch.reg[i] = rdev->scratch.reg_base + (i * 4);
	}
}

/**
 * cik_ring_test - basic gfx ring test
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Allocate a scratch register and write to it using the gfx ring (CIK).
 * Provides a basic gfx ring test to verify that the ring is working.
 * Used by cik_cp_gfx_resume();
 * Returns 0 on success, error on failure.
 */
int cik_ring_test(struct radeon_device *rdev, struct radeon_ring *ring)
{
	uint32_t scratch;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	r = radeon_scratch_get(rdev, &scratch);
	if (r) {
		DRM_ERROR("radeon: cp failed to get scratch reg (%d).\n", r);
		return r;
	}
	WREG32(scratch, 0xCAFEDEAD);
	r = radeon_ring_lock(rdev, ring, 3);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring %d (%d).\n", ring->idx, r);
		radeon_scratch_free(rdev, scratch);
		return r;
	}
	radeon_ring_write(ring, PACKET3(PACKET3_SET_UCONFIG_REG, 1));
	radeon_ring_write(ring, ((scratch - PACKET3_SET_UCONFIG_REG_START) >> 2));
	radeon_ring_write(ring, 0xDEADBEEF);
	radeon_ring_unlock_commit(rdev, ring, false);

	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(scratch);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}
	if (i < rdev->usec_timeout) {
		DRM_INFO("ring test on %d succeeded in %d usecs\n", ring->idx, i);
	} else {
		DRM_ERROR("radeon: ring %d test failed (scratch(0x%04X)=0x%08X)\n",
			  ring->idx, scratch, tmp);
		r = -EINVAL;
	}
	radeon_scratch_free(rdev, scratch);
	return r;
}

/**
 * cik_hdp_flush_cp_ring_emit - emit an hdp flush on the cp
 *
 * @rdev: radeon_device pointer
 * @ridx: radeon ring index
 *
 * Emits an hdp flush on the cp.
 */
static void cik_hdp_flush_cp_ring_emit(struct radeon_device *rdev,
				       int ridx)
{
	struct radeon_ring *ring = &rdev->ring[ridx];
	u32 ref_and_mask;

	switch (ring->idx) {
	case CAYMAN_RING_TYPE_CP1_INDEX:
	case CAYMAN_RING_TYPE_CP2_INDEX:
	default:
		switch (ring->me) {
		case 0:
			ref_and_mask = CP2 << ring->pipe;
			break;
		case 1:
			ref_and_mask = CP6 << ring->pipe;
			break;
		default:
			return;
		}
		break;
	case RADEON_RING_TYPE_GFX_INDEX:
		ref_and_mask = CP0;
		break;
	}

	radeon_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	radeon_ring_write(ring, (WAIT_REG_MEM_OPERATION(1) | /* write, wait, write */
				 WAIT_REG_MEM_FUNCTION(3) |  /* == */
				 WAIT_REG_MEM_ENGINE(1)));   /* pfp */
	radeon_ring_write(ring, GPU_HDP_FLUSH_REQ >> 2);
	radeon_ring_write(ring, GPU_HDP_FLUSH_DONE >> 2);
	radeon_ring_write(ring, ref_and_mask);
	radeon_ring_write(ring, ref_and_mask);
	radeon_ring_write(ring, 0x20); /* poll interval */
}

/**
 * cik_fence_gfx_ring_emit - emit a fence on the gfx ring
 *
 * @rdev: radeon_device pointer
 * @fence: radeon fence object
 *
 * Emits a fence sequnce number on the gfx ring and flushes
 * GPU caches.
 */
void cik_fence_gfx_ring_emit(struct radeon_device *rdev,
			     struct radeon_fence *fence)
{
	struct radeon_ring *ring = &rdev->ring[fence->ring];
	u64 addr = rdev->fence_drv[fence->ring].gpu_addr;

	/* Workaround for cache flush problems. First send a dummy EOP
	 * event down the pipe with seq one below.
	 */
	radeon_ring_write(ring, PACKET3(PACKET3_EVENT_WRITE_EOP, 4));
	radeon_ring_write(ring, (EOP_TCL1_ACTION_EN |
				 EOP_TC_ACTION_EN |
				 EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) |
				 EVENT_INDEX(5)));
	radeon_ring_write(ring, addr & 0xfffffffc);
	radeon_ring_write(ring, (upper_32_bits(addr) & 0xffff) |
				DATA_SEL(1) | INT_SEL(0));
	radeon_ring_write(ring, fence->seq - 1);
	radeon_ring_write(ring, 0);

	/* Then send the real EOP event down the pipe. */
	radeon_ring_write(ring, PACKET3(PACKET3_EVENT_WRITE_EOP, 4));
	radeon_ring_write(ring, (EOP_TCL1_ACTION_EN |
				 EOP_TC_ACTION_EN |
				 EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) |
				 EVENT_INDEX(5)));
	radeon_ring_write(ring, addr & 0xfffffffc);
	radeon_ring_write(ring, (upper_32_bits(addr) & 0xffff) | DATA_SEL(1) | INT_SEL(2));
	radeon_ring_write(ring, fence->seq);
	radeon_ring_write(ring, 0);
}

/**
 * cik_fence_compute_ring_emit - emit a fence on the compute ring
 *
 * @rdev: radeon_device pointer
 * @fence: radeon fence object
 *
 * Emits a fence sequnce number on the compute ring and flushes
 * GPU caches.
 */
void cik_fence_compute_ring_emit(struct radeon_device *rdev,
				 struct radeon_fence *fence)
{
	struct radeon_ring *ring = &rdev->ring[fence->ring];
	u64 addr = rdev->fence_drv[fence->ring].gpu_addr;

	/* RELEASE_MEM - flush caches, send int */
	radeon_ring_write(ring, PACKET3(PACKET3_RELEASE_MEM, 5));
	radeon_ring_write(ring, (EOP_TCL1_ACTION_EN |
				 EOP_TC_ACTION_EN |
				 EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) |
				 EVENT_INDEX(5)));
	radeon_ring_write(ring, DATA_SEL(1) | INT_SEL(2));
	radeon_ring_write(ring, addr & 0xfffffffc);
	radeon_ring_write(ring, upper_32_bits(addr));
	radeon_ring_write(ring, fence->seq);
	radeon_ring_write(ring, 0);
}

/**
 * cik_semaphore_ring_emit - emit a semaphore on the CP ring
 *
 * @rdev: radeon_device pointer
 * @ring: radeon ring buffer object
 * @semaphore: radeon semaphore object
 * @emit_wait: Is this a sempahore wait?
 *
 * Emits a semaphore signal/wait packet to the CP ring and prevents the PFP
 * from running ahead of semaphore waits.
 */
bool cik_semaphore_ring_emit(struct radeon_device *rdev,
			     struct radeon_ring *ring,
			     struct radeon_semaphore *semaphore,
			     bool emit_wait)
{
	uint64_t addr = semaphore->gpu_addr;
	unsigned sel = emit_wait ? PACKET3_SEM_SEL_WAIT : PACKET3_SEM_SEL_SIGNAL;

	radeon_ring_write(ring, PACKET3(PACKET3_MEM_SEMAPHORE, 1));
	radeon_ring_write(ring, lower_32_bits(addr));
	radeon_ring_write(ring, (upper_32_bits(addr) & 0xffff) | sel);

	if (emit_wait && ring->idx == RADEON_RING_TYPE_GFX_INDEX) {
		/* Prevent the PFP from running ahead of the semaphore wait */
		radeon_ring_write(ring, PACKET3(PACKET3_PFP_SYNC_ME, 0));
		radeon_ring_write(ring, 0x0);
	}

	return true;
}

/**
 * cik_copy_cpdma - copy pages using the CP DMA engine
 *
 * @rdev: radeon_device pointer
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @num_gpu_pages: number of GPU pages to xfer
 * @resv: reservation object to sync to
 *
 * Copy GPU paging using the CP DMA engine (CIK+).
 * Used by the radeon ttm implementation to move pages if
 * registered as the asic copy callback.
 */
struct radeon_fence *cik_copy_cpdma(struct radeon_device *rdev,
				    uint64_t src_offset, uint64_t dst_offset,
				    unsigned num_gpu_pages,
				    struct reservation_object *resv)
{
	struct radeon_fence *fence;
	struct radeon_sync sync;
	int ring_index = rdev->asic->copy.blit_ring_index;
	struct radeon_ring *ring = &rdev->ring[ring_index];
	u32 size_in_bytes, cur_size_in_bytes, control;
	int i, num_loops;
	int r = 0;

	radeon_sync_create(&sync);

	size_in_bytes = (num_gpu_pages << RADEON_GPU_PAGE_SHIFT);
	num_loops = DIV_ROUND_UP(size_in_bytes, 0x1fffff);
	r = radeon_ring_lock(rdev, ring, num_loops * 7 + 18);
	if (r) {
		DRM_ERROR("radeon: moving bo (%d).\n", r);
		radeon_sync_free(rdev, &sync, NULL);
		return ERR_PTR(r);
	}

	radeon_sync_resv(rdev, &sync, resv, false);
	radeon_sync_rings(rdev, &sync, ring->idx);

	for (i = 0; i < num_loops; i++) {
		cur_size_in_bytes = size_in_bytes;
		if (cur_size_in_bytes > 0x1fffff)
			cur_size_in_bytes = 0x1fffff;
		size_in_bytes -= cur_size_in_bytes;
		control = 0;
		if (size_in_bytes == 0)
			control |= PACKET3_DMA_DATA_CP_SYNC;
		radeon_ring_write(ring, PACKET3(PACKET3_DMA_DATA, 5));
		radeon_ring_write(ring, control);
		radeon_ring_write(ring, lower_32_bits(src_offset));
		radeon_ring_write(ring, upper_32_bits(src_offset));
		radeon_ring_write(ring, lower_32_bits(dst_offset));
		radeon_ring_write(ring, upper_32_bits(dst_offset));
		radeon_ring_write(ring, cur_size_in_bytes);
		src_offset += cur_size_in_bytes;
		dst_offset += cur_size_in_bytes;
	}

	r = radeon_fence_emit(rdev, &fence, ring->idx);
	if (r) {
		radeon_ring_unlock_undo(rdev, ring);
		radeon_sync_free(rdev, &sync, NULL);
		return ERR_PTR(r);
	}

	radeon_ring_unlock_commit(rdev, ring, false);
	radeon_sync_free(rdev, &sync, fence);

	return fence;
}

/*
 * IB stuff
 */
/**
 * cik_ring_ib_execute - emit an IB (Indirect Buffer) on the gfx ring
 *
 * @rdev: radeon_device pointer
 * @ib: radeon indirect buffer object
 *
 * Emits an DE (drawing engine) or CE (constant engine) IB
 * on the gfx ring.  IBs are usually generated by userspace
 * acceleration drivers and submitted to the kernel for
 * sheduling on the ring.  This function schedules the IB
 * on the gfx ring for execution by the GPU.
 */
void cik_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib)
{
	struct radeon_ring *ring = &rdev->ring[ib->ring];
	unsigned vm_id = ib->vm ? ib->vm->ids[ib->ring].id : 0;
	u32 header, control = INDIRECT_BUFFER_VALID;

	if (ib->is_const_ib) {
		/* set switch buffer packet before const IB */
		radeon_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
		radeon_ring_write(ring, 0);

		header = PACKET3(PACKET3_INDIRECT_BUFFER_CONST, 2);
	} else {
		u32 next_rptr;
		if (ring->rptr_save_reg) {
			next_rptr = ring->wptr + 3 + 4;
			radeon_ring_write(ring, PACKET3(PACKET3_SET_UCONFIG_REG, 1));
			radeon_ring_write(ring, ((ring->rptr_save_reg -
						  PACKET3_SET_UCONFIG_REG_START) >> 2));
			radeon_ring_write(ring, next_rptr);
		} else if (rdev->wb.enabled) {
			next_rptr = ring->wptr + 5 + 4;
			radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
			radeon_ring_write(ring, WRITE_DATA_DST_SEL(1));
			radeon_ring_write(ring, ring->next_rptr_gpu_addr & 0xfffffffc);
			radeon_ring_write(ring, upper_32_bits(ring->next_rptr_gpu_addr));
			radeon_ring_write(ring, next_rptr);
		}

		header = PACKET3(PACKET3_INDIRECT_BUFFER, 2);
	}

	control |= ib->length_dw | (vm_id << 24);

	radeon_ring_write(ring, header);
	radeon_ring_write(ring,
#ifdef __BIG_ENDIAN
			  (2 << 0) |
#endif
			  (ib->gpu_addr & 0xFFFFFFFC));
	radeon_ring_write(ring, upper_32_bits(ib->gpu_addr) & 0xFFFF);
	radeon_ring_write(ring, control);
}

/**
 * cik_ib_test - basic gfx ring IB test
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Allocate an IB and execute it on the gfx ring (CIK).
 * Provides a basic gfx ring test to verify that IBs are working.
 * Returns 0 on success, error on failure.
 */
int cik_ib_test(struct radeon_device *rdev, struct radeon_ring *ring)
{
	struct radeon_ib ib;
	uint32_t scratch;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	r = radeon_scratch_get(rdev, &scratch);
	if (r) {
		DRM_ERROR("radeon: failed to get scratch reg (%d).\n", r);
		return r;
	}
	WREG32(scratch, 0xCAFEDEAD);
	r = radeon_ib_get(rdev, ring->idx, &ib, NULL, 256);
	if (r) {
		DRM_ERROR("radeon: failed to get ib (%d).\n", r);
		radeon_scratch_free(rdev, scratch);
		return r;
	}
	ib.ptr[0] = PACKET3(PACKET3_SET_UCONFIG_REG, 1);
	ib.ptr[1] = ((scratch - PACKET3_SET_UCONFIG_REG_START) >> 2);
	ib.ptr[2] = 0xDEADBEEF;
	ib.length_dw = 3;
	r = radeon_ib_schedule(rdev, &ib, NULL, false);
	if (r) {
		radeon_scratch_free(rdev, scratch);
		radeon_ib_free(rdev, &ib);
		DRM_ERROR("radeon: failed to schedule ib (%d).\n", r);
		return r;
	}
	r = radeon_fence_wait(ib.fence, false);
	if (r) {
		DRM_ERROR("radeon: fence wait failed (%d).\n", r);
		radeon_scratch_free(rdev, scratch);
		radeon_ib_free(rdev, &ib);
		return r;
	}
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(scratch);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}
	if (i < rdev->usec_timeout) {
		DRM_INFO("ib test on ring %d succeeded in %u usecs\n", ib.fence->ring, i);
	} else {
		DRM_ERROR("radeon: ib test failed (scratch(0x%04X)=0x%08X)\n",
			  scratch, tmp);
		r = -EINVAL;
	}
	radeon_scratch_free(rdev, scratch);
	radeon_ib_free(rdev, &ib);
	return r;
}

/*
 * CP.
 * On CIK, gfx and compute now have independant command processors.
 *
 * GFX
 * Gfx consists of a single ring and can process both gfx jobs and
 * compute jobs.  The gfx CP consists of three microengines (ME):
 * PFP - Pre-Fetch Parser
 * ME - Micro Engine
 * CE - Constant Engine
 * The PFP and ME make up what is considered the Drawing Engine (DE).
 * The CE is an asynchronous engine used for updating buffer desciptors
 * used by the DE so that they can be loaded into cache in parallel
 * while the DE is processing state update packets.
 *
 * Compute
 * The compute CP consists of two microengines (ME):
 * MEC1 - Compute MicroEngine 1
 * MEC2 - Compute MicroEngine 2
 * Each MEC supports 4 compute pipes and each pipe supports 8 queues.
 * The queues are exposed to userspace and are programmed directly
 * by the compute runtime.
 */
/**
 * cik_cp_gfx_enable - enable/disable the gfx CP MEs
 *
 * @rdev: radeon_device pointer
 * @enable: enable or disable the MEs
 *
 * Halts or unhalts the gfx MEs.
 */
static void cik_cp_gfx_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32(CP_ME_CNTL, 0);
	else {
		if (rdev->asic->copy.copy_ring_index == RADEON_RING_TYPE_GFX_INDEX)
			radeon_ttm_set_active_vram_size(rdev, rdev->mc.visible_vram_size);
		WREG32(CP_ME_CNTL, (CP_ME_HALT | CP_PFP_HALT | CP_CE_HALT));
		rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = false;
	}
	udelay(50);
}

/**
 * cik_cp_gfx_load_microcode - load the gfx CP ME ucode
 *
 * @rdev: radeon_device pointer
 *
 * Loads the gfx PFP, ME, and CE ucode.
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int cik_cp_gfx_load_microcode(struct radeon_device *rdev)
{
	int i;

	if (!rdev->me_fw || !rdev->pfp_fw || !rdev->ce_fw)
		return -EINVAL;

	cik_cp_gfx_enable(rdev, false);

	if (rdev->new_fw) {
		const struct gfx_firmware_header_v1_0 *pfp_hdr =
			(const struct gfx_firmware_header_v1_0 *)rdev->pfp_fw->data;
		const struct gfx_firmware_header_v1_0 *ce_hdr =
			(const struct gfx_firmware_header_v1_0 *)rdev->ce_fw->data;
		const struct gfx_firmware_header_v1_0 *me_hdr =
			(const struct gfx_firmware_header_v1_0 *)rdev->me_fw->data;
		const __le32 *fw_data;
		u32 fw_size;

		radeon_ucode_print_gfx_hdr(&pfp_hdr->header);
		radeon_ucode_print_gfx_hdr(&ce_hdr->header);
		radeon_ucode_print_gfx_hdr(&me_hdr->header);

		/* PFP */
		fw_data = (const __le32 *)
			(rdev->pfp_fw->data + le32_to_cpu(pfp_hdr->header.ucode_array_offset_bytes));
		fw_size = le32_to_cpu(pfp_hdr->header.ucode_size_bytes) / 4;
		WREG32(CP_PFP_UCODE_ADDR, 0);
		for (i = 0; i < fw_size; i++)
			WREG32(CP_PFP_UCODE_DATA, le32_to_cpup(fw_data++));
		WREG32(CP_PFP_UCODE_ADDR, le32_to_cpu(pfp_hdr->header.ucode_version));

		/* CE */
		fw_data = (const __le32 *)
			(rdev->ce_fw->data + le32_to_cpu(ce_hdr->header.ucode_array_offset_bytes));
		fw_size = le32_to_cpu(ce_hdr->header.ucode_size_bytes) / 4;
		WREG32(CP_CE_UCODE_ADDR, 0);
		for (i = 0; i < fw_size; i++)
			WREG32(CP_CE_UCODE_DATA, le32_to_cpup(fw_data++));
		WREG32(CP_CE_UCODE_ADDR, le32_to_cpu(ce_hdr->header.ucode_version));

		/* ME */
		fw_data = (const __be32 *)
			(rdev->me_fw->data + le32_to_cpu(me_hdr->header.ucode_array_offset_bytes));
		fw_size = le32_to_cpu(me_hdr->header.ucode_size_bytes) / 4;
		WREG32(CP_ME_RAM_WADDR, 0);
		for (i = 0; i < fw_size; i++)
			WREG32(CP_ME_RAM_DATA, le32_to_cpup(fw_data++));
		WREG32(CP_ME_RAM_WADDR, le32_to_cpu(me_hdr->header.ucode_version));
		WREG32(CP_ME_RAM_RADDR, le32_to_cpu(me_hdr->header.ucode_version));
	} else {
		const __be32 *fw_data;

		/* PFP */
		fw_data = (const __be32 *)rdev->pfp_fw->data;
		WREG32(CP_PFP_UCODE_ADDR, 0);
		for (i = 0; i < CIK_PFP_UCODE_SIZE; i++)
			WREG32(CP_PFP_UCODE_DATA, be32_to_cpup(fw_data++));
		WREG32(CP_PFP_UCODE_ADDR, 0);

		/* CE */
		fw_data = (const __be32 *)rdev->ce_fw->data;
		WREG32(CP_CE_UCODE_ADDR, 0);
		for (i = 0; i < CIK_CE_UCODE_SIZE; i++)
			WREG32(CP_CE_UCODE_DATA, be32_to_cpup(fw_data++));
		WREG32(CP_CE_UCODE_ADDR, 0);

		/* ME */
		fw_data = (const __be32 *)rdev->me_fw->data;
		WREG32(CP_ME_RAM_WADDR, 0);
		for (i = 0; i < CIK_ME_UCODE_SIZE; i++)
			WREG32(CP_ME_RAM_DATA, be32_to_cpup(fw_data++));
		WREG32(CP_ME_RAM_WADDR, 0);
	}

	return 0;
}

/**
 * cik_cp_gfx_start - start the gfx ring
 *
 * @rdev: radeon_device pointer
 *
 * Enables the ring and loads the clear state context and other
 * packets required to init the ring.
 * Returns 0 for success, error for failure.
 */
static int cik_cp_gfx_start(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	int r, i;

	/* init the CP */
	WREG32(CP_MAX_CONTEXT, rdev->config.cik.max_hw_contexts - 1);
	WREG32(CP_ENDIAN_SWAP, 0);
	WREG32(CP_DEVICE_ID, 1);

	cik_cp_gfx_enable(rdev, true);

	r = radeon_ring_lock(rdev, ring, cik_default_size + 17);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring (%d).\n", r);
		return r;
	}

	/* init the CE partitions.  CE only used for gfx on CIK */
	radeon_ring_write(ring, PACKET3(PACKET3_SET_BASE, 2));
	radeon_ring_write(ring, PACKET3_BASE_INDEX(CE_PARTITION_BASE));
	radeon_ring_write(ring, 0x8000);
	radeon_ring_write(ring, 0x8000);

	/* setup clear context state */
	radeon_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	radeon_ring_write(ring, PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	radeon_ring_write(ring, PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	radeon_ring_write(ring, 0x80000000);
	radeon_ring_write(ring, 0x80000000);

	for (i = 0; i < cik_default_size; i++)
		radeon_ring_write(ring, cik_default_state[i]);

	radeon_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	radeon_ring_write(ring, PACKET3_PREAMBLE_END_CLEAR_STATE);

	/* set clear context state */
	radeon_ring_write(ring, PACKET3(PACKET3_CLEAR_STATE, 0));
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	radeon_ring_write(ring, 0x00000316);
	radeon_ring_write(ring, 0x0000000e); /* VGT_VERTEX_REUSE_BLOCK_CNTL */
	radeon_ring_write(ring, 0x00000010); /* VGT_OUT_DEALLOC_CNTL */

	radeon_ring_unlock_commit(rdev, ring, false);

	return 0;
}

/**
 * cik_cp_gfx_fini - stop the gfx ring
 *
 * @rdev: radeon_device pointer
 *
 * Stop the gfx ring and tear down the driver ring
 * info.
 */
static void cik_cp_gfx_fini(struct radeon_device *rdev)
{
	cik_cp_gfx_enable(rdev, false);
	radeon_ring_fini(rdev, &rdev->ring[RADEON_RING_TYPE_GFX_INDEX]);
}

/**
 * cik_cp_gfx_resume - setup the gfx ring buffer registers
 *
 * @rdev: radeon_device pointer
 *
 * Program the location and size of the gfx ring buffer
 * and test it to make sure it's working.
 * Returns 0 for success, error for failure.
 */
static int cik_cp_gfx_resume(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	u32 tmp;
	u32 rb_bufsz;
	u64 rb_addr;
	int r;

	WREG32(CP_SEM_WAIT_TIMER, 0x0);
	if (rdev->family != CHIP_HAWAII)
		WREG32(CP_SEM_INCOMPLETE_TIMER_CNTL, 0x0);

	/* Set the write pointer delay */
	WREG32(CP_RB_WPTR_DELAY, 0);

	/* set the RB to use vmid 0 */
	WREG32(CP_RB_VMID, 0);

	WREG32(SCRATCH_ADDR, ((rdev->wb.gpu_addr + RADEON_WB_SCRATCH_OFFSET) >> 8) & 0xFFFFFFFF);

	/* ring 0 - compute and gfx */
	/* Set ring buffer size */
	ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	rb_bufsz = order_base_2(ring->ring_size / 8);
	tmp = (order_base_2(RADEON_GPU_PAGE_SIZE/8) << 8) | rb_bufsz;
#ifdef __BIG_ENDIAN
	tmp |= BUF_SWAP_32BIT;
#endif
	WREG32(CP_RB0_CNTL, tmp);

	/* Initialize the ring buffer's read and write pointers */
	WREG32(CP_RB0_CNTL, tmp | RB_RPTR_WR_ENA);
	ring->wptr = 0;
	WREG32(CP_RB0_WPTR, ring->wptr);

	/* set the wb address wether it's enabled or not */
	WREG32(CP_RB0_RPTR_ADDR, (rdev->wb.gpu_addr + RADEON_WB_CP_RPTR_OFFSET) & 0xFFFFFFFC);
	WREG32(CP_RB0_RPTR_ADDR_HI, upper_32_bits(rdev->wb.gpu_addr + RADEON_WB_CP_RPTR_OFFSET) & 0xFF);

	/* scratch register shadowing is no longer supported */
	WREG32(SCRATCH_UMSK, 0);

	if (!rdev->wb.enabled)
		tmp |= RB_NO_UPDATE;

	mdelay(1);
	WREG32(CP_RB0_CNTL, tmp);

	rb_addr = ring->gpu_addr >> 8;
	WREG32(CP_RB0_BASE, rb_addr);
	WREG32(CP_RB0_BASE_HI, upper_32_bits(rb_addr));

	/* start the ring */
	cik_cp_gfx_start(rdev);
	rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = true;
	r = radeon_ring_test(rdev, RADEON_RING_TYPE_GFX_INDEX, &rdev->ring[RADEON_RING_TYPE_GFX_INDEX]);
	if (r) {
		rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = false;
		return r;
	}

	if (rdev->asic->copy.copy_ring_index == RADEON_RING_TYPE_GFX_INDEX)
		radeon_ttm_set_active_vram_size(rdev, rdev->mc.real_vram_size);

	return 0;
}

u32 cik_gfx_get_rptr(struct radeon_device *rdev,
		     struct radeon_ring *ring)
{
	u32 rptr;

	if (rdev->wb.enabled)
		rptr = rdev->wb.wb[ring->rptr_offs/4];
	else
		rptr = RREG32(CP_RB0_RPTR);

	return rptr;
}

u32 cik_gfx_get_wptr(struct radeon_device *rdev,
		     struct radeon_ring *ring)
{
	u32 wptr;

	wptr = RREG32(CP_RB0_WPTR);

	return wptr;
}

void cik_gfx_set_wptr(struct radeon_device *rdev,
		      struct radeon_ring *ring)
{
	WREG32(CP_RB0_WPTR, ring->wptr);
	(void)RREG32(CP_RB0_WPTR);
}

u32 cik_compute_get_rptr(struct radeon_device *rdev,
			 struct radeon_ring *ring)
{
	u32 rptr;

	if (rdev->wb.enabled) {
		rptr = rdev->wb.wb[ring->rptr_offs/4];
	} else {
		mutex_lock(&rdev->srbm_mutex);
		cik_srbm_select(rdev, ring->me, ring->pipe, ring->queue, 0);
		rptr = RREG32(CP_HQD_PQ_RPTR);
		cik_srbm_select(rdev, 0, 0, 0, 0);
		mutex_unlock(&rdev->srbm_mutex);
	}

	return rptr;
}

u32 cik_compute_get_wptr(struct radeon_device *rdev,
			 struct radeon_ring *ring)
{
	u32 wptr;

	if (rdev->wb.enabled) {
		/* XXX check if swapping is necessary on BE */
		wptr = rdev->wb.wb[ring->wptr_offs/4];
	} else {
		mutex_lock(&rdev->srbm_mutex);
		cik_srbm_select(rdev, ring->me, ring->pipe, ring->queue, 0);
		wptr = RREG32(CP_HQD_PQ_WPTR);
		cik_srbm_select(rdev, 0, 0, 0, 0);
		mutex_unlock(&rdev->srbm_mutex);
	}

	return wptr;
}

void cik_compute_set_wptr(struct radeon_device *rdev,
			  struct radeon_ring *ring)
{
	/* XXX check if swapping is necessary on BE */
	rdev->wb.wb[ring->wptr_offs/4] = ring->wptr;
	WDOORBELL32(ring->doorbell_index, ring->wptr);
}

/**
 * cik_cp_compute_enable - enable/disable the compute CP MEs
 *
 * @rdev: radeon_device pointer
 * @enable: enable or disable the MEs
 *
 * Halts or unhalts the compute MEs.
 */
static void cik_cp_compute_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32(CP_MEC_CNTL, 0);
	else {
		WREG32(CP_MEC_CNTL, (MEC_ME1_HALT | MEC_ME2_HALT));
		rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX].ready = false;
		rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX].ready = false;
	}
	udelay(50);
}

/**
 * cik_cp_compute_load_microcode - load the compute CP ME ucode
 *
 * @rdev: radeon_device pointer
 *
 * Loads the compute MEC1&2 ucode.
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int cik_cp_compute_load_microcode(struct radeon_device *rdev)
{
	int i;

	if (!rdev->mec_fw)
		return -EINVAL;

	cik_cp_compute_enable(rdev, false);

	if (rdev->new_fw) {
		const struct gfx_firmware_header_v1_0 *mec_hdr =
			(const struct gfx_firmware_header_v1_0 *)rdev->mec_fw->data;
		const __le32 *fw_data;
		u32 fw_size;

		radeon_ucode_print_gfx_hdr(&mec_hdr->header);

		/* MEC1 */
		fw_data = (const __le32 *)
			(rdev->mec_fw->data + le32_to_cpu(mec_hdr->header.ucode_array_offset_bytes));
		fw_size = le32_to_cpu(mec_hdr->header.ucode_size_bytes) / 4;
		WREG32(CP_MEC_ME1_UCODE_ADDR, 0);
		for (i = 0; i < fw_size; i++)
			WREG32(CP_MEC_ME1_UCODE_DATA, le32_to_cpup(fw_data++));
		WREG32(CP_MEC_ME1_UCODE_ADDR, le32_to_cpu(mec_hdr->header.ucode_version));

		/* MEC2 */
		if (rdev->family == CHIP_KAVERI) {
			const struct gfx_firmware_header_v1_0 *mec2_hdr =
				(const struct gfx_firmware_header_v1_0 *)rdev->mec2_fw->data;

			fw_data = (const __le32 *)
				(rdev->mec2_fw->data +
				 le32_to_cpu(mec2_hdr->header.ucode_array_offset_bytes));
			fw_size = le32_to_cpu(mec2_hdr->header.ucode_size_bytes) / 4;
			WREG32(CP_MEC_ME2_UCODE_ADDR, 0);
			for (i = 0; i < fw_size; i++)
				WREG32(CP_MEC_ME2_UCODE_DATA, le32_to_cpup(fw_data++));
			WREG32(CP_MEC_ME2_UCODE_ADDR, le32_to_cpu(mec2_hdr->header.ucode_version));
		}
	} else {
		const __be32 *fw_data;

		/* MEC1 */
		fw_data = (const __be32 *)rdev->mec_fw->data;
		WREG32(CP_MEC_ME1_UCODE_ADDR, 0);
		for (i = 0; i < CIK_MEC_UCODE_SIZE; i++)
			WREG32(CP_MEC_ME1_UCODE_DATA, be32_to_cpup(fw_data++));
		WREG32(CP_MEC_ME1_UCODE_ADDR, 0);

		if (rdev->family == CHIP_KAVERI) {
			/* MEC2 */
			fw_data = (const __be32 *)rdev->mec_fw->data;
			WREG32(CP_MEC_ME2_UCODE_ADDR, 0);
			for (i = 0; i < CIK_MEC_UCODE_SIZE; i++)
				WREG32(CP_MEC_ME2_UCODE_DATA, be32_to_cpup(fw_data++));
			WREG32(CP_MEC_ME2_UCODE_ADDR, 0);
		}
	}

	return 0;
}

/**
 * cik_cp_compute_start - start the compute queues
 *
 * @rdev: radeon_device pointer
 *
 * Enable the compute queues.
 * Returns 0 for success, error for failure.
 */
static int cik_cp_compute_start(struct radeon_device *rdev)
{
	cik_cp_compute_enable(rdev, true);

	return 0;
}

/**
 * cik_cp_compute_fini - stop the compute queues
 *
 * @rdev: radeon_device pointer
 *
 * Stop the compute queues and tear down the driver queue
 * info.
 */
static void cik_cp_compute_fini(struct radeon_device *rdev)
{
	int i, idx, r;

	cik_cp_compute_enable(rdev, false);

	for (i = 0; i < 2; i++) {
		if (i == 0)
			idx = CAYMAN_RING_TYPE_CP1_INDEX;
		else
			idx = CAYMAN_RING_TYPE_CP2_INDEX;

		if (rdev->ring[idx].mqd_obj) {
			r = radeon_bo_reserve(rdev->ring[idx].mqd_obj, false);
			if (unlikely(r != 0))
				dev_warn(rdev->dev, "(%d) reserve MQD bo failed\n", r);

			radeon_bo_unpin(rdev->ring[idx].mqd_obj);
			radeon_bo_unreserve(rdev->ring[idx].mqd_obj);

			radeon_bo_unref(&rdev->ring[idx].mqd_obj);
			rdev->ring[idx].mqd_obj = NULL;
		}
	}
}

static void cik_mec_fini(struct radeon_device *rdev)
{
	int r;

	if (rdev->mec.hpd_eop_obj) {
		r = radeon_bo_reserve(rdev->mec.hpd_eop_obj, false);
		if (unlikely(r != 0))
			dev_warn(rdev->dev, "(%d) reserve HPD EOP bo failed\n", r);
		radeon_bo_unpin(rdev->mec.hpd_eop_obj);
		radeon_bo_unreserve(rdev->mec.hpd_eop_obj);

		radeon_bo_unref(&rdev->mec.hpd_eop_obj);
		rdev->mec.hpd_eop_obj = NULL;
	}
}

#define MEC_HPD_SIZE 2048

static int cik_mec_init(struct radeon_device *rdev)
{
	int r;
	u32 *hpd;

	/*
	 * KV:    2 MEC, 4 Pipes/MEC, 8 Queues/Pipe - 64 Queues total
	 * CI/KB: 1 MEC, 4 Pipes/MEC, 8 Queues/Pipe - 32 Queues total
	 * Nonetheless, we assign only 1 pipe because all other pipes will
	 * be handled by KFD
	 */
	rdev->mec.num_mec = 1;
	rdev->mec.num_pipe = 1;
	rdev->mec.num_queue = rdev->mec.num_mec * rdev->mec.num_pipe * 8;

	if (rdev->mec.hpd_eop_obj == NULL) {
		r = radeon_bo_create(rdev,
				     rdev->mec.num_mec *rdev->mec.num_pipe * MEC_HPD_SIZE * 2,
				     PAGE_SIZE, true,
				     RADEON_GEM_DOMAIN_GTT, 0, NULL, NULL,
				     &rdev->mec.hpd_eop_obj);
		if (r) {
			dev_warn(rdev->dev, "(%d) create HDP EOP bo failed\n", r);
			return r;
		}
	}

	r = radeon_bo_reserve(rdev->mec.hpd_eop_obj, false);
	if (unlikely(r != 0)) {
		cik_mec_fini(rdev);
		return r;
	}
	r = radeon_bo_pin(rdev->mec.hpd_eop_obj, RADEON_GEM_DOMAIN_GTT,
			  &rdev->mec.hpd_eop_gpu_addr);
	if (r) {
		dev_warn(rdev->dev, "(%d) pin HDP EOP bo failed\n", r);
		cik_mec_fini(rdev);
		return r;
	}
	r = radeon_bo_kmap(rdev->mec.hpd_eop_obj, (void **)&hpd);
	if (r) {
		dev_warn(rdev->dev, "(%d) map HDP EOP bo failed\n", r);
		cik_mec_fini(rdev);
		return r;
	}

	/* clear memory.  Not sure if this is required or not */
	memset(hpd, 0, rdev->mec.num_mec *rdev->mec.num_pipe * MEC_HPD_SIZE * 2);

	radeon_bo_kunmap(rdev->mec.hpd_eop_obj);
	radeon_bo_unreserve(rdev->mec.hpd_eop_obj);

	return 0;
}

struct hqd_registers
{
	u32 cp_mqd_base_addr;
	u32 cp_mqd_base_addr_hi;
	u32 cp_hqd_active;
	u32 cp_hqd_vmid;
	u32 cp_hqd_persistent_state;
	u32 cp_hqd_pipe_priority;
	u32 cp_hqd_queue_priority;
	u32 cp_hqd_quantum;
	u32 cp_hqd_pq_base;
	u32 cp_hqd_pq_base_hi;
	u32 cp_hqd_pq_rptr;
	u32 cp_hqd_pq_rptr_report_addr;
	u32 cp_hqd_pq_rptr_report_addr_hi;
	u32 cp_hqd_pq_wptr_poll_addr;
	u32 cp_hqd_pq_wptr_poll_addr_hi;
	u32 cp_hqd_pq_doorbell_control;
	u32 cp_hqd_pq_wptr;
	u32 cp_hqd_pq_control;
	u32 cp_hqd_ib_base_addr;
	u32 cp_hqd_ib_base_addr_hi;
	u32 cp_hqd_ib_rptr;
	u32 cp_hqd_ib_control;
	u32 cp_hqd_iq_timer;
	u32 cp_hqd_iq_rptr;
	u32 cp_hqd_dequeue_request;
	u32 cp_hqd_dma_offload;
	u32 cp_hqd_sema_cmd;
	u32 cp_hqd_msg_type;
	u32 cp_hqd_atomic0_preop_lo;
	u32 cp_hqd_atomic0_preop_hi;
	u32 cp_hqd_atomic1_preop_lo;
	u32 cp_hqd_atomic1_preop_hi;
	u32 cp_hqd_hq_scheduler0;
	u32 cp_hqd_hq_scheduler1;
	u32 cp_mqd_control;
};

struct bonaire_mqd
{
	u32 header;
	u32 dispatch_initiator;
	u32 dimensions[3];
	u32 start_idx[3];
	u32 num_threads[3];
	u32 pipeline_stat_enable;
	u32 perf_counter_enable;
	u32 pgm[2];
	u32 tba[2];
	u32 tma[2];
	u32 pgm_rsrc[2];
	u32 vmid;
	u32 resource_limits;
	u32 static_thread_mgmt01[2];
	u32 tmp_ring_size;
	u32 static_thread_mgmt23[2];
	u32 restart[3];
	u32 thread_trace_enable;
	u32 reserved1;
	u32 user_data[16];
	u32 vgtcs_invoke_count[2];
	struct hqd_registers queue_state;
	u32 dequeue_cntr;
	u32 interrupt_queue[64];
};

/**
 * cik_cp_compute_resume - setup the compute queue registers
 *
 * @rdev: radeon_device pointer
 *
 * Program the compute queues and test them to make sure they
 * are working.
 * Returns 0 for success, error for failure.
 */
static int cik_cp_compute_resume(struct radeon_device *rdev)
{
	int r, i, j, idx;
	u32 tmp;
	bool use_doorbell = true;
	u64 hqd_gpu_addr;
	u64 mqd_gpu_addr;
	u64 eop_gpu_addr;
	u64 wb_gpu_addr;
	u32 *buf;
	struct bonaire_mqd *mqd;

	r = cik_cp_compute_start(rdev);
	if (r)
		return r;

	/* fix up chicken bits */
	tmp = RREG32(CP_CPF_DEBUG);
	tmp |= (1 << 23);
	WREG32(CP_CPF_DEBUG, tmp);

	/* init the pipes */
	mutex_lock(&rdev->srbm_mutex);

	eop_gpu_addr = rdev->mec.hpd_eop_gpu_addr;

	cik_srbm_select(rdev, 0, 0, 0, 0);

	/* write the EOP addr */
	WREG32(CP_HPD_EOP_BASE_ADDR, eop_gpu_addr >> 8);
	WREG32(CP_HPD_EOP_BASE_ADDR_HI, upper_32_bits(eop_gpu_addr) >> 8);

	/* set the VMID assigned */
	WREG32(CP_HPD_EOP_VMID, 0);

	/* set the EOP size, register value is 2^(EOP_SIZE+1) dwords */
	tmp = RREG32(CP_HPD_EOP_CONTROL);
	tmp &= ~EOP_SIZE_MASK;
	tmp |= order_base_2(MEC_HPD_SIZE / 8);
	WREG32(CP_HPD_EOP_CONTROL, tmp);

	mutex_unlock(&rdev->srbm_mutex);

	/* init the queues.  Just two for now. */
	for (i = 0; i < 2; i++) {
		if (i == 0)
			idx = CAYMAN_RING_TYPE_CP1_INDEX;
		else
			idx = CAYMAN_RING_TYPE_CP2_INDEX;

		if (rdev->ring[idx].mqd_obj == NULL) {
			r = radeon_bo_create(rdev,
					     sizeof(struct bonaire_mqd),
					     PAGE_SIZE, true,
					     RADEON_GEM_DOMAIN_GTT, 0, NULL,
					     NULL, &rdev->ring[idx].mqd_obj);
			if (r) {
				dev_warn(rdev->dev, "(%d) create MQD bo failed\n", r);
				return r;
			}
		}

		r = radeon_bo_reserve(rdev->ring[idx].mqd_obj, false);
		if (unlikely(r != 0)) {
			cik_cp_compute_fini(rdev);
			return r;
		}
		r = radeon_bo_pin(rdev->ring[idx].mqd_obj, RADEON_GEM_DOMAIN_GTT,
				  &mqd_gpu_addr);
		if (r) {
			dev_warn(rdev->dev, "(%d) pin MQD bo failed\n", r);
			cik_cp_compute_fini(rdev);
			return r;
		}
		r = radeon_bo_kmap(rdev->ring[idx].mqd_obj, (void **)&buf);
		if (r) {
			dev_warn(rdev->dev, "(%d) map MQD bo failed\n", r);
			cik_cp_compute_fini(rdev);
			return r;
		}

		/* init the mqd struct */
		memset(buf, 0, sizeof(struct bonaire_mqd));

		mqd = (struct bonaire_mqd *)buf;
		mqd->header = 0xC0310800;
		mqd->static_thread_mgmt01[0] = 0xffffffff;
		mqd->static_thread_mgmt01[1] = 0xffffffff;
		mqd->static_thread_mgmt23[0] = 0xffffffff;
		mqd->static_thread_mgmt23[1] = 0xffffffff;

		mutex_lock(&rdev->srbm_mutex);
		cik_srbm_select(rdev, rdev->ring[idx].me,
				rdev->ring[idx].pipe,
				rdev->ring[idx].queue, 0);

		/* disable wptr polling */
		tmp = RREG32(CP_PQ_WPTR_POLL_CNTL);
		tmp &= ~WPTR_POLL_EN;
		WREG32(CP_PQ_WPTR_POLL_CNTL, tmp);

		/* enable doorbell? */
		mqd->queue_state.cp_hqd_pq_doorbell_control =
			RREG32(CP_HQD_PQ_DOORBELL_CONTROL);
		if (use_doorbell)
			mqd->queue_state.cp_hqd_pq_doorbell_control |= DOORBELL_EN;
		else
			mqd->queue_state.cp_hqd_pq_doorbell_control &= ~DOORBELL_EN;
		WREG32(CP_HQD_PQ_DOORBELL_CONTROL,
		       mqd->queue_state.cp_hqd_pq_doorbell_control);

		/* disable the queue if it's active */
		mqd->queue_state.cp_hqd_dequeue_request = 0;
		mqd->queue_state.cp_hqd_pq_rptr = 0;
		mqd->queue_state.cp_hqd_pq_wptr= 0;
		if (RREG32(CP_HQD_ACTIVE) & 1) {
			WREG32(CP_HQD_DEQUEUE_REQUEST, 1);
			for (j = 0; j < rdev->usec_timeout; j++) {
				if (!(RREG32(CP_HQD_ACTIVE) & 1))
					break;
				udelay(1);
			}
			WREG32(CP_HQD_DEQUEUE_REQUEST, mqd->queue_state.cp_hqd_dequeue_request);
			WREG32(CP_HQD_PQ_RPTR, mqd->queue_state.cp_hqd_pq_rptr);
			WREG32(CP_HQD_PQ_WPTR, mqd->queue_state.cp_hqd_pq_wptr);
		}

		/* set the pointer to the MQD */
		mqd->queue_state.cp_mqd_base_addr = mqd_gpu_addr & 0xfffffffc;
		mqd->queue_state.cp_mqd_base_addr_hi = upper_32_bits(mqd_gpu_addr);
		WREG32(CP_MQD_BASE_ADDR, mqd->queue_state.cp_mqd_base_addr);
		WREG32(CP_MQD_BASE_ADDR_HI, mqd->queue_state.cp_mqd_base_addr_hi);
		/* set MQD vmid to 0 */
		mqd->queue_state.cp_mqd_control = RREG32(CP_MQD_CONTROL);
		mqd->queue_state.cp_mqd_control &= ~MQD_VMID_MASK;
		WREG32(CP_MQD_CONTROL, mqd->queue_state.cp_mqd_control);

		/* set the pointer to the HQD, this is similar CP_RB0_BASE/_HI */
		hqd_gpu_addr = rdev->ring[idx].gpu_addr >> 8;
		mqd->queue_state.cp_hqd_pq_base = hqd_gpu_addr;
		mqd->queue_state.cp_hqd_pq_base_hi = upper_32_bits(hqd_gpu_addr);
		WREG32(CP_HQD_PQ_BASE, mqd->queue_state.cp_hqd_pq_base);
		WREG32(CP_HQD_PQ_BASE_HI, mqd->queue_state.cp_hqd_pq_base_hi);

		/* set up the HQD, this is similar to CP_RB0_CNTL */
		mqd->queue_state.cp_hqd_pq_control = RREG32(CP_HQD_PQ_CONTROL);
		mqd->queue_state.cp_hqd_pq_control &=
			~(QUEUE_SIZE_MASK | RPTR_BLOCK_SIZE_MASK);

		mqd->queue_state.cp_hqd_pq_control |=
			order_base_2(rdev->ring[idx].ring_size / 8);
		mqd->queue_state.cp_hqd_pq_control |=
			(order_base_2(RADEON_GPU_PAGE_SIZE/8) << 8);
#ifdef __BIG_ENDIAN
		mqd->queue_state.cp_hqd_pq_control |= BUF_SWAP_32BIT;
#endif
		mqd->queue_state.cp_hqd_pq_control &=
			~(UNORD_DISPATCH | ROQ_PQ_IB_FLIP | PQ_VOLATILE);
		mqd->queue_state.cp_hqd_pq_control |=
			PRIV_STATE | KMD_QUEUE; /* assuming kernel queue control */
		WREG32(CP_HQD_PQ_CONTROL, mqd->queue_state.cp_hqd_pq_control);

		/* only used if CP_PQ_WPTR_POLL_CNTL.WPTR_POLL_EN=1 */
		if (i == 0)
			wb_gpu_addr = rdev->wb.gpu_addr + CIK_WB_CP1_WPTR_OFFSET;
		else
			wb_gpu_addr = rdev->wb.gpu_addr + CIK_WB_CP2_WPTR_OFFSET;
		mqd->queue_state.cp_hqd_pq_wptr_poll_addr = wb_gpu_addr & 0xfffffffc;
		mqd->queue_state.cp_hqd_pq_wptr_poll_addr_hi = upper_32_bits(wb_gpu_addr) & 0xffff;
		WREG32(CP_HQD_PQ_WPTR_POLL_ADDR, mqd->queue_state.cp_hqd_pq_wptr_poll_addr);
		WREG32(CP_HQD_PQ_WPTR_POLL_ADDR_HI,
		       mqd->queue_state.cp_hqd_pq_wptr_poll_addr_hi);

		/* set the wb address wether it's enabled or not */
		if (i == 0)
			wb_gpu_addr = rdev->wb.gpu_addr + RADEON_WB_CP1_RPTR_OFFSET;
		else
			wb_gpu_addr = rdev->wb.gpu_addr + RADEON_WB_CP2_RPTR_OFFSET;
		mqd->queue_state.cp_hqd_pq_rptr_report_addr = wb_gpu_addr & 0xfffffffc;
		mqd->queue_state.cp_hqd_pq_rptr_report_addr_hi =
			upper_32_bits(wb_gpu_addr) & 0xffff;
		WREG32(CP_HQD_PQ_RPTR_REPORT_ADDR,
		       mqd->queue_state.cp_hqd_pq_rptr_report_addr);
		WREG32(CP_HQD_PQ_RPTR_REPORT_ADDR_HI,
		       mqd->queue_state.cp_hqd_pq_rptr_report_addr_hi);

		/* enable the doorbell if requested */
		if (use_doorbell) {
			mqd->queue_state.cp_hqd_pq_doorbell_control =
				RREG32(CP_HQD_PQ_DOORBELL_CONTROL);
			mqd->queue_state.cp_hqd_pq_doorbell_control &= ~DOORBELL_OFFSET_MASK;
			mqd->queue_state.cp_hqd_pq_doorbell_control |=
				DOORBELL_OFFSET(rdev->ring[idx].doorbell_index);
			mqd->queue_state.cp_hqd_pq_doorbell_control |= DOORBELL_EN;
			mqd->queue_state.cp_hqd_pq_doorbell_control &=
				~(DOORBELL_SOURCE | DOORBELL_HIT);

		} else {
			mqd->queue_state.cp_hqd_pq_doorbell_control = 0;
		}
		WREG32(CP_HQD_PQ_DOORBELL_CONTROL,
		       mqd->queue_state.cp_hqd_pq_doorbell_control);

		/* read and write pointers, similar to CP_RB0_WPTR/_RPTR */
		rdev->ring[idx].wptr = 0;
		mqd->queue_state.cp_hqd_pq_wptr = rdev->ring[idx].wptr;
		WREG32(CP_HQD_PQ_WPTR, mqd->queue_state.cp_hqd_pq_wptr);
		mqd->queue_state.cp_hqd_pq_rptr = RREG32(CP_HQD_PQ_RPTR);

		/* set the vmid for the queue */
		mqd->queue_state.cp_hqd_vmid = 0;
		WREG32(CP_HQD_VMID, mqd->queue_state.cp_hqd_vmid);

		/* activate the queue */
		mqd->queue_state.cp_hqd_active = 1;
		WREG32(CP_HQD_ACTIVE, mqd->queue_state.cp_hqd_active);

		cik_srbm_select(rdev, 0, 0, 0, 0);
		mutex_unlock(&rdev->srbm_mutex);

		radeon_bo_kunmap(rdev->ring[idx].mqd_obj);
		radeon_bo_unreserve(rdev->ring[idx].mqd_obj);

		rdev->ring[idx].ready = true;
		r = radeon_ring_test(rdev, idx, &rdev->ring[idx]);
		if (r)
			rdev->ring[idx].ready = false;
	}

	return 0;
}

static void cik_cp_enable(struct radeon_device *rdev, bool enable)
{
	cik_cp_gfx_enable(rdev, enable);
	cik_cp_compute_enable(rdev, enable);
}

static int cik_cp_load_microcode(struct radeon_device *rdev)
{
	int r;

	r = cik_cp_gfx_load_microcode(rdev);
	if (r)
		return r;
	r = cik_cp_compute_load_microcode(rdev);
	if (r)
		return r;

	return 0;
}

static void cik_cp_fini(struct radeon_device *rdev)
{
	cik_cp_gfx_fini(rdev);
	cik_cp_compute_fini(rdev);
}

static int cik_cp_resume(struct radeon_device *rdev)
{
	int r;

	cik_enable_gui_idle_interrupt(rdev, false);

	r = cik_cp_load_microcode(rdev);
	if (r)
		return r;

	r = cik_cp_gfx_resume(rdev);
	if (r)
		return r;
	r = cik_cp_compute_resume(rdev);
	if (r)
		return r;

	cik_enable_gui_idle_interrupt(rdev, true);

	return 0;
}

static void cik_print_gpu_status_regs(struct radeon_device *rdev)
{
	dev_info(rdev->dev, "  GRBM_STATUS=0x%08X\n",
		RREG32(GRBM_STATUS));
	dev_info(rdev->dev, "  GRBM_STATUS2=0x%08X\n",
		RREG32(GRBM_STATUS2));
	dev_info(rdev->dev, "  GRBM_STATUS_SE0=0x%08X\n",
		RREG32(GRBM_STATUS_SE0));
	dev_info(rdev->dev, "  GRBM_STATUS_SE1=0x%08X\n",
		RREG32(GRBM_STATUS_SE1));
	dev_info(rdev->dev, "  GRBM_STATUS_SE2=0x%08X\n",
		RREG32(GRBM_STATUS_SE2));
	dev_info(rdev->dev, "  GRBM_STATUS_SE3=0x%08X\n",
		RREG32(GRBM_STATUS_SE3));
	dev_info(rdev->dev, "  SRBM_STATUS=0x%08X\n",
		RREG32(SRBM_STATUS));
	dev_info(rdev->dev, "  SRBM_STATUS2=0x%08X\n",
		RREG32(SRBM_STATUS2));
	dev_info(rdev->dev, "  SDMA0_STATUS_REG   = 0x%08X\n",
		RREG32(SDMA0_STATUS_REG + SDMA0_REGISTER_OFFSET));
	dev_info(rdev->dev, "  SDMA1_STATUS_REG   = 0x%08X\n",
		 RREG32(SDMA0_STATUS_REG + SDMA1_REGISTER_OFFSET));
	dev_info(rdev->dev, "  CP_STAT = 0x%08x\n", RREG32(CP_STAT));
	dev_info(rdev->dev, "  CP_STALLED_STAT1 = 0x%08x\n",
		 RREG32(CP_STALLED_STAT1));
	dev_info(rdev->dev, "  CP_STALLED_STAT2 = 0x%08x\n",
		 RREG32(CP_STALLED_STAT2));
	dev_info(rdev->dev, "  CP_STALLED_STAT3 = 0x%08x\n",
		 RREG32(CP_STALLED_STAT3));
	dev_info(rdev->dev, "  CP_CPF_BUSY_STAT = 0x%08x\n",
		 RREG32(CP_CPF_BUSY_STAT));
	dev_info(rdev->dev, "  CP_CPF_STALLED_STAT1 = 0x%08x\n",
		 RREG32(CP_CPF_STALLED_STAT1));
	dev_info(rdev->dev, "  CP_CPF_STATUS = 0x%08x\n", RREG32(CP_CPF_STATUS));
	dev_info(rdev->dev, "  CP_CPC_BUSY_STAT = 0x%08x\n", RREG32(CP_CPC_BUSY_STAT));
	dev_info(rdev->dev, "  CP_CPC_STALLED_STAT1 = 0x%08x\n",
		 RREG32(CP_CPC_STALLED_STAT1));
	dev_info(rdev->dev, "  CP_CPC_STATUS = 0x%08x\n", RREG32(CP_CPC_STATUS));
}

/**
 * cik_gpu_check_soft_reset - check which blocks are busy
 *
 * @rdev: radeon_device pointer
 *
 * Check which blocks are busy and return the relevant reset
 * mask to be used by cik_gpu_soft_reset().
 * Returns a mask of the blocks to be reset.
 */
u32 cik_gpu_check_soft_reset(struct radeon_device *rdev)
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

	if (tmp & (CP_BUSY | CP_COHERENCY_BUSY))
		reset_mask |= RADEON_RESET_CP;

	/* GRBM_STATUS2 */
	tmp = RREG32(GRBM_STATUS2);
	if (tmp & RLC_BUSY)
		reset_mask |= RADEON_RESET_RLC;

	/* SDMA0_STATUS_REG */
	tmp = RREG32(SDMA0_STATUS_REG + SDMA0_REGISTER_OFFSET);
	if (!(tmp & SDMA_IDLE))
		reset_mask |= RADEON_RESET_DMA;

	/* SDMA1_STATUS_REG */
	tmp = RREG32(SDMA0_STATUS_REG + SDMA1_REGISTER_OFFSET);
	if (!(tmp & SDMA_IDLE))
		reset_mask |= RADEON_RESET_DMA1;

	/* SRBM_STATUS2 */
	tmp = RREG32(SRBM_STATUS2);
	if (tmp & SDMA_BUSY)
		reset_mask |= RADEON_RESET_DMA;

	if (tmp & SDMA1_BUSY)
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

	/* Skip MC reset as it's mostly likely not hung, just busy */
	if (reset_mask & RADEON_RESET_MC) {
		DRM_DEBUG("MC busy: 0x%08X, clearing.\n", reset_mask);
		reset_mask &= ~RADEON_RESET_MC;
	}

	return reset_mask;
}

/**
 * cik_gpu_soft_reset - soft reset GPU
 *
 * @rdev: radeon_device pointer
 * @reset_mask: mask of which blocks to reset
 *
 * Soft reset the blocks specified in @reset_mask.
 */
static void cik_gpu_soft_reset(struct radeon_device *rdev, u32 reset_mask)
{
	struct evergreen_mc_save save;
	u32 grbm_soft_reset = 0, srbm_soft_reset = 0;
	u32 tmp;

	if (reset_mask == 0)
		return;

	dev_info(rdev->dev, "GPU softreset: 0x%08X\n", reset_mask);

	cik_print_gpu_status_regs(rdev);
	dev_info(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_ADDR   0x%08X\n",
		 RREG32(VM_CONTEXT1_PROTECTION_FAULT_ADDR));
	dev_info(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_STATUS 0x%08X\n",
		 RREG32(VM_CONTEXT1_PROTECTION_FAULT_STATUS));

	/* disable CG/PG */
	cik_fini_pg(rdev);
	cik_fini_cg(rdev);

	/* stop the rlc */
	cik_rlc_stop(rdev);

	/* Disable GFX parsing/prefetching */
	WREG32(CP_ME_CNTL, CP_ME_HALT | CP_PFP_HALT | CP_CE_HALT);

	/* Disable MEC parsing/prefetching */
	WREG32(CP_MEC_CNTL, MEC_ME1_HALT | MEC_ME2_HALT);

	if (reset_mask & RADEON_RESET_DMA) {
		/* sdma0 */
		tmp = RREG32(SDMA0_ME_CNTL + SDMA0_REGISTER_OFFSET);
		tmp |= SDMA_HALT;
		WREG32(SDMA0_ME_CNTL + SDMA0_REGISTER_OFFSET, tmp);
	}
	if (reset_mask & RADEON_RESET_DMA1) {
		/* sdma1 */
		tmp = RREG32(SDMA0_ME_CNTL + SDMA1_REGISTER_OFFSET);
		tmp |= SDMA_HALT;
		WREG32(SDMA0_ME_CNTL + SDMA1_REGISTER_OFFSET, tmp);
	}

	evergreen_mc_stop(rdev, &save);
	if (evergreen_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}

	if (reset_mask & (RADEON_RESET_GFX | RADEON_RESET_COMPUTE | RADEON_RESET_CP))
		grbm_soft_reset = SOFT_RESET_CP | SOFT_RESET_GFX;

	if (reset_mask & RADEON_RESET_CP) {
		grbm_soft_reset |= SOFT_RESET_CP;

		srbm_soft_reset |= SOFT_RESET_GRBM;
	}

	if (reset_mask & RADEON_RESET_DMA)
		srbm_soft_reset |= SOFT_RESET_SDMA;

	if (reset_mask & RADEON_RESET_DMA1)
		srbm_soft_reset |= SOFT_RESET_SDMA1;

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

	cik_print_gpu_status_regs(rdev);
}

struct kv_reset_save_regs {
	u32 gmcon_reng_execute;
	u32 gmcon_misc;
	u32 gmcon_misc3;
};

static void kv_save_regs_for_reset(struct radeon_device *rdev,
				   struct kv_reset_save_regs *save)
{
	save->gmcon_reng_execute = RREG32(GMCON_RENG_EXECUTE);
	save->gmcon_misc = RREG32(GMCON_MISC);
	save->gmcon_misc3 = RREG32(GMCON_MISC3);

	WREG32(GMCON_RENG_EXECUTE, save->gmcon_reng_execute & ~RENG_EXECUTE_ON_PWR_UP);
	WREG32(GMCON_MISC, save->gmcon_misc & ~(RENG_EXECUTE_ON_REG_UPDATE |
						STCTRL_STUTTER_EN));
}

static void kv_restore_regs_for_reset(struct radeon_device *rdev,
				      struct kv_reset_save_regs *save)
{
	int i;

	WREG32(GMCON_PGFSM_WRITE, 0);
	WREG32(GMCON_PGFSM_CONFIG, 0x200010ff);

	for (i = 0; i < 5; i++)
		WREG32(GMCON_PGFSM_WRITE, 0);

	WREG32(GMCON_PGFSM_WRITE, 0);
	WREG32(GMCON_PGFSM_CONFIG, 0x300010ff);

	for (i = 0; i < 5; i++)
		WREG32(GMCON_PGFSM_WRITE, 0);

	WREG32(GMCON_PGFSM_WRITE, 0x210000);
	WREG32(GMCON_PGFSM_CONFIG, 0xa00010ff);

	for (i = 0; i < 5; i++)
		WREG32(GMCON_PGFSM_WRITE, 0);

	WREG32(GMCON_PGFSM_WRITE, 0x21003);
	WREG32(GMCON_PGFSM_CONFIG, 0xb00010ff);

	for (i = 0; i < 5; i++)
		WREG32(GMCON_PGFSM_WRITE, 0);

	WREG32(GMCON_PGFSM_WRITE, 0x2b00);
	WREG32(GMCON_PGFSM_CONFIG, 0xc00010ff);

	for (i = 0; i < 5; i++)
		WREG32(GMCON_PGFSM_WRITE, 0);

	WREG32(GMCON_PGFSM_WRITE, 0);
	WREG32(GMCON_PGFSM_CONFIG, 0xd00010ff);

	for (i = 0; i < 5; i++)
		WREG32(GMCON_PGFSM_WRITE, 0);

	WREG32(GMCON_PGFSM_WRITE, 0x420000);
	WREG32(GMCON_PGFSM_CONFIG, 0x100010ff);

	for (i = 0; i < 5; i++)
		WREG32(GMCON_PGFSM_WRITE, 0);

	WREG32(GMCON_PGFSM_WRITE, 0x120202);
	WREG32(GMCON_PGFSM_CONFIG, 0x500010ff);

	for (i = 0; i < 5; i++)
		WREG32(GMCON_PGFSM_WRITE, 0);

	WREG32(GMCON_PGFSM_WRITE, 0x3e3e36);
	WREG32(GMCON_PGFSM_CONFIG, 0x600010ff);

	for (i = 0; i < 5; i++)
		WREG32(GMCON_PGFSM_WRITE, 0);

	WREG32(GMCON_PGFSM_WRITE, 0x373f3e);
	WREG32(GMCON_PGFSM_CONFIG, 0x700010ff);

	for (i = 0; i < 5; i++)
		WREG32(GMCON_PGFSM_WRITE, 0);

	WREG32(GMCON_PGFSM_WRITE, 0x3e1332);
	WREG32(GMCON_PGFSM_CONFIG, 0xe00010ff);

	WREG32(GMCON_MISC3, save->gmcon_misc3);
	WREG32(GMCON_MISC, save->gmcon_misc);
	WREG32(GMCON_RENG_EXECUTE, save->gmcon_reng_execute);
}

static void cik_gpu_pci_config_reset(struct radeon_device *rdev)
{
	struct evergreen_mc_save save;
	struct kv_reset_save_regs kv_save = { 0 };
	u32 tmp, i;

	dev_info(rdev->dev, "GPU pci config reset\n");

	/* disable dpm? */

	/* disable cg/pg */
	cik_fini_pg(rdev);
	cik_fini_cg(rdev);

	/* Disable GFX parsing/prefetching */
	WREG32(CP_ME_CNTL, CP_ME_HALT | CP_PFP_HALT | CP_CE_HALT);

	/* Disable MEC parsing/prefetching */
	WREG32(CP_MEC_CNTL, MEC_ME1_HALT | MEC_ME2_HALT);

	/* sdma0 */
	tmp = RREG32(SDMA0_ME_CNTL + SDMA0_REGISTER_OFFSET);
	tmp |= SDMA_HALT;
	WREG32(SDMA0_ME_CNTL + SDMA0_REGISTER_OFFSET, tmp);
	/* sdma1 */
	tmp = RREG32(SDMA0_ME_CNTL + SDMA1_REGISTER_OFFSET);
	tmp |= SDMA_HALT;
	WREG32(SDMA0_ME_CNTL + SDMA1_REGISTER_OFFSET, tmp);
	/* XXX other engines? */

	/* halt the rlc, disable cp internal ints */
	cik_rlc_stop(rdev);

	udelay(50);

	/* disable mem access */
	evergreen_mc_stop(rdev, &save);
	if (evergreen_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timed out !\n");
	}

	if (rdev->flags & RADEON_IS_IGP)
		kv_save_regs_for_reset(rdev, &kv_save);

	/* disable BM */
	pci_clear_master(rdev->pdev);
	/* reset */
	radeon_pci_config_reset(rdev);

	udelay(100);

	/* wait for asic to come out of reset */
	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(CONFIG_MEMSIZE) != 0xffffffff)
			break;
		udelay(1);
	}

	/* does asic init need to be run first??? */
	if (rdev->flags & RADEON_IS_IGP)
		kv_restore_regs_for_reset(rdev, &kv_save);
}

/**
 * cik_asic_reset - soft reset GPU
 *
 * @rdev: radeon_device pointer
 *
 * Look up which blocks are hung and attempt
 * to reset them.
 * Returns 0 for success.
 */
int cik_asic_reset(struct radeon_device *rdev)
{
	u32 reset_mask;

	reset_mask = cik_gpu_check_soft_reset(rdev);

	if (reset_mask)
		r600_set_bios_scratch_engine_hung(rdev, true);

	/* try soft reset */
	cik_gpu_soft_reset(rdev, reset_mask);

	reset_mask = cik_gpu_check_soft_reset(rdev);

	/* try pci config reset */
	if (reset_mask && radeon_hard_reset)
		cik_gpu_pci_config_reset(rdev);

	reset_mask = cik_gpu_check_soft_reset(rdev);

	if (!reset_mask)
		r600_set_bios_scratch_engine_hung(rdev, false);

	return 0;
}

/**
 * cik_gfx_is_lockup - check if the 3D engine is locked up
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Check if the 3D engine is locked up (CIK).
 * Returns true if the engine is locked, false if not.
 */
bool cik_gfx_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring)
{
	u32 reset_mask = cik_gpu_check_soft_reset(rdev);

	if (!(reset_mask & (RADEON_RESET_GFX |
			    RADEON_RESET_COMPUTE |
			    RADEON_RESET_CP))) {
		radeon_ring_lockup_update(rdev, ring);
		return false;
	}
	return radeon_ring_test_lockup(rdev, ring);
}

/* MC */
/**
 * cik_mc_program - program the GPU memory controller
 *
 * @rdev: radeon_device pointer
 *
 * Set the location of vram, gart, and AGP in the GPU's
 * physical address space (CIK).
 */
static void cik_mc_program(struct radeon_device *rdev)
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
	/* we need to own VRAM, so turn off the VGA renderer here
	 * to stop it overwriting our objects */
	rv515_vga_render_disable(rdev);
}

/**
 * cik_mc_init - initialize the memory controller driver params
 *
 * @rdev: radeon_device pointer
 *
 * Look up the amount of vram, vram width, and decide how to place
 * vram and gart within the GPU's physical address space (CIK).
 * Returns 0 for success.
 */
static int cik_mc_init(struct radeon_device *rdev)
{
	u32 tmp;
	int chansize, numchan;

	/* Get VRAM informations */
	rdev->mc.vram_is_ddr = true;
	tmp = RREG32(MC_ARB_RAMCFG);
	if (tmp & CHANSIZE_MASK) {
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
 * VMID 0 is the physical GPU addresses as used by the kernel.
 * VMIDs 1-15 are used for userspace clients and are handled
 * by the radeon vm/hsa code.
 */
/**
 * cik_pcie_gart_tlb_flush - gart tlb flush callback
 *
 * @rdev: radeon_device pointer
 *
 * Flush the TLB for the VMID 0 page table (CIK).
 */
void cik_pcie_gart_tlb_flush(struct radeon_device *rdev)
{
	/* flush hdp cache */
	WREG32(HDP_MEM_COHERENCY_FLUSH_CNTL, 0);

	/* bits 0-15 are the VM contexts0-15 */
	WREG32(VM_INVALIDATE_REQUEST, 0x1);
}

static void cik_pcie_init_compute_vmid(struct radeon_device *rdev)
{
	int i;
	uint32_t sh_mem_bases, sh_mem_config;

	sh_mem_bases = 0x6000 | 0x6000 << 16;
	sh_mem_config = ALIGNMENT_MODE(SH_MEM_ALIGNMENT_MODE_UNALIGNED);
	sh_mem_config |= DEFAULT_MTYPE(MTYPE_NONCACHED);

	mutex_lock(&rdev->srbm_mutex);
	for (i = 8; i < 16; i++) {
		cik_srbm_select(rdev, 0, 0, 0, i);
		/* CP and shaders */
		WREG32(SH_MEM_CONFIG, sh_mem_config);
		WREG32(SH_MEM_APE1_BASE, 1);
		WREG32(SH_MEM_APE1_LIMIT, 0);
		WREG32(SH_MEM_BASES, sh_mem_bases);
	}
	cik_srbm_select(rdev, 0, 0, 0, 0);
	mutex_unlock(&rdev->srbm_mutex);
}

/**
 * cik_pcie_gart_enable - gart enable
 *
 * @rdev: radeon_device pointer
 *
 * This sets up the TLBs, programs the page tables for VMID0,
 * sets up the hw for VMIDs 1-15 which are allocated on
 * demand, and sets up the global locations for the LDS, GDS,
 * and GPUVM for FSA64 clients (CIK).
 * Returns 0 for success, errors for failure.
 */
static int cik_pcie_gart_enable(struct radeon_device *rdev)
{
	int r, i;

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
	       BANK_SELECT(4) |
	       L2_CACHE_BIGK_FRAGMENT_SIZE(4));
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

	/* restore context1-15 */
	/* set vm size, must be a multiple of 4 */
	WREG32(VM_CONTEXT1_PAGE_TABLE_START_ADDR, 0);
	WREG32(VM_CONTEXT1_PAGE_TABLE_END_ADDR, rdev->vm_manager.max_pfn);
	for (i = 1; i < 16; i++) {
		if (i < 8)
			WREG32(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (i << 2),
			       rdev->vm_manager.saved_table_addr[i]);
		else
			WREG32(VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + ((i - 8) << 2),
			       rdev->vm_manager.saved_table_addr[i]);
	}

	/* enable context1-15 */
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

	if (rdev->family == CHIP_KAVERI) {
		u32 tmp = RREG32(CHUB_CONTROL);
		tmp &= ~BYPASS_VM;
		WREG32(CHUB_CONTROL, tmp);
	}

	/* XXX SH_MEM regs */
	/* where to put LDS, scratch, GPUVM in FSA64 space */
	mutex_lock(&rdev->srbm_mutex);
	for (i = 0; i < 16; i++) {
		cik_srbm_select(rdev, 0, 0, 0, i);
		/* CP and shaders */
		WREG32(SH_MEM_CONFIG, 0);
		WREG32(SH_MEM_APE1_BASE, 1);
		WREG32(SH_MEM_APE1_LIMIT, 0);
		WREG32(SH_MEM_BASES, 0);
		/* SDMA GFX */
		WREG32(SDMA0_GFX_VIRTUAL_ADDR + SDMA0_REGISTER_OFFSET, 0);
		WREG32(SDMA0_GFX_APE1_CNTL + SDMA0_REGISTER_OFFSET, 0);
		WREG32(SDMA0_GFX_VIRTUAL_ADDR + SDMA1_REGISTER_OFFSET, 0);
		WREG32(SDMA0_GFX_APE1_CNTL + SDMA1_REGISTER_OFFSET, 0);
		/* XXX SDMA RLC - todo */
	}
	cik_srbm_select(rdev, 0, 0, 0, 0);
	mutex_unlock(&rdev->srbm_mutex);

	cik_pcie_init_compute_vmid(rdev);

	cik_pcie_gart_tlb_flush(rdev);
	DRM_INFO("PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(rdev->mc.gtt_size >> 20),
		 (unsigned long long)rdev->gart.table_addr);
	rdev->gart.ready = true;
	return 0;
}

/**
 * cik_pcie_gart_disable - gart disable
 *
 * @rdev: radeon_device pointer
 *
 * This disables all VM page table (CIK).
 */
static void cik_pcie_gart_disable(struct radeon_device *rdev)
{
	unsigned i;

	for (i = 1; i < 16; ++i) {
		uint32_t reg;
		if (i < 8)
			reg = VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (i << 2);
		else
			reg = VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + ((i - 8) << 2);
		rdev->vm_manager.saved_table_addr[i] = RREG32(reg);
	}

	/* Disable all tables */
	WREG32(VM_CONTEXT0_CNTL, 0);
	WREG32(VM_CONTEXT1_CNTL, 0);
	/* Setup TLB control */
	WREG32(MC_VM_MX_L1_TLB_CNTL, SYSTEM_ACCESS_MODE_NOT_IN_SYS |
	       SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU);
	/* Setup L2 cache */
	WREG32(VM_L2_CNTL,
	       ENABLE_L2_FRAGMENT_PROCESSING |
	       ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE |
	       ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE |
	       EFFECTIVE_L2_QUEUE_SIZE(7) |
	       CONTEXT1_IDENTITY_ACCESS_MODE(1));
	WREG32(VM_L2_CNTL2, 0);
	WREG32(VM_L2_CNTL3, L2_CACHE_BIGK_ASSOCIATIVITY |
	       L2_CACHE_BIGK_FRAGMENT_SIZE(6));
	radeon_gart_table_vram_unpin(rdev);
}

/**
 * cik_pcie_gart_fini - vm fini callback
 *
 * @rdev: radeon_device pointer
 *
 * Tears down the driver GART/VM setup (CIK).
 */
static void cik_pcie_gart_fini(struct radeon_device *rdev)
{
	cik_pcie_gart_disable(rdev);
	radeon_gart_table_vram_free(rdev);
	radeon_gart_fini(rdev);
}

/* vm parser */
/**
 * cik_ib_parse - vm ib_parse callback
 *
 * @rdev: radeon_device pointer
 * @ib: indirect buffer pointer
 *
 * CIK uses hw IB checking so this is a nop (CIK).
 */
int cik_ib_parse(struct radeon_device *rdev, struct radeon_ib *ib)
{
	return 0;
}

/*
 * vm
 * VMID 0 is the physical GPU addresses as used by the kernel.
 * VMIDs 1-15 are used for userspace clients and are handled
 * by the radeon vm/hsa code.
 */
/**
 * cik_vm_init - cik vm init callback
 *
 * @rdev: radeon_device pointer
 *
 * Inits cik specific vm parameters (number of VMs, base of vram for
 * VMIDs 1-15) (CIK).
 * Returns 0 for success.
 */
int cik_vm_init(struct radeon_device *rdev)
{
	/*
	 * number of VMs
	 * VMID 0 is reserved for System
	 * radeon graphics/compute will use VMIDs 1-7
	 * amdkfd will use VMIDs 8-15
	 */
	rdev->vm_manager.nvm = RADEON_NUM_OF_VMIDS;
	/* base offset of vram pages */
	if (rdev->flags & RADEON_IS_IGP) {
		u64 tmp = RREG32(MC_VM_FB_OFFSET);
		tmp <<= 22;
		rdev->vm_manager.vram_base_offset = tmp;
	} else
		rdev->vm_manager.vram_base_offset = 0;

	return 0;
}

/**
 * cik_vm_fini - cik vm fini callback
 *
 * @rdev: radeon_device pointer
 *
 * Tear down any asic specific VM setup (CIK).
 */
void cik_vm_fini(struct radeon_device *rdev)
{
}

/**
 * cik_vm_decode_fault - print human readable fault info
 *
 * @rdev: radeon_device pointer
 * @status: VM_CONTEXT1_PROTECTION_FAULT_STATUS register value
 * @addr: VM_CONTEXT1_PROTECTION_FAULT_ADDR register value
 *
 * Print human readable fault information (CIK).
 */
static void cik_vm_decode_fault(struct radeon_device *rdev,
				u32 status, u32 addr, u32 mc_client)
{
	u32 mc_id;
	u32 vmid = (status & FAULT_VMID_MASK) >> FAULT_VMID_SHIFT;
	u32 protections = (status & PROTECTIONS_MASK) >> PROTECTIONS_SHIFT;
	char block[5] = { mc_client >> 24, (mc_client >> 16) & 0xff,
		(mc_client >> 8) & 0xff, mc_client & 0xff, 0 };

	if (rdev->family == CHIP_HAWAII)
		mc_id = (status & HAWAII_MEMORY_CLIENT_ID_MASK) >> MEMORY_CLIENT_ID_SHIFT;
	else
		mc_id = (status & MEMORY_CLIENT_ID_MASK) >> MEMORY_CLIENT_ID_SHIFT;

	printk("VM fault (0x%02x, vmid %d) at page %u, %s from '%s' (0x%08x) (%d)\n",
	       protections, vmid, addr,
	       (status & MEMORY_CLIENT_RW_MASK) ? "write" : "read",
	       block, mc_client, mc_id);
}

/**
 * cik_vm_flush - cik vm flush using the CP
 *
 * @rdev: radeon_device pointer
 *
 * Update the page table base and flush the VM TLB
 * using the CP (CIK).
 */
void cik_vm_flush(struct radeon_device *rdev, struct radeon_ring *ring,
		  unsigned vm_id, uint64_t pd_addr)
{
	int usepfp = (ring->idx == RADEON_RING_TYPE_GFX_INDEX);

	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(usepfp) |
				 WRITE_DATA_DST_SEL(0)));
	if (vm_id < 8) {
		radeon_ring_write(ring,
				  (VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (vm_id << 2)) >> 2);
	} else {
		radeon_ring_write(ring,
				  (VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + ((vm_id - 8) << 2)) >> 2);
	}
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, pd_addr >> 12);

	/* update SH_MEM_* regs */
	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(usepfp) |
				 WRITE_DATA_DST_SEL(0)));
	radeon_ring_write(ring, SRBM_GFX_CNTL >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, VMID(vm_id));

	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 6));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(usepfp) |
				 WRITE_DATA_DST_SEL(0)));
	radeon_ring_write(ring, SH_MEM_BASES >> 2);
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, 0); /* SH_MEM_BASES */
	radeon_ring_write(ring, 0); /* SH_MEM_CONFIG */
	radeon_ring_write(ring, 1); /* SH_MEM_APE1_BASE */
	radeon_ring_write(ring, 0); /* SH_MEM_APE1_LIMIT */

	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(usepfp) |
				 WRITE_DATA_DST_SEL(0)));
	radeon_ring_write(ring, SRBM_GFX_CNTL >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, VMID(0));

	/* HDP flush */
	cik_hdp_flush_cp_ring_emit(rdev, ring->idx);

	/* bits 0-15 are the VM contexts0-15 */
	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(usepfp) |
				 WRITE_DATA_DST_SEL(0)));
	radeon_ring_write(ring, VM_INVALIDATE_REQUEST >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 1 << vm_id);

	/* wait for the invalidate to complete */
	radeon_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	radeon_ring_write(ring, (WAIT_REG_MEM_OPERATION(0) | /* wait */
				 WAIT_REG_MEM_FUNCTION(0) |  /* always */
				 WAIT_REG_MEM_ENGINE(0))); /* me */
	radeon_ring_write(ring, VM_INVALIDATE_REQUEST >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 0); /* ref */
	radeon_ring_write(ring, 0); /* mask */
	radeon_ring_write(ring, 0x20); /* poll interval */

	/* compute doesn't have PFP */
	if (usepfp) {
		/* sync PFP to ME, otherwise we might get invalid PFP reads */
		radeon_ring_write(ring, PACKET3(PACKET3_PFP_SYNC_ME, 0));
		radeon_ring_write(ring, 0x0);
	}
}

/*
 * RLC
 * The RLC is a multi-purpose microengine that handles a
 * variety of functions, the most important of which is
 * the interrupt controller.
 */
static void cik_enable_gui_idle_interrupt(struct radeon_device *rdev,
					  bool enable)
{
	u32 tmp = RREG32(CP_INT_CNTL_RING0);

	if (enable)
		tmp |= (CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE);
	else
		tmp &= ~(CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE);
	WREG32(CP_INT_CNTL_RING0, tmp);
}

static void cik_enable_lbpw(struct radeon_device *rdev, bool enable)
{
	u32 tmp;

	tmp = RREG32(RLC_LB_CNTL);
	if (enable)
		tmp |= LOAD_BALANCE_ENABLE;
	else
		tmp &= ~LOAD_BALANCE_ENABLE;
	WREG32(RLC_LB_CNTL, tmp);
}

static void cik_wait_for_rlc_serdes(struct radeon_device *rdev)
{
	u32 i, j, k;
	u32 mask;

	mutex_lock(&rdev->grbm_idx_mutex);
	for (i = 0; i < rdev->config.cik.max_shader_engines; i++) {
		for (j = 0; j < rdev->config.cik.max_sh_per_se; j++) {
			cik_select_se_sh(rdev, i, j);
			for (k = 0; k < rdev->usec_timeout; k++) {
				if (RREG32(RLC_SERDES_CU_MASTER_BUSY) == 0)
					break;
				udelay(1);
			}
		}
	}
	cik_select_se_sh(rdev, 0xffffffff, 0xffffffff);
	mutex_unlock(&rdev->grbm_idx_mutex);

	mask = SE_MASTER_BUSY_MASK | GC_MASTER_BUSY | TC0_MASTER_BUSY | TC1_MASTER_BUSY;
	for (k = 0; k < rdev->usec_timeout; k++) {
		if ((RREG32(RLC_SERDES_NONCU_MASTER_BUSY) & mask) == 0)
			break;
		udelay(1);
	}
}

static void cik_update_rlc(struct radeon_device *rdev, u32 rlc)
{
	u32 tmp;

	tmp = RREG32(RLC_CNTL);
	if (tmp != rlc)
		WREG32(RLC_CNTL, rlc);
}

static u32 cik_halt_rlc(struct radeon_device *rdev)
{
	u32 data, orig;

	orig = data = RREG32(RLC_CNTL);

	if (data & RLC_ENABLE) {
		u32 i;

		data &= ~RLC_ENABLE;
		WREG32(RLC_CNTL, data);

		for (i = 0; i < rdev->usec_timeout; i++) {
			if ((RREG32(RLC_GPM_STAT) & RLC_GPM_BUSY) == 0)
				break;
			udelay(1);
		}

		cik_wait_for_rlc_serdes(rdev);
	}

	return orig;
}

void cik_enter_rlc_safe_mode(struct radeon_device *rdev)
{
	u32 tmp, i, mask;

	tmp = REQ | MESSAGE(MSG_ENTER_RLC_SAFE_MODE);
	WREG32(RLC_GPR_REG2, tmp);

	mask = GFX_POWER_STATUS | GFX_CLOCK_STATUS;
	for (i = 0; i < rdev->usec_timeout; i++) {
		if ((RREG32(RLC_GPM_STAT) & mask) == mask)
			break;
		udelay(1);
	}

	for (i = 0; i < rdev->usec_timeout; i++) {
		if ((RREG32(RLC_GPR_REG2) & REQ) == 0)
			break;
		udelay(1);
	}
}

void cik_exit_rlc_safe_mode(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = REQ | MESSAGE(MSG_EXIT_RLC_SAFE_MODE);
	WREG32(RLC_GPR_REG2, tmp);
}

/**
 * cik_rlc_stop - stop the RLC ME
 *
 * @rdev: radeon_device pointer
 *
 * Halt the RLC ME (MicroEngine) (CIK).
 */
static void cik_rlc_stop(struct radeon_device *rdev)
{
	WREG32(RLC_CNTL, 0);

	cik_enable_gui_idle_interrupt(rdev, false);

	cik_wait_for_rlc_serdes(rdev);
}

/**
 * cik_rlc_start - start the RLC ME
 *
 * @rdev: radeon_device pointer
 *
 * Unhalt the RLC ME (MicroEngine) (CIK).
 */
static void cik_rlc_start(struct radeon_device *rdev)
{
	WREG32(RLC_CNTL, RLC_ENABLE);

	cik_enable_gui_idle_interrupt(rdev, true);

	udelay(50);
}

/**
 * cik_rlc_resume - setup the RLC hw
 *
 * @rdev: radeon_device pointer
 *
 * Initialize the RLC registers, load the ucode,
 * and start the RLC (CIK).
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int cik_rlc_resume(struct radeon_device *rdev)
{
	u32 i, size, tmp;

	if (!rdev->rlc_fw)
		return -EINVAL;

	cik_rlc_stop(rdev);

	/* disable CG */
	tmp = RREG32(RLC_CGCG_CGLS_CTRL) & 0xfffffffc;
	WREG32(RLC_CGCG_CGLS_CTRL, tmp);

	si_rlc_reset(rdev);

	cik_init_pg(rdev);

	cik_init_cg(rdev);

	WREG32(RLC_LB_CNTR_INIT, 0);
	WREG32(RLC_LB_CNTR_MAX, 0x00008000);

	mutex_lock(&rdev->grbm_idx_mutex);
	cik_select_se_sh(rdev, 0xffffffff, 0xffffffff);
	WREG32(RLC_LB_INIT_CU_MASK, 0xffffffff);
	WREG32(RLC_LB_PARAMS, 0x00600408);
	WREG32(RLC_LB_CNTL, 0x80000004);
	mutex_unlock(&rdev->grbm_idx_mutex);

	WREG32(RLC_MC_CNTL, 0);
	WREG32(RLC_UCODE_CNTL, 0);

	if (rdev->new_fw) {
		const struct rlc_firmware_header_v1_0 *hdr =
			(const struct rlc_firmware_header_v1_0 *)rdev->rlc_fw->data;
		const __le32 *fw_data = (const __le32 *)
			(rdev->rlc_fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));

		radeon_ucode_print_rlc_hdr(&hdr->header);

		size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;
		WREG32(RLC_GPM_UCODE_ADDR, 0);
		for (i = 0; i < size; i++)
			WREG32(RLC_GPM_UCODE_DATA, le32_to_cpup(fw_data++));
		WREG32(RLC_GPM_UCODE_ADDR, le32_to_cpu(hdr->header.ucode_version));
	} else {
		const __be32 *fw_data;

		switch (rdev->family) {
		case CHIP_BONAIRE:
		case CHIP_HAWAII:
		default:
			size = BONAIRE_RLC_UCODE_SIZE;
			break;
		case CHIP_KAVERI:
			size = KV_RLC_UCODE_SIZE;
			break;
		case CHIP_KABINI:
			size = KB_RLC_UCODE_SIZE;
			break;
		case CHIP_MULLINS:
			size = ML_RLC_UCODE_SIZE;
			break;
		}

		fw_data = (const __be32 *)rdev->rlc_fw->data;
		WREG32(RLC_GPM_UCODE_ADDR, 0);
		for (i = 0; i < size; i++)
			WREG32(RLC_GPM_UCODE_DATA, be32_to_cpup(fw_data++));
		WREG32(RLC_GPM_UCODE_ADDR, 0);
	}

	/* XXX - find out what chips support lbpw */
	cik_enable_lbpw(rdev, false);

	if (rdev->family == CHIP_BONAIRE)
		WREG32(RLC_DRIVER_DMA_STATUS, 0);

	cik_rlc_start(rdev);

	return 0;
}

static void cik_enable_cgcg(struct radeon_device *rdev, bool enable)
{
	u32 data, orig, tmp, tmp2;

	orig = data = RREG32(RLC_CGCG_CGLS_CTRL);

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_GFX_CGCG)) {
		cik_enable_gui_idle_interrupt(rdev, true);

		tmp = cik_halt_rlc(rdev);

		mutex_lock(&rdev->grbm_idx_mutex);
		cik_select_se_sh(rdev, 0xffffffff, 0xffffffff);
		WREG32(RLC_SERDES_WR_CU_MASTER_MASK, 0xffffffff);
		WREG32(RLC_SERDES_WR_NONCU_MASTER_MASK, 0xffffffff);
		tmp2 = BPM_ADDR_MASK | CGCG_OVERRIDE_0 | CGLS_ENABLE;
		WREG32(RLC_SERDES_WR_CTRL, tmp2);
		mutex_unlock(&rdev->grbm_idx_mutex);

		cik_update_rlc(rdev, tmp);

		data |= CGCG_EN | CGLS_EN;
	} else {
		cik_enable_gui_idle_interrupt(rdev, false);

		RREG32(CB_CGTT_SCLK_CTRL);
		RREG32(CB_CGTT_SCLK_CTRL);
		RREG32(CB_CGTT_SCLK_CTRL);
		RREG32(CB_CGTT_SCLK_CTRL);

		data &= ~(CGCG_EN | CGLS_EN);
	}

	if (orig != data)
		WREG32(RLC_CGCG_CGLS_CTRL, data);

}

static void cik_enable_mgcg(struct radeon_device *rdev, bool enable)
{
	u32 data, orig, tmp = 0;

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_GFX_MGCG)) {
		if (rdev->cg_flags & RADEON_CG_SUPPORT_GFX_MGLS) {
			if (rdev->cg_flags & RADEON_CG_SUPPORT_GFX_CP_LS) {
				orig = data = RREG32(CP_MEM_SLP_CNTL);
				data |= CP_MEM_LS_EN;
				if (orig != data)
					WREG32(CP_MEM_SLP_CNTL, data);
			}
		}

		orig = data = RREG32(RLC_CGTT_MGCG_OVERRIDE);
		data |= 0x00000001;
		data &= 0xfffffffd;
		if (orig != data)
			WREG32(RLC_CGTT_MGCG_OVERRIDE, data);

		tmp = cik_halt_rlc(rdev);

		mutex_lock(&rdev->grbm_idx_mutex);
		cik_select_se_sh(rdev, 0xffffffff, 0xffffffff);
		WREG32(RLC_SERDES_WR_CU_MASTER_MASK, 0xffffffff);
		WREG32(RLC_SERDES_WR_NONCU_MASTER_MASK, 0xffffffff);
		data = BPM_ADDR_MASK | MGCG_OVERRIDE_0;
		WREG32(RLC_SERDES_WR_CTRL, data);
		mutex_unlock(&rdev->grbm_idx_mutex);

		cik_update_rlc(rdev, tmp);

		if (rdev->cg_flags & RADEON_CG_SUPPORT_GFX_CGTS) {
			orig = data = RREG32(CGTS_SM_CTRL_REG);
			data &= ~SM_MODE_MASK;
			data |= SM_MODE(0x2);
			data |= SM_MODE_ENABLE;
			data &= ~CGTS_OVERRIDE;
			if ((rdev->cg_flags & RADEON_CG_SUPPORT_GFX_MGLS) &&
			    (rdev->cg_flags & RADEON_CG_SUPPORT_GFX_CGTS_LS))
				data &= ~CGTS_LS_OVERRIDE;
			data &= ~ON_MONITOR_ADD_MASK;
			data |= ON_MONITOR_ADD_EN;
			data |= ON_MONITOR_ADD(0x96);
			if (orig != data)
				WREG32(CGTS_SM_CTRL_REG, data);
		}
	} else {
		orig = data = RREG32(RLC_CGTT_MGCG_OVERRIDE);
		data |= 0x00000003;
		if (orig != data)
			WREG32(RLC_CGTT_MGCG_OVERRIDE, data);

		data = RREG32(RLC_MEM_SLP_CNTL);
		if (data & RLC_MEM_LS_EN) {
			data &= ~RLC_MEM_LS_EN;
			WREG32(RLC_MEM_SLP_CNTL, data);
		}

		data = RREG32(CP_MEM_SLP_CNTL);
		if (data & CP_MEM_LS_EN) {
			data &= ~CP_MEM_LS_EN;
			WREG32(CP_MEM_SLP_CNTL, data);
		}

		orig = data = RREG32(CGTS_SM_CTRL_REG);
		data |= CGTS_OVERRIDE | CGTS_LS_OVERRIDE;
		if (orig != data)
			WREG32(CGTS_SM_CTRL_REG, data);

		tmp = cik_halt_rlc(rdev);

		mutex_lock(&rdev->grbm_idx_mutex);
		cik_select_se_sh(rdev, 0xffffffff, 0xffffffff);
		WREG32(RLC_SERDES_WR_CU_MASTER_MASK, 0xffffffff);
		WREG32(RLC_SERDES_WR_NONCU_MASTER_MASK, 0xffffffff);
		data = BPM_ADDR_MASK | MGCG_OVERRIDE_1;
		WREG32(RLC_SERDES_WR_CTRL, data);
		mutex_unlock(&rdev->grbm_idx_mutex);

		cik_update_rlc(rdev, tmp);
	}
}

static const u32 mc_cg_registers[] =
{
	MC_HUB_MISC_HUB_CG,
	MC_HUB_MISC_SIP_CG,
	MC_HUB_MISC_VM_CG,
	MC_XPB_CLK_GAT,
	ATC_MISC_CG,
	MC_CITF_MISC_WR_CG,
	MC_CITF_MISC_RD_CG,
	MC_CITF_MISC_VM_CG,
	VM_L2_CG,
};

static void cik_enable_mc_ls(struct radeon_device *rdev,
			     bool enable)
{
	int i;
	u32 orig, data;

	for (i = 0; i < ARRAY_SIZE(mc_cg_registers); i++) {
		orig = data = RREG32(mc_cg_registers[i]);
		if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_MC_LS))
			data |= MC_LS_ENABLE;
		else
			data &= ~MC_LS_ENABLE;
		if (data != orig)
			WREG32(mc_cg_registers[i], data);
	}
}

static void cik_enable_mc_mgcg(struct radeon_device *rdev,
			       bool enable)
{
	int i;
	u32 orig, data;

	for (i = 0; i < ARRAY_SIZE(mc_cg_registers); i++) {
		orig = data = RREG32(mc_cg_registers[i]);
		if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_MC_MGCG))
			data |= MC_CG_ENABLE;
		else
			data &= ~MC_CG_ENABLE;
		if (data != orig)
			WREG32(mc_cg_registers[i], data);
	}
}

static void cik_enable_sdma_mgcg(struct radeon_device *rdev,
				 bool enable)
{
	u32 orig, data;

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_SDMA_MGCG)) {
		WREG32(SDMA0_CLK_CTRL + SDMA0_REGISTER_OFFSET, 0x00000100);
		WREG32(SDMA0_CLK_CTRL + SDMA1_REGISTER_OFFSET, 0x00000100);
	} else {
		orig = data = RREG32(SDMA0_CLK_CTRL + SDMA0_REGISTER_OFFSET);
		data |= 0xff000000;
		if (data != orig)
			WREG32(SDMA0_CLK_CTRL + SDMA0_REGISTER_OFFSET, data);

		orig = data = RREG32(SDMA0_CLK_CTRL + SDMA1_REGISTER_OFFSET);
		data |= 0xff000000;
		if (data != orig)
			WREG32(SDMA0_CLK_CTRL + SDMA1_REGISTER_OFFSET, data);
	}
}

static void cik_enable_sdma_mgls(struct radeon_device *rdev,
				 bool enable)
{
	u32 orig, data;

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_SDMA_LS)) {
		orig = data = RREG32(SDMA0_POWER_CNTL + SDMA0_REGISTER_OFFSET);
		data |= 0x100;
		if (orig != data)
			WREG32(SDMA0_POWER_CNTL + SDMA0_REGISTER_OFFSET, data);

		orig = data = RREG32(SDMA0_POWER_CNTL + SDMA1_REGISTER_OFFSET);
		data |= 0x100;
		if (orig != data)
			WREG32(SDMA0_POWER_CNTL + SDMA1_REGISTER_OFFSET, data);
	} else {
		orig = data = RREG32(SDMA0_POWER_CNTL + SDMA0_REGISTER_OFFSET);
		data &= ~0x100;
		if (orig != data)
			WREG32(SDMA0_POWER_CNTL + SDMA0_REGISTER_OFFSET, data);

		orig = data = RREG32(SDMA0_POWER_CNTL + SDMA1_REGISTER_OFFSET);
		data &= ~0x100;
		if (orig != data)
			WREG32(SDMA0_POWER_CNTL + SDMA1_REGISTER_OFFSET, data);
	}
}

static void cik_enable_uvd_mgcg(struct radeon_device *rdev,
				bool enable)
{
	u32 orig, data;

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_UVD_MGCG)) {
		data = RREG32_UVD_CTX(UVD_CGC_MEM_CTRL);
		data = 0xfff;
		WREG32_UVD_CTX(UVD_CGC_MEM_CTRL, data);

		orig = data = RREG32(UVD_CGC_CTRL);
		data |= DCM;
		if (orig != data)
			WREG32(UVD_CGC_CTRL, data);
	} else {
		data = RREG32_UVD_CTX(UVD_CGC_MEM_CTRL);
		data &= ~0xfff;
		WREG32_UVD_CTX(UVD_CGC_MEM_CTRL, data);

		orig = data = RREG32(UVD_CGC_CTRL);
		data &= ~DCM;
		if (orig != data)
			WREG32(UVD_CGC_CTRL, data);
	}
}

static void cik_enable_bif_mgls(struct radeon_device *rdev,
			       bool enable)
{
	u32 orig, data;

	orig = data = RREG32_PCIE_PORT(PCIE_CNTL2);

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_BIF_LS))
		data |= SLV_MEM_LS_EN | MST_MEM_LS_EN |
			REPLAY_MEM_LS_EN | SLV_MEM_AGGRESSIVE_LS_EN;
	else
		data &= ~(SLV_MEM_LS_EN | MST_MEM_LS_EN |
			  REPLAY_MEM_LS_EN | SLV_MEM_AGGRESSIVE_LS_EN);

	if (orig != data)
		WREG32_PCIE_PORT(PCIE_CNTL2, data);
}

static void cik_enable_hdp_mgcg(struct radeon_device *rdev,
				bool enable)
{
	u32 orig, data;

	orig = data = RREG32(HDP_HOST_PATH_CNTL);

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_HDP_MGCG))
		data &= ~CLOCK_GATING_DIS;
	else
		data |= CLOCK_GATING_DIS;

	if (orig != data)
		WREG32(HDP_HOST_PATH_CNTL, data);
}

static void cik_enable_hdp_ls(struct radeon_device *rdev,
			      bool enable)
{
	u32 orig, data;

	orig = data = RREG32(HDP_MEM_POWER_LS);

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_HDP_LS))
		data |= HDP_LS_ENABLE;
	else
		data &= ~HDP_LS_ENABLE;

	if (orig != data)
		WREG32(HDP_MEM_POWER_LS, data);
}

void cik_update_cg(struct radeon_device *rdev,
		   u32 block, bool enable)
{

	if (block & RADEON_CG_BLOCK_GFX) {
		cik_enable_gui_idle_interrupt(rdev, false);
		/* order matters! */
		if (enable) {
			cik_enable_mgcg(rdev, true);
			cik_enable_cgcg(rdev, true);
		} else {
			cik_enable_cgcg(rdev, false);
			cik_enable_mgcg(rdev, false);
		}
		cik_enable_gui_idle_interrupt(rdev, true);
	}

	if (block & RADEON_CG_BLOCK_MC) {
		if (!(rdev->flags & RADEON_IS_IGP)) {
			cik_enable_mc_mgcg(rdev, enable);
			cik_enable_mc_ls(rdev, enable);
		}
	}

	if (block & RADEON_CG_BLOCK_SDMA) {
		cik_enable_sdma_mgcg(rdev, enable);
		cik_enable_sdma_mgls(rdev, enable);
	}

	if (block & RADEON_CG_BLOCK_BIF) {
		cik_enable_bif_mgls(rdev, enable);
	}

	if (block & RADEON_CG_BLOCK_UVD) {
		if (rdev->has_uvd)
			cik_enable_uvd_mgcg(rdev, enable);
	}

	if (block & RADEON_CG_BLOCK_HDP) {
		cik_enable_hdp_mgcg(rdev, enable);
		cik_enable_hdp_ls(rdev, enable);
	}

	if (block & RADEON_CG_BLOCK_VCE) {
		vce_v2_0_enable_mgcg(rdev, enable);
	}
}

static void cik_init_cg(struct radeon_device *rdev)
{

	cik_update_cg(rdev, RADEON_CG_BLOCK_GFX, true);

	if (rdev->has_uvd)
		si_init_uvd_internal_cg(rdev);

	cik_update_cg(rdev, (RADEON_CG_BLOCK_MC |
			     RADEON_CG_BLOCK_SDMA |
			     RADEON_CG_BLOCK_BIF |
			     RADEON_CG_BLOCK_UVD |
			     RADEON_CG_BLOCK_HDP), true);
}

static void cik_fini_cg(struct radeon_device *rdev)
{
	cik_update_cg(rdev, (RADEON_CG_BLOCK_MC |
			     RADEON_CG_BLOCK_SDMA |
			     RADEON_CG_BLOCK_BIF |
			     RADEON_CG_BLOCK_UVD |
			     RADEON_CG_BLOCK_HDP), false);

	cik_update_cg(rdev, RADEON_CG_BLOCK_GFX, false);
}

static void cik_enable_sck_slowdown_on_pu(struct radeon_device *rdev,
					  bool enable)
{
	u32 data, orig;

	orig = data = RREG32(RLC_PG_CNTL);
	if (enable && (rdev->pg_flags & RADEON_PG_SUPPORT_RLC_SMU_HS))
		data |= SMU_CLK_SLOWDOWN_ON_PU_ENABLE;
	else
		data &= ~SMU_CLK_SLOWDOWN_ON_PU_ENABLE;
	if (orig != data)
		WREG32(RLC_PG_CNTL, data);
}

static void cik_enable_sck_slowdown_on_pd(struct radeon_device *rdev,
					  bool enable)
{
	u32 data, orig;

	orig = data = RREG32(RLC_PG_CNTL);
	if (enable && (rdev->pg_flags & RADEON_PG_SUPPORT_RLC_SMU_HS))
		data |= SMU_CLK_SLOWDOWN_ON_PD_ENABLE;
	else
		data &= ~SMU_CLK_SLOWDOWN_ON_PD_ENABLE;
	if (orig != data)
		WREG32(RLC_PG_CNTL, data);
}

static void cik_enable_cp_pg(struct radeon_device *rdev, bool enable)
{
	u32 data, orig;

	orig = data = RREG32(RLC_PG_CNTL);
	if (enable && (rdev->pg_flags & RADEON_PG_SUPPORT_CP))
		data &= ~DISABLE_CP_PG;
	else
		data |= DISABLE_CP_PG;
	if (orig != data)
		WREG32(RLC_PG_CNTL, data);
}

static void cik_enable_gds_pg(struct radeon_device *rdev, bool enable)
{
	u32 data, orig;

	orig = data = RREG32(RLC_PG_CNTL);
	if (enable && (rdev->pg_flags & RADEON_PG_SUPPORT_GDS))
		data &= ~DISABLE_GDS_PG;
	else
		data |= DISABLE_GDS_PG;
	if (orig != data)
		WREG32(RLC_PG_CNTL, data);
}

#define CP_ME_TABLE_SIZE    96
#define CP_ME_TABLE_OFFSET  2048
#define CP_MEC_TABLE_OFFSET 4096

void cik_init_cp_pg_table(struct radeon_device *rdev)
{
	volatile u32 *dst_ptr;
	int me, i, max_me = 4;
	u32 bo_offset = 0;
	u32 table_offset, table_size;

	if (rdev->family == CHIP_KAVERI)
		max_me = 5;

	if (rdev->rlc.cp_table_ptr == NULL)
		return;

	/* write the cp table buffer */
	dst_ptr = rdev->rlc.cp_table_ptr;
	for (me = 0; me < max_me; me++) {
		if (rdev->new_fw) {
			const __le32 *fw_data;
			const struct gfx_firmware_header_v1_0 *hdr;

			if (me == 0) {
				hdr = (const struct gfx_firmware_header_v1_0 *)rdev->ce_fw->data;
				fw_data = (const __le32 *)
					(rdev->ce_fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));
				table_offset = le32_to_cpu(hdr->jt_offset);
				table_size = le32_to_cpu(hdr->jt_size);
			} else if (me == 1) {
				hdr = (const struct gfx_firmware_header_v1_0 *)rdev->pfp_fw->data;
				fw_data = (const __le32 *)
					(rdev->pfp_fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));
				table_offset = le32_to_cpu(hdr->jt_offset);
				table_size = le32_to_cpu(hdr->jt_size);
			} else if (me == 2) {
				hdr = (const struct gfx_firmware_header_v1_0 *)rdev->me_fw->data;
				fw_data = (const __le32 *)
					(rdev->me_fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));
				table_offset = le32_to_cpu(hdr->jt_offset);
				table_size = le32_to_cpu(hdr->jt_size);
			} else if (me == 3) {
				hdr = (const struct gfx_firmware_header_v1_0 *)rdev->mec_fw->data;
				fw_data = (const __le32 *)
					(rdev->mec_fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));
				table_offset = le32_to_cpu(hdr->jt_offset);
				table_size = le32_to_cpu(hdr->jt_size);
			} else {
				hdr = (const struct gfx_firmware_header_v1_0 *)rdev->mec2_fw->data;
				fw_data = (const __le32 *)
					(rdev->mec2_fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));
				table_offset = le32_to_cpu(hdr->jt_offset);
				table_size = le32_to_cpu(hdr->jt_size);
			}

			for (i = 0; i < table_size; i ++) {
				dst_ptr[bo_offset + i] =
					cpu_to_le32(le32_to_cpu(fw_data[table_offset + i]));
			}
			bo_offset += table_size;
		} else {
			const __be32 *fw_data;
			table_size = CP_ME_TABLE_SIZE;

			if (me == 0) {
				fw_data = (const __be32 *)rdev->ce_fw->data;
				table_offset = CP_ME_TABLE_OFFSET;
			} else if (me == 1) {
				fw_data = (const __be32 *)rdev->pfp_fw->data;
				table_offset = CP_ME_TABLE_OFFSET;
			} else if (me == 2) {
				fw_data = (const __be32 *)rdev->me_fw->data;
				table_offset = CP_ME_TABLE_OFFSET;
			} else {
				fw_data = (const __be32 *)rdev->mec_fw->data;
				table_offset = CP_MEC_TABLE_OFFSET;
			}

			for (i = 0; i < table_size; i ++) {
				dst_ptr[bo_offset + i] =
					cpu_to_le32(be32_to_cpu(fw_data[table_offset + i]));
			}
			bo_offset += table_size;
		}
	}
}

static void cik_enable_gfx_cgpg(struct radeon_device *rdev,
				bool enable)
{
	u32 data, orig;

	if (enable && (rdev->pg_flags & RADEON_PG_SUPPORT_GFX_PG)) {
		orig = data = RREG32(RLC_PG_CNTL);
		data |= GFX_PG_ENABLE;
		if (orig != data)
			WREG32(RLC_PG_CNTL, data);

		orig = data = RREG32(RLC_AUTO_PG_CTRL);
		data |= AUTO_PG_EN;
		if (orig != data)
			WREG32(RLC_AUTO_PG_CTRL, data);
	} else {
		orig = data = RREG32(RLC_PG_CNTL);
		data &= ~GFX_PG_ENABLE;
		if (orig != data)
			WREG32(RLC_PG_CNTL, data);

		orig = data = RREG32(RLC_AUTO_PG_CTRL);
		data &= ~AUTO_PG_EN;
		if (orig != data)
			WREG32(RLC_AUTO_PG_CTRL, data);

		data = RREG32(DB_RENDER_CONTROL);
	}
}

static u32 cik_get_cu_active_bitmap(struct radeon_device *rdev, u32 se, u32 sh)
{
	u32 mask = 0, tmp, tmp1;
	int i;

	mutex_lock(&rdev->grbm_idx_mutex);
	cik_select_se_sh(rdev, se, sh);
	tmp = RREG32(CC_GC_SHADER_ARRAY_CONFIG);
	tmp1 = RREG32(GC_USER_SHADER_ARRAY_CONFIG);
	cik_select_se_sh(rdev, 0xffffffff, 0xffffffff);
	mutex_unlock(&rdev->grbm_idx_mutex);

	tmp &= 0xffff0000;

	tmp |= tmp1;
	tmp >>= 16;

	for (i = 0; i < rdev->config.cik.max_cu_per_sh; i ++) {
		mask <<= 1;
		mask |= 1;
	}

	return (~tmp) & mask;
}

static void cik_init_ao_cu_mask(struct radeon_device *rdev)
{
	u32 i, j, k, active_cu_number = 0;
	u32 mask, counter, cu_bitmap;
	u32 tmp = 0;

	for (i = 0; i < rdev->config.cik.max_shader_engines; i++) {
		for (j = 0; j < rdev->config.cik.max_sh_per_se; j++) {
			mask = 1;
			cu_bitmap = 0;
			counter = 0;
			for (k = 0; k < rdev->config.cik.max_cu_per_sh; k ++) {
				if (cik_get_cu_active_bitmap(rdev, i, j) & mask) {
					if (counter < 2)
						cu_bitmap |= mask;
					counter ++;
				}
				mask <<= 1;
			}

			active_cu_number += counter;
			tmp |= (cu_bitmap << (i * 16 + j * 8));
		}
	}

	WREG32(RLC_PG_AO_CU_MASK, tmp);

	tmp = RREG32(RLC_MAX_PG_CU);
	tmp &= ~MAX_PU_CU_MASK;
	tmp |= MAX_PU_CU(active_cu_number);
	WREG32(RLC_MAX_PG_CU, tmp);
}

static void cik_enable_gfx_static_mgpg(struct radeon_device *rdev,
				       bool enable)
{
	u32 data, orig;

	orig = data = RREG32(RLC_PG_CNTL);
	if (enable && (rdev->pg_flags & RADEON_PG_SUPPORT_GFX_SMG))
		data |= STATIC_PER_CU_PG_ENABLE;
	else
		data &= ~STATIC_PER_CU_PG_ENABLE;
	if (orig != data)
		WREG32(RLC_PG_CNTL, data);
}

static void cik_enable_gfx_dynamic_mgpg(struct radeon_device *rdev,
					bool enable)
{
	u32 data, orig;

	orig = data = RREG32(RLC_PG_CNTL);
	if (enable && (rdev->pg_flags & RADEON_PG_SUPPORT_GFX_DMG))
		data |= DYN_PER_CU_PG_ENABLE;
	else
		data &= ~DYN_PER_CU_PG_ENABLE;
	if (orig != data)
		WREG32(RLC_PG_CNTL, data);
}

#define RLC_SAVE_AND_RESTORE_STARTING_OFFSET 0x90
#define RLC_CLEAR_STATE_DESCRIPTOR_OFFSET    0x3D

static void cik_init_gfx_cgpg(struct radeon_device *rdev)
{
	u32 data, orig;
	u32 i;

	if (rdev->rlc.cs_data) {
		WREG32(RLC_GPM_SCRATCH_ADDR, RLC_CLEAR_STATE_DESCRIPTOR_OFFSET);
		WREG32(RLC_GPM_SCRATCH_DATA, upper_32_bits(rdev->rlc.clear_state_gpu_addr));
		WREG32(RLC_GPM_SCRATCH_DATA, lower_32_bits(rdev->rlc.clear_state_gpu_addr));
		WREG32(RLC_GPM_SCRATCH_DATA, rdev->rlc.clear_state_size);
	} else {
		WREG32(RLC_GPM_SCRATCH_ADDR, RLC_CLEAR_STATE_DESCRIPTOR_OFFSET);
		for (i = 0; i < 3; i++)
			WREG32(RLC_GPM_SCRATCH_DATA, 0);
	}
	if (rdev->rlc.reg_list) {
		WREG32(RLC_GPM_SCRATCH_ADDR, RLC_SAVE_AND_RESTORE_STARTING_OFFSET);
		for (i = 0; i < rdev->rlc.reg_list_size; i++)
			WREG32(RLC_GPM_SCRATCH_DATA, rdev->rlc.reg_list[i]);
	}

	orig = data = RREG32(RLC_PG_CNTL);
	data |= GFX_PG_SRC;
	if (orig != data)
		WREG32(RLC_PG_CNTL, data);

	WREG32(RLC_SAVE_AND_RESTORE_BASE, rdev->rlc.save_restore_gpu_addr >> 8);
	WREG32(RLC_CP_TABLE_RESTORE, rdev->rlc.cp_table_gpu_addr >> 8);

	data = RREG32(CP_RB_WPTR_POLL_CNTL);
	data &= ~IDLE_POLL_COUNT_MASK;
	data |= IDLE_POLL_COUNT(0x60);
	WREG32(CP_RB_WPTR_POLL_CNTL, data);

	data = 0x10101010;
	WREG32(RLC_PG_DELAY, data);

	data = RREG32(RLC_PG_DELAY_2);
	data &= ~0xff;
	data |= 0x3;
	WREG32(RLC_PG_DELAY_2, data);

	data = RREG32(RLC_AUTO_PG_CTRL);
	data &= ~GRBM_REG_SGIT_MASK;
	data |= GRBM_REG_SGIT(0x700);
	WREG32(RLC_AUTO_PG_CTRL, data);

}

static void cik_update_gfx_pg(struct radeon_device *rdev, bool enable)
{
	cik_enable_gfx_cgpg(rdev, enable);
	cik_enable_gfx_static_mgpg(rdev, enable);
	cik_enable_gfx_dynamic_mgpg(rdev, enable);
}

u32 cik_get_csb_size(struct radeon_device *rdev)
{
	u32 count = 0;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;

	if (rdev->rlc.cs_data == NULL)
		return 0;

	/* begin clear state */
	count += 2;
	/* context control state */
	count += 3;

	for (sect = rdev->rlc.cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT)
				count += 2 + ext->reg_count;
			else
				return 0;
		}
	}
	/* pa_sc_raster_config/pa_sc_raster_config1 */
	count += 4;
	/* end clear state */
	count += 2;
	/* clear state */
	count += 2;

	return count;
}

void cik_get_csb_buffer(struct radeon_device *rdev, volatile u32 *buffer)
{
	u32 count = 0, i;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;

	if (rdev->rlc.cs_data == NULL)
		return;
	if (buffer == NULL)
		return;

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	buffer[count++] = cpu_to_le32(PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	buffer[count++] = cpu_to_le32(0x80000000);
	buffer[count++] = cpu_to_le32(0x80000000);

	for (sect = rdev->rlc.cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT) {
				buffer[count++] =
					cpu_to_le32(PACKET3(PACKET3_SET_CONTEXT_REG, ext->reg_count));
				buffer[count++] = cpu_to_le32(ext->reg_index - 0xa000);
				for (i = 0; i < ext->reg_count; i++)
					buffer[count++] = cpu_to_le32(ext->extent[i]);
			} else {
				return;
			}
		}
	}

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	buffer[count++] = cpu_to_le32(PA_SC_RASTER_CONFIG - PACKET3_SET_CONTEXT_REG_START);
	switch (rdev->family) {
	case CHIP_BONAIRE:
		buffer[count++] = cpu_to_le32(0x16000012);
		buffer[count++] = cpu_to_le32(0x00000000);
		break;
	case CHIP_KAVERI:
		buffer[count++] = cpu_to_le32(0x00000000); /* XXX */
		buffer[count++] = cpu_to_le32(0x00000000);
		break;
	case CHIP_KABINI:
	case CHIP_MULLINS:
		buffer[count++] = cpu_to_le32(0x00000000); /* XXX */
		buffer[count++] = cpu_to_le32(0x00000000);
		break;
	case CHIP_HAWAII:
		buffer[count++] = cpu_to_le32(0x3a00161a);
		buffer[count++] = cpu_to_le32(0x0000002e);
		break;
	default:
		buffer[count++] = cpu_to_le32(0x00000000);
		buffer[count++] = cpu_to_le32(0x00000000);
		break;
	}

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	buffer[count++] = cpu_to_le32(PACKET3_PREAMBLE_END_CLEAR_STATE);

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_CLEAR_STATE, 0));
	buffer[count++] = cpu_to_le32(0);
}

static void cik_init_pg(struct radeon_device *rdev)
{
	if (rdev->pg_flags) {
		cik_enable_sck_slowdown_on_pu(rdev, true);
		cik_enable_sck_slowdown_on_pd(rdev, true);
		if (rdev->pg_flags & RADEON_PG_SUPPORT_GFX_PG) {
			cik_init_gfx_cgpg(rdev);
			cik_enable_cp_pg(rdev, true);
			cik_enable_gds_pg(rdev, true);
		}
		cik_init_ao_cu_mask(rdev);
		cik_update_gfx_pg(rdev, true);
	}
}

static void cik_fini_pg(struct radeon_device *rdev)
{
	if (rdev->pg_flags) {
		cik_update_gfx_pg(rdev, false);
		if (rdev->pg_flags & RADEON_PG_SUPPORT_GFX_PG) {
			cik_enable_cp_pg(rdev, false);
			cik_enable_gds_pg(rdev, false);
		}
	}
}

/*
 * Interrupts
 * Starting with r6xx, interrupts are handled via a ring buffer.
 * Ring buffers are areas of GPU accessible memory that the GPU
 * writes interrupt vectors into and the host reads vectors out of.
 * There is a rptr (read pointer) that determines where the
 * host is currently reading, and a wptr (write pointer)
 * which determines where the GPU has written.  When the
 * pointers are equal, the ring is idle.  When the GPU
 * writes vectors to the ring buffer, it increments the
 * wptr.  When there is an interrupt, the host then starts
 * fetching commands and processing them until the pointers are
 * equal again at which point it updates the rptr.
 */

/**
 * cik_enable_interrupts - Enable the interrupt ring buffer
 *
 * @rdev: radeon_device pointer
 *
 * Enable the interrupt ring buffer (CIK).
 */
static void cik_enable_interrupts(struct radeon_device *rdev)
{
	u32 ih_cntl = RREG32(IH_CNTL);
	u32 ih_rb_cntl = RREG32(IH_RB_CNTL);

	ih_cntl |= ENABLE_INTR;
	ih_rb_cntl |= IH_RB_ENABLE;
	WREG32(IH_CNTL, ih_cntl);
	WREG32(IH_RB_CNTL, ih_rb_cntl);
	rdev->ih.enabled = true;
}

/**
 * cik_disable_interrupts - Disable the interrupt ring buffer
 *
 * @rdev: radeon_device pointer
 *
 * Disable the interrupt ring buffer (CIK).
 */
static void cik_disable_interrupts(struct radeon_device *rdev)
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

/**
 * cik_disable_interrupt_state - Disable all interrupt sources
 *
 * @rdev: radeon_device pointer
 *
 * Clear all interrupt enable bits used by the driver (CIK).
 */
static void cik_disable_interrupt_state(struct radeon_device *rdev)
{
	u32 tmp;

	/* gfx ring */
	tmp = RREG32(CP_INT_CNTL_RING0) &
		(CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE);
	WREG32(CP_INT_CNTL_RING0, tmp);
	/* sdma */
	tmp = RREG32(SDMA0_CNTL + SDMA0_REGISTER_OFFSET) & ~TRAP_ENABLE;
	WREG32(SDMA0_CNTL + SDMA0_REGISTER_OFFSET, tmp);
	tmp = RREG32(SDMA0_CNTL + SDMA1_REGISTER_OFFSET) & ~TRAP_ENABLE;
	WREG32(SDMA0_CNTL + SDMA1_REGISTER_OFFSET, tmp);
	/* compute queues */
	WREG32(CP_ME1_PIPE0_INT_CNTL, 0);
	WREG32(CP_ME1_PIPE1_INT_CNTL, 0);
	WREG32(CP_ME1_PIPE2_INT_CNTL, 0);
	WREG32(CP_ME1_PIPE3_INT_CNTL, 0);
	WREG32(CP_ME2_PIPE0_INT_CNTL, 0);
	WREG32(CP_ME2_PIPE1_INT_CNTL, 0);
	WREG32(CP_ME2_PIPE2_INT_CNTL, 0);
	WREG32(CP_ME2_PIPE3_INT_CNTL, 0);
	/* grbm */
	WREG32(GRBM_INT_CNTL, 0);
	/* SRBM */
	WREG32(SRBM_INT_CNTL, 0);
	/* vline/vblank, etc. */
	WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC0_REGISTER_OFFSET, 0);
	WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC1_REGISTER_OFFSET, 0);
	if (rdev->num_crtc >= 4) {
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC2_REGISTER_OFFSET, 0);
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC3_REGISTER_OFFSET, 0);
	}
	if (rdev->num_crtc >= 6) {
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC4_REGISTER_OFFSET, 0);
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC5_REGISTER_OFFSET, 0);
	}
	/* pflip */
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

	/* dac hotplug */
	WREG32(DAC_AUTODETECT_INT_CONTROL, 0);

	/* digital hotplug */
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

/**
 * cik_irq_init - init and enable the interrupt ring
 *
 * @rdev: radeon_device pointer
 *
 * Allocate a ring buffer for the interrupt controller,
 * enable the RLC, disable interrupts, enable the IH
 * ring buffer and enable it (CIK).
 * Called at device load and reume.
 * Returns 0 for success, errors for failure.
 */
static int cik_irq_init(struct radeon_device *rdev)
{
	int ret = 0;
	int rb_bufsz;
	u32 interrupt_cntl, ih_cntl, ih_rb_cntl;

	/* allocate ring */
	ret = r600_ih_ring_alloc(rdev);
	if (ret)
		return ret;

	/* disable irqs */
	cik_disable_interrupts(rdev);

	/* init rlc */
	ret = cik_rlc_resume(rdev);
	if (ret) {
		r600_ih_ring_fini(rdev);
		return ret;
	}

	/* setup interrupt control */
	/* XXX this should actually be a bus address, not an MC address. same on older asics */
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
	rb_bufsz = order_base_2(rdev->ih.ring_size / 4);

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
	cik_disable_interrupt_state(rdev);

	pci_set_master(rdev->pdev);

	/* enable irqs */
	cik_enable_interrupts(rdev);

	return ret;
}

/**
 * cik_irq_set - enable/disable interrupt sources
 *
 * @rdev: radeon_device pointer
 *
 * Enable interrupt sources on the GPU (vblanks, hpd,
 * etc.) (CIK).
 * Returns 0 for success, errors for failure.
 */
int cik_irq_set(struct radeon_device *rdev)
{
	u32 cp_int_cntl;
	u32 cp_m1p0;
	u32 crtc1 = 0, crtc2 = 0, crtc3 = 0, crtc4 = 0, crtc5 = 0, crtc6 = 0;
	u32 hpd1, hpd2, hpd3, hpd4, hpd5, hpd6;
	u32 grbm_int_cntl = 0;
	u32 dma_cntl, dma_cntl1;

	if (!rdev->irq.installed) {
		WARN(1, "Can't enable IRQ/MSI because no handler is installed\n");
		return -EINVAL;
	}
	/* don't enable anything if the ih is disabled */
	if (!rdev->ih.enabled) {
		cik_disable_interrupts(rdev);
		/* force the active interrupt state to all disabled */
		cik_disable_interrupt_state(rdev);
		return 0;
	}

	cp_int_cntl = RREG32(CP_INT_CNTL_RING0) &
		(CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE);
	cp_int_cntl |= PRIV_INSTR_INT_ENABLE | PRIV_REG_INT_ENABLE;

	hpd1 = RREG32(DC_HPD1_INT_CONTROL) & ~(DC_HPDx_INT_EN | DC_HPDx_RX_INT_EN);
	hpd2 = RREG32(DC_HPD2_INT_CONTROL) & ~(DC_HPDx_INT_EN | DC_HPDx_RX_INT_EN);
	hpd3 = RREG32(DC_HPD3_INT_CONTROL) & ~(DC_HPDx_INT_EN | DC_HPDx_RX_INT_EN);
	hpd4 = RREG32(DC_HPD4_INT_CONTROL) & ~(DC_HPDx_INT_EN | DC_HPDx_RX_INT_EN);
	hpd5 = RREG32(DC_HPD5_INT_CONTROL) & ~(DC_HPDx_INT_EN | DC_HPDx_RX_INT_EN);
	hpd6 = RREG32(DC_HPD6_INT_CONTROL) & ~(DC_HPDx_INT_EN | DC_HPDx_RX_INT_EN);

	dma_cntl = RREG32(SDMA0_CNTL + SDMA0_REGISTER_OFFSET) & ~TRAP_ENABLE;
	dma_cntl1 = RREG32(SDMA0_CNTL + SDMA1_REGISTER_OFFSET) & ~TRAP_ENABLE;

	cp_m1p0 = RREG32(CP_ME1_PIPE0_INT_CNTL) & ~TIME_STAMP_INT_ENABLE;

	/* enable CP interrupts on all rings */
	if (atomic_read(&rdev->irq.ring_int[RADEON_RING_TYPE_GFX_INDEX])) {
		DRM_DEBUG("cik_irq_set: sw int gfx\n");
		cp_int_cntl |= TIME_STAMP_INT_ENABLE;
	}
	if (atomic_read(&rdev->irq.ring_int[CAYMAN_RING_TYPE_CP1_INDEX])) {
		struct radeon_ring *ring = &rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX];
		DRM_DEBUG("si_irq_set: sw int cp1\n");
		if (ring->me == 1) {
			switch (ring->pipe) {
			case 0:
				cp_m1p0 |= TIME_STAMP_INT_ENABLE;
				break;
			default:
				DRM_DEBUG("si_irq_set: sw int cp1 invalid pipe %d\n", ring->pipe);
				break;
			}
		} else {
			DRM_DEBUG("si_irq_set: sw int cp1 invalid me %d\n", ring->me);
		}
	}
	if (atomic_read(&rdev->irq.ring_int[CAYMAN_RING_TYPE_CP2_INDEX])) {
		struct radeon_ring *ring = &rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX];
		DRM_DEBUG("si_irq_set: sw int cp2\n");
		if (ring->me == 1) {
			switch (ring->pipe) {
			case 0:
				cp_m1p0 |= TIME_STAMP_INT_ENABLE;
				break;
			default:
				DRM_DEBUG("si_irq_set: sw int cp2 invalid pipe %d\n", ring->pipe);
				break;
			}
		} else {
			DRM_DEBUG("si_irq_set: sw int cp2 invalid me %d\n", ring->me);
		}
	}

	if (atomic_read(&rdev->irq.ring_int[R600_RING_TYPE_DMA_INDEX])) {
		DRM_DEBUG("cik_irq_set: sw int dma\n");
		dma_cntl |= TRAP_ENABLE;
	}

	if (atomic_read(&rdev->irq.ring_int[CAYMAN_RING_TYPE_DMA1_INDEX])) {
		DRM_DEBUG("cik_irq_set: sw int dma1\n");
		dma_cntl1 |= TRAP_ENABLE;
	}

	if (rdev->irq.crtc_vblank_int[0] ||
	    atomic_read(&rdev->irq.pflip[0])) {
		DRM_DEBUG("cik_irq_set: vblank 0\n");
		crtc1 |= VBLANK_INTERRUPT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[1] ||
	    atomic_read(&rdev->irq.pflip[1])) {
		DRM_DEBUG("cik_irq_set: vblank 1\n");
		crtc2 |= VBLANK_INTERRUPT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[2] ||
	    atomic_read(&rdev->irq.pflip[2])) {
		DRM_DEBUG("cik_irq_set: vblank 2\n");
		crtc3 |= VBLANK_INTERRUPT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[3] ||
	    atomic_read(&rdev->irq.pflip[3])) {
		DRM_DEBUG("cik_irq_set: vblank 3\n");
		crtc4 |= VBLANK_INTERRUPT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[4] ||
	    atomic_read(&rdev->irq.pflip[4])) {
		DRM_DEBUG("cik_irq_set: vblank 4\n");
		crtc5 |= VBLANK_INTERRUPT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[5] ||
	    atomic_read(&rdev->irq.pflip[5])) {
		DRM_DEBUG("cik_irq_set: vblank 5\n");
		crtc6 |= VBLANK_INTERRUPT_MASK;
	}
	if (rdev->irq.hpd[0]) {
		DRM_DEBUG("cik_irq_set: hpd 1\n");
		hpd1 |= DC_HPDx_INT_EN | DC_HPDx_RX_INT_EN;
	}
	if (rdev->irq.hpd[1]) {
		DRM_DEBUG("cik_irq_set: hpd 2\n");
		hpd2 |= DC_HPDx_INT_EN | DC_HPDx_RX_INT_EN;
	}
	if (rdev->irq.hpd[2]) {
		DRM_DEBUG("cik_irq_set: hpd 3\n");
		hpd3 |= DC_HPDx_INT_EN | DC_HPDx_RX_INT_EN;
	}
	if (rdev->irq.hpd[3]) {
		DRM_DEBUG("cik_irq_set: hpd 4\n");
		hpd4 |= DC_HPDx_INT_EN | DC_HPDx_RX_INT_EN;
	}
	if (rdev->irq.hpd[4]) {
		DRM_DEBUG("cik_irq_set: hpd 5\n");
		hpd5 |= DC_HPDx_INT_EN | DC_HPDx_RX_INT_EN;
	}
	if (rdev->irq.hpd[5]) {
		DRM_DEBUG("cik_irq_set: hpd 6\n");
		hpd6 |= DC_HPDx_INT_EN | DC_HPDx_RX_INT_EN;
	}

	WREG32(CP_INT_CNTL_RING0, cp_int_cntl);

	WREG32(SDMA0_CNTL + SDMA0_REGISTER_OFFSET, dma_cntl);
	WREG32(SDMA0_CNTL + SDMA1_REGISTER_OFFSET, dma_cntl1);

	WREG32(CP_ME1_PIPE0_INT_CNTL, cp_m1p0);

	WREG32(GRBM_INT_CNTL, grbm_int_cntl);

	WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC0_REGISTER_OFFSET, crtc1);
	WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC1_REGISTER_OFFSET, crtc2);
	if (rdev->num_crtc >= 4) {
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC2_REGISTER_OFFSET, crtc3);
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC3_REGISTER_OFFSET, crtc4);
	}
	if (rdev->num_crtc >= 6) {
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC4_REGISTER_OFFSET, crtc5);
		WREG32(LB_INTERRUPT_MASK + EVERGREEN_CRTC5_REGISTER_OFFSET, crtc6);
	}

	if (rdev->num_crtc >= 2) {
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC0_REGISTER_OFFSET,
		       GRPH_PFLIP_INT_MASK);
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC1_REGISTER_OFFSET,
		       GRPH_PFLIP_INT_MASK);
	}
	if (rdev->num_crtc >= 4) {
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC2_REGISTER_OFFSET,
		       GRPH_PFLIP_INT_MASK);
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC3_REGISTER_OFFSET,
		       GRPH_PFLIP_INT_MASK);
	}
	if (rdev->num_crtc >= 6) {
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC4_REGISTER_OFFSET,
		       GRPH_PFLIP_INT_MASK);
		WREG32(GRPH_INT_CONTROL + EVERGREEN_CRTC5_REGISTER_OFFSET,
		       GRPH_PFLIP_INT_MASK);
	}

	WREG32(DC_HPD1_INT_CONTROL, hpd1);
	WREG32(DC_HPD2_INT_CONTROL, hpd2);
	WREG32(DC_HPD3_INT_CONTROL, hpd3);
	WREG32(DC_HPD4_INT_CONTROL, hpd4);
	WREG32(DC_HPD5_INT_CONTROL, hpd5);
	WREG32(DC_HPD6_INT_CONTROL, hpd6);

	/* posting read */
	RREG32(SRBM_STATUS);

	return 0;
}

/**
 * cik_irq_ack - ack interrupt sources
 *
 * @rdev: radeon_device pointer
 *
 * Ack interrupt sources on the GPU (vblanks, hpd,
 * etc.) (CIK).  Certain interrupts sources are sw
 * generated and do not require an explicit ack.
 */
static inline void cik_irq_ack(struct radeon_device *rdev)
{
	u32 tmp;

	rdev->irq.stat_regs.cik.disp_int = RREG32(DISP_INTERRUPT_STATUS);
	rdev->irq.stat_regs.cik.disp_int_cont = RREG32(DISP_INTERRUPT_STATUS_CONTINUE);
	rdev->irq.stat_regs.cik.disp_int_cont2 = RREG32(DISP_INTERRUPT_STATUS_CONTINUE2);
	rdev->irq.stat_regs.cik.disp_int_cont3 = RREG32(DISP_INTERRUPT_STATUS_CONTINUE3);
	rdev->irq.stat_regs.cik.disp_int_cont4 = RREG32(DISP_INTERRUPT_STATUS_CONTINUE4);
	rdev->irq.stat_regs.cik.disp_int_cont5 = RREG32(DISP_INTERRUPT_STATUS_CONTINUE5);
	rdev->irq.stat_regs.cik.disp_int_cont6 = RREG32(DISP_INTERRUPT_STATUS_CONTINUE6);

	rdev->irq.stat_regs.cik.d1grph_int = RREG32(GRPH_INT_STATUS +
		EVERGREEN_CRTC0_REGISTER_OFFSET);
	rdev->irq.stat_regs.cik.d2grph_int = RREG32(GRPH_INT_STATUS +
		EVERGREEN_CRTC1_REGISTER_OFFSET);
	if (rdev->num_crtc >= 4) {
		rdev->irq.stat_regs.cik.d3grph_int = RREG32(GRPH_INT_STATUS +
			EVERGREEN_CRTC2_REGISTER_OFFSET);
		rdev->irq.stat_regs.cik.d4grph_int = RREG32(GRPH_INT_STATUS +
			EVERGREEN_CRTC3_REGISTER_OFFSET);
	}
	if (rdev->num_crtc >= 6) {
		rdev->irq.stat_regs.cik.d5grph_int = RREG32(GRPH_INT_STATUS +
			EVERGREEN_CRTC4_REGISTER_OFFSET);
		rdev->irq.stat_regs.cik.d6grph_int = RREG32(GRPH_INT_STATUS +
			EVERGREEN_CRTC5_REGISTER_OFFSET);
	}

	if (rdev->irq.stat_regs.cik.d1grph_int & GRPH_PFLIP_INT_OCCURRED)
		WREG32(GRPH_INT_STATUS + EVERGREEN_CRTC0_REGISTER_OFFSET,
		       GRPH_PFLIP_INT_CLEAR);
	if (rdev->irq.stat_regs.cik.d2grph_int & GRPH_PFLIP_INT_OCCURRED)
		WREG32(GRPH_INT_STATUS + EVERGREEN_CRTC1_REGISTER_OFFSET,
		       GRPH_PFLIP_INT_CLEAR);
	if (rdev->irq.stat_regs.cik.disp_int & LB_D1_VBLANK_INTERRUPT)
		WREG32(LB_VBLANK_STATUS + EVERGREEN_CRTC0_REGISTER_OFFSET, VBLANK_ACK);
	if (rdev->irq.stat_regs.cik.disp_int & LB_D1_VLINE_INTERRUPT)
		WREG32(LB_VLINE_STATUS + EVERGREEN_CRTC0_REGISTER_OFFSET, VLINE_ACK);
	if (rdev->irq.stat_regs.cik.disp_int_cont & LB_D2_VBLANK_INTERRUPT)
		WREG32(LB_VBLANK_STATUS + EVERGREEN_CRTC1_REGISTER_OFFSET, VBLANK_ACK);
	if (rdev->irq.stat_regs.cik.disp_int_cont & LB_D2_VLINE_INTERRUPT)
		WREG32(LB_VLINE_STATUS + EVERGREEN_CRTC1_REGISTER_OFFSET, VLINE_ACK);

	if (rdev->num_crtc >= 4) {
		if (rdev->irq.stat_regs.cik.d3grph_int & GRPH_PFLIP_INT_OCCURRED)
			WREG32(GRPH_INT_STATUS + EVERGREEN_CRTC2_REGISTER_OFFSET,
			       GRPH_PFLIP_INT_CLEAR);
		if (rdev->irq.stat_regs.cik.d4grph_int & GRPH_PFLIP_INT_OCCURRED)
			WREG32(GRPH_INT_STATUS + EVERGREEN_CRTC3_REGISTER_OFFSET,
			       GRPH_PFLIP_INT_CLEAR);
		if (rdev->irq.stat_regs.cik.disp_int_cont2 & LB_D3_VBLANK_INTERRUPT)
			WREG32(LB_VBLANK_STATUS + EVERGREEN_CRTC2_REGISTER_OFFSET, VBLANK_ACK);
		if (rdev->irq.stat_regs.cik.disp_int_cont2 & LB_D3_VLINE_INTERRUPT)
			WREG32(LB_VLINE_STATUS + EVERGREEN_CRTC2_REGISTER_OFFSET, VLINE_ACK);
		if (rdev->irq.stat_regs.cik.disp_int_cont3 & LB_D4_VBLANK_INTERRUPT)
			WREG32(LB_VBLANK_STATUS + EVERGREEN_CRTC3_REGISTER_OFFSET, VBLANK_ACK);
		if (rdev->irq.stat_regs.cik.disp_int_cont3 & LB_D4_VLINE_INTERRUPT)
			WREG32(LB_VLINE_STATUS + EVERGREEN_CRTC3_REGISTER_OFFSET, VLINE_ACK);
	}

	if (rdev->num_crtc >= 6) {
		if (rdev->irq.stat_regs.cik.d5grph_int & GRPH_PFLIP_INT_OCCURRED)
			WREG32(GRPH_INT_STATUS + EVERGREEN_CRTC4_REGISTER_OFFSET,
			       GRPH_PFLIP_INT_CLEAR);
		if (rdev->irq.stat_regs.cik.d6grph_int & GRPH_PFLIP_INT_OCCURRED)
			WREG32(GRPH_INT_STATUS + EVERGREEN_CRTC5_REGISTER_OFFSET,
			       GRPH_PFLIP_INT_CLEAR);
		if (rdev->irq.stat_regs.cik.disp_int_cont4 & LB_D5_VBLANK_INTERRUPT)
			WREG32(LB_VBLANK_STATUS + EVERGREEN_CRTC4_REGISTER_OFFSET, VBLANK_ACK);
		if (rdev->irq.stat_regs.cik.disp_int_cont4 & LB_D5_VLINE_INTERRUPT)
			WREG32(LB_VLINE_STATUS + EVERGREEN_CRTC4_REGISTER_OFFSET, VLINE_ACK);
		if (rdev->irq.stat_regs.cik.disp_int_cont5 & LB_D6_VBLANK_INTERRUPT)
			WREG32(LB_VBLANK_STATUS + EVERGREEN_CRTC5_REGISTER_OFFSET, VBLANK_ACK);
		if (rdev->irq.stat_regs.cik.disp_int_cont5 & LB_D6_VLINE_INTERRUPT)
			WREG32(LB_VLINE_STATUS + EVERGREEN_CRTC5_REGISTER_OFFSET, VLINE_ACK);
	}

	if (rdev->irq.stat_regs.cik.disp_int & DC_HPD1_INTERRUPT) {
		tmp = RREG32(DC_HPD1_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD1_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int_cont & DC_HPD2_INTERRUPT) {
		tmp = RREG32(DC_HPD2_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD2_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int_cont2 & DC_HPD3_INTERRUPT) {
		tmp = RREG32(DC_HPD3_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD3_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int_cont3 & DC_HPD4_INTERRUPT) {
		tmp = RREG32(DC_HPD4_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD4_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int_cont4 & DC_HPD5_INTERRUPT) {
		tmp = RREG32(DC_HPD5_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD5_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int_cont5 & DC_HPD6_INTERRUPT) {
		tmp = RREG32(DC_HPD5_INT_CONTROL);
		tmp |= DC_HPDx_INT_ACK;
		WREG32(DC_HPD6_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int & DC_HPD1_RX_INTERRUPT) {
		tmp = RREG32(DC_HPD1_INT_CONTROL);
		tmp |= DC_HPDx_RX_INT_ACK;
		WREG32(DC_HPD1_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int_cont & DC_HPD2_RX_INTERRUPT) {
		tmp = RREG32(DC_HPD2_INT_CONTROL);
		tmp |= DC_HPDx_RX_INT_ACK;
		WREG32(DC_HPD2_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int_cont2 & DC_HPD3_RX_INTERRUPT) {
		tmp = RREG32(DC_HPD3_INT_CONTROL);
		tmp |= DC_HPDx_RX_INT_ACK;
		WREG32(DC_HPD3_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int_cont3 & DC_HPD4_RX_INTERRUPT) {
		tmp = RREG32(DC_HPD4_INT_CONTROL);
		tmp |= DC_HPDx_RX_INT_ACK;
		WREG32(DC_HPD4_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int_cont4 & DC_HPD5_RX_INTERRUPT) {
		tmp = RREG32(DC_HPD5_INT_CONTROL);
		tmp |= DC_HPDx_RX_INT_ACK;
		WREG32(DC_HPD5_INT_CONTROL, tmp);
	}
	if (rdev->irq.stat_regs.cik.disp_int_cont5 & DC_HPD6_RX_INTERRUPT) {
		tmp = RREG32(DC_HPD5_INT_CONTROL);
		tmp |= DC_HPDx_RX_INT_ACK;
		WREG32(DC_HPD6_INT_CONTROL, tmp);
	}
}

/**
 * cik_irq_disable - disable interrupts
 *
 * @rdev: radeon_device pointer
 *
 * Disable interrupts on the hw (CIK).
 */
static void cik_irq_disable(struct radeon_device *rdev)
{
	cik_disable_interrupts(rdev);
	/* Wait and acknowledge irq */
	mdelay(1);
	cik_irq_ack(rdev);
	cik_disable_interrupt_state(rdev);
}

/**
 * cik_irq_disable - disable interrupts for suspend
 *
 * @rdev: radeon_device pointer
 *
 * Disable interrupts and stop the RLC (CIK).
 * Used for suspend.
 */
static void cik_irq_suspend(struct radeon_device *rdev)
{
	cik_irq_disable(rdev);
	cik_rlc_stop(rdev);
}

/**
 * cik_irq_fini - tear down interrupt support
 *
 * @rdev: radeon_device pointer
 *
 * Disable interrupts on the hw and free the IH ring
 * buffer (CIK).
 * Used for driver unload.
 */
static void cik_irq_fini(struct radeon_device *rdev)
{
	cik_irq_suspend(rdev);
	r600_ih_ring_fini(rdev);
}

/**
 * cik_get_ih_wptr - get the IH ring buffer wptr
 *
 * @rdev: radeon_device pointer
 *
 * Get the IH ring buffer wptr from either the register
 * or the writeback memory buffer (CIK).  Also check for
 * ring buffer overflow and deal with it.
 * Used by cik_irq_process().
 * Returns the value of the wptr.
 */
static inline u32 cik_get_ih_wptr(struct radeon_device *rdev)
{
	u32 wptr, tmp;

	if (rdev->wb.enabled)
		wptr = le32_to_cpu(rdev->wb.wb[R600_WB_IH_WPTR_OFFSET/4]);
	else
		wptr = RREG32(IH_RB_WPTR);

	if (wptr & RB_OVERFLOW) {
		wptr &= ~RB_OVERFLOW;
		/* When a ring buffer overflow happen start parsing interrupt
		 * from the last not overwritten vector (wptr + 16). Hopefully
		 * this should allow us to catchup.
		 */
		dev_warn(rdev->dev, "IH ring buffer overflow (0x%08X, 0x%08X, 0x%08X)\n",
			 wptr, rdev->ih.rptr, (wptr + 16) & rdev->ih.ptr_mask);
		rdev->ih.rptr = (wptr + 16) & rdev->ih.ptr_mask;
		tmp = RREG32(IH_RB_CNTL);
		tmp |= IH_WPTR_OVERFLOW_CLEAR;
		WREG32(IH_RB_CNTL, tmp);
	}
	return (wptr & rdev->ih.ptr_mask);
}

/*        CIK IV Ring
 * Each IV ring entry is 128 bits:
 * [7:0]    - interrupt source id
 * [31:8]   - reserved
 * [59:32]  - interrupt source data
 * [63:60]  - reserved
 * [71:64]  - RINGID
 *            CP:
 *            ME_ID [1:0], PIPE_ID[1:0], QUEUE_ID[2:0]
 *            QUEUE_ID - for compute, which of the 8 queues owned by the dispatcher
 *                     - for gfx, hw shader state (0=PS...5=LS, 6=CS)
 *            ME_ID - 0 = gfx, 1 = first 4 CS pipes, 2 = second 4 CS pipes
 *            PIPE_ID - ME0 0=3D
 *                    - ME1&2 compute dispatcher (4 pipes each)
 *            SDMA:
 *            INSTANCE_ID [1:0], QUEUE_ID[1:0]
 *            INSTANCE_ID - 0 = sdma0, 1 = sdma1
 *            QUEUE_ID - 0 = gfx, 1 = rlc0, 2 = rlc1
 * [79:72]  - VMID
 * [95:80]  - PASID
 * [127:96] - reserved
 */
/**
 * cik_irq_process - interrupt handler
 *
 * @rdev: radeon_device pointer
 *
 * Interrupt hander (CIK).  Walk the IH ring,
 * ack interrupts and schedule work to handle
 * interrupt events.
 * Returns irq process return code.
 */
int cik_irq_process(struct radeon_device *rdev)
{
	struct radeon_ring *cp1_ring = &rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX];
	struct radeon_ring *cp2_ring = &rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX];
	u32 wptr;
	u32 rptr;
	u32 src_id, src_data, ring_id;
	u8 me_id, pipe_id, queue_id;
	u32 ring_index;
	bool queue_hotplug = false;
	bool queue_dp = false;
	bool queue_reset = false;
	u32 addr, status, mc_client;
	bool queue_thermal = false;

	if (!rdev->ih.enabled || rdev->shutdown)
		return IRQ_NONE;

	wptr = cik_get_ih_wptr(rdev);

restart_ih:
	/* is somebody else already processing irqs? */
	if (atomic_xchg(&rdev->ih.lock, 1))
		return IRQ_NONE;

	rptr = rdev->ih.rptr;
	DRM_DEBUG("cik_irq_process start: rptr %d, wptr %d\n", rptr, wptr);

	/* Order reading of wptr vs. reading of IH ring data */
	rmb();

	/* display interrupts */
	cik_irq_ack(rdev);

	while (rptr != wptr) {
		/* wptr/rptr are in bytes! */
		ring_index = rptr / 4;

		radeon_kfd_interrupt(rdev,
				(const void *) &rdev->ih.ring[ring_index]);

		src_id =  le32_to_cpu(rdev->ih.ring[ring_index]) & 0xff;
		src_data = le32_to_cpu(rdev->ih.ring[ring_index + 1]) & 0xfffffff;
		ring_id = le32_to_cpu(rdev->ih.ring[ring_index + 2]) & 0xff;

		switch (src_id) {
		case 1: /* D1 vblank/vline */
			switch (src_data) {
			case 0: /* D1 vblank */
				if (rdev->irq.stat_regs.cik.disp_int & LB_D1_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[0]) {
						drm_handle_vblank(rdev->ddev, 0);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[0]))
						radeon_crtc_handle_vblank(rdev, 0);
					rdev->irq.stat_regs.cik.disp_int &= ~LB_D1_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D1 vblank\n");
				}
				break;
			case 1: /* D1 vline */
				if (rdev->irq.stat_regs.cik.disp_int & LB_D1_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int &= ~LB_D1_VLINE_INTERRUPT;
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
				if (rdev->irq.stat_regs.cik.disp_int_cont & LB_D2_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[1]) {
						drm_handle_vblank(rdev->ddev, 1);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[1]))
						radeon_crtc_handle_vblank(rdev, 1);
					rdev->irq.stat_regs.cik.disp_int_cont &= ~LB_D2_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D2 vblank\n");
				}
				break;
			case 1: /* D2 vline */
				if (rdev->irq.stat_regs.cik.disp_int_cont & LB_D2_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont &= ~LB_D2_VLINE_INTERRUPT;
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
				if (rdev->irq.stat_regs.cik.disp_int_cont2 & LB_D3_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[2]) {
						drm_handle_vblank(rdev->ddev, 2);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[2]))
						radeon_crtc_handle_vblank(rdev, 2);
					rdev->irq.stat_regs.cik.disp_int_cont2 &= ~LB_D3_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D3 vblank\n");
				}
				break;
			case 1: /* D3 vline */
				if (rdev->irq.stat_regs.cik.disp_int_cont2 & LB_D3_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont2 &= ~LB_D3_VLINE_INTERRUPT;
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
				if (rdev->irq.stat_regs.cik.disp_int_cont3 & LB_D4_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[3]) {
						drm_handle_vblank(rdev->ddev, 3);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[3]))
						radeon_crtc_handle_vblank(rdev, 3);
					rdev->irq.stat_regs.cik.disp_int_cont3 &= ~LB_D4_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D4 vblank\n");
				}
				break;
			case 1: /* D4 vline */
				if (rdev->irq.stat_regs.cik.disp_int_cont3 & LB_D4_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont3 &= ~LB_D4_VLINE_INTERRUPT;
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
				if (rdev->irq.stat_regs.cik.disp_int_cont4 & LB_D5_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[4]) {
						drm_handle_vblank(rdev->ddev, 4);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[4]))
						radeon_crtc_handle_vblank(rdev, 4);
					rdev->irq.stat_regs.cik.disp_int_cont4 &= ~LB_D5_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D5 vblank\n");
				}
				break;
			case 1: /* D5 vline */
				if (rdev->irq.stat_regs.cik.disp_int_cont4 & LB_D5_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont4 &= ~LB_D5_VLINE_INTERRUPT;
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
				if (rdev->irq.stat_regs.cik.disp_int_cont5 & LB_D6_VBLANK_INTERRUPT) {
					if (rdev->irq.crtc_vblank_int[5]) {
						drm_handle_vblank(rdev->ddev, 5);
						rdev->pm.vblank_sync = true;
						wake_up(&rdev->irq.vblank_queue);
					}
					if (atomic_read(&rdev->irq.pflip[5]))
						radeon_crtc_handle_vblank(rdev, 5);
					rdev->irq.stat_regs.cik.disp_int_cont5 &= ~LB_D6_VBLANK_INTERRUPT;
					DRM_DEBUG("IH: D6 vblank\n");
				}
				break;
			case 1: /* D6 vline */
				if (rdev->irq.stat_regs.cik.disp_int_cont5 & LB_D6_VLINE_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont5 &= ~LB_D6_VLINE_INTERRUPT;
					DRM_DEBUG("IH: D6 vline\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 8: /* D1 page flip */
		case 10: /* D2 page flip */
		case 12: /* D3 page flip */
		case 14: /* D4 page flip */
		case 16: /* D5 page flip */
		case 18: /* D6 page flip */
			DRM_DEBUG("IH: D%d flip\n", ((src_id - 8) >> 1) + 1);
			if (radeon_use_pflipirq > 0)
				radeon_crtc_handle_flip(rdev, (src_id - 8) >> 1);
			break;
		case 42: /* HPD hotplug */
			switch (src_data) {
			case 0:
				if (rdev->irq.stat_regs.cik.disp_int & DC_HPD1_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int &= ~DC_HPD1_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD1\n");
				}
				break;
			case 1:
				if (rdev->irq.stat_regs.cik.disp_int_cont & DC_HPD2_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont &= ~DC_HPD2_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD2\n");
				}
				break;
			case 2:
				if (rdev->irq.stat_regs.cik.disp_int_cont2 & DC_HPD3_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont2 &= ~DC_HPD3_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD3\n");
				}
				break;
			case 3:
				if (rdev->irq.stat_regs.cik.disp_int_cont3 & DC_HPD4_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont3 &= ~DC_HPD4_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD4\n");
				}
				break;
			case 4:
				if (rdev->irq.stat_regs.cik.disp_int_cont4 & DC_HPD5_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont4 &= ~DC_HPD5_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD5\n");
				}
				break;
			case 5:
				if (rdev->irq.stat_regs.cik.disp_int_cont5 & DC_HPD6_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont5 &= ~DC_HPD6_INTERRUPT;
					queue_hotplug = true;
					DRM_DEBUG("IH: HPD6\n");
				}
				break;
			case 6:
				if (rdev->irq.stat_regs.cik.disp_int & DC_HPD1_RX_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int &= ~DC_HPD1_RX_INTERRUPT;
					queue_dp = true;
					DRM_DEBUG("IH: HPD_RX 1\n");
				}
				break;
			case 7:
				if (rdev->irq.stat_regs.cik.disp_int_cont & DC_HPD2_RX_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont &= ~DC_HPD2_RX_INTERRUPT;
					queue_dp = true;
					DRM_DEBUG("IH: HPD_RX 2\n");
				}
				break;
			case 8:
				if (rdev->irq.stat_regs.cik.disp_int_cont2 & DC_HPD3_RX_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont2 &= ~DC_HPD3_RX_INTERRUPT;
					queue_dp = true;
					DRM_DEBUG("IH: HPD_RX 3\n");
				}
				break;
			case 9:
				if (rdev->irq.stat_regs.cik.disp_int_cont3 & DC_HPD4_RX_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont3 &= ~DC_HPD4_RX_INTERRUPT;
					queue_dp = true;
					DRM_DEBUG("IH: HPD_RX 4\n");
				}
				break;
			case 10:
				if (rdev->irq.stat_regs.cik.disp_int_cont4 & DC_HPD5_RX_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont4 &= ~DC_HPD5_RX_INTERRUPT;
					queue_dp = true;
					DRM_DEBUG("IH: HPD_RX 5\n");
				}
				break;
			case 11:
				if (rdev->irq.stat_regs.cik.disp_int_cont5 & DC_HPD6_RX_INTERRUPT) {
					rdev->irq.stat_regs.cik.disp_int_cont5 &= ~DC_HPD6_RX_INTERRUPT;
					queue_dp = true;
					DRM_DEBUG("IH: HPD_RX 6\n");
				}
				break;
			default:
				DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 96:
			DRM_ERROR("SRBM_READ_ERROR: 0x%x\n", RREG32(SRBM_READ_ERROR));
			WREG32(SRBM_INT_ACK, 0x1);
			break;
		case 124: /* UVD */
			DRM_DEBUG("IH: UVD int: 0x%08x\n", src_data);
			radeon_fence_process(rdev, R600_RING_TYPE_UVD_INDEX);
			break;
		case 146:
		case 147:
			addr = RREG32(VM_CONTEXT1_PROTECTION_FAULT_ADDR);
			status = RREG32(VM_CONTEXT1_PROTECTION_FAULT_STATUS);
			mc_client = RREG32(VM_CONTEXT1_PROTECTION_FAULT_MCCLIENT);
			/* reset addr and status */
			WREG32_P(VM_CONTEXT1_CNTL2, 1, ~1);
			if (addr == 0x0 && status == 0x0)
				break;
			dev_err(rdev->dev, "GPU fault detected: %d 0x%08x\n", src_id, src_data);
			dev_err(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_ADDR   0x%08X\n",
				addr);
			dev_err(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_STATUS 0x%08X\n",
				status);
			cik_vm_decode_fault(rdev, status, addr, mc_client);
			break;
		case 167: /* VCE */
			DRM_DEBUG("IH: VCE int: 0x%08x\n", src_data);
			switch (src_data) {
			case 0:
				radeon_fence_process(rdev, TN_RING_TYPE_VCE1_INDEX);
				break;
			case 1:
				radeon_fence_process(rdev, TN_RING_TYPE_VCE2_INDEX);
				break;
			default:
				DRM_ERROR("Unhandled interrupt: %d %d\n", src_id, src_data);
				break;
			}
			break;
		case 176: /* GFX RB CP_INT */
		case 177: /* GFX IB CP_INT */
			radeon_fence_process(rdev, RADEON_RING_TYPE_GFX_INDEX);
			break;
		case 181: /* CP EOP event */
			DRM_DEBUG("IH: CP EOP\n");
			/* XXX check the bitfield order! */
			me_id = (ring_id & 0x60) >> 5;
			pipe_id = (ring_id & 0x18) >> 3;
			queue_id = (ring_id & 0x7) >> 0;
			switch (me_id) {
			case 0:
				radeon_fence_process(rdev, RADEON_RING_TYPE_GFX_INDEX);
				break;
			case 1:
			case 2:
				if ((cp1_ring->me == me_id) & (cp1_ring->pipe == pipe_id))
					radeon_fence_process(rdev, CAYMAN_RING_TYPE_CP1_INDEX);
				if ((cp2_ring->me == me_id) & (cp2_ring->pipe == pipe_id))
					radeon_fence_process(rdev, CAYMAN_RING_TYPE_CP2_INDEX);
				break;
			}
			break;
		case 184: /* CP Privileged reg access */
			DRM_ERROR("Illegal register access in command stream\n");
			/* XXX check the bitfield order! */
			me_id = (ring_id & 0x60) >> 5;
			pipe_id = (ring_id & 0x18) >> 3;
			queue_id = (ring_id & 0x7) >> 0;
			switch (me_id) {
			case 0:
				/* This results in a full GPU reset, but all we need to do is soft
				 * reset the CP for gfx
				 */
				queue_reset = true;
				break;
			case 1:
				/* XXX compute */
				queue_reset = true;
				break;
			case 2:
				/* XXX compute */
				queue_reset = true;
				break;
			}
			break;
		case 185: /* CP Privileged inst */
			DRM_ERROR("Illegal instruction in command stream\n");
			/* XXX check the bitfield order! */
			me_id = (ring_id & 0x60) >> 5;
			pipe_id = (ring_id & 0x18) >> 3;
			queue_id = (ring_id & 0x7) >> 0;
			switch (me_id) {
			case 0:
				/* This results in a full GPU reset, but all we need to do is soft
				 * reset the CP for gfx
				 */
				queue_reset = true;
				break;
			case 1:
				/* XXX compute */
				queue_reset = true;
				break;
			case 2:
				/* XXX compute */
				queue_reset = true;
				break;
			}
			break;
		case 224: /* SDMA trap event */
			/* XXX check the bitfield order! */
			me_id = (ring_id & 0x3) >> 0;
			queue_id = (ring_id & 0xc) >> 2;
			DRM_DEBUG("IH: SDMA trap\n");
			switch (me_id) {
			case 0:
				switch (queue_id) {
				case 0:
					radeon_fence_process(rdev, R600_RING_TYPE_DMA_INDEX);
					break;
				case 1:
					/* XXX compute */
					break;
				case 2:
					/* XXX compute */
					break;
				}
				break;
			case 1:
				switch (queue_id) {
				case 0:
					radeon_fence_process(rdev, CAYMAN_RING_TYPE_DMA1_INDEX);
					break;
				case 1:
					/* XXX compute */
					break;
				case 2:
					/* XXX compute */
					break;
				}
				break;
			}
			break;
		case 230: /* thermal low to high */
			DRM_DEBUG("IH: thermal low to high\n");
			rdev->pm.dpm.thermal.high_to_low = false;
			queue_thermal = true;
			break;
		case 231: /* thermal high to low */
			DRM_DEBUG("IH: thermal high to low\n");
			rdev->pm.dpm.thermal.high_to_low = true;
			queue_thermal = true;
			break;
		case 233: /* GUI IDLE */
			DRM_DEBUG("IH: GUI idle\n");
			break;
		case 241: /* SDMA Privileged inst */
		case 247: /* SDMA Privileged inst */
			DRM_ERROR("Illegal instruction in SDMA command stream\n");
			/* XXX check the bitfield order! */
			me_id = (ring_id & 0x3) >> 0;
			queue_id = (ring_id & 0xc) >> 2;
			switch (me_id) {
			case 0:
				switch (queue_id) {
				case 0:
					queue_reset = true;
					break;
				case 1:
					/* XXX compute */
					queue_reset = true;
					break;
				case 2:
					/* XXX compute */
					queue_reset = true;
					break;
				}
				break;
			case 1:
				switch (queue_id) {
				case 0:
					queue_reset = true;
					break;
				case 1:
					/* XXX compute */
					queue_reset = true;
					break;
				case 2:
					/* XXX compute */
					queue_reset = true;
					break;
				}
				break;
			}
			break;
		default:
			DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
			break;
		}

		/* wptr/rptr are in bytes! */
		rptr += 16;
		rptr &= rdev->ih.ptr_mask;
		WREG32(IH_RB_RPTR, rptr);
	}
	if (queue_dp)
		schedule_work(&rdev->dp_work);
	if (queue_hotplug)
		schedule_work(&rdev->hotplug_work);
	if (queue_reset) {
		rdev->needs_reset = true;
		wake_up_all(&rdev->fence_queue);
	}
	if (queue_thermal)
		schedule_work(&rdev->pm.dpm.thermal.work);
	rdev->ih.rptr = rptr;
	atomic_set(&rdev->ih.lock, 0);

	/* make sure wptr hasn't changed while processing */
	wptr = cik_get_ih_wptr(rdev);
	if (wptr != rptr)
		goto restart_ih;

	return IRQ_HANDLED;
}

/*
 * startup/shutdown callbacks
 */
/**
 * cik_startup - program the asic to a functional state
 *
 * @rdev: radeon_device pointer
 *
 * Programs the asic to a functional state (CIK).
 * Called by cik_init() and cik_resume().
 * Returns 0 for success, error for failure.
 */
static int cik_startup(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	u32 nop;
	int r;

	/* enable pcie gen2/3 link */
	cik_pcie_gen3_enable(rdev);
	/* enable aspm */
	cik_program_aspm(rdev);

	/* scratch needs to be initialized before MC */
	r = r600_vram_scratch_init(rdev);
	if (r)
		return r;

	cik_mc_program(rdev);

	if (!(rdev->flags & RADEON_IS_IGP) && !rdev->pm.dpm_enabled) {
		r = ci_mc_load_microcode(rdev);
		if (r) {
			DRM_ERROR("Failed to load MC firmware!\n");
			return r;
		}
	}

	r = cik_pcie_gart_enable(rdev);
	if (r)
		return r;
	cik_gpu_init(rdev);

	/* allocate rlc buffers */
	if (rdev->flags & RADEON_IS_IGP) {
		if (rdev->family == CHIP_KAVERI) {
			rdev->rlc.reg_list = spectre_rlc_save_restore_register_list;
			rdev->rlc.reg_list_size =
				(u32)ARRAY_SIZE(spectre_rlc_save_restore_register_list);
		} else {
			rdev->rlc.reg_list = kalindi_rlc_save_restore_register_list;
			rdev->rlc.reg_list_size =
				(u32)ARRAY_SIZE(kalindi_rlc_save_restore_register_list);
		}
	}
	rdev->rlc.cs_data = ci_cs_data;
	rdev->rlc.cp_table_size = CP_ME_TABLE_SIZE * 5 * 4;
	r = sumo_rlc_init(rdev);
	if (r) {
		DRM_ERROR("Failed to init rlc BOs!\n");
		return r;
	}

	/* allocate wb buffer */
	r = radeon_wb_init(rdev);
	if (r)
		return r;

	/* allocate mec buffers */
	r = cik_mec_init(rdev);
	if (r) {
		DRM_ERROR("Failed to init MEC BOs!\n");
		return r;
	}

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

	r = radeon_uvd_resume(rdev);
	if (!r) {
		r = uvd_v4_2_resume(rdev);
		if (!r) {
			r = radeon_fence_driver_start_ring(rdev,
							   R600_RING_TYPE_UVD_INDEX);
			if (r)
				dev_err(rdev->dev, "UVD fences init error (%d).\n", r);
		}
	}
	if (r)
		rdev->ring[R600_RING_TYPE_UVD_INDEX].ring_size = 0;

	r = radeon_vce_resume(rdev);
	if (!r) {
		r = vce_v2_0_resume(rdev);
		if (!r)
			r = radeon_fence_driver_start_ring(rdev,
							   TN_RING_TYPE_VCE1_INDEX);
		if (!r)
			r = radeon_fence_driver_start_ring(rdev,
							   TN_RING_TYPE_VCE2_INDEX);
	}
	if (r) {
		dev_err(rdev->dev, "VCE init error (%d).\n", r);
		rdev->ring[TN_RING_TYPE_VCE1_INDEX].ring_size = 0;
		rdev->ring[TN_RING_TYPE_VCE2_INDEX].ring_size = 0;
	}

	/* Enable IRQ */
	if (!rdev->irq.installed) {
		r = radeon_irq_kms_init(rdev);
		if (r)
			return r;
	}

	r = cik_irq_init(rdev);
	if (r) {
		DRM_ERROR("radeon: IH init failed (%d).\n", r);
		radeon_irq_kms_fini(rdev);
		return r;
	}
	cik_irq_set(rdev);

	if (rdev->family == CHIP_HAWAII) {
		if (rdev->new_fw)
			nop = PACKET3(PACKET3_NOP, 0x3FFF);
		else
			nop = RADEON_CP_PACKET2;
	} else {
		nop = PACKET3(PACKET3_NOP, 0x3FFF);
	}

	ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, RADEON_WB_CP_RPTR_OFFSET,
			     nop);
	if (r)
		return r;

	/* set up the compute queues */
	/* type-2 packets are deprecated on MEC, use type-3 instead */
	ring = &rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, RADEON_WB_CP1_RPTR_OFFSET,
			     nop);
	if (r)
		return r;
	ring->me = 1; /* first MEC */
	ring->pipe = 0; /* first pipe */
	ring->queue = 0; /* first queue */
	ring->wptr_offs = CIK_WB_CP1_WPTR_OFFSET;

	/* type-2 packets are deprecated on MEC, use type-3 instead */
	ring = &rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, RADEON_WB_CP2_RPTR_OFFSET,
			     nop);
	if (r)
		return r;
	/* dGPU only have 1 MEC */
	ring->me = 1; /* first MEC */
	ring->pipe = 0; /* first pipe */
	ring->queue = 1; /* second queue */
	ring->wptr_offs = CIK_WB_CP2_WPTR_OFFSET;

	ring = &rdev->ring[R600_RING_TYPE_DMA_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, R600_WB_DMA_RPTR_OFFSET,
			     SDMA_PACKET(SDMA_OPCODE_NOP, 0, 0));
	if (r)
		return r;

	ring = &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, CAYMAN_WB_DMA1_RPTR_OFFSET,
			     SDMA_PACKET(SDMA_OPCODE_NOP, 0, 0));
	if (r)
		return r;

	r = cik_cp_resume(rdev);
	if (r)
		return r;

	r = cik_sdma_resume(rdev);
	if (r)
		return r;

	ring = &rdev->ring[R600_RING_TYPE_UVD_INDEX];
	if (ring->ring_size) {
		r = radeon_ring_init(rdev, ring, ring->ring_size, 0,
				     RADEON_CP_PACKET2);
		if (!r)
			r = uvd_v1_0_init(rdev);
		if (r)
			DRM_ERROR("radeon: failed initializing UVD (%d).\n", r);
	}

	r = -ENOENT;

	ring = &rdev->ring[TN_RING_TYPE_VCE1_INDEX];
	if (ring->ring_size)
		r = radeon_ring_init(rdev, ring, ring->ring_size, 0,
				     VCE_CMD_NO_OP);

	ring = &rdev->ring[TN_RING_TYPE_VCE2_INDEX];
	if (ring->ring_size)
		r = radeon_ring_init(rdev, ring, ring->ring_size, 0,
				     VCE_CMD_NO_OP);

	if (!r)
		r = vce_v1_0_init(rdev);
	else if (r != -ENOENT)
		DRM_ERROR("radeon: failed initializing VCE (%d).\n", r);

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

	r = radeon_kfd_resume(rdev);
	if (r)
		return r;

	return 0;
}

/**
 * cik_resume - resume the asic to a functional state
 *
 * @rdev: radeon_device pointer
 *
 * Programs the asic to a functional state (CIK).
 * Called at resume.
 * Returns 0 for success, error for failure.
 */
int cik_resume(struct radeon_device *rdev)
{
	int r;

	/* post card */
	atom_asic_init(rdev->mode_info.atom_context);

	/* init golden registers */
	cik_init_golden_registers(rdev);

	if (rdev->pm.pm_method == PM_METHOD_DPM)
		radeon_pm_resume(rdev);

	rdev->accel_working = true;
	r = cik_startup(rdev);
	if (r) {
		DRM_ERROR("cik startup failed on resume\n");
		rdev->accel_working = false;
		return r;
	}

	return r;

}

/**
 * cik_suspend - suspend the asic
 *
 * @rdev: radeon_device pointer
 *
 * Bring the chip into a state suitable for suspend (CIK).
 * Called at suspend.
 * Returns 0 for success.
 */
int cik_suspend(struct radeon_device *rdev)
{
	radeon_kfd_suspend(rdev);
	radeon_pm_suspend(rdev);
	radeon_audio_fini(rdev);
	radeon_vm_manager_fini(rdev);
	cik_cp_enable(rdev, false);
	cik_sdma_enable(rdev, false);
	uvd_v1_0_fini(rdev);
	radeon_uvd_suspend(rdev);
	radeon_vce_suspend(rdev);
	cik_fini_pg(rdev);
	cik_fini_cg(rdev);
	cik_irq_suspend(rdev);
	radeon_wb_disable(rdev);
	cik_pcie_gart_disable(rdev);
	return 0;
}

/* Plan is to move initialization in that function and use
 * helper function so that radeon_device_init pretty much
 * do nothing more than calling asic specific function. This
 * should also allow to remove a bunch of callback function
 * like vram_info.
 */
/**
 * cik_init - asic specific driver and hw init
 *
 * @rdev: radeon_device pointer
 *
 * Setup asic specific driver variables and program the hw
 * to a functional state (CIK).
 * Called at driver startup.
 * Returns 0 for success, errors for failure.
 */
int cik_init(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
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
	cik_init_golden_registers(rdev);
	/* Initialize scratch registers */
	cik_scratch_init(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	/* Initialize clocks */
	radeon_get_clock_info(rdev->ddev);

	/* Fence driver */
	r = radeon_fence_driver_init(rdev);
	if (r)
		return r;

	/* initialize memory controller */
	r = cik_mc_init(rdev);
	if (r)
		return r;
	/* Memory manager */
	r = radeon_bo_init(rdev);
	if (r)
		return r;

	if (rdev->flags & RADEON_IS_IGP) {
		if (!rdev->me_fw || !rdev->pfp_fw || !rdev->ce_fw ||
		    !rdev->mec_fw || !rdev->sdma_fw || !rdev->rlc_fw) {
			r = cik_init_microcode(rdev);
			if (r) {
				DRM_ERROR("Failed to load firmware!\n");
				return r;
			}
		}
	} else {
		if (!rdev->me_fw || !rdev->pfp_fw || !rdev->ce_fw ||
		    !rdev->mec_fw || !rdev->sdma_fw || !rdev->rlc_fw ||
		    !rdev->mc_fw) {
			r = cik_init_microcode(rdev);
			if (r) {
				DRM_ERROR("Failed to load firmware!\n");
				return r;
			}
		}
	}

	/* Initialize power management */
	radeon_pm_init(rdev);

	ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 1024 * 1024);

	ring = &rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 1024 * 1024);
	r = radeon_doorbell_get(rdev, &ring->doorbell_index);
	if (r)
		return r;

	ring = &rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 1024 * 1024);
	r = radeon_doorbell_get(rdev, &ring->doorbell_index);
	if (r)
		return r;

	ring = &rdev->ring[R600_RING_TYPE_DMA_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 256 * 1024);

	ring = &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 256 * 1024);

	r = radeon_uvd_init(rdev);
	if (!r) {
		ring = &rdev->ring[R600_RING_TYPE_UVD_INDEX];
		ring->ring_obj = NULL;
		r600_ring_init(rdev, ring, 4096);
	}

	r = radeon_vce_init(rdev);
	if (!r) {
		ring = &rdev->ring[TN_RING_TYPE_VCE1_INDEX];
		ring->ring_obj = NULL;
		r600_ring_init(rdev, ring, 4096);

		ring = &rdev->ring[TN_RING_TYPE_VCE2_INDEX];
		ring->ring_obj = NULL;
		r600_ring_init(rdev, ring, 4096);
	}

	rdev->ih.ring_obj = NULL;
	r600_ih_ring_init(rdev, 64 * 1024);

	r = r600_pcie_gart_init(rdev);
	if (r)
		return r;

	rdev->accel_working = true;
	r = cik_startup(rdev);
	if (r) {
		dev_err(rdev->dev, "disabling GPU acceleration\n");
		cik_cp_fini(rdev);
		cik_sdma_fini(rdev);
		cik_irq_fini(rdev);
		sumo_rlc_fini(rdev);
		cik_mec_fini(rdev);
		radeon_wb_fini(rdev);
		radeon_ib_pool_fini(rdev);
		radeon_vm_manager_fini(rdev);
		radeon_irq_kms_fini(rdev);
		cik_pcie_gart_fini(rdev);
		rdev->accel_working = false;
	}

	/* Don't start up if the MC ucode is missing.
	 * The default clocks and voltages before the MC ucode
	 * is loaded are not suffient for advanced operations.
	 */
	if (!rdev->mc_fw && !(rdev->flags & RADEON_IS_IGP)) {
		DRM_ERROR("radeon: MC ucode required for NI+.\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * cik_fini - asic specific driver and hw fini
 *
 * @rdev: radeon_device pointer
 *
 * Tear down the asic specific driver variables and program the hw
 * to an idle state (CIK).
 * Called at driver unload.
 */
void cik_fini(struct radeon_device *rdev)
{
	radeon_pm_fini(rdev);
	cik_cp_fini(rdev);
	cik_sdma_fini(rdev);
	cik_fini_pg(rdev);
	cik_fini_cg(rdev);
	cik_irq_fini(rdev);
	sumo_rlc_fini(rdev);
	cik_mec_fini(rdev);
	radeon_wb_fini(rdev);
	radeon_vm_manager_fini(rdev);
	radeon_ib_pool_fini(rdev);
	radeon_irq_kms_fini(rdev);
	uvd_v1_0_fini(rdev);
	radeon_uvd_fini(rdev);
	radeon_vce_fini(rdev);
	cik_pcie_gart_fini(rdev);
	r600_vram_scratch_fini(rdev);
	radeon_gem_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_bo_fini(rdev);
	radeon_atombios_fini(rdev);
	kfree(rdev->bios);
	rdev->bios = NULL;
}

void dce8_program_fmt(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
	struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);
	int bpc = 0;
	u32 tmp = 0;
	enum radeon_connector_dither dither = RADEON_FMT_DITHER_DISABLE;

	if (connector) {
		struct radeon_connector *radeon_connector = to_radeon_connector(connector);
		bpc = radeon_get_monitor_bpc(connector);
		dither = radeon_connector->dither;
	}

	/* LVDS/eDP FMT is set up by atom */
	if (radeon_encoder->devices & ATOM_DEVICE_LCD_SUPPORT)
		return;

	/* not needed for analog */
	if ((radeon_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1) ||
	    (radeon_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2))
		return;

	if (bpc == 0)
		return;

	switch (bpc) {
	case 6:
		if (dither == RADEON_FMT_DITHER_ENABLE)
			/* XXX sort out optimal dither settings */
			tmp |= (FMT_FRAME_RANDOM_ENABLE | FMT_HIGHPASS_RANDOM_ENABLE |
				FMT_SPATIAL_DITHER_EN | FMT_SPATIAL_DITHER_DEPTH(0));
		else
			tmp |= (FMT_TRUNCATE_EN | FMT_TRUNCATE_DEPTH(0));
		break;
	case 8:
		if (dither == RADEON_FMT_DITHER_ENABLE)
			/* XXX sort out optimal dither settings */
			tmp |= (FMT_FRAME_RANDOM_ENABLE | FMT_HIGHPASS_RANDOM_ENABLE |
				FMT_RGB_RANDOM_ENABLE |
				FMT_SPATIAL_DITHER_EN | FMT_SPATIAL_DITHER_DEPTH(1));
		else
			tmp |= (FMT_TRUNCATE_EN | FMT_TRUNCATE_DEPTH(1));
		break;
	case 10:
		if (dither == RADEON_FMT_DITHER_ENABLE)
			/* XXX sort out optimal dither settings */
			tmp |= (FMT_FRAME_RANDOM_ENABLE | FMT_HIGHPASS_RANDOM_ENABLE |
				FMT_RGB_RANDOM_ENABLE |
				FMT_SPATIAL_DITHER_EN | FMT_SPATIAL_DITHER_DEPTH(2));
		else
			tmp |= (FMT_TRUNCATE_EN | FMT_TRUNCATE_DEPTH(2));
		break;
	default:
		/* not needed */
		break;
	}

	WREG32(FMT_BIT_DEPTH_CONTROL + radeon_crtc->crtc_offset, tmp);
}

/* display watermark setup */
/**
 * dce8_line_buffer_adjust - Set up the line buffer
 *
 * @rdev: radeon_device pointer
 * @radeon_crtc: the selected display controller
 * @mode: the current display mode on the selected display
 * controller
 *
 * Setup up the line buffer allocation for
 * the selected display controller (CIK).
 * Returns the line buffer size in pixels.
 */
static u32 dce8_line_buffer_adjust(struct radeon_device *rdev,
				   struct radeon_crtc *radeon_crtc,
				   struct drm_display_mode *mode)
{
	u32 tmp, buffer_alloc, i;
	u32 pipe_offset = radeon_crtc->crtc_id * 0x20;
	/*
	 * Line Buffer Setup
	 * There are 6 line buffers, one for each display controllers.
	 * There are 3 partitions per LB. Select the number of partitions
	 * to enable based on the display width.  For display widths larger
	 * than 4096, you need use to use 2 display controllers and combine
	 * them using the stereo blender.
	 */
	if (radeon_crtc->base.enabled && mode) {
		if (mode->crtc_hdisplay < 1920) {
			tmp = 1;
			buffer_alloc = 2;
		} else if (mode->crtc_hdisplay < 2560) {
			tmp = 2;
			buffer_alloc = 2;
		} else if (mode->crtc_hdisplay < 4096) {
			tmp = 0;
			buffer_alloc = (rdev->flags & RADEON_IS_IGP) ? 2 : 4;
		} else {
			DRM_DEBUG_KMS("Mode too big for LB!\n");
			tmp = 0;
			buffer_alloc = (rdev->flags & RADEON_IS_IGP) ? 2 : 4;
		}
	} else {
		tmp = 1;
		buffer_alloc = 0;
	}

	WREG32(LB_MEMORY_CTRL + radeon_crtc->crtc_offset,
	       LB_MEMORY_CONFIG(tmp) | LB_MEMORY_SIZE(0x6B0));

	WREG32(PIPE0_DMIF_BUFFER_CONTROL + pipe_offset,
	       DMIF_BUFFERS_ALLOCATED(buffer_alloc));
	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(PIPE0_DMIF_BUFFER_CONTROL + pipe_offset) &
		    DMIF_BUFFERS_ALLOCATED_COMPLETED)
			break;
		udelay(1);
	}

	if (radeon_crtc->base.enabled && mode) {
		switch (tmp) {
		case 0:
		default:
			return 4096 * 2;
		case 1:
			return 1920 * 2;
		case 2:
			return 2560 * 2;
		}
	}

	/* controller not enabled, so no lb used */
	return 0;
}

/**
 * cik_get_number_of_dram_channels - get the number of dram channels
 *
 * @rdev: radeon_device pointer
 *
 * Look up the number of video ram channels (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the number of dram channels
 */
static u32 cik_get_number_of_dram_channels(struct radeon_device *rdev)
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

struct dce8_wm_params {
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

/**
 * dce8_dram_bandwidth - get the dram bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the raw dram bandwidth (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the dram bandwidth in MBytes/s
 */
static u32 dce8_dram_bandwidth(struct dce8_wm_params *wm)
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

/**
 * dce8_dram_bandwidth_for_display - get the dram bandwidth for display
 *
 * @wm: watermark calculation data
 *
 * Calculate the dram bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the dram bandwidth for display in MBytes/s
 */
static u32 dce8_dram_bandwidth_for_display(struct dce8_wm_params *wm)
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

/**
 * dce8_data_return_bandwidth - get the data return bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the data return bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the data return bandwidth in MBytes/s
 */
static u32 dce8_data_return_bandwidth(struct dce8_wm_params *wm)
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

/**
 * dce8_dmif_request_bandwidth - get the dmif bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the dmif bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the dmif bandwidth in MBytes/s
 */
static u32 dce8_dmif_request_bandwidth(struct dce8_wm_params *wm)
{
	/* Calculate the DMIF Request Bandwidth */
	fixed20_12 disp_clk_request_efficiency; /* 0.8 */
	fixed20_12 disp_clk, bandwidth;
	fixed20_12 a, b;

	a.full = dfixed_const(1000);
	disp_clk.full = dfixed_const(wm->disp_clk);
	disp_clk.full = dfixed_div(disp_clk, a);
	a.full = dfixed_const(32);
	b.full = dfixed_mul(a, disp_clk);

	a.full = dfixed_const(10);
	disp_clk_request_efficiency.full = dfixed_const(8);
	disp_clk_request_efficiency.full = dfixed_div(disp_clk_request_efficiency, a);

	bandwidth.full = dfixed_mul(b, disp_clk_request_efficiency);

	return dfixed_trunc(bandwidth);
}

/**
 * dce8_available_bandwidth - get the min available bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the min available bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the min available bandwidth in MBytes/s
 */
static u32 dce8_available_bandwidth(struct dce8_wm_params *wm)
{
	/* Calculate the Available bandwidth. Display can use this temporarily but not in average. */
	u32 dram_bandwidth = dce8_dram_bandwidth(wm);
	u32 data_return_bandwidth = dce8_data_return_bandwidth(wm);
	u32 dmif_req_bandwidth = dce8_dmif_request_bandwidth(wm);

	return min(dram_bandwidth, min(data_return_bandwidth, dmif_req_bandwidth));
}

/**
 * dce8_average_bandwidth - get the average available bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the average available bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the average available bandwidth in MBytes/s
 */
static u32 dce8_average_bandwidth(struct dce8_wm_params *wm)
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

/**
 * dce8_latency_watermark - get the latency watermark
 *
 * @wm: watermark calculation data
 *
 * Calculate the latency watermark (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the latency watermark in ns
 */
static u32 dce8_latency_watermark(struct dce8_wm_params *wm)
{
	/* First calculate the latency in ns */
	u32 mc_latency = 2000; /* 2000 ns. */
	u32 available_bandwidth = dce8_available_bandwidth(wm);
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

/**
 * dce8_average_bandwidth_vs_dram_bandwidth_for_display - check
 * average and available dram bandwidth
 *
 * @wm: watermark calculation data
 *
 * Check if the display average bandwidth fits in the display
 * dram bandwidth (CIK).
 * Used for display watermark bandwidth calculations
 * Returns true if the display fits, false if not.
 */
static bool dce8_average_bandwidth_vs_dram_bandwidth_for_display(struct dce8_wm_params *wm)
{
	if (dce8_average_bandwidth(wm) <=
	    (dce8_dram_bandwidth_for_display(wm) / wm->num_heads))
		return true;
	else
		return false;
}

/**
 * dce8_average_bandwidth_vs_available_bandwidth - check
 * average and available bandwidth
 *
 * @wm: watermark calculation data
 *
 * Check if the display average bandwidth fits in the display
 * available bandwidth (CIK).
 * Used for display watermark bandwidth calculations
 * Returns true if the display fits, false if not.
 */
static bool dce8_average_bandwidth_vs_available_bandwidth(struct dce8_wm_params *wm)
{
	if (dce8_average_bandwidth(wm) <=
	    (dce8_available_bandwidth(wm) / wm->num_heads))
		return true;
	else
		return false;
}

/**
 * dce8_check_latency_hiding - check latency hiding
 *
 * @wm: watermark calculation data
 *
 * Check latency hiding (CIK).
 * Used for display watermark bandwidth calculations
 * Returns true if the display fits, false if not.
 */
static bool dce8_check_latency_hiding(struct dce8_wm_params *wm)
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

	if (dce8_latency_watermark(wm) <= latency_hiding)
		return true;
	else
		return false;
}

/**
 * dce8_program_watermarks - program display watermarks
 *
 * @rdev: radeon_device pointer
 * @radeon_crtc: the selected display controller
 * @lb_size: line buffer size
 * @num_heads: number of display controllers in use
 *
 * Calculate and program the display watermarks for the
 * selected display controller (CIK).
 */
static void dce8_program_watermarks(struct radeon_device *rdev,
				    struct radeon_crtc *radeon_crtc,
				    u32 lb_size, u32 num_heads)
{
	struct drm_display_mode *mode = &radeon_crtc->base.mode;
	struct dce8_wm_params wm_low, wm_high;
	u32 pixel_period;
	u32 line_time = 0;
	u32 latency_watermark_a = 0, latency_watermark_b = 0;
	u32 tmp, wm_mask;

	if (radeon_crtc->base.enabled && num_heads && mode) {
		pixel_period = 1000000 / (u32)mode->clock;
		line_time = min((u32)mode->crtc_htotal * pixel_period, (u32)65535);

		/* watermark for high clocks */
		if ((rdev->pm.pm_method == PM_METHOD_DPM) &&
		    rdev->pm.dpm_enabled) {
			wm_high.yclk =
				radeon_dpm_get_mclk(rdev, false) * 10;
			wm_high.sclk =
				radeon_dpm_get_sclk(rdev, false) * 10;
		} else {
			wm_high.yclk = rdev->pm.current_mclk * 10;
			wm_high.sclk = rdev->pm.current_sclk * 10;
		}

		wm_high.disp_clk = mode->clock;
		wm_high.src_width = mode->crtc_hdisplay;
		wm_high.active_time = mode->crtc_hdisplay * pixel_period;
		wm_high.blank_time = line_time - wm_high.active_time;
		wm_high.interlaced = false;
		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			wm_high.interlaced = true;
		wm_high.vsc = radeon_crtc->vsc;
		wm_high.vtaps = 1;
		if (radeon_crtc->rmx_type != RMX_OFF)
			wm_high.vtaps = 2;
		wm_high.bytes_per_pixel = 4; /* XXX: get this from fb config */
		wm_high.lb_size = lb_size;
		wm_high.dram_channels = cik_get_number_of_dram_channels(rdev);
		wm_high.num_heads = num_heads;

		/* set for high clocks */
		latency_watermark_a = min(dce8_latency_watermark(&wm_high), (u32)65535);

		/* possibly force display priority to high */
		/* should really do this at mode validation time... */
		if (!dce8_average_bandwidth_vs_dram_bandwidth_for_display(&wm_high) ||
		    !dce8_average_bandwidth_vs_available_bandwidth(&wm_high) ||
		    !dce8_check_latency_hiding(&wm_high) ||
		    (rdev->disp_priority == 2)) {
			DRM_DEBUG_KMS("force priority to high\n");
		}

		/* watermark for low clocks */
		if ((rdev->pm.pm_method == PM_METHOD_DPM) &&
		    rdev->pm.dpm_enabled) {
			wm_low.yclk =
				radeon_dpm_get_mclk(rdev, true) * 10;
			wm_low.sclk =
				radeon_dpm_get_sclk(rdev, true) * 10;
		} else {
			wm_low.yclk = rdev->pm.current_mclk * 10;
			wm_low.sclk = rdev->pm.current_sclk * 10;
		}

		wm_low.disp_clk = mode->clock;
		wm_low.src_width = mode->crtc_hdisplay;
		wm_low.active_time = mode->crtc_hdisplay * pixel_period;
		wm_low.blank_time = line_time - wm_low.active_time;
		wm_low.interlaced = false;
		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			wm_low.interlaced = true;
		wm_low.vsc = radeon_crtc->vsc;
		wm_low.vtaps = 1;
		if (radeon_crtc->rmx_type != RMX_OFF)
			wm_low.vtaps = 2;
		wm_low.bytes_per_pixel = 4; /* XXX: get this from fb config */
		wm_low.lb_size = lb_size;
		wm_low.dram_channels = cik_get_number_of_dram_channels(rdev);
		wm_low.num_heads = num_heads;

		/* set for low clocks */
		latency_watermark_b = min(dce8_latency_watermark(&wm_low), (u32)65535);

		/* possibly force display priority to high */
		/* should really do this at mode validation time... */
		if (!dce8_average_bandwidth_vs_dram_bandwidth_for_display(&wm_low) ||
		    !dce8_average_bandwidth_vs_available_bandwidth(&wm_low) ||
		    !dce8_check_latency_hiding(&wm_low) ||
		    (rdev->disp_priority == 2)) {
			DRM_DEBUG_KMS("force priority to high\n");
		}
	}

	/* select wm A */
	wm_mask = RREG32(DPG_WATERMARK_MASK_CONTROL + radeon_crtc->crtc_offset);
	tmp = wm_mask;
	tmp &= ~LATENCY_WATERMARK_MASK(3);
	tmp |= LATENCY_WATERMARK_MASK(1);
	WREG32(DPG_WATERMARK_MASK_CONTROL + radeon_crtc->crtc_offset, tmp);
	WREG32(DPG_PIPE_LATENCY_CONTROL + radeon_crtc->crtc_offset,
	       (LATENCY_LOW_WATERMARK(latency_watermark_a) |
		LATENCY_HIGH_WATERMARK(line_time)));
	/* select wm B */
	tmp = RREG32(DPG_WATERMARK_MASK_CONTROL + radeon_crtc->crtc_offset);
	tmp &= ~LATENCY_WATERMARK_MASK(3);
	tmp |= LATENCY_WATERMARK_MASK(2);
	WREG32(DPG_WATERMARK_MASK_CONTROL + radeon_crtc->crtc_offset, tmp);
	WREG32(DPG_PIPE_LATENCY_CONTROL + radeon_crtc->crtc_offset,
	       (LATENCY_LOW_WATERMARK(latency_watermark_b) |
		LATENCY_HIGH_WATERMARK(line_time)));
	/* restore original selection */
	WREG32(DPG_WATERMARK_MASK_CONTROL + radeon_crtc->crtc_offset, wm_mask);

	/* save values for DPM */
	radeon_crtc->line_time = line_time;
	radeon_crtc->wm_high = latency_watermark_a;
	radeon_crtc->wm_low = latency_watermark_b;
}

/**
 * dce8_bandwidth_update - program display watermarks
 *
 * @rdev: radeon_device pointer
 *
 * Calculate and program the display watermarks and line
 * buffer allocation (CIK).
 */
void dce8_bandwidth_update(struct radeon_device *rdev)
{
	struct drm_display_mode *mode = NULL;
	u32 num_heads = 0, lb_size;
	int i;

	if (!rdev->mode_info.mode_config_initialized)
		return;

	radeon_update_display_priority(rdev);

	for (i = 0; i < rdev->num_crtc; i++) {
		if (rdev->mode_info.crtcs[i]->base.enabled)
			num_heads++;
	}
	for (i = 0; i < rdev->num_crtc; i++) {
		mode = &rdev->mode_info.crtcs[i]->base.mode;
		lb_size = dce8_line_buffer_adjust(rdev, rdev->mode_info.crtcs[i], mode);
		dce8_program_watermarks(rdev, rdev->mode_info.crtcs[i], lb_size, num_heads);
	}
}

/**
 * cik_get_gpu_clock_counter - return GPU clock counter snapshot
 *
 * @rdev: radeon_device pointer
 *
 * Fetches a GPU clock counter snapshot (SI).
 * Returns the 64 bit clock counter snapshot.
 */
uint64_t cik_get_gpu_clock_counter(struct radeon_device *rdev)
{
	uint64_t clock;

	mutex_lock(&rdev->gpu_clock_mutex);
	WREG32(RLC_CAPTURE_GPU_CLOCK_COUNT, 1);
	clock = (uint64_t)RREG32(RLC_GPU_CLOCK_COUNT_LSB) |
	        ((uint64_t)RREG32(RLC_GPU_CLOCK_COUNT_MSB) << 32ULL);
	mutex_unlock(&rdev->gpu_clock_mutex);
	return clock;
}

static int cik_set_uvd_clock(struct radeon_device *rdev, u32 clock,
                              u32 cntl_reg, u32 status_reg)
{
	int r, i;
	struct atom_clock_dividers dividers;
	uint32_t tmp;

	r = radeon_atom_get_clock_dividers(rdev, COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK,
					   clock, false, &dividers);
	if (r)
		return r;

	tmp = RREG32_SMC(cntl_reg);
	tmp &= ~(DCLK_DIR_CNTL_EN|DCLK_DIVIDER_MASK);
	tmp |= dividers.post_divider;
	WREG32_SMC(cntl_reg, tmp);

	for (i = 0; i < 100; i++) {
		if (RREG32_SMC(status_reg) & DCLK_STATUS)
			break;
		mdelay(10);
	}
	if (i == 100)
		return -ETIMEDOUT;

	return 0;
}

int cik_set_uvd_clocks(struct radeon_device *rdev, u32 vclk, u32 dclk)
{
	int r = 0;

	r = cik_set_uvd_clock(rdev, vclk, CG_VCLK_CNTL, CG_VCLK_STATUS);
	if (r)
		return r;

	r = cik_set_uvd_clock(rdev, dclk, CG_DCLK_CNTL, CG_DCLK_STATUS);
	return r;
}

int cik_set_vce_clocks(struct radeon_device *rdev, u32 evclk, u32 ecclk)
{
	int r, i;
	struct atom_clock_dividers dividers;
	u32 tmp;

	r = radeon_atom_get_clock_dividers(rdev, COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK,
					   ecclk, false, &dividers);
	if (r)
		return r;

	for (i = 0; i < 100; i++) {
		if (RREG32_SMC(CG_ECLK_STATUS) & ECLK_STATUS)
			break;
		mdelay(10);
	}
	if (i == 100)
		return -ETIMEDOUT;

	tmp = RREG32_SMC(CG_ECLK_CNTL);
	tmp &= ~(ECLK_DIR_CNTL_EN|ECLK_DIVIDER_MASK);
	tmp |= dividers.post_divider;
	WREG32_SMC(CG_ECLK_CNTL, tmp);

	for (i = 0; i < 100; i++) {
		if (RREG32_SMC(CG_ECLK_STATUS) & ECLK_STATUS)
			break;
		mdelay(10);
	}
	if (i == 100)
		return -ETIMEDOUT;

	return 0;
}

static void cik_pcie_gen3_enable(struct radeon_device *rdev)
{
	struct pci_dev *root = rdev->pdev->bus->self;
	int bridge_pos, gpu_pos;
	u32 speed_cntl, mask, current_data_rate;
	int ret, i;
	u16 tmp16;

	if (pci_is_root_bus(rdev->pdev->bus))
		return;

	if (radeon_pcie_gen2 == 0)
		return;

	if (rdev->flags & RADEON_IS_IGP)
		return;

	if (!(rdev->flags & RADEON_IS_PCIE))
		return;

	ret = drm_pcie_get_speed_cap_mask(rdev->ddev, &mask);
	if (ret != 0)
		return;

	if (!(mask & (DRM_PCIE_SPEED_50 | DRM_PCIE_SPEED_80)))
		return;

	speed_cntl = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
	current_data_rate = (speed_cntl & LC_CURRENT_DATA_RATE_MASK) >>
		LC_CURRENT_DATA_RATE_SHIFT;
	if (mask & DRM_PCIE_SPEED_80) {
		if (current_data_rate == 2) {
			DRM_INFO("PCIE gen 3 link speeds already enabled\n");
			return;
		}
		DRM_INFO("enabling PCIE gen 3 link speeds, disable with radeon.pcie_gen2=0\n");
	} else if (mask & DRM_PCIE_SPEED_50) {
		if (current_data_rate == 1) {
			DRM_INFO("PCIE gen 2 link speeds already enabled\n");
			return;
		}
		DRM_INFO("enabling PCIE gen 2 link speeds, disable with radeon.pcie_gen2=0\n");
	}

	bridge_pos = pci_pcie_cap(root);
	if (!bridge_pos)
		return;

	gpu_pos = pci_pcie_cap(rdev->pdev);
	if (!gpu_pos)
		return;

	if (mask & DRM_PCIE_SPEED_80) {
		/* re-try equalization if gen3 is not already enabled */
		if (current_data_rate != 2) {
			u16 bridge_cfg, gpu_cfg;
			u16 bridge_cfg2, gpu_cfg2;
			u32 max_lw, current_lw, tmp;

			pci_read_config_word(root, bridge_pos + PCI_EXP_LNKCTL, &bridge_cfg);
			pci_read_config_word(rdev->pdev, gpu_pos + PCI_EXP_LNKCTL, &gpu_cfg);

			tmp16 = bridge_cfg | PCI_EXP_LNKCTL_HAWD;
			pci_write_config_word(root, bridge_pos + PCI_EXP_LNKCTL, tmp16);

			tmp16 = gpu_cfg | PCI_EXP_LNKCTL_HAWD;
			pci_write_config_word(rdev->pdev, gpu_pos + PCI_EXP_LNKCTL, tmp16);

			tmp = RREG32_PCIE_PORT(PCIE_LC_STATUS1);
			max_lw = (tmp & LC_DETECTED_LINK_WIDTH_MASK) >> LC_DETECTED_LINK_WIDTH_SHIFT;
			current_lw = (tmp & LC_OPERATING_LINK_WIDTH_MASK) >> LC_OPERATING_LINK_WIDTH_SHIFT;

			if (current_lw < max_lw) {
				tmp = RREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL);
				if (tmp & LC_RENEGOTIATION_SUPPORT) {
					tmp &= ~(LC_LINK_WIDTH_MASK | LC_UPCONFIGURE_DIS);
					tmp |= (max_lw << LC_LINK_WIDTH_SHIFT);
					tmp |= LC_UPCONFIGURE_SUPPORT | LC_RENEGOTIATE_EN | LC_RECONFIG_NOW;
					WREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL, tmp);
				}
			}

			for (i = 0; i < 10; i++) {
				/* check status */
				pci_read_config_word(rdev->pdev, gpu_pos + PCI_EXP_DEVSTA, &tmp16);
				if (tmp16 & PCI_EXP_DEVSTA_TRPND)
					break;

				pci_read_config_word(root, bridge_pos + PCI_EXP_LNKCTL, &bridge_cfg);
				pci_read_config_word(rdev->pdev, gpu_pos + PCI_EXP_LNKCTL, &gpu_cfg);

				pci_read_config_word(root, bridge_pos + PCI_EXP_LNKCTL2, &bridge_cfg2);
				pci_read_config_word(rdev->pdev, gpu_pos + PCI_EXP_LNKCTL2, &gpu_cfg2);

				tmp = RREG32_PCIE_PORT(PCIE_LC_CNTL4);
				tmp |= LC_SET_QUIESCE;
				WREG32_PCIE_PORT(PCIE_LC_CNTL4, tmp);

				tmp = RREG32_PCIE_PORT(PCIE_LC_CNTL4);
				tmp |= LC_REDO_EQ;
				WREG32_PCIE_PORT(PCIE_LC_CNTL4, tmp);

				mdelay(100);

				/* linkctl */
				pci_read_config_word(root, bridge_pos + PCI_EXP_LNKCTL, &tmp16);
				tmp16 &= ~PCI_EXP_LNKCTL_HAWD;
				tmp16 |= (bridge_cfg & PCI_EXP_LNKCTL_HAWD);
				pci_write_config_word(root, bridge_pos + PCI_EXP_LNKCTL, tmp16);

				pci_read_config_word(rdev->pdev, gpu_pos + PCI_EXP_LNKCTL, &tmp16);
				tmp16 &= ~PCI_EXP_LNKCTL_HAWD;
				tmp16 |= (gpu_cfg & PCI_EXP_LNKCTL_HAWD);
				pci_write_config_word(rdev->pdev, gpu_pos + PCI_EXP_LNKCTL, tmp16);

				/* linkctl2 */
				pci_read_config_word(root, bridge_pos + PCI_EXP_LNKCTL2, &tmp16);
				tmp16 &= ~((1 << 4) | (7 << 9));
				tmp16 |= (bridge_cfg2 & ((1 << 4) | (7 << 9)));
				pci_write_config_word(root, bridge_pos + PCI_EXP_LNKCTL2, tmp16);

				pci_read_config_word(rdev->pdev, gpu_pos + PCI_EXP_LNKCTL2, &tmp16);
				tmp16 &= ~((1 << 4) | (7 << 9));
				tmp16 |= (gpu_cfg2 & ((1 << 4) | (7 << 9)));
				pci_write_config_word(rdev->pdev, gpu_pos + PCI_EXP_LNKCTL2, tmp16);

				tmp = RREG32_PCIE_PORT(PCIE_LC_CNTL4);
				tmp &= ~LC_SET_QUIESCE;
				WREG32_PCIE_PORT(PCIE_LC_CNTL4, tmp);
			}
		}
	}

	/* set the link speed */
	speed_cntl |= LC_FORCE_EN_SW_SPEED_CHANGE | LC_FORCE_DIS_HW_SPEED_CHANGE;
	speed_cntl &= ~LC_FORCE_DIS_SW_SPEED_CHANGE;
	WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, speed_cntl);

	pci_read_config_word(rdev->pdev, gpu_pos + PCI_EXP_LNKCTL2, &tmp16);
	tmp16 &= ~0xf;
	if (mask & DRM_PCIE_SPEED_80)
		tmp16 |= 3; /* gen3 */
	else if (mask & DRM_PCIE_SPEED_50)
		tmp16 |= 2; /* gen2 */
	else
		tmp16 |= 1; /* gen1 */
	pci_write_config_word(rdev->pdev, gpu_pos + PCI_EXP_LNKCTL2, tmp16);

	speed_cntl = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
	speed_cntl |= LC_INITIATE_LINK_SPEED_CHANGE;
	WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, speed_cntl);

	for (i = 0; i < rdev->usec_timeout; i++) {
		speed_cntl = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
		if ((speed_cntl & LC_INITIATE_LINK_SPEED_CHANGE) == 0)
			break;
		udelay(1);
	}
}

static void cik_program_aspm(struct radeon_device *rdev)
{
	u32 data, orig;
	bool disable_l0s = false, disable_l1 = false, disable_plloff_in_l1 = false;
	bool disable_clkreq = false;

	if (radeon_aspm == 0)
		return;

	/* XXX double check IGPs */
	if (rdev->flags & RADEON_IS_IGP)
		return;

	if (!(rdev->flags & RADEON_IS_PCIE))
		return;

	orig = data = RREG32_PCIE_PORT(PCIE_LC_N_FTS_CNTL);
	data &= ~LC_XMIT_N_FTS_MASK;
	data |= LC_XMIT_N_FTS(0x24) | LC_XMIT_N_FTS_OVERRIDE_EN;
	if (orig != data)
		WREG32_PCIE_PORT(PCIE_LC_N_FTS_CNTL, data);

	orig = data = RREG32_PCIE_PORT(PCIE_LC_CNTL3);
	data |= LC_GO_TO_RECOVERY;
	if (orig != data)
		WREG32_PCIE_PORT(PCIE_LC_CNTL3, data);

	orig = data = RREG32_PCIE_PORT(PCIE_P_CNTL);
	data |= P_IGNORE_EDB_ERR;
	if (orig != data)
		WREG32_PCIE_PORT(PCIE_P_CNTL, data);

	orig = data = RREG32_PCIE_PORT(PCIE_LC_CNTL);
	data &= ~(LC_L0S_INACTIVITY_MASK | LC_L1_INACTIVITY_MASK);
	data |= LC_PMI_TO_L1_DIS;
	if (!disable_l0s)
		data |= LC_L0S_INACTIVITY(7);

	if (!disable_l1) {
		data |= LC_L1_INACTIVITY(7);
		data &= ~LC_PMI_TO_L1_DIS;
		if (orig != data)
			WREG32_PCIE_PORT(PCIE_LC_CNTL, data);

		if (!disable_plloff_in_l1) {
			bool clk_req_support;

			orig = data = RREG32_PCIE_PORT(PB0_PIF_PWRDOWN_0);
			data &= ~(PLL_POWER_STATE_IN_OFF_0_MASK | PLL_POWER_STATE_IN_TXS2_0_MASK);
			data |= PLL_POWER_STATE_IN_OFF_0(7) | PLL_POWER_STATE_IN_TXS2_0(7);
			if (orig != data)
				WREG32_PCIE_PORT(PB0_PIF_PWRDOWN_0, data);

			orig = data = RREG32_PCIE_PORT(PB0_PIF_PWRDOWN_1);
			data &= ~(PLL_POWER_STATE_IN_OFF_1_MASK | PLL_POWER_STATE_IN_TXS2_1_MASK);
			data |= PLL_POWER_STATE_IN_OFF_1(7) | PLL_POWER_STATE_IN_TXS2_1(7);
			if (orig != data)
				WREG32_PCIE_PORT(PB0_PIF_PWRDOWN_1, data);

			orig = data = RREG32_PCIE_PORT(PB1_PIF_PWRDOWN_0);
			data &= ~(PLL_POWER_STATE_IN_OFF_0_MASK | PLL_POWER_STATE_IN_TXS2_0_MASK);
			data |= PLL_POWER_STATE_IN_OFF_0(7) | PLL_POWER_STATE_IN_TXS2_0(7);
			if (orig != data)
				WREG32_PCIE_PORT(PB1_PIF_PWRDOWN_0, data);

			orig = data = RREG32_PCIE_PORT(PB1_PIF_PWRDOWN_1);
			data &= ~(PLL_POWER_STATE_IN_OFF_1_MASK | PLL_POWER_STATE_IN_TXS2_1_MASK);
			data |= PLL_POWER_STATE_IN_OFF_1(7) | PLL_POWER_STATE_IN_TXS2_1(7);
			if (orig != data)
				WREG32_PCIE_PORT(PB1_PIF_PWRDOWN_1, data);

			orig = data = RREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL);
			data &= ~LC_DYN_LANES_PWR_STATE_MASK;
			data |= LC_DYN_LANES_PWR_STATE(3);
			if (orig != data)
				WREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL, data);

			if (!disable_clkreq &&
			    !pci_is_root_bus(rdev->pdev->bus)) {
				struct pci_dev *root = rdev->pdev->bus->self;
				u32 lnkcap;

				clk_req_support = false;
				pcie_capability_read_dword(root, PCI_EXP_LNKCAP, &lnkcap);
				if (lnkcap & PCI_EXP_LNKCAP_CLKPM)
					clk_req_support = true;
			} else {
				clk_req_support = false;
			}

			if (clk_req_support) {
				orig = data = RREG32_PCIE_PORT(PCIE_LC_CNTL2);
				data |= LC_ALLOW_PDWN_IN_L1 | LC_ALLOW_PDWN_IN_L23;
				if (orig != data)
					WREG32_PCIE_PORT(PCIE_LC_CNTL2, data);

				orig = data = RREG32_SMC(THM_CLK_CNTL);
				data &= ~(CMON_CLK_SEL_MASK | TMON_CLK_SEL_MASK);
				data |= CMON_CLK_SEL(1) | TMON_CLK_SEL(1);
				if (orig != data)
					WREG32_SMC(THM_CLK_CNTL, data);

				orig = data = RREG32_SMC(MISC_CLK_CTRL);
				data &= ~(DEEP_SLEEP_CLK_SEL_MASK | ZCLK_SEL_MASK);
				data |= DEEP_SLEEP_CLK_SEL(1) | ZCLK_SEL(1);
				if (orig != data)
					WREG32_SMC(MISC_CLK_CTRL, data);

				orig = data = RREG32_SMC(CG_CLKPIN_CNTL);
				data &= ~BCLK_AS_XCLK;
				if (orig != data)
					WREG32_SMC(CG_CLKPIN_CNTL, data);

				orig = data = RREG32_SMC(CG_CLKPIN_CNTL_2);
				data &= ~FORCE_BIF_REFCLK_EN;
				if (orig != data)
					WREG32_SMC(CG_CLKPIN_CNTL_2, data);

				orig = data = RREG32_SMC(MPLL_BYPASSCLK_SEL);
				data &= ~MPLL_CLKOUT_SEL_MASK;
				data |= MPLL_CLKOUT_SEL(4);
				if (orig != data)
					WREG32_SMC(MPLL_BYPASSCLK_SEL, data);
			}
		}
	} else {
		if (orig != data)
			WREG32_PCIE_PORT(PCIE_LC_CNTL, data);
	}

	orig = data = RREG32_PCIE_PORT(PCIE_CNTL2);
	data |= SLV_MEM_LS_EN | MST_MEM_LS_EN | REPLAY_MEM_LS_EN;
	if (orig != data)
		WREG32_PCIE_PORT(PCIE_CNTL2, data);

	if (!disable_l0s) {
		data = RREG32_PCIE_PORT(PCIE_LC_N_FTS_CNTL);
		if((data & LC_N_FTS_MASK) == LC_N_FTS_MASK) {
			data = RREG32_PCIE_PORT(PCIE_LC_STATUS1);
			if ((data & LC_REVERSE_XMIT) && (data & LC_REVERSE_RCVR)) {
				orig = data = RREG32_PCIE_PORT(PCIE_LC_CNTL);
				data &= ~LC_L0S_INACTIVITY_MASK;
				if (orig != data)
					WREG32_PCIE_PORT(PCIE_LC_CNTL, data);
			}
		}
	}
}
