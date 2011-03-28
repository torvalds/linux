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
	if ((rdev->family >= CHIP_R600) && (rdev->family <= CHIP_HEMLOCK)) {
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
static struct radeon_asic r100_asic = {
	.init = &r100_init,
	.fini = &r100_fini,
	.suspend = &r100_suspend,
	.resume = &r100_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_is_lockup = &r100_gpu_is_lockup,
	.asic_reset = &r100_asic_reset,
	.gart_tlb_flush = &r100_pci_gart_tlb_flush,
	.gart_set_page = &r100_pci_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &r100_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &r100_irq_set,
	.irq_process = &r100_irq_process,
	.get_vblank_counter = &r100_get_vblank_counter,
	.fence_ring_emit = &r100_fence_ring_emit,
	.cs_parse = &r100_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = NULL,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_legacy_get_engine_clock,
	.set_engine_clock = &radeon_legacy_set_engine_clock,
	.get_memory_clock = &radeon_legacy_get_memory_clock,
	.set_memory_clock = NULL,
	.get_pcie_lanes = NULL,
	.set_pcie_lanes = NULL,
	.set_clock_gating = &radeon_legacy_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &r100_bandwidth_update,
	.hpd_init = &r100_hpd_init,
	.hpd_fini = &r100_hpd_fini,
	.hpd_sense = &r100_hpd_sense,
	.hpd_set_polarity = &r100_hpd_set_polarity,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.pm_misc = &r100_pm_misc,
	.pm_prepare = &r100_pm_prepare,
	.pm_finish = &r100_pm_finish,
	.pm_init_profile = &r100_pm_init_profile,
	.pm_get_dynpm_state = &r100_pm_get_dynpm_state,
	.pre_page_flip = &r100_pre_page_flip,
	.page_flip = &r100_page_flip,
	.post_page_flip = &r100_post_page_flip,
};

static struct radeon_asic r200_asic = {
	.init = &r100_init,
	.fini = &r100_fini,
	.suspend = &r100_suspend,
	.resume = &r100_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_is_lockup = &r100_gpu_is_lockup,
	.asic_reset = &r100_asic_reset,
	.gart_tlb_flush = &r100_pci_gart_tlb_flush,
	.gart_set_page = &r100_pci_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &r100_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &r100_irq_set,
	.irq_process = &r100_irq_process,
	.get_vblank_counter = &r100_get_vblank_counter,
	.fence_ring_emit = &r100_fence_ring_emit,
	.cs_parse = &r100_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r200_copy_dma,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_legacy_get_engine_clock,
	.set_engine_clock = &radeon_legacy_set_engine_clock,
	.get_memory_clock = &radeon_legacy_get_memory_clock,
	.set_memory_clock = NULL,
	.set_pcie_lanes = NULL,
	.set_clock_gating = &radeon_legacy_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &r100_bandwidth_update,
	.hpd_init = &r100_hpd_init,
	.hpd_fini = &r100_hpd_fini,
	.hpd_sense = &r100_hpd_sense,
	.hpd_set_polarity = &r100_hpd_set_polarity,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.pm_misc = &r100_pm_misc,
	.pm_prepare = &r100_pm_prepare,
	.pm_finish = &r100_pm_finish,
	.pm_init_profile = &r100_pm_init_profile,
	.pm_get_dynpm_state = &r100_pm_get_dynpm_state,
	.pre_page_flip = &r100_pre_page_flip,
	.page_flip = &r100_page_flip,
	.post_page_flip = &r100_post_page_flip,
};

static struct radeon_asic r300_asic = {
	.init = &r300_init,
	.fini = &r300_fini,
	.suspend = &r300_suspend,
	.resume = &r300_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_is_lockup = &r300_gpu_is_lockup,
	.asic_reset = &r300_asic_reset,
	.gart_tlb_flush = &r100_pci_gart_tlb_flush,
	.gart_set_page = &r100_pci_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &r300_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &r100_irq_set,
	.irq_process = &r100_irq_process,
	.get_vblank_counter = &r100_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r200_copy_dma,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_legacy_get_engine_clock,
	.set_engine_clock = &radeon_legacy_set_engine_clock,
	.get_memory_clock = &radeon_legacy_get_memory_clock,
	.set_memory_clock = NULL,
	.get_pcie_lanes = &rv370_get_pcie_lanes,
	.set_pcie_lanes = &rv370_set_pcie_lanes,
	.set_clock_gating = &radeon_legacy_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &r100_bandwidth_update,
	.hpd_init = &r100_hpd_init,
	.hpd_fini = &r100_hpd_fini,
	.hpd_sense = &r100_hpd_sense,
	.hpd_set_polarity = &r100_hpd_set_polarity,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.pm_misc = &r100_pm_misc,
	.pm_prepare = &r100_pm_prepare,
	.pm_finish = &r100_pm_finish,
	.pm_init_profile = &r100_pm_init_profile,
	.pm_get_dynpm_state = &r100_pm_get_dynpm_state,
	.pre_page_flip = &r100_pre_page_flip,
	.page_flip = &r100_page_flip,
	.post_page_flip = &r100_post_page_flip,
};

static struct radeon_asic r300_asic_pcie = {
	.init = &r300_init,
	.fini = &r300_fini,
	.suspend = &r300_suspend,
	.resume = &r300_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_is_lockup = &r300_gpu_is_lockup,
	.asic_reset = &r300_asic_reset,
	.gart_tlb_flush = &rv370_pcie_gart_tlb_flush,
	.gart_set_page = &rv370_pcie_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &r300_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &r100_irq_set,
	.irq_process = &r100_irq_process,
	.get_vblank_counter = &r100_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r200_copy_dma,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_legacy_get_engine_clock,
	.set_engine_clock = &radeon_legacy_set_engine_clock,
	.get_memory_clock = &radeon_legacy_get_memory_clock,
	.set_memory_clock = NULL,
	.set_pcie_lanes = &rv370_set_pcie_lanes,
	.set_clock_gating = &radeon_legacy_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &r100_bandwidth_update,
	.hpd_init = &r100_hpd_init,
	.hpd_fini = &r100_hpd_fini,
	.hpd_sense = &r100_hpd_sense,
	.hpd_set_polarity = &r100_hpd_set_polarity,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.pm_misc = &r100_pm_misc,
	.pm_prepare = &r100_pm_prepare,
	.pm_finish = &r100_pm_finish,
	.pm_init_profile = &r100_pm_init_profile,
	.pm_get_dynpm_state = &r100_pm_get_dynpm_state,
	.pre_page_flip = &r100_pre_page_flip,
	.page_flip = &r100_page_flip,
	.post_page_flip = &r100_post_page_flip,
};

static struct radeon_asic r420_asic = {
	.init = &r420_init,
	.fini = &r420_fini,
	.suspend = &r420_suspend,
	.resume = &r420_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_is_lockup = &r300_gpu_is_lockup,
	.asic_reset = &r300_asic_reset,
	.gart_tlb_flush = &rv370_pcie_gart_tlb_flush,
	.gart_set_page = &rv370_pcie_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &r300_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &r100_irq_set,
	.irq_process = &r100_irq_process,
	.get_vblank_counter = &r100_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r200_copy_dma,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.get_pcie_lanes = &rv370_get_pcie_lanes,
	.set_pcie_lanes = &rv370_set_pcie_lanes,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &r100_bandwidth_update,
	.hpd_init = &r100_hpd_init,
	.hpd_fini = &r100_hpd_fini,
	.hpd_sense = &r100_hpd_sense,
	.hpd_set_polarity = &r100_hpd_set_polarity,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.pm_misc = &r100_pm_misc,
	.pm_prepare = &r100_pm_prepare,
	.pm_finish = &r100_pm_finish,
	.pm_init_profile = &r420_pm_init_profile,
	.pm_get_dynpm_state = &r100_pm_get_dynpm_state,
	.pre_page_flip = &r100_pre_page_flip,
	.page_flip = &r100_page_flip,
	.post_page_flip = &r100_post_page_flip,
};

static struct radeon_asic rs400_asic = {
	.init = &rs400_init,
	.fini = &rs400_fini,
	.suspend = &rs400_suspend,
	.resume = &rs400_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_is_lockup = &r300_gpu_is_lockup,
	.asic_reset = &r300_asic_reset,
	.gart_tlb_flush = &rs400_gart_tlb_flush,
	.gart_set_page = &rs400_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &r300_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &r100_irq_set,
	.irq_process = &r100_irq_process,
	.get_vblank_counter = &r100_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r200_copy_dma,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_legacy_get_engine_clock,
	.set_engine_clock = &radeon_legacy_set_engine_clock,
	.get_memory_clock = &radeon_legacy_get_memory_clock,
	.set_memory_clock = NULL,
	.get_pcie_lanes = NULL,
	.set_pcie_lanes = NULL,
	.set_clock_gating = &radeon_legacy_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &r100_bandwidth_update,
	.hpd_init = &r100_hpd_init,
	.hpd_fini = &r100_hpd_fini,
	.hpd_sense = &r100_hpd_sense,
	.hpd_set_polarity = &r100_hpd_set_polarity,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.pm_misc = &r100_pm_misc,
	.pm_prepare = &r100_pm_prepare,
	.pm_finish = &r100_pm_finish,
	.pm_init_profile = &r100_pm_init_profile,
	.pm_get_dynpm_state = &r100_pm_get_dynpm_state,
	.pre_page_flip = &r100_pre_page_flip,
	.page_flip = &r100_page_flip,
	.post_page_flip = &r100_post_page_flip,
};

static struct radeon_asic rs600_asic = {
	.init = &rs600_init,
	.fini = &rs600_fini,
	.suspend = &rs600_suspend,
	.resume = &rs600_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_is_lockup = &r300_gpu_is_lockup,
	.asic_reset = &rs600_asic_reset,
	.gart_tlb_flush = &rs600_gart_tlb_flush,
	.gart_set_page = &rs600_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &r300_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &rs600_irq_set,
	.irq_process = &rs600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r200_copy_dma,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.get_pcie_lanes = NULL,
	.set_pcie_lanes = NULL,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &rs600_bandwidth_update,
	.hpd_init = &rs600_hpd_init,
	.hpd_fini = &rs600_hpd_fini,
	.hpd_sense = &rs600_hpd_sense,
	.hpd_set_polarity = &rs600_hpd_set_polarity,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.pm_misc = &rs600_pm_misc,
	.pm_prepare = &rs600_pm_prepare,
	.pm_finish = &rs600_pm_finish,
	.pm_init_profile = &r420_pm_init_profile,
	.pm_get_dynpm_state = &r100_pm_get_dynpm_state,
	.pre_page_flip = &rs600_pre_page_flip,
	.page_flip = &rs600_page_flip,
	.post_page_flip = &rs600_post_page_flip,
};

static struct radeon_asic rs690_asic = {
	.init = &rs690_init,
	.fini = &rs690_fini,
	.suspend = &rs690_suspend,
	.resume = &rs690_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_is_lockup = &r300_gpu_is_lockup,
	.asic_reset = &rs600_asic_reset,
	.gart_tlb_flush = &rs400_gart_tlb_flush,
	.gart_set_page = &rs400_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &r300_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &rs600_irq_set,
	.irq_process = &rs600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r200_copy_dma,
	.copy = &r200_copy_dma,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.get_pcie_lanes = NULL,
	.set_pcie_lanes = NULL,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &rs690_bandwidth_update,
	.hpd_init = &rs600_hpd_init,
	.hpd_fini = &rs600_hpd_fini,
	.hpd_sense = &rs600_hpd_sense,
	.hpd_set_polarity = &rs600_hpd_set_polarity,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.pm_misc = &rs600_pm_misc,
	.pm_prepare = &rs600_pm_prepare,
	.pm_finish = &rs600_pm_finish,
	.pm_init_profile = &r420_pm_init_profile,
	.pm_get_dynpm_state = &r100_pm_get_dynpm_state,
	.pre_page_flip = &rs600_pre_page_flip,
	.page_flip = &rs600_page_flip,
	.post_page_flip = &rs600_post_page_flip,
};

static struct radeon_asic rv515_asic = {
	.init = &rv515_init,
	.fini = &rv515_fini,
	.suspend = &rv515_suspend,
	.resume = &rv515_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_is_lockup = &r300_gpu_is_lockup,
	.asic_reset = &rs600_asic_reset,
	.gart_tlb_flush = &rv370_pcie_gart_tlb_flush,
	.gart_set_page = &rv370_pcie_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &rv515_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &rs600_irq_set,
	.irq_process = &rs600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r200_copy_dma,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.get_pcie_lanes = &rv370_get_pcie_lanes,
	.set_pcie_lanes = &rv370_set_pcie_lanes,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &rv515_bandwidth_update,
	.hpd_init = &rs600_hpd_init,
	.hpd_fini = &rs600_hpd_fini,
	.hpd_sense = &rs600_hpd_sense,
	.hpd_set_polarity = &rs600_hpd_set_polarity,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.pm_misc = &rs600_pm_misc,
	.pm_prepare = &rs600_pm_prepare,
	.pm_finish = &rs600_pm_finish,
	.pm_init_profile = &r420_pm_init_profile,
	.pm_get_dynpm_state = &r100_pm_get_dynpm_state,
	.pre_page_flip = &rs600_pre_page_flip,
	.page_flip = &rs600_page_flip,
	.post_page_flip = &rs600_post_page_flip,
};

static struct radeon_asic r520_asic = {
	.init = &r520_init,
	.fini = &rv515_fini,
	.suspend = &rv515_suspend,
	.resume = &r520_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_is_lockup = &r300_gpu_is_lockup,
	.asic_reset = &rs600_asic_reset,
	.gart_tlb_flush = &rv370_pcie_gart_tlb_flush,
	.gart_set_page = &rv370_pcie_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &rv515_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &rs600_irq_set,
	.irq_process = &rs600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r200_copy_dma,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.get_pcie_lanes = &rv370_get_pcie_lanes,
	.set_pcie_lanes = &rv370_set_pcie_lanes,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &rv515_bandwidth_update,
	.hpd_init = &rs600_hpd_init,
	.hpd_fini = &rs600_hpd_fini,
	.hpd_sense = &rs600_hpd_sense,
	.hpd_set_polarity = &rs600_hpd_set_polarity,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.pm_misc = &rs600_pm_misc,
	.pm_prepare = &rs600_pm_prepare,
	.pm_finish = &rs600_pm_finish,
	.pm_init_profile = &r420_pm_init_profile,
	.pm_get_dynpm_state = &r100_pm_get_dynpm_state,
	.pre_page_flip = &rs600_pre_page_flip,
	.page_flip = &rs600_page_flip,
	.post_page_flip = &rs600_post_page_flip,
};

static struct radeon_asic r600_asic = {
	.init = &r600_init,
	.fini = &r600_fini,
	.suspend = &r600_suspend,
	.resume = &r600_resume,
	.cp_commit = &r600_cp_commit,
	.vga_set_state = &r600_vga_set_state,
	.gpu_is_lockup = &r600_gpu_is_lockup,
	.asic_reset = &r600_asic_reset,
	.gart_tlb_flush = &r600_pcie_gart_tlb_flush,
	.gart_set_page = &rs600_gart_set_page,
	.ring_test = &r600_ring_test,
	.ring_ib_execute = &r600_ring_ib_execute,
	.irq_set = &r600_irq_set,
	.irq_process = &r600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r600_fence_ring_emit,
	.cs_parse = &r600_cs_parse,
	.copy_blit = &r600_copy_blit,
	.copy_dma = &r600_copy_blit,
	.copy = &r600_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.get_pcie_lanes = &r600_get_pcie_lanes,
	.set_pcie_lanes = &r600_set_pcie_lanes,
	.set_clock_gating = NULL,
	.set_surface_reg = r600_set_surface_reg,
	.clear_surface_reg = r600_clear_surface_reg,
	.bandwidth_update = &rv515_bandwidth_update,
	.hpd_init = &r600_hpd_init,
	.hpd_fini = &r600_hpd_fini,
	.hpd_sense = &r600_hpd_sense,
	.hpd_set_polarity = &r600_hpd_set_polarity,
	.ioctl_wait_idle = r600_ioctl_wait_idle,
	.gui_idle = &r600_gui_idle,
	.pm_misc = &r600_pm_misc,
	.pm_prepare = &rs600_pm_prepare,
	.pm_finish = &rs600_pm_finish,
	.pm_init_profile = &r600_pm_init_profile,
	.pm_get_dynpm_state = &r600_pm_get_dynpm_state,
	.pre_page_flip = &rs600_pre_page_flip,
	.page_flip = &rs600_page_flip,
	.post_page_flip = &rs600_post_page_flip,
};

static struct radeon_asic rs780_asic = {
	.init = &r600_init,
	.fini = &r600_fini,
	.suspend = &r600_suspend,
	.resume = &r600_resume,
	.cp_commit = &r600_cp_commit,
	.gpu_is_lockup = &r600_gpu_is_lockup,
	.vga_set_state = &r600_vga_set_state,
	.asic_reset = &r600_asic_reset,
	.gart_tlb_flush = &r600_pcie_gart_tlb_flush,
	.gart_set_page = &rs600_gart_set_page,
	.ring_test = &r600_ring_test,
	.ring_ib_execute = &r600_ring_ib_execute,
	.irq_set = &r600_irq_set,
	.irq_process = &r600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r600_fence_ring_emit,
	.cs_parse = &r600_cs_parse,
	.copy_blit = &r600_copy_blit,
	.copy_dma = &r600_copy_blit,
	.copy = &r600_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = NULL,
	.set_memory_clock = NULL,
	.get_pcie_lanes = NULL,
	.set_pcie_lanes = NULL,
	.set_clock_gating = NULL,
	.set_surface_reg = r600_set_surface_reg,
	.clear_surface_reg = r600_clear_surface_reg,
	.bandwidth_update = &rs690_bandwidth_update,
	.hpd_init = &r600_hpd_init,
	.hpd_fini = &r600_hpd_fini,
	.hpd_sense = &r600_hpd_sense,
	.hpd_set_polarity = &r600_hpd_set_polarity,
	.ioctl_wait_idle = r600_ioctl_wait_idle,
	.gui_idle = &r600_gui_idle,
	.pm_misc = &r600_pm_misc,
	.pm_prepare = &rs600_pm_prepare,
	.pm_finish = &rs600_pm_finish,
	.pm_init_profile = &rs780_pm_init_profile,
	.pm_get_dynpm_state = &r600_pm_get_dynpm_state,
	.pre_page_flip = &rs600_pre_page_flip,
	.page_flip = &rs600_page_flip,
	.post_page_flip = &rs600_post_page_flip,
};

static struct radeon_asic rv770_asic = {
	.init = &rv770_init,
	.fini = &rv770_fini,
	.suspend = &rv770_suspend,
	.resume = &rv770_resume,
	.cp_commit = &r600_cp_commit,
	.asic_reset = &r600_asic_reset,
	.gpu_is_lockup = &r600_gpu_is_lockup,
	.vga_set_state = &r600_vga_set_state,
	.gart_tlb_flush = &r600_pcie_gart_tlb_flush,
	.gart_set_page = &rs600_gart_set_page,
	.ring_test = &r600_ring_test,
	.ring_ib_execute = &r600_ring_ib_execute,
	.irq_set = &r600_irq_set,
	.irq_process = &r600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r600_fence_ring_emit,
	.cs_parse = &r600_cs_parse,
	.copy_blit = &r600_copy_blit,
	.copy_dma = &r600_copy_blit,
	.copy = &r600_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.get_pcie_lanes = &r600_get_pcie_lanes,
	.set_pcie_lanes = &r600_set_pcie_lanes,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r600_set_surface_reg,
	.clear_surface_reg = r600_clear_surface_reg,
	.bandwidth_update = &rv515_bandwidth_update,
	.hpd_init = &r600_hpd_init,
	.hpd_fini = &r600_hpd_fini,
	.hpd_sense = &r600_hpd_sense,
	.hpd_set_polarity = &r600_hpd_set_polarity,
	.ioctl_wait_idle = r600_ioctl_wait_idle,
	.gui_idle = &r600_gui_idle,
	.pm_misc = &rv770_pm_misc,
	.pm_prepare = &rs600_pm_prepare,
	.pm_finish = &rs600_pm_finish,
	.pm_init_profile = &r600_pm_init_profile,
	.pm_get_dynpm_state = &r600_pm_get_dynpm_state,
	.pre_page_flip = &rs600_pre_page_flip,
	.page_flip = &rv770_page_flip,
	.post_page_flip = &rs600_post_page_flip,
};

static struct radeon_asic evergreen_asic = {
	.init = &evergreen_init,
	.fini = &evergreen_fini,
	.suspend = &evergreen_suspend,
	.resume = &evergreen_resume,
	.cp_commit = &r600_cp_commit,
	.gpu_is_lockup = &evergreen_gpu_is_lockup,
	.asic_reset = &evergreen_asic_reset,
	.vga_set_state = &r600_vga_set_state,
	.gart_tlb_flush = &evergreen_pcie_gart_tlb_flush,
	.gart_set_page = &rs600_gart_set_page,
	.ring_test = &r600_ring_test,
	.ring_ib_execute = &evergreen_ring_ib_execute,
	.irq_set = &evergreen_irq_set,
	.irq_process = &evergreen_irq_process,
	.get_vblank_counter = &evergreen_get_vblank_counter,
	.fence_ring_emit = &r600_fence_ring_emit,
	.cs_parse = &evergreen_cs_parse,
	.copy_blit = &evergreen_copy_blit,
	.copy_dma = &evergreen_copy_blit,
	.copy = &evergreen_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.get_pcie_lanes = &r600_get_pcie_lanes,
	.set_pcie_lanes = &r600_set_pcie_lanes,
	.set_clock_gating = NULL,
	.set_surface_reg = r600_set_surface_reg,
	.clear_surface_reg = r600_clear_surface_reg,
	.bandwidth_update = &evergreen_bandwidth_update,
	.hpd_init = &evergreen_hpd_init,
	.hpd_fini = &evergreen_hpd_fini,
	.hpd_sense = &evergreen_hpd_sense,
	.hpd_set_polarity = &evergreen_hpd_set_polarity,
	.gui_idle = &r600_gui_idle,
	.pm_misc = &evergreen_pm_misc,
	.pm_prepare = &evergreen_pm_prepare,
	.pm_finish = &evergreen_pm_finish,
	.pm_init_profile = &r600_pm_init_profile,
	.pm_get_dynpm_state = &r600_pm_get_dynpm_state,
	.pre_page_flip = &evergreen_pre_page_flip,
	.page_flip = &evergreen_page_flip,
	.post_page_flip = &evergreen_post_page_flip,
};

static struct radeon_asic sumo_asic = {
	.init = &evergreen_init,
	.fini = &evergreen_fini,
	.suspend = &evergreen_suspend,
	.resume = &evergreen_resume,
	.cp_commit = &r600_cp_commit,
	.gpu_is_lockup = &evergreen_gpu_is_lockup,
	.asic_reset = &evergreen_asic_reset,
	.vga_set_state = &r600_vga_set_state,
	.gart_tlb_flush = &evergreen_pcie_gart_tlb_flush,
	.gart_set_page = &rs600_gart_set_page,
	.ring_test = &r600_ring_test,
	.ring_ib_execute = &evergreen_ring_ib_execute,
	.irq_set = &evergreen_irq_set,
	.irq_process = &evergreen_irq_process,
	.get_vblank_counter = &evergreen_get_vblank_counter,
	.fence_ring_emit = &r600_fence_ring_emit,
	.cs_parse = &evergreen_cs_parse,
	.copy_blit = &evergreen_copy_blit,
	.copy_dma = &evergreen_copy_blit,
	.copy = &evergreen_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = NULL,
	.set_memory_clock = NULL,
	.get_pcie_lanes = NULL,
	.set_pcie_lanes = NULL,
	.set_clock_gating = NULL,
	.set_surface_reg = r600_set_surface_reg,
	.clear_surface_reg = r600_clear_surface_reg,
	.bandwidth_update = &evergreen_bandwidth_update,
	.hpd_init = &evergreen_hpd_init,
	.hpd_fini = &evergreen_hpd_fini,
	.hpd_sense = &evergreen_hpd_sense,
	.hpd_set_polarity = &evergreen_hpd_set_polarity,
	.gui_idle = &r600_gui_idle,
	.pm_misc = &evergreen_pm_misc,
	.pm_prepare = &evergreen_pm_prepare,
	.pm_finish = &evergreen_pm_finish,
	.pm_init_profile = &rs780_pm_init_profile,
	.pm_get_dynpm_state = &r600_pm_get_dynpm_state,
	.pre_page_flip = &evergreen_pre_page_flip,
	.page_flip = &evergreen_page_flip,
	.post_page_flip = &evergreen_post_page_flip,
};

static struct radeon_asic btc_asic = {
	.init = &evergreen_init,
	.fini = &evergreen_fini,
	.suspend = &evergreen_suspend,
	.resume = &evergreen_resume,
	.cp_commit = &r600_cp_commit,
	.gpu_is_lockup = &evergreen_gpu_is_lockup,
	.asic_reset = &evergreen_asic_reset,
	.vga_set_state = &r600_vga_set_state,
	.gart_tlb_flush = &evergreen_pcie_gart_tlb_flush,
	.gart_set_page = &rs600_gart_set_page,
	.ring_test = &r600_ring_test,
	.ring_ib_execute = &evergreen_ring_ib_execute,
	.irq_set = &evergreen_irq_set,
	.irq_process = &evergreen_irq_process,
	.get_vblank_counter = &evergreen_get_vblank_counter,
	.fence_ring_emit = &r600_fence_ring_emit,
	.cs_parse = &evergreen_cs_parse,
	.copy_blit = &evergreen_copy_blit,
	.copy_dma = &evergreen_copy_blit,
	.copy = &evergreen_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.get_pcie_lanes = NULL,
	.set_pcie_lanes = NULL,
	.set_clock_gating = NULL,
	.set_surface_reg = r600_set_surface_reg,
	.clear_surface_reg = r600_clear_surface_reg,
	.bandwidth_update = &evergreen_bandwidth_update,
	.hpd_init = &evergreen_hpd_init,
	.hpd_fini = &evergreen_hpd_fini,
	.hpd_sense = &evergreen_hpd_sense,
	.hpd_set_polarity = &evergreen_hpd_set_polarity,
	.gui_idle = &r600_gui_idle,
	.pm_misc = &evergreen_pm_misc,
	.pm_prepare = &evergreen_pm_prepare,
	.pm_finish = &evergreen_pm_finish,
	.pm_init_profile = &r600_pm_init_profile,
	.pm_get_dynpm_state = &r600_pm_get_dynpm_state,
	.pre_page_flip = &evergreen_pre_page_flip,
	.page_flip = &evergreen_page_flip,
	.post_page_flip = &evergreen_post_page_flip,
};

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
		/* handle macs */
		if (rdev->bios == NULL) {
			rdev->asic->get_engine_clock = &radeon_legacy_get_engine_clock;
			rdev->asic->set_engine_clock = &radeon_legacy_set_engine_clock;
			rdev->asic->get_memory_clock = &radeon_legacy_get_memory_clock;
			rdev->asic->set_memory_clock = NULL;
		}
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
		rdev->asic = &r600_asic;
		break;
	case CHIP_RS780:
	case CHIP_RS880:
		rdev->asic = &rs780_asic;
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
	case CHIP_PALM:
		rdev->asic = &sumo_asic;
		break;
	case CHIP_BARTS:
	case CHIP_TURKS:
	case CHIP_CAICOS:
		rdev->asic = &btc_asic;
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	if (rdev->flags & RADEON_IS_IGP) {
		rdev->asic->get_memory_clock = NULL;
		rdev->asic->set_memory_clock = NULL;
	}

	/* set the number of crtcs */
	if (rdev->flags & RADEON_SINGLE_CRTC)
		rdev->num_crtc = 1;
	else {
		if (ASIC_IS_DCE41(rdev))
			rdev->num_crtc = 2;
		else if (ASIC_IS_DCE4(rdev))
			rdev->num_crtc = 6;
		else
			rdev->num_crtc = 2;
	}

	return 0;
}

