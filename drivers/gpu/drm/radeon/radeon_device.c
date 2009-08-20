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
#include "radeon_reg.h"
#include "radeon.h"
#include "radeon_asic.h"
#include "atom.h"

/*
 * Clear GPU surface registers.
 */
static void radeon_surface_init(struct radeon_device *rdev)
{
	/* FIXME: check this out */
	if (rdev->family < CHIP_R600) {
		int i;

		for (i = 0; i < 8; i++) {
			WREG32(RADEON_SURFACE0_INFO +
			       i * (RADEON_SURFACE1_INFO - RADEON_SURFACE0_INFO),
			       0);
		}
		/* enable surfaces */
		WREG32(RADEON_SURFACE_CNTL, 0);
	}
}

/*
 * GPU scratch registers helpers function.
 */
static void radeon_scratch_init(struct radeon_device *rdev)
{
	int i;

	/* FIXME: check this out */
	if (rdev->family < CHIP_R300) {
		rdev->scratch.num_reg = 5;
	} else {
		rdev->scratch.num_reg = 7;
	}
	for (i = 0; i < rdev->scratch.num_reg; i++) {
		rdev->scratch.free[i] = true;
		rdev->scratch.reg[i] = RADEON_SCRATCH_REG0 + (i * 4);
	}
}

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
 * MC common functions
 */
int radeon_mc_setup(struct radeon_device *rdev)
{
	uint32_t tmp;

	/* Some chips have an "issue" with the memory controller, the
	 * location must be aligned to the size. We just align it down,
	 * too bad if we walk over the top of system memory, we don't
	 * use DMA without a remapped anyway.
	 * Affected chips are rv280, all r3xx, and all r4xx, but not IGP
	 */
	/* FGLRX seems to setup like this, VRAM a 0, then GART.
	 */
	/*
	 * Note: from R6xx the address space is 40bits but here we only
	 * use 32bits (still have to see a card which would exhaust 4G
	 * address space).
	 */
	if (rdev->mc.vram_location != 0xFFFFFFFFUL) {
		/* vram location was already setup try to put gtt after
		 * if it fits */
		tmp = rdev->mc.vram_location + rdev->mc.mc_vram_size;
		tmp = (tmp + rdev->mc.gtt_size - 1) & ~(rdev->mc.gtt_size - 1);
		if ((0xFFFFFFFFUL - tmp) >= rdev->mc.gtt_size) {
			rdev->mc.gtt_location = tmp;
		} else {
			if (rdev->mc.gtt_size >= rdev->mc.vram_location) {
				printk(KERN_ERR "[drm] GTT too big to fit "
				       "before or after vram location.\n");
				return -EINVAL;
			}
			rdev->mc.gtt_location = 0;
		}
	} else if (rdev->mc.gtt_location != 0xFFFFFFFFUL) {
		/* gtt location was already setup try to put vram before
		 * if it fits */
		if (rdev->mc.mc_vram_size < rdev->mc.gtt_location) {
			rdev->mc.vram_location = 0;
		} else {
			tmp = rdev->mc.gtt_location + rdev->mc.gtt_size;
			tmp += (rdev->mc.mc_vram_size - 1);
			tmp &= ~(rdev->mc.mc_vram_size - 1);
			if ((0xFFFFFFFFUL - tmp) >= rdev->mc.mc_vram_size) {
				rdev->mc.vram_location = tmp;
			} else {
				printk(KERN_ERR "[drm] vram too big to fit "
				       "before or after GTT location.\n");
				return -EINVAL;
			}
		}
	} else {
		rdev->mc.vram_location = 0;
		tmp = rdev->mc.mc_vram_size;
		tmp = (tmp + rdev->mc.gtt_size - 1) & ~(rdev->mc.gtt_size - 1);
		rdev->mc.gtt_location = tmp;
	}
	DRM_INFO("radeon: VRAM %uM\n", rdev->mc.real_vram_size >> 20);
	DRM_INFO("radeon: VRAM from 0x%08X to 0x%08X\n",
		 rdev->mc.vram_location,
		 rdev->mc.vram_location + rdev->mc.mc_vram_size - 1);
	if (rdev->mc.real_vram_size != rdev->mc.mc_vram_size)
		DRM_INFO("radeon: VRAM less than aperture workaround enabled\n");
	DRM_INFO("radeon: GTT %uM\n", rdev->mc.gtt_size >> 20);
	DRM_INFO("radeon: GTT from 0x%08X to 0x%08X\n",
		 rdev->mc.gtt_location,
		 rdev->mc.gtt_location + rdev->mc.gtt_size - 1);
	return 0;
}


/*
 * GPU helpers function.
 */
static bool radeon_card_posted(struct radeon_device *rdev)
{
	uint32_t reg;

	/* first check CRTCs */
	if (ASIC_IS_AVIVO(rdev)) {
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


/*
 * Registers accessors functions.
 */
uint32_t radeon_invalid_rreg(struct radeon_device *rdev, uint32_t reg)
{
	DRM_ERROR("Invalid callback to read register 0x%04X\n", reg);
	BUG_ON(1);
	return 0;
}

void radeon_invalid_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	DRM_ERROR("Invalid callback to write register 0x%04X with 0x%08X\n",
		  reg, v);
	BUG_ON(1);
}

void radeon_register_accessor_init(struct radeon_device *rdev)
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
	if (rdev->family >= CHIP_R600) {
		rdev->pciep_rreg = &r600_pciep_rreg;
		rdev->pciep_wreg = &r600_pciep_wreg;
	}
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
	case CHIP_R200:
	case CHIP_RV250:
	case CHIP_RS300:
	case CHIP_RV280:
		rdev->asic = &r100_asic;
		break;
	case CHIP_R300:
	case CHIP_R350:
	case CHIP_RV350:
	case CHIP_RV380:
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
	case CHIP_RV770:
	case CHIP_RV730:
	case CHIP_RV710:
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}
	return 0;
}


/*
 * Wrapper around modesetting bits.
 */
int radeon_clocks_init(struct radeon_device *rdev)
{
	int r;

	radeon_get_clock_info(rdev->ddev);
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

/* ATOM accessor methods */
static uint32_t cail_pll_read(struct card_info *info, uint32_t reg)
{
	struct radeon_device *rdev = info->dev->dev_private;
	uint32_t r;

	r = rdev->pll_rreg(rdev, reg);
	return r;
}

static void cail_pll_write(struct card_info *info, uint32_t reg, uint32_t val)
{
	struct radeon_device *rdev = info->dev->dev_private;

	rdev->pll_wreg(rdev, reg, val);
}

static uint32_t cail_mc_read(struct card_info *info, uint32_t reg)
{
	struct radeon_device *rdev = info->dev->dev_private;
	uint32_t r;

	r = rdev->mc_rreg(rdev, reg);
	return r;
}

static void cail_mc_write(struct card_info *info, uint32_t reg, uint32_t val)
{
	struct radeon_device *rdev = info->dev->dev_private;

	rdev->mc_wreg(rdev, reg, val);
}

static void cail_reg_write(struct card_info *info, uint32_t reg, uint32_t val)
{
	struct radeon_device *rdev = info->dev->dev_private;

	WREG32(reg*4, val);
}

static uint32_t cail_reg_read(struct card_info *info, uint32_t reg)
{
	struct radeon_device *rdev = info->dev->dev_private;
	uint32_t r;

	r = RREG32(reg*4);
	return r;
}

static struct card_info atom_card_info = {
	.dev = NULL,
	.reg_read = cail_reg_read,
	.reg_write = cail_reg_write,
	.mc_read = cail_mc_read,
	.mc_write = cail_mc_write,
	.pll_read = cail_pll_read,
	.pll_write = cail_pll_write,
};

int radeon_atombios_init(struct radeon_device *rdev)
{
	atom_card_info.dev = rdev->ddev;
	rdev->mode_info.atom_context = atom_parse(&atom_card_info, rdev->bios);
	radeon_atom_initialize_bios_scratch_regs(rdev->ddev);
	return 0;
}

void radeon_atombios_fini(struct radeon_device *rdev)
{
	kfree(rdev->mode_info.atom_context);
}

int radeon_combios_init(struct radeon_device *rdev)
{
	radeon_combios_initialize_bios_scratch_regs(rdev->ddev);
	return 0;
}

void radeon_combios_fini(struct radeon_device *rdev)
{
}

int radeon_modeset_init(struct radeon_device *rdev);
void radeon_modeset_fini(struct radeon_device *rdev);


/*
 * Radeon device.
 */
int radeon_device_init(struct radeon_device *rdev,
		       struct drm_device *ddev,
		       struct pci_dev *pdev,
		       uint32_t flags)
{
	int r, ret;
	int dma_bits;

	DRM_INFO("radeon: Initializing kernel modesetting.\n");
	rdev->shutdown = false;
	rdev->ddev = ddev;
	rdev->pdev = pdev;
	rdev->flags = flags;
	rdev->family = flags & RADEON_FAMILY_MASK;
	rdev->is_atom_bios = false;
	rdev->usec_timeout = RADEON_MAX_USEC_TIMEOUT;
	rdev->mc.gtt_size = radeon_gart_size * 1024 * 1024;
	rdev->gpu_lockup = false;
	/* mutex initialization are all done here so we
	 * can recall function without having locking issues */
	mutex_init(&rdev->cs_mutex);
	mutex_init(&rdev->ib_pool.mutex);
	mutex_init(&rdev->cp.mutex);
	rwlock_init(&rdev->fence_drv.lock);

	if (radeon_agpmode == -1) {
		rdev->flags &= ~RADEON_IS_AGP;
		if (rdev->family > CHIP_RV515 ||
		    rdev->family == CHIP_RV380 ||
		    rdev->family == CHIP_RV410 ||
		    rdev->family == CHIP_R423) {
			DRM_INFO("Forcing AGP to PCIE mode\n");
			rdev->flags |= RADEON_IS_PCIE;
		} else {
			DRM_INFO("Forcing AGP to PCI mode\n");
			rdev->flags |= RADEON_IS_PCI;
		}
	}

	/* Set asic functions */
	r = radeon_asic_init(rdev);
	if (r) {
		return r;
	}
	r = radeon_init(rdev);
	if (r) {
		return r;
	}

	/* set DMA mask + need_dma32 flags.
	 * PCIE - can handle 40-bits.
	 * IGP - can handle 40-bits (in theory)
	 * AGP - generally dma32 is safest
	 * PCI - only dma32
	 */
	rdev->need_dma32 = false;
	if (rdev->flags & RADEON_IS_AGP)
		rdev->need_dma32 = true;
	if (rdev->flags & RADEON_IS_PCI)
		rdev->need_dma32 = true;

	dma_bits = rdev->need_dma32 ? 32 : 40;
	r = pci_set_dma_mask(rdev->pdev, DMA_BIT_MASK(dma_bits));
	if (r) {
		printk(KERN_WARNING "radeon: No suitable DMA available.\n");
	}

	/* Registers mapping */
	/* TODO: block userspace mapping of io register */
	rdev->rmmio_base = drm_get_resource_start(rdev->ddev, 2);
	rdev->rmmio_size = drm_get_resource_len(rdev->ddev, 2);
	rdev->rmmio = ioremap(rdev->rmmio_base, rdev->rmmio_size);
	if (rdev->rmmio == NULL) {
		return -ENOMEM;
	}
	DRM_INFO("register mmio base: 0x%08X\n", (uint32_t)rdev->rmmio_base);
	DRM_INFO("register mmio size: %u\n", (unsigned)rdev->rmmio_size);

	/* Setup errata flags */
	radeon_errata(rdev);
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
	if (radeon_gpu_reset(rdev)) {
		/* FIXME: what do we want to do here ? */
	}
	/* check if cards are posted or not */
	if (!radeon_card_posted(rdev) && rdev->bios) {
		DRM_INFO("GPU not posted. posting now...\n");
		if (rdev->is_atom_bios) {
			atom_asic_init(rdev->mode_info.atom_context);
		} else {
			radeon_combios_asic_init(rdev->ddev);
		}
	}
	/* Initialize clocks */
	r = radeon_clocks_init(rdev);
	if (r) {
		return r;
	}
	/* Get vram informations */
	radeon_vram_info(rdev);

	/* Add an MTRR for the VRAM */
	rdev->mc.vram_mtrr = mtrr_add(rdev->mc.aper_base, rdev->mc.aper_size,
				      MTRR_TYPE_WRCOMB, 1);
	DRM_INFO("Detected VRAM RAM=%uM, BAR=%uM\n",
		 rdev->mc.real_vram_size >> 20,
		 (unsigned)rdev->mc.aper_size >> 20);
	DRM_INFO("RAM width %dbits %cDR\n",
		 rdev->mc.vram_width, rdev->mc.vram_is_ddr ? 'D' : 'S');
	/* Initialize memory controller (also test AGP) */
	r = radeon_mc_init(rdev);
	if (r) {
		return r;
	}
	/* Fence driver */
	r = radeon_fence_driver_init(rdev);
	if (r) {
		return r;
	}
	r = radeon_irq_kms_init(rdev);
	if (r) {
		return r;
	}
	/* Memory manager */
	r = radeon_object_init(rdev);
	if (r) {
		return r;
	}
	/* Initialize GART (initialize after TTM so we can allocate
	 * memory through TTM but finalize after TTM) */
	r = radeon_gart_enable(rdev);
	if (!r) {
		r = radeon_gem_init(rdev);
	}

	/* 1M ring buffer */
	if (!r) {
		r = radeon_cp_init(rdev, 1024 * 1024);
	}
	if (!r) {
		r = radeon_wb_init(rdev);
		if (r) {
			DRM_ERROR("radeon: failled initializing WB (%d).\n", r);
			return r;
		}
	}
	if (!r) {
		r = radeon_ib_pool_init(rdev);
		if (r) {
			DRM_ERROR("radeon: failled initializing IB pool (%d).\n", r);
			return r;
		}
	}
	if (!r) {
		r = radeon_ib_test(rdev);
		if (r) {
			DRM_ERROR("radeon: failled testing IB (%d).\n", r);
			return r;
		}
	}
	ret = r;
	r = radeon_modeset_init(rdev);
	if (r) {
		return r;
	}
	if (!ret) {
		DRM_INFO("radeon: kernel modesetting successfully initialized.\n");
	}
	if (radeon_testing) {
		radeon_test_moves(rdev);
	}
	if (radeon_benchmarking) {
		radeon_benchmark(rdev);
	}
	return ret;
}

void radeon_device_fini(struct radeon_device *rdev)
{
	if (rdev == NULL || rdev->rmmio == NULL) {
		return;
	}
	DRM_INFO("radeon: finishing device.\n");
	rdev->shutdown = true;
	/* Order matter so becarefull if you rearrange anythings */
	radeon_modeset_fini(rdev);
	radeon_ib_pool_fini(rdev);
	radeon_cp_fini(rdev);
	radeon_wb_fini(rdev);
	radeon_gem_fini(rdev);
	radeon_object_fini(rdev);
	/* mc_fini must be after object_fini */
	radeon_mc_fini(rdev);
#if __OS_HAS_AGP
	radeon_agp_fini(rdev);
#endif
	radeon_irq_kms_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_clocks_fini(rdev);
	if (rdev->is_atom_bios) {
		radeon_atombios_fini(rdev);
	} else {
		radeon_combios_fini(rdev);
	}
	kfree(rdev->bios);
	rdev->bios = NULL;
	iounmap(rdev->rmmio);
	rdev->rmmio = NULL;
}


/*
 * Suspend & resume.
 */
int radeon_suspend_kms(struct drm_device *dev, pm_message_t state)
{
	struct radeon_device *rdev = dev->dev_private;
	struct drm_crtc *crtc;

	if (dev == NULL || rdev == NULL) {
		return -ENODEV;
	}
	if (state.event == PM_EVENT_PRETHAW) {
		return 0;
	}
	/* unpin the front buffers */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct radeon_framebuffer *rfb = to_radeon_framebuffer(crtc->fb);
		struct radeon_object *robj;

		if (rfb == NULL || rfb->obj == NULL) {
			continue;
		}
		robj = rfb->obj->driver_private;
		if (robj != rdev->fbdev_robj) {
			radeon_object_unpin(robj);
		}
	}
	/* evict vram memory */
	radeon_object_evict_vram(rdev);
	/* wait for gpu to finish processing current batch */
	radeon_fence_wait_last(rdev);

	radeon_cp_disable(rdev);
	radeon_gart_disable(rdev);

	/* evict remaining vram memory */
	radeon_object_evict_vram(rdev);

	rdev->irq.sw_int = false;
	radeon_irq_set(rdev);

	pci_save_state(dev->pdev);
	if (state.event == PM_EVENT_SUSPEND) {
		/* Shut down the device */
		pci_disable_device(dev->pdev);
		pci_set_power_state(dev->pdev, PCI_D3hot);
	}
	acquire_console_sem();
	fb_set_suspend(rdev->fbdev_info, 1);
	release_console_sem();
	return 0;
}

int radeon_resume_kms(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	int r;

	acquire_console_sem();
	pci_set_power_state(dev->pdev, PCI_D0);
	pci_restore_state(dev->pdev);
	if (pci_enable_device(dev->pdev)) {
		release_console_sem();
		return -1;
	}
	pci_set_master(dev->pdev);
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_gpu_reset(rdev)) {
		/* FIXME: what do we want to do here ? */
	}
	/* post card */
	if (rdev->is_atom_bios) {
		atom_asic_init(rdev->mode_info.atom_context);
	} else {
		radeon_combios_asic_init(rdev->ddev);
	}
	/* Initialize clocks */
	r = radeon_clocks_init(rdev);
	if (r) {
		release_console_sem();
		return r;
	}
	/* Enable IRQ */
	rdev->irq.sw_int = true;
	radeon_irq_set(rdev);
	/* Initialize GPU Memory Controller */
	r = radeon_mc_init(rdev);
	if (r) {
		goto out;
	}
	r = radeon_gart_enable(rdev);
	if (r) {
		goto out;
	}
	r = radeon_cp_init(rdev, rdev->cp.ring_size);
	if (r) {
		goto out;
	}
out:
	fb_set_suspend(rdev->fbdev_info, 0);
	release_console_sem();

	/* blat the mode back in */
	drm_helper_resume_force_mode(dev);
	return 0;
}


/*
 * Debugfs
 */
struct radeon_debugfs {
	struct drm_info_list	*files;
	unsigned		num_files;
};
static struct radeon_debugfs _radeon_debugfs[RADEON_DEBUGFS_MAX_NUM_FILES];
static unsigned _radeon_debugfs_count = 0;

int radeon_debugfs_add_files(struct radeon_device *rdev,
			     struct drm_info_list *files,
			     unsigned nfiles)
{
	unsigned i;

	for (i = 0; i < _radeon_debugfs_count; i++) {
		if (_radeon_debugfs[i].files == files) {
			/* Already registered */
			return 0;
		}
	}
	if ((_radeon_debugfs_count + nfiles) > RADEON_DEBUGFS_MAX_NUM_FILES) {
		DRM_ERROR("Reached maximum number of debugfs files.\n");
		DRM_ERROR("Report so we increase RADEON_DEBUGFS_MAX_NUM_FILES.\n");
		return -EINVAL;
	}
	_radeon_debugfs[_radeon_debugfs_count].files = files;
	_radeon_debugfs[_radeon_debugfs_count].num_files = nfiles;
	_radeon_debugfs_count++;
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

#if defined(CONFIG_DEBUG_FS)
int radeon_debugfs_init(struct drm_minor *minor)
{
	return 0;
}

void radeon_debugfs_cleanup(struct drm_minor *minor)
{
	unsigned i;

	for (i = 0; i < _radeon_debugfs_count; i++) {
		drm_debugfs_remove_files(_radeon_debugfs[i].files,
					 _radeon_debugfs[i].num_files, minor);
	}
}
#endif
