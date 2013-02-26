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
#include <linux/slab.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/radeon_drm.h>
#include <linux/vgaarb.h>
#include <linux/vga_switcheroo.h>
#include <linux/efi.h>
#include "radeon_reg.h"
#include "radeon.h"
#include "atom.h"

static const char radeon_family_name[][16] = {
	"R100",
	"RV100",
	"RS100",
	"RV200",
	"RS200",
	"R200",
	"RV250",
	"RS300",
	"RV280",
	"R300",
	"R350",
	"RV350",
	"RV380",
	"R420",
	"R423",
	"RV410",
	"RS400",
	"RS480",
	"RS600",
	"RS690",
	"RS740",
	"RV515",
	"R520",
	"RV530",
	"RV560",
	"RV570",
	"R580",
	"R600",
	"RV610",
	"RV630",
	"RV670",
	"RV620",
	"RV635",
	"RS780",
	"RS880",
	"RV770",
	"RV730",
	"RV710",
	"RV740",
	"CEDAR",
	"REDWOOD",
	"JUNIPER",
	"CYPRESS",
	"HEMLOCK",
	"PALM",
	"SUMO",
	"SUMO2",
	"BARTS",
	"TURKS",
	"CAICOS",
	"CAYMAN",
	"ARUBA",
	"TAHITI",
	"PITCAIRN",
	"VERDE",
	"OLAND",
	"LAST",
};

/**
 * radeon_program_register_sequence - program an array of registers.
 *
 * @rdev: radeon_device pointer
 * @registers: pointer to the register array
 * @array_size: size of the register array
 *
 * Programs an array or registers with and and or masks.
 * This is a helper for setting golden registers.
 */
void radeon_program_register_sequence(struct radeon_device *rdev,
				      const u32 *registers,
				      const u32 array_size)
{
	u32 tmp, reg, and_mask, or_mask;
	int i;

	if (array_size % 3)
		return;

	for (i = 0; i < array_size; i +=3) {
		reg = registers[i + 0];
		and_mask = registers[i + 1];
		or_mask = registers[i + 2];

		if (and_mask == 0xffffffff) {
			tmp = or_mask;
		} else {
			tmp = RREG32(reg);
			tmp &= ~and_mask;
			tmp |= or_mask;
		}
		WREG32(reg, tmp);
	}
}

/**
 * radeon_surface_init - Clear GPU surface registers.
 *
 * @rdev: radeon_device pointer
 *
 * Clear GPU surface registers (r1xx-r5xx).
 */
void radeon_surface_init(struct radeon_device *rdev)
{
	/* FIXME: check this out */
	if (rdev->family < CHIP_R600) {
		int i;

		for (i = 0; i < RADEON_GEM_MAX_SURFACES; i++) {
			if (rdev->surface_regs[i].bo)
				radeon_bo_get_surface_reg(rdev->surface_regs[i].bo);
			else
				radeon_clear_surface_reg(rdev, i);
		}
		/* enable surfaces */
		WREG32(RADEON_SURFACE_CNTL, 0);
	}
}

/*
 * GPU scratch registers helpers function.
 */
/**
 * radeon_scratch_init - Init scratch register driver information.
 *
 * @rdev: radeon_device pointer
 *
 * Init CP scratch register driver information (r1xx-r5xx)
 */
void radeon_scratch_init(struct radeon_device *rdev)
{
	int i;

	/* FIXME: check this out */
	if (rdev->family < CHIP_R300) {
		rdev->scratch.num_reg = 5;
	} else {
		rdev->scratch.num_reg = 7;
	}
	rdev->scratch.reg_base = RADEON_SCRATCH_REG0;
	for (i = 0; i < rdev->scratch.num_reg; i++) {
		rdev->scratch.free[i] = true;
		rdev->scratch.reg[i] = rdev->scratch.reg_base + (i * 4);
	}
}

/**
 * radeon_scratch_get - Allocate a scratch register
 *
 * @rdev: radeon_device pointer
 * @reg: scratch register mmio offset
 *
 * Allocate a CP scratch register for use by the driver (all asics).
 * Returns 0 on success or -EINVAL on failure.
 */
int radeon_scratch_get(struct radeon_device *rdev, uint32_t *reg)
{
	int i;

	for (i = 0; i < rdev->scratch.num_reg; i++) {
		if (rdev->scratch.free[i]) {
			rdev->scratch.free[i] = false;
			*reg = rdev->scratch.reg[i];
			return 0;
		}
	}
	return -EINVAL;
}

/**
 * radeon_scratch_free - Free a scratch register
 *
 * @rdev: radeon_device pointer
 * @reg: scratch register mmio offset
 *
 * Free a CP scratch register allocated for use by the driver (all asics)
 */
void radeon_scratch_free(struct radeon_device *rdev, uint32_t reg)
{
	int i;

	for (i = 0; i < rdev->scratch.num_reg; i++) {
		if (rdev->scratch.reg[i] == reg) {
			rdev->scratch.free[i] = true;
			return;
		}
	}
}

/*
 * radeon_wb_*()
 * Writeback is the the method by which the the GPU updates special pages
 * in memory with the status of certain GPU events (fences, ring pointers,
 * etc.).
 */

/**
 * radeon_wb_disable - Disable Writeback
 *
 * @rdev: radeon_device pointer
 *
 * Disables Writeback (all asics).  Used for suspend.
 */
void radeon_wb_disable(struct radeon_device *rdev)
{
	int r;

	if (rdev->wb.wb_obj) {
		r = radeon_bo_reserve(rdev->wb.wb_obj, false);
		if (unlikely(r != 0))
			return;
		radeon_bo_kunmap(rdev->wb.wb_obj);
		radeon_bo_unpin(rdev->wb.wb_obj);
		radeon_bo_unreserve(rdev->wb.wb_obj);
	}
	rdev->wb.enabled = false;
}

/**
 * radeon_wb_fini - Disable Writeback and free memory
 *
 * @rdev: radeon_device pointer
 *
 * Disables Writeback and frees the Writeback memory (all asics).
 * Used at driver shutdown.
 */
void radeon_wb_fini(struct radeon_device *rdev)
{
	radeon_wb_disable(rdev);
	if (rdev->wb.wb_obj) {
		radeon_bo_unref(&rdev->wb.wb_obj);
		rdev->wb.wb = NULL;
		rdev->wb.wb_obj = NULL;
	}
}

/**
 * radeon_wb_init- Init Writeback driver info and allocate memory
 *
 * @rdev: radeon_device pointer
 *
 * Disables Writeback and frees the Writeback memory (all asics).
 * Used at driver startup.
 * Returns 0 on success or an -error on failure.
 */
int radeon_wb_init(struct radeon_device *rdev)
{
	int r;

	if (rdev->wb.wb_obj == NULL) {
		r = radeon_bo_create(rdev, RADEON_GPU_PAGE_SIZE, PAGE_SIZE, true,
				     RADEON_GEM_DOMAIN_GTT, NULL, &rdev->wb.wb_obj);
		if (r) {
			dev_warn(rdev->dev, "(%d) create WB bo failed\n", r);
			return r;
		}
	}
	r = radeon_bo_reserve(rdev->wb.wb_obj, false);
	if (unlikely(r != 0)) {
		radeon_wb_fini(rdev);
		return r;
	}
	r = radeon_bo_pin(rdev->wb.wb_obj, RADEON_GEM_DOMAIN_GTT,
			  &rdev->wb.gpu_addr);
	if (r) {
		radeon_bo_unreserve(rdev->wb.wb_obj);
		dev_warn(rdev->dev, "(%d) pin WB bo failed\n", r);
		radeon_wb_fini(rdev);
		return r;
	}
	r = radeon_bo_kmap(rdev->wb.wb_obj, (void **)&rdev->wb.wb);
	radeon_bo_unreserve(rdev->wb.wb_obj);
	if (r) {
		dev_warn(rdev->dev, "(%d) map WB bo failed\n", r);
		radeon_wb_fini(rdev);
		return r;
	}

	/* clear wb memory */
	memset((char *)rdev->wb.wb, 0, RADEON_GPU_PAGE_SIZE);
	/* disable event_write fences */
	rdev->wb.use_event = false;
	/* disabled via module param */
	if (radeon_no_wb == 1) {
		rdev->wb.enabled = false;
	} else {
		if (rdev->flags & RADEON_IS_AGP) {
			/* often unreliable on AGP */
			rdev->wb.enabled = false;
		} else if (rdev->family < CHIP_R300) {
			/* often unreliable on pre-r300 */
			rdev->wb.enabled = false;
		} else {
			rdev->wb.enabled = true;
			/* event_write fences are only available on r600+ */
			if (rdev->family >= CHIP_R600) {
				rdev->wb.use_event = true;
			}
		}
	}
	/* always use writeback/events on NI, APUs */
	if (rdev->family >= CHIP_PALM) {
		rdev->wb.enabled = true;
		rdev->wb.use_event = true;
	}

	dev_info(rdev->dev, "WB %sabled\n", rdev->wb.enabled ? "en" : "dis");

	return 0;
}

/**
 * radeon_vram_location - try to find VRAM location
 * @rdev: radeon device structure holding all necessary informations
 * @mc: memory controller structure holding memory informations
 * @base: base address at which to put VRAM
 *
 * Function will place try to place VRAM at base address provided
 * as parameter (which is so far either PCI aperture address or
 * for IGP TOM base address).
 *
 * If there is not enough space to fit the unvisible VRAM in the 32bits
 * address space then we limit the VRAM size to the aperture.
 *
 * If we are using AGP and if the AGP aperture doesn't allow us to have
 * room for all the VRAM than we restrict the VRAM to the PCI aperture
 * size and print a warning.
 *
 * This function will never fails, worst case are limiting VRAM.
 *
 * Note: GTT start, end, size should be initialized before calling this
 * function on AGP platform.
 *
 * Note: We don't explicitly enforce VRAM start to be aligned on VRAM size,
 * this shouldn't be a problem as we are using the PCI aperture as a reference.
 * Otherwise this would be needed for rv280, all r3xx, and all r4xx, but
 * not IGP.
 *
 * Note: we use mc_vram_size as on some board we need to program the mc to
 * cover the whole aperture even if VRAM size is inferior to aperture size
 * Novell bug 204882 + along with lots of ubuntu ones
 *
 * Note: when limiting vram it's safe to overwritte real_vram_size because
 * we are not in case where real_vram_size is inferior to mc_vram_size (ie
 * note afected by bogus hw of Novell bug 204882 + along with lots of ubuntu
 * ones)
 *
 * Note: IGP TOM addr should be the same as the aperture addr, we don't
 * explicitly check for that thought.
 *
 * FIXME: when reducing VRAM size align new size on power of 2.
 */
void radeon_vram_location(struct radeon_device *rdev, struct radeon_mc *mc, u64 base)
{
	uint64_t limit = (uint64_t)radeon_vram_limit << 20;

	mc->vram_start = base;
	if (mc->mc_vram_size > (rdev->mc.mc_mask - base + 1)) {
		dev_warn(rdev->dev, "limiting VRAM to PCI aperture size\n");
		mc->real_vram_size = mc->aper_size;
		mc->mc_vram_size = mc->aper_size;
	}
	mc->vram_end = mc->vram_start + mc->mc_vram_size - 1;
	if (rdev->flags & RADEON_IS_AGP && mc->vram_end > mc->gtt_start && mc->vram_start <= mc->gtt_end) {
		dev_warn(rdev->dev, "limiting VRAM to PCI aperture size\n");
		mc->real_vram_size = mc->aper_size;
		mc->mc_vram_size = mc->aper_size;
	}
	mc->vram_end = mc->vram_start + mc->mc_vram_size - 1;
	if (limit && limit < mc->real_vram_size)
		mc->real_vram_size = limit;
	dev_info(rdev->dev, "VRAM: %lluM 0x%016llX - 0x%016llX (%lluM used)\n",
			mc->mc_vram_size >> 20, mc->vram_start,
			mc->vram_end, mc->real_vram_size >> 20);
}

/**
 * radeon_gtt_location - try to find GTT location
 * @rdev: radeon device structure holding all necessary informations
 * @mc: memory controller structure holding memory informations
 *
 * Function will place try to place GTT before or after VRAM.
 *
 * If GTT size is bigger than space left then we ajust GTT size.
 * Thus function will never fails.
 *
 * FIXME: when reducing GTT size align new size on power of 2.
 */
void radeon_gtt_location(struct radeon_device *rdev, struct radeon_mc *mc)
{
	u64 size_af, size_bf;

	size_af = ((rdev->mc.mc_mask - mc->vram_end) + mc->gtt_base_align) & ~mc->gtt_base_align;
	size_bf = mc->vram_start & ~mc->gtt_base_align;
	if (size_bf > size_af) {
		if (mc->gtt_size > size_bf) {
			dev_warn(rdev->dev, "limiting GTT\n");
			mc->gtt_size = size_bf;
		}
		mc->gtt_start = (mc->vram_start & ~mc->gtt_base_align) - mc->gtt_size;
	} else {
		if (mc->gtt_size > size_af) {
			dev_warn(rdev->dev, "limiting GTT\n");
			mc->gtt_size = size_af;
		}
		mc->gtt_start = (mc->vram_end + 1 + mc->gtt_base_align) & ~mc->gtt_base_align;
	}
	mc->gtt_end = mc->gtt_start + mc->gtt_size - 1;
	dev_info(rdev->dev, "GTT: %lluM 0x%016llX - 0x%016llX\n",
			mc->gtt_size >> 20, mc->gtt_start, mc->gtt_end);
}

/*
 * GPU helpers function.
 */
/**
 * radeon_card_posted - check if the hw has already been initialized
 *
 * @rdev: radeon_device pointer
 *
 * Check if the asic has been initialized (all asics).
 * Used at driver startup.
 * Returns true if initialized or false if not.
 */
bool radeon_card_posted(struct radeon_device *rdev)
{
	uint32_t reg;

	if (efi_enabled(EFI_BOOT) &&
	    rdev->pdev->subsystem_vendor == PCI_VENDOR_ID_APPLE)
		return false;

	/* first check CRTCs */
	if (ASIC_IS_DCE41(rdev)) {
		reg = RREG32(EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC0_REGISTER_OFFSET) |
			RREG32(EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC1_REGISTER_OFFSET);
		if (reg & EVERGREEN_CRTC_MASTER_EN)
			return true;
	} else if (ASIC_IS_DCE4(rdev)) {
		reg = RREG32(EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC0_REGISTER_OFFSET) |
			RREG32(EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC1_REGISTER_OFFSET) |
			RREG32(EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC2_REGISTER_OFFSET) |
			RREG32(EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC3_REGISTER_OFFSET) |
			RREG32(EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC4_REGISTER_OFFSET) |
			RREG32(EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC5_REGISTER_OFFSET);
		if (reg & EVERGREEN_CRTC_MASTER_EN)
			return true;
	} else if (ASIC_IS_AVIVO(rdev)) {
		reg = RREG32(AVIVO_D1CRTC_CONTROL) |
		      RREG32(AVIVO_D2CRTC_CONTROL);
		if (reg & AVIVO_CRTC_EN) {
			return true;
		}
	} else {
		reg = RREG32(RADEON_CRTC_GEN_CNTL) |
		      RREG32(RADEON_CRTC2_GEN_CNTL);
		if (reg & RADEON_CRTC_EN) {
			return true;
		}
	}

	/* then check MEM_SIZE, in case the crtcs are off */
	if (rdev->family >= CHIP_R600)
		reg = RREG32(R600_CONFIG_MEMSIZE);
	else
		reg = RREG32(RADEON_CONFIG_MEMSIZE);

	if (reg)
		return true;

	return false;

}

/**
 * radeon_update_bandwidth_info - update display bandwidth params
 *
 * @rdev: radeon_device pointer
 *
 * Used when sclk/mclk are switched or display modes are set.
 * params are used to calculate display watermarks (all asics)
 */
void radeon_update_bandwidth_info(struct radeon_device *rdev)
{
	fixed20_12 a;
	u32 sclk = rdev->pm.current_sclk;
	u32 mclk = rdev->pm.current_mclk;

	/* sclk/mclk in Mhz */
	a.full = dfixed_const(100);
	rdev->pm.sclk.full = dfixed_const(sclk);
	rdev->pm.sclk.full = dfixed_div(rdev->pm.sclk, a);
	rdev->pm.mclk.full = dfixed_const(mclk);
	rdev->pm.mclk.full = dfixed_div(rdev->pm.mclk, a);

	if (rdev->flags & RADEON_IS_IGP) {
		a.full = dfixed_const(16);
		/* core_bandwidth = sclk(Mhz) * 16 */
		rdev->pm.core_bandwidth.full = dfixed_div(rdev->pm.sclk, a);
	}
}

/**
 * radeon_boot_test_post_card - check and possibly initialize the hw
 *
 * @rdev: radeon_device pointer
 *
 * Check if the asic is initialized and if not, attempt to initialize
 * it (all asics).
 * Returns true if initialized or false if not.
 */
bool radeon_boot_test_post_card(struct radeon_device *rdev)
{
	if (radeon_card_posted(rdev))
		return true;

	if (rdev->bios) {
		DRM_INFO("GPU not posted. posting now...\n");
		if (rdev->is_atom_bios)
			atom_asic_init(rdev->mode_info.atom_context);
		else
			radeon_combios_asic_init(rdev->ddev);
		return true;
	} else {
		dev_err(rdev->dev, "Card not posted and no BIOS - ignoring\n");
		return false;
	}
}

/**
 * radeon_dummy_page_init - init dummy page used by the driver
 *
 * @rdev: radeon_device pointer
 *
 * Allocate the dummy page used by the driver (all asics).
 * This dummy page is used by the driver as a filler for gart entries
 * when pages are taken out of the GART
 * Returns 0 on sucess, -ENOMEM on failure.
 */
int radeon_dummy_page_init(struct radeon_device *rdev)
{
	if (rdev->dummy_page.page)
		return 0;
	rdev->dummy_page.page = alloc_page(GFP_DMA32 | GFP_KERNEL | __GFP_ZERO);
	if (rdev->dummy_page.page == NULL)
		return -ENOMEM;
	rdev->dummy_page.addr = pci_map_page(rdev->pdev, rdev->dummy_page.page,
					0, PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	if (pci_dma_mapping_error(rdev->pdev, rdev->dummy_page.addr)) {
		dev_err(&rdev->pdev->dev, "Failed to DMA MAP the dummy page\n");
		__free_page(rdev->dummy_page.page);
		rdev->dummy_page.page = NULL;
		return -ENOMEM;
	}
	return 0;
}

/**
 * radeon_dummy_page_fini - free dummy page used by the driver
 *
 * @rdev: radeon_device pointer
 *
 * Frees the dummy page used by the driver (all asics).
 */
void radeon_dummy_page_fini(struct radeon_device *rdev)
{
	if (rdev->dummy_page.page == NULL)
		return;
	pci_unmap_page(rdev->pdev, rdev->dummy_page.addr,
			PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	__free_page(rdev->dummy_page.page);
	rdev->dummy_page.page = NULL;
}


/* ATOM accessor methods */
/*
 * ATOM is an interpreted byte code stored in tables in the vbios.  The
 * driver registers callbacks to access registers and the interpreter
 * in the driver parses the tables and executes then to program specific
 * actions (set display modes, asic init, etc.).  See radeon_atombios.c,
 * atombios.h, and atom.c
 */

/**
 * cail_pll_read - read PLL register
 *
 * @info: atom card_info pointer
 * @reg: PLL register offset
 *
 * Provides a PLL register accessor for the atom interpreter (r4xx+).
 * Returns the value of the PLL register.
 */
static uint32_t cail_pll_read(struct card_info *info, uint32_t reg)
{
	struct radeon_device *rdev = info->dev->dev_private;
	uint32_t r;

	r = rdev->pll_rreg(rdev, reg);
	return r;
}

/**
 * cail_pll_write - write PLL register
 *
 * @info: atom card_info pointer
 * @reg: PLL register offset
 * @val: value to write to the pll register
 *
 * Provides a PLL register accessor for the atom interpreter (r4xx+).
 */
static void cail_pll_write(struct card_info *info, uint32_t reg, uint32_t val)
{
	struct radeon_device *rdev = info->dev->dev_private;

	rdev->pll_wreg(rdev, reg, val);
}

/**
 * cail_mc_read - read MC (Memory Controller) register
 *
 * @info: atom card_info pointer
 * @reg: MC register offset
 *
 * Provides an MC register accessor for the atom interpreter (r4xx+).
 * Returns the value of the MC register.
 */
static uint32_t cail_mc_read(struct card_info *info, uint32_t reg)
{
	struct radeon_device *rdev = info->dev->dev_private;
	uint32_t r;

	r = rdev->mc_rreg(rdev, reg);
	return r;
}

/**
 * cail_mc_write - write MC (Memory Controller) register
 *
 * @info: atom card_info pointer
 * @reg: MC register offset
 * @val: value to write to the pll register
 *
 * Provides a MC register accessor for the atom interpreter (r4xx+).
 */
static void cail_mc_write(struct card_info *info, uint32_t reg, uint32_t val)
{
	struct radeon_device *rdev = info->dev->dev_private;

	rdev->mc_wreg(rdev, reg, val);
}

/**
 * cail_reg_write - write MMIO register
 *
 * @info: atom card_info pointer
 * @reg: MMIO register offset
 * @val: value to write to the pll register
 *
 * Provides a MMIO register accessor for the atom interpreter (r4xx+).
 */
static void cail_reg_write(struct card_info *info, uint32_t reg, uint32_t val)
{
	struct radeon_device *rdev = info->dev->dev_private;

	WREG32(reg*4, val);
}

/**
 * cail_reg_read - read MMIO register
 *
 * @info: atom card_info pointer
 * @reg: MMIO register offset
 *
 * Provides an MMIO register accessor for the atom interpreter (r4xx+).
 * Returns the value of the MMIO register.
 */
static uint32_t cail_reg_read(struct card_info *info, uint32_t reg)
{
	struct radeon_device *rdev = info->dev->dev_private;
	uint32_t r;

	r = RREG32(reg*4);
	return r;
}

/**
 * cail_ioreg_write - write IO register
 *
 * @info: atom card_info pointer
 * @reg: IO register offset
 * @val: value to write to the pll register
 *
 * Provides a IO register accessor for the atom interpreter (r4xx+).
 */
static void cail_ioreg_write(struct card_info *info, uint32_t reg, uint32_t val)
{
	struct radeon_device *rdev = info->dev->dev_private;

	WREG32_IO(reg*4, val);
}

/**
 * cail_ioreg_read - read IO register
 *
 * @info: atom card_info pointer
 * @reg: IO register offset
 *
 * Provides an IO register accessor for the atom interpreter (r4xx+).
 * Returns the value of the IO register.
 */
static uint32_t cail_ioreg_read(struct card_info *info, uint32_t reg)
{
	struct radeon_device *rdev = info->dev->dev_private;
	uint32_t r;

	r = RREG32_IO(reg*4);
	return r;
}

/**
 * radeon_atombios_init - init the driver info and callbacks for atombios
 *
 * @rdev: radeon_device pointer
 *
 * Initializes the driver info and register access callbacks for the
 * ATOM interpreter (r4xx+).
 * Returns 0 on sucess, -ENOMEM on failure.
 * Called at driver startup.
 */
int radeon_atombios_init(struct radeon_device *rdev)
{
	struct card_info *atom_card_info =
	    kzalloc(sizeof(struct card_info), GFP_KERNEL);

	if (!atom_card_info)
		return -ENOMEM;

	rdev->mode_info.atom_card_info = atom_card_info;
	atom_card_info->dev = rdev->ddev;
	atom_card_info->reg_read = cail_reg_read;
	atom_card_info->reg_write = cail_reg_write;
	/* needed for iio ops */
	if (rdev->rio_mem) {
		atom_card_info->ioreg_read = cail_ioreg_read;
		atom_card_info->ioreg_write = cail_ioreg_write;
	} else {
		DRM_ERROR("Unable to find PCI I/O BAR; using MMIO for ATOM IIO\n");
		atom_card_info->ioreg_read = cail_reg_read;
		atom_card_info->ioreg_write = cail_reg_write;
	}
	atom_card_info->mc_read = cail_mc_read;
	atom_card_info->mc_write = cail_mc_write;
	atom_card_info->pll_read = cail_pll_read;
	atom_card_info->pll_write = cail_pll_write;

	rdev->mode_info.atom_context = atom_parse(atom_card_info, rdev->bios);
	if (!rdev->mode_info.atom_context) {
		radeon_atombios_fini(rdev);
		return -ENOMEM;
	}

	mutex_init(&rdev->mode_info.atom_context->mutex);
	radeon_atom_initialize_bios_scratch_regs(rdev->ddev);
	atom_allocate_fb_scratch(rdev->mode_info.atom_context);
	return 0;
}

/**
 * radeon_atombios_fini - free the driver info and callbacks for atombios
 *
 * @rdev: radeon_device pointer
 *
 * Frees the driver info and register access callbacks for the ATOM
 * interpreter (r4xx+).
 * Called at driver shutdown.
 */
void radeon_atombios_fini(struct radeon_device *rdev)
{
	if (rdev->mode_info.atom_context) {
		kfree(rdev->mode_info.atom_context->scratch);
	}
	kfree(rdev->mode_info.atom_context);
	rdev->mode_info.atom_context = NULL;
	kfree(rdev->mode_info.atom_card_info);
	rdev->mode_info.atom_card_info = NULL;
}

/* COMBIOS */
/*
 * COMBIOS is the bios format prior to ATOM. It provides
 * command tables similar to ATOM, but doesn't have a unified
 * parser.  See radeon_combios.c
 */

/**
 * radeon_combios_init - init the driver info for combios
 *
 * @rdev: radeon_device pointer
 *
 * Initializes the driver info for combios (r1xx-r3xx).
 * Returns 0 on sucess.
 * Called at driver startup.
 */
int radeon_combios_init(struct radeon_device *rdev)
{
	radeon_combios_initialize_bios_scratch_regs(rdev->ddev);
	return 0;
}

/**
 * radeon_combios_fini - free the driver info for combios
 *
 * @rdev: radeon_device pointer
 *
 * Frees the driver info for combios (r1xx-r3xx).
 * Called at driver shutdown.
 */
void radeon_combios_fini(struct radeon_device *rdev)
{
}

/* if we get transitioned to only one device, take VGA back */
/**
 * radeon_vga_set_decode - enable/disable vga decode
 *
 * @cookie: radeon_device pointer
 * @state: enable/disable vga decode
 *
 * Enable/disable vga decode (all asics).
 * Returns VGA resource flags.
 */
static unsigned int radeon_vga_set_decode(void *cookie, bool state)
{
	struct radeon_device *rdev = cookie;
	radeon_vga_set_state(rdev, state);
	if (state)
		return VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
		       VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
	else
		return VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
}

/**
 * radeon_check_pot_argument - check that argument is a power of two
 *
 * @arg: value to check
 *
 * Validates that a certain argument is a power of two (all asics).
 * Returns true if argument is valid.
 */
static bool radeon_check_pot_argument(int arg)
{
	return (arg & (arg - 1)) == 0;
}

/**
 * radeon_check_arguments - validate module params
 *
 * @rdev: radeon_device pointer
 *
 * Validates certain module parameters and updates
 * the associated values used by the driver (all asics).
 */
static void radeon_check_arguments(struct radeon_device *rdev)
{
	/* vramlimit must be a power of two */
	if (!radeon_check_pot_argument(radeon_vram_limit)) {
		dev_warn(rdev->dev, "vram limit (%d) must be a power of 2\n",
				radeon_vram_limit);
		radeon_vram_limit = 0;
	}

	/* gtt size must be power of two and greater or equal to 32M */
	if (radeon_gart_size < 32) {
		dev_warn(rdev->dev, "gart size (%d) too small forcing to 512M\n",
				radeon_gart_size);
		radeon_gart_size = 512;

	} else if (!radeon_check_pot_argument(radeon_gart_size)) {
		dev_warn(rdev->dev, "gart size (%d) must be a power of 2\n",
				radeon_gart_size);
		radeon_gart_size = 512;
	}
	rdev->mc.gtt_size = (uint64_t)radeon_gart_size << 20;

	/* AGP mode can only be -1, 1, 2, 4, 8 */
	switch (radeon_agpmode) {
	case -1:
	case 0:
	case 1:
	case 2:
	case 4:
	case 8:
		break;
	default:
		dev_warn(rdev->dev, "invalid AGP mode %d (valid mode: "
				"-1, 0, 1, 2, 4, 8)\n", radeon_agpmode);
		radeon_agpmode = 0;
		break;
	}
}

/**
 * radeon_switcheroo_quirk_long_wakeup - return true if longer d3 delay is
 * needed for waking up.
 *
 * @pdev: pci dev pointer
 */
static bool radeon_switcheroo_quirk_long_wakeup(struct pci_dev *pdev)
{

	/* 6600m in a macbook pro */
	if (pdev->subsystem_vendor == PCI_VENDOR_ID_APPLE &&
	    pdev->subsystem_device == 0x00e2) {
		printk(KERN_INFO "radeon: quirking longer d3 wakeup delay\n");
		return true;
	}

	return false;
}

/**
 * radeon_switcheroo_set_state - set switcheroo state
 *
 * @pdev: pci dev pointer
 * @state: vga switcheroo state
 *
 * Callback for the switcheroo driver.  Suspends or resumes the
 * the asics before or after it is powered up using ACPI methods.
 */
static void radeon_switcheroo_set_state(struct pci_dev *pdev, enum vga_switcheroo_state state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	pm_message_t pmm = { .event = PM_EVENT_SUSPEND };
	if (state == VGA_SWITCHEROO_ON) {
		unsigned d3_delay = dev->pdev->d3_delay;

		printk(KERN_INFO "radeon: switched on\n");
		/* don't suspend or resume card normally */
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;

		if (d3_delay < 20 && radeon_switcheroo_quirk_long_wakeup(pdev))
			dev->pdev->d3_delay = 20;

		radeon_resume_kms(dev);

		dev->pdev->d3_delay = d3_delay;

		dev->switch_power_state = DRM_SWITCH_POWER_ON;
		drm_kms_helper_poll_enable(dev);
	} else {
		printk(KERN_INFO "radeon: switched off\n");
		drm_kms_helper_poll_disable(dev);
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		radeon_suspend_kms(dev, pmm);
		dev->switch_power_state = DRM_SWITCH_POWER_OFF;
	}
}

/**
 * radeon_switcheroo_can_switch - see if switcheroo state can change
 *
 * @pdev: pci dev pointer
 *
 * Callback for the switcheroo driver.  Check of the switcheroo
 * state can be changed.
 * Returns true if the state can be changed, false if not.
 */
static bool radeon_switcheroo_can_switch(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	bool can_switch;

	spin_lock(&dev->count_lock);
	can_switch = (dev->open_count == 0);
	spin_unlock(&dev->count_lock);
	return can_switch;
}

static const struct vga_switcheroo_client_ops radeon_switcheroo_ops = {
	.set_gpu_state = radeon_switcheroo_set_state,
	.reprobe = NULL,
	.can_switch = radeon_switcheroo_can_switch,
};

/**
 * radeon_device_init - initialize the driver
 *
 * @rdev: radeon_device pointer
 * @pdev: drm dev pointer
 * @pdev: pci dev pointer
 * @flags: driver flags
 *
 * Initializes the driver info and hw (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver startup.
 */
int radeon_device_init(struct radeon_device *rdev,
		       struct drm_device *ddev,
		       struct pci_dev *pdev,
		       uint32_t flags)
{
	int r, i;
	int dma_bits;

	rdev->shutdown = false;
	rdev->dev = &pdev->dev;
	rdev->ddev = ddev;
	rdev->pdev = pdev;
	rdev->flags = flags;
	rdev->family = flags & RADEON_FAMILY_MASK;
	rdev->is_atom_bios = false;
	rdev->usec_timeout = RADEON_MAX_USEC_TIMEOUT;
	rdev->mc.gtt_size = radeon_gart_size * 1024 * 1024;
	rdev->accel_working = false;
	/* set up ring ids */
	for (i = 0; i < RADEON_NUM_RINGS; i++) {
		rdev->ring[i].idx = i;
	}

	DRM_INFO("initializing kernel modesetting (%s 0x%04X:0x%04X 0x%04X:0x%04X).\n",
		radeon_family_name[rdev->family], pdev->vendor, pdev->device,
		pdev->subsystem_vendor, pdev->subsystem_device);

	/* mutex initialization are all done here so we
	 * can recall function without having locking issues */
	mutex_init(&rdev->ring_lock);
	mutex_init(&rdev->dc_hw_i2c_mutex);
	atomic_set(&rdev->ih.lock, 0);
	mutex_init(&rdev->gem.mutex);
	mutex_init(&rdev->pm.mutex);
	mutex_init(&rdev->gpu_clock_mutex);
	init_rwsem(&rdev->pm.mclk_lock);
	init_rwsem(&rdev->exclusive_lock);
	init_waitqueue_head(&rdev->irq.vblank_queue);
	r = radeon_gem_init(rdev);
	if (r)
		return r;
	/* initialize vm here */
	mutex_init(&rdev->vm_manager.lock);
	/* Adjust VM size here.
	 * Currently set to 4GB ((1 << 20) 4k pages).
	 * Max GPUVM size for cayman and SI is 40 bits.
	 */
	rdev->vm_manager.max_pfn = 1 << 20;
	INIT_LIST_HEAD(&rdev->vm_manager.lru_vm);

	/* Set asic functions */
	r = radeon_asic_init(rdev);
	if (r)
		return r;
	radeon_check_arguments(rdev);

	/* all of the newer IGP chips have an internal gart
	 * However some rs4xx report as AGP, so remove that here.
	 */
	if ((rdev->family >= CHIP_RS400) &&
	    (rdev->flags & RADEON_IS_IGP)) {
		rdev->flags &= ~RADEON_IS_AGP;
	}

	if (rdev->flags & RADEON_IS_AGP && radeon_agpmode == -1) {
		radeon_agp_disable(rdev);
	}

	/* Set the internal MC address mask
	 * This is the max address of the GPU's
	 * internal address space.
	 */
	if (rdev->family >= CHIP_CAYMAN)
		rdev->mc.mc_mask = 0xffffffffffULL; /* 40 bit MC */
	else if (rdev->family >= CHIP_CEDAR)
		rdev->mc.mc_mask = 0xfffffffffULL; /* 36 bit MC */
	else
		rdev->mc.mc_mask = 0xffffffffULL; /* 32 bit MC */

	/* set DMA mask + need_dma32 flags.
	 * PCIE - can handle 40-bits.
	 * IGP - can handle 40-bits
	 * AGP - generally dma32 is safest
	 * PCI - dma32 for legacy pci gart, 40 bits on newer asics
	 */
	rdev->need_dma32 = false;
	if (rdev->flags & RADEON_IS_AGP)
		rdev->need_dma32 = true;
	if ((rdev->flags & RADEON_IS_PCI) &&
	    (rdev->family <= CHIP_RS740))
		rdev->need_dma32 = true;

	dma_bits = rdev->need_dma32 ? 32 : 40;
	r = pci_set_dma_mask(rdev->pdev, DMA_BIT_MASK(dma_bits));
	if (r) {
		rdev->need_dma32 = true;
		dma_bits = 32;
		printk(KERN_WARNING "radeon: No suitable DMA available.\n");
	}
	r = pci_set_consistent_dma_mask(rdev->pdev, DMA_BIT_MASK(dma_bits));
	if (r) {
		pci_set_consistent_dma_mask(rdev->pdev, DMA_BIT_MASK(32));
		printk(KERN_WARNING "radeon: No coherent DMA available.\n");
	}

	/* Registers mapping */
	/* TODO: block userspace mapping of io register */
	spin_lock_init(&rdev->mmio_idx_lock);
	rdev->rmmio_base = pci_resource_start(rdev->pdev, 2);
	rdev->rmmio_size = pci_resource_len(rdev->pdev, 2);
	rdev->rmmio = ioremap(rdev->rmmio_base, rdev->rmmio_size);
	if (rdev->rmmio == NULL) {
		return -ENOMEM;
	}
	DRM_INFO("register mmio base: 0x%08X\n", (uint32_t)rdev->rmmio_base);
	DRM_INFO("register mmio size: %u\n", (unsigned)rdev->rmmio_size);

	/* io port mapping */
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		if (pci_resource_flags(rdev->pdev, i) & IORESOURCE_IO) {
			rdev->rio_mem_size = pci_resource_len(rdev->pdev, i);
			rdev->rio_mem = pci_iomap(rdev->pdev, i, rdev->rio_mem_size);
			break;
		}
	}
	if (rdev->rio_mem == NULL)
		DRM_ERROR("Unable to find PCI I/O BAR\n");

	/* if we have > 1 VGA cards, then disable the radeon VGA resources */
	/* this will fail for cards that aren't VGA class devices, just
	 * ignore it */
	vga_client_register(rdev->pdev, rdev, NULL, radeon_vga_set_decode);
	vga_switcheroo_register_client(rdev->pdev, &radeon_switcheroo_ops);

	r = radeon_init(rdev);
	if (r)
		return r;

	r = radeon_ib_ring_tests(rdev);
	if (r)
		DRM_ERROR("ib ring test failed (%d).\n", r);

	if (rdev->flags & RADEON_IS_AGP && !rdev->accel_working) {
		/* Acceleration not working on AGP card try again
		 * with fallback to PCI or PCIE GART
		 */
		radeon_asic_reset(rdev);
		radeon_fini(rdev);
		radeon_agp_disable(rdev);
		r = radeon_init(rdev);
		if (r)
			return r;
	}
	if ((radeon_testing & 1)) {
		radeon_test_moves(rdev);
	}
	if ((radeon_testing & 2)) {
		radeon_test_syncing(rdev);
	}
	if (radeon_benchmarking) {
		radeon_benchmark(rdev, radeon_benchmarking);
	}
	return 0;
}

static void radeon_debugfs_remove_files(struct radeon_device *rdev);

/**
 * radeon_device_fini - tear down the driver
 *
 * @rdev: radeon_device pointer
 *
 * Tear down the driver info (all asics).
 * Called at driver shutdown.
 */
void radeon_device_fini(struct radeon_device *rdev)
{
	DRM_INFO("radeon: finishing device.\n");
	rdev->shutdown = true;
	/* evict vram memory */
	radeon_bo_evict_vram(rdev);
	radeon_fini(rdev);
	vga_switcheroo_unregister_client(rdev->pdev);
	vga_client_register(rdev->pdev, NULL, NULL, NULL);
	if (rdev->rio_mem)
		pci_iounmap(rdev->pdev, rdev->rio_mem);
	rdev->rio_mem = NULL;
	iounmap(rdev->rmmio);
	rdev->rmmio = NULL;
	radeon_debugfs_remove_files(rdev);
}


/*
 * Suspend & resume.
 */
/**
 * radeon_suspend_kms - initiate device suspend
 *
 * @pdev: drm dev pointer
 * @state: suspend state
 *
 * Puts the hw in the suspend state (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver suspend.
 */
int radeon_suspend_kms(struct drm_device *dev, pm_message_t state)
{
	struct radeon_device *rdev;
	struct drm_crtc *crtc;
	struct drm_connector *connector;
	int i, r;
	bool force_completion = false;

	if (dev == NULL || dev->dev_private == NULL) {
		return -ENODEV;
	}
	if (state.event == PM_EVENT_PRETHAW) {
		return 0;
	}
	rdev = dev->dev_private;

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	drm_kms_helper_poll_disable(dev);

	/* turn off display hw */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		drm_helper_connector_dpms(connector, DRM_MODE_DPMS_OFF);
	}

	/* unpin the front buffers */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct radeon_framebuffer *rfb = to_radeon_framebuffer(crtc->fb);
		struct radeon_bo *robj;

		if (rfb == NULL || rfb->obj == NULL) {
			continue;
		}
		robj = gem_to_radeon_bo(rfb->obj);
		/* don't unpin kernel fb objects */
		if (!radeon_fbdev_robj_is_fb(rdev, robj)) {
			r = radeon_bo_reserve(robj, false);
			if (r == 0) {
				radeon_bo_unpin(robj);
				radeon_bo_unreserve(robj);
			}
		}
	}
	/* evict vram memory */
	radeon_bo_evict_vram(rdev);

	mutex_lock(&rdev->ring_lock);
	/* wait for gpu to finish processing current batch */
	for (i = 0; i < RADEON_NUM_RINGS; i++) {
		r = radeon_fence_wait_empty_locked(rdev, i);
		if (r) {
			/* delay GPU reset to resume */
			force_completion = true;
		}
	}
	if (force_completion) {
		radeon_fence_driver_force_completion(rdev);
	}
	mutex_unlock(&rdev->ring_lock);

	radeon_save_bios_scratch_regs(rdev);

	radeon_pm_suspend(rdev);
	radeon_suspend(rdev);
	radeon_hpd_fini(rdev);
	/* evict remaining vram memory */
	radeon_bo_evict_vram(rdev);

	radeon_agp_suspend(rdev);

	pci_save_state(dev->pdev);
	if (state.event == PM_EVENT_SUSPEND) {
		/* Shut down the device */
		pci_disable_device(dev->pdev);
		pci_set_power_state(dev->pdev, PCI_D3hot);
	}
	console_lock();
	radeon_fbdev_set_suspend(rdev, 1);
	console_unlock();
	return 0;
}

/**
 * radeon_resume_kms - initiate device resume
 *
 * @pdev: drm dev pointer
 *
 * Bring the hw back to operating state (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver resume.
 */
int radeon_resume_kms(struct drm_device *dev)
{
	struct drm_connector *connector;
	struct radeon_device *rdev = dev->dev_private;
	int r;

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	console_lock();
	pci_set_power_state(dev->pdev, PCI_D0);
	pci_restore_state(dev->pdev);
	if (pci_enable_device(dev->pdev)) {
		console_unlock();
		return -1;
	}
	/* resume AGP if in use */
	radeon_agp_resume(rdev);
	radeon_resume(rdev);

	r = radeon_ib_ring_tests(rdev);
	if (r)
		DRM_ERROR("ib ring test failed (%d).\n", r);

	radeon_pm_resume(rdev);
	radeon_restore_bios_scratch_regs(rdev);

	radeon_fbdev_set_suspend(rdev, 0);
	console_unlock();

	/* init dig PHYs, disp eng pll */
	if (rdev->is_atom_bios) {
		radeon_atom_encoder_init(rdev);
		radeon_atom_disp_eng_pll_init(rdev);
		/* turn on the BL */
		if (rdev->mode_info.bl_encoder) {
			u8 bl_level = radeon_get_backlight_level(rdev,
								 rdev->mode_info.bl_encoder);
			radeon_set_backlight_level(rdev, rdev->mode_info.bl_encoder,
						   bl_level);
		}
	}
	/* reset hpd state */
	radeon_hpd_init(rdev);
	/* blat the mode back in */
	drm_helper_resume_force_mode(dev);
	/* turn on display hw */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		drm_helper_connector_dpms(connector, DRM_MODE_DPMS_ON);
	}

	drm_kms_helper_poll_enable(dev);
	return 0;
}

/**
 * radeon_gpu_reset - reset the asic
 *
 * @rdev: radeon device pointer
 *
 * Attempt the reset the GPU if it has hung (all asics).
 * Returns 0 for success or an error on failure.
 */
int radeon_gpu_reset(struct radeon_device *rdev)
{
	unsigned ring_sizes[RADEON_NUM_RINGS];
	uint32_t *ring_data[RADEON_NUM_RINGS];

	bool saved = false;

	int i, r;
	int resched;

	down_write(&rdev->exclusive_lock);
	radeon_save_bios_scratch_regs(rdev);
	/* block TTM */
	resched = ttm_bo_lock_delayed_workqueue(&rdev->mman.bdev);
	radeon_suspend(rdev);

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		ring_sizes[i] = radeon_ring_backup(rdev, &rdev->ring[i],
						   &ring_data[i]);
		if (ring_sizes[i]) {
			saved = true;
			dev_info(rdev->dev, "Saved %d dwords of commands "
				 "on ring %d.\n", ring_sizes[i], i);
		}
	}

retry:
	r = radeon_asic_reset(rdev);
	if (!r) {
		dev_info(rdev->dev, "GPU reset succeeded, trying to resume\n");
		radeon_resume(rdev);
	}

	radeon_restore_bios_scratch_regs(rdev);

	if (!r) {
		for (i = 0; i < RADEON_NUM_RINGS; ++i) {
			radeon_ring_restore(rdev, &rdev->ring[i],
					    ring_sizes[i], ring_data[i]);
			ring_sizes[i] = 0;
			ring_data[i] = NULL;
		}

		r = radeon_ib_ring_tests(rdev);
		if (r) {
			dev_err(rdev->dev, "ib ring test failed (%d).\n", r);
			if (saved) {
				saved = false;
				radeon_suspend(rdev);
				goto retry;
			}
		}
	} else {
		radeon_fence_driver_force_completion(rdev);
		for (i = 0; i < RADEON_NUM_RINGS; ++i) {
			kfree(ring_data[i]);
		}
	}

	drm_helper_resume_force_mode(rdev->ddev);

	ttm_bo_unlock_delayed_workqueue(&rdev->mman.bdev, resched);
	if (r) {
		/* bad news, how to tell it to userspace ? */
		dev_info(rdev->dev, "GPU reset failed\n");
	}

	up_write(&rdev->exclusive_lock);
	return r;
}


/*
 * Debugfs
 */
int radeon_debugfs_add_files(struct radeon_device *rdev,
			     struct drm_info_list *files,
			     unsigned nfiles)
{
	unsigned i;

	for (i = 0; i < rdev->debugfs_count; i++) {
		if (rdev->debugfs[i].files == files) {
			/* Already registered */
			return 0;
		}
	}

	i = rdev->debugfs_count + 1;
	if (i > RADEON_DEBUGFS_MAX_COMPONENTS) {
		DRM_ERROR("Reached maximum number of debugfs components.\n");
		DRM_ERROR("Report so we increase "
		          "RADEON_DEBUGFS_MAX_COMPONENTS.\n");
		return -EINVAL;
	}
	rdev->debugfs[rdev->debugfs_count].files = files;
	rdev->debugfs[rdev->debugfs_count].num_files = nfiles;
	rdev->debugfs_count = i;
#if defined(CONFIG_DEBUG_FS)
	drm_debugfs_create_files(files, nfiles,
				 rdev->ddev->control->debugfs_root,
				 rdev->ddev->control);
	drm_debugfs_create_files(files, nfiles,
				 rdev->ddev->primary->debugfs_root,
				 rdev->ddev->primary);
#endif
	return 0;
}

static void radeon_debugfs_remove_files(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	unsigned i;

	for (i = 0; i < rdev->debugfs_count; i++) {
		drm_debugfs_remove_files(rdev->debugfs[i].files,
					 rdev->debugfs[i].num_files,
					 rdev->ddev->control);
		drm_debugfs_remove_files(rdev->debugfs[i].files,
					 rdev->debugfs[i].num_files,
					 rdev->ddev->primary);
	}
#endif
}

#if defined(CONFIG_DEBUG_FS)
int radeon_debugfs_init(struct drm_minor *minor)
{
	return 0;
}

void radeon_debugfs_cleanup(struct drm_minor *minor)
{
}
#endif
