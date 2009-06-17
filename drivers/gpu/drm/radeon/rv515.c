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
#include "radeon_reg.h"
#include "radeon.h"

/* rv515 depends on : */
void r100_hdp_reset(struct radeon_device *rdev);
int r100_cp_reset(struct radeon_device *rdev);
int r100_rb2d_reset(struct radeon_device *rdev);
int r100_gui_wait_for_idle(struct radeon_device *rdev);
int r100_cp_init(struct radeon_device *rdev, unsigned ring_size);
int rv370_pcie_gart_enable(struct radeon_device *rdev);
void rv370_pcie_gart_disable(struct radeon_device *rdev);
void r420_pipes_init(struct radeon_device *rdev);
void rs600_mc_disable_clients(struct radeon_device *rdev);
void rs600_disable_vga(struct radeon_device *rdev);

/* This files gather functions specifics to:
 * rv515
 *
 * Some of these functions might be used by newer ASICs.
 */
int rv515_debugfs_pipes_info_init(struct radeon_device *rdev);
int rv515_debugfs_ga_info_init(struct radeon_device *rdev);
void rv515_gpu_init(struct radeon_device *rdev);
int rv515_mc_wait_for_idle(struct radeon_device *rdev);


/*
 * MC
 */
int rv515_mc_init(struct radeon_device *rdev)
{
	uint32_t tmp;
	int r;

	if (r100_debugfs_rbbm_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for RBBM !\n");
	}
	if (rv515_debugfs_pipes_info_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for pipes !\n");
	}
	if (rv515_debugfs_ga_info_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for pipes !\n");
	}

	rv515_gpu_init(rdev);
	rv370_pcie_gart_disable(rdev);

	/* Setup GPU memory space */
	rdev->mc.vram_location = 0xFFFFFFFFUL;
	rdev->mc.gtt_location = 0xFFFFFFFFUL;
	if (rdev->flags & RADEON_IS_AGP) {
		r = radeon_agp_init(rdev);
		if (r) {
			printk(KERN_WARNING "[drm] Disabling AGP\n");
			rdev->flags &= ~RADEON_IS_AGP;
			rdev->mc.gtt_size = radeon_gart_size * 1024 * 1024;
		} else {
			rdev->mc.gtt_location = rdev->mc.agp_base;
		}
	}
	r = radeon_mc_setup(rdev);
	if (r) {
		return r;
	}

	/* Program GPU memory space */
	rs600_mc_disable_clients(rdev);
	if (rv515_mc_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait MC idle while "
		       "programming pipes. Bad things might happen.\n");
	}
	/* Write VRAM size in case we are limiting it */
	WREG32(RADEON_CONFIG_MEMSIZE, rdev->mc.vram_size);
	tmp = REG_SET(RV515_MC_FB_START, rdev->mc.vram_location >> 16);
	WREG32(0x134, tmp);
	tmp = rdev->mc.vram_location + rdev->mc.vram_size - 1;
	tmp = REG_SET(RV515_MC_FB_TOP, tmp >> 16);
	tmp |= REG_SET(RV515_MC_FB_START, rdev->mc.vram_location >> 16);
	WREG32_MC(RV515_MC_FB_LOCATION, tmp);
	WREG32(RS690_HDP_FB_LOCATION, rdev->mc.vram_location >> 16);
	WREG32(0x310, rdev->mc.vram_location);
	if (rdev->flags & RADEON_IS_AGP) {
		tmp = rdev->mc.gtt_location + rdev->mc.gtt_size - 1;
		tmp = REG_SET(RV515_MC_AGP_TOP, tmp >> 16);
		tmp |= REG_SET(RV515_MC_AGP_START, rdev->mc.gtt_location >> 16);
		WREG32_MC(RV515_MC_AGP_LOCATION, tmp);
		WREG32_MC(RV515_MC_AGP_BASE, rdev->mc.agp_base);
		WREG32_MC(RV515_MC_AGP_BASE_2, 0);
	} else {
		WREG32_MC(RV515_MC_AGP_LOCATION, 0x0FFFFFFF);
		WREG32_MC(RV515_MC_AGP_BASE, 0);
		WREG32_MC(RV515_MC_AGP_BASE_2, 0);
	}
	return 0;
}

void rv515_mc_fini(struct radeon_device *rdev)
{
	rv370_pcie_gart_disable(rdev);
	radeon_gart_table_vram_free(rdev);
	radeon_gart_fini(rdev);
}


/*
 * Global GPU functions
 */
void rv515_ring_start(struct radeon_device *rdev)
{
	unsigned gb_tile_config;
	int r;

	/* Sub pixel 1/12 so we can have 4K rendering according to doc */
	gb_tile_config = R300_ENABLE_TILING | R300_TILE_SIZE_16;
	switch (rdev->num_gb_pipes) {
	case 2:
		gb_tile_config |= R300_PIPE_COUNT_R300;
		break;
	case 3:
		gb_tile_config |= R300_PIPE_COUNT_R420_3P;
		break;
	case 4:
		gb_tile_config |= R300_PIPE_COUNT_R420;
		break;
	case 1:
	default:
		gb_tile_config |= R300_PIPE_COUNT_RV350;
		break;
	}

	r = radeon_ring_lock(rdev, 64);
	if (r) {
		return;
	}
	radeon_ring_write(rdev, PACKET0(RADEON_ISYNC_CNTL, 0));
	radeon_ring_write(rdev,
			  RADEON_ISYNC_ANY2D_IDLE3D |
			  RADEON_ISYNC_ANY3D_IDLE2D |
			  RADEON_ISYNC_WAIT_IDLEGUI |
			  RADEON_ISYNC_CPSCRATCH_IDLEGUI);
	radeon_ring_write(rdev, PACKET0(R300_GB_TILE_CONFIG, 0));
	radeon_ring_write(rdev, gb_tile_config);
	radeon_ring_write(rdev, PACKET0(RADEON_WAIT_UNTIL, 0));
	radeon_ring_write(rdev,
			  RADEON_WAIT_2D_IDLECLEAN |
			  RADEON_WAIT_3D_IDLECLEAN);
	radeon_ring_write(rdev, PACKET0(0x170C, 0));
	radeon_ring_write(rdev, 1 << 31);
	radeon_ring_write(rdev, PACKET0(R300_GB_SELECT, 0));
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, PACKET0(R300_GB_ENABLE, 0));
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, PACKET0(0x42C8, 0));
	radeon_ring_write(rdev, (1 << rdev->num_gb_pipes) - 1);
	radeon_ring_write(rdev, PACKET0(R500_VAP_INDEX_OFFSET, 0));
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, PACKET0(R300_RB3D_DSTCACHE_CTLSTAT, 0));
	radeon_ring_write(rdev, R300_RB3D_DC_FLUSH | R300_RB3D_DC_FREE);
	radeon_ring_write(rdev, PACKET0(R300_RB3D_ZCACHE_CTLSTAT, 0));
	radeon_ring_write(rdev, R300_ZC_FLUSH | R300_ZC_FREE);
	radeon_ring_write(rdev, PACKET0(RADEON_WAIT_UNTIL, 0));
	radeon_ring_write(rdev,
			  RADEON_WAIT_2D_IDLECLEAN |
			  RADEON_WAIT_3D_IDLECLEAN);
	radeon_ring_write(rdev, PACKET0(R300_GB_AA_CONFIG, 0));
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, PACKET0(R300_RB3D_DSTCACHE_CTLSTAT, 0));
	radeon_ring_write(rdev, R300_RB3D_DC_FLUSH | R300_RB3D_DC_FREE);
	radeon_ring_write(rdev, PACKET0(R300_RB3D_ZCACHE_CTLSTAT, 0));
	radeon_ring_write(rdev, R300_ZC_FLUSH | R300_ZC_FREE);
	radeon_ring_write(rdev, PACKET0(R300_GB_MSPOS0, 0));
	radeon_ring_write(rdev,
			  ((6 << R300_MS_X0_SHIFT) |
			   (6 << R300_MS_Y0_SHIFT) |
			   (6 << R300_MS_X1_SHIFT) |
			   (6 << R300_MS_Y1_SHIFT) |
			   (6 << R300_MS_X2_SHIFT) |
			   (6 << R300_MS_Y2_SHIFT) |
			   (6 << R300_MSBD0_Y_SHIFT) |
			   (6 << R300_MSBD0_X_SHIFT)));
	radeon_ring_write(rdev, PACKET0(R300_GB_MSPOS1, 0));
	radeon_ring_write(rdev,
			  ((6 << R300_MS_X3_SHIFT) |
			   (6 << R300_MS_Y3_SHIFT) |
			   (6 << R300_MS_X4_SHIFT) |
			   (6 << R300_MS_Y4_SHIFT) |
			   (6 << R300_MS_X5_SHIFT) |
			   (6 << R300_MS_Y5_SHIFT) |
			   (6 << R300_MSBD1_SHIFT)));
	radeon_ring_write(rdev, PACKET0(R300_GA_ENHANCE, 0));
	radeon_ring_write(rdev, R300_GA_DEADLOCK_CNTL | R300_GA_FASTSYNC_CNTL);
	radeon_ring_write(rdev, PACKET0(R300_GA_POLY_MODE, 0));
	radeon_ring_write(rdev,
			  R300_FRONT_PTYPE_TRIANGE | R300_BACK_PTYPE_TRIANGE);
	radeon_ring_write(rdev, PACKET0(R300_GA_ROUND_MODE, 0));
	radeon_ring_write(rdev,
			  R300_GEOMETRY_ROUND_NEAREST |
			  R300_COLOR_ROUND_NEAREST);
	radeon_ring_unlock_commit(rdev);
}

void rv515_errata(struct radeon_device *rdev)
{
	rdev->pll_errata = 0;
}

int rv515_mc_wait_for_idle(struct radeon_device *rdev)
{
	unsigned i;
	uint32_t tmp;

	for (i = 0; i < rdev->usec_timeout; i++) {
		/* read MC_STATUS */
		tmp = RREG32_MC(RV515_MC_STATUS);
		if (tmp & RV515_MC_STATUS_IDLE) {
			return 0;
		}
		DRM_UDELAY(1);
	}
	return -1;
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

	rs600_disable_vga(rdev);

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
		WREG32(RADEON_CP_CSQ_MODE, 0);
		WREG32(RADEON_CP_CSQ_CNTL, 0);
		WREG32(RADEON_RBBM_SOFT_RESET, 0x32005);
		(void)RREG32(RADEON_RBBM_SOFT_RESET);
		udelay(200);
		WREG32(RADEON_RBBM_SOFT_RESET, 0);
		/* Wait to prevent race in RBBM_STATUS */
		mdelay(1);
		tmp = RREG32(RADEON_RBBM_STATUS);
		if (tmp & ((1 << 20) | (1 << 26))) {
			DRM_ERROR("VAP & CP still busy (RBBM_STATUS=0x%08X)\n", tmp);
			/* GA still busy soft reset it */
			WREG32(0x429C, 0x200);
			WREG32(R300_VAP_PVS_STATE_FLUSH_REG, 0);
			WREG32(0x43E0, 0);
			WREG32(0x43E4, 0);
			WREG32(0x24AC, 0);
		}
		/* Wait to prevent race in RBBM_STATUS */
		mdelay(1);
		tmp = RREG32(RADEON_RBBM_STATUS);
		if (!(tmp & ((1 << 20) | (1 << 26)))) {
			break;
		}
	}
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(RADEON_RBBM_STATUS);
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
	tmp = RREG32(RADEON_RBBM_STATUS);
	DRM_ERROR("Failed to reset GA ! (RBBM_STATUS=0x%08X)\n", tmp);
	return -1;
}

int rv515_gpu_reset(struct radeon_device *rdev)
{
	uint32_t status;

	/* reset order likely matter */
	status = RREG32(RADEON_RBBM_STATUS);
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
	status = RREG32(RADEON_RBBM_STATUS);
	if (status & (1 << 16)) {
		r100_cp_reset(rdev);
	}
	/* Check if GPU is idle */
	status = RREG32(RADEON_RBBM_STATUS);
	if (status & (1 << 31)) {
		DRM_ERROR("Failed to reset GPU (RBBM_STATUS=0x%08X)\n", status);
		return -1;
	}
	DRM_INFO("GPU reset succeed (RBBM_STATUS=0x%08X)\n", status);
	return 0;
}


/*
 * VRAM info
 */
static void rv515_vram_get_type(struct radeon_device *rdev)
{
	uint32_t tmp;

	rdev->mc.vram_width = 128;
	rdev->mc.vram_is_ddr = true;
	tmp = RREG32_MC(RV515_MC_CNTL);
	tmp &= RV515_MEM_NUM_CHANNELS_MASK;
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
	rv515_vram_get_type(rdev);
	rdev->mc.vram_size = RREG32(RADEON_CONFIG_MEMSIZE);

	rdev->mc.aper_base = drm_get_resource_start(rdev->ddev, 0);
	rdev->mc.aper_size = drm_get_resource_len(rdev->ddev, 0);
}


/*
 * Indirect registers accessor
 */
uint32_t rv515_mc_rreg(struct radeon_device *rdev, uint32_t reg)
{
	uint32_t r;

	WREG32(R520_MC_IND_INDEX, 0x7f0000 | (reg & 0xffff));
	r = RREG32(R520_MC_IND_DATA);
	WREG32(R520_MC_IND_INDEX, 0);
	return r;
}

void rv515_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	WREG32(R520_MC_IND_INDEX, 0xff0000 | ((reg) & 0xffff));
	WREG32(R520_MC_IND_DATA, (v));
	WREG32(R520_MC_IND_INDEX, 0);
}

uint32_t rv515_pcie_rreg(struct radeon_device *rdev, uint32_t reg)
{
	uint32_t r;

	WREG32(RADEON_PCIE_INDEX, ((reg) & 0x7ff));
	(void)RREG32(RADEON_PCIE_INDEX);
	r = RREG32(RADEON_PCIE_DATA);
	return r;
}

void rv515_pcie_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	WREG32(RADEON_PCIE_INDEX, ((reg) & 0x7ff));
	(void)RREG32(RADEON_PCIE_INDEX);
	WREG32(RADEON_PCIE_DATA, (v));
	(void)RREG32(RADEON_PCIE_DATA);
}


/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)
static int rv515_debugfs_pipes_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t tmp;

	tmp = RREG32(R400_GB_PIPE_SELECT);
	seq_printf(m, "GB_PIPE_SELECT 0x%08x\n", tmp);
	tmp = RREG32(R500_SU_REG_DEST);
	seq_printf(m, "SU_REG_DEST 0x%08x\n", tmp);
	tmp = RREG32(R300_GB_TILE_CONFIG);
	seq_printf(m, "GB_TILE_CONFIG 0x%08x\n", tmp);
	tmp = RREG32(R300_DST_PIPE_CONFIG);
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
