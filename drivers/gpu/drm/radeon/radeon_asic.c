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

#include <linux/console.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/radeon_drm.h>
#include <linux/vgaarb.h>
#include <linux/vga_switcheroo.h>
#include "radeon_reg.h"
#include "radeon.h"
#include "radeon_asic.h"
#include "atom.h"

/*
 * Registers accessors functions.
 */
static uint32_t radeon_invalid_rreg(struct radeon_device *rdev, uint32_t reg)
{
	DRM_ERROR("Invalid callback to read register 0x%04X\n", reg);
	BUG_ON(1);
	return 0;
}

static void radeon_invalid_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	DRM_ERROR("Invalid callback to write register 0x%04X with 0x%08X\n",
		  reg, v);
	BUG_ON(1);
}

static void radeon_register_accessor_init(struct radeon_device *rdev)
{
	rdev->mc_rreg = &radeon_invalid_rreg;
	rdev->mc_wreg = &radeon_invalid_wreg;
	rdev->pll_rreg = &radeon_invalid_rreg;
	rdev->pll_wreg = &radeon_invalid_wreg;
	rdev->pciep_rreg = &radeon_invalid_rreg;
	rdev->pciep_wreg = &radeon_invalid_wreg;

	/* Don't change order as we are overridding accessor. */
	if (rdev->family < CHIP_RV515) {
		rdev->pcie_reg_mask = 0xff;
	} else {
		rdev->pcie_reg_mask = 0x7ff;
	}
	/* FIXME: not sure here */
	if (rdev->family <= CHIP_R580) {
		rdev->pll_rreg = &r100_pll_rreg;
		rdev->pll_wreg = &r100_pll_wreg;
	}
	if (rdev->family >= CHIP_R420) {
		rdev->mc_rreg = &r420_mc_rreg;
		rdev->mc_wreg = &r420_mc_wreg;
	}
	if (rdev->family >= CHIP_RV515) {
		rdev->mc_rreg = &rv515_mc_rreg;
		rdev->mc_wreg = &rv515_mc_wreg;
	}
	if (rdev->family == CHIP_RS400 || rdev->family == CHIP_RS480) {
		rdev->mc_rreg = &rs400_mc_rreg;
		rdev->mc_wreg = &rs400_mc_wreg;
	}
	if (rdev->family == CHIP_RS690 || rdev->family == CHIP_RS740) {
		rdev->mc_rreg = &rs690_mc_rreg;
		rdev->mc_wreg = &rs690_mc_wreg;
	}
	if (rdev->family == CHIP_RS600) {
		rdev->mc_rreg = &rs600_mc_rreg;
		rdev->mc_wreg = &rs600_mc_wreg;
	}
	if ((rdev->family >= CHIP_R600) && (rdev->family <= CHIP_RV740)) {
		rdev->pciep_rreg = &r600_pciep_rreg;
		rdev->pciep_wreg = &r600_pciep_wreg;
	}
}


/* helper to disable agp */
void radeon_agp_disable(struct radeon_device *rdev)
{
	rdev->flags &= ~RADEON_IS_AGP;
	if (rdev->family >= CHIP_R600) {
		DRM_INFO("Forcing AGP to PCIE mode\n");
		rdev->flags |= RADEON_IS_PCIE;
	} else if (rdev->family >= CHIP_RV515 ||
			rdev->family == CHIP_RV380 ||
			rdev->family == CHIP_RV410 ||
			rdev->family == CHIP_R423) {
		DRM_INFO("Forcing AGP to PCIE mode\n");
		rdev->flags |= RADEON_IS_PCIE;
		rdev->asic->gart_tlb_flush = &rv370_pcie_gart_tlb_flush;
		rdev->asic->gart_set_page = &rv370_pcie_gart_set_page;
	} else {
		DRM_INFO("Forcing AGP to PCI mode\n");
		rdev->flags |= RADEON_IS_PCI;
		rdev->asic->gart_tlb_flush = &r100_pci_gart_tlb_flush;
		rdev->asic->gart_set_page = &r100_pci_gart_set_page;
	}
	rdev->mc.gtt_size = radeon_gart_size * 1024 * 1024;
}

/*
 * ASIC
 */
int radeon_asic_init(struct radeon_device *rdev)
{
	radeon_register_accessor_init(rdev);
	switch (rdev->family) {
	case CHIP_R100:
	case CHIP_RV100:
	case CHIP_RS100:
	case CHIP_RV200:
	case CHIP_RS200:
		rdev->asic = &r100_asic;
		break;
	case CHIP_R200:
	case CHIP_RV250:
	case CHIP_RS300:
	case CHIP_RV280:
		rdev->asic = &r200_asic;
		break;
	case CHIP_R300:
	case CHIP_R350:
	case CHIP_RV350:
	case CHIP_RV380:
		if (rdev->flags & RADEON_IS_PCIE)
			rdev->asic = &r300_asic_pcie;
		else
			rdev->asic = &r300_asic;
		break;
	case CHIP_R420:
	case CHIP_R423:
	case CHIP_RV410:
		rdev->asic = &r420_asic;
		break;
	case CHIP_RS400:
	case CHIP_RS480:
		rdev->asic = &rs400_asic;
		break;
	case CHIP_RS600:
		rdev->asic = &rs600_asic;
		break;
	case CHIP_RS690:
	case CHIP_RS740:
		rdev->asic = &rs690_asic;
		break;
	case CHIP_RV515:
		rdev->asic = &rv515_asic;
		break;
	case CHIP_R520:
	case CHIP_RV530:
	case CHIP_RV560:
	case CHIP_RV570:
	case CHIP_R580:
		rdev->asic = &r520_asic;
		break;
	case CHIP_R600:
	case CHIP_RV610:
	case CHIP_RV630:
	case CHIP_RV620:
	case CHIP_RV635:
	case CHIP_RV670:
	case CHIP_RS780:
	case CHIP_RS880:
		rdev->asic = &r600_asic;
		break;
	case CHIP_RV770:
	case CHIP_RV730:
	case CHIP_RV710:
	case CHIP_RV740:
		rdev->asic = &rv770_asic;
		break;
	case CHIP_CEDAR:
	case CHIP_REDWOOD:
	case CHIP_JUNIPER:
	case CHIP_CYPRESS:
	case CHIP_HEMLOCK:
		rdev->asic = &evergreen_asic;
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	if (rdev->flags & RADEON_IS_IGP) {
		rdev->asic->get_memory_clock = NULL;
		rdev->asic->set_memory_clock = NULL;
	}

	return 0;
}

/*
 * Wrapper around modesetting bits. Move to radeon_clocks.c?
 */
int radeon_clocks_init(struct radeon_device *rdev)
{
	int r;

	r = radeon_static_clocks_init(rdev->ddev);
	if (r) {
		return r;
	}
	DRM_INFO("Clocks initialized !\n");
	return 0;
}

void radeon_clocks_fini(struct radeon_device *rdev)
{
}
