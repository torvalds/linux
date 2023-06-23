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
#include <linux/efi.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/vga_switcheroo.h>
#include <linux/vgaarb.h>

#include <drm/drm_cache.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_probe_helper.h>
#include <drm/radeon_drm.h>

#include "radeon_device.h"
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
	"HAINAN",
	"BONAIRE",
	"KAVERI",
	"KABINI",
	"HAWAII",
	"MULLINS",
	"LAST",
};

#if defined(CONFIG_VGA_SWITCHEROO)
bool radeon_has_atpx_dgpu_power_cntl(void);
bool radeon_is_atpx_hybrid(void);
#else
static inline bool radeon_has_atpx_dgpu_power_cntl(void) { return false; }
static inline bool radeon_is_atpx_hybrid(void) { return false; }
#endif

#define RADEON_PX_QUIRK_DISABLE_PX  (1 << 0)

struct radeon_px_quirk {
	u32 chip_vendor;
	u32 chip_device;
	u32 subsys_vendor;
	u32 subsys_device;
	u32 px_quirk_flags;
};

static struct radeon_px_quirk radeon_px_quirk_list[] = {
	/* Acer aspire 5560g (CPU: AMD A4-3305M; GPU: AMD Radeon HD 6480g + 7470m)
	 * https://bugzilla.kernel.org/show_bug.cgi?id=74551
	 */
	{ PCI_VENDOR_ID_ATI, 0x6760, 0x1025, 0x0672, RADEON_PX_QUIRK_DISABLE_PX },
	/* Asus K73TA laptop with AMD A6-3400M APU and Radeon 6550 GPU
	 * https://bugzilla.kernel.org/show_bug.cgi?id=51381
	 */
	{ PCI_VENDOR_ID_ATI, 0x6741, 0x1043, 0x108c, RADEON_PX_QUIRK_DISABLE_PX },
	/* Asus K53TK laptop with AMD A6-3420M APU and Radeon 7670m GPU
	 * https://bugzilla.kernel.org/show_bug.cgi?id=51381
	 */
	{ PCI_VENDOR_ID_ATI, 0x6840, 0x1043, 0x2122, RADEON_PX_QUIRK_DISABLE_PX },
	/* Asus K53TK laptop with AMD A6-3420M APU and Radeon 7670m GPU
	 * https://bugs.freedesktop.org/show_bug.cgi?id=101491
	 */
	{ PCI_VENDOR_ID_ATI, 0x6741, 0x1043, 0x2122, RADEON_PX_QUIRK_DISABLE_PX },
	/* Asus K73TK laptop with AMD A6-3420M APU and Radeon 7670m GPU
	 * https://bugzilla.kernel.org/show_bug.cgi?id=51381#c52
	 */
	{ PCI_VENDOR_ID_ATI, 0x6840, 0x1043, 0x2123, RADEON_PX_QUIRK_DISABLE_PX },
	{ 0, 0, 0, 0, 0 },
};

bool radeon_is_px(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;

	if (rdev->flags & RADEON_IS_PX)
		return true;
	return false;
}

static void radeon_device_handle_px_quirks(struct radeon_device *rdev)
{
	struct radeon_px_quirk *p = radeon_px_quirk_list;

	/* Apply PX quirks */
	while (p && p->chip_device != 0) {
		if (rdev->pdev->vendor == p->chip_vendor &&
		    rdev->pdev->device == p->chip_device &&
		    rdev->pdev->subsystem_vendor == p->subsys_vendor &&
		    rdev->pdev->subsystem_device == p->subsys_device) {
			rdev->px_quirk_flags = p->px_quirk_flags;
			break;
		}
		++p;
	}

	if (rdev->px_quirk_flags & RADEON_PX_QUIRK_DISABLE_PX)
		rdev->flags &= ~RADEON_IS_PX;

	/* disable PX is the system doesn't support dGPU power control or hybrid gfx */
	if (!radeon_is_atpx_hybrid() &&
	    !radeon_has_atpx_dgpu_power_cntl())
		rdev->flags &= ~RADEON_IS_PX;
}

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

void radeon_pci_config_reset(struct radeon_device *rdev)
{
	pci_write_config_dword(rdev->pdev, 0x7c, RADEON_ASIC_RESET_DATA);
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
 * GPU doorbell aperture helpers function.
 */
/**
 * radeon_doorbell_init - Init doorbell driver information.
 *
 * @rdev: radeon_device pointer
 *
 * Init doorbell driver information (CIK)
 * Returns 0 on success, error on failure.
 */
static int radeon_doorbell_init(struct radeon_device *rdev)
{
	/* doorbell bar mapping */
	rdev->doorbell.base = pci_resource_start(rdev->pdev, 2);
	rdev->doorbell.size = pci_resource_len(rdev->pdev, 2);

	rdev->doorbell.num_doorbells = min_t(u32, rdev->doorbell.size / sizeof(u32), RADEON_MAX_DOORBELLS);
	if (rdev->doorbell.num_doorbells == 0)
		return -EINVAL;

	rdev->doorbell.ptr = ioremap(rdev->doorbell.base, rdev->doorbell.num_doorbells * sizeof(u32));
	if (rdev->doorbell.ptr == NULL) {
		return -ENOMEM;
	}
	DRM_INFO("doorbell mmio base: 0x%08X\n", (uint32_t)rdev->doorbell.base);
	DRM_INFO("doorbell mmio size: %u\n", (unsigned)rdev->doorbell.size);

	memset(&rdev->doorbell.used, 0, sizeof(rdev->doorbell.used));

	return 0;
}

/**
 * radeon_doorbell_fini - Tear down doorbell driver information.
 *
 * @rdev: radeon_device pointer
 *
 * Tear down doorbell driver information (CIK)
 */
static void radeon_doorbell_fini(struct radeon_device *rdev)
{
	iounmap(rdev->doorbell.ptr);
	rdev->doorbell.ptr = NULL;
}

/**
 * radeon_doorbell_get - Allocate a doorbell entry
 *
 * @rdev: radeon_device pointer
 * @doorbell: doorbell index
 *
 * Allocate a doorbell for use by the driver (all asics).
 * Returns 0 on success or -EINVAL on failure.
 */
int radeon_doorbell_get(struct radeon_device *rdev, u32 *doorbell)
{
	unsigned long offset = find_first_zero_bit(rdev->doorbell.used, rdev->doorbell.num_doorbells);
	if (offset < rdev->doorbell.num_doorbells) {
		__set_bit(offset, rdev->doorbell.used);
		*doorbell = offset;
		return 0;
	} else {
		return -EINVAL;
	}
}

/**
 * radeon_doorbell_free - Free a doorbell entry
 *
 * @rdev: radeon_device pointer
 * @doorbell: doorbell index
 *
 * Free a doorbell allocated for use by the driver (all asics)
 */
void radeon_doorbell_free(struct radeon_device *rdev, u32 doorbell)
{
	if (doorbell < rdev->doorbell.num_doorbells)
		__clear_bit(doorbell, rdev->doorbell.used);
}

/*
 * radeon_wb_*()
 * Writeback is the method by which the GPU updates special pages
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
		if (!radeon_bo_reserve(rdev->wb.wb_obj, false)) {
			radeon_bo_kunmap(rdev->wb.wb_obj);
			radeon_bo_unpin(rdev->wb.wb_obj);
			radeon_bo_unreserve(rdev->wb.wb_obj);
		}
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
				     RADEON_GEM_DOMAIN_GTT, 0, NULL, NULL,
				     &rdev->wb.wb_obj);
		if (r) {
			dev_warn(rdev->dev, "(%d) create WB bo failed\n", r);
			return r;
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
 * Note 1: We don't explicitly enforce VRAM start to be aligned on VRAM size,
 * this shouldn't be a problem as we are using the PCI aperture as a reference.
 * Otherwise this would be needed for rv280, all r3xx, and all r4xx, but
 * not IGP.
 *
 * Note 2: we use mc_vram_size as on some board we need to program the mc to
 * cover the whole aperture even if VRAM size is inferior to aperture size
 * Novell bug 204882 + along with lots of ubuntu ones
 *
 * Note 3: when limiting vram it's safe to overwritte real_vram_size because
 * we are not in case where real_vram_size is inferior to mc_vram_size (ie
 * note afected by bogus hw of Novell bug 204882 + along with lots of ubuntu
 * ones)
 *
 * Note 4: IGP TOM addr should be the same as the aperture addr, we don't
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

/*
 * radeon_device_is_virtual - check if we are running is a virtual environment
 *
 * Check if the asic has been passed through to a VM (all asics).
 * Used at driver startup.
 * Returns true if virtual or false if not.
 */
bool radeon_device_is_virtual(void)
{
#ifdef CONFIG_X86
	return boot_cpu_has(X86_FEATURE_HYPERVISOR);
#else
	return false;
#endif
}

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

	/* for pass through, always force asic_init for CI */
	if (rdev->family >= CHIP_BONAIRE &&
	    radeon_device_is_virtual())
		return false;

	/* required for EFI mode on macbook2,1 which uses an r5xx asic */
	if (efi_enabled(EFI_BOOT) &&
	    (rdev->pdev->subsystem_vendor == PCI_VENDOR_ID_APPLE) &&
	    (rdev->family < CHIP_R600))
		return false;

	if (ASIC_IS_NODCE(rdev))
		goto check_memsize;

	/* first check CRTCs */
	if (ASIC_IS_DCE4(rdev)) {
		reg = RREG32(EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC0_REGISTER_OFFSET) |
			RREG32(EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC1_REGISTER_OFFSET);
			if (rdev->num_crtc >= 4) {
				reg |= RREG32(EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC2_REGISTER_OFFSET) |
					RREG32(EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC3_REGISTER_OFFSET);
			}
			if (rdev->num_crtc >= 6) {
				reg |= RREG32(EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC4_REGISTER_OFFSET) |
					RREG32(EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC5_REGISTER_OFFSET);
			}
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

check_memsize:
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
	rdev->dummy_page.addr = dma_map_page(&rdev->pdev->dev, rdev->dummy_page.page,
					0, PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(&rdev->pdev->dev, rdev->dummy_page.addr)) {
		dev_err(&rdev->pdev->dev, "Failed to DMA MAP the dummy page\n");
		__free_page(rdev->dummy_page.page);
		rdev->dummy_page.page = NULL;
		return -ENOMEM;
	}
	rdev->dummy_page.entry = radeon_gart_get_page_entry(rdev->dummy_page.addr,
							    RADEON_GART_PAGE_DUMMY);
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
	dma_unmap_page(&rdev->pdev->dev, rdev->dummy_page.addr, PAGE_SIZE,
		       DMA_BIDIRECTIONAL);
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
	mutex_init(&rdev->mode_info.atom_context->scratch_mutex);
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
 * @pdev: PCI device
 * @state: enable/disable vga decode
 *
 * Enable/disable vga decode (all asics).
 * Returns VGA resource flags.
 */
static unsigned int radeon_vga_set_decode(struct pci_dev *pdev, bool state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct radeon_device *rdev = dev->dev_private;
	radeon_vga_set_state(rdev, state);
	if (state)
		return VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
		       VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
	else
		return VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
}

/**
 * radeon_gart_size_auto - Determine a sensible default GART size
 *                         according to ASIC family.
 *
 * @family: ASIC family name
 */
static int radeon_gart_size_auto(enum radeon_family family)
{
	/* default to a larger gart size on newer asics */
	if (family >= CHIP_TAHITI)
		return 2048;
	else if (family >= CHIP_RV770)
		return 1024;
	else
		return 512;
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
	if (radeon_vram_limit != 0 && !is_power_of_2(radeon_vram_limit)) {
		dev_warn(rdev->dev, "vram limit (%d) must be a power of 2\n",
				radeon_vram_limit);
		radeon_vram_limit = 0;
	}

	if (radeon_gart_size == -1) {
		radeon_gart_size = radeon_gart_size_auto(rdev->family);
	}
	/* gtt size must be power of two and greater or equal to 32M */
	if (radeon_gart_size < 32) {
		dev_warn(rdev->dev, "gart size (%d) too small\n",
				radeon_gart_size);
		radeon_gart_size = radeon_gart_size_auto(rdev->family);
	} else if (!is_power_of_2(radeon_gart_size)) {
		dev_warn(rdev->dev, "gart size (%d) must be a power of 2\n",
				radeon_gart_size);
		radeon_gart_size = radeon_gart_size_auto(rdev->family);
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

	if (!is_power_of_2(radeon_vm_size)) {
		dev_warn(rdev->dev, "VM size (%d) must be a power of 2\n",
			 radeon_vm_size);
		radeon_vm_size = 4;
	}

	if (radeon_vm_size < 1) {
		dev_warn(rdev->dev, "VM size (%d) too small, min is 1GB\n",
			 radeon_vm_size);
		radeon_vm_size = 4;
	}

	/*
	 * Max GPUVM size for Cayman, SI and CI are 40 bits.
	 */
	if (radeon_vm_size > 1024) {
		dev_warn(rdev->dev, "VM size (%d) too large, max is 1TB\n",
			 radeon_vm_size);
		radeon_vm_size = 4;
	}

	/* defines number of bits in page table versus page directory,
	 * a page is 4KB so we have 12 bits offset, minimum 9 bits in the
	 * page table and the remaining bits are in the page directory */
	if (radeon_vm_block_size == -1) {

		/* Total bits covered by PD + PTs */
		unsigned bits = ilog2(radeon_vm_size) + 18;

		/* Make sure the PD is 4K in size up to 8GB address space.
		   Above that split equal between PD and PTs */
		if (radeon_vm_size <= 8)
			radeon_vm_block_size = bits - 9;
		else
			radeon_vm_block_size = (bits + 3) / 2;

	} else if (radeon_vm_block_size < 9) {
		dev_warn(rdev->dev, "VM page table size (%d) too small\n",
			 radeon_vm_block_size);
		radeon_vm_block_size = 9;
	}

	if (radeon_vm_block_size > 24 ||
	    (radeon_vm_size * 1024) < (1ull << radeon_vm_block_size)) {
		dev_warn(rdev->dev, "VM page table size (%d) too large\n",
			 radeon_vm_block_size);
		radeon_vm_block_size = 9;
	}
}

/**
 * radeon_switcheroo_set_state - set switcheroo state
 *
 * @pdev: pci dev pointer
 * @state: vga_switcheroo state
 *
 * Callback for the switcheroo driver.  Suspends or resumes the
 * the asics before or after it is powered up using ACPI methods.
 */
static void radeon_switcheroo_set_state(struct pci_dev *pdev, enum vga_switcheroo_state state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	if (radeon_is_px(dev) && state == VGA_SWITCHEROO_OFF)
		return;

	if (state == VGA_SWITCHEROO_ON) {
		pr_info("radeon: switched on\n");
		/* don't suspend or resume card normally */
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;

		radeon_resume_kms(dev, true, true);

		dev->switch_power_state = DRM_SWITCH_POWER_ON;
		drm_kms_helper_poll_enable(dev);
	} else {
		pr_info("radeon: switched off\n");
		drm_kms_helper_poll_disable(dev);
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		radeon_suspend_kms(dev, true, true, false);
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

	/*
	 * FIXME: open_count is protected by drm_global_mutex but that would lead to
	 * locking inversion with the driver load path. And the access here is
	 * completely racy anyway. So don't bother with locking for now.
	 */
	return atomic_read(&dev->open_count) == 0;
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
 * @ddev: drm dev pointer
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
	bool runtime = false;

	rdev->shutdown = false;
	rdev->dev = &pdev->dev;
	rdev->ddev = ddev;
	rdev->pdev = pdev;
	rdev->flags = flags;
	rdev->family = flags & RADEON_FAMILY_MASK;
	rdev->is_atom_bios = false;
	rdev->usec_timeout = RADEON_MAX_USEC_TIMEOUT;
	rdev->mc.gtt_size = 512 * 1024 * 1024;
	rdev->accel_working = false;
	/* set up ring ids */
	for (i = 0; i < RADEON_NUM_RINGS; i++) {
		rdev->ring[i].idx = i;
	}
	rdev->fence_context = dma_fence_context_alloc(RADEON_NUM_RINGS);

	DRM_INFO("initializing kernel modesetting (%s 0x%04X:0x%04X 0x%04X:0x%04X 0x%02X).\n",
		 radeon_family_name[rdev->family], pdev->vendor, pdev->device,
		 pdev->subsystem_vendor, pdev->subsystem_device, pdev->revision);

	/* mutex initialization are all done here so we
	 * can recall function without having locking issues */
	mutex_init(&rdev->ring_lock);
	mutex_init(&rdev->dc_hw_i2c_mutex);
	atomic_set(&rdev->ih.lock, 0);
	mutex_init(&rdev->gem.mutex);
	mutex_init(&rdev->pm.mutex);
	mutex_init(&rdev->gpu_clock_mutex);
	mutex_init(&rdev->srbm_mutex);
	init_rwsem(&rdev->pm.mclk_lock);
	init_rwsem(&rdev->exclusive_lock);
	init_waitqueue_head(&rdev->irq.vblank_queue);
	r = radeon_gem_init(rdev);
	if (r)
		return r;

	radeon_check_arguments(rdev);
	/* Adjust VM size here.
	 * Max GPUVM size for cayman+ is 40 bits.
	 */
	rdev->vm_manager.max_pfn = radeon_vm_size << 18;

	/* Set asic functions */
	r = radeon_asic_init(rdev);
	if (r)
		return r;

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

	/* set DMA mask.
	 * PCIE - can handle 40-bits.
	 * IGP - can handle 40-bits
	 * AGP - generally dma32 is safest
	 * PCI - dma32 for legacy pci gart, 40 bits on newer asics
	 */
	dma_bits = 40;
	if (rdev->flags & RADEON_IS_AGP)
		dma_bits = 32;
	if ((rdev->flags & RADEON_IS_PCI) &&
	    (rdev->family <= CHIP_RS740))
		dma_bits = 32;
#ifdef CONFIG_PPC64
	if (rdev->family == CHIP_CEDAR)
		dma_bits = 32;
#endif

	r = dma_set_mask_and_coherent(&rdev->pdev->dev, DMA_BIT_MASK(dma_bits));
	if (r) {
		pr_warn("radeon: No suitable DMA available\n");
		return r;
	}
	rdev->need_swiotlb = drm_need_swiotlb(dma_bits);

	/* Registers mapping */
	/* TODO: block userspace mapping of io register */
	spin_lock_init(&rdev->mmio_idx_lock);
	spin_lock_init(&rdev->smc_idx_lock);
	spin_lock_init(&rdev->pll_idx_lock);
	spin_lock_init(&rdev->mc_idx_lock);
	spin_lock_init(&rdev->pcie_idx_lock);
	spin_lock_init(&rdev->pciep_idx_lock);
	spin_lock_init(&rdev->pif_idx_lock);
	spin_lock_init(&rdev->cg_idx_lock);
	spin_lock_init(&rdev->uvd_idx_lock);
	spin_lock_init(&rdev->rcu_idx_lock);
	spin_lock_init(&rdev->didt_idx_lock);
	spin_lock_init(&rdev->end_idx_lock);
	if (rdev->family >= CHIP_BONAIRE) {
		rdev->rmmio_base = pci_resource_start(rdev->pdev, 5);
		rdev->rmmio_size = pci_resource_len(rdev->pdev, 5);
	} else {
		rdev->rmmio_base = pci_resource_start(rdev->pdev, 2);
		rdev->rmmio_size = pci_resource_len(rdev->pdev, 2);
	}
	rdev->rmmio = ioremap(rdev->rmmio_base, rdev->rmmio_size);
	if (rdev->rmmio == NULL)
		return -ENOMEM;

	/* doorbell bar mapping */
	if (rdev->family >= CHIP_BONAIRE)
		radeon_doorbell_init(rdev);

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

	if (rdev->flags & RADEON_IS_PX)
		radeon_device_handle_px_quirks(rdev);

	/* if we have > 1 VGA cards, then disable the radeon VGA resources */
	/* this will fail for cards that aren't VGA class devices, just
	 * ignore it */
	vga_client_register(rdev->pdev, radeon_vga_set_decode);

	if (rdev->flags & RADEON_IS_PX)
		runtime = true;
	if (!pci_is_thunderbolt_attached(rdev->pdev))
		vga_switcheroo_register_client(rdev->pdev,
					       &radeon_switcheroo_ops, runtime);
	if (runtime)
		vga_switcheroo_init_domain_pm_ops(rdev->dev, &rdev->vga_pm_domain);

	r = radeon_init(rdev);
	if (r)
		goto failed;

	radeon_gem_debugfs_init(rdev);

	if (rdev->flags & RADEON_IS_AGP && !rdev->accel_working) {
		/* Acceleration not working on AGP card try again
		 * with fallback to PCI or PCIE GART
		 */
		radeon_asic_reset(rdev);
		radeon_fini(rdev);
		radeon_agp_disable(rdev);
		r = radeon_init(rdev);
		if (r)
			goto failed;
	}

	r = radeon_ib_ring_tests(rdev);
	if (r)
		DRM_ERROR("ib ring test failed (%d).\n", r);

	/*
	 * Turks/Thames GPU will freeze whole laptop if DPM is not restarted
	 * after the CP ring have chew one packet at least. Hence here we stop
	 * and restart DPM after the radeon_ib_ring_tests().
	 */
	if (rdev->pm.dpm_enabled &&
	    (rdev->pm.pm_method == PM_METHOD_DPM) &&
	    (rdev->family == CHIP_TURKS) &&
	    (rdev->flags & RADEON_IS_MOBILITY)) {
		mutex_lock(&rdev->pm.mutex);
		radeon_dpm_disable(rdev);
		radeon_dpm_enable(rdev);
		mutex_unlock(&rdev->pm.mutex);
	}

	if ((radeon_testing & 1)) {
		if (rdev->accel_working)
			radeon_test_moves(rdev);
		else
			DRM_INFO("radeon: acceleration disabled, skipping move tests\n");
	}
	if ((radeon_testing & 2)) {
		if (rdev->accel_working)
			radeon_test_syncing(rdev);
		else
			DRM_INFO("radeon: acceleration disabled, skipping sync tests\n");
	}
	if (radeon_benchmarking) {
		if (rdev->accel_working)
			radeon_benchmark(rdev, radeon_benchmarking);
		else
			DRM_INFO("radeon: acceleration disabled, skipping benchmarks\n");
	}
	return 0;

failed:
	/* balance pm_runtime_get_sync() in radeon_driver_unload_kms() */
	if (radeon_is_px(ddev))
		pm_runtime_put_noidle(ddev->dev);
	if (runtime)
		vga_switcheroo_fini_domain_pm_ops(rdev->dev);
	return r;
}

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
	if (!pci_is_thunderbolt_attached(rdev->pdev))
		vga_switcheroo_unregister_client(rdev->pdev);
	if (rdev->flags & RADEON_IS_PX)
		vga_switcheroo_fini_domain_pm_ops(rdev->dev);
	vga_client_unregister(rdev->pdev);
	if (rdev->rio_mem)
		pci_iounmap(rdev->pdev, rdev->rio_mem);
	rdev->rio_mem = NULL;
	iounmap(rdev->rmmio);
	rdev->rmmio = NULL;
	if (rdev->family >= CHIP_BONAIRE)
		radeon_doorbell_fini(rdev);
}


/*
 * Suspend & resume.
 */
/*
 * radeon_suspend_kms - initiate device suspend
 *
 * Puts the hw in the suspend state (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver suspend.
 */
int radeon_suspend_kms(struct drm_device *dev, bool suspend,
		       bool fbcon, bool freeze)
{
	struct radeon_device *rdev;
	struct pci_dev *pdev;
	struct drm_crtc *crtc;
	struct drm_connector *connector;
	int i, r;

	if (dev == NULL || dev->dev_private == NULL) {
		return -ENODEV;
	}

	rdev = dev->dev_private;
	pdev = to_pci_dev(dev->dev);

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	drm_kms_helper_poll_disable(dev);

	drm_modeset_lock_all(dev);
	/* turn off display hw */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		drm_helper_connector_dpms(connector, DRM_MODE_DPMS_OFF);
	}
	drm_modeset_unlock_all(dev);

	/* unpin the front buffers and cursors */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
		struct drm_framebuffer *fb = crtc->primary->fb;
		struct radeon_bo *robj;

		if (radeon_crtc->cursor_bo) {
			struct radeon_bo *robj = gem_to_radeon_bo(radeon_crtc->cursor_bo);
			r = radeon_bo_reserve(robj, false);
			if (r == 0) {
				radeon_bo_unpin(robj);
				radeon_bo_unreserve(robj);
			}
		}

		if (fb == NULL || fb->obj[0] == NULL) {
			continue;
		}
		robj = gem_to_radeon_bo(fb->obj[0]);
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

	/* wait for gpu to finish processing current batch */
	for (i = 0; i < RADEON_NUM_RINGS; i++) {
		r = radeon_fence_wait_empty(rdev, i);
		if (r) {
			/* delay GPU reset to resume */
			radeon_fence_driver_force_completion(rdev, i);
		} else {
			/* finish executing delayed work */
			flush_delayed_work(&rdev->fence_drv[i].lockup_work);
		}
	}

	radeon_save_bios_scratch_regs(rdev);

	radeon_suspend(rdev);
	radeon_hpd_fini(rdev);
	/* evict remaining vram memory
	 * This second call to evict vram is to evict the gart page table
	 * using the CPU.
	 */
	radeon_bo_evict_vram(rdev);

	radeon_agp_suspend(rdev);

	pci_save_state(pdev);
	if (freeze && rdev->family >= CHIP_CEDAR && !(rdev->flags & RADEON_IS_IGP)) {
		rdev->asic->asic_reset(rdev, true);
		pci_restore_state(pdev);
	} else if (suspend) {
		/* Shut down the device */
		pci_disable_device(pdev);
		pci_set_power_state(pdev, PCI_D3hot);
	}

	if (fbcon) {
		console_lock();
		radeon_fbdev_set_suspend(rdev, 1);
		console_unlock();
	}
	return 0;
}

/*
 * radeon_resume_kms - initiate device resume
 *
 * Bring the hw back to operating state (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver resume.
 */
int radeon_resume_kms(struct drm_device *dev, bool resume, bool fbcon)
{
	struct drm_connector *connector;
	struct radeon_device *rdev = dev->dev_private;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct drm_crtc *crtc;
	int r;

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	if (fbcon) {
		console_lock();
	}
	if (resume) {
		pci_set_power_state(pdev, PCI_D0);
		pci_restore_state(pdev);
		if (pci_enable_device(pdev)) {
			if (fbcon)
				console_unlock();
			return -1;
		}
	}
	/* resume AGP if in use */
	radeon_agp_resume(rdev);
	radeon_resume(rdev);

	r = radeon_ib_ring_tests(rdev);
	if (r)
		DRM_ERROR("ib ring test failed (%d).\n", r);

	if ((rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled) {
		/* do dpm late init */
		r = radeon_pm_late_init(rdev);
		if (r) {
			rdev->pm.dpm_enabled = false;
			DRM_ERROR("radeon_pm_late_init failed, disabling dpm\n");
		}
	} else {
		/* resume old pm late */
		radeon_pm_resume(rdev);
	}

	radeon_restore_bios_scratch_regs(rdev);

	/* pin cursors */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);

		if (radeon_crtc->cursor_bo) {
			struct radeon_bo *robj = gem_to_radeon_bo(radeon_crtc->cursor_bo);
			r = radeon_bo_reserve(robj, false);
			if (r == 0) {
				/* Only 27 bit offset for legacy cursor */
				r = radeon_bo_pin_restricted(robj,
							     RADEON_GEM_DOMAIN_VRAM,
							     ASIC_IS_AVIVO(rdev) ?
							     0 : 1 << 27,
							     &radeon_crtc->cursor_addr);
				if (r != 0)
					DRM_ERROR("Failed to pin cursor BO (%d)\n", r);
				radeon_bo_unreserve(robj);
			}
		}
	}

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
	if (fbcon) {
		drm_helper_resume_force_mode(dev);
		/* turn on display hw */
		drm_modeset_lock_all(dev);
		list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
			drm_helper_connector_dpms(connector, DRM_MODE_DPMS_ON);
		}
		drm_modeset_unlock_all(dev);
	}

	drm_kms_helper_poll_enable(dev);

	/* set the power state here in case we are a PX system or headless */
	if ((rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled)
		radeon_pm_compute_clocks(rdev);

	if (fbcon) {
		radeon_fbdev_set_suspend(rdev, 0);
		console_unlock();
	}

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

	if (!rdev->needs_reset) {
		up_write(&rdev->exclusive_lock);
		return 0;
	}

	atomic_inc(&rdev->gpu_reset_counter);

	radeon_save_bios_scratch_regs(rdev);
	/* block TTM */
	resched = ttm_bo_lock_delayed_workqueue(&rdev->mman.bdev);
	radeon_suspend(rdev);
	radeon_hpd_fini(rdev);

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		ring_sizes[i] = radeon_ring_backup(rdev, &rdev->ring[i],
						   &ring_data[i]);
		if (ring_sizes[i]) {
			saved = true;
			dev_info(rdev->dev, "Saved %d dwords of commands "
				 "on ring %d.\n", ring_sizes[i], i);
		}
	}

	r = radeon_asic_reset(rdev);
	if (!r) {
		dev_info(rdev->dev, "GPU reset succeeded, trying to resume\n");
		radeon_resume(rdev);
	}

	radeon_restore_bios_scratch_regs(rdev);

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		if (!r && ring_data[i]) {
			radeon_ring_restore(rdev, &rdev->ring[i],
					    ring_sizes[i], ring_data[i]);
		} else {
			radeon_fence_driver_force_completion(rdev, i);
			kfree(ring_data[i]);
		}
	}

	if ((rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled) {
		/* do dpm late init */
		r = radeon_pm_late_init(rdev);
		if (r) {
			rdev->pm.dpm_enabled = false;
			DRM_ERROR("radeon_pm_late_init failed, disabling dpm\n");
		}
	} else {
		/* resume old pm late */
		radeon_pm_resume(rdev);
	}

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

	ttm_bo_unlock_delayed_workqueue(&rdev->mman.bdev, resched);

	rdev->in_reset = true;
	rdev->needs_reset = false;

	downgrade_write(&rdev->exclusive_lock);

	drm_helper_resume_force_mode(rdev->ddev);

	/* set the power state here in case we are a PX system or headless */
	if ((rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled)
		radeon_pm_compute_clocks(rdev);

	if (!r) {
		r = radeon_ib_ring_tests(rdev);
		if (r && saved)
			r = -EAGAIN;
	} else {
		/* bad news, how to tell it to userspace ? */
		dev_info(rdev->dev, "GPU reset failed\n");
	}

	rdev->needs_reset = r == -EAGAIN;
	rdev->in_reset = false;

	up_read(&rdev->exclusive_lock);
	return r;
}
