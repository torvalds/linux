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
#include "drmP.h"
#include "radeon_reg.h"
#include "radeon.h"

/* rs690,rs740 depends on : */
void r100_hdp_reset(struct radeon_device *rdev);
int r300_mc_wait_for_idle(struct radeon_device *rdev);
void r420_pipes_init(struct radeon_device *rdev);
void rs400_gart_disable(struct radeon_device *rdev);
int rs400_gart_enable(struct radeon_device *rdev);
void rs400_gart_adjust_size(struct radeon_device *rdev);
void rs600_mc_disable_clients(struct radeon_device *rdev);
void rs600_disable_vga(struct radeon_device *rdev);

/* This files gather functions specifics to :
 * rs690,rs740
 *
 * Some of these functions might be used by newer ASICs.
 */
void rs690_gpu_init(struct radeon_device *rdev);
int rs690_mc_wait_for_idle(struct radeon_device *rdev);


/*
 * MC functions.
 */
int rs690_mc_init(struct radeon_device *rdev)
{
	uint32_t tmp;
	int r;

	if (r100_debugfs_rbbm_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for RBBM !\n");
	}

	rs690_gpu_init(rdev);
	rs400_gart_disable(rdev);

	/* Setup GPU memory space */
	rdev->mc.gtt_location = rdev->mc.vram_size;
	rdev->mc.gtt_location += (rdev->mc.gtt_size - 1);
	rdev->mc.gtt_location &= ~(rdev->mc.gtt_size - 1);
	rdev->mc.vram_location = 0xFFFFFFFFUL;
	r = radeon_mc_setup(rdev);
	if (r) {
		return r;
	}

	/* Program GPU memory space */
	rs600_mc_disable_clients(rdev);
	if (rs690_mc_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait MC idle while "
		       "programming pipes. Bad things might happen.\n");
	}
	tmp = rdev->mc.vram_location + rdev->mc.vram_size - 1;
	tmp = REG_SET(RS690_MC_FB_TOP, tmp >> 16);
	tmp |= REG_SET(RS690_MC_FB_START, rdev->mc.vram_location >> 16);
	WREG32_MC(RS690_MCCFG_FB_LOCATION, tmp);
	/* FIXME: Does this reg exist on RS480,RS740 ? */
	WREG32(0x310, rdev->mc.vram_location);
	WREG32(RS690_HDP_FB_LOCATION, rdev->mc.vram_location >> 16);
	return 0;
}

void rs690_mc_fini(struct radeon_device *rdev)
{
	rs400_gart_disable(rdev);
	radeon_gart_table_ram_free(rdev);
	radeon_gart_fini(rdev);
}


/*
 * Global GPU functions
 */
int rs690_mc_wait_for_idle(struct radeon_device *rdev)
{
	unsigned i;
	uint32_t tmp;

	for (i = 0; i < rdev->usec_timeout; i++) {
		/* read MC_STATUS */
		tmp = RREG32_MC(RS690_MC_STATUS);
		if (tmp & RS690_MC_STATUS_IDLE) {
			return 0;
		}
		DRM_UDELAY(1);
	}
	return -1;
}

void rs690_errata(struct radeon_device *rdev)
{
	rdev->pll_errata = 0;
}

void rs690_gpu_init(struct radeon_device *rdev)
{
	/* FIXME: HDP same place on rs690 ? */
	r100_hdp_reset(rdev);
	rs600_disable_vga(rdev);
	/* FIXME: is this correct ? */
	r420_pipes_init(rdev);
	if (rs690_mc_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait MC idle while "
		       "programming pipes. Bad things might happen.\n");
	}
}


/*
 * VRAM info.
 */
void rs690_vram_info(struct radeon_device *rdev)
{
	uint32_t tmp;

	rs400_gart_adjust_size(rdev);
	/* DDR for all card after R300 & IGP */
	rdev->mc.vram_is_ddr = true;
	/* FIXME: is this correct for RS690/RS740 ? */
	tmp = RREG32(RADEON_MEM_CNTL);
	if (tmp & R300_MEM_NUM_CHANNELS_MASK) {
		rdev->mc.vram_width = 128;
	} else {
		rdev->mc.vram_width = 64;
	}
	rdev->mc.vram_size = RREG32(RADEON_CONFIG_MEMSIZE);

	rdev->mc.aper_base = drm_get_resource_start(rdev->ddev, 0);
	rdev->mc.aper_size = drm_get_resource_len(rdev->ddev, 0);
}


/*
 * Indirect registers accessor
 */
uint32_t rs690_mc_rreg(struct radeon_device *rdev, uint32_t reg)
{
	uint32_t r;

	WREG32(RS690_MC_INDEX, (reg & RS690_MC_INDEX_MASK));
	r = RREG32(RS690_MC_DATA);
	WREG32(RS690_MC_INDEX, RS690_MC_INDEX_MASK);
	return r;
}

void rs690_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	WREG32(RS690_MC_INDEX,
	       RS690_MC_INDEX_WR_EN | ((reg) & RS690_MC_INDEX_MASK));
	WREG32(RS690_MC_DATA, v);
	WREG32(RS690_MC_INDEX, RS690_MC_INDEX_WR_ACK);
}
