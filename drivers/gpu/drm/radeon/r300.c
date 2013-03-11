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
#include <linux/slab.h>
#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/drm_crtc_helper.h>
#include "radeon_reg.h"
#include "radeon.h"
#include "radeon_asic.h"
#include <drm/radeon_drm.h>
#include "r100_track.h"
#include "r300d.h"
#include "rv350d.h"
#include "r300_reg_safe.h"

/* This files gather functions specifics to: r300,r350,rv350,rv370,rv380
 *
 * GPU Errata:
 * - HOST_PATH_CNTL: r300 family seems to dislike write to HOST_PATH_CNTL
 *   using MMIO to flush host path read cache, this lead to HARDLOCKUP.
 *   However, scheduling such write to the ring seems harmless, i suspect
 *   the CP read collide with the flush somehow, or maybe the MC, hard to
 *   tell. (Jerome Glisse)
 */

/*
 * rv370,rv380 PCIE GART
 */
static int rv370_debugfs_pcie_gart_info_init(struct radeon_device *rdev);

void rv370_pcie_gart_tlb_flush(struct radeon_device *rdev)
{
	uint32_t tmp;
	int i;

	/* Workaround HW bug do flush 2 times */
	for (i = 0; i < 2; i++) {
		tmp = RREG32_PCIE(RADEON_PCIE_TX_GART_CNTL);
		WREG32_PCIE(RADEON_PCIE_TX_GART_CNTL, tmp | RADEON_PCIE_TX_GART_INVALIDATE_TLB);
		(void)RREG32_PCIE(RADEON_PCIE_TX_GART_CNTL);
		WREG32_PCIE(RADEON_PCIE_TX_GART_CNTL, tmp);
	}
	mb();
}

#define R300_PTE_WRITEABLE (1 << 2)
#define R300_PTE_READABLE  (1 << 3)

int rv370_pcie_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr)
{
	void __iomem *ptr = rdev->gart.ptr;

	if (i < 0 || i > rdev->gart.num_gpu_pages) {
		return -EINVAL;
	}
	addr = (lower_32_bits(addr) >> 8) |
	       ((upper_32_bits(addr) & 0xff) << 24) |
	       R300_PTE_WRITEABLE | R300_PTE_READABLE;
	/* on x86 we want this to be CPU endian, on powerpc
	 * on powerpc without HW swappers, it'll get swapped on way
	 * into VRAM - so no need for cpu_to_le32 on VRAM tables */
	writel(addr, ((void __iomem *)ptr) + (i * 4));
	return 0;
}

int rv370_pcie_gart_init(struct radeon_device *rdev)
{
	int r;

	if (rdev->gart.robj) {
		WARN(1, "RV370 PCIE GART already initialized\n");
		return 0;
	}
	/* Initialize common gart structure */
	r = radeon_gart_init(rdev);
	if (r)
		return r;
	r = rv370_debugfs_pcie_gart_info_init(rdev);
	if (r)
		DRM_ERROR("Failed to register debugfs file for PCIE gart !\n");
	rdev->gart.table_size = rdev->gart.num_gpu_pages * 4;
	rdev->asic->gart.tlb_flush = &rv370_pcie_gart_tlb_flush;
	rdev->asic->gart.set_page = &rv370_pcie_gart_set_page;
	return radeon_gart_table_vram_alloc(rdev);
}

int rv370_pcie_gart_enable(struct radeon_device *rdev)
{
	uint32_t table_addr;
	uint32_t tmp;
	int r;

	if (rdev->gart.robj == NULL) {
		dev_err(rdev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}
	r = radeon_gart_table_vram_pin(rdev);
	if (r)
		return r;
	radeon_gart_restore(rdev);
	/* discard memory request outside of configured range */
	tmp = RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD;
	WREG32_PCIE(RADEON_PCIE_TX_GART_CNTL, tmp);
	WREG32_PCIE(RADEON_PCIE_TX_GART_START_LO, rdev->mc.gtt_start);
	tmp = rdev->mc.gtt_end & ~RADEON_GPU_PAGE_MASK;
	WREG32_PCIE(RADEON_PCIE_TX_GART_END_LO, tmp);
	WREG32_PCIE(RADEON_PCIE_TX_GART_START_HI, 0);
	WREG32_PCIE(RADEON_PCIE_TX_GART_END_HI, 0);
	table_addr = rdev->gart.table_addr;
	WREG32_PCIE(RADEON_PCIE_TX_GART_BASE, table_addr);
	/* FIXME: setup default page */
	WREG32_PCIE(RADEON_PCIE_TX_DISCARD_RD_ADDR_LO, rdev->mc.vram_start);
	WREG32_PCIE(RADEON_PCIE_TX_DISCARD_RD_ADDR_HI, 0);
	/* Clear error */
	WREG32_PCIE(RADEON_PCIE_TX_GART_ERROR, 0);
	tmp = RREG32_PCIE(RADEON_PCIE_TX_GART_CNTL);
	tmp |= RADEON_PCIE_TX_GART_EN;
	tmp |= RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD;
	WREG32_PCIE(RADEON_PCIE_TX_GART_CNTL, tmp);
	rv370_pcie_gart_tlb_flush(rdev);
	DRM_INFO("PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(rdev->mc.gtt_size >> 20),
		 (unsigned long long)table_addr);
	rdev->gart.ready = true;
	return 0;
}

void rv370_pcie_gart_disable(struct radeon_device *rdev)
{
	u32 tmp;

	WREG32_PCIE(RADEON_PCIE_TX_GART_START_LO, 0);
	WREG32_PCIE(RADEON_PCIE_TX_GART_END_LO, 0);
	WREG32_PCIE(RADEON_PCIE_TX_GART_START_HI, 0);
	WREG32_PCIE(RADEON_PCIE_TX_GART_END_HI, 0);
	tmp = RREG32_PCIE(RADEON_PCIE_TX_GART_CNTL);
	tmp |= RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD;
	WREG32_PCIE(RADEON_PCIE_TX_GART_CNTL, tmp & ~RADEON_PCIE_TX_GART_EN);
	radeon_gart_table_vram_unpin(rdev);
}

void rv370_pcie_gart_fini(struct radeon_device *rdev)
{
	radeon_gart_fini(rdev);
	rv370_pcie_gart_disable(rdev);
	radeon_gart_table_vram_free(rdev);
}

void r300_fence_ring_emit(struct radeon_device *rdev,
			  struct radeon_fence *fence)
{
	struct radeon_ring *ring = &rdev->ring[fence->ring];

	/* Who ever call radeon_fence_emit should call ring_lock and ask
	 * for enough space (today caller are ib schedule and buffer move) */
	/* Write SC register so SC & US assert idle */
	radeon_ring_write(ring, PACKET0(R300_RE_SCISSORS_TL, 0));
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, PACKET0(R300_RE_SCISSORS_BR, 0));
	radeon_ring_write(ring, 0);
	/* Flush 3D cache */
	radeon_ring_write(ring, PACKET0(R300_RB3D_DSTCACHE_CTLSTAT, 0));
	radeon_ring_write(ring, R300_RB3D_DC_FLUSH);
	radeon_ring_write(ring, PACKET0(R300_RB3D_ZCACHE_CTLSTAT, 0));
	radeon_ring_write(ring, R300_ZC_FLUSH);
	/* Wait until IDLE & CLEAN */
	radeon_ring_write(ring, PACKET0(RADEON_WAIT_UNTIL, 0));
	radeon_ring_write(ring, (RADEON_WAIT_3D_IDLECLEAN |
				 RADEON_WAIT_2D_IDLECLEAN |
				 RADEON_WAIT_DMA_GUI_IDLE));
	radeon_ring_write(ring, PACKET0(RADEON_HOST_PATH_CNTL, 0));
	radeon_ring_write(ring, rdev->config.r300.hdp_cntl |
				RADEON_HDP_READ_BUFFER_INVALIDATE);
	radeon_ring_write(ring, PACKET0(RADEON_HOST_PATH_CNTL, 0));
	radeon_ring_write(ring, rdev->config.r300.hdp_cntl);
	/* Emit fence sequence & fire IRQ */
	radeon_ring_write(ring, PACKET0(rdev->fence_drv[fence->ring].scratch_reg, 0));
	radeon_ring_write(ring, fence->seq);
	radeon_ring_write(ring, PACKET0(RADEON_GEN_INT_STATUS, 0));
	radeon_ring_write(ring, RADEON_SW_INT_FIRE);
}

void r300_ring_start(struct radeon_device *rdev, struct radeon_ring *ring)
{
	unsigned gb_tile_config;
	int r;

	/* Sub pixel 1/12 so we can have 4K rendering according to doc */
	gb_tile_config = (R300_ENABLE_TILING | R300_TILE_SIZE_16);
	switch(rdev->num_gb_pipes) {
	case 2:
		gb_tile_config |= R300_PIPE_COUNT_R300;
		break;
	case 3:
		gb_tile_config |= R300_PIPE_COUNT_R420_3P;
		break;
	case 4:
		gb_tile_config |= R300_PIPE_COUNT_R420;
		break;
	case 1:
	default:
		gb_tile_config |= R300_PIPE_COUNT_RV350;
		break;
	}

	r = radeon_ring_lock(rdev, ring, 64);
	if (r) {
		return;
	}
	radeon_ring_write(ring, PACKET0(RADEON_ISYNC_CNTL, 0));
	radeon_ring_write(ring,
			  RADEON_ISYNC_ANY2D_IDLE3D |
			  RADEON_ISYNC_ANY3D_IDLE2D |
			  RADEON_ISYNC_WAIT_IDLEGUI |
			  RADEON_ISYNC_CPSCRATCH_IDLEGUI);
	radeon_ring_write(ring, PACKET0(R300_GB_TILE_CONFIG, 0));
	radeon_ring_write(ring, gb_tile_config);
	radeon_ring_write(ring, PACKET0(RADEON_WAIT_UNTIL, 0));
	radeon_ring_write(ring,
			  RADEON_WAIT_2D_IDLECLEAN |
			  RADEON_WAIT_3D_IDLECLEAN);
	radeon_ring_write(ring, PACKET0(R300_DST_PIPE_CONFIG, 0));
	radeon_ring_write(ring, R300_PIPE_AUTO_CONFIG);
	radeon_ring_write(ring, PACKET0(R300_GB_SELECT, 0));
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, PACKET0(R300_GB_ENABLE, 0));
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, PACKET0(R300_RB3D_DSTCACHE_CTLSTAT, 0));
	radeon_ring_write(ring, R300_RB3D_DC_FLUSH | R300_RB3D_DC_FREE);
	radeon_ring_write(ring, PACKET0(R300_RB3D_ZCACHE_CTLSTAT, 0));
	radeon_ring_write(ring, R300_ZC_FLUSH | R300_ZC_FREE);
	radeon_ring_write(ring, PACKET0(RADEON_WAIT_UNTIL, 0));
	radeon_ring_write(ring,
			  RADEON_WAIT_2D_IDLECLEAN |
			  RADEON_WAIT_3D_IDLECLEAN);
	radeon_ring_write(ring, PACKET0(R300_GB_AA_CONFIG, 0));
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, PACKET0(R300_RB3D_DSTCACHE_CTLSTAT, 0));
	radeon_ring_write(ring, R300_RB3D_DC_FLUSH | R300_RB3D_DC_FREE);
	radeon_ring_write(ring, PACKET0(R300_RB3D_ZCACHE_CTLSTAT, 0));
	radeon_ring_write(ring, R300_ZC_FLUSH | R300_ZC_FREE);
	radeon_ring_write(ring, PACKET0(R300_GB_MSPOS0, 0));
	radeon_ring_write(ring,
			  ((6 << R300_MS_X0_SHIFT) |
			   (6 << R300_MS_Y0_SHIFT) |
			   (6 << R300_MS_X1_SHIFT) |
			   (6 << R300_MS_Y1_SHIFT) |
			   (6 << R300_MS_X2_SHIFT) |
			   (6 << R300_MS_Y2_SHIFT) |
			   (6 << R300_MSBD0_Y_SHIFT) |
			   (6 << R300_MSBD0_X_SHIFT)));
	radeon_ring_write(ring, PACKET0(R300_GB_MSPOS1, 0));
	radeon_ring_write(ring,
			  ((6 << R300_MS_X3_SHIFT) |
			   (6 << R300_MS_Y3_SHIFT) |
			   (6 << R300_MS_X4_SHIFT) |
			   (6 << R300_MS_Y4_SHIFT) |
			   (6 << R300_MS_X5_SHIFT) |
			   (6 << R300_MS_Y5_SHIFT) |
			   (6 << R300_MSBD1_SHIFT)));
	radeon_ring_write(ring, PACKET0(R300_GA_ENHANCE, 0));
	radeon_ring_write(ring, R300_GA_DEADLOCK_CNTL | R300_GA_FASTSYNC_CNTL);
	radeon_ring_write(ring, PACKET0(R300_GA_POLY_MODE, 0));
	radeon_ring_write(ring,
			  R300_FRONT_PTYPE_TRIANGE | R300_BACK_PTYPE_TRIANGE);
	radeon_ring_write(ring, PACKET0(R300_GA_ROUND_MODE, 0));
	radeon_ring_write(ring,
			  R300_GEOMETRY_ROUND_NEAREST |
			  R300_COLOR_ROUND_NEAREST);
	radeon_ring_unlock_commit(rdev, ring);
}

static void r300_errata(struct radeon_device *rdev)
{
	rdev->pll_errata = 0;

	if (rdev->family == CHIP_R300 &&
	    (RREG32(RADEON_CONFIG_CNTL) & RADEON_CFG_ATI_REV_ID_MASK) == RADEON_CFG_ATI_REV_A11) {
		rdev->pll_errata |= CHIP_ERRATA_R300_CG;
	}
}

int r300_mc_wait_for_idle(struct radeon_device *rdev)
{
	unsigned i;
	uint32_t tmp;

	for (i = 0; i < rdev->usec_timeout; i++) {
		/* read MC_STATUS */
		tmp = RREG32(RADEON_MC_STATUS);
		if (tmp & R300_MC_IDLE) {
			return 0;
		}
		DRM_UDELAY(1);
	}
	return -1;
}

static void r300_gpu_init(struct radeon_device *rdev)
{
	uint32_t gb_tile_config, tmp;

	if ((rdev->family == CHIP_R300 && rdev->pdev->device != 0x4144) ||
	    (rdev->family == CHIP_R350 && rdev->pdev->device != 0x4148)) {
		/* r300,r350 */
		rdev->num_gb_pipes = 2;
	} else {
		/* rv350,rv370,rv380,r300 AD, r350 AH */
		rdev->num_gb_pipes = 1;
	}
	rdev->num_z_pipes = 1;
	gb_tile_config = (R300_ENABLE_TILING | R300_TILE_SIZE_16);
	switch (rdev->num_gb_pipes) {
	case 2:
		gb_tile_config |= R300_PIPE_COUNT_R300;
		break;
	case 3:
		gb_tile_config |= R300_PIPE_COUNT_R420_3P;
		break;
	case 4:
		gb_tile_config |= R300_PIPE_COUNT_R420;
		break;
	default:
	case 1:
		gb_tile_config |= R300_PIPE_COUNT_RV350;
		break;
	}
	WREG32(R300_GB_TILE_CONFIG, gb_tile_config);

	if (r100_gui_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait GUI idle while "
		       "programming pipes. Bad things might happen.\n");
	}

	tmp = RREG32(R300_DST_PIPE_CONFIG);
	WREG32(R300_DST_PIPE_CONFIG, tmp | R300_PIPE_AUTO_CONFIG);

	WREG32(R300_RB2D_DSTCACHE_MODE,
	       R300_DC_AUTOFLUSH_ENABLE |
	       R300_DC_DC_DISABLE_IGNORE_PE);

	if (r100_gui_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait GUI idle while "
		       "programming pipes. Bad things might happen.\n");
	}
	if (r300_mc_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait MC idle while "
		       "programming pipes. Bad things might happen.\n");
	}
	DRM_INFO("radeon: %d quad pipes, %d Z pipes initialized.\n",
		 rdev->num_gb_pipes, rdev->num_z_pipes);
}

int r300_asic_reset(struct radeon_device *rdev)
{
	struct r100_mc_save save;
	u32 status, tmp;
	int ret = 0;

	status = RREG32(R_000E40_RBBM_STATUS);
	if (!G_000E40_GUI_ACTIVE(status)) {
		return 0;
	}
	r100_mc_stop(rdev, &save);
	status = RREG32(R_000E40_RBBM_STATUS);
	dev_info(rdev->dev, "(%s:%d) RBBM_STATUS=0x%08X\n", __func__, __LINE__, status);
	/* stop CP */
	WREG32(RADEON_CP_CSQ_CNTL, 0);
	tmp = RREG32(RADEON_CP_RB_CNTL);
	WREG32(RADEON_CP_RB_CNTL, tmp | RADEON_RB_RPTR_WR_ENA);
	WREG32(RADEON_CP_RB_RPTR_WR, 0);
	WREG32(RADEON_CP_RB_WPTR, 0);
	WREG32(RADEON_CP_RB_CNTL, tmp);
	/* save PCI state */
	pci_save_state(rdev->pdev);
	/* disable bus mastering */
	r100_bm_disable(rdev);
	WREG32(R_0000F0_RBBM_SOFT_RESET, S_0000F0_SOFT_RESET_VAP(1) |
					S_0000F0_SOFT_RESET_GA(1));
	RREG32(R_0000F0_RBBM_SOFT_RESET);
	mdelay(500);
	WREG32(R_0000F0_RBBM_SOFT_RESET, 0);
	mdelay(1);
	status = RREG32(R_000E40_RBBM_STATUS);
	dev_info(rdev->dev, "(%s:%d) RBBM_STATUS=0x%08X\n", __func__, __LINE__, status);
	/* resetting the CP seems to be problematic sometimes it end up
	 * hard locking the computer, but it's necessary for successful
	 * reset more test & playing is needed on R3XX/R4XX to find a
	 * reliable (if any solution)
	 */
	WREG32(R_0000F0_RBBM_SOFT_RESET, S_0000F0_SOFT_RESET_CP(1));
	RREG32(R_0000F0_RBBM_SOFT_RESET);
	mdelay(500);
	WREG32(R_0000F0_RBBM_SOFT_RESET, 0);
	mdelay(1);
	status = RREG32(R_000E40_RBBM_STATUS);
	dev_info(rdev->dev, "(%s:%d) RBBM_STATUS=0x%08X\n", __func__, __LINE__, status);
	/* restore PCI & busmastering */
	pci_restore_state(rdev->pdev);
	r100_enable_bm(rdev);
	/* Check if GPU is idle */
	if (G_000E40_GA_BUSY(status) || G_000E40_VAP_BUSY(status)) {
		dev_err(rdev->dev, "failed to reset GPU\n");
		ret = -1;
	} else
		dev_info(rdev->dev, "GPU reset succeed\n");
	r100_mc_resume(rdev, &save);
	return ret;
}

/*
 * r300,r350,rv350,rv380 VRAM info
 */
void r300_mc_init(struct radeon_device *rdev)
{
	u64 base;
	u32 tmp;

	/* DDR for all card after R300 & IGP */
	rdev->mc.vram_is_ddr = true;
	tmp = RREG32(RADEON_MEM_CNTL);
	tmp &= R300_MEM_NUM_CHANNELS_MASK;
	switch (tmp) {
	case 0: rdev->mc.vram_width = 64; break;
	case 1: rdev->mc.vram_width = 128; break;
	case 2: rdev->mc.vram_width = 256; break;
	default:  rdev->mc.vram_width = 128; break;
	}
	r100_vram_init_sizes(rdev);
	base = rdev->mc.aper_base;
	if (rdev->flags & RADEON_IS_IGP)
		base = (RREG32(RADEON_NB_TOM) & 0xffff) << 16;
	radeon_vram_location(rdev, &rdev->mc, base);
	rdev->mc.gtt_base_align = 0;
	if (!(rdev->flags & RADEON_IS_AGP))
		radeon_gtt_location(rdev, &rdev->mc);
	radeon_update_bandwidth_info(rdev);
}

void rv370_set_pcie_lanes(struct radeon_device *rdev, int lanes)
{
	uint32_t link_width_cntl, mask;

	if (rdev->flags & RADEON_IS_IGP)
		return;

	if (!(rdev->flags & RADEON_IS_PCIE))
		return;

	/* FIXME wait for idle */

	switch (lanes) {
	case 0:
		mask = RADEON_PCIE_LC_LINK_WIDTH_X0;
		break;
	case 1:
		mask = RADEON_PCIE_LC_LINK_WIDTH_X1;
		break;
	case 2:
		mask = RADEON_PCIE_LC_LINK_WIDTH_X2;
		break;
	case 4:
		mask = RADEON_PCIE_LC_LINK_WIDTH_X4;
		break;
	case 8:
		mask = RADEON_PCIE_LC_LINK_WIDTH_X8;
		break;
	case 12:
		mask = RADEON_PCIE_LC_LINK_WIDTH_X12;
		break;
	case 16:
	default:
		mask = RADEON_PCIE_LC_LINK_WIDTH_X16;
		break;
	}

	link_width_cntl = RREG32_PCIE(RADEON_PCIE_LC_LINK_WIDTH_CNTL);

	if ((link_width_cntl & RADEON_PCIE_LC_LINK_WIDTH_RD_MASK) ==
	    (mask << RADEON_PCIE_LC_LINK_WIDTH_RD_SHIFT))
		return;

	link_width_cntl &= ~(RADEON_PCIE_LC_LINK_WIDTH_MASK |
			     RADEON_PCIE_LC_RECONFIG_NOW |
			     RADEON_PCIE_LC_RECONFIG_LATER |
			     RADEON_PCIE_LC_SHORT_RECONFIG_EN);
	link_width_cntl |= mask;
	WREG32_PCIE(RADEON_PCIE_LC_LINK_WIDTH_CNTL, link_width_cntl);
	WREG32_PCIE(RADEON_PCIE_LC_LINK_WIDTH_CNTL, (link_width_cntl |
						     RADEON_PCIE_LC_RECONFIG_NOW));

	/* wait for lane set to complete */
	link_width_cntl = RREG32_PCIE(RADEON_PCIE_LC_LINK_WIDTH_CNTL);
	while (link_width_cntl == 0xffffffff)
		link_width_cntl = RREG32_PCIE(RADEON_PCIE_LC_LINK_WIDTH_CNTL);

}

int rv370_get_pcie_lanes(struct radeon_device *rdev)
{
	u32 link_width_cntl;

	if (rdev->flags & RADEON_IS_IGP)
		return 0;

	if (!(rdev->flags & RADEON_IS_PCIE))
		return 0;

	/* FIXME wait for idle */

	link_width_cntl = RREG32_PCIE(RADEON_PCIE_LC_LINK_WIDTH_CNTL);

	switch ((link_width_cntl & RADEON_PCIE_LC_LINK_WIDTH_RD_MASK) >> RADEON_PCIE_LC_LINK_WIDTH_RD_SHIFT) {
	case RADEON_PCIE_LC_LINK_WIDTH_X0:
		return 0;
	case RADEON_PCIE_LC_LINK_WIDTH_X1:
		return 1;
	case RADEON_PCIE_LC_LINK_WIDTH_X2:
		return 2;
	case RADEON_PCIE_LC_LINK_WIDTH_X4:
		return 4;
	case RADEON_PCIE_LC_LINK_WIDTH_X8:
		return 8;
	case RADEON_PCIE_LC_LINK_WIDTH_X16:
	default:
		return 16;
	}
}

#if defined(CONFIG_DEBUG_FS)
static int rv370_debugfs_pcie_gart_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t tmp;

	tmp = RREG32_PCIE(RADEON_PCIE_TX_GART_CNTL);
	seq_printf(m, "PCIE_TX_GART_CNTL 0x%08x\n", tmp);
	tmp = RREG32_PCIE(RADEON_PCIE_TX_GART_BASE);
	seq_printf(m, "PCIE_TX_GART_BASE 0x%08x\n", tmp);
	tmp = RREG32_PCIE(RADEON_PCIE_TX_GART_START_LO);
	seq_printf(m, "PCIE_TX_GART_START_LO 0x%08x\n", tmp);
	tmp = RREG32_PCIE(RADEON_PCIE_TX_GART_START_HI);
	seq_printf(m, "PCIE_TX_GART_START_HI 0x%08x\n", tmp);
	tmp = RREG32_PCIE(RADEON_PCIE_TX_GART_END_LO);
	seq_printf(m, "PCIE_TX_GART_END_LO 0x%08x\n", tmp);
	tmp = RREG32_PCIE(RADEON_PCIE_TX_GART_END_HI);
	seq_printf(m, "PCIE_TX_GART_END_HI 0x%08x\n", tmp);
	tmp = RREG32_PCIE(RADEON_PCIE_TX_GART_ERROR);
	seq_printf(m, "PCIE_TX_GART_ERROR 0x%08x\n", tmp);
	return 0;
}

static struct drm_info_list rv370_pcie_gart_info_list[] = {
	{"rv370_pcie_gart_info", rv370_debugfs_pcie_gart_info, 0, NULL},
};
#endif

static int rv370_debugfs_pcie_gart_info_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, rv370_pcie_gart_info_list, 1);
#else
	return 0;
#endif
}

static int r300_packet0_check(struct radeon_cs_parser *p,
		struct radeon_cs_packet *pkt,
		unsigned idx, unsigned reg)
{
	struct radeon_cs_reloc *reloc;
	struct r100_cs_track *track;
	volatile uint32_t *ib;
	uint32_t tmp, tile_flags = 0;
	unsigned i;
	int r;
	u32 idx_value;

	ib = p->ib.ptr;
	track = (struct r100_cs_track *)p->track;
	idx_value = radeon_get_ib_value(p, idx);

	switch(reg) {
	case AVIVO_D1MODE_VLINE_START_END:
	case RADEON_CRTC_GUI_TRIG_VLINE:
		r = r100_cs_packet_parse_vline(p);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		break;
	case RADEON_DST_PITCH_OFFSET:
	case RADEON_SRC_PITCH_OFFSET:
		r = r100_reloc_pitch_offset(p, pkt, idx, reg);
		if (r)
			return r;
		break;
	case R300_RB3D_COLOROFFSET0:
	case R300_RB3D_COLOROFFSET1:
	case R300_RB3D_COLOROFFSET2:
	case R300_RB3D_COLOROFFSET3:
		i = (reg - R300_RB3D_COLOROFFSET0) >> 2;
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		track->cb[i].robj = reloc->robj;
		track->cb[i].offset = idx_value;
		track->cb_dirty = true;
		ib[idx] = idx_value + ((u32)reloc->lobj.gpu_offset);
		break;
	case R300_ZB_DEPTHOFFSET:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		track->zb.robj = reloc->robj;
		track->zb.offset = idx_value;
		track->zb_dirty = true;
		ib[idx] = idx_value + ((u32)reloc->lobj.gpu_offset);
		break;
	case R300_TX_OFFSET_0:
	case R300_TX_OFFSET_0+4:
	case R300_TX_OFFSET_0+8:
	case R300_TX_OFFSET_0+12:
	case R300_TX_OFFSET_0+16:
	case R300_TX_OFFSET_0+20:
	case R300_TX_OFFSET_0+24:
	case R300_TX_OFFSET_0+28:
	case R300_TX_OFFSET_0+32:
	case R300_TX_OFFSET_0+36:
	case R300_TX_OFFSET_0+40:
	case R300_TX_OFFSET_0+44:
	case R300_TX_OFFSET_0+48:
	case R300_TX_OFFSET_0+52:
	case R300_TX_OFFSET_0+56:
	case R300_TX_OFFSET_0+60:
		i = (reg - R300_TX_OFFSET_0) >> 2;
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}

		if (p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS) {
			ib[idx] = (idx_value & 31) | /* keep the 1st 5 bits */
				  ((idx_value & ~31) + (u32)reloc->lobj.gpu_offset);
		} else {
			if (reloc->lobj.tiling_flags & RADEON_TILING_MACRO)
				tile_flags |= R300_TXO_MACRO_TILE;
			if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO)
				tile_flags |= R300_TXO_MICRO_TILE;
			else if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO_SQUARE)
				tile_flags |= R300_TXO_MICRO_TILE_SQUARE;

			tmp = idx_value + ((u32)reloc->lobj.gpu_offset);
			tmp |= tile_flags;
			ib[idx] = tmp;
		}
		track->textures[i].robj = reloc->robj;
		track->tex_dirty = true;
		break;
	/* Tracked registers */
	case 0x2084:
		/* VAP_VF_CNTL */
		track->vap_vf_cntl = idx_value;
		break;
	case 0x20B4:
		/* VAP_VTX_SIZE */
		track->vtx_size = idx_value & 0x7F;
		break;
	case 0x2134:
		/* VAP_VF_MAX_VTX_INDX */
		track->max_indx = idx_value & 0x00FFFFFFUL;
		break;
	case 0x2088:
		/* VAP_ALT_NUM_VERTICES - only valid on r500 */
		if (p->rdev->family < CHIP_RV515)
			goto fail;
		track->vap_alt_nverts = idx_value & 0xFFFFFF;
		break;
	case 0x43E4:
		/* SC_SCISSOR1 */
		track->maxy = ((idx_value >> 13) & 0x1FFF) + 1;
		if (p->rdev->family < CHIP_RV515) {
			track->maxy -= 1440;
		}
		track->cb_dirty = true;
		track->zb_dirty = true;
		break;
	case 0x4E00:
		/* RB3D_CCTL */
		if ((idx_value & (1 << 10)) && /* CMASK_ENABLE */
		    p->rdev->cmask_filp != p->filp) {
			DRM_ERROR("Invalid RB3D_CCTL: Cannot enable CMASK.\n");
			return -EINVAL;
		}
		track->num_cb = ((idx_value >> 5) & 0x3) + 1;
		track->cb_dirty = true;
		break;
	case 0x4E38:
	case 0x4E3C:
	case 0x4E40:
	case 0x4E44:
		/* RB3D_COLORPITCH0 */
		/* RB3D_COLORPITCH1 */
		/* RB3D_COLORPITCH2 */
		/* RB3D_COLORPITCH3 */
		if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS)) {
			r = radeon_cs_packet_next_reloc(p, &reloc, 0);
			if (r) {
				DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					  idx, reg);
				radeon_cs_dump_packet(p, pkt);
				return r;
			}

			if (reloc->lobj.tiling_flags & RADEON_TILING_MACRO)
				tile_flags |= R300_COLOR_TILE_ENABLE;
			if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO)
				tile_flags |= R300_COLOR_MICROTILE_ENABLE;
			else if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO_SQUARE)
				tile_flags |= R300_COLOR_MICROTILE_SQUARE_ENABLE;

			tmp = idx_value & ~(0x7 << 16);
			tmp |= tile_flags;
			ib[idx] = tmp;
		}
		i = (reg - 0x4E38) >> 2;
		track->cb[i].pitch = idx_value & 0x3FFE;
		switch (((idx_value >> 21) & 0xF)) {
		case 9:
		case 11:
		case 12:
			track->cb[i].cpp = 1;
			break;
		case 3:
		case 4:
		case 13:
		case 15:
			track->cb[i].cpp = 2;
			break;
		case 5:
			if (p->rdev->family < CHIP_RV515) {
				DRM_ERROR("Invalid color buffer format (%d)!\n",
					  ((idx_value >> 21) & 0xF));
				return -EINVAL;
			}
			/* Pass through. */
		case 6:
			track->cb[i].cpp = 4;
			break;
		case 10:
			track->cb[i].cpp = 8;
			break;
		case 7:
			track->cb[i].cpp = 16;
			break;
		default:
			DRM_ERROR("Invalid color buffer format (%d) !\n",
				  ((idx_value >> 21) & 0xF));
			return -EINVAL;
		}
		track->cb_dirty = true;
		break;
	case 0x4F00:
		/* ZB_CNTL */
		if (idx_value & 2) {
			track->z_enabled = true;
		} else {
			track->z_enabled = false;
		}
		track->zb_dirty = true;
		break;
	case 0x4F10:
		/* ZB_FORMAT */
		switch ((idx_value & 0xF)) {
		case 0:
		case 1:
			track->zb.cpp = 2;
			break;
		case 2:
			track->zb.cpp = 4;
			break;
		default:
			DRM_ERROR("Invalid z buffer format (%d) !\n",
				  (idx_value & 0xF));
			return -EINVAL;
		}
		track->zb_dirty = true;
		break;
	case 0x4F24:
		/* ZB_DEPTHPITCH */
		if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS)) {
			r = radeon_cs_packet_next_reloc(p, &reloc, 0);
			if (r) {
				DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					  idx, reg);
				radeon_cs_dump_packet(p, pkt);
				return r;
			}

			if (reloc->lobj.tiling_flags & RADEON_TILING_MACRO)
				tile_flags |= R300_DEPTHMACROTILE_ENABLE;
			if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO)
				tile_flags |= R300_DEPTHMICROTILE_TILED;
			else if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO_SQUARE)
				tile_flags |= R300_DEPTHMICROTILE_TILED_SQUARE;

			tmp = idx_value & ~(0x7 << 16);
			tmp |= tile_flags;
			ib[idx] = tmp;
		}
		track->zb.pitch = idx_value & 0x3FFC;
		track->zb_dirty = true;
		break;
	case 0x4104:
		/* TX_ENABLE */
		for (i = 0; i < 16; i++) {
			bool enabled;

			enabled = !!(idx_value & (1 << i));
			track->textures[i].enabled = enabled;
		}
		track->tex_dirty = true;
		break;
	case 0x44C0:
	case 0x44C4:
	case 0x44C8:
	case 0x44CC:
	case 0x44D0:
	case 0x44D4:
	case 0x44D8:
	case 0x44DC:
	case 0x44E0:
	case 0x44E4:
	case 0x44E8:
	case 0x44EC:
	case 0x44F0:
	case 0x44F4:
	case 0x44F8:
	case 0x44FC:
		/* TX_FORMAT1_[0-15] */
		i = (reg - 0x44C0) >> 2;
		tmp = (idx_value >> 25) & 0x3;
		track->textures[i].tex_coord_type = tmp;
		switch ((idx_value & 0x1F)) {
		case R300_TX_FORMAT_X8:
		case R300_TX_FORMAT_Y4X4:
		case R300_TX_FORMAT_Z3Y3X2:
			track->textures[i].cpp = 1;
			track->textures[i].compress_format = R100_TRACK_COMP_NONE;
			break;
		case R300_TX_FORMAT_X16:
		case R300_TX_FORMAT_FL_I16:
		case R300_TX_FORMAT_Y8X8:
		case R300_TX_FORMAT_Z5Y6X5:
		case R300_TX_FORMAT_Z6Y5X5:
		case R300_TX_FORMAT_W4Z4Y4X4:
		case R300_TX_FORMAT_W1Z5Y5X5:
		case R300_TX_FORMAT_D3DMFT_CxV8U8:
		case R300_TX_FORMAT_B8G8_B8G8:
		case R300_TX_FORMAT_G8R8_G8B8:
			track->textures[i].cpp = 2;
			track->textures[i].compress_format = R100_TRACK_COMP_NONE;
			break;
		case R300_TX_FORMAT_Y16X16:
		case R300_TX_FORMAT_FL_I16A16:
		case R300_TX_FORMAT_Z11Y11X10:
		case R300_TX_FORMAT_Z10Y11X11:
		case R300_TX_FORMAT_W8Z8Y8X8:
		case R300_TX_FORMAT_W2Z10Y10X10:
		case 0x17:
		case R300_TX_FORMAT_FL_I32:
		case 0x1e:
			track->textures[i].cpp = 4;
			track->textures[i].compress_format = R100_TRACK_COMP_NONE;
			break;
		case R300_TX_FORMAT_W16Z16Y16X16:
		case R300_TX_FORMAT_FL_R16G16B16A16:
		case R300_TX_FORMAT_FL_I32A32:
			track->textures[i].cpp = 8;
			track->textures[i].compress_format = R100_TRACK_COMP_NONE;
			break;
		case R300_TX_FORMAT_FL_R32G32B32A32:
			track->textures[i].cpp = 16;
			track->textures[i].compress_format = R100_TRACK_COMP_NONE;
			break;
		case R300_TX_FORMAT_DXT1:
			track->textures[i].cpp = 1;
			track->textures[i].compress_format = R100_TRACK_COMP_DXT1;
			break;
		case R300_TX_FORMAT_ATI2N:
			if (p->rdev->family < CHIP_R420) {
				DRM_ERROR("Invalid texture format %u\n",
					  (idx_value & 0x1F));
				return -EINVAL;
			}
			/* The same rules apply as for DXT3/5. */
			/* Pass through. */
		case R300_TX_FORMAT_DXT3:
		case R300_TX_FORMAT_DXT5:
			track->textures[i].cpp = 1;
			track->textures[i].compress_format = R100_TRACK_COMP_DXT35;
			break;
		default:
			DRM_ERROR("Invalid texture format %u\n",
				  (idx_value & 0x1F));
			return -EINVAL;
		}
		track->tex_dirty = true;
		break;
	case 0x4400:
	case 0x4404:
	case 0x4408:
	case 0x440C:
	case 0x4410:
	case 0x4414:
	case 0x4418:
	case 0x441C:
	case 0x4420:
	case 0x4424:
	case 0x4428:
	case 0x442C:
	case 0x4430:
	case 0x4434:
	case 0x4438:
	case 0x443C:
		/* TX_FILTER0_[0-15] */
		i = (reg - 0x4400) >> 2;
		tmp = idx_value & 0x7;
		if (tmp == 2 || tmp == 4 || tmp == 6) {
			track->textures[i].roundup_w = false;
		}
		tmp = (idx_value >> 3) & 0x7;
		if (tmp == 2 || tmp == 4 || tmp == 6) {
			track->textures[i].roundup_h = false;
		}
		track->tex_dirty = true;
		break;
	case 0x4500:
	case 0x4504:
	case 0x4508:
	case 0x450C:
	case 0x4510:
	case 0x4514:
	case 0x4518:
	case 0x451C:
	case 0x4520:
	case 0x4524:
	case 0x4528:
	case 0x452C:
	case 0x4530:
	case 0x4534:
	case 0x4538:
	case 0x453C:
		/* TX_FORMAT2_[0-15] */
		i = (reg - 0x4500) >> 2;
		tmp = idx_value & 0x3FFF;
		track->textures[i].pitch = tmp + 1;
		if (p->rdev->family >= CHIP_RV515) {
			tmp = ((idx_value >> 15) & 1) << 11;
			track->textures[i].width_11 = tmp;
			tmp = ((idx_value >> 16) & 1) << 11;
			track->textures[i].height_11 = tmp;

			/* ATI1N */
			if (idx_value & (1 << 14)) {
				/* The same rules apply as for DXT1. */
				track->textures[i].compress_format =
					R100_TRACK_COMP_DXT1;
			}
		} else if (idx_value & (1 << 14)) {
			DRM_ERROR("Forbidden bit TXFORMAT_MSB\n");
			return -EINVAL;
		}
		track->tex_dirty = true;
		break;
	case 0x4480:
	case 0x4484:
	case 0x4488:
	case 0x448C:
	case 0x4490:
	case 0x4494:
	case 0x4498:
	case 0x449C:
	case 0x44A0:
	case 0x44A4:
	case 0x44A8:
	case 0x44AC:
	case 0x44B0:
	case 0x44B4:
	case 0x44B8:
	case 0x44BC:
		/* TX_FORMAT0_[0-15] */
		i = (reg - 0x4480) >> 2;
		tmp = idx_value & 0x7FF;
		track->textures[i].width = tmp + 1;
		tmp = (idx_value >> 11) & 0x7FF;
		track->textures[i].height = tmp + 1;
		tmp = (idx_value >> 26) & 0xF;
		track->textures[i].num_levels = tmp;
		tmp = idx_value & (1 << 31);
		track->textures[i].use_pitch = !!tmp;
		tmp = (idx_value >> 22) & 0xF;
		track->textures[i].txdepth = tmp;
		track->tex_dirty = true;
		break;
	case R300_ZB_ZPASS_ADDR:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		ib[idx] = idx_value + ((u32)reloc->lobj.gpu_offset);
		break;
	case 0x4e0c:
		/* RB3D_COLOR_CHANNEL_MASK */
		track->color_channel_mask = idx_value;
		track->cb_dirty = true;
		break;
	case 0x43a4:
		/* SC_HYPERZ_EN */
		/* r300c emits this register - we need to disable hyperz for it
		 * without complaining */
		if (p->rdev->hyperz_filp != p->filp) {
			if (idx_value & 0x1)
				ib[idx] = idx_value & ~1;
		}
		break;
	case 0x4f1c:
		/* ZB_BW_CNTL */
		track->zb_cb_clear = !!(idx_value & (1 << 5));
		track->cb_dirty = true;
		track->zb_dirty = true;
		if (p->rdev->hyperz_filp != p->filp) {
			if (idx_value & (R300_HIZ_ENABLE |
					 R300_RD_COMP_ENABLE |
					 R300_WR_COMP_ENABLE |
					 R300_FAST_FILL_ENABLE))
				goto fail;
		}
		break;
	case 0x4e04:
		/* RB3D_BLENDCNTL */
		track->blend_read_enable = !!(idx_value & (1 << 2));
		track->cb_dirty = true;
		break;
	case R300_RB3D_AARESOLVE_OFFSET:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		track->aa.robj = reloc->robj;
		track->aa.offset = idx_value;
		track->aa_dirty = true;
		ib[idx] = idx_value + ((u32)reloc->lobj.gpu_offset);
		break;
	case R300_RB3D_AARESOLVE_PITCH:
		track->aa.pitch = idx_value & 0x3FFE;
		track->aa_dirty = true;
		break;
	case R300_RB3D_AARESOLVE_CTL:
		track->aaresolve = idx_value & 0x1;
		track->aa_dirty = true;
		break;
	case 0x4f30: /* ZB_MASK_OFFSET */
	case 0x4f34: /* ZB_ZMASK_PITCH */
	case 0x4f44: /* ZB_HIZ_OFFSET */
	case 0x4f54: /* ZB_HIZ_PITCH */
		if (idx_value && (p->rdev->hyperz_filp != p->filp))
			goto fail;
		break;
	case 0x4028:
		if (idx_value && (p->rdev->hyperz_filp != p->filp))
			goto fail;
		/* GB_Z_PEQ_CONFIG */
		if (p->rdev->family >= CHIP_RV350)
			break;
		goto fail;
		break;
	case 0x4be8:
		/* valid register only on RV530 */
		if (p->rdev->family == CHIP_RV530)
			break;
		/* fallthrough do not move */
	default:
		goto fail;
	}
	return 0;
fail:
	printk(KERN_ERR "Forbidden register 0x%04X in cs at %d (val=%08x)\n",
	       reg, idx, idx_value);
	return -EINVAL;
}

static int r300_packet3_check(struct radeon_cs_parser *p,
			      struct radeon_cs_packet *pkt)
{
	struct radeon_cs_reloc *reloc;
	struct r100_cs_track *track;
	volatile uint32_t *ib;
	unsigned idx;
	int r;

	ib = p->ib.ptr;
	idx = pkt->idx + 1;
	track = (struct r100_cs_track *)p->track;
	switch(pkt->opcode) {
	case PACKET3_3D_LOAD_VBPNTR:
		r = r100_packet3_load_vbpntr(p, pkt, idx);
		if (r)
			return r;
		break;
	case PACKET3_INDX_BUFFER:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for packet3 %d\n", pkt->opcode);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		ib[idx+1] = radeon_get_ib_value(p, idx + 1) + ((u32)reloc->lobj.gpu_offset);
		r = r100_cs_track_check_pkt3_indx_buffer(p, pkt, reloc->robj);
		if (r) {
			return r;
		}
		break;
	/* Draw packet */
	case PACKET3_3D_DRAW_IMMD:
		/* Number of dwords is vtx_size * (num_vertices - 1)
		 * PRIM_WALK must be equal to 3 vertex data in embedded
		 * in cmd stream */
		if (((radeon_get_ib_value(p, idx + 1) >> 4) & 0x3) != 3) {
			DRM_ERROR("PRIM_WALK must be 3 for IMMD draw\n");
			return -EINVAL;
		}
		track->vap_vf_cntl = radeon_get_ib_value(p, idx + 1);
		track->immd_dwords = pkt->count - 1;
		r = r100_cs_track_check(p->rdev, track);
		if (r) {
			return r;
		}
		break;
	case PACKET3_3D_DRAW_IMMD_2:
		/* Number of dwords is vtx_size * (num_vertices - 1)
		 * PRIM_WALK must be equal to 3 vertex data in embedded
		 * in cmd stream */
		if (((radeon_get_ib_value(p, idx) >> 4) & 0x3) != 3) {
			DRM_ERROR("PRIM_WALK must be 3 for IMMD draw\n");
			return -EINVAL;
		}
		track->vap_vf_cntl = radeon_get_ib_value(p, idx);
		track->immd_dwords = pkt->count;
		r = r100_cs_track_check(p->rdev, track);
		if (r) {
			return r;
		}
		break;
	case PACKET3_3D_DRAW_VBUF:
		track->vap_vf_cntl = radeon_get_ib_value(p, idx + 1);
		r = r100_cs_track_check(p->rdev, track);
		if (r) {
			return r;
		}
		break;
	case PACKET3_3D_DRAW_VBUF_2:
		track->vap_vf_cntl = radeon_get_ib_value(p, idx);
		r = r100_cs_track_check(p->rdev, track);
		if (r) {
			return r;
		}
		break;
	case PACKET3_3D_DRAW_INDX:
		track->vap_vf_cntl = radeon_get_ib_value(p, idx + 1);
		r = r100_cs_track_check(p->rdev, track);
		if (r) {
			return r;
		}
		break;
	case PACKET3_3D_DRAW_INDX_2:
		track->vap_vf_cntl = radeon_get_ib_value(p, idx);
		r = r100_cs_track_check(p->rdev, track);
		if (r) {
			return r;
		}
		break;
	case PACKET3_3D_CLEAR_HIZ:
	case PACKET3_3D_CLEAR_ZMASK:
		if (p->rdev->hyperz_filp != p->filp)
			return -EINVAL;
		break;
	case PACKET3_3D_CLEAR_CMASK:
		if (p->rdev->cmask_filp != p->filp)
			return -EINVAL;
		break;
	case PACKET3_NOP:
		break;
	default:
		DRM_ERROR("Packet3 opcode %x not supported\n", pkt->opcode);
		return -EINVAL;
	}
	return 0;
}

int r300_cs_parse(struct radeon_cs_parser *p)
{
	struct radeon_cs_packet pkt;
	struct r100_cs_track *track;
	int r;

	track = kzalloc(sizeof(*track), GFP_KERNEL);
	if (track == NULL)
		return -ENOMEM;
	r100_cs_track_clear(p->rdev, track);
	p->track = track;
	do {
		r = radeon_cs_packet_parse(p, &pkt, p->idx);
		if (r) {
			return r;
		}
		p->idx += pkt.count + 2;
		switch (pkt.type) {
		case RADEON_PACKET_TYPE0:
			r = r100_cs_parse_packet0(p, &pkt,
						  p->rdev->config.r300.reg_safe_bm,
						  p->rdev->config.r300.reg_safe_bm_size,
						  &r300_packet0_check);
			break;
		case RADEON_PACKET_TYPE2:
			break;
		case RADEON_PACKET_TYPE3:
			r = r300_packet3_check(p, &pkt);
			break;
		default:
			DRM_ERROR("Unknown packet type %d !\n", pkt.type);
			return -EINVAL;
		}
		if (r) {
			return r;
		}
	} while (p->idx < p->chunks[p->chunk_ib_idx].length_dw);
	return 0;
}

void r300_set_reg_safe(struct radeon_device *rdev)
{
	rdev->config.r300.reg_safe_bm = r300_reg_safe_bm;
	rdev->config.r300.reg_safe_bm_size = ARRAY_SIZE(r300_reg_safe_bm);
}

void r300_mc_program(struct radeon_device *rdev)
{
	struct r100_mc_save save;
	int r;

	r = r100_debugfs_mc_info_init(rdev);
	if (r) {
		dev_err(rdev->dev, "Failed to create r100_mc debugfs file.\n");
	}

	/* Stops all mc clients */
	r100_mc_stop(rdev, &save);
	if (rdev->flags & RADEON_IS_AGP) {
		WREG32(R_00014C_MC_AGP_LOCATION,
			S_00014C_MC_AGP_START(rdev->mc.gtt_start >> 16) |
			S_00014C_MC_AGP_TOP(rdev->mc.gtt_end >> 16));
		WREG32(R_000170_AGP_BASE, lower_32_bits(rdev->mc.agp_base));
		WREG32(R_00015C_AGP_BASE_2,
			upper_32_bits(rdev->mc.agp_base) & 0xff);
	} else {
		WREG32(R_00014C_MC_AGP_LOCATION, 0x0FFFFFFF);
		WREG32(R_000170_AGP_BASE, 0);
		WREG32(R_00015C_AGP_BASE_2, 0);
	}
	/* Wait for mc idle */
	if (r300_mc_wait_for_idle(rdev))
		DRM_INFO("Failed to wait MC idle before programming MC.\n");
	/* Program MC, should be a 32bits limited address space */
	WREG32(R_000148_MC_FB_LOCATION,
		S_000148_MC_FB_START(rdev->mc.vram_start >> 16) |
		S_000148_MC_FB_TOP(rdev->mc.vram_end >> 16));
	r100_mc_resume(rdev, &save);
}

void r300_clock_startup(struct radeon_device *rdev)
{
	u32 tmp;

	if (radeon_dynclks != -1 && radeon_dynclks)
		radeon_legacy_set_clock_gating(rdev, 1);
	/* We need to force on some of the block */
	tmp = RREG32_PLL(R_00000D_SCLK_CNTL);
	tmp |= S_00000D_FORCE_CP(1) | S_00000D_FORCE_VIP(1);
	if ((rdev->family == CHIP_RV350) || (rdev->family == CHIP_RV380))
		tmp |= S_00000D_FORCE_VAP(1);
	WREG32_PLL(R_00000D_SCLK_CNTL, tmp);
}

static int r300_startup(struct radeon_device *rdev)
{
	int r;

	/* set common regs */
	r100_set_common_regs(rdev);
	/* program mc */
	r300_mc_program(rdev);
	/* Resume clock */
	r300_clock_startup(rdev);
	/* Initialize GPU configuration (# pipes, ...) */
	r300_gpu_init(rdev);
	/* Initialize GART (initialize after TTM so we can allocate
	 * memory through TTM but finalize after TTM) */
	if (rdev->flags & RADEON_IS_PCIE) {
		r = rv370_pcie_gart_enable(rdev);
		if (r)
			return r;
	}

	if (rdev->family == CHIP_R300 ||
	    rdev->family == CHIP_R350 ||
	    rdev->family == CHIP_RV350)
		r100_enable_bm(rdev);

	if (rdev->flags & RADEON_IS_PCI) {
		r = r100_pci_gart_enable(rdev);
		if (r)
			return r;
	}

	/* allocate wb buffer */
	r = radeon_wb_init(rdev);
	if (r)
		return r;

	r = radeon_fence_driver_start_ring(rdev, RADEON_RING_TYPE_GFX_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP fences (%d).\n", r);
		return r;
	}

	/* Enable IRQ */
	r100_irq_set(rdev);
	rdev->config.r300.hdp_cntl = RREG32(RADEON_HOST_PATH_CNTL);
	/* 1M ring buffer */
	r = r100_cp_init(rdev, 1024 * 1024);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP (%d).\n", r);
		return r;
	}

	r = radeon_ib_pool_init(rdev);
	if (r) {
		dev_err(rdev->dev, "IB initialization failed (%d).\n", r);
		return r;
	}

	return 0;
}

int r300_resume(struct radeon_device *rdev)
{
	int r;

	/* Make sur GART are not working */
	if (rdev->flags & RADEON_IS_PCIE)
		rv370_pcie_gart_disable(rdev);
	if (rdev->flags & RADEON_IS_PCI)
		r100_pci_gart_disable(rdev);
	/* Resume clock before doing reset */
	r300_clock_startup(rdev);
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_asic_reset(rdev)) {
		dev_warn(rdev->dev, "GPU reset failed ! (0xE40=0x%08X, 0x7C0=0x%08X)\n",
			RREG32(R_000E40_RBBM_STATUS),
			RREG32(R_0007C0_CP_STAT));
	}
	/* post */
	radeon_combios_asic_init(rdev->ddev);
	/* Resume clock after posting */
	r300_clock_startup(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);

	rdev->accel_working = true;
	r = r300_startup(rdev);
	if (r) {
		rdev->accel_working = false;
	}
	return r;
}

int r300_suspend(struct radeon_device *rdev)
{
	r100_cp_disable(rdev);
	radeon_wb_disable(rdev);
	r100_irq_disable(rdev);
	if (rdev->flags & RADEON_IS_PCIE)
		rv370_pcie_gart_disable(rdev);
	if (rdev->flags & RADEON_IS_PCI)
		r100_pci_gart_disable(rdev);
	return 0;
}

void r300_fini(struct radeon_device *rdev)
{
	r100_cp_fini(rdev);
	radeon_wb_fini(rdev);
	radeon_ib_pool_fini(rdev);
	radeon_gem_fini(rdev);
	if (rdev->flags & RADEON_IS_PCIE)
		rv370_pcie_gart_fini(rdev);
	if (rdev->flags & RADEON_IS_PCI)
		r100_pci_gart_fini(rdev);
	radeon_agp_fini(rdev);
	radeon_irq_kms_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_bo_fini(rdev);
	radeon_atombios_fini(rdev);
	kfree(rdev->bios);
	rdev->bios = NULL;
}

int r300_init(struct radeon_device *rdev)
{
	int r;

	/* Disable VGA */
	r100_vga_render_disable(rdev);
	/* Initialize scratch registers */
	radeon_scratch_init(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	/* TODO: disable VGA need to use VGA request */
	/* restore some register to sane defaults */
	r100_restore_sanity(rdev);
	/* BIOS*/
	if (!radeon_get_bios(rdev)) {
		if (ASIC_IS_AVIVO(rdev))
			return -EINVAL;
	}
	if (rdev->is_atom_bios) {
		dev_err(rdev->dev, "Expecting combios for RS400/RS480 GPU\n");
		return -EINVAL;
	} else {
		r = radeon_combios_init(rdev);
		if (r)
			return r;
	}
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_asic_reset(rdev)) {
		dev_warn(rdev->dev,
			"GPU reset failed ! (0xE40=0x%08X, 0x7C0=0x%08X)\n",
			RREG32(R_000E40_RBBM_STATUS),
			RREG32(R_0007C0_CP_STAT));
	}
	/* check if cards are posted or not */
	if (radeon_boot_test_post_card(rdev) == false)
		return -EINVAL;
	/* Set asic errata */
	r300_errata(rdev);
	/* Initialize clocks */
	radeon_get_clock_info(rdev->ddev);
	/* initialize AGP */
	if (rdev->flags & RADEON_IS_AGP) {
		r = radeon_agp_init(rdev);
		if (r) {
			radeon_agp_disable(rdev);
		}
	}
	/* initialize memory controller */
	r300_mc_init(rdev);
	/* Fence driver */
	r = radeon_fence_driver_init(rdev);
	if (r)
		return r;
	r = radeon_irq_kms_init(rdev);
	if (r)
		return r;
	/* Memory manager */
	r = radeon_bo_init(rdev);
	if (r)
		return r;
	if (rdev->flags & RADEON_IS_PCIE) {
		r = rv370_pcie_gart_init(rdev);
		if (r)
			return r;
	}
	if (rdev->flags & RADEON_IS_PCI) {
		r = r100_pci_gart_init(rdev);
		if (r)
			return r;
	}
	r300_set_reg_safe(rdev);

	rdev->accel_working = true;
	r = r300_startup(rdev);
	if (r) {
		/* Somethings want wront with the accel init stop accel */
		dev_err(rdev->dev, "Disabling GPU acceleration\n");
		r100_cp_fini(rdev);
		radeon_wb_fini(rdev);
		radeon_ib_pool_fini(rdev);
		radeon_irq_kms_fini(rdev);
		if (rdev->flags & RADEON_IS_PCIE)
			rv370_pcie_gart_fini(rdev);
		if (rdev->flags & RADEON_IS_PCI)
			r100_pci_gart_fini(rdev);
		radeon_agp_fini(rdev);
		rdev->accel_working = false;
	}
	return 0;
}
