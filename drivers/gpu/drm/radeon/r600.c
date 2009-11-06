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
#include <linux/seq_file.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include "drmP.h"
#include "radeon_drm.h"
#include "radeon.h"
#include "radeon_mode.h"
#include "r600d.h"
#include "atom.h"
#include "avivod.h"

#define PFP_UCODE_SIZE 576
#define PM4_UCODE_SIZE 1792
#define R700_PFP_UCODE_SIZE 848
#define R700_PM4_UCODE_SIZE 1360

/* Firmware Names */
MODULE_FIRMWARE("radeon/R600_pfp.bin");
MODULE_FIRMWARE("radeon/R600_me.bin");
MODULE_FIRMWARE("radeon/RV610_pfp.bin");
MODULE_FIRMWARE("radeon/RV610_me.bin");
MODULE_FIRMWARE("radeon/RV630_pfp.bin");
MODULE_FIRMWARE("radeon/RV630_me.bin");
MODULE_FIRMWARE("radeon/RV620_pfp.bin");
MODULE_FIRMWARE("radeon/RV620_me.bin");
MODULE_FIRMWARE("radeon/RV635_pfp.bin");
MODULE_FIRMWARE("radeon/RV635_me.bin");
MODULE_FIRMWARE("radeon/RV670_pfp.bin");
MODULE_FIRMWARE("radeon/RV670_me.bin");
MODULE_FIRMWARE("radeon/RS780_pfp.bin");
MODULE_FIRMWARE("radeon/RS780_me.bin");
MODULE_FIRMWARE("radeon/RV770_pfp.bin");
MODULE_FIRMWARE("radeon/RV770_me.bin");
MODULE_FIRMWARE("radeon/RV730_pfp.bin");
MODULE_FIRMWARE("radeon/RV730_me.bin");
MODULE_FIRMWARE("radeon/RV710_pfp.bin");
MODULE_FIRMWARE("radeon/RV710_me.bin");

int r600_debugfs_mc_info_init(struct radeon_device *rdev);

/* r600,rv610,rv630,rv620,rv635,rv670 */
int r600_mc_wait_for_idle(struct radeon_device *rdev);
void r600_gpu_init(struct radeon_device *rdev);
void r600_fini(struct radeon_device *rdev);

/*
 * R600 PCIE GART
 */
int r600_gart_clear_page(struct radeon_device *rdev, int i)
{
	void __iomem *ptr = (void *)rdev->gart.table.vram.ptr;
	u64 pte;

	if (i < 0 || i > rdev->gart.num_gpu_pages)
		return -EINVAL;
	pte = 0;
	writeq(pte, ((void __iomem *)ptr) + (i * 8));
	return 0;
}

void r600_pcie_gart_tlb_flush(struct radeon_device *rdev)
{
	unsigned i;
	u32 tmp;

	WREG32(VM_CONTEXT0_INVALIDATION_LOW_ADDR, rdev->mc.gtt_start >> 12);
	WREG32(VM_CONTEXT0_INVALIDATION_HIGH_ADDR, (rdev->mc.gtt_end - 1) >> 12);
	WREG32(VM_CONTEXT0_REQUEST_RESPONSE, REQUEST_TYPE(1));
	for (i = 0; i < rdev->usec_timeout; i++) {
		/* read MC_STATUS */
		tmp = RREG32(VM_CONTEXT0_REQUEST_RESPONSE);
		tmp = (tmp & RESPONSE_TYPE_MASK) >> RESPONSE_TYPE_SHIFT;
		if (tmp == 2) {
			printk(KERN_WARNING "[drm] r600 flush TLB failed\n");
			return;
		}
		if (tmp) {
			return;
		}
		udelay(1);
	}
}

int r600_pcie_gart_init(struct radeon_device *rdev)
{
	int r;

	if (rdev->gart.table.vram.robj) {
		WARN(1, "R600 PCIE GART already initialized.\n");
		return 0;
	}
	/* Initialize common gart structure */
	r = radeon_gart_init(rdev);
	if (r)
		return r;
	rdev->gart.table_size = rdev->gart.num_gpu_pages * 8;
	return radeon_gart_table_vram_alloc(rdev);
}

int r600_pcie_gart_enable(struct radeon_device *rdev)
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

	/* Setup L2 cache */
	WREG32(VM_L2_CNTL, ENABLE_L2_CACHE | ENABLE_L2_FRAGMENT_PROCESSING |
				ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE |
				EFFECTIVE_L2_QUEUE_SIZE(7));
	WREG32(VM_L2_CNTL2, 0);
	WREG32(VM_L2_CNTL3, BANK_SELECT_0(0) | BANK_SELECT_1(1));
	/* Setup TLB control */
	tmp = ENABLE_L1_TLB | ENABLE_L1_FRAGMENT_PROCESSING |
		SYSTEM_ACCESS_MODE_NOT_IN_SYS |
		EFFECTIVE_L1_TLB_SIZE(5) | EFFECTIVE_L1_QUEUE_SIZE(5) |
		ENABLE_WAIT_L2_QUERY;
	WREG32(MC_VM_L1_TLB_MCB_RD_SYS_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_WR_SYS_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_RD_HDP_CNTL, tmp | ENABLE_L1_STRICT_ORDERING);
	WREG32(MC_VM_L1_TLB_MCB_WR_HDP_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCD_RD_A_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCD_WR_A_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCD_RD_B_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCD_WR_B_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_RD_GFX_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_WR_GFX_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_RD_PDMA_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_WR_PDMA_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_RD_SEM_CNTL, tmp | ENABLE_SEMAPHORE_MODE);
	WREG32(MC_VM_L1_TLB_MCB_WR_SEM_CNTL, tmp | ENABLE_SEMAPHORE_MODE);
	WREG32(VM_CONTEXT0_PAGE_TABLE_START_ADDR, rdev->mc.gtt_start >> 12);
	WREG32(VM_CONTEXT0_PAGE_TABLE_END_ADDR, rdev->mc.gtt_end >> 12);
	WREG32(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR, rdev->gart.table_addr >> 12);
	WREG32(VM_CONTEXT0_CNTL, ENABLE_CONTEXT | PAGE_TABLE_DEPTH(0) |
				RANGE_PROTECTION_FAULT_ENABLE_DEFAULT);
	WREG32(VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR,
			(u32)(rdev->dummy_page.addr >> 12));
	for (i = 1; i < 7; i++)
		WREG32(VM_CONTEXT0_CNTL + (i * 4), 0);

	r600_pcie_gart_tlb_flush(rdev);
	rdev->gart.ready = true;
	return 0;
}

void r600_pcie_gart_disable(struct radeon_device *rdev)
{
	u32 tmp;
	int i;

	/* Disable all tables */
	for (i = 0; i < 7; i++)
		WREG32(VM_CONTEXT0_CNTL + (i * 4), 0);

	/* Disable L2 cache */
	WREG32(VM_L2_CNTL, ENABLE_L2_FRAGMENT_PROCESSING |
				EFFECTIVE_L2_QUEUE_SIZE(7));
	WREG32(VM_L2_CNTL3, BANK_SELECT_0(0) | BANK_SELECT_1(1));
	/* Setup L1 TLB control */
	tmp = EFFECTIVE_L1_TLB_SIZE(5) | EFFECTIVE_L1_QUEUE_SIZE(5) |
		ENABLE_WAIT_L2_QUERY;
	WREG32(MC_VM_L1_TLB_MCD_RD_A_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCD_WR_A_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCD_RD_B_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCD_WR_B_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_RD_GFX_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_WR_GFX_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_RD_PDMA_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_WR_PDMA_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_RD_SEM_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_WR_SEM_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_RD_SYS_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_WR_SYS_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_RD_HDP_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_WR_HDP_CNTL, tmp);
	if (rdev->gart.table.vram.robj) {
		radeon_object_kunmap(rdev->gart.table.vram.robj);
		radeon_object_unpin(rdev->gart.table.vram.robj);
	}
}

void r600_pcie_gart_fini(struct radeon_device *rdev)
{
	r600_pcie_gart_disable(rdev);
	radeon_gart_table_vram_free(rdev);
	radeon_gart_fini(rdev);
}

void r600_agp_enable(struct radeon_device *rdev)
{
	u32 tmp;
	int i;

	/* Setup L2 cache */
	WREG32(VM_L2_CNTL, ENABLE_L2_CACHE | ENABLE_L2_FRAGMENT_PROCESSING |
				ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE |
				EFFECTIVE_L2_QUEUE_SIZE(7));
	WREG32(VM_L2_CNTL2, 0);
	WREG32(VM_L2_CNTL3, BANK_SELECT_0(0) | BANK_SELECT_1(1));
	/* Setup TLB control */
	tmp = ENABLE_L1_TLB | ENABLE_L1_FRAGMENT_PROCESSING |
		SYSTEM_ACCESS_MODE_NOT_IN_SYS |
		EFFECTIVE_L1_TLB_SIZE(5) | EFFECTIVE_L1_QUEUE_SIZE(5) |
		ENABLE_WAIT_L2_QUERY;
	WREG32(MC_VM_L1_TLB_MCB_RD_SYS_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_WR_SYS_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_RD_HDP_CNTL, tmp | ENABLE_L1_STRICT_ORDERING);
	WREG32(MC_VM_L1_TLB_MCB_WR_HDP_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCD_RD_A_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCD_WR_A_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCD_RD_B_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCD_WR_B_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_RD_GFX_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_WR_GFX_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_RD_PDMA_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_WR_PDMA_CNTL, tmp);
	WREG32(MC_VM_L1_TLB_MCB_RD_SEM_CNTL, tmp | ENABLE_SEMAPHORE_MODE);
	WREG32(MC_VM_L1_TLB_MCB_WR_SEM_CNTL, tmp | ENABLE_SEMAPHORE_MODE);
	for (i = 0; i < 7; i++)
		WREG32(VM_CONTEXT0_CNTL + (i * 4), 0);
}

int r600_mc_wait_for_idle(struct radeon_device *rdev)
{
	unsigned i;
	u32 tmp;

	for (i = 0; i < rdev->usec_timeout; i++) {
		/* read MC_STATUS */
		tmp = RREG32(R_000E50_SRBM_STATUS) & 0x3F00;
		if (!tmp)
			return 0;
		udelay(1);
	}
	return -1;
}

static void r600_mc_program(struct radeon_device *rdev)
{
	struct rv515_mc_save save;
	u32 tmp;
	int i, j;

	/* Initialize HDP */
	for (i = 0, j = 0; i < 32; i++, j += 0x18) {
		WREG32((0x2c14 + j), 0x00000000);
		WREG32((0x2c18 + j), 0x00000000);
		WREG32((0x2c1c + j), 0x00000000);
		WREG32((0x2c20 + j), 0x00000000);
		WREG32((0x2c24 + j), 0x00000000);
	}
	WREG32(HDP_REG_COHERENCY_FLUSH_CNTL, 0);

	rv515_mc_stop(rdev, &save);
	if (r600_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}
	/* Lockout access through VGA aperture (doesn't exist before R600) */
	WREG32(VGA_HDP_CONTROL, VGA_MEMORY_DISABLE);
	/* Update configuration */
	if (rdev->flags & RADEON_IS_AGP) {
		if (rdev->mc.vram_start < rdev->mc.gtt_start) {
			/* VRAM before AGP */
			WREG32(MC_VM_SYSTEM_APERTURE_LOW_ADDR,
				rdev->mc.vram_start >> 12);
			WREG32(MC_VM_SYSTEM_APERTURE_HIGH_ADDR,
				rdev->mc.gtt_end >> 12);
		} else {
			/* VRAM after AGP */
			WREG32(MC_VM_SYSTEM_APERTURE_LOW_ADDR,
				rdev->mc.gtt_start >> 12);
			WREG32(MC_VM_SYSTEM_APERTURE_HIGH_ADDR,
				rdev->mc.vram_end >> 12);
		}
	} else {
		WREG32(MC_VM_SYSTEM_APERTURE_LOW_ADDR, rdev->mc.vram_start >> 12);
		WREG32(MC_VM_SYSTEM_APERTURE_HIGH_ADDR, rdev->mc.vram_end >> 12);
	}
	WREG32(MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR, 0);
	tmp = ((rdev->mc.vram_end >> 24) & 0xFFFF) << 16;
	tmp |= ((rdev->mc.vram_start >> 24) & 0xFFFF);
	WREG32(MC_VM_FB_LOCATION, tmp);
	WREG32(HDP_NONSURFACE_BASE, (rdev->mc.vram_start >> 8));
	WREG32(HDP_NONSURFACE_INFO, (2 << 7));
	WREG32(HDP_NONSURFACE_SIZE, rdev->mc.mc_vram_size | 0x3FF);
	if (rdev->flags & RADEON_IS_AGP) {
		WREG32(MC_VM_AGP_TOP, rdev->mc.gtt_end >> 22);
		WREG32(MC_VM_AGP_BOT, rdev->mc.gtt_start >> 22);
		WREG32(MC_VM_AGP_BASE, rdev->mc.agp_base >> 22);
	} else {
		WREG32(MC_VM_AGP_BASE, 0);
		WREG32(MC_VM_AGP_TOP, 0x0FFFFFFF);
		WREG32(MC_VM_AGP_BOT, 0x0FFFFFFF);
	}
	if (r600_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}
	rv515_mc_resume(rdev, &save);
	/* we need to own VRAM, so turn off the VGA renderer here
	 * to stop it overwriting our objects */
	rv515_vga_render_disable(rdev);
}

int r600_mc_init(struct radeon_device *rdev)
{
	fixed20_12 a;
	u32 tmp;
	int chansize, numchan;
	int r;

	/* Get VRAM informations */
	rdev->mc.vram_is_ddr = true;
	tmp = RREG32(RAMCFG);
	if (tmp & CHANSIZE_OVERRIDE) {
		chansize = 16;
	} else if (tmp & CHANSIZE_MASK) {
		chansize = 64;
	} else {
		chansize = 32;
	}
	tmp = RREG32(CHMAP);
	switch ((tmp & NOOFCHAN_MASK) >> NOOFCHAN_SHIFT) {
	case 0:
	default:
		numchan = 1;
		break;
	case 1:
		numchan = 2;
		break;
	case 2:
		numchan = 4;
		break;
	case 3:
		numchan = 8;
		break;
	}
	rdev->mc.vram_width = numchan * chansize;
	/* Could aper size report 0 ? */
	rdev->mc.aper_base = drm_get_resource_start(rdev->ddev, 0);
	rdev->mc.aper_size = drm_get_resource_len(rdev->ddev, 0);
	/* Setup GPU memory space */
	rdev->mc.mc_vram_size = RREG32(CONFIG_MEMSIZE);
	rdev->mc.real_vram_size = RREG32(CONFIG_MEMSIZE);

	if (rdev->mc.mc_vram_size > rdev->mc.aper_size)
		rdev->mc.mc_vram_size = rdev->mc.aper_size;

	if (rdev->mc.real_vram_size > rdev->mc.aper_size)
		rdev->mc.real_vram_size = rdev->mc.aper_size;

	if (rdev->flags & RADEON_IS_AGP) {
		r = radeon_agp_init(rdev);
		if (r)
			return r;
		/* gtt_size is setup by radeon_agp_init */
		rdev->mc.gtt_location = rdev->mc.agp_base;
		tmp = 0xFFFFFFFFUL - rdev->mc.agp_base - rdev->mc.gtt_size;
		/* Try to put vram before or after AGP because we
		 * we want SYSTEM_APERTURE to cover both VRAM and
		 * AGP so that GPU can catch out of VRAM/AGP access
		 */
		if (rdev->mc.gtt_location > rdev->mc.mc_vram_size) {
			/* Enought place before */
			rdev->mc.vram_location = rdev->mc.gtt_location -
							rdev->mc.mc_vram_size;
		} else if (tmp > rdev->mc.mc_vram_size) {
			/* Enought place after */
			rdev->mc.vram_location = rdev->mc.gtt_location +
							rdev->mc.gtt_size;
		} else {
			/* Try to setup VRAM then AGP might not
			 * not work on some card
			 */
			rdev->mc.vram_location = 0x00000000UL;
			rdev->mc.gtt_location = rdev->mc.mc_vram_size;
		}
	} else {
		rdev->mc.gtt_size = radeon_gart_size * 1024 * 1024;
		rdev->mc.vram_location = (RREG32(MC_VM_FB_LOCATION) &
							0xFFFF) << 24;
		tmp = rdev->mc.vram_location + rdev->mc.mc_vram_size;
		if ((0xFFFFFFFFUL - tmp) >= rdev->mc.gtt_size) {
			/* Enough place after vram */
			rdev->mc.gtt_location = tmp;
		} else if (rdev->mc.vram_location >= rdev->mc.gtt_size) {
			/* Enough place before vram */
			rdev->mc.gtt_location = 0;
		} else {
			/* Not enough place after or before shrink
			 * gart size
			 */
			if (rdev->mc.vram_location > (0xFFFFFFFFUL - tmp)) {
				rdev->mc.gtt_location = 0;
				rdev->mc.gtt_size = rdev->mc.vram_location;
			} else {
				rdev->mc.gtt_location = tmp;
				rdev->mc.gtt_size = 0xFFFFFFFFUL - tmp;
			}
		}
		rdev->mc.gtt_location = rdev->mc.mc_vram_size;
	}
	rdev->mc.vram_start = rdev->mc.vram_location;
	rdev->mc.vram_end = rdev->mc.vram_location + rdev->mc.mc_vram_size - 1;
	rdev->mc.gtt_start = rdev->mc.gtt_location;
	rdev->mc.gtt_end = rdev->mc.gtt_location + rdev->mc.gtt_size - 1;
	/* FIXME: we should enforce default clock in case GPU is not in
	 * default setup
	 */
	a.full = rfixed_const(100);
	rdev->pm.sclk.full = rfixed_const(rdev->clock.default_sclk);
	rdev->pm.sclk.full = rfixed_div(rdev->pm.sclk, a);
	return 0;
}

/* We doesn't check that the GPU really needs a reset we simply do the
 * reset, it's up to the caller to determine if the GPU needs one. We
 * might add an helper function to check that.
 */
int r600_gpu_soft_reset(struct radeon_device *rdev)
{
	struct rv515_mc_save save;
	u32 grbm_busy_mask = S_008010_VC_BUSY(1) | S_008010_VGT_BUSY_NO_DMA(1) |
				S_008010_VGT_BUSY(1) | S_008010_TA03_BUSY(1) |
				S_008010_TC_BUSY(1) | S_008010_SX_BUSY(1) |
				S_008010_SH_BUSY(1) | S_008010_SPI03_BUSY(1) |
				S_008010_SMX_BUSY(1) | S_008010_SC_BUSY(1) |
				S_008010_PA_BUSY(1) | S_008010_DB03_BUSY(1) |
				S_008010_CR_BUSY(1) | S_008010_CB03_BUSY(1) |
				S_008010_GUI_ACTIVE(1);
	u32 grbm2_busy_mask = S_008014_SPI0_BUSY(1) | S_008014_SPI1_BUSY(1) |
			S_008014_SPI2_BUSY(1) | S_008014_SPI3_BUSY(1) |
			S_008014_TA0_BUSY(1) | S_008014_TA1_BUSY(1) |
			S_008014_TA2_BUSY(1) | S_008014_TA3_BUSY(1) |
			S_008014_DB0_BUSY(1) | S_008014_DB1_BUSY(1) |
			S_008014_DB2_BUSY(1) | S_008014_DB3_BUSY(1) |
			S_008014_CB0_BUSY(1) | S_008014_CB1_BUSY(1) |
			S_008014_CB2_BUSY(1) | S_008014_CB3_BUSY(1);
	u32 srbm_reset = 0;
	u32 tmp;

	dev_info(rdev->dev, "GPU softreset \n");
	dev_info(rdev->dev, "  R_008010_GRBM_STATUS=0x%08X\n",
		RREG32(R_008010_GRBM_STATUS));
	dev_info(rdev->dev, "  R_008014_GRBM_STATUS2=0x%08X\n",
		RREG32(R_008014_GRBM_STATUS2));
	dev_info(rdev->dev, "  R_000E50_SRBM_STATUS=0x%08X\n",
		RREG32(R_000E50_SRBM_STATUS));
	rv515_mc_stop(rdev, &save);
	if (r600_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}
	/* Disable CP parsing/prefetching */
	WREG32(R_0086D8_CP_ME_CNTL, S_0086D8_CP_ME_HALT(0xff));
	/* Check if any of the rendering block is busy and reset it */
	if ((RREG32(R_008010_GRBM_STATUS) & grbm_busy_mask) ||
	    (RREG32(R_008014_GRBM_STATUS2) & grbm2_busy_mask)) {
		tmp = S_008020_SOFT_RESET_CR(1) |
			S_008020_SOFT_RESET_DB(1) |
			S_008020_SOFT_RESET_CB(1) |
			S_008020_SOFT_RESET_PA(1) |
			S_008020_SOFT_RESET_SC(1) |
			S_008020_SOFT_RESET_SMX(1) |
			S_008020_SOFT_RESET_SPI(1) |
			S_008020_SOFT_RESET_SX(1) |
			S_008020_SOFT_RESET_SH(1) |
			S_008020_SOFT_RESET_TC(1) |
			S_008020_SOFT_RESET_TA(1) |
			S_008020_SOFT_RESET_VC(1) |
			S_008020_SOFT_RESET_VGT(1);
		dev_info(rdev->dev, "  R_008020_GRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(R_008020_GRBM_SOFT_RESET, tmp);
		(void)RREG32(R_008020_GRBM_SOFT_RESET);
		udelay(50);
		WREG32(R_008020_GRBM_SOFT_RESET, 0);
		(void)RREG32(R_008020_GRBM_SOFT_RESET);
	}
	/* Reset CP (we always reset CP) */
	tmp = S_008020_SOFT_RESET_CP(1);
	dev_info(rdev->dev, "R_008020_GRBM_SOFT_RESET=0x%08X\n", tmp);
	WREG32(R_008020_GRBM_SOFT_RESET, tmp);
	(void)RREG32(R_008020_GRBM_SOFT_RESET);
	udelay(50);
	WREG32(R_008020_GRBM_SOFT_RESET, 0);
	(void)RREG32(R_008020_GRBM_SOFT_RESET);
	/* Reset others GPU block if necessary */
	if (G_000E50_RLC_BUSY(RREG32(R_000E50_SRBM_STATUS)))
		srbm_reset |= S_000E60_SOFT_RESET_RLC(1);
	if (G_000E50_GRBM_RQ_PENDING(RREG32(R_000E50_SRBM_STATUS)))
		srbm_reset |= S_000E60_SOFT_RESET_GRBM(1);
	if (G_000E50_HI_RQ_PENDING(RREG32(R_000E50_SRBM_STATUS)))
		srbm_reset |= S_000E60_SOFT_RESET_IH(1);
	if (G_000E50_VMC_BUSY(RREG32(R_000E50_SRBM_STATUS)))
		srbm_reset |= S_000E60_SOFT_RESET_VMC(1);
	if (G_000E50_MCB_BUSY(RREG32(R_000E50_SRBM_STATUS)))
		srbm_reset |= S_000E60_SOFT_RESET_MC(1);
	if (G_000E50_MCDZ_BUSY(RREG32(R_000E50_SRBM_STATUS)))
		srbm_reset |= S_000E60_SOFT_RESET_MC(1);
	if (G_000E50_MCDY_BUSY(RREG32(R_000E50_SRBM_STATUS)))
		srbm_reset |= S_000E60_SOFT_RESET_MC(1);
	if (G_000E50_MCDX_BUSY(RREG32(R_000E50_SRBM_STATUS)))
		srbm_reset |= S_000E60_SOFT_RESET_MC(1);
	if (G_000E50_MCDW_BUSY(RREG32(R_000E50_SRBM_STATUS)))
		srbm_reset |= S_000E60_SOFT_RESET_MC(1);
	if (G_000E50_RLC_BUSY(RREG32(R_000E50_SRBM_STATUS)))
		srbm_reset |= S_000E60_SOFT_RESET_RLC(1);
	if (G_000E50_SEM_BUSY(RREG32(R_000E50_SRBM_STATUS)))
		srbm_reset |= S_000E60_SOFT_RESET_SEM(1);
	if (G_000E50_BIF_BUSY(RREG32(R_000E50_SRBM_STATUS)))
		srbm_reset |= S_000E60_SOFT_RESET_BIF(1);
	dev_info(rdev->dev, "  R_000E60_SRBM_SOFT_RESET=0x%08X\n", srbm_reset);
	WREG32(R_000E60_SRBM_SOFT_RESET, srbm_reset);
	(void)RREG32(R_000E60_SRBM_SOFT_RESET);
	udelay(50);
	WREG32(R_000E60_SRBM_SOFT_RESET, 0);
	(void)RREG32(R_000E60_SRBM_SOFT_RESET);
	WREG32(R_000E60_SRBM_SOFT_RESET, srbm_reset);
	(void)RREG32(R_000E60_SRBM_SOFT_RESET);
	udelay(50);
	WREG32(R_000E60_SRBM_SOFT_RESET, 0);
	(void)RREG32(R_000E60_SRBM_SOFT_RESET);
	/* Wait a little for things to settle down */
	udelay(50);
	dev_info(rdev->dev, "  R_008010_GRBM_STATUS=0x%08X\n",
		RREG32(R_008010_GRBM_STATUS));
	dev_info(rdev->dev, "  R_008014_GRBM_STATUS2=0x%08X\n",
		RREG32(R_008014_GRBM_STATUS2));
	dev_info(rdev->dev, "  R_000E50_SRBM_STATUS=0x%08X\n",
		RREG32(R_000E50_SRBM_STATUS));
	/* After reset we need to reinit the asic as GPU often endup in an
	 * incoherent state.
	 */
	atom_asic_init(rdev->mode_info.atom_context);
	rv515_mc_resume(rdev, &save);
	return 0;
}

int r600_gpu_reset(struct radeon_device *rdev)
{
	return r600_gpu_soft_reset(rdev);
}

static u32 r600_get_tile_pipe_to_backend_map(u32 num_tile_pipes,
					     u32 num_backends,
					     u32 backend_disable_mask)
{
	u32 backend_map = 0;
	u32 enabled_backends_mask;
	u32 enabled_backends_count;
	u32 cur_pipe;
	u32 swizzle_pipe[R6XX_MAX_PIPES];
	u32 cur_backend;
	u32 i;

	if (num_tile_pipes > R6XX_MAX_PIPES)
		num_tile_pipes = R6XX_MAX_PIPES;
	if (num_tile_pipes < 1)
		num_tile_pipes = 1;
	if (num_backends > R6XX_MAX_BACKENDS)
		num_backends = R6XX_MAX_BACKENDS;
	if (num_backends < 1)
		num_backends = 1;

	enabled_backends_mask = 0;
	enabled_backends_count = 0;
	for (i = 0; i < R6XX_MAX_BACKENDS; ++i) {
		if (((backend_disable_mask >> i) & 1) == 0) {
			enabled_backends_mask |= (1 << i);
			++enabled_backends_count;
		}
		if (enabled_backends_count == num_backends)
			break;
	}

	if (enabled_backends_count == 0) {
		enabled_backends_mask = 1;
		enabled_backends_count = 1;
	}

	if (enabled_backends_count != num_backends)
		num_backends = enabled_backends_count;

	memset((uint8_t *)&swizzle_pipe[0], 0, sizeof(u32) * R6XX_MAX_PIPES);
	switch (num_tile_pipes) {
	case 1:
		swizzle_pipe[0] = 0;
		break;
	case 2:
		swizzle_pipe[0] = 0;
		swizzle_pipe[1] = 1;
		break;
	case 3:
		swizzle_pipe[0] = 0;
		swizzle_pipe[1] = 1;
		swizzle_pipe[2] = 2;
		break;
	case 4:
		swizzle_pipe[0] = 0;
		swizzle_pipe[1] = 1;
		swizzle_pipe[2] = 2;
		swizzle_pipe[3] = 3;
		break;
	case 5:
		swizzle_pipe[0] = 0;
		swizzle_pipe[1] = 1;
		swizzle_pipe[2] = 2;
		swizzle_pipe[3] = 3;
		swizzle_pipe[4] = 4;
		break;
	case 6:
		swizzle_pipe[0] = 0;
		swizzle_pipe[1] = 2;
		swizzle_pipe[2] = 4;
		swizzle_pipe[3] = 5;
		swizzle_pipe[4] = 1;
		swizzle_pipe[5] = 3;
		break;
	case 7:
		swizzle_pipe[0] = 0;
		swizzle_pipe[1] = 2;
		swizzle_pipe[2] = 4;
		swizzle_pipe[3] = 6;
		swizzle_pipe[4] = 1;
		swizzle_pipe[5] = 3;
		swizzle_pipe[6] = 5;
		break;
	case 8:
		swizzle_pipe[0] = 0;
		swizzle_pipe[1] = 2;
		swizzle_pipe[2] = 4;
		swizzle_pipe[3] = 6;
		swizzle_pipe[4] = 1;
		swizzle_pipe[5] = 3;
		swizzle_pipe[6] = 5;
		swizzle_pipe[7] = 7;
		break;
	}

	cur_backend = 0;
	for (cur_pipe = 0; cur_pipe < num_tile_pipes; ++cur_pipe) {
		while (((1 << cur_backend) & enabled_backends_mask) == 0)
			cur_backend = (cur_backend + 1) % R6XX_MAX_BACKENDS;

		backend_map |= (u32)(((cur_backend & 3) << (swizzle_pipe[cur_pipe] * 2)));

		cur_backend = (cur_backend + 1) % R6XX_MAX_BACKENDS;
	}

	return backend_map;
}

int r600_count_pipe_bits(uint32_t val)
{
	int i, ret = 0;

	for (i = 0; i < 32; i++) {
		ret += val & 1;
		val >>= 1;
	}
	return ret;
}

void r600_gpu_init(struct radeon_device *rdev)
{
	u32 tiling_config;
	u32 ramcfg;
	u32 tmp;
	int i, j;
	u32 sq_config;
	u32 sq_gpr_resource_mgmt_1 = 0;
	u32 sq_gpr_resource_mgmt_2 = 0;
	u32 sq_thread_resource_mgmt = 0;
	u32 sq_stack_resource_mgmt_1 = 0;
	u32 sq_stack_resource_mgmt_2 = 0;

	/* FIXME: implement */
	switch (rdev->family) {
	case CHIP_R600:
		rdev->config.r600.max_pipes = 4;
		rdev->config.r600.max_tile_pipes = 8;
		rdev->config.r600.max_simds = 4;
		rdev->config.r600.max_backends = 4;
		rdev->config.r600.max_gprs = 256;
		rdev->config.r600.max_threads = 192;
		rdev->config.r600.max_stack_entries = 256;
		rdev->config.r600.max_hw_contexts = 8;
		rdev->config.r600.max_gs_threads = 16;
		rdev->config.r600.sx_max_export_size = 128;
		rdev->config.r600.sx_max_export_pos_size = 16;
		rdev->config.r600.sx_max_export_smx_size = 128;
		rdev->config.r600.sq_num_cf_insts = 2;
		break;
	case CHIP_RV630:
	case CHIP_RV635:
		rdev->config.r600.max_pipes = 2;
		rdev->config.r600.max_tile_pipes = 2;
		rdev->config.r600.max_simds = 3;
		rdev->config.r600.max_backends = 1;
		rdev->config.r600.max_gprs = 128;
		rdev->config.r600.max_threads = 192;
		rdev->config.r600.max_stack_entries = 128;
		rdev->config.r600.max_hw_contexts = 8;
		rdev->config.r600.max_gs_threads = 4;
		rdev->config.r600.sx_max_export_size = 128;
		rdev->config.r600.sx_max_export_pos_size = 16;
		rdev->config.r600.sx_max_export_smx_size = 128;
		rdev->config.r600.sq_num_cf_insts = 2;
		break;
	case CHIP_RV610:
	case CHIP_RV620:
	case CHIP_RS780:
	case CHIP_RS880:
		rdev->config.r600.max_pipes = 1;
		rdev->config.r600.max_tile_pipes = 1;
		rdev->config.r600.max_simds = 2;
		rdev->config.r600.max_backends = 1;
		rdev->config.r600.max_gprs = 128;
		rdev->config.r600.max_threads = 192;
		rdev->config.r600.max_stack_entries = 128;
		rdev->config.r600.max_hw_contexts = 4;
		rdev->config.r600.max_gs_threads = 4;
		rdev->config.r600.sx_max_export_size = 128;
		rdev->config.r600.sx_max_export_pos_size = 16;
		rdev->config.r600.sx_max_export_smx_size = 128;
		rdev->config.r600.sq_num_cf_insts = 1;
		break;
	case CHIP_RV670:
		rdev->config.r600.max_pipes = 4;
		rdev->config.r600.max_tile_pipes = 4;
		rdev->config.r600.max_simds = 4;
		rdev->config.r600.max_backends = 4;
		rdev->config.r600.max_gprs = 192;
		rdev->config.r600.max_threads = 192;
		rdev->config.r600.max_stack_entries = 256;
		rdev->config.r600.max_hw_contexts = 8;
		rdev->config.r600.max_gs_threads = 16;
		rdev->config.r600.sx_max_export_size = 128;
		rdev->config.r600.sx_max_export_pos_size = 16;
		rdev->config.r600.sx_max_export_smx_size = 128;
		rdev->config.r600.sq_num_cf_insts = 2;
		break;
	default:
		break;
	}

	/* Initialize HDP */
	for (i = 0, j = 0; i < 32; i++, j += 0x18) {
		WREG32((0x2c14 + j), 0x00000000);
		WREG32((0x2c18 + j), 0x00000000);
		WREG32((0x2c1c + j), 0x00000000);
		WREG32((0x2c20 + j), 0x00000000);
		WREG32((0x2c24 + j), 0x00000000);
	}

	WREG32(GRBM_CNTL, GRBM_READ_TIMEOUT(0xff));

	/* Setup tiling */
	tiling_config = 0;
	ramcfg = RREG32(RAMCFG);
	switch (rdev->config.r600.max_tile_pipes) {
	case 1:
		tiling_config |= PIPE_TILING(0);
		break;
	case 2:
		tiling_config |= PIPE_TILING(1);
		break;
	case 4:
		tiling_config |= PIPE_TILING(2);
		break;
	case 8:
		tiling_config |= PIPE_TILING(3);
		break;
	default:
		break;
	}
	tiling_config |= BANK_TILING((ramcfg & NOOFBANK_MASK) >> NOOFBANK_SHIFT);
	tiling_config |= GROUP_SIZE(0);
	tmp = (ramcfg & NOOFROWS_MASK) >> NOOFROWS_SHIFT;
	if (tmp > 3) {
		tiling_config |= ROW_TILING(3);
		tiling_config |= SAMPLE_SPLIT(3);
	} else {
		tiling_config |= ROW_TILING(tmp);
		tiling_config |= SAMPLE_SPLIT(tmp);
	}
	tiling_config |= BANK_SWAPS(1);
	tmp = r600_get_tile_pipe_to_backend_map(rdev->config.r600.max_tile_pipes,
						rdev->config.r600.max_backends,
						(0xff << rdev->config.r600.max_backends) & 0xff);
	tiling_config |= BACKEND_MAP(tmp);
	WREG32(GB_TILING_CONFIG, tiling_config);
	WREG32(DCP_TILING_CONFIG, tiling_config & 0xffff);
	WREG32(HDP_TILING_CONFIG, tiling_config & 0xffff);

	tmp = BACKEND_DISABLE((R6XX_MAX_BACKENDS_MASK << rdev->config.r600.max_backends) & R6XX_MAX_BACKENDS_MASK);
	WREG32(CC_RB_BACKEND_DISABLE, tmp);

	/* Setup pipes */
	tmp = INACTIVE_QD_PIPES((R6XX_MAX_PIPES_MASK << rdev->config.r600.max_pipes) & R6XX_MAX_PIPES_MASK);
	tmp |= INACTIVE_SIMDS((R6XX_MAX_SIMDS_MASK << rdev->config.r600.max_simds) & R6XX_MAX_SIMDS_MASK);
	WREG32(CC_GC_SHADER_PIPE_CONFIG, tmp);
	WREG32(GC_USER_SHADER_PIPE_CONFIG, tmp);

	tmp = R6XX_MAX_BACKENDS - r600_count_pipe_bits(tmp & INACTIVE_QD_PIPES_MASK);
	WREG32(VGT_OUT_DEALLOC_CNTL, (tmp * 4) & DEALLOC_DIST_MASK);
	WREG32(VGT_VERTEX_REUSE_BLOCK_CNTL, ((tmp * 4) - 2) & VTX_REUSE_DEPTH_MASK);

	/* Setup some CP states */
	WREG32(CP_QUEUE_THRESHOLDS, (ROQ_IB1_START(0x16) | ROQ_IB2_START(0x2b)));
	WREG32(CP_MEQ_THRESHOLDS, (MEQ_END(0x40) | ROQ_END(0x40)));

	WREG32(TA_CNTL_AUX, (DISABLE_CUBE_ANISO | SYNC_GRADIENT |
			     SYNC_WALKER | SYNC_ALIGNER));
	/* Setup various GPU states */
	if (rdev->family == CHIP_RV670)
		WREG32(ARB_GDEC_RD_CNTL, 0x00000021);

	tmp = RREG32(SX_DEBUG_1);
	tmp |= SMX_EVENT_RELEASE;
	if ((rdev->family > CHIP_R600))
		tmp |= ENABLE_NEW_SMX_ADDRESS;
	WREG32(SX_DEBUG_1, tmp);

	if (((rdev->family) == CHIP_R600) ||
	    ((rdev->family) == CHIP_RV630) ||
	    ((rdev->family) == CHIP_RV610) ||
	    ((rdev->family) == CHIP_RV620) ||
	    ((rdev->family) == CHIP_RS780) ||
	    ((rdev->family) == CHIP_RS880)) {
		WREG32(DB_DEBUG, PREZ_MUST_WAIT_FOR_POSTZ_DONE);
	} else {
		WREG32(DB_DEBUG, 0);
	}
	WREG32(DB_WATERMARKS, (DEPTH_FREE(4) | DEPTH_CACHELINE_FREE(16) |
			       DEPTH_FLUSH(16) | DEPTH_PENDING_FREE(4)));

	WREG32(PA_SC_MULTI_CHIP_CNTL, 0);
	WREG32(VGT_NUM_INSTANCES, 0);

	WREG32(SPI_CONFIG_CNTL, GPR_WRITE_PRIORITY(0));
	WREG32(SPI_CONFIG_CNTL_1, VTX_DONE_DELAY(0));

	tmp = RREG32(SQ_MS_FIFO_SIZES);
	if (((rdev->family) == CHIP_RV610) ||
	    ((rdev->family) == CHIP_RV620) ||
	    ((rdev->family) == CHIP_RS780) ||
	    ((rdev->family) == CHIP_RS880)) {
		tmp = (CACHE_FIFO_SIZE(0xa) |
		       FETCH_FIFO_HIWATER(0xa) |
		       DONE_FIFO_HIWATER(0xe0) |
		       ALU_UPDATE_FIFO_HIWATER(0x8));
	} else if (((rdev->family) == CHIP_R600) ||
		   ((rdev->family) == CHIP_RV630)) {
		tmp &= ~DONE_FIFO_HIWATER(0xff);
		tmp |= DONE_FIFO_HIWATER(0x4);
	}
	WREG32(SQ_MS_FIFO_SIZES, tmp);

	/* SQ_CONFIG, SQ_GPR_RESOURCE_MGMT, SQ_THREAD_RESOURCE_MGMT, SQ_STACK_RESOURCE_MGMT
	 * should be adjusted as needed by the 2D/3D drivers.  This just sets default values
	 */
	sq_config = RREG32(SQ_CONFIG);
	sq_config &= ~(PS_PRIO(3) |
		       VS_PRIO(3) |
		       GS_PRIO(3) |
		       ES_PRIO(3));
	sq_config |= (DX9_CONSTS |
		      VC_ENABLE |
		      PS_PRIO(0) |
		      VS_PRIO(1) |
		      GS_PRIO(2) |
		      ES_PRIO(3));

	if ((rdev->family) == CHIP_R600) {
		sq_gpr_resource_mgmt_1 = (NUM_PS_GPRS(124) |
					  NUM_VS_GPRS(124) |
					  NUM_CLAUSE_TEMP_GPRS(4));
		sq_gpr_resource_mgmt_2 = (NUM_GS_GPRS(0) |
					  NUM_ES_GPRS(0));
		sq_thread_resource_mgmt = (NUM_PS_THREADS(136) |
					   NUM_VS_THREADS(48) |
					   NUM_GS_THREADS(4) |
					   NUM_ES_THREADS(4));
		sq_stack_resource_mgmt_1 = (NUM_PS_STACK_ENTRIES(128) |
					    NUM_VS_STACK_ENTRIES(128));
		sq_stack_resource_mgmt_2 = (NUM_GS_STACK_ENTRIES(0) |
					    NUM_ES_STACK_ENTRIES(0));
	} else if (((rdev->family) == CHIP_RV610) ||
		   ((rdev->family) == CHIP_RV620) ||
		   ((rdev->family) == CHIP_RS780) ||
		   ((rdev->family) == CHIP_RS880)) {
		/* no vertex cache */
		sq_config &= ~VC_ENABLE;

		sq_gpr_resource_mgmt_1 = (NUM_PS_GPRS(44) |
					  NUM_VS_GPRS(44) |
					  NUM_CLAUSE_TEMP_GPRS(2));
		sq_gpr_resource_mgmt_2 = (NUM_GS_GPRS(17) |
					  NUM_ES_GPRS(17));
		sq_thread_resource_mgmt = (NUM_PS_THREADS(79) |
					   NUM_VS_THREADS(78) |
					   NUM_GS_THREADS(4) |
					   NUM_ES_THREADS(31));
		sq_stack_resource_mgmt_1 = (NUM_PS_STACK_ENTRIES(40) |
					    NUM_VS_STACK_ENTRIES(40));
		sq_stack_resource_mgmt_2 = (NUM_GS_STACK_ENTRIES(32) |
					    NUM_ES_STACK_ENTRIES(16));
	} else if (((rdev->family) == CHIP_RV630) ||
		   ((rdev->family) == CHIP_RV635)) {
		sq_gpr_resource_mgmt_1 = (NUM_PS_GPRS(44) |
					  NUM_VS_GPRS(44) |
					  NUM_CLAUSE_TEMP_GPRS(2));
		sq_gpr_resource_mgmt_2 = (NUM_GS_GPRS(18) |
					  NUM_ES_GPRS(18));
		sq_thread_resource_mgmt = (NUM_PS_THREADS(79) |
					   NUM_VS_THREADS(78) |
					   NUM_GS_THREADS(4) |
					   NUM_ES_THREADS(31));
		sq_stack_resource_mgmt_1 = (NUM_PS_STACK_ENTRIES(40) |
					    NUM_VS_STACK_ENTRIES(40));
		sq_stack_resource_mgmt_2 = (NUM_GS_STACK_ENTRIES(32) |
					    NUM_ES_STACK_ENTRIES(16));
	} else if ((rdev->family) == CHIP_RV670) {
		sq_gpr_resource_mgmt_1 = (NUM_PS_GPRS(44) |
					  NUM_VS_GPRS(44) |
					  NUM_CLAUSE_TEMP_GPRS(2));
		sq_gpr_resource_mgmt_2 = (NUM_GS_GPRS(17) |
					  NUM_ES_GPRS(17));
		sq_thread_resource_mgmt = (NUM_PS_THREADS(79) |
					   NUM_VS_THREADS(78) |
					   NUM_GS_THREADS(4) |
					   NUM_ES_THREADS(31));
		sq_stack_resource_mgmt_1 = (NUM_PS_STACK_ENTRIES(64) |
					    NUM_VS_STACK_ENTRIES(64));
		sq_stack_resource_mgmt_2 = (NUM_GS_STACK_ENTRIES(64) |
					    NUM_ES_STACK_ENTRIES(64));
	}

	WREG32(SQ_CONFIG, sq_config);
	WREG32(SQ_GPR_RESOURCE_MGMT_1,  sq_gpr_resource_mgmt_1);
	WREG32(SQ_GPR_RESOURCE_MGMT_2,  sq_gpr_resource_mgmt_2);
	WREG32(SQ_THREAD_RESOURCE_MGMT, sq_thread_resource_mgmt);
	WREG32(SQ_STACK_RESOURCE_MGMT_1, sq_stack_resource_mgmt_1);
	WREG32(SQ_STACK_RESOURCE_MGMT_2, sq_stack_resource_mgmt_2);

	if (((rdev->family) == CHIP_RV610) ||
	    ((rdev->family) == CHIP_RV620) ||
	    ((rdev->family) == CHIP_RS780) ||
	    ((rdev->family) == CHIP_RS880)) {
		WREG32(VGT_CACHE_INVALIDATION, CACHE_INVALIDATION(TC_ONLY));
	} else {
		WREG32(VGT_CACHE_INVALIDATION, CACHE_INVALIDATION(VC_AND_TC));
	}

	/* More default values. 2D/3D driver should adjust as needed */
	WREG32(PA_SC_AA_SAMPLE_LOCS_2S, (S0_X(0xc) | S0_Y(0x4) |
					 S1_X(0x4) | S1_Y(0xc)));
	WREG32(PA_SC_AA_SAMPLE_LOCS_4S, (S0_X(0xe) | S0_Y(0xe) |
					 S1_X(0x2) | S1_Y(0x2) |
					 S2_X(0xa) | S2_Y(0x6) |
					 S3_X(0x6) | S3_Y(0xa)));
	WREG32(PA_SC_AA_SAMPLE_LOCS_8S_WD0, (S0_X(0xe) | S0_Y(0xb) |
					     S1_X(0x4) | S1_Y(0xc) |
					     S2_X(0x1) | S2_Y(0x6) |
					     S3_X(0xa) | S3_Y(0xe)));
	WREG32(PA_SC_AA_SAMPLE_LOCS_8S_WD1, (S4_X(0x6) | S4_Y(0x1) |
					     S5_X(0x0) | S5_Y(0x0) |
					     S6_X(0xb) | S6_Y(0x4) |
					     S7_X(0x7) | S7_Y(0x8)));

	WREG32(VGT_STRMOUT_EN, 0);
	tmp = rdev->config.r600.max_pipes * 16;
	switch (rdev->family) {
	case CHIP_RV610:
	case CHIP_RV620:
	case CHIP_RS780:
	case CHIP_RS880:
		tmp += 32;
		break;
	case CHIP_RV670:
		tmp += 128;
		break;
	default:
		break;
	}
	if (tmp > 256) {
		tmp = 256;
	}
	WREG32(VGT_ES_PER_GS, 128);
	WREG32(VGT_GS_PER_ES, tmp);
	WREG32(VGT_GS_PER_VS, 2);
	WREG32(VGT_GS_VERTEX_REUSE, 16);

	/* more default values. 2D/3D driver should adjust as needed */
	WREG32(PA_SC_LINE_STIPPLE_STATE, 0);
	WREG32(VGT_STRMOUT_EN, 0);
	WREG32(SX_MISC, 0);
	WREG32(PA_SC_MODE_CNTL, 0);
	WREG32(PA_SC_AA_CONFIG, 0);
	WREG32(PA_SC_LINE_STIPPLE, 0);
	WREG32(SPI_INPUT_Z, 0);
	WREG32(SPI_PS_IN_CONTROL_0, NUM_INTERP(2));
	WREG32(CB_COLOR7_FRAG, 0);

	/* Clear render buffer base addresses */
	WREG32(CB_COLOR0_BASE, 0);
	WREG32(CB_COLOR1_BASE, 0);
	WREG32(CB_COLOR2_BASE, 0);
	WREG32(CB_COLOR3_BASE, 0);
	WREG32(CB_COLOR4_BASE, 0);
	WREG32(CB_COLOR5_BASE, 0);
	WREG32(CB_COLOR6_BASE, 0);
	WREG32(CB_COLOR7_BASE, 0);
	WREG32(CB_COLOR7_FRAG, 0);

	switch (rdev->family) {
	case CHIP_RV610:
	case CHIP_RV620:
	case CHIP_RS780:
	case CHIP_RS880:
		tmp = TC_L2_SIZE(8);
		break;
	case CHIP_RV630:
	case CHIP_RV635:
		tmp = TC_L2_SIZE(4);
		break;
	case CHIP_R600:
		tmp = TC_L2_SIZE(0) | L2_DISABLE_LATE_HIT;
		break;
	default:
		tmp = TC_L2_SIZE(0);
		break;
	}
	WREG32(TC_CNTL, tmp);

	tmp = RREG32(HDP_HOST_PATH_CNTL);
	WREG32(HDP_HOST_PATH_CNTL, tmp);

	tmp = RREG32(ARB_POP);
	tmp |= ENABLE_TC128;
	WREG32(ARB_POP, tmp);

	WREG32(PA_SC_MULTI_CHIP_CNTL, 0);
	WREG32(PA_CL_ENHANCE, (CLIP_VTX_REORDER_ENA |
			       NUM_CLIP_SEQ(3)));
	WREG32(PA_SC_ENHANCE, FORCE_EOV_MAX_CLK_CNT(4095));
}


/*
 * Indirect registers accessor
 */
u32 r600_pciep_rreg(struct radeon_device *rdev, u32 reg)
{
	u32 r;

	WREG32(PCIE_PORT_INDEX, ((reg) & 0xff));
	(void)RREG32(PCIE_PORT_INDEX);
	r = RREG32(PCIE_PORT_DATA);
	return r;
}

void r600_pciep_wreg(struct radeon_device *rdev, u32 reg, u32 v)
{
	WREG32(PCIE_PORT_INDEX, ((reg) & 0xff));
	(void)RREG32(PCIE_PORT_INDEX);
	WREG32(PCIE_PORT_DATA, (v));
	(void)RREG32(PCIE_PORT_DATA);
}


/*
 * CP & Ring
 */
void r600_cp_stop(struct radeon_device *rdev)
{
	WREG32(R_0086D8_CP_ME_CNTL, S_0086D8_CP_ME_HALT(1));
}

int r600_cp_init_microcode(struct radeon_device *rdev)
{
	struct platform_device *pdev;
	const char *chip_name;
	size_t pfp_req_size, me_req_size;
	char fw_name[30];
	int err;

	DRM_DEBUG("\n");

	pdev = platform_device_register_simple("radeon_cp", 0, NULL, 0);
	err = IS_ERR(pdev);
	if (err) {
		printk(KERN_ERR "radeon_cp: Failed to register firmware\n");
		return -EINVAL;
	}

	switch (rdev->family) {
	case CHIP_R600: chip_name = "R600"; break;
	case CHIP_RV610: chip_name = "RV610"; break;
	case CHIP_RV630: chip_name = "RV630"; break;
	case CHIP_RV620: chip_name = "RV620"; break;
	case CHIP_RV635: chip_name = "RV635"; break;
	case CHIP_RV670: chip_name = "RV670"; break;
	case CHIP_RS780:
	case CHIP_RS880: chip_name = "RS780"; break;
	case CHIP_RV770: chip_name = "RV770"; break;
	case CHIP_RV730:
	case CHIP_RV740: chip_name = "RV730"; break;
	case CHIP_RV710: chip_name = "RV710"; break;
	default: BUG();
	}

	if (rdev->family >= CHIP_RV770) {
		pfp_req_size = R700_PFP_UCODE_SIZE * 4;
		me_req_size = R700_PM4_UCODE_SIZE * 4;
	} else {
		pfp_req_size = PFP_UCODE_SIZE * 4;
		me_req_size = PM4_UCODE_SIZE * 12;
	}

	DRM_INFO("Loading %s CP Microcode\n", chip_name);

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_pfp.bin", chip_name);
	err = request_firmware(&rdev->pfp_fw, fw_name, &pdev->dev);
	if (err)
		goto out;
	if (rdev->pfp_fw->size != pfp_req_size) {
		printk(KERN_ERR
		       "r600_cp: Bogus length %zu in firmware \"%s\"\n",
		       rdev->pfp_fw->size, fw_name);
		err = -EINVAL;
		goto out;
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_me.bin", chip_name);
	err = request_firmware(&rdev->me_fw, fw_name, &pdev->dev);
	if (err)
		goto out;
	if (rdev->me_fw->size != me_req_size) {
		printk(KERN_ERR
		       "r600_cp: Bogus length %zu in firmware \"%s\"\n",
		       rdev->me_fw->size, fw_name);
		err = -EINVAL;
	}
out:
	platform_device_unregister(pdev);

	if (err) {
		if (err != -EINVAL)
			printk(KERN_ERR
			       "r600_cp: Failed to load firmware \"%s\"\n",
			       fw_name);
		release_firmware(rdev->pfp_fw);
		rdev->pfp_fw = NULL;
		release_firmware(rdev->me_fw);
		rdev->me_fw = NULL;
	}
	return err;
}

static int r600_cp_load_microcode(struct radeon_device *rdev)
{
	const __be32 *fw_data;
	int i;

	if (!rdev->me_fw || !rdev->pfp_fw)
		return -EINVAL;

	r600_cp_stop(rdev);

	WREG32(CP_RB_CNTL, RB_NO_UPDATE | RB_BLKSZ(15) | RB_BUFSZ(3));

	/* Reset cp */
	WREG32(GRBM_SOFT_RESET, SOFT_RESET_CP);
	RREG32(GRBM_SOFT_RESET);
	mdelay(15);
	WREG32(GRBM_SOFT_RESET, 0);

	WREG32(CP_ME_RAM_WADDR, 0);

	fw_data = (const __be32 *)rdev->me_fw->data;
	WREG32(CP_ME_RAM_WADDR, 0);
	for (i = 0; i < PM4_UCODE_SIZE * 3; i++)
		WREG32(CP_ME_RAM_DATA,
		       be32_to_cpup(fw_data++));

	fw_data = (const __be32 *)rdev->pfp_fw->data;
	WREG32(CP_PFP_UCODE_ADDR, 0);
	for (i = 0; i < PFP_UCODE_SIZE; i++)
		WREG32(CP_PFP_UCODE_DATA,
		       be32_to_cpup(fw_data++));

	WREG32(CP_PFP_UCODE_ADDR, 0);
	WREG32(CP_ME_RAM_WADDR, 0);
	WREG32(CP_ME_RAM_RADDR, 0);
	return 0;
}

int r600_cp_start(struct radeon_device *rdev)
{
	int r;
	uint32_t cp_me;

	r = radeon_ring_lock(rdev, 7);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring (%d).\n", r);
		return r;
	}
	radeon_ring_write(rdev, PACKET3(PACKET3_ME_INITIALIZE, 5));
	radeon_ring_write(rdev, 0x1);
	if (rdev->family < CHIP_RV770) {
		radeon_ring_write(rdev, 0x3);
		radeon_ring_write(rdev, rdev->config.r600.max_hw_contexts - 1);
	} else {
		radeon_ring_write(rdev, 0x0);
		radeon_ring_write(rdev, rdev->config.rv770.max_hw_contexts - 1);
	}
	radeon_ring_write(rdev, PACKET3_ME_INITIALIZE_DEVICE_ID(1));
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, 0);
	radeon_ring_unlock_commit(rdev);

	cp_me = 0xff;
	WREG32(R_0086D8_CP_ME_CNTL, cp_me);
	return 0;
}

int r600_cp_resume(struct radeon_device *rdev)
{
	u32 tmp;
	u32 rb_bufsz;
	int r;

	/* Reset cp */
	WREG32(GRBM_SOFT_RESET, SOFT_RESET_CP);
	RREG32(GRBM_SOFT_RESET);
	mdelay(15);
	WREG32(GRBM_SOFT_RESET, 0);

	/* Set ring buffer size */
	rb_bufsz = drm_order(rdev->cp.ring_size / 8);
	tmp = RB_NO_UPDATE | (drm_order(RADEON_GPU_PAGE_SIZE/8) << 8) | rb_bufsz;
#ifdef __BIG_ENDIAN
	tmp |= BUF_SWAP_32BIT;
#endif
	WREG32(CP_RB_CNTL, tmp);
	WREG32(CP_SEM_WAIT_TIMER, 0x4);

	/* Set the write pointer delay */
	WREG32(CP_RB_WPTR_DELAY, 0);

	/* Initialize the ring buffer's read and write pointers */
	WREG32(CP_RB_CNTL, tmp | RB_RPTR_WR_ENA);
	WREG32(CP_RB_RPTR_WR, 0);
	WREG32(CP_RB_WPTR, 0);
	WREG32(CP_RB_RPTR_ADDR, rdev->cp.gpu_addr & 0xFFFFFFFF);
	WREG32(CP_RB_RPTR_ADDR_HI, upper_32_bits(rdev->cp.gpu_addr));
	mdelay(1);
	WREG32(CP_RB_CNTL, tmp);

	WREG32(CP_RB_BASE, rdev->cp.gpu_addr >> 8);
	WREG32(CP_DEBUG, (1 << 27) | (1 << 28));

	rdev->cp.rptr = RREG32(CP_RB_RPTR);
	rdev->cp.wptr = RREG32(CP_RB_WPTR);

	r600_cp_start(rdev);
	rdev->cp.ready = true;
	r = radeon_ring_test(rdev);
	if (r) {
		rdev->cp.ready = false;
		return r;
	}
	return 0;
}

void r600_cp_commit(struct radeon_device *rdev)
{
	WREG32(CP_RB_WPTR, rdev->cp.wptr);
	(void)RREG32(CP_RB_WPTR);
}

void r600_ring_init(struct radeon_device *rdev, unsigned ring_size)
{
	u32 rb_bufsz;

	/* Align ring size */
	rb_bufsz = drm_order(ring_size / 8);
	ring_size = (1 << (rb_bufsz + 1)) * 4;
	rdev->cp.ring_size = ring_size;
	rdev->cp.align_mask = 16 - 1;
}


/*
 * GPU scratch registers helpers function.
 */
void r600_scratch_init(struct radeon_device *rdev)
{
	int i;

	rdev->scratch.num_reg = 7;
	for (i = 0; i < rdev->scratch.num_reg; i++) {
		rdev->scratch.free[i] = true;
		rdev->scratch.reg[i] = SCRATCH_REG0 + (i * 4);
	}
}

int r600_ring_test(struct radeon_device *rdev)
{
	uint32_t scratch;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	r = radeon_scratch_get(rdev, &scratch);
	if (r) {
		DRM_ERROR("radeon: cp failed to get scratch reg (%d).\n", r);
		return r;
	}
	WREG32(scratch, 0xCAFEDEAD);
	r = radeon_ring_lock(rdev, 3);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring (%d).\n", r);
		radeon_scratch_free(rdev, scratch);
		return r;
	}
	radeon_ring_write(rdev, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	radeon_ring_write(rdev, ((scratch - PACKET3_SET_CONFIG_REG_OFFSET) >> 2));
	radeon_ring_write(rdev, 0xDEADBEEF);
	radeon_ring_unlock_commit(rdev);
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(scratch);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}
	if (i < rdev->usec_timeout) {
		DRM_INFO("ring test succeeded in %d usecs\n", i);
	} else {
		DRM_ERROR("radeon: ring test failed (scratch(0x%04X)=0x%08X)\n",
			  scratch, tmp);
		r = -EINVAL;
	}
	radeon_scratch_free(rdev, scratch);
	return r;
}

void r600_wb_disable(struct radeon_device *rdev)
{
	WREG32(SCRATCH_UMSK, 0);
	if (rdev->wb.wb_obj) {
		radeon_object_kunmap(rdev->wb.wb_obj);
		radeon_object_unpin(rdev->wb.wb_obj);
	}
}

void r600_wb_fini(struct radeon_device *rdev)
{
	r600_wb_disable(rdev);
	if (rdev->wb.wb_obj) {
		radeon_object_unref(&rdev->wb.wb_obj);
		rdev->wb.wb = NULL;
		rdev->wb.wb_obj = NULL;
	}
}

int r600_wb_enable(struct radeon_device *rdev)
{
	int r;

	if (rdev->wb.wb_obj == NULL) {
		r = radeon_object_create(rdev, NULL, RADEON_GPU_PAGE_SIZE, true,
				RADEON_GEM_DOMAIN_GTT, false, &rdev->wb.wb_obj);
		if (r) {
			dev_warn(rdev->dev, "failed to create WB buffer (%d).\n", r);
			return r;
		}
		r = radeon_object_pin(rdev->wb.wb_obj, RADEON_GEM_DOMAIN_GTT,
				&rdev->wb.gpu_addr);
		if (r) {
			dev_warn(rdev->dev, "failed to pin WB buffer (%d).\n", r);
			r600_wb_fini(rdev);
			return r;
		}
		r = radeon_object_kmap(rdev->wb.wb_obj, (void **)&rdev->wb.wb);
		if (r) {
			dev_warn(rdev->dev, "failed to map WB buffer (%d).\n", r);
			r600_wb_fini(rdev);
			return r;
		}
	}
	WREG32(SCRATCH_ADDR, (rdev->wb.gpu_addr >> 8) & 0xFFFFFFFF);
	WREG32(CP_RB_RPTR_ADDR, (rdev->wb.gpu_addr + 1024) & 0xFFFFFFFC);
	WREG32(CP_RB_RPTR_ADDR_HI, upper_32_bits(rdev->wb.gpu_addr + 1024) & 0xFF);
	WREG32(SCRATCH_UMSK, 0xff);
	return 0;
}

void r600_fence_ring_emit(struct radeon_device *rdev,
			  struct radeon_fence *fence)
{
	/* Emit fence sequence & fire IRQ */
	radeon_ring_write(rdev, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	radeon_ring_write(rdev, ((rdev->fence_drv.scratch_reg - PACKET3_SET_CONFIG_REG_OFFSET) >> 2));
	radeon_ring_write(rdev, fence->seq);
}

int r600_copy_dma(struct radeon_device *rdev,
		  uint64_t src_offset,
		  uint64_t dst_offset,
		  unsigned num_pages,
		  struct radeon_fence *fence)
{
	/* FIXME: implement */
	return 0;
}

int r600_copy_blit(struct radeon_device *rdev,
		   uint64_t src_offset, uint64_t dst_offset,
		   unsigned num_pages, struct radeon_fence *fence)
{
	r600_blit_prepare_copy(rdev, num_pages * RADEON_GPU_PAGE_SIZE);
	r600_kms_blit_copy(rdev, src_offset, dst_offset, num_pages * RADEON_GPU_PAGE_SIZE);
	r600_blit_done_copy(rdev, fence);
	return 0;
}

int r600_irq_process(struct radeon_device *rdev)
{
	/* FIXME: implement */
	return 0;
}

int r600_irq_set(struct radeon_device *rdev)
{
	/* FIXME: implement */
	return 0;
}

int r600_set_surface_reg(struct radeon_device *rdev, int reg,
			 uint32_t tiling_flags, uint32_t pitch,
			 uint32_t offset, uint32_t obj_size)
{
	/* FIXME: implement */
	return 0;
}

void r600_clear_surface_reg(struct radeon_device *rdev, int reg)
{
	/* FIXME: implement */
}


bool r600_card_posted(struct radeon_device *rdev)
{
	uint32_t reg;

	/* first check CRTCs */
	reg = RREG32(D1CRTC_CONTROL) |
		RREG32(D2CRTC_CONTROL);
	if (reg & CRTC_EN)
		return true;

	/* then check MEM_SIZE, in case the crtcs are off */
	if (RREG32(CONFIG_MEMSIZE))
		return true;

	return false;
}

int r600_startup(struct radeon_device *rdev)
{
	int r;

	r600_mc_program(rdev);
	if (rdev->flags & RADEON_IS_AGP) {
		r600_agp_enable(rdev);
	} else {
		r = r600_pcie_gart_enable(rdev);
		if (r)
			return r;
	}
	r600_gpu_init(rdev);

	r = radeon_object_pin(rdev->r600_blit.shader_obj, RADEON_GEM_DOMAIN_VRAM,
			      &rdev->r600_blit.shader_gpu_addr);
	if (r) {
		DRM_ERROR("failed to pin blit object %d\n", r);
		return r;
	}

	r = radeon_ring_init(rdev, rdev->cp.ring_size);
	if (r)
		return r;
	r = r600_cp_load_microcode(rdev);
	if (r)
		return r;
	r = r600_cp_resume(rdev);
	if (r)
		return r;
	/* write back buffer are not vital so don't worry about failure */
	r600_wb_enable(rdev);
	return 0;
}

int r600_resume(struct radeon_device *rdev)
{
	int r;

	/* Do not reset GPU before posting, on r600 hw unlike on r500 hw,
	 * posting will perform necessary task to bring back GPU into good
	 * shape.
	 */
	/* post card */
	atom_asic_init(rdev->mode_info.atom_context);
	/* Initialize clocks */
	r = radeon_clocks_init(rdev);
	if (r) {
		return r;
	}

	r = r600_startup(rdev);
	if (r) {
		DRM_ERROR("r600 startup failed on resume\n");
		return r;
	}

	r = r600_ib_test(rdev);
	if (r) {
		DRM_ERROR("radeon: failled testing IB (%d).\n", r);
		return r;
	}
	return r;
}

int r600_suspend(struct radeon_device *rdev)
{
	/* FIXME: we should wait for ring to be empty */
	r600_cp_stop(rdev);
	rdev->cp.ready = false;
	r600_wb_disable(rdev);
	r600_pcie_gart_disable(rdev);
	/* unpin shaders bo */
	radeon_object_unpin(rdev->r600_blit.shader_obj);
	return 0;
}

/* Plan is to move initialization in that function and use
 * helper function so that radeon_device_init pretty much
 * do nothing more than calling asic specific function. This
 * should also allow to remove a bunch of callback function
 * like vram_info.
 */
int r600_init(struct radeon_device *rdev)
{
	int r;

	r = radeon_dummy_page_init(rdev);
	if (r)
		return r;
	if (r600_debugfs_mc_info_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for mc !\n");
	}
	/* This don't do much */
	r = radeon_gem_init(rdev);
	if (r)
		return r;
	/* Read BIOS */
	if (!radeon_get_bios(rdev)) {
		if (ASIC_IS_AVIVO(rdev))
			return -EINVAL;
	}
	/* Must be an ATOMBIOS */
	if (!rdev->is_atom_bios) {
		dev_err(rdev->dev, "Expecting atombios for R600 GPU\n");
		return -EINVAL;
	}
	r = radeon_atombios_init(rdev);
	if (r)
		return r;
	/* Post card if necessary */
	if (!r600_card_posted(rdev) && rdev->bios) {
		DRM_INFO("GPU not posted. posting now...\n");
		atom_asic_init(rdev->mode_info.atom_context);
	}
	/* Initialize scratch registers */
	r600_scratch_init(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	/* Initialize clocks */
	radeon_get_clock_info(rdev->ddev);
	r = radeon_clocks_init(rdev);
	if (r)
		return r;
	/* Initialize power management */
	radeon_pm_init(rdev);
	/* Fence driver */
	r = radeon_fence_driver_init(rdev);
	if (r)
		return r;
	r = r600_mc_init(rdev);
	if (r)
		return r;
	/* Memory manager */
	r = radeon_object_init(rdev);
	if (r)
		return r;
	rdev->cp.ring_obj = NULL;
	r600_ring_init(rdev, 1024 * 1024);

	if (!rdev->me_fw || !rdev->pfp_fw) {
		r = r600_cp_init_microcode(rdev);
		if (r) {
			DRM_ERROR("Failed to load firmware!\n");
			return r;
		}
	}

	r = r600_pcie_gart_init(rdev);
	if (r)
		return r;

	rdev->accel_working = true;
	r = r600_blit_init(rdev);
	if (r) {
		DRM_ERROR("radeon: failled blitter (%d).\n", r);
		return r;
	}

	r = r600_startup(rdev);
	if (r) {
		r600_suspend(rdev);
		r600_wb_fini(rdev);
		radeon_ring_fini(rdev);
		r600_pcie_gart_fini(rdev);
		rdev->accel_working = false;
	}
	if (rdev->accel_working) {
		r = radeon_ib_pool_init(rdev);
		if (r) {
			DRM_ERROR("radeon: failled initializing IB pool (%d).\n", r);
			rdev->accel_working = false;
		}
		r = r600_ib_test(rdev);
		if (r) {
			DRM_ERROR("radeon: failled testing IB (%d).\n", r);
			rdev->accel_working = false;
		}
	}
	return 0;
}

void r600_fini(struct radeon_device *rdev)
{
	/* Suspend operations */
	r600_suspend(rdev);

	r600_blit_fini(rdev);
	radeon_ring_fini(rdev);
	r600_wb_fini(rdev);
	r600_pcie_gart_fini(rdev);
	radeon_gem_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_clocks_fini(rdev);
	if (rdev->flags & RADEON_IS_AGP)
		radeon_agp_fini(rdev);
	radeon_object_fini(rdev);
	radeon_atombios_fini(rdev);
	kfree(rdev->bios);
	rdev->bios = NULL;
	radeon_dummy_page_fini(rdev);
}


/*
 * CS stuff
 */
void r600_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib)
{
	/* FIXME: implement */
	radeon_ring_write(rdev, PACKET3(PACKET3_INDIRECT_BUFFER, 2));
	radeon_ring_write(rdev, ib->gpu_addr & 0xFFFFFFFC);
	radeon_ring_write(rdev, upper_32_bits(ib->gpu_addr) & 0xFF);
	radeon_ring_write(rdev, ib->length_dw);
}

int r600_ib_test(struct radeon_device *rdev)
{
	struct radeon_ib *ib;
	uint32_t scratch;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	r = radeon_scratch_get(rdev, &scratch);
	if (r) {
		DRM_ERROR("radeon: failed to get scratch reg (%d).\n", r);
		return r;
	}
	WREG32(scratch, 0xCAFEDEAD);
	r = radeon_ib_get(rdev, &ib);
	if (r) {
		DRM_ERROR("radeon: failed to get ib (%d).\n", r);
		return r;
	}
	ib->ptr[0] = PACKET3(PACKET3_SET_CONFIG_REG, 1);
	ib->ptr[1] = ((scratch - PACKET3_SET_CONFIG_REG_OFFSET) >> 2);
	ib->ptr[2] = 0xDEADBEEF;
	ib->ptr[3] = PACKET2(0);
	ib->ptr[4] = PACKET2(0);
	ib->ptr[5] = PACKET2(0);
	ib->ptr[6] = PACKET2(0);
	ib->ptr[7] = PACKET2(0);
	ib->ptr[8] = PACKET2(0);
	ib->ptr[9] = PACKET2(0);
	ib->ptr[10] = PACKET2(0);
	ib->ptr[11] = PACKET2(0);
	ib->ptr[12] = PACKET2(0);
	ib->ptr[13] = PACKET2(0);
	ib->ptr[14] = PACKET2(0);
	ib->ptr[15] = PACKET2(0);
	ib->length_dw = 16;
	r = radeon_ib_schedule(rdev, ib);
	if (r) {
		radeon_scratch_free(rdev, scratch);
		radeon_ib_free(rdev, &ib);
		DRM_ERROR("radeon: failed to schedule ib (%d).\n", r);
		return r;
	}
	r = radeon_fence_wait(ib->fence, false);
	if (r) {
		DRM_ERROR("radeon: fence wait failed (%d).\n", r);
		return r;
	}
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(scratch);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}
	if (i < rdev->usec_timeout) {
		DRM_INFO("ib test succeeded in %u usecs\n", i);
	} else {
		DRM_ERROR("radeon: ib test failed (sracth(0x%04X)=0x%08X)\n",
			  scratch, tmp);
		r = -EINVAL;
	}
	radeon_scratch_free(rdev, scratch);
	radeon_ib_free(rdev, &ib);
	return r;
}




/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)

static int r600_debugfs_cp_ring_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t rdp, wdp;
	unsigned count, i, j;

	radeon_ring_free_size(rdev);
	rdp = RREG32(CP_RB_RPTR);
	wdp = RREG32(CP_RB_WPTR);
	count = (rdp + rdev->cp.ring_size - wdp) & rdev->cp.ptr_mask;
	seq_printf(m, "CP_STAT 0x%08x\n", RREG32(CP_STAT));
	seq_printf(m, "CP_RB_WPTR 0x%08x\n", wdp);
	seq_printf(m, "CP_RB_RPTR 0x%08x\n", rdp);
	seq_printf(m, "%u free dwords in ring\n", rdev->cp.ring_free_dw);
	seq_printf(m, "%u dwords in ring\n", count);
	for (j = 0; j <= count; j++) {
		i = (rdp + j) & rdev->cp.ptr_mask;
		seq_printf(m, "r[%04d]=0x%08x\n", i, rdev->cp.ring[i]);
	}
	return 0;
}

static int r600_debugfs_mc_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;

	DREG32_SYS(m, rdev, R_000E50_SRBM_STATUS);
	DREG32_SYS(m, rdev, VM_L2_STATUS);
	return 0;
}

static struct drm_info_list r600_mc_info_list[] = {
	{"r600_mc_info", r600_debugfs_mc_info, 0, NULL},
	{"r600_ring_info", r600_debugfs_cp_ring_info, 0, NULL},
};
#endif

int r600_debugfs_mc_info_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, r600_mc_info_list, ARRAY_SIZE(r600_mc_info_list));
#else
	return 0;
#endif
}
