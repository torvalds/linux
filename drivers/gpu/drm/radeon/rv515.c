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
#include <linux/seq_file.h>
#include "drmP.h"
#include "rv515d.h"
#include "radeon.h"
#include "atom.h"
#include "rv515_reg_safe.h"

/* This files gather functions specifics to: rv515 */
int rv515_debugfs_pipes_info_init(struct radeon_device *rdev);
int rv515_debugfs_ga_info_init(struct radeon_device *rdev);
void rv515_gpu_init(struct radeon_device *rdev);
int rv515_mc_wait_for_idle(struct radeon_device *rdev);

void rv515_debugfs(struct radeon_device *rdev)
{
	if (r100_debugfs_rbbm_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for RBBM !\n");
	}
	if (rv515_debugfs_pipes_info_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for pipes !\n");
	}
	if (rv515_debugfs_ga_info_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for pipes !\n");
	}
}

void rv515_ring_start(struct radeon_device *rdev)
{
	int r;

	r = radeon_ring_lock(rdev, 64);
	if (r) {
		return;
	}
	radeon_ring_write(rdev, PACKET0(ISYNC_CNTL, 0));
	radeon_ring_write(rdev,
			  ISYNC_ANY2D_IDLE3D |
			  ISYNC_ANY3D_IDLE2D |
			  ISYNC_WAIT_IDLEGUI |
			  ISYNC_CPSCRATCH_IDLEGUI);
	radeon_ring_write(rdev, PACKET0(WAIT_UNTIL, 0));
	radeon_ring_write(rdev, WAIT_2D_IDLECLEAN | WAIT_3D_IDLECLEAN);
	radeon_ring_write(rdev, PACKET0(0x170C, 0));
	radeon_ring_write(rdev, 1 << 31);
	radeon_ring_write(rdev, PACKET0(GB_SELECT, 0));
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, PACKET0(GB_ENABLE, 0));
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, PACKET0(0x42C8, 0));
	radeon_ring_write(rdev, (1 << rdev->num_gb_pipes) - 1);
	radeon_ring_write(rdev, PACKET0(VAP_INDEX_OFFSET, 0));
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, PACKET0(RB3D_DSTCACHE_CTLSTAT, 0));
	radeon_ring_write(rdev, RB3D_DC_FLUSH | RB3D_DC_FREE);
	radeon_ring_write(rdev, PACKET0(ZB_ZCACHE_CTLSTAT, 0));
	radeon_ring_write(rdev, ZC_FLUSH | ZC_FREE);
	radeon_ring_write(rdev, PACKET0(WAIT_UNTIL, 0));
	radeon_ring_write(rdev, WAIT_2D_IDLECLEAN | WAIT_3D_IDLECLEAN);
	radeon_ring_write(rdev, PACKET0(GB_AA_CONFIG, 0));
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, PACKET0(RB3D_DSTCACHE_CTLSTAT, 0));
	radeon_ring_write(rdev, RB3D_DC_FLUSH | RB3D_DC_FREE);
	radeon_ring_write(rdev, PACKET0(ZB_ZCACHE_CTLSTAT, 0));
	radeon_ring_write(rdev, ZC_FLUSH | ZC_FREE);
	radeon_ring_write(rdev, PACKET0(GB_MSPOS0, 0));
	radeon_ring_write(rdev,
			  ((6 << MS_X0_SHIFT) |
			   (6 << MS_Y0_SHIFT) |
			   (6 << MS_X1_SHIFT) |
			   (6 << MS_Y1_SHIFT) |
			   (6 << MS_X2_SHIFT) |
			   (6 << MS_Y2_SHIFT) |
			   (6 << MSBD0_Y_SHIFT) |
			   (6 << MSBD0_X_SHIFT)));
	radeon_ring_write(rdev, PACKET0(GB_MSPOS1, 0));
	radeon_ring_write(rdev,
			  ((6 << MS_X3_SHIFT) |
			   (6 << MS_Y3_SHIFT) |
			   (6 << MS_X4_SHIFT) |
			   (6 << MS_Y4_SHIFT) |
			   (6 << MS_X5_SHIFT) |
			   (6 << MS_Y5_SHIFT) |
			   (6 << MSBD1_SHIFT)));
	radeon_ring_write(rdev, PACKET0(GA_ENHANCE, 0));
	radeon_ring_write(rdev, GA_DEADLOCK_CNTL | GA_FASTSYNC_CNTL);
	radeon_ring_write(rdev, PACKET0(GA_POLY_MODE, 0));
	radeon_ring_write(rdev, FRONT_PTYPE_TRIANGE | BACK_PTYPE_TRIANGE);
	radeon_ring_write(rdev, PACKET0(GA_ROUND_MODE, 0));
	radeon_ring_write(rdev, GEOMETRY_ROUND_NEAREST | COLOR_ROUND_NEAREST);
	radeon_ring_write(rdev, PACKET0(0x20C8, 0));
	radeon_ring_write(rdev, 0);
	radeon_ring_unlock_commit(rdev);
}

int rv515_mc_wait_for_idle(struct radeon_device *rdev)
{
	unsigned i;
	uint32_t tmp;

	for (i = 0; i < rdev->usec_timeout; i++) {
		/* read MC_STATUS */
		tmp = RREG32_MC(MC_STATUS);
		if (tmp & MC_STATUS_IDLE) {
			return 0;
		}
		DRM_UDELAY(1);
	}
	return -1;
}

void rv515_vga_render_disable(struct radeon_device *rdev)
{
	WREG32(R_000330_D1VGA_CONTROL, 0);
	WREG32(R_000338_D2VGA_CONTROL, 0);
	WREG32(R_000300_VGA_RENDER_CONTROL,
		RREG32(R_000300_VGA_RENDER_CONTROL) & C_000300_VGA_VSTATUS_CNTL);
}

void rv515_gpu_init(struct radeon_device *rdev)
{
	unsigned pipe_select_current, gb_pipe_select, tmp;

	r100_hdp_reset(rdev);
	r100_rb2d_reset(rdev);

	if (r100_gui_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait GUI idle while "
		       "reseting GPU. Bad things might happen.\n");
	}

	rv515_vga_render_disable(rdev);

	r420_pipes_init(rdev);
	gb_pipe_select = RREG32(0x402C);
	tmp = RREG32(0x170C);
	pipe_select_current = (tmp >> 2) & 3;
	tmp = (1 << pipe_select_current) |
	      (((gb_pipe_select >> 8) & 0xF) << 4);
	WREG32_PLL(0x000D, tmp);
	if (r100_gui_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait GUI idle while "
		       "reseting GPU. Bad things might happen.\n");
	}
	if (rv515_mc_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait MC idle while "
		       "programming pipes. Bad things might happen.\n");
	}
}

int rv515_ga_reset(struct radeon_device *rdev)
{
	uint32_t tmp;
	bool reinit_cp;
	int i;

	reinit_cp = rdev->cp.ready;
	rdev->cp.ready = false;
	for (i = 0; i < rdev->usec_timeout; i++) {
		WREG32(CP_CSQ_MODE, 0);
		WREG32(CP_CSQ_CNTL, 0);
		WREG32(RBBM_SOFT_RESET, 0x32005);
		(void)RREG32(RBBM_SOFT_RESET);
		udelay(200);
		WREG32(RBBM_SOFT_RESET, 0);
		/* Wait to prevent race in RBBM_STATUS */
		mdelay(1);
		tmp = RREG32(RBBM_STATUS);
		if (tmp & ((1 << 20) | (1 << 26))) {
			DRM_ERROR("VAP & CP still busy (RBBM_STATUS=0x%08X)\n", tmp);
			/* GA still busy soft reset it */
			WREG32(0x429C, 0x200);
			WREG32(VAP_PVS_STATE_FLUSH_REG, 0);
			WREG32(0x43E0, 0);
			WREG32(0x43E4, 0);
			WREG32(0x24AC, 0);
		}
		/* Wait to prevent race in RBBM_STATUS */
		mdelay(1);
		tmp = RREG32(RBBM_STATUS);
		if (!(tmp & ((1 << 20) | (1 << 26)))) {
			break;
		}
	}
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(RBBM_STATUS);
		if (!(tmp & ((1 << 20) | (1 << 26)))) {
			DRM_INFO("GA reset succeed (RBBM_STATUS=0x%08X)\n",
				 tmp);
			DRM_INFO("GA_IDLE=0x%08X\n", RREG32(0x425C));
			DRM_INFO("RB3D_RESET_STATUS=0x%08X\n", RREG32(0x46f0));
			DRM_INFO("ISYNC_CNTL=0x%08X\n", RREG32(0x1724));
			if (reinit_cp) {
				return r100_cp_init(rdev, rdev->cp.ring_size);
			}
			return 0;
		}
		DRM_UDELAY(1);
	}
	tmp = RREG32(RBBM_STATUS);
	DRM_ERROR("Failed to reset GA ! (RBBM_STATUS=0x%08X)\n", tmp);
	return -1;
}

int rv515_gpu_reset(struct radeon_device *rdev)
{
	uint32_t status;

	/* reset order likely matter */
	status = RREG32(RBBM_STATUS);
	/* reset HDP */
	r100_hdp_reset(rdev);
	/* reset rb2d */
	if (status & ((1 << 17) | (1 << 18) | (1 << 27))) {
		r100_rb2d_reset(rdev);
	}
	/* reset GA */
	if (status & ((1 << 20) | (1 << 26))) {
		rv515_ga_reset(rdev);
	}
	/* reset CP */
	status = RREG32(RBBM_STATUS);
	if (status & (1 << 16)) {
		r100_cp_reset(rdev);
	}
	/* Check if GPU is idle */
	status = RREG32(RBBM_STATUS);
	if (status & (1 << 31)) {
		DRM_ERROR("Failed to reset GPU (RBBM_STATUS=0x%08X)\n", status);
		return -1;
	}
	DRM_INFO("GPU reset succeed (RBBM_STATUS=0x%08X)\n", status);
	return 0;
}

static void rv515_vram_get_type(struct radeon_device *rdev)
{
	uint32_t tmp;

	rdev->mc.vram_width = 128;
	rdev->mc.vram_is_ddr = true;
	tmp = RREG32_MC(RV515_MC_CNTL) & MEM_NUM_CHANNELS_MASK;
	switch (tmp) {
	case 0:
		rdev->mc.vram_width = 64;
		break;
	case 1:
		rdev->mc.vram_width = 128;
		break;
	default:
		rdev->mc.vram_width = 128;
		break;
	}
}

void rv515_vram_info(struct radeon_device *rdev)
{
	fixed20_12 a;

	rv515_vram_get_type(rdev);

	r100_vram_init_sizes(rdev);
	/* FIXME: we should enforce default clock in case GPU is not in
	 * default setup
	 */
	a.full = rfixed_const(100);
	rdev->pm.sclk.full = rfixed_const(rdev->clock.default_sclk);
	rdev->pm.sclk.full = rfixed_div(rdev->pm.sclk, a);
}

uint32_t rv515_mc_rreg(struct radeon_device *rdev, uint32_t reg)
{
	uint32_t r;

	WREG32(MC_IND_INDEX, 0x7f0000 | (reg & 0xffff));
	r = RREG32(MC_IND_DATA);
	WREG32(MC_IND_INDEX, 0);
	return r;
}

void rv515_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	WREG32(MC_IND_INDEX, 0xff0000 | ((reg) & 0xffff));
	WREG32(MC_IND_DATA, (v));
	WREG32(MC_IND_INDEX, 0);
}

#if defined(CONFIG_DEBUG_FS)
static int rv515_debugfs_pipes_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t tmp;

	tmp = RREG32(GB_PIPE_SELECT);
	seq_printf(m, "GB_PIPE_SELECT 0x%08x\n", tmp);
	tmp = RREG32(SU_REG_DEST);
	seq_printf(m, "SU_REG_DEST 0x%08x\n", tmp);
	tmp = RREG32(GB_TILE_CONFIG);
	seq_printf(m, "GB_TILE_CONFIG 0x%08x\n", tmp);
	tmp = RREG32(DST_PIPE_CONFIG);
	seq_printf(m, "DST_PIPE_CONFIG 0x%08x\n", tmp);
	return 0;
}

static int rv515_debugfs_ga_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t tmp;

	tmp = RREG32(0x2140);
	seq_printf(m, "VAP_CNTL_STATUS 0x%08x\n", tmp);
	radeon_gpu_reset(rdev);
	tmp = RREG32(0x425C);
	seq_printf(m, "GA_IDLE 0x%08x\n", tmp);
	return 0;
}

static struct drm_info_list rv515_pipes_info_list[] = {
	{"rv515_pipes_info", rv515_debugfs_pipes_info, 0, NULL},
};

static struct drm_info_list rv515_ga_info_list[] = {
	{"rv515_ga_info", rv515_debugfs_ga_info, 0, NULL},
};
#endif

int rv515_debugfs_pipes_info_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, rv515_pipes_info_list, 1);
#else
	return 0;
#endif
}

int rv515_debugfs_ga_info_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, rv515_ga_info_list, 1);
#else
	return 0;
#endif
}

void rv515_mc_stop(struct radeon_device *rdev, struct rv515_mc_save *save)
{
	save->d1vga_control = RREG32(R_000330_D1VGA_CONTROL);
	save->d2vga_control = RREG32(R_000338_D2VGA_CONTROL);
	save->vga_render_control = RREG32(R_000300_VGA_RENDER_CONTROL);
	save->vga_hdp_control = RREG32(R_000328_VGA_HDP_CONTROL);
	save->d1crtc_control = RREG32(R_006080_D1CRTC_CONTROL);
	save->d2crtc_control = RREG32(R_006880_D2CRTC_CONTROL);

	/* Stop all video */
	WREG32(R_000330_D1VGA_CONTROL, 0);
	WREG32(R_0068E8_D2CRTC_UPDATE_LOCK, 0);
	WREG32(R_000300_VGA_RENDER_CONTROL, 0);
	WREG32(R_0060E8_D1CRTC_UPDATE_LOCK, 1);
	WREG32(R_0068E8_D2CRTC_UPDATE_LOCK, 1);
	WREG32(R_006080_D1CRTC_CONTROL, 0);
	WREG32(R_006880_D2CRTC_CONTROL, 0);
	WREG32(R_0060E8_D1CRTC_UPDATE_LOCK, 0);
	WREG32(R_0068E8_D2CRTC_UPDATE_LOCK, 0);
}

void rv515_mc_resume(struct radeon_device *rdev, struct rv515_mc_save *save)
{
	WREG32(R_006110_D1GRPH_PRIMARY_SURFACE_ADDRESS, rdev->mc.vram_start);
	WREG32(R_006118_D1GRPH_SECONDARY_SURFACE_ADDRESS, rdev->mc.vram_start);
	WREG32(R_006910_D2GRPH_PRIMARY_SURFACE_ADDRESS, rdev->mc.vram_start);
	WREG32(R_006918_D2GRPH_SECONDARY_SURFACE_ADDRESS, rdev->mc.vram_start);
	WREG32(R_000310_VGA_MEMORY_BASE_ADDRESS, rdev->mc.vram_start);
	/* Unlock host access */
	WREG32(R_000328_VGA_HDP_CONTROL, save->vga_hdp_control);
	mdelay(1);
	/* Restore video state */
	WREG32(R_0060E8_D1CRTC_UPDATE_LOCK, 1);
	WREG32(R_0068E8_D2CRTC_UPDATE_LOCK, 1);
	WREG32(R_006080_D1CRTC_CONTROL, save->d1crtc_control);
	WREG32(R_006880_D2CRTC_CONTROL, save->d2crtc_control);
	WREG32(R_0060E8_D1CRTC_UPDATE_LOCK, 0);
	WREG32(R_0068E8_D2CRTC_UPDATE_LOCK, 0);
	WREG32(R_000330_D1VGA_CONTROL, save->d1vga_control);
	WREG32(R_000338_D2VGA_CONTROL, save->d2vga_control);
	WREG32(R_000300_VGA_RENDER_CONTROL, save->vga_render_control);
}

void rv515_mc_program(struct radeon_device *rdev)
{
	struct rv515_mc_save save;

	/* Stops all mc clients */
	rv515_mc_stop(rdev, &save);

	/* Wait for mc idle */
	if (rv515_mc_wait_for_idle(rdev))
		dev_warn(rdev->dev, "Wait MC idle timeout before updating MC.\n");
	/* Write VRAM size in case we are limiting it */
	WREG32(R_0000F8_CONFIG_MEMSIZE, rdev->mc.real_vram_size);
	/* Program MC, should be a 32bits limited address space */
	WREG32_MC(R_000001_MC_FB_LOCATION,
			S_000001_MC_FB_START(rdev->mc.vram_start >> 16) |
			S_000001_MC_FB_TOP(rdev->mc.vram_end >> 16));
	WREG32(R_000134_HDP_FB_LOCATION,
		S_000134_HDP_FB_START(rdev->mc.vram_start >> 16));
	if (rdev->flags & RADEON_IS_AGP) {
		WREG32_MC(R_000002_MC_AGP_LOCATION,
			S_000002_MC_AGP_START(rdev->mc.gtt_start >> 16) |
			S_000002_MC_AGP_TOP(rdev->mc.gtt_end >> 16));
		WREG32_MC(R_000003_MC_AGP_BASE, lower_32_bits(rdev->mc.agp_base));
		WREG32_MC(R_000004_MC_AGP_BASE_2,
			S_000004_AGP_BASE_ADDR_2(upper_32_bits(rdev->mc.agp_base)));
	} else {
		WREG32_MC(R_000002_MC_AGP_LOCATION, 0xFFFFFFFF);
		WREG32_MC(R_000003_MC_AGP_BASE, 0);
		WREG32_MC(R_000004_MC_AGP_BASE_2, 0);
	}

	rv515_mc_resume(rdev, &save);
}

void rv515_clock_startup(struct radeon_device *rdev)
{
	if (radeon_dynclks != -1 && radeon_dynclks)
		radeon_atom_set_clock_gating(rdev, 1);
	/* We need to force on some of the block */
	WREG32_PLL(R_00000F_CP_DYN_CNTL,
		RREG32_PLL(R_00000F_CP_DYN_CNTL) | S_00000F_CP_FORCEON(1));
	WREG32_PLL(R_000011_E2_DYN_CNTL,
		RREG32_PLL(R_000011_E2_DYN_CNTL) | S_000011_E2_FORCEON(1));
	WREG32_PLL(R_000013_IDCT_DYN_CNTL,
		RREG32_PLL(R_000013_IDCT_DYN_CNTL) | S_000013_IDCT_FORCEON(1));
}

static int rv515_startup(struct radeon_device *rdev)
{
	int r;

	rv515_mc_program(rdev);
	/* Resume clock */
	rv515_clock_startup(rdev);
	/* Initialize GPU configuration (# pipes, ...) */
	rv515_gpu_init(rdev);
	/* Initialize GART (initialize after TTM so we can allocate
	 * memory through TTM but finalize after TTM) */
	if (rdev->flags & RADEON_IS_PCIE) {
		r = rv370_pcie_gart_enable(rdev);
		if (r)
			return r;
	}
	/* Enable IRQ */
	rdev->irq.sw_int = true;
	rs600_irq_set(rdev);
	/* 1M ring buffer */
	r = r100_cp_init(rdev, 1024 * 1024);
	if (r) {
		dev_err(rdev->dev, "failled initializing CP (%d).\n", r);
		return r;
	}
	r = r100_wb_init(rdev);
	if (r)
		dev_err(rdev->dev, "failled initializing WB (%d).\n", r);
	r = r100_ib_init(rdev);
	if (r) {
		dev_err(rdev->dev, "failled initializing IB (%d).\n", r);
		return r;
	}
	return 0;
}

int rv515_resume(struct radeon_device *rdev)
{
	/* Make sur GART are not working */
	if (rdev->flags & RADEON_IS_PCIE)
		rv370_pcie_gart_disable(rdev);
	/* Resume clock before doing reset */
	rv515_clock_startup(rdev);
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_gpu_reset(rdev)) {
		dev_warn(rdev->dev, "GPU reset failed ! (0xE40=0x%08X, 0x7C0=0x%08X)\n",
			RREG32(R_000E40_RBBM_STATUS),
			RREG32(R_0007C0_CP_STAT));
	}
	/* post */
	atom_asic_init(rdev->mode_info.atom_context);
	/* Resume clock after posting */
	rv515_clock_startup(rdev);
	return rv515_startup(rdev);
}

int rv515_suspend(struct radeon_device *rdev)
{
	r100_cp_disable(rdev);
	r100_wb_disable(rdev);
	rs600_irq_disable(rdev);
	if (rdev->flags & RADEON_IS_PCIE)
		rv370_pcie_gart_disable(rdev);
	return 0;
}

void rv515_set_safe_registers(struct radeon_device *rdev)
{
	rdev->config.r300.reg_safe_bm = rv515_reg_safe_bm;
	rdev->config.r300.reg_safe_bm_size = ARRAY_SIZE(rv515_reg_safe_bm);
}

void rv515_fini(struct radeon_device *rdev)
{
	rv515_suspend(rdev);
	r100_cp_fini(rdev);
	r100_wb_fini(rdev);
	r100_ib_fini(rdev);
	radeon_gem_fini(rdev);
    rv370_pcie_gart_fini(rdev);
	radeon_agp_fini(rdev);
	radeon_irq_kms_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_object_fini(rdev);
	radeon_atombios_fini(rdev);
	kfree(rdev->bios);
	rdev->bios = NULL;
}

int rv515_init(struct radeon_device *rdev)
{
	int r;

	/* Initialize scratch registers */
	radeon_scratch_init(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	/* TODO: disable VGA need to use VGA request */
	/* BIOS*/
	if (!radeon_get_bios(rdev)) {
		if (ASIC_IS_AVIVO(rdev))
			return -EINVAL;
	}
	if (rdev->is_atom_bios) {
		r = radeon_atombios_init(rdev);
		if (r)
			return r;
	} else {
		dev_err(rdev->dev, "Expecting atombios for RV515 GPU\n");
		return -EINVAL;
	}
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_gpu_reset(rdev)) {
		dev_warn(rdev->dev,
			"GPU reset failed ! (0xE40=0x%08X, 0x7C0=0x%08X)\n",
			RREG32(R_000E40_RBBM_STATUS),
			RREG32(R_0007C0_CP_STAT));
	}
	/* check if cards are posted or not */
	if (!radeon_card_posted(rdev) && rdev->bios) {
		DRM_INFO("GPU not posted. posting now...\n");
		atom_asic_init(rdev->mode_info.atom_context);
	}
	/* Initialize clocks */
	radeon_get_clock_info(rdev->ddev);
	/* Initialize power management */
	radeon_pm_init(rdev);
	/* Get vram informations */
	rv515_vram_info(rdev);
	/* Initialize memory controller (also test AGP) */
	r = r420_mc_init(rdev);
	if (r)
		return r;
	rv515_debugfs(rdev);
	/* Fence driver */
	r = radeon_fence_driver_init(rdev);
	if (r)
		return r;
	r = radeon_irq_kms_init(rdev);
	if (r)
		return r;
	/* Memory manager */
	r = radeon_object_init(rdev);
	if (r)
		return r;
	r = rv370_pcie_gart_init(rdev);
	if (r)
		return r;
	rv515_set_safe_registers(rdev);
	rdev->accel_working = true;
	r = rv515_startup(rdev);
	if (r) {
		/* Somethings want wront with the accel init stop accel */
		dev_err(rdev->dev, "Disabling GPU acceleration\n");
		rv515_suspend(rdev);
		r100_cp_fini(rdev);
		r100_wb_fini(rdev);
		r100_ib_fini(rdev);
		rv370_pcie_gart_fini(rdev);
		radeon_agp_fini(rdev);
		radeon_irq_kms_fini(rdev);
		rdev->accel_working = false;
	}
	return 0;
}

void atom_rv515_force_tv_scaler(struct radeon_device *rdev, struct radeon_crtc *crtc)
{
	int index_reg = 0x6578 + crtc->crtc_offset;
	int data_reg = 0x657c + crtc->crtc_offset;

	WREG32(0x659C + crtc->crtc_offset, 0x0);
	WREG32(0x6594 + crtc->crtc_offset, 0x705);
	WREG32(0x65A4 + crtc->crtc_offset, 0x10001);
	WREG32(0x65D8 + crtc->crtc_offset, 0x0);
	WREG32(0x65B0 + crtc->crtc_offset, 0x0);
	WREG32(0x65C0 + crtc->crtc_offset, 0x0);
	WREG32(0x65D4 + crtc->crtc_offset, 0x0);
	WREG32(index_reg, 0x0);
	WREG32(data_reg, 0x841880A8);
	WREG32(index_reg, 0x1);
	WREG32(data_reg, 0x84208680);
	WREG32(index_reg, 0x2);
	WREG32(data_reg, 0xBFF880B0);
	WREG32(index_reg, 0x100);
	WREG32(data_reg, 0x83D88088);
	WREG32(index_reg, 0x101);
	WREG32(data_reg, 0x84608680);
	WREG32(index_reg, 0x102);
	WREG32(data_reg, 0xBFF080D0);
	WREG32(index_reg, 0x200);
	WREG32(data_reg, 0x83988068);
	WREG32(index_reg, 0x201);
	WREG32(data_reg, 0x84A08680);
	WREG32(index_reg, 0x202);
	WREG32(data_reg, 0xBFF080F8);
	WREG32(index_reg, 0x300);
	WREG32(data_reg, 0x83588058);
	WREG32(index_reg, 0x301);
	WREG32(data_reg, 0x84E08660);
	WREG32(index_reg, 0x302);
	WREG32(data_reg, 0xBFF88120);
	WREG32(index_reg, 0x400);
	WREG32(data_reg, 0x83188040);
	WREG32(index_reg, 0x401);
	WREG32(data_reg, 0x85008660);
	WREG32(index_reg, 0x402);
	WREG32(data_reg, 0xBFF88150);
	WREG32(index_reg, 0x500);
	WREG32(data_reg, 0x82D88030);
	WREG32(index_reg, 0x501);
	WREG32(data_reg, 0x85408640);
	WREG32(index_reg, 0x502);
	WREG32(data_reg, 0xBFF88180);
	WREG32(index_reg, 0x600);
	WREG32(data_reg, 0x82A08018);
	WREG32(index_reg, 0x601);
	WREG32(data_reg, 0x85808620);
	WREG32(index_reg, 0x602);
	WREG32(data_reg, 0xBFF081B8);
	WREG32(index_reg, 0x700);
	WREG32(data_reg, 0x82608010);
	WREG32(index_reg, 0x701);
	WREG32(data_reg, 0x85A08600);
	WREG32(index_reg, 0x702);
	WREG32(data_reg, 0x800081F0);
	WREG32(index_reg, 0x800);
	WREG32(data_reg, 0x8228BFF8);
	WREG32(index_reg, 0x801);
	WREG32(data_reg, 0x85E085E0);
	WREG32(index_reg, 0x802);
	WREG32(data_reg, 0xBFF88228);
	WREG32(index_reg, 0x10000);
	WREG32(data_reg, 0x82A8BF00);
	WREG32(index_reg, 0x10001);
	WREG32(data_reg, 0x82A08CC0);
	WREG32(index_reg, 0x10002);
	WREG32(data_reg, 0x8008BEF8);
	WREG32(index_reg, 0x10100);
	WREG32(data_reg, 0x81F0BF28);
	WREG32(index_reg, 0x10101);
	WREG32(data_reg, 0x83608CA0);
	WREG32(index_reg, 0x10102);
	WREG32(data_reg, 0x8018BED0);
	WREG32(index_reg, 0x10200);
	WREG32(data_reg, 0x8148BF38);
	WREG32(index_reg, 0x10201);
	WREG32(data_reg, 0x84408C80);
	WREG32(index_reg, 0x10202);
	WREG32(data_reg, 0x8008BEB8);
	WREG32(index_reg, 0x10300);
	WREG32(data_reg, 0x80B0BF78);
	WREG32(index_reg, 0x10301);
	WREG32(data_reg, 0x85008C20);
	WREG32(index_reg, 0x10302);
	WREG32(data_reg, 0x8020BEA0);
	WREG32(index_reg, 0x10400);
	WREG32(data_reg, 0x8028BF90);
	WREG32(index_reg, 0x10401);
	WREG32(data_reg, 0x85E08BC0);
	WREG32(index_reg, 0x10402);
	WREG32(data_reg, 0x8018BE90);
	WREG32(index_reg, 0x10500);
	WREG32(data_reg, 0xBFB8BFB0);
	WREG32(index_reg, 0x10501);
	WREG32(data_reg, 0x86C08B40);
	WREG32(index_reg, 0x10502);
	WREG32(data_reg, 0x8010BE90);
	WREG32(index_reg, 0x10600);
	WREG32(data_reg, 0xBF58BFC8);
	WREG32(index_reg, 0x10601);
	WREG32(data_reg, 0x87A08AA0);
	WREG32(index_reg, 0x10602);
	WREG32(data_reg, 0x8010BE98);
	WREG32(index_reg, 0x10700);
	WREG32(data_reg, 0xBF10BFF0);
	WREG32(index_reg, 0x10701);
	WREG32(data_reg, 0x886089E0);
	WREG32(index_reg, 0x10702);
	WREG32(data_reg, 0x8018BEB0);
	WREG32(index_reg, 0x10800);
	WREG32(data_reg, 0xBED8BFE8);
	WREG32(index_reg, 0x10801);
	WREG32(data_reg, 0x89408940);
	WREG32(index_reg, 0x10802);
	WREG32(data_reg, 0xBFE8BED8);
	WREG32(index_reg, 0x20000);
	WREG32(data_reg, 0x80008000);
	WREG32(index_reg, 0x20001);
	WREG32(data_reg, 0x90008000);
	WREG32(index_reg, 0x20002);
	WREG32(data_reg, 0x80008000);
	WREG32(index_reg, 0x20003);
	WREG32(data_reg, 0x80008000);
	WREG32(index_reg, 0x20100);
	WREG32(data_reg, 0x80108000);
	WREG32(index_reg, 0x20101);
	WREG32(data_reg, 0x8FE0BF70);
	WREG32(index_reg, 0x20102);
	WREG32(data_reg, 0xBFE880C0);
	WREG32(index_reg, 0x20103);
	WREG32(data_reg, 0x80008000);
	WREG32(index_reg, 0x20200);
	WREG32(data_reg, 0x8018BFF8);
	WREG32(index_reg, 0x20201);
	WREG32(data_reg, 0x8F80BF08);
	WREG32(index_reg, 0x20202);
	WREG32(data_reg, 0xBFD081A0);
	WREG32(index_reg, 0x20203);
	WREG32(data_reg, 0xBFF88000);
	WREG32(index_reg, 0x20300);
	WREG32(data_reg, 0x80188000);
	WREG32(index_reg, 0x20301);
	WREG32(data_reg, 0x8EE0BEC0);
	WREG32(index_reg, 0x20302);
	WREG32(data_reg, 0xBFB082A0);
	WREG32(index_reg, 0x20303);
	WREG32(data_reg, 0x80008000);
	WREG32(index_reg, 0x20400);
	WREG32(data_reg, 0x80188000);
	WREG32(index_reg, 0x20401);
	WREG32(data_reg, 0x8E00BEA0);
	WREG32(index_reg, 0x20402);
	WREG32(data_reg, 0xBF8883C0);
	WREG32(index_reg, 0x20403);
	WREG32(data_reg, 0x80008000);
	WREG32(index_reg, 0x20500);
	WREG32(data_reg, 0x80188000);
	WREG32(index_reg, 0x20501);
	WREG32(data_reg, 0x8D00BE90);
	WREG32(index_reg, 0x20502);
	WREG32(data_reg, 0xBF588500);
	WREG32(index_reg, 0x20503);
	WREG32(data_reg, 0x80008008);
	WREG32(index_reg, 0x20600);
	WREG32(data_reg, 0x80188000);
	WREG32(index_reg, 0x20601);
	WREG32(data_reg, 0x8BC0BE98);
	WREG32(index_reg, 0x20602);
	WREG32(data_reg, 0xBF308660);
	WREG32(index_reg, 0x20603);
	WREG32(data_reg, 0x80008008);
	WREG32(index_reg, 0x20700);
	WREG32(data_reg, 0x80108000);
	WREG32(index_reg, 0x20701);
	WREG32(data_reg, 0x8A80BEB0);
	WREG32(index_reg, 0x20702);
	WREG32(data_reg, 0xBF0087C0);
	WREG32(index_reg, 0x20703);
	WREG32(data_reg, 0x80008008);
	WREG32(index_reg, 0x20800);
	WREG32(data_reg, 0x80108000);
	WREG32(index_reg, 0x20801);
	WREG32(data_reg, 0x8920BED0);
	WREG32(index_reg, 0x20802);
	WREG32(data_reg, 0xBED08920);
	WREG32(index_reg, 0x20803);
	WREG32(data_reg, 0x80008010);
	WREG32(index_reg, 0x30000);
	WREG32(data_reg, 0x90008000);
	WREG32(index_reg, 0x30001);
	WREG32(data_reg, 0x80008000);
	WREG32(index_reg, 0x30100);
	WREG32(data_reg, 0x8FE0BF90);
	WREG32(index_reg, 0x30101);
	WREG32(data_reg, 0xBFF880A0);
	WREG32(index_reg, 0x30200);
	WREG32(data_reg, 0x8F60BF40);
	WREG32(index_reg, 0x30201);
	WREG32(data_reg, 0xBFE88180);
	WREG32(index_reg, 0x30300);
	WREG32(data_reg, 0x8EC0BF00);
	WREG32(index_reg, 0x30301);
	WREG32(data_reg, 0xBFC88280);
	WREG32(index_reg, 0x30400);
	WREG32(data_reg, 0x8DE0BEE0);
	WREG32(index_reg, 0x30401);
	WREG32(data_reg, 0xBFA083A0);
	WREG32(index_reg, 0x30500);
	WREG32(data_reg, 0x8CE0BED0);
	WREG32(index_reg, 0x30501);
	WREG32(data_reg, 0xBF7884E0);
	WREG32(index_reg, 0x30600);
	WREG32(data_reg, 0x8BA0BED8);
	WREG32(index_reg, 0x30601);
	WREG32(data_reg, 0xBF508640);
	WREG32(index_reg, 0x30700);
	WREG32(data_reg, 0x8A60BEE8);
	WREG32(index_reg, 0x30701);
	WREG32(data_reg, 0xBF2087A0);
	WREG32(index_reg, 0x30800);
	WREG32(data_reg, 0x8900BF00);
	WREG32(index_reg, 0x30801);
	WREG32(data_reg, 0xBF008900);
}

struct rv515_watermark {
	u32        lb_request_fifo_depth;
	fixed20_12 num_line_pair;
	fixed20_12 estimated_width;
	fixed20_12 worst_case_latency;
	fixed20_12 consumption_rate;
	fixed20_12 active_time;
	fixed20_12 dbpp;
	fixed20_12 priority_mark_max;
	fixed20_12 priority_mark;
	fixed20_12 sclk;
};

void rv515_crtc_bandwidth_compute(struct radeon_device *rdev,
				  struct radeon_crtc *crtc,
				  struct rv515_watermark *wm)
{
	struct drm_display_mode *mode = &crtc->base.mode;
	fixed20_12 a, b, c;
	fixed20_12 pclk, request_fifo_depth, tolerable_latency, estimated_width;
	fixed20_12 consumption_time, line_time, chunk_time, read_delay_latency;

	if (!crtc->base.enabled) {
		/* FIXME: wouldn't it better to set priority mark to maximum */
		wm->lb_request_fifo_depth = 4;
		return;
	}

	if (crtc->vsc.full > rfixed_const(2))
		wm->num_line_pair.full = rfixed_const(2);
	else
		wm->num_line_pair.full = rfixed_const(1);

	b.full = rfixed_const(mode->crtc_hdisplay);
	c.full = rfixed_const(256);
	a.full = rfixed_mul(wm->num_line_pair, b);
	request_fifo_depth.full = rfixed_div(a, c);
	if (a.full < rfixed_const(4)) {
		wm->lb_request_fifo_depth = 4;
	} else {
		wm->lb_request_fifo_depth = rfixed_trunc(request_fifo_depth);
	}

	/* Determine consumption rate
	 *  pclk = pixel clock period(ns) = 1000 / (mode.clock / 1000)
	 *  vtaps = number of vertical taps,
	 *  vsc = vertical scaling ratio, defined as source/destination
	 *  hsc = horizontal scaling ration, defined as source/destination
	 */
	a.full = rfixed_const(mode->clock);
	b.full = rfixed_const(1000);
	a.full = rfixed_div(a, b);
	pclk.full = rfixed_div(b, a);
	if (crtc->rmx_type != RMX_OFF) {
		b.full = rfixed_const(2);
		if (crtc->vsc.full > b.full)
			b.full = crtc->vsc.full;
		b.full = rfixed_mul(b, crtc->hsc);
		c.full = rfixed_const(2);
		b.full = rfixed_div(b, c);
		consumption_time.full = rfixed_div(pclk, b);
	} else {
		consumption_time.full = pclk.full;
	}
	a.full = rfixed_const(1);
	wm->consumption_rate.full = rfixed_div(a, consumption_time);


	/* Determine line time
	 *  LineTime = total time for one line of displayhtotal
	 *  LineTime = total number of horizontal pixels
	 *  pclk = pixel clock period(ns)
	 */
	a.full = rfixed_const(crtc->base.mode.crtc_htotal);
	line_time.full = rfixed_mul(a, pclk);

	/* Determine active time
	 *  ActiveTime = time of active region of display within one line,
	 *  hactive = total number of horizontal active pixels
	 *  htotal = total number of horizontal pixels
	 */
	a.full = rfixed_const(crtc->base.mode.crtc_htotal);
	b.full = rfixed_const(crtc->base.mode.crtc_hdisplay);
	wm->active_time.full = rfixed_mul(line_time, b);
	wm->active_time.full = rfixed_div(wm->active_time, a);

	/* Determine chunk time
	 * ChunkTime = the time it takes the DCP to send one chunk of data
	 * to the LB which consists of pipeline delay and inter chunk gap
	 * sclk = system clock(Mhz)
	 */
	a.full = rfixed_const(600 * 1000);
	chunk_time.full = rfixed_div(a, rdev->pm.sclk);
	read_delay_latency.full = rfixed_const(1000);

	/* Determine the worst case latency
	 * NumLinePair = Number of line pairs to request(1=2 lines, 2=4 lines)
	 * WorstCaseLatency = worst case time from urgent to when the MC starts
	 *                    to return data
	 * READ_DELAY_IDLE_MAX = constant of 1us
	 * ChunkTime = time it takes the DCP to send one chunk of data to the LB
	 *             which consists of pipeline delay and inter chunk gap
	 */
	if (rfixed_trunc(wm->num_line_pair) > 1) {
		a.full = rfixed_const(3);
		wm->worst_case_latency.full = rfixed_mul(a, chunk_time);
		wm->worst_case_latency.full += read_delay_latency.full;
	} else {
		wm->worst_case_latency.full = chunk_time.full + read_delay_latency.full;
	}

	/* Determine the tolerable latency
	 * TolerableLatency = Any given request has only 1 line time
	 *                    for the data to be returned
	 * LBRequestFifoDepth = Number of chunk requests the LB can
	 *                      put into the request FIFO for a display
	 *  LineTime = total time for one line of display
	 *  ChunkTime = the time it takes the DCP to send one chunk
	 *              of data to the LB which consists of
	 *  pipeline delay and inter chunk gap
	 */
	if ((2+wm->lb_request_fifo_depth) >= rfixed_trunc(request_fifo_depth)) {
		tolerable_latency.full = line_time.full;
	} else {
		tolerable_latency.full = rfixed_const(wm->lb_request_fifo_depth - 2);
		tolerable_latency.full = request_fifo_depth.full - tolerable_latency.full;
		tolerable_latency.full = rfixed_mul(tolerable_latency, chunk_time);
		tolerable_latency.full = line_time.full - tolerable_latency.full;
	}
	/* We assume worst case 32bits (4 bytes) */
	wm->dbpp.full = rfixed_const(2 * 16);

	/* Determine the maximum priority mark
	 *  width = viewport width in pixels
	 */
	a.full = rfixed_const(16);
	wm->priority_mark_max.full = rfixed_const(crtc->base.mode.crtc_hdisplay);
	wm->priority_mark_max.full = rfixed_div(wm->priority_mark_max, a);

	/* Determine estimated width */
	estimated_width.full = tolerable_latency.full - wm->worst_case_latency.full;
	estimated_width.full = rfixed_div(estimated_width, consumption_time);
	if (rfixed_trunc(estimated_width) > crtc->base.mode.crtc_hdisplay) {
		wm->priority_mark.full = rfixed_const(10);
	} else {
		a.full = rfixed_const(16);
		wm->priority_mark.full = rfixed_div(estimated_width, a);
		wm->priority_mark.full = wm->priority_mark_max.full - wm->priority_mark.full;
	}
}

void rv515_bandwidth_avivo_update(struct radeon_device *rdev)
{
	struct drm_display_mode *mode0 = NULL;
	struct drm_display_mode *mode1 = NULL;
	struct rv515_watermark wm0;
	struct rv515_watermark wm1;
	u32 tmp;
	fixed20_12 priority_mark02, priority_mark12, fill_rate;
	fixed20_12 a, b;

	if (rdev->mode_info.crtcs[0]->base.enabled)
		mode0 = &rdev->mode_info.crtcs[0]->base.mode;
	if (rdev->mode_info.crtcs[1]->base.enabled)
		mode1 = &rdev->mode_info.crtcs[1]->base.mode;
	rs690_line_buffer_adjust(rdev, mode0, mode1);

	rv515_crtc_bandwidth_compute(rdev, rdev->mode_info.crtcs[0], &wm0);
	rv515_crtc_bandwidth_compute(rdev, rdev->mode_info.crtcs[1], &wm1);

	tmp = wm0.lb_request_fifo_depth;
	tmp |= wm1.lb_request_fifo_depth << 16;
	WREG32(LB_MAX_REQ_OUTSTANDING, tmp);

	if (mode0 && mode1) {
		if (rfixed_trunc(wm0.dbpp) > 64)
			a.full = rfixed_div(wm0.dbpp, wm0.num_line_pair);
		else
			a.full = wm0.num_line_pair.full;
		if (rfixed_trunc(wm1.dbpp) > 64)
			b.full = rfixed_div(wm1.dbpp, wm1.num_line_pair);
		else
			b.full = wm1.num_line_pair.full;
		a.full += b.full;
		fill_rate.full = rfixed_div(wm0.sclk, a);
		if (wm0.consumption_rate.full > fill_rate.full) {
			b.full = wm0.consumption_rate.full - fill_rate.full;
			b.full = rfixed_mul(b, wm0.active_time);
			a.full = rfixed_const(16);
			b.full = rfixed_div(b, a);
			a.full = rfixed_mul(wm0.worst_case_latency,
						wm0.consumption_rate);
			priority_mark02.full = a.full + b.full;
		} else {
			a.full = rfixed_mul(wm0.worst_case_latency,
						wm0.consumption_rate);
			b.full = rfixed_const(16 * 1000);
			priority_mark02.full = rfixed_div(a, b);
		}
		if (wm1.consumption_rate.full > fill_rate.full) {
			b.full = wm1.consumption_rate.full - fill_rate.full;
			b.full = rfixed_mul(b, wm1.active_time);
			a.full = rfixed_const(16);
			b.full = rfixed_div(b, a);
			a.full = rfixed_mul(wm1.worst_case_latency,
						wm1.consumption_rate);
			priority_mark12.full = a.full + b.full;
		} else {
			a.full = rfixed_mul(wm1.worst_case_latency,
						wm1.consumption_rate);
			b.full = rfixed_const(16 * 1000);
			priority_mark12.full = rfixed_div(a, b);
		}
		if (wm0.priority_mark.full > priority_mark02.full)
			priority_mark02.full = wm0.priority_mark.full;
		if (rfixed_trunc(priority_mark02) < 0)
			priority_mark02.full = 0;
		if (wm0.priority_mark_max.full > priority_mark02.full)
			priority_mark02.full = wm0.priority_mark_max.full;
		if (wm1.priority_mark.full > priority_mark12.full)
			priority_mark12.full = wm1.priority_mark.full;
		if (rfixed_trunc(priority_mark12) < 0)
			priority_mark12.full = 0;
		if (wm1.priority_mark_max.full > priority_mark12.full)
			priority_mark12.full = wm1.priority_mark_max.full;
		WREG32(D1MODE_PRIORITY_A_CNT, rfixed_trunc(priority_mark02));
		WREG32(D1MODE_PRIORITY_B_CNT, rfixed_trunc(priority_mark02));
		WREG32(D2MODE_PRIORITY_A_CNT, rfixed_trunc(priority_mark12));
		WREG32(D2MODE_PRIORITY_B_CNT, rfixed_trunc(priority_mark12));
	} else if (mode0) {
		if (rfixed_trunc(wm0.dbpp) > 64)
			a.full = rfixed_div(wm0.dbpp, wm0.num_line_pair);
		else
			a.full = wm0.num_line_pair.full;
		fill_rate.full = rfixed_div(wm0.sclk, a);
		if (wm0.consumption_rate.full > fill_rate.full) {
			b.full = wm0.consumption_rate.full - fill_rate.full;
			b.full = rfixed_mul(b, wm0.active_time);
			a.full = rfixed_const(16);
			b.full = rfixed_div(b, a);
			a.full = rfixed_mul(wm0.worst_case_latency,
						wm0.consumption_rate);
			priority_mark02.full = a.full + b.full;
		} else {
			a.full = rfixed_mul(wm0.worst_case_latency,
						wm0.consumption_rate);
			b.full = rfixed_const(16);
			priority_mark02.full = rfixed_div(a, b);
		}
		if (wm0.priority_mark.full > priority_mark02.full)
			priority_mark02.full = wm0.priority_mark.full;
		if (rfixed_trunc(priority_mark02) < 0)
			priority_mark02.full = 0;
		if (wm0.priority_mark_max.full > priority_mark02.full)
			priority_mark02.full = wm0.priority_mark_max.full;
		WREG32(D1MODE_PRIORITY_A_CNT, rfixed_trunc(priority_mark02));
		WREG32(D1MODE_PRIORITY_B_CNT, rfixed_trunc(priority_mark02));
		WREG32(D2MODE_PRIORITY_A_CNT, MODE_PRIORITY_OFF);
		WREG32(D2MODE_PRIORITY_B_CNT, MODE_PRIORITY_OFF);
	} else {
		if (rfixed_trunc(wm1.dbpp) > 64)
			a.full = rfixed_div(wm1.dbpp, wm1.num_line_pair);
		else
			a.full = wm1.num_line_pair.full;
		fill_rate.full = rfixed_div(wm1.sclk, a);
		if (wm1.consumption_rate.full > fill_rate.full) {
			b.full = wm1.consumption_rate.full - fill_rate.full;
			b.full = rfixed_mul(b, wm1.active_time);
			a.full = rfixed_const(16);
			b.full = rfixed_div(b, a);
			a.full = rfixed_mul(wm1.worst_case_latency,
						wm1.consumption_rate);
			priority_mark12.full = a.full + b.full;
		} else {
			a.full = rfixed_mul(wm1.worst_case_latency,
						wm1.consumption_rate);
			b.full = rfixed_const(16 * 1000);
			priority_mark12.full = rfixed_div(a, b);
		}
		if (wm1.priority_mark.full > priority_mark12.full)
			priority_mark12.full = wm1.priority_mark.full;
		if (rfixed_trunc(priority_mark12) < 0)
			priority_mark12.full = 0;
		if (wm1.priority_mark_max.full > priority_mark12.full)
			priority_mark12.full = wm1.priority_mark_max.full;
		WREG32(D1MODE_PRIORITY_A_CNT, MODE_PRIORITY_OFF);
		WREG32(D1MODE_PRIORITY_B_CNT, MODE_PRIORITY_OFF);
		WREG32(D2MODE_PRIORITY_A_CNT, rfixed_trunc(priority_mark12));
		WREG32(D2MODE_PRIORITY_B_CNT, rfixed_trunc(priority_mark12));
	}
}

void rv515_bandwidth_update(struct radeon_device *rdev)
{
	uint32_t tmp;
	struct drm_display_mode *mode0 = NULL;
	struct drm_display_mode *mode1 = NULL;

	if (rdev->mode_info.crtcs[0]->base.enabled)
		mode0 = &rdev->mode_info.crtcs[0]->base.mode;
	if (rdev->mode_info.crtcs[1]->base.enabled)
		mode1 = &rdev->mode_info.crtcs[1]->base.mode;
	/*
	 * Set display0/1 priority up in the memory controller for
	 * modes if the user specifies HIGH for displaypriority
	 * option.
	 */
	if (rdev->disp_priority == 2) {
		tmp = RREG32_MC(MC_MISC_LAT_TIMER);
		tmp &= ~MC_DISP1R_INIT_LAT_MASK;
		tmp &= ~MC_DISP0R_INIT_LAT_MASK;
		if (mode1)
			tmp |= (1 << MC_DISP1R_INIT_LAT_SHIFT);
		if (mode0)
			tmp |= (1 << MC_DISP0R_INIT_LAT_SHIFT);
		WREG32_MC(MC_MISC_LAT_TIMER, tmp);
	}
	rv515_bandwidth_avivo_update(rdev);
}
