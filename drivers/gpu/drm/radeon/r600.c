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

/* r600,rv610,rv630,rv620,rv635,rv670 depends on : */
void rs600_mc_disable_clients(struct radeon_device *rdev);

/* This files gather functions specifics to:
 * r600,rv610,rv630,rv620,rv635,rv670
 *
 * Some of these functions might be used by newer ASICs.
 */
int r600_mc_wait_for_idle(struct radeon_device *rdev);
void r600_gpu_init(struct radeon_device *rdev);


/*
 * MC
 */
int r600_mc_init(struct radeon_device *rdev)
{
	uint32_t tmp;

	r600_gpu_init(rdev);

	/* setup the gart before changing location so we can ask to
	 * discard unmapped mc request
	 */
	/* FIXME: disable out of gart access */
	tmp = rdev->mc.gtt_location / 4096;
	tmp = REG_SET(R600_LOGICAL_PAGE_NUMBER, tmp);
	WREG32(R600_MC_VM_SYSTEM_APERTURE_LOW_ADDR, tmp);
	tmp = (rdev->mc.gtt_location + rdev->mc.gtt_size) / 4096;
	tmp = REG_SET(R600_LOGICAL_PAGE_NUMBER, tmp);
	WREG32(R600_MC_VM_SYSTEM_APERTURE_HIGH_ADDR, tmp);

	rs600_mc_disable_clients(rdev);
	if (r600_mc_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait MC idle while "
		       "programming pipes. Bad things might happen.\n");
	}

	tmp = rdev->mc.vram_location + rdev->mc.vram_size - 1;
	tmp = REG_SET(R600_MC_FB_TOP, tmp >> 24);
	tmp |= REG_SET(R600_MC_FB_BASE, rdev->mc.vram_location >> 24);
	WREG32(R600_MC_VM_FB_LOCATION, tmp);
	tmp = rdev->mc.gtt_location + rdev->mc.gtt_size - 1;
	tmp = REG_SET(R600_MC_AGP_TOP, tmp >> 22);
	WREG32(R600_MC_VM_AGP_TOP, tmp);
	tmp = REG_SET(R600_MC_AGP_BOT, rdev->mc.gtt_location >> 22);
	WREG32(R600_MC_VM_AGP_BOT, tmp);
	return 0;
}

void r600_mc_fini(struct radeon_device *rdev)
{
	/* FIXME: implement */
}


/*
 * Global GPU functions
 */
void r600_errata(struct radeon_device *rdev)
{
	rdev->pll_errata = 0;
}

int r600_mc_wait_for_idle(struct radeon_device *rdev)
{
	/* FIXME: implement */
	return 0;
}

void r600_gpu_init(struct radeon_device *rdev)
{
	/* FIXME: implement */
}


/*
 * VRAM info
 */
void r600_vram_get_type(struct radeon_device *rdev)
{
	uint32_t tmp;
	int chansize;

	rdev->mc.vram_width = 128;
	rdev->mc.vram_is_ddr = true;

	tmp = RREG32(R600_RAMCFG);
	if (tmp & R600_CHANSIZE_OVERRIDE) {
		chansize = 16;
	} else if (tmp & R600_CHANSIZE) {
		chansize = 64;
	} else {
		chansize = 32;
	}
	if (rdev->family == CHIP_R600) {
		rdev->mc.vram_width = 8 * chansize;
	} else if (rdev->family == CHIP_RV670) {
		rdev->mc.vram_width = 4 * chansize;
	} else if ((rdev->family == CHIP_RV610) ||
			(rdev->family == CHIP_RV620)) {
		rdev->mc.vram_width = chansize;
	} else if ((rdev->family == CHIP_RV630) ||
			(rdev->family == CHIP_RV635)) {
		rdev->mc.vram_width = 2 * chansize;
	}
}

void r600_vram_info(struct radeon_device *rdev)
{
	r600_vram_get_type(rdev);
	rdev->mc.vram_size = RREG32(R600_CONFIG_MEMSIZE);

	/* Could aper size report 0 ? */
	rdev->mc.aper_base = drm_get_resource_start(rdev->ddev, 0);
	rdev->mc.aper_size = drm_get_resource_len(rdev->ddev, 0);
}

/*
 * Indirect registers accessor
 */
uint32_t r600_pciep_rreg(struct radeon_device *rdev, uint32_t reg)
{
	uint32_t r;

	WREG32(R600_PCIE_PORT_INDEX, ((reg) & 0xff));
	(void)RREG32(R600_PCIE_PORT_INDEX);
	r = RREG32(R600_PCIE_PORT_DATA);
	return r;
}

void r600_pciep_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	WREG32(R600_PCIE_PORT_INDEX, ((reg) & 0xff));
	(void)RREG32(R600_PCIE_PORT_INDEX);
	WREG32(R600_PCIE_PORT_DATA, (v));
	(void)RREG32(R600_PCIE_PORT_DATA);
}
