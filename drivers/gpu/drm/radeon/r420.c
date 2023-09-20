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

#include <linux/pci.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>

#include "atom.h"
#include "r100d.h"
#include "r420_reg_safe.h"
#include "r420d.h"
#include "radeon.h"
#include "radeon_asic.h"
#include "radeon_reg.h"

void r420_pm_init_profile(struct radeon_device *rdev)
{
	/* default */
	rdev->pm.profiles[PM_PROFILE_DEFAULT_IDX].dpms_off_ps_idx = rdev->pm.default_power_state_index;
	rdev->pm.profiles[PM_PROFILE_DEFAULT_IDX].dpms_on_ps_idx = rdev->pm.default_power_state_index;
	rdev->pm.profiles[PM_PROFILE_DEFAULT_IDX].dpms_off_cm_idx = 0;
	rdev->pm.profiles[PM_PROFILE_DEFAULT_IDX].dpms_on_cm_idx = 0;
	/* low sh */
	rdev->pm.profiles[PM_PROFILE_LOW_SH_IDX].dpms_off_ps_idx = 0;
	rdev->pm.profiles[PM_PROFILE_LOW_SH_IDX].dpms_on_ps_idx = 0;
	rdev->pm.profiles[PM_PROFILE_LOW_SH_IDX].dpms_off_cm_idx = 0;
	rdev->pm.profiles[PM_PROFILE_LOW_SH_IDX].dpms_on_cm_idx = 0;
	/* mid sh */
	rdev->pm.profiles[PM_PROFILE_MID_SH_IDX].dpms_off_ps_idx = 0;
	rdev->pm.profiles[PM_PROFILE_MID_SH_IDX].dpms_on_ps_idx = 1;
	rdev->pm.profiles[PM_PROFILE_MID_SH_IDX].dpms_off_cm_idx = 0;
	rdev->pm.profiles[PM_PROFILE_MID_SH_IDX].dpms_on_cm_idx = 0;
	/* high sh */
	rdev->pm.profiles[PM_PROFILE_HIGH_SH_IDX].dpms_off_ps_idx = 0;
	rdev->pm.profiles[PM_PROFILE_HIGH_SH_IDX].dpms_on_ps_idx = rdev->pm.default_power_state_index;
	rdev->pm.profiles[PM_PROFILE_HIGH_SH_IDX].dpms_off_cm_idx = 0;
	rdev->pm.profiles[PM_PROFILE_HIGH_SH_IDX].dpms_on_cm_idx = 0;
	/* low mh */
	rdev->pm.profiles[PM_PROFILE_LOW_MH_IDX].dpms_off_ps_idx = 0;
	rdev->pm.profiles[PM_PROFILE_LOW_MH_IDX].dpms_on_ps_idx = rdev->pm.default_power_state_index;
	rdev->pm.profiles[PM_PROFILE_LOW_MH_IDX].dpms_off_cm_idx = 0;
	rdev->pm.profiles[PM_PROFILE_LOW_MH_IDX].dpms_on_cm_idx = 0;
	/* mid mh */
	rdev->pm.profiles[PM_PROFILE_MID_MH_IDX].dpms_off_ps_idx = 0;
	rdev->pm.profiles[PM_PROFILE_MID_MH_IDX].dpms_on_ps_idx = rdev->pm.default_power_state_index;
	rdev->pm.profiles[PM_PROFILE_MID_MH_IDX].dpms_off_cm_idx = 0;
	rdev->pm.profiles[PM_PROFILE_MID_MH_IDX].dpms_on_cm_idx = 0;
	/* high mh */
	rdev->pm.profiles[PM_PROFILE_HIGH_MH_IDX].dpms_off_ps_idx = 0;
	rdev->pm.profiles[PM_PROFILE_HIGH_MH_IDX].dpms_on_ps_idx = rdev->pm.default_power_state_index;
	rdev->pm.profiles[PM_PROFILE_HIGH_MH_IDX].dpms_off_cm_idx = 0;
	rdev->pm.profiles[PM_PROFILE_HIGH_MH_IDX].dpms_on_cm_idx = 0;
}

static void r420_set_reg_safe(struct radeon_device *rdev)
{
	rdev->config.r300.reg_safe_bm = r420_reg_safe_bm;
	rdev->config.r300.reg_safe_bm_size = ARRAY_SIZE(r420_reg_safe_bm);
}

void r420_pipes_init(struct radeon_device *rdev)
{
	unsigned tmp;
	unsigned gb_pipe_select;
	unsigned num_pipes;

	/* GA_ENHANCE workaround TCL deadlock issue */
	WREG32(R300_GA_ENHANCE, R300_GA_DEADLOCK_CNTL | R300_GA_FASTSYNC_CNTL |
	       (1 << 2) | (1 << 3));
	/* add idle wait as per freedesktop.org bug 24041 */
	if (r100_gui_wait_for_idle(rdev)) {
		pr_warn("Failed to wait GUI idle while programming pipes. Bad things might happen.\n");
	}
	/* get max number of pipes */
	gb_pipe_select = RREG32(R400_GB_PIPE_SELECT);
	num_pipes = ((gb_pipe_select >> 12) & 3) + 1;

	/* SE chips have 1 pipe */
	if ((rdev->pdev->device == 0x5e4c) ||
	    (rdev->pdev->device == 0x5e4f))
		num_pipes = 1;

	rdev->num_gb_pipes = num_pipes;
	tmp = 0;
	switch (num_pipes) {
	default:
		/* force to 1 pipe */
		num_pipes = 1;
		fallthrough;
	case 1:
		tmp = (0 << 1);
		break;
	case 2:
		tmp = (3 << 1);
		break;
	case 3:
		tmp = (6 << 1);
		break;
	case 4:
		tmp = (7 << 1);
		break;
	}
	WREG32(R500_SU_REG_DEST, (1 << num_pipes) - 1);
	/* Sub pixel 1/12 so we can have 4K rendering according to doc */
	tmp |= R300_TILE_SIZE_16 | R300_ENABLE_TILING;
	WREG32(R300_GB_TILE_CONFIG, tmp);
	if (r100_gui_wait_for_idle(rdev)) {
		pr_warn("Failed to wait GUI idle while programming pipes. Bad things might happen.\n");
	}

	tmp = RREG32(R300_DST_PIPE_CONFIG);
	WREG32(R300_DST_PIPE_CONFIG, tmp | R300_PIPE_AUTO_CONFIG);

	WREG32(R300_RB2D_DSTCACHE_MODE,
	       RREG32(R300_RB2D_DSTCACHE_MODE) |
	       R300_DC_AUTOFLUSH_ENABLE |
	       R300_DC_DC_DISABLE_IGNORE_PE);

	if (r100_gui_wait_for_idle(rdev)) {
		pr_warn("Failed to wait GUI idle while programming pipes. Bad things might happen.\n");
	}

	if (rdev->family == CHIP_RV530) {
		tmp = RREG32(RV530_GB_PIPE_SELECT2);
		if ((tmp & 3) == 3)
			rdev->num_z_pipes = 2;
		else
			rdev->num_z_pipes = 1;
	} else
		rdev->num_z_pipes = 1;

	DRM_INFO("radeon: %d quad pipes, %d z pipes initialized.\n",
		 rdev->num_gb_pipes, rdev->num_z_pipes);
}

u32 r420_mc_rreg(struct radeon_device *rdev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&rdev->mc_idx_lock, flags);
	WREG32(R_0001F8_MC_IND_INDEX, S_0001F8_MC_IND_ADDR(reg));
	r = RREG32(R_0001FC_MC_IND_DATA);
	spin_unlock_irqrestore(&rdev->mc_idx_lock, flags);
	return r;
}

void r420_mc_wreg(struct radeon_device *rdev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&rdev->mc_idx_lock, flags);
	WREG32(R_0001F8_MC_IND_INDEX, S_0001F8_MC_IND_ADDR(reg) |
		S_0001F8_MC_IND_WR_EN(1));
	WREG32(R_0001FC_MC_IND_DATA, v);
	spin_unlock_irqrestore(&rdev->mc_idx_lock, flags);
}

static void r420_debugfs(struct radeon_device *rdev)
{
	r100_debugfs_rbbm_init(rdev);
	r420_debugfs_pipes_info_init(rdev);
}

static void r420_clock_resume(struct radeon_device *rdev)
{
	u32 sclk_cntl;

	if (radeon_dynclks != -1 && radeon_dynclks)
		radeon_atom_set_clock_gating(rdev, 1);
	sclk_cntl = RREG32_PLL(R_00000D_SCLK_CNTL);
	sclk_cntl |= S_00000D_FORCE_CP(1) | S_00000D_FORCE_VIP(1);
	if (rdev->family == CHIP_R420)
		sclk_cntl |= S_00000D_FORCE_PX(1) | S_00000D_FORCE_TX(1);
	WREG32_PLL(R_00000D_SCLK_CNTL, sclk_cntl);
}

static void r420_cp_errata_init(struct radeon_device *rdev)
{
	int r;
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];

	/* RV410 and R420 can lock up if CP DMA to host memory happens
	 * while the 2D engine is busy.
	 *
	 * The proper workaround is to queue a RESYNC at the beginning
	 * of the CP init, apparently.
	 */
	radeon_scratch_get(rdev, &rdev->config.r300.resync_scratch);
	r = radeon_ring_lock(rdev, ring, 8);
	WARN_ON(r);
	radeon_ring_write(ring, PACKET0(R300_CP_RESYNC_ADDR, 1));
	radeon_ring_write(ring, rdev->config.r300.resync_scratch);
	radeon_ring_write(ring, 0xDEADBEEF);
	radeon_ring_unlock_commit(rdev, ring, false);
}

static void r420_cp_errata_fini(struct radeon_device *rdev)
{
	int r;
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];

	/* Catch the RESYNC we dispatched all the way back,
	 * at the very beginning of the CP init.
	 */
	r = radeon_ring_lock(rdev, ring, 8);
	WARN_ON(r);
	radeon_ring_write(ring, PACKET0(R300_RB3D_DSTCACHE_CTLSTAT, 0));
	radeon_ring_write(ring, R300_RB3D_DC_FINISH);
	radeon_ring_unlock_commit(rdev, ring, false);
	radeon_scratch_free(rdev, rdev->config.r300.resync_scratch);
}

static int r420_startup(struct radeon_device *rdev)
{
	int r;

	/* set common regs */
	r100_set_common_regs(rdev);
	/* program mc */
	r300_mc_program(rdev);
	/* Resume clock */
	r420_clock_resume(rdev);
	/* Initialize GART (initialize after TTM so we can allocate
	 * memory through TTM but finalize after TTM) */
	if (rdev->flags & RADEON_IS_PCIE) {
		r = rv370_pcie_gart_enable(rdev);
		if (r)
			return r;
	}
	if (rdev->flags & RADEON_IS_PCI) {
		r = r100_pci_gart_enable(rdev);
		if (r)
			return r;
	}
	r420_pipes_init(rdev);

	/* allocate wb buffer */
	r = radeon_wb_init(rdev);
	if (r)
		return r;

	r = radeon_fence_driver_start_ring(rdev, RADEON_RING_TYPE_GFX_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP fences (%d).\n", r);
		return r;
	}

	/* Enable IRQ */
	if (!rdev->irq.installed) {
		r = radeon_irq_kms_init(rdev);
		if (r)
			return r;
	}

	r100_irq_set(rdev);
	rdev->config.r300.hdp_cntl = RREG32(RADEON_HOST_PATH_CNTL);
	/* 1M ring buffer */
	r = r100_cp_init(rdev, 1024 * 1024);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP (%d).\n", r);
		return r;
	}
	r420_cp_errata_init(rdev);

	r = radeon_ib_pool_init(rdev);
	if (r) {
		dev_err(rdev->dev, "IB initialization failed (%d).\n", r);
		return r;
	}

	return 0;
}

int r420_resume(struct radeon_device *rdev)
{
	int r;

	/* Make sur GART are not working */
	if (rdev->flags & RADEON_IS_PCIE)
		rv370_pcie_gart_disable(rdev);
	if (rdev->flags & RADEON_IS_PCI)
		r100_pci_gart_disable(rdev);
	/* Resume clock before doing reset */
	r420_clock_resume(rdev);
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_asic_reset(rdev)) {
		dev_warn(rdev->dev, "GPU reset failed ! (0xE40=0x%08X, 0x7C0=0x%08X)\n",
			RREG32(R_000E40_RBBM_STATUS),
			RREG32(R_0007C0_CP_STAT));
	}
	/* check if cards are posted or not */
	if (rdev->is_atom_bios) {
		atom_asic_init(rdev->mode_info.atom_context);
	} else {
		radeon_combios_asic_init(rdev->ddev);
	}
	/* Resume clock after posting */
	r420_clock_resume(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);

	rdev->accel_working = true;
	r = r420_startup(rdev);
	if (r) {
		rdev->accel_working = false;
	}
	return r;
}

int r420_suspend(struct radeon_device *rdev)
{
	radeon_pm_suspend(rdev);
	r420_cp_errata_fini(rdev);
	r100_cp_disable(rdev);
	radeon_wb_disable(rdev);
	r100_irq_disable(rdev);
	if (rdev->flags & RADEON_IS_PCIE)
		rv370_pcie_gart_disable(rdev);
	if (rdev->flags & RADEON_IS_PCI)
		r100_pci_gart_disable(rdev);
	return 0;
}

void r420_fini(struct radeon_device *rdev)
{
	radeon_pm_fini(rdev);
	r100_cp_fini(rdev);
	radeon_wb_fini(rdev);
	radeon_ib_pool_fini(rdev);
	radeon_gem_fini(rdev);
	if (rdev->flags & RADEON_IS_PCIE)
		rv370_pcie_gart_fini(rdev);
	if (rdev->flags & RADEON_IS_PCI)
		r100_pci_gart_fini(rdev);
	radeon_agp_fini(rdev);
	radeon_irq_kms_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_bo_fini(rdev);
	if (rdev->is_atom_bios) {
		radeon_atombios_fini(rdev);
	} else {
		radeon_combios_fini(rdev);
	}
	kfree(rdev->bios);
	rdev->bios = NULL;
}

int r420_init(struct radeon_device *rdev)
{
	int r;

	/* Initialize scratch registers */
	radeon_scratch_init(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	/* TODO: disable VGA need to use VGA request */
	/* restore some register to sane defaults */
	r100_restore_sanity(rdev);
	/* BIOS*/
	if (!radeon_get_bios(rdev)) {
		if (ASIC_IS_AVIVO(rdev))
			return -EINVAL;
	}
	if (rdev->is_atom_bios) {
		r = radeon_atombios_init(rdev);
		if (r) {
			return r;
		}
	} else {
		r = radeon_combios_init(rdev);
		if (r) {
			return r;
		}
	}
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_asic_reset(rdev)) {
		dev_warn(rdev->dev,
			"GPU reset failed ! (0xE40=0x%08X, 0x7C0=0x%08X)\n",
			RREG32(R_000E40_RBBM_STATUS),
			RREG32(R_0007C0_CP_STAT));
	}
	/* check if cards are posted or not */
	if (radeon_boot_test_post_card(rdev) == false)
		return -EINVAL;

	/* Initialize clocks */
	radeon_get_clock_info(rdev->ddev);
	/* initialize AGP */
	if (rdev->flags & RADEON_IS_AGP) {
		r = radeon_agp_init(rdev);
		if (r) {
			radeon_agp_disable(rdev);
		}
	}
	/* initialize memory controller */
	r300_mc_init(rdev);
	r420_debugfs(rdev);
	/* Fence driver */
	radeon_fence_driver_init(rdev);
	/* Memory manager */
	r = radeon_bo_init(rdev);
	if (r) {
		return r;
	}
	if (rdev->family == CHIP_R420)
		r100_enable_bm(rdev);

	if (rdev->flags & RADEON_IS_PCIE) {
		r = rv370_pcie_gart_init(rdev);
		if (r)
			return r;
	}
	if (rdev->flags & RADEON_IS_PCI) {
		r = r100_pci_gart_init(rdev);
		if (r)
			return r;
	}
	r420_set_reg_safe(rdev);

	/* Initialize power management */
	radeon_pm_init(rdev);

	rdev->accel_working = true;
	r = r420_startup(rdev);
	if (r) {
		/* Somethings want wront with the accel init stop accel */
		dev_err(rdev->dev, "Disabling GPU acceleration\n");
		r100_cp_fini(rdev);
		radeon_wb_fini(rdev);
		radeon_ib_pool_fini(rdev);
		radeon_irq_kms_fini(rdev);
		if (rdev->flags & RADEON_IS_PCIE)
			rv370_pcie_gart_fini(rdev);
		if (rdev->flags & RADEON_IS_PCI)
			r100_pci_gart_fini(rdev);
		radeon_agp_fini(rdev);
		rdev->accel_working = false;
	}
	return 0;
}

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)
static int r420_debugfs_pipes_info_show(struct seq_file *m, void *unused)
{
	struct radeon_device *rdev = m->private;
	uint32_t tmp;

	tmp = RREG32(R400_GB_PIPE_SELECT);
	seq_printf(m, "GB_PIPE_SELECT 0x%08x\n", tmp);
	tmp = RREG32(R300_GB_TILE_CONFIG);
	seq_printf(m, "GB_TILE_CONFIG 0x%08x\n", tmp);
	tmp = RREG32(R300_DST_PIPE_CONFIG);
	seq_printf(m, "DST_PIPE_CONFIG 0x%08x\n", tmp);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(r420_debugfs_pipes_info);
#endif

void r420_debugfs_pipes_info_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	struct dentry *root = rdev->ddev->primary->debugfs_root;

	debugfs_create_file("r420_pipes_info", 0444, root, rdev,
			    &r420_debugfs_pipes_info_fops);
#endif
}
