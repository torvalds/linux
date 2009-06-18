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
#include "drmP.h"
#include "drm.h"
#include "radeon_reg.h"
#include "radeon.h"

/* r300,r350,rv350,rv370,rv380 depends on : */
void r100_hdp_reset(struct radeon_device *rdev);
int r100_cp_reset(struct radeon_device *rdev);
int r100_rb2d_reset(struct radeon_device *rdev);
int r100_cp_init(struct radeon_device *rdev, unsigned ring_size);
int r100_pci_gart_enable(struct radeon_device *rdev);
void r100_pci_gart_disable(struct radeon_device *rdev);
void r100_mc_setup(struct radeon_device *rdev);
void r100_mc_disable_clients(struct radeon_device *rdev);
int r100_gui_wait_for_idle(struct radeon_device *rdev);
int r100_cs_packet_parse(struct radeon_cs_parser *p,
			 struct radeon_cs_packet *pkt,
			 unsigned idx);
int r100_cs_packet_next_reloc(struct radeon_cs_parser *p,
			      struct radeon_cs_reloc **cs_reloc);
int r100_cs_parse_packet0(struct radeon_cs_parser *p,
			  struct radeon_cs_packet *pkt,
			  unsigned *auth, unsigned n,
			  radeon_packet0_check_t check);
int r100_cs_parse_packet3(struct radeon_cs_parser *p,
			  struct radeon_cs_packet *pkt,
			  unsigned *auth, unsigned n,
			  radeon_packet3_check_t check);
void r100_cs_dump_packet(struct radeon_cs_parser *p,
			 struct radeon_cs_packet *pkt);

/* This files gather functions specifics to:
 * r300,r350,rv350,rv370,rv380
 *
 * Some of these functions might be used by newer ASICs.
 */
void r300_gpu_init(struct radeon_device *rdev);
int r300_mc_wait_for_idle(struct radeon_device *rdev);
int rv370_debugfs_pcie_gart_info_init(struct radeon_device *rdev);


/*
 * rv370,rv380 PCIE GART
 */
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
		mb();
	}
}

int rv370_pcie_gart_enable(struct radeon_device *rdev)
{
	uint32_t table_addr;
	uint32_t tmp;
	int r;

	/* Initialize common gart structure */
	r = radeon_gart_init(rdev);
	if (r) {
		return r;
	}
	r = rv370_debugfs_pcie_gart_info_init(rdev);
	if (r) {
		DRM_ERROR("Failed to register debugfs file for PCIE gart !\n");
	}
	rdev->gart.table_size = rdev->gart.num_gpu_pages * 4;
	r = radeon_gart_table_vram_alloc(rdev);
	if (r) {
		return r;
	}
	/* discard memory request outside of configured range */
	tmp = RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD;
	WREG32_PCIE(RADEON_PCIE_TX_GART_CNTL, tmp);
	WREG32_PCIE(RADEON_PCIE_TX_GART_START_LO, rdev->mc.gtt_location);
	tmp = rdev->mc.gtt_location + rdev->mc.gtt_size - 4096;
	WREG32_PCIE(RADEON_PCIE_TX_GART_END_LO, tmp);
	WREG32_PCIE(RADEON_PCIE_TX_GART_START_HI, 0);
	WREG32_PCIE(RADEON_PCIE_TX_GART_END_HI, 0);
	table_addr = rdev->gart.table_addr;
	WREG32_PCIE(RADEON_PCIE_TX_GART_BASE, table_addr);
	/* FIXME: setup default page */
	WREG32_PCIE(RADEON_PCIE_TX_DISCARD_RD_ADDR_LO, rdev->mc.vram_location);
	WREG32_PCIE(RADEON_PCIE_TX_DISCARD_RD_ADDR_HI, 0);
	/* Clear error */
	WREG32_PCIE(0x18, 0);
	tmp = RREG32_PCIE(RADEON_PCIE_TX_GART_CNTL);
	tmp |= RADEON_PCIE_TX_GART_EN;
	tmp |= RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD;
	WREG32_PCIE(RADEON_PCIE_TX_GART_CNTL, tmp);
	rv370_pcie_gart_tlb_flush(rdev);
	DRM_INFO("PCIE GART of %uM enabled (table at 0x%08X).\n",
		 rdev->mc.gtt_size >> 20, table_addr);
	rdev->gart.ready = true;
	return 0;
}

void rv370_pcie_gart_disable(struct radeon_device *rdev)
{
	uint32_t tmp;

	tmp = RREG32_PCIE(RADEON_PCIE_TX_GART_CNTL);
	tmp |= RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD;
	WREG32_PCIE(RADEON_PCIE_TX_GART_CNTL, tmp & ~RADEON_PCIE_TX_GART_EN);
	if (rdev->gart.table.vram.robj) {
		radeon_object_kunmap(rdev->gart.table.vram.robj);
		radeon_object_unpin(rdev->gart.table.vram.robj);
	}
}

int rv370_pcie_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr)
{
	void __iomem *ptr = (void *)rdev->gart.table.vram.ptr;

	if (i < 0 || i > rdev->gart.num_gpu_pages) {
		return -EINVAL;
	}
	addr = (((u32)addr) >> 8) | ((upper_32_bits(addr) & 0xff) << 4) | 0xC;
	writel(cpu_to_le32(addr), ((void __iomem *)ptr) + (i * 4));
	return 0;
}

int r300_gart_enable(struct radeon_device *rdev)
{
#if __OS_HAS_AGP
	if (rdev->flags & RADEON_IS_AGP) {
		if (rdev->family > CHIP_RV350) {
			rv370_pcie_gart_disable(rdev);
		} else {
			r100_pci_gart_disable(rdev);
		}
		return 0;
	}
#endif
	if (rdev->flags & RADEON_IS_PCIE) {
		rdev->asic->gart_disable = &rv370_pcie_gart_disable;
		rdev->asic->gart_tlb_flush = &rv370_pcie_gart_tlb_flush;
		rdev->asic->gart_set_page = &rv370_pcie_gart_set_page;
		return rv370_pcie_gart_enable(rdev);
	}
	return r100_pci_gart_enable(rdev);
}


/*
 * MC
 */
int r300_mc_init(struct radeon_device *rdev)
{
	int r;

	if (r100_debugfs_rbbm_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for RBBM !\n");
	}

	r300_gpu_init(rdev);
	r100_pci_gart_disable(rdev);
	if (rdev->flags & RADEON_IS_PCIE) {
		rv370_pcie_gart_disable(rdev);
	}

	/* Setup GPU memory space */
	rdev->mc.vram_location = 0xFFFFFFFFUL;
	rdev->mc.gtt_location = 0xFFFFFFFFUL;
	if (rdev->flags & RADEON_IS_AGP) {
		r = radeon_agp_init(rdev);
		if (r) {
			printk(KERN_WARNING "[drm] Disabling AGP\n");
			rdev->flags &= ~RADEON_IS_AGP;
			rdev->mc.gtt_size = radeon_gart_size * 1024 * 1024;
		} else {
			rdev->mc.gtt_location = rdev->mc.agp_base;
		}
	}
	r = radeon_mc_setup(rdev);
	if (r) {
		return r;
	}

	/* Program GPU memory space */
	r100_mc_disable_clients(rdev);
	if (r300_mc_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait MC idle while "
		       "programming pipes. Bad things might happen.\n");
	}
	r100_mc_setup(rdev);
	return 0;
}

void r300_mc_fini(struct radeon_device *rdev)
{
	if (rdev->flags & RADEON_IS_PCIE) {
		rv370_pcie_gart_disable(rdev);
		radeon_gart_table_vram_free(rdev);
	} else {
		r100_pci_gart_disable(rdev);
		radeon_gart_table_ram_free(rdev);
	}
	radeon_gart_fini(rdev);
}


/*
 * Fence emission
 */
void r300_fence_ring_emit(struct radeon_device *rdev,
			  struct radeon_fence *fence)
{
	/* Who ever call radeon_fence_emit should call ring_lock and ask
	 * for enough space (today caller are ib schedule and buffer move) */
	/* Write SC register so SC & US assert idle */
	radeon_ring_write(rdev, PACKET0(0x43E0, 0));
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, PACKET0(0x43E4, 0));
	radeon_ring_write(rdev, 0);
	/* Flush 3D cache */
	radeon_ring_write(rdev, PACKET0(0x4E4C, 0));
	radeon_ring_write(rdev, (2 << 0));
	radeon_ring_write(rdev, PACKET0(0x4F18, 0));
	radeon_ring_write(rdev, (1 << 0));
	/* Wait until IDLE & CLEAN */
	radeon_ring_write(rdev, PACKET0(0x1720, 0));
	radeon_ring_write(rdev, (1 << 17) | (1 << 16)  | (1 << 9));
	/* Emit fence sequence & fire IRQ */
	radeon_ring_write(rdev, PACKET0(rdev->fence_drv.scratch_reg, 0));
	radeon_ring_write(rdev, fence->seq);
	radeon_ring_write(rdev, PACKET0(RADEON_GEN_INT_STATUS, 0));
	radeon_ring_write(rdev, RADEON_SW_INT_FIRE);
}


/*
 * Global GPU functions
 */
int r300_copy_dma(struct radeon_device *rdev,
		  uint64_t src_offset,
		  uint64_t dst_offset,
		  unsigned num_pages,
		  struct radeon_fence *fence)
{
	uint32_t size;
	uint32_t cur_size;
	int i, num_loops;
	int r = 0;

	/* radeon pitch is /64 */
	size = num_pages << PAGE_SHIFT;
	num_loops = DIV_ROUND_UP(size, 0x1FFFFF);
	r = radeon_ring_lock(rdev, num_loops * 4 + 64);
	if (r) {
		DRM_ERROR("radeon: moving bo (%d).\n", r);
		return r;
	}
	/* Must wait for 2D idle & clean before DMA or hangs might happen */
	radeon_ring_write(rdev, PACKET0(RADEON_WAIT_UNTIL, 0));
	radeon_ring_write(rdev, (1 << 16));
	for (i = 0; i < num_loops; i++) {
		cur_size = size;
		if (cur_size > 0x1FFFFF) {
			cur_size = 0x1FFFFF;
		}
		size -= cur_size;
		radeon_ring_write(rdev, PACKET0(0x720, 2));
		radeon_ring_write(rdev, src_offset);
		radeon_ring_write(rdev, dst_offset);
		radeon_ring_write(rdev, cur_size | (1 << 31) | (1 << 30));
		src_offset += cur_size;
		dst_offset += cur_size;
	}
	radeon_ring_write(rdev, PACKET0(RADEON_WAIT_UNTIL, 0));
	radeon_ring_write(rdev, RADEON_WAIT_DMA_GUI_IDLE);
	if (fence) {
		r = radeon_fence_emit(rdev, fence);
	}
	radeon_ring_unlock_commit(rdev);
	return r;
}

void r300_ring_start(struct radeon_device *rdev)
{
	unsigned gb_tile_config;
	int r;

	/* Sub pixel 1/12 so we can have 4K rendering according to doc */
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
	case 1:
	default:
		gb_tile_config |= R300_PIPE_COUNT_RV350;
		break;
	}

	r = radeon_ring_lock(rdev, 64);
	if (r) {
		return;
	}
	radeon_ring_write(rdev, PACKET0(RADEON_ISYNC_CNTL, 0));
	radeon_ring_write(rdev,
			  RADEON_ISYNC_ANY2D_IDLE3D |
			  RADEON_ISYNC_ANY3D_IDLE2D |
			  RADEON_ISYNC_WAIT_IDLEGUI |
			  RADEON_ISYNC_CPSCRATCH_IDLEGUI);
	radeon_ring_write(rdev, PACKET0(R300_GB_TILE_CONFIG, 0));
	radeon_ring_write(rdev, gb_tile_config);
	radeon_ring_write(rdev, PACKET0(RADEON_WAIT_UNTIL, 0));
	radeon_ring_write(rdev,
			  RADEON_WAIT_2D_IDLECLEAN |
			  RADEON_WAIT_3D_IDLECLEAN);
	radeon_ring_write(rdev, PACKET0(0x170C, 0));
	radeon_ring_write(rdev, 1 << 31);
	radeon_ring_write(rdev, PACKET0(R300_GB_SELECT, 0));
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, PACKET0(R300_GB_ENABLE, 0));
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, PACKET0(R300_RB3D_DSTCACHE_CTLSTAT, 0));
	radeon_ring_write(rdev, R300_RB3D_DC_FLUSH | R300_RB3D_DC_FREE);
	radeon_ring_write(rdev, PACKET0(R300_RB3D_ZCACHE_CTLSTAT, 0));
	radeon_ring_write(rdev, R300_ZC_FLUSH | R300_ZC_FREE);
	radeon_ring_write(rdev, PACKET0(RADEON_WAIT_UNTIL, 0));
	radeon_ring_write(rdev,
			  RADEON_WAIT_2D_IDLECLEAN |
			  RADEON_WAIT_3D_IDLECLEAN);
	radeon_ring_write(rdev, PACKET0(R300_GB_AA_CONFIG, 0));
	radeon_ring_write(rdev, 0);
	radeon_ring_write(rdev, PACKET0(R300_RB3D_DSTCACHE_CTLSTAT, 0));
	radeon_ring_write(rdev, R300_RB3D_DC_FLUSH | R300_RB3D_DC_FREE);
	radeon_ring_write(rdev, PACKET0(R300_RB3D_ZCACHE_CTLSTAT, 0));
	radeon_ring_write(rdev, R300_ZC_FLUSH | R300_ZC_FREE);
	radeon_ring_write(rdev, PACKET0(R300_GB_MSPOS0, 0));
	radeon_ring_write(rdev,
			  ((6 << R300_MS_X0_SHIFT) |
			   (6 << R300_MS_Y0_SHIFT) |
			   (6 << R300_MS_X1_SHIFT) |
			   (6 << R300_MS_Y1_SHIFT) |
			   (6 << R300_MS_X2_SHIFT) |
			   (6 << R300_MS_Y2_SHIFT) |
			   (6 << R300_MSBD0_Y_SHIFT) |
			   (6 << R300_MSBD0_X_SHIFT)));
	radeon_ring_write(rdev, PACKET0(R300_GB_MSPOS1, 0));
	radeon_ring_write(rdev,
			  ((6 << R300_MS_X3_SHIFT) |
			   (6 << R300_MS_Y3_SHIFT) |
			   (6 << R300_MS_X4_SHIFT) |
			   (6 << R300_MS_Y4_SHIFT) |
			   (6 << R300_MS_X5_SHIFT) |
			   (6 << R300_MS_Y5_SHIFT) |
			   (6 << R300_MSBD1_SHIFT)));
	radeon_ring_write(rdev, PACKET0(R300_GA_ENHANCE, 0));
	radeon_ring_write(rdev, R300_GA_DEADLOCK_CNTL | R300_GA_FASTSYNC_CNTL);
	radeon_ring_write(rdev, PACKET0(R300_GA_POLY_MODE, 0));
	radeon_ring_write(rdev,
			  R300_FRONT_PTYPE_TRIANGE | R300_BACK_PTYPE_TRIANGE);
	radeon_ring_write(rdev, PACKET0(R300_GA_ROUND_MODE, 0));
	radeon_ring_write(rdev,
			  R300_GEOMETRY_ROUND_NEAREST |
			  R300_COLOR_ROUND_NEAREST);
	radeon_ring_unlock_commit(rdev);
}

void r300_errata(struct radeon_device *rdev)
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
		tmp = RREG32(0x0150);
		if (tmp & (1 << 4)) {
			return 0;
		}
		DRM_UDELAY(1);
	}
	return -1;
}

void r300_gpu_init(struct radeon_device *rdev)
{
	uint32_t gb_tile_config, tmp;

	r100_hdp_reset(rdev);
	/* FIXME: rv380 one pipes ? */
	if ((rdev->family == CHIP_R300) || (rdev->family == CHIP_R350)) {
		/* r300,r350 */
		rdev->num_gb_pipes = 2;
	} else {
		/* rv350,rv370,rv380 */
		rdev->num_gb_pipes = 1;
	}
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
	case 1:
	default:
		gb_tile_config |= R300_PIPE_COUNT_RV350;
		break;
	}
	WREG32(R300_GB_TILE_CONFIG, gb_tile_config);

	if (r100_gui_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait GUI idle while "
		       "programming pipes. Bad things might happen.\n");
	}

	tmp = RREG32(0x170C);
	WREG32(0x170C, tmp | (1 << 31));

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
	DRM_INFO("radeon: %d pipes initialized.\n", rdev->num_gb_pipes);
}

int r300_ga_reset(struct radeon_device *rdev)
{
	uint32_t tmp;
	bool reinit_cp;
	int i;

	reinit_cp = rdev->cp.ready;
	rdev->cp.ready = false;
	for (i = 0; i < rdev->usec_timeout; i++) {
		WREG32(RADEON_CP_CSQ_MODE, 0);
		WREG32(RADEON_CP_CSQ_CNTL, 0);
		WREG32(RADEON_RBBM_SOFT_RESET, 0x32005);
		(void)RREG32(RADEON_RBBM_SOFT_RESET);
		udelay(200);
		WREG32(RADEON_RBBM_SOFT_RESET, 0);
		/* Wait to prevent race in RBBM_STATUS */
		mdelay(1);
		tmp = RREG32(RADEON_RBBM_STATUS);
		if (tmp & ((1 << 20) | (1 << 26))) {
			DRM_ERROR("VAP & CP still busy (RBBM_STATUS=0x%08X)", tmp);
			/* GA still busy soft reset it */
			WREG32(0x429C, 0x200);
			WREG32(R300_VAP_PVS_STATE_FLUSH_REG, 0);
			WREG32(0x43E0, 0);
			WREG32(0x43E4, 0);
			WREG32(0x24AC, 0);
		}
		/* Wait to prevent race in RBBM_STATUS */
		mdelay(1);
		tmp = RREG32(RADEON_RBBM_STATUS);
		if (!(tmp & ((1 << 20) | (1 << 26)))) {
			break;
		}
	}
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(RADEON_RBBM_STATUS);
		if (!(tmp & ((1 << 20) | (1 << 26)))) {
			DRM_INFO("GA reset succeed (RBBM_STATUS=0x%08X)\n",
				 tmp);
			if (reinit_cp) {
				return r100_cp_init(rdev, rdev->cp.ring_size);
			}
			return 0;
		}
		DRM_UDELAY(1);
	}
	tmp = RREG32(RADEON_RBBM_STATUS);
	DRM_ERROR("Failed to reset GA ! (RBBM_STATUS=0x%08X)\n", tmp);
	return -1;
}

int r300_gpu_reset(struct radeon_device *rdev)
{
	uint32_t status;

	/* reset order likely matter */
	status = RREG32(RADEON_RBBM_STATUS);
	/* reset HDP */
	r100_hdp_reset(rdev);
	/* reset rb2d */
	if (status & ((1 << 17) | (1 << 18) | (1 << 27))) {
		r100_rb2d_reset(rdev);
	}
	/* reset GA */
	if (status & ((1 << 20) | (1 << 26))) {
		r300_ga_reset(rdev);
	}
	/* reset CP */
	status = RREG32(RADEON_RBBM_STATUS);
	if (status & (1 << 16)) {
		r100_cp_reset(rdev);
	}
	/* Check if GPU is idle */
	status = RREG32(RADEON_RBBM_STATUS);
	if (status & (1 << 31)) {
		DRM_ERROR("Failed to reset GPU (RBBM_STATUS=0x%08X)\n", status);
		return -1;
	}
	DRM_INFO("GPU reset succeed (RBBM_STATUS=0x%08X)\n", status);
	return 0;
}


/*
 * r300,r350,rv350,rv380 VRAM info
 */
void r300_vram_info(struct radeon_device *rdev)
{
	uint32_t tmp;

	/* DDR for all card after R300 & IGP */
	rdev->mc.vram_is_ddr = true;
	tmp = RREG32(RADEON_MEM_CNTL);
	if (tmp & R300_MEM_NUM_CHANNELS_MASK) {
		rdev->mc.vram_width = 128;
	} else {
		rdev->mc.vram_width = 64;
	}
	rdev->mc.vram_size = RREG32(RADEON_CONFIG_MEMSIZE);

	rdev->mc.aper_base = drm_get_resource_start(rdev->ddev, 0);
	rdev->mc.aper_size = drm_get_resource_len(rdev->ddev, 0);
}


/*
 * Indirect registers accessor
 */
uint32_t rv370_pcie_rreg(struct radeon_device *rdev, uint32_t reg)
{
	uint32_t r;

	WREG8(RADEON_PCIE_INDEX, ((reg) & 0xff));
	(void)RREG32(RADEON_PCIE_INDEX);
	r = RREG32(RADEON_PCIE_DATA);
	return r;
}

void rv370_pcie_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	WREG8(RADEON_PCIE_INDEX, ((reg) & 0xff));
	(void)RREG32(RADEON_PCIE_INDEX);
	WREG32(RADEON_PCIE_DATA, (v));
	(void)RREG32(RADEON_PCIE_DATA);
}

/*
 * PCIE Lanes
 */

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


/*
 * Debugfs info
 */
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

int rv370_debugfs_pcie_gart_info_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, rv370_pcie_gart_info_list, 1);
#else
	return 0;
#endif
}


/*
 * CS functions
 */
struct r300_cs_track_cb {
	struct radeon_object	*robj;
	unsigned		pitch;
	unsigned		cpp;
	unsigned		offset;
};

struct r300_cs_track {
	unsigned		num_cb;
	unsigned		maxy;
	struct r300_cs_track_cb cb[4];
	struct r300_cs_track_cb zb;
	bool			z_enabled;
};

int r300_cs_track_check(struct radeon_device *rdev, struct r300_cs_track *track)
{
	unsigned i;
	unsigned long size;

	for (i = 0; i < track->num_cb; i++) {
		if (track->cb[i].robj == NULL) {
			DRM_ERROR("[drm] No buffer for color buffer %d !\n", i);
			return -EINVAL;
		}
		size = track->cb[i].pitch * track->cb[i].cpp * track->maxy;
		size += track->cb[i].offset;
		if (size > radeon_object_size(track->cb[i].robj)) {
			DRM_ERROR("[drm] Buffer too small for color buffer %d "
				  "(need %lu have %lu) !\n", i, size,
				  radeon_object_size(track->cb[i].robj));
			DRM_ERROR("[drm] color buffer %d (%u %u %u %u)\n",
				  i, track->cb[i].pitch, track->cb[i].cpp,
				  track->cb[i].offset, track->maxy);
			return -EINVAL;
		}
	}
	if (track->z_enabled) {
		if (track->zb.robj == NULL) {
			DRM_ERROR("[drm] No buffer for z buffer !\n");
			return -EINVAL;
		}
		size = track->zb.pitch * track->zb.cpp * track->maxy;
		size += track->zb.offset;
		if (size > radeon_object_size(track->zb.robj)) {
			DRM_ERROR("[drm] Buffer too small for z buffer "
				  "(need %lu have %lu) !\n", size,
				  radeon_object_size(track->zb.robj));
			return -EINVAL;
		}
	}
	return 0;
}

static inline void r300_cs_track_clear(struct r300_cs_track *track)
{
	unsigned i;

	track->num_cb = 4;
	track->maxy = 4096;
	for (i = 0; i < track->num_cb; i++) {
		track->cb[i].robj = NULL;
		track->cb[i].pitch = 8192;
		track->cb[i].cpp = 16;
		track->cb[i].offset = 0;
	}
	track->z_enabled = true;
	track->zb.robj = NULL;
	track->zb.pitch = 8192;
	track->zb.cpp = 4;
	track->zb.offset = 0;
}

static unsigned r300_auth_reg[] = {
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	0xFFFFFFBF, 0xFFFFFFFF, 0xFFFFFFBF, 0xFFFFFFFF,
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
	0xFFFFFFFF, 0xFFFFCFCC, 0xF00E9FFF, 0x007C0000,
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
	0xFFFFFC78, 0xFFFFFFFF, 0xFFFFFFFC, 0xFFFFFFFF,
	0x38FF8F50, 0xFFF88082, 0xF000000C, 0xFAE009FF,
	0x00000000, 0x00000000, 0xFFFF0000, 0x00000000,
	0x00000000, 0x0000C100, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0xFFFF0000, 0xFFFFFFFF, 0xFF80FFFF,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x0003FC01, 0xFFFFFFF8, 0xFE800B19,
};

static int r300_packet0_check(struct radeon_cs_parser *p,
		struct radeon_cs_packet *pkt,
		unsigned idx, unsigned reg)
{
	struct radeon_cs_chunk *ib_chunk;
	struct radeon_cs_reloc *reloc;
	struct r300_cs_track *track;
	volatile uint32_t *ib;
	uint32_t tmp;
	unsigned i;
	int r;

	ib = p->ib->ptr;
	ib_chunk = &p->chunks[p->chunk_ib_idx];
	track = (struct r300_cs_track *)p->track;
	switch (reg) {
	case RADEON_DST_PITCH_OFFSET:
	case RADEON_SRC_PITCH_OFFSET:
		r = r100_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					idx, reg);
			r100_cs_dump_packet(p, pkt);
			return r;
		}
		tmp = ib_chunk->kdata[idx] & 0x003fffff;
		tmp += (((u32)reloc->lobj.gpu_offset) >> 10);
		ib[idx] = (ib_chunk->kdata[idx] & 0xffc00000) | tmp;
		break;
	case R300_RB3D_COLOROFFSET0:
	case R300_RB3D_COLOROFFSET1:
	case R300_RB3D_COLOROFFSET2:
	case R300_RB3D_COLOROFFSET3:
		i = (reg - R300_RB3D_COLOROFFSET0) >> 2;
		r = r100_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					idx, reg);
			r100_cs_dump_packet(p, pkt);
			return r;
		}
		track->cb[i].robj = reloc->robj;
		track->cb[i].offset = ib_chunk->kdata[idx];
		ib[idx] = ib_chunk->kdata[idx] + ((u32)reloc->lobj.gpu_offset);
		break;
	case R300_ZB_DEPTHOFFSET:
		r = r100_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					idx, reg);
			r100_cs_dump_packet(p, pkt);
			return r;
		}
		track->zb.robj = reloc->robj;
		track->zb.offset = ib_chunk->kdata[idx];
		ib[idx] = ib_chunk->kdata[idx] + ((u32)reloc->lobj.gpu_offset);
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
		r = r100_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
					idx, reg);
			r100_cs_dump_packet(p, pkt);
			return r;
		}
		ib[idx] = ib_chunk->kdata[idx] + ((u32)reloc->lobj.gpu_offset);
		break;
	/* Tracked registers */
	case 0x43E4:
		/* SC_SCISSOR1 */

		track->maxy = ((ib_chunk->kdata[idx] >> 13) & 0x1FFF) + 1;
		if (p->rdev->family < CHIP_RV515) {
			track->maxy -= 1440;
		}
		break;
	case 0x4E00:
		/* RB3D_CCTL */
		track->num_cb = ((ib_chunk->kdata[idx] >> 5) & 0x3) + 1;
		break;
	case 0x4E38:
	case 0x4E3C:
	case 0x4E40:
	case 0x4E44:
		/* RB3D_COLORPITCH0 */
		/* RB3D_COLORPITCH1 */
		/* RB3D_COLORPITCH2 */
		/* RB3D_COLORPITCH3 */
		i = (reg - 0x4E38) >> 2;
		track->cb[i].pitch = ib_chunk->kdata[idx] & 0x3FFE;
		switch (((ib_chunk->kdata[idx] >> 21) & 0xF)) {
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
				  ((ib_chunk->kdata[idx] >> 21) & 0xF));
			return -EINVAL;
		}
		break;
	case 0x4F00:
		/* ZB_CNTL */
		if (ib_chunk->kdata[idx] & 2) {
			track->z_enabled = true;
		} else {
			track->z_enabled = false;
		}
		break;
	case 0x4F10:
		/* ZB_FORMAT */
		switch ((ib_chunk->kdata[idx] & 0xF)) {
		case 0:
		case 1:
			track->zb.cpp = 2;
			break;
		case 2:
			track->zb.cpp = 4;
			break;
		default:
			DRM_ERROR("Invalid z buffer format (%d) !\n",
				  (ib_chunk->kdata[idx] & 0xF));
			return -EINVAL;
		}
		break;
	case 0x4F24:
		/* ZB_DEPTHPITCH */
		track->zb.pitch = ib_chunk->kdata[idx] & 0x3FFC;
		break;
	default:
		printk(KERN_ERR "Forbidden register 0x%04X in cs at %d\n", reg, idx);
		return -EINVAL;
	}
	return 0;
}

static int r300_packet3_check(struct radeon_cs_parser *p,
			      struct radeon_cs_packet *pkt)
{
	struct radeon_cs_chunk *ib_chunk;
	struct radeon_cs_reloc *reloc;
	struct r300_cs_track *track;
	volatile uint32_t *ib;
	unsigned idx;
	unsigned i, c;
	int r;

	ib = p->ib->ptr;
	ib_chunk = &p->chunks[p->chunk_ib_idx];
	idx = pkt->idx + 1;
	track = (struct r300_cs_track *)p->track;
	switch (pkt->opcode) {
	case PACKET3_3D_LOAD_VBPNTR:
		c = ib_chunk->kdata[idx++];
		for (i = 0; i < (c - 1); i += 2, idx += 3) {
			r = r100_cs_packet_next_reloc(p, &reloc);
			if (r) {
				DRM_ERROR("No reloc for packet3 %d\n",
					  pkt->opcode);
				r100_cs_dump_packet(p, pkt);
				return r;
			}
			ib[idx+1] = ib_chunk->kdata[idx+1] + ((u32)reloc->lobj.gpu_offset);
			r = r100_cs_packet_next_reloc(p, &reloc);
			if (r) {
				DRM_ERROR("No reloc for packet3 %d\n",
					  pkt->opcode);
				r100_cs_dump_packet(p, pkt);
				return r;
			}
			ib[idx+2] = ib_chunk->kdata[idx+2] + ((u32)reloc->lobj.gpu_offset);
		}
		if (c & 1) {
			r = r100_cs_packet_next_reloc(p, &reloc);
			if (r) {
				DRM_ERROR("No reloc for packet3 %d\n",
					  pkt->opcode);
				r100_cs_dump_packet(p, pkt);
				return r;
			}
			ib[idx+1] = ib_chunk->kdata[idx+1] + ((u32)reloc->lobj.gpu_offset);
		}
		break;
	case PACKET3_INDX_BUFFER:
		r = r100_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("No reloc for packet3 %d\n", pkt->opcode);
			r100_cs_dump_packet(p, pkt);
			return r;
		}
		ib[idx+1] = ib_chunk->kdata[idx+1] + ((u32)reloc->lobj.gpu_offset);
		break;
	/* Draw packet */
	case PACKET3_3D_DRAW_VBUF:
	case PACKET3_3D_DRAW_IMMD:
	case PACKET3_3D_DRAW_INDX:
	case PACKET3_3D_DRAW_VBUF_2:
	case PACKET3_3D_DRAW_IMMD_2:
	case PACKET3_3D_DRAW_INDX_2:
		r = r300_cs_track_check(p->rdev, track);
		if (r) {
			return r;
		}
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
	struct r300_cs_track track;
	int r;

	r300_cs_track_clear(&track);
	p->track = &track;
	do {
		r = r100_cs_packet_parse(p, &pkt, p->idx);
		if (r) {
			return r;
		}
		p->idx += pkt.count + 2;
		switch (pkt.type) {
		case PACKET_TYPE0:
			r = r100_cs_parse_packet0(p, &pkt,
						  r300_auth_reg,
						  ARRAY_SIZE(r300_auth_reg),
						  &r300_packet0_check);
			break;
		case PACKET_TYPE2:
			break;
		case PACKET_TYPE3:
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
