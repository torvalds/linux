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

/* rs600 depends on : */
void r100_hdp_reset(struct radeon_device *rdev);
int r100_gui_wait_for_idle(struct radeon_device *rdev);
int r300_mc_wait_for_idle(struct radeon_device *rdev);
void r420_pipes_init(struct radeon_device *rdev);

/* This files gather functions specifics to :
 * rs600
 *
 * Some of these functions might be used by newer ASICs.
 */
void rs600_gpu_init(struct radeon_device *rdev);
int rs600_mc_wait_for_idle(struct radeon_device *rdev);
void rs600_disable_vga(struct radeon_device *rdev);


/*
 * GART.
 */
void rs600_gart_tlb_flush(struct radeon_device *rdev)
{
	uint32_t tmp;

	tmp = RREG32_MC(RS600_MC_PT0_CNTL);
	tmp &= ~(RS600_INVALIDATE_ALL_L1_TLBS | RS600_INVALIDATE_L2_CACHE);
	WREG32_MC(RS600_MC_PT0_CNTL, tmp);

	tmp = RREG32_MC(RS600_MC_PT0_CNTL);
	tmp |= RS600_INVALIDATE_ALL_L1_TLBS | RS600_INVALIDATE_L2_CACHE;
	WREG32_MC(RS600_MC_PT0_CNTL, tmp);

	tmp = RREG32_MC(RS600_MC_PT0_CNTL);
	tmp &= ~(RS600_INVALIDATE_ALL_L1_TLBS | RS600_INVALIDATE_L2_CACHE);
	WREG32_MC(RS600_MC_PT0_CNTL, tmp);
	tmp = RREG32_MC(RS600_MC_PT0_CNTL);
}

int rs600_gart_enable(struct radeon_device *rdev)
{
	uint32_t tmp;
	int i;
	int r;

	/* Initialize common gart structure */
	r = radeon_gart_init(rdev);
	if (r) {
		return r;
	}
	rdev->gart.table_size = rdev->gart.num_gpu_pages * 8;
	r = radeon_gart_table_vram_alloc(rdev);
	if (r) {
		return r;
	}
	/* FIXME: setup default page */
	WREG32_MC(RS600_MC_PT0_CNTL,
		 (RS600_EFFECTIVE_L2_CACHE_SIZE(6) |
		  RS600_EFFECTIVE_L2_QUEUE_SIZE(6)));
	for (i = 0; i < 19; i++) {
		WREG32_MC(RS600_MC_PT0_CLIENT0_CNTL + i,
			 (RS600_ENABLE_TRANSLATION_MODE_OVERRIDE |
			  RS600_SYSTEM_ACCESS_MODE_IN_SYS |
			  RS600_SYSTEM_APERTURE_UNMAPPED_ACCESS_DEFAULT_PAGE |
			  RS600_EFFECTIVE_L1_CACHE_SIZE(3) |
			  RS600_ENABLE_FRAGMENT_PROCESSING |
			  RS600_EFFECTIVE_L1_QUEUE_SIZE(3)));
	}

	/* System context map to GART space */
	WREG32_MC(RS600_MC_PT0_SYSTEM_APERTURE_LOW_ADDR, rdev->mc.gtt_location);
	tmp = rdev->mc.gtt_location + rdev->mc.gtt_size - 1;
	WREG32_MC(RS600_MC_PT0_SYSTEM_APERTURE_HIGH_ADDR, tmp);

	/* enable first context */
	WREG32_MC(RS600_MC_PT0_CONTEXT0_FLAT_START_ADDR, rdev->mc.gtt_location);
	tmp = rdev->mc.gtt_location + rdev->mc.gtt_size - 1;
	WREG32_MC(RS600_MC_PT0_CONTEXT0_FLAT_END_ADDR, tmp);
	WREG32_MC(RS600_MC_PT0_CONTEXT0_CNTL,
		 (RS600_ENABLE_PAGE_TABLE | RS600_PAGE_TABLE_TYPE_FLAT));
	/* disable all other contexts */
	for (i = 1; i < 8; i++) {
		WREG32_MC(RS600_MC_PT0_CONTEXT0_CNTL + i, 0);
	}

	/* setup the page table */
	WREG32_MC(RS600_MC_PT0_CONTEXT0_FLAT_BASE_ADDR,
		 rdev->gart.table_addr);
	WREG32_MC(RS600_MC_PT0_CONTEXT0_DEFAULT_READ_ADDR, 0);

	/* enable page tables */
	tmp = RREG32_MC(RS600_MC_PT0_CNTL);
	WREG32_MC(RS600_MC_PT0_CNTL, (tmp | RS600_ENABLE_PT));
	tmp = RREG32_MC(RS600_MC_CNTL1);
	WREG32_MC(RS600_MC_CNTL1, (tmp | RS600_ENABLE_PAGE_TABLES));
	rs600_gart_tlb_flush(rdev);
	rdev->gart.ready = true;
	return 0;
}

void rs600_gart_disable(struct radeon_device *rdev)
{
	uint32_t tmp;

	/* FIXME: disable out of gart access */
	WREG32_MC(RS600_MC_PT0_CNTL, 0);
	tmp = RREG32_MC(RS600_MC_CNTL1);
	tmp &= ~RS600_ENABLE_PAGE_TABLES;
	WREG32_MC(RS600_MC_CNTL1, tmp);
	radeon_object_kunmap(rdev->gart.table.vram.robj);
	radeon_object_unpin(rdev->gart.table.vram.robj);
}

#define R600_PTE_VALID     (1 << 0)
#define R600_PTE_SYSTEM    (1 << 1)
#define R600_PTE_SNOOPED   (1 << 2)
#define R600_PTE_READABLE  (1 << 5)
#define R600_PTE_WRITEABLE (1 << 6)

int rs600_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr)
{
	void __iomem *ptr = (void *)rdev->gart.table.vram.ptr;

	if (i < 0 || i > rdev->gart.num_gpu_pages) {
		return -EINVAL;
	}
	addr = addr & 0xFFFFFFFFFFFFF000ULL;
	addr |= R600_PTE_VALID | R600_PTE_SYSTEM | R600_PTE_SNOOPED;
	addr |= R600_PTE_READABLE | R600_PTE_WRITEABLE;
	writeq(addr, ((void __iomem *)ptr) + (i * 8));
	return 0;
}


/*
 * MC.
 */
void rs600_mc_disable_clients(struct radeon_device *rdev)
{
	unsigned tmp;

	if (r100_gui_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait GUI idle while "
		       "programming pipes. Bad things might happen.\n");
	}

	tmp = RREG32(AVIVO_D1VGA_CONTROL);
	WREG32(AVIVO_D1VGA_CONTROL, tmp & ~AVIVO_DVGA_CONTROL_MODE_ENABLE);
	tmp = RREG32(AVIVO_D2VGA_CONTROL);
	WREG32(AVIVO_D2VGA_CONTROL, tmp & ~AVIVO_DVGA_CONTROL_MODE_ENABLE);

	tmp = RREG32(AVIVO_D1CRTC_CONTROL);
	WREG32(AVIVO_D1CRTC_CONTROL, tmp & ~AVIVO_CRTC_EN);
	tmp = RREG32(AVIVO_D2CRTC_CONTROL);
	WREG32(AVIVO_D2CRTC_CONTROL, tmp & ~AVIVO_CRTC_EN);

	/* make sure all previous write got through */
	tmp = RREG32(AVIVO_D2CRTC_CONTROL);

	mdelay(1);
}

int rs600_mc_init(struct radeon_device *rdev)
{
	uint32_t tmp;
	int r;

	if (r100_debugfs_rbbm_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for RBBM !\n");
	}

	rs600_gpu_init(rdev);
	rs600_gart_disable(rdev);

	/* Setup GPU memory space */
	rdev->mc.vram_location = 0xFFFFFFFFUL;
	rdev->mc.gtt_location = 0xFFFFFFFFUL;
	r = radeon_mc_setup(rdev);
	if (r) {
		return r;
	}

	/* Program GPU memory space */
	/* Enable bus master */
	tmp = RREG32(RADEON_BUS_CNTL) & ~RS600_BUS_MASTER_DIS;
	WREG32(RADEON_BUS_CNTL, tmp);
	/* FIXME: What does AGP means for such chipset ? */
	WREG32_MC(RS600_MC_AGP_LOCATION, 0x0FFFFFFF);
	/* FIXME: are this AGP reg in indirect MC range ? */
	WREG32_MC(RS600_MC_AGP_BASE, 0);
	WREG32_MC(RS600_MC_AGP_BASE_2, 0);
	rs600_mc_disable_clients(rdev);
	if (rs600_mc_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait MC idle while "
		       "programming pipes. Bad things might happen.\n");
	}
	tmp = rdev->mc.vram_location + rdev->mc.mc_vram_size - 1;
	tmp = REG_SET(RS600_MC_FB_TOP, tmp >> 16);
	tmp |= REG_SET(RS600_MC_FB_START, rdev->mc.vram_location >> 16);
	WREG32_MC(RS600_MC_FB_LOCATION, tmp);
	WREG32(RS690_HDP_FB_LOCATION, rdev->mc.vram_location >> 16);
	return 0;
}

void rs600_mc_fini(struct radeon_device *rdev)
{
	rs600_gart_disable(rdev);
	radeon_gart_table_vram_free(rdev);
	radeon_gart_fini(rdev);
}


/*
 * Interrupts
 */
int rs600_irq_set(struct radeon_device *rdev)
{
	uint32_t tmp = 0;
	uint32_t mode_int = 0;

	if (rdev->irq.sw_int) {
		tmp |= RADEON_SW_INT_ENABLE;
	}
	if (rdev->irq.crtc_vblank_int[0]) {
		tmp |= AVIVO_DISPLAY_INT_STATUS;
		mode_int |= AVIVO_D1MODE_INT_MASK;
	}
	if (rdev->irq.crtc_vblank_int[1]) {
		tmp |= AVIVO_DISPLAY_INT_STATUS;
		mode_int |= AVIVO_D2MODE_INT_MASK;
	}
	WREG32(RADEON_GEN_INT_CNTL, tmp);
	WREG32(AVIVO_DxMODE_INT_MASK, mode_int);
	return 0;
}

static inline uint32_t rs600_irq_ack(struct radeon_device *rdev, u32 *r500_disp_int)
{
	uint32_t irqs = RREG32(RADEON_GEN_INT_STATUS);
	uint32_t irq_mask = RADEON_SW_INT_TEST;

	if (irqs & AVIVO_DISPLAY_INT_STATUS) {
		*r500_disp_int = RREG32(AVIVO_DISP_INTERRUPT_STATUS);
		if (*r500_disp_int & AVIVO_D1_VBLANK_INTERRUPT) {
			WREG32(AVIVO_D1MODE_VBLANK_STATUS, AVIVO_VBLANK_ACK);
		}
		if (*r500_disp_int & AVIVO_D2_VBLANK_INTERRUPT) {
			WREG32(AVIVO_D2MODE_VBLANK_STATUS, AVIVO_VBLANK_ACK);
		}
	} else {
		*r500_disp_int = 0;
	}

	if (irqs) {
		WREG32(RADEON_GEN_INT_STATUS, irqs);
	}
	return irqs & irq_mask;
}

int rs600_irq_process(struct radeon_device *rdev)
{
	uint32_t status;
	uint32_t r500_disp_int;

	status = rs600_irq_ack(rdev, &r500_disp_int);
	if (!status && !r500_disp_int) {
		return IRQ_NONE;
	}
	while (status || r500_disp_int) {
		/* SW interrupt */
		if (status & RADEON_SW_INT_TEST) {
			radeon_fence_process(rdev);
		}
		/* Vertical blank interrupts */
		if (r500_disp_int & AVIVO_D1_VBLANK_INTERRUPT) {
			drm_handle_vblank(rdev->ddev, 0);
		}
		if (r500_disp_int & AVIVO_D2_VBLANK_INTERRUPT) {
			drm_handle_vblank(rdev->ddev, 1);
		}
		status = rs600_irq_ack(rdev, &r500_disp_int);
	}
	return IRQ_HANDLED;
}

u32 rs600_get_vblank_counter(struct radeon_device *rdev, int crtc)
{
	if (crtc == 0)
		return RREG32(AVIVO_D1CRTC_FRAME_COUNT);
	else
		return RREG32(AVIVO_D2CRTC_FRAME_COUNT);
}


/*
 * Global GPU functions
 */
void rs600_disable_vga(struct radeon_device *rdev)
{
	unsigned tmp;

	WREG32(0x330, 0);
	WREG32(0x338, 0);
	tmp = RREG32(0x300);
	tmp &= ~(3 << 16);
	WREG32(0x300, tmp);
	WREG32(0x308, (1 << 8));
	WREG32(0x310, rdev->mc.vram_location);
	WREG32(0x594, 0);
}

int rs600_mc_wait_for_idle(struct radeon_device *rdev)
{
	unsigned i;
	uint32_t tmp;

	for (i = 0; i < rdev->usec_timeout; i++) {
		/* read MC_STATUS */
		tmp = RREG32_MC(RS600_MC_STATUS);
		if (tmp & RS600_MC_STATUS_IDLE) {
			return 0;
		}
		DRM_UDELAY(1);
	}
	return -1;
}

void rs600_errata(struct radeon_device *rdev)
{
	rdev->pll_errata = 0;
}

void rs600_gpu_init(struct radeon_device *rdev)
{
	/* FIXME: HDP same place on rs600 ? */
	r100_hdp_reset(rdev);
	rs600_disable_vga(rdev);
	/* FIXME: is this correct ? */
	r420_pipes_init(rdev);
	if (rs600_mc_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait MC idle while "
		       "programming pipes. Bad things might happen.\n");
	}
}


/*
 * VRAM info.
 */
void rs600_vram_info(struct radeon_device *rdev)
{
	/* FIXME: to do or is these values sane ? */
	rdev->mc.vram_is_ddr = true;
	rdev->mc.vram_width = 128;
}

void rs600_bandwidth_update(struct radeon_device *rdev)
{
	/* FIXME: implement, should this be like rs690 ? */
}


/*
 * Indirect registers accessor
 */
uint32_t rs600_mc_rreg(struct radeon_device *rdev, uint32_t reg)
{
	uint32_t r;

	WREG32(RS600_MC_INDEX,
	       ((reg & RS600_MC_ADDR_MASK) | RS600_MC_IND_CITF_ARB0));
	r = RREG32(RS600_MC_DATA);
	return r;
}

void rs600_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	WREG32(RS600_MC_INDEX,
		RS600_MC_IND_WR_EN | RS600_MC_IND_CITF_ARB0 |
		((reg) & RS600_MC_ADDR_MASK));
	WREG32(RS600_MC_DATA, v);
}

static const unsigned rs600_reg_safe_bm[219] = {
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0x17FF1FFF, 0xFFFFFFFC, 0xFFFFFFFF, 0xFF30FFBF,
	0xFFFFFFF8, 0xC3E6FFFF, 0xFFFFF6DF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFF03F,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFEFCE, 0xF00EBFFF, 0x007C0000,
	0xF0000078, 0xFF000009, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFF7FF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFC78, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFF,
	0x38FF8F50, 0xFFF88082, 0xF000000C, 0xFAE009FF,
	0x0000FFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000,
	0x00000000, 0x0000C100, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0xFFFF0000, 0xFFFFFFFF, 0xFF80FFFF,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x0003FC01, 0xFFFFFCF8, 0xFF800B19, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
};

int rs600_init(struct radeon_device *rdev)
{
	rdev->config.r300.reg_safe_bm = rs600_reg_safe_bm;
	rdev->config.r300.reg_safe_bm_size = ARRAY_SIZE(rs600_reg_safe_bm);
	return 0;
}
