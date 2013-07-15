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
/**
 * radeon_invalid_rreg - dummy reg read function
 *
 * @rdev: radeon device pointer
 * @reg: offset of register
 *
 * Dummy register read function.  Used for register blocks
 * that certain asics don't have (all asics).
 * Returns the value in the register.
 */
static uint32_t radeon_invalid_rreg(struct radeon_device *rdev, uint32_t reg)
{
	DRM_ERROR("Invalid callback to read register 0x%04X\n", reg);
	BUG_ON(1);
	return 0;
}

/**
 * radeon_invalid_wreg - dummy reg write function
 *
 * @rdev: radeon device pointer
 * @reg: offset of register
 * @v: value to write to the register
 *
 * Dummy register read function.  Used for register blocks
 * that certain asics don't have (all asics).
 */
static void radeon_invalid_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	DRM_ERROR("Invalid callback to write register 0x%04X with 0x%08X\n",
		  reg, v);
	BUG_ON(1);
}

/**
 * radeon_register_accessor_init - sets up the register accessor callbacks
 *
 * @rdev: radeon device pointer
 *
 * Sets up the register accessor callbacks for various register
 * apertures.  Not all asics have all apertures (all asics).
 */
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
	if (rdev->family == CHIP_RS780 || rdev->family == CHIP_RS880) {
		rdev->mc_rreg = &rs780_mc_rreg;
		rdev->mc_wreg = &rs780_mc_wreg;
	}

	if (rdev->family >= CHIP_BONAIRE) {
		rdev->pciep_rreg = &cik_pciep_rreg;
		rdev->pciep_wreg = &cik_pciep_wreg;
	} else if (rdev->family >= CHIP_R600) {
		rdev->pciep_rreg = &r600_pciep_rreg;
		rdev->pciep_wreg = &r600_pciep_wreg;
	}
}


/* helper to disable agp */
/**
 * radeon_agp_disable - AGP disable helper function
 *
 * @rdev: radeon device pointer
 *
 * Removes AGP flags and changes the gart callbacks on AGP
 * cards when using the internal gart rather than AGP (all asics).
 */
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
		rdev->asic->gart.tlb_flush = &rv370_pcie_gart_tlb_flush;
		rdev->asic->gart.set_page = &rv370_pcie_gart_set_page;
	} else {
		DRM_INFO("Forcing AGP to PCI mode\n");
		rdev->flags |= RADEON_IS_PCI;
		rdev->asic->gart.tlb_flush = &r100_pci_gart_tlb_flush;
		rdev->asic->gart.set_page = &r100_pci_gart_set_page;
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
	.asic_reset = &r100_asic_reset,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.mc_wait_for_idle = &r100_mc_wait_for_idle,
	.gart = {
		.tlb_flush = &r100_pci_gart_tlb_flush,
		.set_page = &r100_pci_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &r100_ring_ib_execute,
			.emit_fence = &r100_fence_ring_emit,
			.emit_semaphore = &r100_semaphore_ring_emit,
			.cs_parse = &r100_cs_parse,
			.ring_start = &r100_ring_start,
			.ring_test = &r100_ring_test,
			.ib_test = &r100_ib_test,
			.is_lockup = &r100_gpu_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &r100_irq_set,
		.process = &r100_irq_process,
	},
	.display = {
		.bandwidth_update = &r100_bandwidth_update,
		.get_vblank_counter = &r100_get_vblank_counter,
		.wait_for_vblank = &r100_wait_for_vblank,
		.set_backlight_level = &radeon_legacy_set_backlight_level,
		.get_backlight_level = &radeon_legacy_get_backlight_level,
	},
	.copy = {
		.blit = &r100_copy_blit,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = NULL,
		.dma_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.copy = &r100_copy_blit,
		.copy_ring_index = RADEON_RING_TYPE_GFX_INDEX,
	},
	.surface = {
		.set_reg = r100_set_surface_reg,
		.clear_reg = r100_clear_surface_reg,
	},
	.hpd = {
		.init = &r100_hpd_init,
		.fini = &r100_hpd_fini,
		.sense = &r100_hpd_sense,
		.set_polarity = &r100_hpd_set_polarity,
	},
	.pm = {
		.misc = &r100_pm_misc,
		.prepare = &r100_pm_prepare,
		.finish = &r100_pm_finish,
		.init_profile = &r100_pm_init_profile,
		.get_dynpm_state = &r100_pm_get_dynpm_state,
		.get_engine_clock = &radeon_legacy_get_engine_clock,
		.set_engine_clock = &radeon_legacy_set_engine_clock,
		.get_memory_clock = &radeon_legacy_get_memory_clock,
		.set_memory_clock = NULL,
		.get_pcie_lanes = NULL,
		.set_pcie_lanes = NULL,
		.set_clock_gating = &radeon_legacy_set_clock_gating,
	},
	.pflip = {
		.pre_page_flip = &r100_pre_page_flip,
		.page_flip = &r100_page_flip,
		.post_page_flip = &r100_post_page_flip,
	},
};

static struct radeon_asic r200_asic = {
	.init = &r100_init,
	.fini = &r100_fini,
	.suspend = &r100_suspend,
	.resume = &r100_resume,
	.vga_set_state = &r100_vga_set_state,
	.asic_reset = &r100_asic_reset,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.mc_wait_for_idle = &r100_mc_wait_for_idle,
	.gart = {
		.tlb_flush = &r100_pci_gart_tlb_flush,
		.set_page = &r100_pci_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &r100_ring_ib_execute,
			.emit_fence = &r100_fence_ring_emit,
			.emit_semaphore = &r100_semaphore_ring_emit,
			.cs_parse = &r100_cs_parse,
			.ring_start = &r100_ring_start,
			.ring_test = &r100_ring_test,
			.ib_test = &r100_ib_test,
			.is_lockup = &r100_gpu_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &r100_irq_set,
		.process = &r100_irq_process,
	},
	.display = {
		.bandwidth_update = &r100_bandwidth_update,
		.get_vblank_counter = &r100_get_vblank_counter,
		.wait_for_vblank = &r100_wait_for_vblank,
		.set_backlight_level = &radeon_legacy_set_backlight_level,
		.get_backlight_level = &radeon_legacy_get_backlight_level,
	},
	.copy = {
		.blit = &r100_copy_blit,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &r200_copy_dma,
		.dma_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.copy = &r100_copy_blit,
		.copy_ring_index = RADEON_RING_TYPE_GFX_INDEX,
	},
	.surface = {
		.set_reg = r100_set_surface_reg,
		.clear_reg = r100_clear_surface_reg,
	},
	.hpd = {
		.init = &r100_hpd_init,
		.fini = &r100_hpd_fini,
		.sense = &r100_hpd_sense,
		.set_polarity = &r100_hpd_set_polarity,
	},
	.pm = {
		.misc = &r100_pm_misc,
		.prepare = &r100_pm_prepare,
		.finish = &r100_pm_finish,
		.init_profile = &r100_pm_init_profile,
		.get_dynpm_state = &r100_pm_get_dynpm_state,
		.get_engine_clock = &radeon_legacy_get_engine_clock,
		.set_engine_clock = &radeon_legacy_set_engine_clock,
		.get_memory_clock = &radeon_legacy_get_memory_clock,
		.set_memory_clock = NULL,
		.get_pcie_lanes = NULL,
		.set_pcie_lanes = NULL,
		.set_clock_gating = &radeon_legacy_set_clock_gating,
	},
	.pflip = {
		.pre_page_flip = &r100_pre_page_flip,
		.page_flip = &r100_page_flip,
		.post_page_flip = &r100_post_page_flip,
	},
};

static struct radeon_asic r300_asic = {
	.init = &r300_init,
	.fini = &r300_fini,
	.suspend = &r300_suspend,
	.resume = &r300_resume,
	.vga_set_state = &r100_vga_set_state,
	.asic_reset = &r300_asic_reset,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.mc_wait_for_idle = &r300_mc_wait_for_idle,
	.gart = {
		.tlb_flush = &r100_pci_gart_tlb_flush,
		.set_page = &r100_pci_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &r100_ring_ib_execute,
			.emit_fence = &r300_fence_ring_emit,
			.emit_semaphore = &r100_semaphore_ring_emit,
			.cs_parse = &r300_cs_parse,
			.ring_start = &r300_ring_start,
			.ring_test = &r100_ring_test,
			.ib_test = &r100_ib_test,
			.is_lockup = &r100_gpu_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &r100_irq_set,
		.process = &r100_irq_process,
	},
	.display = {
		.bandwidth_update = &r100_bandwidth_update,
		.get_vblank_counter = &r100_get_vblank_counter,
		.wait_for_vblank = &r100_wait_for_vblank,
		.set_backlight_level = &radeon_legacy_set_backlight_level,
		.get_backlight_level = &radeon_legacy_get_backlight_level,
	},
	.copy = {
		.blit = &r100_copy_blit,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &r200_copy_dma,
		.dma_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.copy = &r100_copy_blit,
		.copy_ring_index = RADEON_RING_TYPE_GFX_INDEX,
	},
	.surface = {
		.set_reg = r100_set_surface_reg,
		.clear_reg = r100_clear_surface_reg,
	},
	.hpd = {
		.init = &r100_hpd_init,
		.fini = &r100_hpd_fini,
		.sense = &r100_hpd_sense,
		.set_polarity = &r100_hpd_set_polarity,
	},
	.pm = {
		.misc = &r100_pm_misc,
		.prepare = &r100_pm_prepare,
		.finish = &r100_pm_finish,
		.init_profile = &r100_pm_init_profile,
		.get_dynpm_state = &r100_pm_get_dynpm_state,
		.get_engine_clock = &radeon_legacy_get_engine_clock,
		.set_engine_clock = &radeon_legacy_set_engine_clock,
		.get_memory_clock = &radeon_legacy_get_memory_clock,
		.set_memory_clock = NULL,
		.get_pcie_lanes = &rv370_get_pcie_lanes,
		.set_pcie_lanes = &rv370_set_pcie_lanes,
		.set_clock_gating = &radeon_legacy_set_clock_gating,
	},
	.pflip = {
		.pre_page_flip = &r100_pre_page_flip,
		.page_flip = &r100_page_flip,
		.post_page_flip = &r100_post_page_flip,
	},
};

static struct radeon_asic r300_asic_pcie = {
	.init = &r300_init,
	.fini = &r300_fini,
	.suspend = &r300_suspend,
	.resume = &r300_resume,
	.vga_set_state = &r100_vga_set_state,
	.asic_reset = &r300_asic_reset,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.mc_wait_for_idle = &r300_mc_wait_for_idle,
	.gart = {
		.tlb_flush = &rv370_pcie_gart_tlb_flush,
		.set_page = &rv370_pcie_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &r100_ring_ib_execute,
			.emit_fence = &r300_fence_ring_emit,
			.emit_semaphore = &r100_semaphore_ring_emit,
			.cs_parse = &r300_cs_parse,
			.ring_start = &r300_ring_start,
			.ring_test = &r100_ring_test,
			.ib_test = &r100_ib_test,
			.is_lockup = &r100_gpu_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &r100_irq_set,
		.process = &r100_irq_process,
	},
	.display = {
		.bandwidth_update = &r100_bandwidth_update,
		.get_vblank_counter = &r100_get_vblank_counter,
		.wait_for_vblank = &r100_wait_for_vblank,
		.set_backlight_level = &radeon_legacy_set_backlight_level,
		.get_backlight_level = &radeon_legacy_get_backlight_level,
	},
	.copy = {
		.blit = &r100_copy_blit,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &r200_copy_dma,
		.dma_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.copy = &r100_copy_blit,
		.copy_ring_index = RADEON_RING_TYPE_GFX_INDEX,
	},
	.surface = {
		.set_reg = r100_set_surface_reg,
		.clear_reg = r100_clear_surface_reg,
	},
	.hpd = {
		.init = &r100_hpd_init,
		.fini = &r100_hpd_fini,
		.sense = &r100_hpd_sense,
		.set_polarity = &r100_hpd_set_polarity,
	},
	.pm = {
		.misc = &r100_pm_misc,
		.prepare = &r100_pm_prepare,
		.finish = &r100_pm_finish,
		.init_profile = &r100_pm_init_profile,
		.get_dynpm_state = &r100_pm_get_dynpm_state,
		.get_engine_clock = &radeon_legacy_get_engine_clock,
		.set_engine_clock = &radeon_legacy_set_engine_clock,
		.get_memory_clock = &radeon_legacy_get_memory_clock,
		.set_memory_clock = NULL,
		.get_pcie_lanes = &rv370_get_pcie_lanes,
		.set_pcie_lanes = &rv370_set_pcie_lanes,
		.set_clock_gating = &radeon_legacy_set_clock_gating,
	},
	.pflip = {
		.pre_page_flip = &r100_pre_page_flip,
		.page_flip = &r100_page_flip,
		.post_page_flip = &r100_post_page_flip,
	},
};

static struct radeon_asic r420_asic = {
	.init = &r420_init,
	.fini = &r420_fini,
	.suspend = &r420_suspend,
	.resume = &r420_resume,
	.vga_set_state = &r100_vga_set_state,
	.asic_reset = &r300_asic_reset,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.mc_wait_for_idle = &r300_mc_wait_for_idle,
	.gart = {
		.tlb_flush = &rv370_pcie_gart_tlb_flush,
		.set_page = &rv370_pcie_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &r100_ring_ib_execute,
			.emit_fence = &r300_fence_ring_emit,
			.emit_semaphore = &r100_semaphore_ring_emit,
			.cs_parse = &r300_cs_parse,
			.ring_start = &r300_ring_start,
			.ring_test = &r100_ring_test,
			.ib_test = &r100_ib_test,
			.is_lockup = &r100_gpu_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &r100_irq_set,
		.process = &r100_irq_process,
	},
	.display = {
		.bandwidth_update = &r100_bandwidth_update,
		.get_vblank_counter = &r100_get_vblank_counter,
		.wait_for_vblank = &r100_wait_for_vblank,
		.set_backlight_level = &atombios_set_backlight_level,
		.get_backlight_level = &atombios_get_backlight_level,
	},
	.copy = {
		.blit = &r100_copy_blit,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &r200_copy_dma,
		.dma_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.copy = &r100_copy_blit,
		.copy_ring_index = RADEON_RING_TYPE_GFX_INDEX,
	},
	.surface = {
		.set_reg = r100_set_surface_reg,
		.clear_reg = r100_clear_surface_reg,
	},
	.hpd = {
		.init = &r100_hpd_init,
		.fini = &r100_hpd_fini,
		.sense = &r100_hpd_sense,
		.set_polarity = &r100_hpd_set_polarity,
	},
	.pm = {
		.misc = &r100_pm_misc,
		.prepare = &r100_pm_prepare,
		.finish = &r100_pm_finish,
		.init_profile = &r420_pm_init_profile,
		.get_dynpm_state = &r100_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = &radeon_atom_get_memory_clock,
		.set_memory_clock = &radeon_atom_set_memory_clock,
		.get_pcie_lanes = &rv370_get_pcie_lanes,
		.set_pcie_lanes = &rv370_set_pcie_lanes,
		.set_clock_gating = &radeon_atom_set_clock_gating,
	},
	.pflip = {
		.pre_page_flip = &r100_pre_page_flip,
		.page_flip = &r100_page_flip,
		.post_page_flip = &r100_post_page_flip,
	},
};

static struct radeon_asic rs400_asic = {
	.init = &rs400_init,
	.fini = &rs400_fini,
	.suspend = &rs400_suspend,
	.resume = &rs400_resume,
	.vga_set_state = &r100_vga_set_state,
	.asic_reset = &r300_asic_reset,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.mc_wait_for_idle = &rs400_mc_wait_for_idle,
	.gart = {
		.tlb_flush = &rs400_gart_tlb_flush,
		.set_page = &rs400_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &r100_ring_ib_execute,
			.emit_fence = &r300_fence_ring_emit,
			.emit_semaphore = &r100_semaphore_ring_emit,
			.cs_parse = &r300_cs_parse,
			.ring_start = &r300_ring_start,
			.ring_test = &r100_ring_test,
			.ib_test = &r100_ib_test,
			.is_lockup = &r100_gpu_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &r100_irq_set,
		.process = &r100_irq_process,
	},
	.display = {
		.bandwidth_update = &r100_bandwidth_update,
		.get_vblank_counter = &r100_get_vblank_counter,
		.wait_for_vblank = &r100_wait_for_vblank,
		.set_backlight_level = &radeon_legacy_set_backlight_level,
		.get_backlight_level = &radeon_legacy_get_backlight_level,
	},
	.copy = {
		.blit = &r100_copy_blit,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &r200_copy_dma,
		.dma_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.copy = &r100_copy_blit,
		.copy_ring_index = RADEON_RING_TYPE_GFX_INDEX,
	},
	.surface = {
		.set_reg = r100_set_surface_reg,
		.clear_reg = r100_clear_surface_reg,
	},
	.hpd = {
		.init = &r100_hpd_init,
		.fini = &r100_hpd_fini,
		.sense = &r100_hpd_sense,
		.set_polarity = &r100_hpd_set_polarity,
	},
	.pm = {
		.misc = &r100_pm_misc,
		.prepare = &r100_pm_prepare,
		.finish = &r100_pm_finish,
		.init_profile = &r100_pm_init_profile,
		.get_dynpm_state = &r100_pm_get_dynpm_state,
		.get_engine_clock = &radeon_legacy_get_engine_clock,
		.set_engine_clock = &radeon_legacy_set_engine_clock,
		.get_memory_clock = &radeon_legacy_get_memory_clock,
		.set_memory_clock = NULL,
		.get_pcie_lanes = NULL,
		.set_pcie_lanes = NULL,
		.set_clock_gating = &radeon_legacy_set_clock_gating,
	},
	.pflip = {
		.pre_page_flip = &r100_pre_page_flip,
		.page_flip = &r100_page_flip,
		.post_page_flip = &r100_post_page_flip,
	},
};

static struct radeon_asic rs600_asic = {
	.init = &rs600_init,
	.fini = &rs600_fini,
	.suspend = &rs600_suspend,
	.resume = &rs600_resume,
	.vga_set_state = &r100_vga_set_state,
	.asic_reset = &rs600_asic_reset,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.mc_wait_for_idle = &rs600_mc_wait_for_idle,
	.gart = {
		.tlb_flush = &rs600_gart_tlb_flush,
		.set_page = &rs600_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &r100_ring_ib_execute,
			.emit_fence = &r300_fence_ring_emit,
			.emit_semaphore = &r100_semaphore_ring_emit,
			.cs_parse = &r300_cs_parse,
			.ring_start = &r300_ring_start,
			.ring_test = &r100_ring_test,
			.ib_test = &r100_ib_test,
			.is_lockup = &r100_gpu_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &rs600_irq_set,
		.process = &rs600_irq_process,
	},
	.display = {
		.bandwidth_update = &rs600_bandwidth_update,
		.get_vblank_counter = &rs600_get_vblank_counter,
		.wait_for_vblank = &avivo_wait_for_vblank,
		.set_backlight_level = &atombios_set_backlight_level,
		.get_backlight_level = &atombios_get_backlight_level,
		.hdmi_enable = &r600_hdmi_enable,
		.hdmi_setmode = &r600_hdmi_setmode,
	},
	.copy = {
		.blit = &r100_copy_blit,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &r200_copy_dma,
		.dma_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.copy = &r100_copy_blit,
		.copy_ring_index = RADEON_RING_TYPE_GFX_INDEX,
	},
	.surface = {
		.set_reg = r100_set_surface_reg,
		.clear_reg = r100_clear_surface_reg,
	},
	.hpd = {
		.init = &rs600_hpd_init,
		.fini = &rs600_hpd_fini,
		.sense = &rs600_hpd_sense,
		.set_polarity = &rs600_hpd_set_polarity,
	},
	.pm = {
		.misc = &rs600_pm_misc,
		.prepare = &rs600_pm_prepare,
		.finish = &rs600_pm_finish,
		.init_profile = &r420_pm_init_profile,
		.get_dynpm_state = &r100_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = &radeon_atom_get_memory_clock,
		.set_memory_clock = &radeon_atom_set_memory_clock,
		.get_pcie_lanes = NULL,
		.set_pcie_lanes = NULL,
		.set_clock_gating = &radeon_atom_set_clock_gating,
	},
	.pflip = {
		.pre_page_flip = &rs600_pre_page_flip,
		.page_flip = &rs600_page_flip,
		.post_page_flip = &rs600_post_page_flip,
	},
};

static struct radeon_asic rs690_asic = {
	.init = &rs690_init,
	.fini = &rs690_fini,
	.suspend = &rs690_suspend,
	.resume = &rs690_resume,
	.vga_set_state = &r100_vga_set_state,
	.asic_reset = &rs600_asic_reset,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.mc_wait_for_idle = &rs690_mc_wait_for_idle,
	.gart = {
		.tlb_flush = &rs400_gart_tlb_flush,
		.set_page = &rs400_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &r100_ring_ib_execute,
			.emit_fence = &r300_fence_ring_emit,
			.emit_semaphore = &r100_semaphore_ring_emit,
			.cs_parse = &r300_cs_parse,
			.ring_start = &r300_ring_start,
			.ring_test = &r100_ring_test,
			.ib_test = &r100_ib_test,
			.is_lockup = &r100_gpu_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &rs600_irq_set,
		.process = &rs600_irq_process,
	},
	.display = {
		.get_vblank_counter = &rs600_get_vblank_counter,
		.bandwidth_update = &rs690_bandwidth_update,
		.wait_for_vblank = &avivo_wait_for_vblank,
		.set_backlight_level = &atombios_set_backlight_level,
		.get_backlight_level = &atombios_get_backlight_level,
		.hdmi_enable = &r600_hdmi_enable,
		.hdmi_setmode = &r600_hdmi_setmode,
	},
	.copy = {
		.blit = &r100_copy_blit,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &r200_copy_dma,
		.dma_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.copy = &r200_copy_dma,
		.copy_ring_index = RADEON_RING_TYPE_GFX_INDEX,
	},
	.surface = {
		.set_reg = r100_set_surface_reg,
		.clear_reg = r100_clear_surface_reg,
	},
	.hpd = {
		.init = &rs600_hpd_init,
		.fini = &rs600_hpd_fini,
		.sense = &rs600_hpd_sense,
		.set_polarity = &rs600_hpd_set_polarity,
	},
	.pm = {
		.misc = &rs600_pm_misc,
		.prepare = &rs600_pm_prepare,
		.finish = &rs600_pm_finish,
		.init_profile = &r420_pm_init_profile,
		.get_dynpm_state = &r100_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = &radeon_atom_get_memory_clock,
		.set_memory_clock = &radeon_atom_set_memory_clock,
		.get_pcie_lanes = NULL,
		.set_pcie_lanes = NULL,
		.set_clock_gating = &radeon_atom_set_clock_gating,
	},
	.pflip = {
		.pre_page_flip = &rs600_pre_page_flip,
		.page_flip = &rs600_page_flip,
		.post_page_flip = &rs600_post_page_flip,
	},
};

static struct radeon_asic rv515_asic = {
	.init = &rv515_init,
	.fini = &rv515_fini,
	.suspend = &rv515_suspend,
	.resume = &rv515_resume,
	.vga_set_state = &r100_vga_set_state,
	.asic_reset = &rs600_asic_reset,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.mc_wait_for_idle = &rv515_mc_wait_for_idle,
	.gart = {
		.tlb_flush = &rv370_pcie_gart_tlb_flush,
		.set_page = &rv370_pcie_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &r100_ring_ib_execute,
			.emit_fence = &r300_fence_ring_emit,
			.emit_semaphore = &r100_semaphore_ring_emit,
			.cs_parse = &r300_cs_parse,
			.ring_start = &rv515_ring_start,
			.ring_test = &r100_ring_test,
			.ib_test = &r100_ib_test,
			.is_lockup = &r100_gpu_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &rs600_irq_set,
		.process = &rs600_irq_process,
	},
	.display = {
		.get_vblank_counter = &rs600_get_vblank_counter,
		.bandwidth_update = &rv515_bandwidth_update,
		.wait_for_vblank = &avivo_wait_for_vblank,
		.set_backlight_level = &atombios_set_backlight_level,
		.get_backlight_level = &atombios_get_backlight_level,
	},
	.copy = {
		.blit = &r100_copy_blit,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &r200_copy_dma,
		.dma_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.copy = &r100_copy_blit,
		.copy_ring_index = RADEON_RING_TYPE_GFX_INDEX,
	},
	.surface = {
		.set_reg = r100_set_surface_reg,
		.clear_reg = r100_clear_surface_reg,
	},
	.hpd = {
		.init = &rs600_hpd_init,
		.fini = &rs600_hpd_fini,
		.sense = &rs600_hpd_sense,
		.set_polarity = &rs600_hpd_set_polarity,
	},
	.pm = {
		.misc = &rs600_pm_misc,
		.prepare = &rs600_pm_prepare,
		.finish = &rs600_pm_finish,
		.init_profile = &r420_pm_init_profile,
		.get_dynpm_state = &r100_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = &radeon_atom_get_memory_clock,
		.set_memory_clock = &radeon_atom_set_memory_clock,
		.get_pcie_lanes = &rv370_get_pcie_lanes,
		.set_pcie_lanes = &rv370_set_pcie_lanes,
		.set_clock_gating = &radeon_atom_set_clock_gating,
	},
	.pflip = {
		.pre_page_flip = &rs600_pre_page_flip,
		.page_flip = &rs600_page_flip,
		.post_page_flip = &rs600_post_page_flip,
	},
};

static struct radeon_asic r520_asic = {
	.init = &r520_init,
	.fini = &rv515_fini,
	.suspend = &rv515_suspend,
	.resume = &r520_resume,
	.vga_set_state = &r100_vga_set_state,
	.asic_reset = &rs600_asic_reset,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r100_gui_idle,
	.mc_wait_for_idle = &r520_mc_wait_for_idle,
	.gart = {
		.tlb_flush = &rv370_pcie_gart_tlb_flush,
		.set_page = &rv370_pcie_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &r100_ring_ib_execute,
			.emit_fence = &r300_fence_ring_emit,
			.emit_semaphore = &r100_semaphore_ring_emit,
			.cs_parse = &r300_cs_parse,
			.ring_start = &rv515_ring_start,
			.ring_test = &r100_ring_test,
			.ib_test = &r100_ib_test,
			.is_lockup = &r100_gpu_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &rs600_irq_set,
		.process = &rs600_irq_process,
	},
	.display = {
		.bandwidth_update = &rv515_bandwidth_update,
		.get_vblank_counter = &rs600_get_vblank_counter,
		.wait_for_vblank = &avivo_wait_for_vblank,
		.set_backlight_level = &atombios_set_backlight_level,
		.get_backlight_level = &atombios_get_backlight_level,
	},
	.copy = {
		.blit = &r100_copy_blit,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &r200_copy_dma,
		.dma_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.copy = &r100_copy_blit,
		.copy_ring_index = RADEON_RING_TYPE_GFX_INDEX,
	},
	.surface = {
		.set_reg = r100_set_surface_reg,
		.clear_reg = r100_clear_surface_reg,
	},
	.hpd = {
		.init = &rs600_hpd_init,
		.fini = &rs600_hpd_fini,
		.sense = &rs600_hpd_sense,
		.set_polarity = &rs600_hpd_set_polarity,
	},
	.pm = {
		.misc = &rs600_pm_misc,
		.prepare = &rs600_pm_prepare,
		.finish = &rs600_pm_finish,
		.init_profile = &r420_pm_init_profile,
		.get_dynpm_state = &r100_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = &radeon_atom_get_memory_clock,
		.set_memory_clock = &radeon_atom_set_memory_clock,
		.get_pcie_lanes = &rv370_get_pcie_lanes,
		.set_pcie_lanes = &rv370_set_pcie_lanes,
		.set_clock_gating = &radeon_atom_set_clock_gating,
	},
	.pflip = {
		.pre_page_flip = &rs600_pre_page_flip,
		.page_flip = &rs600_page_flip,
		.post_page_flip = &rs600_post_page_flip,
	},
};

static struct radeon_asic r600_asic = {
	.init = &r600_init,
	.fini = &r600_fini,
	.suspend = &r600_suspend,
	.resume = &r600_resume,
	.vga_set_state = &r600_vga_set_state,
	.asic_reset = &r600_asic_reset,
	.ioctl_wait_idle = r600_ioctl_wait_idle,
	.gui_idle = &r600_gui_idle,
	.mc_wait_for_idle = &r600_mc_wait_for_idle,
	.get_xclk = &r600_get_xclk,
	.get_gpu_clock_counter = &r600_get_gpu_clock_counter,
	.gart = {
		.tlb_flush = &r600_pcie_gart_tlb_flush,
		.set_page = &rs600_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &r600_ring_ib_execute,
			.emit_fence = &r600_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = &r600_cs_parse,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &r600_gfx_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_DMA_INDEX] = {
			.ib_execute = &r600_dma_ring_ib_execute,
			.emit_fence = &r600_dma_fence_ring_emit,
			.emit_semaphore = &r600_dma_semaphore_ring_emit,
			.cs_parse = &r600_dma_cs_parse,
			.ring_test = &r600_dma_ring_test,
			.ib_test = &r600_dma_ib_test,
			.is_lockup = &r600_dma_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &r600_irq_set,
		.process = &r600_irq_process,
	},
	.display = {
		.bandwidth_update = &rv515_bandwidth_update,
		.get_vblank_counter = &rs600_get_vblank_counter,
		.wait_for_vblank = &avivo_wait_for_vblank,
		.set_backlight_level = &atombios_set_backlight_level,
		.get_backlight_level = &atombios_get_backlight_level,
		.hdmi_enable = &r600_hdmi_enable,
		.hdmi_setmode = &r600_hdmi_setmode,
	},
	.copy = {
		.blit = &r600_copy_cpdma,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &r600_copy_dma,
		.dma_ring_index = R600_RING_TYPE_DMA_INDEX,
		.copy = &r600_copy_cpdma,
		.copy_ring_index = RADEON_RING_TYPE_GFX_INDEX,
	},
	.surface = {
		.set_reg = r600_set_surface_reg,
		.clear_reg = r600_clear_surface_reg,
	},
	.hpd = {
		.init = &r600_hpd_init,
		.fini = &r600_hpd_fini,
		.sense = &r600_hpd_sense,
		.set_polarity = &r600_hpd_set_polarity,
	},
	.pm = {
		.misc = &r600_pm_misc,
		.prepare = &rs600_pm_prepare,
		.finish = &rs600_pm_finish,
		.init_profile = &r600_pm_init_profile,
		.get_dynpm_state = &r600_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = &radeon_atom_get_memory_clock,
		.set_memory_clock = &radeon_atom_set_memory_clock,
		.get_pcie_lanes = &r600_get_pcie_lanes,
		.set_pcie_lanes = &r600_set_pcie_lanes,
		.set_clock_gating = NULL,
		.get_temperature = &rv6xx_get_temp,
	},
	.pflip = {
		.pre_page_flip = &rs600_pre_page_flip,
		.page_flip = &rs600_page_flip,
		.post_page_flip = &rs600_post_page_flip,
	},
};

static struct radeon_asic rv6xx_asic = {
	.init = &r600_init,
	.fini = &r600_fini,
	.suspend = &r600_suspend,
	.resume = &r600_resume,
	.vga_set_state = &r600_vga_set_state,
	.asic_reset = &r600_asic_reset,
	.ioctl_wait_idle = r600_ioctl_wait_idle,
	.gui_idle = &r600_gui_idle,
	.mc_wait_for_idle = &r600_mc_wait_for_idle,
	.get_xclk = &r600_get_xclk,
	.get_gpu_clock_counter = &r600_get_gpu_clock_counter,
	.gart = {
		.tlb_flush = &r600_pcie_gart_tlb_flush,
		.set_page = &rs600_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &r600_ring_ib_execute,
			.emit_fence = &r600_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = &r600_cs_parse,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &r600_gfx_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_DMA_INDEX] = {
			.ib_execute = &r600_dma_ring_ib_execute,
			.emit_fence = &r600_dma_fence_ring_emit,
			.emit_semaphore = &r600_dma_semaphore_ring_emit,
			.cs_parse = &r600_dma_cs_parse,
			.ring_test = &r600_dma_ring_test,
			.ib_test = &r600_dma_ib_test,
			.is_lockup = &r600_dma_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &r600_irq_set,
		.process = &r600_irq_process,
	},
	.display = {
		.bandwidth_update = &rv515_bandwidth_update,
		.get_vblank_counter = &rs600_get_vblank_counter,
		.wait_for_vblank = &avivo_wait_for_vblank,
		.set_backlight_level = &atombios_set_backlight_level,
		.get_backlight_level = &atombios_get_backlight_level,
	},
	.copy = {
		.blit = &r600_copy_cpdma,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &r600_copy_dma,
		.dma_ring_index = R600_RING_TYPE_DMA_INDEX,
		.copy = &r600_copy_cpdma,
		.copy_ring_index = RADEON_RING_TYPE_GFX_INDEX,
	},
	.surface = {
		.set_reg = r600_set_surface_reg,
		.clear_reg = r600_clear_surface_reg,
	},
	.hpd = {
		.init = &r600_hpd_init,
		.fini = &r600_hpd_fini,
		.sense = &r600_hpd_sense,
		.set_polarity = &r600_hpd_set_polarity,
	},
	.pm = {
		.misc = &r600_pm_misc,
		.prepare = &rs600_pm_prepare,
		.finish = &rs600_pm_finish,
		.init_profile = &r600_pm_init_profile,
		.get_dynpm_state = &r600_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = &radeon_atom_get_memory_clock,
		.set_memory_clock = &radeon_atom_set_memory_clock,
		.get_pcie_lanes = &r600_get_pcie_lanes,
		.set_pcie_lanes = &r600_set_pcie_lanes,
		.set_clock_gating = NULL,
		.get_temperature = &rv6xx_get_temp,
	},
	.dpm = {
		.init = &rv6xx_dpm_init,
		.setup_asic = &rv6xx_setup_asic,
		.enable = &rv6xx_dpm_enable,
		.disable = &rv6xx_dpm_disable,
		.pre_set_power_state = &r600_dpm_pre_set_power_state,
		.set_power_state = &rv6xx_dpm_set_power_state,
		.post_set_power_state = &r600_dpm_post_set_power_state,
		.display_configuration_changed = &rv6xx_dpm_display_configuration_changed,
		.fini = &rv6xx_dpm_fini,
		.get_sclk = &rv6xx_dpm_get_sclk,
		.get_mclk = &rv6xx_dpm_get_mclk,
		.print_power_state = &rv6xx_dpm_print_power_state,
		.debugfs_print_current_performance_level = &rv6xx_dpm_debugfs_print_current_performance_level,
		.force_performance_level = &rv6xx_dpm_force_performance_level,
	},
	.pflip = {
		.pre_page_flip = &rs600_pre_page_flip,
		.page_flip = &rs600_page_flip,
		.post_page_flip = &rs600_post_page_flip,
	},
};

static struct radeon_asic rs780_asic = {
	.init = &r600_init,
	.fini = &r600_fini,
	.suspend = &r600_suspend,
	.resume = &r600_resume,
	.vga_set_state = &r600_vga_set_state,
	.asic_reset = &r600_asic_reset,
	.ioctl_wait_idle = r600_ioctl_wait_idle,
	.gui_idle = &r600_gui_idle,
	.mc_wait_for_idle = &r600_mc_wait_for_idle,
	.get_xclk = &r600_get_xclk,
	.get_gpu_clock_counter = &r600_get_gpu_clock_counter,
	.gart = {
		.tlb_flush = &r600_pcie_gart_tlb_flush,
		.set_page = &rs600_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &r600_ring_ib_execute,
			.emit_fence = &r600_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = &r600_cs_parse,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &r600_gfx_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_DMA_INDEX] = {
			.ib_execute = &r600_dma_ring_ib_execute,
			.emit_fence = &r600_dma_fence_ring_emit,
			.emit_semaphore = &r600_dma_semaphore_ring_emit,
			.cs_parse = &r600_dma_cs_parse,
			.ring_test = &r600_dma_ring_test,
			.ib_test = &r600_dma_ib_test,
			.is_lockup = &r600_dma_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &r600_irq_set,
		.process = &r600_irq_process,
	},
	.display = {
		.bandwidth_update = &rs690_bandwidth_update,
		.get_vblank_counter = &rs600_get_vblank_counter,
		.wait_for_vblank = &avivo_wait_for_vblank,
		.set_backlight_level = &atombios_set_backlight_level,
		.get_backlight_level = &atombios_get_backlight_level,
		.hdmi_enable = &r600_hdmi_enable,
		.hdmi_setmode = &r600_hdmi_setmode,
	},
	.copy = {
		.blit = &r600_copy_cpdma,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &r600_copy_dma,
		.dma_ring_index = R600_RING_TYPE_DMA_INDEX,
		.copy = &r600_copy_cpdma,
		.copy_ring_index = RADEON_RING_TYPE_GFX_INDEX,
	},
	.surface = {
		.set_reg = r600_set_surface_reg,
		.clear_reg = r600_clear_surface_reg,
	},
	.hpd = {
		.init = &r600_hpd_init,
		.fini = &r600_hpd_fini,
		.sense = &r600_hpd_sense,
		.set_polarity = &r600_hpd_set_polarity,
	},
	.pm = {
		.misc = &r600_pm_misc,
		.prepare = &rs600_pm_prepare,
		.finish = &rs600_pm_finish,
		.init_profile = &rs780_pm_init_profile,
		.get_dynpm_state = &r600_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = NULL,
		.set_memory_clock = NULL,
		.get_pcie_lanes = NULL,
		.set_pcie_lanes = NULL,
		.set_clock_gating = NULL,
		.get_temperature = &rv6xx_get_temp,
	},
	.dpm = {
		.init = &rs780_dpm_init,
		.setup_asic = &rs780_dpm_setup_asic,
		.enable = &rs780_dpm_enable,
		.disable = &rs780_dpm_disable,
		.pre_set_power_state = &r600_dpm_pre_set_power_state,
		.set_power_state = &rs780_dpm_set_power_state,
		.post_set_power_state = &r600_dpm_post_set_power_state,
		.display_configuration_changed = &rs780_dpm_display_configuration_changed,
		.fini = &rs780_dpm_fini,
		.get_sclk = &rs780_dpm_get_sclk,
		.get_mclk = &rs780_dpm_get_mclk,
		.print_power_state = &rs780_dpm_print_power_state,
		.debugfs_print_current_performance_level = &rs780_dpm_debugfs_print_current_performance_level,
	},
	.pflip = {
		.pre_page_flip = &rs600_pre_page_flip,
		.page_flip = &rs600_page_flip,
		.post_page_flip = &rs600_post_page_flip,
	},
};

static struct radeon_asic rv770_asic = {
	.init = &rv770_init,
	.fini = &rv770_fini,
	.suspend = &rv770_suspend,
	.resume = &rv770_resume,
	.asic_reset = &r600_asic_reset,
	.vga_set_state = &r600_vga_set_state,
	.ioctl_wait_idle = r600_ioctl_wait_idle,
	.gui_idle = &r600_gui_idle,
	.mc_wait_for_idle = &r600_mc_wait_for_idle,
	.get_xclk = &rv770_get_xclk,
	.get_gpu_clock_counter = &r600_get_gpu_clock_counter,
	.gart = {
		.tlb_flush = &r600_pcie_gart_tlb_flush,
		.set_page = &rs600_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &r600_ring_ib_execute,
			.emit_fence = &r600_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = &r600_cs_parse,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &r600_gfx_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_DMA_INDEX] = {
			.ib_execute = &r600_dma_ring_ib_execute,
			.emit_fence = &r600_dma_fence_ring_emit,
			.emit_semaphore = &r600_dma_semaphore_ring_emit,
			.cs_parse = &r600_dma_cs_parse,
			.ring_test = &r600_dma_ring_test,
			.ib_test = &r600_dma_ib_test,
			.is_lockup = &r600_dma_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_UVD_INDEX] = {
			.ib_execute = &r600_uvd_ib_execute,
			.emit_fence = &r600_uvd_fence_emit,
			.emit_semaphore = &r600_uvd_semaphore_emit,
			.cs_parse = &radeon_uvd_cs_parse,
			.ring_test = &r600_uvd_ring_test,
			.ib_test = &r600_uvd_ib_test,
			.is_lockup = &radeon_ring_test_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &r600_irq_set,
		.process = &r600_irq_process,
	},
	.display = {
		.bandwidth_update = &rv515_bandwidth_update,
		.get_vblank_counter = &rs600_get_vblank_counter,
		.wait_for_vblank = &avivo_wait_for_vblank,
		.set_backlight_level = &atombios_set_backlight_level,
		.get_backlight_level = &atombios_get_backlight_level,
		.hdmi_enable = &r600_hdmi_enable,
		.hdmi_setmode = &r600_hdmi_setmode,
	},
	.copy = {
		.blit = &r600_copy_cpdma,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &rv770_copy_dma,
		.dma_ring_index = R600_RING_TYPE_DMA_INDEX,
		.copy = &rv770_copy_dma,
		.copy_ring_index = R600_RING_TYPE_DMA_INDEX,
	},
	.surface = {
		.set_reg = r600_set_surface_reg,
		.clear_reg = r600_clear_surface_reg,
	},
	.hpd = {
		.init = &r600_hpd_init,
		.fini = &r600_hpd_fini,
		.sense = &r600_hpd_sense,
		.set_polarity = &r600_hpd_set_polarity,
	},
	.pm = {
		.misc = &rv770_pm_misc,
		.prepare = &rs600_pm_prepare,
		.finish = &rs600_pm_finish,
		.init_profile = &r600_pm_init_profile,
		.get_dynpm_state = &r600_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = &radeon_atom_get_memory_clock,
		.set_memory_clock = &radeon_atom_set_memory_clock,
		.get_pcie_lanes = &r600_get_pcie_lanes,
		.set_pcie_lanes = &r600_set_pcie_lanes,
		.set_clock_gating = &radeon_atom_set_clock_gating,
		.set_uvd_clocks = &rv770_set_uvd_clocks,
		.get_temperature = &rv770_get_temp,
	},
	.dpm = {
		.init = &rv770_dpm_init,
		.setup_asic = &rv770_dpm_setup_asic,
		.enable = &rv770_dpm_enable,
		.disable = &rv770_dpm_disable,
		.pre_set_power_state = &r600_dpm_pre_set_power_state,
		.set_power_state = &rv770_dpm_set_power_state,
		.post_set_power_state = &r600_dpm_post_set_power_state,
		.display_configuration_changed = &rv770_dpm_display_configuration_changed,
		.fini = &rv770_dpm_fini,
		.get_sclk = &rv770_dpm_get_sclk,
		.get_mclk = &rv770_dpm_get_mclk,
		.print_power_state = &rv770_dpm_print_power_state,
		.debugfs_print_current_performance_level = &rv770_dpm_debugfs_print_current_performance_level,
		.force_performance_level = &rv770_dpm_force_performance_level,
		.vblank_too_short = &rv770_dpm_vblank_too_short,
	},
	.pflip = {
		.pre_page_flip = &rs600_pre_page_flip,
		.page_flip = &rv770_page_flip,
		.post_page_flip = &rs600_post_page_flip,
	},
};

static struct radeon_asic evergreen_asic = {
	.init = &evergreen_init,
	.fini = &evergreen_fini,
	.suspend = &evergreen_suspend,
	.resume = &evergreen_resume,
	.asic_reset = &evergreen_asic_reset,
	.vga_set_state = &r600_vga_set_state,
	.ioctl_wait_idle = r600_ioctl_wait_idle,
	.gui_idle = &r600_gui_idle,
	.mc_wait_for_idle = &evergreen_mc_wait_for_idle,
	.get_xclk = &rv770_get_xclk,
	.get_gpu_clock_counter = &r600_get_gpu_clock_counter,
	.gart = {
		.tlb_flush = &evergreen_pcie_gart_tlb_flush,
		.set_page = &rs600_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &evergreen_ring_ib_execute,
			.emit_fence = &r600_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = &evergreen_cs_parse,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &evergreen_gfx_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_DMA_INDEX] = {
			.ib_execute = &evergreen_dma_ring_ib_execute,
			.emit_fence = &evergreen_dma_fence_ring_emit,
			.emit_semaphore = &r600_dma_semaphore_ring_emit,
			.cs_parse = &evergreen_dma_cs_parse,
			.ring_test = &r600_dma_ring_test,
			.ib_test = &r600_dma_ib_test,
			.is_lockup = &evergreen_dma_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_UVD_INDEX] = {
			.ib_execute = &r600_uvd_ib_execute,
			.emit_fence = &r600_uvd_fence_emit,
			.emit_semaphore = &r600_uvd_semaphore_emit,
			.cs_parse = &radeon_uvd_cs_parse,
			.ring_test = &r600_uvd_ring_test,
			.ib_test = &r600_uvd_ib_test,
			.is_lockup = &radeon_ring_test_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &evergreen_irq_set,
		.process = &evergreen_irq_process,
	},
	.display = {
		.bandwidth_update = &evergreen_bandwidth_update,
		.get_vblank_counter = &evergreen_get_vblank_counter,
		.wait_for_vblank = &dce4_wait_for_vblank,
		.set_backlight_level = &atombios_set_backlight_level,
		.get_backlight_level = &atombios_get_backlight_level,
		.hdmi_enable = &evergreen_hdmi_enable,
		.hdmi_setmode = &evergreen_hdmi_setmode,
	},
	.copy = {
		.blit = &r600_copy_cpdma,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &evergreen_copy_dma,
		.dma_ring_index = R600_RING_TYPE_DMA_INDEX,
		.copy = &evergreen_copy_dma,
		.copy_ring_index = R600_RING_TYPE_DMA_INDEX,
	},
	.surface = {
		.set_reg = r600_set_surface_reg,
		.clear_reg = r600_clear_surface_reg,
	},
	.hpd = {
		.init = &evergreen_hpd_init,
		.fini = &evergreen_hpd_fini,
		.sense = &evergreen_hpd_sense,
		.set_polarity = &evergreen_hpd_set_polarity,
	},
	.pm = {
		.misc = &evergreen_pm_misc,
		.prepare = &evergreen_pm_prepare,
		.finish = &evergreen_pm_finish,
		.init_profile = &r600_pm_init_profile,
		.get_dynpm_state = &r600_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = &radeon_atom_get_memory_clock,
		.set_memory_clock = &radeon_atom_set_memory_clock,
		.get_pcie_lanes = &r600_get_pcie_lanes,
		.set_pcie_lanes = &r600_set_pcie_lanes,
		.set_clock_gating = NULL,
		.set_uvd_clocks = &evergreen_set_uvd_clocks,
		.get_temperature = &evergreen_get_temp,
	},
	.dpm = {
		.init = &cypress_dpm_init,
		.setup_asic = &cypress_dpm_setup_asic,
		.enable = &cypress_dpm_enable,
		.disable = &cypress_dpm_disable,
		.pre_set_power_state = &r600_dpm_pre_set_power_state,
		.set_power_state = &cypress_dpm_set_power_state,
		.post_set_power_state = &r600_dpm_post_set_power_state,
		.display_configuration_changed = &cypress_dpm_display_configuration_changed,
		.fini = &cypress_dpm_fini,
		.get_sclk = &rv770_dpm_get_sclk,
		.get_mclk = &rv770_dpm_get_mclk,
		.print_power_state = &rv770_dpm_print_power_state,
		.debugfs_print_current_performance_level = &rv770_dpm_debugfs_print_current_performance_level,
		.force_performance_level = &rv770_dpm_force_performance_level,
		.vblank_too_short = &cypress_dpm_vblank_too_short,
	},
	.pflip = {
		.pre_page_flip = &evergreen_pre_page_flip,
		.page_flip = &evergreen_page_flip,
		.post_page_flip = &evergreen_post_page_flip,
	},
};

static struct radeon_asic sumo_asic = {
	.init = &evergreen_init,
	.fini = &evergreen_fini,
	.suspend = &evergreen_suspend,
	.resume = &evergreen_resume,
	.asic_reset = &evergreen_asic_reset,
	.vga_set_state = &r600_vga_set_state,
	.ioctl_wait_idle = r600_ioctl_wait_idle,
	.gui_idle = &r600_gui_idle,
	.mc_wait_for_idle = &evergreen_mc_wait_for_idle,
	.get_xclk = &r600_get_xclk,
	.get_gpu_clock_counter = &r600_get_gpu_clock_counter,
	.gart = {
		.tlb_flush = &evergreen_pcie_gart_tlb_flush,
		.set_page = &rs600_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &evergreen_ring_ib_execute,
			.emit_fence = &r600_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = &evergreen_cs_parse,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &evergreen_gfx_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_DMA_INDEX] = {
			.ib_execute = &evergreen_dma_ring_ib_execute,
			.emit_fence = &evergreen_dma_fence_ring_emit,
			.emit_semaphore = &r600_dma_semaphore_ring_emit,
			.cs_parse = &evergreen_dma_cs_parse,
			.ring_test = &r600_dma_ring_test,
			.ib_test = &r600_dma_ib_test,
			.is_lockup = &evergreen_dma_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_UVD_INDEX] = {
			.ib_execute = &r600_uvd_ib_execute,
			.emit_fence = &r600_uvd_fence_emit,
			.emit_semaphore = &r600_uvd_semaphore_emit,
			.cs_parse = &radeon_uvd_cs_parse,
			.ring_test = &r600_uvd_ring_test,
			.ib_test = &r600_uvd_ib_test,
			.is_lockup = &radeon_ring_test_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &evergreen_irq_set,
		.process = &evergreen_irq_process,
	},
	.display = {
		.bandwidth_update = &evergreen_bandwidth_update,
		.get_vblank_counter = &evergreen_get_vblank_counter,
		.wait_for_vblank = &dce4_wait_for_vblank,
		.set_backlight_level = &atombios_set_backlight_level,
		.get_backlight_level = &atombios_get_backlight_level,
		.hdmi_enable = &evergreen_hdmi_enable,
		.hdmi_setmode = &evergreen_hdmi_setmode,
	},
	.copy = {
		.blit = &r600_copy_cpdma,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &evergreen_copy_dma,
		.dma_ring_index = R600_RING_TYPE_DMA_INDEX,
		.copy = &evergreen_copy_dma,
		.copy_ring_index = R600_RING_TYPE_DMA_INDEX,
	},
	.surface = {
		.set_reg = r600_set_surface_reg,
		.clear_reg = r600_clear_surface_reg,
	},
	.hpd = {
		.init = &evergreen_hpd_init,
		.fini = &evergreen_hpd_fini,
		.sense = &evergreen_hpd_sense,
		.set_polarity = &evergreen_hpd_set_polarity,
	},
	.pm = {
		.misc = &evergreen_pm_misc,
		.prepare = &evergreen_pm_prepare,
		.finish = &evergreen_pm_finish,
		.init_profile = &sumo_pm_init_profile,
		.get_dynpm_state = &r600_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = NULL,
		.set_memory_clock = NULL,
		.get_pcie_lanes = NULL,
		.set_pcie_lanes = NULL,
		.set_clock_gating = NULL,
		.set_uvd_clocks = &sumo_set_uvd_clocks,
		.get_temperature = &sumo_get_temp,
	},
	.dpm = {
		.init = &sumo_dpm_init,
		.setup_asic = &sumo_dpm_setup_asic,
		.enable = &sumo_dpm_enable,
		.disable = &sumo_dpm_disable,
		.pre_set_power_state = &sumo_dpm_pre_set_power_state,
		.set_power_state = &sumo_dpm_set_power_state,
		.post_set_power_state = &sumo_dpm_post_set_power_state,
		.display_configuration_changed = &sumo_dpm_display_configuration_changed,
		.fini = &sumo_dpm_fini,
		.get_sclk = &sumo_dpm_get_sclk,
		.get_mclk = &sumo_dpm_get_mclk,
		.print_power_state = &sumo_dpm_print_power_state,
		.debugfs_print_current_performance_level = &sumo_dpm_debugfs_print_current_performance_level,
		.force_performance_level = &sumo_dpm_force_performance_level,
	},
	.pflip = {
		.pre_page_flip = &evergreen_pre_page_flip,
		.page_flip = &evergreen_page_flip,
		.post_page_flip = &evergreen_post_page_flip,
	},
};

static struct radeon_asic btc_asic = {
	.init = &evergreen_init,
	.fini = &evergreen_fini,
	.suspend = &evergreen_suspend,
	.resume = &evergreen_resume,
	.asic_reset = &evergreen_asic_reset,
	.vga_set_state = &r600_vga_set_state,
	.ioctl_wait_idle = r600_ioctl_wait_idle,
	.gui_idle = &r600_gui_idle,
	.mc_wait_for_idle = &evergreen_mc_wait_for_idle,
	.get_xclk = &rv770_get_xclk,
	.get_gpu_clock_counter = &r600_get_gpu_clock_counter,
	.gart = {
		.tlb_flush = &evergreen_pcie_gart_tlb_flush,
		.set_page = &rs600_gart_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &evergreen_ring_ib_execute,
			.emit_fence = &r600_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = &evergreen_cs_parse,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &evergreen_gfx_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_DMA_INDEX] = {
			.ib_execute = &evergreen_dma_ring_ib_execute,
			.emit_fence = &evergreen_dma_fence_ring_emit,
			.emit_semaphore = &r600_dma_semaphore_ring_emit,
			.cs_parse = &evergreen_dma_cs_parse,
			.ring_test = &r600_dma_ring_test,
			.ib_test = &r600_dma_ib_test,
			.is_lockup = &evergreen_dma_is_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_UVD_INDEX] = {
			.ib_execute = &r600_uvd_ib_execute,
			.emit_fence = &r600_uvd_fence_emit,
			.emit_semaphore = &r600_uvd_semaphore_emit,
			.cs_parse = &radeon_uvd_cs_parse,
			.ring_test = &r600_uvd_ring_test,
			.ib_test = &r600_uvd_ib_test,
			.is_lockup = &radeon_ring_test_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &evergreen_irq_set,
		.process = &evergreen_irq_process,
	},
	.display = {
		.bandwidth_update = &evergreen_bandwidth_update,
		.get_vblank_counter = &evergreen_get_vblank_counter,
		.wait_for_vblank = &dce4_wait_for_vblank,
		.set_backlight_level = &atombios_set_backlight_level,
		.get_backlight_level = &atombios_get_backlight_level,
		.hdmi_enable = &evergreen_hdmi_enable,
		.hdmi_setmode = &evergreen_hdmi_setmode,
	},
	.copy = {
		.blit = &r600_copy_cpdma,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &evergreen_copy_dma,
		.dma_ring_index = R600_RING_TYPE_DMA_INDEX,
		.copy = &evergreen_copy_dma,
		.copy_ring_index = R600_RING_TYPE_DMA_INDEX,
	},
	.surface = {
		.set_reg = r600_set_surface_reg,
		.clear_reg = r600_clear_surface_reg,
	},
	.hpd = {
		.init = &evergreen_hpd_init,
		.fini = &evergreen_hpd_fini,
		.sense = &evergreen_hpd_sense,
		.set_polarity = &evergreen_hpd_set_polarity,
	},
	.pm = {
		.misc = &evergreen_pm_misc,
		.prepare = &evergreen_pm_prepare,
		.finish = &evergreen_pm_finish,
		.init_profile = &btc_pm_init_profile,
		.get_dynpm_state = &r600_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = &radeon_atom_get_memory_clock,
		.set_memory_clock = &radeon_atom_set_memory_clock,
		.get_pcie_lanes = &r600_get_pcie_lanes,
		.set_pcie_lanes = &r600_set_pcie_lanes,
		.set_clock_gating = NULL,
		.set_uvd_clocks = &evergreen_set_uvd_clocks,
		.get_temperature = &evergreen_get_temp,
	},
	.dpm = {
		.init = &btc_dpm_init,
		.setup_asic = &btc_dpm_setup_asic,
		.enable = &btc_dpm_enable,
		.disable = &btc_dpm_disable,
		.pre_set_power_state = &btc_dpm_pre_set_power_state,
		.set_power_state = &btc_dpm_set_power_state,
		.post_set_power_state = &btc_dpm_post_set_power_state,
		.display_configuration_changed = &cypress_dpm_display_configuration_changed,
		.fini = &btc_dpm_fini,
		.get_sclk = &btc_dpm_get_sclk,
		.get_mclk = &btc_dpm_get_mclk,
		.print_power_state = &rv770_dpm_print_power_state,
		.debugfs_print_current_performance_level = &rv770_dpm_debugfs_print_current_performance_level,
		.force_performance_level = &rv770_dpm_force_performance_level,
		.vblank_too_short = &btc_dpm_vblank_too_short,
	},
	.pflip = {
		.pre_page_flip = &evergreen_pre_page_flip,
		.page_flip = &evergreen_page_flip,
		.post_page_flip = &evergreen_post_page_flip,
	},
};

static struct radeon_asic cayman_asic = {
	.init = &cayman_init,
	.fini = &cayman_fini,
	.suspend = &cayman_suspend,
	.resume = &cayman_resume,
	.asic_reset = &cayman_asic_reset,
	.vga_set_state = &r600_vga_set_state,
	.ioctl_wait_idle = r600_ioctl_wait_idle,
	.gui_idle = &r600_gui_idle,
	.mc_wait_for_idle = &evergreen_mc_wait_for_idle,
	.get_xclk = &rv770_get_xclk,
	.get_gpu_clock_counter = &r600_get_gpu_clock_counter,
	.gart = {
		.tlb_flush = &cayman_pcie_gart_tlb_flush,
		.set_page = &rs600_gart_set_page,
	},
	.vm = {
		.init = &cayman_vm_init,
		.fini = &cayman_vm_fini,
		.pt_ring_index = R600_RING_TYPE_DMA_INDEX,
		.set_page = &cayman_vm_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &cayman_ring_ib_execute,
			.ib_parse = &evergreen_ib_parse,
			.emit_fence = &cayman_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = &evergreen_cs_parse,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &cayman_gfx_is_lockup,
			.vm_flush = &cayman_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[CAYMAN_RING_TYPE_CP1_INDEX] = {
			.ib_execute = &cayman_ring_ib_execute,
			.ib_parse = &evergreen_ib_parse,
			.emit_fence = &cayman_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = &evergreen_cs_parse,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &cayman_gfx_is_lockup,
			.vm_flush = &cayman_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[CAYMAN_RING_TYPE_CP2_INDEX] = {
			.ib_execute = &cayman_ring_ib_execute,
			.ib_parse = &evergreen_ib_parse,
			.emit_fence = &cayman_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = &evergreen_cs_parse,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &cayman_gfx_is_lockup,
			.vm_flush = &cayman_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_DMA_INDEX] = {
			.ib_execute = &cayman_dma_ring_ib_execute,
			.ib_parse = &evergreen_dma_ib_parse,
			.emit_fence = &evergreen_dma_fence_ring_emit,
			.emit_semaphore = &r600_dma_semaphore_ring_emit,
			.cs_parse = &evergreen_dma_cs_parse,
			.ring_test = &r600_dma_ring_test,
			.ib_test = &r600_dma_ib_test,
			.is_lockup = &cayman_dma_is_lockup,
			.vm_flush = &cayman_dma_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[CAYMAN_RING_TYPE_DMA1_INDEX] = {
			.ib_execute = &cayman_dma_ring_ib_execute,
			.ib_parse = &evergreen_dma_ib_parse,
			.emit_fence = &evergreen_dma_fence_ring_emit,
			.emit_semaphore = &r600_dma_semaphore_ring_emit,
			.cs_parse = &evergreen_dma_cs_parse,
			.ring_test = &r600_dma_ring_test,
			.ib_test = &r600_dma_ib_test,
			.is_lockup = &cayman_dma_is_lockup,
			.vm_flush = &cayman_dma_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_UVD_INDEX] = {
			.ib_execute = &r600_uvd_ib_execute,
			.emit_fence = &r600_uvd_fence_emit,
			.emit_semaphore = &cayman_uvd_semaphore_emit,
			.cs_parse = &radeon_uvd_cs_parse,
			.ring_test = &r600_uvd_ring_test,
			.ib_test = &r600_uvd_ib_test,
			.is_lockup = &radeon_ring_test_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &evergreen_irq_set,
		.process = &evergreen_irq_process,
	},
	.display = {
		.bandwidth_update = &evergreen_bandwidth_update,
		.get_vblank_counter = &evergreen_get_vblank_counter,
		.wait_for_vblank = &dce4_wait_for_vblank,
		.set_backlight_level = &atombios_set_backlight_level,
		.get_backlight_level = &atombios_get_backlight_level,
		.hdmi_enable = &evergreen_hdmi_enable,
		.hdmi_setmode = &evergreen_hdmi_setmode,
	},
	.copy = {
		.blit = &r600_copy_cpdma,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &evergreen_copy_dma,
		.dma_ring_index = R600_RING_TYPE_DMA_INDEX,
		.copy = &evergreen_copy_dma,
		.copy_ring_index = R600_RING_TYPE_DMA_INDEX,
	},
	.surface = {
		.set_reg = r600_set_surface_reg,
		.clear_reg = r600_clear_surface_reg,
	},
	.hpd = {
		.init = &evergreen_hpd_init,
		.fini = &evergreen_hpd_fini,
		.sense = &evergreen_hpd_sense,
		.set_polarity = &evergreen_hpd_set_polarity,
	},
	.pm = {
		.misc = &evergreen_pm_misc,
		.prepare = &evergreen_pm_prepare,
		.finish = &evergreen_pm_finish,
		.init_profile = &btc_pm_init_profile,
		.get_dynpm_state = &r600_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = &radeon_atom_get_memory_clock,
		.set_memory_clock = &radeon_atom_set_memory_clock,
		.get_pcie_lanes = &r600_get_pcie_lanes,
		.set_pcie_lanes = &r600_set_pcie_lanes,
		.set_clock_gating = NULL,
		.set_uvd_clocks = &evergreen_set_uvd_clocks,
		.get_temperature = &evergreen_get_temp,
	},
	.dpm = {
		.init = &ni_dpm_init,
		.setup_asic = &ni_dpm_setup_asic,
		.enable = &ni_dpm_enable,
		.disable = &ni_dpm_disable,
		.pre_set_power_state = &ni_dpm_pre_set_power_state,
		.set_power_state = &ni_dpm_set_power_state,
		.post_set_power_state = &ni_dpm_post_set_power_state,
		.display_configuration_changed = &cypress_dpm_display_configuration_changed,
		.fini = &ni_dpm_fini,
		.get_sclk = &ni_dpm_get_sclk,
		.get_mclk = &ni_dpm_get_mclk,
		.print_power_state = &ni_dpm_print_power_state,
		.debugfs_print_current_performance_level = &ni_dpm_debugfs_print_current_performance_level,
		.force_performance_level = &ni_dpm_force_performance_level,
		.vblank_too_short = &ni_dpm_vblank_too_short,
	},
	.pflip = {
		.pre_page_flip = &evergreen_pre_page_flip,
		.page_flip = &evergreen_page_flip,
		.post_page_flip = &evergreen_post_page_flip,
	},
};

static struct radeon_asic trinity_asic = {
	.init = &cayman_init,
	.fini = &cayman_fini,
	.suspend = &cayman_suspend,
	.resume = &cayman_resume,
	.asic_reset = &cayman_asic_reset,
	.vga_set_state = &r600_vga_set_state,
	.ioctl_wait_idle = r600_ioctl_wait_idle,
	.gui_idle = &r600_gui_idle,
	.mc_wait_for_idle = &evergreen_mc_wait_for_idle,
	.get_xclk = &r600_get_xclk,
	.get_gpu_clock_counter = &r600_get_gpu_clock_counter,
	.gart = {
		.tlb_flush = &cayman_pcie_gart_tlb_flush,
		.set_page = &rs600_gart_set_page,
	},
	.vm = {
		.init = &cayman_vm_init,
		.fini = &cayman_vm_fini,
		.pt_ring_index = R600_RING_TYPE_DMA_INDEX,
		.set_page = &cayman_vm_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &cayman_ring_ib_execute,
			.ib_parse = &evergreen_ib_parse,
			.emit_fence = &cayman_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = &evergreen_cs_parse,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &cayman_gfx_is_lockup,
			.vm_flush = &cayman_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[CAYMAN_RING_TYPE_CP1_INDEX] = {
			.ib_execute = &cayman_ring_ib_execute,
			.ib_parse = &evergreen_ib_parse,
			.emit_fence = &cayman_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = &evergreen_cs_parse,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &cayman_gfx_is_lockup,
			.vm_flush = &cayman_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[CAYMAN_RING_TYPE_CP2_INDEX] = {
			.ib_execute = &cayman_ring_ib_execute,
			.ib_parse = &evergreen_ib_parse,
			.emit_fence = &cayman_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = &evergreen_cs_parse,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &cayman_gfx_is_lockup,
			.vm_flush = &cayman_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_DMA_INDEX] = {
			.ib_execute = &cayman_dma_ring_ib_execute,
			.ib_parse = &evergreen_dma_ib_parse,
			.emit_fence = &evergreen_dma_fence_ring_emit,
			.emit_semaphore = &r600_dma_semaphore_ring_emit,
			.cs_parse = &evergreen_dma_cs_parse,
			.ring_test = &r600_dma_ring_test,
			.ib_test = &r600_dma_ib_test,
			.is_lockup = &cayman_dma_is_lockup,
			.vm_flush = &cayman_dma_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[CAYMAN_RING_TYPE_DMA1_INDEX] = {
			.ib_execute = &cayman_dma_ring_ib_execute,
			.ib_parse = &evergreen_dma_ib_parse,
			.emit_fence = &evergreen_dma_fence_ring_emit,
			.emit_semaphore = &r600_dma_semaphore_ring_emit,
			.cs_parse = &evergreen_dma_cs_parse,
			.ring_test = &r600_dma_ring_test,
			.ib_test = &r600_dma_ib_test,
			.is_lockup = &cayman_dma_is_lockup,
			.vm_flush = &cayman_dma_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_UVD_INDEX] = {
			.ib_execute = &r600_uvd_ib_execute,
			.emit_fence = &r600_uvd_fence_emit,
			.emit_semaphore = &cayman_uvd_semaphore_emit,
			.cs_parse = &radeon_uvd_cs_parse,
			.ring_test = &r600_uvd_ring_test,
			.ib_test = &r600_uvd_ib_test,
			.is_lockup = &radeon_ring_test_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &evergreen_irq_set,
		.process = &evergreen_irq_process,
	},
	.display = {
		.bandwidth_update = &dce6_bandwidth_update,
		.get_vblank_counter = &evergreen_get_vblank_counter,
		.wait_for_vblank = &dce4_wait_for_vblank,
		.set_backlight_level = &atombios_set_backlight_level,
		.get_backlight_level = &atombios_get_backlight_level,
	},
	.copy = {
		.blit = &r600_copy_cpdma,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &evergreen_copy_dma,
		.dma_ring_index = R600_RING_TYPE_DMA_INDEX,
		.copy = &evergreen_copy_dma,
		.copy_ring_index = R600_RING_TYPE_DMA_INDEX,
	},
	.surface = {
		.set_reg = r600_set_surface_reg,
		.clear_reg = r600_clear_surface_reg,
	},
	.hpd = {
		.init = &evergreen_hpd_init,
		.fini = &evergreen_hpd_fini,
		.sense = &evergreen_hpd_sense,
		.set_polarity = &evergreen_hpd_set_polarity,
	},
	.pm = {
		.misc = &evergreen_pm_misc,
		.prepare = &evergreen_pm_prepare,
		.finish = &evergreen_pm_finish,
		.init_profile = &sumo_pm_init_profile,
		.get_dynpm_state = &r600_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = NULL,
		.set_memory_clock = NULL,
		.get_pcie_lanes = NULL,
		.set_pcie_lanes = NULL,
		.set_clock_gating = NULL,
		.set_uvd_clocks = &sumo_set_uvd_clocks,
		.get_temperature = &tn_get_temp,
	},
	.dpm = {
		.init = &trinity_dpm_init,
		.setup_asic = &trinity_dpm_setup_asic,
		.enable = &trinity_dpm_enable,
		.disable = &trinity_dpm_disable,
		.pre_set_power_state = &trinity_dpm_pre_set_power_state,
		.set_power_state = &trinity_dpm_set_power_state,
		.post_set_power_state = &trinity_dpm_post_set_power_state,
		.display_configuration_changed = &trinity_dpm_display_configuration_changed,
		.fini = &trinity_dpm_fini,
		.get_sclk = &trinity_dpm_get_sclk,
		.get_mclk = &trinity_dpm_get_mclk,
		.print_power_state = &trinity_dpm_print_power_state,
		.debugfs_print_current_performance_level = &trinity_dpm_debugfs_print_current_performance_level,
		.force_performance_level = &trinity_dpm_force_performance_level,
	},
	.pflip = {
		.pre_page_flip = &evergreen_pre_page_flip,
		.page_flip = &evergreen_page_flip,
		.post_page_flip = &evergreen_post_page_flip,
	},
};

static struct radeon_asic si_asic = {
	.init = &si_init,
	.fini = &si_fini,
	.suspend = &si_suspend,
	.resume = &si_resume,
	.asic_reset = &si_asic_reset,
	.vga_set_state = &r600_vga_set_state,
	.ioctl_wait_idle = r600_ioctl_wait_idle,
	.gui_idle = &r600_gui_idle,
	.mc_wait_for_idle = &evergreen_mc_wait_for_idle,
	.get_xclk = &si_get_xclk,
	.get_gpu_clock_counter = &si_get_gpu_clock_counter,
	.gart = {
		.tlb_flush = &si_pcie_gart_tlb_flush,
		.set_page = &rs600_gart_set_page,
	},
	.vm = {
		.init = &si_vm_init,
		.fini = &si_vm_fini,
		.pt_ring_index = R600_RING_TYPE_DMA_INDEX,
		.set_page = &si_vm_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &si_ring_ib_execute,
			.ib_parse = &si_ib_parse,
			.emit_fence = &si_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = NULL,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &si_gfx_is_lockup,
			.vm_flush = &si_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[CAYMAN_RING_TYPE_CP1_INDEX] = {
			.ib_execute = &si_ring_ib_execute,
			.ib_parse = &si_ib_parse,
			.emit_fence = &si_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = NULL,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &si_gfx_is_lockup,
			.vm_flush = &si_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[CAYMAN_RING_TYPE_CP2_INDEX] = {
			.ib_execute = &si_ring_ib_execute,
			.ib_parse = &si_ib_parse,
			.emit_fence = &si_fence_ring_emit,
			.emit_semaphore = &r600_semaphore_ring_emit,
			.cs_parse = NULL,
			.ring_test = &r600_ring_test,
			.ib_test = &r600_ib_test,
			.is_lockup = &si_gfx_is_lockup,
			.vm_flush = &si_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_DMA_INDEX] = {
			.ib_execute = &cayman_dma_ring_ib_execute,
			.ib_parse = &evergreen_dma_ib_parse,
			.emit_fence = &evergreen_dma_fence_ring_emit,
			.emit_semaphore = &r600_dma_semaphore_ring_emit,
			.cs_parse = NULL,
			.ring_test = &r600_dma_ring_test,
			.ib_test = &r600_dma_ib_test,
			.is_lockup = &si_dma_is_lockup,
			.vm_flush = &si_dma_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[CAYMAN_RING_TYPE_DMA1_INDEX] = {
			.ib_execute = &cayman_dma_ring_ib_execute,
			.ib_parse = &evergreen_dma_ib_parse,
			.emit_fence = &evergreen_dma_fence_ring_emit,
			.emit_semaphore = &r600_dma_semaphore_ring_emit,
			.cs_parse = NULL,
			.ring_test = &r600_dma_ring_test,
			.ib_test = &r600_dma_ib_test,
			.is_lockup = &si_dma_is_lockup,
			.vm_flush = &si_dma_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_UVD_INDEX] = {
			.ib_execute = &r600_uvd_ib_execute,
			.emit_fence = &r600_uvd_fence_emit,
			.emit_semaphore = &cayman_uvd_semaphore_emit,
			.cs_parse = &radeon_uvd_cs_parse,
			.ring_test = &r600_uvd_ring_test,
			.ib_test = &r600_uvd_ib_test,
			.is_lockup = &radeon_ring_test_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &si_irq_set,
		.process = &si_irq_process,
	},
	.display = {
		.bandwidth_update = &dce6_bandwidth_update,
		.get_vblank_counter = &evergreen_get_vblank_counter,
		.wait_for_vblank = &dce4_wait_for_vblank,
		.set_backlight_level = &atombios_set_backlight_level,
		.get_backlight_level = &atombios_get_backlight_level,
	},
	.copy = {
		.blit = NULL,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &si_copy_dma,
		.dma_ring_index = R600_RING_TYPE_DMA_INDEX,
		.copy = &si_copy_dma,
		.copy_ring_index = R600_RING_TYPE_DMA_INDEX,
	},
	.surface = {
		.set_reg = r600_set_surface_reg,
		.clear_reg = r600_clear_surface_reg,
	},
	.hpd = {
		.init = &evergreen_hpd_init,
		.fini = &evergreen_hpd_fini,
		.sense = &evergreen_hpd_sense,
		.set_polarity = &evergreen_hpd_set_polarity,
	},
	.pm = {
		.misc = &evergreen_pm_misc,
		.prepare = &evergreen_pm_prepare,
		.finish = &evergreen_pm_finish,
		.init_profile = &sumo_pm_init_profile,
		.get_dynpm_state = &r600_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = &radeon_atom_get_memory_clock,
		.set_memory_clock = &radeon_atom_set_memory_clock,
		.get_pcie_lanes = &r600_get_pcie_lanes,
		.set_pcie_lanes = &r600_set_pcie_lanes,
		.set_clock_gating = NULL,
		.set_uvd_clocks = &si_set_uvd_clocks,
		.get_temperature = &si_get_temp,
	},
	.dpm = {
		.init = &si_dpm_init,
		.setup_asic = &si_dpm_setup_asic,
		.enable = &si_dpm_enable,
		.disable = &si_dpm_disable,
		.pre_set_power_state = &si_dpm_pre_set_power_state,
		.set_power_state = &si_dpm_set_power_state,
		.post_set_power_state = &si_dpm_post_set_power_state,
		.display_configuration_changed = &si_dpm_display_configuration_changed,
		.fini = &si_dpm_fini,
		.get_sclk = &ni_dpm_get_sclk,
		.get_mclk = &ni_dpm_get_mclk,
		.print_power_state = &ni_dpm_print_power_state,
		.debugfs_print_current_performance_level = &si_dpm_debugfs_print_current_performance_level,
		.force_performance_level = &si_dpm_force_performance_level,
		.vblank_too_short = &ni_dpm_vblank_too_short,
	},
	.pflip = {
		.pre_page_flip = &evergreen_pre_page_flip,
		.page_flip = &evergreen_page_flip,
		.post_page_flip = &evergreen_post_page_flip,
	},
};

static struct radeon_asic ci_asic = {
	.init = &cik_init,
	.fini = &cik_fini,
	.suspend = &cik_suspend,
	.resume = &cik_resume,
	.asic_reset = &cik_asic_reset,
	.vga_set_state = &r600_vga_set_state,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r600_gui_idle,
	.mc_wait_for_idle = &evergreen_mc_wait_for_idle,
	.get_xclk = &cik_get_xclk,
	.get_gpu_clock_counter = &cik_get_gpu_clock_counter,
	.gart = {
		.tlb_flush = &cik_pcie_gart_tlb_flush,
		.set_page = &rs600_gart_set_page,
	},
	.vm = {
		.init = &cik_vm_init,
		.fini = &cik_vm_fini,
		.pt_ring_index = R600_RING_TYPE_DMA_INDEX,
		.set_page = &cik_vm_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &cik_ring_ib_execute,
			.ib_parse = &cik_ib_parse,
			.emit_fence = &cik_fence_gfx_ring_emit,
			.emit_semaphore = &cik_semaphore_ring_emit,
			.cs_parse = NULL,
			.ring_test = &cik_ring_test,
			.ib_test = &cik_ib_test,
			.is_lockup = &cik_gfx_is_lockup,
			.vm_flush = &cik_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[CAYMAN_RING_TYPE_CP1_INDEX] = {
			.ib_execute = &cik_ring_ib_execute,
			.ib_parse = &cik_ib_parse,
			.emit_fence = &cik_fence_compute_ring_emit,
			.emit_semaphore = &cik_semaphore_ring_emit,
			.cs_parse = NULL,
			.ring_test = &cik_ring_test,
			.ib_test = &cik_ib_test,
			.is_lockup = &cik_gfx_is_lockup,
			.vm_flush = &cik_vm_flush,
			.get_rptr = &cik_compute_ring_get_rptr,
			.get_wptr = &cik_compute_ring_get_wptr,
			.set_wptr = &cik_compute_ring_set_wptr,
		},
		[CAYMAN_RING_TYPE_CP2_INDEX] = {
			.ib_execute = &cik_ring_ib_execute,
			.ib_parse = &cik_ib_parse,
			.emit_fence = &cik_fence_compute_ring_emit,
			.emit_semaphore = &cik_semaphore_ring_emit,
			.cs_parse = NULL,
			.ring_test = &cik_ring_test,
			.ib_test = &cik_ib_test,
			.is_lockup = &cik_gfx_is_lockup,
			.vm_flush = &cik_vm_flush,
			.get_rptr = &cik_compute_ring_get_rptr,
			.get_wptr = &cik_compute_ring_get_wptr,
			.set_wptr = &cik_compute_ring_set_wptr,
		},
		[R600_RING_TYPE_DMA_INDEX] = {
			.ib_execute = &cik_sdma_ring_ib_execute,
			.ib_parse = &cik_ib_parse,
			.emit_fence = &cik_sdma_fence_ring_emit,
			.emit_semaphore = &cik_sdma_semaphore_ring_emit,
			.cs_parse = NULL,
			.ring_test = &cik_sdma_ring_test,
			.ib_test = &cik_sdma_ib_test,
			.is_lockup = &cik_sdma_is_lockup,
			.vm_flush = &cik_dma_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[CAYMAN_RING_TYPE_DMA1_INDEX] = {
			.ib_execute = &cik_sdma_ring_ib_execute,
			.ib_parse = &cik_ib_parse,
			.emit_fence = &cik_sdma_fence_ring_emit,
			.emit_semaphore = &cik_sdma_semaphore_ring_emit,
			.cs_parse = NULL,
			.ring_test = &cik_sdma_ring_test,
			.ib_test = &cik_sdma_ib_test,
			.is_lockup = &cik_sdma_is_lockup,
			.vm_flush = &cik_dma_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_UVD_INDEX] = {
			.ib_execute = &r600_uvd_ib_execute,
			.emit_fence = &r600_uvd_fence_emit,
			.emit_semaphore = &cayman_uvd_semaphore_emit,
			.cs_parse = &radeon_uvd_cs_parse,
			.ring_test = &r600_uvd_ring_test,
			.ib_test = &r600_uvd_ib_test,
			.is_lockup = &radeon_ring_test_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &cik_irq_set,
		.process = &cik_irq_process,
	},
	.display = {
		.bandwidth_update = &dce8_bandwidth_update,
		.get_vblank_counter = &evergreen_get_vblank_counter,
		.wait_for_vblank = &dce4_wait_for_vblank,
	},
	.copy = {
		.blit = NULL,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &cik_copy_dma,
		.dma_ring_index = R600_RING_TYPE_DMA_INDEX,
		.copy = &cik_copy_dma,
		.copy_ring_index = R600_RING_TYPE_DMA_INDEX,
	},
	.surface = {
		.set_reg = r600_set_surface_reg,
		.clear_reg = r600_clear_surface_reg,
	},
	.hpd = {
		.init = &evergreen_hpd_init,
		.fini = &evergreen_hpd_fini,
		.sense = &evergreen_hpd_sense,
		.set_polarity = &evergreen_hpd_set_polarity,
	},
	.pm = {
		.misc = &evergreen_pm_misc,
		.prepare = &evergreen_pm_prepare,
		.finish = &evergreen_pm_finish,
		.init_profile = &sumo_pm_init_profile,
		.get_dynpm_state = &r600_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = &radeon_atom_get_memory_clock,
		.set_memory_clock = &radeon_atom_set_memory_clock,
		.get_pcie_lanes = NULL,
		.set_pcie_lanes = NULL,
		.set_clock_gating = NULL,
		.set_uvd_clocks = &cik_set_uvd_clocks,
		.get_temperature = &ci_get_temp,
	},
	.dpm = {
		.init = &ci_dpm_init,
		.setup_asic = &ci_dpm_setup_asic,
		.enable = &ci_dpm_enable,
		.disable = &ci_dpm_disable,
		.pre_set_power_state = &ci_dpm_pre_set_power_state,
		.set_power_state = &ci_dpm_set_power_state,
		.post_set_power_state = &ci_dpm_post_set_power_state,
		.display_configuration_changed = &ci_dpm_display_configuration_changed,
		.fini = &ci_dpm_fini,
		.get_sclk = &ci_dpm_get_sclk,
		.get_mclk = &ci_dpm_get_mclk,
		.print_power_state = &ci_dpm_print_power_state,
		.debugfs_print_current_performance_level = &ci_dpm_debugfs_print_current_performance_level,
		.force_performance_level = &ci_dpm_force_performance_level,
	},
	.pflip = {
		.pre_page_flip = &evergreen_pre_page_flip,
		.page_flip = &evergreen_page_flip,
		.post_page_flip = &evergreen_post_page_flip,
	},
};

static struct radeon_asic kv_asic = {
	.init = &cik_init,
	.fini = &cik_fini,
	.suspend = &cik_suspend,
	.resume = &cik_resume,
	.asic_reset = &cik_asic_reset,
	.vga_set_state = &r600_vga_set_state,
	.ioctl_wait_idle = NULL,
	.gui_idle = &r600_gui_idle,
	.mc_wait_for_idle = &evergreen_mc_wait_for_idle,
	.get_xclk = &cik_get_xclk,
	.get_gpu_clock_counter = &cik_get_gpu_clock_counter,
	.gart = {
		.tlb_flush = &cik_pcie_gart_tlb_flush,
		.set_page = &rs600_gart_set_page,
	},
	.vm = {
		.init = &cik_vm_init,
		.fini = &cik_vm_fini,
		.pt_ring_index = R600_RING_TYPE_DMA_INDEX,
		.set_page = &cik_vm_set_page,
	},
	.ring = {
		[RADEON_RING_TYPE_GFX_INDEX] = {
			.ib_execute = &cik_ring_ib_execute,
			.ib_parse = &cik_ib_parse,
			.emit_fence = &cik_fence_gfx_ring_emit,
			.emit_semaphore = &cik_semaphore_ring_emit,
			.cs_parse = NULL,
			.ring_test = &cik_ring_test,
			.ib_test = &cik_ib_test,
			.is_lockup = &cik_gfx_is_lockup,
			.vm_flush = &cik_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[CAYMAN_RING_TYPE_CP1_INDEX] = {
			.ib_execute = &cik_ring_ib_execute,
			.ib_parse = &cik_ib_parse,
			.emit_fence = &cik_fence_compute_ring_emit,
			.emit_semaphore = &cik_semaphore_ring_emit,
			.cs_parse = NULL,
			.ring_test = &cik_ring_test,
			.ib_test = &cik_ib_test,
			.is_lockup = &cik_gfx_is_lockup,
			.vm_flush = &cik_vm_flush,
			.get_rptr = &cik_compute_ring_get_rptr,
			.get_wptr = &cik_compute_ring_get_wptr,
			.set_wptr = &cik_compute_ring_set_wptr,
		},
		[CAYMAN_RING_TYPE_CP2_INDEX] = {
			.ib_execute = &cik_ring_ib_execute,
			.ib_parse = &cik_ib_parse,
			.emit_fence = &cik_fence_compute_ring_emit,
			.emit_semaphore = &cik_semaphore_ring_emit,
			.cs_parse = NULL,
			.ring_test = &cik_ring_test,
			.ib_test = &cik_ib_test,
			.is_lockup = &cik_gfx_is_lockup,
			.vm_flush = &cik_vm_flush,
			.get_rptr = &cik_compute_ring_get_rptr,
			.get_wptr = &cik_compute_ring_get_wptr,
			.set_wptr = &cik_compute_ring_set_wptr,
		},
		[R600_RING_TYPE_DMA_INDEX] = {
			.ib_execute = &cik_sdma_ring_ib_execute,
			.ib_parse = &cik_ib_parse,
			.emit_fence = &cik_sdma_fence_ring_emit,
			.emit_semaphore = &cik_sdma_semaphore_ring_emit,
			.cs_parse = NULL,
			.ring_test = &cik_sdma_ring_test,
			.ib_test = &cik_sdma_ib_test,
			.is_lockup = &cik_sdma_is_lockup,
			.vm_flush = &cik_dma_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[CAYMAN_RING_TYPE_DMA1_INDEX] = {
			.ib_execute = &cik_sdma_ring_ib_execute,
			.ib_parse = &cik_ib_parse,
			.emit_fence = &cik_sdma_fence_ring_emit,
			.emit_semaphore = &cik_sdma_semaphore_ring_emit,
			.cs_parse = NULL,
			.ring_test = &cik_sdma_ring_test,
			.ib_test = &cik_sdma_ib_test,
			.is_lockup = &cik_sdma_is_lockup,
			.vm_flush = &cik_dma_vm_flush,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		},
		[R600_RING_TYPE_UVD_INDEX] = {
			.ib_execute = &r600_uvd_ib_execute,
			.emit_fence = &r600_uvd_fence_emit,
			.emit_semaphore = &cayman_uvd_semaphore_emit,
			.cs_parse = &radeon_uvd_cs_parse,
			.ring_test = &r600_uvd_ring_test,
			.ib_test = &r600_uvd_ib_test,
			.is_lockup = &radeon_ring_test_lockup,
			.get_rptr = &radeon_ring_generic_get_rptr,
			.get_wptr = &radeon_ring_generic_get_wptr,
			.set_wptr = &radeon_ring_generic_set_wptr,
		}
	},
	.irq = {
		.set = &cik_irq_set,
		.process = &cik_irq_process,
	},
	.display = {
		.bandwidth_update = &dce8_bandwidth_update,
		.get_vblank_counter = &evergreen_get_vblank_counter,
		.wait_for_vblank = &dce4_wait_for_vblank,
	},
	.copy = {
		.blit = NULL,
		.blit_ring_index = RADEON_RING_TYPE_GFX_INDEX,
		.dma = &cik_copy_dma,
		.dma_ring_index = R600_RING_TYPE_DMA_INDEX,
		.copy = &cik_copy_dma,
		.copy_ring_index = R600_RING_TYPE_DMA_INDEX,
	},
	.surface = {
		.set_reg = r600_set_surface_reg,
		.clear_reg = r600_clear_surface_reg,
	},
	.hpd = {
		.init = &evergreen_hpd_init,
		.fini = &evergreen_hpd_fini,
		.sense = &evergreen_hpd_sense,
		.set_polarity = &evergreen_hpd_set_polarity,
	},
	.pm = {
		.misc = &evergreen_pm_misc,
		.prepare = &evergreen_pm_prepare,
		.finish = &evergreen_pm_finish,
		.init_profile = &sumo_pm_init_profile,
		.get_dynpm_state = &r600_pm_get_dynpm_state,
		.get_engine_clock = &radeon_atom_get_engine_clock,
		.set_engine_clock = &radeon_atom_set_engine_clock,
		.get_memory_clock = &radeon_atom_get_memory_clock,
		.set_memory_clock = &radeon_atom_set_memory_clock,
		.get_pcie_lanes = NULL,
		.set_pcie_lanes = NULL,
		.set_clock_gating = NULL,
		.set_uvd_clocks = &cik_set_uvd_clocks,
		.get_temperature = &kv_get_temp,
	},
	.dpm = {
		.init = &kv_dpm_init,
		.setup_asic = &kv_dpm_setup_asic,
		.enable = &kv_dpm_enable,
		.disable = &kv_dpm_disable,
		.pre_set_power_state = &kv_dpm_pre_set_power_state,
		.set_power_state = &kv_dpm_set_power_state,
		.post_set_power_state = &kv_dpm_post_set_power_state,
		.display_configuration_changed = &kv_dpm_display_configuration_changed,
		.fini = &kv_dpm_fini,
		.get_sclk = &kv_dpm_get_sclk,
		.get_mclk = &kv_dpm_get_mclk,
		.print_power_state = &kv_dpm_print_power_state,
	},
	.pflip = {
		.pre_page_flip = &evergreen_pre_page_flip,
		.page_flip = &evergreen_page_flip,
		.post_page_flip = &evergreen_post_page_flip,
	},
};

/**
 * radeon_asic_init - register asic specific callbacks
 *
 * @rdev: radeon device pointer
 *
 * Registers the appropriate asic specific callbacks for each
 * chip family.  Also sets other asics specific info like the number
 * of crtcs and the register aperture accessors (all asics).
 * Returns 0 for success.
 */
int radeon_asic_init(struct radeon_device *rdev)
{
	radeon_register_accessor_init(rdev);

	/* set the number of crtcs */
	if (rdev->flags & RADEON_SINGLE_CRTC)
		rdev->num_crtc = 1;
	else
		rdev->num_crtc = 2;

	rdev->has_uvd = false;

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
			rdev->asic->pm.get_engine_clock = &radeon_legacy_get_engine_clock;
			rdev->asic->pm.set_engine_clock = &radeon_legacy_set_engine_clock;
			rdev->asic->pm.get_memory_clock = &radeon_legacy_get_memory_clock;
			rdev->asic->pm.set_memory_clock = NULL;
			rdev->asic->display.set_backlight_level = &radeon_legacy_set_backlight_level;
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
		rdev->asic = &r600_asic;
		break;
	case CHIP_RV610:
	case CHIP_RV630:
	case CHIP_RV620:
	case CHIP_RV635:
	case CHIP_RV670:
		rdev->asic = &rv6xx_asic;
		rdev->has_uvd = true;
		break;
	case CHIP_RS780:
	case CHIP_RS880:
		rdev->asic = &rs780_asic;
		rdev->has_uvd = true;
		break;
	case CHIP_RV770:
	case CHIP_RV730:
	case CHIP_RV710:
	case CHIP_RV740:
		rdev->asic = &rv770_asic;
		rdev->has_uvd = true;
		break;
	case CHIP_CEDAR:
	case CHIP_REDWOOD:
	case CHIP_JUNIPER:
	case CHIP_CYPRESS:
	case CHIP_HEMLOCK:
		/* set num crtcs */
		if (rdev->family == CHIP_CEDAR)
			rdev->num_crtc = 4;
		else
			rdev->num_crtc = 6;
		rdev->asic = &evergreen_asic;
		rdev->has_uvd = true;
		break;
	case CHIP_PALM:
	case CHIP_SUMO:
	case CHIP_SUMO2:
		rdev->asic = &sumo_asic;
		rdev->has_uvd = true;
		break;
	case CHIP_BARTS:
	case CHIP_TURKS:
	case CHIP_CAICOS:
		/* set num crtcs */
		if (rdev->family == CHIP_CAICOS)
			rdev->num_crtc = 4;
		else
			rdev->num_crtc = 6;
		rdev->asic = &btc_asic;
		rdev->has_uvd = true;
		break;
	case CHIP_CAYMAN:
		rdev->asic = &cayman_asic;
		/* set num crtcs */
		rdev->num_crtc = 6;
		rdev->has_uvd = true;
		break;
	case CHIP_ARUBA:
		rdev->asic = &trinity_asic;
		/* set num crtcs */
		rdev->num_crtc = 4;
		rdev->has_uvd = true;
		break;
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
	case CHIP_OLAND:
	case CHIP_HAINAN:
		rdev->asic = &si_asic;
		/* set num crtcs */
		if (rdev->family == CHIP_HAINAN)
			rdev->num_crtc = 0;
		else if (rdev->family == CHIP_OLAND)
			rdev->num_crtc = 2;
		else
			rdev->num_crtc = 6;
		if (rdev->family == CHIP_HAINAN)
			rdev->has_uvd = false;
		else
			rdev->has_uvd = true;
		break;
	case CHIP_BONAIRE:
		rdev->asic = &ci_asic;
		rdev->num_crtc = 6;
		rdev->has_uvd = true;
		break;
	case CHIP_KAVERI:
	case CHIP_KABINI:
		rdev->asic = &kv_asic;
		/* set num crtcs */
		if (rdev->family == CHIP_KAVERI)
			rdev->num_crtc = 4;
		else
			rdev->num_crtc = 2;
		rdev->has_uvd = true;
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	if (rdev->flags & RADEON_IS_IGP) {
		rdev->asic->pm.get_memory_clock = NULL;
		rdev->asic->pm.set_memory_clock = NULL;
	}

	return 0;
}

