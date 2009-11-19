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
/* RS600 / Radeon X1250/X1270 integrated GPU
 *
 * This file gather function specific to RS600 which is the IGP of
 * the X1250/X1270 family supporting intel CPU (while RS690/RS740
 * is the X1250/X1270 supporting AMD CPU). The display engine are
 * the avivo one, bios is an atombios, 3D block are the one of the
 * R4XX family. The GART is different from the RS400 one and is very
 * close to the one of the R600 family (R600 likely being an evolution
 * of the RS600 GART block).
 */
#include "drmP.h"
#include "radeon.h"
#include "atom.h"
#include "rs600d.h"

#include "rs600_reg_safe.h"

void rs600_gpu_init(struct radeon_device *rdev);
int rs600_mc_wait_for_idle(struct radeon_device *rdev);

/*
 * GART.
 */
void rs600_gart_tlb_flush(struct radeon_device *rdev)
{
	uint32_t tmp;

	tmp = RREG32_MC(R_000100_MC_PT0_CNTL);
	tmp &= C_000100_INVALIDATE_ALL_L1_TLBS & C_000100_INVALIDATE_L2_CACHE;
	WREG32_MC(R_000100_MC_PT0_CNTL, tmp);

	tmp = RREG32_MC(R_000100_MC_PT0_CNTL);
	tmp |= S_000100_INVALIDATE_ALL_L1_TLBS(1) & S_000100_INVALIDATE_L2_CACHE(1);
	WREG32_MC(R_000100_MC_PT0_CNTL, tmp);

	tmp = RREG32_MC(R_000100_MC_PT0_CNTL);
	tmp &= C_000100_INVALIDATE_ALL_L1_TLBS & C_000100_INVALIDATE_L2_CACHE;
	WREG32_MC(R_000100_MC_PT0_CNTL, tmp);
	tmp = RREG32_MC(R_000100_MC_PT0_CNTL);
}

int rs600_gart_init(struct radeon_device *rdev)
{
	int r;

	if (rdev->gart.table.vram.robj) {
		WARN(1, "RS600 GART already initialized.\n");
		return 0;
	}
	/* Initialize common gart structure */
	r = radeon_gart_init(rdev);
	if (r) {
		return r;
	}
	rdev->gart.table_size = rdev->gart.num_gpu_pages * 8;
	return radeon_gart_table_vram_alloc(rdev);
}

int rs600_gart_enable(struct radeon_device *rdev)
{
	u32 tmp;
	int r, i;

	if (rdev->gart.table.vram.robj == NULL) {
		dev_err(rdev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}
	r = radeon_gart_table_vram_pin(rdev);
	if (r)
		return r;
	/* Enable bus master */
	tmp = RREG32(R_00004C_BUS_CNTL) & C_00004C_BUS_MASTER_DIS;
	WREG32(R_00004C_BUS_CNTL, tmp);
	/* FIXME: setup default page */
	WREG32_MC(R_000100_MC_PT0_CNTL,
		 (S_000100_EFFECTIVE_L2_CACHE_SIZE(6) |
		  S_000100_EFFECTIVE_L2_QUEUE_SIZE(6)));
	for (i = 0; i < 19; i++) {
		WREG32_MC(R_00016C_MC_PT0_CLIENT0_CNTL + i,
			S_00016C_ENABLE_TRANSLATION_MODE_OVERRIDE(1) |
			S_00016C_SYSTEM_ACCESS_MODE_MASK(
				V_00016C_SYSTEM_ACCESS_MODE_IN_SYS) |
			S_00016C_SYSTEM_APERTURE_UNMAPPED_ACCESS(
				V_00016C_SYSTEM_APERTURE_UNMAPPED_DEFAULT_PAGE) |
			S_00016C_EFFECTIVE_L1_CACHE_SIZE(1) |
			S_00016C_ENABLE_FRAGMENT_PROCESSING(1) |
			S_00016C_EFFECTIVE_L1_QUEUE_SIZE(1));
	}

	/* System context map to GART space */
	WREG32_MC(R_000112_MC_PT0_SYSTEM_APERTURE_LOW_ADDR, rdev->mc.gtt_start);
	WREG32_MC(R_000114_MC_PT0_SYSTEM_APERTURE_HIGH_ADDR, rdev->mc.gtt_end);

	/* enable first context */
	WREG32_MC(R_00013C_MC_PT0_CONTEXT0_FLAT_START_ADDR, rdev->mc.gtt_start);
	WREG32_MC(R_00014C_MC_PT0_CONTEXT0_FLAT_END_ADDR, rdev->mc.gtt_end);
	WREG32_MC(R_000102_MC_PT0_CONTEXT0_CNTL,
			S_000102_ENABLE_PAGE_TABLE(1) |
			S_000102_PAGE_TABLE_DEPTH(V_000102_PAGE_TABLE_FLAT));
	/* disable all other contexts */
	for (i = 1; i < 8; i++) {
		WREG32_MC(R_000102_MC_PT0_CONTEXT0_CNTL + i, 0);
	}

	/* setup the page table */
	WREG32_MC(R_00012C_MC_PT0_CONTEXT0_FLAT_BASE_ADDR,
			rdev->gart.table_addr);
	WREG32_MC(R_00011C_MC_PT0_CONTEXT0_DEFAULT_READ_ADDR, 0);

	/* enable page tables */
	tmp = RREG32_MC(R_000100_MC_PT0_CNTL);
	WREG32_MC(R_000100_MC_PT0_CNTL, (tmp | S_000100_ENABLE_PT(1)));
	tmp = RREG32_MC(R_000009_MC_CNTL1);
	WREG32_MC(R_000009_MC_CNTL1, (tmp | S_000009_ENABLE_PAGE_TABLES(1)));
	rs600_gart_tlb_flush(rdev);
	rdev->gart.ready = true;
	return 0;
}

void rs600_gart_disable(struct radeon_device *rdev)
{
	uint32_t tmp;

	/* FIXME: disable out of gart access */
	WREG32_MC(R_000100_MC_PT0_CNTL, 0);
	tmp = RREG32_MC(R_000009_MC_CNTL1);
	WREG32_MC(R_000009_MC_CNTL1, tmp & C_000009_ENABLE_PAGE_TABLES);
	if (rdev->gart.table.vram.robj) {
		radeon_object_kunmap(rdev->gart.table.vram.robj);
		radeon_object_unpin(rdev->gart.table.vram.robj);
	}
}

void rs600_gart_fini(struct radeon_device *rdev)
{
	rs600_gart_disable(rdev);
	radeon_gart_table_vram_free(rdev);
	radeon_gart_fini(rdev);
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

int rs600_irq_set(struct radeon_device *rdev)
{
	uint32_t tmp = 0;
	uint32_t mode_int = 0;

	if (rdev->irq.sw_int) {
		tmp |= S_000040_SW_INT_EN(1);
	}
	if (rdev->irq.crtc_vblank_int[0]) {
		mode_int |= S_006540_D1MODE_VBLANK_INT_MASK(1);
	}
	if (rdev->irq.crtc_vblank_int[1]) {
		mode_int |= S_006540_D2MODE_VBLANK_INT_MASK(1);
	}
	WREG32(R_000040_GEN_INT_CNTL, tmp);
	WREG32(R_006540_DxMODE_INT_MASK, mode_int);
	return 0;
}

static inline uint32_t rs600_irq_ack(struct radeon_device *rdev, u32 *r500_disp_int)
{
	uint32_t irqs = RREG32(R_000044_GEN_INT_STATUS);
	uint32_t irq_mask = ~C_000044_SW_INT;

	if (G_000044_DISPLAY_INT_STAT(irqs)) {
		*r500_disp_int = RREG32(R_007EDC_DISP_INTERRUPT_STATUS);
		if (G_007EDC_LB_D1_VBLANK_INTERRUPT(*r500_disp_int)) {
			WREG32(R_006534_D1MODE_VBLANK_STATUS,
				S_006534_D1MODE_VBLANK_ACK(1));
		}
		if (G_007EDC_LB_D2_VBLANK_INTERRUPT(*r500_disp_int)) {
			WREG32(R_006D34_D2MODE_VBLANK_STATUS,
				S_006D34_D2MODE_VBLANK_ACK(1));
		}
	} else {
		*r500_disp_int = 0;
	}

	if (irqs) {
		WREG32(R_000044_GEN_INT_STATUS, irqs);
	}
	return irqs & irq_mask;
}

void rs600_irq_disable(struct radeon_device *rdev)
{
	u32 tmp;

	WREG32(R_000040_GEN_INT_CNTL, 0);
	WREG32(R_006540_DxMODE_INT_MASK, 0);
	/* Wait and acknowledge irq */
	mdelay(1);
	rs600_irq_ack(rdev, &tmp);
}

int rs600_irq_process(struct radeon_device *rdev)
{
	uint32_t status, msi_rearm;
	uint32_t r500_disp_int;

	status = rs600_irq_ack(rdev, &r500_disp_int);
	if (!status && !r500_disp_int) {
		return IRQ_NONE;
	}
	while (status || r500_disp_int) {
		/* SW interrupt */
		if (G_000040_SW_INT_EN(status))
			radeon_fence_process(rdev);
		/* Vertical blank interrupts */
		if (G_007EDC_LB_D1_VBLANK_INTERRUPT(r500_disp_int))
			drm_handle_vblank(rdev->ddev, 0);
		if (G_007EDC_LB_D2_VBLANK_INTERRUPT(r500_disp_int))
			drm_handle_vblank(rdev->ddev, 1);
		status = rs600_irq_ack(rdev, &r500_disp_int);
	}
	if (rdev->msi_enabled) {
		switch (rdev->family) {
		case CHIP_RS600:
		case CHIP_RS690:
		case CHIP_RS740:
			msi_rearm = RREG32(RADEON_BUS_CNTL) & ~RS600_MSI_REARM;
			WREG32(RADEON_BUS_CNTL, msi_rearm);
			WREG32(RADEON_BUS_CNTL, msi_rearm | RS600_MSI_REARM);
			break;
		default:
			msi_rearm = RREG32(RADEON_MSI_REARM_EN) & ~RV370_MSI_REARM_EN;
			WREG32(RADEON_MSI_REARM_EN, msi_rearm);
			WREG32(RADEON_MSI_REARM_EN, msi_rearm | RV370_MSI_REARM_EN);
			break;
		}
	}
	return IRQ_HANDLED;
}

u32 rs600_get_vblank_counter(struct radeon_device *rdev, int crtc)
{
	if (crtc == 0)
		return RREG32(R_0060A4_D1CRTC_STATUS_FRAME_COUNT);
	else
		return RREG32(R_0068A4_D2CRTC_STATUS_FRAME_COUNT);
}

int rs600_mc_wait_for_idle(struct radeon_device *rdev)
{
	unsigned i;

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (G_000000_MC_IDLE(RREG32_MC(R_000000_MC_STATUS)))
			return 0;
		udelay(1);
	}
	return -1;
}

void rs600_gpu_init(struct radeon_device *rdev)
{
	/* FIXME: HDP same place on rs600 ? */
	r100_hdp_reset(rdev);
	/* FIXME: is this correct ? */
	r420_pipes_init(rdev);
	/* Wait for mc idle */
	if (rs600_mc_wait_for_idle(rdev))
		dev_warn(rdev->dev, "Wait MC idle timeout before updating MC.\n");
}

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

uint32_t rs600_mc_rreg(struct radeon_device *rdev, uint32_t reg)
{
	WREG32(R_000070_MC_IND_INDEX, S_000070_MC_IND_ADDR(reg) |
		S_000070_MC_IND_CITF_ARB0(1));
	return RREG32(R_000074_MC_IND_DATA);
}

void rs600_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	WREG32(R_000070_MC_IND_INDEX, S_000070_MC_IND_ADDR(reg) |
		S_000070_MC_IND_CITF_ARB0(1) | S_000070_MC_IND_WR_EN(1));
	WREG32(R_000074_MC_IND_DATA, v);
}

void rs600_debugfs(struct radeon_device *rdev)
{
	if (r100_debugfs_rbbm_init(rdev))
		DRM_ERROR("Failed to register debugfs file for RBBM !\n");
}

void rs600_set_safe_registers(struct radeon_device *rdev)
{
	rdev->config.r300.reg_safe_bm = rs600_reg_safe_bm;
	rdev->config.r300.reg_safe_bm_size = ARRAY_SIZE(rs600_reg_safe_bm);
}

static void rs600_mc_program(struct radeon_device *rdev)
{
	struct rv515_mc_save save;

	/* Stops all mc clients */
	rv515_mc_stop(rdev, &save);

	/* Wait for mc idle */
	if (rs600_mc_wait_for_idle(rdev))
		dev_warn(rdev->dev, "Wait MC idle timeout before updating MC.\n");

	/* FIXME: What does AGP means for such chipset ? */
	WREG32_MC(R_000005_MC_AGP_LOCATION, 0x0FFFFFFF);
	WREG32_MC(R_000006_AGP_BASE, 0);
	WREG32_MC(R_000007_AGP_BASE_2, 0);
	/* Program MC */
	WREG32_MC(R_000004_MC_FB_LOCATION,
			S_000004_MC_FB_START(rdev->mc.vram_start >> 16) |
			S_000004_MC_FB_TOP(rdev->mc.vram_end >> 16));
	WREG32(R_000134_HDP_FB_LOCATION,
		S_000134_HDP_FB_START(rdev->mc.vram_start >> 16));

	rv515_mc_resume(rdev, &save);
}

static int rs600_startup(struct radeon_device *rdev)
{
	int r;

	rs600_mc_program(rdev);
	/* Resume clock */
	rv515_clock_startup(rdev);
	/* Initialize GPU configuration (# pipes, ...) */
	rs600_gpu_init(rdev);
	/* Initialize GART (initialize after TTM so we can allocate
	 * memory through TTM but finalize after TTM) */
	r = rs600_gart_enable(rdev);
	if (r)
		return r;
	/* Enable IRQ */
	rdev->irq.sw_int = true;
	rs600_irq_set(rdev);
	/* 1M ring buffer */
	r = r100_cp_init(rdev, 1024 * 1024);
	if (r) {
		dev_err(rdev->dev, "failled initializing CP (%d).\n", r);
		return r;
	}
	r = r100_wb_init(rdev);
	if (r)
		dev_err(rdev->dev, "failled initializing WB (%d).\n", r);
	r = r100_ib_init(rdev);
	if (r) {
		dev_err(rdev->dev, "failled initializing IB (%d).\n", r);
		return r;
	}
	return 0;
}

int rs600_resume(struct radeon_device *rdev)
{
	/* Make sur GART are not working */
	rs600_gart_disable(rdev);
	/* Resume clock before doing reset */
	rv515_clock_startup(rdev);
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_gpu_reset(rdev)) {
		dev_warn(rdev->dev, "GPU reset failed ! (0xE40=0x%08X, 0x7C0=0x%08X)\n",
			RREG32(R_000E40_RBBM_STATUS),
			RREG32(R_0007C0_CP_STAT));
	}
	/* post */
	atom_asic_init(rdev->mode_info.atom_context);
	/* Resume clock after posting */
	rv515_clock_startup(rdev);
	return rs600_startup(rdev);
}

int rs600_suspend(struct radeon_device *rdev)
{
	r100_cp_disable(rdev);
	r100_wb_disable(rdev);
	rs600_irq_disable(rdev);
	rs600_gart_disable(rdev);
	return 0;
}

void rs600_fini(struct radeon_device *rdev)
{
	rs600_suspend(rdev);
	r100_cp_fini(rdev);
	r100_wb_fini(rdev);
	r100_ib_fini(rdev);
	radeon_gem_fini(rdev);
	rs600_gart_fini(rdev);
	radeon_irq_kms_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_object_fini(rdev);
	radeon_atombios_fini(rdev);
	kfree(rdev->bios);
	rdev->bios = NULL;
}

int rs600_init(struct radeon_device *rdev)
{
	int r;

	/* Disable VGA */
	rv515_vga_render_disable(rdev);
	/* Initialize scratch registers */
	radeon_scratch_init(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	/* BIOS */
	if (!radeon_get_bios(rdev)) {
		if (ASIC_IS_AVIVO(rdev))
			return -EINVAL;
	}
	if (rdev->is_atom_bios) {
		r = radeon_atombios_init(rdev);
		if (r)
			return r;
	} else {
		dev_err(rdev->dev, "Expecting atombios for RS600 GPU\n");
		return -EINVAL;
	}
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_gpu_reset(rdev)) {
		dev_warn(rdev->dev,
			"GPU reset failed ! (0xE40=0x%08X, 0x7C0=0x%08X)\n",
			RREG32(R_000E40_RBBM_STATUS),
			RREG32(R_0007C0_CP_STAT));
	}
	/* check if cards are posted or not */
	if (!radeon_card_posted(rdev) && rdev->bios) {
		DRM_INFO("GPU not posted. posting now...\n");
		atom_asic_init(rdev->mode_info.atom_context);
	}
	/* Initialize clocks */
	radeon_get_clock_info(rdev->ddev);
	/* Initialize power management */
	radeon_pm_init(rdev);
	/* Get vram informations */
	rs600_vram_info(rdev);
	/* Initialize memory controller (also test AGP) */
	r = r420_mc_init(rdev);
	if (r)
		return r;
	rs600_debugfs(rdev);
	/* Fence driver */
	r = radeon_fence_driver_init(rdev);
	if (r)
		return r;
	r = radeon_irq_kms_init(rdev);
	if (r)
		return r;
	/* Memory manager */
	r = radeon_object_init(rdev);
	if (r)
		return r;
	r = rs600_gart_init(rdev);
	if (r)
		return r;
	rs600_set_safe_registers(rdev);
	rdev->accel_working = true;
	r = rs600_startup(rdev);
	if (r) {
		/* Somethings want wront with the accel init stop accel */
		dev_err(rdev->dev, "Disabling GPU acceleration\n");
		rs600_suspend(rdev);
		r100_cp_fini(rdev);
		r100_wb_fini(rdev);
		r100_ib_fini(rdev);
		rs600_gart_fini(rdev);
		radeon_irq_kms_fini(rdev);
		rdev->accel_working = false;
	}
	return 0;
}
